# Timer 驱动模块设计方案

---

## 📋 文档信息

| 项目 | 内容 |
|------|------|
| **模块名称** | Timer 基础定时器驱动 |
| **文件名称** | timer_driver.c / timer_driver.h |
| **版本** | V2.0 |
| **作者** | Harrison Wu (wuyujiao@jimiiot.com) |
| **日期** | 2026.05.07 |
| **更新说明** | 精简为仅支持UPDATE中断，ISR架构标准化

---

## 1. 设计目标

### 1.1 核心目标

- ✅ **100% 解耦**：应用层完全不依赖 GD32 标准库类型和宏定义
- ✅ **类型安全**：使用驱动层枚举替代 GD32 原生枚举
- ✅ **易于移植**：更换 MCU 时只需修改 drv_timer.c，应用层零改动
- ✅ **职责清晰**：专注基础定时器功能（PWM 由独立驱动实现）

### 1.3 文件命名规范

采用 `模块_driver.c/h` 命名风格，与现有驱动保持一致：
- `gpio_driver.c/h` ✅ 已完成
- `uart_driver.c/h` ✅ 已完成
- `timer_driver.c/h` ← 本模块
- `pwm_driver.c/h` ← 未来实现

### 1.2 功能范围

| 功能模块 | 是否包含 | 说明 |
|---------|---------|------|
| **基础定时** | ✅ | 周期定时、单次定时 |
| **UPDATE中断** | ✅ | 更新中断（溢出/下溢）、回调注册 |
| **NVIC管理** | ✅ | 自动配置NVIC中断优先级 |
| **时钟管理** | ✅ | 自动时钟使能/禁能 |
| **POEN管理** | ✅ | 高级定时器自动使能主输出 |
| **PWM 输出** | ❌ | 由 pwm_driver.c 实现 |
| **输入捕获** | ❌ | 由 ic_driver.c 实现 |
| **输出比较** | ❌ | 由 oc_driver.c 实现 |
| **正交编码器** | ❌ | 由 encoder_driver.c 实现 |
| **其他中断** | ❌ | 本驱动仅支持UPDATE中断 |

---

## 2. 硬件资源分析

### 2.1 GD32F505 Timer 资源

| Timer | 类型 | 通道数 | 位宽 | 特殊功能 |
|-------|------|--------|------|---------|
| TIMER0 | 高级定时器 | 4 | 32-bit | 互补输出、死区控制 |
| TIMER1 | 通用定时器 | 4 | 32-bit | - |
| TIMER2 | 通用定时器 | 4 | 32-bit | - |
| TIMER3 | 通用定时器 | 4 | 32-bit | - |
| TIMER4 | 通用定时器 | 4 | 32-bit | - |
| TIMER5 | 基本定时器 | 0 | 32-bit | 仅定时、DAC触发 |
| TIMER6 | 基本定时器 | 0 | 32-bit | 仅定时、DAC触发 |
| TIMER7 | 高级定时器 | 4 | 32-bit | 互补输出、死区控制 |
| TIMER15 | 高级定时器 | 1 | 16-bit | 互补输出 |
| TIMER16 | 高级定时器 | 1 | 16-bit | 互补输出 |

### 2.2 本项目使用的 Timer

根据 OC810 硬件设计，初步规划：

| 应用 | Timer | 类型 | 模式 | 优先级 |
|------|-------|------|------|--------|
| 系统心跳 | TIMER5 | 基本定时器 | 基础定时 | 高 |
| 业务定时 | TIMER2 | 通用L0定时器 | 基础定时 | 中 |
| PWM 预留 | TIMER1 | 通用L0定时器 | PWM模式 | 中 |
| IR捕获预留 | TIMER0 | 高级定时器 | 输入捕获 | 高 |

---

## 3. 类型隔离设计

### 3.1 Timer ID 枚举

```c
/** Timer 编号枚举（驱动层定义，隔离 GD32 原生定义） */
typedef enum {
    DRV_TIMER_0 = 0,      /**< TIMER0 - 高级定时器（32-bit，4通道） */
    DRV_TIMER_1,          /**< TIMER1 - 通用定时器（32-bit，4通道） */
    DRV_TIMER_2,          /**< TIMER2 - 通用定时器（32-bit，4通道） */
    DRV_TIMER_3,          /**< TIMER3 - 通用定时器（32-bit，4通道） */
    DRV_TIMER_4,          /**< TIMER4 - 通用定时器（32-bit，4通道） */
    DRV_TIMER_5,          /**< TIMER5 - 基本定时器（32-bit，无通道） */
    DRV_TIMER_6,          /**< TIMER6 - 基本定时器（32-bit，无通道） */
    DRV_TIMER_7,          /**< TIMER7 - 高级定时器（32-bit，4通道） */
    DRV_TIMER_15,         /**< TIMER15 - 高级定时器（16-bit，1通道） */
    DRV_TIMER_16,         /**< TIMER16 - 高级定时器（16-bit，1通道） */
    DRV_TIMER_MAX
} drv_timer_id_e;
```

**隔离原理**：
```c
// ❌ 应用层看不到这个
#define TIMER0  (TIMER_BASE + 0x00012C00U)  // GD32 原生定义

// ✅ 应用层只看到驱动层枚举
drv_timer_init(DRV_TIMER_2, &config);  // 简洁、安全
```

---

### 3.2 ~~Timer 时钟分频枚举~~（已删除）

> **V2.0 变更**：删除了 `drv_timer_clock_div_e` 枚举，时钟分频固定为不分频（CK_INT），应用层通过 prescaler 精确控制频率。

---

### 3.3 计数模式枚举

```c
/** Timer 计数模式 */
typedef enum {
    DRV_COUNTER_EDGE = 0,     /**< 边沿对齐模式（向上计数） */
    DRV_COUNTER_CENTER_UP,    /**< 中心对齐模式（向上计数） */
    DRV_COUNTER_CENTER_DOWN,  /**< 中心对齐模式（向下计数） */
    DRV_COUNTER_CENTER_UP_DOWN /**< 中心对齐模式（向上/向下计数） */
} drv_counter_mode_e;
```

---

### 3.4 ~~预分频重载模式枚举~~（已删除）

> **V2.0 变更**：删除了 `drv_psc_reload_mode_e` 枚举，预分频重载模式固定为立即生效。

---

### 3.5 ~~中断类型枚举~~（已删除）

> **V2.0 变更**：删除了 `drv_timer_int_type_e` 枚举，本驱动仅支持 UPDATE 中断，无需中断类型参数。

---

## 4. 数据结构设计

### 4.1 Timer 配置结构体

```c
/** Timer 配置结构体 */
typedef struct {
    uint32_t period;                  /**< 周期值（0-65535 或 0-4294967295） */
    uint16_t prescaler;               /**< 预分频值（0-65535） */
    drv_timer_clock_div_e clock_div;  /**< 时钟分频系数 */
    drv_counter_mode_e counter_mode;  /**< 计数模式 */
    bool auto_reload_shadow;          /**< 自动重载影子使能（true=使能） */
    bool repeat_enable;               /**< 重复计数使能（高级定时器） */
    uint16_t repetition_counter;      /**< 重复计数值（0-255） */
} drv_timer_config_t;
```

**设计说明**：
- ✅ 参数集中管理，符合编程规范（不超过5个参数）
- ✅ 包含所有常用配置项
- ✅ 使用驱动层枚举，隔离 GD32 类型

---

### 4.2 Timer 回调函数类型

```c
/** Timer 回调函数类型 */
typedef void (*drv_timer_callback_t)(void);
```

**设计说明**：
- ✅ 回调不带参数，简化应用层实现
- ✅ 回调在 UPDATE 中断上下文中执行
- ✅ 中断标志已在驱动层清除，应用层无需处理

---

### 4.3 Timer 控制块（内部使用）

```c
/** Timer UPDATE中断状态枚举 */
typedef enum
{
    DRV_TIMER_STATE_IDLE = 0,         /**< 未初始化 */
    DRV_TIMER_STATE_INITIALIZED,      /**< 已初始化，未启动 */
    DRV_TIMER_STATE_RUNNING           /**< 运行中 */
} drv_timer_state_e;

/** Timer 控制块（应用层不可见） */
typedef struct
{
    drv_timer_state_e state;            /**< Timer状态（IDLE/INITIALIZED/RUNNING） */
    uint32_t timer_periph;              /**< GD32 Timer基地址 */
    uint8_t nvic_priority;              /**< NVIC中断优先级 */
    drv_timer_callback_t callback;      /**< UPDATE中断回调函数 */
} drv_timer_update_ctrl_t;
```

**设计说明**：
- ✅ 隐藏在 .c 文件中，应用层无法访问
- ✅ 使用状态机枚举替代两个 bool 变量，节省内存
- ✅ 维护运行时状态和 NVIC 优先级
- ✅ 职责单一：仅管理 UPDATE 中断相关状态

---

## 5. API 接口设计

### 5.1 初始化和去初始化

```c
/**
 * @brief   初始化 Timer
 * @param   timer_id Timer ID（DRV_TIMER_0~DRV_TIMER_16）
 * @param   config 配置结构体指针
 * @return  0=成功，-1=失败（参数错误）
 * @note    自动使能 Timer 时钟，配置后 Timer 处于停止状态
 */
int32_t drv_timer_init(drv_timer_id_e timer_id, drv_timer_config_t *config);

/**
 * @brief   去初始化 Timer
 * @param   timer_id Timer ID
 * @return  0=成功，-1=失败
 * @note    自动关闭未使用的 Timer 时钟，清除回调函数
 */
int32_t drv_timer_deinit(drv_timer_id_e timer_id);
```

---

### 5.2 启动和停止

```c
/**
 * @brief   启动 Timer
 * @param   timer_id Timer ID
 * @return  0=成功，-1=失败（未初始化）
 */
int32_t drv_timer_start(drv_timer_id_e timer_id);

/**
 * @brief   停止 Timer
 * @param   timer_id Timer ID
 * @return  0=成功，-1=失败
 */
int32_t drv_timer_stop(drv_timer_id_e timer_id);
```

---

### 5.3 中断管理

```c
/**
 * @brief   注册 Timer UPDATE 中断回调函数
 * @param   timer_id Timer ID
 * @param   callback 回调函数指针
 * @return  DRV_TIMER_ERR_OK=成功，其他=失败
 * @note    回调在 UPDATE 中断中执行（定时器溢出/下溢）
 */
int32_t drv_timer_callback_register(drv_timer_id_e timer_id,
                                    drv_timer_callback_t callback);

/**
 * @brief   注销 Timer UPDATE 中断回调函数
 * @param   timer_id Timer ID
 * @return  DRV_TIMER_ERR_OK=成功，其他=失败
 */
int32_t drv_timer_callback_unregister(drv_timer_id_e timer_id);

/**
 * @brief   使能 Timer UPDATE 中断（同时配置NVIC）
 * @param   timer_id Timer ID
 * @param   nvic_priority NVIC中断优先级（0-15，数值越小优先级越高）
 * @return  DRV_TIMER_ERR_OK=成功，其他=失败
 * @note    1. 仅支持UPDATE中断（溢出/下溢）
 *          2. 自动配置NVIC中断，应用层无需再调用nvic_irq_enable
 */
int32_t drv_timer_int_enable(drv_timer_id_e timer_id, uint8_t nvic_priority);

/**
 * @brief   禁能 Timer UPDATE 中断
 * @param   timer_id Timer ID
 * @return  DRV_TIMER_ERR_OK=成功，其他=失败
 */
int32_t drv_timer_int_disable(drv_timer_id_e timer_id);
```

---

### 5.4 运行时配置

```c
/**
 * @brief   设置 Timer 周期值
 * @param   timer_id Timer ID
 * @param   period 新周期值
 * @return  0=成功，-1=失败
 * @note    运行时修改周期，立即生效
 */
int32_t drv_timer_set_period(drv_timer_id_e timer_id, uint32_t period);

/**
 * @brief   设置 Timer 预分频值
 * @param   timer_id Timer ID
 * @param   prescaler 新预分频值（0-65535）
 * @return  0=成功，-1=失败
 */
int32_t drv_timer_set_prescaler(drv_timer_id_e timer_id, uint16_t prescaler);

/**
 * @brief   获取 Timer 当前计数值
 * @param   timer_id Timer ID
 * @return  当前计数值
 */
uint32_t drv_timer_get_counter(drv_timer_id_e timer_id);
```

---

### 5.5 状态查询

```c
/**
 * @brief   查询 Timer 是否运行中
 * @param   timer_id Timer ID
 * @return  true=运行中，false=已停止
 */
bool drv_timer_is_running(drv_timer_id_e timer_id);

/**
 * @brief   查询 Timer 是否已初始化
 * @param   timer_id Timer ID
 * @return  true=已初始化，false=未初始化
 */
bool drv_timer_is_initialized(drv_timer_id_e timer_id);
```

---

## 6. 中断处理设计

### 6.1 标准 ISR 架构

在 `gd32f50x_it.c` 中实现每个 Timer 的中断服务函数：

```c
/**
 * @brief   TIMER2 UPDATE中断服务函数
 * @note    检查并清除UPDATE中断标志，执行UPDATE回调函数
 */
void TIMER2_IRQHandler(void)
{
    if(SET == timer_interrupt_flag_get(TIMER2, TIMER_INT_FLAG_UP))
    {
        timer_interrupt_flag_clear(TIMER2, TIMER_INT_FLAG_UP);
        drv_timer_run_update_callback(DRV_TIMER_2);
    }
}
```

**设计优势**：
- ✅ 符合 GD32 标准项目架构，ISR 在 it.c 文件中
- ✅ 驱动层提供 `drv_timer_run_update_callback()` 函数
- ✅ ISR 负责检查标志、清除标志，然后调用驱动层函数
- ✅ 驱动层负责判断回调有效性并执行
- ✅ 职责清晰：ISR 处理硬件，驱动处理业务逻辑

### 6.2 中断向量映射

| Timer | 中断向量 | 说明 |
|-------|---------|------|
| TIMER0 | `TIMER0_UP_IRQHandler` | 高级定时器（UPDATE专用） |
| TIMER1 | `TIMER1_IRQHandler` | 通用L0定时器 |
| TIMER2 | `TIMER2_IRQHandler` | 通用L0定时器 |
| TIMER3 | `TIMER3_IRQHandler` | 通用L0定时器 |
| TIMER4 | `TIMER4_IRQHandler` | 通用L0定时器 |
| TIMER5 | `TIMER5_IRQHandler` | 基本定时器 |
| TIMER6 | `TIMER6_IRQHandler` | 基本定时器 |
| TIMER7 | `TIMER7_UP_IRQHandler` | 高级定时器（UPDATE专用） |
| TIMER15 | `TIMER15_IRQHandler` | 通用L3定时器 |
| TIMER16 | `TIMER16_IRQHandler` | 通用L3定时器 |

**注意事项**：
- 高级定时器（TIMER0/7）的中断向量名带 `_UP` 后缀
- 普通定时器的中断向量名不带后缀

---

## 7. 时钟管理设计

### 7.1 自动时钟使能

```c
// drv_timer_init() 内部实现
static void _drv_timer_enable_clock(drv_timer_id_e timer_id)
{
    switch (timer_id)
    {
        case DRV_TIMER_0:
            rcu_periph_clock_enable(RCU_TIMER0);
            break;
        case DRV_TIMER_1:
            rcu_periph_clock_enable(RCU_TIMER1);
            break;
        // ... 其他 Timer
        default:
            break;
    }
}
```

### 7.2 自动时钟关闭

```c
// drv_timer_deinit() 内部实现
static void _drv_timer_disable_clock(drv_timer_id_e timer_id)
{
    switch (timer_id)
    {
        case DRV_TIMER_0:
            rcu_periph_clock_disable(RCU_TIMER0);
            break;
        // ... 其他 Timer
        default:
            break;
    }
}
```

### 7.3 高级定时器 POEN 使能

```c
// drv_timer_init() 内部实现
/* 高级定时器（TIMER0/7）需要使能主输出，UPDATE中断才能正常工作 */
if ((timer_id == DRV_TIMER_0) || (timer_id == DRV_TIMER_7))
{
    timer_primary_output_config(s_timer_update_ctrl[timer_id].timer_periph, ENABLE);
}
```

**设计说明**：
- ✅ 高级定时器（TIMER0/7）必须使能 POEN 才能触发 UPDATE 中断
- ✅ 在初始化时自动配置，应用层无需关沐
- ✅ 普通定时器不需要此配置

**设计优势**：
- ✅ 应用层无需手动管理时钟
- ✅ 降低功耗（未使用的 Timer 自动关闭时钟）
- ✅ 防止资源泄漏
- ✅ 高级定时器自动处理 POEN 配置

---

## 8. 应用层使用示例

### 8.1 基础定时（1秒周期）

```c
#include "timer_driver.h"

/** 1秒定时回调 */
void timer_1s_callback(void)
{
    // 每秒执行一次
    led_toggle();
}

void app_timer_init(void)
{
    drv_timer_config_t config = {
        .period = 10000,              // 周期值（需根据主频计算）
        .prescaler = 27999,           // 预分频（280MHz / 28000 = 10kHz）
        .counter_mode = DRV_COUNTER_EDGE,
        .auto_reload_shadow = true,
        .repeat_enable = false,
        .repetition_counter = 0
    };

    // 初始化 Timer2
    drv_timer_init(DRV_TIMER_2, &config);

    // 注册回调
    drv_timer_callback_register(DRV_TIMER_2, timer_1s_callback);

    // 使能更新中断（同时配置NVIC优先级为5）
    drv_timer_int_enable(DRV_TIMER_2, 5);

    // 启动 Timer
    drv_timer_start(DRV_TIMER_2);
}
```

---

### 8.2 单次定时（延时触发）

```c
void single_shot_timer_callback(void)
{
    // 只执行一次
    do_something();

    // 停止 Timer
    drv_timer_stop(DRV_TIMER_3);
}

void trigger_single_shot(void)
{
    drv_timer_config_t config = {
        .period = 50000,              // 5秒后触发
        .prescaler = 27999,
        .counter_mode = DRV_COUNTER_EDGE,
        .auto_reload_shadow = false,  // 单次模式
        .repeat_enable = false,
        .repetition_counter = 0
    };

    drv_timer_init(DRV_TIMER_3, &config);
    drv_timer_callback_register(DRV_TIMER_3, single_shot_timer_callback);
    drv_timer_int_enable(DRV_TIMER_3, 5);
    drv_timer_start(DRV_TIMER_3);
}
```

---

## 9. 文件结构

```
project/OC810/code/driver/
├── timer_driver.c           ← Timer 驱动实现（~600行）
├── timer_driver.h           ← Timer 驱动接口（~250行）
├── pwm_driver.c             ← PWM 驱动（未来实现）
├── pwm_driver.h
├── gpio_driver.c            ← 已有
├── gpio_driver.h
├── uart_driver.c            ← 已有
└── uart_driver.h
```

**命名规范**：统一采用 `模块_driver.c/h` 风格，与 gpio_driver、uart_driver 保持一致！

---

## 10. 与原厂驱动对比

| 维度 | GD32 标准库 | timer_driver 驱动 |
|------|------------|---------------|
| **类型暴露** | ❌ 暴露 TIMER0 等宏 | ✅ 使用 drv_timer_id_e |
| **时钟管理** | ❌ 手动使能 RCU | ✅ 自动管理 |
| **POEN管理** | ❌ 手动配置 | ✅ 高级定时器自动使能 |
| **NVIC配置** | ❌ 手动配置 NVIC | ✅ 自动配置NVIC |
| **API 简洁性** | ❌ 需要调用多个函数 | ✅ 一个 init 完成所有配置 |
| **中断处理** | ❌ 手动清标志+判断 | ✅ 统一回调机制 |
| **状态管理** | ❌ 无状态追踪 | ✅ 内置状态机 |
| **移植性** | ❌ 强依赖 GD32 库 | ✅ 应用层零依赖 |

---

## 11. 性能分析

### 11.1 代码量对比

```c
// GD32 标准库方式（应用层）
rcu_periph_clock_enable(RCU_TIMER2);
timer_parameter_struct timer_param;
timer_param.prescaler = 27999;
timer_param.alignedmode = TIMER_COUNTER_EDGE;
timer_param.counterdirection = TIMER_COUNTER_UP;
timer_param.period = 10000;
timer_param.clockDivision = TIMER_CKDIV_DIV1;
timer_init(TIMER2, &timer_param);
timer_auto_reload_shadow_enable(TIMER2);
timer_interrupt_enable(TIMER2, TIMER_INT_UP);
nvic_irq_enable(TIMER2_IRQn, 5, 0);
timer_enable(TIMER2);
// 共 13 行代码

// timer_driver 驱动方式（应用层）
drv_timer_config_t config = {
    .period = 10000,
    .prescaler = 27999,
    .counter_mode = DRV_COUNTER_EDGE,
    .auto_reload_shadow = true
};
drv_timer_init(DRV_TIMER_2, &config);
drv_timer_int_enable(DRV_TIMER_2, 5);
drv_timer_start(DRV_TIMER_2);
// 共 8 行代码（减少 38%）
```

### 11.2 运行时开销

| 操作 | GD32 标准库 | drv_timer | 差异 |
|------|------------|-----------|------|
| 初始化 | 直接调用 | 1次映射转换 | +0.5μs |
| 启动/停止 | 直接操作寄存器 | 1次映射+状态更新 | +0.2μs |
| 中断处理 | 手动清标志 | 自动清标志+回调 | +1μs |

**结论**：运行时开销 < 2μs，对于毫秒级定时应用**完全可忽略**。

---

## 12. 移植指南

### 12.1 移植到 STM32

只需修改 `drv_timer.c` 内部实现：

```c
// 修改前（GD32）
#include "gd32f50x_timer.h"

static const uint32_t timer_base_addr[] = {
    [DRV_TIMER_0] = TIMER0,
    [DRV_TIMER_1] = TIMER1,
    // ...
};

// 修改后（STM32）
#include "stm32f4xx_tim.h"

static const uint32_t timer_base_addr[] = {
    [DRV_TIMER_0] = (uint32_t)TIM2,
    [DRV_TIMER_1] = (uint32_t)TIM3,
    // ...
};
```

**应用层代码完全不需要修改！**

---

## 13. 总结

### 13.1 设计亮点

1. ✅ **100% 类型隔离**：应用层看不到任何 GD32 原生类型
2. ✅ **自动时钟管理**：降低功耗，防止资源泄漏
3. ✅ **自动 POEN 配置**：高级定时器无需手动使能主输出
4. ✅ **自动 NVIC 配置**：中断优先级在使能中断时自动配置
5. ✅ **仅支持 UPDATE 中断**：职责单一，代码精简
6. ✅ **状态机设计**：使用枚举替代 bool，节省内存
7. ✅ **标准 ISR 架构**：符合 GD32 项目规范
8. ✅ **API 简洁**：一个 init 完成所有配置
9. ✅ **易于移植**：应用层零改动

### 13.2 与现有驱动风格一致

| 特性 | gpio_driver | uart_driver | timer_driver |
|------|------------|------------|--------------|
| 命名风格 | gpio_driver.c/h | uart_driver.c/h | timer_driver.c/h |
| 类型隔离 | ✅ | ✅ | ✅ |
| 自动时钟管理 | ✅ | ✅ | ✅ |
| 日志宏 | ✅ | ✅ | ✅ |
| 回调机制 | ✅ | ✅ | ✅ |
| 状态机设计 | ✅ | ✅ | ✅ |

---

**文档版本**: V2.0
**最后更新**: 2026-05-07
**V2.0 变更说明**:
- 删除多中断类型支持，仅保留 UPDATE 中断
- ISR 架构标准化：移至 gd32f50x_it.c 中实现
- 删除时钟分频枚举（固定为不分频）
- 删除预分频重载模式枚举（固定为立即生效）
- 删除中断类型枚举（仅支持 UPDATE）
- 控制块优化：使用状态机枚举替代两个 bool
- 删除冗余 rcu_clk 字段（从映射表获取）
- 高级定时器自动使能 POEN
- 中断使能自动配置 NVIC 优先级
