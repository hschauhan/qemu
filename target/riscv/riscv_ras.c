/*
 * RISC-V RAS (Reliability, Availability and Serviceability) block
 *
 * Copyright (c) 2022 Rivos Inc
 * Copyright (c) 2023 Ventana Micro
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/coroutine.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "target/riscv/cpu.h"
#include "hw/core/cpu.h"
#include "cpu-qom.h"
#include "hw/irq.h"
#include "riscv_ras.h"
#include "riscv_ras_reference.h"
#include "riscv_ras_agent.h"

typedef struct RiscvRasState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public>*/
    qemu_irq irqs[2];
    Coroutine *co;
    QemuCoSleep w;
    QemuMutex lock;
    MemoryRegion iomem;
    RiscvRasComponentRegisters *regs;
} RiscvRasState;

enum {
    IRQ_HIGH_PRIORITY = 0,
    IRQ_LOW_PRIORITY = 1
};

DECLARE_INSTANCE_CHECKER(RiscvRasState, RISCV_RAS, TYPE_RISCV_RAS)

static void coroutine_fn riscv_error_inject(void *opaque);
static RiscvRasState *g_ras_state = NULL;

int riscv_ras_inject(void *opaque, int record, vaddr addr, uint64_t info)
{
    RiscvRasState *s = opaque;
    RiscvRasStatus sts;
    int rc;

    if (record >= RECORD_NUM)
        return EINVAL;

    sts.value = 0;
    sts.ue = 1;
    sts.at = 3;
    sts.tt = 5;
    sts.iv = 1;

    qemu_mutex_lock(&s->lock);
    rc = riscv_ras_do_inject(&s->regs->records[record], sts, addr, info);
    qemu_mutex_unlock(&s->lock);
    return rc;
}

static MemTxResult riscv_ras_read_impl(void *opaque, hwaddr addr,
                                        uint64_t *data, unsigned size,
                                        MemTxAttrs attrs)
{
    RiscvRasState *s = opaque;
    int error;

    if (attrs.user) {
        return MEMTX_ERROR;
    }

    qemu_mutex_lock(&s->lock);
    error = riscv_ras_read(s->regs, addr, data, size);
    qemu_mutex_unlock(&s->lock);
    if (error != 0) {
        return MEMTX_ERROR;
    }

    return MEMTX_OK;
}

int riscv_ras_get_component_errors(RiscvRaSComponentId *comp,
                                   RiscvRasErrorRecord *comp_recs)
{
    RiscvRasState *s = g_ras_state;
    int error;
    RiscvRaSComponentId cid;
    RiscvRasErrorRecord erec;

    qemu_mutex_lock(&s->lock);
    /*  Match the component ID
     *  TODO: Need to add multiple components and then check against
     *  each one of them.
     */ 
    error = riscv_ras_read(s->regs, offsetof(RiscvRasComponentRegisters, component_id),
                           &cid.value, sizeof(cid));
    if (error)
        goto out;

    if (cid.inst_id != comp->inst_id) {
        error = -1;
        goto out;
    }

    /* Read the error record
     * TODO: Since only one record is implemented right now,
     * read first. Later need to loop over each one of them.
     */
    error = riscv_ras_read_error_record(s->regs, 0, &erec);
    if (error)
        goto out;

    qemu_log_mask(LOG_GUEST_ERROR, "%s: Status: 0x%llx (Valid? %s)\n",
                  __func__, (unsigned long long)erec.status_i.value,
                  (erec.status_i.v ? "YES" : "NO"));

    /* if valid error, report it */
    if (!erec.status_i.v) {
        memcpy(comp_recs, &erec, sizeof(erec));
        comp_recs->status_i.v = 1;
        comp_recs->addr_i = 0x4000000;
        error = 0;
    } else
        error = -2;

out:
    qemu_mutex_unlock(&s->lock);
    return error;
}

static MemTxResult riscv_ras_write_impl(void *opaque, hwaddr addr,
                                      uint64_t val64, unsigned int size,
                                      MemTxAttrs attrs)
{
    RiscvRasState *s = opaque;
    bool inject = false; bool clrsts = false;
    int error;
    CPUState *cpu = cpu_by_arch_id(0);

    if (attrs.user) {
        return MEMTX_ERROR;
    }

    qemu_mutex_lock(&s->lock);
    error = riscv_ras_write(s->regs, addr, val64, size, &inject, &clrsts);
    qemu_mutex_unlock(&s->lock);
    if (error != 0) {
        return MEMTX_ERROR;
    }

    if (inject) {
        if (s->w.to_wake != NULL) {
            qemu_co_sleep_wake(&s->w);
        } else {
            qemu_coroutine_enter_if_inactive(s->co);
        }
    }
    if (clrsts)
        riscv_cpu_update_mip(cpu->env_ptr, MIP_RASHIP, ~(MIP_RASHIP));

    return MEMTX_OK;
}

static void coroutine_fn riscv_error_inject(void *opaque)
{
    RiscvRasState *s = opaque;
    RiscvRasErrorRecord *record;

    CPUState *cpu = cpu_by_arch_id(0);
    int rc;

    record = &s->regs->records[0];

    qemu_mutex_lock(&s->lock);
    rc = riscv_error_injection_tick(record);
    while (rc == RISCV_RAS_INJECT_WAIT) {
        qemu_mutex_unlock(&s->lock);
        qemu_co_sleep_ns_wakeable(&s->w, QEMU_CLOCK_VIRTUAL, 1000000);
        qemu_mutex_lock(&s->lock);
        rc = riscv_error_injection_tick(record);
    }
    qemu_mutex_unlock(&s->lock);

    if (rc == RISCV_RAS_INJECT_LOW) {
        qemu_irq_raise(s->irqs[IRQ_LOW_PRIORITY]);
    } else if (rc == RISCV_RAS_INJECT_HIGH) {
        riscv_cpu_update_mip(cpu->env_ptr, MIP_RASHIP, (MIP_RASHIP));
    }
}

static const MemoryRegionOps riscv_ras_ops = {
    .read_with_attrs = riscv_ras_read_impl,
    .write_with_attrs = riscv_ras_write_impl,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8
    }
};

static void riscv_ras_instance_init(Object *obj)
{
    RiscvRasState *s;
    s = g_ras_state = RISCV_RAS(obj);
    fprintf(stderr, "%s\n", __func__);
    RISCVCPU *cpu = RISCV_CPU(cpu_by_arch_id(0));
    RiscvRaSComponentId cid[1];

    s->regs = (RiscvRasComponentRegisters *)malloc(sizeof(RiscvRasComponentRegisters));
    memset(s->regs, 0, sizeof(RiscvRasComponentRegisters));

    riscv_ras_init(s->regs, 0x1af4, 0xabcd);

    riscv_ras_read(s->regs, offsetof(RiscvRasComponentRegisters, component_id),
                   &cid[0].value, sizeof(cid));
    riscv_ras_agent_init(&cid[0], 1);

    memory_region_init_io(&s->iomem, obj, &riscv_ras_ops,
                        s, TYPE_RISCV_RAS, 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irqs[IRQ_HIGH_PRIORITY]);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irqs[IRQ_LOW_PRIORITY]);

    if (riscv_cpu_claim_interrupts(cpu, MIP_RASHIP) < 0) {
        fprintf(stderr, "%s: Already claimed MIP_RASHIP\n", __func__);
        exit(1);
    }
    s->co = qemu_coroutine_create(riscv_error_inject, s);
    qemu_mutex_init(&s->lock);
}

static const TypeInfo riscv_ras_info = {
    .name = TYPE_RISCV_RAS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RiscvRasState),
    .instance_init = riscv_ras_instance_init,
};

static void riscv_ras_register_types(void)
{
    type_register_static(&riscv_ras_info);
}

type_init(riscv_ras_register_types);

DeviceState *riscv_ras_create(hwaddr addr)
{
    DeviceState *dev = qdev_new(TYPE_RISCV_RAS);
    SysBusDevice *s = SYS_BUS_DEVICE(dev);

    sysbus_mmio_map(s, 0, addr);
    dev->id = g_strdup(TYPE_RISCV_RAS);

    sysbus_realize_and_unref(s, &error_fatal);

    return dev;
}
