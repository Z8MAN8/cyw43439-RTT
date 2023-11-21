/*
 * Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <string.h>
#include "async_context_rtthread.h"
#include "pico/async_context_base.h"
#include "pico/sync.h"
#include "hardware/irq.h"

#if configNUM_CORES > 1 && !defined(configUSE_CORE_AFFINITY)
#error async_context_rtthread requires configUSE_CORE_AFFINITY under SMP
#endif

static const async_context_type_t template;

static void async_context_rtthread_acquire_lock_blocking(async_context_t *self_base);
static void async_context_rtthread_release_lock(async_context_t *self_base);
static void async_context_rtthread_lock_check(async_context_t *self_base);

static rt_uint32_t sensible_ticks_until(absolute_time_t until) {
    rt_uint32_t ticks;
    int64_t delay_us = absolute_time_diff_us(get_absolute_time(), until);
    if (delay_us <= 0) {
        ticks = 0;
    } else {
        static const uint32_t max_delay = 60000000;
        uint32_t delay_us_32 = delay_us > max_delay ? max_delay : (uint32_t) delay_us;
        ticks = pdMS_TO_TICKS((delay_us_32+999)/1000);
        // we want to round up, as both rounding down to zero is wrong (may produce no delays
        // where delays are needed), but also we don't want to wake up, and then realize there
        // is no work to do yet!
        ticks++;
    }
    return ticks;
}

static void process_under_lock(async_context_rtthread_t *self) {
#ifndef NDEBUG
    async_context_rtthread_lock_check(&self->core);
#endif
    bool repeat;
    do {
        repeat = false;
        absolute_time_t next_time = async_context_base_execute_once(&self->core);
        rt_uint32_t ticks;
        if (is_at_the_end_of_time(next_time)) {
            ticks = RT_TICK_MAX / 2 - 1;
        } else {
            ticks = sensible_ticks_until(next_time);
        }
        if (ticks) {
            // last parameter (timeout) is also 'ticks', since there is no point waiting to change the period
            // for longer than the period itself!
            repeat = RT_EOK != rt_timer_control(self->timer_handle, RT_TIMER_CTRL_SET_TIME, &ticks);
        } else {
            repeat = true;
        }
    } while (repeat);
}

static void async_context_task(void *param) {
    async_context_rtthread_t *self = (async_context_rtthread_t *)param;
    rt_uint32_t e;
    do {
        rt_event_recv(self->notify_event, 1, RT_EVENT_FLAG_CLEAR | RT_EVENT_FLAG_AND, RT_TICK_MAX / 2 - 1, &e);
        if (self->task_should_exit) break;
        async_context_rtthread_acquire_lock_blocking(&self->core);
        process_under_lock(self);
        async_context_rtthread_release_lock(&self->core);
        __sev(); // it is possible regular code is waiting on a WFE on the other core
    } while (!self->task_should_exit);
    rt_thread_delete(rt_thread_self());
}

static void async_context_rtthread_wake_up(async_context_t *self_base) {
    async_context_rtthread_t *self = (async_context_rtthread_t *)self_base;
    if (self->task_handle) {
        rt_bool_t in_isr = rt_interrupt_get_nest() > 0;
        if (in_isr) {
            rt_sem_release(self->work_needed_sem);
            rt_event_send(self->notify_event, 1);
        } else {
            // We don't want to wake ourselves up (we will only ever be called
            // from the async_context_task if we own the lock, in which case processing
            // will already happen when the lock is finally unlocked.
            if (rt_thread_self() != self->task_handle) {
                rt_sem_release(self->work_needed_sem);
                rt_event_send(self->notify_event, 1);
            } else {
    #ifndef NDEBUG
                async_context_rtthread_lock_check(self_base);
    #endif
            }
        }
    }
}

static void timer_handler(void *parameter)
{
    async_context_rtthread_t *self = (async_context_rtthread_t *)parameter;
    async_context_rtthread_wake_up(&self->core);
}

bool async_context_rtthread_init(async_context_rtthread_t *self, async_context_rtthread_config_t *config) {
    memset(self, 0, sizeof(*self));
    self->core.type = &template;
    self->core.flags = ASYNC_CONTEXT_FLAG_CALLBACK_FROM_NON_IRQ;
    self->core.core_num = get_core_num();
    self->lock_mutex = rt_mutex_create("async_lock", RT_IPC_FLAG_PRIO);
    self->work_needed_sem = rt_sem_create("async_sem", 0, RT_IPC_FLAG_PRIO);
    self->notify_event = rt_event_create("notify_event", RT_IPC_FLAG_PRIO);
    self->task_handle = rt_thread_create("async_context_task", async_context_task, self, config->task_stack_size, config->task_priority, 20);
    self->timer_handle = rt_timer_create("async_context_timer", timer_handler, self, RT_TICK_MAX / 2 - 1, RT_TIMER_FLAG_PERIODIC);
    rt_timer_start(self->timer_handle);
    rt_thread_startup(self->task_handle);

    if (!self->lock_mutex ||
        !self->work_needed_sem ||
        !self->notify_event ||
        !self->timer_handle ||
        !self->task_handle
        ) {
        async_context_deinit(&self->core);
        return false;
    }
#if configNUM_CORES > 1
    rt_uint8_t core_id = config->task_core_id;
    if (core_id == (rt_uint8_t)-1) {
        core_id = portGET_CORE_ID();
    }
    // we must run on a single core
    // vTaskCoreAffinitySet(self->task_handle, 1u << core_id);
#endif
    return true;
}

static uint32_t end_task_func(void *param) {
    async_context_rtthread_t *self = (async_context_rtthread_t *)param;
    // we will immediately exit
    self->task_should_exit = true;
    return 0;
}

void async_context_rtthread_deinit(async_context_t *self_base) {
    async_context_rtthread_t *self = (async_context_rtthread_t *)self_base;
    if (self->task_handle) {
        async_context_execute_sync(self_base, end_task_func, self_base);
    }
    if (self->timer_handle) {
        rt_timer_stop(self->timer_handle);
        rt_timer_delete(self->timer_handle);
    }
    if (self->lock_mutex) {
        rt_mutex_delete(self->lock_mutex);
    }
    if (self->work_needed_sem) {
        rt_sem_delete(self->work_needed_sem);
    }
    if (self->notify_event) {
        rt_event_delete(self->notify_event);
    }
    memset(self, 0, sizeof(*self));
}

void async_context_rtthread_acquire_lock_blocking(async_context_t *self_base) {
    async_context_rtthread_t *self = (async_context_rtthread_t *)self_base;
    // Lock the other core and stop low_prio_irq running
    RT_ASSERT(!rt_interrupt_get_nest());
    rt_mutex_take(self->lock_mutex, RT_TICK_MAX / 2 - 1);
    self->nesting++;
}

void async_context_rtthread_lock_check(__unused async_context_t *self_base) {
#ifndef NDEBUG
    async_context_rtthread_t *self = (async_context_rtthread_t *)self_base;
    // Lock the other core and stop low_prio_irq running
    RT_ASSERT(self->lock_mutex->owner == rt_thread_self());
#endif
}

typedef struct sync_func_call{
    async_when_pending_worker_t worker;
    rt_sem_t sem;
    uint32_t (*func)(void *param);
    void *param;
    uint32_t rc;
} sync_func_call_t;

static void handle_sync_func_call(async_context_t *context, async_when_pending_worker_t *worker) {
    sync_func_call_t *call = (sync_func_call_t *)worker;
    call->rc = call->func(call->param);
    rt_sem_release(call->sem);
    async_context_remove_when_pending_worker(context, worker);
}

uint32_t async_context_rtthread_execute_sync(async_context_t *self_base, uint32_t (*func)(void *param), void *param) {
    async_context_rtthread_t *self = (async_context_rtthread_t*)self_base;
    // Use RT-Thread's assertion mechanism
    RT_ASSERT(self->lock_mutex->owner != rt_thread_self());

    sync_func_call_t call;
    call.worker.do_work = handle_sync_func_call;
    call.func = func;
    call.param = param;
    call.sem = rt_sem_create("sync_sem", 0, RT_IPC_FLAG_PRIO);
    async_context_add_when_pending_worker(self_base, &call.worker);
    async_context_set_work_pending(self_base, &call.worker);
    rt_sem_take(call.sem, RT_TICK_MAX / 2 - 1);
    rt_sem_delete(call.sem);
    return call.rc;
}

void async_context_rtthread_release_lock(async_context_t *self_base) {
    async_context_rtthread_t *self = (async_context_rtthread_t *)self_base;
    bool do_wakeup = false;

    if (self->nesting == 1) {
        // note that we always do a processing on outermost lock exit, to facilitate cases
        // like lwIP where we have no notification when lwIP timers are added.
        //
        // this operation must be done from the right task
        if (self->task_handle != rt_thread_self()) {
            // note we defer the wakeup until after we release the lock, otherwise it can be wasteful
            // (waking up the task, but then having it block immediately on us)
            do_wakeup = true;
        } else {
            process_under_lock(self);
        }
    }

    --self->nesting;
    rt_mutex_release(self->lock_mutex);

    if (do_wakeup) {
        async_context_rtthread_wake_up(self_base);
    }
}

static bool async_context_rtthread_add_at_time_worker(async_context_t *self_base, async_at_time_worker_t *worker) {
    async_context_rtthread_acquire_lock_blocking(self_base);
    bool rc = async_context_base_add_at_time_worker(self_base, worker);
    async_context_rtthread_release_lock(self_base);
    return rc;
}

static bool async_context_rtthread_remove_at_time_worker(async_context_t *self_base, async_at_time_worker_t *worker) {
    async_context_rtthread_acquire_lock_blocking(self_base);
    bool rc = async_context_base_remove_at_time_worker(self_base, worker);
    async_context_rtthread_release_lock(self_base);
    return rc;
}

static bool async_context_rtthread_add_when_pending_worker(async_context_t *self_base, async_when_pending_worker_t *worker) {
    async_context_rtthread_acquire_lock_blocking(self_base);
    bool rc = async_context_base_add_when_pending_worker(self_base, worker);
    async_context_rtthread_release_lock(self_base);
    return rc;
}

static bool async_context_rtthread_remove_when_pending_worker(async_context_t *self_base, async_when_pending_worker_t *worker) {
    async_context_rtthread_acquire_lock_blocking(self_base);
    bool rc = async_context_base_remove_when_pending_worker(self_base, worker);
    async_context_rtthread_release_lock(self_base);
    return rc;
}

static void async_context_rtthread_set_work_pending(async_context_t *self_base, async_when_pending_worker_t *worker) {
    worker->work_pending = true;
    async_context_rtthread_wake_up(self_base);
}

static void async_context_rtthread_wait_until(async_context_t *self_base, absolute_time_t until) {
    RT_ASSERT(!rt_interrupt_get_nest());

    rt_int32_t ticks = sensible_ticks_until(until);
    rt_thread_delay(ticks);
    // rt_thread_delay(ticks * RT_TICK_PER_SECOND / configTICK_RATE_HZ);
}

static void async_context_rtthread_wait_for_work_until(async_context_t *self_base, absolute_time_t until) {
    async_context_rtthread_t *self = (async_context_rtthread_t *)self_base;
    // Use RT-Thread's RT_ASSERT
    RT_ASSERT(!rt_interrupt_get_nest());

    while (!time_reached(until)) {
        rt_int32_t ticks = sensible_ticks_until(until);
        if (!ticks || rt_sem_take(self->work_needed_sem, ticks)) return;
    }
}

static const async_context_type_t template = {
        .type = ASYNC_CONTEXT_FREERTOS,
        .acquire_lock_blocking = async_context_rtthread_acquire_lock_blocking,
        .release_lock = async_context_rtthread_release_lock,
        .lock_check = async_context_rtthread_lock_check,
        .execute_sync = async_context_rtthread_execute_sync,
        .add_at_time_worker = async_context_rtthread_add_at_time_worker,
        .remove_at_time_worker = async_context_rtthread_remove_at_time_worker,
        .add_when_pending_worker = async_context_rtthread_add_when_pending_worker,
        .remove_when_pending_worker = async_context_rtthread_remove_when_pending_worker,
        .set_work_pending = async_context_rtthread_set_work_pending,
        .poll = 0,
        .wait_until = async_context_rtthread_wait_until,
        .wait_for_work_until = async_context_rtthread_wait_for_work_until,
        .deinit = async_context_rtthread_deinit,
};
