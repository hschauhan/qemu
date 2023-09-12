/*
 * RISC-V RAS (Reliability, Availability and Serviceability) block
 *
 * Copyright (c) 2023 Rivos Inc
 * Copyright (c) 2023 Ventana Micro Systems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

#ifndef HW_MISC_RISCV_RAS_H
#define HW_MISC_RISCV_RAS_H

#include "qom/object.h"
#include "riscv_ras_reference.h"

#define TYPE_RISCV_RAS "riscv_ras"

DeviceState *riscv_ras_create(hwaddr);

int riscv_ras_inject(void *opaque, int record, hwaddr addr, uint64_t info);
int riscv_ras_get_component_errors(RiscvRaSComponentId *comp,
                                   RiscvRasErrorRecord *comp_recs);

#endif
