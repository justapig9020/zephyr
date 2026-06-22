/*
 * Copyright (c) 2026 Realtek Semiconductor, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_ARCH_ARM_INCLUDE_CORTEX_A_R_FPU_H_
#define ZEPHYR_ARCH_ARM_INCLUDE_CORTEX_A_R_FPU_H_

#include <stdbool.h>

#if defined(CONFIG_CPU_HAS_FPU)
void z_arm_floating_point_init(void);
#endif

#if defined(CONFIG_FPU_SHARING) && defined(CONFIG_USE_SWITCH)
struct k_thread;
struct arch_esf;

void arch_flush_local_fpu(void);
void arch_flush_fpu_ipi(unsigned int cpu);
bool z_arm_fpu_trap(struct arch_esf *esf);
void z_arm_fpu_exit_exc(void);
void z_arm_fpu_thread_context_switch(struct k_thread *thread);
#endif

#endif /* ZEPHYR_ARCH_ARM_INCLUDE_CORTEX_A_R_FPU_H_ */
