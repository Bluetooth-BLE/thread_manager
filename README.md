# Thread Manager

> Chinese version: [README_zh.md](./README_zh.md)

## 1. Introduction

**Thread Manager** is an RT-Thread software package that coordinates multiple application threads: a **manager thread** handles startup/exit synchronization; each task has a private **mailbox** and an optional **memory pool** for messages; a **link-time message registry** routes callbacks by the triple **`(subscriber_task_id, publisher_task_id, msg_id)`**. **Cross-task subscription** is supported: when the publisher task dispatches a message it produced, the framework can forward copies to other subscribers while preserving `head.publisher_task` in the header.

## 2. Features

- **Task registry** — `THREAD_CTRL_SLOT_ENTRY` places `thread_control_t` in a linker section; `thread_task_lookup()` resolves IDs at runtime.
- **Spawn order** — `thread_spawn_all_registered()` calls each task’s `f_thread_init()` in turn (except the manager slot).
- **Manager thread** — Startup handshake (`start_sem` + events), optional **system-ready** broadcast, and exit coordination.
- **Message dispatch** — `THREAD_MSG_REGISTRY_ENTRY(sub, pub, msg_id, cb, active)` lives in ROM; at boot it is copied to RAM so `subscribe_active` can change at runtime; `thread_msg_registry_dispatch_message()` matches `head.publisher_task` and `head.msg_id`.
- **Forwarding** — After sorting, the indexed path is about `O(log n)`; if the RAM index cannot be built, dispatch falls back to a linear scan of the ROM table.
- **Delayed events** — `thread_evt_send_delayed_by_task_id()`.
- **Samples** (optional) — `thread_test` / `thread_test1` / `thread_test2` demonstrate cross-subscription.

## 3. Directory structure

```
thread_manager/
├── README.md               # This file (English)
├── README_zh.md            # Chinese readme
├── doc/                    # THREAD_MSG_TOPIC_PUBSUB.md, THREAD_FILE_GEN.md, test.gif, …
├── inc/                    # Public headers (thread.h, thread_msg.h, thread_manager.h, thread_sysready.h, …)
│   └── thread_config.h     # Product-specific: thread IDs, stacks, priorities
├── src/
│   ├── thread.c            # Registry bootstrap, thread_task_lookup, thread_spawn_all_registered
│   ├── thread_msg.c        # Registry init, dispatch, forward, subscribe APIs
│   ├── thread_manager.c    # Manager thread, startup/exit sync
│   └── thread_sysready.c   # Optional system-ready (when THREAD_SYSTEM_READY is on)
├── samples/                # Sample tasks + thread_file_gen.py (generator helper)
├── thread.ld               # Linker fragment for thread_ctrl_slot / thread_msg_reg (INCLUDE from BSP)
└── SConscript              # DefineGroup, CPPPATH
```

## 4. Dependencies

- **RT-Thread** — The package uses mailbox, mempool (or heap fallback), semaphore, event, mutex (delayed-event path), soft timer, and other kernel objects.
- **Toolchain / linker** — GNU ld-style section symbols, including sections `thread_ctrl_slot` and `thread_msg_reg` (see the linker note below).

## 5. Quick start

### 5.1 Enable in menuconfig

In your target BSP project, use menuconfig roughly as follows:

```
RT-Thread online packages
    system packages --->
        [*] Thread manager (task registry, message dispatch, manager thread) --->
            [*] Publish system-ready event after startup sync
            [*] Build samples (thread_test / thread_test1 / thread_test2)
```

1. Open **menuconfig** (or run `scons --menuconfig`).
2. Enable **`PKG_USING_THREAD_MANAGER`** (menu text may mention *Thread manager*, task registry, etc.).
3. Adjust sub-options as needed:
   - **`THREAD_SYSTEM_READY`** — After startup sync, publish system-ready (default: on).
   - **`THREAD_MANAGER_USING_SAMPLES`** — Build the sample threads (default: on).
4. Save and confirm `rtconfig.h` contains `#define PKG_USING_THREAD_MANAGER`.
5. Enabling this package typically also turns on **`PKG_USING_EVENT_LOOP`** (dependency for delayed events).

### 5.2 Build

From the BSP root, run `scons` (or your usual RT-Thread build). This package is built only when `PKG_USING_THREAD_MANAGER` is defined.

### 5.3 Linker script

If linking fails with undefined `__start_thread_ctrl_slot` / `__stop_thread_ctrl_slot` or `__start_thread_msg_reg` / `__stop_thread_msg_reg`, merge the package’s linker fragment into the board link script. Example (paths as used on **nrf52840** BSP; `INCLUDE` is relative to the BSP root / SCons link working directory):

`bsp/nrf5x/nrf52840/board/linker_scripts/link.lds`

```text
    .text :
    {
        …

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

### 5.4 Application configuration

Edit **`inc/thread_config.h`**:

- `CONFIG_THREAD_ID_*` — Numeric task IDs and `CONFIG_THREAD_ID_MUX_NUMBER`.
- Stack sizes and preempt priorities for each application thread and the manager.

Each application thread usually defines one `thread_control_t` (mailbox buffer, mempool, hooks) and registers it with **`THREAD_CTRL_SLOT_ENTRY`**.

### 5.5 `thread_file_gen.py` — generating `thread_xxx.c` / `.h`

See **[doc/THREAD_FILE_GEN.md](./doc/THREAD_FILE_GEN.md)** for full details (files touched, build integration, `--integrate`).

```bash
# From the script directory (or invoke the script by absolute path)
cd packages/thread_manager/samples

# Generate thread_sensor.c / thread_sensor.h in the current directory
python thread_file_gen.py sensor

# Output directory
python thread_file_gen.py thread_sensor -o ./out
python thread_file_gen.py sensor --out-dir ./out

# Overwrite existing files
python thread_file_gen.py sensor -o . -f

# Generate and merge into thread_config.h (recommended with in-tree layout)
python thread_file_gen.py thread_foo --integrate
python thread_file_gen.py thread_bar --integrate --stack 3072 --prio 26

# Custom thread_config.h path
python thread_file_gen.py sensor --integrate --thread-config-h E:/path/to/thread_config.h
```

## 6. Sample demo (GIF)

![Thread manager sample animation](./doc/test.gif)

## 7. Theory / message routing

**[doc/THREAD_MSG_TOPIC_PUBSUB.md](./doc/THREAD_MSG_TOPIC_PUBSUB.md)** — Topics (`msg_id`), registry entries, dispatch, and forwarding.

## 8. License

Apache License 2.0 (see SPDX headers in source files and `package.json` if present).

## 9. Maintainer and repository

John
