/*
 * QEMU LS2xxxx compatible FSL Management Complex driver
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

#if !defined(FSL_MC_FSL_MC_H)
#define FSL_MC_FSL_MC_H

#include "hw/hw.h"
#include "sysemu/sysemu.h"
#include "hw/sysbus.h"

struct FslMcDPRCState;
struct FslMcBusState;

#define TYPE_FSL_MC_BUS "fsl-mc-bus"
#define FSL_MC_BUS(obj) OBJECT_CHECK(FslMcBusState, (obj), TYPE_FSL_MC_BUS)

typedef uint32_t (*fsl_mc_token_alloc_fn)(struct FslMcBusState *);

struct FslMcBusState {
    BusState qbus;
    fsl_mc_token_alloc_fn alloc_token;
};
typedef struct FslMcBusState FslMcBusState;

#define TYPE_FSL_MC_HOST "fsl-mc-host"
#define FSL_MC_HOST(obj) OBJECT_CHECK(FslMcHostState, (obj), TYPE_FSL_MC_HOST)


struct FslMcHostState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/
    MemoryRegion io_portal;
    MemoryRegion io_control;
    FslMcBusState bus;
    struct FslMcDPRCState *root;
    int token_pool;
};
typedef struct FslMcHostState FslMcHostState;

#define TYPE_FSL_MC_DEVICE "fsl-mc-device"
#define FSL_MC_DEVICE(obj) OBJECT_CHECK(FslMcDeviceState, (obj), TYPE_FSL_MC_DEVICE)

struct FslMcDeviceState {
    /*< private >*/
    DeviceState parent_obj;
    /*< public >*/
    uint32_t id;
    uint16_t vendor;
    uint8_t irq_count;
    uint8_t region_count;
    uint32_t state;
    uint16_t ver_major;
    uint16_t ver_minor;
    char type[16];
};
typedef struct FslMcDeviceState FslMcDeviceState;

enum mc_cmd_status {
    MC_CMD_STATUS_OK = 0x0,             /* Completed successfully */
    MC_CMD_STATUS_READY = 0x1,          /* Ready to be processed */
    MC_CMD_STATUS_AUTH_ERR = 0x3,       /* Authentication error */
    MC_CMD_STATUS_NO_PRIVILEGE = 0x4,   /* No privilege */
    MC_CMD_STATUS_DMA_ERR = 0x5,        /* DMA or I/O error */
    MC_CMD_STATUS_CONFIG_ERR = 0x6,     /* Configuration error */
    MC_CMD_STATUS_TIMEOUT = 0x7,        /* Operation timed out */
    MC_CMD_STATUS_NO_RESOURCE = 0x8,    /* No resources */
    MC_CMD_STATUS_NO_MEMORY = 0x9,      /* No memory available */
    MC_CMD_STATUS_BUSY = 0xA,           /* Device is busy */
    MC_CMD_STATUS_UNSUPPORTED_OP = 0xB, /* Unsupported operation */
    MC_CMD_STATUS_INVALID_STATE = 0xC   /* Invalid state */
};

/* Command IDs */
#define DPMNG_CMDID_GET_VERSION                 0x831
#define DPMNG_CMDID_RESET_AIOP                  0x832
#define DPMNG_CMDID_LOAD_AIOP                   0x833
#define DPMNG_CMDID_RUN_AIOP                    0x834
#define DPMNG_CMDID_RESET_MC_PORTAL             0x835

struct FslMcDPRCState *fsl_mc_find_dprc(FslMcBusState *dprc, int token);

#endif /* !defined(FSL_MC_FSL_MC_H) */
