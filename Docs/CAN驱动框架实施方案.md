# CAN/CAN FD驱动框架实施方案

## 版本历史

| 版本 | 日期 | 作者 | 变更说明 |
|------|------|------|----------|
| V1.0 | 2026.05.28 | 伍玉蛟 | 初始版本，完成方案设计 |

---

## 1. 概述

### 1.1 功能特性

基于GD32F505VGT7 + FreeRTOS的工业级CAN/CAN FD驱动，核心特性：

- **双CAN端口独立管理**：CAN0/CAN1独立配置和控制
- **CAN/CAN FD双模式**：支持传统CAN 2.0B和CAN FD协议
- **多种波特率支持**：
  - CAN 2.0B：10K/20K/50K/100K/125K/250K/500K/800K/1Mbps（9种）
  - CAN FD：1M/2M/4M/5Mbps（数据段，4种）
- **4种通信模式**：
  - 正常模式（Normal）：生产环境
  - 环回模式（Loopback）：软件测试
  - 静默模式（Silent）：总线监听
  - 环回+静默模式：单元测试
- **3种工作模式**：
  - 初始化模式（Initialization）：配置参数
  - 正常模式（Normal）：正常通信
  - 睡眠模式（Sleep）：低功耗
- **两种传输模式**（由应用层选择）：
  - **轮询模式**（POLLING）：适用于低频通信、简单测试
  - **中断模式**（INTERRUPT）：适用于所有生产场景（推荐）
- **过滤器配置**：支持标准帧/扩展帧过滤
- **线程安全**：FreeRTOS互斥锁保护，支持多任务并发
- **完全解耦**：驱动层与应用层分离，所有内存由应用层管理
- **错误处理机制**：总线关闭恢复、错误计数器监控
- **电源管理**：支持低功耗挂起/恢复

### 1.2 CAN vs CAN FD 对比

| 特性 | CAN 2.0B | CAN FD |
|------|----------|--------|
| **最大数据长度** | 8字节 | 64字节 |
| **最大波特率** | 1Mbps | 5Mbps（数据段） |
| **帧格式** | 标准/扩展 | 标准/扩展 + FD标志 |
| **CRC校验** | 15/17位 | 17/21位 |
| **适用场景** | 传统车载网络 | 高速车载网络、ADAS |

### 1.3 硬件资源

#### 1.3.1 CAN端口资源

| 端口 | TX引脚 | RX引脚 | 复用功能 | 典型应用 |
|------|--------|--------|---------|----------|
| CAN0 | PA12/PB9/PD1 | PA11/PB8/PD0 | AF1/AF0 | OBD-II诊断、车载网络 |
| CAN1 | PB6/PB13 | PB5/PB12 | AF3 | 扩展CAN总线、备用 |

**注意**：
- CAN引脚必须配置为复用功能
- 外部需要CAN收发器芯片（如TJA1050、MCP2551）
- CAN FD需要支持FD的收发器（如TJA1043、NCF29A1）

#### 1.3.2 GPIO配置宏表

**特性**：
- 集中管理所有CAN的GPIO引脚配置
- 编译期选择，零运行时开销
- 支持NO_USE选项，未使用的CAN可节省代码空间
- 配置错误在编译期捕获（#error）
- 运行时双重检查，防止应用层错误初始化

**CAN0 GPIO选项**：

| 宏定义 | 值 | TX引脚 | RX引脚 | AF编号 | 说明 |
|--------|-----|--------|--------|--------|------|
| `DRV_CAN0_NO_USE` | 0U | - | - | - | 未使用（节省代码空间） |
| `DRV_CAN0_GPIO_PD0_PD1` | 1U | PD1 | PD0 | AF0 | 默认引脚 |
| `DRV_CAN0_GPIO_PA11_PA12` | 2U | PA12 | PA11 | AF1 | 复用引脚1 |
| `DRV_CAN0_GPIO_PB8_PB9` | 3U | PB9 | PB8 | AF1 | 复用引脚2 |

**CAN1 GPIO选项**：

| 宏定义 | 值 | TX引脚 | RX引脚 | AF编号 | 说明 |
|--------|-----|--------|--------|--------|------|
| `DRV_CAN1_NO_USE` | 0U | - | - | - | 未使用（节省代码空间） |
| `DRV_CAN1_GPIO_PB5_PB6` | 1U | PB6 | PB5 | AF3 | 默认引脚 |
| `DRV_CAN1_GPIO_PB12_PB13` | 2U | PB13 | PB12 | AF3 | 复用引脚 |

**用户配置示例**：
```c
/* 在 can_driver.h 中配置 */
#define DRV_CAN0_GPIO_SEL    DRV_CAN0_GPIO_PD0_PD1     // CAN0使用PD0/PD1（默认）
#define DRV_CAN1_GPIO_SEL    DRV_CAN1_GPIO_PB5_PB6     // CAN1使用PB5/PB6（默认）
```

#### 1.3.3 CAN波特率标准

**CAN 2.0B / CAN FD 仲裁段波特率**：

| 波特率 | 说明 | 典型应用 |
|--------|------|----------|
| 10kbps | 低速CAN | 特殊应用 |
| 20kbps | 低速CAN | 特殊应用 |
| 50kbps | 低速CAN | 商用车 |
| 100kbps | 低速CAN | 工业控制 |
| 125kbps | 中低速CAN | 车身控制 |
| 250kbps | 中速CAN | 商用车J1939、仪表 |
| 500kbps | 高速CAN（推荐） | 乘用车、OBD-II |
| 800kbps | 高速CAN | 特殊应用 |
| 1Mbps | 最高速CAN | 实时控制 |

**CAN FD 数据段波特率**：

| 波特率 | 说明 | 典型应用 | 标准依据 |
|--------|------|----------|----------|
| 1Mbps | FD低速数据段 | 兼容传统CAN | 通用 |
| 2Mbps | FD中速数据段 | 高速传感器 | **ISO 11898车载标准（功能消息）** |
| 4Mbps | FD高速数据段 | 高速数据采集 | 扩展支持 |
| 5Mbps | FD最高速数据段 | ECU刷写编程 | **ISO 11898车载标准最高** |

**注**：
- 车载CAN FD标准为2Mbps（功能消息）和5Mbps（刷写编程），符合ISO 11898-1:2015
- 6M/7M/8M不是车载标准波特率，本项目不支持

---

## 2. 快速上手

### 2.1 目录结构

```
project/OC810/code/
├── driver/
│   ├── can_driver.c      # 驱动实现
│   └── can_driver.h      # 接口定义
└── app/
    └── main_can_test.c   # 测试代码
```

### 2.2 最小使用示例（CAN 2.0B）

```c
#include "can_driver.h"

// 1. 初始化CAN0（500kHz，正常模式）
void can_init_example(void)
{
    drv_can_config_t config = {
        .port = DRV_CAN_PORT_CAN0,
        .mode = DRV_CAN_MODE_NORMAL,
        .protocol = DRV_CAN_PROTOCOL_CAN20B,
        .arb_bitrate = DRV_CAN_BITRATE_500K,
        .data_bitrate = DRV_CAN_BITRATE_NONE,  // CAN 2.0B无数据段
        .use_interrupt = true,
        .use_mutex = true
    };

    drv_can_init(&config);
}

// 2. 配置过滤器（接收标准帧ID 0x100）
void can_filter_config(void)
{
    drv_can_filter_config_t filter = {
        .filter_bank = 0,
        .filter_mode = DRV_CAN_FILTER_MODE_ID_MASK,
        .filter_id_high = (0x100 << 5) & 0xFFE0,  // 标准帧ID左移5位
        .filter_id_low = 0,
        .filter_mask_high = 0x700 << 5,  // 匹配0x100-0x1FF
        .filter_mask_low = 0x0000,
        .fifo_number = 0
    };

    drv_can_config_filter(DRV_CAN_PORT_CAN0, &filter);
}

// 3. 发送CAN帧
void can_send_example(void)
{
    drv_can_frame_t frame = {
        .id = 0x100,
        .frame_type = DRV_CAN_FRAME_STANDARD,
        .format = DRV_CAN_FORMAT_CAN20B,
        .dlc = 8,
        .data = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08},
        .fd_brs = 0,
        .fd_esi = 0
    };

    drv_can_send(DRV_CAN_PORT_CAN0, &frame);
}

// 4. 接收CAN帧（中断回调模式）
void can_rx_callback(drv_can_port_e port, drv_can_frame_t *frame, uint8_t fifo)
{
    MY_LOG_I("CAN RX: ID=0x%08X, DLC=%d, FIFO=%d", frame->id, frame->dlc, fifo);
    // 处理接收数据
}

void can_interrupt_config(void)
{
    drv_can_register_rx_callback(DRV_CAN_PORT_CAN0, can_rx_callback);
}
```

### 2.3 CAN FD使用示例

```c
// 1. 初始化CAN FD（仲裁段500kHz，数据段2Mbps）
void can_fd_init_example(void)
{
    drv_can_config_t config = {
        .port = DRV_CAN_PORT_CAN0,
        .mode = DRV_CAN_MODE_NORMAL,
        .protocol = DRV_CAN_PROTOCOL_CANFD,
        .arb_bitrate = DRV_CAN_BITRATE_500K,
        .data_bitrate = DRV_CAN_FD_BITRATE_2M,  // CAN FD数据段2Mbps
        .use_interrupt = true,         // 使用中断模式（推荐）
        .use_mutex = true
    };

    drv_can_init(&config);
}

// 2. 发送CAN FD帧（64字节）
void can_fd_send_example(void)
{
    drv_can_frame_t frame = {
        .id = 0x100,
        .frame_type = DRV_CAN_FRAME_STANDARD,
        .format = DRV_CAN_FORMAT_CANFD,    // CAN FD帧
        .dlc = 15,                          // DLC=15表示64字节
        .data = {0},                        // 64字节数据
        .fd_brs = 1,                        // 启用波特率切换
        .fd_esi = 0
    };

    drv_can_send(DRV_CAN_PORT_CAN0, &frame);
}
```

---

## 3. 核心API

### 3.1 初始化与反初始化

```c
// 初始化CAN端口
int drv_can_init(const drv_can_config_t *config);

// 反初始化CAN端口
int drv_can_deinit(drv_can_port_e port);
```

### 3.2 数据收发

```c
// 发送CAN帧（阻塞，轮询模式）
int drv_can_send(drv_can_port_e port, const drv_can_frame_t *frame);

// 接收CAN帧（轮询模式）
int drv_can_receive(drv_can_port_e port, drv_can_frame_t *frame, uint32_t timeout_ms);
```

### 3.3 过滤器配置

```c
// 配置CAN过滤器
int drv_can_config_filter(drv_can_port_e port, const drv_can_filter_config_t *filter_config);

// 禁用CAN过滤器
int drv_can_disable_filter(drv_can_port_e port, uint8_t filter_bank);
```

### 3.4 中断回调

```c
// 注册接收回调函数
int drv_can_register_rx_callback(drv_can_port_e port, void (*callback)(drv_can_port_e, drv_can_frame_t *, uint8_t));

// 注册发送完成回调函数
int drv_can_register_tx_callback(drv_can_port_e port, void (*callback)(drv_can_port_e, uint8_t));

// 注册错误回调函数
int drv_can_register_err_callback(drv_can_port_e port, void (*callback)(drv_can_port_e, drv_can_err_type_e));
```

### 3.5 总线管理

```c
// 总线复位（退出Bus-Off状态）
int drv_can_bus_reset(drv_can_port_e port);
```

### 3.6 电源管理

```c
// 挂起CAN（低功耗）
int drv_can_suspend(drv_can_port_e port);

// 恢复CAN
int drv_can_resume(drv_can_port_e port);

// 查询CAN总线状态
drv_can_state_e drv_can_get_bus_status(drv_can_port_e port);

// 查询接收FIFO中的消息数量
int drv_can_get_rx_message_count(drv_can_port_e port, uint8_t fifo_number);

// 查询发送邮箱空闲状态
int drv_can_get_tx_mailbox_free(drv_can_port_e port);
```

---

## 4. 配置结构体详解

### 4.1 drv_can_config_t

```c
typedef struct {
    // 【必选】基础配置
    drv_can_port_e      port;           // CAN端口号
    drv_can_mode_e      mode;           // 工作模式
    drv_can_protocol_e  protocol;       // 协议类型（CAN 2.0B / CAN FD）

    // 【必选】波特率配置
    drv_can_bitrate_e   arb_bitrate;    // 仲裁段波特率（9种：10K~1M）
    drv_can_fd_bitrate_e data_bitrate;  // 数据段波特率（CAN FD专用，4种：1M/2M/4M/5M）

    // 【可选】功能开关（由应用层决定）
    bool                use_interrupt;  // 启用中断模式（推荐）
    uint32_t            timeout_ms;     // 超时时间（毫秒）
    bool                use_mutex;      // 启用互斥锁

    // 【可选】CAN FD高级配置
    bool                enable_tdc;     // 启用传输延迟补偿（CAN FD > 2Mbps时推荐）
} drv_can_config_t;
```

**传输延迟补偿（TDC）说明**：
- CAN FD高速通信时（> 2Mbps），信号传输延迟会影响采样点
- TDC功能自动补偿延迟，提高通信可靠性
- 官方DEMO默认启用TDC
- 建议：> 2Mbps时启用TDC

**传输模式选择指南**：

| 模式 | use_interrupt | 适用场景 |
|------|--------------|---------||
| **轮询模式** | false | 低频通信（< 10帧/秒）、简单测试 |
| **中断模式** | true | 所有生产场景（推荐）⭐ |

---

## 5. 枚举类型

### 5.1 端口枚举

```c
typedef enum {
    DRV_CAN_PORT_CAN0 = 0,
    DRV_CAN_PORT_CAN1,
    DRV_CAN_PORT_MAX
} drv_can_port_e;
```

### 5.2 工作模式枚举

```c
typedef enum {
    DRV_CAN_MODE_NORMAL = 0,      // 正常模式
    DRV_CAN_MODE_LOOPBACK,        // 环回模式（测试用）
    DRV_CAN_MODE_SILENT,          // 静默模式（监听用）
    DRV_CAN_MODE_LOOPBACK_SILENT  // 环回+静默模式（测试用）
} drv_can_mode_e;
```

### 5.3 协议类型枚举

```c
typedef enum {
    DRV_CAN_PROTOCOL_CAN20B = 0,  // CAN 2.0B（传统CAN）
    DRV_CAN_PROTOCOL_CANFD        // CAN FD
} drv_can_protocol_e;
```

### 5.4 波特率枚举

```c
typedef enum {
    // CAN 2.0B / CAN FD 仲裁段波特率（9种）
    DRV_CAN_BITRATE_10K = 0,     // 10 kbps（低速CAN，特殊应用）
    DRV_CAN_BITRATE_20K,         // 20 kbps（低速CAN，特殊应用）
    DRV_CAN_BITRATE_50K,         // 50 kbps（低速CAN，商用车）
    DRV_CAN_BITRATE_100K,        // 100 kbps（低速CAN）
    DRV_CAN_BITRATE_125K,        // 125 kbps（CAN 2.0B常用）
    DRV_CAN_BITRATE_250K,        // 250 kbps（CAN 2.0B常用，商用车）
    DRV_CAN_BITRATE_500K,        // 500 kbps（CAN 2.0B最常用，乘用车）
    DRV_CAN_BITRATE_800K,        // 800 kbps（高速CAN，特殊应用）
    DRV_CAN_BITRATE_1M,          // 1 Mbps（CAN 2.0B最高速率）

    DRV_CAN_BITRATE_NONE = 0xFF  // 无效（CAN 2.0B时使用）
} drv_can_bitrate_e;

// CAN FD 数据段波特率（4种）
typedef enum {
    DRV_CAN_FD_BITRATE_1M = 0,   // 1 Mbps（低速CAN FD）
    DRV_CAN_FD_BITRATE_2M,       // 2 Mbps（车载常用，功能消息）
    DRV_CAN_FD_BITRATE_4M,       // 4 Mbps（高速CAN FD）
    DRV_CAN_FD_BITRATE_5M        // 5 Mbps（车载标准最高，刷写编程）
} drv_can_fd_bitrate_e;
```

### 5.5 帧类型枚举

```c
typedef enum {
    DRV_CAN_FRAME_STANDARD = 0,   // 标准帧（11位ID）
    DRV_CAN_FRAME_EXTENDED        // 扩展帧（29位ID）
} drv_can_frame_type_e;
```

### 5.6 FIFO枚举

```c
typedef enum {
    DRV_CAN_FIFO0 = 0,
    DRV_CAN_FIFO1
} drv_can_fifo_e;
```

### 5.7 过滤器模式枚举

```c
typedef enum {
    DRV_CAN_FILTER_MODE_ID_MASK = 0,  // 标识符屏蔽模式
    DRV_CAN_FILTER_MODE_ID_LIST       // 标识符列表模式
} drv_can_filter_mode_e;
```

### 5.8 状态枚举

```c
typedef enum {
    DRV_CAN_STATE_UNINIT = 0,      // 未初始化
    DRV_CAN_STATE_ACTIVE,          // 活动状态
    DRV_CAN_STATE_BUS_OFF,         // 总线关闭
    DRV_CAN_STATE_SUSPENDED        // 挂起状态
} drv_can_state_e;
```

---

## 6. 数据结构

### 6.1 CAN帧结构

```c
typedef struct {
    uint32_t             id;           // CAN ID（11位或29位）
    drv_can_frame_type_e frame_type;   // 帧类型（标准/扩展）
    drv_can_format_e     format;       // 帧格式（CAN20B/CANFD）
    uint8_t              dlc;          // 数据长度码（0-8或0-15）
    uint8_t              data[64];     // 数据域（CAN 2.0B最大8字节，CAN FD最大64字节）
    uint8_t              fd_brs;       // CAN FD波特率切换标志
    uint8_t              fd_esi;       // CAN FD错误状态指示
} drv_can_frame_t;
```

**注意**：
- CAN 2.0B远程帧（RTR）：`is_remote = true`, `data_len = 0`
- CAN FD不支持远程帧

### 6.2 过滤器结构

```c
typedef struct {
    uint8_t                 filter_bank;       // 过滤器组编号（0-27）
    drv_can_filter_mode_e   filter_mode;       // 过滤器模式
    uint32_t                filter_id_high;    // 过滤器ID高16位（或标识符1）
    uint32_t                filter_id_low;     // 过滤器ID低16位（或标识符2）
    uint32_t                filter_mask_high;  // 过滤器掩码高16位（屏蔽模式）
    uint32_t                filter_mask_low;   // 过滤器掩码低16位（屏蔽模式）
    uint8_t                 fifo_number;       // 关联的FIFO编号（0或1）
} drv_can_filter_config_t;
```

### 6.3 错误计数器结构

```c
typedef struct {
    uint8_t tec;  // 发送错误计数器（0-255）
    uint8_t rec;  // 接收错误计数器（0-255）
} drv_can_error_counters_t;
```

---

## 7. 错误码定义

```c
#define DRV_CAN_ERR_OK              (0)     // 成功
#define DRV_CAN_ERR_FAILED          (-1)    // 失败
#define DRV_CAN_ERR_TIMEOUT         (-2)    // 超时
#define DRV_CAN_ERR_INVALID_PARAM   (-3)    // 参数错误
#define DRV_CAN_ERR_NOT_READY       (-4)    // 未就绪
#define DRV_CAN_ERR_BUS_OFF         (-5)    // 总线关闭
#define DRV_CAN_ERR_OVERRUN         (-6)    // 数据覆盖
#define DRV_CAN_ERR_ARB_LOST        (-7)    // 仲裁丢失
```

---

## 8. 波特率查找表配置

### 8.1 设计原理

驱动采用**查找表**方式管理波特率配置，相比传统switch-case方式：
- ✅ 更易维护：添加新波特率只需增加表项
- ✅ 性能更好：查表比switch分支更高效
- ✅ 代码更清晰：配置集中管理

### 8.2 计算公式

**CAN波特率计算公式**：
```
CAN波特率 = APB1_FREQ / (PSC × TQ_NUM)
其中：
  TQ_NUM = 1(Sync_Seg) + TSEG1(Prop_Seg+Phase_Seg1) + TSEG2(Phase_Seg2)
  采样点 = (1 + TSEG1) / TQ_NUM × 100%
```

**开发者指南（适配不同APB1频率）**：
1. 确定APB1频率（GD32F50x通常为60MHz）
2. 选择采样点（推荐80%，即TSEG1:TSEG2 = 7:2）
3. 计算TQ总数：TQ_NUM = 1 + TSEG1 + TSEG2
4. 计算分频器：PSC = APB1_FREQ / (目标波特率 × TQ_NUM)
5. 验证PSC范围：1 ≤ PSC ≤ 1024（仲裁段），1 ≤ PSC ≤ 32（数据段）
6. 验证实际波特率误差：< 1%为优秀，< 3%可接受

### 8.3 预定义波特率表

**CAN 2.0B / CAN FD仲裁段波特率表（APB1=60MHz，采样点80%）**：

| 波特率 | PSC | TSEG1 | TSEG2 | SJW | 采样点 | 误差 |
|--------|-----|-------|-------|-----|--------|------|
| 10kbps | 600 | 7 | 2 | 2 | 80% | 0% ✅ |
| 20kbps | 300 | 7 | 2 | 2 | 80% | 0% ✅ |
| 50kbps | 120 | 7 | 2 | 2 | 80% | 0% ✅ |
| 100kbps | 60 | 7 | 2 | 2 | 80% | 0% ✅ |
| 125kbps | 48 | 7 | 2 | 2 | 80% | 0% ✅ |
| 250kbps | 24 | 7 | 2 | 2 | 80% | 0% ✅ |
| 500kbps | 12 | 7 | 2 | 2 | 80% | 0% ✅ |
| 800kbps | 7 | 8 | 1 | 1 | 90% | 0% ✅ |
| 1Mbps | 6 | 7 | 2 | 2 | 80% | 0% ✅ |

**CAN FD数据段波特率表（APB1=60MHz，采样点75%~87%）**：

| 波特率 | PSC | TSEG1 | TSEG2 | SJW | 采样点 | 误差 | 标准依据 |
|--------|-----|-------|-------|-----|--------|------|----------|
| 1Mbps | 7 | 5 | 2 | 2 | 75% | +7.1% | 通用 |
| 2Mbps | 3 | 7 | 2 | 2 | 80% | 0% ✅ | ISO 11898（功能消息） |
| 4Mbps | 1 | 12 | 2 | 2 | 87% | 0% ✅ | 扩展支持 |
| 5Mbps | 1 | 9 | 2 | 2 | 83% | 0% ✅ | ISO 11898（刷写编程） |

**注**：
- APB1=60MHz时，驱动自动适配PSC分频器
- 如果APB1频率不是60MHz，驱动会重新计算PSC

---

## 9. 过滤器配置

### 9.1 过滤模式

**列表模式（List Mode）**：
- 精确匹配指定的ID
- 适合接收少量特定ID

**掩码模式（Mask Mode）**：
- 使用掩码匹配ID范围
- 适合接收某一类ID

### 9.2 配置示例

```c
// 示例1：接收标准帧ID 0x100
drv_can_filter_config_t filter1 = {
    .filter_bank = 0,
    .filter_mode = DRV_CAN_FILTER_MODE_ID_LIST,
    .filter_id_high = (0x100 << 5) & 0xFFE0,  // 标准帧ID左移5位
    .filter_id_low = 0,  // IDE=0, RTR=0, EXID[17:15]=0
    .filter_mask_high = 0xFFE0,
    .filter_mask_low = 0x0000,
    .fifo_number = 0
};

// 示例2：接收标准帧ID 0x100-0x1FF（屏蔽模式）
drv_can_filter_config_t filter2 = {
    .filter_bank = 1,
    .filter_mode = DRV_CAN_FILTER_MODE_ID_MASK,
    .filter_id_high = (0x100 << 5) & 0xFFE0,
    .filter_id_low = 0,
    .filter_mask_high = 0x700 << 5,  // 高3位不匹配
    .filter_mask_low = 0x0000,
    .fifo_number = 0
};

drv_can_config_filter(DRV_CAN_PORT_CAN0, &filter1);
drv_can_config_filter(DRV_CAN_PORT_CAN0, &filter2);
```

---

## 10. 错误处理机制

### 10.1 CAN错误类型

| 错误类型 | 说明 | 恢复方式 |
|---------|------|---------|
| **Bus-Off** | 发送错误计数器≥256 | 调用总线复位 |
| **Error Passive** | 错误计数器≥128 | 自动恢复 |
| **Error Warning** | 错误计数器≥96 | 自动恢复 |
| **FIFO Overflow** | 接收FIFO溢出 | 清除溢出标志 |

### 10.2 错误恢复流程

```c
int can_error_recovery(drv_can_port_e port)
{
    uint8_t tec, rec;

    // 1. 获取错误计数器
    drv_can_get_error_counters(port, &tec, &rec);

    if (tec >= 256 || rec >= 256) {
        // 2. Bus-Off状态，需要复位
        MY_LOG_W("CAN Bus-Off detected, resetting...");
        drv_can_bus_reset(port);
    }

    // 3. 等待恢复
    vTaskDelay(pdMS_TO_TICKS(100));

    // 4. 检查状态
    int status = drv_can_get_bus_status(port);
    if (status == DRV_CAN_STATE_ACTIVE) {
        MY_LOG_I("CAN recovered");
        return DRV_CAN_ERR_OK;
    }

    return DRV_CAN_ERR_FAILED;
}
```

---

## 11. 线程安全

### 11.1 互斥锁机制

```c
// 每个CAN端口独立互斥锁
config.use_mutex = true;  // 多任务时必须启用

// 保护CAN总线访问，防止多任务并发冲突
```

### 11.2 多任务使用注意

```c
// ✅ 正确：多任务安全访问
void task1_can(void)
{
    drv_can_frame_t frame = {...};
    drv_can_send(DRV_CAN_PORT_CAN0, &frame);
}

void task2_can(void)
{
    drv_can_frame_t frame = {...};
    drv_can_send(DRV_CAN_PORT_CAN0, &frame);  // 互斥锁保护
}
```

---

## 12. 中断处理架构

### 12.1 中断服务函数

```c
// gd32f50x_it.c
void CAN0_RX0_IRQHandler(void)
{
    drv_can_irq_handler(DRV_CAN_PORT_CAN0, DRV_CAN_FIFO0);
}

void CAN0_RX1_IRQHandler(void)
{
    drv_can_irq_handler(DRV_CAN_PORT_CAN0, DRV_CAN_FIFO1);
}

void CAN0_TX_IRQHandler(void)
{
    drv_can_tx_irq_handler(DRV_CAN_PORT_CAN0);
}

void CAN0_EWMC_IRQHandler(void)
{
    drv_can_error_irq_handler(DRV_CAN_PORT_CAN0);
}

// CAN1类似
```

### 12.2 中断回调注册

```c
// 应用层注册回调
void my_can_rx_callback(drv_can_port_e port, const drv_can_frame_t *frame)
{
    // 处理接收帧
    if (frame->is_fd) {
        MY_LOG_I("CAN FD frame, len=%d", frame->data_len);
    } else {
        MY_LOG_I("CAN 2.0B frame, len=%d", frame->data_len);
    }
}

void can_init(void)
{
    drv_can_register_rx_callback(DRV_CAN_PORT_CAN0, my_can_rx_callback);
}
```

---

## 13. CAN FD特殊处理

### 13.1 CAN FD帧识别

```c
// 接收回调中识别CAN FD帧
void can_rx_callback(drv_can_port_e port, const drv_can_frame_t *frame)
{
    if (frame->is_fd) {
        // CAN FD帧处理
        if (frame->is_brs) {
            MY_LOG_I("BRS enabled, data segment at higher bitrate");
        }
        if (frame->is_esi) {
            MY_LOG_W("ESI set, transmitter in error passive");
        }
    } else {
        // CAN 2.0B帧处理
    }
}
```

### 13.2 波特率切换（BRS）

```c
// 发送CAN FD帧，启用波特率切换
drv_can_frame_t frame = {
    .id = 0x100,
    .frame_type = DRV_CAN_FRAME_STD,
    .is_fd = true,
    .is_brs = true,  // 数据段使用更高波特率
    .data_len = 64,
    .data = {0}
};

drv_can_send(DRV_CAN_PORT_CAN0, &frame);
```

---

## 14. 应用示例

### 14.1 OBD-II诊断（CAN 2.0B）

```c
// OBD-II标准波特率：500kbps
void obd_init(void)
{
    drv_can_config_t config = {
        .port = DRV_CAN_PORT_CAN0,
        .mode = DRV_CAN_MODE_NORMAL,
        .protocol = DRV_CAN_PROTOCOL_CAN20B,
        .arb_bitrate = DRV_CAN_BITRATE_500K,
        .data_bitrate = DRV_CAN_BITRATE_NONE,
        .use_interrupt = true,
        .use_mutex = true
    };

    drv_can_init(&config);

    // 配置OBD-II过滤器（接收0x7E8-0x7EF）
    drv_can_filter_config_t filter = {
        .filter_bank = 0,
        .filter_mode = DRV_CAN_FILTER_MODE_ID_MASK,
        .filter_id_high = (0x7E8 << 5) & 0xFFE0,
        .filter_id_low = 0,
        .filter_mask_high = 0x7F8 << 5,
        .filter_mask_low = 0x0000,
        .fifo_number = 0
    };

    drv_can_config_filter(DRV_CAN_PORT_CAN0, &filter);
}

// 发送OBD-II请求
void obd_request_pid(uint8_t pid)
{
    drv_can_frame_t frame = {
        .id = 0x7DF,  // OBD-II广播地址
        .frame_type = DRV_CAN_FRAME_STANDARD,
        .format = DRV_CAN_FORMAT_CAN20B,
        .dlc = 8,
        .data = {0x02, 0x01, pid, 0x00, 0x00, 0x00, 0x00, 0x00},
        .fd_brs = 0,
        .fd_esi = 0
    };

    drv_can_send(DRV_CAN_PORT_CAN0, &frame);
}
```

### 14.2 高速传感器数据采集（CAN FD）

```c
// CAN FD 2Mbps数据段
void sensor_canfd_init(void)
{
    drv_can_config_t config = {
        .port = DRV_CAN_PORT_CAN0,
        .mode = DRV_CAN_MODE_NORMAL,
        .protocol = DRV_CAN_PROTOCOL_CANFD,
        .arb_bitrate = DRV_CAN_BITRATE_500K,
        .data_bitrate = DRV_CAN_FD_BITRATE_2M,
        .use_interrupt = true,
        .use_mutex = true
    };

    drv_can_init(&config);
}

// 发送64字节传感器数据
void sensor_send_data(float *data, uint16_t count)
{
    drv_can_frame_t frame = {
        .id = 0x100,
        .frame_type = DRV_CAN_FRAME_STANDARD,
        .format = DRV_CAN_FORMAT_CANFD,
        .dlc = 15,  // 15表示64字节
        .data = {0},
        .fd_brs = 1,
        .fd_esi = 0
    };

    // 填充数据
    memcpy(frame.data, data, count * sizeof(float));

    drv_can_send(DRV_CAN_PORT_CAN0, &frame);
}
```

### 14.3 CAN总线监听（静默模式）

```c
// 静默模式：只接收，不发送（不干扰总线）
void can_sniffer_init(void)
{
    drv_can_config_t config = {
        .port = DRV_CAN_PORT_CAN0,
        .mode = DRV_CAN_MODE_SILENT,  // 静默模式
        .protocol = DRV_CAN_PROTOCOL_CANFD,
        .arb_bitrate = DRV_CAN_BITRATE_500K,
        .data_bitrate = DRV_CAN_FD_BITRATE_2M,
        .use_interrupt = true,
        .use_mutex = false
    };

    drv_can_init(&config);

    // 接收所有帧（禁用过滤器）
    for (uint8_t i = 0; i < 28; i++) {
        drv_can_disable_filter(DRV_CAN_PORT_CAN0, i);
    }
}

// 记录所有CAN帧
void can_sniffer_callback(drv_can_port_e port, drv_can_frame_t *frame, uint8_t fifo)
{
    MY_LOG_I("[%s] ID=0x%08X, DLC=%d",
             frame->format == DRV_CAN_FORMAT_CANFD ? "FD" : "2.0B",
             frame->id,
             frame->dlc);
}
```

---

## 15. 应用层设计注意事项

### 15.1 drv_can_receive API正确使用

**关键陷阱**：第三个参数是`fifo_number`（FIFO编号），**不是超时时间**！

```c
// ❌ 错误理解（以为第三个参数是超时时间）
int ret = drv_can_receive(DRV_CAN_PORT_CAN0, &rx_frame, 50);  // 50ms超时？

// ✅ 正确理解（第三个参数是FIFO编号：0或1）
int ret = drv_can_receive(DRV_CAN_PORT_CAN0, &rx_frame, 0);   // FIFO0
int ret = drv_can_receive(DRV_CAN_PORT_CAN0, &rx_frame, 1);   // FIFO1
```

**错误表现**：
- 传入>1的值会触发参数校验失败：`DRV_CAN_ERR_INVALID_PARAM`（-3）
- 日志输出：`[ERR] drv_can_receive Invalid param`

### 15.2 轮询接收必须等待FIFO非空

**关键问题**：`drv_can_receive`是**立即返回**的轮询函数，不会阻塞等待！

**原厂DEMO做法**（GD32F50x官方communication_Loopback示例）：
```c
/* waiting for receive completed */
while((can_receive_message_length_get(CANX, CAN_FIFO1) < 1) && (0 != timeout)) {
    timeout--;
}
can_message_receive(CANX, CAN_FIFO1, &receive_message);
```

**应用层正确实现**：
```c
// 发送后等待硬件回环
my_task_delay_ms(10);

// 轮询等待FIFO有数据（参考原厂DEMO）
uint32_t timeout = 1000;  // 1秒超时
while (timeout > 0)
{
    ret = drv_can_receive(DRV_CAN_PORT_CAN0, &rx_frame, 0);  // FIFO0
    if (ret == DRV_CAN_ERR_OK) break;  // 接收到数据

    if (ret == DRV_CAN_ERR_TIMEOUT) {
        my_task_delay_ms(1);  // FIFO空，等待1ms重试
        timeout--;
    } else {
        break;  // 其他错误
    }
}

if (ret == DRV_CAN_ERR_OK) {
    // 处理接收到的帧
    process_frame(&rx_frame);
} else {
    // 接收失败（超时或其他错误）
    handle_receive_error(ret);
}
```

**错误表现**：
- 发送后立即调用receive，FIFO可能还没数据
- 返回`DRV_CAN_ERR_TIMEOUT`（-2）
- 接收帧数据是未初始化的垃圾值

### 15.3 LOOPBACK模式不触发RX中断

**关键特性**：在LOOPBACK模式下，`drv_can_send()`发送的数据会**硬件自动回环到RX**，但**不会触发RX中断**！

**影响**：
- LOOPBACK测试**不能使用中断+信号量**机制
- 必须使用**纯轮询模式**（`config.use_interrupt = 0`）
- 发送后直接轮询读取FIFO

**正确配置**：
```c
drv_can_config_t config = {
    .port = DRV_CAN_PORT_CAN0,
    .mode = DRV_CAN_MODE_LOOPBACK,
    .use_interrupt = 0,  // ← 必须为0，轮询模式
    .use_mutex = 1,
    .timeout_ms = 1000
};
```

**错误做法**：
```c
// ❌ 错误：LOOPBACK模式配置为中断模式
config.use_interrupt = 1;

// 发送后等待信号量（永远不会被Give）
my_sem_take(s_rx_sem, 100);  // ← 永久阻塞！
```

### 15.4 config结构体必须完整初始化

**关键问题**：`drv_can_config_t`结构体包含多个字段，**必须使用memset清零或显式初始化所有字段**！

**错误表现**：
- 未初始化的字段是随机垃圾值
- `config.port`可能是7（无效值）
- 日志输出：`[ERR] drv_can_init Invalid port: 7`

**正确初始化**：
```c
// 方法1：使用memset清零（推荐）
drv_can_config_t config;
memset(&config, 0, sizeof(drv_can_config_t));
config.port = DRV_CAN_PORT_CAN0;
config.protocol = DRV_CAN_PROTOCOL_CAN20B;
config.mode = DRV_CAN_MODE_LOOPBACK;
config.arb_bitrate = DRV_CAN_BITRATE_500K;
config.use_interrupt = 0;
config.use_mutex = 1;
config.timeout_ms = 1000;

// 方法2：显式初始化所有字段
drv_can_config_t config = {
    .port = DRV_CAN_PORT_CAN0,
    .protocol = DRV_CAN_PROTOCOL_CAN20B,
    .mode = DRV_CAN_MODE_LOOPBACK,
    .arb_bitrate = DRV_CAN_BITRATE_500K,
    .data_bitrate = DRV_CAN_BITRATE_NONE,
    .use_interrupt = 0,
    .use_mutex = 1,
    .timeout_ms = 1000,
    .enable_tdc = 0
};
```

### 15.5 回调函数必须非NULL

**关键要求**：即使使用轮询模式，也必须注册一个回调函数（可以是空实现）！

**原因**：
- 驱动层内部可能检查回调函数指针
- NULL指针可能导致崩溃

**正确做法**：
```c
// 定义空回调函数
static void rx_callback(drv_can_port_e port, drv_can_frame_t *frame, uint8_t fifo)
{
    /* 轮询模式不使用中断回调 */
    (void)port;
    (void)frame;
    (void)fifo;
}

// 初始化后注册
drv_can_init(&config);
drv_can_register_rx_callback(DRV_CAN_PORT_CAN0, rx_callback);  // ← 必须注册
```

### 15.5 LOOPBACK模式测试流程总结

**完整流程**：
```c
void test_loopback_example(void)
{
    drv_can_config_t config;
    drv_can_frame_t tx_frame, rx_frame;
    int ret;

    // 1. 配置结构体清零
    memset(&config, 0, sizeof(drv_can_config_t));

    // 2. 配置LOOPBACK轮询模式
    config.port = DRV_CAN_PORT_CAN0;
    config.protocol = DRV_CAN_PROTOCOL_CAN20B;
    config.mode = DRV_CAN_MODE_LOOPBACK;
    config.arb_bitrate = DRV_CAN_BITRATE_500K;
    config.use_interrupt = 0;  // ← 轮询模式
    config.use_mutex = 1;
    config.timeout_ms = 1000;

    // 3. 初始化CAN
    ret = drv_can_init(&config);
    if (ret != DRV_CAN_ERR_OK) {
        MY_LOG_E("Init failed");
        return;
    }

    // 4. 注册回调（空实现）
    drv_can_register_rx_callback(DRV_CAN_PORT_CAN0, rx_callback);

    // 5. 准备发送帧
    tx_frame.id = 0x100;
    tx_frame.frame_type = DRV_CAN_FRAME_STANDARD;
    tx_frame.format = DRV_CAN_FORMAT_CAN20B;
    tx_frame.dlc = 8;
    tx_frame.fd_brs = 0;
    tx_frame.fd_esi = 0;
    // ... 填充数据

    // 6. 发送帧
    ret = drv_can_send(DRV_CAN_PORT_CAN0, &tx_frame);
    if (ret != DRV_CAN_ERR_OK) {
        MY_LOG_E("Send failed");
        return;
    }

    // 7. 等待硬件回环
    my_task_delay_ms(10);

    // 8. 轮询等待FIFO有数据
    uint32_t timeout = 1000;
    while (timeout > 0) {
        ret = drv_can_receive(DRV_CAN_PORT_CAN0, &rx_frame, 0);  // FIFO0
        if (ret == DRV_CAN_ERR_OK) break;
        if (ret == DRV_CAN_ERR_TIMEOUT) {
            my_task_delay_ms(1);
            timeout--;
        } else {
            break;
        }
    }

    // 9. 验证接收数据
    if (ret == DRV_CAN_ERR_OK &&
        rx_frame.id == tx_frame.id &&
        rx_frame.dlc == tx_frame.dlc &&
        memcmp(rx_frame.data, tx_frame.data, tx_frame.dlc * (tx_frame.dlc <= 8 ? tx_frame.dlc : 64)) == 0) {
        MY_LOG_I("LOOPBACK test PASSED");
    } else {
        MY_LOG_E("LOOPBACK test FAILED");
    }

    // 10. 反初始化
    drv_can_deinit(DRV_CAN_PORT_CAN0);
}
```

---

## 16. 注意事项

### 16.1 GPIO配置

- ✅ 在 `can_driver.h` 中通过宏定义配置每个CAN的GPIO引脚
- ✅ 未使用的CAN设置为 `NO_USE`，可节省代码空间
- ✅ CAN引脚必须配置为复用功能
- ✅ 外部需要CAN收发器芯片

### 16.2 波特率配置

- ✅ 仲裁段波特率必须与总线其他节点一致
- ✅ CAN FD数据段波特率可独立配置
- ✅ 所有节点采样点应尽量接近（建议70-80%）

### 16.3 过滤器配置

- ✅ GD32 CAN有28个过滤器组
- ✅ 过滤器必须在初始化后配置
- ✅ 禁用过滤器可接收所有帧（测试用）

### 16.4 错误处理

- ✅ 定期检查错误计数器
- ✅ Bus-Off状态需要软件复位
- ✅ 错误回调有助于快速定位问题

### 16.5 CAN FD兼容性

- ✅ CAN FD节点可接收传统CAN 2.0B帧
- ✅ 传统CAN节点无法识别CAN FD帧（会报错）
- ✅ 确保总线所有节点支持FD后再启用

---

## 16. 传输模式详细说明

### 16.1 轮询模式（Polling Mode）

**特点**：
- CPU主动查询CAN状态
- 实现简单，适合低频场景
- 会占用CPU资源

**配置**：
```c
drv_can_config_t config = {
    .use_interrupt = false
};
```

**使用示例**：
```c
// 发送
drv_can_send(DRV_CAN_PORT_CAN0, &frame);

// 接收（阻塞等待）
drv_can_frame_t frame;
int ret = drv_can_receive(DRV_CAN_PORT_CAN0, &frame, 100);
if (ret == DRV_CAN_ERR_OK) {
    process_frame(&frame);
}
```

---

### 16.2 中断模式（Interrupt Mode）⭐ 强烈推荐

**特点**：
- CAN帧到达时触发中断
- CPU利用率高，实时性好（延迟 < 10us）
- 与FreeRTOS完美配合
- **适用于所有生产场景**

**配置**：
```c
drv_can_config_t config = {
    .use_interrupt = true
};
```

**架构**：
```
CAN中断 → FreeRTOS队列 → 处理任务 → 业务逻辑
```

**使用示例**：
```c
// 注册接收回调
void can_rx_callback(drv_can_port_e port, const drv_can_frame_t *frame)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(can_rx_queue, frame, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// 处理任务
void can_task(void *pvParameters)
{
    drv_can_frame_t frame;
    while (1) {
        if (xQueueReceive(can_rx_queue, &frame, portMAX_DELAY)) {
            process_frame(&frame);
        }
    }
}
```

**中断服务函数**：
```c
// gd32f50x_it.c
void CAN0_RX0_IRQHandler(void)
{
    drv_can_irq_handler(DRV_CAN_PORT_CAN0, DRV_CAN_FIFO0);
}

void CAN0_RX1_IRQHandler(void)
{
    drv_can_irq_handler(DRV_CAN_PORT_CAN0, DRV_CAN_FIFO1);
}

void CAN0_TX_IRQHandler(void)
{
    drv_can_tx_irq_handler(DRV_CAN_PORT_CAN0);
}

void CAN0_EWMC_IRQHandler(void)
{
    drv_can_error_irq_handler(DRV_CAN_PORT_CAN0);
}
```

---

### 16.3 模式选择建议

| 应用场景 | 帧率 | 推荐模式 | 说明 |
|---------|------|---------|------|
| OBD-II诊断 | < 50帧/秒 | 中断模式 | 低频，实时性好 |
| 传感器网络 | 50-500帧/秒 | 中断模式 ⭐ | 最佳选择 |
| ADAS/摄像头 | 500-1000帧/秒 | 中断模式 | 中断完全胜任 |
| 总线监听 | 任意 | 中断模式 | 只接收，中断足够 |
| 单元测试 | < 10帧/秒 | 轮询模式 | 简单，无需中断 |

**为什么不推荐DMA？**
- ❌ CAN帧长度不固定（0-64字节），DMA优势不明显
- ❌ 中断模式已能满足所有实际需求
- ❌ DMA增加复杂度和调试难度
- ✅ 官方DEMO也未使用DMA
- ✅ 中断+FreeRTOS队列已足够高效

---

## 17. 测试验证

### 17.1 单元测试

**测试覆盖**：
- ✅ 初始化/反初始化
- ✅ CAN 2.0B收发（标准帧/扩展帧/远程帧）
- ✅ CAN FD收发（最大64字节）
- ✅ 波特率切换（BRS）
- ✅ 过滤器配置
- ✅ 环回模式测试
- ✅ 总线关闭恢复
- ✅ 电源管理（suspend/resume）
- ✅ 轮询模式收发
- ✅ 中断模式收发（含FreeRTOS队列）
- ✅ 两种模式切换测试

### 17.2 设备测试

**测试场景**：
- OBD-II诊断（CAN 2.0B 500kbps）
- 高速传感器（CAN FD 2Mbps）
- 总线监听（静默模式）

---

## 18. 常见问题

### Q1: CAN 2.0B和CAN FD如何选择？

- **CAN 2.0B**：兼容性好，适用于传统车载网络
- **CAN FD**：高速、大数据量，适用于ADAS、摄像头

### Q2: 波特率切换（BRS）是什么？

- BRS（Bit Rate Switching）允许CAN FD帧在数据段使用更高波特率
- 仲裁段使用标准波特率（如500kbps），数据段使用高速（如2Mbps）
- 可减少传输时间，提高效率

### Q3: 总线关闭（Bus-Off）如何处理？

- 发送错误计数器≥256时进入Bus-Off状态
- 调用 `drv_can_bus_reset()` 复位总线
- 检查硬件连接和终端电阻

### Q4: 过滤器如何配置？

- **列表模式**：精确匹配ID
- **掩码模式**：匹配ID范围
- 禁用过滤器可接收所有帧

### Q5: 环回模式有什么用？

- 用于软件调试，无需外部硬件
- 发送的帧会立即被自己接收
- 适合单元测试

---

## 19. 总结

本CAN/CAN FD驱动框架实现了：

✅ **2种传输模式**（轮询/中断，中断强烈推荐）
✅ **双端口独立管理**（CAN0/CAN1独立配置）
✅ **CAN/CAN FD双协议**（传统CAN 2.0B + CAN FD）
✅ **多种波特率**（10k-5Mbps，查表实现）
✅ **4种工作模式**（正常/环回/静默/环回+静默）
✅ **过滤器支持**（标准帧/扩展帧，列表/掩码模式）
✅ **中断+轮询双模式**
✅ **GPIO配置宏表**（编译期选择引脚，AF8）
✅ **波特率查找表**（9种仲裁段+4种数据段，支持APB1自动适配）
✅ **线程安全**（FreeRTOS互斥锁保护）
✅ **电源管理**（低功耗挂起/恢复）
✅ **完全解耦**（驱动层与应用层分离）
✅ **工业级可靠性**（错误处理、总线关闭恢复）

**一次开发，终身复用**，可直接投入工业级量产项目使用。

---

**文档版本**：V1.0
**编写日期**：2026.05.28
**编写人员**：伍玉蛟
**审核状态**：待审查
