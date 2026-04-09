# Copyright (c) 2026, John.liu <450547566@qq.com>
#
# SPDX-License-Identifier: Apache-2.0
#
# Change Logs:
# Date           Author     Notes
# 2026-04-07     John       first version

"""
Generate thread_xxx.c / thread_xxx.h templates from a base name (RT-Thread).

Defaults (aligned with inc/thread_config.h and thread_test.c; overridable):
  - CONFIG_THREAD_STACK_<SHORT>  -> 2048 (bytes)
  - CONFIG_THREAD_PRIO_<SHORT>   -> 25 (must be < RT_THREAD_PRIORITY_MAX, often 32)
  - Generated .c uses CONFIG_THREAD_ID_<SHORT> / CONFIG_THREAD_STACK_<SHORT> /
    CONFIG_THREAD_PRIO_<SHORT> (maintained by you in inc/thread_config.h)

Usage:
  python thread_file_gen.py thread_sensor
  python thread_file_gen.py thread_sensor.c -o .
  python thread_file_gen.py sensor --out-dir ./out

  # Merge into the project (writes inc/thread_config.h)
  python thread_file_gen.py thread_foo --integrate
  python thread_file_gen.py thread_bar --integrate --stack 3072 --prio 36

Notes:
  - Accepts thread_sensor, thread_sensor.c, or sensor (normalized to thread_sensor)
  - --integrate writes to inc/thread_config.h:
      · CONFIG_THREAD_ID_<UP> inserted immediately before CONFIG_THREAD_ID_MANAGER (enum: " = " spaced)
      · CONFIG_THREAD_ID_MANAGER and CONFIG_THREAD_ID_MUX_NUMBER each incremented by 1; enum block re-aligned
      · CONFIG_THREAD_STACK_<UP> inserted before CONFIG_THREAD_STACK_MANAGER; STACK block column-aligned
      · CONFIG_THREAD_PRIO_<UP> inserted before CONFIG_THREAD_PRIO_MANAGER; PRIO block column-aligned
  - Without --integrate, add the macros to inc/thread_config.h manually
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

DEFAULT_STACK = 1024
DEFAULT_PRIO  = 25


def normalize_stem(raw: str) -> str:
    """Normalize to thread_xxx stem (no extension)."""
    p = Path(raw)
    name = p.stem if p.suffix.lower() in (".c", ".h") else p.name
    name = name.strip()
    if not name.startswith("thread_"):
        name = f"thread_{name}"
    if not re.match(r"^[a-zA-Z][a-zA-Z0-9_]*$", name):
        raise ValueError(f"Invalid base name: {raw!r}")
    return name


def short_from_stem(stem: str) -> str:
    """thread_ble -> ble"""
    assert stem.startswith("thread_")
    return stem[len("thread_"):]


def upper_snake(s: str) -> str:
    return s.upper()


def render_c(stem: str, short: str, up: str) -> str:
    ctr = f"s_thread_ctr_{short}"
    return f'''/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author     Notes
 * 2026-04-07     John       first version
 */

#include <string.h>
#include <rtthread.h>

#define DBG_TAG  "t{short}"
#define DBG_LVL  DBG_LOG
#include <rtdbg.h>

#include "{stem}.h"

/**********************
 *  STATIC DEFINES
 **********************/
#define THREAD_ID_SELF                           ((THREAD_ID_E)CONFIG_THREAD_ID_{up})
#define THREAD_NAME                              "t{short}"

/** Max payload per mempool block (bytes) */
#define THREAD_{up}_MSG_MAX_PAYLOAD       64U
#define THREAD_{up}_MSG_SLAB_BLOCK_SIZE   (sizeof(thread_msg_t) + THREAD_{up}_MSG_MAX_PAYLOAD)
#define THREAD_{up}_MSGQ_DEPTH            16U
#define THREAD_{up}_MSG_SLAB_NUM          (THREAD_{up}_MSGQ_DEPTH + 2U)
/** Max payload on rt_malloc path; caps abnormal lengths from exhausting the heap */
#define THREAD_{up}_MSG_MALLOC_MAX_PAYLOAD (256U)

/**********************
 *  STATIC PROTOTYPES
 **********************/
/* _thread_init must return int (thread_control_t.f_thread_init / INIT_APP_EXPORT). */
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
static void _thread_entry(void *parameter);
static void _thread_exit(void);
static void _thread_dispatch_events(rt_uint32_t ev);
static void _thread_dispatch_evt_delayed(void *pargs);
static void _thread_msg_process(void *p_msg);

/**********************
 *  STATIC VARIABLES
 **********************/
static struct rt_thread  s_{short}_thread;
static rt_uint8_t        s_{short}_stack[CONFIG_THREAD_STACK_{up}];

static struct rt_mailbox s_{short}_msgq;
static rt_ubase_t        s_{short}_msgq_buf[THREAD_{up}_MSGQ_DEPTH];

static struct rt_mempool s_{short}_msg_slab;
static rt_uint8_t        s_{short}_msg_slab_buf[
    THREAD_MP_POOL_SIZE(THREAD_{up}_MSG_SLAB_BLOCK_SIZE, THREAD_{up}_MSG_SLAB_NUM)];

static thread_control_t {ctr} = {{
    .f_thread_init          = _thread_init,
    .f_thread_deinit        = _thread_deinit,
    .msgq                   = &s_{short}_msgq,
    .msgq_buf               = s_{short}_msgq_buf,
    .msgq_depth             = THREAD_{up}_MSGQ_DEPTH,
    .msg_slab               = &s_{short}_msg_slab,
    .msg_slab_buf           = s_{short}_msg_slab_buf,
    .msg_slab_block_size    = THREAD_{up}_MSG_SLAB_BLOCK_SIZE,
    .msg_slab_num           = THREAD_{up}_MSG_SLAB_NUM,
    .f_evt_delayed_dispatch = _thread_dispatch_evt_delayed,
}};

/**********************
 *  STATIC FUNCTIONS
 **********************/
static int _thread_init(void)
{{
    rt_thread_init(&s_{short}_thread,
                   THREAD_NAME,
                   _thread_entry, RT_NULL,
                   s_{short}_stack, sizeof(s_{short}_stack),
                   (rt_uint8_t)CONFIG_THREAD_PRIO_{up},
                   10U);
    {ctr}.tid = &s_{short}_thread;
    rt_thread_startup(&s_{short}_thread);
    return 0;
}}

static void _thread_deinit(void)
{{
    if ({ctr}.tid != RT_NULL) {{
        rt_thread_detach({ctr}.tid);
        {ctr}.tid = RT_NULL;
    }}
    thread_task_unregister(THREAD_ID_SELF);
}}

static inline void _thread_hardware_init(void)
{{
    LOG_I("hardware init");
}}

static inline void _thread_resource_init(void)
{{
    LOG_I("resource init");
}}

static inline void _thread_service_init(void)
{{
    LOG_I("service init");
}}

static inline void _thread_hardware_deinit(void)
{{
    LOG_I("hardware deinit");
}}

static inline void _thread_resource_deinit(void)
{{
    LOG_I("resource deinit");
}}

static inline void _thread_service_deinit(void)
{{
    LOG_I("service deinit");
}}

static inline void _thread_wait_system_ready(void)
{{
#ifdef CONFIG_THREAD_SYSTEM_READY
    {{
        const rt_err_t err = thread_sysready_wait(RT_WAITING_FOREVER);
        if (err != RT_EOK) {{
            LOG_E("system ready wait failed: %d", err);
        }}
    }}
#endif
}}

static inline void _thread_system_ready_init(void)
{{
    LOG_I("system ready init");
}}

static void _thread_msg_hello(const void *p_msg)
{{
    const thread_msg_t *msg = (const thread_msg_t *)p_msg;

    LOG_I("msgq hello (local), msg_id = %u publisher=%u",
          (unsigned)msg->head.msg_id, (unsigned)msg->head.publisher_task);
}}

/**********************
 *  thread entry
 **********************/
static void _thread_entry(void *parameter)
{{
    ARG_UNUSED(parameter);

    {ctr}.tid = rt_thread_self();

    thread_mgr_sys_start_sync_wait(THREAD_ID_SELF);

    _thread_hardware_init();
    _thread_resource_init();
    _thread_service_init();

    thread_mgr_sys_start_sync_end(THREAD_ID_SELF);

    _thread_wait_system_ready();

    _thread_system_ready_init();

    LOG_I("%s thread started: %u", THREAD_NAME, (unsigned)THREAD_ID_SELF);

    while (true) {{
        rt_uint32_t recved = 0U;

        rt_event_recv(&{ctr}.notify,
                      THREAD_NOTIFY_MSG_BIT | THREAD_NOTIFY_EXIT_BIT,
                      RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                      RT_WAITING_FOREVER, &recved);

        if ((recved & THREAD_NOTIFY_MSG_BIT) != 0U) {{
            _thread_dispatch_events(SYSTEM_THREAD_NOTIRY_MSG_READY);
        }}

        if ((recved & THREAD_NOTIFY_EXIT_BIT) != 0U) {{
            _thread_dispatch_events(SYSTEM_THREAD_NOTIRY_SYNC_EXIT);
        }}
    }}
}}

/**********************
 *  thread exit
 **********************/
static void _thread_exit(void)
{{
    LOG_I("%s thread exit: %u", THREAD_NAME, (unsigned)THREAD_ID_SELF);
    thread_mgr_sys_exit_sync(THREAD_ID_SELF);

    while (true) {{
        rt_thread_delay(RT_WAITING_FOREVER);
    }}
}}

/**********************
 *  thread dispatch events
 **********************/
static void _thread_dispatch_events(rt_uint32_t ev)
{{
    if ((ev & SYSTEM_THREAD_NOTIRY_SYNC_EXIT) != 0U) {{
        _thread_hardware_deinit();
        _thread_resource_deinit();
        _thread_service_deinit();
        _thread_exit();
    }}

    if ((ev & SYSTEM_THREAD_NOTIRY_MSG_READY) != 0U) {{
        rt_ubase_t val;

        while ({ctr}.msgq != RT_NULL &&
               rt_mb_recv({ctr}.msgq, &val, RT_WAITING_NO) == RT_EOK) {{
            void *p_msg = (void *)(uintptr_t)val;
            _thread_msg_process(p_msg);
            thread_msg_evt_free((void *)&{ctr}, p_msg);
        }}
    }}
}}

/**********************
 *  thread dispatch events delayed by EVENT_LOOP
 **********************/
static void _thread_dispatch_evt_delayed(void *pargs)
{{
    if (pargs == RT_NULL) {{
        return;
    }}
    const uint32_t mid = (uint32_t)(uintptr_t)pargs;

    (void)thread_evt_send_by_task_id(THREAD_ID_SELF, mid);
}}

/**********************
 *  thread msg process
 **********************/
static void _thread_msg_process(void *p_msg)
{{
    if (!thread_msg_registry_dispatch_message(THREAD_ID_SELF, p_msg)) {{
        LOG_E("receive error message, id = [%u] publisher = [%u]",
              (unsigned)((thread_msg_t *)p_msg)->head.msg_id,
              (unsigned)((thread_msg_t *)p_msg)->head.publisher_task);
    }}
}}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
int32_t thread_{short}_evt_msg_send(uint32_t msg_id, uint8_t *pdata, uint32_t length)
{{
    if (length > THREAD_{up}_MSG_MALLOC_MAX_PAYLOAD) {{
        LOG_E("thread_{short}_evt_msg_send length %u > rt_malloc max %u",
              (unsigned)length, (unsigned)THREAD_{up}_MSG_MALLOC_MAX_PAYLOAD);
        return -1;
    }}
    return thread_evt_msg_send_by_task_id((THREAD_ID_E)CONFIG_THREAD_ID_{up},
                                          msg_id, pdata, length);
}}

int32_t thread_{short}_evt_send(uint32_t msg_id)
{{
    return thread_{short}_evt_msg_send(msg_id, RT_NULL, 0U);
}}

int32_t thread_{short}_evt_send_delayed(uint32_t msg_id, uint32_t delay_ms)
{{
    return thread_evt_send_delayed_by_task_id((THREAD_ID_E)THREAD_ID_SELF, msg_id, delay_ms);
}}

int32_t thread_{short}_evt_cancel_delayed(uint32_t msg_id)
{{
    return thread_evt_cancel_delayed_by_task_id((THREAD_ID_E)THREAD_ID_SELF, msg_id);
}}

THREAD_CTRL_SLOT_ENTRY(((THREAD_ID_E)CONFIG_THREAD_ID_{up}), &{ctr});

THREAD_MSG_REGISTRY_ENTRY(THREAD_ID_SELF, THREAD_ID_SELF, EVENT_{up}_MSG_HELLO,
                           _thread_msg_hello, true);
'''


def render_h(stem: str, short: str, up: str) -> str:
    guard = f"__{stem.upper()}_H__"
    msg_t = f"thread_{short}_msg_t"
    return f'''/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author     Notes
 * 2026-04-07     John       first version
 */

#ifndef {guard}
#define {guard}

#include <stdint.h>
#include <stddef.h>

#include "thread.h"

#ifdef __cplusplus
extern "C" {{
#endif


/**********************
 *  STATIC DEFINES
 **********************/
typedef enum {{
    EVENT_{up}_MSG_HELLO = 0x01,
}} EVENT_{up}_MSG_ID_E;


/**********************
 *      TYPEDEFS
 **********************/
typedef struct {{
    thread_msg_head_t head;
    uint8_t           msg_buf[];
}} {msg_t};

/**********************
 *  FUNCTIONS
 **********************/
int32_t thread_{short}_evt_msg_send(uint32_t msg_id, uint8_t *pdata, uint32_t length);
int32_t thread_{short}_evt_send(uint32_t msg_id);
int32_t thread_{short}_evt_send_delayed(uint32_t msg_id, uint32_t delay_ms);
int32_t thread_{short}_evt_cancel_delayed(uint32_t msg_id);

#ifdef __cplusplus
}}
#endif
#endif /* {guard} */
'''


def _reformat_thread_id_enum(lines: list[str]) -> None:
    """Align CONFIG_THREAD_ID_* enum lines: ``NAME<pad> = value,<suffix>``."""
    pat = re.compile(r"^(\s*)(CONFIG_THREAD_ID_\w+)\s*=\s*(\d+)\s*,(.*)$")
    entries: list[tuple[int, str, str, str, str]] = []
    for i, ln in enumerate(lines):
        s = ln.rstrip("\n\r")
        m = pat.match(s)
        if m:
            entries.append((i, m.group(1), m.group(2), m.group(3), m.group(4)))
    if not entries:
        return
    max_name = max(len(e[2]) for e in entries)
    for i, indent, name, val, suf in entries:
        suf = suf.rstrip()
        if suf and not suf.startswith(" "):
            suf = " " + suf
        lines[i] = f"{indent}{name:<{max_name}} = {val},{suf}\n"


def _reformat_stack_block(lines: list[str]) -> None:
    """Column-align #define CONFIG_THREAD_STACK_* ... through CONFIG_THREAD_STACK_MANAGER."""
    pat = re.compile(r"^#define\s+(CONFIG_THREAD_STACK_\w+)\s+(\S+)\s*$")
    block: list[tuple[int, str, str]] = []
    started = False
    for i, ln in enumerate(lines):
        s = ln.rstrip("\n\r")
        if not s.strip():
            if started:
                continue
            continue
        m = pat.match(s)
        if not m:
            if started:
                break
            continue
        started = True
        block.append((i, m.group(1), m.group(2)))
        if m.group(1) == "CONFIG_THREAD_STACK_MANAGER":
            break
    if not block:
        return
    max_len = max(len(n) for _, n, _ in block)
    for i, name, val in block:
        lines[i] = f"#define {name:<{max_len}} {val}\n"


def _reformat_prio_block(lines: list[str]) -> None:
    """Column-align #define CONFIG_THREAD_PRIO_* ... through CONFIG_THREAD_PRIO_MANAGER."""
    pat = re.compile(r"^#define\s+(CONFIG_THREAD_PRIO_\w+)\s+(\S+)\s*$")
    block: list[tuple[int, str, str]] = []
    started = False
    for i, ln in enumerate(lines):
        s = ln.rstrip("\n\r")
        if not s.strip():
            if started:
                continue
            continue
        m = pat.match(s)
        if not m:
            if started:
                break
            continue
        started = True
        block.append((i, m.group(1), m.group(2)))
        if m.group(1) == "CONFIG_THREAD_PRIO_MANAGER":
            break
    if not block:
        return
    max_len = max(len(n) for _, n, _ in block)
    for i, name, val in block:
        lines[i] = f"#define {name:<{max_len}} {val}\n"


def integrate_thread_config_h(
    path: Path,
    up: str,
    stack: int,
    prio: str,
) -> list[str]:
    """
    Write new task ID / STACK / PRIO into inc/thread_config.h.

    - CONFIG_THREAD_ID_<UP> before CONFIG_THREAD_ID_MANAGER (enum uses `` = `` with spaces; block re-aligned)
    - CONFIG_THREAD_ID_MANAGER / CONFIG_THREAD_ID_MUX_NUMBER incremented by 1
    - CONFIG_THREAD_STACK_<UP> before CONFIG_THREAD_STACK_MANAGER (column-aligned with other STACK lines)
    - CONFIG_THREAD_PRIO_<UP> before CONFIG_THREAD_PRIO_MANAGER (column-aligned; excludes CONFIG_THREAD_MANAGER_PRIO_SYNC)
    """
    log: list[str] = []
    text = path.read_text(encoding="utf-8")
    lines = text.splitlines(keepends=True)

    id_macro = f"CONFIG_THREAD_ID_{up}"
    stack_macro = f"CONFIG_THREAD_STACK_{up}"
    prio_macro = f"CONFIG_THREAD_PRIO_{up}"

    enum_mgr_pat = re.compile(r"^(\s*)CONFIG_THREAD_ID_MANAGER\s*=\s*(\d+)\s*,(.*)$")
    enum_mux_pat = re.compile(r"^(\s*)CONFIG_THREAD_ID_MUX_NUMBER\s*=\s*(\d+)\s*,(.*)$")

    flat = "".join(lines)

    # ---- 1. Insert THREAD_ID ----------------------------------------------
    if id_macro in flat:
        log.append(f"skip: already has {id_macro}")
    else:
        mgr_line_idx = None
        mgr_val = None
        indent = "    "
        uses_enum = False

        for i, ln in enumerate(lines):
            em = enum_mgr_pat.match(ln.rstrip("\n\r"))
            if em:
                uses_enum = True
                mgr_line_idx = i
                indent = em.group(1)
                mgr_val = int(em.group(2))
                break

        if not uses_enum:
            mgr_pat = re.compile(
                r"^(#define\s+CONFIG_THREAD_ID_MANAGER\s+)(\d+)([ \t]*)$",
            )
            for i, ln in enumerate(lines):
                m = mgr_pat.match(ln.rstrip("\n\r"))
                if m:
                    mgr_line_idx = i
                    mgr_val = int(m.group(2))
                    new_id_line = f"#define {id_macro:<40} {mgr_val}\n"
                    new_mgr_line = f"{m.group(1)}{mgr_val + 1}{m.group(3)}\n"
                    lines.insert(mgr_line_idx, new_id_line)
                    lines[mgr_line_idx + 1] = new_mgr_line
                    log.append(
                        f"added: {id_macro} = {mgr_val}, CONFIG_THREAD_ID_MANAGER -> {mgr_val + 1}"
                    )
                    break
            else:
                raise ValueError(
                    f"{path}: CONFIG_THREAD_ID_MANAGER not found (enum or #define); cannot insert new ID"
                )
        else:
            if mgr_line_idx is None or mgr_val is None:
                raise ValueError(f"{path}: enum CONFIG_THREAD_ID_MANAGER line parse failed")
            new_id_line = f"{indent}{id_macro} = {mgr_val},\n"
            new_mgr_line = f"{indent}CONFIG_THREAD_ID_MANAGER = {mgr_val + 1},\n"
            lines.insert(mgr_line_idx, new_id_line)
            lines[mgr_line_idx + 1] = new_mgr_line
            log.append(
                f"added: {id_macro} = {mgr_val}, CONFIG_THREAD_ID_MANAGER -> {mgr_val + 1}"
            )

    flat = "".join(lines)

    # ---- 2. Update MUX_NUMBER ----------------------------------------------
    mux_done = False
    for i, ln in enumerate(lines):
        em = enum_mux_pat.match(ln.rstrip("\n\r"))
        if em:
            mux_val = int(em.group(2))
            suf = em.group(3).rstrip()
            if suf and not suf.startswith(" "):
                suf = " " + suf
            lines[i] = f"{em.group(1)}CONFIG_THREAD_ID_MUX_NUMBER = {mux_val + 1},{suf}\n"
            log.append(f"CONFIG_THREAD_ID_MUX_NUMBER: {mux_val} -> {mux_val + 1}")
            mux_done = True
            break
    if not mux_done:
        d_mux = re.compile(r"^#define\s+CONFIG_THREAD_ID_MUX_NUMBER\s+(\d+)([ \t]*)\s*$")
        for i, ln in enumerate(lines):
            m = d_mux.match(ln.rstrip("\n\r"))
            if m:
                mux_val = int(m.group(1))
                lines[i] = f"#define CONFIG_THREAD_ID_MUX_NUMBER {mux_val + 1}{m.group(2)}\n"
                log.append(f"CONFIG_THREAD_ID_MUX_NUMBER: {mux_val} -> {mux_val + 1}")
                mux_done = True
                break
        if not mux_done:
            log.append("warning: CONFIG_THREAD_ID_MUX_NUMBER not found; skip increment")

    _reformat_thread_id_enum(lines)

    flat = "".join(lines)

    # ---- 3. STACK: insert before CONFIG_THREAD_STACK_MANAGER --------------
    if stack_macro in flat:
        log.append(f"skip: already has {stack_macro}")
    else:
        mgr_i = None
        for i, ln in enumerate(lines):
            if re.match(r"^#define\s+CONFIG_THREAD_STACK_MANAGER\b", ln):
                mgr_i = i
                break
        if mgr_i is None:
            raise ValueError(
                f"{path}: CONFIG_THREAD_STACK_MANAGER not found; cannot insert {stack_macro}"
            )
        lines.insert(mgr_i, f"#define {stack_macro} {stack}\n")
        _reformat_stack_block(lines)
        log.append(f"added: {stack_macro} = {stack} (before CONFIG_THREAD_STACK_MANAGER, aligned)")

    flat = "".join(lines)

    # ---- 4. PRIO: insert before CONFIG_THREAD_PRIO_MANAGER ---------------
    if prio_macro in flat:
        log.append(f"skip: already has {prio_macro}")
    else:
        mgr_i = None
        for i, ln in enumerate(lines):
            if re.match(r"^#define\s+CONFIG_THREAD_PRIO_MANAGER\b", ln):
                mgr_i = i
                break
        if mgr_i is None:
            raise ValueError(
                f"{path}: CONFIG_THREAD_PRIO_MANAGER not found; cannot insert {prio_macro}"
            )
        lines.insert(mgr_i, f"#define {prio_macro} {prio}\n")
        _reformat_prio_block(lines)
        log.append(f"added: {prio_macro} = {prio} (before CONFIG_THREAD_PRIO_MANAGER, aligned)")

    path.write_text("".join(lines), encoding="utf-8", newline="\n")
    return log


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Generate thread_xxx.c / thread_xxx.h (RT-Thread) and optionally merge into inc/thread_config.h",
    )
    ap.add_argument(
        "name",
        help="Base name, e.g. thread_sensor, thread_sensor.c, or sensor",
    )
    ap.add_argument(
        "-o",
        "--out-dir",
        type=Path,
        default=Path("."),
        help="Output directory for .c/.h (default: current directory)",
    )
    ap.add_argument(
        "-f",
        "--force",
        action="store_true",
        help="Overwrite existing .c/.h files",
    )
    ap.add_argument(
        "--integrate",
        action="store_true",
        help="Write new task ID / STACK / PRIO macros into thread_config.h",
    )
    ap.add_argument(
        "--thread-config-h",
        type=Path,
        default=None,
        help="Path to thread_config.h (default: <script dir>/../inc/thread_config.h)",
    )
    ap.add_argument(
        "--stack",
        type=int,
        default=DEFAULT_STACK,
        help=f"CONFIG_THREAD_STACK_<SHORT> value in bytes (default {DEFAULT_STACK})",
    )
    ap.add_argument(
        "--prio",
        default=str(DEFAULT_PRIO),
        help=f"CONFIG_THREAD_PRIO_<SHORT> value, number or symbol (default {DEFAULT_PRIO})",
    )
    args = ap.parse_args()

    try:
        stem = normalize_stem(args.name)
    except ValueError as e:
        print(e, file=sys.stderr)
        return 1

    short = short_from_stem(stem)
    up = upper_snake(short)

    out_dir = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    c_path = out_dir / f"{stem}.c"
    h_path = out_dir / f"{stem}.h"

    for p in (c_path, h_path):
        if p.exists() and not args.force:
            print(f"already exists (use -f to overwrite): {p}", file=sys.stderr)
            return 2

    c_path.write_text(render_c(stem, short, up), encoding="utf-8", newline="\n")
    h_path.write_text(render_h(stem, short, up), encoding="utf-8", newline="\n")

    print(f"wrote: {c_path}")
    print(f"wrote: {h_path}")

    if args.integrate:
        tch = args.thread_config_h
        if tch is None:
            tch = Path(__file__).resolve().parent.parent / "inc" / "thread_config.h"
        tch = tch.resolve()
        try:
            if not tch.is_file():
                raise ValueError(f"thread_config.h not found: {tch} (use --thread-config-h)")
            for msg in integrate_thread_config_h(tch, up, args.stack, args.prio):
                print(f"[integrate] {msg}")
        except (ValueError, OSError) as e:
            print(f"[integrate] failed: {e}", file=sys.stderr)
            return 3
    else:
        stack_macro = f"CONFIG_THREAD_STACK_{up}"
        prio_macro  = f"CONFIG_THREAD_PRIO_{up}"
        print()
        print("Not using --integrate. Add manually to inc/thread_config.h:")
        print(f"  Enum: CONFIG_THREAD_ID_{up} = <old MANAGER>, before CONFIG_THREAD_ID_MANAGER (use spaces around =);")
        print("  then bump CONFIG_THREAD_ID_MANAGER and CONFIG_THREAD_ID_MUX_NUMBER by 1; align enum columns.")
        print(f"  #define {stack_macro} <stack>  immediately before CONFIG_THREAD_STACK_MANAGER; align values.")
        print(f"  #define {prio_macro} <prio>   immediately before CONFIG_THREAD_PRIO_MANAGER; align values.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
