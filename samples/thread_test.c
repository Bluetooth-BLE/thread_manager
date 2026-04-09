/*
 * Copyright (c) 2026, John.liu <450547566@qq.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author     Notes
 * 2026-04-07     John       first version
 */

#include <string.h>
#include <rtthread.h>

#define DBG_TAG  "ttest"
#define DBG_LVL  DBG_LOG
#include <rtdbg.h>

#include "thread_test.h"
#include "thread_test1.h"
#include "thread_test2.h"

/**********************
 *  STATIC DEFINES
 **********************/
#define THREAD_ID_SELF                      ((THREAD_ID_E)CONFIG_THREAD_ID_TEST)
#define THREAD_NAME                         "ttest"

/** Max payload per mempool block (bytes) */
#define THREAD_TEST_MSG_MAX_PAYLOAD       64U
#define THREAD_TEST_MSG_SLAB_BLOCK_SIZE   (sizeof(thread_msg_t) + THREAD_TEST_MSG_MAX_PAYLOAD)
#define THREAD_TEST_MSGQ_DEPTH            16U
#define THREAD_TEST_MSG_SLAB_NUM          (THREAD_TEST_MSGQ_DEPTH + 2U)
/** Max payload on rt_malloc path; caps abnormal lengths from exhausting the heap */
#define THREAD_TEST_MSG_MALLOC_MAX_PAYLOAD (256U)

/**********************
 *  STATIC PROTOTYPES
 **********************/
static int _thread_init(void);
static void _thread_deinit(void);
static inline void _thread_hardware_init(void);
static inline void _thread_resource_init(void);
static inline void _thread_service_init(void);
static inline void _thread_hardware_deinit(void);
static inline void _thread_resource_deinit(void);
static inline void _thread_service_deinit(void);
static inline void _thread_wait_system_ready(void);
static inline void _thread_system_ready_init(void);
static void _thread_msg_hello(const void *p_msg);
static void _thread_msg_hello1(const void *p_msg);
static void _thread_msg_hello2(const void *p_msg);
static void _thread_entry(void *parameter);
static void _thread_exit(void);
static void _thread_dispatch_events(rt_uint32_t ev);
static void _thread_dispatch_evt_delayed(void *pargs);
static void _thread_msg_process(void *p_msg);

/**********************
 *  STATIC VARIABLES
 **********************/
static struct rt_thread  s_test_thread;
static rt_uint8_t        s_test_stack[CONFIG_THREAD_STACK_TEST];

static struct rt_mailbox s_test_msgq;
static rt_ubase_t        s_test_msgq_buf[THREAD_TEST_MSGQ_DEPTH];

static struct rt_mempool s_test_msg_slab;
static rt_uint8_t        s_test_msg_slab_buf[
    THREAD_MP_POOL_SIZE(THREAD_TEST_MSG_SLAB_BLOCK_SIZE, THREAD_TEST_MSG_SLAB_NUM)];

static thread_control_t s_thread_ctr_test = {
    .f_thread_init          = _thread_init,
    .f_thread_deinit        = _thread_deinit,
    .msgq                   = &s_test_msgq,
    .msgq_buf               = s_test_msgq_buf,
    .msgq_depth             = THREAD_TEST_MSGQ_DEPTH,
    .msg_slab               = &s_test_msg_slab,
    .msg_slab_buf           = s_test_msg_slab_buf,
    .msg_slab_block_size    = THREAD_TEST_MSG_SLAB_BLOCK_SIZE,
    .msg_slab_num           = THREAD_TEST_MSG_SLAB_NUM,
    .f_evt_delayed_dispatch = _thread_dispatch_evt_delayed,
};

/**********************
 *  STATIC FUNCTIONS
 **********************/
static int _thread_init(void)
{
    rt_thread_init(&s_test_thread,
                   THREAD_NAME,
                   _thread_entry, RT_NULL,
                   s_test_stack, sizeof(s_test_stack),
                   (rt_uint8_t)CONFIG_THREAD_PRIO_TEST,
                   10U);
    s_thread_ctr_test.tid = &s_test_thread;
    rt_thread_startup(&s_test_thread);
    return 0;
}

static void _thread_deinit(void)
{
    if (s_thread_ctr_test.tid != RT_NULL) {
        rt_thread_detach(s_thread_ctr_test.tid);
        s_thread_ctr_test.tid = RT_NULL;
    }
    thread_task_unregister(THREAD_ID_SELF);
}

static inline void _thread_hardware_init(void)
{
    LOG_I("hardware init");
}

static inline void _thread_resource_init(void)
{
    LOG_I("resource init");
}

static inline void _thread_service_init(void)
{
    LOG_I("service init");
}

static inline void _thread_hardware_deinit(void)
{
    LOG_I("hardware deinit");
}

static inline void _thread_resource_deinit(void)
{
    LOG_I("resource deinit");
}

static inline void _thread_service_deinit(void)
{
    LOG_I("service deinit");
}

static inline void _thread_wait_system_ready(void)
{
#ifdef CONFIG_THREAD_SYSTEM_READY
    {
        const rt_err_t err = thread_sysready_wait(RT_WAITING_FOREVER);

        if (err != RT_EOK) {
            LOG_E("system ready wait failed: %d", err);
        }
    }
#endif
}

static inline void _thread_system_ready_init(void)
{
    LOG_I("system ready init");
    /* Demo: broadcast EVENT_TEST_MSG_HELLO so test1 / test2 subscribers run */
    (void)thread_test_evt_send(EVENT_TEST_MSG_HELLO);
}

static void _log_msg_with_publisher(const char *evt_label, const thread_msg_t *msg)
{
    THREAD_ID_E   pub = (THREAD_ID_E)msg->head.publisher_task;
    const char   *nm  = thread_task_get_name(pub);
    char          pub_disp[RT_NAME_MAX + 12U];
    const char   *scope = (pub == THREAD_ID_SELF) ? "local" : "cross-task";

    if (nm != NULL && nm[0] != '\0') {
        rt_snprintf(pub_disp, sizeof(pub_disp), "%s", nm);
    } else {
        rt_snprintf(pub_disp, sizeof(pub_disp), "task%u", (unsigned)pub);
    }

    LOG_I("msgq %s [%s] msg_id=%u publisher=%s(%u)",
          evt_label, scope, (unsigned)msg->head.msg_id, pub_disp, (unsigned)pub);
}

static void _thread_msg_hello(const void *p_msg)
{
    _log_msg_with_publisher("hello", (const thread_msg_t *)p_msg);
}

static void _thread_msg_hello1(const void *p_msg)
{
    _log_msg_with_publisher("hello", (const thread_msg_t *)p_msg);
}

static void _thread_msg_hello2(const void *p_msg)
{
    _log_msg_with_publisher("hello", (const thread_msg_t *)p_msg);
}

/**********************
 *  thread entry
 **********************/
static void _thread_entry(void *parameter)
{
    ARG_UNUSED(parameter);

    s_thread_ctr_test.tid = rt_thread_self();

    thread_mgr_sys_start_sync_wait(THREAD_ID_SELF);

    _thread_hardware_init();
    _thread_resource_init();
    _thread_service_init();

    thread_mgr_sys_start_sync_end(THREAD_ID_SELF);

    _thread_wait_system_ready();

    _thread_system_ready_init();

    LOG_I("%s thread started: %u", THREAD_NAME, (unsigned)THREAD_ID_SELF);

    while (true) {
        rt_uint32_t recved = 0U;

        rt_event_recv(&s_thread_ctr_test.notify,
                      THREAD_NOTIFY_MSG_BIT | THREAD_NOTIFY_EXIT_BIT,
                      RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                      RT_WAITING_FOREVER, &recved);

        if ((recved & THREAD_NOTIFY_MSG_BIT) != 0U) {
            _thread_dispatch_events(SYSTEM_THREAD_NOTIRY_MSG_READY);
        }

        if ((recved & THREAD_NOTIFY_EXIT_BIT) != 0U) {
            _thread_dispatch_events(SYSTEM_THREAD_NOTIRY_SYNC_EXIT);
        }
    }
}

/**********************
 *  thread exit
 **********************/
static void _thread_exit(void)
{
    LOG_I("%s thread exit: %u", THREAD_NAME, (unsigned)THREAD_ID_SELF);
    thread_mgr_sys_exit_sync(THREAD_ID_SELF);

    while (true) {
        rt_thread_delay(RT_WAITING_FOREVER);
    }
}

/**********************
 *  thread dispatch events
 **********************/
static void _thread_dispatch_events(rt_uint32_t ev)
{
    if ((ev & SYSTEM_THREAD_NOTIRY_SYNC_EXIT) != 0U) {
        _thread_hardware_deinit();
        _thread_resource_deinit();
        _thread_service_deinit();
        _thread_exit();
    }

    if ((ev & SYSTEM_THREAD_NOTIRY_MSG_READY) != 0U) {
        rt_ubase_t val;

        while (s_thread_ctr_test.msgq != RT_NULL &&
               rt_mb_recv(s_thread_ctr_test.msgq, &val, RT_WAITING_NO) == RT_EOK) {
            void *p_msg = (void *)(uintptr_t)val;
            _thread_msg_process(p_msg);
            thread_msg_evt_free((void *)&s_thread_ctr_test, p_msg);
        }
    }
}

/**********************
 *  thread dispatch events delayed by soft-timer
 **********************/
static void _thread_dispatch_evt_delayed(void *pargs)
{
    if (pargs == RT_NULL) {
        return;
    }
    const uint32_t mid = (uint32_t)(uintptr_t)pargs;

    (void)thread_evt_send_by_task_id(THREAD_ID_SELF, mid);
}

/**********************
 *  thread msg process
 **********************/
static void _thread_msg_process(void *p_msg)
{
    if (!thread_msg_registry_dispatch_message(THREAD_ID_SELF, p_msg)) {
        LOG_E("receive error message, id = [%u] publisher = [%u]",
              (unsigned)((thread_msg_t *)p_msg)->head.msg_id,
              (unsigned)((thread_msg_t *)p_msg)->head.publisher_task);
    }
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
int32_t thread_test_evt_msg_send(uint32_t msg_id, uint8_t *pdata, uint32_t length)
{
    if (length > THREAD_TEST_MSG_MALLOC_MAX_PAYLOAD) {
        LOG_E("thread_test_evt_msg_send length %u > rt_malloc max %u",
              (unsigned)length, (unsigned)THREAD_TEST_MSG_MALLOC_MAX_PAYLOAD);
        return -1;
    }
    return thread_evt_msg_send_by_task_id((THREAD_ID_E)CONFIG_THREAD_ID_TEST,
                                          msg_id, pdata, length);
}

int32_t thread_test_evt_send(uint32_t msg_id)
{
    return thread_test_evt_msg_send(msg_id, RT_NULL, 0U);
}

int32_t thread_test_evt_send_delayed(uint32_t msg_id, uint32_t delay_ms)
{
    return thread_evt_send_delayed_by_task_id((THREAD_ID_E)THREAD_ID_SELF,
                                              msg_id, delay_ms);
}

int32_t thread_test_evt_cancel_delayed(uint32_t msg_id)
{
    return thread_evt_cancel_delayed_by_task_id((THREAD_ID_E)THREAD_ID_SELF, msg_id);
}

THREAD_CTRL_SLOT_ENTRY(((THREAD_ID_E)CONFIG_THREAD_ID_TEST), &s_thread_ctr_test);

THREAD_MSG_REGISTRY_ENTRY(((THREAD_ID_E)CONFIG_THREAD_ID_TEST), ((THREAD_ID_E)CONFIG_THREAD_ID_TEST),
                          EVENT_TEST_MSG_HELLO, _thread_msg_hello, true);
THREAD_MSG_REGISTRY_ENTRY(((THREAD_ID_E)CONFIG_THREAD_ID_TEST), ((THREAD_ID_E)CONFIG_THREAD_ID_TEST1),
                          EVENT_TEST1_MSG_HELLO, _thread_msg_hello1, true);
THREAD_MSG_REGISTRY_ENTRY(((THREAD_ID_E)CONFIG_THREAD_ID_TEST), ((THREAD_ID_E)CONFIG_THREAD_ID_TEST2),
                          EVENT_TEST2_MSG_HELLO, _thread_msg_hello2, true);
