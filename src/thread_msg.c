/*
 * Copyright (c) 2026, John.liu <450547566@qq.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author     Notes
 * 2026-04-07     John       first version
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <rtthread.h>
#include <rtatomic.h>

#define DBG_TAG  "thread_msg"
#define DBG_LVL  DBG_LOG
#include <rtdbg.h>

#include "thread.h"
#include "event_loop.h"

/**********************
 *  STATIC DEFINES
 **********************/
#define REG_ENTRY_ACTIVE(e) (rt_atomic_load((rt_atomic_t *)&(e)->subscribe_active) != 0)

/**********************
 *  STATIC PROTOTYPES
 **********************/
static thread_msg_t *_thread_evt_msg_malloc(thread_control_t *sub, uint32_t msg_id,
                                            const uint8_t *pdata, uint32_t length);
static int32_t _thread_evt_msg_forward(THREAD_ID_E subscriber_task,
                                       THREAD_ID_E publisher_task,
                                       uint32_t msg_id, const uint8_t *pdata,
                                       uint32_t length);
static int _thread_reg_cmp_direct(const void *a, const void *b);
static int _thread_reg_cmp_forward(const void *a, const void *b);
static int _thread_reg_triple_cmp(THREAD_ID_E s1, THREAD_ID_E p1, uint32_t m1,
                                  THREAD_ID_E s2, THREAD_ID_E p2, uint32_t m2);
static int _thread_reg_pair_cmp(THREAD_ID_E p1, uint32_t m1,
                                THREAD_ID_E p2, uint32_t m2);
static size_t _thread_reg_lower_direct(const thread_msg_registry_entry_t *a, size_t n,
                                       THREAD_ID_E s, THREAD_ID_E p, uint32_t m);
static size_t _thread_reg_upper_direct(const thread_msg_registry_entry_t *a, size_t n,
                                       THREAD_ID_E s, THREAD_ID_E p, uint32_t m);
static size_t _thread_reg_lower_forward(thread_msg_registry_entry_t **a, size_t n,
                                        THREAD_ID_E pub, uint32_t msg_id);
static size_t _thread_reg_upper_forward(thread_msg_registry_entry_t **a, size_t n,
                                        THREAD_ID_E pub, uint32_t msg_id);
static void _thread_msg_registry_dispatch_linear(THREAD_ID_E subscriber_task,
                                                 void *p_msg, bool *handled,
                                                 bool *forwarded);
static int _thread_msg_registry_init(void);

static void _thread_delayed_evt_loop_fire(void *arg);

/**********************
 *  STATIC VARIABLES
 **********************/
static thread_msg_registry_entry_t *s_reg_ram;
static size_t                       s_reg_ram_count;
static thread_msg_registry_entry_t **s_reg_forward;
static size_t                       s_reg_forward_count;
static bool                         s_reg_index_ready;

typedef struct {
    THREAD_ID_E task_id;
    uint32_t    msg_id;
    bool        in_use;
} _delayed_evt_slot_t;

static _delayed_evt_slot_t s_delayed_slots[CONFIG_THREAD_ID_MUX_NUMBER];
static struct rt_mutex     s_delayed_mutex;

/**********************
 *  STATIC FUNCTIONS — memory
 **********************/
static thread_msg_t *_thread_evt_msg_malloc(thread_control_t *sub, uint32_t msg_id,
                                            const uint8_t *pdata, uint32_t length)
{
    if (sub == NULL || (length > 0U && pdata == NULL)) {
        LOG_E("evt_msg_malloc: invalid args (msg_id=%u len=%u)", msg_id, length);
        return NULL;
    }

    if (sub->msgq == NULL) {
        LOG_E("evt_msg_malloc: msgq null (msg_id=%u)", msg_id);
        return NULL;
    }

    const rt_size_t need = sizeof(thread_msg_t) + (rt_size_t)length;
    thread_msg_t *cur;

    if (need > (rt_size_t)sub->msg_slab_block_size) {
        /* Message is larger than slab block — fall back to heap */
        cur = (thread_msg_t *)rt_malloc(need);
        if (cur == NULL) {
            LOG_E("evt_msg_malloc: rt_malloc fail (msg_id=%u len=%u)", msg_id, length);
            return NULL;
        }
    } else {
        if (sub->msg_slab == NULL) {
            LOG_E("evt_msg_malloc: msg_slab null (msg_id=%u)", msg_id);
            return NULL;
        }
        void *raw = rt_mp_alloc(sub->msg_slab, RT_WAITING_NO);
        if (raw == NULL) {
            LOG_E("evt_msg_malloc: slab alloc fail (msg_id=%u len=%u)", msg_id, length);
            return NULL;
        }
        cur = (thread_msg_t *)raw;
    }

    return cur;
}

/**********************
 *  STATIC FUNCTIONS — forward
 **********************/
static int32_t _thread_evt_msg_forward(THREAD_ID_E subscriber_task,
                                       THREAD_ID_E publisher_task,
                                       uint32_t msg_id, const uint8_t *pdata,
                                       uint32_t length)
{
    thread_control_t *sub = thread_task_lookup(subscriber_task);

    if (sub == NULL) {
        LOG_E("evt_msg_forward: subscriber not found (sub=%u pub=%u msg_id=%u)",
              (unsigned)subscriber_task, (unsigned)publisher_task, (unsigned)msg_id);
        return -RT_EINVAL;
    }

    thread_msg_t *cur = _thread_evt_msg_malloc(sub, msg_id, pdata, length);

    if (cur == NULL) {
        return -RT_ENOMEM;
    }

    cur->head.msg_id         = msg_id;
    cur->head.msg_len        = length;
    cur->head.publisher_task = publisher_task;
    if (length > 0U && pdata != NULL) {
        memcpy(cur->msg_buf, pdata, length);
    }

    if (rt_mb_send(sub->msgq, (rt_ubase_t)(uintptr_t)cur) != RT_EOK) {
        thread_msg_evt_free((void *)sub, cur);
        LOG_E("evt_msg_forward: mailbox full (sub=%u pub=%u msg_id=%u)",
              (unsigned)subscriber_task, (unsigned)publisher_task, (unsigned)msg_id);
        return -RT_EINVAL;
    }

    /* Wake the subscriber thread */
    rt_event_send(&sub->notify, THREAD_NOTIFY_MSG_BIT);
    return 0;
}

/**********************
 *  STATIC FUNCTIONS — sort / binary search
 **********************/
static int _thread_reg_triple_cmp(THREAD_ID_E s1, THREAD_ID_E p1, uint32_t m1,
                                  THREAD_ID_E s2, THREAD_ID_E p2, uint32_t m2)
{
    if ((unsigned)s1 < (unsigned)s2) return -1;
    if ((unsigned)s1 > (unsigned)s2) return  1;
    if (p1 < p2) return -1;
    if (p1 > p2) return  1;
    if (m1 < m2) return -1;
    if (m1 > m2) return  1;
    return 0;
}

static int _thread_reg_pair_cmp(THREAD_ID_E p1, uint32_t m1,
                                THREAD_ID_E p2, uint32_t m2)
{
    if (p1 < p2) return -1;
    if (p1 > p2) return  1;
    if (m1 < m2) return -1;
    if (m1 > m2) return  1;
    return 0;
}

static int _thread_reg_cmp_direct(const void *a, const void *b)
{
    const thread_msg_registry_entry_t *x = (const thread_msg_registry_entry_t *)a;
    const thread_msg_registry_entry_t *y = (const thread_msg_registry_entry_t *)b;

    return _thread_reg_triple_cmp(x->subscriber_task_id, x->publisher_task_id, x->msg_id,
                                  y->subscriber_task_id, y->publisher_task_id, y->msg_id);
}

static int _thread_reg_cmp_forward(const void *a, const void *b)
{
    const thread_msg_registry_entry_t *x = *(const thread_msg_registry_entry_t *const *)a;
    const thread_msg_registry_entry_t *y = *(const thread_msg_registry_entry_t *const *)b;
    int c = _thread_reg_pair_cmp(x->publisher_task_id, x->msg_id,
                                 y->publisher_task_id, y->msg_id);

    if (c != 0) return c;
    if ((unsigned)x->subscriber_task_id < (unsigned)y->subscriber_task_id) return -1;
    if ((unsigned)x->subscriber_task_id > (unsigned)y->subscriber_task_id) return  1;
    return 0;
}

static size_t _thread_reg_lower_direct(const thread_msg_registry_entry_t *a, size_t n,
                                       THREAD_ID_E s, THREAD_ID_E p, uint32_t m)
{
    size_t lo = 0U, hi = n;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2U;
        if (_thread_reg_triple_cmp(a[mid].subscriber_task_id,
                                   a[mid].publisher_task_id, a[mid].msg_id,
                                   s, p, m) < 0) {
            lo = mid + 1U;
        } else {
            hi = mid;
        }
    }
    return lo;
}

static size_t _thread_reg_upper_direct(const thread_msg_registry_entry_t *a, size_t n,
                                       THREAD_ID_E s, THREAD_ID_E p, uint32_t m)
{
    size_t lo = 0U, hi = n;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2U;
        if (_thread_reg_triple_cmp(a[mid].subscriber_task_id,
                                   a[mid].publisher_task_id, a[mid].msg_id,
                                   s, p, m) <= 0) {
            lo = mid + 1U;
        } else {
            hi = mid;
        }
    }
    return lo;
}

static size_t _thread_reg_lower_forward(thread_msg_registry_entry_t **a, size_t n,
                                        THREAD_ID_E pub, uint32_t msg_id)
{
    size_t lo = 0U, hi = n;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2U;
        if (_thread_reg_pair_cmp(a[mid]->publisher_task_id, a[mid]->msg_id,
                                 pub, msg_id) < 0) {
            lo = mid + 1U;
        } else {
            hi = mid;
        }
    }
    return lo;
}

static size_t _thread_reg_upper_forward(thread_msg_registry_entry_t **a, size_t n,
                                        THREAD_ID_E pub, uint32_t msg_id)
{
    size_t lo = 0U, hi = n;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2U;
        if (_thread_reg_pair_cmp(a[mid]->publisher_task_id, a[mid]->msg_id,
                                 pub, msg_id) <= 0) {
            lo = mid + 1U;
        } else {
            hi = mid;
        }
    }
    return lo;
}

/**********************
 *  STATIC FUNCTIONS — linear fallback
 **********************/
static void _thread_msg_registry_dispatch_linear(THREAD_ID_E subscriber_task,
                                                 void *p_msg, bool *handled,
                                                 bool *forwarded)
{
    const thread_msg_t *h = (const thread_msg_t *)p_msg;

    THREAD_MSG_REG_FOREACH(it) {
        if (it->msg_id != h->head.msg_id) {
            continue;
        }

        if (it->subscriber_task_id != subscriber_task &&
            it->publisher_task_id == subscriber_task &&
            h->head.publisher_task == (uint32_t)subscriber_task &&
            REG_ENTRY_ACTIVE(it) &&
            (unsigned)it->subscriber_task_id < (unsigned)CONFIG_THREAD_ID_MUX_NUMBER &&
            thread_task_lookup(it->subscriber_task_id) != NULL &&
            !forwarded[it->subscriber_task_id]) {
            (void)_thread_evt_msg_forward(it->subscriber_task_id,
                                          (THREAD_ID_E)h->head.publisher_task,
                                          h->head.msg_id,
                                          (const uint8_t *)h->msg_buf, h->head.msg_len);
            forwarded[it->subscriber_task_id] = true;
        }

        if (it->subscriber_task_id == subscriber_task &&
            it->publisher_task_id == h->head.publisher_task &&
            it->callback != NULL && REG_ENTRY_ACTIVE(it)) {
            it->callback(p_msg);
            *handled = true;
        }
    }
}

/**********************
 *  STATIC FUNCTIONS — init
 **********************/
static int _thread_msg_registry_init(void)
{
    /* Count ROM entries */
    size_t n = (size_t)(__stop_thread_msg_reg - __start_thread_msg_reg);

    s_reg_ram          = NULL;
    s_reg_ram_count    = 0U;
    s_reg_forward      = NULL;
    s_reg_forward_count = 0U;
    s_reg_index_ready  = false;

    /* Init delayed-event mutex */
    rt_mutex_init(&s_delayed_mutex, "etd_mtx", RT_IPC_FLAG_FIFO);

    if (n == 0U) {
        s_reg_index_ready = true;
        return 0;
    }

    /* Copy ROM → RAM (so subscribe_active can be mutated at runtime) */
    s_reg_ram = (thread_msg_registry_entry_t *)rt_malloc(n * sizeof(*s_reg_ram));
    if (s_reg_ram == NULL) {
        LOG_W("thread_msg_registry: rt_malloc ram table failed, using linear scan");
        return -RT_ENOMEM;
    }

    const thread_msg_registry_entry_t *start = __start_thread_msg_reg;
    for (size_t i = 0U; i < n; i++) {
        const thread_msg_registry_entry_t *rom = &start[i];

        s_reg_ram[i].subscriber_task_id = rom->subscriber_task_id;
        s_reg_ram[i].publisher_task_id  = rom->publisher_task_id;
        s_reg_ram[i].msg_id             = rom->msg_id;
        s_reg_ram[i].callback           = rom->callback;
        rt_atomic_store(&s_reg_ram[i].subscribe_active,
                        rt_atomic_load((rt_atomic_t *)(uintptr_t)&rom->subscribe_active));
    }

    qsort(s_reg_ram, n, sizeof(*s_reg_ram), _thread_reg_cmp_direct);
    s_reg_ram_count = n;

    /* Build forward index (entries where subscriber != publisher) */
    size_t nf = 0U;
    for (size_t i = 0U; i < n; i++) {
        if (s_reg_ram[i].subscriber_task_id != s_reg_ram[i].publisher_task_id) {
            nf++;
        }
    }

    if (nf > 0U) {
        s_reg_forward = (thread_msg_registry_entry_t **)rt_malloc(
            nf * sizeof(s_reg_forward[0]));
        if (s_reg_forward == NULL) {
            LOG_W("thread_msg_registry: rt_malloc forward index failed");
        } else {
            size_t w = 0U;
            for (size_t i = 0U; i < n; i++) {
                if (s_reg_ram[i].subscriber_task_id != s_reg_ram[i].publisher_task_id) {
                    s_reg_forward[w++] = &s_reg_ram[i];
                }
            }
            qsort(s_reg_forward, nf, sizeof(s_reg_forward[0]), _thread_reg_cmp_forward);
            s_reg_forward_count = nf;
        }
    }

    s_reg_index_ready = true;
    LOG_D("thread_msg_registry: ram_entries=%u forward_eligible=%u",
          (unsigned)n, (unsigned)nf);
    return RT_EOK;
}

/**********************
 *  STATIC FUNCTIONS — delayed events (event_loop)
 **********************/
static void _thread_delayed_evt_loop_fire(void *arg)
{
    _delayed_evt_slot_t *slot = (_delayed_evt_slot_t *)arg;

    if (slot == NULL) {
        return;
    }

    thread_control_t *sub = thread_task_lookup(slot->task_id);

    if (sub != NULL && sub->f_evt_delayed_dispatch != NULL) {
        sub->f_evt_delayed_dispatch((void *)(uintptr_t)slot->msg_id);
    }

    rt_mutex_take(&s_delayed_mutex, RT_WAITING_FOREVER);
    slot->in_use = false;
    rt_mutex_release(&s_delayed_mutex);
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
/* ---- thread_msg_evt_free ---- */
void thread_msg_evt_free(void *p_ctrl, void *p_msg)
{
    if (p_ctrl == NULL || p_msg == NULL) {
        return;
    }
    const rt_size_t need =
        sizeof(thread_msg_t) + (rt_size_t)((thread_msg_t *)p_msg)->head.msg_len;
    thread_control_t *ctrl = (thread_control_t *)p_ctrl;

    if (need > (rt_size_t)ctrl->msg_slab_block_size) {
        rt_free(p_msg);
    } else if (ctrl->msg_slab != NULL) {
        rt_mp_free(p_msg);
    }
}

/* ---- subscribe control ---- */
void thread_msg_subscribe_set_active(THREAD_ID_E subscriber, THREAD_ID_E publisher,
                                     uint32_t msg_id, thread_msg_callback_t cb,
                                     bool active)
{
    if (!s_reg_index_ready || s_reg_ram == NULL || s_reg_ram_count == 0U) {
        return;
    }

    size_t lo = _thread_reg_lower_direct(s_reg_ram, s_reg_ram_count,
                                         subscriber, publisher, msg_id);
    size_t hi = _thread_reg_upper_direct(s_reg_ram, s_reg_ram_count,
                                         subscriber, publisher, msg_id);

    for (size_t i = lo; i < hi; i++) {
        if (cb != NULL && s_reg_ram[i].callback != cb) {
            continue;
        }
        rt_atomic_store(&s_reg_ram[i].subscribe_active, active ? 1 : 0);
    }
}

bool thread_msg_subscribe_is_active(THREAD_ID_E subscriber, THREAD_ID_E publisher,
                                    uint32_t msg_id, thread_msg_callback_t cb)
{
    if (!s_reg_index_ready || s_reg_ram == NULL || s_reg_ram_count == 0U) {
        return false;
    }

    size_t lo = _thread_reg_lower_direct(s_reg_ram, s_reg_ram_count,
                                         subscriber, publisher, msg_id);
    size_t hi = _thread_reg_upper_direct(s_reg_ram, s_reg_ram_count,
                                         subscriber, publisher, msg_id);

    if (lo >= hi) {
        return false;
    }

    bool any_match = false;

    for (size_t i = lo; i < hi; i++) {
        if (cb != NULL && s_reg_ram[i].callback != cb) {
            continue;
        }
        any_match = true;
        if (!REG_ENTRY_ACTIVE(&s_reg_ram[i])) {
            return false;
        }
    }
    return any_match;
}

/* ---- dispatch ---- */
bool thread_msg_registry_dispatch_message(THREAD_ID_E subscriber_task, void *p_msg)
{
    const thread_msg_t *h = (const thread_msg_t *)p_msg;
    bool handled = false;
    bool forwarded[CONFIG_THREAD_ID_MUX_NUMBER];

    memset(forwarded, 0, sizeof(forwarded));

    if (!s_reg_index_ready) {
        _thread_msg_registry_dispatch_linear(subscriber_task, p_msg,
                                             &handled, forwarded);
        return handled;
    }

    const THREAD_ID_E pub = (THREAD_ID_E)h->head.publisher_task;
    const uint32_t    mid = h->head.msg_id;

    size_t d_lo = _thread_reg_lower_direct(s_reg_ram, s_reg_ram_count,
                                           subscriber_task, pub, mid);
    size_t d_hi = _thread_reg_upper_direct(s_reg_ram, s_reg_ram_count,
                                           subscriber_task, pub, mid);

    if (s_reg_forward != NULL && s_reg_forward_count > 0U &&
        subscriber_task == pub) {
        size_t f_lo = _thread_reg_lower_forward(s_reg_forward, s_reg_forward_count,
                                                pub, mid);
        size_t f_hi = _thread_reg_upper_forward(s_reg_forward, s_reg_forward_count,
                                                pub, mid);

        for (size_t j = f_lo; j < f_hi; j++) {
            thread_msg_registry_entry_t *re = s_reg_forward[j];

            if (re->subscriber_task_id == subscriber_task) {
                continue;
            }
            if (!REG_ENTRY_ACTIVE(re) ||
                (unsigned)re->subscriber_task_id >= (unsigned)CONFIG_THREAD_ID_MUX_NUMBER ||
                thread_task_lookup(re->subscriber_task_id) == NULL ||
                forwarded[re->subscriber_task_id]) {
                continue;
            }
            (void)_thread_evt_msg_forward(re->subscriber_task_id, pub,
                                          h->head.msg_id,
                                          (const uint8_t *)h->msg_buf, h->head.msg_len);
            forwarded[re->subscriber_task_id] = true;
        }
    }

    for (size_t i = d_lo; i < d_hi; i++) {
        thread_msg_registry_entry_t *re = &s_reg_ram[i];

        if (re->callback != NULL && REG_ENTRY_ACTIVE(re)) {
            re->callback(p_msg);
            handled = true;
        }
    }

    return handled;
}

bool thread_msg_registry_dispatch_event(THREAD_ID_E subscriber_task,
                                        uint32_t event_id, void *opaque)
{
    if (s_reg_index_ready && s_reg_ram != NULL) {
        for (size_t i = 0U; i < s_reg_ram_count; i++) {
            thread_msg_registry_entry_t *re = &s_reg_ram[i];

            if (re->subscriber_task_id != subscriber_task) continue;
            if (re->publisher_task_id  != subscriber_task) continue;
            if (re->msg_id             != event_id)        continue;

            if (re->callback != NULL && REG_ENTRY_ACTIVE(re)) {
                re->callback(opaque);
                return true;
            }
        }
        return false;
    }

    /* Fallback: linear scan of ROM table */
    THREAD_MSG_REG_FOREACH(it) {
        if (it->subscriber_task_id != subscriber_task) continue;
        if (it->publisher_task_id  != subscriber_task) continue;
        if (it->msg_id             != event_id)        continue;

        if (it->callback != NULL &&
            rt_atomic_load((rt_atomic_t *)(uintptr_t)&it->subscribe_active) != 0) {
            it->callback(opaque);
            return true;
        }
    }
    return false;
}

/* ---- send helpers ---- */
int32_t thread_evt_msg_send_by_task_id(THREAD_ID_E task_id, uint32_t msg_id,
                                       uint8_t *pdata, uint32_t length)
{
    return _thread_evt_msg_forward(task_id, task_id, msg_id,
                                   (const uint8_t *)pdata, length);
}

int32_t thread_evt_send_by_task_id(THREAD_ID_E task_id, uint32_t msg_id)
{
    return _thread_evt_msg_forward(task_id, task_id, msg_id, NULL, 0U);
}

int32_t thread_evt_send_delayed_by_task_id(THREAD_ID_E task_id, uint32_t msg_id,
                                           uint32_t delay_ms)
{
    if (delay_ms == 0U) {
        return _thread_evt_msg_forward(task_id, task_id, msg_id, NULL, 0U);
    }

    thread_control_t *sub = thread_task_lookup(task_id);

    if (sub == NULL || sub->f_evt_delayed_dispatch == NULL) {
        LOG_E("delayed: task %u missing or dispatch unset", (unsigned)task_id);
        return -RT_EINVAL;
    }

    rt_mutex_take(&s_delayed_mutex, RT_WAITING_FOREVER);

    _delayed_evt_slot_t *slot = NULL;
    for (unsigned i = 0U; i < CONFIG_THREAD_ID_MUX_NUMBER; i++) {
        if (!s_delayed_slots[i].in_use) {
            slot = &s_delayed_slots[i];
            break;
        }
    }

    if (slot == NULL) {
        rt_mutex_release(&s_delayed_mutex);
        LOG_E("delayed: pool full (task=%u msg=%u)", (unsigned)task_id, (unsigned)msg_id);
        return -RT_ENOMEM;
    }

    slot->task_id = task_id;
    slot->msg_id  = msg_id;
    slot->in_use  = true;

    rt_mutex_release(&s_delayed_mutex);

    EVT_LOOP_PUSH((void *)_thread_delayed_evt_loop_fire, (void *)slot, delay_ms);
    return RT_EOK;
}

int32_t thread_evt_cancel_delayed_by_task_id(THREAD_ID_E task_id, uint32_t msg_id)
{
    rt_mutex_take(&s_delayed_mutex, RT_WAITING_FOREVER);

    for (unsigned i = 0U; i < CONFIG_THREAD_ID_MUX_NUMBER; i++) {
        _delayed_evt_slot_t *e = &s_delayed_slots[i];

        if (e->in_use && e->task_id == task_id && e->msg_id == msg_id) {
            (void)EVT_LOOP_REMOVE_WITH_ARGS((void *)_thread_delayed_evt_loop_fire, (void *)e);
            e->in_use = false;
            rt_mutex_release(&s_delayed_mutex);
            return RT_EOK;
        }
    }

    rt_mutex_release(&s_delayed_mutex);
    return -RT_ENOENT;
}

#if defined(RT_USING_FINSH) && defined(FINSH_USING_MSH)
static int msh_thread_evt_send_delayed(int argc, char **argv)
{
    char          *end;
    unsigned long  tid;
    unsigned long  mid;
    unsigned long  delay_ms;

    if (argc != 4) {
        LOG_W("Usage: tmsg_tx_delay <task_id> <msg_id> <delay_ms>");
        LOG_W("  ids: decimal or 0x hex; delay_ms > 0 uses event_loop delayed path");
        return -RT_EINVAL;
    }

    tid = strtoul(argv[1], &end, 0);
    if (end == argv[1] || *end != '\0') {
        LOG_E("bad task_id: %s", argv[1]);
        return -RT_EINVAL;
    }
    mid = strtoul(argv[2], &end, 0);
    if (end == argv[2] || *end != '\0') {
        LOG_E("bad msg_id: %s", argv[2]);
        return -RT_EINVAL;
    }
    delay_ms = strtoul(argv[3], &end, 0);
    if (end == argv[3] || *end != '\0') {
        LOG_E("bad delay_ms: %s", argv[3]);
        return -RT_EINVAL;
    }

    int32_t ret = thread_evt_send_delayed_by_task_id((THREAD_ID_E)tid, (uint32_t)mid, (uint32_t)delay_ms);
    if (ret != 0) {
        LOG_E("tmsg_tx_delay failed: %ld (task=%lu msg_id=0x%lx delay=%lu)",
              (long)ret, tid, mid, delay_ms);
        return -RT_EINVAL;
    }
    LOG_I("tmsg_tx_delay ok task_id=%lu msg_id=0x%lx delay_ms=%lu", tid, mid, delay_ms);
    return RT_EOK;
}

static int msh_thread_evt_cancel_delayed(int argc, char **argv)
{
    char          *end;
    unsigned long  tid;
    unsigned long  mid;

    if (argc != 3) {
        LOG_W("Usage: tmsg_cancel <task_id> <msg_id>");
        return -RT_EINVAL;
    }

    tid = strtoul(argv[1], &end, 0);
    if (end == argv[1] || *end != '\0') {
        LOG_E("bad task_id: %s", argv[1]);
        return -RT_EINVAL;
    }
    mid = strtoul(argv[2], &end, 0);
    if (end == argv[2] || *end != '\0') {
        LOG_E("bad msg_id: %s", argv[2]);
        return -RT_EINVAL;
    }

    int32_t ret = thread_evt_cancel_delayed_by_task_id((THREAD_ID_E)tid, (uint32_t)mid);
    if (ret != 0) {
        LOG_E("tmsg_cancel failed: %ld (task=%lu msg_id=0x%lx)", (long)ret, tid, mid);
        return -RT_EINVAL;
    }
    LOG_I("tmsg_cancel ok task_id=%lu msg_id=0x%lx", tid, mid);
    return RT_EOK;
}

static int msh_thread_evt_send(int argc, char **argv)
{
    char          *end;
    unsigned long  tid;
    unsigned long  mid;

    if (argc != 3) {
        LOG_W("Usage: tmsg_send <task_id> <msg_id>");
        LOG_W("  ids: decimal or 0x hex (e.g. tmsg_send 0 1)");
        return -RT_EINVAL;
    }

    tid = strtoul(argv[1], &end, 0);
    if (end == argv[1] || *end != '\0') {
        LOG_E("bad task_id: %s", argv[1]);
        return -RT_EINVAL;
    }
    mid = strtoul(argv[2], &end, 0);
    if (end == argv[2] || *end != '\0') {
        LOG_E("bad msg_id: %s", argv[2]);
        return -RT_EINVAL;
    }

    int32_t ret = thread_evt_send_by_task_id((THREAD_ID_E)tid, (uint32_t)mid);
    if (ret != 0) {
        LOG_E("tmsg_send failed: %ld (task=%lu msg_id=0x%lx)", (long)ret, tid, mid);
        return -RT_EINVAL;
    }
    LOG_I("tmsg_send ok task_id=%lu msg_id=0x%lx", tid, mid);
    return RT_EOK;
}

MSH_CMD_EXPORT_ALIAS(msh_thread_evt_send, tmsg_send, send zero-payload event to task mailbox);
MSH_CMD_EXPORT_ALIAS(msh_thread_evt_send_delayed, tmsg_send_de,
                     send delayed zero-payload event via event_loop);
MSH_CMD_EXPORT_ALIAS(msh_thread_evt_cancel_delayed, tmsg_cal,
                     cancel pending delayed event for task/msg);
#endif /* RT_USING_FINSH && FINSH_USING_MSH */

/* Run just after _thread_registry_bootstrap (component level) */
INIT_COMPONENT_EXPORT(_thread_msg_registry_init);
