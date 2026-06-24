/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#if CONFIG_MP_MAX_NUM_CPUS < 2
#error "SMP FPU sharing test requires at least two CPUs"
#endif

#if defined(CONFIG_X86)
#if defined(__GNUC__)
#include "float_regs_x86_gcc.h"
#else
#include "float_regs_x86_other.h"
#endif
#elif defined(CONFIG_ARM)
#if defined(CONFIG_ARMV7_M_ARMV8_M_FP) || defined(CONFIG_ARMV7_R_FP) || \
	defined(CONFIG_CPU_HAS_VFP)
#if defined(__GNUC__) || defined(__ICCARM__)
#include "float_regs_arm_gcc.h"
#else
#include "float_regs_arm_other.h"
#endif
#endif
#elif defined(CONFIG_ARM64)
#if defined(__GNUC__)
#include "float_regs_arm64_gcc.h"
#else
#include "float_regs_arm64_other.h"
#endif
#elif defined(CONFIG_ISA_ARCV2)
#if defined(__GNUC__)
#include "float_regs_arc_gcc.h"
#else
#include "float_regs_arc_other.h"
#endif
#elif defined(CONFIG_RISCV)
#if defined(__GNUC__)
#include "float_regs_riscv_gcc.h"
#else
#include "float_regs_riscv_other.h"
#endif
#elif defined(CONFIG_SPARC)
#include "float_regs_sparc.h"
#elif defined(CONFIG_XTENSA)
#include "float_regs_xtensa.h"
#endif

#include "float_context.h"

#define STACK_SIZE 2048
#define PIN_WAIT_LIMIT 1000

struct fpu_thread_result {
	int expected_cpu;
	int observed_cpu;
	bool done;
};

static struct k_thread fpu_threads[CONFIG_MP_MAX_NUM_CPUS];
static K_THREAD_STACK_ARRAY_DEFINE(fpu_stacks, CONFIG_MP_MAX_NUM_CPUS, STACK_SIZE);
static struct fpu_thread_result results[CONFIG_MP_MAX_NUM_CPUS];
static K_SEM_DEFINE(done_sem, 0, CONFIG_MP_MAX_NUM_CPUS);

#ifdef CONFIG_FPU_SHARING
static struct k_thread migration_thread;
static K_THREAD_STACK_DEFINE(migration_stack, STACK_SIZE);
static K_SEM_DEFINE(migration_loaded, 0, 1);
static K_SEM_DEFINE(migration_continue, 0, 1);
static K_SEM_DEFINE(migration_done, 0, 1);
static int migration_result;
static unsigned int migration_src_cpu;
static unsigned int migration_dst_cpu;
static struct fp_register_set migration_input;
static struct fp_register_set migration_output;
#endif

static int current_cpu_id(void)
{
	unsigned int key = arch_irq_lock();
	int cpu_id = arch_curr_cpu()->id;

	arch_irq_unlock(key);

	return cpu_id;
}

static void fill_fp_pattern(struct fp_register_set *regs, unsigned int seed)
{
	uint8_t *ptr = (uint8_t *)regs;

	for (size_t i = 0; i < SIZEOF_FP_REGISTER_SET; i++) {
		ptr[i] = (uint8_t)(seed + i);
	}
}

static void exercise_fp_round_trip(unsigned int seed)
{
	struct fp_register_set input;
	struct fp_register_set output;

	fill_fp_pattern(&input, seed);
	memset(&output, 0, sizeof(output));

	_load_all_float_registers(&input);
	_store_all_float_registers(&output);

	zassert_mem_equal(&output, &input, SIZEOF_FP_REGISTER_SET,
			  "FP register round-trip failed");
}

static void per_cpu_fpu_thread(void *p1, void *p2, void *p3)
{
	uintptr_t cpu_id = (uintptr_t)p1;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	results[cpu_id].expected_cpu = cpu_id;
	results[cpu_id].observed_cpu = current_cpu_id();

	exercise_fp_round_trip(0x20u + cpu_id);

	results[cpu_id].done = true;
	k_sem_give(&done_sem);
}

ZTEST(fpu_sharing_smp, test_per_cpu_fpu_round_trip)
{
	unsigned int num_cpus = arch_num_cpus();

	zassert_true(num_cpus > 1, "SMP FPU test requires at least two online CPUs");

	for (unsigned int i = 0; i < num_cpus; i++) {
		results[i].expected_cpu = -1;
		results[i].observed_cpu = -1;
		results[i].done = false;

		k_tid_t tid = k_thread_create(&fpu_threads[i], fpu_stacks[i],
					      STACK_SIZE, per_cpu_fpu_thread,
					      (void *)(uintptr_t)i, NULL, NULL,
					      K_PRIO_PREEMPT(1), K_FP_REGS,
					      K_FOREVER);
		int ret = k_thread_cpu_pin(tid, i);

		zassert_ok(ret, "failed to pin FP thread to CPU %u", i);
		k_thread_start(tid);
	}

	for (unsigned int i = 0; i < num_cpus; i++) {
		zassert_ok(k_sem_take(&done_sem, K_SECONDS(5)),
			   "timed out waiting for CPU %u FP thread", i);
	}

	for (unsigned int i = 0; i < num_cpus; i++) {
		zassert_true(results[i].done, "CPU %u FP thread did not complete", i);
		zassert_equal(results[i].observed_cpu, results[i].expected_cpu,
			      "FP thread expected CPU %d but ran on CPU %d",
			      results[i].expected_cpu, results[i].observed_cpu);
	}
}

#ifdef CONFIG_FPU_SHARING
static int pin_blocked_thread(k_tid_t tid, unsigned int cpu)
{
	for (int i = 0; i < PIN_WAIT_LIMIT; i++) {
		int ret = k_thread_cpu_pin(tid, cpu);

		if (ret == 0) {
			return 0;
		}

		if (ret != -EINVAL) {
			return ret;
		}

		k_sleep(K_MSEC(1));
	}

	return -ETIMEDOUT;
}

static void migration_thread_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	if (current_cpu_id() != migration_src_cpu) {
		migration_result = -EINVAL;
		k_sem_give(&migration_loaded);
		k_sem_give(&migration_done);
		return;
	}

	_load_all_float_registers(&migration_input);
	k_sem_give(&migration_loaded);

	if (k_sem_take(&migration_continue, K_SECONDS(5)) != 0) {
		migration_result = -ETIMEDOUT;
		k_sem_give(&migration_done);
		return;
	}

	if (current_cpu_id() != migration_dst_cpu) {
		migration_result = -EINVAL;
		k_sem_give(&migration_done);
		return;
	}

	_store_all_float_registers(&migration_output);

	if (memcmp(&migration_output, &migration_input, SIZEOF_FP_REGISTER_SET) != 0) {
		migration_result = -EFAULT;
	}

	k_sem_give(&migration_done);
}

static int migrate_fp_context(unsigned int src_cpu, unsigned int dst_cpu,
			      unsigned int seed)
{
	k_sem_reset(&migration_loaded);
	k_sem_reset(&migration_continue);
	k_sem_reset(&migration_done);
	migration_result = 0;
	migration_src_cpu = src_cpu;
	migration_dst_cpu = dst_cpu;

	fill_fp_pattern(&migration_input, seed);
	memset(&migration_output, 0, sizeof(migration_output));

	k_tid_t tid = k_thread_create(&migration_thread, migration_stack,
				      STACK_SIZE, migration_thread_entry,
				      NULL, NULL, NULL, K_PRIO_PREEMPT(1),
				      K_FP_REGS, K_FOREVER);
	int ret = k_thread_cpu_pin(tid, src_cpu);

	if (ret != 0) {
		return ret;
	}

	k_thread_start(tid);

	ret = k_sem_take(&migration_loaded, K_SECONDS(5));
	if (ret != 0) {
		return ret;
	}

	if (migration_result != 0) {
		(void)k_thread_join(tid, K_SECONDS(5));
		return migration_result;
	}

	ret = pin_blocked_thread(tid, dst_cpu);
	if (ret != 0) {
		k_sem_give(&migration_continue);
		(void)k_thread_join(tid, K_SECONDS(5));
		return ret;
	}

	k_sem_give(&migration_continue);

	ret = k_sem_take(&migration_done, K_SECONDS(5));
	if (ret != 0) {
		k_thread_abort(tid);
		(void)k_thread_join(tid, K_SECONDS(5));
		return ret;
	}

	ret = k_thread_join(tid, K_SECONDS(5));
	if (ret != 0) {
		return ret;
	}

	return migration_result;
}

ZTEST(fpu_sharing_smp, test_shared_fpu_survives_cpu_migration)
{
	unsigned int num_cpus = arch_num_cpus();

	zassert_true(num_cpus > 1, "SMP FPU migration test requires at least two CPUs");

	for (unsigned int src_cpu = 0; src_cpu < num_cpus; src_cpu++) {
		unsigned int dst_cpu = (src_cpu + 1U) % num_cpus;
		int ret = migrate_fp_context(src_cpu, dst_cpu, 0x80u + src_cpu * 0x10u);

		zassert_ok(ret, "FP migration from CPU %u to CPU %u failed: %d",
			   src_cpu, dst_cpu, ret);
	}
}

#endif

ZTEST_SUITE(fpu_sharing_smp, NULL, NULL, NULL, NULL, NULL);
