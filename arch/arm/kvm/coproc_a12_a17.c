/*
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Copyright (C) 2013 - ARM Ltd
 *
 * Authors: Rusty Russell <rusty@rustcorp.au>
 *          Christoffer Dall <c.dall@virtualopensystems.com>
 *          Jonathan Austin <jonathan.austin@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include <linux/kvm_host.h>
#include <asm/kvm_coproc.h>
#include <asm/kvm_emulate.h>
#include <linux/init.h>

#include "coproc.h"

static bool access_scuctlr(struct kvm_vcpu *vcpu,
			  const struct coproc_params *p,
			  const struct coproc_reg *r)
{
	u32 scuctlr, ncores;

	if (p->is_write)
		return ignore_write(vcpu, p);

	asm volatile("mrc p15, 1, %0, c9, c0, 4\n" : "=r" (scuctlr));
	/* masking out every bit except the reserved ones */
	scuctlr &= 0x0000ff0c;

	/* this register supports only up to 4 CPUs */
	ncores = atomic_read(&vcpu->kvm->online_vcpus);
	ncores = min(ncores, 4U);
printk("%s: ncores %d\n", __func__, ncores);

	scuctlr |= ncores - 1;
	/* one SMP bit for each online CPU */
	scuctlr |= ((1 << ncores) - 1) << 4;

	/* mark non-existent cores as not present in the power status bits */
	scuctlr |= 0x11110000 & ~(((1 << ncores * 4) - 1) << 16);

	*vcpu_reg(vcpu, p->Rt1) = scuctlr;
	return true;
}

/*
 * Cortex-A12/A17 specific CP15 registers.
 * CRn denotes the primary register number, but is copied to the CRm in the
 * user space API for 64-bit register access in line with the terminology used
 * in the ARM ARM.
 * Important: Must be sorted ascending by CRn, CRM, Op1, Op2 and with 64-bit
 *            registers preceding 32-bit ones.
 */
static const struct coproc_reg a12_a17_regs[] = {
	/* SCTLR: swapped by interrupt.S. */
	{ CRn( 1), CRm( 0), Op1( 0), Op2( 0), is32,
			access_vm_reg, reset_val, c1_SCTLR, 0x00C50878 },
	/* SCUCTLR RO/WI */
	{ CRn( 9), CRm( 0), Op1( 1), Op2( 4), is32, access_scuctlr },
};

static struct kvm_coproc_target_table a12_a17_target_table = {
	.target = KVM_ARM_TARGET_CORTEX_A12_A17,
	.table = a12_a17_regs,
	.num = ARRAY_SIZE(a12_a17_regs),
};

static int __init coproc_a12_a17_init(void)
{
	kvm_register_target_coproc_table(&a12_a17_target_table);
	return 0;
}
late_initcall(coproc_a12_a17_init);
