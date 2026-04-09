/*
 * Copyright (c) 2026, John.liu <450547566@qq.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author     Notes
 * 2026-04-07     John       first version
 */

#ifndef THREAD_MANAGER_H__
#define THREAD_MANAGER_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**********************
 *      DEFINES
 **********************/
#ifndef BIT
#define BIT(n) ((uint32_t)(1U << (n)))
#endif

#define EVENT_SYSTEM_MSG_HELLO      BIT(0)
#define EVENT_SYSTEM_POWEROFF       BIT(1)
#define EVENT_SYSTEM_REBOOT         BIT(2)
#define EVENT_SYSTEM_SHIPMODE       BIT(3)
#define EVENT_SYSTEM_LOWPOWER       BIT(4)

/**********************
 *      TYPEDEFS
 **********************/
typedef uint32_t THREAD_ID_E;

/**********************
 *  FUNCTIONS
 **********************/
void thread_mgr_sys_start_sync_wait(THREAD_ID_E task);
void thread_mgr_sys_start_sync_end(THREAD_ID_E task);
void thread_mgr_sys_exit_sync(THREAD_ID_E task);
void thread_mgr_event_send(uint32_t event);

#ifdef __cplusplus
}
#endif
#endif /* THREAD_MANAGER_H__ */
