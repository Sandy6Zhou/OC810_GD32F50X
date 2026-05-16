/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       timer_driver.h
**文件描述：       Timer驱动模块头文件 (UPDATE中断功能)
**当前版本：       V1.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.05.07
*********************************************************************
** 功能描述：       1. 基于GD32标准库的轻量级Timer封装
**                 2. 提供基础定时功能（周期定时、单次定时）
**                 3. 仅提供UPDATE中断回调（溢出/下溢）
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
 * @note   回调在UPDATE中断上下文中执行（定时器溢出/下溢）
 *         中断标志已在驱动层清除，应用层无需再调用清除函数
 */
typedef void (*drv_timer_callback_t)(void);

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
 * @brief   注册Timer UPDATE中断回调函数
 * @param   timer_id Timer ID
 * @param   callback 回调函数指针
 * @return  DRV_TIMER_ERR_OK=成功，其他=失败（见drv_timer_err_e）
 * @note    回调在UPDATE中断中执行（定时器溢出/下溢）
 */
int32_t drv_timer_callback_register(drv_timer_id_e timer_id,
                                    drv_timer_callback_t callback);

/**
 * @brief   注销Timer UPDATE中断回调函数
 * @param   timer_id Timer ID
 * @return  DRV_TIMER_ERR_OK=成功，其他=失败（见drv_timer_err_e）
 */
int32_t drv_timer_callback_unregister(drv_timer_id_e timer_id);

/**
 * @brief   使能Timer UPDATE中断（同时配置NVIC）
 * @param   timer_id Timer ID
 * @param   nvic_priority NVIC中断优先级（0-15，数值越小优先级越高）
 * @return  DRV_TIMER_ERR_OK=成功，其他=失败（见drv_timer_err_e）
 * @note    1. 仅支持UPDATE中断（溢出/下溢）
 *          2. 自动配置NVIC中断，应用层无需再调用nvic_irq_enable
 */
int32_t drv_timer_int_enable(drv_timer_id_e timer_id, uint8_t nvic_priority);

/**
 * @brief   禁能Timer UPDATE中断
 * @param   timer_id Timer ID
 * @return  DRV_TIMER_ERR_OK=成功，其他=失败（见drv_timer_err_e）
 */
int32_t drv_timer_int_disable(drv_timer_id_e timer_id);

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
 * 中断处理宏定义
 *
 * 设计说明：
 *   1. gd32f50x_it.c 中的 ISR 检查中断标志后调用此宏
 *   2. 宏内部调用驱动层函数判断是否执行回调
 *   3. 保持标准项目架构，驱动层职责单一
 *********************************************************************/

/**
 * @brief   执行 Timer UPDATE 中断回调函数
 * @param   timer_id Timer ID
 * @note    此函数由 gd32f50x_it.c 中的 ISR 直接调用
 *          内部检查回调有效性后执行
 */
void drv_timer_run_update_callback(drv_timer_id_e timer_id);

#endif /* __TIMER_DRIVER_H__ */
