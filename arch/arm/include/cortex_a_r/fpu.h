/*
 * Copyright (c) 2026 Realtek Semiconductor, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_ARCH_ARM_INCLUDE_CORTEX_A_R_FPU_H_
#define ZEPHYR_ARCH_ARM_INCLUDE_CORTEX_A_R_FPU_H_

#if defined(CONFIG_CPU_HAS_FPU)
void z_arm_floating_point_init(void);
#endif

#endif /* ZEPHYR_ARCH_ARM_INCLUDE_CORTEX_A_R_FPU_H_ */
