/*
 *  AArch64 translation
 *
 *  Copyright (c) 2013 Alexander Graf <agraf@suse.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "cpu.h"
#include "tcg-op.h"
#include "qemu/log.h"
#include "translate.h"
#include "qemu/host-utils.h"

#include "helper.h"
#define GEN_HELPER 1
#include "helper.h"

static TCGv_i64 cpu_X[32];
static TCGv_i64 cpu_pc;
static TCGv_i32 pstate;

static const char *regnames[] = {
    "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",
    "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
    "x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23",
    "x24", "x25", "x26", "x27", "x28", "x29", "lr", "sp"
};

/* initialize TCG globals.  */
void a64_translate_init(void)
{
    int i;

    cpu_pc = tcg_global_mem_new_i64(TCG_AREG0,
                                    offsetof(CPUARMState, pc),
                                    "pc");
    for (i = 0; i < 32; i++) {
        cpu_X[i] = tcg_global_mem_new_i64(TCG_AREG0,
                                          offsetof(CPUARMState, xregs[i]),
                                          regnames[i]);
    }

    pstate = tcg_global_mem_new_i32(TCG_AREG0,
                                    offsetof(CPUARMState, pstate),
                                    "pstate");
}

void aarch64_cpu_dump_state(CPUState *cs, FILE *f,
                            fprintf_function cpu_fprintf, int flags)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    int i;

    cpu_fprintf(f, "PC=%016"PRIx64"  SP=%016"PRIx64"\n",
            env->pc, env->xregs[31]);
    for (i = 0; i < 31; i++) {
        cpu_fprintf(f, "X%02d=%016"PRIx64, i, env->xregs[i]);
        if ((i % 4) == 3) {
            cpu_fprintf(f, "\n");
        } else {
            cpu_fprintf(f, " ");
        }
    }
    cpu_fprintf(f, "PSTATE=%c%c%c%c\n",
        env->pstate & PSTATE_N ? 'n' : '.',
        env->pstate & PSTATE_Z ? 'z' : '.',
        env->pstate & PSTATE_C ? 'c' : '.',
        env->pstate & PSTATE_V ? 'v' : '.');
    cpu_fprintf(f, "\n");

    if (flags & CPU_DUMP_FPU) {
        int numvfpregs = 32;
        for (i = 0; i < numvfpregs; i++) {
            uint64_t v = float64_val(env->vfp.regs[i * 2]);
            uint64_t v1 = float64_val(env->vfp.regs[(i * 2) + 1]);
            if (!v && !v1) {
                /* skip empty registers - makes traces easier to read */
                continue;
            }
            cpu_fprintf(f, "d%02d.0=%016" PRIx64 " " "d%02d.0=%016" PRIx64 "\n",
                        i, v, i, v1);
        }
        cpu_fprintf(f, "FPSCR: %08x\n", (int)env->vfp.xregs[ARM_VFP_FPSCR]);
    }
}

static int get_mem_index(DisasContext *s)
{
    /* XXX only user mode for now */
    return 1;
}

void gen_a64_set_pc_im(uint64_t val)
{
    tcg_gen_movi_i64(cpu_pc, val);
}

static void gen_exception(int excp)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_movi_i32(tmp, excp);
    gen_helper_exception(cpu_env, tmp);
    tcg_temp_free_i32(tmp);
}

static void gen_exception_insn(DisasContext *s, int offset, int excp)
{
    gen_a64_set_pc_im(s->pc - offset);
    gen_exception(excp);
    s->is_jmp = DISAS_JUMP;
}

static void real_unallocated_encoding(DisasContext *s)
{
    fprintf(stderr, "Unknown instruction: %#x\n", s->insn);
    gen_exception_insn(s, 4, EXCP_UDEF);
}

#define unallocated_encoding(s) do { \
    fprintf(stderr, "unallocated encoding at line: %d\n", __LINE__); \
    real_unallocated_encoding(s); \
    } while (0)


static TCGv_i64 cpu_reg(int reg)
{
    if (reg == 31) {
        /* XXX leaks temps */
        return tcg_const_i64(0);
    } else {
        return cpu_X[reg];
    }
}

static TCGv_i64 cpu_reg_sp(int reg)
{
    return cpu_X[reg];
}

static void clear_fpreg(int dest)
{
    int freg_offs = offsetof(CPUARMState, vfp.regs[dest * 2]);
    TCGv_i64 tcg_zero = tcg_const_i64(0);

    tcg_gen_st_i64(tcg_zero, cpu_env, freg_offs);
    tcg_gen_st_i64(tcg_zero, cpu_env, freg_offs + sizeof(float64));
}

static inline bool use_goto_tb(DisasContext *s, int n, uint64_t dest)
{
    /* No direct tb linking with singlestep or deterministic io */
    if (s->singlestep_enabled || (s->tb->cflags & CF_LAST_IO)) {
        return false;
    }

    /* Only link tbs from inside the same guest page */
    if ((s->tb->pc & TARGET_PAGE_MASK) != (dest & TARGET_PAGE_MASK)) {
        return false;
    }

    return true;
}

static inline void gen_goto_tb(DisasContext *s, int n, uint64_t dest)
{
    TranslationBlock *tb;

    tb = s->tb;
    if (use_goto_tb(s, n, dest)) {
        tcg_gen_goto_tb(n);
        gen_a64_set_pc_im(dest);
        tcg_gen_exit_tb((tcg_target_long)tb + n);
        s->is_jmp = DISAS_TB_JUMP;
    } else {
        gen_a64_set_pc_im(dest);
        tcg_gen_exit_tb(0);
        s->is_jmp = DISAS_JUMP;
    }
}

static void handle_b(DisasContext *s, uint32_t insn)
{
    uint64_t addr = s->pc - 4 + (sextract32(insn, 0, 26) << 2);

    if (extract32(insn, 31, 1)) {
        /* BL */
        tcg_gen_movi_i64(cpu_reg(30), s->pc);
    }
    gen_goto_tb(s, 0, addr);
}

static void handle_br(DisasContext *s, uint32_t insn)
{
    int branch_type = extract32(insn, 21, 2);
    int source = extract32(insn, 5, 5);

    switch (branch_type) {
    case 0: /* JMP */
    case 2: /* RET */
        break;
    case 1: /* CALL */
        tcg_gen_movi_i64(cpu_reg(30), s->pc);
        break;
    case 3:
        unallocated_encoding(s);
        return;
    }

    tcg_gen_mov_i64(cpu_pc, cpu_reg(source));
    s->is_jmp = DISAS_JUMP;
}

static void ldst_do_vec_int(DisasContext *s, int freg_offs, TCGv_i64 tcg_addr,
                            int size, bool is_store)
{
    TCGv_i64 tcg_tmp = tcg_temp_new_i64();

    if (is_store) {
        switch (size) {
        case 0:
            tcg_gen_ld8u_i64(tcg_tmp, cpu_env, freg_offs);
            tcg_gen_qemu_st8(tcg_tmp, tcg_addr, get_mem_index(s));
            break;
        case 1:
            tcg_gen_ld16u_i64(tcg_tmp, cpu_env, freg_offs);
            tcg_gen_qemu_st16(tcg_tmp, tcg_addr, get_mem_index(s));
            break;
        case 2:
            tcg_gen_ld32u_i64(tcg_tmp, cpu_env, freg_offs);
            tcg_gen_qemu_st32(tcg_tmp, tcg_addr, get_mem_index(s));
            break;
        case 4:
            tcg_gen_ld_i64(tcg_tmp, cpu_env, freg_offs);
            tcg_gen_qemu_st64(tcg_tmp, tcg_addr, get_mem_index(s));
            freg_offs += sizeof(uint64_t);
            tcg_gen_addi_i64(tcg_addr, tcg_addr, sizeof(uint64_t));
            /* fall through */
        case 3:
            tcg_gen_ld_i64(tcg_tmp, cpu_env, freg_offs);
            tcg_gen_qemu_st64(tcg_tmp, tcg_addr, get_mem_index(s));
            break;
        }
    } else {
        switch (size) {
        case 0:
            tcg_gen_qemu_ld8u(tcg_tmp, tcg_addr, get_mem_index(s));
            tcg_gen_st8_i64(tcg_tmp, cpu_env, freg_offs);
            break;
        case 1:
            tcg_gen_qemu_ld16u(tcg_tmp, tcg_addr, get_mem_index(s));
            tcg_gen_st16_i64(tcg_tmp, cpu_env, freg_offs);
            break;
        case 2:
            tcg_gen_qemu_ld32u(tcg_tmp, tcg_addr, get_mem_index(s));
            tcg_gen_st32_i64(tcg_tmp, cpu_env, freg_offs);
            break;
        case 4:
            tcg_gen_qemu_ld64(tcg_tmp, tcg_addr, get_mem_index(s));
            tcg_gen_st_i64(tcg_tmp, cpu_env, freg_offs);
            freg_offs += sizeof(uint64_t);
            tcg_gen_addi_i64(tcg_addr, tcg_addr, sizeof(uint64_t));
            /* fall through */
        case 3:
            tcg_gen_qemu_ld64(tcg_tmp, tcg_addr, get_mem_index(s));
            tcg_gen_st_i64(tcg_tmp, cpu_env, freg_offs);
            break;
        }
    }

    tcg_temp_free_i64(tcg_tmp);
}

static void ldst_do_vec(DisasContext *s, int dest, TCGv_i64 tcg_addr_real,
                        int size, bool is_store)
{
    TCGv_i64 tcg_addr = tcg_temp_new_i64();
    int freg_offs = offsetof(CPUARMState, vfp.regs[dest * 2]);

    /* we don't want to modify the caller's tcg_addr */
    tcg_gen_mov_i64(tcg_addr, tcg_addr_real);

    if (!is_store) {
        /* normal ldst clears non-loaded bits */
        clear_fpreg(dest);
    }

    ldst_do_vec_int(s, freg_offs, tcg_addr, size, is_store);

    tcg_temp_free(tcg_addr);
}

static void ldst_do_gpr(DisasContext *s, int dest, TCGv_i64 tcg_addr, int size,
                        bool is_store, bool is_signed)
{
    if (is_store) {
        switch (size) {
        case 0:
            tcg_gen_qemu_st8(cpu_reg(dest), tcg_addr, get_mem_index(s));
            break;
        case 1:
            tcg_gen_qemu_st16(cpu_reg(dest), tcg_addr, get_mem_index(s));
            break;
        case 2:
            tcg_gen_qemu_st32(cpu_reg(dest), tcg_addr, get_mem_index(s));
            break;
        case 3:
            tcg_gen_qemu_st64(cpu_reg(dest), tcg_addr, get_mem_index(s));
            break;
        }
    } else {
        if (is_signed) {
            /* XXX check what impact regsize has */
            switch (size) {
            case 0:
                tcg_gen_qemu_ld8s(cpu_reg(dest), tcg_addr, get_mem_index(s));
                break;
            case 1:
                tcg_gen_qemu_ld16s(cpu_reg(dest), tcg_addr, get_mem_index(s));
                break;
            case 2:
                tcg_gen_qemu_ld32s(cpu_reg(dest), tcg_addr, get_mem_index(s));
                break;
            case 3:
                tcg_gen_qemu_ld64(cpu_reg(dest), tcg_addr, get_mem_index(s));
                break;
            }
        } else {
            switch (size) {
            case 0:
                tcg_gen_qemu_ld8u(cpu_reg(dest), tcg_addr, get_mem_index(s));
                break;
            case 1:
                tcg_gen_qemu_ld16u(cpu_reg(dest), tcg_addr, get_mem_index(s));
                break;
            case 2:
                tcg_gen_qemu_ld32u(cpu_reg(dest), tcg_addr, get_mem_index(s));
                break;
            case 3:
                tcg_gen_qemu_ld64(cpu_reg(dest), tcg_addr, get_mem_index(s));
                break;
            }
        }
    }
}

static void ldst_do(DisasContext *s, int dest, TCGv_i64 tcg_addr, int size,
                    bool is_store, bool is_signed, bool is_vector)
{
    if (is_vector) {
        ldst_do_vec(s, dest, tcg_addr, size, is_store);
    } else {
        ldst_do_gpr(s, dest, tcg_addr, size, is_store, is_signed);
    }
}

static void handle_stp(DisasContext *s, uint32_t insn)
{
    int rt = extract32(insn, 0, 5);
    int rn = extract32(insn, 5, 5);
    int rt2 = extract32(insn, 10, 5);
    int offset = sextract32(insn, 15, 7);
    int is_store = !extract32(insn, 22, 1);
    int type = extract32(insn, 23, 2);
    int is_vector = extract32(insn, 26, 1);
    int is_signed = extract32(insn, 30, 1);
    int is_32bit = !extract32(insn, 31, 1);
    TCGv_i64 tcg_addr;
    bool postindex;
    bool wback;
    int size = is_32bit ? 2 : 3;

    if (is_vector) {
        size = 2 + extract32(insn, 30, 2);
    }

    switch (type) {
    default:
    case 0:
        postindex = false;
        wback = false;
        break;
    case 1: /* STP (post-index) */
        postindex = true;
        wback = true;
        break;
    case 2: /* STP (signed offset */
        postindex = false;
        wback = false;
        break;
    case 3: /* STP (pre-index) */
        postindex = false;
        wback = true;
        break;
    }

    if (is_signed && !is_32bit) {
        unallocated_encoding(s);
        return;
    }

    offset <<= size;

    tcg_addr = tcg_temp_new_i64();
    if (rn == 31) {
        /* XXX check SP alignment */
    }
    tcg_gen_mov_i64(tcg_addr, cpu_reg_sp(rn));

    if (!postindex) {
        tcg_gen_addi_i64(tcg_addr, tcg_addr, offset);
    }

    ldst_do(s, rt, tcg_addr, size, is_store, is_signed, is_vector);
    tcg_gen_addi_i64(tcg_addr, tcg_addr, 1 << size);
    ldst_do(s, rt2, tcg_addr, size, is_store, is_signed, is_vector);
    tcg_gen_subi_i64(tcg_addr, tcg_addr, 1 << size);

    if (wback) {
        if (postindex) {
            tcg_gen_addi_i64(tcg_addr, tcg_addr, offset);
        }
        tcg_gen_mov_i64(cpu_reg_sp(rn), tcg_addr);
    }

    tcg_temp_free_i64(tcg_addr);
}

static void handle_ldarx(DisasContext *s, uint32_t insn)
{
    int rt = extract32(insn, 0, 5);
    int rn = extract32(insn, 5, 5);
    int rt2 = extract32(insn, 10, 5);
    int is_atomic = !extract32(insn, 15, 1);
    int rs = extract32(insn, 16, 5);
    int is_pair = extract32(insn, 21, 1);
    int is_store = !extract32(insn, 22, 1);
    int is_excl = !extract32(insn, 23, 1);
    int size = extract32(insn, 30, 2);
    TCGv_i64 tcg_addr;

    tcg_addr = tcg_temp_new_i64();
    if (rn == 31) {
        /* XXX check SP alignment */
    }
    tcg_gen_mov_i64(tcg_addr, cpu_reg_sp(rn));

    if (is_atomic) {
        /* XXX add locking */
    }
    if (is_store && is_excl) {
        /* XXX find out what status it wants */
        tcg_gen_movi_i64(cpu_reg(rs), 0);
    }

    ldst_do_gpr(s, rt, tcg_addr, size, is_store, false);
    if (is_pair) {
        tcg_gen_addi_i64(tcg_addr, tcg_addr, 1 << size);
        ldst_do_gpr(s, rt2, tcg_addr, size, is_store, false);
    }

    tcg_temp_free_i64(tcg_addr);
}

void disas_a64_insn(CPUARMState *env, DisasContext *s)
{
    uint32_t insn;

    insn = arm_ldl_code(env, s->pc, s->bswap_code);
    s->insn = insn;
    s->pc += 4;

    /* One-off branch instruction layout */
    switch (insn >> 26) {
    case 0x25:
    case 0x5:
        handle_b(s, insn);
        goto insn_done;
    case 0x35:
        if ((insn & 0xff9ffc1f) == 0xd61f0000) {
            handle_br(s, insn);
            goto insn_done;
        }
        break;
    }

    /* Typical major opcode encoding */
    switch ((insn >> 24) & 0x1f) {
    case 0x08:
    case 0x09:
        if (extract32(insn, 29, 1)) {
            handle_stp(s, insn);
        } else {
            handle_ldarx(s, insn);
        }
        break;
    case 0x0c:
        if (extract32(insn, 29, 1)) {
            handle_stp(s, insn);
        } else {
            unallocated_encoding(s);
        }
        break;
    case 0x0d:
        if (extract32(insn, 29, 1)) {
            handle_stp(s, insn);
        } else {
            unallocated_encoding(s);
        }
        break;
    default:
        unallocated_encoding(s);
        break;
    }

insn_done:
    if (unlikely(s->singlestep_enabled) && (s->is_jmp == DISAS_TB_JUMP)) {
        /* go through the main loop for single step */
        s->is_jmp = DISAS_JUMP;
    }
}
