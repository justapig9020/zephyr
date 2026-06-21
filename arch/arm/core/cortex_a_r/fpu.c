/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/kernel_structs.h>
#include <zephyr/arch/exception.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/barrier.h>
#include <zephyr/arch/cpu.h>
#include <cmsis_core.h>
#include <cortex_a_r/fpu.h>

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
	context->fpexc = __get_FPEXC();

#if defined(CONFIG_VFP_FEATURE_REGS_S64_D32)
	__asm__ volatile (
		"vstmia %0, {d0-d15};\n\t"
		"vstmia %1, {d16-d31};\n\t"
		:
		: "r" (&context->d[0]), "r" (&context->d[16])
		: "memory"
	);
#else
	__asm__ volatile (
		"vstmia %0, {s0-s31};\n\t"
		:
		: "r" (&context->s[0])
		: "memory"
	);
#endif
}

static ALWAYS_INLINE void vfp_restore(struct z_arm_vfp_context *context)
{
	__set_FPSCR(context->fpscr);

#if defined(CONFIG_VFP_FEATURE_REGS_S64_D32)
	__asm__ volatile (
		"vldmia %0, {d0-d15};\n\t"
		"vldmia %1, {d16-d31};\n\t"
		:
		: "r" (&context->d[0]), "r" (&context->d[16])
		: "memory"
	);
#else
	__asm__ volatile (
		"vldmia %0, {s0-s31};\n\t"
		:
		: "r" (&context->s[0])
		: "memory"
	);
#endif
}

void arch_flush_local_fpu(void)
{
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
	for (unsigned int i = 0; i < arch_num_cpus(); i++) {
		if (atomic_ptr_get(&_kernel.cpus[i].arch.fpu_owner) != thread) {
			continue;
		}

		if (i == arch_curr_cpu()->id) {
			arch_flush_local_fpu();
		} else {
			arch_flush_fpu_ipi(i);

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
	if (arch_curr_cpu()->arch.exc_depth == exc_update_level &&
	    atomic_ptr_get(&arch_curr_cpu()->arch.fpu_owner) == thread) {
		vfp_access_enable();
	} else {
		vfp_access_disable();
	}
}

bool z_arm_fpu_trap(struct arch_esf *esf)
{
	ARG_UNUSED(esf);

	if ((__get_FPEXC() & FPEXC_EN) != 0) {
		return true;
	}

	vfp_access_enable();

	struct k_thread *owner = atomic_ptr_get(&arch_curr_cpu()->arch.fpu_owner);

	if (owner != NULL) {
		vfp_save(&owner->arch.saved_fp_context);
		barrier_dsync_fence_full();
		atomic_ptr_clear(&arch_curr_cpu()->arch.fpu_owner);
	}

	if (arch_curr_cpu()->arch.exc_depth > 1) {
		return false;
	}

#ifdef CONFIG_SMP
	flush_owned_fpu(_current);
#endif

	_current->base.user_options |= K_FP_REGS;
	atomic_ptr_set(&arch_curr_cpu()->arch.fpu_owner, _current);
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
	if (thread != NULL) {
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
	}

	return 0;
}

int arch_float_enable(struct k_thread *thread, unsigned int options)
{
	ARG_UNUSED(thread);
	ARG_UNUSED(options);

	return 0;
}

#endif /* CONFIG_FPU_SHARING && CONFIG_USE_SWITCH */
