# Thread 消息 Topic（`msg_id`）与 Sub/Pub 机制说明

本文说明 **消息主题（`msg_id`）**、**注册表项（`thread_msg_registry_entry_t`）** 与 **`thread_msg_registry_dispatch_message` 分发 / 自动转发** 的工作原理，并与 `packages/thread_manager/src` 中实现及 `samples/` 下示例一一对应。

**运行环境**：本包基于 **RT-Thread**（`rt_mailbox`、`rt_mempool` / `rt_malloc`、`rt_event` 等）；链路脚本需提供 `thread_msg_reg`、`thread_ctrl_slot` 等段（见 `thread_msg.h` / `thread.h` 注释）。

---

## 1. 三个核心概念

| 概念 | 含义 |
|------|------|
| **Topic** | 用 `uint32_t msg_id` 标识的一类消息（如 `EVENT_TEST_MSG_HELLO`）。同一 `(subscriber, publisher, msg_id)` 在表中可对应一条注册项；同一 `msg_id` 也可被不同任务以不同 `(sub, pub)` 组合订阅。 |
| **Pub（发布）** | 向某任务的 **邮箱** 投递一条 **`thread_msg_t` 指针**；消息头 `head.publisher_task` 标明逻辑上的发布者任务 ID。常用 API：`thread_evt_msg_send_by_task_id` / `thread_evt_send_by_task_id`（见 `thread_msg.c`）。 |
| **Sub（订阅）** | 通过宏 **`THREAD_MSG_REGISTRY_ENTRY`** 在链接段 `thread_msg_reg` 中放入一条 `thread_msg_registry_entry_t`。含义：**当订阅者任务从自己的邮箱取出消息并调用 `thread_msg_registry_dispatch_message(订阅者, 消息)` 时**，若 `msg_id` 与 `head.publisher_task` 与表项匹配，则调用回调；在满足条件时还可 **自动转发** 到其他任务的邮箱。 |

本框架 **不是** 中央广播总线，而是：**每个任务有私有邮箱 + 可选内存池；取到消息后在本任务上下文调用 `thread_msg_registry_dispatch_message`，由注册表（启动时从 Flash 拷到 RAM 并建索引）完成回调与可选跨任务转发。**

---

## 2. 传输层：邮箱、通知与内存

每个 `thread_control_t`（`thread.h`）包含：

- **`msgq`**：`struct rt_mailbox *`，队列元素为 **`rt_ubase_t` 承载的消息块指针**（`thread_msg_t *` 强转为整数投递）。
- **`notify`**：`rt_event`，新消息时框架侧会 **`rt_event_send(&sub->notify, THREAD_NOTIFY_MSG_BIT)`** 唤醒订阅线程（见 `_thread_evt_msg_forward`）。
- **`msg_slab`**：消息块优先从 **内存池** 分配；若 `sizeof(thread_msg_t) + msg_len` 大于 `msg_slab_block_size`，则 **`rt_malloc` 回退路径**（`_thread_evt_msg_malloc`）。

任务侧典型模式（与 `samples/thread_test.c` 相同）：`rt_event_recv` → 循环 `rt_mb_recv` 非阻塞取完邮箱 → `_thread_msg_process` 内 `thread_msg_registry_dispatch_message` → **`thread_msg_evt_free` 归还块**（按块大小选择 `rt_mp_free` 或 `rt_free`）。

---

## 3. 消息头与注册表项

### 3.1 `thread_msg_head_t`（`thread_msg.h`）

```c
typedef struct {
    uint32_t msg_id;
    uint32_t msg_len;
    uint32_t publisher_task;  /* 发布者任务 ID */
} thread_msg_head_t;
```

- **`msg_id`**：Topic，与注册表项的 `msg_id` 一致时参与匹配。
- **`msg_len`**：紧跟头部的 `msg_buf[]` 有效长度。
- **`publisher_task`**：逻辑发布者。`thread_evt_msg_send_by_task_id(task_id, ...)` 内部调用 `_thread_evt_msg_forward(task_id, task_id, ...)`，故 **自发自收时 `publisher_task == 接收任务 ID`**；经转发注入的消息里 **`publisher_task` 为原发布任务**（转发函数第二参数）。

### 3.2 `thread_msg_registry_entry_t`

```c
typedef struct thread_msg_registry_entry {
    THREAD_ID_E           subscriber_task_id;  /* 谁执行 dispatch 时要匹配「订阅者」 */
    THREAD_ID_E           publisher_task_id;   /* 与 head.publisher_task 配对 */
    uint32_t              msg_id;
    thread_msg_callback_t callback;
    rt_atomic_t           subscribe_active;    /* ROM 初值；启动后仅在 RAM 副本上改 */
} thread_msg_registry_entry_t;
```

语义可记为：

> **在任务 `subscriber_task_id` 里分发时，若 `head.msg_id == msg_id` 且 `head.publisher_task == publisher_task_id`，则对匹配区间内的表项调用 `callback`（且 `subscribe_active` 非 0）。**

### 3.3 注册宏 `THREAD_MSG_REGISTRY_ENTRY`

```c
#define THREAD_MSG_REGISTRY_ENTRY(_subscriber, _publisher, _msg_id, _cb, active) \
```

| 参数 | 含义 |
|------|------|
| `_subscriber` | 订阅者任务 ID：应在 **该任务** 的 `*_msg_process` 里调用 `thread_msg_registry_dispatch_message(该任务 ID, msg)`。 |
| `_publisher` | 与 **`head.publisher_task`** 一致时才命中直接回调；**自动转发** 表项里该字段表示「谁发消息时会触发向本 subscriber 转发」。 |
| `_msg_id` | Topic。 |
| `_cb` | `void (*)(const void *p_msg)`。 |
| `active` | ROM 中 **`subscribe_active` 初值**（`true`/`false`）；启动后拷贝到 RAM，可用 `thread_msg_subscribe_set_active` 修改。 |

实现上条目落在段 **`thread_msg_reg`**，链接符号 **`__start_thread_msg_reg` / `__stop_thread_msg_reg`** 界定数组范围；宏用 **`__COUNTER__`** 生成唯一符号名，**无需** 手写 `_sort_name`。

---

## 4. `thread_msg_registry_dispatch_message` 原理（与源码一致）

实现文件：**`thread_msg.c`**。

### 4.1 启动时：ROM → RAM + 索引

**`_thread_msg_registry_init`**（`INIT_COMPONENT_EXPORT`，组件初始化阶段执行，在 `thread.c` 的 `_thread_registry_bootstrap` 之后由链接顺序/初始化级别保证可用）：

1. **`n = __stop_thread_msg_reg - __start_thread_msg_reg`** 统计条目数。
2. **`rt_malloc(n * sizeof(thread_msg_registry_entry_t))` 得到 `s_reg_ram`**，把 ROM 表 **逐字段拷贝** 到 RAM（**`subscribe_active` 可在 RAM 上原子改写**）。
3. 对 **`s_reg_ram` 整块** 按 **`(subscriber_task_id, publisher_task_id, msg_id)`** 做 `qsort`（比较函数 `_thread_reg_cmp_direct`）。
4. 统计 **`subscriber != publisher`** 的条数 `nf`，再分配 **`s_reg_forward`**（`thread_msg_registry_entry_t **`），填入指向 RAM 条目的指针，按 **`(publisher_task_id, msg_id, subscriber_task_id)`** 排序（`_thread_reg_cmp_forward`）。

若 **`s_reg_ram` 分配失败**：`s_reg_index_ready` 保持 `false`，分发走 **`THREAD_MSG_REG_FOREACH` 线性扫描 ROM**（`_thread_msg_registry_dispatch_linear`）。

若 RAM 表成功：末尾 **`s_reg_index_ready = true`**。仅当 **转发表分配失败** 时 `s_reg_forward` 可能为 `NULL`，此时 **索引路径下自动转发不会发生**（直接回调仍可走 RAM+二分），属异常资源情况，应关注启动日志。

### 4.2 运行时：`forwarded[]` 去重

`dispatch_message` 使用 **`bool forwarded[CONFIG_THREAD_ID_MUX_NUMBER]`**，对每个 **目标 subscriber 任务 ID** 最多转发一次，避免重复入队。

### 4.3 索引路径（`s_reg_index_ready == true`）—— 先转发，再回调

对当前消息 `h`：

- **`pub = (THREAD_ID_E)h->head.publisher_task`**
- **`mid = h->head.msg_id`**

**① 自动转发**（仅当 **`subscriber_task == pub`**，即 **当前正在分发的任务就是消息头里的发布者**）：

- 在 **`s_reg_forward`** 上对 **`(pub, mid)`** 做二分，得到区间 **`[f_lo, f_hi)`**。
- 对区间内每条 `re`：`re->subscriber_task_id` 必须 **≠** 当前任务；`subscribe_active` 非 0；目标在 `thread_task_lookup` 中存在；且 **`!forwarded[subscriber]`**。
- 调用 **`_thread_evt_msg_forward(re->subscriber_task_id, pub, mid, payload, len)`**：在 **目标任务的池/堆** 上分配新块，**`head.publisher_task = pub`**，拷贝 payload，**`rt_mb_send` + `rt_event_send(MSG_BIT)`**。

**② 直接回调**：

- 在 **`s_reg_ram`** 上对键 **`(subscriber_task, pub, mid)`** 二分，得 **`[d_lo, d_hi)`**。
- 对区间内每条：`callback != NULL` 且 **`REG_ENTRY_ACTIVE(re)`**（原子读 `subscribe_active`）则 **`re->callback(p_msg)`**，并 **`handled = true`**。

**与线性回退路径的差异**：线性路径在 **`THREAD_MSG_REG_FOREACH`** 中 **按 ROM 顺序** 对每条先尝试转发再尝试回调；索引路径 **固定为先整段转发再整段回调**。对常见「每消息一条回调」场景结果一致；若依赖遍历顺序，应知悉此差别。

### 4.4 线性回退 `_thread_msg_registry_dispatch_linear`

当 **`!s_reg_index_ready`** 时调用。条件与索引路径语义对齐要点：

- **转发**：`it->subscriber_task_id != subscriber_task` 且 **`it->publisher_task_id == subscriber_task`** 且 **`h->head.publisher_task == (uint32_t)subscriber_task`**（即 **本条消息必须是本任务自发自收语义**，发布者字段等于当前任务），且 active、目标有效、未转发过。
- **回调**：`it->subscriber_task_id == subscriber_task` 且 **`it->publisher_task_id == h->head.publisher_task`** 且 callback 非空且 active。

---

## 5. 发送与转发 API（`thread_msg.c` / `thread_msg.h`）

| API | 作用 |
|-----|------|
| `thread_evt_msg_send_by_task_id(task_id, msg_id, pdata, len)` | 等价 `_thread_evt_msg_forward(task_id, task_id, ...)`：**发布者字段 = 接收任务**。 |
| `thread_evt_send_by_task_id(task_id, msg_id)` | 零负载事件。 |
| `thread_evt_send_delayed_by_task_id` | `delay_ms==0` 直接发送；否则经 **`event_loop`** 定时，在 `_thread_delayed_evt_loop_fire` 里调用任务的 **`f_evt_delayed_dispatch`**（samples 里转为再次 `thread_evt_send_by_task_id`）。 |
| `thread_evt_cancel_delayed_by_task_id` | 取消未触发的延迟项。 |
| `thread_msg_evt_free(p_ctrl, p_msg)` | 按控制块里的 `msg_slab_block_size` 选择 **池释放或 `rt_free`**。 |

（若开启 Finsh：`tmsg_send`、`tmsg_send_de`、`tmsg_cal` 为上述行为的 MSH 封装。）

---

## 6. `samples` 目录示例说明（三任务互订 + 自动转发）

以下为 **`thread_test.c` / `thread_test1.c` / `thread_test2.c`** 中的注册与触发关系，对应 **「本地回调 + 以发布者为桥的自动转发」**。

### 6.1 任务 ID 与消息 ID

- 任务：`CONFIG_THREAD_ID_TEST`、`CONFIG_THREAD_ID_TEST1`、`CONFIG_THREAD_ID_TEST2`（见 `thread_config.h` / Kconfig）。
- 消息：`EVENT_TEST_MSG_HELLO`、`EVENT_TEST1_MSG_HELLO`、`EVENT_TEST2_MSG_HELLO`（各任务头文件中枚举）。**示例里三者数值均为 `0x01`**，依赖 **`(subscriber_task_id, publisher_task_id, msg_id)` 三元组** 在注册表中区分不同语义；正式项目中建议为不同 Topic 分配 **互不相同的 `msg_id` 数值**，以免维护时混淆。

### 6.2 `thread_test.c`（`ttest`）

**注册（节选）**：

- `(TEST, TEST, EVENT_TEST_MSG_HELLO)` → 本地处理本任务发出的 `EVENT_TEST_MSG_HELLO`。
- `(TEST, TEST1, EVENT_TEST1_MSG_HELLO)` → 当 **`publisher_task == TEST1`** 且 `msg_id == EVENT_TEST1` 时，在 **TEST** 上下文回调（来自 TEST1 经转发或直接投递）。
- `(TEST, TEST2, EVENT_TEST2_MSG_HELLO)` → 同理监听 TEST2。

**触发**：`_thread_system_ready_init` 中 **`thread_test_evt_send(EVENT_TEST_MSG_HELLO)`**，即向 **TEST 邮箱** 投递一条 **`publisher_task == TEST`** 的消息。  
**TEST** `dispatch` 时满足 **`subscriber_task == pub == TEST`**，转发表中命中 **`(TEST1, TEST, EVENT_TEST_MSG_HELLO)`**、**`(TEST2, TEST, EVENT_TEST_MSG_HELLO)`**（定义在 test1/test2 源文件中），于是 **向 TEST1、TEST2 各转发一份拷贝**（`head.publisher_task` 仍为 **TEST**）。  
**TEST** 本地还命中 `(TEST,TEST,EVENT_TEST_MSG_HELLO)` → `_thread_msg_hello`。

因此日志上可看到：**ttest 发 `EVENT_TEST_MSG_HELLO` 后，ttest / ttest1 / ttest2 均可能打印 hello**；其中 test1/test2 侧 `scope` 为 **cross-task**，`publisher` 显示 **ttest**。

### 6.3 `thread_test1.c`（`ttest1`）

**注册**：

- `(TEST1, TEST1, EVENT_TEST1_MSG_HELLO)` 本地；
- `(TEST1, TEST, EVENT_TEST_MSG_HELLO)`：接收 **TEST 发布的** `EVENT_TEST_MSG_HELLO`（含转发来的）；
- `(TEST1, TEST2, EVENT_TEST2_MSG_HELLO)`：接收 **TEST2 发布的** `EVENT_TEST2`。

**触发**：`_thread_system_ready_init` 中 **`thread_test1_evt_send(EVENT_TEST1_MSG_HELLO)`**。  
TEST1 自发自收 `dispatch` 时 **`pub == TEST1`**，转发区间命中 **`(TEST, TEST1, EVENT_TEST1)`**、**`(TEST2, TEST1, EVENT_TEST1)`**（定义在 test / test2），故 **TEST 与 TEST2** 会收到 **`publisher_task == TEST1`** 的 `EVENT_TEST1` 拷贝。

### 6.4 `thread_test2.c`（`ttest2`）

**注册**：

- `(TEST2, TEST2, EVENT_TEST2_MSG_HELLO)` 本地；
- `(TEST2, TEST, EVENT_TEST_MSG_HELLO)`；
- `(TEST2, TEST1, EVENT_TEST1_MSG_HELLO)`。

**触发**：**`thread_test2_evt_send(EVENT_TEST2_MSG_HELLO)`**，转发至 **`(TEST, TEST2, EVENT_TEST2)`**、**`(TEST1, TEST2, EVENT_TEST2)`** 指向的订阅者。

### 6.5 示例中的统一处理与延迟路径

三个文件均使用 **`_log_msg_with_publisher`**：根据 **`head.publisher_task == THREAD_ID_SELF`** 打印 **local** 或 **cross-task**，并打印 `thread_task_get_name(pub)`。

**延迟发送**：`thread_*_evt_send_delayed` → `thread_evt_send_delayed_by_task_id` → **`EVT_LOOP_PUSH`**；到期后在 **`f_evt_delayed_dispatch`** 里 **`thread_evt_send_by_task_id(SELF, msg_id)`**，再走与普通消息相同的邮箱 + `dispatch` 路径。

---

## 7. `thread_msg_registry_dispatch_event`（与 `dispatch_message` 区分）

**`thread_msg_registry_dispatch_event(subscriber_task, event_id, opaque)`**：

- 在 RAM 表（或 ROM 回退）中查找 **`subscriber_task_id == publisher_task_id == subscriber_task` 且 `msg_id == event_id`** 的项，命中则 **`callback(opaque)`**，**不** 走 `thread_msg_t` 消息头、**不** 做上述自动转发。
- 适用于 **不透明指针 / 管理器内部事件** 等场景；任务间 **`thread_msg_t` 邮箱消息** 以 **`dispatch_message`** 为主。

---

## 8. 运行时使能：`thread_msg_subscribe_set_active`

ROM 段中的表项 **不能卸载**；运行时通过 **RAM 副本上的 `subscribe_active`** 实现「软开关」：

```c
void thread_msg_subscribe_set_active(THREAD_ID_E subscriber, THREAD_ID_E publisher,
                                     uint32_t msg_id, thread_msg_callback_t cb, bool active);
bool thread_msg_subscribe_is_active(THREAD_ID_E subscriber, THREAD_ID_E publisher,
                                    uint32_t msg_id, thread_msg_callback_t cb);
```

- 在 **`(subscriber, publisher, msg_id)`** 的 **equal-range** 内，若 **`cb != NULL`** 则只改 **回调等于 `cb`** 的条目；**`cb == NULL`** 时匹配该三元组下 **所有** 条目（与 **`THREAD_MSG_SUBSCRIBE_DISABLE_ALL` / `ENABLE_ALL`** 宏一致）。
- **`active == false`**：对应项 **`subscribe_active` 置 0** → **不执行回调**，且在转发路径上 **`REG_ENTRY_ACTIVE`** 为假 → **跳过 `_thread_evt_msg_forward`**（与「避免无意义唤醒」一致）。
- 查询 **`thread_msg_subscribe_is_active`**：在区间内若存在匹配项且 **全部为 active** 才返回 `true`（实现见 `thread_msg.c`）。

**注意**：静音/使能粒度是 **「注册表项 + 可选回调指针」**，不依赖额外的堆分配哈希表；并发上与其他 RT-Thread 代码一样，应避免在回调内长时间占用调度相关锁（原子标志本身为 `rt_atomic_t`）。

---

## 9. 索引与复杂度（摘要）

| 符号 / 阶段 | 含义 |
|-------------|------|
| `s_reg_ram` | **连续 RAM 数组**，长度 `n`，已按 `(sub, pub, msg_id)` 排序。 |
| `s_reg_forward` | **指针数组**，仅指向 `subscriber != publisher` 的 RAM 项，按 `(pub, msg_id, sub)` 排序。 |
| 每次 `dispatch_message`（索引路径） | 至多 **两次二分**（直接区间 + 转发区间）+ **区间内遍历**；转发仅当 **`当前任务 ID == head.publisher_task`** 时进行。 |
| 启动 | **\(O(n \log n)\)** 排序 + **`O(n)`** 拷贝；额外 RAM 约为 **`n * sizeof(entry) + n_f * sizeof(指针)`**（`n_f ≤ n`）。 |

---

## 10. 新手 checklist

1. 为每个任务定义 **`THREAD_CTRL_SLOT_ENTRY`** 与 **`thread_control_t`**（邮箱缓冲、池、深度、块大小与 **`sizeof(thread_msg_t)+最大负载`** 对齐）。
2. 在 **消费邮箱的线程** 内：`rt_mb_recv` → **`thread_msg_registry_dispatch_message(本任务 ID, p_msg)`** → **`thread_msg_evt_free`**。
3. 用 **`THREAD_MSG_REGISTRY_ENTRY(sub, pub, msg_id, cb, active)`** 注册；**`msg_id`**。
4. 发送时保证 **`head.msg_id` / `head.msg_len` / `head.publisher_task`** 正确；**自动转发** 要求 **发布者任务** 从邮箱取出 **自发自收语义** 的消息并 `dispatch`（`head.publisher_task` 等于该任务 ID），且存在 **`(subscriber=其它任务, publisher=该任务, msg_id)`** 的表项。
5. 转发失败查日志：`evt_msg_forward` / `evt_msg_malloc` 等 **`LOG_E`**（邮箱满、未注册任务、分配失败等）。
6. 运行时开关订阅：**`thread_msg_subscribe_set_active(..., cb, false/true)`** 或 **`THREAD_MSG_SUBSCRIBE_DISABLE` / `ENABLE`**；同一 `(sub, pub, msg_id)` 多条目时用 **`cb` 区分** 或 **`THREAD_MSG_SUBSCRIBE_DISABLE_ALL`**。

---

## 11. 参考源码位置

| 内容 | 位置 |
|------|------|
| 消息类型、注册宏、订阅 API 声明 | `inc/thread_msg.h` |
| 分发、转发、索引初始化、延迟事件、Finsh 命令 | `src/thread_msg.c` |
| 任务控制块、邮箱/池初始化、`thread_task_lookup` | `src/thread.c` |
| 三任务互发/互转示例 | `samples/thread_test.c`、`thread_test1.c`、`thread_test2.c` |

---

