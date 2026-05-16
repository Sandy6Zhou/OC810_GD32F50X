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

/** GPIO端口到EXTI端口源映射表 */
static const uint8_t s_gpio_port_source[DRV_MAX_GPIO_PORT_COUNT] = {
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
static uint16_t s_gpio_use[DRV_MAX_GPIO_PORT_COUNT] = {0};

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
static uint8_t _drv_gpio_port_to_index(drv_gpio_port_e port);
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
    uint8_t port_index;
    uint32_t pin_bit;

    if (config == NULL)
    {
        return -1;
    }

    /* 获取端口索引 */
    port_index = _drv_gpio_port_to_index(config->port);
    if (port_index >= DRV_MAX_GPIO_PORT_COUNT)
    {
        DRV_GPIO_LOGE("Invalid GPIO port: %d", config->port);
        return -1;
    }

    /* 检查是否已使用 */
    pin_bit = __CLZ(config->pin);
    if (s_gpio_use[port_index] & (1U << (15 - pin_bit)))
    {
        DRV_GPIO_LOGW("GPIO already init: port=%d, pin=%d", port_index, 15 - pin_bit);
        return 0;
    }

    /* 使能时钟 */
    _drv_gpio_enable_clock(config->port);

    /* 配置GPIO模式 */
    gpio_mode_set(config->port, config->mode, config->pupd, config->pin);

    /* 配置输出类型和速度（仅输出/复用模式） */
    if ((config->mode == DRV_GPIO_MODE_OUTPUT) || (config->mode == DRV_GPIO_MODE_AF))
    {
        gpio_output_options_set(config->port, config->otype, config->speed, config->pin);

        /* 设置初始状态 */
        if (config->initial_state)
        {
            gpio_bit_set(config->port, config->pin);
        }
        else
        {
            gpio_bit_reset(config->port, config->pin);
        }
    }

    /* 配置复用功能（仅AF模式） */
    if (config->mode == DRV_GPIO_MODE_AF)
    {
        gpio_af_set(config->port, config->af, config->pin);
    }

    /* 标记GPIO已使用 */
    s_gpio_use[port_index] |= (1U << (15 - pin_bit));

    DRV_GPIO_LOGD("GPIO init: port=%d, pin=%d, mode=%d", port_index, 15 - pin_bit, config->mode);

    return 0;
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
    uint8_t port_index;
    uint32_t pin_bit;
    uint32_t exti_line;

    /* 获取端口索引 */
    port_index = _drv_gpio_port_to_index(port);
    if (port_index >= DRV_MAX_GPIO_PORT_COUNT)
    {
        DRV_GPIO_LOGE("Invalid GPIO port: %d", port);
        return -1;
    }

    /* 清除GPIO使用标志 */
    pin_bit = __CLZ(pin);
    s_gpio_use[port_index] &= ~(1U << (15 - pin_bit));

    /* 禁用EXTI中断 */
    exti_line = _drv_gpio_pin_to_exti_line(pin);
    if (s_exti_table[exti_line - EXTI_0].is_exti)
    {
        drv_gpio_exti_disable(port, pin);
    }

    /* 恢复为默认输入模式 */
    gpio_mode_set(port, DRV_GPIO_MODE_INPUT, DRV_GPIO_PUPD_NONE, pin);

    /* 检查该端口是否还有其他GPIO在使用 */
    if (s_gpio_use[port_index] == 0)
    {
        _drv_gpio_disable_clock(port);
        DRV_GPIO_LOGD("GPIO clock disabled: port=%d", port_index);
    }

    DRV_GPIO_LOGD("GPIO deinit: port=%d, pin=%d", port_index, 15 - pin_bit);

    return 0;
}

/*********************************************************************
 * 批量操作函数
 *********************************************************************/

/*********************************************************************
 * @brief   同时设置多个引脚
 * @param   port GPIO端口基地址(DRV_GPIOA~DRV_GPIOE)
 * @param   value 16位输出值（bit0~bit15对应pin0~pin15）
 * @note    写入整个端口的输出寄存器
 *********************************************************************/
void drv_gpio_write_port(drv_gpio_port_e port, uint16_t value)
{
    gpio_port_write(port, value);
}

/*********************************************************************
 * @brief   读取整个端口状态
 * @param   port GPIO端口基地址
 * @return  16位输入状态
 * @note    读取整个端口的输入寄存器
 *********************************************************************/
uint16_t drv_gpio_read_port(drv_gpio_port_e port)
{
    return gpio_input_port_get(port);
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
    uint8_t port_index;
    uint32_t exti_index;

    if (callback == NULL)
    {
        return -1;
    }

    /* 获取EXTI线号和中断号 */
    exti_line = _drv_gpio_pin_to_exti_line(pin);
    irqn = gpio_pin_to_irqn(pin);
    exti_index = exti_line - EXTI_0;

    /* 获取端口源 */
    port_index = _drv_gpio_port_to_index(port);
    if (port_index >= DRV_MAX_GPIO_PORT_COUNT)
    {
        return -1;
    }
    port_source = s_gpio_port_source[port_index];

    /* 连接GPIO到EXTI */
    gpio_exti_source_select(port_source, (uint8_t)(__CLZ(pin)));

    /* 配置EXTI中断模式和触发方式 */
    exti_init(exti_line, (exti_mode_enum)mode, (exti_trig_type_enum)trigger);

    /* 清除EXTI挂起位 */
    exti_interrupt_flag_clear(exti_line);

    /* 注册回调函数到EXTI表 */
    s_exti_table[exti_index].port = port;
    s_exti_table[exti_index].pin = pin;
    s_exti_table[exti_index].callback = callback;
    s_exti_table[exti_index].is_exti = true;

    /* 配置NVIC中断 */
    nvic_irq_enable(irqn, irq_priority, 0);

    DRV_GPIO_LOGD("GPIO EXTI configured: pin=%d, line=%d, irqn=%d", exti_index, exti_line, irqn);

    return 0;
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
    exti_interrupt_enable(exti_line);

    DRV_GPIO_LOGD("GPIO EXTI enabled: pin=0x%04X", pin);
}

/*********************************************************************
 * @brief   禁用EXTI中断
 * @param   port GPIO端口基地址
 * @param   pin 引脚掩码
 * @note    禁用EXTI中断并清除回调函数
 *********************************************************************/
void drv_gpio_exti_disable(drv_gpio_port_e port, drv_gpio_pin_e pin)
{
    uint32_t exti_line;
    uint32_t exti_index;

    /* 清除EXTI回调表 */
    exti_line = _drv_gpio_pin_to_exti_line(pin);
    exti_index = exti_line - EXTI_0;
    s_exti_table[exti_index].port = GPIOA;
    s_exti_table[exti_index].pin = (drv_gpio_pin_e)0;
    s_exti_table[exti_index].callback = NULL;
    s_exti_table[exti_index].is_exti = false;

    /* 禁用EXTI中断 */
    exti_interrupt_disable(exti_line);

    DRV_GPIO_LOGD("GPIO EXTI disabled: pin=%d", exti_index);
}

/*********************************************************************
 * 电源管理
 *********************************************************************/

/*********************************************************************
 * @brief   锁定GPIO引脚配置
 * @param   port GPIO端口基地址
 * @param   pin 引脚掩码
 * @note    锁定后无法修改配置，直到下次复位
 *********************************************************************/
void drv_gpio_lock(drv_gpio_port_e port, drv_gpio_pin_e pin)
{
    gpio_pin_lock(port, pin);
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

    /* O(1)直接索引查找 */
    exti_index = exti_line - EXTI_0;
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
 * @brief   端口基地址转索引（A=0, B=1, C=2, D=3, E=4）
 * @param   port GPIO端口基地址
 * @return  端口索引（0~4），无效端口返回DRV_MAX_GPIO_PORT_COUNT
 *********************************************************************/
static uint8_t _drv_gpio_port_to_index(drv_gpio_port_e port)
{
    uint8_t index = (uint8_t)((port - GPIOA) / 0x400U);

    /* 检查索引有效性 */
    if (index >= DRV_MAX_GPIO_PORT_COUNT)
    {
        return DRV_MAX_GPIO_PORT_COUNT; /* 返回无效值 */
    }

    return index;
}

/*********************************************************************
 * @brief   使能GPIO时钟
 * @param   port GPIO端口基地址
 * @note    根据端口使能对应的RCU时钟
 *********************************************************************/
static void _drv_gpio_enable_clock(drv_gpio_port_e port)
{
    switch (port)
    {
        case DRV_GPIOA:
            rcu_periph_clock_enable(RCU_GPIOA);
            break;
        case DRV_GPIOB:
            rcu_periph_clock_enable(RCU_GPIOB);
            break;
        case DRV_GPIOC:
            rcu_periph_clock_enable(RCU_GPIOC);
            break;
        case DRV_GPIOD:
            rcu_periph_clock_enable(RCU_GPIOD);
            break;
        case DRV_GPIOE:
            rcu_periph_clock_enable(RCU_GPIOE);
            break;
        default:
            DRV_GPIO_LOGE("Invalid GPIO port for clock enable: 0x%08X", port);
            break;
    }
}

/*********************************************************************
 * @brief   关闭GPIO时钟
 * @param   port GPIO端口基地址(DRV_GPIOA~DRV_GPIOE)
 * @note    根据端口关闭对应的RCU时钟
 *********************************************************************/
static void _drv_gpio_disable_clock(drv_gpio_port_e port)
{
    switch (port)
    {
        case DRV_GPIOA:
            rcu_periph_clock_disable(RCU_GPIOA);
            break;
        case DRV_GPIOB:
            rcu_periph_clock_disable(RCU_GPIOB);
            break;
        case DRV_GPIOC:
            rcu_periph_clock_disable(RCU_GPIOC);
            break;
        case DRV_GPIOD:
            rcu_periph_clock_disable(RCU_GPIOD);
            break;
        case DRV_GPIOE:
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
            return (EXTI_0 + i);
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
    uint32_t i = 15U - __CLZ(pin);

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
