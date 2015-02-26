/*
 * FSL-MC Resource Container Object
 *
 * Copyright (C) 2014 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: Alexander Graf, <alex@csgraf.de>
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of  the GNU General  Public License as published by
 * the Free Software Foundation;  either version 2 of the  License, or
 * (at your option) any later version.
 *
 * *****************************************************************
 *
 * The FSL Management Complex is basically a simple bus controller that
 * manages a virtual bus view of the DPAA hardware block. All of its
 * subdevices are partitioned pieces of real hardware blocks on actual
 * systems.
 */

#if !defined(FSL_MC_DPRC_H)
#define FSL_MC_DPRC_H

#include "fsl-mc.h"

#define TYPE_FSL_MC_DPRC "fsl-mc-dprc"
#define FSL_MC_DPRC(obj) OBJECT_CHECK(FslMcDPRCState, (obj), TYPE_FSL_MC_DPRC)

struct FslMcDPRCState {
    /*< private >*/
    FslMcDeviceState parent_obj;
    /*< public >*/
    MemoryRegion io_portal;
    FslMcBusState bus;
    uint32_t token;
    uint32_t icid;
    uint32_t options;
    uint32_t portal_id;
    uint64_t portal[8];
};

typedef struct FslMcDPRCState FslMcDPRCState;

/* Command IDs */
#define DPRC_CMDID_CLOSE                        0x800
#define DPRC_CMDID_OPEN                         0x805
#define DPRC_CMDID_CREATE                       0x905

#define DPRC_CMDID_GET_ATTR                     0x004
#define DPRC_CMDID_RESET_CONT                   0x005

#define DPRC_CMDID_SET_IRQ                      0x010
#define DPRC_CMDID_GET_IRQ                      0x011
#define DPRC_CMDID_SET_IRQ_ENABLE               0x012
#define DPRC_CMDID_GET_IRQ_ENABLE               0x013
#define DPRC_CMDID_SET_IRQ_MASK                 0x014
#define DPRC_CMDID_GET_IRQ_MASK                 0x015
#define DPRC_CMDID_GET_IRQ_STATUS               0x016
#define DPRC_CMDID_CLEAR_IRQ_STATUS             0x017

#define DPRC_CMDID_CREATE_CONT                  0x151
#define DPRC_CMDID_DESTROY_CONT                 0x152
#define DPRC_CMDID_GET_CONT_ID                  0x830
#define DPRC_CMDID_SET_RES_QUOTA                0x155
#define DPRC_CMDID_GET_RES_QUOTA                0x156
#define DPRC_CMDID_ASSIGN                       0x157
#define DPRC_CMDID_UNASSIGN                     0x158
#define DPRC_CMDID_GET_OBJ_COUNT                0x159
#define DPRC_CMDID_GET_OBJ                      0x15A
#define DPRC_CMDID_GET_RES_COUNT                0x15B
#define DPRC_CMDID_GET_RES_IDS                  0x15C
#define DPRC_CMDID_GET_OBJ_REG                  0x15E

#define DPRC_CMDID_CONNECT                      0x167
#define DPRC_CMDID_DISCONNECT                   0x168
#define DPRC_CMDID_GET_POOL                     0x169
#define DPRC_CMDID_GET_POOL_COUNT               0x16A
#define DPRC_CMDID_GET_PORTAL_PADDR             0x16B

#define DPRC_CMDID_GET_CONNECTION               0x16C

void dprc_run_cmd(FslMcDPRCState *s, uint64_t *portal);

#endif /* !defined(FSL_MC_DPRC_H) */
