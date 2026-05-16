# my_os 模块设计使用说明

## 1. 模块概述

### 1.1 模块简介

`my_os` 模块是 OC810 项目的 **OS 抽象层（Operating System Abstraction Layer）**，基于 FreeRTOS v10.3.1 实现，提供统一的操作系统接口封装。

### 1.2 设计目标

- **跨平台移植**：隐藏 FreeRTOS 底层 API，便于后续更换 RTOS
- **简化开发**：提供简洁易用的接口，降低 RTOS 使用门槛
- **安全可靠**：完善的参数检查、错误处理和线程安全机制
- **灵活配置**：支持编译时日志开关和级别控制

### 1.3 文件位置

```
project/OC810/code/os_abs/
├── my_os.h    # OS抽象层头文件（接口声明）
└── my_os.c    # OS抽象层实现文件
```

### 1.4 版本信息

- **当前版本**：V1.2
- **更新日期**：2026.05.07
- **作者**：伍玉蛟 (wuyujiao@jimiiot.com)

---

## 2. 功能架构

### 2.1 功能模块

```
my_os 模块
├── 任务管理（Task Management）
│   ├── 创建/删除任务
│   ├── 挂起/恢复任务
│   ├── 任务延时（普通/周期性）
│   └── 获取任务句柄
│
├── 信号量管理（Semaphore Management）
│   ├── 二值信号量
│   ├── 计数信号量
│   ├── 互斥信号量
│   └── 获取/释放（支持任务/中断上下文）
│
├── 临界区保护（Critical Section）
│   ├── 任务上下文临界区
│   └── 中断上下文临界区（可嵌套）
│
├── 消息队列（Message Queue）
│   ├── 创建/删除队列
│   ├── 发送/接收消息（支持任务/中断上下文）
│   └── 查询/清空队列
│
├── 定时器管理（Timer Management）
│   ├── 创建/删除定时器
│   ├── 启动/停止/重置定时器（支持任务/中断上下文）
│   ├── 修改定时器周期（支持任务/中断上下文）
│   └── 周期/单次定时器支持
│
├── 时间服务（Time Service）
│   ├── 获取系统滴答（任务/中断上下文）
│   └── 毫秒到 tick 转换
│
└── 系统功能（System Functions）
    ├── 错误处理（死循环+看门狗）
    ├── 系统复位（可配置延时）
    └── 任务统计初始化
```

### 2.2 日志系统

模块内置灵活的日志配置系统：

```c
// 在 my_os.h 中配置
#ifndef MY_OS_LOG_ENABLE
#define MY_OS_LOG_ENABLE    1   // 1=启用, 0=禁用
#endif

#ifndef MY_OS_LOG_LEVEL
#define MY_OS_LOG_LEVEL     3   // 0=关闭, 1=ERROR, 2=WARNING, 3=DEBUG
#endif
```

**日志宏：**
- `MY_OS_LOGD(fmt, ...)` - DEBUG 级别
- `MY_OS_LOGW(fmt, ...)` - WARNING 级别
- `MY_OS_LOGE(fmt, ...)` - ERROR 级别
- `MY_OS_LOGI(fmt, ...)` - INFO 级别

所有日志自动添加 `[OS]` 前缀，便于识别。

---

## 3. 类型定义

### 3.1 OS抽象层基础类型

```c
typedef BaseType_t    my_base_type_t;         // 基础类型（返回值、状态标志）
typedef UBaseType_t   my_ubase_type_t;        // 无符号基础类型（优先级、计数器）
typedef TickType_t    my_tick_type_t;         // 系统滴答类型（时间计算、延时）
```

### 3.2 任务相关类型

```c
typedef TaskHandle_t    my_task_handle_t;      // 任务句柄
typedef my_ubase_type_t my_task_priority_t;    // 任务优先级（基于 my_ubase_type_t）
typedef void (*my_task_func_t)(void *param);   // 任务函数类型
```

### 3.3 信号量类型

```c
typedef SemaphoreHandle_t my_sem_t;            // 信号量句柄
```

### 3.4 消息相关类型

```c
typedef QueueHandle_t my_msg_queue_t;           // 消息队列句柄

typedef enum {
    MY_MSG_ID_BASE = 0,                         // 消息ID基值
    MY_MSG_ID_TEST,                             // 测试消息

    MY_MSG_ID_MAX                               // 消息ID最大值
} my_msg_id_e;

typedef struct {
    my_msg_id_e msg_id;                         // 消息ID
    void *msg_data;                             // 消息数据指针
    uint32_t msg_len;                           // 消息数据长度（字节）
} my_msg_t;
```

### 3.5 定时器相关类型

```c
typedef enum {
    MY_TIMER_ID_ONE_MINUTE = 0,                 // 1分钟定时器（核心定时器）
    MY_TIMER_ID_TEST,                           // 测试定时器

    MY_TIMER_ID_MAX                             // 定时器ID最大值
} my_timer_id_e;

typedef void* my_timer_handle_t;  // 定时器句柄（透明指针）
typedef void (*my_timer_callback_t)(my_timer_handle_t timer_handle);  // 定时器回调
```

---

## 4. API 接口说明

### 4.1 任务管理接口

#### 4.1.1 my_task_create - 创建任务

```c
int32_t my_task_create(my_task_handle_t *task_handle,
                       const char *task_name,
                       uint32_t stack_size,
                       my_task_func_t task_func,
                       void *param,
                       my_task_priority_t priority);
```

**参数说明：**
- `task_handle`: 任务句柄指针（输出）
- `task_name`: 任务名称（最大 16 字符）
- `stack_size`: 堆栈大小（字，1字=4字节）
- `task_func`: 任务函数指针
- `param`: 任务参数
- `priority`: 任务优先级（0=configMAX_PRIORITIES-1，数值越大优先级越高）

**返回值：**
- `0`: 成功
- `-1`: 失败

**使用示例：**
```c
void my_task(void *param)
{
    while (1) {
        // 任务处理逻辑
        my_task_delay_ms(100);
    }
}

// 创建任务
my_task_handle_t task_handle;
my_task_create(&task_handle, "my_task", 1024, my_task, NULL, 1);
```

---

#### 4.1.2 my_task_delay_until - 周期性延时

```c
void my_task_delay_until(uint32_t period_ms, my_tick_type_t *last_wake_time);
```

**参数说明：**
- `period_ms`: 周期时间（毫秒）
- `last_wake_time`: 上次唤醒时间指针（由调用者维护）

**注意事项：**
- ⚠️ `last_wake_time` 必须由调用者初始化，每个任务使用独立变量
- 用于实现精确周期性任务，避免累积误差

**使用示例：**
```c
// 在任务中维护 last_wake_time 变量
static my_tick_type_t last_wake_time = 0;

void my_task(void *param)
{
    // 初始化
    last_wake_time = my_os_get_tick();

    while (1) {
        do_periodic_work();
        my_task_delay_until(100, &last_wake_time);  // 100ms周期
    }
}
```

---

#### 4.1.3 其他任务接口（宏定义）

```c
// 删除任务
#define my_task_delete(task_handle)

// 挂起任务
#define my_task_suspend(task_handle)

// 恢复任务
#define my_task_resume(task_handle)

// 获取当前任务句柄
#define my_task_get_current()

// 任务延时（毫秒）
#define my_task_delay_ms(delay_ms)
```

---

### 4.2 信号量接口

#### 4.2.1 my_sem_binary_create - 创建二值信号量

```c
my_sem_t my_sem_binary_create(void);
```

**返回值：** 信号量句柄，NULL 表示创建失败

**注意事项：**
- 初始状态为无效（需要先 Give 才能 Take）

**使用示例：**
```c
my_sem_t sem = my_sem_binary_create();
my_sem_give(sem);  // 先释放，使信号量有效
my_sem_take(sem, 1000);  // 获取，超时1000ms
```

---

#### 4.2.2 my_sem_counting_create - 创建计数信号量

```c
my_sem_t my_sem_counting_create(uint32_t max_count, uint32_t init_count);
```

**参数说明：**
- `max_count`: 最大计数值
- `init_count`: 初始计数值

---

#### 4.2.3 my_sem_mutex_create - 创建互斥信号量

```c
my_sem_t my_sem_mutex_create(void);
```

**注意事项：**
- 互斥信号量支持优先级继承，防止优先级翻转

---

#### 4.2.4 信号量操作接口

```c
// 获取信号量（任务上下文）
int32_t my_sem_take(my_sem_t sem, uint32_t timeout_ms);

// 获取信号量（中断上下文）
int32_t my_sem_take_from_isr(my_sem_t sem, my_base_type_t *higher_priority_task_woken);

// 释放信号量（任务上下文）
int32_t my_sem_give(my_sem_t sem);

// 释放信号量（中断上下文）
int32_t my_sem_give_from_isr(my_sem_t sem, my_base_type_t *higher_priority_task_woken);

// 删除信号量
#define my_sem_delete(sem)
```

**参数说明：**
- `timeout_ms`: 超时时间（毫秒）
  - `0`: 立即返回
  - `portMAX_DELAY`: 永久等待
  - 其他值: 等待指定毫秒数

**使用示例：**

任务上下文：
```c
// 获取信号量，超时1000ms
if (my_sem_take(sem, 1000) == 0) {
    // 成功获取
} else {
    // 超时
}
```

中断上下文：
```c
void EXTI_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // 在中断中获取信号量
    if (my_sem_take_from_isr(sem, &xHigherPriorityTaskWoken) == 0) {
        // 成功获取
    }

    // 如果需要，触发上下文切换
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
```

---

### 4.3 临界区保护接口

```c
// 进入临界区（任务上下文）
#define my_critical_enter()

// 退出临界区（任务上下文）
#define my_critical_exit()

// 进入临界区并保存中断状态（可嵌套，支持任务和中断上下文）
// 命名带 _from_isr 是与 FreeRTOS 原生 API 保持一致
#define my_critical_enter_from_isr()

// 退出临界区并恢复中断状态
#define my_critical_exit_from_isr(state)
```

**使用示例：**
```c
// 任务上下文（简单场景）
my_critical_enter();
// 临界区代码
shared_variable++;
my_critical_exit();

// 可嵌套临界区（任务/中断通用）
my_ubase_type_t state = my_critical_enter_from_isr();
// 临界区代码
shared_variable++;
my_critical_exit_from_isr(state);
```

---

### 4.4 消息队列接口

#### 4.4.1 my_msg_queue_create - 创建消息队列

```c
my_msg_queue_t my_msg_queue_create(uint32_t queue_len, uint32_t item_size);
```

**参数说明：**
- `queue_len`: 队列长度（可容纳消息数量）
- `item_size`: 每个消息项的大小（字节）

**注意事项：**
- ⚠️ 队列长度和消息项大小必须根据实际需求选择合适的值

**使用示例：**
```c
// 创建可容纳10条my_msg_t消息的队列
my_msg_queue_t queue = my_msg_queue_create(10, sizeof(my_msg_t));

// 发送消息
my_msg_t msg = {.msg_id = MY_MSG_ID_TEST, .msg_data = NULL, .msg_len = 0};
my_msg_send(queue, &msg, 100);

// 接收消息
my_msg_t recv_msg;
my_msg_recv(queue, &recv_msg, 100);
```

---

#### 4.4.2 消息队列操作接口

```c
// 发送消息（任务上下文）
int32_t my_msg_send(my_msg_queue_t queue, const my_msg_t *msg, uint32_t timeout_ms);

// 发送消息（中断上下文）
int32_t my_msg_send_from_isr(my_msg_queue_t queue, const my_msg_t *msg,
                              my_base_type_t *higher_priority_task_woken);

// 接收消息（任务上下文）
int32_t my_msg_recv(my_msg_queue_t queue, my_msg_t *msg, uint32_t timeout_ms);

// 接收消息（中断上下文）
int32_t my_msg_recv_from_isr(my_msg_queue_t queue, my_msg_t *msg,
                              my_base_type_t *higher_priority_task_woken);

// 查询消息数量
uint32_t my_msg_queue_get_count(my_msg_queue_t queue);

// 清空队列
int32_t my_msg_queue_reset(my_msg_queue_t queue);

// 删除队列
#define my_msg_queue_delete(queue)
```

---

### 4.5 定时器接口

#### 4.5.1 my_timer_create - 创建定时器

```c
int32_t my_timer_create(my_timer_id_e timer_id, my_timer_callback_t callback,
                        uint32_t period_ms);
```

**参数说明：**
- `timer_id`: 定时器ID（在 my_timer_id_e 中定义）
- `callback`: 定时器回调函数
- `period_ms`: 定时器周期（毫秒），0 表示单次定时器

**注意事项：**
- 定时器创建后处于停止状态，需调用 `my_timer_start` 启动
- 每个定时器应使用独立的回调函数，无需在回调中判断 timer_id

**使用示例：**
```c
// 每个定时器使用独立的回调函数
void one_minute_timer_callback(my_timer_handle_t timer_handle)
{
    // 1分钟定时处理（不需要判断timer_id）
}

void test_timer_callback(my_timer_handle_t timer_handle)
{
    // 测试定时处理
}

// 在初始化时创建
my_timer_create(MY_TIMER_ID_ONE_MINUTE, one_minute_timer_callback, 60000);
my_timer_create(MY_TIMER_ID_TEST, test_timer_callback, 1000);

// 需要时启动
my_timer_start(MY_TIMER_ID_ONE_MINUTE, 0);
my_timer_start(MY_TIMER_ID_TEST, 0);
```

---

#### 4.5.2 定时器操作接口

**任务上下文 API：**

```c
// 启动定时器
int32_t my_timer_start(my_timer_id_e timer_id, uint32_t timeout_ms);

// 停止定时器
int32_t my_timer_stop(my_timer_id_e timer_id);

// 删除定时器（⚠️不能在中断中调用）
int32_t my_timer_delete(my_timer_id_e timer_id);

// 查询是否运行中
bool my_timer_is_running(my_timer_id_e timer_id);

// 重置定时器计数
int32_t my_timer_reset(my_timer_id_e timer_id);

// 修改定时器周期
int32_t my_timer_change(my_timer_id_e timer_id, uint32_t new_period_ms);
```

**中断上下文 API（FromISR）：**

```c
// 在中断中启动定时器
int32_t my_timer_start_from_isr(my_timer_id_e timer_id);

// 在中断中停止定时器
int32_t my_timer_stop_from_isr(my_timer_id_e timer_id);

// 在中断中重置定时器
int32_t my_timer_reset_from_isr(my_timer_id_e timer_id);

// 在中断中修改定时器周期
int32_t my_timer_change_from_isr(my_timer_id_e timer_id, uint32_t new_period_ms);
```

**重要说明：**
- ⚠️ `my_timer_delete()` **不能在中断中调用**，因为删除操作需要释放内存，在中断中不安全
- 如需在中断中删除定时器，请设置标志位，在任务中检查并执行删除
- FromISR 系列函数仅用于中断上下文，不能阻塞

---

### 4.6 时间服务接口

```c
// 获取系统滴答计数（任务上下文）
#define my_os_get_tick()

// 获取系统滴答计数（中断上下文）
#define my_os_get_tick_from_isr()

// 毫秒转系统滴答
#define my_ms_to_ticks(ms)
```

**使用示例：**
```c
// 超时检测
uint32_t start = my_os_get_tick();
while (!condition) {
    if ((my_os_get_tick() - start) > my_ms_to_ticks(1000)) {
        return -1;  // 超时
    }
}
```

---

### 4.7 系统功能接口

```c
// 错误处理函数（进入死循环，等待看门狗复位）
void my_error_handler(void);

// 系统复位
void my_system_reset(uint32_t delay_ms);

// 初始化任务信息统计
void my_task_info_init(void);
```

**使用示例：**
```c
// 错误处理
if (critical_error) {
    my_error_handler();  // 进入死循环，看门狗会复位
}

// 系统复位
my_system_reset(100);  // 延时100ms后复位（确保日志输出）
my_system_reset(0);    // 立即复位
```

---

## 5. 使用指南

### 5.1 头文件引用

在使用 my_os 模块的文件中引用：

```c
#include "my_os.h"
```

确保 Keil 项目中已添加 include 路径：
```
..\code\os_abs
```

### 5.2 初始化流程

```c
void system_init(void)
{
    // 1. 初始化日志系统
    my_log_init();

    // 2. 创建信号量
    my_sem_t sem = my_sem_binary_create();
    my_sem_give(sem);

    // 3. 创建消息队列
    my_msg_queue_t queue = my_msg_queue_create(10, sizeof(my_msg_t));

    // 4. 创建定时器
    my_timer_create(MY_TIMER_ID_ONE_MINUTE, timer_callback, 60000, NULL);
    my_timer_start(MY_TIMER_ID_ONE_MINUTE, 0);

    // 5. 创建任务
    my_task_handle_t task_handle;
    my_task_create(&task_handle, "AppTask", 512, app_task, NULL, 2);
}
```

### 5.3 任务上下文 vs 中断上下文

**重要：** 部分接口区分任务上下文和中断上下文，不能混用！

| 接口 | 任务上下文 | 中断上下文 |
|------|-----------|-----------|
| `my_sem_take` | ✅ | ❌ |
| `my_sem_take_from_isr` | ❌ | ✅ |
| `my_sem_give` | ✅ | ❌ |
| `my_sem_give_from_isr` | ❌ | ✅ |
| `my_msg_send` | ✅ | ❌ |
| `my_msg_send_from_isr` | ❌ | ✅ |
| `my_msg_recv` | ✅ | ❌ |
| `my_msg_recv_from_isr` | ❌ | ✅ |
| `my_os_get_tick` | ✅ | ❌ |
| `my_os_get_tick_from_isr` | ❌ | ✅ |

---

## 6. 最佳实践

### 6.1 任务优先级分配建议

```
优先级范围：0 到 (configMAX_PRIORITIES - 1)

0              - 空闲任务（系统保留）
1-2            - 低优先级（日志、监控）
3-4            - 中优先级（业务逻辑）
5-6            - 高优先级（实时控制、通信）
configMAX-1    - 最高优先级（关键中断处理）
```

### 6.2 信号量选择指南

| 场景 | 推荐类型 | 说明 |
|------|---------|------|
| 任务同步 | 二值信号量 | 简单的任务间同步 |
| 资源计数 | 计数信号量 | 管理多个同类资源 |
| 互斥访问 | 互斥信号量 | 保护共享资源，防止优先级翻转 |

### 6.3 消息队列设计建议

```c
// 根据实际需求选择合适的队列长度和消息大小
// 过大会浪费 RAM，过小会丢失消息

// 示例：低频数据（1分钟1次）
my_msg_queue_create(5, sizeof(sensor_data_t));

// 示例：高频数据（100ms1次）
my_msg_queue_create(20, sizeof(event_t));
```

### 6.4 定时器使用注意事项

1. **定时器ID管理**：在 `my_timer_id_e` 中定义所有定时器ID
2. **回调函数执行时间**：定时器回调在 Timer Task 中执行，不应阻塞
3. **周期 vs 单次**：`period_ms > 0` 为周期定时器，`period_ms = 0` 为单次定时器
4. **独立回调函数**：推荐每个定时器使用独立的回调函数，避免在回调中判断 timer_id
5. **中断安全**：
   - 删除定时器（`my_timer_delete`）只能在任务上下文中调用
   - 启动/停止/重置/修改周期操作提供 FromISR 版本供中断使用
   - 如需在中断中删除定时器，使用标志位机制在任务中执行
6. **创建与启停分离**：定时器创建后处于停止状态，需显式调用 `my_timer_start` 启动

---

## 7. 错误处理

### 7.1 返回值检查

所有创建类接口都可能失败，必须检查返回值：

```c
my_sem_t sem = my_sem_binary_create();
if (sem == NULL) {
    MY_OS_LOGE("Failed to create semaphore");
    my_error_handler();
}

int32_t ret = my_task_create(&handle, "Task", 512, task_func, NULL, 2);
if (ret != 0) {
    MY_OS_LOGE("Failed to create task");
    my_error_handler();
}
```

### 7.2 超时处理

```c
// 检查超时返回值
int32_t ret = my_sem_take(sem, 1000);
if (ret != 0) {
    MY_OS_LOGW("Semaphore take timeout");
    // 处理超时逻辑
}
```

---

## 8. 配置选项

### 8.1 日志配置

在 `my_os.h` 中修改：

```c
// 完全禁用日志（节省代码空间）
#define MY_OS_LOG_ENABLE    0

// 只输出错误日志
#define MY_OS_LOG_LEVEL     1

// 输出警告和错误
#define MY_OS_LOG_LEVEL     2

// 输出所有日志（默认）
#define MY_OS_LOG_LEVEL     3
```

### 8.2 定时器数量

在 `my_os.h` 的 `my_timer_id_e` 中定义：

```c
typedef enum {
    MY_TIMER_ID_ONE_MINUTE = 0,
    MY_TIMER_ID_HEARTBEAT,
    MY_TIMER_ID_TIMEOUT,

    MY_TIMER_ID_MAX  // 必须放在最后
} my_timer_id_e;
```

### 8.3 消息ID

在 `my_os.h` 的 `my_msg_id_e` 中定义：

```c
typedef enum {
    MY_MSG_ID_SENSOR_DATA = 0,
    MY_MSG_ID_CONTROL_CMD,
    MY_MSG_ID_STATUS_REPORT,

    MY_MSG_ID_MAX  // 必须放在最后
} my_msg_id_e;
```

---

## 9. 常见问题

### Q1: my_task_delay_until 如何在多个任务中使用？

**A:** 每个任务维护独立的 `last_wake_time` 变量：

```c
void task1(void *param)
{
    static my_tick_type_t last_wake_time = 0;
    last_wake_time = my_os_get_tick();

    while (1) {
        // ...
        my_task_delay_until(100, &last_wake_time);
    }
}

void task2(void *param)
{
    static my_tick_type_t last_wake_time = 0;
    last_wake_time = my_os_get_tick();

    while (1) {
        // ...
        my_task_delay_until(200, &last_wake_time);
    }
}
```

### Q2: 如何在中断中使用消息队列？

**A:** 使用 `_from_isr` 后缀的接口：

```c
void EXTI_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    my_msg_t msg = {.msg_id = MY_MSG_ID_EVENT, .msg_data = NULL};
    my_msg_send_from_isr(queue, &msg, &xHigherPriorityTaskWoken);

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
```

### Q3: 定时器回调中可以做哪些操作？

**A:** 定时器回调在 Timer Task 中执行，可以：
- ✅ 发送消息队列
- ✅ 释放信号量
- ✅ 修改变量
- ❌ 调用延时函数（如 `my_task_delay_ms`）
- ❌ 执行阻塞操作

---

## 10. 移植指南

### 10.1 移植到其他 RTOS

如果需要更换 RTOS，只需修改 `my_os.h` 和 `my_os.c` 的实现：

1. 修改类型定义（替换 FreeRTOS 类型）
2. 修改宏定义（替换为新的 RTOS API）
3. 修改函数实现（适配新的 RTOS）

上层业务代码无需修改。

### 10.2 日志系统适配

如果不使用 `my_log` 系统，可以重新定义日志宏：

```c
// 在 my_os.h 中修改
#define MY_OS_LOGD(fmt, ...)   printf("[OS-D] " fmt "\n", ##__VA_ARGS__)
#define MY_OS_LOGW(fmt, ...)   printf("[OS-W] " fmt "\n", ##__VA_ARGS__)
#define MY_OS_LOGE(fmt, ...)   printf("[OS-E] " fmt "\n", ##__VA_ARGS__)
#define MY_OS_LOGI(fmt, ...)   printf("[OS-I] " fmt "\n", ##__VA_ARGS__)
```

---

## 11. 版本历史

| 版本 | 日期 | 变更说明 |
|------|------|----------|
| V1.2 | 2026.05.07 | 封装FreeRTOS基础类型为my_前缀类型（my_base_type_t/my_ubase_type_t/my_tick_type_t）；更新临界区宏命名 |
| V1.1 | 2026.05.07 | 添加my_sem_take_from_isr中断安全API；完善定时器中断安全API；修正回调示例 |
| V1.0 | 2026.05.06 | 初始版本，创建 os_abs 目录，迁移 my_common 为 my_os |

---

## 12. 联系信息

**作者：** 伍玉蛟
**邮箱：** wuyujiao@jimiiot.com
**公司：** 深圳市几米物联有限公司
**项目：** OC810_GD32F50X
