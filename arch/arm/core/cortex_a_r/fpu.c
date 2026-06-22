/*
 * Copyright (c) 2013-2014 Wind River Systems, Inc.
 * Copyright (c) 2021 BayLibre SAS
 * Copyright 2024 Arm Limited and/or its affiliates <open-source-office@arm.com>
 * Copyright (c) 2026 Realtek Semiconductor, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/kernel_structs.h>
#include <kernel_internal.h>
#include <zephyr/arch/exception.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/barrier.h>
#include <zephyr/arch/cpu.h>
#include <cmsis_core.h>
#include <cortex_a_r/fpu.h>

void z_arm_floating_point_init(void)
{
#if defined(CONFIG_FPU)
	uint32_t reg_val = 0;

	/*
	 * CPACR : Coprocessor Access Control Register -> CP15 1/0/2
	 * comp. ARM Architecture Reference Manual, ARMv7-A and ARMv7-R edition,
	 * chap. B4.1.40
	 *
	 * Must be accessed in >= PL1!
	 * [23..22] = CP11 access control bits,
	 * [21..20] = CP10 access control bits.
	 * 11b = Full access as defined for the respective CP,
	 * 10b = UNDEFINED,
	 * 01b = Access at PL1 only,
	 * 00b = No access.
	 */
	reg_val = __get_CPACR();
	/* Enable PL1 access to CP10, CP11 */
	reg_val |= (CPACR_CP10(CPACR_FA) | CPACR_CP11(CPACR_FA));
	__set_CPACR(reg_val);
	barrier_isync_fence_full();

	/*
	 * FPEXC: Floating-Point Exception Control register
	 * comp. ARM Architecture Reference Manual, ARMv7-A and ARMv7-R edition,
	 * chap. B6.1.38
	 *
	 * Must be accessed in >= PL1!
	 * [31] EX bit = determines which registers comprise the current state
	 *               of the FPU. The effects of setting this bit to 1 are
	 *               subarchitecture defined. If EX=0, the following
	 *               registers contain the complete current state
	 *               information of the FPU and must therefore be saved
	 *               during a context switch:
	 *               * D0-D15
	 *               * D16-D31 if implemented
	 *               * FPSCR
	 *               * FPEXC.
	 * [30] EN bit = Advanced SIMD/Floating Point Extensions enable bit.
	 * [29..00]    = Subarchitecture defined -> not relevant here.
	 */
	__set_FPEXC(FPEXC_EN);
#endif
}

#if defined(CONFIG_FPU_SHARING) && defined(CONFIG_USE_SWITCH)

static ALWAYS_INLINE void vfp_access_enable(void)
{
	__set_FPEXC(FPEXC_EN);
	barrier_isync_fence_full();
}

static ALWAYS_INLINE void vfp_access_disable(void)
{
	__set_FPEXC(0);
	barrier_isync_fence_full();
}

static ALWAYS_INLINE void vfp_save(struct z_arm_vfp_context *context)
{
	context->fpscr = __get_FPSCR();

#if defined(CONFIG_VFP_FEATURE_REGS_S64_D32)
	__asm__ volatile("vstmia %0, {d0-d15};\n\t"
			 "vstmia %1, {d16-d31};\n\t"
			 :
			 : "r"(&context->d[0]), "r"(&context->d[16])
			 : "memory");
#else
	__asm__ volatile("vstmia %0, {s0-s31};\n\t" : : "r"(&context->s[0]) : "memory");
#endif
}

static ALWAYS_INLINE void vfp_restore(struct z_arm_vfp_context *context)
{
	__set_FPSCR(context->fpscr);

#if defined(CONFIG_VFP_FEATURE_REGS_S64_D32)
	__asm__ volatile("vldmia %0, {d0-d15};\n\t"
			 "vldmia %1, {d16-d31};\n\t"
			 :
			 : "r"(&context->d[0]), "r"(&context->d[16])
			 : "memory");
#else
	__asm__ volatile("vldmia %0, {s0-s31};\n\t" : : "r"(&context->s[0]) : "memory");
#endif
}

void arch_flush_local_fpu(void)
{
	__ASSERT(__get_CPSR() & I_BIT, "must be called with IRQs disabled");
	struct k_thread *owner = atomic_ptr_get(&arch_curr_cpu()->arch.fpu_owner);

	if (owner == NULL) {
		return;
	}

	vfp_access_enable();
	vfp_save(&owner->arch.saved_fp_context);
	barrier_dsync_fence_full();
	atomic_ptr_clear(&arch_curr_cpu()->arch.fpu_owner);
	vfp_access_disable();
}

#ifdef CONFIG_SMP
static void flush_owned_fpu(struct k_thread *thread)
{
	__ASSERT(__get_CPSR() & I_BIT, "must be called with IRQs disabled");

	/* search all CPUs for the owner we want */
	for (unsigned int i = 0; i < arch_num_cpus(); i++) {
		if (atomic_ptr_get(&_kernel.cpus[i].arch.fpu_owner) != thread) {
			continue;
		}
		/* we found it live on CPU i */
		if (i == arch_curr_cpu()->id) {
			arch_flush_local_fpu();
		} else {
			/* the FPU context is live on another CPU */
			arch_flush_fpu_ipi(i);

			/*
			 * Wait for it only if this is about the thread
			 * currently running on this CPU. Otherwise the
			 * other CPU running some other thread could regain
			 * ownership the moment it is removed from it and
			 * we would be stuck here.
			 *
			 * Also, if this is for the thread running on this
			 * CPU, then we preemptively flush any live context
			 * on this CPU as well since we're likely to
			 * replace it, and this avoids a deadlock where
			 * two CPUs want to pull each other's FPU context.
			 */
			if (thread == _current) {
				arch_flush_local_fpu();
				while (atomic_ptr_get(&_kernel.cpus[i].arch.fpu_owner) == thread) {
					barrier_dsync_fence_full();
				}
			}
		}

		break;
	}
}
#endif

static void fpu_access_update(struct k_thread *thread, unsigned int exc_update_level)
{
	__ASSERT(__get_CPSR() & I_BIT, "must be called with IRQs disabled");
	if (arch_curr_cpu()->arch.exc_depth == exc_update_level &&
	    atomic_ptr_get(&arch_curr_cpu()->arch.fpu_owner) == thread) {
		vfp_access_enable();
	} else {
		vfp_access_disable();
	}
}

bool z_arm_fpu_trap(struct arch_esf *esf)
{
	__ASSERT(__get_CPSR() & I_BIT, "must be called with IRQs disabled");
	if ((__get_FPEXC() & FPEXC_EN) != 0) {
		return true;
	}

	/* turn on FPU access */
	vfp_access_enable();

	/* save current owner's content if any */
	struct k_thread *owner = atomic_ptr_get(&arch_curr_cpu()->arch.fpu_owner);

	if (owner != NULL) {
		vfp_save(&owner->arch.saved_fp_context);
		barrier_dsync_fence_full();
		atomic_ptr_clear(&arch_curr_cpu()->arch.fpu_owner);
	}

	if (arch_curr_cpu()->arch.exc_depth > 1) {
		/*
		 * The FP access was trapped while already handling an
		 * exception/ISR. We grant access but record no owner, so the FP
		 * registers this nested context uses are not backed by any
		 * thread and cannot be saved/restored. Keep IRQs masked in the
		 * context we return to (set I_BIT in the saved SPSR) so a
		 * further nested interrupt cannot clobber that live, unowned FP
		 * state. Mirrors z_arm64_fpu_trap() on AArch64.
		 */
		esf->basic.xpsr |= I_BIT;
		return false;
	}

#ifdef CONFIG_SMP
	/*
	 * Make sure the FPU context we need isn't live on another CPU.
	 * The current CPU's FPU context is NULL at this point.
	 */
	flush_owned_fpu(_current);
#endif

	_current->base.user_options |= K_FP_REGS;
	/* become new owner */
	atomic_ptr_set(&arch_curr_cpu()->arch.fpu_owner, _current);
	/* restore our content */
	vfp_restore(&_current->arch.saved_fp_context);

	return false;
}

void z_arm_fpu_exit_exc(void)
{
	fpu_access_update(_current, 0);
}

void z_arm_fpu_thread_context_switch(struct k_thread *thread)
{
	fpu_access_update(thread, 0);
}

int arch_float_disable(struct k_thread *thread)
{
	/* Disabling FP is only supported for the current thread, and not
	 * from interrupt context, matching the AArch32 Cortex-A/R contract.
	 */
	if (thread != _current) {
		return -EINVAL;
	}

	if (arch_is_in_isr()) {
		return -EINVAL;
	}

	unsigned int key = arch_irq_lock();

	thread->base.user_options &= ~K_FP_REGS;

#ifdef CONFIG_SMP
	flush_owned_fpu(thread);
#else
	if (thread == atomic_ptr_get(&arch_curr_cpu()->arch.fpu_owner)) {
		arch_flush_local_fpu();
	}
#endif

	arch_irq_unlock(key);

	return 0;
}

int arch_float_enable(struct k_thread *thread, unsigned int options)
{
	ARG_UNUSED(thread);
	ARG_UNUSED(options);

	/* FPU access is granted lazily on the next FP trap; nothing to do. */
	return 0;
}

#endif /* CONFIG_FPU_SHARING && CONFIG_USE_SWITCH */
