# `thread_file_gen.py` 使用说明

本文说明 `samples/thread_file_gen.py` 的用途、命令行用法、会改哪些文件，以及生成后工程里还需要做什么才能编译、运行。
代码是在nrf52840目录先测试，所以下面会说到52840

---

## 1. 脚本做什么

根据 **基础名**（如 `sensor` → `thread_sensor`）生成一对模板文件：

| 输出文件 | 内容概要 |
|----------|----------|
| `thread_<short>.c` | RT-Thread 线程入口、邮箱/内存池、`thread_control_t`、`THREAD_CTRL_SLOT_ENTRY`、`THREAD_MSG_REGISTRY_ENTRY`（仅本任务自订阅示例 `EVENT_<UP>_MSG_HELLO`）、`thread_<short>_evt_*` 发送封装等 |
| `thread_<short>.h` | 头文件保护、`EVENT_<UP>_MSG_HELLO` 枚举（默认值为 `0x01`）、对外 API 声明 |

其中 `<short>` 为去掉 `thread_` 后的名字（如 `thread_sensor` → `sensor`），`<UP>` 为大写（如 `SENSOR`）。生成代码风格与 `samples/thread_test.c` 等一致。

可选 **`--integrate`**：自动改写 **`inc/thread_config.h`**，插入新任务的 `CONFIG_THREAD_ID_*`、`CONFIG_THREAD_STACK_*`、`CONFIG_THREAD_PRIO_*`，并调整 `CONFIG_THREAD_ID_MANAGER`、`CONFIG_THREAD_ID_MUX_NUMBER`。

---

## 2. 命令行用法

在仓库中执行（需 Python 3）：

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

**名称规则**（与脚本内 `normalize_stem` 一致）：

- 可写 `sensor`、`thread_sensor`、`thread_sensor.c` / `.h`，最终统一为带 `thread_` 前缀、无扩展名的 **stem**。
- 须为合法 C 标识符：`[a-zA-Z][a-zA-Z0-9_]*`。

**主要参数**：

| 参数 | 含义 |
|------|------|
| `name` | 基础名（位置参数） |
| `-o` / `--out-dir` | `.c` / `.h` 输出目录（默认：当前工作目录 `.`） |
| `-f` / `--force` | 目标已存在时仍覆盖写入 |
| `--integrate` | 修改 `thread_config.h`（见下文） |
| `--thread-config-h` | `thread_config.h` 路径；默认：`<脚本目录>/../inc/thread_config.h` |
| `--stack` | 新任务的 `CONFIG_THREAD_STACK_<UP>`（字节）；**默认 1024**（以脚本内 `DEFAULT_STACK` 为准） |
| `--prio` | 新任务的 `CONFIG_THREAD_PRIO_<UP>`；默认 `25`，也可填符号名（字符串原样写入 `#define`） |

---

## 3. 会修改 / 生成哪些文件

### 3.1 始终会写入（由 `-o` 决定目录）

- `thread_<short>.c`
- `thread_<short>.h`

若未加 `-f` 且任一文件已存在，脚本 **报错退出**（exit code 2），不覆盖。

### 3.2 仅在使用 `--integrate` 时

- **`thread_config.h`**（默认 `packages/thread_manager/inc/thread_config.h`，可由 `--thread-config-h` 指定）

脚本对该文件做的编辑包括（与 `integrate_thread_config_h` 一致）：

1. 在 **`CONFIG_THREAD_ID_MANAGER` 前** 插入 `CONFIG_THREAD_ID_<UP> = <原 MANAGER 数值>`，并把 **`CONFIG_THREAD_ID_MANAGER` 加 1**。
2. **`CONFIG_THREAD_ID_MUX_NUMBER` 加 1**（支持 `enum` 或 `#define` 两种写法）。
3. 重排 enum 内 `CONFIG_THREAD_ID_*` 列对齐。
4. 在 **`CONFIG_THREAD_STACK_MANAGER` 前** 插入 `#define CONFIG_THREAD_STACK_<UP> <stack>`，并与其他 `CONFIG_THREAD_STACK_*` 列对齐。
5. 在 **`CONFIG_THREAD_PRIO_MANAGER` 前** 插入 `#define CONFIG_THREAD_PRIO_<UP> <prio>`，并与其他 `CONFIG_THREAD_PRIO_*` 列对齐。

若某宏已存在（例如重复执行 integrate），对应步骤会 **skip**（日志里会有 `skip: already has ...`）。

---

## 4. 生成之后，用户通常还要做什么

脚本 **不会** 自动把新 `.c` 加进 SCons 工程；**不** 改 `SConscript`、`rtconfig.h`、链接脚本（`thread.ld` 已通过 BSP 的 `link.lds` 引入时，一般无需为 `THREAD_CTRL_SLOT_ENTRY` 再改链接）。

### 4.1 必须把新源文件加入编译（必选其一）

| 做法 | 说明 |
|------|------|
| **继续放在 `packages/thread_manager/samples/`** | 需编辑 **`packages/thread_manager/SConscript`**：在 `THREAD_MANAGER_USING_SAMPLES` 分支里为 **`src += [...]`** 增加一行 `os.path.join(samples_dir, 'thread_xxx.c')`，并保证 `rtconfig.h` 中已定义 **`THREAD_MANAGER_USING_SAMPLES`**。 |
| **自建组件目录 + SConscript** | 按项目规范把 `.c` 注册进 `DefineGroup`。 |

若不做这一步，链接阶段不会出现新线程文件，任务不会创建。

### 4.2 头文件与包含路径

生成的 `.c` 使用 `#include "<stem>.h"`（例如 `thread_sensor.h`）。  
若 `.c` 与 `.h` 与 **`thread.h` 不在同一套 CPPPATH 下**，需在对应 `SConscript` 的 `CPPPATH` 中增加 **`packages/thread_manager/inc`**（以及启用 `event_loop` 时通常已有的路径），否则编译会找不到 `thread.h`。

### 4.3 业务逻辑（几乎总要改）

模板里 `_thread_hardware_init`、`_thread_resource_init`、`_thread_service_init` 等仅为 **LOG 占位**；`_thread_system_ready_init` 默认为空（仅打 log），**没有** 像 `thread_test.c` 那样自动发消息。  
你需要按实际需求填写初始化、消息处理、以及是否在 `_thread_msg_process` 之外增加 **`THREAD_MSG_REGISTRY_ENTRY`**（跨任务订阅要 **#include 其它任务头文件** 并注册 `(subscriber, publisher, msg_id)`，参见 `doc/THREAD_MSG_TOPIC_PUBSUB.md`）。

### 4.4 消息 ID 冲突

每个生成的头文件里都有 **`EVENT_<UP>_MSG_HELLO = 0x01`**。多个任务都用 `0x01` 时，框架靠 **(subscriber, publisher, msg_id)** 区分；为可读性与避免误连，建议在工程内为不同 Topic 分配 **不同数值** 的 `msg_id`。

### 4.5 `thread_config.h` 与 Kconfig

当前 nrf52840 包使用 **静态** `inc/thread_config.h`。若你改为由 Kconfig 生成同名配置，**`--integrate` 直接改文件可能与菜单配置冲突**，此时应关闭 integrate，手工在 Kconfig / 生成规则里增加 ID、栈、优先级。

---

## 5. 退出码（便于脚本调用）

| 码 | 含义 |
|----|------|
| 0 | 成功 |
| 1 | 非法 `name` |
| 2 | 输出文件已存在且未给 `-f` |
| 3 | `--integrate` 失败（找不到 `thread_config.h`、缺少 `CONFIG_THREAD_ID_MANAGER` 等锚点、磁盘错误等） |

---

## 6. 与脚本顶部注释的差异说明

脚本文件开头的英文 docstring 里默认栈曾写为 2048；**实际 argparse 默认栈大小以代码中的 `DEFAULT_STACK`（当前为 1024）为准**。需要 2048 时请显式传入 `--stack 2048`。

---

## 7. 相关文件

| 文件 | 作用 |
|------|------|
| `samples/thread_file_gen.py` | 生成器本体 |
| `inc/thread_config.h` | 任务 ID / 栈 / 优先级；`--integrate` 修改目标 |
| `packages/thread_manager/SConscript` | 框架与可选 samples 源文件列表 |
| `doc/THREAD_MSG_TOPIC_PUBSUB.md` | 消息注册表与跨任务订阅说明 |
