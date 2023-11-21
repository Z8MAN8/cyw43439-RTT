/*
 * Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _PICO_LWIP_RTTHREAD_H
#define _PICO_LWIP_RTTHREAD_H

#include "pico.h"
#include "pico/async_context.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \file pico/lwip_rtthread.h
* \defgroup pico_lwip_rtthread pico_lwip_rtthread
* \ingroup pico_lwip
* \brief Glue library for integration lwIP in \c NO_SYS=0 mode with the SDK. Simple \c init and \c deinit
* are all that is required to hook up lwIP (with full blocking API support) via an \ref async_context instance.
*/

/*! \brief Initializes lwIP (NO_SYS=0 mode) support support for rtthread using the provided async_context
 *  \ingroup pico_lwip_rtthread
 *
 * If the initialization succeeds, \ref lwip_rtthread_deinit() can be called to shutdown lwIP support
 *
 * \param context the async_context instance that provides the abstraction for handling asynchronous work. Note in general
 * this would be an \ref async_context_rtthread instance, though it doesn't have to be.
 *
 * \return true if the initialization succeeded
*/
bool lwip_rtthread_init(async_context_t *context);

/*! \brief De-initialize lwIP (NO_SYS=0 mode) support for rtthread
 *  \ingroup pico_lwip_rtthread
 *
 * Note that since lwIP may only be initialized once, and doesn't itself provide a shutdown mechanism, lwIP
 * itself may still consume resources.
 *
 * It is however safe to call \ref lwip_rtthread_init again later.
 *
 * \param context the async_context the lwip_rtthread support was added to via \ref lwip_rtthread_init
*/
void lwip_rtthread_deinit(async_context_t *context);

#ifdef __cplusplus
}
#endif
#endif
