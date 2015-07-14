/*
 * vfio based device assignment support
 *
 * Copyright Red Hat, Inc. 2012
 *
 * Authors:
 *  Alex Williamson <alex.williamson@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Based on qemu-kvm device-assignment:
 *  Adapted for KVM by Qumranet.
 *  Copyright (c) 2007, Neocleus, Alex Novik (alex@neocleus.com)
 *  Copyright (c) 2007, Neocleus, Guy Zana (guy@neocleus.com)
 *  Copyright (C) 2008, Qumranet, Amit Shah (amit.shah@qumranet.com)
 *  Copyright (C) 2008, Red Hat, Amit Shah (amit.shah@redhat.com)
 *  Copyright (C) 2008, IBM, Muli Ben-Yehuda (muli@il.ibm.com)
 */

#include <dirent.h>
#include <linux/vfio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "config.h"
#include "exec/address-spaces.h"
#include "exec/memory.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pci.h"
#include "qemu-common.h"
#include "qemu/error-report.h"
#include "qemu/event_notifier.h"
#include "qemu/queue.h"
#include "qemu/range.h"
#include "sysemu/kvm.h"
#include "sysemu/sysemu.h"
#include "trace.h"
#include "hw/vfio/vfio.h"
#include "hw/vfio/vfio-common.h"
#include "hw/vfio/pci.h"
#include "hw/vfio/i40evf.h"

// #define DEBUG_I40EVF

#ifdef DEBUG_I40EVF
static const int debug_i40evf = 1;
#else
static const int debug_i40evf = 0;
#endif

#undef DPRINTF /* from vfio-common */
#define DPRINTF(fmt, ...) do { \
        if (debug_i40evf) { \
            printf("i40evf: %38s:%04d" fmt "\n" , \
                   __func__, __LINE__, ## __VA_ARGS__); \
        } \
    } while (0)

void vfio_enable_msi(VFIOPCIDevice *vdev);
void vfio_enable_msix(VFIOPCIDevice *vdev);

static const char *vfio_i40evf_v_opcode_name(int reg)
{
    static char unk[] = "Unknown v_opcode 0x00000000";

    switch (reg) {
    case I40E_VIRTCHNL_OP_UNKNOWN:
        return "I40E_VIRTCHNL_OP_UNKNOWN";
    case I40E_VIRTCHNL_OP_VERSION:
        return "I40E_VIRTCHNL_OP_VERSION";
    case I40E_VIRTCHNL_OP_RESET_VF:
        return "I40E_VIRTCHNL_OP_RESET_VF";
    case I40E_VIRTCHNL_OP_GET_VF_RESOURCES:
        return "I40E_VIRTCHNL_OP_GET_VF_RESOURCES";
    case I40E_VIRTCHNL_OP_CONFIG_TX_QUEUE:
        return "I40E_VIRTCHNL_OP_CONFIG_TX_QUEUE";
    case I40E_VIRTCHNL_OP_CONFIG_RX_QUEUE:
        return "I40E_VIRTCHNL_OP_CONFIG_RX_QUEUE";
    case I40E_VIRTCHNL_OP_CONFIG_VSI_QUEUES:
        return "I40E_VIRTCHNL_OP_CONFIG_VSI_QUEUES";
    case I40E_VIRTCHNL_OP_CONFIG_IRQ_MAP:
        return "I40E_VIRTCHNL_OP_CONFIG_IRQ_MAP";
    case I40E_VIRTCHNL_OP_ENABLE_QUEUES:
        return "I40E_VIRTCHNL_OP_ENABLE_QUEUES";
    case I40E_VIRTCHNL_OP_DISABLE_QUEUES:
        return "I40E_VIRTCHNL_OP_DISABLE_QUEUES";
    case I40E_VIRTCHNL_OP_ADD_ETHER_ADDRESS:
        return "I40E_VIRTCHNL_OP_ADD_ETHER_ADDRESS";
    case I40E_VIRTCHNL_OP_DEL_ETHER_ADDRESS:
        return "I40E_VIRTCHNL_OP_DEL_ETHER_ADDRESS";
    case I40E_VIRTCHNL_OP_ADD_VLAN:
        return "I40E_VIRTCHNL_OP_ADD_VLAN";
    case I40E_VIRTCHNL_OP_DEL_VLAN:
        return "I40E_VIRTCHNL_OP_DEL_VLAN";
    case I40E_VIRTCHNL_OP_CONFIG_PROMISCUOUS_MODE:
        return "I40E_VIRTCHNL_OP_CONFIG_PROMISCUOUS_MODE";
    case I40E_VIRTCHNL_OP_GET_STATS:
        return "I40E_VIRTCHNL_OP_GET_STATS";
    case I40E_VIRTCHNL_OP_FCOE:
        return "I40E_VIRTCHNL_OP_FCOE";
    case I40E_VIRTCHNL_OP_EVENT:
        return "I40E_VIRTCHNL_OP_EVENT";
    case I40E_VIRTCHNL_OP_CONFIG_RSS:
        return "I40E_VIRTCHNL_OP_CONFIG_RSS";
    default: sprintf(unk, "Unknown v_opcode %#x", reg); return unk;
    }
}

static const char *vfio_i40evf_reg_name(int reg)
{
    static char unk[] = "Unknown reg 0x00000000";

    switch (reg) {
    case I40E_VF_ARQBAH1: return "I40E_VF_ARQBAH1";
    case I40E_VF_ARQBAL1: return "I40E_VF_ARQBAL1";
    case I40E_VF_ARQH1: return "I40E_VF_ARQH1";
    case I40E_VF_ARQLEN1: return "I40E_VF_ARQLEN1";
    case I40E_VF_ARQT1: return "I40E_VF_ARQT1";
    case I40E_VF_ATQBAH1: return "I40E_VF_ATQBAH1";
    case I40E_VF_ATQBAL1: return "I40E_VF_ATQBAL1";
    case I40E_VF_ATQH1: return "I40E_VF_ATQH1";
    case I40E_VF_ATQLEN1: return "I40E_VF_ATQLEN1";
    case I40E_VF_ATQT1: return "I40E_VF_ATQT1";
    case I40E_VFGEN_RSTAT: return "I40E_VFGEN_RSTAT";
    case I40E_VFINT_DYN_CTL01: return "I40E_VFINT_DYN_CTL01";
    case I40E_VFINT_DYN_CTLN1(0): return "I40E_VFINT_DYN_CTLN1(0)";
    case I40E_VFINT_DYN_CTLN1(1): return "I40E_VFINT_DYN_CTLN1(1)";
    case I40E_VFINT_DYN_CTLN1(2): return "I40E_VFINT_DYN_CTLN1(2)";
    case I40E_VFINT_DYN_CTLN1(3): return "I40E_VFINT_DYN_CTLN1(3)";
    case I40E_VFINT_DYN_CTLN1(4): return "I40E_VFINT_DYN_CTLN1(4)";
    case I40E_VFINT_DYN_CTLN1(5): return "I40E_VFINT_DYN_CTLN1(5)";
    case I40E_VFINT_DYN_CTLN1(6): return "I40E_VFINT_DYN_CTLN1(6)";
    case I40E_VFINT_DYN_CTLN1(7): return "I40E_VFINT_DYN_CTLN1(7)";
    case I40E_VFINT_DYN_CTLN1(8): return "I40E_VFINT_DYN_CTLN1(8)";
    case I40E_VFINT_DYN_CTLN1(9): return "I40E_VFINT_DYN_CTLN1(9)";
    case I40E_VFINT_DYN_CTLN1(10): return "I40E_VFINT_DYN_CTLN1(10)";
    case I40E_VFINT_DYN_CTLN1(11): return "I40E_VFINT_DYN_CTLN1(11)";
    case I40E_VFINT_DYN_CTLN1(12): return "I40E_VFINT_DYN_CTLN1(12)";
    case I40E_VFINT_DYN_CTLN1(13): return "I40E_VFINT_DYN_CTLN1(13)";
    case I40E_VFINT_DYN_CTLN1(14): return "I40E_VFINT_DYN_CTLN1(14)";
    case I40E_VFINT_DYN_CTLN1(15): return "I40E_VFINT_DYN_CTLN1(15)";
    case I40E_VFINT_ICR0_ENA1: return "I40E_VFINT_ICR0_ENA1";
    case I40E_VFINT_ICR01: return "I40E_VFINT_ICR01";
    case I40E_VFINT_ITR01(0): return "I40E_VFINT_ITR01(0)";
    case I40E_VFINT_ITR01(1): return "I40E_VFINT_ITR01(1)";
    case I40E_VFINT_ITR01(2): return "I40E_VFINT_ITR01(2)";
    case I40E_VFINT_ITRN1(0, 0): return "I40E_VFINT_ITRN1(0, 0)";
    case I40E_VFINT_ITRN1(1, 0): return "I40E_VFINT_ITRN1(1, 0)";
    case I40E_VFINT_ITRN1(2, 0): return "I40E_VFINT_ITRN1(2, 0)";
    case I40E_VFINT_ITRN1(0, 1): return "I40E_VFINT_ITRN1(0, 1)";
    case I40E_VFINT_ITRN1(1, 1): return "I40E_VFINT_ITRN1(1, 1)";
    case I40E_VFINT_ITRN1(2, 1): return "I40E_VFINT_ITRN1(2, 1)";
    case I40E_VFINT_ITRN1(0, 2): return "I40E_VFINT_ITRN1(0, 2)";
    case I40E_VFINT_ITRN1(1, 2): return "I40E_VFINT_ITRN1(1, 2)";
    case I40E_VFINT_ITRN1(2, 2): return "I40E_VFINT_ITRN1(2, 2)";
    case I40E_VFINT_ITRN1(0, 3): return "I40E_VFINT_ITRN1(0, 3)";
    case I40E_VFINT_ITRN1(1, 3): return "I40E_VFINT_ITRN1(1, 3)";
    case I40E_VFINT_ITRN1(2, 3): return "I40E_VFINT_ITRN1(2, 3)";
    case I40E_VFINT_ITRN1(0, 4): return "I40E_VFINT_ITRN1(0, 4)";
    case I40E_VFINT_ITRN1(1, 4): return "I40E_VFINT_ITRN1(1, 4)";
    case I40E_VFINT_ITRN1(2, 4): return "I40E_VFINT_ITRN1(2, 4)";
    case I40E_VFINT_ITRN1(0, 5): return "I40E_VFINT_ITRN1(0, 5)";
    case I40E_VFINT_ITRN1(1, 5): return "I40E_VFINT_ITRN1(1, 5)";
    case I40E_VFINT_ITRN1(2, 5): return "I40E_VFINT_ITRN1(2, 5)";
    case I40E_VFINT_ITRN1(0, 6): return "I40E_VFINT_ITRN1(0, 6)";
    case I40E_VFINT_ITRN1(1, 6): return "I40E_VFINT_ITRN1(1, 6)";
    case I40E_VFINT_ITRN1(2, 6): return "I40E_VFINT_ITRN1(2, 6)";
    case I40E_VFINT_ITRN1(0, 7): return "I40E_VFINT_ITRN1(0, 7)";
    case I40E_VFINT_ITRN1(1, 7): return "I40E_VFINT_ITRN1(1, 7)";
    case I40E_VFINT_ITRN1(2, 7): return "I40E_VFINT_ITRN1(2, 7)";
    case I40E_VFINT_ITRN1(0, 8): return "I40E_VFINT_ITRN1(0, 8)";
    case I40E_VFINT_ITRN1(1, 8): return "I40E_VFINT_ITRN1(1, 8)";
    case I40E_VFINT_ITRN1(2, 8): return "I40E_VFINT_ITRN1(2, 8)";
    case I40E_VFINT_ITRN1(0, 9): return "I40E_VFINT_ITRN1(0, 9)";
    case I40E_VFINT_ITRN1(1, 9): return "I40E_VFINT_ITRN1(1, 9)";
    case I40E_VFINT_ITRN1(2, 9): return "I40E_VFINT_ITRN1(2, 9)";
    case I40E_VFINT_ITRN1(0, 10): return "I40E_VFINT_ITRN1(0, 10)";
    case I40E_VFINT_ITRN1(1, 10): return "I40E_VFINT_ITRN1(1, 10)";
    case I40E_VFINT_ITRN1(2, 10): return "I40E_VFINT_ITRN1(2, 10)";
    case I40E_VFINT_ITRN1(0, 11): return "I40E_VFINT_ITRN1(0, 11)";
    case I40E_VFINT_ITRN1(1, 11): return "I40E_VFINT_ITRN1(1, 11)";
    case I40E_VFINT_ITRN1(2, 11): return "I40E_VFINT_ITRN1(2, 11)";
    case I40E_VFINT_ITRN1(0, 12): return "I40E_VFINT_ITRN1(0, 12)";
    case I40E_VFINT_ITRN1(1, 12): return "I40E_VFINT_ITRN1(1, 12)";
    case I40E_VFINT_ITRN1(2, 12): return "I40E_VFINT_ITRN1(2, 12)";
    case I40E_VFINT_ITRN1(0, 13): return "I40E_VFINT_ITRN1(0, 13)";
    case I40E_VFINT_ITRN1(1, 13): return "I40E_VFINT_ITRN1(1, 13)";
    case I40E_VFINT_ITRN1(2, 13): return "I40E_VFINT_ITRN1(2, 13)";
    case I40E_VFINT_ITRN1(0, 14): return "I40E_VFINT_ITRN1(0, 14)";
    case I40E_VFINT_ITRN1(1, 14): return "I40E_VFINT_ITRN1(1, 14)";
    case I40E_VFINT_ITRN1(2, 14): return "I40E_VFINT_ITRN1(2, 14)";
    case I40E_VFINT_ITRN1(0, 15): return "I40E_VFINT_ITRN1(0, 15)";
    case I40E_VFINT_ITRN1(1, 15): return "I40E_VFINT_ITRN1(1, 15)";
    case I40E_VFINT_ITRN1(2, 15): return "I40E_VFINT_ITRN1(2, 15)";
    case I40E_VFINT_STAT_CTL01: return "I40E_VFINT_STAT_CTL01";
    case I40E_QRX_TAIL1(0): return "I40E_QRX_TAIL1(0)";
    case I40E_QRX_TAIL1(1): return "I40E_QRX_TAIL1(1)";
    case I40E_QRX_TAIL1(2): return "I40E_QRX_TAIL1(2)";
    case I40E_QRX_TAIL1(3): return "I40E_QRX_TAIL1(3)";
    case I40E_QRX_TAIL1(4): return "I40E_QRX_TAIL1(4)";
    case I40E_QRX_TAIL1(5): return "I40E_QRX_TAIL1(5)";
    case I40E_QRX_TAIL1(6): return "I40E_QRX_TAIL1(6)";
    case I40E_QRX_TAIL1(7): return "I40E_QRX_TAIL1(7)";
    case I40E_QRX_TAIL1(8): return "I40E_QRX_TAIL1(8)";
    case I40E_QRX_TAIL1(9): return "I40E_QRX_TAIL1(9)";
    case I40E_QRX_TAIL1(10): return "I40E_QRX_TAIL1(10)";
    case I40E_QRX_TAIL1(11): return "I40E_QRX_TAIL1(11)";
    case I40E_QRX_TAIL1(12): return "I40E_QRX_TAIL1(12)";
    case I40E_QRX_TAIL1(13): return "I40E_QRX_TAIL1(13)";
    case I40E_QRX_TAIL1(14): return "I40E_QRX_TAIL1(14)";
    case I40E_QRX_TAIL1(15): return "I40E_QRX_TAIL1(15)";
    case I40E_QTX_TAIL1(0): return "I40E_QTX_TAIL1(0)";
    case I40E_QTX_TAIL1(1): return "I40E_QTX_TAIL1(1)";
    case I40E_QTX_TAIL1(2): return "I40E_QTX_TAIL1(2)";
    case I40E_QTX_TAIL1(3): return "I40E_QTX_TAIL1(3)";
    case I40E_QTX_TAIL1(4): return "I40E_QTX_TAIL1(4)";
    case I40E_QTX_TAIL1(5): return "I40E_QTX_TAIL1(5)";
    case I40E_QTX_TAIL1(6): return "I40E_QTX_TAIL1(6)";
    case I40E_QTX_TAIL1(7): return "I40E_QTX_TAIL1(7)";
    case I40E_QTX_TAIL1(8): return "I40E_QTX_TAIL1(8)";
    case I40E_QTX_TAIL1(9): return "I40E_QTX_TAIL1(9)";
    case I40E_QTX_TAIL1(10): return "I40E_QTX_TAIL1(10)";
    case I40E_QTX_TAIL1(11): return "I40E_QTX_TAIL1(11)";
    case I40E_QTX_TAIL1(12): return "I40E_QTX_TAIL1(12)";
    case I40E_QTX_TAIL1(13): return "I40E_QTX_TAIL1(13)";
    case I40E_QTX_TAIL1(14): return "I40E_QTX_TAIL1(14)";
    case I40E_QTX_TAIL1(15): return "I40E_QTX_TAIL1(15)";
    case I40E_VFCM_PE_ERRDATA: return "I40E_VFCM_PE_ERRDATA";
    case I40E_VFCM_PE_ERRINFO: return "I40E_VFCM_PE_ERRINFO";
    case I40E_VFQF_HENA(0): return "I40E_VFQF_HENA(0)";
    case I40E_VFQF_HENA(1): return "I40E_VFQF_HENA(1)";
    case I40E_VFQF_HKEY(0): return "I40E_VFQF_HKEY(0)";
    case I40E_VFQF_HKEY(1): return "I40E_VFQF_HKEY(1)";
    case I40E_VFQF_HKEY(2): return "I40E_VFQF_HKEY(2)";
    case I40E_VFQF_HKEY(3): return "I40E_VFQF_HKEY(3)";
    case I40E_VFQF_HKEY(4): return "I40E_VFQF_HKEY(4)";
    case I40E_VFQF_HKEY(5): return "I40E_VFQF_HKEY(5)";
    case I40E_VFQF_HKEY(6): return "I40E_VFQF_HKEY(6)";
    case I40E_VFQF_HKEY(7): return "I40E_VFQF_HKEY(7)";
    case I40E_VFQF_HKEY(8): return "I40E_VFQF_HKEY(8)";
    case I40E_VFQF_HKEY(9): return "I40E_VFQF_HKEY(9)";
    case I40E_VFQF_HKEY(10): return "I40E_VFQF_HKEY(10)";
    case I40E_VFQF_HKEY(11): return "I40E_VFQF_HKEY(11)";
    case I40E_VFQF_HKEY(12): return "I40E_VFQF_HKEY(12)";
    case I40E_VFQF_HLUT(0): return "I40E_VFQF_HLUT(0)";
    case I40E_VFQF_HLUT(1): return "I40E_VFQF_HLUT(1)";
    case I40E_VFQF_HLUT(2): return "I40E_VFQF_HLUT(2)";
    case I40E_VFQF_HLUT(3): return "I40E_VFQF_HLUT(3)";
    case I40E_VFQF_HLUT(4): return "I40E_VFQF_HLUT(4)";
    case I40E_VFQF_HLUT(5): return "I40E_VFQF_HLUT(5)";
    case I40E_VFQF_HLUT(6): return "I40E_VFQF_HLUT(6)";
    case I40E_VFQF_HLUT(7): return "I40E_VFQF_HLUT(7)";
    case I40E_VFQF_HLUT(8): return "I40E_VFQF_HLUT(8)";
    case I40E_VFQF_HLUT(9): return "I40E_VFQF_HLUT(9)";
    case I40E_VFQF_HLUT(10): return "I40E_VFQF_HLUT(10)";
    case I40E_VFQF_HLUT(11): return "I40E_VFQF_HLUT(11)";
    case I40E_VFQF_HLUT(12): return "I40E_VFQF_HLUT(12)";
    case I40E_VFQF_HLUT(13): return "I40E_VFQF_HLUT(13)";
    case I40E_VFQF_HLUT(14): return "I40E_VFQF_HLUT(14)";
    case I40E_VFQF_HLUT(15): return "I40E_VFQF_HLUT(15)";
    case I40E_VFQF_HREGION(0): return "I40E_VFQF_HREGION(0)";
    case I40E_VFQF_HREGION(1): return "I40E_VFQF_HREGION(1)";
    case I40E_VFQF_HREGION(2): return "I40E_VFQF_HREGION(2)";
    case I40E_VFQF_HREGION(3): return "I40E_VFQF_HREGION(3)";
    case I40E_VFQF_HREGION(4): return "I40E_VFQF_HREGION(4)";
    case I40E_VFQF_HREGION(5): return "I40E_VFQF_HREGION(5)";
    case I40E_VFQF_HREGION(6): return "I40E_VFQF_HREGION(6)";
    case I40E_VFQF_HREGION(7): return "I40E_VFQF_HREGION(7)";
    default: sprintf(unk, "Unknown reg %#x", reg); return unk;
    }
}

/* Read a real (hardware visible) register */
static uint32_t vfio_i40evf_r32(VFIOI40EDevice *vdev, int reg)
{
    VFIOPCIDevice *vpdev = VFIO_PCI(vdev);
    VFIOBAR *bar = &vpdev->bars[0];
    uint64_t val = 0;
    volatile uint32_t *p = bar->region.mmap + reg;

    val = *p;
    DPRINTF(" -> reg=%s val=%#lx", vfio_i40evf_reg_name(reg), val);

    return val;
}

/* Write a real (hardware visible) register */
static void vfio_i40evf_w32(VFIOI40EDevice *vdev, int reg, uint32_t val)
{
    VFIOPCIDevice *vpdev = VFIO_PCI(vdev);
    VFIOBAR *bar = &vpdev->bars[0];
    volatile uint32_t *p = bar->region.mmap + reg;

    *p = val;
    DPRINTF(" -> reg=%s val=%#x", vfio_i40evf_reg_name(reg), val);
}

/* Read a virtual (guest visible) register */
static uint32_t vfio_i40evf_vr32(VFIOI40EDevice *vdev, int reg)
{
    uint64_t val = vdev->regs[reg / 4];
    DPRINTF(" -> reg=%s val=%#lx", vfio_i40evf_reg_name(reg), val);
    return val;
}

/* Write a virtual (guest visible) register */
static void vfio_i40evf_vw32(VFIOI40EDevice *vdev, int reg, uint32_t val)
{
    DPRINTF(" -> reg=%s val=%#x", vfio_i40evf_reg_name(reg), val);
    vdev->regs[reg / 4] = val;
}

static void stop_store_aq(VFIOI40EDevice *vdev, int lenreg)
{
    uint32_t lenval;

    lenval = vfio_i40evf_r32(vdev, lenreg);
    vfio_i40evf_vw32(vdev, lenreg, lenval);
    lenval &= ~I40E_VF_ARQLEN1_ARQENABLE_MASK;
    vfio_i40evf_w32(vdev, lenreg, lenval);
}

static void vfio_i40e_aq_map(VFIOI40EDevice *vdev)
{
    I40eAdminQueueDescriptor *arq = vdev->admin_queue +
        (I40E_AQ_LOCATION_ARQ - I40E_AQ_LOCATION);
    int i;

    I40eAdminQueueDescriptor init_arq =
        {
             .datalen = 0x1000,
             .flags = I40E_AQ_FLAG_BUF,
             .params.external.addr_high = I40E_AQ_LOCATION_ARQ_DATA >> 32,
             .params.external.addr_low = (uint32_t)I40E_AQ_LOCATION_ARQ_DATA,
        };
    *arq = init_arq;

    vfio_i40evf_w32(vdev, I40E_VF_ARQBAH1, I40E_AQ_LOCATION_ARQ >> 32);
    vfio_i40evf_w32(vdev, I40E_VF_ARQBAL1, (uint32_t)I40E_AQ_LOCATION_ARQ);
    vfio_i40evf_w32(vdev, I40E_VF_ARQH1, 0);
    vfio_i40evf_w32(vdev, I40E_VF_ARQT1, 0);
    vfio_i40evf_w32(vdev, I40E_VF_ARQLEN1,
                    vdev->aq_len | I40E_VF_ARQLEN1_ARQENABLE_MASK);

    vfio_i40evf_w32(vdev, I40E_VF_ATQBAH1, (I40E_AQ_LOCATION_ATQ) >> 32);
    vfio_i40evf_w32(vdev, I40E_VF_ATQBAL1, (uint32_t)I40E_AQ_LOCATION_ATQ);
    vfio_i40evf_w32(vdev, I40E_VF_ATQH1, 0);
    vfio_i40evf_w32(vdev, I40E_VF_ATQT1, 0);
    vfio_i40evf_w32(vdev, I40E_VF_ATQLEN1,
                    vdev->aq_len | I40E_VF_ATQLEN1_ATQENABLE_MASK);

    /* Expose ARQ entries */
    for (i = 0; i < vdev->aq_len; i++) {
        uint64_t arq_data = I40E_AQ_LOCATION_ARQ_DATA + (I40E_ARQ_DATA_LEN * i);
        int arq_offset = (I40E_AQ_LOCATION_ARQ - I40E_AQ_LOCATION);
        volatile I40eAdminQueueDescriptor *arq = vdev->admin_queue + arq_offset;
        volatile I40eAdminQueueDescriptor *arq_cur = &arq[i];
        I40eAdminQueueDescriptor desc = {
            .datalen = I40E_ARQ_DATA_LEN,
            .flags = I40E_AQ_FLAG_BUF,
            .params.external.addr_high = arq_data >> 32,
            .params.external.addr_low = (uint32_t)arq_data,
        };
        *arq_cur = desc;
    }
    vfio_i40evf_w32(vdev, I40E_VF_ARQT1, 1);
}

/*
 * Different VFs have different VSI IDs. Since the guest has no awareness
 * that it's running on a different host now and thus may have a different
 * vsi id, let's dynamically patch its admin queue requests to expose the
 * one the host expects
 */
static void vfio_i40evf_atq_fixup_vsi_id(VFIOI40EDevice *vdev,
                                         volatile I40eAdminQueueDescriptor *req)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    void *data = vdev->admin_queue +
                 (I40E_AQ_LOCATION_ATQ_DATA - I40E_AQ_LOCATION);
    uint64_t data_addr;
    int i;

    if (!req->datalen || (req->opcode != I40E_AQC_OPC_SEND_MSG_TO_PF)) {
        return;
    }

    data_addr = req->params.external.addr_high;
    data_addr <<= 32;
    data_addr |= req->params.external.addr_low;

    switch (req->cookie_high) {
    case I40E_VIRTCHNL_OP_CONFIG_IRQ_MAP:
    case I40E_VIRTCHNL_OP_CONFIG_VSI_QUEUES:
    case I40E_VIRTCHNL_OP_ADD_ETHER_ADDRESS:
    case I40E_VIRTCHNL_OP_DEL_ETHER_ADDRESS:
    case I40E_VIRTCHNL_OP_GET_STATS:
    case I40E_VIRTCHNL_OP_ENABLE_QUEUES:
    case I40E_VIRTCHNL_OP_DISABLE_QUEUES:
        if (data_addr == I40E_AQ_LOCATION_ATQ_DATA) {
            /* Commands come from us, no need to copy */
            break;
        }

        pci_dma_read(pdev, data_addr, data, req->datalen);
        req->params.external.addr_high = I40E_AQ_LOCATION_ATQ_DATA >> 32;
        req->params.external.addr_low = (uint32_t)I40E_AQ_LOCATION_ATQ_DATA;
        break;
    }

    switch (req->cookie_high) {
    case I40E_VIRTCHNL_OP_CONFIG_IRQ_MAP: {
        struct i40e_virtchnl_irq_map_info *mydata = (void*)data;

        for (i = 0; i < MIN(mydata->num_vectors, 16); i++) {
            DPRINTF(" CONFIG_IRQ_MAP: Setting VSI ID of vec[%d] to %#x", i,
                    vdev->vsi_id);
            mydata->vecmap[i].vsi_id = vdev->vsi_id;
        }
        break;
    }
    case I40E_VIRTCHNL_OP_CONFIG_VSI_QUEUES: {
        struct i40e_virtchnl_vsi_queue_config_info *mydata = (void*)data;
        mydata->vsi_id = vdev->vsi_id;
        for (i = 0; i < MIN(mydata->num_queue_pairs, 16); i++) {
            mydata->qpair[i].txq.vsi_id = vdev->vsi_id;
            mydata->qpair[i].rxq.vsi_id = vdev->vsi_id;
        }
        break;
    }
    case I40E_VIRTCHNL_OP_ADD_ETHER_ADDRESS:
    case I40E_VIRTCHNL_OP_DEL_ETHER_ADDRESS: {
        struct i40e_virtchnl_ether_addr_list *mydata = (void*)data;
        mydata->vsi_id = vdev->vsi_id;
        break;
    }
    case I40E_VIRTCHNL_OP_GET_STATS:
    case I40E_VIRTCHNL_OP_ENABLE_QUEUES:
    case I40E_VIRTCHNL_OP_DISABLE_QUEUES: {
        struct i40e_virtchnl_queue_select *mydata = (void*)data;
        mydata->vsi_id = vdev->vsi_id;
        break;
    }
    }
}

/* Returns the Admin Receive Queue Base Address as configured by the guest */
static hwaddr vfio_i40e_get_arqba(VFIOI40EDevice *vdev)
{
    hwaddr ret;

    ret = vfio_i40evf_vr32(vdev, I40E_VF_ARQBAH1);
    ret <<= 32;
    ret |= vfio_i40evf_vr32(vdev, I40E_VF_ARQBAL1);

    return ret;
}

/* Returns the Admin Transmit Queue Base Address as configured by the guest */
static hwaddr vfio_i40e_get_atqba(VFIOI40EDevice *vdev)
{
    hwaddr ret;

    ret = vfio_i40evf_vr32(vdev, I40E_VF_ATQBAH1);
    ret <<= 32;
    ret |= vfio_i40evf_vr32(vdev, I40E_VF_ATQBAL1);

    return ret;
}

/* Increment the guest visible admin queue pointer by one */
static void vfio_i40e_aq_inc(VFIOI40EDevice *vdev, int reg)
{
    uint32_t val = vfio_i40evf_vr32(vdev, reg);
    uint32_t len, lenreg;

    switch (reg) {
    case I40E_VF_ATQT1:
    case I40E_VF_ATQH1:
        lenreg = I40E_VF_ATQLEN1;
        break;
    case I40E_VF_ARQT1:
    case I40E_VF_ARQH1:
        lenreg = I40E_VF_ARQLEN1;
        break;
    default:
        assert(0);
    }

    len = vfio_i40evf_vr32(vdev, lenreg) & I40E_VF_ARQLEN1_ARQLEN_MASK;
    val = (val + 1) % len;
    vfio_i40evf_vw32(vdev, reg, val);
}

/* Send one Admin Transmit Queue Command to the device */
static int vfio_i40e_atq_send_nowait(VFIOI40EDevice *vdev,
                                     I40eAdminQueueDescriptor *req)
{
    int atqt = vfio_i40evf_r32(vdev, I40E_VF_ATQT1);
    int atq_offset = (I40E_AQ_LOCATION_ATQ - I40E_AQ_LOCATION);
    volatile I40eAdminQueueDescriptor *atq = vdev->admin_queue + atq_offset;
    volatile I40eAdminQueueDescriptor *atq_cur = &atq[atqt];

    DPRINTF(" hware queue index = %#x", atqt);

    /* Copy command into our own queue buffer */
    *atq_cur = *req;

    /* Change the vsi id on the fly if we have to */
    vfio_i40evf_atq_fixup_vsi_id(vdev, atq_cur);

    /* Move the tail one ahead and thus tell the card we have a cmd */
    atqt = (atqt + 1) % vdev->aq_len;
    vfio_i40evf_w32(vdev, I40E_VF_ATQT1, atqt);

    return atqt;
}

static void vfio_i40e_atq_send(VFIOI40EDevice *vdev,
                               I40eAdminQueueDescriptor *req,
                               I40eAdminQueueDescriptor *res)
{
    int atqt = vfio_i40evf_r32(vdev, I40E_VF_ATQT1);
    int atq_offset = (I40E_AQ_LOCATION_ATQ - I40E_AQ_LOCATION);
    volatile I40eAdminQueueDescriptor *atq = vdev->admin_queue + atq_offset;
    volatile I40eAdminQueueDescriptor *atq_cur = &atq[atqt];

    atqt = vfio_i40e_atq_send_nowait(vdev, req);

    /* Wait for completion */
    while(vfio_i40evf_r32(vdev, I40E_VF_ATQH1) != atqt) usleep(5000);

    /* Copy response into res */
    *res = *atq_cur;
}

static void vfio_i40e_print_aq_cmd(PCIDevice *pdev,
                                   I40eAdminQueueDescriptor *req,
                                   const char *name)
{
    uint64_t data_addr;
    unsigned char *data;
    int i;
    const char *opname;

    if (!debug_i40evf) {
        return;
    }

    data = g_malloc(req->datalen);
    opname = vfio_i40evf_v_opcode_name(req->cookie_high);

    data_addr = req->params.external.addr_high;
    data_addr <<= 32;
    data_addr |= req->params.external.addr_low;

    pci_dma_read(pdev, data_addr, data, req->datalen);

    DPRINTF(" %s", name);
    DPRINTF(" |- opcode    = %#x", req->opcode);
    DPRINTF(" |- length    = %#x", req->datalen);
    DPRINTF(" |- flags     = %#x", req->flags);
    DPRINTF(" |- cookie hi = %#x (%s)", req->cookie_high, opname);
    DPRINTF(" |- cookie lo = %#x", req->cookie_low);
    DPRINTF(" |- retval    = %#x", req->retval);
    DPRINTF(" `- data addr = %#"PRIx64"", data_addr);

    if (req->datalen && (req->opcode == I40E_AQC_OPC_SEND_MSG_TO_PF)) {
        switch (req->cookie_high) { /* v_opcode */
        case I40E_VIRTCHNL_OP_VERSION: {
            struct i40e_virtchnl_version_info *verinfo = (void*)data;
            DPRINTF("    |- major = %#x", verinfo->major);
            DPRINTF("    `- minor = %#x", verinfo->minor);
            break;
        }
        case I40E_VIRTCHNL_OP_CONFIG_VSI_QUEUES: {
            struct i40e_virtchnl_vsi_queue_config_info *mydata = (void*)data;
            DPRINTF("    |- num_queue_pairs = %#x", mydata->num_queue_pairs);
            for (i = 0; i < mydata->num_queue_pairs; i++) {
                struct i40e_virtchnl_queue_pair_info *qp = &mydata->qpair[i];
                DPRINTF("    |    |- txq.vsi_id           = %#x",
                        qp->txq.vsi_id);
                DPRINTF("    |    |- txq.queue_id         = %#x",
                        qp->txq.queue_id);
                DPRINTF("    |    |- txq.ring_len         = %#x",
                        qp->txq.ring_len);
                DPRINTF("    |    |- txq.headwb_enabled   = %#x",
                        qp->txq.headwb_enabled);
                DPRINTF("    |    |- txq.dma_ring_addr    = %#"PRIx64"",
                        qp->txq.dma_ring_addr);
                DPRINTF("    |    |- txq.dma_headwb_addr  = %#"PRIx64"",
                        qp->txq.dma_headwb_addr);
                DPRINTF("    |    |- rxq.vsi_id           = %#x",
                        qp->rxq.vsi_id);
                DPRINTF("    |    |- rxq.queue_id         = %#x",
                        qp->rxq.queue_id);
                DPRINTF("    |    |- rxq.ring_len         = %#x",
                        qp->rxq.ring_len);
                DPRINTF("    |    |- rxq.hdr_size         = %#x",
                        qp->rxq.hdr_size);
                DPRINTF("    |    |- rxq.splithdr_enabled = %#x",
                        qp->rxq.splithdr_enabled);
                DPRINTF("    |    |- rxq.databuffer_size  = %#x",
                        qp->rxq.databuffer_size);
                DPRINTF("    |    |- rxq.max_pkt_size     = %#x",
                        qp->rxq.max_pkt_size);
                DPRINTF("    |    |- rxq.dma_ring_addr    = %#"PRIx64"",
                        qp->rxq.dma_ring_addr);
                DPRINTF("    |    |- rxq.splithdr_enabled = %#x",
                        qp->rxq.splithdr_enabled);
                DPRINTF("    |    `- rxq.rx_split_pos     = %#x",
                        qp->rxq.rx_split_pos);
            }
            DPRINTF("    `- vsi_id          = %#x", mydata->vsi_id);
            break;
        }
        case I40E_VIRTCHNL_OP_CONFIG_IRQ_MAP: {
            struct i40e_virtchnl_irq_map_info *mydata = (void*)data;
            DPRINTF("    |- num_vectors = %#x", mydata->num_vectors);
            for (i = 0; i < mydata->num_vectors; i++) {
                struct i40e_virtchnl_vector_map *map = &mydata->vecmap[i];
                DPRINTF("        |- vsi_id    = %#x", map->vsi_id);
                DPRINTF("        |- vector_id = %#x", map->vector_id);
                DPRINTF("        |- rxq_map   = %#x", map->rxq_map);
                DPRINTF("        |- txq_map   = %#x", map->txq_map);
                DPRINTF("        |- rxitr_idx = %#x", map->rxitr_idx);
                DPRINTF("        `- txitr_idx = %#x", map->txitr_idx);
            }
            break;
        }
        case I40E_VIRTCHNL_OP_GET_STATS:
        case I40E_VIRTCHNL_OP_ENABLE_QUEUES:
        case I40E_VIRTCHNL_OP_DISABLE_QUEUES: {
            struct i40e_virtchnl_queue_select *mydata = (void*)data;
            DPRINTF("    |- vsi_id    = %#x", mydata->vsi_id);
            DPRINTF("    |- pad       = %#x", mydata->pad);
            DPRINTF("    |- rx_queues = %#x", mydata->rx_queues);
            DPRINTF("    `- tx_queues = %#x", mydata->tx_queues);
            break;
        }
        case I40E_VIRTCHNL_OP_ADD_ETHER_ADDRESS:
        case I40E_VIRTCHNL_OP_DEL_ETHER_ADDRESS: {
            struct i40e_virtchnl_ether_addr_list *mydata = (void*)data;
            DPRINTF("    |- num_elements = %#x", mydata->num_elements);
            for (i = 0; i < mydata->num_elements; i++) {
                struct i40e_virtchnl_ether_addr *addr = &mydata->list[i];
                DPRINTF("    |   `- addr = %02x:%02x:%02x:%02x:%02x:%02x",
                    addr->addr[0], addr->addr[1], addr->addr[2], addr->addr[3],
                    addr->addr[4], addr->addr[5]);
            }
            DPRINTF("    `- vsi_id          = %#x", mydata->vsi_id);
            break;
        }
        default:
            DPRINTF("    `- data = ");
            for (i = 0; i < req->datalen; i++) {
                DPRINTF("%02x ", (uint8_t)data[i]);
            }
            DPRINTF("");
            break;
        }
    }

    if (req->datalen && (req->opcode == I40E_AQC_OPC_SEND_MSG_TO_VF)) {
        switch (req->cookie_high) { /* v_opcode */
        case I40E_VIRTCHNL_OP_VERSION: {
            struct i40e_virtchnl_version_info *verinfo = (void*)data;
            DPRINTF("    |- major = %#x", verinfo->major);
            DPRINTF("    `- minor = %#x", verinfo->minor);
            break;
        }
        case I40E_VIRTCHNL_OP_GET_VF_RESOURCES: {
            struct i40e_virtchnl_vf_resource *vfres = (void*)data;
            DPRINTF("    |- num_vsis          = %#x", vfres->num_vsis);
            for (i = 0; i < vfres->num_vsis; i++) {
                struct i40e_virtchnl_vsi_resource *vsi = &vfres->vsi_res[i];
                DPRINTF("    |   |- vsi_id           = %#x",
                        vsi->vsi_id);
                DPRINTF("    |   |- num_queue_pairs  = %#x",
                        vsi->num_queue_pairs);
                DPRINTF("    |   |- vsi_type         = %#x",
                        vsi->vsi_type);
                DPRINTF("    |   |- qset_handle      = %#x",
                        vsi->qset_handle);
                DPRINTF("    |   `- default_mac_addr = %02x:%02x:%02x:"
                        "%02x:%02x:%02x",
                        vsi->default_mac_addr[0], vsi->default_mac_addr[1],
                        vsi->default_mac_addr[2], vsi->default_mac_addr[3],
                        vsi->default_mac_addr[4], vsi->default_mac_addr[5]);
            }
            DPRINTF("    |- num_queue_pairs   = %#x", vfres->num_queue_pairs);
            DPRINTF("    |- max_vectors       = %#x", vfres->max_vectors);
            DPRINTF("    |- max_mtu           = %#x", vfres->max_mtu);
            DPRINTF("    |- vf_offload_flags  = %#x", vfres->vf_offload_flags);
            DPRINTF("    |- max_fcoe_contexts = %#x", vfres->max_fcoe_contexts);
            DPRINTF("    `- max_fcoe_filters  = %#x", vfres->max_fcoe_filters);
            break;
        }
        case I40E_VIRTCHNL_OP_GET_STATS: {
            struct i40e_eth_stats *mydata = (void*)data;
            DPRINTF("    |- rx_bytes            = %#"PRIx64,
                    mydata->rx_bytes);
            DPRINTF("    |- rx_unicast          = %#"PRIx64,
                    mydata->rx_unicast);
            DPRINTF("    |- rx_multicast        = %#"PRIx64,
                    mydata->rx_multicast);
            DPRINTF("    |- rx_broadcast        = %#"PRIx64,
                    mydata->rx_broadcast);
            DPRINTF("    |- rx_discards         = %#"PRIx64,
                    mydata->rx_discards);
            DPRINTF("    |- rx_unknown_protocol = %#"PRIx64,
                    mydata->rx_unknown_protocol);
            DPRINTF("    |- tx_bytes            = %#"PRIx64,
                    mydata->tx_bytes);
            DPRINTF("    |- tx_unicast          = %#"PRIx64,
                    mydata->tx_unicast);
            DPRINTF("    |- tx_multicast        = %#"PRIx64,
                    mydata->tx_multicast);
            DPRINTF("    |- tx_broadcast        = %#"PRIx64,
                    mydata->tx_broadcast);
            DPRINTF("    |- tx_discards         = %#"PRIx64,
                    mydata->tx_discards);
            DPRINTF("    `- tx_errors           = %#"PRIx64,
                    mydata->tx_errors);
            break;
        }
        default:
            DPRINTF("    `- data = ");
            for (i = 0; i < req->datalen; i++) {
                DPRINTF("%02x ", (uint8_t)data[i]);
            }
            DPRINTF("");
            break;
        }
    }

    g_free(data);
}

static int vfio_i40e_record_atq_cmd(VFIOI40EDevice *vdev,
                                    PCIDevice *pdev,
                                    I40eAdminQueueDescriptor *desc)
{
    unsigned char data[desc->datalen];
    uint64_t data_addr;

    if ((desc->opcode == I40E_AQC_OPC_SEND_MSG_TO_PF) &&
        (desc->cookie_high == I40E_VIRTCHNL_OP_RESET_VF)) {
        vdev->arq_ignore = 0;
        vdev->arq_active = false;
        vfio_i40evf_w32(vdev, I40E_VF_ATQT1, 0);
        vfio_i40evf_vw32(vdev, I40E_VF_ARQLEN1, 0);
        vfio_i40evf_vw32(vdev, I40E_VF_ATQH1, 0);
        vfio_i40evf_vw32(vdev, I40E_VF_ATQT1, 0);
        vfio_i40evf_vw32(vdev, I40E_VF_ARQH1, 0);
        vfio_i40evf_vw32(vdev, I40E_VF_ARQT1, 0);
        vfio_i40e_atq_send_nowait(vdev, desc);
        return 1;
    }

    if (!desc->datalen || (desc->opcode != I40E_AQC_OPC_SEND_MSG_TO_PF)) {
        return 0;
    }

    data_addr = desc->params.external.addr_high;
    data_addr <<= 32;
    data_addr |= desc->params.external.addr_low;

    switch (desc->cookie_high) {
    case I40E_VIRTCHNL_OP_CONFIG_IRQ_MAP: {
        pci_dma_read(pdev, data_addr, &vdev->irq_map,
                     MIN(desc->datalen, sizeof(vdev->irq_map)));
        break;
    }
    case I40E_VIRTCHNL_OP_CONFIG_VSI_QUEUES: {
        pci_dma_read(pdev, data_addr, &vdev->vsi_config,
                     MIN(desc->datalen, sizeof(vdev->vsi_config)));
        break;
    }
    case I40E_VIRTCHNL_OP_ADD_ETHER_ADDRESS: {
        if (!vdev->addr.vsi_id) {
            pci_dma_read(pdev, data_addr, &vdev->addr,
                         MIN(desc->datalen, sizeof(vdev->addr)));
        }
        break;
    }
    case I40E_VIRTCHNL_OP_ENABLE_QUEUES: {
        struct i40e_virtchnl_queue_select *mydata = (void*)data;
        pci_dma_read(pdev, data_addr, data, desc->datalen);
        vdev->queue_select = *mydata;
        break;
    }
    }

    return 0;
}

/* Process one Admin Queue Command from the guest */
static void vfio_i40e_atq_process_one(VFIOI40EDevice *vdev, int index)
{
    char *name;
    I40eAdminQueueDescriptor desc;
    PCIDevice *pdev = PCI_DEVICE(vdev);
    hwaddr addr = vfio_i40e_get_atqba(vdev) + (index * sizeof(desc));

    DPRINTF(" guest queue index = %#x", index);
    DPRINTF(" queue addr = %#lx", addr);

    /* Read guest's command */
    pci_dma_read(pdev, addr, &desc, sizeof(desc));

    if (debug_i40evf) {
        name = g_strdup_printf("ATQ request %#x", index);
        vfio_i40e_print_aq_cmd(pdev, &desc, name);
        g_free(name);
    }

    if (vfio_i40e_record_atq_cmd(vdev, pdev, &desc)) {
        /* Command is already handled */
        return;
    }

    /* Run guest command on card */
    vfio_i40e_atq_send(vdev, &desc, &desc);

    if (debug_i40evf) {
        name = g_strdup_printf("ATQ response %#x", index);
        vfio_i40e_print_aq_cmd(pdev, &desc, name);
        g_free(name);
    }

    pci_dma_write(pdev, addr, &desc, sizeof(desc));

    /* Notify guest that the request is finished */
    vfio_i40e_aq_inc(vdev, I40E_VF_ATQH1);
}

static void vfio_i40evf_arq_set_vsi_id(VFIOI40EDevice *vdev,
                                       PCIDevice *pdev,
                                       I40eAdminQueueDescriptor *desc)
{
    uint64_t data_addr;
    unsigned char *data;
    struct i40e_virtchnl_vf_resource *mydata;

    if ((desc->opcode != I40E_AQC_OPC_SEND_MSG_TO_VF) ||
        (desc->cookie_high != I40E_VIRTCHNL_OP_GET_VF_RESOURCES) ||
        !desc->datalen) {
        /* Something went wrong. Ignore this round */
        return;
    }

    data = g_malloc(desc->datalen);
    mydata = (void*)data;
    data_addr = desc->params.external.addr_high;
    data_addr <<= 32;
    data_addr |= desc->params.external.addr_low;
    pci_dma_read(pdev, data_addr, data, desc->datalen);

    /* XXX We only support a single vsi id for now */
    vdev->vsi_id = mydata->vsi_res[0].vsi_id;
    DPRINTF(" new vsi_id: %#x", vdev->vsi_id);

    vdev->arq_fetch_vsi_id = false;
    g_free(data);
}

/* Send one Admin Receive Queue Command to the guest */
static void vfio_i40e_arq_recv(VFIOI40EDevice *vdev,
                              I40eAdminQueueDescriptor *req)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    int arqh = vfio_i40evf_vr32(vdev, I40E_VF_ARQH1);
    hwaddr addr = vfio_i40e_get_arqba(vdev) + (arqh * sizeof(*req));
    I40eAdminQueueDescriptor guestdesc;
    uint32_t datalen;
    void *data = vdev->admin_queue +
                 (I40E_AQ_LOCATION_ARQ_DATA - I40E_AQ_LOCATION) +
                 (I40E_ARQ_DATA_LEN * arqh);
    uint64_t data_addr;

    /* Fetch guest descriptor */
    pci_dma_read(pdev, addr, &guestdesc, sizeof(guestdesc));
    datalen = MIN(I40E_ARQ_DATA_LEN, guestdesc.datalen);
    data_addr = guestdesc.params.external.addr_high;
    data_addr <<= 32;
    data_addr |= guestdesc.params.external.addr_low;

    /* Copy command descriptor into guest */
    pci_dma_write(pdev, addr, req, sizeof(*req));
    /* Copy data payload */
    pci_dma_write(pdev, data_addr, data, datalen);

    /* Move the head one ahead */
    vfio_i40e_aq_inc(vdev, I40E_VF_ARQH1);
}

/* Process one Admin Receive Queue Command from the device */
static void vfio_i40e_arq_process_one(VFIOI40EDevice *vdev, int hwarqh)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    char *name;
    int arq_offset = (I40E_AQ_LOCATION_ARQ - I40E_AQ_LOCATION);
    volatile I40eAdminQueueDescriptor *arq = vdev->admin_queue + arq_offset;
    volatile I40eAdminQueueDescriptor *arq_cur = &arq[hwarqh];
    I40eAdminQueueDescriptor desc;

    /* Read request */
    desc = *arq_cur;

    if (debug_i40evf) {
        name = g_strdup_printf("ARQ request %#x", hwarqh);
        vfio_i40e_print_aq_cmd(pdev, &desc, name);
        g_free(name);
    }

    if (vdev->arq_ignore) {
        /* Internal ARQ packet, ignore it */
        vdev->arq_ignore--;
    } else if (vdev->arq_fetch_vsi_id) {
        /* Packet containing our vsi id - use it to update our status */
        vfio_i40evf_arq_set_vsi_id(vdev, pdev, &desc);
    } else {
        /* Copy command to guest */
        vfio_i40e_arq_recv(vdev, &desc);
    }

    /* Make record available for reuse */
    arq_cur->flags = I40E_AQ_FLAG_BUF;
    arq_cur->datalen = I40E_ARQ_DATA_LEN;

    /* Remember that we're done with the command */
    vdev->arq_last = vfio_i40evf_r32(vdev, I40E_VF_ARQH1);
    vfio_i40evf_w32(vdev, I40E_VF_ARQT1,
                    (vdev->arq_last + 1) & (vdev->aq_len - 1));
}

static void vfio_i40e_aq_update(VFIOI40EDevice *vdev)
{

    bool arqact = vfio_i40evf_vr32(vdev, I40E_VF_ARQLEN1) &
                  I40E_VF_ARQLEN1_ARQENABLE_MASK;

    if (!vdev->arq_active && arqact) {
        /* Guest enabled ARQ, map our own admin queues now */
        vdev->arq_active = true; /* XXX unset on reset? */
        vfio_i40e_aq_map(vdev);
    }

    if (!vdev->arq_active) {
        return;
    }

    while (vfio_i40evf_vr32(vdev, I40E_VF_ATQT1) !=
           vfio_i40evf_vr32(vdev, I40E_VF_ATQH1)) {
        vfio_i40e_atq_process_one(vdev, vfio_i40evf_vr32(vdev, I40E_VF_ATQH1));
    }

    while (vdev->arq_last != vfio_i40evf_r32(vdev, I40E_VF_ARQH1)) {
        vfio_i40e_arq_process_one(vdev, vdev->arq_last);
    }
}

/************ mmio debug **************/

static uint64_t vfio_i40evf_mmio_mem_region_read(void *opaque, hwaddr addr,
                                               unsigned size)
{
    VFIOI40EDevice *vdev = opaque;
    uint32_t val;

    DPRINTF(" -> %s %#x", vfio_i40evf_reg_name(addr), size);
    assert(size == 4);
    val = vfio_i40evf_r32(vdev, addr);
    DPRINTF(" -> val = %#x", val);
    return val;
}

static void vfio_i40evf_mmio_mem_region_write(void *opaque, hwaddr addr,
                                              uint64_t data, unsigned size)
{
    VFIOI40EDevice *vdev = opaque;

    DPRINTF(" -> %s %#x %#lx", vfio_i40evf_reg_name(addr), size, data);
    assert(size == 4);
    vfio_i40evf_w32(vdev, addr, data);
}

const MemoryRegionOps vfio_i40evf_mmio_mem_region_ops = {
    .read = vfio_i40evf_mmio_mem_region_read,
    .write = vfio_i40evf_mmio_mem_region_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/************ end of mmio debug **************/

static uint64_t vfio_i40evf_aq_mmio_mem_region_read(void *opaque,
                                                    hwaddr subaddr,
                                                    unsigned size)
{
    hwaddr addr = subaddr + I40E_VF_ARQBAH1;
    VFIOI40EDevice *vdev = opaque;
    uint32_t val;

    DPRINTF(" -> %s %#x", vfio_i40evf_reg_name(addr), size);
    assert(size == 4);

    switch (addr) {
    case I40E_VF_ARQBAH1:
    case I40E_VF_ARQBAL1:
    case I40E_VF_ARQH1:
    case I40E_VF_ARQLEN1:
    case I40E_VF_ARQT1:
    case I40E_VF_ATQBAH1:
    case I40E_VF_ATQBAL1:
    case I40E_VF_ATQH1:
    case I40E_VF_ATQLEN1:
    case I40E_VF_ATQT1:
        val = vfio_i40evf_vr32(vdev, addr);
        break;
    default:
        val = vfio_i40evf_r32(vdev, addr);
        break;
    }
    DPRINTF(" -> val = %#x", val);
    return val;
}

static void vfio_i40evf_aq_mmio_mem_region_write(void *opaque, hwaddr subaddr,
                                            uint64_t data, unsigned size)
{
    hwaddr addr = subaddr + I40E_VF_ARQBAH1;
    VFIOI40EDevice *vdev = opaque;

    DPRINTF(" -> %s %#x %#lx", vfio_i40evf_reg_name(addr), size, data);
    assert(size == 4);

    switch (addr) {
    case I40E_VF_ARQBAH1:
    case I40E_VF_ARQBAL1:
    case I40E_VF_ARQH1:
    case I40E_VF_ARQLEN1:
    case I40E_VF_ARQT1:
    case I40E_VF_ATQBAH1:
    case I40E_VF_ATQBAL1:
    case I40E_VF_ATQH1:
    case I40E_VF_ATQT1:
    case I40E_VF_ATQLEN1:
        vfio_i40evf_vw32(vdev, addr, data);
        vfio_i40e_aq_update(vdev);
        break;
    default:
        vfio_i40evf_w32(vdev, addr, data);
        break;
    }
}

const MemoryRegionOps vfio_i40evf_aq_mmio_mem_region_ops = {
    .read = vfio_i40evf_aq_mmio_mem_region_read,
    .write = vfio_i40evf_aq_mmio_mem_region_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const uint32_t vfio_i40evf_migration_reg_list[] = {
    I40E_VFQF_HKEY(0), I40E_VFQF_HKEY(1), I40E_VFQF_HKEY(2), I40E_VFQF_HKEY(3),
    I40E_VFQF_HKEY(4), I40E_VFQF_HKEY(5), I40E_VFQF_HKEY(6), I40E_VFQF_HKEY(7),
    I40E_VFQF_HKEY(8), I40E_VFQF_HKEY(9), I40E_VFQF_HKEY(10),
    I40E_VFQF_HKEY(11), I40E_VFQF_HKEY(12),

    I40E_VFQF_HENA(0), I40E_VFQF_HENA(1),

    I40E_VFQF_HLUT(0), I40E_VFQF_HLUT(1), I40E_VFQF_HLUT(2), I40E_VFQF_HLUT(3),
    I40E_VFQF_HLUT(4), I40E_VFQF_HLUT(5), I40E_VFQF_HLUT(6), I40E_VFQF_HLUT(7),
    I40E_VFQF_HLUT(8), I40E_VFQF_HLUT(9), I40E_VFQF_HLUT(10),
    I40E_VFQF_HLUT(11), I40E_VFQF_HLUT(12), I40E_VFQF_HLUT(13),
    I40E_VFQF_HLUT(14), I40E_VFQF_HLUT(15),

    I40E_QTX_TAIL1(0), I40E_QTX_TAIL1(1), I40E_QTX_TAIL1(2), I40E_QTX_TAIL1(3),
    I40E_QTX_TAIL1(4), I40E_QTX_TAIL1(5), I40E_QTX_TAIL1(6), I40E_QTX_TAIL1(7),
    I40E_QTX_TAIL1(8), I40E_QTX_TAIL1(9), I40E_QTX_TAIL1(10),
    I40E_QTX_TAIL1(11), I40E_QTX_TAIL1(12), I40E_QTX_TAIL1(13),
    I40E_QTX_TAIL1(14), I40E_QTX_TAIL1(15),

    I40E_QRX_TAIL1(0), I40E_QRX_TAIL1(1), I40E_QRX_TAIL1(2), I40E_QRX_TAIL1(3),
    I40E_QRX_TAIL1(4), I40E_QRX_TAIL1(5), I40E_QRX_TAIL1(6), I40E_QRX_TAIL1(7),
    I40E_QRX_TAIL1(8), I40E_QRX_TAIL1(9), I40E_QRX_TAIL1(10),
    I40E_QRX_TAIL1(11), I40E_QRX_TAIL1(12), I40E_QRX_TAIL1(13),
    I40E_QRX_TAIL1(14), I40E_QRX_TAIL1(15),

    I40E_VFINT_DYN_CTL01, I40E_VFINT_ICR0_ENA1,
};

static void vfio_i40evf_start_migration(const MigrationParams *params,
                                        void *opaque)
{
    VFIOI40EDevice *vdev = opaque;
    void *data = vdev->admin_queue +
                 (I40E_AQ_LOCATION_ATQ_DATA - I40E_AQ_LOCATION);
    volatile struct i40e_virtchnl_queue_select *mydata = data;
    I40eAdminQueueDescriptor desc = {
        .flags = I40E_AQ_FLAG_SI | I40E_AQ_FLAG_BUF | I40E_AQ_FLAG_RD,
        .opcode = I40E_AQC_OPC_SEND_MSG_TO_PF,
        .datalen = sizeof(struct i40e_virtchnl_queue_select),
        .cookie_high = I40E_VIRTCHNL_OP_DISABLE_QUEUES,
        .params.external.addr_high = I40E_AQ_LOCATION_ATQ_DATA >> 32,
        .params.external.addr_low = (uint32_t)I40E_AQ_LOCATION_ATQ_DATA,
    };
    int i;

    /* Process anything that's still dangling */
    vfio_i40e_aq_update(vdev);

    /* Stop VF queues */
    *mydata = vdev->queue_select;
    vfio_i40e_atq_send(vdev, &desc, &desc);
    vdev->arq_ignore++;
    vfio_i40e_aq_update(vdev);

    /* Stop Admin Queue processing */
    if (0) { /* We still need it, no? */
        stop_store_aq(vdev, I40E_VF_ARQLEN1);
        stop_store_aq(vdev, I40E_VF_ATQLEN1);
    }

    /* Read registers */
    for (i = 0; i < (ARRAY_SIZE(vfio_i40evf_migration_reg_list)); i++) {
        int reg = vfio_i40evf_migration_reg_list[i];

        vfio_i40evf_vw32(vdev, reg, vfio_i40evf_r32(vdev, reg));
    }
}

static int vfio_i40evf_load(void *opaque, int version_id);
static void vfio_i40evf_cancel_migration(void *opaque)
{
    /* Start up VF again, same as incoming migration */
    vfio_i40evf_load(opaque, 1);
}

static bool vfio_i40evf_is_active(void *opaque)
{
    /* We transmit all state via VMSD */
    return false;
}

static SaveVMHandlers savevm_handlers = {
    .set_params = vfio_i40evf_start_migration,
    .cancel = vfio_i40evf_cancel_migration,
    .is_active = vfio_i40evf_is_active,
};

static uint16_t irq_map_size(struct i40e_virtchnl_irq_map_info *map)
{
    uint16_t size = sizeof(*map);

    /* Remove all optional array items from the size */
    size -= sizeof(map->vecmap);
    /* And add the ones actually in use again */
    size += sizeof(map->vecmap[0]) * (map->num_vectors + 1);

    return size;
}

static uint16_t ether_list_size(struct i40e_virtchnl_ether_addr_list *list)
{
    uint16_t size = sizeof(*list);

    /* Remove all optional array items from the size */
    size -= sizeof(list->list);
    /* And add the ones actually in use again */
    size += sizeof(list->list[0]) * (list->num_elements + 1);

    return size;
}

static uint16_t vsi_config_size(struct i40e_virtchnl_vsi_queue_config_info *vsi)
{
    uint16_t size = sizeof(*vsi);

    /* Remove all optional array items from the size */
    size -= sizeof(vsi->qpair);
    /* And add the ones actually in use again */
    size += sizeof(vsi->qpair[0]) * (vsi->num_queue_pairs + 1);

    return size;
}

static int vfio_i40evf_load(void *opaque, int version_id)
{
    VFIOI40EDevice *vdev = opaque;
    PCIDevice *pdev = PCI_DEVICE(vdev);
    void *data = vdev->admin_queue +
                 (I40E_AQ_LOCATION_ATQ_DATA - I40E_AQ_LOCATION);
    I40eAdminQueueDescriptor desc = {
        .flags = I40E_AQ_FLAG_SI | I40E_AQ_FLAG_BUF | I40E_AQ_FLAG_RD,
        .opcode = I40E_AQC_OPC_SEND_MSG_TO_PF,
        .datalen = sizeof(struct i40e_virtchnl_queue_select),
        .cookie_high = I40E_VIRTCHNL_OP_DISABLE_QUEUES,
        .params.external.addr_high = I40E_AQ_LOCATION_ATQ_DATA >> 32,
        .params.external.addr_low = (uint32_t)I40E_AQ_LOCATION_ATQ_DATA,
    };
    I40eAdminQueueDescriptor resp;
    int i, cmd;

    /* Restore config space */
    cmd = pdev->config_read(pdev, PCI_COMMAND, 2);
    cmd |= (PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER );
    pdev->config_write(pdev, PCI_COMMAND, cmd, 2);

    /* XXX Hack to make RX work. Somehow the hashing is broken. Still need to
           figure out why. Disable filtering for now. */
    vfio_i40evf_vw32(vdev, I40E_VFQF_HENA(1), 0x80000000);

    /* Restore Registers */
    for (i = 0; i < (ARRAY_SIZE(vfio_i40evf_migration_reg_list)); i++) {
        int reg = vfio_i40evf_migration_reg_list[i];

        vfio_i40evf_w32(vdev, reg, vfio_i40evf_vr32(vdev, reg));
    }

    /* Set up admin queue */
    vdev->arq_active = true;
    vfio_i40e_aq_map(vdev);

    /* Enable MSI */
    if (msi_enabled(pdev)) {
        DPRINTF(" Enabling MSI");
        vfio_enable_msi(&vdev->parent_obj);
    }
    if (msix_enabled(pdev)) {
        DPRINTF(" Enabling MSI-X");
        vfio_enable_msix(&vdev->parent_obj);
    }

    /* Make VF happy by asking it a few questions */
    {
        volatile struct i40e_virtchnl_version_info *mydata = data;
        mydata->major = 1;
        mydata->minor = 0;
        desc.cookie_high = I40E_VIRTCHNL_OP_VERSION;
        desc.datalen = sizeof(struct i40e_virtchnl_version_info);
        vfio_i40e_print_aq_cmd(pdev, &desc, "ATQ GET VERSION");
        vfio_i40e_atq_send(vdev, &desc, &resp);
        vfio_i40e_print_aq_cmd(pdev, &resp, "ATQ GET VERSION response");
        vdev->arq_ignore++;
        vfio_i40e_aq_update(vdev);
    }

    {
        I40eAdminQueueDescriptor mydesc = desc;
        mydesc.cookie_high = I40E_VIRTCHNL_OP_GET_VF_RESOURCES;
        mydesc.datalen = 0;
        mydesc.flags = I40E_AQ_FLAG_SI;
        vfio_i40e_print_aq_cmd(pdev, &mydesc, "ATQ GET VF RESOURCES");
        vfio_i40e_atq_send(vdev, &mydesc, &resp);
        vfio_i40e_print_aq_cmd(pdev, &resp, "ATQ GET VF RESOURCES response");
        vdev->arq_fetch_vsi_id = true;
        vfio_i40e_aq_update(vdev);
    }

    /* Restore VF configuration */
    {
        desc.cookie_high = I40E_VIRTCHNL_OP_CONFIG_IRQ_MAP;
        desc.datalen = MIN(irq_map_size(&vdev->irq_map), sizeof(vdev->irq_map));
        memcpy(data, &vdev->irq_map, desc.datalen);
        vfio_i40e_print_aq_cmd(pdev, &desc, "ATQ IRQ MAP");
        vfio_i40e_atq_send(vdev, &desc, &resp);
        vfio_i40e_print_aq_cmd(pdev, &resp, "ATQ IRQ MAP response");
        vdev->arq_ignore++;
        vfio_i40e_aq_update(vdev);
    }

    {
        desc.cookie_high = I40E_VIRTCHNL_OP_DEL_ETHER_ADDRESS;
        desc.datalen = MIN(ether_list_size(&vdev->addr), sizeof(vdev->addr));
        memcpy(data, &vdev->addr, desc.datalen);
        vfio_i40e_print_aq_cmd(pdev, &desc, "ATQ DEL ETHER ADDRESS");
        vfio_i40e_atq_send(vdev, &desc, &resp);
        vfio_i40e_print_aq_cmd(pdev, &resp, "ATQ DEL ETHER ADDRESS response");
        vdev->arq_ignore++;
        vfio_i40e_aq_update(vdev);
    }

    {
        desc.cookie_high = I40E_VIRTCHNL_OP_ADD_ETHER_ADDRESS;
        desc.datalen = MIN(ether_list_size(&vdev->addr), sizeof(vdev->addr));
        memcpy(data, &vdev->addr, desc.datalen);
        vfio_i40e_print_aq_cmd(pdev, &desc, "ATQ ETHER ADDRESS");
        vfio_i40e_atq_send(vdev, &desc, &resp);
        vfio_i40e_print_aq_cmd(pdev, &resp, "ATQ ETHER ADDRESS response");
        vdev->arq_ignore++;
        vfio_i40e_aq_update(vdev);
    }

    {
        desc.cookie_high = I40E_VIRTCHNL_OP_CONFIG_VSI_QUEUES;
        desc.datalen = MIN(vsi_config_size(&vdev->vsi_config),
                           sizeof(vdev->vsi_config));
        memcpy(data, &vdev->vsi_config, desc.datalen);
        vfio_i40e_print_aq_cmd(pdev, &desc, "ATQ VSI QUEUES");
        vfio_i40e_atq_send(vdev, &desc, &resp);
        vfio_i40e_print_aq_cmd(pdev, &resp, "ATQ VSI QUEUES response");
        vdev->arq_ignore++;
        vfio_i40e_aq_update(vdev);
    }

    {
        volatile struct i40e_virtchnl_queue_select *mydata = data;
        *mydata = vdev->queue_select;
        desc.cookie_high = I40E_VIRTCHNL_OP_ENABLE_QUEUES;
        desc.datalen = sizeof(*mydata);
        vfio_i40e_print_aq_cmd(pdev, &desc, "ATQ ENABLE QUEUES");
        vfio_i40e_atq_send(vdev, &desc, &resp);
        vfio_i40e_print_aq_cmd(pdev, &resp, "ATQ ENABLE QUEUES response");
        vdev->arq_ignore++;
        vfio_i40e_aq_update(vdev);
    }

    DPRINTF("");
    return 0;
}

static void vfio_i40evf_save(void *opaque)
{
    VFIOI40EDevice *vdev = opaque;

    DPRINTF("");
    I40eAdminQueueDescriptor desc = {
        .flags = I40E_AQ_FLAG_SI | I40E_AQ_FLAG_BUF | I40E_AQ_FLAG_RD,
        .opcode = I40E_AQC_OPC_SEND_MSG_TO_PF,
        .cookie_high = I40E_VIRTCHNL_OP_RESET_VF,
    };

    /* Destroy real VF, freeing up the mac address */
    vfio_i40e_atq_send(vdev, &desc, &desc);
}

static const VMStateDescription vmstate_vector_map = {
    .name = "vector_map",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(vsi_id, struct i40e_virtchnl_vector_map),
        VMSTATE_UINT16(vector_id, struct i40e_virtchnl_vector_map),
        VMSTATE_UINT16(rxq_map, struct i40e_virtchnl_vector_map),
        VMSTATE_UINT16(txq_map, struct i40e_virtchnl_vector_map),
        VMSTATE_UINT16(rxitr_idx, struct i40e_virtchnl_vector_map),
        VMSTATE_UINT16(txitr_idx, struct i40e_virtchnl_vector_map),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_irq_map_info = {
    .name = "irq_map_info",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(num_vectors, struct i40e_virtchnl_irq_map_info),
        VMSTATE_STRUCT_VARRAY_UINT16(vecmap, struct i40e_virtchnl_irq_map_info,
                                     num_vectors, 1, vmstate_vector_map,
                                     struct i40e_virtchnl_vector_map),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_ether_addr = {
    .name = "ether_addr",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_ARRAY(addr, struct i40e_virtchnl_ether_addr, 6),
        VMSTATE_UINT8_ARRAY(pad, struct i40e_virtchnl_ether_addr, 2),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_ether_addr_list = {
    .name = "ether_addr_list",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(vsi_id, struct i40e_virtchnl_ether_addr_list),
        VMSTATE_UINT16(num_elements, struct i40e_virtchnl_ether_addr_list),
        VMSTATE_STRUCT_VARRAY_UINT16(list, struct i40e_virtchnl_ether_addr_list,
                                     num_elements, 1, vmstate_ether_addr,
                                     struct i40e_virtchnl_ether_addr),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_queue_pair = {
    .name = "queue_pair_info",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(txq.vsi_id, struct i40e_virtchnl_queue_pair_info),
        VMSTATE_UINT16(txq.queue_id, struct i40e_virtchnl_queue_pair_info),
        VMSTATE_UINT16(txq.ring_len, struct i40e_virtchnl_queue_pair_info),
        VMSTATE_UINT16(txq.headwb_enabled,
                       struct i40e_virtchnl_queue_pair_info),
        VMSTATE_UINT64(txq.dma_ring_addr, struct i40e_virtchnl_queue_pair_info),
        VMSTATE_UINT64(txq.dma_headwb_addr,
                       struct i40e_virtchnl_queue_pair_info),
        VMSTATE_UINT16(rxq.vsi_id, struct i40e_virtchnl_queue_pair_info),
        VMSTATE_UINT16(rxq.queue_id, struct i40e_virtchnl_queue_pair_info),
        VMSTATE_UINT32(rxq.ring_len, struct i40e_virtchnl_queue_pair_info),
        VMSTATE_UINT16(rxq.hdr_size, struct i40e_virtchnl_queue_pair_info),
        VMSTATE_UINT16(rxq.splithdr_enabled,
                       struct i40e_virtchnl_queue_pair_info),
        VMSTATE_UINT32(rxq.databuffer_size,
                       struct i40e_virtchnl_queue_pair_info),
        VMSTATE_UINT32(rxq.max_pkt_size, struct i40e_virtchnl_queue_pair_info),
        VMSTATE_UINT64(rxq.dma_ring_addr, struct i40e_virtchnl_queue_pair_info),
        VMSTATE_UINT32(rxq.rx_split_pos, struct i40e_virtchnl_queue_pair_info),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_vsi_config = {
    .name = "vsi_config",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(vsi_id, struct i40e_virtchnl_vsi_queue_config_info),
        VMSTATE_UINT16(num_queue_pairs,
                       struct i40e_virtchnl_vsi_queue_config_info),
        VMSTATE_STRUCT_VARRAY_UINT16(qpair,
                                     struct i40e_virtchnl_vsi_queue_config_info,
                                     num_queue_pairs, 1, vmstate_queue_pair,
                                     struct i40e_virtchnl_queue_pair_info),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_queue_select = {
    .name = "queue_select",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(vsi_id, struct i40e_virtchnl_queue_select),
        VMSTATE_UINT32(rx_queues, struct i40e_virtchnl_queue_select),
        VMSTATE_UINT32(tx_queues, struct i40e_virtchnl_queue_select),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vfio_i40evf_vmstate = {
    .name = "vfio-i40evf",
    .post_load = vfio_i40evf_load,
    .pre_save = vfio_i40evf_save,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCIE_DEVICE(parent_obj.pdev, VFIOI40EDevice),
        VMSTATE_MSIX(parent_obj.pdev, VFIOI40EDevice),
        VMSTATE_UINT32_ARRAY(regs, VFIOI40EDevice, (64 * 1024) / 4),
        VMSTATE_STRUCT(irq_map, VFIOI40EDevice, 1, vmstate_irq_map_info,
                       struct i40e_virtchnl_irq_map_info),
        VMSTATE_STRUCT(addr, VFIOI40EDevice, 1, vmstate_ether_addr_list,
                       struct i40e_virtchnl_ether_addr_list),
        VMSTATE_STRUCT(vsi_config, VFIOI40EDevice, 1, vmstate_vsi_config,
                       struct i40e_virtchnl_vsi_queue_config_info),
        VMSTATE_STRUCT(queue_select, VFIOI40EDevice, 1, vmstate_queue_select,
                       struct i40e_virtchnl_queue_select),
        VMSTATE_END_OF_LIST()
    }
};

static void vfio_i40e_map_aq_data(VFIOI40EDevice *vdev)
{
    uintptr_t page_size = getpagesize();
    PCIDevice *pci_dev = PCI_DEVICE(vdev);
    Object *obj = OBJECT(vdev);
    AddressSpace *as = pci_device_iommu_address_space(pci_dev);
    MemoryRegion *mr = as->root;
    int len = 64 * 1024;

    vdev->admin_queue = qemu_memalign(page_size, len);

    memory_region_init_ram_ptr(&vdev->aq_data_mem, obj, "i40e aq data", len,
                               vdev->admin_queue);
    memory_region_add_subregion_overlap(mr, I40E_AQ_LOCATION,
                                        &vdev->aq_data_mem, 30);
}

static void vfio_i40evf_instance_init(Object *obj)
{
    VFIOI40EDevice *vdev = VFIO_I40EVF(obj);

    register_savevm_live(NULL, "i40evf", -1, 1, &savevm_handlers, vdev);

    DPRINTF("");
}

static int vfio_i40evf_initfn(PCIDevice *dev)
{
    VFIOI40EDeviceClass *vk = VFIO_I40EVF_GET_CLASS(dev);
    VFIOI40EDevice *vdev = VFIO_I40EVF(dev);
    VFIOPCIDevice *vpdev = VFIO_PCI(dev);
    VFIOBAR *bar = &vpdev->bars[0];

    vk->parent_init(dev);
    DPRINTF("");
    vdev->aq_len = 32;

    /* Trap and show every MMIO access */
    if (debug_i40evf) {
        memory_region_init_io(&vdev->mmio_mem, OBJECT(dev),
                              &vfio_i40evf_mmio_mem_region_ops,
                              vdev, "i40evf config", 64 * 1024);
        memory_region_add_subregion_overlap(&bar->region.mem, 0,
                                            &vdev->mmio_mem, 29);
    }

    /* Override the Admin Queue configuration so that we can inject our
     * own handlers */
    memory_region_init_io(&vdev->aq_mmio_mem, OBJECT(dev),
                          &vfio_i40evf_aq_mmio_mem_region_ops,
                          vdev, "i40evf AQ config",
                          I40E_VFGEN_RSTAT - I40E_VF_ARQBAH1);
    memory_region_add_subregion_overlap(&bar->region.mem, I40E_VF_ARQBAH1,
                                        &vdev->aq_mmio_mem, 30);
    vfio_i40e_map_aq_data(vdev);

    return 0;
}

static void vfio_i40evf_dev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    VFIOI40EDeviceClass *vk = VFIO_I40EVF_CLASS(klass);

    dc->vmsd = &vfio_i40evf_vmstate;
    dc->desc = "VFIO-based i40evf card";
    vk->parent_init = k->init;
    k->init = vfio_i40evf_initfn;
}

static const TypeInfo vfio_i40evf_dev_info = {
    .name = "vfio-i40e",
    .parent = "vfio-pci",
    .instance_size = sizeof(VFIOI40EDevice),
    .class_init = vfio_i40evf_dev_class_init,
    .instance_init = vfio_i40evf_instance_init,
};

static void register_vfio_i40evf_dev_type(void)
{
    type_register_static(&vfio_i40evf_dev_info);
}

type_init(register_vfio_i40evf_dev_type)
