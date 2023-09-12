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
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "target/riscv/cpu.h"
#include "hw/core/cpu.h"
#include "cpu-qom.h"
#include "hw/irq.h"
#include "riscv_ras.h"
#include "riscv_ras_reference.h"
#include "riscv_ras_agent.h"

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

    for (i = 0; i < nr_components; i++) {
        err_src = ras_err_srcs + i;
        comp = &err_src->r_comp;
        rc = riscv_ras_get_component_errors(comp, &erec);
        if (rc < 0) {
            return rc;
        }
    }

    return 0;
}

int riscv_ras_agent_init(RiscvRaSComponentId *comps, int nr_comps)
{
    int i;
    RasErrorSource *r_src;
    RiscvRaSComponentId *r_comp;

    ras_err_srcs = (RasErrorSource *)malloc(nr_comps * sizeof(RasErrorSource));
    memset(ras_err_srcs, 0, nr_comps * sizeof(RasErrorSource));

    for (i = 0; i < nr_comps; i++) {
        r_src = &ras_err_srcs[i];
        r_comp = &r_src->r_comp;
        memcpy(r_comp, &comps[i], sizeof(RiscvRaSComponentId));
    }

    init_done = 1;

    return 0;
}
