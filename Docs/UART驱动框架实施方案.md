# UART驱动框架实施方案

## 版本历史

| 版本 | 日期 | 作者 | 变更说明 |
|------|------|------|----------|
| V1.0 | 2026.04.15 | 伍玉蛟 | 初始版本，完成方案设计 |
| V1.1 | 2026.04.22 | 伍玉蛟 | 增加断言机制、日志系统规范、GD32F50X库特性说明 |
| V1.2 | 2026.05.07 | 伍玉蛟 | 增加DMA异步发送模式、多端口竞态条件防护 |
| V1.3 | 2026.05.12 | 伍玉蛟 | 重构为运行时TX模式配置、API标准化、全面优化注释 |
| V1.4 | 2026.05.13 | 伍玉蛟 | 增加TXE中断发送模式（UART_TX_MODE_INTERRUPT），无需DMA通道 |
| V1.5 | 2026.05.20 | 伍玉蛟 | 增加GPIO配置宏表、NO_USE选项、编译期/运行时双重检查 |
| V1.6 | 2026.06.20 | 伍玉蛟 | RingBuffer更名为my_rb_t，SPSC无锁设计，目录结构精简 |
| V1.7 | 2026.07.15 | 伍玉蛟 | DMA回调携带channel_id精准定位端口，移除全局状态变量，并发安全重构 |

---

## 1. 概述

### 1.1 功能特性

基于GD32F505VGT7 + FreeRTOS的工业级UART驱动，核心特性：

- **多端口独立管理**：5个UART端口（USART0/1/2, UART3/4）独立配置和控制
- **5种TX发送模式**（运行时可配置）：
  - `UART_TX_MODE_POLLING`: 轮询发送（CPU阻塞）
  - `UART_TX_MODE_INTERRUPT`: 中断发送（TXE中断，无需DMA通道）
  - `UART_TX_MODE_DMA_SYNC`: DMA同步发送（查询硬件标志）
  - `UART_TX_MODE_DMA_ASYNC`: DMA异步发送（FreeRTOS信号量，推荐）
  - `UART_TX_MODE_DMA_DUAL_BUF`: DMA双缓冲循环发送（大数据流）
- **灵活的RX接收**：DMA + IDLE空闲中断 + RingBuffer，零CPU干预
- **低功耗支持**：唤醒串口（保持RX）/ 普通串口（彻底关闭）
- **线程安全**：FreeRTOS互斥锁保护，支持多任务并发
- **完全解耦**：驱动层与应用层分离，所有内存由应用层管理

### 1.2 硬件资源

#### 1.2.1 UART端口资源

| 端口 | TX引脚 | RX引脚 | DMA支持 | 典型应用 |
|------|--------|--------|---------|----------|
| USART0 | PA9 | PA10 | ✅ | RS232 |
| USART1 | PA2 | PA3 | ✅ | NT98332 |
| USART2 | PB10 | PB11 | ✅ | 4G Module |
| UART3 | PC10 | PC11 | ✅ | GNSS |
| UART4 | PC12 | PD2 | ❌ | RS485 |

**注意**：UART4 无 DMA 支持，可使用轮询发送（`UART_TX_MODE_POLLING`）或中断发送（`UART_TX_MODE_INTERRUPT`）。

#### 1.2.2 GPIO配置宏表（V1.5新增）

**特性**：
- 集中管理所有UART的GPIO引脚配置
- 编译期选择，零运行时开销
- 支持NO_USE选项，未使用的UART可节省代码空间
- 配置错误在编译期捕获（#error）
- 运行时双重检查，防止应用层错误初始化

**USART0 GPIO选项**：

| 宏定义 | 值 | 引脚 | AF编号 | 说明 |
|--------|-----|------|--------|------|
| `DRV_USART0_NO_USE` | 0U | - | - | 未使用（节省代码空间） |
| `DRV_USART0_GPIO_PA9_PA10` | 1U | PA9(TX), PA10(RX) | AF0 | 默认引脚 |
| `DRV_USART0_GPIO_PB6_PB7` | 2U | PB6(TX), PB7(RX) | AF0 | 复用引脚 |

**USART1 GPIO选项**：

| 宏定义 | 值 | 引脚 | AF编号 | 说明 |
|--------|-----|------|--------|------|
| `DRV_USART1_NO_USE` | 0U | - | - | 未使用（节省代码空间） |
| `DRV_USART1_GPIO_PA2_PA3` | 1U | PA2(TX), PA3(RX) | AF0 | 默认引脚 |
| `DRV_USART1_GPIO_PD5_PD6` | 2U | PD5(TX), PD6(RX) | AF0 | 复用引脚 |

**UART2 GPIO选项**：

| 宏定义 | 值 | 引脚 | AF编号 | 说明 |
|--------|-----|------|--------|------|
| `DRV_UART2_NO_USE` | 0U | - | - | 未使用（节省代码空间） |
| `DRV_UART2_GPIO_PB10_PB11` | 1U | PB10(TX), PB11(RX) | AF1 | 默认引脚 |
| `DRV_UART2_GPIO_PC10_PC11` | 2U | PC10(TX), PC11(RX) | AF0 | 复用引脚1 |
| `DRV_UART2_GPIO_PD8_PD9` | 3U | PD8(TX), PD9(RX) | AF0 | 复用引脚2 |

**UART3 GPIO选项**：

| 宏定义 | 值 | 引脚 | AF编号 | 说明 |
|--------|-----|------|--------|------|
| `DRV_UART3_NO_USE` | 0U | - | - | 未使用（节省代码空间） |
| `DRV_UART3_GPIO_PC10_PC11` | 1U | PC10(TX), PC11(RX) | AF1 | 默认引脚 |

**UART4 GPIO选项**：

| 宏定义 | 值 | 引脚 | AF编号 | 说明 |
|--------|-----|------|--------|------|
| `DRV_UART4_NO_USE` | 0U | - | - | 未使用（节省代码空间） |
| `DRV_UART4_GPIO_PC12_PD2` | 1U | PC12(TX), PD2(RX) | AF1 | 默认引脚 |

---

## 2. 快速上手

### 2.1 目录结构

```
project/OC810/code/
├── driver/
│   ├── uart_driver.c      # 驱动实现
│   └── uart_driver.h      # 接口定义（含GPIO配置宏表）
├── utility/
│   ├── my_rb.c/h          # 环形缓冲区（SPSC无锁设计）
│   └── my_tq.c/h          # 异步发送队列（动态内存）
├── log/
│   └── my_log.c/h         # 日志系统
└── app/
    ├── main_uart_test.c   # 单元测试
    └── main_uart_all_test.c  # 全端口测试
```

### 2.2 GPIO配置（V1.5新增）

**在 `uart_driver.h` 中配置UART使用的GPIO引脚**：

```c
/* 用户配置区：选择每个UART使用的GPIO引脚 */

/* USART0: 使用PA9/PA10 */
#define DRV_USART0_GPIO_SEL    DRV_USART0_GPIO_PA9_PA10

/* USART1: 未使用（节省代码空间） */
#define DRV_USART1_GPIO_SEL    DRV_USART1_NO_USE

/* UART2: 使用PC10/PC11 */
#define DRV_UART2_GPIO_SEL     DRV_UART2_GPIO_PC10_PC11

/* UART3: 使用PC10/PC11 */
#define DRV_UART3_GPIO_SEL     DRV_UART3_GPIO_PC10_PC11

/* UART4: 使用PC12/PD2 */
#define DRV_UART4_GPIO_SEL     DRV_UART4_GPIO_PC12_PD2
```

**注意事项**：
- 修改宏定义后无需修改驱动代码，重新编译即可
- 设置为 `NO_USE` 的UART，在 `drv_uart_init()` 时会返回错误
- 配置错误会在编译期报错（`#error`）

### 2.4 错误处理示例（V1.5新增）

```c
// 尝试初始化未配置的UART
#define DRV_USART1_GPIO_SEL    DRV_USART1_NO_USE  // USART1未使用

drv_uart_config_t config = {
    .port = DRV_UART_PORT_USART1,
    .baudrate = 115200,
    .rx_buf = rx_buf,
    .rx_buf_size = sizeof(rx_buf),
    // ...
};

int ret = drv_uart_init(&config);
if (ret != DRV_UART_ERR_OK) {
    // 返回 DRV_UART_ERR_FAILED
    // 日志: [UART ERROR] USART1 is not configured (NO_USE), please check DRV_USART1_GPIO_SEL
}
```

### 2.3 最小使用示例

```c
#include "uart_driver.h"
#include "my_rb.h"

// 1. 应用层分配内存（必须为全局或静态变量）
static uint8_t rx_buf[256];
static uint8_t dma_rx_buf[256];
static uint8_t ringbuf_data[512];
static my_rb_t ringbuf;

// 2. 接收回调（中断上下文，必须快速执行）
static void rx_callback(drv_uart_port_e port, uint16_t len)
{
    // 从RingBuffer读取数据
    uint8_t data[128];
    drv_uart_read(port, data, len);
    // 处理数据...
}

// 3. 初始化UART
void uart_init_example(void)
{
    // 初始化RingBuffer
    ringbuf_init(&ringbuf, ringbuf_data, sizeof(ringbuf_data));

    // 配置UART
    drv_uart_config_t config = {
        .port = DRV_UART_PORT_USART0,
        .baudrate = 115200,
        .rx_buf = rx_buf,
        .rx_buf_size = sizeof(rx_buf),
        .dma_rx_buf = dma_rx_buf,
        .dma_rx_buf_size = sizeof(dma_rx_buf),
        .ringbuf = &ringbuf,
        .use_dma_rx = true,           // 启用DMA接收
        .use_idle = true,             // 启用IDLE中断
        .use_ringbuf = true,          // 启用RingBuffer
        .use_dma_tx = true,           // 启用DMA发送
        .tx_mode = UART_TX_MODE_DMA_ASYNC,  // 异步发送模式（推荐）
        .use_tx_mutex = true,         // 启用发送互斥锁
        .is_wakeup_capable = false,   // 普通串口
        .rx_callback = rx_callback,
        .error_callback = NULL
    };

    // 注册UART
    drv_uart_init(&config);
}

// 4. 发送数据
void uart_send_example(void)
{
    uint8_t data[] = "Hello UART!";
    drv_uart_send(DRV_UART_PORT_USART0, data, sizeof(data) - 1);
}
```

---

## 3. 核心API

### 3.1 初始化与反初始化

```c
// 初始化UART端口
int drv_uart_init(const drv_uart_config_t *config);

// 反初始化UART端口
int drv_uart_deinit(drv_uart_port_e port);
```

**使用注意**：
- `drv_uart_init()` 和 `drv_uart_deinit()` 成对使用
- 所有内存资源由应用层分配和管理
- 重复初始化返回错误（-1）

### 3.2 数据收发

```c
// 发送数据（根据tx_mode自动选择发送方式）
// 返回值：实际发送的字节数，-1表示失败
int drv_uart_send(drv_uart_port_e port, const uint8_t *data, uint16_t len);

// 读取数据（从RingBuffer或接收缓冲区）
// 返回值：实际读取的字节数，-1表示失败
int drv_uart_read(drv_uart_port_e port, uint8_t *data, uint16_t len);

// 查询待读取数据量
// 返回值：可读取的字节数，-1表示失败
int drv_uart_get_rx_len(drv_uart_port_e port);
```

### 3.3 电源管理

```c
// 挂起UART（低功耗）
// 返回值：0=成功，-1=失败
int drv_uart_suspend(drv_uart_port_e port);

// 恢复UART
// 返回值：0=成功，-1=失败
int drv_uart_resume(drv_uart_port_e port);

// 关闭UART（彻底关闭硬件）
// 返回值：0=成功，-1=失败
int drv_uart_shutdown(drv_uart_port_e port);

// 查询端口状态
// 返回值：状态枚举值，-1=失败
int drv_uart_get_state(drv_uart_port_e port);
```

**状态机**：
```
UNINIT → INIT → ACTIVE ←→ SUSPENDED
                      ↓
                   SHUTDOWN →（可通过resume恢复）
```

**注意**：
- `SHUTDOWN` 状态可通过 `drv_uart_resume()` 恢复到 `ACTIVE`
- `SHUTDOWN` 不释放控制实例，可通过 `drv_uart_deinit()` 彻底卸载

### 3.4 中断处理

```c
// 统一中断入口（由gd32f50x_it.c调用，应用层不应直接调用）
void drv_uart_irq_handler(drv_uart_port_e port);
```

**示例**（gd32f50x_it.c）：
```c
void USART0_IRQHandler(void)
{
    drv_uart_irq_handler(DRV_UART_PORT_USART0);
}
```

---

## 4. TX发送模式详解

### 4.1 模式对比

| 模式 | CPU占用 | 适用场景 | 资源需求 |
|------|---------|----------|----------|
| POLLING | 阻塞等待 | 短数据、低速率 | 无 |
| INTERRUPT | 不阻塞 | 中低频、无需DMA | TX缓冲区 |
| DMA_SYNC | 查询等待 | 中速率 | DMA通道 |
| DMA_ASYNC | 任务挂起 | 高速率、多任务（推荐） | DMA通道 + 信号量 |
| DMA_DUAL_BUF | 零阻塞 | 连续大数据流 | DMA通道 + 双缓冲 + 环形队列 |

### 4.2 模式配置示例

```c
drv_uart_config_t config;

// 模式1：轮询发送（最简单）
config.tx_mode = UART_TX_MODE_POLLING;
config.use_dma_tx = false;

// 模式2：中断发送（无需DMA通道）
config.tx_mode = UART_TX_MODE_INTERRUPT;
config.use_dma_tx = false;
config.tx_buf = tx_buf;              // TX发送缓冲区
config.tx_buf_size = 256;
config.tx_callback = tx_callback;    // 发送完成回调（可选）

// 模式3：DMA同步发送
config.tx_mode = UART_TX_MODE_DMA_SYNC;
config.use_dma_tx = true;

// 模式4：DMA异步发送（推荐）
config.tx_mode = UART_TX_MODE_DMA_ASYNC;
config.use_dma_tx = true;
config.use_tx_mutex = true;  // 多任务时必须启用

// 模式5：DMA双缓冲循环发送
config.tx_mode = UART_TX_MODE_DMA_DUAL_BUF;
config.use_dma_tx = true;
config.dma_tx_buf = tx_buf;           // [2][256]双缓冲区
config.dma_tx_buf_size = 256;
config.tx_ring_queue = ring_queue;    // 发送环形队列
config.tx_ring_queue_size = 1024;
```

### 4.3 多端口混合配置

```c
// 全端口测试示例：每个端口使用不同TX模式
// 注意：以下为伪代码，实际需使用 drv_uart_init() 函数
drv_uart_init(...);  // USART0: TX_MODE_DMA_DUAL_BUF
drv_uart_init(...);  // USART1: TX_MODE_DMA_ASYNC
drv_uart_init(...);  // USART2: TX_MODE_INTERRUPT (无需DMA)
drv_uart_init(...);  // UART3:  TX_MODE_DMA_SYNC
drv_uart_init(...);  // UART4:  TX_MODE_POLLING（无DMA）
```

---

## 5. 配置结构体详解

### 5.1 drv_uart_config_t

```c
typedef struct {
    // 【必选】基础配置
    drv_uart_port_e   port;             // UART端口号
    uint32_t          baudrate;         // 波特率

    // 【必选】接收缓冲区（始终需要）
    uint8_t           *rx_buf;          // 基础接收缓冲区指针
    uint16_t          rx_buf_size;      // 基础接收缓冲区大小

    // 【可选】DMA接收（use_dma_rx=true时需要）
    uint8_t           *dma_rx_buf;      // DMA接收缓冲区指针
    uint16_t          dma_rx_buf_size;  // DMA接收缓冲区大小

    // 【可选】RingBuffer（use_ringbuf=true时需要）
    my_rb_t           *ringbuf;         // RingBuffer指针（需提前初始化）

    // 【可选】功能开关
    bool              use_dma_rx;       // 启用DMA接收
    bool              use_idle;         // 启用IDLE空闲中断
    bool              use_ringbuf;      // 启用RingBuffer
    bool              use_dma_tx;       // 启用DMA发送
    drv_uart_tx_mode_e tx_mode;         // TX发送模式（运行时配置）
    bool              use_tx_mutex;     // 启用发送互斥锁
    bool              is_wakeup_capable;// 唤醒串口标志

    // 【可选】DMA TX双缓冲（tx_mode=DMA_DUAL_BUF时需要）
    uint8_t           *dma_tx_buf;      // 双缓冲区指针 [2][buf_size]
    uint16_t          dma_tx_buf_size;  // 单个缓冲区大小
    uint8_t           *tx_ring_queue;   // 发送环形队列指针
    uint16_t          tx_ring_queue_size; // 环形队列大小

    // 【可选】中断发送（tx_mode=INTERRUPT时需要）
    uint8_t           *tx_buf;          // TX发送缓冲区指针
    uint16_t          tx_buf_size;      // TX发送缓冲区大小

    // 【可选】回调函数
    void (*rx_callback)(drv_uart_port_e port, uint16_t len);
    void (*error_callback)(drv_uart_port_e port, drv_uart_error_e err);
    void (*tx_callback)(drv_uart_port_e port, uint16_t len);  // 仅中断发送模式使用
} drv_uart_config_t;
```

### 5.2 参数依赖关系

| 开关 | 需要传入的参数 | 说明 |
|------|----------------|------|
| use_dma_rx=true | dma_rx_buf, dma_rx_buf_size | DMA接收缓冲区 |
| use_ringbuf=true | ringbuf（已初始化） | RingBuffer指针 |
| use_dma_tx=true | 无 | 自动启用DMA发送 |
| tx_mode=DMA_DUAL_BUF | dma_tx_buf, dma_tx_buf_size, tx_ring_queue, tx_ring_queue_size | 双缓冲相关 |
| tx_mode=INTERRUPT | tx_buf, tx_buf_size, tx_callback（可选） | 中断发送相关 |

---

## 6. 枚举类型

### 6.1 端口枚举

```c
typedef enum {
    DRV_UART_PORT_USART0 = 0,
    DRV_UART_PORT_USART1,
    DRV_UART_PORT_USART2,
    DRV_UART_PORT_UART3,
    DRV_UART_PORT_UART4,
    DRV_UART_PORT_MAX
} drv_uart_port_e;
```

### 6.2 状态枚举

```c
typedef enum {
    DRV_UART_STATE_UNINIT = 0,      // 未初始化
    DRV_UART_STATE_INIT,            // 已初始化
    DRV_UART_STATE_ACTIVE,          // 活跃状态
    DRV_UART_STATE_SUSPENDED,       // 挂起状态
    DRV_UART_STATE_SHUTDOWN         // 关闭状态
} drv_uart_state_e;
```

### 6.3 TX模式枚举

```c
typedef enum {
    UART_TX_MODE_POLLING = 0,       // 轮询发送
    UART_TX_MODE_INTERRUPT,         // 中断发送（TXE中断，无需DMA通道）
    UART_TX_MODE_DMA_SYNC,          // DMA同步发送
    UART_TX_MODE_DMA_ASYNC,         // DMA异步发送（推荐）
    UART_TX_MODE_DMA_DUAL_BUF       // DMA双缓冲循环发送
} drv_uart_tx_mode_e;
```

### 6.4 错误枚举

```c
typedef enum {
    DRV_UART_ERROR_NONE = 0,        // 无错误
    DRV_UART_ERROR_OVERRUN,         // 数据溢出
    DRV_UART_ERROR_FRAME,           // 帧错误
    DRV_UART_ERROR_PARITY,          // 奇偶校验错误
    DRV_UART_ERROR_NOISE,           // 噪声错误
    DRV_UART_ERROR_DMA,             // DMA错误
    DRV_UART_ERROR_TIMEOUT          // 超时错误
} drv_uart_error_e;
```

---

## 7. 日志与断言

### 7.1 日志系统

```c
// 日志开关（uart_driver.h）
#define DRV_UART_LOG_ENABLE      (1U)  // 1=开启，0=关闭
#define DRV_UART_LOG_CURRENT_LEVEL  (DRV_UART_LOG_LEVEL_INFO)  // 当前日志级别

// 日志级别
#define DRV_UART_LOG_LEVEL_ERROR   (0U)  // 错误日志
#define DRV_UART_LOG_LEVEL_WARN    (1U)  // 警告日志
#define DRV_UART_LOG_LEVEL_INFO    (2U)  // 信息日志
#define DRV_UART_LOG_LEVEL_DEBUG   (3U)  // 调试日志

// 使用示例
DRV_UART_LOGI("UART port %d initialized, TX_Mode=%d", port, config->tx_mode);
DRV_UART_LOGE("Failed to create TX mutex");
```

### 7.2 断言机制

```c
// 断言开关（uart_driver.h）
#ifndef DRV_UART_ASSERT_ENABLE
#define DRV_UART_ASSERT_ENABLE   (0U)  // 1=启用（开发阶段），0=禁用（发布阶段）
#endif

// 使用示例
DRV_UART_ASSERT(config != NULL);
DRV_UART_ASSERT(port < DRV_UART_PORT_MAX);
```

---

## 8. 低功耗设计

### 8.1 唤醒串口 vs 普通串口

| 类型 | 挂起行为 | 恢复行为 | 适用场景 |
|------|----------|----------|----------|
| 唤醒串口 | 仅关闭TX，保持RX | 恢复TX | STOP模式唤醒 |
| 普通串口 | 关闭全部硬件 | 重新初始化 | 降低功耗 |

### 8.2 使用示例

```c
// 配置唤醒串口
config.is_wakeup_capable = true;
drv_uart_init(&config);

// 进入低功耗前
drv_uart_suspend(DRV_UART_PORT_USART0);  // 唤醒串口：保持RX
drv_uart_suspend(DRV_UART_PORT_UART3);   // 普通串口：彻底关闭

// 唤醒后恢复
drv_uart_resume(DRV_UART_PORT_USART0);
drv_uart_resume(DRV_UART_PORT_UART3);
```

---

## 9. 线程安全

### 9.1 互斥锁机制

```c
// 发送互斥锁（每个端口独立）
config.use_tx_mutex = true;  // 多任务时必须启用

// 每端口 dma_tx_active 标志（ISR 回调定位用）
// DMA 回调携带 channel_id，通过反向查找直接定位端口，无全局状态变量
```

### 9.2 多任务使用注意

```c
// ✅ 正确：多任务安全发送
void task1_send(void)
{
    drv_uart_send(DRV_UART_PORT_USART0, data1, len1);
}

void task2_send(void)
{
    drv_uart_send(DRV_UART_PORT_USART0, data2, len2);  // 互斥锁保护
}

// ❌ 错误：DMA异步模式下data指针必须是全局或静态变量
void wrong_send(void)
{
    uint8_t local_data[64];  // 局部变量！
    drv_uart_send(DRV_UART_PORT_USART0, local_data, 64);  // DMA可能访问失效内存
}
```

---

## 10. 测试验证

### 10.1 单元测试

```bash
# 编译测试代码
main_uart_test.c  # 单端口功能测试（34项）
```

**测试覆盖**：
- 错误处理：空指针、无效端口、重复注册
- 状态管理：初始化、挂起、恢复、关闭、卸载
- 收发功能：RingBuffer、DMA接收
- 电源管理：suspend/resume/shutdown

### 10.2 全端口测试

```bash
# 编译全端口测试代码
main_uart_all_test.c  # 5个端口混合TX模式测试
```

**测试配置**：
- USART0: DMA_DUAL_BUF 模式
- USART1: DMA_ASYNC 模式
- USART2: DMA_SYNC 模式
- UART3: DMA_DUAL_BUF 模式
- UART4: POLLING 模式（无DMA）

**测试项**：注册、发送、状态查询、电源管理、卸载

---

## 11. 注意事项

### 11.1 GPIO配置（V1.5新增）

- ✅ 在 `uart_driver.h` 中通过宏定义配置每个UART的GPIO引脚
- ✅ 未使用的UART设置为 `NO_USE`，可节省代码空间
- ✅ 配置错误会在编译期捕获（`#error`）
- ✅ 运行时检查：`drv_uart_init()` 会检查UART是否配置为 `NO_USE`
- ❌ 不要直接修改驱动代码中的GPIO初始化逻辑

### 11.2 内存管理

- ✅ 所有缓冲区由应用层分配：rx_buf、dma_rx_buf、ringbuf、dma_tx_buf
- ✅ 驱动层仅使用，不分配、不释放内存
- ❌ 避免使用局部变量作为DMA缓冲区指针

### 11.3 回调函数

- ⚠️ 回调在**中断上下文**中执行
- ⚠️ 必须快速执行，不能阻塞
- ⚠️ 不能调用FreeRTOS API（非FromISR版本）
- ⚠️ DMA 回调签名必须携带 `drv_dma_channel_id_e channel_id` 参数（V1.7）

### 11.4 DMA发送注意事项

**所有DMA模式（SYNC/ASYNC/DUAL_BUF）**：
- data指针必须在DMA传输期间有效（使用全局或静态变量）
- 不能使用局部变量作为DMA缓冲区指针

**DMA回调定位机制（V1.7）**：
- DMA 回调函数携带 `channel_id` 参数，驱动通过反向查找直接定位端口
- 不再使用全局状态变量（`s_current_dma_tx_port` 等已移除）
- 多端口可并发 DMA TX，互不干扰

**DMA_ASYNC模式**：
- 超时时间应大于最大数据量的传输时间（建议2-3倍）
- 多端口可并发 DMA TX，每端口独立 `dma_tx_active` 标志，无全局锁串行化

### 11.5 反初始化安全（V1.7新增）

- `drv_uart_deinit()` 会自动检测并强制停止进行中的 DMA TX（异步模式和双缓冲模式）
- 清理顺序：清标志 → 停 DMA → 注销回调 → deinit 通道 → 释放信号量
- 应用层无需在 deinit 前手动等待 DMA 完成

### 11.6 中断优先级

- UART中断优先级需低于FreeRTOS系统中断优先级
- 配置方法：`nvic_priority_group_set(NVIC_PRIGROUP_PRE4_SUB0)`

---

## 12. 常见问题

### Q1: 如何配置UART使用的GPIO引脚？

**A**: 在 `uart_driver.h` 中修改 `DRV_USARTx_GPIO_SEL` 宏定义：

```c
// 示例：USART0使用PB6/PB7而不是默认的PA9/PA10
#define DRV_USART0_GPIO_SEL    DRV_USART0_GPIO_PB6_PB7
```

修改后重新编译即可，无需修改驱动代码。

### Q2: 如何节省未使用UART的代码空间？

**A**: 将未使用的UART设置为 `NO_USE`：

```c
#define DRV_USART1_GPIO_SEL    DRV_USART1_NO_USE  // 不使用USART1
#define DRV_UART3_GPIO_SEL     DRV_UART3_NO_USE   // 不使用UART3
```

驱动会跳过这些UART的GPIO初始化，节省Flash空间。

### Q3: 为什么尝试初始化UART时返回错误？

**A**: 可能原因：
1. 该UART在配置宏表中设置为 `NO_USE`
2. 查看日志输出，会明确提示哪个UART未配置
3. 检查 `uart_driver.h` 中的 `DRV_USARTx_GPIO_SEL` 宏定义

### Q4: 如何选择合适的TX模式？

- **短数据、低速率**：POLLING（最简单）
- **中低频、无需DMA**：INTERRUPT（TXE中断，节省DMA通道）
- **中速率、单任务**：DMA_SYNC
- **高速率、多任务**：DMA_ASYNC（推荐）
- **连续大数据流**：DMA_DUAL_BUF

### Q5: INTERRUPT模式和DMA_ASYNC模式的区别？

- **INTERRUPT**：使用TXE中断逐字节发送，无需DMA通道，CPU不阻塞
- **DMA_ASYNC**：使用DMA批量发送，任务挂起等待完成，性能更高

**选择建议**：
- DMA通道紧张时，优先使用 INTERRUPT 模式
- 高频大数据发送时，优先使用 DMA_ASYNC 模式

### Q6: DMA_ASYNC和DMA_DUAL_BUF的区别？

- **DMA_ASYNC**：单次DMA发送，任务挂起等待完成
- **DMA_DUAL_BUF**：双缓冲循环发送，任务零阻塞，适合持续数据流

### Q7: 为什么需要RingBuffer？

- 防止数据丢失（中断接收时缓存数据）
- 解耦接收和处理（应用层按需读取）
- 支持IDLE空闲中断（一帧数据完整接收后回调）

### Q8: 唤醒串口和普通串口如何选择？

- **唤醒串口**：需要在STOP模式下接收数据唤醒系统
- **普通串口**：正常工作时使用，挂起时彻底关闭降低功耗

### Q9: UART4为什么不能使用DMA模式？

- GD32F505硬件限制：UART4外设没有DMA通道映射
- 可以使用轮询发送（`UART_TX_MODE_POLLING`）或中断发送（`UART_TX_MODE_INTERRUPT`）
- 接收可以使用中断模式（非DMA）

---

## 13. 总结

本UART驱动框架实现了：

✅ **5种TX模式运行时可配置**（每个端口独立设置）
✅ **DMA + IDLE + RingBuffer** 零CPU干预接收
✅ **完全解耦**（驱动层与应用层分离，内存由应用层管理）
✅ **线程安全**（FreeRTOS互斥锁保护）
✅ **低功耗支持**（唤醒串口/普通串口差异化处理）
✅ **工业级可靠性**（错误检测、回调上报、断言机制）
✅ **GPIO配置宏表**（V1.5新增：编译期选择引脚，NO_USE节省代码空间）
✅ **双重检查机制**（V1.5新增：编译期#error + 运行时错误返回）
✅ **RingBuffer SPSC无锁设计**（V1.6更新：移除count字段，中断安全，无需volatile）
✅ **目录结构精简**（V1.6更新：my_os/my_safe_memory/my_rb统一归入utility/）
✅ **DMA回调channel_id精准定位**（V1.7：移除全局状态变量，多端口并发安全）
✅ **deinit强制停止DMA TX**（V1.7：防止释放资源后ISR访问已释放信号量）
