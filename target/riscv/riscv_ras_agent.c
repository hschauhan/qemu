/*
 * RISC-V RAS (Reliability, Availability and Serviceability)
 *
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
#include "hw/acpi/ghes.h"

#ifndef MAX_HARTS
#define MAX_HARTS             128
#endif

typedef struct RasErrorSource {
        unsigned int sse_vector;
        RiscvRaSComponentId r_comp;
} RasErrorSource;

static int nr_components;
static RasErrorSource *ras_err_srcs = NULL;
static int init_done = 0;
static int last_sse_vector = 0;

static int allocate_sse_vector(void)
{
        int svec = last_sse_vector;
        last_sse_vector++;
        return svec;
}

int ras_get_agent_version(void)
{
    return RAS_AGENT_VERSION;
}

int riscv_ras_agent_synchronize_errors(int hart_id, struct rpmi_ras_sync_err_resp *resp)
{
    int i, rc;
    RasErrorSource *err_src;
    RiscvRaSComponentId *comp;
    RiscvRasErrorRecord erec;

    if (!init_done) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: RAS agent not initialized.\n", __func__);
            resp->status = -1;
            return -1;
    }

    qemu_log_mask(LOG_GUEST_ERROR, "%s: Hart: %d NR Components: %d\n", __func__, hart_id, nr_components);

    for (i = 0; i < nr_components; i++) {
        err_src = ras_err_srcs + i;
        comp = &err_src->r_comp;
        rc = riscv_ras_get_component_errors(comp, &erec);
        if (rc < 0) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Couldn't get details on error "
                          "source: %d\n", __func__, i);
            return rc;
        }

        qemu_log_mask(LOG_GUEST_ERROR, "%s: Status: 0x%llx\n", __func__, (unsigned long long)erec.status_i.value);

        /* Error is valid process it */
        if (erec.status_i.v == 1) {
                qemu_log_mask(LOG_GUEST_ERROR, "%s: Component: %d has valid error at address 0x%llx\n",
                              __func__, i, (unsigned long long)erec.addr_i);
                /* Update the CPER record */
                rc = acpi_ghes_record_errors(0, erec.addr_i);
                resp->returned = 1;
                resp->remaining = 0;
                resp->status = 0;
                resp->pending_vecs[0] = 0;
        }
    }

    return 0;
}

int riscv_ras_agent_init(RiscvRaSComponentId *comps, int nr_comps)
{
    int i;
    RasErrorSource *r_dst;
    RiscvRaSComponentId *r_comp;

    qemu_log_mask(LOG_GUEST_ERROR, "%s: There are %d error components\n", __func__, nr_comps);

    ras_err_srcs = (RasErrorSource *)malloc(nr_comps * sizeof(RasErrorSource));
    memset(ras_err_srcs, 0, nr_comps * sizeof(RasErrorSource));

    for (i = 0; i < nr_comps; i++) {
        r_dst = &ras_err_srcs[i];
        r_dst->sse_vector = allocate_sse_vector();
        r_comp = &r_dst->r_comp;
        qemu_log_mask(LOG_GUEST_ERROR, "%s: SSE vector 0x%x allocated for "
                      "component %d\n", __func__, r_dst->sse_vector, i);
        memcpy(r_comp, &comps[i], sizeof(RiscvRaSComponentId));
    }

    nr_components = nr_comps;

    init_done = 1;

    return 0;
}
