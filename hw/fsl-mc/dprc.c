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

#include "dprc.h"

static uint64_t dprc_portal_read(void *opaque, hwaddr addr,
                                 unsigned size)
{
    FslMcDPRCState *s = opaque;
    uint64_t value;

    value = s->portal[addr / 8];
    if (addr & 4) {
        value >>= 32;
    }

    return value;
}

static FslMcDPRCState *dprc_find_dprc(FslMcDPRCState *s, int token)
{
    FslMcDPRCState *dprc = NULL;

    if (s->token == token) {
        dprc = s;
    } else {
        dprc = fsl_mc_find_dprc(&s->bus, token);
    }

    return dprc;
}

static void dprc_open(FslMcDPRCState *s, uint64_t *portal)
{
    uint64_t token = portal[1] & 0x3ff;
    FslMcDPRCState *dprc = dprc_find_dprc(s, token);

    if (dprc) {
        portal[0] = (MC_CMD_STATUS_OK << 16) | (token << 38);
    } else {
        portal[0] = MC_CMD_STATUS_NO_PRIVILEGE << 16;
    }
}

static void dprc_get_attr(FslMcDPRCState *s, uint64_t *portal)
{
    int auth = (portal[0] >> 38) & 0x3ff;
    uint64_t major = 3;
    uint64_t minor = 0;
    FslMcDPRCState *dprc = dprc_find_dprc(s, auth);

    if (!dprc) {
        portal[0] = MC_CMD_STATUS_AUTH_ERR << 16;
        return;
    }

    portal[0] = (MC_CMD_STATUS_OK << 16);
    portal[1] = s->token | ((uint64_t)s->icid << 32);
    portal[2] = s->options | ((uint64_t)s->portal_id << 32);
    portal[3] = major | (minor << 16);
}

static void dprc_get_obj_count(FslMcDPRCState *s, uint64_t *portal)
{
    int auth = (portal[0] >> 38) & 0x3ff;
    uint64_t count = 0;
    FslMcDPRCState *dprc = dprc_find_dprc(s, auth);
    BusChild *kid;

    if (!dprc) {
        portal[0] = MC_CMD_STATUS_AUTH_ERR << 16;
        return;
    }

    QTAILQ_FOREACH(kid, &dprc->bus.qbus.children, sibling) {
        count++;
    }

    portal[0] = (MC_CMD_STATUS_OK << 16);
    portal[1] = count << 32;
}

static void dprc_get_obj(FslMcDPRCState *s, uint64_t *portal)
{
    int auth = (portal[0] >> 38) & 0x3ff;
    uint64_t count = 0;
    uint32_t index = portal[1];
    FslMcDPRCState *dprc = dprc_find_dprc(s, auth);
    BusChild *kid;
    FslMcDeviceState *dev = NULL;

    if (!dprc) {
        portal[0] = MC_CMD_STATUS_AUTH_ERR << 16;
        return;
    }

    QTAILQ_FOREACH(kid, &dprc->bus.qbus.children, sibling) {
        if (count++ == index) {
            dev = FSL_MC_DEVICE(kid->child);
            break;
        }
    }

    if (!dev) {
        portal[0] = MC_CMD_STATUS_AUTH_ERR << 16;
        return;
    }

    portal[0] = (MC_CMD_STATUS_OK << 16);
    portal[1] = dev->id;
    portal[2] = dev->vendor |
                (((uint64_t)dev->irq_count) << 16) |
                (((uint64_t)dev->region_count) << 24) |
                (((uint64_t)dev->state) << 32);
    portal[3] = dev->ver_major |
                (((uint64_t)dev->ver_minor) << 16);
    portal[4] = ldq_le_p(dev->type);
    portal[5] = ldq_le_p(&dev->type[8]);
}

void dprc_run_cmd(FslMcDPRCState *s, uint64_t *portal)
{
    uint64_t cmd_enc = portal[0];
    int cmd = (cmd_enc >> 52) & 0xfff;
    int auth = (cmd_enc >> 38) & 0x3ff;
    int pri = (cmd_enc >> 15) & 0x1;
    int status = (cmd_enc >> 16) & 0xff;
    int i;

    printf("dprc %x: Run cmd=%#x auth=%#x pri=%d status=%#x\n",
           s->token, cmd, auth, pri, status);
    printf("Portal dump:\n\n");
    for (i = 0; i < 8; i++) {
        printf("  %d: %016lx\n", i, portal[i]);
    }
    printf("\n");

    switch (cmd) {
    case DPRC_CMDID_OPEN:
        dprc_open(s, portal);
        break;
    case DPRC_CMDID_CLOSE:
        portal[0] = MC_CMD_STATUS_OK << 16;
        break;
    case DPRC_CMDID_GET_CONT_ID:
        portal[0] = MC_CMD_STATUS_OK << 16;
        portal[1] = s->token;
        break;
    case DPRC_CMDID_GET_ATTR:
        dprc_get_attr(s, portal);
        break;
    case DPRC_CMDID_GET_OBJ_COUNT:
        dprc_get_obj_count(s, portal);
        break;
    case DPRC_CMDID_GET_OBJ:
        dprc_get_obj(s, portal);
        break;
    default:
        portal[0] = MC_CMD_STATUS_UNSUPPORTED_OP << 16;
        break;
    }

    printf("dprc %x: cmd result: %x\n", s->token, (int)(portal[0] >> 16) & 0xff);
}

static void dprc_portal_write(void *opaque, hwaddr addr,
                              uint64_t value, unsigned size)
{
    FslMcDPRCState *s = opaque;

    if (size == 4) {
        if (addr & 4) {
            value <<= 32;
            value |= (uint32_t)s->portal[addr / 8];
        } else {
            value |= s->portal[addr / 8] << 32;
        }
    }
    s->portal[addr / 8] = value;

    if ((size == 8 && !addr) || (addr == 0x4)) {
        dprc_run_cmd(s, s->portal);
    }
}

static const MemoryRegionOps dprc_portal_ops = {
    .read = dprc_portal_read,
    .write = dprc_portal_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static uint32_t dprc_alloc_token(FslMcBusState *child_bus)
{
    FslMcDPRCState *s = FSL_MC_DPRC(child_bus->qbus.parent);
    DeviceState *ds = DEVICE(s);
    BusState *parent_bus = ds->parent_bus;
    FslMcBusState *mc_bus = FSL_MC_BUS(parent_bus);

    return mc_bus->alloc_token(mc_bus);
}

static void dprc_initfn(Object *obj)
{
    FslMcDPRCState *s = FSL_MC_DPRC(obj);
    FslMcDeviceState *md = FSL_MC_DEVICE(s);
    DeviceState *ds = DEVICE(obj);

    memory_region_init_io(&s->io_portal, OBJECT(s), &dprc_portal_ops, s,
                          "dprc portal", 0x40);
    // XXX expose to parent

    qbus_create_inplace(&s->bus, sizeof(s->bus), TYPE_FSL_MC_BUS, ds, NULL);
    s->bus.alloc_token = dprc_alloc_token;
    sprintf(md->type, "dprc");
    md->region_count = 1;
}

static void dprc_realize(DeviceState *dev, Error **errp)
{
    FslMcDPRCState *s = FSL_MC_DPRC(dev);

    if (s->token > 0x3ff) {
        s->token = dprc_alloc_token(&s->bus);
    }
}

static Property dprc_properties[] = {
    DEFINE_PROP_UINT32("token", FslMcDPRCState, token, UINT32_MAX),
    DEFINE_PROP_END_OF_LIST(),
};

static void dprc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->props = dprc_properties;
    dc->realize = dprc_realize;
}

static const TypeInfo dprc_info = {
    .name          = TYPE_FSL_MC_DPRC,
    .parent        = TYPE_FSL_MC_DEVICE,
    .instance_size = sizeof(FslMcDPRCState),
    .instance_init = dprc_initfn,
    .class_init    = dprc_class_init,
};

static void dprc_register_types(void)
{
    type_register_static(&dprc_info);
}

type_init(dprc_register_types)
