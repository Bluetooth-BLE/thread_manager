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

#define DBG_TAG  "tmgr"
#define DBG_LVL  DBG_LOG
#include <rtdbg.h>

#include "thread.h"

/**********************
 *  STATIC DEFINES
 **********************/
#define THREAD_ID_SELF    ((THREAD_ID_E)CONFIG_THREAD_ID_MANAGER)
#define THREAD_ID_NAME    "tmanager"

/** Startup-sync timeout: 20 s per task */
#define MGR_START_SYNC_TIMEOUT_MS   20000U
/** Exit-sync timeout: 5 s total */
#define MGR_EXIT_SYNC_TIMEOUT_MS    5000U

/**********************
 *  STATIC PROTOTYPES
 **********************/
static int _thread_init(void);
static void _thread_deinit(void);
static void _thread_hardware_init(void);
static void _thread_service_init(void);
static void _thread_resource_init(void);
static void _thread_tasks_create(void);
static void _thread_msg_hello(const void *p_args);
static void _thread_entry(void *parameter);
static void _thread_startup_sync_by_id(THREAD_ID_E task_id);
static void _thread_startup_sync_all(void);
static rt_uint32_t _thread_exit_mask(void);
static void _thread_request_exit_notify(void);
static void _thread_tasks_exit_wait(void);
static void _thread_before_shutdown(uint32_t ev);
static void _thread_shutdown_sequence(const void *p_args);
static void _thread_dispatch_events(rt_uint32_t ev);

/**********************
 *  STATIC VARIABLES
 **********************/
static bool s_system_ok_flag = false;

/** Startup-done event: bit N set when task N completes startup sync */
static struct rt_event s_startup_done;
/** Exit-done event: bit N set when task N has exited */
static struct rt_event s_exit_done;
/** Manager notification event (application events + MSG_READY) */
static struct rt_event s_notify;

static struct rt_thread s_manager_thread;
static rt_uint8_t s_manager_stack[CONFIG_THREAD_STACK_MANAGER];

static thread_control_t s_thread_ctr_manager = {
    .f_thread_init   = _thread_init,
    .f_thread_deinit = _thread_deinit,
    /* Manager has no msgq / msg_slab — it receives events via s_notify only */
};

/**********************
 *  STATIC FUNCTIONS — base
 **********************/
static int _thread_init(void)
{
    rt_thread_init(&s_manager_thread,
                   THREAD_ID_NAME,
                   _thread_entry, RT_NULL,
                   s_manager_stack, sizeof(s_manager_stack),
                   (rt_uint8_t)CONFIG_THREAD_PRIO_MANAGER,
                   10U);
    rt_thread_startup(&s_manager_thread);
    return 0;
}

static void _thread_deinit(void)
{
    if (s_thread_ctr_manager.tid != RT_NULL) {
        rt_thread_detach(s_thread_ctr_manager.tid);
        s_thread_ctr_manager.tid = RT_NULL;
    }
    thread_task_unregister(THREAD_ID_SELF);
}

static void _thread_hardware_init(void)
{
    /* placeholder — add MCUboot confirmation etc. here if needed */
}

static void _thread_service_init(void)
{
    _thread_tasks_create();
}

static void _thread_resource_init(void)
{
    rt_event_init(&s_startup_done, "mgr_start", RT_IPC_FLAG_FIFO);
    rt_event_init(&s_exit_done,    "mgr_exit",  RT_IPC_FLAG_FIFO);
    rt_event_init(&s_notify,       "mgr_ntf",   RT_IPC_FLAG_FIFO);
}

/**********************
 *  STATIC FUNCTIONS — task create / startup sync
 **********************/
/**
 * Lower manager priority so newly spawned tasks (which immediately block on
 * start_sem) get a chance to run and reach their blocking point before the
 * manager begins the synchronised handshake.
 */
static void _thread_tasks_create(void)
{
    rt_uint8_t prio_sync = (rt_uint8_t)CONFIG_THREAD_MANAGER_PRIO_SYNC;

    rt_thread_control(rt_thread_self(), RT_THREAD_CTRL_CHANGE_PRIORITY, &prio_sync);
    thread_spawn_all_registered();

    rt_uint8_t prio_normal = (rt_uint8_t)CONFIG_THREAD_PRIO_MANAGER;
    rt_thread_control(rt_thread_self(), RT_THREAD_CTRL_CHANGE_PRIORITY, &prio_normal);
}

static void _thread_startup_sync_by_id(THREAD_ID_E task_id)
{
    thread_control_t *t = thread_task_lookup(task_id);

    if (t == NULL) {
        return;
    }

    /* Unblock the task so it can proceed with hardware/service init */
    rt_sem_release(&t->start_sem);

    const rt_uint32_t bit = (rt_uint32_t)(1U << (unsigned)task_id);
    rt_uint32_t recved = 0U;
    rt_err_t err = rt_event_recv(&s_startup_done, bit,
                                 RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                                 rt_tick_from_millisecond(MGR_START_SYNC_TIMEOUT_MS),
                                 &recved);

    if (err != RT_EOK || (recved & bit) == 0U) {
        LOG_E("startup sync timeout task=%u", (unsigned)task_id);
    }
}

static void _thread_startup_sync_all(void)
{
    for (unsigned id = 0U; id < (unsigned)CONFIG_THREAD_ID_MUX_NUMBER; id++) {
        if (id == (unsigned)THREAD_ID_SELF) {
            continue;
        }
        if (thread_task_lookup((THREAD_ID_E)id) == NULL) {
            continue;
        }
        _thread_startup_sync_by_id((THREAD_ID_E)id);
    }
}

static void _thread_msg_hello(const void *p_args)
{
    LOG_I("msg hello, event = 0x%08x", (uint32_t)(uintptr_t)p_args);
}

/**********************
 *  STATIC FUNCTIONS — thread entry
 **********************/
static void _thread_entry(void *parameter)
{
    ARG_UNUSED(parameter);
    s_thread_ctr_manager.tid = rt_thread_self();

    _thread_hardware_init();
    _thread_resource_init();
    _thread_service_init();

    _thread_startup_sync_all();

#ifdef CONFIG_THREAD_SYSTEM_READY
    thread_sysready_publish();
#endif
    s_system_ok_flag = true;

    LOG_I("%s thread started: %u", THREAD_ID_NAME, (unsigned)THREAD_ID_SELF);
    LOG_I("Thread manager: system initialized");

#define THREAD_MANAGER_EVENT_WAIT_MASK \
    (0x0000FFFFu | SYSTEM_THREAD_NOTIRY_MSG_READY)

    while (true) {
        rt_uint32_t recved = 0U;

        rt_event_recv(&s_notify, THREAD_MANAGER_EVENT_WAIT_MASK,
                      RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                      RT_WAITING_FOREVER, &recved);

        if (recved != 0U) {
            _thread_dispatch_events(recved);
        }
    }
#undef THREAD_MANAGER_EVENT_WAIT_MASK
}

/**********************
 *  STATIC FUNCTIONS — exit sequence
 **********************/
static rt_uint32_t _thread_exit_mask(void)
{
    rt_uint32_t m = 0U;

    for (unsigned id = 0U; id < (unsigned)CONFIG_THREAD_ID_MUX_NUMBER; id++) {
        if (id == (unsigned)THREAD_ID_SELF) {
            continue;
        }
        if (thread_task_lookup((THREAD_ID_E)id) == NULL) {
            continue;
        }
        m |= (rt_uint32_t)(1U << id);
    }
    return m;
}

static void _thread_request_exit_notify(void)
{
    for (unsigned id = 0U; id < (unsigned)CONFIG_THREAD_ID_MUX_NUMBER; id++) {
        if (id == (unsigned)THREAD_ID_SELF) {
            continue;
        }
        thread_control_t *t = thread_task_lookup((THREAD_ID_E)id);

        if (t == NULL) {
            continue;
        }
        rt_event_send(&t->notify, THREAD_NOTIFY_EXIT_BIT);
    }
}

static void _thread_tasks_exit_wait(void)
{
    const rt_uint32_t mask = _thread_exit_mask();

    if (mask == 0U) {
        return;
    }

    rt_uint32_t recved = 0U;
    rt_err_t err = rt_event_recv(&s_exit_done, mask,
                                 RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                                 rt_tick_from_millisecond(MGR_EXIT_SYNC_TIMEOUT_MS),
                                 &recved);

    if (err != RT_EOK) {
        LOG_E("exit sync incomplete: want=0x%08x got=0x%08x",
              (unsigned)mask, (unsigned)recved);
    }
}

static void _thread_before_shutdown(uint32_t ev)
{
    LOG_I("before shutdown, ev=0x%08x", (unsigned)ev);
}

static void _thread_shutdown_sequence(const void *p_args)
{
    _thread_before_shutdown((uint32_t)(uintptr_t)p_args);
    _thread_request_exit_notify();
    _thread_tasks_exit_wait();
}

/**********************
 *  STATIC FUNCTIONS — dispatch
 **********************/
static void _thread_dispatch_events(rt_uint32_t ev)
{
    (void)thread_msg_registry_dispatch_event(THREAD_ID_SELF, (uint32_t)ev,
                                             (void *)(uintptr_t)ev);
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
void thread_mgr_sys_start_sync_wait(THREAD_ID_E task)
{
    thread_control_t *c = thread_task_lookup(task);

    if (c == NULL) {
        return;
    }
    rt_sem_take(&c->start_sem, RT_WAITING_FOREVER);
}

void thread_mgr_sys_start_sync_end(THREAD_ID_E task)
{
    rt_event_send(&s_startup_done, (rt_uint32_t)(1U << (unsigned)task));
}

void thread_mgr_sys_exit_sync(THREAD_ID_E task)
{
    rt_event_send(&s_exit_done, (rt_uint32_t)(1U << (unsigned)task));
}

void thread_mgr_event_send(uint32_t event)
{
    rt_event_send(&s_notify, (rt_uint32_t)event);
}

/**********************
 *  MESSAGE REGISTRY
 **********************/
THREAD_MSG_REGISTRY_ENTRY(THREAD_ID_SELF, THREAD_ID_SELF, EVENT_SYSTEM_MSG_HELLO,
                           _thread_msg_hello, true);
THREAD_MSG_REGISTRY_ENTRY(THREAD_ID_SELF, THREAD_ID_SELF, EVENT_SYSTEM_POWEROFF,
                           _thread_shutdown_sequence, true);
THREAD_MSG_REGISTRY_ENTRY(THREAD_ID_SELF, THREAD_ID_SELF, EVENT_SYSTEM_REBOOT,
                           _thread_shutdown_sequence, true);
THREAD_MSG_REGISTRY_ENTRY(THREAD_ID_SELF, THREAD_ID_SELF, EVENT_SYSTEM_SHIPMODE,
                           _thread_shutdown_sequence, true);
THREAD_MSG_REGISTRY_ENTRY(THREAD_ID_SELF, THREAD_ID_SELF, EVENT_SYSTEM_LOWPOWER,
                           _thread_shutdown_sequence, true);

THREAD_CTRL_SLOT_ENTRY(((THREAD_ID_E)CONFIG_THREAD_ID_MANAGER), &s_thread_ctr_manager);

/* Manager thread starts last, after all slot registrations (component level 4) */
INIT_APP_EXPORT(_thread_init);
