/*
 * Copyright (c) 2026, John.liu <450547566@qq.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author     Notes
 * 2026-04-07     John       first version
 */

#ifndef THREAD_H__
#define THREAD_H__

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <rtthread.h>
#include <rtatomic.h>
#include <rtcompiler.h>

#include "thread_manager.h"
#include "thread_msg.h"
#include "thread_sysready.h"

/*
 * CONFIG_THREAD_STACK_* / CONFIG_THREAD_PRIO_* (and related) macros.
 */
#include "thread_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**********************
 *      COMPAT HELPERS
 **********************/
#ifndef BIT
#define BIT(n)  ((uint32_t)(1U << (n)))
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

#ifndef ARG_UNUSED
#define ARG_UNUSED(x) ((void)(x))
#endif

/**********************
 *      EVENT DEFINES
 **********************/
/** Notify event bits stored in thread_control_t.notify */
#define THREAD_NOTIFY_MSG_BIT    ((rt_uint32_t)(1U << 30))
#define THREAD_NOTIFY_EXIT_BIT   ((rt_uint32_t)(1U << 31))

/** Legacy aliases used by dispatch logic */
#define SYSTEM_THREAD_NOTIRY_SYNC_EXIT  THREAD_NOTIFY_EXIT_BIT
#define SYSTEM_THREAD_NOTIRY_MSG_READY  THREAD_NOTIFY_MSG_BIT

/**********************
 *   MEMPOOL SIZE HELPER
 *   RT-Thread rt_mp_init: each block occupies (aligned_block_size + sizeof(rt_uint8_t*)) bytes.
 **********************/
#define THREAD_MP_BLOCK_ALIGNED(bs)    RT_ALIGN((rt_size_t)(bs), RT_ALIGN_SIZE)
#define THREAD_MP_BLOCK_TOTAL(bs)      (THREAD_MP_BLOCK_ALIGNED(bs) + sizeof(rt_uint8_t *))
#define THREAD_MP_POOL_SIZE(bs, num)   ((rt_size_t)(num) * THREAD_MP_BLOCK_TOTAL(bs))

/**********************
 *      TYPEDEFS
 **********************/
typedef int (*func_thread_init_t)(void);
typedef void (*func_thread_deinit_t)(void);
typedef void (*thread_evt_delayed_dispatch_t)(void *pargs);

typedef struct _thread_control_t {
    func_thread_init_t  f_thread_init;
    func_thread_deinit_t f_thread_deinit;
    /** RT-Thread thread handle (set inside the thread's own entry function) */
    rt_thread_t         tid;
    /** Mailbox object pointer (points to static storage in user thread file) */
    struct rt_mailbox  *msgq;
    /** Mailbox backing buffer declared in user thread file */
    rt_ubase_t         *msgq_buf;
    /** Mailbox capacity (number of message pointers) */
    rt_size_t           msgq_depth;
    /** Memory pool object pointer (points to static storage in user thread file) */
    struct rt_mempool  *msg_slab;
    /** Memory pool backing buffer declared in user thread file */
    void               *msg_slab_buf;
    /** Size of a single slab block (payload + header) */
    uint32_t            msg_slab_block_size;
    /** Total number of slab blocks */
    uint32_t            msg_slab_num;
    /** Semaphore: manager gives this to unblock task startup (initialised to 0) */
    struct rt_semaphore start_sem;
    /** Event: MSG_BIT set on new message; EXIT_BIT set on shutdown request */
    struct rt_event     notify;
    /** Application private data (optional) */
    void               *p_private_data;
    /** Callback for delayed-event dispatch (used by soft-timer trampoline) */
    thread_evt_delayed_dispatch_t f_evt_delayed_dispatch;
} thread_control_t;

/**********************
 *   APPLICATION SLOT (linker section)
 **********************/
/** Registered by each application task via THREAD_CTRL_SLOT_ENTRY. */
typedef struct thread_app_slot {
    THREAD_ID_E      id;
    thread_control_t *ctrl;
} thread_app_slot_t;

/*
 * GCC linker section "thread_ctrl_slot".
 * ld automatically provides __start_thread_ctrl_slot / __stop_thread_ctrl_slot
 * for sections whose name is a valid C identifier.
 * The rt_used attribute (which includes "retain" on GCC>=11) prevents --gc-sections removal.
 * For older toolchains add the KEEP directive from thread.ld into the BSP linker script.
 */
#define _THREAD_CTRL_SLOT_ENTRY_IMPL(_task_id, _ctrl_ptr, _uniq)              \
    static rt_used const thread_app_slot_t                                     \
        rt_section("thread_ctrl_slot")                                    \
        THREAD_MGR_PP_CAT(__thread_app_slot_, _uniq) = {                               \
            .id   = (_task_id),                                                \
            .ctrl = (_ctrl_ptr),                                               \
    }

#define THREAD_CTRL_SLOT_ENTRY(_task_id, _ctrl_ptr)   \
    _THREAD_CTRL_SLOT_ENTRY_IMPL(_task_id, _ctrl_ptr, __COUNTER__)

/** Section iteration — use in framework internals only */
extern const thread_app_slot_t __start_thread_ctrl_slot[];
extern const thread_app_slot_t __stop_thread_ctrl_slot[];

#define THREAD_CTRL_SLOT_FOREACH(it)                              \
    for (const thread_app_slot_t *(it) = __start_thread_ctrl_slot; \
         (it) < __stop_thread_ctrl_slot; (it)++)

/**********************
 *   GLOBAL API
 **********************/
void thread_task_unregister(THREAD_ID_E id);
thread_control_t *thread_task_lookup(THREAD_ID_E id);
void thread_spawn_all_registered(void);
const char *thread_task_get_name(THREAD_ID_E tid);

#ifdef __cplusplus
}
#endif
#endif /* THREAD_H__ */
