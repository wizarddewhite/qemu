/*
 * QEMU VMPort emulation
 *
 * Copyright (C) 2007 Hervé Poussineau
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
#include "hw/hw.h"
#include "hw/isa/isa.h"
#include "hw/i386/pc.h"
#include "sysemu/kvm.h"
#include "hw/qdev.h"

//#define VMPORT_DEBUG

#define VMPORT_CMD_GETVERSION 0x0a
#define VMPORT_CMD_GETRAMSIZE 0x14
#define VMPORT_CMD_GETHZ      0x2d

#define VMPORT_ENTRIES 0x2e
#define VMPORT_MAGIC   0x564D5868

#define TYPE_VMPORT "vmport"
#define VMPORT(obj) OBJECT_CHECK(VMPortState, (obj), TYPE_VMPORT)

typedef struct VMPortState
{
    ISADevice parent_obj;

    MemoryRegion io;
    VMPortReadFunc *func[VMPORT_ENTRIES];
    void *opaque[VMPORT_ENTRIES];
} VMPortState;

static VMPortState *port_state;

void vmport_register(unsigned char command, VMPortReadFunc *func, void *opaque)
{
    if (command >= VMPORT_ENTRIES)
        return;

    port_state->func[command] = func;
    port_state->opaque[command] = opaque;
}

static uint64_t vmport_ioport_read(void *opaque, hwaddr addr,
                                   unsigned size)
{
    VMPortState *s = opaque;
    CPUState *cs = current_cpu;
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    unsigned char command;
    uint32_t eax;

    cpu_synchronize_state(cs);

    eax = env->regs[R_EAX];
    if (eax != VMPORT_MAGIC)
        return eax;

    command = env->regs[R_ECX];
    if (command >= VMPORT_ENTRIES)
        return eax;
    if (!s->func[command])
    {
#ifdef VMPORT_DEBUG
        fprintf(stderr, "vmport: unknown command %x\n", command);
#endif
        return eax;
    }

    return s->func[command](s->opaque[command], addr);
}

static void vmport_ioport_write(void *opaque, hwaddr addr,
                                uint64_t val, unsigned size)
{
    X86CPU *cpu = X86_CPU(current_cpu);

    cpu->env.regs[R_EAX] = vmport_ioport_read(opaque, addr, 4);
}

static uint32_t vmport_cmd_get_version(void *opaque, uint32_t addr)
{
    X86CPU *cpu = X86_CPU(current_cpu);

    cpu->env.regs[R_EBX] = VMPORT_MAGIC;
    return 6;
}

static uint32_t vmport_cmd_ram_size(void *opaque, uint32_t addr)
{
    X86CPU *cpu = X86_CPU(current_cpu);

    cpu->env.regs[R_EBX] = 0x1177;
    return ram_size;
}

#ifndef __x86_64__
#error XXX code not adapted to non-x86-64
#endif
static inline uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    uint64_t r;
    /* We cannot use "=A", since this would use %rax on x86_64 */
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    r = ((uint64_t)hi << 32) | lo;
    return r;
}

static uint32_t vmport_cmd_get_hz(void *opaque, uint32_t addr)
{
    X86CPU *cpu = X86_CPU(current_cpu);
    CPUX86State *env = &cpu->env;
    uint64_t tsc_hz;
    const uint64_t apic_hz = 660000000L;

    tsc_hz = (uint64_t)env->tsc_khz;
    if  (!tsc_hz) {
        /* Need to measure locally */
        static uint64_t tsc = 0;
        if ( tsc == 0 ) {
            tsc = rdtsc();
            usleep(100000);
            tsc = (rdtsc() - tsc) * 10;
        }
        tsc_hz = tsc;
    }

    /* Round down to bus frequency */
    tsc_hz = (tsc_hz / apic_hz) * apic_hz;

    /* apic-frequency (bus speed) */
    env->regs[R_ECX] = apic_hz;
    /* High part of tsc-frequency */
    env->regs[R_EBX] = (uint32_t)(tsc_hz >> 32);
    /* Low part of tsc-frequency */
    env->regs[R_EAX] = (uint32_t)tsc_hz;
    return (uint32_t)tsc_hz;
}

/* vmmouse helpers */
void vmmouse_get_data(uint32_t *data)
{
    X86CPU *cpu = X86_CPU(current_cpu);
    CPUX86State *env = &cpu->env;

    data[0] = env->regs[R_EAX]; data[1] = env->regs[R_EBX];
    data[2] = env->regs[R_ECX]; data[3] = env->regs[R_EDX];
    data[4] = env->regs[R_ESI]; data[5] = env->regs[R_EDI];
}

void vmmouse_set_data(const uint32_t *data)
{
    X86CPU *cpu = X86_CPU(current_cpu);
    CPUX86State *env = &cpu->env;

    env->regs[R_EAX] = data[0]; env->regs[R_EBX] = data[1];
    env->regs[R_ECX] = data[2]; env->regs[R_EDX] = data[3];
    env->regs[R_ESI] = data[4]; env->regs[R_EDI] = data[5];
}

static const MemoryRegionOps vmport_ops = {
    .read = vmport_ioport_read,
    .write = vmport_ioport_write,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void vmport_realizefn(DeviceState *dev, Error **errp)
{
    ISADevice *isadev = ISA_DEVICE(dev);
    VMPortState *s = VMPORT(dev);

    memory_region_init_io(&s->io, OBJECT(s), &vmport_ops, s, "vmport", 1);
    isa_register_ioport(isadev, &s->io, 0x5658);

    port_state = s;
    /* Register some generic port commands */
    vmport_register(VMPORT_CMD_GETVERSION, vmport_cmd_get_version, NULL);
    vmport_register(VMPORT_CMD_GETRAMSIZE, vmport_cmd_ram_size, NULL);
    vmport_register(VMPORT_CMD_GETHZ, vmport_cmd_get_hz, NULL);
}

static void vmport_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = vmport_realizefn;
    /* Reason: realize sets global port_state */
    dc->cannot_instantiate_with_device_add_yet = true;
}

static const TypeInfo vmport_info = {
    .name          = TYPE_VMPORT,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(VMPortState),
    .class_init    = vmport_class_initfn,
};

static void vmport_register_types(void)
{
    type_register_static(&vmport_info);
}

type_init(vmport_register_types)
