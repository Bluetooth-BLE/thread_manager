# Copyright (c) 2026, John.liu <450547566@qq.com>
#
# SPDX-License-Identifier: Apache-2.0
#
# Change Logs:
# Date           Author     Notes
# 2026-04-07     John       first version


import os
from building import *

cwd     = GetCurrentDir()
src_dir = os.path.join(cwd, 'src')
inc_dir = os.path.join(cwd, 'inc')

src = [
    os.path.join(src_dir, 'thread.c'),
    os.path.join(src_dir, 'thread_msg.c'),
    os.path.join(src_dir, 'thread_manager.c'),
]

if GetDepend('THREAD_SYSTEM_READY'):
    src.append(os.path.join(src_dir, 'thread_sysready.c'))

samples_dir = os.path.join(cwd, 'samples')
if GetDepend('THREAD_MANAGER_USING_SAMPLES'):
    src += [
        os.path.join(samples_dir, 'thread_test.c'),
        os.path.join(samples_dir, 'thread_test1.c'),
        os.path.join(samples_dir, 'thread_test2.c'),
    ]

cpppath = [inc_dir, samples_dir, os.path.join(cwd, '..', 'event_loop', 'inc')]

group = DefineGroup(
    'thread_manager',
    src,
    depend  = ['PKG_USING_THREAD_MANAGER'],
    CPPPATH = cpppath,
)

Return('group')
