/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       timer_driver.h
**文件描述：       Timer驱动模块头文件 (基础定时器功能)
**当前版本：       V1.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.05.07
*********************************************************************
** 功能描述：       1. 基于GD32标准库的轻量级Timer封装
**                 2. 提供基础定时功能（周期定时、单次定时）
**                 3. 提供中断回调注册和统一处理
**                 4. 提供智能时钟管理（deinit自动关闭未使用时钟）
**                 5. 使用驱动层类型枚举，实现与GD32库类型隔离
*********************************************************************/

#ifndef __TIMER_DRIVER_H__
#define __TIMER_DRIVER_H__

#include "gd32f50x_timer.h"
#include "gd32f50x_rcu.h"
#include <stdbool.h>
#include <stdint.h>

/*********************************************************************
 * 日志宏定义（便于移植和独立控制）
 *********************************************************************/

/* 日志开关（1=开启，0=关闭） */
#define DRV_TIMER_LOG_ENABLE      (1U)  /* 驱动日志使能 */

/* 日志级别定义 */
#define DRV_TIMER_LOG_LEVEL_ERROR        (0U)    /**< 错误日志 */
#define DRV_TIMER_LOG_LEVEL_WARN         (1U)    /**< 警告日志 */
#define DRV_TIMER_LOG_LEVEL_INFO         (2U)    /**< 信息日志 */
#define DRV_TIMER_LOG_LEVEL_DEBUG        (3U)    /**< 调试日志 */

/* 当前日志级别（可通过修改此值控制日志输出详细程度） */
#define DRV_TIMER_LOG_CURRENT_LEVEL      (DRV_TIMER_LOG_LEVEL_INFO)

/* 日志输出宏（可根据项目实际情况修改底层实现） */
#if DRV_TIMER_LOG_ENABLE == 1U

/* 根据项目实际使用的日志系统修改此处 */
#include "my_log.h"

#define DRV_TIMER_LOGE(fmt, ...)    MY_LOG_E("[TIMER] " fmt, ##__VA_ARGS__)
#define DRV_TIMER_LOGW(fmt, ...)    MY_LOG_W("[TIMER] " fmt, ##__VA_ARGS__)
#define DRV_TIMER_LOGI(fmt, ...)    MY_LOG_I("[TIMER] " fmt, ##__VA_ARGS__)
#define DRV_TIMER_LOGD(fmt, ...)    do { \
                                        if (DRV_TIMER_LOG_CURRENT_LEVEL >= DRV_TIMER_LOG_LEVEL_DEBUG) \
                                        { \
                                            MY_LOG_D("[TIMER] " fmt, ##__VA_ARGS__); \
                                        } \
                                    } while(0)

#else

#define DRV_TIMER_LOGE(fmt, ...)
#define DRV_TIMER_LOGW(fmt, ...)
#define DRV_TIMER_LOGI(fmt, ...)
#define DRV_TIMER_LOGD(fmt, ...)

#endif /* DRV_TIMER_LOG_ENABLE */

/*********************************************************************
 * 硬件相关宏定义（便于移植到不同芯片）
 *********************************************************************/

/*********************************************************************
 * 错误码定义
 *********************************************************************/

/**
 * @brief  Timer驱动错误码
 */
typedef enum
{
    DRV_TIMER_ERR_OK = 0,             /**< 成功 */
    DRV_TIMER_ERR_INVALID_ID,         /**< 无效的Timer ID */
    DRV_TIMER_ERR_NOT_INITIALIZED,    /**< Timer未初始化 */
    DRV_TIMER_ERR_UNSUPPORTED,        /**< 不支持的功能（如非高级定时器使用刹车中断） */
    DRV_TIMER_ERR_BUSY,               /**< Timer忙（已初始化或运行中） */
    DRV_TIMER_ERR_INVALID_PARAM       /**< 无效的参数 */
} drv_timer_err_e;

/*********************************************************************
 * 枚举类型定义
 *********************************************************************/

/**
 * @brief  驱动层Timer ID枚举
 * @note   实现与GD32库TIMER0/TIMER1等宏的类型隔离
 */
typedef enum
{
    DRV_TIMER_0 = 0,          /**< TIMER0 - 高级定时器（16-bit，4独立通道+2互补通道，三相PWM多路复用（6通道），可编程死区时间生成,电机控制专用，可作电机专用，可作为完整通用定时器使用，编码器接口（正交解码） */
    DRV_TIMER_1,              /**< TIMER1 - 通用L0定时器（32-bit，4独立通道，输入捕获/输出比较，PWM生成（边沿/中心 对齐），单脉冲模式，编码器接口（正交解码），外部信号同步） */
    DRV_TIMER_2,              /**< TIMER2 - 通用L0定时器（16-bit，4独立通道，输入捕获/输出比较，PWM生成（边沿/中心 对齐），单脉冲模式，编码器接口（正交解码），外部信号同步） */
    DRV_TIMER_3,              /**< TIMER3 - 通用L0定时器（16-bit，4独立通道，输入捕获/输出比较，PWM生成（边沿/中心 对齐），单脉冲模式，编码器接口（正交解码），外部信号同步） */
    DRV_TIMER_4,              /**< TIMER4 - 通用L0定时器（16-bit，4独立通道，输入捕获/输出比较，PWM生成（边沿/中心 对齐），单脉冲模式，编码器接口（正交解码），外部信号同步） */
    DRV_TIMER_5,              /**< TIMER5 - 基本定时器（16-bit，无通道，仅支持定时功能，TRGO连接DAC（通过TRIGSEL模块） */
    DRV_TIMER_6,              /**< TIMER6 - 基本定时器（16-bit，无通道，仅支持定时功能，TRGO连接DAC（通过TRIGSEL模块） */
    DRV_TIMER_7,              /**< TIMER7 - 高级定时器（16-bit，4独立通道+2互补通道，三相PWM多路复用（6通道），可编程死区时间生成,电机控制专用，可作电机专用，可作为完整通用定时器使用，编码器接口（正交解码） */
    DRV_TIMER_15,             /**< TIMER15 - 通用L3定时器（16-bit，3通道，BREAK中止输入，可编程死区时间，CH0N互补输出，重复计数器，PWM生成，电机控制/电源管理） */
    DRV_TIMER_16,             /**< TIMER16 - 通用L3定时器（16-bit，3通道，BREAK中止输入，可编程死区时间，CH0N互补输出，重复计数器，PWM生成，电机控制/电源管理） */
    DRV_TIMER_MAX
} drv_timer_id_e;

/**
 * @brief  Timer计数模式
 */
typedef enum
{
    DRV_COUNTER_EDGE = 0,     /**< 边沿对齐模式（向上计数） */
    DRV_COUNTER_CENTER_UP,    /**< 中心对齐模式（向上计数） */
    DRV_COUNTER_CENTER_DOWN,  /**< 中心对齐模式（向下计数） */
    DRV_COUNTER_CENTER_UP_DOWN /**< 中心对齐模式（向上/向下计数） */
} drv_counter_mode_e;

/**
 * @brief  Timer类型
 */
typedef enum
{
    DRV_TIMER_TYPE_ADVANCED = 0,    /**< 高级定时器（TIMER0/7） */
    DRV_TIMER_TYPE_GENERAL_L0,      /**< 通用L0定时器（TIMER1/2/3/4） */
    DRV_TIMER_TYPE_GENERAL_L3,      /**< 通用L3定时器（TIMER15/16） */
    DRV_TIMER_TYPE_BASIC            /**< 基本定时器（TIMER5/6） */
} drv_timer_type_e;

/**
 * @brief  Timer能力标志
 */
typedef struct
{
    drv_timer_type_e type;          /**< Timer类型 */
    uint8_t bit_width;              /**< 位宽（16或32） */
    uint8_t channel_count;          /**< 通道数 */
    bool has_complementary;         /**< 是否有互补输出 */
    bool has_break;                 /**< 是否有刹车功能 */
    bool has_encoder;               /**< 是否有编码器接口 */
} drv_timer_capability_t;

/**
 * @brief  Timer中断类型
 * @note   支持所有Timer中断类型，包括输入捕获、输出比较、刹车等
 */
typedef enum
{
    DRV_TIMER_INT_UPDATE = TIMER_INT_UP,     /**< 更新中断（溢出/下溢） */
    DRV_TIMER_INT_CH0 = TIMER_INT_CH0,       /**< 通道0中断（输入捕获/输出比较） */
    DRV_TIMER_INT_CH1 = TIMER_INT_CH1,       /**< 通道1中断（输入捕获/输出比较） */
    DRV_TIMER_INT_CH2 = TIMER_INT_CH2,       /**< 通道2中断（输入捕获/输出比较） */
    DRV_TIMER_INT_CH3 = TIMER_INT_CH3,       /**< 通道3中断（输入捕获/输出比较） */
    DRV_TIMER_INT_TRIGGER = TIMER_INT_TRG,   /**< 触发中断（外部触发/内部触发） */
    DRV_TIMER_INT_BREAK = TIMER_INT_BRK      /**< 刹车中断（高级定时器专用） */
} drv_timer_int_type_e;

/*********************************************************************
 * 数据结构定义
 *********************************************************************/

/**
 * @brief  Timer配置结构体
 */
typedef struct
{
    uint32_t period;                      /**< 周期值（0-65535或0-4294967295） */
    uint16_t prescaler;                   /**< 预分频值（0-65535） */
    drv_counter_mode_e counter_mode;      /**< 计数模式 */
    bool auto_reload_shadow;              /**< 自动重载影子使能（true=使能） */
    bool repeat_enable;                   /**< 重复计数使能（高级定时器） */
    uint16_t repetition_counter;          /**< 重复计数值（0-255） */
} drv_timer_config_t;

/**
 * @brief  Timer回调函数类型
 * @param  intf_raw   INTF寄存器原始值（驱动层已清除所有标志，应用层只需判断无需清除）
 * @note   应用层通过 intf_raw 位掩码判断具体中断类型：
 *         - (intf_raw & TIMER_INT_FLAG_UP)  -> 更新中断
 *         - (intf_raw & TIMER_INT_FLAG_CH0) -> 通道0中断
 *         - (intf_raw & TIMER_INT_FLAG_BRK) -> 刹车中断
 *         - 其他标志见 GD32 标准库 TIMER_INT_FLAG_xx 定义
 *         驱动层已清除所有中断标志，应用层无需再调用清除函数
 */
typedef void (*drv_timer_callback_t)(uint32_t intf_raw);

/*********************************************************************
 * 初始化和去初始化接口
 *********************************************************************/

/**
 * @brief   初始化Timer
 * @param   timer_id Timer ID（DRV_TIMER_0~DRV_TIMER_16）
 * @param   config 配置结构体指针
 * @return  DRV_TIMER_ERR_OK=成功，其他=失败（见drv_timer_err_e）
 * @note    自动使能Timer时钟，配置后Timer处于停止状态
 * @example
 * @code
 * drv_timer_config_t config = {
 *     .period = 10000,
 *     .prescaler = 27999,
 *     .counter_mode = DRV_COUNTER_EDGE,
 *     .auto_reload_shadow = true,
 *     .repeat_enable = false,
 *     .repetition_counter = 0
 * };
 * drv_timer_init(DRV_TIMER_2, &config);
 * @endcode
 */
int32_t drv_timer_init(drv_timer_id_e timer_id, drv_timer_config_t *config);

/**
 * @brief   去初始化Timer
 * @param   timer_id Timer ID
 * @return  DRV_TIMER_ERR_OK=成功，其他=失败（见drv_timer_err_e）
 * @note    自动关闭Timer时钟，清除回调函数
 */
int32_t drv_timer_deinit(drv_timer_id_e timer_id);

/*********************************************************************
 * 启动和停止接口
 *********************************************************************/

/**
 * @brief   启动Timer
 * @param   timer_id Timer ID
 * @return  DRV_TIMER_ERR_OK=成功，其他=失败（见drv_timer_err_e）
 */
int32_t drv_timer_start(drv_timer_id_e timer_id);

/**
 * @brief   停止Timer
 * @param   timer_id Timer ID
 * @return  DRV_TIMER_ERR_OK=成功，其他=失败（见drv_timer_err_e）
 */
int32_t drv_timer_stop(drv_timer_id_e timer_id);

/*********************************************************************
 * 中断管理接口
 *********************************************************************/

/**
 * @brief   注册Timer中断回调函数
 * @param   timer_id Timer ID
 * @param   callback 回调函数指针
 * @return  DRV_TIMER_ERR_OK=成功，其他=失败（见drv_timer_err_e）
 * @note    回调在中断中执行，int_type 参数指示触发来源（UPDATE/CH0/BRK 等）
 */
int32_t drv_timer_callback_register(drv_timer_id_e timer_id,
                                    drv_timer_callback_t callback);

/**
 * @brief   注销Timer中断回调函数
 * @param   timer_id Timer ID
 * @return  DRV_TIMER_ERR_OK=成功，其他=失败（见drv_timer_err_e）
 */
int32_t drv_timer_callback_unregister(drv_timer_id_e timer_id);

/**
 * @brief   使能Timer中断
 * @param   timer_id Timer ID
 * @param   int_type 中断类型
 * @return  DRV_TIMER_ERR_OK=成功，其他=失败（见drv_timer_err_e）
 */
int32_t drv_timer_int_enable(drv_timer_id_e timer_id,
                             drv_timer_int_type_e int_type);

/**
 * @brief   禁能Timer中断
 * @param   timer_id Timer ID
 * @param   int_type 中断类型
 * @return  DRV_TIMER_ERR_OK=成功，其他=失败（见drv_timer_err_e）
 */
int32_t drv_timer_int_disable(drv_timer_id_e timer_id,
                              drv_timer_int_type_e int_type);

/*********************************************************************
 * 运行时配置接口
 *********************************************************************/

/**
 * @brief   设置Timer周期值
 * @param   timer_id Timer ID
 * @param   period 新周期值
 * @return  DRV_TIMER_ERR_OK=成功，其他=失败（见drv_timer_err_e）
 * @note    运行时修改周期，立即生效
 */
int32_t drv_timer_set_period(drv_timer_id_e timer_id, uint32_t period);

/**
 * @brief   设置Timer预分频值
 * @param   timer_id Timer ID
 * @param   prescaler 新预分频值（0-65535）
 * @return  DRV_TIMER_ERR_OK=成功，其他=失败（见drv_timer_err_e）
 */
int32_t drv_timer_set_prescaler(drv_timer_id_e timer_id, uint16_t prescaler);

/**
 * @brief   获取Timer当前计数值
 * @param   timer_id Timer ID
 * @return  当前计数值
 */
uint32_t drv_timer_get_counter(drv_timer_id_e timer_id);

/*********************************************************************
 * 状态查询接口
 *********************************************************************/

/**
 * @brief   查询Timer是否运行中
 * @param   timer_id Timer ID
 * @return  true=运行中，false=已停止
 */
bool drv_timer_is_running(drv_timer_id_e timer_id);

/**
 * @brief   查询Timer是否已初始化
 * @param   timer_id Timer ID
 * @return  true=已初始化，false=未初始化
 */
bool drv_timer_is_initialized(drv_timer_id_e timer_id);

/*********************************************************************
 * Timer能力查询接口
 *********************************************************************/

/**
 * @brief   获取Timer能力信息
 * @param   timer_id Timer ID
 * @param   cap 能力信息输出指针
 * @return  DRV_TIMER_ERR_OK=成功，其他=失败
 * @note    应用层可通过此接口查询Timer的位宽、通道数、特性等
 * @example
 * @code
 * drv_timer_capability_t cap;
 * drv_timer_get_capability(DRV_TIMER_0, &cap);
 * // cap.type = DRV_TIMER_TYPE_ADVANCED
 * // cap.bit_width = 16
 * // cap.channel_count = 4
 * // cap.has_complementary = true
 * // cap.has_break = true
 * // cap.has_encoder = true
 * @endcode
 */
int32_t drv_timer_get_capability(drv_timer_id_e timer_id, drv_timer_capability_t *cap);

/*********************************************************************
 * 中断处理接口实现
 *
 * 设计说明：
 *   1. ISR 直接定义在驱动层（而非 gd32f50x_it.c），避免额外的函数调用开销
 *   2. 每个 Timer 独立 ISR，编译期确定 periph 和 timer_id，零查表开销
 *   3. 一次性读取 INTF 寄存器，一次性清除所有标志（W1C 机制）
 *   4. 回调函数接收 intf_raw 原始值，应用层自行判断具体中断类型
 *   5. 驱动层不判断优先级、不猜测应用层意图，职责单一
 *********************************************************************/

/*********************************************************************
 * @brief   TIMER0 中断服务函数
 *********************************************************************/
void TIMER0_IRQHandler(void);

/*********************************************************************
 * @brief   TIMER1 中断服务函数
 *********************************************************************/
void TIMER1_IRQHandler(void);

/*********************************************************************
 * @brief   TIMER2 中断服务函数
 *********************************************************************/
void TIMER2_IRQHandler(void);

/*********************************************************************
 * @brief   TIMER3 中断服务函数
 *********************************************************************/
void TIMER3_IRQHandler(void);

/*********************************************************************
 * @brief   TIMER4 中断服务函数
 *********************************************************************/
void TIMER4_IRQHandler(void);

/*********************************************************************
 * @brief   TIMER5 中断服务函数
 *********************************************************************/
void TIMER5_IRQHandler(void);

/*********************************************************************
 * @brief   TIMER6 中断服务函数
 *********************************************************************/
void TIMER6_IRQHandler(void);

/*********************************************************************
 * @brief   TIMER7 中断服务函数
 *********************************************************************/
void TIMER7_IRQHandler(void);

/*********************************************************************
 * @brief   TIMER15 中断服务函数
 *********************************************************************/
void TIMER15_IRQHandler(void);

/*********************************************************************
 * @brief   TIMER16 中断服务函数
 *********************************************************************/
void TIMER16_IRQHandler(void);

#endif /* __TIMER_DRIVER_H__ */
