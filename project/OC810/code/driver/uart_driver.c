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
    uint16_t        rx_write_index;     /**< rx_buf写指针（中断中使用） */
    uint16_t        rx_read_index;      /**< rx_buf读指针（应用层使用） */
    uint8_t         rx_mode;            /**< 接收模式标识（注册时确定） */
    uint8_t         irq_enabled;        /**< 中断使能标志（注册时确定） */
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

    /* 初始化接收模式标识 */
    if (config->use_dma_rx == true)
    {
        ctrl->rx_mode = (config->use_ringbuf == true) ? UART_RX_MODE_DMA_RINGBUF : UART_RX_MODE_DMA_RXBUF;
    }
    else
    {
        ctrl->rx_mode = (config->use_ringbuf == true) ? UART_RX_MODE_NODMA_RINGBUF : UART_RX_MODE_NODMA_RXBUF;
    }

    /* 初始化中断使能标志 */
    ctrl->irq_enabled = UART_IRQ_RBNE | UART_IRQ_ERR;  /* RXNE和ERR总是使能 */
    if (config->use_idle == true)
    {
        ctrl->irq_enabled |= UART_IRQ_IDLE;
    }

    /* 根据配置创建发送互斥锁 */
    if (config->use_tx_mutex == true)
    {
        ctrl->tx_mutex = xSemaphoreCreateMutex();
        if (ctrl->tx_mutex == NULL)
        {
            UART_LOG_ERROR("Failed to create TX mutex for port %d", port);
            return -1;
        }

        UART_LOG_DEBUG("TX mutex created for port %d", port);
    }
    else
    {
        ctrl->tx_mutex = NULL;
        UART_LOG_DEBUG("TX mutex disabled for port %d", port);
    }

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
    uint32_t usart_base;
    uart_ctrl_t *ctrl;
    int send_len = 0;
    uint16_t i;

    /* 断言检查：参数合法性 */
    UART_ASSERT(port < UART_PORT_MAX);
    UART_ASSERT(data != NULL);
    UART_ASSERT(len > 0);

    /* 参数校验 */
    if (port >= UART_PORT_MAX)
    {
        UART_LOG_ERROR("Invalid UART port: %d", port);
        return -1;
    }

    if (data == NULL || len == 0)
    {
        UART_LOG_ERROR("Invalid send parameter");
        return -1;
    }

    ctrl = &s_uart_ctrl[port];

    /* 状态检查：只有活跃状态才能发送 */
    if (ctrl->state != UART_STATE_ACTIVE)
    {
        UART_LOG_WARN("UART port %d not active, state=%d", port, ctrl->state);
        return -1;
    }

    usart_base = s_usart_base[port];

    /* 如果启用了互斥锁，获取互斥锁（线程安全） */
    if (ctrl->tx_mutex != NULL)
    {
        /* 使用超时机制，避免永久阻塞 */
#if UART_TX_MUTEX_TIMEOUT_MS > 0
        if (xSemaphoreTake(ctrl->tx_mutex, pdMS_TO_TICKS(UART_TX_MUTEX_TIMEOUT_MS)) != pdTRUE)
        {
            UART_LOG_ERROR("TX mutex timeout for port %d", port);
            return -1;
        }
#else
        if (xSemaphoreTake(ctrl->tx_mutex, portMAX_DELAY) != pdTRUE)
        {
            UART_LOG_ERROR("Failed to take TX mutex for port %d", port);
            return -1;
        }
#endif
    }

    /* 判断是否启用DMA发送 */
    if (ctrl->config.use_dma_tx == true)
    {
        /* TODO: DMA发送实现（第5步中断处理时完善） */
        UART_LOG_WARN("DMA TX not implemented yet, use polling mode");

        /* 临时使用轮询发送 */
        for (i = 0; i < len; i++)
        {
            usart_data_transmit(usart_base, data[i]);

            /* 等待发送完成 */
            while (usart_flag_get(usart_base, USART_FLAG_TBE) == RESET)
            {
                ;
            }
        }

        send_len = len;
    }
    else
    {
        /* 普通轮询发送 */
        UART_LOG_DEBUG("UART port %d send %d bytes (polling mode)", port, len);

        for (i = 0; i < len; i++)
        {
            usart_data_transmit(usart_base, data[i]);

            /* 等待发送完成 */
            while (usart_flag_get(usart_base, USART_FLAG_TBE) == RESET)
            {
                ;
            }
        }

        /* 等待最后一帧发送完成 */
        while (usart_flag_get(usart_base, USART_FLAG_TC) == RESET)
        {
            ;
        }

        send_len = len;
    }

    /* 如果启用了互斥锁，释放互斥锁 */
    if (ctrl->tx_mutex != NULL)
    {
        xSemaphoreGive(ctrl->tx_mutex);
    }

    UART_LOG_DEBUG("UART port %d send completed, len=%d", port, send_len);

    return send_len;
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
    uart_ctrl_t *ctrl;
    int read_len = 0;

    /* 断言检查：参数合法性 */
    UART_ASSERT(port < UART_PORT_MAX);
    UART_ASSERT(data != NULL);
    UART_ASSERT(len > 0);

    /* 参数校验 */
    if (port >= UART_PORT_MAX)
    {
        UART_LOG_ERROR("Invalid UART port: %d", port);
        return -1;
    }

    if (data == NULL || len == 0)
    {
        UART_LOG_ERROR("Invalid read parameter");
        return -1;
    }

    ctrl = &s_uart_ctrl[port];

    /* 状态检查 */
    if (ctrl->state != UART_STATE_ACTIVE)
    {
        UART_LOG_WARN("UART port %d not active, state=%d", port, ctrl->state);
        return -1;
    }

    /* 判断是否启用RingBuffer */
    if (ctrl->config.use_ringbuf == true)
    {
        /* 从RingBuffer读取数据 */
        if (ctrl->config.ringbuf == NULL)
        {
            UART_LOG_ERROR("RingBuffer pointer is NULL for port %d", port);
            return -1;
        }

        read_len = ringbuf_read(ctrl->config.ringbuf, data, len);

        UART_LOG_DEBUG("UART port %d read %d bytes from RingBuffer", port, read_len);
    }
    else
    {
        /* 从基础接收缓存读取（循环缓冲区） */
        /* 计算可读取的数据长度 */
        uint16_t available_len;
        uint16_t i;

        if (ctrl->rx_write_index >= ctrl->rx_read_index)
        {
            /* 写指针在读指针之后或相等 */
            available_len = ctrl->rx_write_index - ctrl->rx_read_index;
        }
        else
        {
            /* 写指针在读指针之前（循环） */
            available_len = ctrl->config.rx_buf_size - ctrl->rx_read_index + ctrl->rx_write_index;
        }

        /* 取期望长度和可用长度的较小值 */
        uint16_t copy_len = (len < available_len) ? len : available_len;

        if (copy_len == 0)
        {
            UART_LOG_DEBUG("UART port %d no data available", port);
            return 0;
        }

        /* 拷贝数据（需要处理循环情况） */
        for (i = 0; i < copy_len; i++)
        {
            data[i] = ctrl->config.rx_buf[ctrl->rx_read_index];
            ctrl->rx_read_index++;

            if (ctrl->rx_read_index >= ctrl->config.rx_buf_size)
            {
                ctrl->rx_read_index = 0;
            }
        }

        read_len = copy_len;

        UART_LOG_DEBUG("UART port %d read %d bytes from RX buffer", port, read_len);
    }

    return read_len;
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
 * @brief   查询可读取的接收数据长度
 * @param   port    UART端口号
 * @return  可读取的字节数，-1表示失败（端口无效）
 * @note    应用层可通过此接口查询有多少数据待读取，避免无效调用uart_read
 *********************************************************************/
int uart_get_rx_len(uart_port_e port)
{
    uart_ctrl_t *ctrl;
    uint16_t available_len = 0;

    /* 断言检查：端口号必须合法 */
    UART_ASSERT(port < UART_PORT_MAX);

    if (port >= UART_PORT_MAX)
    {
        UART_LOG_ERROR("Invalid UART port: %d", port);
        return -1;
    }

    ctrl = &s_uart_ctrl[port];

    /* 状态检查 */
    if (ctrl->state != UART_STATE_ACTIVE)
    {
        UART_LOG_WARN("UART port %d not active, state=%d", port, ctrl->state);
        return -1;
    }

    /* 判断是否启用RingBuffer */
    if (ctrl->config.use_ringbuf == true)
    {
        /* 从RingBuffer查询数据长度 */
        if (ctrl->config.ringbuf == NULL)
        {
            UART_LOG_ERROR("RingBuffer pointer is NULL for port %d", port);
            return -1;
        }

        available_len = (uint16_t)ringbuf_get_data_size(ctrl->config.ringbuf);
    }
    else
    {
        /* 从基础接收缓存计算可读取长度 */
        if (ctrl->rx_write_index >= ctrl->rx_read_index)
        {
            available_len = ctrl->rx_write_index - ctrl->rx_read_index;
        }
        else
        {
            available_len = ctrl->config.rx_buf_size - ctrl->rx_read_index + ctrl->rx_write_index;
        }
    }

    UART_LOG_DEBUG("UART port %d rx available: %d bytes", port, available_len);

    return (int)available_len;
}

/*********************************************************************
 * @brief   UART中断处理函数（统一入口）
 * @param   port    UART端口号
 * @return  无
 * @note    本函数由gd32f50x_it.c中的官方中断服务函数调用，例如：
 *          void USART0_IRQHandler(void) { uart_irq_handler(UART_PORT_USART0); }
 *          应用层不应直接调用此函数
 *          优化：直接读取寄存器，避免函数调用开销
 *********************************************************************/
void uart_irq_handler(uart_port_e port)
{
    uint32_t usart_base;
    uart_ctrl_t *ctrl;
    uint8_t data;
    uint16_t dma_len = 0;
    uint32_t stat0;
    uint32_t int0;

    /* 断言检查：端口号必须合法 */
    UART_ASSERT(port < UART_PORT_MAX);

    if (port >= UART_PORT_MAX)
    {
        return;
    }

    usart_base = s_usart_base[port];
    ctrl = &s_uart_ctrl[port];

    /* 检查端口状态，非活跃状态不处理中断 */
    if (ctrl->state != UART_STATE_ACTIVE)
    {
        return;
    }

    /* 一次性读取状态寄存器和中断使能寄存器（优化：避免多次函数调用） */
    stat0 = USART_STAT0(usart_base);
    int0 = USART_CTL0(usart_base);

    /* 1. 处理RXNE中断（接收数据非空） */
    if ((stat0 & USART_STAT0_RBNE) && (int0 & USART_CTL0_RBNEIE))
    {
        /* 读DR寄存器清除RBNE标志 */
        data = (uint8_t)USART_DATA(usart_base);

        /* 根据接收模式分发处理（switch优化） */
        switch (ctrl->rx_mode)
        {
            case UART_RX_MODE_NODMA_RINGBUF:
                /* 非DMA + RingBuffer模式 */
                ringbuf_write(ctrl->config.ringbuf, &data, 1);
                break;

            case UART_RX_MODE_NODMA_RXBUF:
                /* 非DMA + rx_buf模式（循环缓冲区） */
                ctrl->config.rx_buf[ctrl->rx_write_index] = data;
                ctrl->rx_write_index++;
                if (ctrl->rx_write_index >= ctrl->config.rx_buf_size)
                {
                    ctrl->rx_write_index = 0;
                }
                break;

            default:
                /* DMA模式：不处理RXNE，由DMA自动搬运 */
                break;
        }
    }

    /* 2. 处理IDLE中断（空闲帧，一帧数据接收完成） */
    if ((ctrl->irq_enabled & UART_IRQ_IDLE) && (stat0 & USART_STAT0_IDLEF) && (int0 & USART_CTL0_IDLEIE))
    {
        /* 清除IDLE标志：读STAT0后读DATA */
        (void)USART_DATA(usart_base);

        /* 如果启用DMA接收，计算DMA已接收的数据长度 */
        if (ctrl->config.use_dma_rx == true)
        {
            /* TODO: 获取DMA剩余计数，计算已接收长度 */
            dma_len = ctrl->dma_rx_len;

            /* 将DMA缓冲区数据写入RingBuffer */
            if (ctrl->config.use_ringbuf == true && ctrl->config.ringbuf != NULL)
            {
                if (dma_len > 0)
                {
                    ringbuf_write(ctrl->config.ringbuf, ctrl->config.dma_rx_buf, dma_len);
                }
            }

            /* 重启DMA接收 */
            _uart_enable_dma_rx(port);
        }
        else
        {
            /* 非DMA模式：计算接收长度（简单估算） */
            dma_len = 0;  /* 暂无法精确获取，应用层应从RingBuffer查询 */
        }

        /* 调用接收完成回调 */
        if (ctrl->config.rx_callback != NULL)
        {
            ctrl->config.rx_callback(port, dma_len);
        }
    }

    /* 3. 处理错误中断（只检查使能的错误标志） */
    if (ctrl->irq_enabled & UART_IRQ_ERR)
    {
        /* 帧错误 */
        if (stat0 & USART_STAT0_FERR)
        {
            USART_STAT0(usart_base) &= ~USART_STAT0_FERR;  /* 写1清除 */

            if (ctrl->config.error_callback != NULL)
            {
                ctrl->config.error_callback(port, UART_ERROR_FRAME);
            }
        }

        /* 溢出错误 */
        if (stat0 & USART_STAT0_ORERR)
        {
            USART_STAT0(usart_base) &= ~USART_STAT0_ORERR;

            if (ctrl->config.error_callback != NULL)
            {
                ctrl->config.error_callback(port, UART_ERROR_OVERRUN);
            }
        }

        /* 噪声错误 */
        if (stat0 & USART_STAT0_NERR)
        {
            USART_STAT0(usart_base) &= ~USART_STAT0_NERR;

            if (ctrl->config.error_callback != NULL)
            {
                ctrl->config.error_callback(port, UART_ERROR_NOISE);
            }
        }

        /* 奇偶校验错误 */
        if (stat0 & USART_STAT0_PERR)
        {
            USART_STAT0(usart_base) &= ~USART_STAT0_PERR;

            if (ctrl->config.error_callback != NULL)
            {
                ctrl->config.error_callback(port, UART_ERROR_PARITY);
            }
        }
    }

    /* 4. 处理TXE中断（发送数据寄存器空） */
    if ((stat0 & USART_STAT0_TBE) && (int0 & USART_CTL0_TBEIE))
    {
        /* TODO: 中断发送实现（需要添加发送缓冲区） */
        /* 暂时关闭TXE中断，使用轮询发送 */
        USART_CTL0(usart_base) &= ~USART_CTL0_TBEIE;
    }

    /* 5. 处理TC中断（发送完成） */
    if (stat0 & USART_STAT0_TC)
    {
        USART_STAT0(usart_base) &= ~USART_STAT0_TC;

        /* TODO: 发送完成回调 */
    }
}
