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

//#define DEBUG_I40EVF

#ifdef DEBUG_I40EVF
static const int debug_i40evf = 1;
#else
static const int debug_i40evf = 0;
#endif

#undef DPRINTF /* from vfio-common */
#define DPRINTF(fmt, ...) do { \
        if (debug_i40evf) { \
            printf(fmt , ## __VA_ARGS__); \
        } \
    } while (0)

static const char *vfio_i40evf_v_opcode_name(int reg)
{
    static char unk[] = "Unknown v_opcode 0x00000000";

    switch (reg) {
    case I40E_VIRTCHNL_OP_UNKNOWN: return "I40E_VIRTCHNL_OP_UNKNOWN";
    case I40E_VIRTCHNL_OP_VERSION: return "I40E_VIRTCHNL_OP_VERSION";
    case I40E_VIRTCHNL_OP_RESET_VF: return "I40E_VIRTCHNL_OP_RESET_VF";
    case I40E_VIRTCHNL_OP_GET_VF_RESOURCES: return "I40E_VIRTCHNL_OP_GET_VF_RESOURCES";
    case I40E_VIRTCHNL_OP_CONFIG_TX_QUEUE: return "I40E_VIRTCHNL_OP_CONFIG_TX_QUEUE";
    case I40E_VIRTCHNL_OP_CONFIG_RX_QUEUE: return "I40E_VIRTCHNL_OP_CONFIG_RX_QUEUE";
    case I40E_VIRTCHNL_OP_CONFIG_VSI_QUEUES: return "I40E_VIRTCHNL_OP_CONFIG_VSI_QUEUES";
    case I40E_VIRTCHNL_OP_CONFIG_IRQ_MAP: return "I40E_VIRTCHNL_OP_CONFIG_IRQ_MAP";
    case I40E_VIRTCHNL_OP_ENABLE_QUEUES: return "I40E_VIRTCHNL_OP_ENABLE_QUEUES";
    case I40E_VIRTCHNL_OP_DISABLE_QUEUES: return "I40E_VIRTCHNL_OP_DISABLE_QUEUES";
    case I40E_VIRTCHNL_OP_ADD_ETHER_ADDRESS: return "I40E_VIRTCHNL_OP_ADD_ETHER_ADDRESS";
    case I40E_VIRTCHNL_OP_DEL_ETHER_ADDRESS: return "I40E_VIRTCHNL_OP_DEL_ETHER_ADDRESS";
    case I40E_VIRTCHNL_OP_ADD_VLAN: return "I40E_VIRTCHNL_OP_ADD_VLAN";
    case I40E_VIRTCHNL_OP_DEL_VLAN: return "I40E_VIRTCHNL_OP_DEL_VLAN";
    case I40E_VIRTCHNL_OP_CONFIG_PROMISCUOUS_MODE: return "I40E_VIRTCHNL_OP_CONFIG_PROMISCUOUS_MODE";
    case I40E_VIRTCHNL_OP_GET_STATS: return "I40E_VIRTCHNL_OP_GET_STATS";
    case I40E_VIRTCHNL_OP_FCOE: return "I40E_VIRTCHNL_OP_FCOE";
    case I40E_VIRTCHNL_OP_EVENT: return "I40E_VIRTCHNL_OP_EVENT";
    case I40E_VIRTCHNL_OP_CONFIG_RSS: return "I40E_VIRTCHNL_OP_CONFIG_RSS";
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
#if 0
    case I40E_VFMSIX_PBA: return "I40E_VFMSIX_PBA";
    case I40E_VFMSIX_TADD(0): return "I40E_VFMSIX_TADD(0)";
    case I40E_VFMSIX_TADD(1): return "I40E_VFMSIX_TADD(1)";
    case I40E_VFMSIX_TADD(2): return "I40E_VFMSIX_TADD(2)";
    case I40E_VFMSIX_TADD(3): return "I40E_VFMSIX_TADD(3)";
    case I40E_VFMSIX_TADD(4): return "I40E_VFMSIX_TADD(4)";
    case I40E_VFMSIX_TADD(5): return "I40E_VFMSIX_TADD(5)";
    case I40E_VFMSIX_TADD(6): return "I40E_VFMSIX_TADD(6)";
    case I40E_VFMSIX_TADD(7): return "I40E_VFMSIX_TADD(7)";
    case I40E_VFMSIX_TADD(8): return "I40E_VFMSIX_TADD(8)";
    case I40E_VFMSIX_TADD(9): return "I40E_VFMSIX_TADD(9)";
    case I40E_VFMSIX_TADD(10): return "I40E_VFMSIX_TADD(10)";
    case I40E_VFMSIX_TADD(11): return "I40E_VFMSIX_TADD(11)";
    case I40E_VFMSIX_TADD(12): return "I40E_VFMSIX_TADD(12)";
    case I40E_VFMSIX_TADD(13): return "I40E_VFMSIX_TADD(13)";
    case I40E_VFMSIX_TADD(14): return "I40E_VFMSIX_TADD(14)";
    case I40E_VFMSIX_TADD(15): return "I40E_VFMSIX_TADD(15)";
    case I40E_VFMSIX_TMSG(0): return "I40E_VFMSIX_TMSG(0)";
    case I40E_VFMSIX_TMSG(1): return "I40E_VFMSIX_TMSG(1)";
    case I40E_VFMSIX_TMSG(2): return "I40E_VFMSIX_TMSG(2)";
    case I40E_VFMSIX_TMSG(3): return "I40E_VFMSIX_TMSG(3)";
    case I40E_VFMSIX_TMSG(4): return "I40E_VFMSIX_TMSG(4)";
    case I40E_VFMSIX_TMSG(5): return "I40E_VFMSIX_TMSG(5)";
    case I40E_VFMSIX_TMSG(6): return "I40E_VFMSIX_TMSG(6)";
    case I40E_VFMSIX_TMSG(7): return "I40E_VFMSIX_TMSG(7)";
    case I40E_VFMSIX_TMSG(8): return "I40E_VFMSIX_TMSG(8)";
    case I40E_VFMSIX_TMSG(9): return "I40E_VFMSIX_TMSG(9)";
    case I40E_VFMSIX_TMSG(10): return "I40E_VFMSIX_TMSG(10)";
    case I40E_VFMSIX_TMSG(11): return "I40E_VFMSIX_TMSG(11)";
    case I40E_VFMSIX_TMSG(12): return "I40E_VFMSIX_TMSG(12)";
    case I40E_VFMSIX_TMSG(13): return "I40E_VFMSIX_TMSG(13)";
    case I40E_VFMSIX_TMSG(14): return "I40E_VFMSIX_TMSG(14)";
    case I40E_VFMSIX_TMSG(15): return "I40E_VFMSIX_TMSG(15)";
    case I40E_VFMSIX_TUADD(0): return "I40E_VFMSIX_TUADD(0)";
    case I40E_VFMSIX_TUADD(1): return "I40E_VFMSIX_TUADD(1)";
    case I40E_VFMSIX_TUADD(2): return "I40E_VFMSIX_TUADD(2)";
    case I40E_VFMSIX_TUADD(3): return "I40E_VFMSIX_TUADD(3)";
    case I40E_VFMSIX_TUADD(4): return "I40E_VFMSIX_TUADD(4)";
    case I40E_VFMSIX_TUADD(5): return "I40E_VFMSIX_TUADD(5)";
    case I40E_VFMSIX_TUADD(6): return "I40E_VFMSIX_TUADD(6)";
    case I40E_VFMSIX_TUADD(7): return "I40E_VFMSIX_TUADD(7)";
    case I40E_VFMSIX_TUADD(8): return "I40E_VFMSIX_TUADD(8)";
    case I40E_VFMSIX_TUADD(9): return "I40E_VFMSIX_TUADD(9)";
    case I40E_VFMSIX_TUADD(10): return "I40E_VFMSIX_TUADD(10)";
    case I40E_VFMSIX_TUADD(11): return "I40E_VFMSIX_TUADD(11)";
    case I40E_VFMSIX_TUADD(12): return "I40E_VFMSIX_TUADD(12)";
    case I40E_VFMSIX_TUADD(13): return "I40E_VFMSIX_TUADD(13)";
    case I40E_VFMSIX_TUADD(14): return "I40E_VFMSIX_TUADD(14)";
    case I40E_VFMSIX_TUADD(15): return "I40E_VFMSIX_TUADD(15)";
    case I40E_VFMSIX_TVCTRL(0): return "I40E_VFMSIX_TVCTRL(0)";
    case I40E_VFMSIX_TVCTRL(1): return "I40E_VFMSIX_TVCTRL(1)";
    case I40E_VFMSIX_TVCTRL(2): return "I40E_VFMSIX_TVCTRL(2)";
    case I40E_VFMSIX_TVCTRL(3): return "I40E_VFMSIX_TVCTRL(3)";
    case I40E_VFMSIX_TVCTRL(4): return "I40E_VFMSIX_TVCTRL(4)";
    case I40E_VFMSIX_TVCTRL(5): return "I40E_VFMSIX_TVCTRL(5)";
    case I40E_VFMSIX_TVCTRL(6): return "I40E_VFMSIX_TVCTRL(6)";
    case I40E_VFMSIX_TVCTRL(7): return "I40E_VFMSIX_TVCTRL(7)";
    case I40E_VFMSIX_TVCTRL(8): return "I40E_VFMSIX_TVCTRL(8)";
    case I40E_VFMSIX_TVCTRL(9): return "I40E_VFMSIX_TVCTRL(9)";
    case I40E_VFMSIX_TVCTRL(10): return "I40E_VFMSIX_TVCTRL(10)";
    case I40E_VFMSIX_TVCTRL(11): return "I40E_VFMSIX_TVCTRL(11)";
    case I40E_VFMSIX_TVCTRL(12): return "I40E_VFMSIX_TVCTRL(12)";
    case I40E_VFMSIX_TVCTRL(13): return "I40E_VFMSIX_TVCTRL(13)";
    case I40E_VFMSIX_TVCTRL(14): return "I40E_VFMSIX_TVCTRL(14)";
    case I40E_VFMSIX_TVCTRL(15): return "I40E_VFMSIX_TVCTRL(15)";
#endif
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
    DPRINTF("XXX %s:%d -> reg=%s val=%#lx\n", __func__, __LINE__, vfio_i40evf_reg_name(reg), val);

    return val;
}

/* Write a real (hardware visible) register */
static void vfio_i40evf_w32(VFIOI40EDevice *vdev, int reg, uint32_t val)
{
    VFIOPCIDevice *vpdev = VFIO_PCI(vdev);
    VFIOBAR *bar = &vpdev->bars[0];
    volatile uint32_t *p = bar->region.mmap + reg;

    *p = val;
    DPRINTF("XXX %s:%d -> reg=%s val=%#x\n", __func__, __LINE__, vfio_i40evf_reg_name(reg), val);
}

/* Read a virtual (guest visible) register */
static uint32_t vfio_i40evf_vr32(VFIOI40EDevice *vdev, int reg)
{
    uint64_t val = vdev->regs[reg / 4];
    DPRINTF("XXX %s:%d -> reg=%s val=%#lx\n", __func__, __LINE__, vfio_i40evf_reg_name(reg), val);
    return val;
}

/* Write a virtual (guest visible) register */
static void vfio_i40evf_vw32(VFIOI40EDevice *vdev, int reg, uint32_t val)
{
    DPRINTF("XXX %s:%d -> reg=%s val=%#x\n", __func__, __LINE__, vfio_i40evf_reg_name(reg), val);
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
    I40eAdminQueueDescriptor *arq = vdev->admin_queue + (I40E_AQ_LOCATION_ARQ - I40E_AQ_LOCATION);
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
    vfio_i40evf_w32(vdev, I40E_VF_ARQLEN1, vdev->aq_len | I40E_VF_ARQLEN1_ARQENABLE_MASK);

    vfio_i40evf_w32(vdev, I40E_VF_ATQBAH1, (I40E_AQ_LOCATION_ATQ) >> 32);
    vfio_i40evf_w32(vdev, I40E_VF_ATQBAL1, (uint32_t)I40E_AQ_LOCATION_ATQ);
    vfio_i40evf_w32(vdev, I40E_VF_ATQH1, 0);
    vfio_i40evf_w32(vdev, I40E_VF_ATQT1, 0);
    vfio_i40evf_w32(vdev, I40E_VF_ATQLEN1, vdev->aq_len | I40E_VF_ATQLEN1_ATQENABLE_MASK);

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
static void vfio_i40e_atq_send(VFIOI40EDevice *vdev,
                               I40eAdminQueueDescriptor *req,
                               I40eAdminQueueDescriptor *res)
{
    int atqt = vfio_i40evf_r32(vdev, I40E_VF_ATQT1);
    int atq_offset = (I40E_AQ_LOCATION_ATQ - I40E_AQ_LOCATION);
    volatile I40eAdminQueueDescriptor *atq = vdev->admin_queue + atq_offset;
    volatile I40eAdminQueueDescriptor *atq_cur = &atq[atqt];

DPRINTF("XXX %s:%d hware queue index = %#x\n", __func__, __LINE__, atqt);
    /* Copy command into our own queue buffer */
    *atq_cur = *req;

    /* Move the tail one ahead */
    atqt = (atqt + 1) % vdev->aq_len;
    vfio_i40evf_w32(vdev, I40E_VF_ATQT1, atqt);

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

    printf(" %s\n", name);
    printf(" |- opcode    = %#x\n", req->opcode);
    printf(" |- length    = %#x\n", req->datalen);
    printf(" |- flags     = %#x\n", req->flags);
    printf(" |- cookie hi = %#x (%s)\n", req->cookie_high, opname);
    printf(" |- cookie lo = %#x\n", req->cookie_low);
    printf(" |- retval    = %#x\n", req->retval);
    printf(" `- data addr = %#"PRIx64"\n", data_addr);

    if (req->datalen && (req->opcode == I40E_AQC_OPC_SEND_MSG_TO_PF)) {
        switch (req->cookie_high) { /* v_opcode */
        case I40E_VIRTCHNL_OP_VERSION: {
            struct i40e_virtchnl_version_info *verinfo = (void*)data;
            printf("    |- major = %#x\n", verinfo->major);
            printf("    `- minor = %#x\n", verinfo->minor);
            break;
        }
        case I40E_VIRTCHNL_OP_CONFIG_VSI_QUEUES: {
            struct i40e_virtchnl_vsi_queue_config_info *mydata = (void*)data;
            printf("    |- num_queue_pairs = %#x\n", mydata->num_queue_pairs);
            for (i = 0; i < mydata->num_queue_pairs; i++) {
                struct i40e_virtchnl_queue_pair_info *qp = &mydata->qpair[i];
                printf("    |    |- txq.vsi_id           = %#x\n", qp->txq.vsi_id);
                printf("    |    |- txq.queue_id         = %#x\n", qp->txq.queue_id);
                printf("    |    |- txq.ring_len         = %#x\n", qp->txq.ring_len);
                printf("    |    |- txq.headwb_enabled   = %#x\n", qp->txq.headwb_enabled);
                printf("    |    |- txq.dma_ring_addr    = %#"PRIx64"\n", qp->txq.dma_ring_addr);
                printf("    |    |- txq.dma_headwb_addr  = %#"PRIx64"\n", qp->txq.dma_headwb_addr);
                printf("    |    |- rxq.vsi_id           = %#x\n", qp->rxq.vsi_id);
                printf("    |    |- rxq.queue_id         = %#x\n", qp->rxq.queue_id);
                printf("    |    |- rxq.ring_len         = %#x\n", qp->rxq.ring_len);
                printf("    |    |- rxq.hdr_size         = %#x\n", qp->rxq.hdr_size);
                printf("    |    |- rxq.splithdr_enabled = %#x\n", qp->rxq.splithdr_enabled);
                printf("    |    |- rxq.databuffer_size  = %#x\n", qp->rxq.databuffer_size);
                printf("    |    |- rxq.max_pkt_size     = %#x\n", qp->rxq.max_pkt_size);
                printf("    |    |- rxq.dma_ring_addr    = %#"PRIx64"\n", qp->rxq.dma_ring_addr);
                printf("    |    |- rxq.splithdr_enabled = %#x\n", qp->rxq.splithdr_enabled);
                printf("    |    `- rxq.rx_split_pos     = %#x\n", qp->rxq.rx_split_pos);
            }
            printf("    `- vsi_id          = %#x\n", mydata->vsi_id);
            break;
        }
        case I40E_VIRTCHNL_OP_CONFIG_IRQ_MAP: {
            struct i40e_virtchnl_irq_map_info *mydata = (void*)data;
            printf("    |- num_vectors = %#x\n", mydata->num_vectors);
            for (i = 0; i < mydata->num_vectors; i++) {
                struct i40e_virtchnl_vector_map *map = &mydata->vecmap[i];
                printf("        |- vsi_id    = %#x\n", map->vsi_id);
                printf("        |- vector_id = %#x\n", map->vector_id);
                printf("        |- rxq_map   = %#x\n", map->rxq_map);
                printf("        |- txq_map   = %#x\n", map->txq_map);
                printf("        |- rxitr_idx = %#x\n", map->rxitr_idx);
                printf("        `- txitr_idx = %#x\n", map->txitr_idx);
            }
            break;
        }
        case I40E_VIRTCHNL_OP_GET_STATS:
        case I40E_VIRTCHNL_OP_ENABLE_QUEUES:
        case I40E_VIRTCHNL_OP_DISABLE_QUEUES: {
            struct i40e_virtchnl_queue_select *mydata = (void*)data;
            printf("    |- vsi_id    = %#x\n", mydata->vsi_id);
            printf("    |- pad       = %#x\n", mydata->pad);
            printf("    |- rx_queues = %#x\n", mydata->rx_queues);
            printf("    `- tx_queues = %#x\n", mydata->tx_queues);
            break;
        }
        case I40E_VIRTCHNL_OP_ADD_ETHER_ADDRESS:
        case I40E_VIRTCHNL_OP_DEL_ETHER_ADDRESS: {
            struct i40e_virtchnl_ether_addr_list *mydata = (void*)data;
            printf("    |- num_elements = %#x\n", mydata->num_elements);
            for (i = 0; i < mydata->num_elements; i++) {
                struct i40e_virtchnl_ether_addr *addr = &mydata->list[i];
                printf("    |   `- addr = %02x:%02x:%02x:%02x:%02x:%02x\n",
                    addr->addr[0], addr->addr[1], addr->addr[2], addr->addr[3],
                    addr->addr[4], addr->addr[5]);
            }
            printf("    `- vsi_id          = %#x\n", mydata->vsi_id);
            break;
        }
        default:
            printf("    `- data = ");
            for (i = 0; i < req->datalen; i++) {
                printf("%02x ", (uint8_t)data[i]);
            }
            printf("\n");
            break;
        }
    }

    if (req->datalen && (req->opcode == I40E_AQC_OPC_SEND_MSG_TO_VF)) {
        switch (req->cookie_high) { /* v_opcode */
        case I40E_VIRTCHNL_OP_VERSION: {
            struct i40e_virtchnl_version_info *verinfo = (void*)data;
            printf("    |- major = %#x\n", verinfo->major);
            printf("    `- minor = %#x\n", verinfo->minor);
            break;
        }
        case I40E_VIRTCHNL_OP_GET_VF_RESOURCES: {
            struct i40e_virtchnl_vf_resource *mydata = (void*)data;
            printf("    |- num_vsis          = %#x\n", mydata->num_vsis);
            for (i = 0; i < mydata->num_vsis; i++) {
                struct i40e_virtchnl_vsi_resource *vsi = &mydata->vsi_res[i];
                printf("    |   |- vsi_id           = %#x\n", vsi->vsi_id);
                printf("    |   |- num_queue_pairs  = %#x\n", vsi->num_queue_pairs);
                printf("    |   |- vsi_type         = %#x\n", vsi->vsi_type);
                printf("    |   |- qset_handle      = %#x\n", vsi->qset_handle);
                printf("    |   `- default_mac_addr = %02x:%02x:%02x:%02x:%02x:%02x\n",
                    vsi->default_mac_addr[0], vsi->default_mac_addr[1],
                    vsi->default_mac_addr[2], vsi->default_mac_addr[3],
                    vsi->default_mac_addr[4], vsi->default_mac_addr[5]);
            }
            printf("    |- num_queue_pairs   = %#x\n", mydata->num_queue_pairs);
            printf("    |- max_vectors       = %#x\n", mydata->max_vectors);
            printf("    |- max_mtu           = %#x\n", mydata->max_mtu);
            printf("    |- vf_offload_flags  = %#x\n", mydata->vf_offload_flags);
            printf("    |- max_fcoe_contexts = %#x\n", mydata->max_fcoe_contexts);
            printf("    `- max_fcoe_filters  = %#x\n", mydata->max_fcoe_filters);
            break;
        }
        case I40E_VIRTCHNL_OP_GET_STATS: {
            struct i40e_eth_stats *mydata = (void*)data;
            printf("    |- rx_bytes            = %#"PRIx64"\n", mydata->rx_bytes);
            printf("    |- rx_unicast          = %#"PRIx64"\n", mydata->rx_unicast);
            printf("    |- rx_multicast        = %#"PRIx64"\n", mydata->rx_multicast);
            printf("    |- rx_broadcast        = %#"PRIx64"\n", mydata->rx_broadcast);
            printf("    |- rx_discards         = %#"PRIx64"\n", mydata->rx_discards);
            printf("    |- rx_unknown_protocol = %#"PRIx64"\n", mydata->rx_unknown_protocol);
            printf("    |- tx_bytes            = %#"PRIx64"\n", mydata->tx_bytes);
            printf("    |- tx_unicast          = %#"PRIx64"\n", mydata->tx_unicast);
            printf("    |- tx_multicast        = %#"PRIx64"\n", mydata->tx_multicast);
            printf("    |- tx_broadcast        = %#"PRIx64"\n", mydata->tx_broadcast);
            printf("    |- tx_discards         = %#"PRIx64"\n", mydata->tx_discards);
            printf("    `- tx_errors           = %#"PRIx64"\n", mydata->tx_errors);
            break;
        }
        default:
            printf("    `- data = ");
            for (i = 0; i < req->datalen; i++) {
                printf("%02x ", (uint8_t)data[i]);
            }
            printf("\n");
            break;
        }
    }

    g_free(data);
}

static void vfio_i40e_record_atq_cmd(VFIOI40EDevice *vdev,
                                     I40eAdminQueueDescriptor *desc)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    unsigned char data[desc->datalen];
    uint64_t data_addr;

    if (!desc->datalen || (desc->opcode != I40E_AQC_OPC_SEND_MSG_TO_PF)) {
        return;
    }

    data_addr = desc->params.external.addr_high;
    data_addr <<= 32;
    data_addr |= desc->params.external.addr_low;

    switch (desc->cookie_high) {
    case I40E_VIRTCHNL_OP_CONFIG_IRQ_MAP: {
        struct i40e_virtchnl_irq_map_info *mydata = (void*)data;
        pci_dma_read(pdev, data_addr, data, desc->datalen);
        /* XXX make dynamic */
        vdev->irq_map = *mydata;
        break;
    }
    case I40E_VIRTCHNL_OP_CONFIG_VSI_QUEUES: {
        struct i40e_virtchnl_vsi_queue_config_info *mydata = (void*)data;
        pci_dma_read(pdev, data_addr, data, desc->datalen);
        /* XXX make dynamic */
        vdev->vsi_config = *mydata;
        break;
    }
    case I40E_VIRTCHNL_OP_ADD_ETHER_ADDRESS: {
        struct i40e_virtchnl_ether_addr_list *mydata = (void*)data;
        if (!vdev->addr.vsi_id) {
            pci_dma_read(pdev, data_addr, data, desc->datalen);
            /* XXX make dynamic */
            vdev->addr = *mydata;
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
}

/* Process one Admin Queue Command from the guest */
static void vfio_i40e_atq_process_one(VFIOI40EDevice *vdev, int index)
{
    char *name;
    I40eAdminQueueDescriptor desc;
    PCIDevice *pdev = PCI_DEVICE(vdev);
    hwaddr addr = vfio_i40e_get_atqba(vdev) + (index * sizeof(desc));

DPRINTF("XXX %s:%d guest queue index = %#x\n", __func__, __LINE__, index);
DPRINTF("XXX %s:%d queue addr = %#lx\n", __func__, __LINE__, addr);
    /* Read guest's command */
    pci_dma_read(pdev, addr, &desc, sizeof(desc));

    if (debug_i40evf) {
        name = g_strdup_printf("ATQ request %#x", index);
        vfio_i40e_print_aq_cmd(pdev, &desc, name);
        g_free(name);
    }

    vfio_i40e_record_atq_cmd(vdev, &desc);

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

    /* XXX do we need to copy back for the device? */
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
    } else {
        /* Copy command to guest */
        vfio_i40e_arq_recv(vdev, &desc);
    }

    /* Make record available for reuse */
    arq_cur->flags = I40E_AQ_FLAG_BUF;
    arq_cur->datalen = I40E_ARQ_DATA_LEN;

    /* Remember that we're done with the command */
    vdev->arq_last = vfio_i40evf_r32(vdev, I40E_VF_ARQH1);
    vfio_i40evf_w32(vdev, I40E_VF_ARQT1, (vdev->arq_last + 1) & (vdev->aq_len - 1));
}

static void vfio_i40e_aq_update(VFIOI40EDevice *vdev)
{

    bool arqact = vfio_i40evf_vr32(vdev, I40E_VF_ARQLEN1) & I40E_VF_ARQLEN1_ARQENABLE_MASK;

    if (!vdev->arq_active && arqact) {
        /* Guest enabled ARQ, map our own admin queues now */
        vdev->arq_active = true; /* XXX unset on reset? */
        vfio_i40e_aq_map(vdev);
    }

    if (!vdev->arq_active) {
        return;
    }

    while (vfio_i40evf_vr32(vdev, I40E_VF_ATQT1) != vfio_i40evf_vr32(vdev, I40E_VF_ATQH1)) {
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

    DPRINTF("XXX %s:%d -> %s %#x\n", __func__, __LINE__, vfio_i40evf_reg_name(addr), size);
    assert(size == 4);
    val = vfio_i40evf_r32(vdev, addr);
    DPRINTF("XXX %s:%d -> val = %#x\n", __func__, __LINE__, val);
    return val;
}

static void vfio_i40evf_mmio_mem_region_write(void *opaque, hwaddr addr,
                                              uint64_t data, unsigned size)
{
    VFIOI40EDevice *vdev = opaque;

    DPRINTF("XXX %s:%d -> %s %#x %#lx\n", __func__, __LINE__, vfio_i40evf_reg_name(addr), size, data);
    assert(size == 4);
    vfio_i40evf_w32(vdev, addr, data);
}

const MemoryRegionOps vfio_i40evf_mmio_mem_region_ops = {
    .read = vfio_i40evf_mmio_mem_region_read,
    .write = vfio_i40evf_mmio_mem_region_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/************ end of mmio debug **************/

static uint64_t vfio_i40evf_aq_mmio_mem_region_read(void *opaque, hwaddr subaddr,
                                               unsigned size)
{
    hwaddr addr = subaddr + I40E_VF_ARQBAH1;
    VFIOI40EDevice *vdev = opaque;
    uint32_t val;

    DPRINTF("XXX %s:%d -> %s %#x\n", __func__, __LINE__, vfio_i40evf_reg_name(addr), size);
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
    DPRINTF("XXX %s:%d -> val = %#x\n", __func__, __LINE__, val);
    return val;
}

static void vfio_i40evf_aq_mmio_mem_region_write(void *opaque, hwaddr subaddr,
                                            uint64_t data, unsigned size)
{
    hwaddr addr = subaddr + I40E_VF_ARQBAH1;
    VFIOI40EDevice *vdev = opaque;

    DPRINTF("XXX %s:%d -> %s %#x %#lx\n", __func__, __LINE__, vfio_i40evf_reg_name(addr), size, data);
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
    I40E_VFQF_HKEY(8), I40E_VFQF_HKEY(9), I40E_VFQF_HKEY(10), I40E_VFQF_HKEY(11),
    I40E_VFQF_HKEY(12),

    I40E_VFQF_HENA(0), I40E_VFQF_HENA(1),

    I40E_VFQF_HLUT(0), I40E_VFQF_HLUT(1), I40E_VFQF_HLUT(2), I40E_VFQF_HLUT(3),
    I40E_VFQF_HLUT(4), I40E_VFQF_HLUT(5), I40E_VFQF_HLUT(6), I40E_VFQF_HLUT(7),
    I40E_VFQF_HLUT(8), I40E_VFQF_HLUT(9), I40E_VFQF_HLUT(10), I40E_VFQF_HLUT(11),
    I40E_VFQF_HLUT(12), I40E_VFQF_HLUT(13), I40E_VFQF_HLUT(14), I40E_VFQF_HLUT(15),

    I40E_QTX_TAIL1(0), I40E_QTX_TAIL1(1), I40E_QTX_TAIL1(2), I40E_QTX_TAIL1(3),
    I40E_QTX_TAIL1(4), I40E_QTX_TAIL1(5), I40E_QTX_TAIL1(6), I40E_QTX_TAIL1(7),
    I40E_QTX_TAIL1(8), I40E_QTX_TAIL1(9), I40E_QTX_TAIL1(10), I40E_QTX_TAIL1(11),
    I40E_QTX_TAIL1(12), I40E_QTX_TAIL1(13), I40E_QTX_TAIL1(14), I40E_QTX_TAIL1(15),

    I40E_QRX_TAIL1(0), I40E_QRX_TAIL1(1), I40E_QRX_TAIL1(2), I40E_QRX_TAIL1(3),
    I40E_QRX_TAIL1(4), I40E_QRX_TAIL1(5), I40E_QRX_TAIL1(6), I40E_QRX_TAIL1(7),
    I40E_QRX_TAIL1(8), I40E_QRX_TAIL1(9), I40E_QRX_TAIL1(10), I40E_QRX_TAIL1(11),
    I40E_QRX_TAIL1(12), I40E_QRX_TAIL1(13), I40E_QRX_TAIL1(14), I40E_QRX_TAIL1(15),

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

int vfio_dma_map(VFIOContainer *container, hwaddr iova, ram_addr_t size,
                 void *vaddr, bool readonly);
void vfio_enable_msi(VFIOPCIDevice *vdev);
void vfio_enable_msix(VFIOPCIDevice *vdev);

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

#if 0
    /* Map VFIO region */
    vfio_dma_map(vdev->parent_obj.vbasedev.group->container, I40E_AQ_LOCATION,
                 64 * 1024, vdev->admin_queue, false);
#endif

    /* Restore config space */
    cmd = pdev->config_read(pdev, PCI_COMMAND, 2);
    cmd |= (PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER );
    pdev->config_write(pdev, PCI_COMMAND, cmd, 2);

    vfio_i40evf_vw32(vdev, I40E_VFQF_HENA(1), 0x80000000); // XXX no RX otherwise?

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
        DPRINTF("XXX %s:%d Enabling MSI\n", __func__, __LINE__);
        vfio_enable_msi(&vdev->parent_obj);
    }
    if (msix_enabled(pdev)) {
        DPRINTF("XXX %s:%d Enabling MSI-X\n", __func__, __LINE__);
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
        vdev->arq_ignore++;
        vfio_i40e_aq_update(vdev);
    }

    /* Restore VF configuration */
    {
        volatile struct i40e_virtchnl_irq_map_info *mydata = data;
        *mydata = vdev->irq_map;
        desc.cookie_high = I40E_VIRTCHNL_OP_CONFIG_IRQ_MAP;
        desc.datalen = sizeof(*mydata);
        vfio_i40e_print_aq_cmd(pdev, &desc, "ATQ IRQ MAP");
        vfio_i40e_atq_send(vdev, &desc, &resp);
        vfio_i40e_print_aq_cmd(pdev, &resp, "ATQ IRQ MAP response");
        vdev->arq_ignore++;
        vfio_i40e_aq_update(vdev);
    }

    {
        volatile struct i40e_virtchnl_ether_addr_list *mydata = data;
        *mydata = vdev->addr;
        desc.cookie_high = I40E_VIRTCHNL_OP_DEL_ETHER_ADDRESS;
        desc.datalen = sizeof(*mydata);
        vfio_i40e_print_aq_cmd(pdev, &desc, "ATQ DEL ETHER ADDRESS");
        vfio_i40e_atq_send(vdev, &desc, &resp);
        vfio_i40e_print_aq_cmd(pdev, &resp, "ATQ DEL ETHER ADDRESS response");
        vdev->arq_ignore++;
        vfio_i40e_aq_update(vdev);
    }

    {
        volatile struct i40e_virtchnl_ether_addr_list *mydata = data;
        *mydata = vdev->addr;
        desc.cookie_high = I40E_VIRTCHNL_OP_ADD_ETHER_ADDRESS;
        desc.datalen = sizeof(*mydata);
        vfio_i40e_print_aq_cmd(pdev, &desc, "ATQ ETHER ADDRESS");
        vfio_i40e_atq_send(vdev, &desc, &resp);
        vfio_i40e_print_aq_cmd(pdev, &resp, "ATQ ETHER ADDRESS response");
        vdev->arq_ignore++;
        vfio_i40e_aq_update(vdev);
    }

    {
        volatile struct i40e_virtchnl_vsi_queue_config_info *mydata = data;
        *mydata = vdev->vsi_config;
        desc.cookie_high = I40E_VIRTCHNL_OP_CONFIG_VSI_QUEUES;
        desc.datalen = sizeof(*mydata);
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

    DPRINTF("XXX %s:%d\n", __func__, __LINE__);
    return 0;
}

static void vfio_i40evf_save(void *opaque)
{
    VFIOI40EDevice *vdev = opaque;

    DPRINTF("XXX %s:%d\n", __func__, __LINE__);
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
        VMSTATE_UINT16(txq.headwb_enabled, struct i40e_virtchnl_queue_pair_info),
        VMSTATE_UINT64(txq.dma_ring_addr, struct i40e_virtchnl_queue_pair_info),
        VMSTATE_UINT64(txq.dma_headwb_addr, struct i40e_virtchnl_queue_pair_info),
        VMSTATE_UINT16(rxq.vsi_id, struct i40e_virtchnl_queue_pair_info),
        VMSTATE_UINT16(rxq.queue_id, struct i40e_virtchnl_queue_pair_info),
        VMSTATE_UINT32(rxq.ring_len, struct i40e_virtchnl_queue_pair_info),
        VMSTATE_UINT16(rxq.hdr_size, struct i40e_virtchnl_queue_pair_info),
        VMSTATE_UINT16(rxq.splithdr_enabled, struct i40e_virtchnl_queue_pair_info),
        VMSTATE_UINT32(rxq.databuffer_size, struct i40e_virtchnl_queue_pair_info),
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
        VMSTATE_UINT16(num_queue_pairs, struct i40e_virtchnl_vsi_queue_config_info),
        VMSTATE_STRUCT_VARRAY_UINT16(qpair, struct i40e_virtchnl_vsi_queue_config_info,
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

    DPRINTF("XXX %s:%d\n", __func__, __LINE__);
}

static int (*vfio_pci_init)(PCIDevice *dev);

static int vfio_i40evf_initfn(PCIDevice *dev)
{
    VFIOI40EDevice *vdev = VFIO_I40EVF(dev);
    VFIOPCIDevice *vpdev = VFIO_PCI(dev);
    VFIOBAR *bar = &vpdev->bars[0];

    vfio_pci_init(dev);
    DPRINTF("XXX %s:%d\n", __func__, __LINE__);
    vdev->aq_len = 32;

    /* XXX debug MMIO access */
if (0) {
    memory_region_init_io(&vdev->mmio_mem, OBJECT(dev),
                          &vfio_i40evf_mmio_mem_region_ops,
                          vdev, "i40evf config", 64 * 1024);
    memory_region_add_subregion_overlap(&bar->region.mem, 0, &vdev->mmio_mem, 29);
}

    /* Override the Admin Queue configuration */
    memory_region_init_io(&vdev->aq_mmio_mem, OBJECT(dev),
                          &vfio_i40evf_aq_mmio_mem_region_ops,
                          vdev, "i40evf AQ config",
                          I40E_VFGEN_RSTAT - I40E_VF_ARQBAH1);
    memory_region_add_subregion_overlap(&bar->region.mem, I40E_VF_ARQBAH1, &vdev->aq_mmio_mem, 30);
    vfio_i40e_map_aq_data(vdev);

    return 0;
}

static void vfio_i40evf_dev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    dc->vmsd = &vfio_i40evf_vmstate;
    dc->desc = "VFIO-based i40evf card";
    vfio_pci_init = k->init; /* XXX */
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
