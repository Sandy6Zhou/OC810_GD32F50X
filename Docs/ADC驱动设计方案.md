# ADC 驱动设计方案

---

## 1. 概述

### 1.1 模块名称
`adc_driver` - GD32F505 ADC 驱动模块

### 1.2 功能描述
基于 GD32F50x 标准外设库，封装 ADC 硬件操作，提供统一的应用层接口，支持：
- 单通道/多通道 ADC 转换（规则通道、插入通道）
- DMA 传输模式
- 模拟看门狗功能（用于低功耗唤醒）
- 内部通道使能（温度传感器、内部参考电压）
- 线程安全（可选互斥锁保护）

### 1.3 设计约束
1. **禁止在中断上下文中调用**：以下函数内部使用 `vTaskDelay`，必须在任务上下文中调用：
   - `drv_adc_init()`
   - `drv_adc_single_read()`
2. **无 suspend/resume 接口**：如需低功耗，调用 `drv_adc_deinit()` 完全关闭，需要时重新 `drv_adc_init()`
3. **EOC/EIC 中断不使用**：避免频繁中断带来的 CPU 开销
4. **内部通道使能时序**：必须在 ADC enable 之前使能，或通过 disable→enable 序列生效

### 1.4 设计原则
- **低耦合**：仅依赖 GD32 标准库和 my_log 日志模块
- **高内聚**：所有 ADC 相关功能集中在 adc_driver 模块
- **类型安全**：使用枚举和结构体，避免魔法数字
- **错误处理**：所有接口返回错误码，便于应用层判断

---

## 2. 模块架构

### 2.1 文件结构
```
project/OC810/code/driver/
├── adc_driver.h          # ADC 驱动接口定义
└── adc_driver.c          # ADC 驱动实现
```

### 2.2 依赖关系
```
adc_driver
├── gd32f50x_adc.h        # GD32 ADC 标准库
├── gd32f50x_rcu.h        # GD32 RCU 标准库（时钟控制）
└── my_log.h              # 日志模块（可选）
```

---

## 3. 数据结构设计

### 3.1 ADC 端口枚举
```c
typedef enum {
    DRV_ADC0 = 0,         /**< ADC0 实例（支持内部通道CH16温度、CH17参考电压） */
    DRV_ADC1,             /**< ADC1 实例 */
    DRV_ADC2,             /**< ADC2 实例 */
    DRV_ADC_MAX           /**< ADC 最大数量 */
} drv_adc_port_e;
```

### 3.2 ADC 通道枚举
```c
typedef enum {
    DRV_ADC_CHANNEL_0 = 0,    /**< ADC 通道 0 */
    DRV_ADC_CHANNEL_1,        /**< ADC 通道 1 */
    DRV_ADC_CHANNEL_2,        /**< ADC 通道 2 */
    DRV_ADC_CHANNEL_3,        /**< ADC 通道 3 */
    DRV_ADC_CHANNEL_4,        /**< ADC 通道 4 */
    DRV_ADC_CHANNEL_5,        /**< ADC 通道 5 */
    DRV_ADC_CHANNEL_6,        /**< ADC 通道 6 */
    DRV_ADC_CHANNEL_7,        /**< ADC 通道 7 */
    DRV_ADC_CHANNEL_8,        /**< ADC 通道 8 */
    DRV_ADC_CHANNEL_9,        /**< ADC 通道 9 */
    DRV_ADC_CHANNEL_10,       /**< ADC 通道 10 */
    DRV_ADC_CHANNEL_11,       /**< ADC 通道 11 */
    DRV_ADC_CHANNEL_12,       /**< ADC 通道 12 */
    DRV_ADC_CHANNEL_13,       /**< ADC 通道 13 */
    DRV_ADC_CHANNEL_14,       /**< ADC 通道 14 */
    DRV_ADC_CHANNEL_15,       /**< ADC 通道 15 */
    DRV_ADC_CHANNEL_16,       /**< 通道16（ADC0:温度传感器, ADC1/2:外部通道） */
    DRV_ADC_CHANNEL_17,       /**< 通道17（ADC0:内部参考电压, ADC1:外部通道, ADC2:不支持） */
    DRV_ADC_CHANNEL_MAX
} drv_adc_channel_e;
```

### 3.3 ADC 分辨率枚举
```c
typedef enum {
    DRV_ADC_RESOLUTION_12B = 0,  /**< 12 位分辨率（默认） */
    DRV_ADC_RESOLUTION_10B,      /**< 10 位分辨率 */
    DRV_ADC_RESOLUTION_8B,       /**< 8 位分辨率 */
    DRV_ADC_RESOLUTION_6B        /**< 6 位分辨率 */
} drv_adc_resolution_e;
```

### 3.4 ADC 对齐方式枚举
```c
typedef enum {
    DRV_ADC_DATAALIGN_RIGHT = 0,  /**< 右对齐（LSB） */
    DRV_ADC_DATAALIGN_LEFT        /**< 左对齐（MSB） */
} drv_adc_dataalign_e;
```

### 3.5 ADC 采样时间枚举
```c
typedef enum {
    DRV_ADC_SAMPLETIME_1POINT5 = 0,   /**< 1.5 个周期 */
    DRV_ADC_SAMPLETIME_7POINT5,       /**< 7.5 个周期 */
    DRV_ADC_SAMPLETIME_13POINT5,       /**< 13.5 个周期（推荐默认） */
    DRV_ADC_SAMPLETIME_28POINT5,       /**< 28.5 个周期 */
    DRV_ADC_SAMPLETIME_41POINT5,       /**< 41.5 个周期 */
    DRV_ADC_SAMPLETIME_55POINT5,       /**< 55.5 个周期 */
    DRV_ADC_SAMPLETIME_71POINT5,       /**< 71.5 个周期 */
    DRV_ADC_SAMPLETIME_239POINT5       /**< 239.5 个周期（高阻抗信号） */
} drv_adc_sampletime_e;
```

### 3.6 ADC 转换模式枚举
```c
typedef enum {
    DRV_ADC_MODE_SINGLE = 0,          /**< 单次转换模式（默认） */
    DRV_ADC_MODE_CONTINUOUS,          /**< 连续转换模式 */
    DRV_ADC_MODE_SCAN_SINGLE,         /**< 扫描模式 + 单次转换 */
    DRV_ADC_MODE_SCAN_CONTINUOUS      /**< 扫描模式 + 连续转换 */
} drv_adc_mode_e;
```

### 3.7 ADC 触发源枚举
```c
typedef enum {
    DRV_ADC_TRIGGER_SOFTWARE = 0,     /**< 软件触发（默认） */
    DRV_ADC_TRIGGER_EXTERNAL,         /**< 外部触发（定时器等） */
    DRV_ADC_TRIGGER_DMA               /**< DMA 触发 */
} drv_adc_trigger_e;
```

### 3.8 ADC 标志枚举
```c
typedef enum {
    DRV_ADC_FLAG_EOC,     /**< 转换完成标志（EORC） */
    DRV_ADC_FLAG_WD0E     /**< 看门狗触发标志（WD0E） */
} drv_adc_flag_e;
```

### 3.9 ADC 错误码定义
```c
#define DRV_ADC_ERR_OK             (0)     /**< 成功 */
#define DRV_ADC_ERR_FAILED         (-1)    /**< 失败 */
#define DRV_ADC_ERR_TIMEOUT        (-2)    /**< 超时 */
#define DRV_ADC_ERR_INVALID_PARAM  (-3)    /**< 参数错误 */
#define DRV_ADC_ERR_NOT_READY      (-4)    /**< 未就绪 */
#define DRV_ADC_ERR_BUSY           (-5)    /**< 忙 */
#define DRV_ADC_ERR_NOT_INIT       (-6)    /**< 未初始化 */
```

### 3.10 ADC 配置结构体
```c
typedef struct {
    drv_adc_port_e port;                  /**< ADC 端口（DRV_ADC0/1/2） */
    drv_adc_resolution_e resolution;      /**< 分辨率 */
    drv_adc_dataalign_e data_align;       /**< 数据对齐方式 */
    drv_adc_mode_e mode;                  /**< 转换模式 */
    drv_adc_trigger_e trigger;            /**< 触发源 */
    uint32_t timeout_ms;                  /**< 转换超时时间（ms） */
    bool use_mutex;                       /**< 启用互斥锁（线程安全） */
} drv_adc_config_t;
```

### 3.11 ADC 通道配置结构体
```c
typedef struct {
    drv_adc_channel_e channel;            /**< ADC 通道 */
    drv_adc_sampletime_e sample_time;    /**< 采样时间 */
    uint8_t rank;                         /**< 规则通道序列位置（0-15） */
} drv_adc_channel_config_t;
```

### 3.12 ADC 状态结构体（内部使用）
```c
typedef struct {
    bool is_init;                         /**< 是否已初始化 */
    bool is_converting;                   /**< 是否正在转换 */
    drv_adc_mode_e mode;                  /**< 当前转换模式 */
    uint8_t channel_count;                /**< 配置的通道数量 */
} drv_adc_state_t;
```

---

## 4. API 接口设计

### 4.1 初始化与去初始化

#### 4.1.1 ADC 初始化
```c
int drv_adc_init(const drv_adc_config_t *config);
```
- **功能**：初始化 ADC 端口，配置分辨率、对齐方式、转换模式
- **参数**：config - ADC 配置结构体指针
- **返回**：int 错误码
- **注意**：
  - 自动使能 ADC 时钟
  - 创建互斥锁（如果 use_mutex=true）
  - 禁止在中断上下文中调用（内部使用 vTaskDelay）

#### 4.1.2 ADC 去初始化
```c
int drv_adc_deinit(drv_adc_port_e port);
```
- **功能**：关闭 ADC 端口，释放资源，删除互斥锁
- **参数**：port - ADC 端口
- **返回**：int 错误码
- **清理**：中断标志、NVIC 挂起、回调函数、ADC 时钟

### 4.2 通道配置

#### 4.2.1 配置规则通道
```c
int drv_adc_routine_channel_config(
    drv_adc_port_e port,
    const drv_adc_channel_config_t *channel_config
);
```
- **功能**：配置规则通道及采样时间
- **参数**：
  - port - ADC 端口
  - channel_config - 通道配置（通道、采样时间、序列位置）
- **返回**：int 错误码

#### 4.2.2 配置插入通道
```c
int drv_adc_inserted_channel_config(
    drv_adc_port_e port,
    const drv_adc_channel_config_t *channel_config
);
```
- **功能**：配置插入通道及采样时间
- **参数**：port - ADC 端口，channel_config - 通道配置
- **返回**：int 错误码

### 4.3 转换控制

#### 4.3.1 启动转换
```c
int drv_adc_start_conversion(drv_adc_port_e port);
```
- **功能**：启动 ADC 转换（软件触发）
- **参数**：port - ADC 端口
- **返回**：int 错误码
- **注意**：仅适用于软件触发模式，繁忙时返回 ERR_BUSY

#### 4.3.2 等待转换完成
```c
int drv_adc_wait_conversion_done(drv_adc_port_e port, uint32_t timeout_ms);
```
- **功能**：阻塞等待转换完成
- **参数**：
  - port - ADC 端口
  - timeout_ms - 超时时间（ms）
- **返回**：int 错误码


### 4.4 数据读取

#### 4.4.1 读取规则通道数据
```c
int drv_adc_routine_data_read(drv_adc_port_e port, uint16_t *data);
```
- **功能**：读取规则通道转换结果
- **参数**：port - ADC 端口，data - 数据输出指针
- **返回**：int 错误码

#### 4.4.2 读取插入通道数据
```c
int drv_adc_inserted_data_read(drv_adc_port_e port, uint16_t *data);
```
- **功能**：读取插入通道转换结果
- **参数**：port - ADC 端口，data - 数据输出指针
- **返回**：int 错误码

#### 4.4.3 读取 ADC 转换结果（便捷接口）
```c
uint16_t drv_adc_read(drv_adc_port_e port);
```
- **功能**：读取规则通道转换结果，自动清除 EORC 标志
- **参数**：port - ADC 端口
- **返回**：uint16_t ADC 值（0-4095，12 位分辨率）
- **注意**：适用于轮询模式

### 4.5 使能/禁能

#### 4.5.1 使能 ADC
```c
int32_t drv_adc_enable(drv_adc_port_e port);
```
- **功能**：使能 ADC 开始工作
- **参数**：port - ADC 端口
- **返回**：int 错误码

#### 4.5.2 禁能 ADC
```c
int32_t drv_adc_disable(drv_adc_port_e port);
```
- **功能**：禁能 ADC 进入低功耗模式
- **参数**：port - ADC 端口
- **返回**：int 错误码

### 4.6 标志管理

#### 4.6.1 查询标志状态
```c
bool drv_adc_flag_get(drv_adc_port_e port, drv_adc_flag_e flag);
```
- **功能**：查询 ADC 标志状态
- **参数**：port - ADC 端口，flag - 标志类型（EOC/WD0E）
- **返回**：true=标志置位，false=标志复位

#### 4.6.2 清除标志
```c
int32_t drv_adc_flag_clear(drv_adc_port_e port, drv_adc_flag_e flag);
```
- **功能**：清除指定的 ADC 标志
- **参数**：port - ADC 端口，flag - 标志类型
- **返回**：int 错误码

### 4.7 便捷接口

#### 4.7.1 单通道单次转换（最常用）
```c
int drv_adc_single_read(
    drv_adc_port_e port,
    drv_adc_channel_e channel,
    drv_adc_sampletime_e sample_time,
    uint16_t *data
);
```
- **功能**：配置单通道并执行单次转换，读取结果
- **参数**：
  - port - ADC 端口
  - channel - 通道
  - sample_time - 采样时间
  - data - 数据输出指针
- **返回**：int 错误码
- **注意**：
  - 最常用接口，一行代码完成单通道读取
  - 内部通道（温度/参考电压）自动按需使能
  - 禁止在中断上下文中调用

#### 4.7.2 配置规则通道数量
```c
int32_t drv_adc_channel_count(drv_adc_port_e port, uint8_t count);
```
- **功能**：设置规则序列通道数量（1-16）
- **参数**：port - ADC 端口，count - 通道数量
- **返回**：int 错误码

### 4.8 高级功能

#### 4.8.1 配置模拟看门狗
```c
int drv_adc_watchdog_config(
    drv_adc_port_e port,
    drv_adc_channel_e channel,
    uint32_t low_threshold,
    uint32_t high_threshold,
    drv_adc_wdg_callback_t callback
);
```
- **功能**：配置模拟看门狗阈值和中断回调
- **参数**：
  - port - ADC 端口
  - channel - 监控通道
  - low_threshold - 低阈值
  - high_threshold - 高阈值
  - callback - 看门狗中断回调函数（可为 NULL）
- **返回**：int 错误码
- **注意**：
  - callback 为 NULL 时不使能中断，仅配置阈值（轮询模式）
  - callback 非 NULL 时使能中断，用于低功耗唤醒场景

#### 4.8.2 ADC 中断处理
```c
void drv_adc_irq_handler(drv_adc_port_e port);
```
- **功能**：ADC 中断处理函数
- **参数**：port - ADC 端口
- **注意**：在 gd32f50x_it.c 的 ISR 中调用，仅处理看门狗中断

#### 4.8.3 使能/禁能 DMA 模式
```c
int drv_adc_dma_mode_enable(drv_adc_port_e port);
int drv_adc_dma_mode_disable(drv_adc_port_e port);
```
- **功能**：使能/禁能 ADC DMA 请求
- **参数**：port - ADC 端口
- **返回**：int 错误码

---

## 5. 应用场景与代码示例

### 5.1 场景1 - 轮询读取（单次触发）

**适用场景**：上电初始化使用、调试时、单次测量
**特点**：简单直接，会阻塞等待读取完成，无中断开销

```c
/* 初始化 */
drv_adc_config_t config = {
    .port = DRV_ADC0,
    .resolution = DRV_ADC_RESOLUTION_12B,
    .data_align = DRV_ADC_DATAALIGN_RIGHT,
    .mode = DRV_ADC_MODE_SINGLE,
    .trigger = DRV_ADC_TRIGGER_SOFTWARE,
    .timeout_ms = 100,
    .use_mutex = false
};
drv_adc_init(&config);

/* 单通道读取 */
uint16_t adc_value;
int ret = drv_adc_single_read(DRV_ADC0, DRV_ADC_CHANNEL_0,
                              DRV_ADC_SAMPLETIME_13POINT5, &adc_value);
if (ret == DRV_ADC_ERR_OK) {
    printf("ADC value: %d\n", adc_value);
}

/* 或者手动分步操作 */
drv_adc_channel_config_t ch_config = {
    .channel = DRV_ADC_CHANNEL_1,
    .sample_time = DRV_ADC_SAMPLETIME_13POINT5,
    .rank = 0
};
drv_adc_routine_channel_config(DRV_ADC0, &ch_config);
drv_adc_start_conversion(DRV_ADC0);
drv_adc_wait_conversion_done(DRV_ADC0, 100);
uint16_t value = drv_adc_read(DRV_ADC0);
```

---

### 5.2 场景2 - DMA 中断（批量采样）

**适用场景**：多通道批量采样、连续监测
**特点**：配置后自动转换，DMA 传输完成后触发中断处理，几乎不占用 CPU

```c
/* DMA 缓冲区 */
#define ADC_DMA_BUFFER_SIZE  100
uint16_t g_adc_dma_buffer[ADC_DMA_BUFFER_SIZE];

/* 初始化 ADC */
drv_adc_config_t config = {
    .port = DRV_ADC0,
    .resolution = DRV_ADC_RESOLUTION_12B,
    .data_align = DRV_ADC_DATAALIGN_RIGHT,
    .mode = DRV_ADC_MODE_SCAN_CONTINUOUS,
    .trigger = DRV_ADC_TRIGGER_SOFTWARE,
    .use_mutex = false
};
drv_adc_init(&config);

/* 配置多通道（规则序列） */
drv_adc_channel_config_t ch_config;
ch_config.sample_time = DRV_ADC_SAMPLETIME_13POINT5;
ch_config.rank = 0;
ch_config.channel = DRV_ADC_CHANNEL_0;
drv_adc_routine_channel_config(DRV_ADC0, &ch_config);

ch_config.rank = 1;
ch_config.channel = DRV_ADC_CHANNEL_1;
drv_adc_routine_channel_config(DRV_ADC0, &ch_config);

ch_config.rank = 2;
ch_config.channel = DRV_ADC_CHANNEL_2;
drv_adc_routine_channel_config(DRV_ADC0, &ch_config);

/* 配置通道数量和 DMA 模式 */
drv_adc_channel_count(DRV_ADC0, 3);
drv_adc_dma_mode_enable(DRV_ADC0);

/* 配置 DMA 传输（需配合 dma_driver） */
dma_config_t dma_cfg = {
    .periph_addr = (uint32_t)&ADC_RDATA(ADC0),
    .memory_addr = (uint32_t)g_adc_dma_buffer,
    .direction = DMA_PERIPHERAL_TO_MEMORY,
    .memory_size = DMA_MEMORY_SIZE_16BIT,
    .periph_size = DMA_PERIPHERAL_SIZE_16BIT,
    .priority = DMA_PRIORITY_HIGH
};
dma_driver_init(DMA_CHANNEL_ADC0, &dma_cfg);

/* 启动 DMA 和 ADC 转换 */
dma_driver_start(DMA_CHANNEL_ADC0);
drv_adc_start_conversion(DRV_ADC0);

/* DMA 传输完成后，在 DMA 中断中处理数据 */
void DMA_Channel4_IRQHandler(void) {
    if (dma_flag_get(DMA_CHANNEL_ADC0, DMA_FLAG_FTF)) {
        /* 处理 g_adc_dma_buffer 中的数据 */
        for (int i = 0; i < ADC_DMA_BUFFER_SIZE; i++) {
            printf("DMA[%d]: %d\n", i, g_adc_dma_buffer[i]);
        }
        dma_flag_clear(DMA_CHANNEL_ADC0, DMA_FLAG_FTF);
    }
}
```

---

### 5.3 场景3 - DMA 轮询（定时读取）

**适用场景**：多通道定时采样，间隔足够长（如 1 秒）
**特点**：推荐使用，几乎不占用 CPU，无需 DMA 中断

```c
/* 配置 DMA（场景2类似）*/
drv_adc_config_t config = {
    .port = DRV_ADC0,
    .resolution = DRV_ADC_RESOLUTION_12B,
    .data_align = DRV_ADC_DATAALIGN_RIGHT,
    .mode = DRV_ADC_MODE_SCAN_SINGLE,  /* 扫描+单次 */
    .trigger = DRV_ADC_TRIGGER_SOFTWARE,
    .use_mutex = false
};
drv_adc_init(&config);

/* 配置多通道 */
drv_adc_channel_config_t ch_config = {
    .sample_time = DRV_ADC_SAMPLETIME_13POINT5,
    .rank = 0
};
ch_config.channel = DRV_ADC_CHANNEL_0;
drv_adc_routine_channel_config(DRV_ADC0, &ch_config);
ch_config.rank = 1;
ch_config.channel = DRV_ADC_CHANNEL_1;
drv_adc_routine_channel_config(DRV_ADC0, &ch_config);

drv_adc_channel_count(DRV_ADC0, 2);
drv_adc_dma_mode_enable(DRV_ADC0);

/* 配置 DMA（需配合 dma_driver） */
/* ... */

/* 定时任务中轮询读取 */
void periodic_adc_task(void *param) {
    while (1) {
        /* 启动 DMA 传输 */
        dma_driver_start(DMA_CHANNEL_ADC0);
        drv_adc_start_conversion(DRV_ADC0);

        /* 等待 DMA 完成（或使用固定间隔） */
        vTaskDelay(pdMS_TO_TICKS(1000));  /* 1 秒间隔 */

        /* 处理数据（此时 DMA 已完成） */
        printf("CH0: %d, CH1: %d\n",
               g_adc_dma_buffer[0], g_adc_dma_buffer[1]);
    }
}
```

---

### 5.4 场景4 - 低功耗唤醒（看门狗中断）

**适用场景**：系统进入 sleep 模式后，通过 ADC 电压异常唤醒
**特点**：配置看门狗阈值，电压超阈值时触发中断唤醒系统

```c
/* 看门狗回调函数（中断中执行，不能使用阻塞API） */
void adc_watchdog_callback(drv_adc_port_e port) {
    /* 唤醒标志设置，不要做耗时操作 */
    g_adc_wakeup_flag = true;
}

/* 配置 ADC 和看门狗 */
drv_adc_config_t config = {
    .port = DRV_ADC0,
    .resolution = DRV_ADC_RESOLUTION_12B,
    .data_align = DRV_ADC_DATAALIGN_RIGHT,
    .mode = DRV_ADC_MODE_SINGLE,
    .trigger = DRV_ADC_TRIGGER_SOFTWARE,
    .use_mutex = false
};
drv_adc_init(&config);

/* 配置监控通道 */
drv_adc_routine_channel_config(DRV_ADC0, &(drv_adc_channel_config_t){
    .channel = DRV_ADC_CHANNEL_1,
    .sample_time = DRV_ADC_SAMPLETIME_13POINT5,
    .rank = 0
});
drv_adc_channel_count(DRV_ADC0, 1);

/* 配置看门狗阈值和中断回调 */
drv_adc_watchdog_config(DRV_ADC0, DRV_ADC_CHANNEL_1,
                        1000, 3000,  /* 低阈值1000，高阈值3000 */
                        adc_watchdog_callback);

/* 在 ISR 中调用中断处理函数 */
void ADC0_1_IRQHandler(void) {
    drv_adc_irq_handler(DRV_ADC0);
}

/* 或者使用轮询模式（不使能中断） */
drv_adc_watchdog_config(DRV_ADC0, DRV_ADC_CHANNEL_1,
                        1000, 3000, NULL);  /* callback=NULL 不使能中断 */

/* 轮询检查看门狗标志 */
while (1) {
    drv_adc_start_conversion(DRV_ADC0);
    drv_adc_wait_conversion_done(DRV_ADC0, 100);
    uint16_t value = drv_adc_read(DRV_ADC0);

    if (drv_adc_flag_get(DRV_ADC0, DRV_ADC_FLAG_WD0E)) {
        printf("Watchdog triggered! ADC value: %d\n", value);
        drv_adc_flag_clear(DRV_ADC0, DRV_ADC_FLAG_WD0E);
    }
}
```

---

### 5.5 场景5 - 完全关闭（节省功耗）

**适用场景**：系统关机或长时间不使用 ADC 时
**特点**：调用 deinit 完全关闭 ADC 和时钟，节省功耗

```c
/* 初始化 */
drv_adc_init(&config);

/* 使用 ADC... */

/* 不再使用时完全关闭 */
drv_adc_deinit(DRV_ADC0);  /* 关闭 ADC、清除中断、释放互斥锁、关闭时钟 */

/* 后续需要时重新初始化 */
drv_adc_init(&config);  /* 完整恢复状态 */
```

---

### 5.6 读取内部通道（温度传感器、内部参考电压）

**适用场景**：读取芯片内部温度或参考电压
**特点**：仅 ADC0 支持，内部通道自动按需使能

```c
/* 读取温度传感器（ADC0 CH16） */
uint16_t temp_value;
int ret = drv_adc_single_read(DRV_ADC0, DRV_ADC_CHANNEL_16,
                              DRV_ADC_SAMPLETIME_239POINT5,  /* 高采样时间 */
                              &temp_value);
if (ret == DRV_ADC_ERR_OK) {
    /* 转换温度（根据芯片手册公式） */
    float temperature = (1.0f - (float)temp_value / 4095.0f) * 85.0f;
    printf("Temperature: %.2f C\n", temperature);
}

/* 读取内部参考电压（ADC0 CH17） */
uint16_t vref_value;
ret = drv_adc_single_read(DRV_ADC0, DRV_ADC_CHANNEL_17,
                          DRV_ADC_SAMPLETIME_239POINT5,
                          &vref_value);
if (ret == DRV_ADC_ERR_OK) {
    /* 计算实际供电电压 */
    float vdd = (3.3f * 4095.0f) / (float)vref_value;
    printf("VDD: %.3f V\n", vdd);
}
```

---

## 6. 注意事项

### 6.1 硬件限制
| 项目 | 说明 |
|------|------|
| ADC0 | 支持通道 0-17（包括内部温度 CH16、参考电压 CH17） |
| ADC1 | 支持通道 0-17（无内部通道） |
| ADC2 | 支持通道 0-16（无 CH17） |
| ADC 时钟 | 不得超过 14MHz（需在 RCU 层配置分频） |

### 6.2 使用限制
| 限制 | 说明 |
|------|------|
| 中断上下文 | `drv_adc_init()` 和 `drv_adc_single_read()` 禁止在 ISR 中调用 |
| 内部通道使能 | 通过 disable→enable 序列使配置生效 |

### 6.3 线程安全
- 单端口不支持并发转换调用
- 多端口可同时使用（ADC0/1/2 独立）
- 可通过 `use_mutex=true` 启用互斥锁保护
- 互斥锁超时时间 1 秒，避免死锁

### 6.4 性能优化
| 建议 | 说明 |
|------|------|
| 采样时间 | 根据信号源阻抗选择，高阻抗信号使用更长采样时间 |
| 分辨率 | 高分辨率（12 位）转换时间更长 |
| DMA 轮询 | 定时读取场景推荐使用，几乎不占用 CPU |

---

## 7. 错误处理示例

```c
int ret = drv_adc_single_read(DRV_ADC0, DRV_ADC_CHANNEL_0,
                              DRV_ADC_SAMPLETIME_13POINT5, &adc_value);

switch (ret) {
    case DRV_ADC_ERR_OK:
        /* 成功 */
        break;
    case DRV_ADC_ERR_TIMEOUT:
        /* 超时，ADC 无响应 */
        break;
    case DRV_ADC_ERR_BUSY:
        /* ADC 忙，可能上一次转换未完成 */
        break;
    case DRV_ADC_ERR_NOT_INIT:
        /* ADC 未初始化 */
        drv_adc_init(&config);
        break;
    case DRV_ADC_ERR_INVALID_PARAM:
        /* 参数错误 */
        break;
    default:
        /* 其他错误 */
        break;
}
```

---

## 8. 参考文档

- GD32F50x 用户手册：ADC 章节
- GD32F50x 数据手册：ADC 电气特性
- GD32F50x 标准外设库：`gd32f50x_adc.h/c`
- 项目编程规范：`嵌入式C语言编程规范.md`

---

**文档版本**: V1.0
**编写日期**: 2026-05-19
**编写人**: 伍玉蛟 (wuyujiao@jimiiot.com)
**审核人**: 待审核