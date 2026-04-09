/*
 * Copyright (c) 2026, John.liu <450547566@qq.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author     Notes
 * 2026-04-07     John       first version
 */

#ifndef THREAD_SYSREADY_H__
#define THREAD_SYSREADY_H__

#include <stdint.h>
#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Called once by the manager thread after all tasks complete their startup sync.
 */
void thread_sysready_publish(void);

/**
 * Block until the system-ready event has been published.
 * @param timeout  RT-Thread tick timeout (use RT_WAITING_FOREVER for indefinite wait).
 * @return RT_EOK on success, -RT_ETIMEOUT on timeout.
 */
rt_err_t thread_sysready_wait(rt_int32_t timeout);

#ifdef __cplusplus
}
#endif
#endif /* THREAD_SYSREADY_H__ */
