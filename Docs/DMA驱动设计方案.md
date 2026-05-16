# DMA 驱动设计方案

| 项目 | 内容 |
|------|------|
| **模块名称** | DMA Driver（dma_driver） |
| **版本** | V1.0 |
| **设计日期** | 2026.05.08 |
| **设计者** | 伍玉蛟 |
| **参考文档** | GD32F50x 用户手册、gd32f50x_dma.h |

---

## 一、设计目标

### 1.1 核心目标
- **100% 解耦**：应用层不直接调用 GD32 标准库 API
- **简洁设计**：仅封装核心功能，不过度设计
- **类型安全**：使用枚举类型隔离硬件细节
- **中断支持**：提供完整的传输完成/半传输/错误中断回调机制

### 1.2 设计原则
参考已有驱动（timer_driver、gpio_driver）的设计风格：
- 表驱动设计管理硬件资源
- 控制块与映射表职责分离
- 统一错误码体系
- 单行简洁注释风格

---

## 二、硬件资源分析

### 2.1 GD32F505 DMA 特性

| 特性 | 说明 |
|------|------|
| **DMA 控制器** | DMA0、DMA1（2个独立控制器） |
| **通道数量** | DMA0: 7个通道（CH0-CH6），DMA1: 5个通道（CH0-CH4），共 12 个通道 |
| **DMAMUX** | 请求多路复用器（12个复用通道 + 4个生成器通道） |
| **传输方向** | 外设→内存、内存→外设、内存→内存 |
| **数据宽度** | 8-bit、16-bit、32-bit |
| **地址递增** | 支持外设/内存地址递增或固定 |
| **循环模式** | 支持循环传输 |
| **优先级** | 低、中、高、超高（4级） |

### 2.2 DMAMUX 请求源（常用）

| 请求源 | ID | 说明 |
|--------|----|------|
| DMA_REQUEST_M2M | 0 | 内存到内存 |
| DMA_REQUEST_USART0_RX/TX | 20/21 | USART0 收发 |
| DMA_REQUEST_USART1_RX/TX | 22/23 | USART1 收发 |
| DMA_REQUEST_SPI0_RX/TX | 10/11 | SPI0 收发 |
| DMA_REQUEST_TIMER5_UP | 8 | TIMER5 更新 |
| DMA_REQUEST_ADC0_ROUTINE | 5 | ADC0 常规 |

---

## 三、功能范围

### 3.1 支持的功能

| 功能 | 支持 | 说明 |
|------|------|------|
| **通道配置** | ✅ | 完整配置传输参数（地址、数量、宽度、方向等） |
| **启动/停止** | ✅ | 通道使能/禁能 |
| **传输完成中断** | ✅ | FTF（Full Transfer Finish）中断回调 |
| **半传输中断** | ✅ | HTF（Half Transfer Finish）中断回调 |
| **错误中断** | ✅ | ERR（Error）中断回调 |
| **循环模式** | ✅ | 自动重载传输 |
| **内存到内存** | ✅ | 块拷贝功能 |
| **传输数量查询** | ✅ | 获取剩余传输数量 |
| **NVIC管理** | ✅ | 自动配置 NVIC 中断优先级 |

### 3.2 不支持的功能（保持简洁）

| 功能 | 不支持原因 |
|------|------------|
| **DMAMUX 同步模式** | 高级功能，应用场景少 |
| **DMAMUX 生成器模式** | 高级功能，应用场景少 |
| **半传输中断精细控制** | 通过回调统一处理 |

---

## 四、数据类型定义

### 4.1 DMA 通道 ID 枚举

```c
/** DMA 通道 ID 枚举 */
typedef enum
{
    /* DMA0 通道 */
    DRV_DMA0_CH0 = 0,          /**< DMA0 Channel 0 */
    DRV_DMA0_CH1,              /**< DMA0 Channel 1 */
    DRV_DMA0_CH2,              /**< DMA0 Channel 2 */
    DRV_DMA0_CH3,              /**< DMA0 Channel 3 */
    DRV_DMA0_CH4,              /**< DMA0 Channel 4 */
    DRV_DMA0_CH5,              /**< DMA0 Channel 5 */
    DRV_DMA0_CH6,              /**< DMA0 Channel 6 */

    /* DMA1 通道 */
    DRV_DMA1_CH0,              /**< DMA1 Channel 0 */
    DRV_DMA1_CH1,              /**< DMA1 Channel 1 */
    DRV_DMA1_CH2,              /**< DMA1 Channel 2 */
    DRV_DMA1_CH3,              /**< DMA1 Channel 3 */
    DRV_DMA1_CH4,              /**< DMA1 Channel 4 */

    DRV_DMA_MAX                /**< 最大通道数（12） */
} drv_dma_channel_id_e;
```

### 4.2 传输方向枚举

```c
/** DMA 传输方向枚举 */
typedef enum
{
    DRV_DMA_DIR_PERIPH_TO_MEMORY = 0,   /**< 外设到内存 */
    DRV_DMA_DIR_MEMORY_TO_PERIPH,       /**< 内存到外设 */
    DRV_DMA_DIR_MEMORY_TO_MEMORY        /**< 内存到内存 */
} drv_dma_direction_e;
```

### 4.3 数据宽度枚举

```c
/** DMA 数据宽度枚举 */
typedef enum
{
    DRV_DMA_WIDTH_8BIT = 0,      /**< 8-bit */
    DRV_DMA_WIDTH_16BIT,         /**< 16-bit */
    DRV_DMA_WIDTH_32BIT          /**< 32-bit */
} drv_dma_width_e;
```

### 4.4 优先级枚举

```c
/** DMA 通道优先级枚举 */
typedef enum
{
    DRV_DMA_PRIORITY_LOW = 0,    /**< 低优先级 */
    DRV_DMA_PRIORITY_MEDIUM,     /**< 中优先级 */
    DRV_DMA_PRIORITY_HIGH,       /**< 高优先级 */
    DRV_DMA_PRIORITY_ULTRA_HIGH  /**< 超高优先级 */
} drv_dma_priority_e;
```

### 4.5 中断类型枚举（位掩码格式，支持组合）

```c
/** DMA 中断类型枚举 */
typedef enum
{
    DRV_DMA_INT_FTF = (1 << 0),      /**< 传输完成中断（Full Transfer Finish）= 0x01 */
    DRV_DMA_INT_HTF = (1 << 1),      /**< 半传输中断（Half Transfer Finish）= 0x02 */
    DRV_DMA_INT_ERR = (1 << 2)       /**< 错误中断（Error）= 0x04 */
} drv_dma_int_type_e;
```

### 4.6 传输模式枚举

```c
/** DMA 传输模式枚举 */
typedef enum
{
    DRV_DMA_MODE_NORMAL = 0,     /**< 正常模式（传输完成后停止） */
    DRV_DMA_MODE_CIRCULAR        /**< 循环模式（自动重载） */
} drv_dma_mode_e;
```

### 4.7 DMA 通道配置结构体

```c
/** DMA 通道配置结构体 */
typedef struct
{
    uint32_t request_id;                    /**< DMAMUX 请求源 ID（如 DMA_REQUEST_USART0_RX） */
    uint32_t periph_addr;                   /**< 外设地址 */
    uint32_t memory_addr;                   /**< 内存地址 */
    drv_dma_width_e periph_width;           /**< 外设数据宽度 */
    drv_dma_width_e memory_width;           /**< 内存数据宽度 */
    uint16_t transfer_number;               /**< 传输数量（1-65535） */
    drv_dma_direction_e direction;          /**< 传输方向 */
    drv_dma_priority_e priority;            /**< 通道优先级 */
    drv_dma_mode_e mode;                    /**< 传输模式（正常/循环） */
    bool periph_inc;                        /**< 外设地址递增（true=递增，false=固定） */
    bool memory_inc;                        /**< 内存地址递增（true=递增，false=固定） */
} drv_dma_config_t;
```

### 4.8 回调函数类型

```c
/** DMA 中断回调函数类型 */
typedef void (*drv_dma_callback_t)(void);
```

---

## 五、内部数据结构

### 5.1 DMA 通道状态枚举

```c
/** DMA 通道状态枚举（内部使用） */
typedef enum
{
    DRV_DMA_STATE_IDLE = 0,          /**< 未初始化 */
    DRV_DMA_STATE_INITIALIZED,       /**< 已初始化，未启动 */
    DRV_DMA_STATE_RUNNING            /**< 运行中 */
} drv_dma_state_e;
```

### 5.2 DMA 通道控制块

```c
/** DMA 通道控制块（内部使用，应用层不可见） */
typedef struct
{
    drv_dma_state_e state;                    /**< 通道状态 */
    uint32_t dma_periph;                      /**< DMA 控制器基地址（DMA0/DMA1） */
    uint8_t channel_index;                    /**< 通道索引（0-6） */
    uint8_t nvic_irqn;                        /**< NVIC 中断号 */
    uint8_t nvic_priority;                    /**< NVIC 中断优先级 */
    drv_dma_callback_t ftf_callback;          /**< 传输完成中断回调 */
    drv_dma_callback_t htf_callback;          /**< 半传输中断回调 */
    drv_dma_callback_t err_callback;          /**< 错误中断回调 */
} drv_dma_ctrl_t;
```

### 5.3 通道映射表

```c
/** DMA 通道映射表（编译期初始化，存储硬件静态信息） */
typedef struct
{
    uint32_t dma_periph;                      /**< DMA 控制器基地址 */
    uint8_t channel_index;                    /**< 通道索引 */
    uint8_t nvic_irqn;                        /**< NVIC 中断号 */
} drv_dma_map_t;
```

---

## 六、API 接口设计

### 6.1 初始化与去初始化

```c
/**
 * @brief  初始化 DMA 通道
 * @param  channel_id DMA 通道 ID
 * @param  config 通道配置参数
 * @return DRV_DMA_ERR_OK=成功，其他=失败
 */
int32_t drv_dma_init(drv_dma_channel_id_e channel_id, drv_dma_config_t *config);

/**
 * @brief  去初始化 DMA 通道
 * @param  channel_id DMA 通道 ID
 * @return DRV_DMA_ERR_OK=成功，其他=失败
 */
int32_t drv_dma_deinit(drv_dma_channel_id_e channel_id);
```

### 6.2 启动与停止

```c
/**
 * @brief  启动 DMA 传输
 * @param  channel_id DMA 通道 ID
 * @return DRV_DMA_ERR_OK=成功，其他=失败
 */
int32_t drv_dma_start(drv_dma_channel_id_e channel_id);

/**
 * @brief  停止 DMA 传输
 * @param  channel_id DMA 通道 ID
 * @return DRV_DMA_ERR_OK=成功，其他=失败
 * @note   停止后通道状态变为 INITIALIZED，可再次调用 start 启动
 */
int32_t drv_dma_stop(drv_dma_channel_id_e channel_id);
```

### 6.3 回调注册

```c
/**
 * @brief  注册 DMA 中断回调函数
 * @param  channel_id DMA 通道 ID
 * @param  int_type 中断类型（FTF/HTF/ERR）
 * @param  callback 回调函数指针
 * @return DRV_DMA_ERR_OK=成功，其他=失败
 */
int32_t drv_dma_callback_register(drv_dma_channel_id_e channel_id,
                                  drv_dma_int_type_e int_type,
                                  drv_dma_callback_t callback);

/**
 * @brief  注销 DMA 中断回调函数
 * @param  channel_id DMA 通道 ID
 * @param  int_type 中断类型
 * @return DRV_DMA_ERR_OK=成功，其他=失败
 */
int32_t drv_dma_callback_unregister(drv_dma_channel_id_e channel_id,
                                    drv_dma_int_type_e int_type);
```

### 6.4 中断管理

```c
/**
 * @brief  使能 DMA 中断
 * @param  channel_id DMA 通道 ID
 * @param  int_type 中断类型（可组合：DRV_DMA_INT_FTF | DRV_DMA_INT_HTF | DRV_DMA_INT_ERR）
 * @param  nvic_priority NVIC 中断优先级（0-15）
 * @return DRV_DMA_ERR_OK=成功，其他=失败
 */
int32_t drv_dma_int_enable(drv_dma_channel_id_e channel_id,
                           uint8_t int_type,
                           uint8_t nvic_priority);

/**
 * @brief  禁能 DMA 中断
 * @param  channel_id DMA 通道 ID
 * @param  int_type 中断类型
 * @return DRV_DMA_ERR_OK=成功，其他=失败
 */
int32_t drv_dma_int_disable(drv_dma_channel_id_e channel_id,
                            uint8_t int_type);
```

### 6.5 运行时配置

```c
/**
 * @brief  设置传输数量
 * @param  channel_id DMA 通道 ID
 * @param  number 传输数量（1-65535）
 * @return DRV_DMA_ERR_OK=成功，其他=失败
 */
int32_t drv_dma_set_transfer_number(drv_dma_channel_id_e channel_id, uint16_t number);

/**
 * @brief  获取剩余传输数量
 * @param  channel_id DMA 通道 ID
 * @return 剩余传输数量（0 表示传输完成）
 */
uint16_t drv_dma_get_transfer_number(drv_dma_channel_id_e channel_id);

/**
 * @brief  设置内存地址（运行时动态修改）
 * @param  channel_id DMA 通道 ID
 * @param  addr 新内存地址
 * @return DRV_DMA_ERR_OK=成功，其他=失败
 */
int32_t drv_dma_set_memory_address(drv_dma_channel_id_e channel_id, uint32_t addr);

/**
 * @brief  设置外设地址（运行时动态修改）
 * @param  channel_id DMA 通道 ID
 * @param  addr 新外设地址
 * @return DRV_DMA_ERR_OK=成功，其他=失败
 */
int32_t drv_dma_set_periph_address(drv_dma_channel_id_e channel_id, uint32_t addr);
```

### 6.6 状态查询

```c
/**
 * @brief  查询 DMA 通道是否运行中
 * @param  channel_id DMA 通道 ID
 * @return true=运行中，false=未运行
 */
bool drv_dma_is_running(drv_dma_channel_id_e channel_id);

/**
 * @brief  查询 DMA 通道是否已初始化
 * @param  channel_id DMA 通道 ID
 * @return true=已初始化，false=未初始化
 */
bool drv_dma_is_initialized(drv_dma_channel_id_e channel_id);
```

### 6.7 ISR 调用接口

```c
/**
 * @brief  运行 DMA 中断回调（由 ISR 调用）
 * @param  channel_id DMA 通道 ID
 * @param  int_flag 中断标志（FTF/HTF/ERR）
 * @note   此函数由 gd32f50x_it.c 中的 ISR 调用
 */
void drv_dma_run_callback(drv_dma_channel_id_e channel_id, drv_dma_int_type_e int_flag);
```

---

## 七、错误码定义

```c
/** DMA 驱动错误码 */
typedef enum
{
    DRV_DMA_ERR_OK = 0,                 /**< 成功 */
    DRV_DMA_ERR_INVALID_CHANNEL,        /**< 无效的通道 ID */
    DRV_DMA_ERR_INVALID_PARAM,          /**< 无效的参数 */
    DRV_DMA_ERR_NOT_INITIALIZED,        /**< 通道未初始化 */
    DRV_DMA_ERR_BUSY,                   /**< 通道忙 */
    DRV_DMA_ERR_TRANSFER_COMPLETE       /**< 传输已完成 */
} drv_dma_err_e;
```

---

## 八、ISR 架构设计

### 8.1 标准 ISR 模板

在 `gd32f50x_it.c` 中实现每个 DMA 通道的中断服务函数：

```c
/** DMA0 Channel 0 中断服务函数 */
void DMA0_Channel0_IRQHandler(void)
{
    /* 传输完成中断 */
    if (SET == dma_interrupt_flag_get(DMA0, DMA_CH0, DMA_INT_FLAG_FTF))
    {
        dma_interrupt_flag_clear(DMA0, DMA_CH0, DMA_INT_FLAG_GIF);  /* 清除全局中断标志 */
        drv_dma_run_callback(DRV_DMA0_CH0, DRV_DMA_INT_FTF);
    }

    /* 半传输中断 */
    if (SET == dma_interrupt_flag_get(DMA0, DMA_CH0, DMA_INT_FLAG_HTF))
    {
        dma_interrupt_flag_clear(DMA0, DMA_CH0, DMA_INT_FLAG_GIF);  /* 清除全局中断标志 */
        drv_dma_run_callback(DRV_DMA0_CH0, DRV_DMA_INT_HTF);
    }

    /* 错误中断 */
    if (SET == dma_interrupt_flag_get(DMA0, DMA_CH0, DMA_INT_FLAG_ERR))
    {
        dma_interrupt_flag_clear(DMA0, DMA_CH0, DMA_INT_FLAG_GIF);  /* 清除全局中断标志 */
        drv_dma_run_callback(DRV_DMA0_CH0, DRV_DMA_INT_ERR);
    }
}
```

### 8.2 NVIC 自动配置

在 `drv_dma_int_enable` 中自动配置 NVIC：

```c
/* 配置 NVIC 中断优先级 */
nvic_irq_enable(ctrl->nvic_irqn, ctrl->nvic_priority, 0);
```

---

## 九、实现要点

### 9.1 通道映射表设计

```c
/** DMA 通道映射表（编译期初始化） */
static const drv_dma_map_t s_dma_map[DRV_DMA_MAX] =
{
    /* DMA0 通道 */
    {DMA0, DMA_CH0, DMA0_Channel0_IRQn},
    {DMA0, DMA_CH1, DMA0_Channel1_IRQn},
    {DMA0, DMA_CH2, DMA0_Channel2_IRQn},
    {DMA0, DMA_CH3, DMA0_Channel3_IRQn},
    {DMA0, DMA_CH4, DMA0_Channel4_IRQn},
    {DMA0, DMA_CH5, DMA0_Channel5_IRQn},
    {DMA0, DMA_CH6, DMA0_Channel6_IRQn},

    /* DMA1 通道 */
    {DMA1, DMA_CH0, DMA1_Channel0_IRQn},
    {DMA1, DMA_CH1, DMA1_Channel1_IRQn},
    {DMA1, DMA_CH2, DMA1_Channel2_IRQn},
    {DMA1, DMA_CH3, DMA1_Channel3_IRQn},
    {DMA1, DMA_CH4, DMA1_Channel4_IRQn}
};
```

### 9.2 DMAMUX 请求源配置

在 `drv_dma_init` 中自动配置 DMAMUX：

```c
/* 配置 DMAMUX 请求源 */
dmamux_request_id_config(
    (dmamux_multiplexer_channel_enum)channel_index,
    config->request_id
);
```

### 9.3 时钟使能

在 `_drv_dma_enable_clock` 内部函数中自动使能 DMA 控制器和 DMAMUX 时钟：

```c
static void _drv_dma_enable_clock(uint32_t dma_periph)
{
    /* 使能 DMAMUX 时钟（必须先于 DMA 时钟使能） */
    rcu_periph_clock_enable(RCU_DMAMUX);

    if (dma_periph == DMA0)
    {
        rcu_periph_clock_enable(RCU_DMA0);
    }
    else
    {
        rcu_periph_clock_enable(RCU_DMA1);
    }
}
```

### 9.4 参数校验宏

```c
/** 通道 ID 合法性检查 */
#define DMA_CHECK_ID(ch) \
    do { \
        if ((ch) >= DRV_DMA_MAX) { \
            DRV_DMA_LOGE("Invalid channel ID: %d", ch); \
            return DRV_DMA_ERR_INVALID_CHANNEL; \
        } \
    } while(0)

/** 初始化状态检查 */
#define DMA_CHECK_INIT(ch) \
    do { \
        if (s_dma_ctrl[ch].state == DRV_DMA_STATE_IDLE) { \
            DRV_DMA_LOGE("Channel %d not initialized", ch); \
            return DRV_DMA_ERR_NOT_INITIALIZED; \
        } \
    } while(0)
```

---

## 十、应用层使用示例

### 10.1 USART0 RX DMA 接收

```c
#include "drv_dma.h"

/* 接收缓冲区 */
#define RX_BUFFER_SIZE  256
static uint8_t s_rx_buffer[RX_BUFFER_SIZE];

/** DMA 传输完成回调 */
static void usart0_rx_dma_complete_callback(void)
{
    /* 处理接收到的数据 */
    /* 注意：中断上下文中不能调用日志函数 */
}

/** 初始化 USART0 RX DMA */
void usart0_rx_dma_init(void)
{
    drv_dma_config_t config;

    /* 配置 DMA 参数 */
    config.request_id = DMA_REQUEST_USART0_RX;
    config.periph_addr = USART0_DATA_ADDRESS;
    config.memory_addr = (uint32_t)s_rx_buffer;
    config.periph_width = DRV_DMA_WIDTH_8BIT;
    config.memory_width = DRV_DMA_WIDTH_8BIT;
    config.transfer_number = RX_BUFFER_SIZE;
    config.direction = DRV_DMA_DIR_PERIPH_TO_MEMORY;
    config.priority = DRV_DMA_PRIORITY_HIGH;
    config.mode = DRV_DMA_MODE_CIRCULAR;
    config.periph_inc = false;
    config.memory_inc = true;

    /* 初始化 DMA 通道 */
    drv_dma_init(DRV_DMA0_CH0, &config);

    /* 注册传输完成回调 */
    drv_dma_callback_register(DRV_DMA0_CH0, DRV_DMA_INT_FTF,
                              usart0_rx_dma_complete_callback);

    /* 使能中断并设置优先级 */
    drv_dma_int_enable(DRV_DMA0_CH0, DRV_DMA_INT_FTF, 5);

    /* 启动 DMA 传输 */
    drv_dma_start(DRV_DMA0_CH0);
}
```

### 10.2 内存到内存块拷贝

```c
/** DMA 内存拷贝（注意：源/目标地址需按传输宽度对齐） */
void dma_memory_copy(uint32_t src_addr, uint32_t dest_addr, uint16_t size)
{
    drv_dma_config_t config;

    /* 配置 DMA 参数（8-bit 传输，无需对齐） */
    config.request_id = DMA_REQUEST_M2M;
    config.periph_addr = src_addr;
    config.memory_addr = dest_addr;
    config.periph_width = DRV_DMA_WIDTH_8BIT;
    config.memory_width = DRV_DMA_WIDTH_8BIT;
    config.transfer_number = size;  /* 8-bit 传输，数量=字节数 */
    config.direction = DRV_DMA_DIR_MEMORY_TO_MEMORY;
    config.priority = DRV_DMA_PRIORITY_ULTRA_HIGH;
    config.mode = DRV_DMA_MODE_NORMAL;
    config.periph_inc = true;
    config.memory_inc = true;

    /* 初始化并启动 */
    drv_dma_init(DRV_DMA1_CH0, &config);
    drv_dma_start(DRV_DMA1_CH0);

    /* 等待传输完成 */
    while (drv_dma_is_running(DRV_DMA1_CH0))
    {
        /* 等待 */
    }

    /* 清理 */
    drv_dma_deinit(DRV_DMA1_CH0);
}
```

**注意事项：**
- 8-bit 传输：`transfer_number = size`（字节数），无需地址对齐
- 16-bit 传输：`transfer_number = size / 2`，地址需 2 字节对齐
- 32-bit 传输：`transfer_number = size / 4`，地址需 4 字节对齐

### 10.3 在 ISR 中调用回调

在 `gd32f50x_it.c` 中：

```c
void DMA0_Channel0_IRQHandler(void)
{
    /* 传输完成中断 */
    if (SET == dma_interrupt_flag_get(DMA0, DMA_CH0, DMA_INT_FLAG_FTF))
    {
        dma_interrupt_flag_clear(DMA0, DMA_CH0, DMA_INT_FLAG_GIF);  /* 清除全局中断标志 */
        drv_dma_run_callback(DRV_DMA0_CH0, DRV_DMA_INT_FTF);
    }

    /* 半传输中断 */
    if (SET == dma_interrupt_flag_get(DMA0, DMA_CH0, DMA_INT_FLAG_HTF))
    {
        dma_interrupt_flag_clear(DMA0, DMA_CH0, DMA_INT_FLAG_GIF);  /* 清除全局中断标志 */
        drv_dma_run_callback(DRV_DMA0_CH0, DRV_DMA_INT_HTF);
    }

    /* 错误中断 */
    if (SET == dma_interrupt_flag_get(DMA0, DMA_CH0, DMA_INT_FLAG_ERR))
    {
        dma_interrupt_flag_clear(DMA0, DMA_CH0, DMA_INT_FLAG_GIF);  /* 清除全局中断标志 */
        drv_dma_run_callback(DRV_DMA0_CH0, DRV_DMA_INT_ERR);
    }
}
```

---

## 十一、与 timer_driver 对比

| 对比项 | timer_driver | dma_driver |
|--------|--------------|------------|
| **资源数量** | 10 个 TIMER | 12 个 DMA 通道 |
| **硬件特性** | 高级/通用/基本定时器 | DMA0/DMA1 + DMAMUX |
| **中断类型** | 仅 UPDATE 中断 | FTF/HTF/ERR 三种中断 |
| **回调数量** | 1 个回调/Timer | 3 个回调/通道 |
| **映射表** | Timer ID → 基地址 | Channel ID → 控制器+通道+IRQ |
| **自动配置** | 时钟 + POEN + NVIC | 时钟 + DMAMUX + NVIC |
| **运行时配置** | 周期、预分频 | 传输数量、地址 |

---

## 十二、设计总结

### 12.1 设计优势

1. **100% 解耦**：应用层完全不接触 GD32 库 API
2. **类型安全**：所有参数使用枚举，编译期检查
3. **简洁设计**：聚焦核心功能，不过度设计
4. **中断支持完整**：支持 FTF/HTF/ERR 三种中断回调
5. **自动管理**：时钟、DMAMUX、NVIC 全部自动配置
6. **与现有驱动一致**：遵循 timer_driver、gpio_driver 设计风格
7. **命名统一**：文件命名遵循 `xxx_driver.c/h` 规范

### 12.2 文件清单

| 文件 | 说明 |
|------|------|
| `dma_driver.h` | DMA 驱动头文件（API 声明、类型定义） |
| `dma_driver.c` | DMA 驱动实现文件（755 行） |
| `gd32f50x_it.c` | ISR 实现（12 个 DMA 中断服务函数） |
| `gd32f50x_it.h` | ISR 声明（12 个 DMA 中断函数声明） |
| `main_dma_test.c` | DMA 驱动测试文件（59 项测试，100% 覆盖） |

### 12.3 测试验证

**测试环境：** GD32F505VGT7 @ 280MHz, FreeRTOS V10.3.1

**测试结果：** 59/59 测试项全部通过（100%）

| 测试阶段 | 测试项数 | 结果 |
|----------|----------|------|
| Phase 1: 错误处理测试 | 4 | ✅ PASS |
| Phase 2: 状态查询测试 | 6 | ✅ PASS |
| Phase 3: 运行时配置测试 | 4 | ✅ PASS |
| Phase 4: M2M 内存拷贝测试 | 1 | ✅ PASS |
| Phase 5: FTF 中断回调测试 | 4 | ✅ PASS |
| Phase 6: HTF 半传输中断测试 | 4 | ✅ PASS |
| Phase 7: 全通道遍历测试 | 36 | ✅ PASS |

**验证功能：**
- ✅ 12 个 DMA 通道全部可用（DMA0: 7 通道，DMA1: 5 通道）
- ✅ FTF 传输完成中断正常工作
- ✅ HTF 半传输中断正常工作
- ✅ M2M 内存拷贝功能正常
- ✅ 数据完整性验证通过
- ✅ 所有 API 接口功能正确

### 12.4 后续扩展

- 可支持 DMAMUX 同步模式（如果需要）
- 可支持 DMAMUX 生成器模式（如果需要）
- 可添加 DMA 传输链式支持（多缓冲区）

---

**文档版本：V1.0**
**设计日期：2026.05.08**
**设计者：伍玉蛟**
