/*
 * Copyright (c) 2026, John.liu <450547566@qq.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author     Notes
 * 2026-04-07     John       first version
 */

/**
 * RT-Thread port of the system-ready notification.
 *
 * A single global rt_event object is used.  The bit THREAD_SYSREADY_BIT is set
 * once by the manager (thread_sysready_publish) and never cleared, so any
 * task calling thread_sysready_wait — even after publication — returns
 * immediately.
 */

#include <rtthread.h>

#define DBG_TAG  "thread_sysready"
#define DBG_LVL  DBG_LOG
#include <rtdbg.h>

#include "thread_sysready.h"

/**********************
 *  STATIC DEFINES
 **********************/
#define THREAD_SYSREADY_BIT  ((rt_uint32_t)(1U << 0))

/**********************
 *  STATIC VARIABLES
 **********************/
static struct rt_event s_sysready_event;

static int _thread_sysready_init(void)
{
    rt_event_init(&s_sysready_event, "sysrdy", RT_IPC_FLAG_FIFO);
    return 0;
}
INIT_COMPONENT_EXPORT(_thread_sysready_init);

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
void thread_sysready_publish(void)
{
    LOG_I("system ready");
    rt_event_send(&s_sysready_event, THREAD_SYSREADY_BIT);
}

rt_err_t thread_sysready_wait(rt_int32_t timeout)
{
    rt_uint32_t recved = 0U;
    /*
     * RT_EVENT_FLAG_OR: wake when the bit is set.
     * Do NOT use RT_EVENT_FLAG_CLEAR so the bit stays set permanently —
     * all subsequent callers will also return immediately.
     */
    rt_err_t err = rt_event_recv(&s_sysready_event, THREAD_SYSREADY_BIT,
                                 RT_EVENT_FLAG_OR, timeout, &recved);

    if (err != RT_EOK) {
        LOG_E("sysready wait failed: %d", err);
    }
    return err;
}
