/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

static K_THREAD_STACK_DEFINE(irq_start_stack, 1024 + CONFIG_TEST_EXTRA_STACK_SIZE);
static struct k_thread irq_start_thread;
static K_SEM_DEFINE(irq_start_done, 0, 1);
static atomic_t timer_interrupts;

static void irq_start_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	/* Keep this first thread activation running across further timer IRQs.
	 * A busy wait also terminates when interrupt delivery is broken.
	 */
	k_busy_wait(k_ticks_to_us_ceil32(3));
	zassert_true(atomic_get(&timer_interrupts) > 1,
		     "Timer interrupts stopped after entering a new thread from an ISR");
	k_sem_give(&irq_start_done);
}

static void irq_start_expiry(struct k_timer *timer)
{
	ARG_UNUSED(timer);

	if (atomic_inc(&timer_interrupts) == 0) {
		k_thread_start(&irq_start_thread);
	}
}

static K_TIMER_DEFINE(irq_start_timer, irq_start_expiry, NULL);

ZTEST(thread_init, test_first_entry_from_timer_interrupt)
{
	atomic_clear(&timer_interrupts);
	k_thread_create(&irq_start_thread, irq_start_stack,
			K_THREAD_STACK_SIZEOF(irq_start_stack), irq_start_entry,
			NULL, NULL, NULL, K_PRIO_COOP(0), 0, K_FOREVER);
	k_timer_start(&irq_start_timer, K_TICKS(1), K_TICKS(1));
	zassert_ok(k_sem_take(&irq_start_done, K_TICKS(20)));
	k_timer_stop(&irq_start_timer);
	zassert_ok(k_thread_join(&irq_start_thread, K_TICKS(20)));
}
