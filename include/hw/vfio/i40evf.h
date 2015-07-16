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


#define TYPE_VFIO_I40EVF "vfio-i40e"
#define VFIO_I40EVF(obj) \
     OBJECT_CHECK(VFIOI40EDevice, (obj), TYPE_VFIO_I40EVF)
#define VFIO_I40EVF_CLASS(klass) \
     OBJECT_CLASS_CHECK(VFIOI40EDeviceClass, (klass), TYPE_VFIO_I40EVF)
#define VFIO_I40EVF_GET_CLASS(obj) \
     OBJECT_GET_CLASS(VFIOI40EDeviceClass, (obj), TYPE_VFIO_I40EVF)

#define VFIO_PCI(obj) \
     OBJECT_CHECK(VFIOPCIDevice, (obj), "vfio-pci")

typedef struct I40eAdminQueueDescriptor {
    uint16_t flags;
    uint16_t opcode;
    uint16_t datalen;
    uint16_t retval;
    uint32_t cookie_high;
    uint32_t cookie_low;
    union {
        struct {
            uint32_t param0;
            uint32_t param1;
            uint32_t param2;
            uint32_t param3;
        } internal;
        struct {
            uint32_t param0;
            uint32_t param1;
            uint32_t addr_high;
            uint32_t addr_low;
        } external;
        char raw[16];
    } params;
} I40eAdminQueueDescriptor;

/* Flags sub-structure
 * |0  |1  |2  |3  |4  |5  |6  |7  |8  |9  |10 |11 |12 |13 |14 |15 |
 * |DD |CMP|ERR|VFE| * *  RESERVED * * |LB |RD |VFC|BUF|SI |EI |FE |
 */

/* command flags and offsets*/
#define I40E_AQ_FLAG_DD_SHIFT    0
#define I40E_AQ_FLAG_CMP_SHIFT   1
#define I40E_AQ_FLAG_ERR_SHIFT   2
#define I40E_AQ_FLAG_VFE_SHIFT   3
#define I40E_AQ_FLAG_LB_SHIFT    9
#define I40E_AQ_FLAG_RD_SHIFT    10
#define I40E_AQ_FLAG_VFC_SHIFT   11
#define I40E_AQ_FLAG_BUF_SHIFT   12
#define I40E_AQ_FLAG_SI_SHIFT    13
#define I40E_AQ_FLAG_EI_SHIFT    14
#define I40E_AQ_FLAG_FE_SHIFT    15

#define I40E_AQ_FLAG_DD     (1 << I40E_AQ_FLAG_DD_SHIFT)  /* 0x1    */
#define I40E_AQ_FLAG_CMP    (1 << I40E_AQ_FLAG_CMP_SHIFT) /* 0x2    */
#define I40E_AQ_FLAG_ERR    (1 << I40E_AQ_FLAG_ERR_SHIFT) /* 0x4    */
#define I40E_AQ_FLAG_VFE    (1 << I40E_AQ_FLAG_VFE_SHIFT) /* 0x8    */
#define I40E_AQ_FLAG_LB     (1 << I40E_AQ_FLAG_LB_SHIFT)  /* 0x200  */
#define I40E_AQ_FLAG_RD     (1 << I40E_AQ_FLAG_RD_SHIFT)  /* 0x400  */
#define I40E_AQ_FLAG_VFC    (1 << I40E_AQ_FLAG_VFC_SHIFT) /* 0x800  */
#define I40E_AQ_FLAG_BUF    (1 << I40E_AQ_FLAG_BUF_SHIFT) /* 0x1000 */
#define I40E_AQ_FLAG_SI     (1 << I40E_AQ_FLAG_SI_SHIFT)  /* 0x2000 */
#define I40E_AQ_FLAG_EI     (1 << I40E_AQ_FLAG_EI_SHIFT)  /* 0x4000 */
#define I40E_AQ_FLAG_FE     (1 << I40E_AQ_FLAG_FE_SHIFT)  /* 0x8000 */


/* XXX make dynamic */
#define I40E_AQ_LOCATION          0x70000000000ULL
#define I40E_AQ_LOCATION_ARQ      0x70000000000ULL
#define I40E_AQ_LOCATION_ATQ      0x70000000400ULL
#define I40E_AQ_LOCATION_ATQ_DATA 0x70000010000ULL
#define I40E_AQ_LOCATION_ARQ_DATA 0x70000020000ULL
#define I40E_ARQ_DATA_LEN         0x200
#define I40E_RING_LOCATION        0x70000030000ULL

/* I40E_MASK is a macro used on 32 bit registers */
#define I40E_MASK(mask, shift) (mask << shift)

#define I40E_MAX_VSI_QP                 16
#define I40E_MAX_VF_VSI                 3
#define I40E_MAX_CHAINED_RX_BUFFERS     5
#define I40E_MAX_PF_UDP_OFFLOAD_PORTS   16

#define I40E_VF_ARQBAH1 0x00006000 /* Reset: EMPR */
#define I40E_VF_ARQBAH1_ARQBAH_SHIFT 0
#define I40E_VF_ARQBAH1_ARQBAH_MASK I40E_MASK(0xFFFFFFFF, I40E_VF_ARQBAH1_ARQBAH_SHIFT)
#define I40E_VF_ARQBAL1 0x00006C00 /* Reset: EMPR */
#define I40E_VF_ARQBAL1_ARQBAL_SHIFT 0
#define I40E_VF_ARQBAL1_ARQBAL_MASK I40E_MASK(0xFFFFFFFF, I40E_VF_ARQBAL1_ARQBAL_SHIFT)
#define I40E_VF_ARQH1 0x00007400 /* Reset: EMPR */
#define I40E_VF_ARQH1_ARQH_SHIFT 0
#define I40E_VF_ARQH1_ARQH_MASK I40E_MASK(0x3FF, I40E_VF_ARQH1_ARQH_SHIFT)
#define I40E_VF_ARQLEN1 0x00008000 /* Reset: EMPR */
#define I40E_VF_ARQLEN1_ARQLEN_SHIFT 0
#define I40E_VF_ARQLEN1_ARQLEN_MASK I40E_MASK(0x3FF, I40E_VF_ARQLEN1_ARQLEN_SHIFT)
#define I40E_VF_ARQLEN1_ARQVFE_SHIFT 28
#define I40E_VF_ARQLEN1_ARQVFE_MASK I40E_MASK(0x1, I40E_VF_ARQLEN1_ARQVFE_SHIFT)
#define I40E_VF_ARQLEN1_ARQOVFL_SHIFT 29
#define I40E_VF_ARQLEN1_ARQOVFL_MASK I40E_MASK(0x1, I40E_VF_ARQLEN1_ARQOVFL_SHIFT)
#define I40E_VF_ARQLEN1_ARQCRIT_SHIFT 30
#define I40E_VF_ARQLEN1_ARQCRIT_MASK I40E_MASK(0x1, I40E_VF_ARQLEN1_ARQCRIT_SHIFT)
#define I40E_VF_ARQLEN1_ARQENABLE_SHIFT 31
#define I40E_VF_ARQLEN1_ARQENABLE_MASK I40E_MASK(0x1, I40E_VF_ARQLEN1_ARQENABLE_SHIFT)
#define I40E_VF_ARQT1 0x00007000 /* Reset: EMPR */
#define I40E_VF_ARQT1_ARQT_SHIFT 0
#define I40E_VF_ARQT1_ARQT_MASK I40E_MASK(0x3FF, I40E_VF_ARQT1_ARQT_SHIFT)
#define I40E_VF_ATQBAH1 0x00007800 /* Reset: EMPR */
#define I40E_VF_ATQBAH1_ATQBAH_SHIFT 0
#define I40E_VF_ATQBAH1_ATQBAH_MASK I40E_MASK(0xFFFFFFFF, I40E_VF_ATQBAH1_ATQBAH_SHIFT)
#define I40E_VF_ATQBAL1 0x00007C00 /* Reset: EMPR */
#define I40E_VF_ATQBAL1_ATQBAL_SHIFT 0
#define I40E_VF_ATQBAL1_ATQBAL_MASK I40E_MASK(0xFFFFFFFF, I40E_VF_ATQBAL1_ATQBAL_SHIFT)
#define I40E_VF_ATQH1 0x00006400 /* Reset: EMPR */
#define I40E_VF_ATQH1_ATQH_SHIFT 0
#define I40E_VF_ATQH1_ATQH_MASK I40E_MASK(0x3FF, I40E_VF_ATQH1_ATQH_SHIFT)
#define I40E_VF_ATQLEN1 0x00006800 /* Reset: EMPR */
#define I40E_VF_ATQLEN1_ATQLEN_SHIFT 0
#define I40E_VF_ATQLEN1_ATQLEN_MASK I40E_MASK(0x3FF, I40E_VF_ATQLEN1_ATQLEN_SHIFT)
#define I40E_VF_ATQLEN1_ATQVFE_SHIFT 28
#define I40E_VF_ATQLEN1_ATQVFE_MASK I40E_MASK(0x1, I40E_VF_ATQLEN1_ATQVFE_SHIFT)
#define I40E_VF_ATQLEN1_ATQOVFL_SHIFT 29
#define I40E_VF_ATQLEN1_ATQOVFL_MASK I40E_MASK(0x1, I40E_VF_ATQLEN1_ATQOVFL_SHIFT)
#define I40E_VF_ATQLEN1_ATQCRIT_SHIFT 30
#define I40E_VF_ATQLEN1_ATQCRIT_MASK I40E_MASK(0x1, I40E_VF_ATQLEN1_ATQCRIT_SHIFT)
#define I40E_VF_ATQLEN1_ATQENABLE_SHIFT 31
#define I40E_VF_ATQLEN1_ATQENABLE_MASK I40E_MASK(0x1, I40E_VF_ATQLEN1_ATQENABLE_SHIFT)
#define I40E_VF_ATQT1 0x00008400 /* Reset: EMPR */
#define I40E_VF_ATQT1_ATQT_SHIFT 0
#define I40E_VF_ATQT1_ATQT_MASK I40E_MASK(0x3FF, I40E_VF_ATQT1_ATQT_SHIFT)
#define I40E_VFGEN_RSTAT 0x00008800 /* Reset: VFR */
#define I40E_VFGEN_RSTAT_VFR_STATE_SHIFT 0
#define I40E_VFGEN_RSTAT_VFR_STATE_MASK I40E_MASK(0x3, I40E_VFGEN_RSTAT_VFR_STATE_SHIFT)
#define I40E_VFINT_DYN_CTL01 0x00005C00 /* Reset: VFR */
#define I40E_VFINT_DYN_CTL01_INTENA_SHIFT 0
#define I40E_VFINT_DYN_CTL01_INTENA_MASK I40E_MASK(0x1, I40E_VFINT_DYN_CTL01_INTENA_SHIFT)
#define I40E_VFINT_DYN_CTL01_CLEARPBA_SHIFT 1
#define I40E_VFINT_DYN_CTL01_CLEARPBA_MASK I40E_MASK(0x1, I40E_VFINT_DYN_CTL01_CLEARPBA_SHIFT)
#define I40E_VFINT_DYN_CTL01_SWINT_TRIG_SHIFT 2
#define I40E_VFINT_DYN_CTL01_SWINT_TRIG_MASK I40E_MASK(0x1, I40E_VFINT_DYN_CTL01_SWINT_TRIG_SHIFT)
#define I40E_VFINT_DYN_CTL01_ITR_INDX_SHIFT 3
#define I40E_VFINT_DYN_CTL01_ITR_INDX_MASK I40E_MASK(0x3, I40E_VFINT_DYN_CTL01_ITR_INDX_SHIFT)
#define I40E_VFINT_DYN_CTL01_INTERVAL_SHIFT 5
#define I40E_VFINT_DYN_CTL01_INTERVAL_MASK I40E_MASK(0xFFF, I40E_VFINT_DYN_CTL01_INTERVAL_SHIFT)
#define I40E_VFINT_DYN_CTL01_SW_ITR_INDX_ENA_SHIFT 24
#define I40E_VFINT_DYN_CTL01_SW_ITR_INDX_ENA_MASK I40E_MASK(0x1, I40E_VFINT_DYN_CTL01_SW_ITR_INDX_ENA_SHIFT)
#define I40E_VFINT_DYN_CTL01_SW_ITR_INDX_SHIFT 25
#define I40E_VFINT_DYN_CTL01_SW_ITR_INDX_MASK I40E_MASK(0x3, I40E_VFINT_DYN_CTL01_SW_ITR_INDX_SHIFT)
#define I40E_VFINT_DYN_CTL01_INTENA_MSK_SHIFT 31
#define I40E_VFINT_DYN_CTL01_INTENA_MSK_MASK I40E_MASK(0x1, I40E_VFINT_DYN_CTL01_INTENA_MSK_SHIFT)
#define I40E_VFINT_DYN_CTLN1(_INTVF) (0x00003800 + ((_INTVF) * 4)) /* _i=0...15 */ /* Reset: VFR */
#define I40E_VFINT_DYN_CTLN1_MAX_INDEX 15
#define I40E_VFINT_DYN_CTLN1_INTENA_SHIFT 0
#define I40E_VFINT_DYN_CTLN1_INTENA_MASK I40E_MASK(0x1, I40E_VFINT_DYN_CTLN1_INTENA_SHIFT)
#define I40E_VFINT_DYN_CTLN1_CLEARPBA_SHIFT 1
#define I40E_VFINT_DYN_CTLN1_CLEARPBA_MASK I40E_MASK(0x1, I40E_VFINT_DYN_CTLN1_CLEARPBA_SHIFT)
#define I40E_VFINT_DYN_CTLN1_SWINT_TRIG_SHIFT 2
#define I40E_VFINT_DYN_CTLN1_SWINT_TRIG_MASK I40E_MASK(0x1, I40E_VFINT_DYN_CTLN1_SWINT_TRIG_SHIFT)
#define I40E_VFINT_DYN_CTLN1_ITR_INDX_SHIFT 3
#define I40E_VFINT_DYN_CTLN1_ITR_INDX_MASK I40E_MASK(0x3, I40E_VFINT_DYN_CTLN1_ITR_INDX_SHIFT)
#define I40E_VFINT_DYN_CTLN1_INTERVAL_SHIFT 5
#define I40E_VFINT_DYN_CTLN1_INTERVAL_MASK I40E_MASK(0xFFF, I40E_VFINT_DYN_CTLN1_INTERVAL_SHIFT)
#define I40E_VFINT_DYN_CTLN1_SW_ITR_INDX_ENA_SHIFT 24
#define I40E_VFINT_DYN_CTLN1_SW_ITR_INDX_ENA_MASK I40E_MASK(0x1, I40E_VFINT_DYN_CTLN1_SW_ITR_INDX_ENA_SHIFT)
#define I40E_VFINT_DYN_CTLN1_SW_ITR_INDX_SHIFT 25
#define I40E_VFINT_DYN_CTLN1_SW_ITR_INDX_MASK I40E_MASK(0x3, I40E_VFINT_DYN_CTLN1_SW_ITR_INDX_SHIFT)
#define I40E_VFINT_DYN_CTLN1_INTENA_MSK_SHIFT 31
#define I40E_VFINT_DYN_CTLN1_INTENA_MSK_MASK I40E_MASK(0x1, I40E_VFINT_DYN_CTLN1_INTENA_MSK_SHIFT)
#define I40E_VFINT_ICR0_ENA1 0x00005000 /* Reset: CORER */
#define I40E_VFINT_ICR0_ENA1_LINK_STAT_CHANGE_SHIFT 25
#define I40E_VFINT_ICR0_ENA1_LINK_STAT_CHANGE_MASK I40E_MASK(0x1, I40E_VFINT_ICR0_ENA1_LINK_STAT_CHANGE_SHIFT)
#define I40E_VFINT_ICR0_ENA1_ADMINQ_SHIFT 30
#define I40E_VFINT_ICR0_ENA1_ADMINQ_MASK I40E_MASK(0x1, I40E_VFINT_ICR0_ENA1_ADMINQ_SHIFT)
#define I40E_VFINT_ICR0_ENA1_RSVD_SHIFT 31
#define I40E_VFINT_ICR0_ENA1_RSVD_MASK I40E_MASK(0x1, I40E_VFINT_ICR0_ENA1_RSVD_SHIFT)
#define I40E_VFINT_ICR01 0x00004800 /* Reset: CORER */
#define I40E_VFINT_ICR01_INTEVENT_SHIFT 0
#define I40E_VFINT_ICR01_INTEVENT_MASK I40E_MASK(0x1, I40E_VFINT_ICR01_INTEVENT_SHIFT)
#define I40E_VFINT_ICR01_QUEUE_0_SHIFT 1
#define I40E_VFINT_ICR01_QUEUE_0_MASK I40E_MASK(0x1, I40E_VFINT_ICR01_QUEUE_0_SHIFT)
#define I40E_VFINT_ICR01_QUEUE_1_SHIFT 2
#define I40E_VFINT_ICR01_QUEUE_1_MASK I40E_MASK(0x1, I40E_VFINT_ICR01_QUEUE_1_SHIFT)
#define I40E_VFINT_ICR01_QUEUE_2_SHIFT 3
#define I40E_VFINT_ICR01_QUEUE_2_MASK I40E_MASK(0x1, I40E_VFINT_ICR01_QUEUE_2_SHIFT)
#define I40E_VFINT_ICR01_QUEUE_3_SHIFT 4
#define I40E_VFINT_ICR01_QUEUE_3_MASK I40E_MASK(0x1, I40E_VFINT_ICR01_QUEUE_3_SHIFT)
#define I40E_VFINT_ICR01_LINK_STAT_CHANGE_SHIFT 25
#define I40E_VFINT_ICR01_LINK_STAT_CHANGE_MASK I40E_MASK(0x1, I40E_VFINT_ICR01_LINK_STAT_CHANGE_SHIFT)
#define I40E_VFINT_ICR01_ADMINQ_SHIFT 30
#define I40E_VFINT_ICR01_ADMINQ_MASK I40E_MASK(0x1, I40E_VFINT_ICR01_ADMINQ_SHIFT)
#define I40E_VFINT_ICR01_SWINT_SHIFT 31
#define I40E_VFINT_ICR01_SWINT_MASK I40E_MASK(0x1, I40E_VFINT_ICR01_SWINT_SHIFT)
#define I40E_VFINT_ITR01(_i) (0x00004C00 + ((_i) * 4)) /* _i=0...2 */ /* Reset: VFR */
#define I40E_VFINT_ITR01_MAX_INDEX 2
#define I40E_VFINT_ITR01_INTERVAL_SHIFT 0
#define I40E_VFINT_ITR01_INTERVAL_MASK I40E_MASK(0xFFF, I40E_VFINT_ITR01_INTERVAL_SHIFT)
#define I40E_VFINT_ITRN1(_i, _INTVF) (0x00002800 + ((_i) * 64 + (_INTVF) * 4)) /* _i=0...2, _INTVF=0...15 */ /* Reset: VFR */
#define I40E_VFINT_ITRN1_MAX_INDEX 2
#define I40E_VFINT_ITRN1_INTERVAL_SHIFT 0
#define I40E_VFINT_ITRN1_INTERVAL_MASK I40E_MASK(0xFFF, I40E_VFINT_ITRN1_INTERVAL_SHIFT)
#define I40E_VFINT_STAT_CTL01 0x00005400 /* Reset: CORER */
#define I40E_VFINT_STAT_CTL01_OTHER_ITR_INDX_SHIFT 2
#define I40E_VFINT_STAT_CTL01_OTHER_ITR_INDX_MASK I40E_MASK(0x3, I40E_VFINT_STAT_CTL01_OTHER_ITR_INDX_SHIFT)
#define I40E_QRX_TAIL1(_Q) (0x00002000 + ((_Q) * 4)) /* _i=0...15 */ /* Reset: CORER */
#define I40E_QRX_TAIL1_MAX_INDEX 15
#define I40E_QRX_TAIL1_TAIL_SHIFT 0
#define I40E_QRX_TAIL1_TAIL_MASK I40E_MASK(0x1FFF, I40E_QRX_TAIL1_TAIL_SHIFT)
#define I40E_QTX_TAIL1(_Q) (0x00000000 + ((_Q) * 4)) /* _i=0...15 */ /* Reset: PFR */
#define I40E_QTX_TAIL1_MAX_INDEX 15
#define I40E_QTX_TAIL1_TAIL_SHIFT 0
#define I40E_QTX_TAIL1_TAIL_MASK I40E_MASK(0x1FFF, I40E_QTX_TAIL1_TAIL_SHIFT)
#define I40E_VFMSIX_PBA 0x00002000 /* Reset: VFLR */
#define I40E_VFMSIX_PBA_PENBIT_SHIFT 0
#define I40E_VFMSIX_PBA_PENBIT_MASK I40E_MASK(0xFFFFFFFF, I40E_VFMSIX_PBA_PENBIT_SHIFT)
#define I40E_VFMSIX_TADD(_i) (0x00000000 + ((_i) * 16)) /* _i=0...16 */ /* Reset: VFLR */
#define I40E_VFMSIX_TADD_MAX_INDEX 16
#define I40E_VFMSIX_TADD_MSIXTADD10_SHIFT 0
#define I40E_VFMSIX_TADD_MSIXTADD10_MASK I40E_MASK(0x3, I40E_VFMSIX_TADD_MSIXTADD10_SHIFT)
#define I40E_VFMSIX_TADD_MSIXTADD_SHIFT 2
#define I40E_VFMSIX_TADD_MSIXTADD_MASK I40E_MASK(0x3FFFFFFF, I40E_VFMSIX_TADD_MSIXTADD_SHIFT)
#define I40E_VFMSIX_TMSG(_i) (0x00000008 + ((_i) * 16)) /* _i=0...16 */ /* Reset: VFLR */
#define I40E_VFMSIX_TMSG_MAX_INDEX 16
#define I40E_VFMSIX_TMSG_MSIXTMSG_SHIFT 0
#define I40E_VFMSIX_TMSG_MSIXTMSG_MASK I40E_MASK(0xFFFFFFFF, I40E_VFMSIX_TMSG_MSIXTMSG_SHIFT)
#define I40E_VFMSIX_TUADD(_i) (0x00000004 + ((_i) * 16)) /* _i=0...16 */ /* Reset: VFLR */
#define I40E_VFMSIX_TUADD_MAX_INDEX 16
#define I40E_VFMSIX_TUADD_MSIXTUADD_SHIFT 0
#define I40E_VFMSIX_TUADD_MSIXTUADD_MASK I40E_MASK(0xFFFFFFFF, I40E_VFMSIX_TUADD_MSIXTUADD_SHIFT)
#define I40E_VFMSIX_TVCTRL(_i) (0x0000000C + ((_i) * 16)) /* _i=0...16 */ /* Reset: VFLR */
#define I40E_VFMSIX_TVCTRL_MAX_INDEX 16
#define I40E_VFMSIX_TVCTRL_MASK_SHIFT 0
#define I40E_VFMSIX_TVCTRL_MASK_MASK I40E_MASK(0x1, I40E_VFMSIX_TVCTRL_MASK_SHIFT)
#define I40E_VFCM_PE_ERRDATA 0x0000DC00 /* Reset: VFR */
#define I40E_VFCM_PE_ERRDATA_ERROR_CODE_SHIFT 0
#define I40E_VFCM_PE_ERRDATA_ERROR_CODE_MASK I40E_MASK(0xF, I40E_VFCM_PE_ERRDATA_ERROR_CODE_SHIFT)
#define I40E_VFCM_PE_ERRDATA_Q_TYPE_SHIFT 4
#define I40E_VFCM_PE_ERRDATA_Q_TYPE_MASK I40E_MASK(0x7, I40E_VFCM_PE_ERRDATA_Q_TYPE_SHIFT)
#define I40E_VFCM_PE_ERRDATA_Q_NUM_SHIFT 8
#define I40E_VFCM_PE_ERRDATA_Q_NUM_MASK I40E_MASK(0x3FFFF, I40E_VFCM_PE_ERRDATA_Q_NUM_SHIFT)
#define I40E_VFCM_PE_ERRINFO 0x0000D800 /* Reset: VFR */
#define I40E_VFCM_PE_ERRINFO_ERROR_VALID_SHIFT 0
#define I40E_VFCM_PE_ERRINFO_ERROR_VALID_MASK I40E_MASK(0x1, I40E_VFCM_PE_ERRINFO_ERROR_VALID_SHIFT)
#define I40E_VFCM_PE_ERRINFO_ERROR_INST_SHIFT 4
#define I40E_VFCM_PE_ERRINFO_ERROR_INST_MASK I40E_MASK(0x7, I40E_VFCM_PE_ERRINFO_ERROR_INST_SHIFT)
#define I40E_VFCM_PE_ERRINFO_DBL_ERROR_CNT_SHIFT 8
#define I40E_VFCM_PE_ERRINFO_DBL_ERROR_CNT_MASK I40E_MASK(0xFF, I40E_VFCM_PE_ERRINFO_DBL_ERROR_CNT_SHIFT)
#define I40E_VFCM_PE_ERRINFO_RLU_ERROR_CNT_SHIFT 16
#define I40E_VFCM_PE_ERRINFO_RLU_ERROR_CNT_MASK I40E_MASK(0xFF, I40E_VFCM_PE_ERRINFO_RLU_ERROR_CNT_SHIFT)
#define I40E_VFCM_PE_ERRINFO_RLS_ERROR_CNT_SHIFT 24
#define I40E_VFCM_PE_ERRINFO_RLS_ERROR_CNT_MASK I40E_MASK(0xFF, I40E_VFCM_PE_ERRINFO_RLS_ERROR_CNT_SHIFT)
#define I40E_VFQF_HENA(_i) (0x0000C400 + ((_i) * 4)) /* _i=0...1 */ /* Reset: CORER */
#define I40E_VFQF_HENA_MAX_INDEX 1
#define I40E_VFQF_HENA_PTYPE_ENA_SHIFT 0
#define I40E_VFQF_HENA_PTYPE_ENA_MASK I40E_MASK(0xFFFFFFFF, I40E_VFQF_HENA_PTYPE_ENA_SHIFT)
#define I40E_VFQF_HKEY(_i) (0x0000CC00 + ((_i) * 4)) /* _i=0...12 */ /* Reset: CORER */
#define I40E_VFQF_HKEY_MAX_INDEX 12
#define I40E_VFQF_HKEY_KEY_0_SHIFT 0
#define I40E_VFQF_HKEY_KEY_0_MASK I40E_MASK(0xFF, I40E_VFQF_HKEY_KEY_0_SHIFT)
#define I40E_VFQF_HKEY_KEY_1_SHIFT 8
#define I40E_VFQF_HKEY_KEY_1_MASK I40E_MASK(0xFF, I40E_VFQF_HKEY_KEY_1_SHIFT)
#define I40E_VFQF_HKEY_KEY_2_SHIFT 16
#define I40E_VFQF_HKEY_KEY_2_MASK I40E_MASK(0xFF, I40E_VFQF_HKEY_KEY_2_SHIFT)
#define I40E_VFQF_HKEY_KEY_3_SHIFT 24
#define I40E_VFQF_HKEY_KEY_3_MASK I40E_MASK(0xFF, I40E_VFQF_HKEY_KEY_3_SHIFT)
#define I40E_VFQF_HLUT(_i) (0x0000D000 + ((_i) * 4)) /* _i=0...15 */ /* Reset: CORER */
#define I40E_VFQF_HLUT_MAX_INDEX 15
#define I40E_VFQF_HLUT_LUT0_SHIFT 0
#define I40E_VFQF_HLUT_LUT0_MASK I40E_MASK(0xF, I40E_VFQF_HLUT_LUT0_SHIFT)
#define I40E_VFQF_HLUT_LUT1_SHIFT 8
#define I40E_VFQF_HLUT_LUT1_MASK I40E_MASK(0xF, I40E_VFQF_HLUT_LUT1_SHIFT)
#define I40E_VFQF_HLUT_LUT2_SHIFT 16
#define I40E_VFQF_HLUT_LUT2_MASK I40E_MASK(0xF, I40E_VFQF_HLUT_LUT2_SHIFT)
#define I40E_VFQF_HLUT_LUT3_SHIFT 24
#define I40E_VFQF_HLUT_LUT3_MASK I40E_MASK(0xF, I40E_VFQF_HLUT_LUT3_SHIFT)
#define I40E_VFQF_HREGION(_i) (0x0000D400 + ((_i) * 4)) /* _i=0...7 */ /* Reset: CORER */
#define I40E_VFQF_HREGION_MAX_INDEX 7
#define I40E_VFQF_HREGION_OVERRIDE_ENA_0_SHIFT 0
#define I40E_VFQF_HREGION_OVERRIDE_ENA_0_MASK I40E_MASK(0x1, I40E_VFQF_HREGION_OVERRIDE_ENA_0_SHIFT)
#define I40E_VFQF_HREGION_REGION_0_SHIFT 1
#define I40E_VFQF_HREGION_REGION_0_MASK I40E_MASK(0x7, I40E_VFQF_HREGION_REGION_0_SHIFT)
#define I40E_VFQF_HREGION_OVERRIDE_ENA_1_SHIFT 4
#define I40E_VFQF_HREGION_OVERRIDE_ENA_1_MASK I40E_MASK(0x1, I40E_VFQF_HREGION_OVERRIDE_ENA_1_SHIFT)
#define I40E_VFQF_HREGION_REGION_1_SHIFT 5
#define I40E_VFQF_HREGION_REGION_1_MASK I40E_MASK(0x7, I40E_VFQF_HREGION_REGION_1_SHIFT)
#define I40E_VFQF_HREGION_OVERRIDE_ENA_2_SHIFT 8
#define I40E_VFQF_HREGION_OVERRIDE_ENA_2_MASK I40E_MASK(0x1, I40E_VFQF_HREGION_OVERRIDE_ENA_2_SHIFT)
#define I40E_VFQF_HREGION_REGION_2_SHIFT 9
#define I40E_VFQF_HREGION_REGION_2_MASK I40E_MASK(0x7, I40E_VFQF_HREGION_REGION_2_SHIFT)
#define I40E_VFQF_HREGION_OVERRIDE_ENA_3_SHIFT 12
#define I40E_VFQF_HREGION_OVERRIDE_ENA_3_MASK I40E_MASK(0x1, I40E_VFQF_HREGION_OVERRIDE_ENA_3_SHIFT)
#define I40E_VFQF_HREGION_REGION_3_SHIFT 13
#define I40E_VFQF_HREGION_REGION_3_MASK I40E_MASK(0x7, I40E_VFQF_HREGION_REGION_3_SHIFT)
#define I40E_VFQF_HREGION_OVERRIDE_ENA_4_SHIFT 16
#define I40E_VFQF_HREGION_OVERRIDE_ENA_4_MASK I40E_MASK(0x1, I40E_VFQF_HREGION_OVERRIDE_ENA_4_SHIFT)
#define I40E_VFQF_HREGION_REGION_4_SHIFT 17
#define I40E_VFQF_HREGION_REGION_4_MASK I40E_MASK(0x7, I40E_VFQF_HREGION_REGION_4_SHIFT)
#define I40E_VFQF_HREGION_OVERRIDE_ENA_5_SHIFT 20
#define I40E_VFQF_HREGION_OVERRIDE_ENA_5_MASK I40E_MASK(0x1, I40E_VFQF_HREGION_OVERRIDE_ENA_5_SHIFT)
#define I40E_VFQF_HREGION_REGION_5_SHIFT 21
#define I40E_VFQF_HREGION_REGION_5_MASK I40E_MASK(0x7, I40E_VFQF_HREGION_REGION_5_SHIFT)
#define I40E_VFQF_HREGION_OVERRIDE_ENA_6_SHIFT 24
#define I40E_VFQF_HREGION_OVERRIDE_ENA_6_MASK I40E_MASK(0x1, I40E_VFQF_HREGION_OVERRIDE_ENA_6_SHIFT)
#define I40E_VFQF_HREGION_REGION_6_SHIFT 25
#define I40E_VFQF_HREGION_REGION_6_MASK I40E_MASK(0x7, I40E_VFQF_HREGION_REGION_6_SHIFT)
#define I40E_VFQF_HREGION_OVERRIDE_ENA_7_SHIFT 28
#define I40E_VFQF_HREGION_OVERRIDE_ENA_7_MASK I40E_MASK(0x1, I40E_VFQF_HREGION_OVERRIDE_ENA_7_SHIFT)
#define I40E_VFQF_HREGION_REGION_7_SHIFT 29
#define I40E_VFQF_HREGION_REGION_7_MASK I40E_MASK(0x7, I40E_VFQF_HREGION_REGION_7_SHIFT)

#define I40E_AQC_OPC_SEND_MSG_TO_PF 0x0801
#define I40E_AQC_OPC_SEND_MSG_TO_VF 0x0802

/* Opcodes for VF-PF communication. These are placed in the v_opcode field
 * of the virtchnl_msg structure.
 */
enum i40e_virtchnl_ops {
/* The PF sends status change events to VFs using
 * the I40E_VIRTCHNL_OP_EVENT opcode.
 * VFs send requests to the PF using the other ops.
 */
    I40E_VIRTCHNL_OP_UNKNOWN = 0,
    I40E_VIRTCHNL_OP_VERSION = 1, /* must ALWAYS be 1 */
    I40E_VIRTCHNL_OP_RESET_VF = 2,
    I40E_VIRTCHNL_OP_GET_VF_RESOURCES = 3,
    I40E_VIRTCHNL_OP_CONFIG_TX_QUEUE = 4,
    I40E_VIRTCHNL_OP_CONFIG_RX_QUEUE = 5,
    I40E_VIRTCHNL_OP_CONFIG_VSI_QUEUES = 6,
    I40E_VIRTCHNL_OP_CONFIG_IRQ_MAP = 7,
    I40E_VIRTCHNL_OP_ENABLE_QUEUES = 8,
    I40E_VIRTCHNL_OP_DISABLE_QUEUES = 9,
    I40E_VIRTCHNL_OP_ADD_ETHER_ADDRESS = 10,
    I40E_VIRTCHNL_OP_DEL_ETHER_ADDRESS = 11,
    I40E_VIRTCHNL_OP_ADD_VLAN = 12,
    I40E_VIRTCHNL_OP_DEL_VLAN = 13,
    I40E_VIRTCHNL_OP_CONFIG_PROMISCUOUS_MODE = 14,
    I40E_VIRTCHNL_OP_GET_STATS = 15,
    I40E_VIRTCHNL_OP_FCOE = 16,
    I40E_VIRTCHNL_OP_EVENT = 17,
    I40E_VIRTCHNL_OP_CONFIG_RSS = 18,
};

/* Message descriptions and data structures.*/

/* I40E_VIRTCHNL_OP_VERSION
 * VF posts its version number to the PF. PF responds with its version number
 * in the same format, along with a return code.
 * Reply from PF has its major/minor versions also in param0 and param1.
 * If there is a major version mismatch, then the VF cannot operate.
 * If there is a minor version mismatch, then the VF can operate but should
 * add a warning to the system log.
 *
 * This enum element MUST always be specified as == 1, regardless of other
 * changes in the API. The PF must always respond to this message without
 * error regardless of version mismatch.
 */
#define I40E_VIRTCHNL_VERSION_MAJOR        1
#define I40E_VIRTCHNL_VERSION_MINOR        0
struct i40e_virtchnl_version_info {
    uint32_t major;
    uint32_t minor;
};

/* I40E_VIRTCHNL_OP_RESET_VF
 * VF sends this request to PF with no parameters
 * PF does NOT respond! VF driver must delay then poll VFGEN_RSTAT register
 * until reset completion is indicated. The admin queue must be reinitialized
 * after this operation.
 *
 * When reset is complete, PF must ensure that all queues in all VSIs associated
 * with the VF are stopped, all queue configurations in the HMC are set to 0,
 * and all MAC and VLAN filters (except the default MAC address) on all VSIs
 * are cleared.
 */

/* I40E_VIRTCHNL_OP_GET_VF_RESOURCES
 * VF sends this request to PF with no parameters
 * PF responds with an indirect message containing
 * i40e_virtchnl_vf_resource and one or more
 * i40e_virtchnl_vsi_resource structures.
 */

struct i40e_virtchnl_vsi_resource {
    uint16_t vsi_id;
    uint16_t num_queue_pairs;
    int vsi_type; //enum i40e_vsi_type vsi_type;
    uint16_t qset_handle;
    uint8_t default_mac_addr[6];
};
/* VF offload flags */
#define I40E_VIRTCHNL_VF_OFFLOAD_L2    0x00000001
#define I40E_VIRTCHNL_VF_OFFLOAD_FCOE    0x00000004
#define I40E_VIRTCHNL_VF_OFFLOAD_VLAN    0x00010000

struct i40e_virtchnl_vf_resource {
    uint16_t num_vsis;
    uint16_t num_queue_pairs;
    uint16_t max_vectors;
    uint16_t max_mtu;

    uint32_t vf_offload_flags;
    uint32_t max_fcoe_contexts;
    uint32_t max_fcoe_filters;

    struct i40e_virtchnl_vsi_resource vsi_res[1];
};

/* I40E_VIRTCHNL_OP_CONFIG_TX_QUEUE
 * VF sends this message to set up parameters for one TX queue.
 * External data buffer contains one instance of i40e_virtchnl_txq_info.
 * PF configures requested queue and returns a status code.
 */

/* Tx queue config info */
struct i40e_virtchnl_txq_info {
    uint16_t vsi_id;
    uint16_t queue_id;
    uint16_t ring_len;        /* number of descriptors, multiple of 8 */
    uint16_t headwb_enabled;
    uint64_t dma_ring_addr;
    uint64_t dma_headwb_addr;
};

/* I40E_VIRTCHNL_OP_CONFIG_RX_QUEUE
 * VF sends this message to set up parameters for one RX queue.
 * External data buffer contains one instance of i40e_virtchnl_rxq_info.
 * PF configures requested queue and returns a status code.
 */

/* for hsplit_0 field of Rx HMC context */
enum i40e_hmc_obj_rx_hsplit_0 {
    I40E_HMC_OBJ_RX_HSPLIT_0_NO_SPLIT      = 0,
    I40E_HMC_OBJ_RX_HSPLIT_0_SPLIT_L2      = 1,
    I40E_HMC_OBJ_RX_HSPLIT_0_SPLIT_IP      = 2,
    I40E_HMC_OBJ_RX_HSPLIT_0_SPLIT_TCP_UDP = 4,
    I40E_HMC_OBJ_RX_HSPLIT_0_SPLIT_SCTP    = 8,
};

/* Rx queue config info */
struct i40e_virtchnl_rxq_info {
    uint16_t vsi_id;
    uint16_t queue_id;
    uint32_t ring_len;        /* number of descriptors, multiple of 32 */
    uint16_t hdr_size;
    uint16_t splithdr_enabled;
    uint32_t databuffer_size;
    uint32_t max_pkt_size;
    uint64_t dma_ring_addr;
    enum i40e_hmc_obj_rx_hsplit_0 rx_split_pos;
};

/* I40E_VIRTCHNL_OP_CONFIG_VSI_QUEUES
 * VF sends this message to set parameters for all active TX and RX queues
 * associated with the specified VSI.
 * PF configures queues and returns status.
 * If the number of queues specified is greater than the number of queues
 * associated with the VSI, an error is returned and no queues are configured.
 */
struct i40e_virtchnl_queue_pair_info {
    /* NOTE: vsi_id and queue_id should be identical for both queues. */
    struct i40e_virtchnl_txq_info txq;
    struct i40e_virtchnl_rxq_info rxq;
};

struct i40e_virtchnl_vsi_queue_config_info {
    uint16_t vsi_id;
    uint16_t num_queue_pairs;
    struct i40e_virtchnl_queue_pair_info qpair[17];
};

/* I40E_VIRTCHNL_OP_CONFIG_IRQ_MAP
 * VF uses this message to map vectors to queues.
 * The rxq_map and txq_map fields are bitmaps used to indicate which queues
 * are to be associated with the specified vector.
 * The "other" causes are always mapped to vector 0.
 * PF configures interrupt mapping and returns status.
 */
struct i40e_virtchnl_vector_map {
    uint16_t vsi_id;
    uint16_t vector_id;
    uint16_t rxq_map;
    uint16_t txq_map;
    uint16_t rxitr_idx;
    uint16_t txitr_idx;
};

struct i40e_virtchnl_irq_map_info {
    uint16_t num_vectors;
    struct i40e_virtchnl_vector_map vecmap[17];
};

/* I40E_VIRTCHNL_OP_ENABLE_QUEUES
 * I40E_VIRTCHNL_OP_DISABLE_QUEUES
 * VF sends these message to enable or disable TX/RX queue pairs.
 * The queues fields are bitmaps indicating which queues to act upon.
 * (Currently, we only support 16 queues per VF, but we make the field
 * uint32_t to allow for expansion.)
 * PF performs requested action and returns status.
 */
struct i40e_virtchnl_queue_select {
    uint16_t vsi_id;
    uint16_t pad;
    uint32_t rx_queues;
    uint32_t tx_queues;
};

/* I40E_VIRTCHNL_OP_ADD_ETHER_ADDRESS
 * VF sends this message in order to add one or more unicast or multicast
 * address filters for the specified VSI.
 * PF adds the filters and returns status.
 */

/* I40E_VIRTCHNL_OP_DEL_ETHER_ADDRESS
 * VF sends this message in order to remove one or more unicast or multicast
 * filters for the specified VSI.
 * PF removes the filters and returns status.
 */

struct i40e_virtchnl_ether_addr {
    uint8_t addr[6];
    uint8_t pad[2];
};

struct i40e_virtchnl_ether_addr_list {
    uint16_t vsi_id;
    uint16_t num_elements;
    struct i40e_virtchnl_ether_addr list[32];
};

/* I40E_VIRTCHNL_OP_ADD_VLAN
 * VF sends this message to add one or more VLAN tag filters for receives.
 * PF adds the filters and returns status.
 * If a port VLAN is configured by the PF, this operation will return an
 * error to the VF.
 */

/* I40E_VIRTCHNL_OP_DEL_VLAN
 * VF sends this message to remove one or more VLAN tag filters for receives.
 * PF removes the filters and returns status.
 * If a port VLAN is configured by the PF, this operation will return an
 * error to the VF.
 */

struct i40e_virtchnl_vlan_filter_list {
    uint16_t vsi_id;
    uint16_t num_elements;
    uint16_t vlan_id[1];
};

/* I40E_VIRTCHNL_OP_CONFIG_PROMISCUOUS_MODE
 * VF sends VSI id and flags.
 * PF returns status code in retval.
 * Note: we assume that broadcast accept mode is always enabled.
 */
struct i40e_virtchnl_promisc_info {
    uint16_t vsi_id;
    uint16_t flags;
};

#define I40E_FLAG_VF_UNICAST_PROMISC    0x00000001
#define I40E_FLAG_VF_MULTICAST_PROMISC  0x00000002

/* I40E_VIRTCHNL_OP_GET_STATS
 * VF sends this message to request stats for the selected VSI. VF uses
 * the i40e_virtchnl_queue_select struct to specify the VSI. The queue_id
 * field is ignored by the PF.
 *
 * PF replies with struct i40e_eth_stats in an external buffer.
 */

/* Statistics collected by each port, VSI, VEB, and S-channel */
struct i40e_eth_stats {
    uint64_t rx_bytes;                   /* gorc */
    uint64_t rx_unicast;                 /* uprc */
    uint64_t rx_multicast;               /* mprc */
    uint64_t rx_broadcast;               /* bprc */
    uint64_t rx_discards;                /* rdpc */
    uint64_t rx_unknown_protocol;        /* rupp */
    uint64_t tx_bytes;                   /* gotc */
    uint64_t tx_unicast;                 /* uptc */
    uint64_t tx_multicast;               /* mptc */
    uint64_t tx_broadcast;               /* bptc */
    uint64_t tx_discards;                /* tdpc */
    uint64_t tx_errors;                  /* tepc */
};

/* I40E_VIRTCHNL_OP_EVENT
 * PF sends this message to inform the VF driver of events that may affect it.
 * No direct response is expected from the VF, though it may generate other
 * messages in response to this one.
 */
enum i40e_virtchnl_event_codes {
    I40E_VIRTCHNL_EVENT_UNKNOWN = 0,
    I40E_VIRTCHNL_EVENT_LINK_CHANGE,
    I40E_VIRTCHNL_EVENT_RESET_IMPENDING,
    I40E_VIRTCHNL_EVENT_PF_DRIVER_CLOSE,
};
#define I40E_PF_EVENT_SEVERITY_INFO        0
#define I40E_PF_EVENT_SEVERITY_CERTAIN_DOOM    255

#define I40E_LINK_SPEED_100MB_SHIFT     0x1
#define I40E_LINK_SPEED_1000MB_SHIFT    0x2
#define I40E_LINK_SPEED_10GB_SHIFT      0x3
#define I40E_LINK_SPEED_40GB_SHIFT      0x4
#define I40E_LINK_SPEED_20GB_SHIFT      0x5

enum i40e_aq_link_speed {
    I40E_LINK_SPEED_UNKNOWN = 0,
    I40E_LINK_SPEED_100MB   = (1 << I40E_LINK_SPEED_100MB_SHIFT),
    I40E_LINK_SPEED_1GB     = (1 << I40E_LINK_SPEED_1000MB_SHIFT),
    I40E_LINK_SPEED_10GB    = (1 << I40E_LINK_SPEED_10GB_SHIFT),
    I40E_LINK_SPEED_40GB    = (1 << I40E_LINK_SPEED_40GB_SHIFT),
    I40E_LINK_SPEED_20GB    = (1 << I40E_LINK_SPEED_20GB_SHIFT)
};

struct i40e_virtchnl_pf_event {
    enum i40e_virtchnl_event_codes event;
    union {
        struct {
            enum i40e_aq_link_speed link_speed;
            bool link_status;
        } link_event;
    } event_data;

    int severity;
};

/* VF reset states - these are written into the RSTAT register:
 * I40E_VFGEN_RSTAT1 on the PF
 * I40E_VFGEN_RSTAT on the VF
 * When the PF initiates a reset, it writes 0
 * When the reset is complete, it writes 1
 * When the PF detects that the VF has recovered, it writes 2
 * VF checks this register periodically to determine if a reset has occurred,
 * then polls it to know when the reset is complete.
 * If either the PF or VF reads the register while the hardware
 * is in a reset state, it will return DEADBEEF, which, when masked
 * will result in 3.
 */
enum i40e_vfr_states {
    I40E_VFR_INPROGRESS = 0,
    I40E_VFR_COMPLETED,
    I40E_VFR_VFACTIVE,
    I40E_VFR_UNKNOWN,
};

/********************************************/

typedef struct VFIOI40EDevice {
    struct VFIOPCIDevice parent_obj;
    uint32_t regs[(64 * 1024) / 4];

    /* For Admin Queue proxying (always on) */
    MemoryRegion aq_mmio_mem;
    MemoryRegion aq_data_mem;
    void *admin_queue;
    bool arq_active;
    int aq_len;
    int arq_last;
    int arq_ignore; /* nr of arq elements to drop */
    bool arq_fetch_vsi_id;
    int vsi_id;

    /* For RX shadowing (only during migration) */
    MemoryRegion ring_mem;
    MemoryRegion qrx_tail_mem;
    void *ring;
    int ring_head[16];

    /* For debug */
    MemoryRegion mmio_mem;

    /* Intercepted configs from guest */
    struct i40e_virtchnl_irq_map_info irq_map;
    struct i40e_virtchnl_ether_addr_list addr;
    struct i40e_virtchnl_vsi_queue_config_info vsi_config;
    struct i40e_virtchnl_queue_select queue_select;
} VFIOI40EDevice;

typedef struct VFIOI40EDeviceClass {
    PCIDeviceClass parent_class;
    int (*parent_init)(PCIDevice *dev);
} VFIOI40EDeviceClass;
