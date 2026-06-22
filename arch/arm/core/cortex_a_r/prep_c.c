/*
 * Copyright (c) 2013-2014 Wind River Systems, Inc.
 * Copyright 2024 Arm Limited and/or its affiliates <open-source-office@arm.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Full C support initialization
 *
 *
 * Initialization of full C support: zero the .bss, copy the .data if XIP,
 * call z_cstart().
 *
 * Stack is available in this module, but not the global data/bss until their
 * initialization is performed.
 */

#include <zephyr/kernel.h>
#include <kernel_internal.h>
#include <zephyr/linker/linker-defs.h>
#include <zephyr/arch/arm/cortex_a_r/lib_helpers.h>
#include <zephyr/platform/hooks.h>
#include <zephyr/arch/cache.h>
#include <zephyr/arch/common/xip.h>
#include <zephyr/arch/common/init.h>
#include <cortex_a_r/fpu.h>

#if defined(CONFIG_ARMV7_R) || defined(CONFIG_ARMV7_A)
#include <cortex_a_r/stack.h>
#endif

#ifdef CONFIG_ARM_MPU
extern void z_arm_mpu_init(void);
extern void z_arm_configure_static_mpu_regions(void);
#elif defined(CONFIG_ARM_AARCH32_MMU)
extern int z_arm_mmu_init(void);
#endif

extern FUNC_NORETURN void z_cstart(void);

/**
 *
 * @brief Prepare to and run C code
 *
 * This routine prepares for the execution of and runs C code.
 *
 */
FUNC_NORETURN void z_prep_c(void)
{
	soc_prep_hook();

	/* Initialize tpidruro with our struct _cpu instance address */
	write_tpidruro((uintptr_t)&_kernel.cpus[0]);

#if defined(CONFIG_CPU_HAS_FPU)
	z_arm_floating_point_init();
#endif
	arch_bss_zero();
	arch_data_copy();
#if ((defined(CONFIG_ARMV7_R) || defined(CONFIG_ARMV7_A)) && defined(CONFIG_INIT_STACKS))
	z_arm_init_stacks();
#endif
	z_arm_interrupt_init();
#if CONFIG_ARCH_CACHE
	arch_cache_init();
#endif
#ifdef CONFIG_ARM_MPU
	z_arm_mpu_init();
	z_arm_configure_static_mpu_regions();
#elif defined(CONFIG_ARM_AARCH32_MMU)
	z_arm_mmu_init();
#endif
	z_cstart();
	CODE_UNREACHABLE;
}
