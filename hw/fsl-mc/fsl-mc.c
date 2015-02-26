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

#include "fsl-mc.h"
#include "dprc.h"

#define MC_ROOT_CONTAINER_ID 0x123

static uint64_t fsl_mc_control_read(void *opaque, hwaddr addr,
                                   unsigned size)
{
    uint32_t value = 0;

    switch (addr) {
    default:
        fprintf(stderr, "fsl-mc-control: Unknown register read: %x\n", (int)addr);
        break;
    }

    return value;
}

static void fsl_mc_control_write(void *opaque, hwaddr addr,
                                uint64_t value, unsigned size)
{
    switch (addr) {
    default:
        fprintf(stderr, "fsl-mc-control: Unknown register write: %x = %x\n",
                (int)addr, (unsigned)value);
        break;
    }
}

static const MemoryRegionOps fsl_mc_control_ops = {
    .read = fsl_mc_control_read,
    .write = fsl_mc_control_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
#if 0
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
#endif
};

static uint64_t fsl_mc_portal_read(void *opaque, hwaddr addr,
                                   unsigned size)
{
    FslMcHostState *s = opaque;
    uint64_t value;

    value = s->root->portal[addr / 8];
    if (addr & 4) {
        value >>= 32;
    }

    return value;
}

static void fsl_mc_get_version(FslMcHostState *s)
{
    uint64_t major = 3;
    uint64_t minor = 0;
    uint64_t rev = 0;

    s->root->portal[0] = MC_CMD_STATUS_OK << 16;
    s->root->portal[1] = (major << 32) | minor;
    s->root->portal[2] = rev;
}

struct FslMcDPRCState *fsl_mc_find_dprc(FslMcBusState *bus, int token)
{
    BusChild *kid;

    QTAILQ_FOREACH(kid, &bus->qbus.children, sibling) {
        Object *o = OBJECT(kid->child);
        void *ddev = object_dynamic_cast(o, TYPE_FSL_MC_DPRC);
        FslMcDPRCState *dprc = (FslMcDPRCState *)ddev;
        if (ddev) {
            FslMcDPRCState *r;

            if (dprc->token == token) {
                return dprc;
            }

            r = fsl_mc_find_dprc(&dprc->bus, token);
            if (r) {
                return r;
            }
        }
    }

    return NULL;
}

static void fsl_mc_run_cmd(FslMcHostState *s)
{
    uint64_t cmd_enc = s->root->portal[0];
    int cmd = (cmd_enc >> 52) & 0xfff;
    int auth = (cmd_enc >> 38) & 0x3ff;
    int pri = (cmd_enc >> 15) & 0x1;
    int status = (cmd_enc >> 16) & 0xff;

    printf("fsl-mc: Run cmd=%#x auth=%#x pri=%d status=%#x\n",
           cmd, auth, pri, status);

    switch (cmd) {
    case DPMNG_CMDID_GET_VERSION:
        fsl_mc_get_version(s);
        break;
    default:
        dprc_run_cmd(s->root, s->root->portal);
        break;
    }
}

static void fsl_mc_portal_write(void *opaque, hwaddr addr,
                                uint64_t value, unsigned size)
{
    FslMcHostState *s = opaque;

    if (size == 4) {
        if (addr & 4) {
            value <<= 32;
            value |= (uint32_t)s->root->portal[addr / 8];
        } else {
            value |= s->root->portal[addr / 8] << 32;
        }
    }
    s->root->portal[addr / 8] = value;

    if ((size == 8 && !addr) || (addr == 0x4)) {
        fsl_mc_run_cmd(s);
    }
}

static const MemoryRegionOps fsl_mc_portal_ops = {
    .read = fsl_mc_portal_read,
    .write = fsl_mc_portal_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static void fsl_mc_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->bus_type = TYPE_FSL_MC_BUS;
}

static const TypeInfo fsl_mc_device_info = {
    .name          = TYPE_FSL_MC_DEVICE,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(FslMcDeviceState),
    .class_init    = fsl_mc_device_class_init,
};

static uint32_t fsl_mc_alloc_token(FslMcBusState *child_bus)
{
    DeviceState *ds = DEVICE(BUS(child_bus)->parent);
    FslMcHostState *s = FSL_MC_HOST(ds);

    return s->token_pool++;
}

static void fsl_mc_host_initfn(Object *obj)
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    FslMcHostState *s = FSL_MC_HOST(obj);
    DeviceState *ds = DEVICE(obj);
    DeviceState *dprc;

    memory_region_init_io(&s->io_portal, OBJECT(s), &fsl_mc_portal_ops, s,
                          "fsl_mc portal", 0x40);
    sysbus_init_mmio(d, &s->io_portal);

    memory_region_init_io(&s->io_control, OBJECT(s), &fsl_mc_control_ops, s,
                          "fsl_mc control", 0x40000);
    sysbus_init_mmio(d, &s->io_control);

    qbus_create_inplace(&s->bus, sizeof(s->bus), TYPE_FSL_MC_BUS, ds, NULL);
    s->bus.alloc_token = fsl_mc_alloc_token;
    s->token_pool = 0x10;

    /* The MC always comes with a root container already populated */
    dprc = qdev_create(&s->bus.qbus, TYPE_FSL_MC_DPRC);
    qdev_prop_set_uint32(dprc, "token", MC_ROOT_CONTAINER_ID);
    qdev_init_nofail(dprc);
    s->root = FSL_MC_DPRC(dprc);
}

static const TypeInfo fsl_mc_host_info = {
    .name          = TYPE_FSL_MC_HOST,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(FslMcHostState),
    .instance_init = fsl_mc_host_initfn,
};

#if 0
static void fsl_mc_bus_class_init(ObjectClass *klass, void *data)
{
    BusClass *k = BUS_CLASS(klass);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(klass);

    k->print_dev = fsl_mc_bus_dev_print;
    k->get_dev_path = fsl_mc_get_dev_path;
    k->get_fw_dev_path = fsl_mc_get_fw_dev_path;
    hc->unplug = qdev_simple_device_unplug_cb;
}
#endif

static const TypeInfo fsl_mc_bus_info = {
    .name = TYPE_FSL_MC_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(FslMcBusState),
#if 0
    .class_init = fsl_mc_bus_class_init,
#endif
    .interfaces = (InterfaceInfo[]) {
        { TYPE_HOTPLUG_HANDLER },
        { }
    }
};

static void fsl_mc_register_types(void)
{
    type_register_static(&fsl_mc_bus_info);
    type_register_static(&fsl_mc_host_info);
    type_register_static(&fsl_mc_device_info);
}

type_init(fsl_mc_register_types)
