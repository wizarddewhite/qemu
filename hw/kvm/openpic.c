/*
 * KVM in-kernel OpenPIC
 *
 * Copyright 2013 Freescale Semiconductor, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <sys/ioctl.h>
#include "exec/address-spaces.h"
#include "hw/hw.h"
#include "hw/openpic.h"
#include "hw/pci/msi.h"
#include "hw/sysbus.h"
#include "sysemu/kvm.h"
#include "qemu/log.h"

typedef struct KVMOpenPICState {
    SysBusDevice busdev;
    MemoryRegion mem;
    MemoryListener mem_listener;
    hwaddr reg_base;
    uint32_t kern_id;
    uint32_t model;
} KVMOpenPICState;

static void kvm_openpic_set_irq(void *opaque, int n_IRQ, int level)
{
    KVMOpenPICState *opp = opaque;
    struct kvm_device_attr attr;
    uint32_t val32 = level;
    int ret;

    attr.group = KVM_DEV_MPIC_GRP_IRQ_ACTIVE;
    attr.attr = n_IRQ;
    attr.addr = (uint64_t)(long)&val32;

    ret = ioctl(opp->kern_id, KVM_SET_DEVICE_ATTR, &attr);
    if (ret < 0) {
        fprintf(stderr, "%s: %s %llx\n", __func__, strerror(errno), attr.attr);
    }
}

static void kvm_openpic_reset(DeviceState *d)
{
    qemu_log_mask(LOG_UNIMP, "%s: unimplemented\n", __func__);
}

static void kvm_openpic_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned size)
{
    KVMOpenPICState *opp = opaque;
    struct kvm_device_attr attr;
    uint32_t val32 = val;
    int ret;

    attr.group = KVM_DEV_MPIC_GRP_REGISTER;
    attr.attr = addr;
    attr.addr = (uint64_t)(long)&val32;

    ret = ioctl(opp->kern_id, KVM_SET_DEVICE_ATTR, &attr);
    if (ret < 0) {
        qemu_log_mask(LOG_UNIMP, "%s: %s %llx\n", __func__,
                      strerror(errno), attr.attr);
    }
}

static uint64_t kvm_openpic_read(void *opaque, hwaddr addr, unsigned size)
{
    KVMOpenPICState *opp = opaque;
    struct kvm_device_attr attr;
    uint32_t val = 0xdeadbeef;
    int ret;

    attr.group = KVM_DEV_MPIC_GRP_REGISTER;
    attr.attr = addr;
    attr.addr = (uint64_t)(long)&val;

    ret = ioctl(opp->kern_id, KVM_GET_DEVICE_ATTR, &attr);
    if (ret < 0) {
        qemu_log_mask(LOG_UNIMP, "%s: %s %llx\n", __func__,
                      strerror(errno), attr.attr);
        return 0;
    }

    return val;
}

static const MemoryRegionOps kvm_openpic_mem_ops = {
    .write = kvm_openpic_write,
    .read  = kvm_openpic_read,
    .endianness = DEVICE_BIG_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void kvm_openpic_update_reg_base(MemoryListener *listener)
{
    KVMOpenPICState *opp = container_of(listener, KVMOpenPICState,
                                        mem_listener);
    struct kvm_device_attr attr;
    uint64_t reg_base;
    AddressSpace *as;
    int ret;

    reg_base = memory_region_to_address(&opp->mem, &as);
    if (!as) {
        reg_base = 0;
    } else if (as != &address_space_memory) {
        abort();
    }

    if (reg_base == opp->reg_base) {
        return;
    }

    opp->reg_base = reg_base;

    attr.group = KVM_DEV_MPIC_GRP_MISC;
    attr.attr = KVM_DEV_MPIC_BASE_ADDR;
    attr.addr = (uint64_t)(long)&reg_base;

    ret = ioctl(opp->kern_id, KVM_SET_DEVICE_ATTR, &attr);
    if (ret < 0) {
        fprintf(stderr, "%s: %s %llx\n", __func__, strerror(errno), reg_base);
    }
}

static int kvm_openpic_init(SysBusDevice *dev)
{
    KVMOpenPICState *opp = FROM_SYSBUS(typeof(*opp), dev);
    int kvm_openpic_model;

    memory_region_init_io(&opp->mem, &kvm_openpic_mem_ops, opp,
                          "kvm-openpic", 0x40000);

    switch (opp->model) {
    case OPENPIC_MODEL_FSL_MPIC_20:
        kvm_openpic_model = KVM_DEV_TYPE_FSL_MPIC_20;
        break;

    case OPENPIC_MODEL_FSL_MPIC_42:
        kvm_openpic_model = KVM_DEV_TYPE_FSL_MPIC_42;
        break;

    default:
        return -EINVAL;
    }

    sysbus_init_mmio(dev, &opp->mem);
    qdev_init_gpio_in(&dev->qdev, kvm_openpic_set_irq, OPENPIC_MAX_IRQ);

    opp->mem_listener.commit = kvm_openpic_update_reg_base;
    memory_listener_register(&opp->mem_listener, &address_space_memory);

    msi_supported = true;
    return 0;
}

int kvm_openpic_connect_vcpu(DeviceState *d, CPUState *cs)
{
    KVMOpenPICState *opp = FROM_SYSBUS(typeof(*opp), SYS_BUS_DEVICE(d));
    struct kvm_enable_cap encap = {};

    encap.cap = KVM_CAP_IRQ_MPIC;
    encap.args[0] = opp->kern_id;
    encap.args[1] = cs->cpu_index;

    return kvm_vcpu_ioctl(cs, KVM_ENABLE_CAP, &encap);
}

DeviceState *kvm_openpic_create(BusState *bus, int model)
{
    KVMState *s = kvm_state;
    DeviceState *dev;
    struct kvm_create_device cd = {0};
    int ret;

    if (!kvm_check_extension(s, KVM_CAP_DEVICE_CTRL)) {
        return NULL;
    }

    switch (model) {
    case OPENPIC_MODEL_FSL_MPIC_20:
        cd.type = KVM_DEV_TYPE_FSL_MPIC_20;
        break;

    case OPENPIC_MODEL_FSL_MPIC_42:
        cd.type = KVM_DEV_TYPE_FSL_MPIC_42;
        break;

    default:
        qemu_log_mask(LOG_UNIMP, "%s: unknown openpic model %d\n",
                      __func__, model);
        return NULL;
    }

    ret = kvm_vm_ioctl(s, KVM_CREATE_DEVICE, &cd);
    if (ret < 0) {
        qemu_log_mask(LOG_UNIMP, "%s: can't create device %d: %s\n",
                      __func__, cd.type, strerror(errno));
        return NULL;
    }

    dev = qdev_create(NULL, "kvm-openpic");
    qdev_prop_set_uint32(dev, "model", model);
    qdev_prop_set_uint32(dev, "kernel-id", cd.fd);

    return dev;
}

static Property kvm_openpic_properties[] = {
    DEFINE_PROP_UINT32("model", KVMOpenPICState, model,
                       OPENPIC_MODEL_FSL_MPIC_20),
    DEFINE_PROP_UINT32("kernel-id", KVMOpenPICState, kern_id, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void kvm_openpic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = kvm_openpic_init;
    dc->props = kvm_openpic_properties;
    dc->reset = kvm_openpic_reset;
}

static const TypeInfo kvm_openpic_info = {
    .name          = "kvm-openpic",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(KVMOpenPICState),
    .class_init    = kvm_openpic_class_init,
};

static void kvm_openpic_register_types(void)
{
    type_register_static(&kvm_openpic_info);
}

type_init(kvm_openpic_register_types)
