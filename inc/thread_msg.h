/*
 * Copyright (c) 2026, John.liu <450547566@qq.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author     Notes
 * 2026-04-07     John       first version
 */

#ifndef THREAD_MSG_H__
#define THREAD_MSG_H__

#include <stdbool.h>
#include <stdint.h>
#include <rtatomic.h>
#include <rtcompiler.h>

#ifdef __cplusplus
extern "C" {
#endif

/**********************
 *      DEFINES
 **********************/
#ifndef THREAD_MGR_PP_CAT
#define THREAD_MGR_PP_CAT_I(a, b) a##b
#define THREAD_MGR_PP_CAT(a, b)   THREAD_MGR_PP_CAT_I(a, b)
#endif

/**********************
 *      TYPEDEFS
 **********************/
typedef uint32_t THREAD_ID_E;

typedef void (*thread_msg_callback_t)(const void *p_msg);

typedef struct {
    uint32_t msg_id;
    uint32_t msg_len;
    /** Publisher task ID */
    uint32_t publisher_task;
} thread_msg_head_t;

typedef struct {
    thread_msg_head_t head;
    uint8_t           msg_buf[];
} thread_msg_t;

_Static_assert(sizeof(thread_msg_t) == 12U, "thread_msg_t size mismatch");

typedef struct thread_msg_registry_entry {
    THREAD_ID_E              subscriber_task_id;
    THREAD_ID_E              publisher_task_id;
    uint32_t                 msg_id;
    thread_msg_callback_t    callback;
    /**
     * ROM entries are placed in the thread_msg_reg section by THREAD_MSG_REGISTRY_ENTRY.
     * At boot the framework copies the ROM table to a RAM copy and only mutates the RAM copy.
     * At runtime use thread_msg_subscribe_set_active to change subscription state.
     */
    rt_atomic_t              subscribe_active;
} thread_msg_registry_entry_t;

/*
 * Linker section "thread_msg_reg".
 * GCC ld provides __start_thread_msg_reg / __stop_thread_msg_reg automatically.
 */
#define _THREAD_MSG_REGISTRY_ENTRY_IMPL(_subscriber, _publisher, _msg_id, _cb, active, _uniq)  \
    static rt_used const thread_msg_registry_entry_t                                            \
        rt_section("thread_msg_reg")                                                       \
        THREAD_MGR_PP_CAT(__thread_msg_reg_, _uniq) = {                                                 \
            .subscriber_task_id = (_subscriber),                                                \
            .publisher_task_id  = (_publisher),                                                 \
            .msg_id             = (_msg_id),                                                    \
            .callback           = (_cb),                                                        \
            .subscribe_active   = (rt_atomic_t)((active) ? 1 : 0),                             \
    }

#define THREAD_MSG_REGISTRY_ENTRY(_subscriber, _publisher, _msg_id, _cb, active)   \
    _THREAD_MSG_REGISTRY_ENTRY_IMPL(_subscriber, _publisher, _msg_id, _cb, active, __COUNTER__)

/** Section iteration helpers */
extern const thread_msg_registry_entry_t __start_thread_msg_reg[];
extern const thread_msg_registry_entry_t __stop_thread_msg_reg[];

#define THREAD_MSG_REG_FOREACH(it)                                           \
    for (const thread_msg_registry_entry_t *(it) = __start_thread_msg_reg; \
         (it) < __stop_thread_msg_reg; (it)++)

/**********************
 *   SUBSCRIBE API
 **********************/
void thread_msg_subscribe_set_active(THREAD_ID_E subscriber, THREAD_ID_E publisher,
                                     uint32_t msg_id, thread_msg_callback_t cb, bool active);
bool thread_msg_subscribe_is_active(THREAD_ID_E subscriber, THREAD_ID_E publisher,
                                    uint32_t msg_id, thread_msg_callback_t cb);

#define THREAD_MSG_SUBSCRIBE_DISABLE(sub, pub, mid, cb) \
    thread_msg_subscribe_set_active((sub), (pub), (mid), (cb), false)
#define THREAD_MSG_SUBSCRIBE_ENABLE(sub, pub, mid, cb) \
    thread_msg_subscribe_set_active((sub), (pub), (mid), (cb), true)
#define THREAD_MSG_SUBSCRIBE_IS_DISABLED(sub, pub, mid, cb) \
    (!thread_msg_subscribe_is_active((sub), (pub), (mid), (cb)))

#define THREAD_MSG_SUBSCRIBE_DISABLE_ALL(sub, pub, mid) \
    thread_msg_subscribe_set_active((sub), (pub), (mid), (thread_msg_callback_t)0, false)
#define THREAD_MSG_SUBSCRIBE_ENABLE_ALL(sub, pub, mid) \
    thread_msg_subscribe_set_active((sub), (pub), (mid), (thread_msg_callback_t)0, true)

/**********************
 *   MESSAGE API
 **********************/
void    thread_msg_evt_free(void *p_ctrl, void *p_msg);
bool    thread_msg_registry_dispatch_message(THREAD_ID_E subscriber_task, void *p_msg);
bool    thread_msg_registry_dispatch_event(THREAD_ID_E subscriber_task, uint32_t event_id, void *opaque);

int32_t thread_evt_msg_send_by_task_id(THREAD_ID_E task_id, uint32_t msg_id,
                                       uint8_t *pdata, uint32_t length);
int32_t thread_evt_send_by_task_id(THREAD_ID_E task_id, uint32_t msg_id);
int32_t thread_evt_send_delayed_by_task_id(THREAD_ID_E task_id, uint32_t msg_id,
                                           uint32_t delay_ms);
int32_t thread_evt_cancel_delayed_by_task_id(THREAD_ID_E task_id, uint32_t msg_id);

#ifdef __cplusplus
}
#endif
#endif /* THREAD_MSG_H__ */
