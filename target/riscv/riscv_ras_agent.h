/*
 * RISC-V RAS (Reliability, Availability and Serviceability)
 *
 * Copyright (c) 2023 Ventana Micro
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

#ifndef _RISCV_RAS_AGENT_H
#define _RISCV_RAS_AGENT_H

#include "hw/misc/riscv_rpmi_transport.h"
#include "riscv_ras_regs.h"

/* FIXME: Encode */
#define RAS_AGENT_VERSION    1

int riscv_ras_agent_init(RiscvRaSComponentId *comps, int nr_comps);
int ras_get_agent_version(void);
int riscv_ras_agent_synchronize_errors(int hart_id, struct rpmi_ras_sync_err_resp *resp);

#endif /* _RISCV_RAS_AGENT_H */
