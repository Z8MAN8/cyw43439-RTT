/*
 * Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// todo graham #ifdef for LWIP inclusion?

#include "pico/async_context.h"
#include "pico/time.h"
#include "lwip/tcpip.h"
#include "lwip/timeouts.h"

#include "rtthread.h"

#if NO_SYS
#error lwip_rtthread_async_context_bindings requires NO_SYS=0
#endif

static async_context_t * volatile lwip_context;
// lwIP tcpip_task cannot be shutdown, so we block it when we are de-initialized.
static rt_sem_t tcpip_task_blocker;

static void tcpip_init_done(void *param) {
    rt_sem_release((rt_sem_t)param);
}

bool lwip_rtthread_init(async_context_t *context) {
    RT_ASSERT(!lwip_context);
    lwip_context = context;
    static bool done_lwip_init;
    if (!done_lwip_init) {
        done_lwip_init = true;
        rt_sem_t init_sem = rt_sem_create("lwip_init_sem", 0, RT_IPC_FLAG_PRIO);
        tcpip_task_blocker = rt_sem_create("tcpip_task_blocker", 0, RT_IPC_FLAG_PRIO);
        tcpip_init(tcpip_init_done, init_sem);
        rt_sem_take(init_sem, RT_TICK_MAX / 2 - 1);
        rt_sem_delete(init_sem);
    } else {
        rt_sem_release(tcpip_task_blocker);
    }
    return true;
}

static uint32_t clear_lwip_context(__unused void *param) {
    lwip_context = NULL;
    return 0;
}

void lwip_rtthread_deinit(__unused async_context_t *context) {
    // clear the lwip context under lock as lwIP may still be running in tcpip_task
    async_context_execute_sync(context, clear_lwip_context, NULL);
}

void pico_lwip_custom_lock_tcpip_core(void) {
    while (!lwip_context) {
        rt_sem_take(tcpip_task_blocker, RT_TICK_MAX / 2 - 1);
    }
    async_context_acquire_lock_blocking(lwip_context);
}

void pico_lwip_custom_unlock_tcpip_core(void) {
    async_context_release_lock(lwip_context);
}
