/*
 * Copyright (c) 2026, John.liu <450547566@qq.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author     Notes
 * 2026-04-07     John       first version
 */

#ifndef __THREAD_TEST_H__
#define __THREAD_TEST_H__

#include <stdint.h>
#include <stddef.h>

#include "thread.h"

#ifdef __cplusplus
extern "C" {
#endif

/**********************
 *  DEFINES
 **********************/
typedef enum {
    EVENT_TEST_MSG_HELLO = 0x01,
} EVENT_TEST_MSG_ID_E;

/**********************
 *      TYPEDEFS
 **********************/
typedef struct {
    thread_msg_head_t head;
    uint8_t           msg_buf[];
} thread_test_msg_t;

/**********************
 *  FUNCTIONS
 **********************/
int32_t thread_test_evt_msg_send(uint32_t msg_id, uint8_t *pdata, uint32_t length);
int32_t thread_test_evt_send(uint32_t msg_id);
int32_t thread_test_evt_send_delayed(uint32_t msg_id, uint32_t delay_ms);
int32_t thread_test_evt_cancel_delayed(uint32_t msg_id);

#ifdef __cplusplus
}
#endif
#endif /* __THREAD_TEST_H__ */
