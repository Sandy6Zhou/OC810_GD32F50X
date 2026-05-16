/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       gpio_driver.h
**文件描述：       GPIO驱动头文件 (轻量级封装层)
**当前版本：       V2.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.04.20
*********************************************************************
** 功能描述：       1. 基于GD32标准库的轻量级GPIO封装
**                 2. 提供EXTI中断回调注册和统一处理
**                 3. 提供智能电源管理（deinit自动关闭未使用时钟）
**                 4. 使用驱动层类型枚举，实现与GD32库类型隔离
*********************************************************************/

#ifndef __GPIO_DRIVER_H__
#define __GPIO_DRIVER_H__

#include "gd32f50x_gpio.h"
#include "gd32f50x_rcu.h"
#include "gd32f50x_exti.h"
#include <stdbool.h>
#include <stdint.h>

/*********************************************************************
 * 日志宏定义（便于移植和独立控制）
 *********************************************************************/

/* 日志开关（1=开启，0=关闭） */
#define DRV_GPIO_LOG_ENABLE      (1U)

/* 日志级别定义 */
#define DRV_GPIO_LOG_LEVEL_ERROR        (0U)    /**< 错误日志 */
#define DRV_GPIO_LOG_LEVEL_WARN         (1U)    /**< 警告日志 */
#define DRV_GPIO_LOG_LEVEL_INFO         (2U)    /**< 信息日志 */
#define DRV_GPIO_LOG_LEVEL_DEBUG        (3U)    /**< 调试日志 */

/* 当前日志级别（可通过修改此值控制日志输出详细程度） */
#define DRV_GPIO_LOG_CURRENT_LEVEL      (DRV_GPIO_LOG_LEVEL_INFO)

/* 日志输出宏（可根据项目实际情况修改底层实现） */
#if DRV_GPIO_LOG_ENABLE == 1U

/* 根据项目实际使用的日志系统修改此处 */
#include "my_log.h"

#define DRV_GPIO_LOGE(fmt, ...)    MY_LOG_E(fmt, ##__VA_ARGS__)
#define DRV_GPIO_LOGW(fmt, ...)    MY_LOG_W(fmt, ##__VA_ARGS__)
#define DRV_GPIO_LOGI(fmt, ...)    MY_LOG_I(fmt, ##__VA_ARGS__)
#define DRV_GPIO_LOGD(fmt, ...)    do { \
                                        if (DRV_GPIO_LOG_CURRENT_LEVEL >= DRV_GPIO_LOG_LEVEL_DEBUG) \
                                        { \
                                            MY_LOG_D(fmt, ##__VA_ARGS__); \
                                        } \
                                    } while(0)

#else

#define DRV_GPIO_LOGE(fmt, ...)
#define DRV_GPIO_LOGW(fmt, ...)
#define DRV_GPIO_LOGI(fmt, ...)
#define DRV_GPIO_LOGD(fmt, ...)

#endif /* DRV_GPIO_LOG_ENABLE */

/*********************************************************************
 * 错误码定义
 *********************************************************************/

#define DRV_GPIO_OK                (0)     /**< 成功 */
#define DRV_GPIO_ERR_FAILED       (-1)    /**< 通用失败 */
#define DRV_GPIO_ERR_INVALID_PORT (-2)    /**< 无效的GPIO端口 */
#define DRV_GPIO_ERR_INVALID_PIN  (-3)    /**< 无效的GPIO引脚 */
#define DRV_GPIO_ERR_NULL_PTR     (-4)    /**< 空指针参数 */
#define DRV_GPIO_ERR_NOT_INIT     (-5)    /**< 未初始化 */
#define DRV_GPIO_ERR_BUSY         (-6)    /**< 忙绿 */
#define DRV_GPIO_ERR_TIMEOUT      (-7)    /**< 超时 */

/*********************************************************************
 * 硬件相关宏定义（便于移植到不同芯片）
 *********************************************************************/

/** GPIO端口组数（GD32F505有5组：A/B/C/D/E） */
#define DRV_GPIO_PORT_COUNT      (5U)  /**< @deprecated 使用 DRV_GPIO_PORT_MAX 替代 */

/** 每组GPIO的引脚数量（16个：PIN_0~PIN_15） */
#define DRV_MAX_GPIO_PIN_PER_PORT    (16U)

/** EXTI线数量（16条：EXTI_0~EXTI_15） */
#define DRV_MAX_EXTI_LINE_COUNT      (16U)
/*********************************************************************
 * 枚举类型定义
 *********************************************************************/

/**
 * @brief  驱动层GPIO端口枚举
 * @note   使用独立索引值（0-4），不依赖GD32库基地址，实现完全类型隔离
 */
typedef enum
{
    DRV_GPIO_PORT_A = 0,       /**< GPIOA端口 */
    DRV_GPIO_PORT_B,           /**< GPIOB端口 */
    DRV_GPIO_PORT_C,           /**< GPIOC端口 */
    DRV_GPIO_PORT_D,           /**< GPIOD端口 */
    DRV_GPIO_PORT_E,           /**< GPIOE端口 */
    DRV_GPIO_PORT_MAX          /**< 端口数量上限，用于参数校验 */
} drv_gpio_port_e;

/**
 * @brief  驱动层引脚枚举
 * @note   值与GD32库GPIO_PIN_x宏一致，实现类型隔离
 */
typedef enum
{
    DRV_GPIO_PIN_0 = GPIO_PIN_0,       /**< 引脚0 */
    DRV_GPIO_PIN_1 = GPIO_PIN_1,       /**< 引脚1 */
    DRV_GPIO_PIN_2 = GPIO_PIN_2,       /**< 引脚2 */
    DRV_GPIO_PIN_3 = GPIO_PIN_3,       /**< 引脚3 */
    DRV_GPIO_PIN_4 = GPIO_PIN_4,       /**< 引脚4 */
    DRV_GPIO_PIN_5 = GPIO_PIN_5,       /**< 引脚5 */
    DRV_GPIO_PIN_6 = GPIO_PIN_6,       /**< 引脚6 */
    DRV_GPIO_PIN_7 = GPIO_PIN_7,       /**< 引脚7 */
    DRV_GPIO_PIN_8 = GPIO_PIN_8,       /**< 引脚8 */
    DRV_GPIO_PIN_9 = GPIO_PIN_9,       /**< 引脚9 */
    DRV_GPIO_PIN_10 = GPIO_PIN_10,     /**< 引脚10 */
    DRV_GPIO_PIN_11 = GPIO_PIN_11,     /**< 引脚11 */
    DRV_GPIO_PIN_12 = GPIO_PIN_12,     /**< 引脚12 */
    DRV_GPIO_PIN_13 = GPIO_PIN_13,     /**< 引脚13 */
    DRV_GPIO_PIN_14 = GPIO_PIN_14,     /**< 引脚14 */
    DRV_GPIO_PIN_15 = GPIO_PIN_15,     /**< 引脚15 */
    DRV_GPIO_PIN_ALL = GPIO_PIN_ALL    /**< 所有引脚 */
} drv_gpio_pin_e;

/**
 * @brief  驱动层GPIO复用功能枚举
 * @note   值与GD32库GPIO_AF_*宏一致，实现类型隔离
 * @note   具体AF映射关系请参考GD32F50x Datasheet中的"Alternate function mapping"表
 */
typedef enum
{
    DRV_GPIO_AF_0 = GPIO_AF_0,       /**< 复用功能0 */
    DRV_GPIO_AF_1 = GPIO_AF_1,       /**< 复用功能1 */
    DRV_GPIO_AF_2 = GPIO_AF_2,       /**< 复用功能2 */
    DRV_GPIO_AF_3 = GPIO_AF_3,       /**< 复用功能3 */
    DRV_GPIO_AF_4 = GPIO_AF_4,       /**< 复用功能4 */
    DRV_GPIO_AF_5 = GPIO_AF_5,       /**< 复用功能5 */
    DRV_GPIO_AF_6 = GPIO_AF_6,       /**< 复用功能6 */
    DRV_GPIO_AF_7 = GPIO_AF_7,       /**< 复用功能7 */
    DRV_GPIO_AF_8 = GPIO_AF_8        /**< 复用功能8 */
} drv_gpio_af_e;

/**
 * @brief  驱动层GPIO模式枚举
 * @note   值与GD32库GPIO_MODE_*宏一致，实现类型隔离
 */
typedef enum
{
    DRV_GPIO_MODE_OUTPUT = GPIO_MODE_OUTPUT,     /**< 输出模式 */
    DRV_GPIO_MODE_INPUT = GPIO_MODE_INPUT,       /**< 输入模式 */
    DRV_GPIO_MODE_AF = GPIO_MODE_AF,             /**< 复用功能模式 */
    DRV_GPIO_MODE_ANALOG = GPIO_MODE_ANALOG      /**< 模拟模式（ADC/DAC） */
} drv_gpio_mode_e;

/**
 * @brief  驱动层GPIO输出类型枚举
 * @note   值与GD32库GPIO_OTYPE_*宏一致，实现类型隔离
 */
typedef enum
{
    DRV_GPIO_OTYPE_PP = GPIO_OTYPE_PP,   /**< 推挽输出 */
    DRV_GPIO_OTYPE_OD = GPIO_OTYPE_OD    /**< 开漏输出 */
} drv_gpio_otype_e;

/**
 * @brief  驱动层GPIO速度枚举
 * @note   值与GD32库GPIO_OSPEED_*宏一致，实现类型隔离
 */
typedef enum
{
    DRV_GPIO_SPEED_LEVEL0 = GPIO_OSPEED_LEVEL0,  /**< 速度等级0 */
    DRV_GPIO_SPEED_LEVEL1 = GPIO_OSPEED_LEVEL1,  /**< 速度等级1 */
    DRV_GPIO_SPEED_LEVEL2 = GPIO_OSPEED_LEVEL2,  /**< 速度等级2 */
    DRV_GPIO_SPEED_LEVEL3 = GPIO_OSPEED_LEVEL3   /**< 速度等级3 */
} drv_gpio_speed_e;

/**
 * @brief  驱动层GPIO上下拉枚举
 * @note   值与GD32库GPIO_PUPD_*宏一致，实现类型隔离
 */
typedef enum
{
    DRV_GPIO_PUPD_NONE = GPIO_PUPD_NONE,         /**< 无上下拉 */
    DRV_GPIO_PUPD_PULLUP = GPIO_PUPD_PULLUP,     /**< 上拉 */
    DRV_GPIO_PUPD_PULLDOWN = GPIO_PUPD_PULLDOWN  /**< 下拉 */
} drv_gpio_pupd_e;

/**
 * @brief  驱动层EXTI中断模式枚举
 * @note   值与GD32库EXTI_INTERRUPT/EXTI_EVENT宏一致，实现类型隔离
 */
typedef enum
{
    DRV_EXTI_MODE_INTERRUPT = EXTI_INTERRUPT,    /**< 中断模式 */
    DRV_EXTI_MODE_EVENT = EXTI_EVENT             /**< 事件模式 */
} drv_exti_mode_e;

/**
 * @brief  驱动层EXTI触发方式枚举
 * @note   值与GD32库EXTI_TRIG_*宏一致，实现类型隔离
 */
typedef enum
{
    DRV_EXTI_TRIG_RISING = EXTI_TRIG_RISING,         /**< 上升沿触发 */
    DRV_EXTI_TRIG_FALLING = EXTI_TRIG_FALLING,       /**< 下降沿触发 */
    DRV_EXTI_TRIG_BOTH = EXTI_TRIG_BOTH              /**< 双边沿触发 */
} drv_exti_trig_e;

/*********************************************************************
 * 回调函数类型定义
 *********************************************************************/

/**
 * @brief  EXTI中断回调函数类型
 * @param  port 触发中断的GPIO端口
 * @param  pin 触发中断的引脚掩码
 * @note   在中断上下文中调用，必须快速执行，不能阻塞
 *********************************************************************/
typedef void (*drv_gpio_exti_callback_t)(drv_gpio_port_e port, uint32_t pin);

/*********************************************************************
 * 数据结构定义
 *********************************************************************/

/**
 * @brief  GPIO配置结构
 * @note   使用驱动层枚举类型，实现与GD32库的类型隔离
 *********************************************************************/
typedef struct
{
    drv_gpio_port_e port;              /**< GPIO端口（DRV_GPIO_PORT_A~E） */
    drv_gpio_pin_e pin;                /**< 引脚掩码（DRV_GPIO_PIN_0~15） */
    drv_gpio_mode_e mode;              /**< 工作模式（DRV_GPIO_MODE_OUTPUT/INPUT/AF/ANALOG） */
    drv_gpio_otype_e otype;            /**< 输出类型（DRV_GPIO_OTYPE_PP/OD） */
    drv_gpio_speed_e speed;            /**< 速度配置（DRV_GPIO_SPEED_LEVEL0~3） */
    drv_gpio_pupd_e pupd;              /**< 上下拉配置（DRV_GPIO_PUPD_NONE/PULLUP/PULLDOWN） */
    drv_gpio_af_e af;                  /**< 复用功能（DRV_GPIO_AF_0~8，仅AF模式有效） */
    bool initial_state;               /**< 初始状态（true=高电平，false=低电平） */
} drv_gpio_config_t;

/*********************************************************************
 * 初始化和反初始化
 *********************************************************************/

/*********************************************************************
 * @brief   初始化单个GPIO引脚
 * @param   config GPIO配置指针
 * @return  0=成功，-1=参数错误
 * @note    内部自动使能时钟并配置GPIO
 *********************************************************************/
int32_t drv_gpio_init(const drv_gpio_config_t *config);

/*********************************************************************
 * @brief   反初始化GPIO（恢复复位状态）
 * @param   port GPIO端口基地址(DRV_GPIOA~DRV_GPIOE)
 * @param   pin 引脚掩码
 * @return  0=成功
 *********************************************************************/
int32_t drv_gpio_deinit(drv_gpio_port_e port, drv_gpio_pin_e pin);

/*********************************************************************
 * 基础操作函数（inline封装，零开销）
 *********************************************************************/

/*********************************************************************
 * @brief   设置引脚高电平
 * @param   port GPIO端口基地址(DRV_GPIOA~DRV_GPIOE)
 * @param   pin 引脚掩码
 * @note    直接封装gpio_bit_set()
 *********************************************************************/
static inline void drv_gpio_set(drv_gpio_port_e port, drv_gpio_pin_e pin)
{
    gpio_bit_set(port, pin);
}

/*********************************************************************
 * @brief   设置引脚低电平
 * @param   port GPIO端口基地址(DRV_GPIOA~DRV_GPIOE)
 * @param   pin 引脚掩码
 * @note    直接封装gpio_bit_reset()
 *********************************************************************/
static inline void drv_gpio_reset(drv_gpio_port_e port, drv_gpio_pin_e pin)
{
    gpio_bit_reset(port, pin);
}

/*********************************************************************
 * @brief   写入引脚状态
 * @param   port GPIO端口基地址(DRV_GPIOA~DRV_GPIOE)
 * @param   pin 引脚掩码
 * @param   state true=高电平，false=低电平
 *********************************************************************/
static inline void drv_gpio_write(drv_gpio_port_e port, drv_gpio_pin_e pin, bool state)
{
    if (state)
    {
        gpio_bit_set(port, pin);
    }
    else
    {
        gpio_bit_reset(port, pin);
    }
}

/*********************************************************************
 * @brief   读取引脚输入状态
 * @param   port GPIO端口基地址(DRV_GPIOA~DRV_GPIOE)
 * @param   pin 引脚掩码
 * @return  true=高电平，false=低电平
 * @note    读取输入寄存器（ISTAT），适用于输入模式
 *********************************************************************/
static inline bool drv_gpio_read_input(drv_gpio_port_e port, drv_gpio_pin_e pin)
{
    return (gpio_input_bit_get(port, pin) == SET);
}

/*********************************************************************
 * @brief   读取引脚输出状态
 * @param   port GPIO端口基地址(DRV_GPIOA~DRV_GPIOE)
 * @param   pin 引脚掩码
 * @return  true=高电平，false=低电平
 * @note    读取输出寄存器（OCTL），适用于输出模式
 *********************************************************************/
static inline bool drv_gpio_read_output(drv_gpio_port_e port, drv_gpio_pin_e pin)
{
    return (gpio_output_bit_get(port, pin) == SET);
}

/*********************************************************************
 * @brief   读取引脚状态（兼容旧接口）
 * @param   port GPIO端口基地址(DRV_GPIOA~DRV_GPIOE)
 * @param   pin 引脚掩码
 * @return  true=高电平，false=低电平
 * @note    默认读取输入状态，建议使用drv_gpio_read_input/drv_gpio_read_output
 *********************************************************************/
static inline bool drv_gpio_read(drv_gpio_port_e port, drv_gpio_pin_e pin)
{
    return drv_gpio_read_input(port, pin);
}

/*********************************************************************
 * @brief   翻转引脚状态
 * @param   port GPIO端口基地址(DRV_GPIOA~DRV_GPIOE)
 * @param   pin 引脚掩码
 *********************************************************************/
static inline void drv_gpio_toggle(drv_gpio_port_e port, drv_gpio_pin_e pin)
{
    if (gpio_output_bit_get(port, pin) == SET)
    {
        gpio_bit_reset(port, pin);
    }
    else
    {
        gpio_bit_set(port, pin);
    }
}

/*********************************************************************
 * 批量操作函数
 *********************************************************************/

/*********************************************************************
 * @brief   同时设置多个引脚
 * @param   port GPIO端口基地址
 * @param   value 16位输出值（bit0~bit15对应pin0~pin15）
 * @note    写入整个端口的输出寄存器
 *********************************************************************/
void drv_gpio_write_port(drv_gpio_port_e port, uint16_t value);

/*********************************************************************
 * @brief   读取整个端口状态
 * @param   port GPIO端口基地址
 * @return  16位输入状态
 * @note    读取整个端口的输入寄存器
 *********************************************************************/
uint16_t drv_gpio_read_port(drv_gpio_port_e port);

/*********************************************************************
 * EXTI中断管理
 *********************************************************************/

/*********************************************************************
 * @brief   配置GPIO为EXTI中断模式
 * @param   port GPIO端口基地址
 * @param   pin 引脚掩码
 * @param   mode 中断模式（DRV_EXTI_MODE_INTERRUPT/DRV_EXTI_MODE_EVENT）
 * @param   trigger 触发方式（DRV_EXTI_TRIG_RISING/DRV_EXTI_TRIG_FALLING/DRV_EXTI_TRIG_BOTH）
 * @param   callback 中断回调函数
 * @param   irq_priority NVIC中断优先级（0~15）
 * @return  0=成功，-1=参数错误
 * @note    内部处理：连接SYSCFG、配置EXTI、注册回调、配置NVIC
 *********************************************************************/
int32_t drv_gpio_exti_configure(drv_gpio_port_e port, drv_gpio_pin_e pin,
                            drv_exti_mode_e mode, drv_exti_trig_e trigger,
                            drv_gpio_exti_callback_t callback, uint8_t irq_priority);

/*********************************************************************
 * @brief   使能EXTI中断
 * @param   port GPIO端口基地址(DRV_GPIOA~DRV_GPIOE)
 * @param   pin 引脚掩码
 *********************************************************************/
void drv_gpio_exti_enable(drv_gpio_port_e port, drv_gpio_pin_e pin);

/*********************************************************************
 * @brief   禁用EXTI中断
 * @param   port GPIO端口基地址(DRV_GPIOA~DRV_GPIOE)
 * @param   pin 引脚掩码
 *********************************************************************/
void drv_gpio_exti_disable(drv_gpio_port_e port, drv_gpio_pin_e pin);

/*********************************************************************
 * @brief   EXTI中断统一处理函数
 * @param   exti_line EXTI线号（EXTI_0~EXTI_15）
 * @note    在gd32f50x_it.c的中断处理函数中调用
 *********************************************************************/
void drv_gpio_exti_handler(uint32_t exti_line);

/*********************************************************************
 * @brief   锁定GPIO引脚配置
 * @param   port GPIO端口基地址(DRV_GPIOA~DRV_GPIOE)
 * @param   pin 引脚掩码
 * @note    锁定后无法修改配置，直到下次复位
 *********************************************************************/
void drv_gpio_lock(drv_gpio_port_e port, drv_gpio_pin_e pin);

#endif /* __GPIO_DRIVER_H__ */
