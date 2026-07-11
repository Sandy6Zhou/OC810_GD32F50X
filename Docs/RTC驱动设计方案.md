# RTC 驱动设计方案

---

## 版本历史

| 版本 | 日期 | 作者 | 变更说明 |
|------|------|------|----------|
| V1.0 | 2026-06-30 | 伍玉蛟 | 初始版本，完成方案设计 |

---

## 1. 概述

### 1.1 模块名称
`rtc_driver` - GD32F505 RTC 驱动模块

### 1.2 硬件基础规格

1. **RTC 内核**：32 位纯秒计数器，无硬件 BCD 年月日寄存器，预分频器最大 20bit
2. **时钟源**：外部 LXTAL 32.768KHz 晶振，VBAT 纽扣电池独立备份供电，VDD 断电内核持续计时
3. **中断源**：秒中断 SECIF、闹钟 ALRMIF、溢出 OVIF，共用单路 RTC_IRQn 全局中断
4. **备份域**：BKP_DATA_0 存储 0xA5A5 魔数标记，区分冷启动（掉电丢失时间）/ 热启动（电池维持时间）

### 1.3 核心功能

- RTC 初始化 / 反初始化，自动区分冷启动 / 热启动
- 备份域解锁、LXTAL 起振等待、RSYNF/LWOFF 硬件时序封装
- Unix 时间戳读写（日历转换由应用层使用 C 标准库 time.h 实现）
- 秒中断、闹钟中断、溢出中断回调支持
- 功能裁剪（宏开关控制）

### 1.4 设计原则

- **严格遵循硬件时序**：完整实现 GD32F50x 用户手册全部强制时序要求
- **驱动/应用分离**：驱动层（rtc_driver）仅提供核心硬件操作 API 和中断回调注册；应用层（my_rtc）创建任务管理中断事件，通过队列解耦 ISR 和业务逻辑
- **异步写/同步读**：写操作通过消息队列异步化（调用者不阻塞），读操作无锁直接调用
- **使用 C 标准库**：时间转换使用 time.h（localtime_r/mktime），不重复造轮子
- **可裁剪**：无用功能通过宏开关完全剥离，节省 Flash/RAM

---

## 2. 硬件强制时序规范

### 2.1 备份域访问前置流程

所有公开 API 内部第一行自动调用 `_drv_bkp_access_prepare()`：
1. 开启 RCU_PMU、RCU_BKPI 外设时钟
2. `pmu_unlock()` 解锁电源保护
3. `pmu_backup_write_enable()` 开启备份域寄存器写权限

### 2.2 读操作同步

读取 32 位计数器前，必须调用 `rtc_register_sync_wait()` 等待 RSYNF 标志，解决 APB/RTC 双时钟域读取跳变问题。GD 库内部已完成 RSYNF 触发+等待流程。

### 2.3 写操作同步

每次写操作前后各调用一次 `rtc_lwoff_wait()`：
1. 等待 LWOFF 置 1，确认上次写操作完成
2. 执行写操作（GD32 库函数 `rtc_counter_set()`/`rtc_prescaler_set()`/`rtc_alarm_config()` 内部已含配置模式 CMF 进出）
3. 再次等待 LWOFF 置 1，确保本次写入生效

所有 RTC 寄存器写操作（PSC/CNT/ALRM/INTEN 等）均遵循此模式，无需手动管理 CMF。

### 2.4 其他约束

- **时钟频率**：RTCCLK（32768Hz）必须比 APB1 PCLK 至少慢 4 倍
- **中断上下文**：ISR 与回调函数内禁止任何阻塞 API、长循环、硬件读写等待；RTC 中断优先级必须低于 `configMAX_SYSCALL_INTERRUPT_PRIORITY`

---

## 3. 模块架构

### 3.1 文件结构
```
project/OC810/code/
├── driver/
│   ├── rtc_driver.h          # RTC 驱动接口定义、配置宏
│   └── rtc_driver.c          # RTC 驱动实现（零FreeRTOS依赖）
└── app/
    ├── my_rtc.h              # RTC 应用层接口定义
    └── my_rtc.c              # RTC 应用层实现（FreeRTOS任务+消息队列）
```

### 3.2 依赖关系
```
rtc_driver
├── gd32f50x_rtc.h        # GD32 RTC 标准库
├── gd32f50x_rcu.h        # GD32 RCU 标准库
├── gd32f50x_pmu.h        # GD32 PMU 标准库
├── gd32f50x_bkp.h        # GD32 BKP 标准库
├── gd32f50x_misc.h       # GD32 NVIC 标准库
└── my_log.h              # 日志模块（可选，宏裁剪）
```

### 3.3 配置宏

```c
/* 芯片移植宏（更换 MCU 仅修改此处） */
#define DRV_RTC_BKP_MARK_REG        BKP_DATA_0
#define DRV_RTC_BKP_MARK_VAL        0xA5A5U
#define DRV_RTC_1HZ_PSC             32767U

/* 功能裁剪开关 */
#define DRV_RTC_LOG_ENABLE          1U
#define DRV_RTC_ASSERT_ENABLE       0U
```

> 超时等待全部委托 GD32 标准库内部处理（`rcu_osci_stab_wait`/`rtc_lwoff_wait`/`rtc_register_sync_wait`），驱动层无需自定义超时宏。

### 3.4 数据结构与错误码

详见 `rtc_driver.h`。关键类型：
- `drv_rtc_clock_src_e`：时钟源枚举（LXTAL / IRC40K / HXTAL_DIV128 / AHB_DIV10）
- `drv_rtc_int_type_e`：中断类型枚举（SECOND / ALARM / OVERFLOW）
- `drv_rtc_config_t`：配置结构体（含时钟源、预分频、中断使能、回调函数）
- 错误码：`DRV_RTC_ERR_OK(0)` / `FAILED(-1)` / `TIMEOUT(-2)` / `INVALID_PARAM(-3)` / `NOT_READY(-4)`

---

## 4. API 列表

| API | 说明 | 关键时序 |
|-----|------|----------|
| `drv_rtc_init(config)` | 初始化，自动区分冷/热启动 | 冷启动：LXTAL→PSC→BKP标记→IRQ |
| `drv_rtc_deinit()` | 反初始化，禁用中断，清除标记，保留时间 | LWOFF |
| `drv_rtc_set_time(ts)` | 设置时间戳 | LWOFF→写→LWOFF |
| `drv_rtc_get_time(&ts)` | 读取时间戳，多任务安全 | RSYNF→读 |
| `drv_rtc_set_alarm(ts)` | 设置闹钟 | LWOFF→写→LWOFF |
| `drv_rtc_disable_alarm()` | 禁用闹钟 | 写→LWOFF |
| `drv_rtc_interrupt_enable(type)` | 使能指定中断 | 写→LWOFF |
| `drv_rtc_interrupt_disable(type)` | 禁用指定中断 | 写→LWOFF |
| `drv_rtc_is_initialized()` | 检查初始化状态（读 BKP_DATA_0） | 无 |
| `drv_rtc_irq_handler()` | 中断处理入口（由 RTC_IRQHandler 调用） | 无 |

> 回调函数在 init 时通过配置结构体传入，不提供单独的注册 API。

---

## 5. 中断处理流程

```
RTC_IRQHandler → drv_rtc_irq_handler()
    ├─ SECIF: 清标志 → 调回调（my_rtc_second_callback → FromISR 发消息）
    ├─ ALRMIF: 清标志 → 调回调（my_rtc_alarm_callback → FromISR 发消息）
    └─ OVIF: 清标志 → 调回调（my_rtc_overflow_callback → FromISR 发消息）

→ my_rtc 任务阻塞收消息 → my_rtc_msg_handler() 分发处理
```

- ISR 内零阻塞、零等待循环
- 所有业务逻辑在 RTC 任务上下文执行

---

## 6. 应用层线程安全设计（my_rtc 实现）

### 6.1 读操作：天然并发安全（无需锁）

`my_rtc_get_timestamp()` / `my_rtc_get_calendar()` 为纯读操作：栈局部变量 + 原子寄存器读取 + 无共享状态修改，多任务可安全并发调用。

### 6.2 写操作：消息队列异步化（替代互斥锁）

```
调用者任务                      RTC 任务
    │                              │
    ├─ my_rtc_set_timestamp()     │
    │   └─ my_msg_send(MSG_RTC)   │
    │      立即返回 0/-1           │
    │                              ├─ my_msg_recv(永久阻塞)
    │                              ├─ my_rtc_msg_handler()
    │                              │   └─ drv_rtc_set_time()
```

- 写操作仅构造消息并发送到 RTC 任务队列，立即返回
- **天然序列化**：队列 FIFO 特性确保写操作有序执行
- 无互斥锁、无资源竞争、无死锁风险

### 6.3 mktime() 线程安全

`mktime()` 非可重入，必须在单一任务上下文调用。实现方案：调用者侧 malloc 分配 `struct tm` 副本 → memcpy → 发送到 RTC 任务队列 → RTC 任务执行 mktime + set_time → MY_FREE 释放。发送失败则调用者侧立即释放，无泄漏。

---

## 7. 关键注意事项

### 7.1 硬件与时序

1. PC14/PC15 32.768K 晶振需匹配 6~12pF 负载电容，否则起振超时
2. VBAT 纽扣电池供电需稳定，否则掉电后丢失时间
3. 禁止跳过 RSYNF 直接读取计数器，数值跳变
4. 连续写寄存器每次等待 LWOFF，防止写丢失
5. `rtc_counter_get()` 内部已处理高低位寄存器读取竞争，无需额外保护

### 7.2 中断

1. ISR/回调内禁止调用 `drv_rtc_get_time/set_time`（含等待循环）
2. 官方 DEMO 在 ISR 内调用 `rtc_lwoff_wait()` 是错误示范，会导致中断阻塞
3. 正确做法：ISR 仅清除标志位、调用回调、发送事件到队列

### 7.3 初始化状态判断

1. `drv_rtc_is_initialized()` 通过读取 BKP_DATA_0 是否等于 0xA5A5 判断
2. 冷启动（掉电后）：返回 false，需设置时间
3. 热启动（VBAT 维持）：返回 true，保留已有时间
4. `drv_rtc_deinit()` 清除标记，下次 init 走冷启动路径
5. VBAT 完全掉电时标记被清空，需重新校准时间

### 7.4 闹钟

1. **硬件单次触发**：触发后不会自动清除闹钟寄存器，需重新设置
2. 循环闹钟：应用层在回调内计算下一次触发时间戳并重新设置

### 7.5 时间边界

1. 32 位 Unix 时间戳上限 2106 年，溢出触发 OVIF 告警
2. 接近 2106 年时，应用层应通过 NTP 授时同步，避免回绕到 1970 年

### 7.6 ARMCLANG 编译注意

ARMCLANG V6 + newlib-nano 下 `localtime_r` 不受特性宏控制，需在 `my_comm.h` 中 `<time.h>` 之后添加：
```c
extern struct tm *localtime_r(const time_t *__restrict, struct tm *__restrict);
```

---

## 8. 资源占用评估

| 层级 | 项目 | 占用 |
|------|------|------|
| 驱动层 | Flash | ~1.2KB |
| 驱动层 | RAM（回调指针 ×3） | ~12Byte |
| 应用层 | 任务栈 | 512 Bytes |
| 应用层 | 消息队列（10 条） | ~160 Bytes |
| 应用层 | struct tm 副本（瞬时） | ~56 Bytes |
| **合计** | **RAM 运行值** | **~728 Bytes** |

---

**文档编写人**：伍玉蛟 (wuyujiao@jimiiot.com)
**审核人**：待定
**影响范围**：RTC 驱动模块
**风险等级**：低（独立模块，不影响现有功能）
