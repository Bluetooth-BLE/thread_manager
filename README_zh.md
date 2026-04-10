# Thread Manager（中文说明）

> 英文版：[README.md](./README.md)

## 1. 简介

**Thread Manager** 是面向 RT-Thread 的软件包，用于协调多个应用线程：**管理线程**负责启动/退出同步；每个任务拥有私有 **mailbox（邮箱）** 及可选的 **内存池** 承载消息；**链接期消息注册表**按三元组 **`(subscriber_task_id, publisher_task_id, msg_id)`** 路由回调。支持**交叉订阅**：当发布者任务分发自己发出的消息时，框架可将副本转发给其他订阅者，并保留消息头中的 `head.publisher_task`。

## 2. 功能特性

- **任务注册表** — `THREAD_CTRL_SLOT_ENTRY` 将 `thread_control_t` 放入链接段；`thread_task_lookup()` 在运行时按 ID 解析。
- **创建顺序** — `thread_spawn_all_registered()` 依次调用各任务的 `f_thread_init()`（管理线程槽位除外）。
- **管理线程** — 启动握手（`start_sem` + 事件）、可选的 **system-ready** 广播、退出协调。
- **消息分发** — `THREAD_MSG_REGISTRY_ENTRY(sub, pub, msg_id, cb, active)` 在 ROM；启动时拷贝到 RAM 以便运行时修改 `subscribe_active`；`thread_msg_registry_dispatch_message()` 按 `head.publisher_task` 与 `head.msg_id` 匹配。
- **转发** — 排序后走索引路径约 `O(log n)`；若 RAM 索引建立失败则回退为线性扫描 ROM 表。
- **延迟事件** — `thread_evt_send_delayed_by_task_id()` 使用固定数量的软定时器槽（`THREAD_DELAYED_MAX`）。
- **示例**（可选）— `thread_test` / `thread_test1` / `thread_test2` 演示交叉订阅。

## 3. 目录结构

```
thread_manager/
├── README.md               # 英文说明
├── README_zh.md            # 中文说明（本文件）
├── inc/                    # 对外头文件（thread.h, thread_msg.h, thread_manager.h, thread_sysready.h 等）
│   └── thread_config.h     # 应用配置：线程 ID、栈、优先级（按产品修改）
├── src/
│   ├── thread.c            # 注册表引导、thread_task_lookup、thread_spawn_all_registered
│   ├── thread_msg.c        # 注册表初始化、分发、转发、订阅 API
│   ├── thread_manager.c    # 管理线程、启停同步
│   └── thread_sysready.c   # 可选 system-ready（开启 THREAD_SYSTEM_READY 时）
├── samples/                # 示例任务 + thread_file_gen.py（生成辅助脚本）
└── SConscript              # DefineGroup、CPPPATH
```

## 4. 依赖

- **RT-Thread**：本包使用 mailbox、mempool（或堆回退）、semaphore、event、mutex（延迟事件路径）、软定时器等内核对象。
- **工具链 / 链接器**：需 GNU ld 风格的段符号，段名包括 `thread_ctrl_slot` 与 `thread_msg_reg`（详见下文链接说明）。

## 5. 快速上手

### 5.1 在 menuconfig 中开启

在目标 BSP 工程下：


```
RT-Thread online packages
    system packages --->
        [*] Thread manager (task registry, message dispatch,manager thread) --->
            [*] Publish system-ready event after startup sync
            [*] Build samples (thread_test / thread_test1 / thread_test2)
```

1. 打开 **menuconfig**（或执行 `scons --menuconfig`）。
2. 打开 **`PKG_USING_THREAD_MANAGER`**（菜单名含 *Thread manager* 等描述）。
3. 按需调整子选项：
   - **`THREAD_SYSTEM_READY`** — 启动同步完成后发布 system-ready（默认：开启）。
   - **`THREAD_MANAGER_USING_SAMPLES`** — 是否编译示例线程（默认：开启）。
4. 保存配置，确认 `rtconfig.h` 中出现 `#define PKG_USING_THREAD_MANAGER`。
5. 这个默认会使能PKG_USING_EVENT_LOOP

### 5.2 编译

在 BSP 根目录执行 `scons`（或你常用的 RT-Thread 构建方式）。仅当已定义 `PKG_USING_THREAD_MANAGER` 时本包会参与编译。

### 5.3 链接脚本

若链接报错缺少 `__start_thread_ctrl_slot` / `__stop_thread_ctrl_slot` 或 `__start_thread_msg_reg` / `__stop_thread_msg_reg` 等符号，请将 BSP 中提供的**链接片段**合并进板级链接脚本，例如：
bsp\nrf5x\nrf52840\board\linker_scripts\link.lds

```
    .text :
    {
        ........

        /* section information for initial. */
        . = ALIGN(4);
        __rt_init_start = .;
        KEEP(*(SORT(.rti_fn*)))
        __rt_init_end = .;

        /****************************************************************************************************************/
        /* 🔔🔔🔔🔔🔔🔔thread_manager ROM tables; INCLUDE path is relative to BSP root (scons link cwd) 🔔🔔🔔🔔🔔*/
        INCLUDE packages/thread_manager-latest/thread.ld
        /****************************************************************************************************************/

        . = ALIGN(4);

        PROVIDE(__ctors_start__ = .);
        KEEP (*(SORT(.init_array.*)))
        KEEP (*(.init_array))
        PROVIDE(__ctors_end__ = .);

    } > FLASH
```

### 5.4 应用侧配置

编辑 **`inc/thread_config.h`**：

- `CONFIG_THREAD_ID_*` — 数值型任务 ID 与 `CONFIG_THREAD_ID_MUX_NUMBER`。
- 各任务及管理线程的栈大小、抢占优先级。

每个应用线程通常定义一个 `thread_control_t`（邮箱缓冲、mempool、钩子），并通过 **`THREAD_CTRL_SLOT_ENTRY`** 注册。

### 5.5 thread_xxx 脚本生成thread_file_gen.py工具

[THREAD_FILE_GEN](./doc/THREAD_FILE_GEN.md)


```bash
# 进入脚本所在目录（或任意目录，用绝对路径调用脚本）
cd packages/thread_manager/samples

# 仅生成 thread_sensor.c / thread_sensor.h 到当前目录
python thread_file_gen.py sensor

# 指定输出目录
python thread_file_gen.py thread_sensor -o ./out
python thread_file_gen.py sensor --out-dir ./out

# 覆盖已存在文件
python thread_file_gen.py sensor -o . -f

# 生成 + 合并到 thread_config.h（推荐与包内布局一起用）
python thread_file_gen.py thread_foo --integrate
python thread_file_gen.py thread_bar --integrate --stack 3072 --prio 26

# thread_config.h 不在默认位置时
python thread_file_gen.py sensor --integrate --thread-config-h E:/path/to/thread_config.h
```

## 6 示例动图

![thread manager 示例动图](./doc/test.gif)

## 7. 理论文档

[THREAD_MSG_TOPIC_PUBSUB](./doc/THREAD_MSG_TOPIC_PUBSUB.md)


## 8. 许可协议

Apache License 2.0（详见各源文件 SPDX 头与 `package.json`）。

## 9. 维护者与仓库

John
