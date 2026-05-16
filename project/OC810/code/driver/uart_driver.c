/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       uart_driver.c
**文件描述：       UART驱动模块实现文件
**当前版本：       V1.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.04.22
*********************************************************************
** 功能描述：       1. 实现多UART独立管理和状态机控制
**                 2. 实现注册/卸载/收发/电源管理功能
**                 3. 支持DMA、IDLE、RingBuffer、低功耗
**                 4. 驱动层与应用层完全解耦，内存由应用层管理
*********************************************************************/

#include "uart_driver.h"
#include "gd32f50x_usart.h"
#include <string.h>

/*********************************************************************
 * 内部数据结构定义
 *********************************************************************/

/**
 * @brief UART现场保存结构体（用于suspend/resume）
 */
typedef struct {
    uint32_t baudrate;          /**< 保存的波特率 */
    uint32_t stat;              /**< 保存的STAT寄存器值 */
    uint32_t ctrl0;             /**< 保存的CTRL0寄存器值 */
    uint32_t ctrl1;             /**< 保存的CTRL1寄存器值 */
    uint32_t ctrl2;             /**< 保存的CTRL2寄存器值 */
} uart_context_t;

/**
 * @brief UART控制结构体（驱动内部维护）
 */
typedef struct {
    uart_config_t   config;             /**< 应用层配置参数（只读） */
    uart_state_e    state;              /**< 当前状态 */
    SemaphoreHandle_t tx_mutex;         /**< 发送互斥锁 */
    uart_context_t  context;            /**< 挂起时的现场保存 */
    uint16_t        dma_rx_len;         /**< DMA接收数据长度 */
} uart_ctrl_t;

/*********************************************************************
 * 内部全局变量
 *********************************************************************/

/** UART控制实例数组 */
static uart_ctrl_t s_uart_ctrl[UART_PORT_MAX] = {0};

/** USART基地址映射表 */
static uint32_t const s_usart_base[UART_PORT_MAX] = {
    USART0,
    USART1,
    USART2,
    UART3,
    UART4
};

/*********************************************************************
 * 内部辅助函数声明
 *********************************************************************/

static int _uart_check_param(const uart_config_t *config);
static int _uart_hw_init(uart_port_e port);
static int _uart_hw_deinit(uart_port_e port);
static int _uart_enable_interrupt(uart_port_e port);
static int _uart_disable_interrupt(uart_port_e port);
static int _uart_enable_dma_rx(uart_port_e port);
static int _uart_disable_dma_rx(uart_port_e port);

/*********************************************************************
 * 内部辅助函数实现
 *********************************************************************/

/*********************************************************************
 * @brief   检查配置参数合法性
 * @param   config  配置结构体指针
 * @return  0表示合法，-1表示非法
 * @note    无
 *********************************************************************/
static int _uart_check_param(const uart_config_t *config)
{
    if (config == NULL)
    {
        UART_LOG_ERROR("Invalid UART config parameter");
        return -1;
    }

    if (config->port >= UART_PORT_MAX)
    {
        UART_LOG_ERROR("Invalid UART port: %d", config->port);
        return -1;
    }

    if (config->rx_buf == NULL || config->rx_buf_size == 0)
    {
        UART_LOG_ERROR("Invalid UART RX buffer");
        return -1;
    }

    if (config->use_dma_rx == true)
    {
        if (config->dma_rx_buf == NULL || config->dma_rx_buf_size == 0)
        {
            UART_LOG_ERROR("Invalid DMA RX buffer");
            return -1;
        }
    }

    if (config->use_ringbuf == true)
    {
        if (config->ringbuf == NULL)
        {
            UART_LOG_ERROR("Invalid RingBuffer pointer");
            return -1;
        }
    }

    return 0;
}

/*********************************************************************
 * @brief   UART硬件初始化
 * @param   port    UART端口号
 * @return  0表示成功，-1表示失败
 * @note    配置波特率、数据位、停止位、校验位等
 *********************************************************************/
static int _uart_hw_init(uart_port_e port)
{
    uint32_t usart_base;
    uart_ctrl_t *ctrl;

    if (port >= UART_PORT_MAX)
    {
        UART_LOG_ERROR("Invalid port: %d", port);
        return -1;
    }

    usart_base = s_usart_base[port];
    ctrl = &s_uart_ctrl[port];

    UART_LOG_DEBUG("Hardware init UART port %d, baudrate=%d", port, ctrl->config.baudrate);

    /* 使能UART时钟和GPIO时钟（根据端口配置） */
    if (port == UART_PORT_USART0)
    {
        rcu_periph_clock_enable(RCU_USART0);
    }
    else if (port == UART_PORT_USART1)
    {
        rcu_periph_clock_enable(RCU_USART1);
    }
    else if (port == UART_PORT_USART2)
    {
        rcu_periph_clock_enable(RCU_USART2);
    }
    else if (port == UART_PORT_UART3)
    {
        rcu_periph_clock_enable(RCU_UART3);
    }
    else if (port == UART_PORT_UART4)
    {
        rcu_periph_clock_enable(RCU_UART4);
    }

    /* 配置UART参数 */
    usart_deinit(usart_base);
    usart_baudrate_set(usart_base, ctrl->config.baudrate);
    usart_word_length_set(usart_base, USART_WL_8BIT);
    usart_stop_bit_set(usart_base, USART_STB_1BIT);
    usart_parity_config(usart_base, USART_PM_NONE);
    usart_hardware_flow_rts_config(usart_base, USART_RTS_DISABLE);
    usart_hardware_flow_cts_config(usart_base, USART_CTS_DISABLE);
    usart_receive_config(usart_base, USART_RECEIVE_ENABLE);
    usart_transmit_config(usart_base, USART_TRANSMIT_ENABLE);
    usart_enable(usart_base);

    UART_LOG_DEBUG("UART port %d hardware initialized", port);

    return 0;
}

/*********************************************************************
 * @brief   UART硬件去初始化
 * @param   port    UART端口号
 * @return  0表示成功，-1表示失败
 * @note    关闭UART外设和时钟
 *********************************************************************/
static int _uart_hw_deinit(uart_port_e port)
{
    uint32_t usart_base;

    if (port >= UART_PORT_MAX)
    {
        return -1;
    }

    usart_base = s_usart_base[port];

    usart_disable(usart_base);

    /* 关闭UART时钟 */
    if (port == UART_PORT_USART0)
    {
        rcu_periph_clock_disable(RCU_USART0);
    }
    else if (port == UART_PORT_USART1)
    {
        rcu_periph_clock_disable(RCU_USART1);
    }
    else if (port == UART_PORT_USART2)
    {
        rcu_periph_clock_disable(RCU_USART2);
    }
    else if (port == UART_PORT_UART3)
    {
        rcu_periph_clock_disable(RCU_UART3);
    }
    else if (port == UART_PORT_UART4)
    {
        rcu_periph_clock_disable(RCU_UART4);
    }

    return 0;
}

/*********************************************************************
 * @brief   使能UART中断
 * @param   port    UART端口号
 * @return  0表示成功，-1表示失败
 * @note    使能RXNE中断，如果配置了IDLE则使能IDLE中断
 *********************************************************************/
static int _uart_enable_interrupt(uart_port_e port)
{
    uint32_t usart_base;
    uart_ctrl_t *ctrl;

    if (port >= UART_PORT_MAX)
    {
        return -1;
    }

    usart_base = s_usart_base[port];
    ctrl = &s_uart_ctrl[port];

    /* 使能接收中断 */
    usart_interrupt_enable(usart_base, USART_INT_RBNE);

    /* 如果启用IDLE中断 */
    if (ctrl->config.use_idle == true)
    {
        usart_interrupt_enable(usart_base, USART_INT_IDLE);
    }

    /* 使能错误中断 */
    usart_interrupt_enable(usart_base, USART_INT_ERR);

    UART_LOG_DEBUG("UART port %d interrupts enabled, IDLE=%d", port, ctrl->config.use_idle);

    return 0;
}

/*********************************************************************
 * @brief   禁能UART中断
 * @param   port    UART端口号
 * @return  0表示成功，-1表示失败
 * @note    无
 *********************************************************************/
static int _uart_disable_interrupt(uart_port_e port)
{
    uint32_t usart_base;

    if (port >= UART_PORT_MAX)
    {
        return -1;
    }

    usart_base = s_usart_base[port];

    usart_interrupt_disable(usart_base, USART_INT_RBNE);
    usart_interrupt_disable(usart_base, USART_INT_IDLE);
    usart_interrupt_disable(usart_base, USART_INT_ERR);

    UART_LOG_DEBUG("UART port %d interrupts disabled", port);

    return 0;
}

/*********************************************************************
 * @brief   使能DMA接收
 * @param   port    UART端口号
 * @return  0表示成功，-1表示失败
 * @note    配置DMA通道并启动DMA接收
 *********************************************************************/
static int _uart_enable_dma_rx(uart_port_e port)
{
    /* TODO: 根据具体端口配置DMA通道 */
    /* 这里需要根据GD32F505的DMA通道映射来配置 */

    return 0;
}

/*********************************************************************
 * @brief   禁能DMA接收
 * @param   port    UART端口号
 * @return  0表示成功，-1表示失败
 * @note    无
 *********************************************************************/
static int _uart_disable_dma_rx(uart_port_e port)
{
    /* TODO: 关闭DMA通道 */

    return 0;
}

/*********************************************************************
 * 接口函数实现
 *********************************************************************/

/*********************************************************************
 * @brief   注册UART端口
 * @param   config  UART配置结构体指针（应用层传入）
 * @return  0表示成功，-1表示失败（参数错误或端口已注册）
 * @note    应用层需确保配置参数合法，所有内存资源由应用层分配管理
 *********************************************************************/
int uart_register(const uart_config_t *config)
{
    uart_ctrl_t *ctrl;
    uart_port_e port;

    /* 断言检查：配置指针不能为空 */
    UART_ASSERT(config != NULL);

    /* 参数检查 */
    if (_uart_check_param(config) != 0)
    {
        UART_LOG_ERROR("Invalid config parameter for port");
        return -1;
    }

    port = config->port;
    ctrl = &s_uart_ctrl[port];

    /* 断言检查：端口号必须合法 */
    UART_ASSERT(port < UART_PORT_MAX);

    /* 检查端口是否已注册 */
    if (ctrl->state != UART_STATE_UNINIT)
    {
        UART_LOG_WARN("UART port %d already registered, state=%d", port, ctrl->state);
        return -1;
    }

    /* 保存配置参数 */
    memcpy(&ctrl->config, config, sizeof(uart_config_t));

    /* 创建发送互斥锁 */
    ctrl->tx_mutex = xSemaphoreCreateMutex();
    if (ctrl->tx_mutex == NULL)
    {
        UART_LOG_ERROR("Failed to create TX mutex for port %d", port);
        return -1;
    }

    /* 断言检查：互斥锁创建必须成功 */
    UART_ASSERT(ctrl->tx_mutex != NULL);

    /* 初始化状态 */
    ctrl->state = UART_STATE_INIT;

    /* 硬件初始化 */
    if (_uart_hw_init(port) != 0)
    {
        UART_LOG_ERROR("Hardware init failed for port %d", port);
        vSemaphoreDelete(ctrl->tx_mutex);
        ctrl->state = UART_STATE_UNINIT;
        return -1;
    }

    /* 如果启用DMA接收 */
    if (config->use_dma_rx == true)
    {
        if (_uart_enable_dma_rx(port) != 0)
        {
            UART_LOG_ERROR("DMA RX enable failed for port %d", port);
            _uart_hw_deinit(port);
            vSemaphoreDelete(ctrl->tx_mutex);
            ctrl->state = UART_STATE_UNINIT;
            return -1;
        }
    }

    /* 使能中断 */
    if (_uart_enable_interrupt(port) != 0)
    {
        UART_LOG_ERROR("Interrupt enable failed for port %d", port);
        if (config->use_dma_rx == true)
        {
            _uart_disable_dma_rx(port);
        }
        _uart_hw_deinit(port);
        vSemaphoreDelete(ctrl->tx_mutex);
        ctrl->state = UART_STATE_UNINIT;
        return -1;
    }

    /* 切换到活跃状态 */
    ctrl->state = UART_STATE_ACTIVE;

    UART_LOG_INFO("UART port %d registered successfully, baudrate=%d, DMA_RX=%d, IDLE=%d, RingBuf=%d",
                  port, config->baudrate, config->use_dma_rx, config->use_idle, config->use_ringbuf);

    return 0;
}

/*********************************************************************
 * @brief   卸载UART端口
 * @param   port    UART端口号
 * @return  0表示成功，-1表示失败（端口未初始化）
 * @note    释放驱动内部资源，应用层传入的内存由应用层自行管理
 *********************************************************************/
int uart_deinit(uart_port_e port)
{
    uart_ctrl_t *ctrl;

    /* 断言检查：端口号必须合法 */
    UART_ASSERT(port < UART_PORT_MAX);

    if (port >= UART_PORT_MAX)
    {
        UART_LOG_ERROR("Invalid UART port: %d", port);
        return -1;
    }

    ctrl = &s_uart_ctrl[port];

    /* 检查端口状态 */
    if (ctrl->state == UART_STATE_UNINIT)
    {
        UART_LOG_WARN("UART port %d not initialized", port);
        return -1;
    }

    /* 关闭中断 */
    _uart_disable_interrupt(port);

    /* 关闭DMA */
    if (ctrl->config.use_dma_rx == true)
    {
        _uart_disable_dma_rx(port);
    }

    /* 硬件去初始化 */
    _uart_hw_deinit(port);

    /* 释放互斥锁 */
    if (ctrl->tx_mutex != NULL)
    {
        vSemaphoreDelete(ctrl->tx_mutex);
        ctrl->tx_mutex = NULL;
    }

    /* 清空配置 */
    memset(&ctrl->config, 0, sizeof(uart_config_t));
    memset(&ctrl->context, 0, sizeof(uart_context_t));

    /* 状态重置 */
    ctrl->state = UART_STATE_UNINIT;

    UART_LOG_INFO("UART port %d deinitialized successfully", port);

    return 0;
}

/* 以下为待实现的接口函数占位 */

/*********************************************************************
 * @brief   发送数据
 * @param   port    UART端口号
 * @param   data    待发送数据指针
 * @param   len     待发送数据长度（字节）
 * @return  实际发送的字节数，-1表示失败
 * @note    线程安全，支持普通发送和DMA发送（根据配置自动选择）
 *********************************************************************/
int uart_send(uart_port_e port, const uint8_t *data, uint16_t len)
{
    /* 断言检查：参数合法性 */
    UART_ASSERT(port < UART_PORT_MAX);
    UART_ASSERT(data != NULL);
    UART_ASSERT(len > 0);

    /* TODO: 第4步实现 */
    (void)port;
    (void)data;
    (void)len;
    return -1;
}

/*********************************************************************
 * @brief   读取数据
 * @param   port    UART端口号
 * @param   data    读取数据存放指针
 * @param   len     期望读取的数据长度（字节）
 * @return  实际读取的字节数，-1表示失败
 * @note    从RingBuffer或接收缓存中读取数据
 *********************************************************************/
int uart_read(uart_port_e port, uint8_t *data, uint16_t len)
{
    /* 断言检查：参数合法性 */
    UART_ASSERT(port < UART_PORT_MAX);
    UART_ASSERT(data != NULL);
    UART_ASSERT(len > 0);

    /* TODO: 第4步实现 */
    (void)port;
    (void)data;
    (void)len;
    return -1;
}

/*********************************************************************
 * @brief   挂起UART端口（低功耗）
 * @param   port    UART端口号
 * @return  0表示成功，-1表示失败（端口状态不允许挂起）
 * @note    唤醒串口仅关闭TX，普通串口彻底关闭硬件
 *********************************************************************/
int uart_suspend(uart_port_e port)
{
    /* TODO: 第6步实现 */
    (void)port;
    return -1;
}

/*********************************************************************
 * @brief   恢复UART端口
 * @param   port    UART端口号
 * @return  0表示成功，-1表示失败（端口状态不允许恢复）
 * @note    恢复挂起的UART端口到活跃状态
 *********************************************************************/
int uart_resume(uart_port_e port)
{
    /* TODO: 第6步实现 */
    (void)port;
    return -1;
}

/*********************************************************************
 * @brief   关闭UART端口
 * @param   port    UART端口号
 * @return  0表示成功，-1表示失败（端口未初始化）
 * @note    彻底关闭硬件，可通过resume恢复，不释放控制实例
 *********************************************************************/
int uart_shutdown(uart_port_e port)
{
    /* TODO: 第6步实现 */
    (void)port;
    return -1;
}

/*********************************************************************
 * @brief   获取UART端口状态
 * @param   port    UART端口号
 * @return  UART状态枚举值，-1表示失败（端口无效）
 * @note    用于调试和问题排查
 *********************************************************************/
int uart_get_state(uart_port_e port)
{
    /* 断言检查：端口号必须合法 */
    UART_ASSERT(port < UART_PORT_MAX);

    if (port >= UART_PORT_MAX)
    {
        UART_LOG_ERROR("Invalid UART port: %d", port);
        return -1;
    }

    UART_LOG_DEBUG("UART port %d state: %d", port, s_uart_ctrl[port].state);

    return (int)s_uart_ctrl[port].state;
}

/*********************************************************************
 * @brief   UART中断处理函数（统一入口）
 * @param   port    UART端口号
 * @return  无
 * @note    本函数由gd32f50x_it.c中的官方中断服务函数调用，例如：
 *          void USART0_IRQHandler(void) { uart_irq_handler(UART_PORT_USART0); }
 *          应用层不应直接调用此函数
 *********************************************************************/
void uart_irq_handler(uart_port_e port)
{
    /* 断言检查：端口号必须合法 */
    UART_ASSERT(port < UART_PORT_MAX);

    /* TODO: 第5步实现 */
    (void)port;
}
