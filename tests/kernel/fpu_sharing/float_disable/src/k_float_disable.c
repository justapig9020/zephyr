/*
 * Copyright (c) 2019 Nordic Semiconductor ASA.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/sys/barrier.h>

#define STACKSIZE 1024

/* Priority level of the threads used in this test.
 * The priority level, itself, is arbitrary; we only
 * want to ensure they are cooperative threads.
 */
#define PRIORITY  K_PRIO_COOP(0)

#if defined(CONFIG_X86) && defined(CONFIG_X86_SSE)
#define K_FP_OPTS (K_FP_REGS | K_SSE_REGS)
#elif defined(CONFIG_X86) || defined(CONFIG_ARM64) || defined(CONFIG_ARM) || \
	defined(CONFIG_SPARC)
#define K_FP_OPTS K_FP_REGS
#else
#error "Architecture not supported for this test"
#endif

struct k_thread usr_fp_thread;
K_THREAD_STACK_DEFINE(usr_fp_thread_stack, STACKSIZE);

ZTEST_BMEM static volatile int test_ret = TC_PASS;
ZTEST_BMEM static volatile float fp_sink;

static void usr_fp_thread_entry_1(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	k_yield();
}

#if defined(CONFIG_ARM64) || defined(CONFIG_ARM) || \
	(defined(CONFIG_X86) && defined(CONFIG_LAZY_FPU_SHARING))
#define K_FLOAT_DISABLE_SYSCALL_RETVAL 0
#else
#define K_FLOAT_DISABLE_SYSCALL_RETVAL -ENOTSUP
#endif

#if defined(CONFIG_ARM64) || \
	(defined(CONFIG_ARM) && defined(CONFIG_CPU_AARCH32_CORTEX_A) && \
	 defined(CONFIG_USE_SWITCH)) || \
	(defined(CONFIG_X86) && defined(CONFIG_LAZY_FPU_SHARING))
#define K_FLOAT_DISABLE_ANY_THREAD 1
#else
#define K_FLOAT_DISABLE_ANY_THREAD 0
#endif

static void usr_fp_thread_entry_2(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	k_yield();

	/* System call to disable FP mode */
	if (k_float_disable(k_current_get()) != K_FLOAT_DISABLE_SYSCALL_RETVAL) {

		TC_ERROR("k_float_disable() fail - should never see this\n");

		test_ret = TC_FAIL;
	}
}

ZTEST(k_float_disable, test_k_float_disable_common)
{
	test_ret = TC_PASS;

	/* Set thread priority level to the one used
	 * in this test suite for cooperative threads.
	 */
	k_thread_priority_set(k_current_get(), PRIORITY);

	/* Create an FP-capable User thread with the same cooperative
	 * priority as the current thread.
	 */
	k_thread_create(&usr_fp_thread, usr_fp_thread_stack, STACKSIZE,
		usr_fp_thread_entry_1, NULL, NULL, NULL,
		PRIORITY, K_USER | K_FP_OPTS,
		K_NO_WAIT);

	/* Yield will swap-in usr_fp_thread */
	k_yield();

	/* Verify K_FP_OPTS are set properly */
	zassert_true(
		(usr_fp_thread.base.user_options & K_FP_OPTS) != 0,
		"usr_fp_thread FP options not set (0x%0x)",
		usr_fp_thread.base.user_options);

/*
 * ARM Cortex-M and the legacy ARM arch_swap path restrict k_float_disable()
 * to the current thread only. ARM64 and ARM Cortex-A/R arch_switch support
 * disabling FPU for any thread because SMP configurations require
 * flush_owned_fpu() to manage FPU state across multiple CPUs.
 */
#if defined(CONFIG_ARM) && !defined(CONFIG_ARM64) && !K_FLOAT_DISABLE_ANY_THREAD
	/* Verify FP mode can only be disabled for current thread */
	zassert_true((k_float_disable(&usr_fp_thread) == -EINVAL),
		"k_float_disable() successful on thread other than current!");

	/* Verify K_FP_OPTS are still set */
	zassert_true(
		(usr_fp_thread.base.user_options & K_FP_OPTS) != 0,
		"usr_fp_thread FP options cleared");
#elif K_FLOAT_DISABLE_ANY_THREAD
	zassert_true((k_float_disable(&usr_fp_thread) == 0),
		"k_float_disable() failure");

	/* Verify K_FP_OPTS are now cleared */
	zassert_true(
		(usr_fp_thread.base.user_options & K_FP_OPTS) == 0,
		"usr_fp_thread FP options not clear (0x%0x)",
		usr_fp_thread.base.user_options);
#else
	/* Verify k_float_disable() is not supported */
	zassert_true((k_float_disable(&usr_fp_thread) == -ENOTSUP),
		"k_float_disable() successful when not supported");
#endif
}

ZTEST(k_float_disable, test_k_float_disable_syscall)
{
	test_ret = TC_PASS;

	if (IS_ENABLED(CONFIG_SMP) && (CONFIG_MP_MAX_NUM_CPUS > 1)) {
		TC_PRINT("This syscall scheduling test is covered by non-SMP scenarios.\n");
		ztest_test_skip();
	}

	k_thread_priority_set(k_current_get(), PRIORITY);

	/* Create an FP-capable User thread with the same cooperative
	 * priority as the current thread. The thread will disable its
	 * FP mode.
	 */
	k_thread_create(&usr_fp_thread, usr_fp_thread_stack, STACKSIZE,
		usr_fp_thread_entry_2, NULL, NULL, NULL,
		PRIORITY, K_INHERIT_PERMS | K_USER | K_FP_OPTS,
		K_NO_WAIT);

	/* Yield will swap-in usr_fp_thread */
	k_yield();

	/* Verify K_FP_OPTS are set properly */
	zassert_true(
		(usr_fp_thread.base.user_options & K_FP_OPTS) != 0,
		"usr_fp_thread FP options not set (0x%0x)",
		usr_fp_thread.base.user_options);

	/* Yield will swap-in usr_fp_thread */
	k_yield();

#if defined(CONFIG_ARM64) || defined(CONFIG_ARM) || \
	(defined(CONFIG_X86) && defined(CONFIG_LAZY_FPU_SHARING))

	/* Verify K_FP_OPTS are now cleared by the user thread itself */
	zassert_true(
		(usr_fp_thread.base.user_options & K_FP_OPTS) == 0,
		"usr_fp_thread FP options not clear (0x%0x)",
		usr_fp_thread.base.user_options);

	/* test_ret is volatile, static analysis says we can't use in assert */
	bool ok = test_ret == TC_PASS;

	zassert_true(ok, "");
#else
	/* Check skipped for x86 without support for Lazy FP Sharing */
#endif
}

#if defined(CONFIG_SMP) && (CONFIG_MP_MAX_NUM_CPUS > 1) && \
	defined(CONFIG_SCHED_CPU_MASK) && K_FLOAT_DISABLE_ANY_THREAD && \
	(defined(CONFIG_ARM64) || defined(CONFIG_ARM))

struct k_thread remote_fp_thread;
K_THREAD_STACK_DEFINE(remote_fp_thread_stack, STACKSIZE);
struct k_thread remote_controller_thread;
K_THREAD_STACK_DEFINE(remote_controller_thread_stack, STACKSIZE);

static K_SEM_DEFINE(remote_fp_loaded, 0, 1);
static K_SEM_DEFINE(remote_fp_release, 0, 1);
static K_SEM_DEFINE(remote_controller_done, 0, 1);

static int remote_controller_result;

static void remote_fp_thread_entry(void *p1, void *p2, void *p3)
{
	volatile float a = 1.25f;
	volatile float b = 2.5f;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	fp_sink = (a * b) + 0.5f;
	k_sem_give(&remote_fp_loaded);

	if (k_sem_take(&remote_fp_release, K_SECONDS(5)) != 0) {
		test_ret = TC_FAIL;
	}
}

static void remote_controller_thread_entry(void *p1, void *p2, void *p3)
{
	k_tid_t tid;
	int ret;
	bool release_target = false;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	tid = k_thread_create(&remote_fp_thread, remote_fp_thread_stack,
			      STACKSIZE, remote_fp_thread_entry,
			      NULL, NULL, NULL, PRIORITY, K_FP_OPTS,
			      K_FOREVER);

	ret = k_thread_cpu_pin(tid, 1);
	if (ret != 0) {
		remote_controller_result = ret;
		goto done;
	}

	k_thread_start(tid);

	ret = k_sem_take(&remote_fp_loaded, K_SECONDS(5));
	if (ret != 0) {
		remote_controller_result = ret;
		goto abort_target;
	}
	release_target = true;

	ret = k_float_disable(tid);
	if (ret != 0) {
		remote_controller_result = ret;
		goto release_target;
	}

release_target:
	k_sem_give(&remote_fp_release);
	release_target = false;

	ret = k_thread_join(tid, K_SECONDS(5));
	if (ret != 0) {
		k_thread_abort(tid);
	}

	if ((remote_controller_result == 0) && (ret != 0)) {
		remote_controller_result = ret;
	}

	if ((remote_controller_result == 0) && (test_ret != TC_PASS)) {
		remote_controller_result = -EIO;
	}

	goto done;

abort_target:
	k_thread_abort(tid);
	(void)k_thread_join(tid, K_SECONDS(5));

done:
	if (release_target) {
		k_sem_give(&remote_fp_release);
	}
	k_sem_give(&remote_controller_done);
}

ZTEST(k_float_disable, test_k_float_disable_remote_stale_owner)
{
	k_tid_t tid;
	int ret;

	test_ret = TC_PASS;
	remote_controller_result = 0;
	k_sem_reset(&remote_fp_loaded);
	k_sem_reset(&remote_fp_release);
	k_sem_reset(&remote_controller_done);

	tid = k_thread_create(&remote_controller_thread,
			      remote_controller_thread_stack, STACKSIZE,
			      remote_controller_thread_entry,
			      NULL, NULL, NULL, PRIORITY, 0, K_FOREVER);

	ret = k_thread_cpu_pin(tid, 0);
	zassert_ok(ret, "failed to pin controller to CPU0");

	k_thread_start(tid);

	ret = k_sem_take(&remote_controller_done, K_SECONDS(10));
	if (ret != 0) {
		k_thread_abort(tid);
	}

	zassert_ok(ret, "controller thread did not finish");

	ret = k_thread_join(tid, K_SECONDS(5));
	zassert_ok(ret, "controller thread did not join");

	zassert_ok(remote_controller_result, "remote stale-owner disable failed");
}

#endif

#if defined(CONFIG_ARM) && defined(CONFIG_DYNAMIC_INTERRUPTS)

#include <zephyr/arch/cpu.h>
#if defined(CONFIG_CPU_CORTEX_M)
#include <cmsis_core.h>
#else
#include <zephyr/interrupt_util.h>
#endif

struct k_thread sup_fp_thread;
K_THREAD_STACK_DEFINE(sup_fp_thread_stack, STACKSIZE);

void arm_test_isr_handler(const void *args)
{
	ARG_UNUSED(args);

	if (k_float_disable(&sup_fp_thread) != -EINVAL) {

		TC_ERROR("k_float_disable() successful in ISR\n");

		test_ret = TC_FAIL;
	}
}

static void sup_fp_thread_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	/* Verify K_FP_REGS flag is set */
	if ((sup_fp_thread.base.user_options & K_FP_REGS) == 0) {

		TC_ERROR("sup_fp_thread FP options cleared\n");
		test_ret = TC_FAIL;
	}

	/* Determine an NVIC IRQ line that is not currently in use. */
	int i;

#if defined(CONFIG_CPU_CORTEX_M)
	for (i = CONFIG_NUM_IRQS - 1; i >= 0; i--) {
		if (NVIC_GetEnableIRQ(i) == 0) {
			/*
			 * Interrupts configured statically with IRQ_CONNECT(.)
			 * are automatically enabled. NVIC_GetEnableIRQ()
			 * returning false, here, implies that the IRQ line is
			 * not enabled, thus, currently not in use by Zephyr.
			 */
			break;
		}
	}
#elif defined(CONFIG_ARM_CUSTOM_INTERRUPT_CONTROLLER)
	for (i = CONFIG_NUM_IRQS - 1; i >= 0; i--) {
		if (z_soc_irq_is_enabled(i) == 0) {
			/*
			 * Similar to NVIC, get an IRQ line that is not enabled
			 * with the custom ARM controller
			 */
			break;
		}
	}
#else
	/*
	 * SGIs are always enabled by default, so choose the last one
	 * for testing.
	 */
	i = GIC_PPI_INT_BASE - 1;
#endif

	zassert_true(i >= 0,
		"No available IRQ line to use in the test\n");

	TC_PRINT("Available IRQ line: %u\n", i);

	arch_irq_connect_dynamic(i,
		0,
		arm_test_isr_handler,
		NULL,
		0);

#if defined(CONFIG_CPU_CORTEX_M)
	NVIC_ClearPendingIRQ(i);
	NVIC_EnableIRQ(i);
	NVIC_SetPendingIRQ(i);
#else
	arch_irq_enable(i);
	trigger_irq(i);
#endif

	/*
	 * Instruction barriers to make sure the NVIC IRQ is
	 * set to pending state before program proceeds.
	 */
	barrier_dsync_fence_full();
	barrier_isync_fence_full();

	/* Verify K_FP_REGS flag is still set */
	if ((sup_fp_thread.base.user_options & K_FP_REGS) == 0) {

		TC_ERROR("sup_fp_thread FP options cleared\n");
		test_ret = TC_FAIL;
	}
}

ZTEST(k_float_disable, test_k_float_disable_irq)
{
	test_ret = TC_PASS;

	k_thread_priority_set(k_current_get(), PRIORITY);


	/* Create an FP-capable Supervisor thread with the same cooperative
	 * priority as the current thread.
	 */
	k_thread_create(&sup_fp_thread, sup_fp_thread_stack, STACKSIZE,
		sup_fp_thread_entry, NULL, NULL, NULL,
		PRIORITY, K_FP_REGS,
		K_NO_WAIT);

	/* Yield will swap-in sup_fp_thread */
	k_yield();

	/* test_ret is volatile, static analysis says we can't use in assert */
	bool ok = test_ret == TC_PASS;

	zassert_true(ok, "");
}
#else
ZTEST(k_float_disable, test_k_float_disable_irq)
{
	TC_PRINT("This is not an ARM system with DYNAMIC_INTERRUPTS.\n");
	ztest_test_skip();
}
#endif /* CONFIG_ARM && CONFIG_DYNAMIC_INTERRUPTS */

ZTEST_SUITE(k_float_disable, NULL, NULL, NULL, NULL, NULL);
