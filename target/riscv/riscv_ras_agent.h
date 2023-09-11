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

#include "rpmi_msgprot.h"
#include "riscv_ras_regs.h"

/* FIXME: Encode */
#define RAS_AGENT_VERSION    1

int ras_agent_init(RiscvRasComponentRegisters *regs);

static int ras_get_agent_version(void)
{
    return RAS_AGENT_VERSION;
}

int ras_agent_synchronize_errors(int hart_id, struct rpmi_ras_sync_err_resp *resp);

#endif /* _RISCV_RAS_AGENT_H */
