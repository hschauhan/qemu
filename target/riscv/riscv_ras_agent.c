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

#define MAX_HARTS             128

typedef struct RasErrorSource {
        unsigned int sse_vector;
        RiscvRaSComponentId r_comp;
} RasErrorSource;

static int nr_components;
static RasErrorSource *ras_comps = NULL;
static int init_done = 0;

int ras_agent_synchronize_errors(int hart_id, struct rpmi_ras_sync_err_resp *resp)
{
    int i, rc;
    RiscvRaSComponentId *comp;
    RiscvRasErrorRecord erec;

    for (i = 0; i < nr_components; i++) {
        comp = ras_comps + i;
        rc = riscv_ras_get_component_errors(comp, &erec);
        if (rc < 0) {
            return rc;
        }
    }
}

int ras_agent_init(RiscvRaSComponentId *comps, int nr_comps)
{
    int i;
    RasErrorSource *r_src;
    RiscvRaSComponentId *r_comp;

    ras_comps = (RasErrorSrouce *)malloc(nr_comps * sizeof(RasErrorSource));
    memset(ras_comps, 0, nr_comps * sizeof(RasErrorSource));

    for (i = 0; i < nr_comps; i++) {
        r_src = ras_comps[i];
        r_comp = &r_src->r_comp;
        memcpy(r_comp, comps[i], sizeof(RiscvRaSComponentId));
    }

    init_done = 1;
}
