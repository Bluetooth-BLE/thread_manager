/*
 * Copyright (c) 2026, John.liu <450547566@qq.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author     Notes
 * 2026-04-07     John       first version
 *
 * Linker fragment: collect thread manager ROM tables.
 *
 * thread_ctrl_slot and thread_msg_reg sections are collected and
 * bracketed with __start_* / __stop_* symbols for iteration at runtime.
 *
 *   __start_thread_ctrl_slot / __stop_thread_ctrl_slot
 *   __start_thread_msg_reg  / __stop_thread_msg_reg
 *
 * GCC: from board/linker_scripts/link.lds (inside .text), path relative to BSP root / link cwd:
 *   INCLUDE packages/thread_manager/thread.ld
 *   (STM32 BSP and nRF52840 BSP use the same fragment path.)
 *
 * Keil/IAR: no INCLUDE; merge equivalent KEEP blocks into the scatter file by hand.
 */
        . = ALIGN(4);
__start_thread_ctrl_slot = .;
KEEP(*(thread_ctrl_slot))
__stop_thread_ctrl_slot = .;

        . = ALIGN(4);
__start_thread_msg_reg = .;
KEEP(*(thread_msg_reg))
__stop_thread_msg_reg = .;
