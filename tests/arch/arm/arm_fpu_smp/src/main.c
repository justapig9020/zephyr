/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#if CONFIG_MP_MAX_NUM_CPUS < 2
#error "SMP FPU test requires at least two CPUs"
#endif

#define STACK_SIZE 1024

struct fpu_thread_result {
	int expected_cpu;
	int observed_cpu;
	bool done;
};

static struct k_thread fpu_threads[CONFIG_MP_MAX_NUM_CPUS];
static K_THREAD_STACK_ARRAY_DEFINE(fpu_stacks, CONFIG_MP_MAX_NUM_CPUS, STACK_SIZE);
static struct fpu_thread_result results[CONFIG_MP_MAX_NUM_CPUS];
static K_SEM_DEFINE(done_sem, 0, CONFIG_MP_MAX_NUM_CPUS);

static int current_cpu_id(void)
{
	unsigned int key = arch_irq_lock();
	int cpu_id = arch_curr_cpu()->id;

	arch_irq_unlock(key);

	return cpu_id;
}

static void exercise_vfp(unsigned int cpu_id)
{
	uint32_t input[16] __aligned(8);
	uint32_t output[16] __aligned(8) = { 0 };

	for (size_t i = 0; i < ARRAY_SIZE(input); i++) {
		input[i] = 0x3f800000u + (cpu_id << 8) + i;
	}

	__asm__ volatile (
		"vldmia %0, {s0-s15};\n\t"
		"vstmia %1, {s0-s15};\n\t"
		:
		: "r" (&input[0]), "r" (&output[0])
		: "memory"
	);

	zassert_mem_equal(output, input, sizeof(input),
			  "CPU %u could not round-trip VFP registers", cpu_id);
}

static void fpu_thread(void *p1, void *p2, void *p3)
{
	uintptr_t cpu_id = (uintptr_t)p1;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	results[cpu_id].expected_cpu = cpu_id;
	results[cpu_id].observed_cpu = current_cpu_id();

	exercise_vfp(cpu_id);

	results[cpu_id].done = true;
	k_sem_give(&done_sem);
}

ZTEST(arm_fpu_smp, test_unshared_fpu_is_enabled_on_each_cpu)
{
	for (int i = 0; i < CONFIG_MP_MAX_NUM_CPUS; i++) {
		results[i].expected_cpu = -1;
		results[i].observed_cpu = -1;
		results[i].done = false;

		k_tid_t tid = k_thread_create(&fpu_threads[i], fpu_stacks[i],
					      STACK_SIZE, fpu_thread,
					      (void *)(uintptr_t)i, NULL, NULL,
					      K_PRIO_PREEMPT(1), 0, K_FOREVER);
		int ret = k_thread_cpu_pin(tid, i);

		zassert_equal(ret, 0, "failed to pin thread to CPU %d", i);
		k_thread_start(tid);
	}

	for (int i = 0; i < CONFIG_MP_MAX_NUM_CPUS; i++) {
		zassert_ok(k_sem_take(&done_sem, K_SECONDS(5)),
			   "timed out waiting for CPU %d FPU thread", i);
	}

	for (int i = 0; i < CONFIG_MP_MAX_NUM_CPUS; i++) {
		zassert_true(results[i].done, "CPU %d FPU thread did not complete", i);
		zassert_equal(results[i].observed_cpu, results[i].expected_cpu,
			      "FPU thread expected CPU %d but ran on CPU %d",
			      results[i].expected_cpu, results[i].observed_cpu);
	}
}

ZTEST_SUITE(arm_fpu_smp, NULL, NULL, NULL, NULL, NULL);
