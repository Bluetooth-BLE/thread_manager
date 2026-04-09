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

#define DBG_TAG  "thread"
#define DBG_LVL  DBG_LOG
#include <rtdbg.h>

#include "thread.h"

/**********************
 *  STATIC VARIABLES
 **********************/
static thread_control_t *s_registry[CONFIG_THREAD_ID_MUX_NUMBER];

/**********************
 *  STATIC FUNCTIONS
 **********************/
static void _thread_control_init(thread_control_t *ctrl, THREAD_ID_E id)
{
    if (ctrl == NULL) {
        return;
    }

    /* --- Mailbox init --- */
    if (ctrl->msgq != NULL && ctrl->msgq_buf != NULL && ctrl->msgq_depth > 0U) {
        char name[RT_NAME_MAX];
        rt_snprintf(name, sizeof(name), "mbx%u", (unsigned)id);
        rt_mb_init(ctrl->msgq, name, ctrl->msgq_buf, ctrl->msgq_depth, RT_IPC_FLAG_FIFO);
    }

    /* --- Memory pool init --- */
    if (ctrl->msg_slab != NULL && ctrl->msg_slab_buf != NULL &&
        ctrl->msg_slab_num > 0U && ctrl->msg_slab_block_size > 0U) {
        char name[RT_NAME_MAX];
        rt_snprintf(name, sizeof(name), "mp%u", (unsigned)id);
        rt_mp_init(ctrl->msg_slab, name, ctrl->msg_slab_buf,
                   (rt_size_t)ctrl->msg_slab_num *
                       THREAD_MP_BLOCK_TOTAL(ctrl->msg_slab_block_size),
                   (rt_size_t)ctrl->msg_slab_block_size);
    }

    /* --- Start semaphore (released by manager during startup sync) --- */
    {
        char name[RT_NAME_MAX];
        rt_snprintf(name, sizeof(name), "ss%u", (unsigned)id);
        rt_sem_init(&ctrl->start_sem, name, 0U, RT_IPC_FLAG_FIFO);
    }

    /* --- Notify event (MSG_BIT | EXIT_BIT) --- */
    {
        char name[RT_NAME_MAX];
        rt_snprintf(name, sizeof(name), "nf%u", (unsigned)id);
        rt_event_init(&ctrl->notify, name, RT_IPC_FLAG_FIFO);
    }
}

static void _thread_task_register(THREAD_ID_E id, thread_control_t *ctrl)
{
    if ((unsigned)id >= (unsigned)CONFIG_THREAD_ID_MUX_NUMBER) {
        return;
    }
    s_registry[id] = ctrl;
}

static int _thread_registry_bootstrap(void)
{
    THREAD_CTRL_SLOT_FOREACH(s) {
        _thread_control_init(s->ctrl, s->id);
        _thread_task_register(s->id, s->ctrl);
    }
    return 0;
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
void thread_task_unregister(THREAD_ID_E id)
{
    if ((unsigned)id >= (unsigned)CONFIG_THREAD_ID_MUX_NUMBER) {
        return;
    }
    s_registry[id] = NULL;
}

thread_control_t *thread_task_lookup(THREAD_ID_E id)
{
    if ((unsigned)id >= (unsigned)CONFIG_THREAD_ID_MUX_NUMBER) {
        return NULL;
    }
    return s_registry[id];
}

void thread_spawn_all_registered(void)
{
    for (unsigned id = 0U; id < (unsigned)CONFIG_THREAD_ID_MUX_NUMBER; id++) {
        if (id == (unsigned)CONFIG_THREAD_ID_MANAGER) {
            continue;
        }
        thread_control_t *t = s_registry[id];

        if (t != NULL && t->f_thread_init != NULL) {
            (void)t->f_thread_init();
        }
    }
}

const char *thread_task_get_name(THREAD_ID_E tid)
{
    thread_control_t *ctrl = thread_task_lookup(tid);

    if (ctrl == NULL || ctrl->tid == RT_NULL) {
        return NULL;
    }
    return ctrl->tid->parent.name;
}

/* Run at component init level (before app level where manager thread starts) */
INIT_COMPONENT_EXPORT(_thread_registry_bootstrap);
