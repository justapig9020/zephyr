/*
 * Copyright (c) 2013-2014 Wind River Systems, Inc.
 * Copyright 2024 Arm Limited and/or its affiliates <open-source-office@arm.com>
 * Copyright (c) 2026 Realtek Semiconductor, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/barrier.h>
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
