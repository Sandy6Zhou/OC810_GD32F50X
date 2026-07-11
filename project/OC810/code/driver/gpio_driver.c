/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       gpio_driver.c
**文件描述：       GPIO驱动实现文件 (轻量级封装层)
**当前版本：       V2.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.04.20
*********************************************************************
** 功能描述：       1. GPIO初始化/反初始化实现
**                 2. EXTI中断配置和回调管理
**                 3. 智能时钟管理（自动启停）
**                 4. 批量操作实现
*********************************************************************/

#include "gpio_driver.h"
#include "gd32f50x_exti.h"
#include "gd32f50x_misc.h"
#include "gd32f50x_syscfg.h"
#include "string.h"

/*********************************************************************
 * 内部宏定义
 *********************************************************************/

 /** GPIO端口枚举到基地址映射表（extern供inline函数使用） */
uint32_t const g_gpio_port_base[DRV_GPIO_PORT_MAX] = {
    GPIOA,
    GPIOB,
    GPIOC,
    GPIOD,
    GPIOE
};

/** GPIO端口到EXTI端口源映射表 */
static const uint8_t s_gpio_port_source[DRV_GPIO_PORT_MAX] = {
    GPIO_PORT_SOURCE_GPIOA,
    GPIO_PORT_SOURCE_GPIOB,
    GPIO_PORT_SOURCE_GPIOC,
    GPIO_PORT_SOURCE_GPIOD,
    GPIO_PORT_SOURCE_GPIOE
};

/*********************************************************************
 * 内部数据结构
 *********************************************************************/

/** GPIO使用标志表（5组端口×16位，每bit代表一个GPIO是否已使用）
 *  DRV_GPIO_PORT_A -> s_gpio_use[0] [bit0~bit15]
 *  DRV_GPIO_PORT_B -> s_gpio_use[1] [bit0~bit15]
 *  DRV_GPIO_PORT_C -> s_gpio_use[2] [bit0~bit15]
 *  DRV_GPIO_PORT_D -> s_gpio_use[3] [bit0~bit15]
 *  DRV_GPIO_PORT_E -> s_gpio_use[4] [bit0~bit15] */
static uint16_t s_gpio_use[DRV_GPIO_PORT_MAX] = {0};

/** EXTI回调函数表（最多16个EXTI线） */
typedef struct
{
    drv_gpio_port_e port;               /**< GPIO端口 */
    drv_gpio_pin_e pin;                 /**< 引脚掩码 */
    bool is_exti;                      /**< 是否已配置EXTI */
    drv_gpio_exti_callback_t callback;  /**< 回调函数指针 */
} exti_callback_entry_t;

static exti_callback_entry_t s_exti_table[DRV_MAX_EXTI_LINE_COUNT] = {0};

/*********************************************************************
 * 内部辅助函数声明
 *********************************************************************/
static void _drv_gpio_enable_clock(drv_gpio_port_e port);
static void _drv_gpio_disable_clock(drv_gpio_port_e port);
static uint32_t _drv_gpio_pin_to_exti_line(uint32_t pin);
static IRQn_Type gpio_pin_to_irqn(uint32_t pin);

/*********************************************************************
 * 驱动初始化函数
 *********************************************************************/

/*********************************************************************
 * @brief   初始化单个GPIO引脚
 * @param   config GPIO配置结构体指针
 * @return  0=成功，-1=参数错误
 * @note    内部自动使能时钟并配置GPIO，标记GPIO已使用
 *********************************************************************/
int32_t drv_gpio_init(const drv_gpio_config_t *config)
{
    uint32_t pin_mask;
    uint32_t pin_bit;
    uint32_t gpio_base;  /* GD32 GPIO基地址 */

    if (config == NULL)
    {
        return DRV_GPIO_ERR_NULL_PTR;
    }

    /* 参数校验 */
    if (config->port >= DRV_GPIO_PORT_MAX)
    {
        DRV_GPIO_LOGE("Invalid GPIO port: %d", config->port);
        return DRV_GPIO_ERR_INVALID_PORT;
    }

    /* 获取GD32 GPIO基地址 */
    gpio_base = g_gpio_port_base[config->port];

    /* 检查是否有引脚已使用（仅警告，不阻止配置） */
    pin_mask = config->pin;
    while (pin_mask != 0)
    {
        /* __CLZ 计算前导零数量，对于 32 位值：pin_bit = 31 - 前导零数 = 最高位位置 */
        pin_bit = 31U - __CLZ(pin_mask);
        if (s_gpio_use[config->port] & (1U << pin_bit))
        {
            DRV_GPIO_LOGW("GPIO already in use: port=%d, pin=%d, will reconfigure", config->port, pin_bit);
        }
        pin_mask &= ~(1U << pin_bit);  /* 清除已处理的位 */
    }

    /* 使能时钟 */
    _drv_gpio_enable_clock(config->port);

    /* 配置复用功能（仅AF模式，先配置AF再配置模式） */
    if (config->mode == DRV_GPIO_MODE_AF)
    {
        gpio_af_set(gpio_base, config->af, config->pin);
    }

    /* 配置GPIO模式（支持多引脚） */
    gpio_mode_set(gpio_base, config->mode, config->pupd, config->pin);

    /* 配置输出类型和速度（仅输出/复用模式） */
    if ((config->mode == DRV_GPIO_MODE_OUTPUT) || (config->mode == DRV_GPIO_MODE_AF))
    {
        gpio_output_options_set(gpio_base, config->otype, config->speed, config->pin);

        /* 设置初始状态 */
        if (config->initial_state)
        {
            gpio_bit_set(gpio_base, config->pin);
        }
        else
        {
            gpio_bit_reset(gpio_base, config->pin);
        }
    }

    /* 标记所有引脚已使用（支持多引脚） */
    pin_mask = config->pin;
    while (pin_mask != 0)
    {
        /* __CLZ 计算前导零数量，对于 32 位值：pin_bit = 31 - 前导零数 = 最高位位置 */
        pin_bit = 31U - __CLZ(pin_mask);
        s_gpio_use[config->port] |= (1U << pin_bit);
        pin_mask &= ~(1U << pin_bit);  /* 清除已处理的位 */
    }

    DRV_GPIO_LOGD("GPIO init: port=%d, pins=0x%04X, mode=%d", config->port, config->pin, config->mode);

    return DRV_GPIO_OK;
}

/*********************************************************************
 * @brief   反初始化GPIO（恢复复位状态）
 * @param   port GPIO端口基地址
 * @param   pin 引脚掩码
 * @return  0=成功
 * @note    如果端口没有其他GPIO使用，自动关闭时钟
 *********************************************************************/
int32_t drv_gpio_deinit(drv_gpio_port_e port, drv_gpio_pin_e pin)
{
    uint32_t pin_mask;
    uint32_t pin_bit;
    uint32_t exti_line;
    uint32_t gpio_base;  /* GD32 GPIO基地址 */

    /* 参数校验 */
    if (port >= DRV_GPIO_PORT_MAX)
    {
        DRV_GPIO_LOGE("Invalid GPIO port: %d", port);
        return DRV_GPIO_ERR_INVALID_PORT;
    }

    /* 获取GD32 GPIO基地址 */
    gpio_base = g_gpio_port_base[port];

    /* 禁用EXTI中断并清除配置（支持多引脚） */
    pin_mask = pin;
    while (pin_mask != 0)
    {
        /* __CLZ 计算前导零数量，pin_bit = 31 - 前导零数 = 最高位位置 */
        uint32_t highest_bit = 31U - __CLZ(pin_mask);
        uint32_t single_pin = (1U << highest_bit);

        exti_line = _drv_gpio_pin_to_exti_line(single_pin);
        if (exti_line != 0xFFFFFFFFU)  /* 检查EXTI线号是否有效 */
        {
            uint32_t exti_index = 31U - __CLZ(exti_line);

            if (s_exti_table[exti_index].is_exti)
            {
                /* 禁用EXTI中断 */
                exti_interrupt_disable(exti_line);

                /* 清除EXTI挂起位 */
                exti_interrupt_flag_clear(exti_line);

                /* 清除EXTI回调表 */
                s_exti_table[exti_index].callback = NULL;
                s_exti_table[exti_index].is_exti = false;
            }
        }
        pin_mask &= ~single_pin;  /* 清除已处理的位 */
    }

    /* 恢复为默认输入模式（支持多引脚） */
    gpio_mode_set(gpio_base, DRV_GPIO_MODE_INPUT, DRV_GPIO_PUPD_NONE, pin);

    /* 清除所有GPIO使用标志（支持多引脚） */
    pin_mask = pin;
    while (pin_mask != 0)
    {
        /* __CLZ 计算前导零数量，对于 32 位值：pin_bit = 31 - 前导零数 = 最高位位置 */
        pin_bit = 31U - __CLZ(pin_mask);
        s_gpio_use[port] &= ~(1U << pin_bit);
        pin_mask &= ~(1U << pin_bit);  /* 清除已处理的位 */
    }

    /* 检查该端口是否还有其他GPIO在使用 */
    if (s_gpio_use[port] == 0)
    {
        _drv_gpio_disable_clock(port);
        DRV_GPIO_LOGD("GPIO clock disabled: port=%d", port);
    }

    DRV_GPIO_LOGD("GPIO deinit: port=%d, pins=0x%04X", port, pin);

    return DRV_GPIO_OK;
}

/*********************************************************************
 * 批量操作函数
 *********************************************************************/

/*********************************************************************
 * @brief   同时设置多个引脚
 * @param   port GPIO端口枚举值
 * @param   value 16位输出值（bit0~bit15对应pin0~pin15）
 * @note    写入整个端口的输出寄存器
 *********************************************************************/
void drv_gpio_write_port(drv_gpio_port_e port, uint16_t value)
{
    uint32_t gpio_base = g_gpio_port_base[port];

    gpio_port_write(gpio_base, value);
}

/*********************************************************************
 * @brief   读取整个端口状态
 * @param   port GPIO端口枚举值
 * @return  16位输入状态
 * @note    读取整个端口的输入寄存器
 *********************************************************************/
uint16_t drv_gpio_read_port(drv_gpio_port_e port)
{
    return gpio_input_port_get(g_gpio_port_base[port]);
}

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
                            drv_gpio_exti_callback_t callback, uint8_t irq_priority)
{
    uint32_t exti_line;
    IRQn_Type irqn;
    uint8_t port_source;
    uint8_t pin_source;
    uint32_t exti_index;

    /* 参数校验 */
    if (callback == NULL)
    {
        return DRV_GPIO_ERR_NULL_PTR;
    }

    /* 获取端口源 */
    if (port >= DRV_GPIO_PORT_MAX)
    {
        return DRV_GPIO_ERR_INVALID_PORT;
    }

    /* 获取EXTI线号和中断号 */
    exti_line = _drv_gpio_pin_to_exti_line(pin);
    if (exti_line == 0xFFFFFFFFU)
    {
        DRV_GPIO_LOGE("Invalid EXTI pin: 0x%04X", pin);
        return DRV_GPIO_ERR_INVALID_PIN;
    }

    /* 使能AF时钟（EXTI必须） */
    rcu_periph_clock_enable(RCU_AF);

    /* 获取端口源 */
    port_source = s_gpio_port_source[port];
    pin_source = (uint8_t)(31U - __CLZ(pin));

    irqn = gpio_pin_to_irqn(pin);
    exti_index = 31U - __CLZ(exti_line);  /* 位掩码转索引 */

    /* 注册回调函数到EXTI表 */
    s_exti_table[exti_index].port = port;
    s_exti_table[exti_index].pin = pin;
    s_exti_table[exti_index].callback = callback;
    s_exti_table[exti_index].is_exti = true;

    /* 配置NVIC中断 */
    nvic_irq_enable(irqn, irq_priority, 0U);

    /* 连接GPIO到EXTI */
    gpio_exti_source_select(port_source, pin_source);

    /* 配置EXTI中断模式和触发方式 */
    exti_init(exti_line, (exti_mode_enum)mode, (exti_trig_type_enum)trigger);

    /* 清除EXTI挂起位 */
    exti_interrupt_flag_clear(exti_line);

    DRV_GPIO_LOGD("GPIO EXTI configured: pin=%d, line=%d, irqn=%d", exti_index, exti_line, irqn);

    return DRV_GPIO_OK;
}

/*********************************************************************
 * @brief   使能EXTI中断
 * @param   port GPIO端口基地址
 * @param   pin 引脚掩码
 * @note    仅使能EXTI中断，不修改NVIC配置
 *********************************************************************/
void drv_gpio_exti_enable(drv_gpio_port_e port, drv_gpio_pin_e pin)
{
    uint32_t exti_line;

    exti_line = _drv_gpio_pin_to_exti_line(pin);
    if (exti_line == 0xFFFFFFFFU)
    {
        DRV_GPIO_LOGE("Invalid EXTI pin: 0x%04X", pin);
        return;
    }

    exti_interrupt_enable(exti_line);

    DRV_GPIO_LOGD("GPIO EXTI enabled: pin=0x%04X", pin);
}

/*********************************************************************
 * @brief   禁用EXTI中断
 * @param   port GPIO端口基地址
 * @param   pin 引脚掩码
 * @note    仅禁用EXTI中断，不修改回调表配置
 *          用于快速开关中断场景（如关键代码段保护）
 *          回调函数保持注册，后续可通过drv_gpio_exti_enable快速恢复
 *********************************************************************/
void drv_gpio_exti_disable(drv_gpio_port_e port, drv_gpio_pin_e pin)
{
    uint32_t exti_line;

    /* 获取EXTI线号 */
    exti_line = _drv_gpio_pin_to_exti_line(pin);
    if (exti_line == 0xFFFFFFFFU)
    {
        DRV_GPIO_LOGE("Invalid EXTI pin: 0x%04X", pin);
        return;
    }

    /* 禁用EXTI中断（不修改回调表） */
    exti_interrupt_disable(exti_line);

    /* 清除EXTI挂起位（防止重新使能时立即触发中断） */
    exti_interrupt_flag_clear(exti_line);

    DRV_GPIO_LOGD("GPIO EXTI disabled: pin=0x%04X", pin);
}

/*********************************************************************
 * 电源管理
 *********************************************************************/

/*********************************************************************
 * @brief   锁定GPIO引脚配置
 * @param   port GPIO端口枚举值
 * @param   pin 引脚掩码
 * @note    锁定后无法修改配置，直到下次复位
 *********************************************************************/
void drv_gpio_lock(drv_gpio_port_e port, drv_gpio_pin_e pin)
{
    uint32_t gpio_base = g_gpio_port_base[port];
    gpio_pin_lock(gpio_base, pin);
}

/*********************************************************************
 * @brief   EXTI中断统一处理函数
 * @param   exti_line EXTI线号（EXTI_0~EXTI_15）
 * @note    在gd32f50x_it.c的中断处理函数中调用
 *          示例：void EXTI0_IRQHandler(void) {
 *                   drv_gpio_exti_handler(EXTI_0);
 *                   exti_interrupt_flag_clear(EXTI_0);
 *                 }
 *********************************************************************/
void drv_gpio_exti_handler(uint32_t exti_line)
{
    uint32_t exti_index;
    drv_gpio_exti_callback_t callback;

    /* 位掩码转索引（EXTI枚举是位掩码，不是连续整数） */
    exti_index = 31U - __CLZ(exti_line);
    if (exti_index >= DRV_MAX_EXTI_LINE_COUNT)
    {
        return;
    }

    /* 检查是否使能且有回调 */
    if (s_exti_table[exti_index].is_exti &&
        s_exti_table[exti_index].callback != NULL)
    {
        callback = s_exti_table[exti_index].callback;
        callback(s_exti_table[exti_index].port,
                s_exti_table[exti_index].pin);
    }
}

/*********************************************************************
 * 内部辅助函数实现
 *********************************************************************/

/*********************************************************************
 * @brief   使能GPIO时钟
 * @param   port GPIO端口枚举值
 * @note    根据端口使能对应的RCU时钟
 *********************************************************************/
static void _drv_gpio_enable_clock(drv_gpio_port_e port)
{
    switch (port)
    {
        case DRV_GPIO_PORT_A:
            rcu_periph_clock_enable(RCU_GPIOA);
            break;
        case DRV_GPIO_PORT_B:
            rcu_periph_clock_enable(RCU_GPIOB);
            break;
        case DRV_GPIO_PORT_C:
            rcu_periph_clock_enable(RCU_GPIOC);
            break;
        case DRV_GPIO_PORT_D:
            rcu_periph_clock_enable(RCU_GPIOD);
            break;
        case DRV_GPIO_PORT_E:
            rcu_periph_clock_enable(RCU_GPIOE);
            break;
        default:
            DRV_GPIO_LOGE("Invalid GPIO port for clock enable: %d", port);
            break;
    }
}

/*********************************************************************
 * @brief   关闭GPIO时钟
 * @param   port GPIO端口枚举值
 * @note    根据端口关闭对应的RCU时钟
 *********************************************************************/
static void _drv_gpio_disable_clock(drv_gpio_port_e port)
{
    switch (port)
    {
        case DRV_GPIO_PORT_A:
            rcu_periph_clock_disable(RCU_GPIOA);
            break;
        case DRV_GPIO_PORT_B:
            rcu_periph_clock_disable(RCU_GPIOB);
            break;
        case DRV_GPIO_PORT_C:
            rcu_periph_clock_disable(RCU_GPIOC);
            break;
        case DRV_GPIO_PORT_D:
            rcu_periph_clock_disable(RCU_GPIOD);
            break;
        case DRV_GPIO_PORT_E:
            rcu_periph_clock_disable(RCU_GPIOE);
            break;
        default:
            break;
    }
}

/*********************************************************************
 * @brief   根据引脚获取EXTI线号
 * @param   pin 引脚掩码（DRV_GPIO_PIN_0~DRV_GPIO_PIN_15）
 * @return  EXTI线号（EXTI_0~EXTI_15），无效引脚返回0xFFFFFFFF
 * @note    使用位扫描找到引脚对应的EXTI线
 *********************************************************************/
static uint32_t _drv_gpio_pin_to_exti_line(uint32_t pin)
{
    uint32_t i;

    for (i = 0; i < DRV_MAX_GPIO_PIN_PER_PORT; i++)
    {
        if (pin & (1U << i))
        {
            return (1U << i);  /* 返回位掩码，不是 EXTI_0 + i */
        }
    }

    return 0xFFFFFFFFU; /* 无效引脚 */
}

/*********************************************************************
 * @brief   根据引脚获取NVIC中断号
 * @param   pin 引脚掩码（DRV_GPIO_PIN_0~DRV_GPIO_PIN_15）
 * @return  NVIC中断号（EXTI0_IRQn~EXTI10_15_IRQn）
 * @note    Pin0~4: EXTI0_IRQn~EXTI4_IRQn
 *          Pin5~9: EXTI5_9_IRQn
 *          Pin10~15: EXTI10_15_IRQn
 *********************************************************************/
static IRQn_Type gpio_pin_to_irqn(uint32_t pin)
{
    uint32_t i = 31U - __CLZ(pin);

    if (i <= 4U)
    {
        return (IRQn_Type)(EXTI0_IRQn + i);
    }
    else if (i <= 9U)
    {
        return EXTI5_9_IRQn;
    }
    else
    {
        return EXTI10_15_IRQn;
    }
}
