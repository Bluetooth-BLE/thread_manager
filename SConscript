# Copyright (c) 2026, John.liu <450547566@qq.com>
#
# SPDX-License-Identifier: Apache-2.0
#
# Change Logs:
# Date           Author     Notes
# 2026-04-07     John       first version


from building import *

cwd = GetCurrentDir()

CPPPATH = [
    os.path.join(cwd, 'inc'),
    os.path.join(cwd, 'samples'),
    os.path.join(cwd, '..', 'event_loop', 'inc'),
]

src = Split('''
    src/thread.c
    src/thread_msg.c
    src/thread_manager.c
''')

if GetDepend(['THREAD_SYSTEM_READY']):
    src += Glob('src/thread_sysready.c')

if GetDepend(['THREAD_MANAGER_USING_SAMPLES']):
    src += Glob('samples/thread_test.c')
    src += Glob('samples/thread_test1.c')
    src += Glob('samples/thread_test2.c')

group = DefineGroup(
    'thread_manager',
    src,
    depend  = ['PKG_USING_THREAD_MANAGER'],
    CPPPATH = CPPPATH,
)

Return('group')
