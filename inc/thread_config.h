/*
 * Copyright (c) 2026, John.liu <450547566@qq.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author     Notes
 * 2026-04-07     John       first version
 */

/* Priorities must satisfy: 0 <= p < RT_THREAD_PRIORITY_MAX (often 32). Smaller = higher priority. */

#ifndef __THREAD_CONFIG_H__
#define __THREAD_CONFIG_H__

/* ---- Thread IDs ---- */
enum {
    CONFIG_THREAD_ID_TEST       = 0,
    CONFIG_THREAD_ID_TEST1      = 1,
    CONFIG_THREAD_ID_TEST2      = 2,
    CONFIG_THREAD_ID_MANAGER    = 3,
    CONFIG_THREAD_ID_MUX_NUMBER = 4,
};

/* ---- Stack sizes (bytes)  ---- */
#define CONFIG_THREAD_STACK_TEST    512
#define CONFIG_THREAD_STACK_TEST1   512
#define CONFIG_THREAD_STACK_TEST2   512
#define CONFIG_THREAD_STACK_MANAGER 512


/* ---- preempt priorities ---- */
#define CONFIG_THREAD_PRIO_TEST    28
#define CONFIG_THREAD_PRIO_TEST1   27
#define CONFIG_THREAD_PRIO_TEST2   26
#define CONFIG_THREAD_PRIO_MANAGER 4

/* Manager temporarily raises its numeric priority (lower CPU precedence) during spawn; must be < RT_THREAD_PRIORITY_MAX and > CONFIG_THREAD_PRIO_MANAGER */
#define CONFIG_THREAD_MANAGER_PRIO_SYNC  28

#endif /* __THREAD_CONFIG_H__ */
