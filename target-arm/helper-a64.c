/*
 *  AArch64 specific helpers
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

#include "cpu.h"
#include "exec/gdbstub.h"
#include "helper.h"
#include "qemu/host-utils.h"
#include "sysemu/sysemu.h"
#include "qemu/bitops.h"

uint32_t HELPER(pstate_add)(uint32_t pstate, uint64_t a1, uint64_t a2,
                            uint64_t ar)
{
    int64_t s1 = a1;
    int64_t s2 = a2;
    int64_t sr = ar;

    pstate &= ~(PSTATE_N | PSTATE_Z | PSTATE_C | PSTATE_V);

    if (sr < 0) {
        pstate |= PSTATE_N;
    }

    if (!ar) {
        pstate |= PSTATE_Z;
    }

    if (ar && (ar < a1)) {
        pstate |= PSTATE_C;
    }

    if ((s1 > 0 && s2 > 0 && sr < 0) ||
        (s1 < 0 && s2 < 0 && sr > 0)) {
        pstate |= PSTATE_V;
    }

    return pstate;
}
