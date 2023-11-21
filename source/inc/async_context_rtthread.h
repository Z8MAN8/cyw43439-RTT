/*
 * Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _PICO_ASYNC_CONTEXT_RTTHREAD_H
#define _PICO_ASYNC_CONTEXT_RTTHREAD_H

/** \file pico/async_context.h
 *  \defgroup async_context_rtthread async_context_rtthread
 *  \ingroup pico_async_context
 *
 * async_context_rtthread provides an implementation of \ref async_context that handles asynchronous
 * work in a separate RTThread task.
 */
#include "pico/async_context.h"

// RTThread includes
#include "rtthread.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS( xTimeInMs )    ( ( rt_uint32_t ) rt_tick_from_millisecond( (rt_int32_t) xTimeInMs ) )
#endif

#ifndef ASYNC_CONTEXT_DEFAULT_RTTHREAD_TASK_PRIORITY
#define ASYNC_CONTEXT_DEFAULT_RTTHREAD_TASK_PRIORITY 8
#endif

#ifndef ASYNC_CONTEXT_DEFAULT_RTTHREAD_TASK_STACK_SIZE
#define ASYNC_CONTEXT_DEFAULT_RTTHREAD_TASK_STACK_SIZE 2048
#endif

typedef struct async_context_rtthread async_context_rtthread_t;

/**
 * \brief Configuration object for async_context_rtthread instances.
 */
typedef struct async_context_rtthread_config {
    /**
     * Task priority for the async_context task
     */
    rt_uint8_t task_priority;
    /**
     * Stack size for the async_context task
     */
    rt_uint32_t task_stack_size;
    /**
     * the core ID (see \ref portGET_CORE_ID()) to pin the task to.
     * This is only relevant in SMP mode.
     */
#if configUSE_CORE_AFFINITY && configNUM_CORES > 1
    rt_uint8_t task_core_id;
#endif
} async_context_rtthread_config_t;

struct async_context_rtthread {
    async_context_t core;
    rt_mutex_t lock_mutex;
    rt_sem_t work_needed_sem;
    rt_event_t notify_event;
    rt_timer_t timer_handle;
    rt_thread_t task_handle;
    uint8_t nesting;
    volatile bool task_should_exit;
};

/*!
 * \brief Initialize an async_context_rtthread instance using the specified configuration
 * \ingroup async_context_rtthread
 *
 * If this method succeeds (returns true), then the async_context is available for use
 * and can be de-initialized by calling async_context_deinit().
 *
 * \param self a pointer to async_context_rtthread structure to initialize
 * \param config the configuration object specifying characteristics for the async_context
 * \return true if initialization is successful, false otherwise
 */
bool async_context_rtthread_init(async_context_rtthread_t *self, async_context_rtthread_config_t *config);

/*!
 * \brief Return a copy of the default configuration object used by \ref async_context_rtthread_init_with_defaults()
 * \ingroup async_context_rtthread
 *
 * The caller can then modify just the settings it cares about, and call \ref async_context_rtthread_init()
 * \return the default configuration object
 */
 static inline async_context_rtthread_config_t async_context_rtthread_default_config(void) {
    async_context_rtthread_config_t config = {
            .task_priority = ASYNC_CONTEXT_DEFAULT_RTTHREAD_TASK_PRIORITY,
            .task_stack_size = ASYNC_CONTEXT_DEFAULT_RTTHREAD_TASK_STACK_SIZE,
#if configUSE_CORE_AFFINITY && configNUM_CORES > 1
            .task_core_id = (rt_uint8_t)-1, // none
#endif
    };
    return config;

}

/*!
 * \brief Initialize an async_context_rtthread instance with default values
 * \ingroup async_context_rtthread
 *
 * If this method succeeds (returns true), then the async_context is available for use
 * and can be de-initialized by calling async_context_deinit().
 *
 * \param self a pointer to async_context_rtthread structure to initialize
 * \return true if initialization is successful, false otherwise
 */
 static inline bool async_context_rtthread_init_with_defaults(async_context_rtthread_t *self) {
    async_context_rtthread_config_t config = async_context_rtthread_default_config();
    return async_context_rtthread_init(self, &config);
}

#ifdef __cplusplus
}
#endif

#endif
