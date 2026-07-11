/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       uart_driver.c
**文件描述：       UART驱动模块实现文件
**当前版本：       V1.3
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.04.22
**修改日期：       2026.05.20
*********************************************************************
** 功能描述：       1. 实现多UART独立管理和状态机控制
**                 2. 实现注册/卸载/收发/电源管理功能
**                 3. 支持5种TX模式（轮询/中断/DMA同步/DMA异步/DMA双缓冲）
**                 4. 支持DMA接收、IDLE空闲中断、RingBuffer（含半满中断通知防溢出）
**                 5. 支持低功耗挂起/恢复、线程安全
**                 6. 驱动层与应用层完全解耦，内存由应用层管理
**                 7. 每个UART端口可独立配置运行时TX模式
**                 8. 支持GPIO配置宏表，编译期选择引脚，NO_USE节省代码空间
*********************************************************************/

#include "uart_driver.h"
#include "dma_driver.h"
#include "gpio_driver.h"
#include "gd32f50x_usart.h"
#include <string.h>

/*********************************************************************
 * 内部数据结构定义
 *********************************************************************/

/**
 * @brief UART现场保存结构体（用于suspend/resume）
 */
typedef struct {
    uint32_t baudrate;          /**< 波特率 */
    uint32_t stat;              /**< STAT寄存器 */
    uint32_t ctrl0;             /**< CTRL0寄存器 */
    uint32_t ctrl1;             /**< CTRL1寄存器 */
    uint32_t ctrl2;             /**< CTRL2寄存器 */
} drv_uart_context_t;

/**
 * @brief TX中断控制结构（仅INTERRUPT模式使用）
 */
typedef struct {
    uint8_t      *tx_buf;           /**< TX缓冲区指针 */
    uint16_t      tx_buf_size;      /**< 缓冲区大小 */
    uint16_t      tx_write_idx;     /**< 写指针 */
    uint16_t      tx_read_idx;      /**< 读指针 */
    uint16_t      tx_count;         /**< 缓冲区中数据量 */
    uint16_t      tx_total_len;     /**< 本次发送总长度 */
    bool          tx_active;        /**< 是否正在发送 */
} drv_uart_tx_irq_ctrl_t;

/**
 * @brief UART控制结构体（驱动内部维护）
 */
typedef struct {
    drv_uart_config_t   config;             /**< 应用层配置（只读） */
    drv_uart_state_e    state;              /**< 当前状态 */
    drv_uart_tx_mode_e  tx_mode;            /**< 发送模式 */
    SemaphoreHandle_t tx_mutex;             /**< 发送互斥锁 */
    SemaphoreHandle_t tx_sem;               /**< DMA发送完成信号量（仅异步模式） */
    drv_uart_ring_tx_ctrl_t *ring_tx_ctrl;  /**< 双缓冲控制（仅DUAL_BUF模式） */
    drv_uart_tx_irq_ctrl_t *tx_irq_ctrl;    /**< 中断发送控制（仅INTERRUPT模式） */
    drv_uart_context_t  context;            /**< 挂起现场 */
    uint16_t        dma_rx_len;             /**< DMA接收数据长度 */
    uint16_t        rx_write_index;         /**< rx_buf写指针（中断使用） */
    uint16_t        rx_read_index;          /**< rx_buf读指针（应用层使用） */
    uint8_t         rx_mode;                /**< 接收模式 */
    uint8_t         irq_enabled;            /**< 中断使能标志 */
    volatile bool  ringbuf_half_triggered;  /**< RingBuffer半满回调已触发标志（防止重复触发，中断和任务共享需临界区保护） */
} drv_uart_ctrl_t;

/*********************************************************************
 * 内部全局变量
 *********************************************************************/

/** UART控制实例数组 */
static drv_uart_ctrl_t s_uart_ctrl[DRV_UART_PORT_MAX] = {0};

/** 当前正在DMA异步发送的UART端口（用于中断回调定位） */
static drv_uart_port_e s_current_dma_tx_port = DRV_UART_PORT_MAX;

/** DMA TX全局互斥锁（保护s_current_dma_tx_port变量） */
static SemaphoreHandle_t s_dma_tx_mutex = NULL;

/** 当前正在DMA双缓冲发送的UART端口（用于中断回调定位） */
static drv_uart_port_e s_current_ring_tx_port = DRV_UART_PORT_MAX;

/** USART基地址映射表 */
static uint32_t const s_usart_base[DRV_UART_PORT_MAX] = {
    USART0,
    USART1,
    USART2,
    UART3,
    UART4
};

/**
 * @brief USART NVIC 配置结构体
 */
typedef struct {
    IRQn_Type irqn;              /**< NVIC 中断号 */
    uint8_t preempt_priority;    /**< 抢占优先级（0-15，数值越小优先级越高） */
    uint8_t sub_priority;        /**< 子优先级（0-15，数值越小优先级越高） */
} drv_uart_nvic_config_t;

/**
 * @brief USART NVIC 配置表
 * @note  配置说明：
 *        - irqn: NVIC 中断号，由芯片硬件决定，不可修改
 *        - preempt_priority: 抢占优先级，开发人员可根据实际需求修改
 *        - sub_priority: 子优先级，开发人员可根据实际需求修改
 *        - 建议：高速通信端口使用高优先级（数值小），低速端口使用低优先级（数值大）
 *        - 默认配置：所有端口统一使用优先级 3，子优先级 0
 */
static const drv_uart_nvic_config_t s_usart_nvic_config[DRV_UART_PORT_MAX] = {
    {USART0_IRQn, 3, 0},  /* USART0: IRQ37, 抢占优先级3, 子优先级0 */
    {USART1_IRQn, 3, 0},  /* USART1: IRQ38, 抢占优先级3, 子优先级0 */
    {USART2_IRQn, 3, 0},  /* USART2: IRQ39, 抢占优先级3, 子优先级0 */
    {UART3_IRQn,  3, 0},  /* UART3:  IRQ40, 抢占优先级3, 子优先级0 */
    {UART4_IRQn,  3, 0}   /* UART4:  IRQ41, 抢占优先级3, 子优先级0 */
};

/** UART RX DMA通道映射表（UART4无DMA） */
static drv_dma_channel_id_e const s_uart_rx_dma_ch[DRV_UART_PORT_MAX] = {
    DRV_DMA0_CH1,  /* USART0_RX -> DMA0_CH1 */
    DRV_DMA0_CH2,  /* USART1_RX -> DMA0_CH2 */
    DRV_DMA0_CH3,  /* USART2_RX -> DMA0_CH3 */
    DRV_DMA0_CH4,  /* UART3_RX  -> DMA0_CH4 */
    DRV_DMA_MAX    /* UART4     -> 无DMA */
};

/** UART TX DMA通道映射表（UART4无DMA） */
static drv_dma_channel_id_e const s_uart_tx_dma_ch[DRV_UART_PORT_MAX] = {
    DRV_DMA0_CH0,  /* USART0_TX -> DMA0_CH0 */
    DRV_DMA0_CH5,  /* USART1_TX -> DMA0_CH5 */
    DRV_DMA0_CH6,  /* USART2_TX -> DMA0_CH6 */
    DRV_DMA1_CH0,  /* UART3_TX  -> DMA1_CH0 */
    DRV_DMA_MAX    /* UART4     -> 无DMA */
};

/** UART DMA请求源映射表 */
static uint32_t const s_uart_dma_request[DRV_UART_PORT_MAX][2] = {
    {DMA_REQUEST_USART0_RX, DMA_REQUEST_USART0_TX},
    {DMA_REQUEST_USART1_RX, DMA_REQUEST_USART1_TX},
    {DMA_REQUEST_USART2_RX, DMA_REQUEST_USART2_TX},
    {DMA_REQUEST_UART3_RX,  DMA_REQUEST_UART3_TX},
    {0, 0}  /* UART4无DMA */
};

/*********************************************************************
 * 内部辅助函数声明
 *********************************************************************/

static int _drv_uart_check_param(const drv_uart_config_t *config);
static int _drv_uart_hw_init(drv_uart_port_e port);
static int _drv_uart_hw_deinit(drv_uart_port_e port);
static int _drv_uart_enable_interrupt(drv_uart_port_e port);
static int _drv_uart_disable_interrupt(drv_uart_port_e port);
static int _drv_uart_enable_dma_rx(drv_uart_port_e port);
static int _drv_uart_disable_dma_rx(drv_uart_port_e port);

static void uart_dma_tx_isr_callback(void);

static int32_t _uart_ring_queue_push(drv_uart_port_e port, const uint8_t *data, uint16_t len);
static uint16_t _uart_ring_queue_pop(drv_uart_port_e port, uint8_t *data, uint16_t max_len);
static uint16_t _uart_ring_queue_pop_isr(drv_uart_ring_tx_ctrl_t *ring_ctrl, uint8_t *data, uint16_t max_len);
static void uart_dma_ring_tx_htf_callback(void);
static void uart_dma_ring_tx_ftf_callback(void);
static int32_t _uart_send_ring_start(drv_uart_port_e port);

/*********************************************************************
 * 内部辅助函数实现
 *********************************************************************/

/*********************************************************************
 * @brief   检查配置参数合法性
 * @param   config  配置结构体指针
 * @return  0表示合法，-1表示非法
 *********************************************************************/
static int _drv_uart_check_param(const drv_uart_config_t *config)
{
    if (config == NULL)
    {
        DRV_UART_LOGE("Invalid UART config parameter");
        return DRV_UART_ERR_FAILED;
    }

    if (config->port >= DRV_UART_PORT_MAX)
    {
        DRV_UART_LOGE("Invalid UART port: %d", config->port);
        return DRV_UART_ERR_FAILED;
    }

    /* 接收缓冲区校验：使用 RingBuffer 时 rx_buf 可选，否则必须提供 */
    if (config->use_ringbuf == false)
    {
        if (config->rx_buf == NULL || config->rx_buf_size == 0)
        {
            DRV_UART_LOGE("Invalid UART RX buffer (RingBuffer not enabled)");
            return DRV_UART_ERR_FAILED;
        }
    }
    else
    {
        /* RingBuffer 模式：ringbuf 指针必须有效 */
        if (config->ringbuf == NULL)
        {
            DRV_UART_LOGE("Invalid RingBuffer pointer");
            return DRV_UART_ERR_FAILED;
        }
    }

    if (config->use_dma_rx == true)
    {
        if (config->dma_rx_buf == NULL || config->dma_rx_buf_size == 0)
        {
            DRV_UART_LOGE("Invalid DMA RX buffer");
            return DRV_UART_ERR_FAILED;
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
static int _drv_uart_hw_init(drv_uart_port_e port)
{
    uint32_t usart_base;
    drv_uart_ctrl_t *ctrl;
    drv_gpio_config_t gpio_cfg = {0};

    if (port >= DRV_UART_PORT_MAX)
    {
        DRV_UART_LOGE("Invalid port: %d", port);
        return DRV_UART_ERR_FAILED;
    }

    usart_base = s_usart_base[port];
    ctrl = &s_uart_ctrl[port];

    DRV_UART_LOGD("Hardware init UART port %d, baudrate=%d", port, ctrl->config.baudrate);

    /* 使能UART时钟和GPIO时钟（根据端口配置） */
    rcu_periph_clock_enable(RCU_AF);  /* 使能复用功能时钟（必须） */

    switch (port)
    {
        case DRV_UART_PORT_USART0:
#if DRV_USART0_GPIO_SEL == DRV_USART0_GPIO_PA9_PA10
            rcu_periph_clock_enable(RCU_USART0);  /* 使能USART0外设时钟 */

            /* USART0: PA9(TX), PA10(RX) - GD32F50x USART0使用AF_0 */
            gpio_cfg.port = DRV_GPIO_PORT_A;
            gpio_cfg.pin = DRV_GPIO_PIN_9 | DRV_GPIO_PIN_10;
            gpio_cfg.af = DRV_GPIO_AF_0;
            gpio_cfg.mode = DRV_GPIO_MODE_AF;
            gpio_cfg.otype = DRV_GPIO_OTYPE_PP;
            gpio_cfg.speed = DRV_GPIO_SPEED_LEVEL3;
            gpio_cfg.pupd = DRV_GPIO_PUPD_PULLUP;
            drv_gpio_init(&gpio_cfg);
#elif DRV_USART0_GPIO_SEL == DRV_USART0_GPIO_PB6_PB7
            rcu_periph_clock_enable(RCU_USART0);  /* 使能USART0外设时钟 */

            /* USART0: PB6(TX), PB7(RX) - GD32F50x USART0使用AF_0 */
            gpio_cfg.port = DRV_GPIO_PORT_B;
            gpio_cfg.pin = DRV_GPIO_PIN_6 | DRV_GPIO_PIN_7;
            gpio_cfg.af = DRV_GPIO_AF_0;
            gpio_cfg.mode = DRV_GPIO_MODE_AF;
            gpio_cfg.otype = DRV_GPIO_OTYPE_PP;
            gpio_cfg.speed = DRV_GPIO_SPEED_LEVEL3;
            gpio_cfg.pupd = DRV_GPIO_PUPD_PULLUP;
            drv_gpio_init(&gpio_cfg);
#endif
            break;

        case DRV_UART_PORT_USART1:
#if DRV_USART1_GPIO_SEL == DRV_USART1_GPIO_PA2_PA3
            rcu_periph_clock_enable(RCU_USART1);  /* 使能USART1外设时钟 */

            /* USART1: PA2(TX), PA3(RX) - GD32F50x USART1使用AF_0 */
            gpio_cfg.port = DRV_GPIO_PORT_A;
            gpio_cfg.pin = DRV_GPIO_PIN_2 | DRV_GPIO_PIN_3;
            gpio_cfg.af = DRV_GPIO_AF_0;
            gpio_cfg.mode = DRV_GPIO_MODE_AF;
            gpio_cfg.otype = DRV_GPIO_OTYPE_PP;
            gpio_cfg.speed = DRV_GPIO_SPEED_LEVEL3;
            gpio_cfg.pupd = DRV_GPIO_PUPD_PULLUP;
            drv_gpio_init(&gpio_cfg);
#elif DRV_USART1_GPIO_SEL == DRV_USART1_GPIO_PD5_PD6
            rcu_periph_clock_enable(RCU_USART1);  /* 使能USART1外设时钟 */

            /* USART1: PD5(TX), PD6(RX) - GD32F50x USART1使用AF_0 */
            gpio_cfg.port = DRV_GPIO_PORT_D;
            gpio_cfg.pin = DRV_GPIO_PIN_5 | DRV_GPIO_PIN_6;
            gpio_cfg.af = DRV_GPIO_AF_0;
            gpio_cfg.mode = DRV_GPIO_MODE_AF;
            gpio_cfg.otype = DRV_GPIO_OTYPE_PP;
            gpio_cfg.speed = DRV_GPIO_SPEED_LEVEL3;
            gpio_cfg.pupd = DRV_GPIO_PUPD_PULLUP;
            drv_gpio_init(&gpio_cfg);
#endif
            break;

        case DRV_UART_PORT_USART2:
#if DRV_UART2_GPIO_SEL == DRV_UART2_GPIO_PB10_PB11
            rcu_periph_clock_enable(RCU_USART2);  /* 使能USART2外设时钟 */

            /* UART2: PB10(TX), PB11(RX) - GD32F50x UART2使用AF_1 */
            gpio_cfg.port = DRV_GPIO_PORT_B;
            gpio_cfg.pin = DRV_GPIO_PIN_10 | DRV_GPIO_PIN_11;
            gpio_cfg.af = DRV_GPIO_AF_1;
            gpio_cfg.mode = DRV_GPIO_MODE_AF;
            gpio_cfg.otype = DRV_GPIO_OTYPE_PP;
            gpio_cfg.speed = DRV_GPIO_SPEED_LEVEL3;
            gpio_cfg.pupd = DRV_GPIO_PUPD_PULLUP;
            drv_gpio_init(&gpio_cfg);
#elif DRV_UART2_GPIO_SEL == DRV_UART2_GPIO_PC10_PC11
            rcu_periph_clock_enable(RCU_USART2);  /* 使能USART2外设时钟 */

            /* UART2: PC10(TX), PC11(RX) - GD32F50x UART2使用AF_0 */
            gpio_cfg.port = DRV_GPIO_PORT_C;
            gpio_cfg.pin = DRV_GPIO_PIN_10 | DRV_GPIO_PIN_11;
            gpio_cfg.af = DRV_GPIO_AF_0;
            gpio_cfg.mode = DRV_GPIO_MODE_AF;
            gpio_cfg.otype = DRV_GPIO_OTYPE_PP;
            gpio_cfg.speed = DRV_GPIO_SPEED_LEVEL3;
            gpio_cfg.pupd = DRV_GPIO_PUPD_PULLUP;
            drv_gpio_init(&gpio_cfg);
#elif DRV_UART2_GPIO_SEL == DRV_UART2_GPIO_PD8_PD9
            rcu_periph_clock_enable(RCU_USART2);  /* 使能USART2外设时钟 */

            /* UART2: PD8(TX), PD9(RX) - GD32F50x UART2使用AF_0 */
            gpio_cfg.port = DRV_GPIO_PORT_D;
            gpio_cfg.pin = DRV_GPIO_PIN_8 | DRV_GPIO_PIN_9;
            gpio_cfg.af = DRV_GPIO_AF_0;
            gpio_cfg.mode = DRV_GPIO_MODE_AF;
            gpio_cfg.otype = DRV_GPIO_OTYPE_PP;
            gpio_cfg.speed = DRV_GPIO_SPEED_LEVEL3;
            gpio_cfg.pupd = DRV_GPIO_PUPD_PULLUP;
            drv_gpio_init(&gpio_cfg);
#endif
            break;

        case DRV_UART_PORT_UART3:
#if DRV_UART3_GPIO_SEL == DRV_UART3_GPIO_PC10_PC11
            rcu_periph_clock_enable(RCU_UART3);   /* 使能UART3外设时钟 */

            /* UART3: PC10(TX), PC11(RX) - GD32F50x UART3使用AF_1 */
            gpio_cfg.port = DRV_GPIO_PORT_C;
            gpio_cfg.pin = DRV_GPIO_PIN_10 | DRV_GPIO_PIN_11;
            gpio_cfg.af = DRV_GPIO_AF_1;
            gpio_cfg.mode = DRV_GPIO_MODE_AF;
            gpio_cfg.otype = DRV_GPIO_OTYPE_PP;
            gpio_cfg.speed = DRV_GPIO_SPEED_LEVEL3;
            gpio_cfg.pupd = DRV_GPIO_PUPD_PULLUP;
            drv_gpio_init(&gpio_cfg);
#endif
            break;

        case DRV_UART_PORT_UART4:
#if DRV_UART4_GPIO_SEL == DRV_UART4_GPIO_PC12_PD2
            rcu_periph_clock_enable(RCU_UART4);   /* 使能UART4外设时钟 */

            /* UART4: PC12(TX) - GD32F50x UART4使用AF_1 */
            gpio_cfg.port = DRV_GPIO_PORT_C;
            gpio_cfg.pin = DRV_GPIO_PIN_12;
            gpio_cfg.af = DRV_GPIO_AF_1;
            gpio_cfg.mode = DRV_GPIO_MODE_AF;
            gpio_cfg.otype = DRV_GPIO_OTYPE_PP;
            gpio_cfg.speed = DRV_GPIO_SPEED_LEVEL3;
            gpio_cfg.pupd = DRV_GPIO_PUPD_PULLUP;
            drv_gpio_init(&gpio_cfg);

            /* UART4: PD2(RX) - GD32F50x UART4使用AF_1 */
            gpio_cfg.port = DRV_GPIO_PORT_D;
            gpio_cfg.pin = DRV_GPIO_PIN_2;
            gpio_cfg.af = DRV_GPIO_AF_1;
            gpio_cfg.mode = DRV_GPIO_MODE_AF;
            gpio_cfg.otype = DRV_GPIO_OTYPE_PP;
            gpio_cfg.speed = DRV_GPIO_SPEED_LEVEL3;
            gpio_cfg.pupd = DRV_GPIO_PUPD_PULLUP;
            drv_gpio_init(&gpio_cfg);
#endif
            break;

        default:
            break;
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

    DRV_UART_LOGD("UART port %d hardware initialized", port);

    return 0;
}

/*********************************************************************
 * @brief   UART硬件去初始化
 * @param   port    UART端口号
 * @return  0表示成功，-1表示失败
 * @note    关闭UART外设、GPIO时钟，复位GPIO引脚
 *********************************************************************/
static int _drv_uart_hw_deinit(drv_uart_port_e port)
{
    uint32_t usart_base;

    if (port >= DRV_UART_PORT_MAX)
    {
        return DRV_UART_ERR_FAILED;
    }

    usart_base = s_usart_base[port];

    /* 关闭UART外设 */
    usart_disable(usart_base);

    /* 关闭UART外设时钟并反初始化GPIO引脚 */
    switch (port)
    {
        case DRV_UART_PORT_USART0:
            rcu_periph_clock_disable(RCU_USART0);
#if DRV_USART0_GPIO_SEL == DRV_USART0_GPIO_PA9_PA10
            drv_gpio_deinit(DRV_GPIO_PORT_A, DRV_GPIO_PIN_9 | DRV_GPIO_PIN_10);
#elif DRV_USART0_GPIO_SEL == DRV_USART0_GPIO_PB6_PB7
            drv_gpio_deinit(DRV_GPIO_PORT_B, DRV_GPIO_PIN_6 | DRV_GPIO_PIN_7);
#endif
            break;

        case DRV_UART_PORT_USART1:
            rcu_periph_clock_disable(RCU_USART1);
#if DRV_USART1_GPIO_SEL == DRV_USART1_GPIO_PA2_PA3
            drv_gpio_deinit(DRV_GPIO_PORT_A, DRV_GPIO_PIN_2 | DRV_GPIO_PIN_3);
#elif DRV_USART1_GPIO_SEL == DRV_USART1_GPIO_PB13_PB14
            drv_gpio_deinit(DRV_GPIO_PORT_B, DRV_GPIO_PIN_13 | DRV_GPIO_PIN_14);
#endif
            break;

        case DRV_UART_PORT_USART2:
            rcu_periph_clock_disable(RCU_USART2);
#if DRV_UART2_GPIO_SEL == DRV_UART2_GPIO_PB10_PB11
            drv_gpio_deinit(DRV_GPIO_PORT_B, DRV_GPIO_PIN_10 | DRV_GPIO_PIN_11);
#elif DRV_UART2_GPIO_SEL == DRV_UART2_GPIO_PC10_PC11
            drv_gpio_deinit(DRV_GPIO_PORT_C, DRV_GPIO_PIN_10 | DRV_GPIO_PIN_11);
#elif DRV_UART2_GPIO_SEL == DRV_UART2_GPIO_PD8_PD9
            drv_gpio_deinit(DRV_GPIO_PORT_D, DRV_GPIO_PIN_8 | DRV_GPIO_PIN_9);
#endif
            break;

        case DRV_UART_PORT_UART3:
            rcu_periph_clock_disable(RCU_UART3);
#if DRV_UART3_GPIO_SEL == DRV_UART3_GPIO_PC10_PC11
            drv_gpio_deinit(DRV_GPIO_PORT_C, DRV_GPIO_PIN_10 | DRV_GPIO_PIN_11);
#endif
            break;

        case DRV_UART_PORT_UART4:
            rcu_periph_clock_disable(RCU_UART4);
#if DRV_UART4_GPIO_SEL == DRV_UART4_GPIO_PC12_PD2
            drv_gpio_deinit(DRV_GPIO_PORT_C, DRV_GPIO_PIN_12);
            drv_gpio_deinit(DRV_GPIO_PORT_D, DRV_GPIO_PIN_2);
#endif
            break;

        default:
            break;
    }

    return 0;
}

/*********************************************************************
 * @brief   使能UART中断
 * @param   port    UART端口号
 * @return  0表示成功，-1表示失败
 * @note    使能RXNE中断，如果配置了IDLE则使能IDLE中断
 *********************************************************************/
static int _drv_uart_enable_interrupt(drv_uart_port_e port)
{
    uint32_t usart_base;
    drv_uart_ctrl_t *ctrl;
    uint32_t timeout = 10000U;

    if (port >= DRV_UART_PORT_MAX)
    {
        return DRV_UART_ERR_FAILED;
    }

    usart_base = s_usart_base[port];
    ctrl = &s_uart_ctrl[port];

    /* 清除残留的IDLEF标志（防止上电/软复位后残留的IDLEF触发中断）
     * 注：GD32F50x IDLEF清除方式：读STAT0 + 读DATA（参考官方IDLE_receive_interrupt示例）
     * 等待IDLEF置位后再清除，带超时保护防止死循环 */
    while ((RESET == usart_flag_get(usart_base, USART_FLAG_IDLE)) && (timeout > 0U))
    {
        timeout--;
    }
    (void)USART_STAT0(usart_base);         /* 读STAT0 */
    (void)usart_data_receive(usart_base);  /* 读DATA，清除IDLEF */

    /* 使能接收中断 */
    usart_interrupt_enable(usart_base, USART_INT_RBNE);

    /* 如果启用IDLE中断，使能IDLE中断 */
    if (ctrl->config.use_idle == true)
    {
        usart_interrupt_enable(usart_base, USART_INT_IDLE);
    }

    /* 使能错误中断（ERRIE 仅在 DMA 接收使能时有效，参考手册 ERRIE 描述） */
    if (ctrl->config.use_dma_rx == true)
    {
        usart_interrupt_enable(usart_base, USART_INT_ERR);
    }

    /* 使能 NVIC 中断控制器（必须！） */
    nvic_irq_enable(s_usart_nvic_config[port].irqn,
                    s_usart_nvic_config[port].preempt_priority,
                    s_usart_nvic_config[port].sub_priority);

    DRV_UART_LOGI("UART port %d interrupts enabled, IDLE=%d, NVIC=IRQ%d (Prio=%d,%d)",
                  port, ctrl->config.use_idle, s_usart_nvic_config[port].irqn,
                  s_usart_nvic_config[port].preempt_priority,
                  s_usart_nvic_config[port].sub_priority);

    return 0;
}

/*********************************************************************
 * @brief   禁能UART中断
 * @param   port    UART端口号
 * @return  0表示成功，-1表示失败
 * @note    无
 *********************************************************************/
static int _drv_uart_disable_interrupt(drv_uart_port_e port)
{
    uint32_t usart_base;
    uint32_t timeout = 10000U;

    if (port >= DRV_UART_PORT_MAX)
    {
        return DRV_UART_ERR_FAILED;
    }

    usart_base = s_usart_base[port];

    usart_interrupt_disable(usart_base, USART_INT_RBNE);
    usart_interrupt_disable(usart_base, USART_INT_IDLE);
    usart_interrupt_disable(usart_base, USART_INT_ERR);

    /* 清除残留的IDLEF标志（防止关闭中断期间残留，下次使能时误触发） */
    while ((RESET == usart_flag_get(usart_base, USART_FLAG_IDLE)) && (timeout > 0U))
    {
        timeout--;
    }
    (void)USART_STAT0(usart_base);        /* 读STAT0 */
    (void)usart_data_receive(usart_base);  /* 读DATA，清除IDLEF */

    /* 禁用 NVIC 中断控制器 */
    nvic_irq_disable(s_usart_nvic_config[port].irqn);

    DRV_UART_LOGD("UART port %d interrupts disabled", port);

    return 0;
}

/*********************************************************************
 * @brief   使能DMA接收
 * @param   port    UART端口号
 * @return  0表示成功，-1表示失败
 * @note    配置DMA通道并启动DMA接收
 *********************************************************************/
static int _drv_uart_enable_dma_rx(drv_uart_port_e port)
{
    uint32_t usart_base;
    drv_uart_ctrl_t *ctrl;
    drv_dma_channel_id_e dma_ch;
    drv_dma_config_t dma_config;
    uint32_t dma_request;

    /* 参数校验 */
    if (port >= DRV_UART_PORT_MAX)
    {
        DRV_UART_LOGE("Invalid port: %d", port);
        return DRV_UART_ERR_FAILED;
    }

    usart_base = s_usart_base[port];
    ctrl = &s_uart_ctrl[port];
    dma_ch = s_uart_rx_dma_ch[port];

    /* 检查是否支持DMA（UART4无DMA） */
    if (dma_ch >= DRV_DMA_MAX)
    {
        DRV_UART_LOGE("Port %d does not support DMA RX", port);
        return DRV_UART_ERR_FAILED;
    }

    /* 获取DMA请求源 */
    dma_request = s_uart_dma_request[port][0];  /* [0]=RX, [1]=TX */

    DRV_UART_LOGD("Enable DMA RX for port %d, channel=%d, request=0x%02X",
                  port, dma_ch, dma_request);

    /* 配置DMA参数 */
    dma_config.request_id = dma_request;
    dma_config.periph_addr = usart_base + 0x04U;  /* USART_DATA寄存器偏移 */
    dma_config.memory_addr = (uint32_t)ctrl->config.dma_rx_buf;
    dma_config.periph_width = DRV_DMA_WIDTH_8BIT;
    dma_config.memory_width = DRV_DMA_WIDTH_8BIT;
    dma_config.transfer_number = ctrl->config.dma_rx_buf_size;
    dma_config.direction = DRV_DMA_DIR_PERIPH_TO_MEMORY;
    dma_config.priority = DRV_DMA_PRIORITY_HIGH;
    dma_config.mode = DRV_DMA_MODE_NORMAL;
    dma_config.periph_inc = false;
    dma_config.memory_inc = true;

    /* 初始化DMA通道 */
    if (drv_dma_init(dma_ch, &dma_config) != DRV_DMA_ERR_OK)
    {
        DRV_UART_LOGE("DMA init failed for port %d", port);
        return DRV_UART_ERR_FAILED;
    }

    /* 使能UART DMA接收请求 */
    usart_dma_receive_config(usart_base, USART_DENR_ENABLE);

    /* 启动DMA传输 */
    if (drv_dma_start(dma_ch) != DRV_DMA_ERR_OK)
    {
        DRV_UART_LOGE("DMA start failed for port %d", port);
        usart_dma_receive_config(usart_base, USART_DENR_DISABLE);
        return DRV_UART_ERR_FAILED;
    }

    DRV_UART_LOGD("DMA RX enabled for port %d, buffer_size=%d",
                  port, ctrl->config.dma_rx_buf_size);

    return 0;
}

/*********************************************************************
 * @brief   禁能DMA接收
 * @param   port    UART端口号
 * @return  0表示成功，-1表示失败
 * @note    无
 *********************************************************************/
static int _drv_uart_disable_dma_rx(drv_uart_port_e port)
{
    uint32_t usart_base;
    drv_dma_channel_id_e dma_ch;

    /* 参数校验 */
    if (port >= DRV_UART_PORT_MAX)
    {
        return DRV_UART_ERR_FAILED;
    }

    usart_base = s_usart_base[port];
    dma_ch = s_uart_rx_dma_ch[port];

    /* 检查是否支持DMA */
    if (dma_ch >= DRV_DMA_MAX)
    {
        return 0;  /* UART4无DMA，直接返回成功 */
    }

    /* 停止DMA传输 */
    drv_dma_stop(dma_ch);

    /* 禁能UART DMA接收请求 */
    usart_dma_receive_config(usart_base, USART_DENR_DISABLE);

    /* 反初始化DMA通道 */
    drv_dma_deinit(dma_ch);

    DRV_UART_LOGD("DMA RX disabled for port %d", port);

    return 0;
}

/**
 * @brief  轮询发送（所有模式的基础降级方案）
 * @param  port UART端口号
 * @param  data  发送数据缓冲区
 * @param  len   发送数据长度
 * @return 实际发送字节数
 */
static int32_t _uart_send_polling(drv_uart_port_e port, const uint8_t *data, uint16_t len)
{
    uint32_t usart_base = s_usart_base[port];
    uint16_t i;
    TickType_t start_tick, elapsed_tick;

    DRV_UART_LOGD("UART port %d send %d bytes (polling mode)", port, len);

    start_tick = xTaskGetTickCount();

    for (i = 0; i < len; i++)
    {
        usart_data_transmit(usart_base, data[i]);

        /* 等待发送完成（带超时） */
        while (usart_flag_get(usart_base, USART_FLAG_TBE) == RESET)
        {
            elapsed_tick = xTaskGetTickCount() - start_tick;
            if (elapsed_tick >= pdMS_TO_TICKS(UART_TX_TIMEOUT_MS))
            {
                DRV_UART_LOGE("Polling TX TBE timeout at byte %d/%d", i, len);
                return i;  /* 返回已发送字节数 */
            }
        }
    }

    /* 等待最后一帧发送完成（带超时） */
    while (usart_flag_get(usart_base, USART_FLAG_TC) == RESET)
    {
        elapsed_tick = xTaskGetTickCount() - start_tick;
        if (elapsed_tick >= pdMS_TO_TICKS(UART_TX_TIMEOUT_MS))
        {
            DRV_UART_LOGE("Polling TX TC timeout");
            return len;  /* 数据已写入，返回总长度 */
        }
    }

    return len;
}

/**
 * @brief  DMA同步发送（查询硬件FTF标志）
 * @param  port UART端口号
 * @param  data  发送数据缓冲区
 * @param  len   发送数据长度
 * @return 实际发送字节数
 */
static int32_t _uart_send_dma_sync(drv_uart_port_e port, const uint8_t *data, uint16_t len)
{
    uint32_t usart_base = s_usart_base[port];
    drv_dma_channel_id_e dma_ch;
    drv_dma_config_t dma_config;
    uint32_t dma_request;
    uint32_t dma_periph;
    dma_channel_enum dma_ch_enum;
    TickType_t start_tick, elapsed_tick;

    dma_ch = s_uart_tx_dma_ch[port];

    /* 检查是否支持DMA（UART4无DMA） */
    if (dma_ch >= DRV_DMA_MAX)
    {
        DRV_UART_LOGW("Port %d does not support DMA TX, fallback to polling", port);
        return _uart_send_polling(port, data, len);
    }

    /* 获取DMA请求源 */
    dma_request = s_uart_dma_request[port][1];  /* [0]=RX, [1]=TX */

    DRV_UART_LOGD("UART port %d send %d bytes (DMA sync mode)", port, len);

    /* 配置DMA参数 */
    dma_config.request_id = dma_request;
    dma_config.periph_addr = usart_base + 0x04U;  /* USART_DATA寄存器偏移 */
    dma_config.memory_addr = (uint32_t)data;
    dma_config.periph_width = DRV_DMA_WIDTH_8BIT;
    dma_config.memory_width = DRV_DMA_WIDTH_8BIT;
    dma_config.transfer_number = len;
    dma_config.direction = DRV_DMA_DIR_MEMORY_TO_PERIPH;
    dma_config.priority = DRV_DMA_PRIORITY_HIGH;
    dma_config.mode = DRV_DMA_MODE_NORMAL;
    dma_config.periph_inc = false;
    dma_config.memory_inc = true;

    /* 初始化DMA通道 */
    if (drv_dma_init(dma_ch, &dma_config) != DRV_DMA_ERR_OK)
    {
        DRV_UART_LOGE("DMA TX init failed for port %d", port);
        return _uart_send_polling(port, data, len);
    }

    /* 使能UART DMA发送请求 */
    usart_dma_transmit_config(usart_base, USART_DENT_ENABLE);

    /* 启动DMA传输 */
    if (drv_dma_start(dma_ch) != DRV_DMA_ERR_OK)
    {
        DRV_UART_LOGE("DMA TX start failed for port %d", port);
        usart_dma_transmit_config(usart_base, USART_DENT_DISABLE);
        drv_dma_deinit(dma_ch);
        return _uart_send_polling(port, data, len);
    }

    /* 获取DMA外设基地址和通道枚举 */
    if (dma_ch < DRV_DMA1_CH0)
    {
        /* DMA0 通道 */
        dma_periph = DMA0;
        dma_ch_enum = (dma_channel_enum)dma_ch;  /* DMA0_CH0-6 */
    }
    else
    {
        /* DMA1 通道 */
        dma_periph = DMA1;
        dma_ch_enum = (dma_channel_enum)(dma_ch - DRV_DMA1_CH0);  /* DMA1_CH0 */
    }

    /* 等待FTF标志置位（带超时） */
    start_tick = xTaskGetTickCount();
    while (dma_flag_get(dma_periph, dma_ch_enum, DMA_FLAG_FTF) == RESET)
    {
        elapsed_tick = xTaskGetTickCount() - start_tick;
        if (elapsed_tick >= pdMS_TO_TICKS(UART_TX_TIMEOUT_MS))
        {
            DRV_UART_LOGE("DMA TX FTF timeout for port %d", port);
            drv_dma_stop(dma_ch);
            usart_dma_transmit_config(usart_base, USART_DENT_DISABLE);
            drv_dma_deinit(dma_ch);
            return _uart_send_polling(port, data, len);
        }
    }

    /* 清除FTF标志 */
    dma_flag_clear(dma_periph, dma_ch_enum, DMA_FLAG_FTF);

    /* 等待最后一帧发送完成（带超时） */
    start_tick = xTaskGetTickCount();
    while (usart_flag_get(usart_base, USART_FLAG_TC) == RESET)
    {
        elapsed_tick = xTaskGetTickCount() - start_tick;
        if (elapsed_tick >= pdMS_TO_TICKS(UART_TX_TIMEOUT_MS))
        {
            DRV_UART_LOGE("DMA TX TC timeout for port %d", port);
            break;  /* 超时退出，数据已发送到UART */
        }
    }

    /* 禁能UART DMA发送请求 */
    usart_dma_transmit_config(usart_base, USART_DENT_DISABLE);

    /* 反初始化DMA通道 */
    drv_dma_deinit(dma_ch);

    return len;
}

/**
 * @brief  DMA异步发送（FreeRTOS信号量等待）
 * @param  port UART端口号
 * @param  data  发送数据缓冲区
 * @param  len   发送数据长度
 * @return 实际发送字节数
 */
static int32_t _uart_send_dma_async(drv_uart_port_e port, const uint8_t *data, uint16_t len)
{
    uint32_t usart_base = s_usart_base[port];
    drv_uart_ctrl_t *ctrl = &s_uart_ctrl[port];
    drv_dma_channel_id_e dma_ch;
    drv_dma_config_t dma_config;
    uint32_t dma_request;
    TickType_t start_tick, elapsed_tick;

    dma_ch = s_uart_tx_dma_ch[port];

    /* 检查是否支持DMA（UART4无DMA） */
    if (dma_ch >= DRV_DMA_MAX)
    {
        DRV_UART_LOGW("Port %d does not support DMA TX, fallback to polling", port);
        return _uart_send_polling(port, data, len);
    }

    /* 获取DMA请求源 */
    dma_request = s_uart_dma_request[port][1];  /* [0]=RX, [1]=TX */

    DRV_UART_LOGD("UART port %d send %d bytes (DMA async mode)", port, len);

    /* 清空信号量（确保初始状态） */
    xSemaphoreTake(ctrl->tx_sem, 0);

    /* 获取全局DMA TX锁（保护s_current_dma_tx_port） */
    if (xSemaphoreTake(s_dma_tx_mutex, pdMS_TO_TICKS(UART_TX_TIMEOUT_MS)) != pdTRUE)
    {
        DRV_UART_LOGE("Global DMA TX mutex timeout for port %d", port);
        drv_dma_deinit(dma_ch);
        return DRV_UART_ERR_TIMEOUT;
    }

    /* 记录当前DMA发送端口（用于中断回调） */
    s_current_dma_tx_port = port;

    /* 配置DMA参数 */
    dma_config.request_id = dma_request;
    dma_config.periph_addr = usart_base + 0x04U;  /* USART_DATA寄存器偏移 */
    dma_config.memory_addr = (uint32_t)data;
    dma_config.periph_width = DRV_DMA_WIDTH_8BIT;
    dma_config.memory_width = DRV_DMA_WIDTH_8BIT;
    dma_config.transfer_number = len;
    dma_config.direction = DRV_DMA_DIR_MEMORY_TO_PERIPH;
    dma_config.priority = DRV_DMA_PRIORITY_HIGH;
    dma_config.mode = DRV_DMA_MODE_NORMAL;
    dma_config.periph_inc = false;
    dma_config.memory_inc = true;

    /* 初始化DMA通道 */
    if (drv_dma_init(dma_ch, &dma_config) != DRV_DMA_ERR_OK)
    {
        DRV_UART_LOGE("DMA TX init failed for port %d", port);
        /* 释放全局锁 */
        xSemaphoreGive(s_dma_tx_mutex);
        return _uart_send_polling(port, data, len);
    }

    /* 使能DMA传输完成中断（异步模式必须，使用dma_driver API） */
    if (drv_dma_int_enable(dma_ch, DRV_DMA_INT_FTF, 3) != DRV_DMA_ERR_OK)
    {
        DRV_UART_LOGE("DMA TX interrupt enable failed for port %d", port);
        drv_dma_deinit(dma_ch);
        xSemaphoreGive(s_dma_tx_mutex);
        return _uart_send_polling(port, data, len);
    }

    /* 注册DMA TX完成回调（使用dma_driver回调机制） */
    if (drv_dma_callback_register(dma_ch, DRV_DMA_INT_FTF, uart_dma_tx_isr_callback) != DRV_DMA_ERR_OK)
    {
        DRV_UART_LOGE("DMA TX callback register failed for port %d", port);
        drv_dma_int_disable(dma_ch, DRV_DMA_INT_FTF);
        drv_dma_deinit(dma_ch);
        xSemaphoreGive(s_dma_tx_mutex);
        return _uart_send_polling(port, data, len);
    }

    /* 使能UART DMA发送请求 */
    usart_dma_transmit_config(usart_base, USART_DENT_ENABLE);

    /* 启动DMA传输 */
    if (drv_dma_start(dma_ch) != DRV_DMA_ERR_OK)
    {
        DRV_UART_LOGE("DMA TX start failed for port %d", port);
        usart_dma_transmit_config(usart_base, USART_DENT_DISABLE);
        drv_dma_int_disable(dma_ch, DRV_DMA_INT_FTF);
        drv_dma_callback_unregister(dma_ch, DRV_DMA_INT_FTF);
        drv_dma_deinit(dma_ch);
        xSemaphoreGive(s_dma_tx_mutex);
        return _uart_send_polling(port, data, len);
    }

    /* 等待信号量（任务挂起，CPU可执行其他任务） */
    if (xSemaphoreTake(ctrl->tx_sem, pdMS_TO_TICKS(UART_TX_TIMEOUT_MS)) != pdTRUE)
    {
        DRV_UART_LOGE("DMA TX timeout for port %d", port);

        /* 停止DMA并禁能中断 */
        drv_dma_stop(dma_ch);
        drv_dma_int_disable(dma_ch, DRV_DMA_INT_FTF);
        usart_dma_transmit_config(usart_base, USART_DENT_DISABLE);

        /* 清除标记（必须在注销回调之前） */
        s_current_dma_tx_port = DRV_UART_PORT_MAX;
        drv_dma_callback_unregister(dma_ch, DRV_DMA_INT_FTF);

        /* 释放全局锁 */
        xSemaphoreGive(s_dma_tx_mutex);

        drv_dma_deinit(dma_ch);
        return DRV_UART_ERR_TIMEOUT;
    }

    /* 等待最后一帧发送完成（带超时） */
    start_tick = xTaskGetTickCount();
    while (usart_flag_get(usart_base, USART_FLAG_TC) == RESET)
    {
        elapsed_tick = xTaskGetTickCount() - start_tick;
        if (elapsed_tick >= pdMS_TO_TICKS(UART_TX_TIMEOUT_MS))
        {
            DRV_UART_LOGE("DMA async TX TC timeout for port %d", port);
            break;  /* 超时退出，数据已发送到UART */
        }
    }

    /* 禁能DMA中断（防止新的中断触发） */
    drv_dma_int_disable(dma_ch, DRV_DMA_INT_FTF);

    /* 禁能UART DMA发送请求 */
    usart_dma_transmit_config(usart_base, USART_DENT_DISABLE);

    /* 清除DMA发送端口标记（必须在注销回调之前，防止中断访问） */
    s_current_dma_tx_port = DRV_UART_PORT_MAX;

    /* 注销DMA TX回调 */
    drv_dma_callback_unregister(dma_ch, DRV_DMA_INT_FTF);

    /* 释放全局DMA TX锁 */
    xSemaphoreGive(s_dma_tx_mutex);

    /* 反初始化DMA通道 */
    drv_dma_deinit(dma_ch);

    return len;
}

/**
 * @brief  DMA TX 完成回调（在中断中调用）
 * @note   此函数由 dma_driver 的中断回调机制调用
 */
static void uart_dma_tx_isr_callback(void)
{
    /* 使用静态变量精确定位发送端口 */
    if (s_current_dma_tx_port < DRV_UART_PORT_MAX &&
        s_uart_ctrl[s_current_dma_tx_port].tx_sem != NULL)
    {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(s_uart_ctrl[s_current_dma_tx_port].tx_sem, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

/**
 * @brief   环形队列写入（线程安全）
 * @param   port    UART端口号
 * @param   data    数据指针
 * @param   len     数据长度
 * @return  实际写入的字节数
 */
static int32_t _uart_ring_queue_push(drv_uart_port_e port, const uint8_t *data, uint16_t len)
{
    drv_uart_ctrl_t *ctrl = &s_uart_ctrl[port];
    drv_uart_ring_tx_ctrl_t *ring_ctrl = ctrl->ring_tx_ctrl;
    uint16_t write_idx, count, free_space, i;

    if (ring_ctrl == NULL || data == NULL || len == 0 ||
        ring_ctrl->tx_ring_queue == NULL)
    {
        return DRV_UART_ERR_INVALID_PARAM;
    }

    /* 获取互斥锁 */
    if (xSemaphoreTake(ring_ctrl->tx_ring_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        DRV_UART_LOGW("Ring queue mutex timeout for port %d", port);
        return DRV_UART_ERR_TIMEOUT;
    }

    write_idx = ring_ctrl->tx_ring_write_idx;
    count = ring_ctrl->tx_ring_count;
    free_space = ring_ctrl->tx_ring_queue_size - count;

    /* 检查空间 */
    if (len > free_space)
    {
        len = free_space;  /* 截断到可用空间 */
    }

    /* 写入数据 */
    for (i = 0; i < len; i++)
    {
        ring_ctrl->tx_ring_queue[write_idx] = data[i];
        write_idx = (write_idx + 1) % ring_ctrl->tx_ring_queue_size;
    }

    /* 更新状态 */
    ring_ctrl->tx_ring_write_idx = write_idx;
    ring_ctrl->tx_ring_count += len;

    /* 释放互斥锁 */
    xSemaphoreGive(ring_ctrl->tx_ring_mutex);

    return len;
}

/**
 * @brief   环形队列读取（线程安全）
 * @param   port        UART端口号
 * @param   data        数据缓冲区
 * @param   max_len     最大读取长度
 * @return  实际读取的字节数
 */
static uint16_t _uart_ring_queue_pop(drv_uart_port_e port, uint8_t *data, uint16_t max_len)
{
    drv_uart_ctrl_t *ctrl = &s_uart_ctrl[port];
    drv_uart_ring_tx_ctrl_t *ring_ctrl = ctrl->ring_tx_ctrl;
    uint16_t read_idx, count, i;

    if (ring_ctrl == NULL || data == NULL || max_len == 0 ||
        ring_ctrl->tx_ring_queue == NULL)
    {
        return 0;
    }

    /* 获取互斥锁 */
    if (xSemaphoreTake(ring_ctrl->tx_ring_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        DRV_UART_LOGW("Ring queue mutex timeout for port %d", port);
        return 0;
    }

    read_idx = ring_ctrl->tx_ring_read_idx;
    count = ring_ctrl->tx_ring_count;

    /* 检查数据量 */
    if (max_len > count)
    {
        max_len = count;  /* 截断到可用数据 */
    }

    /* 读取数据 */
    for (i = 0; i < max_len; i++)
    {
        data[i] = ring_ctrl->tx_ring_queue[read_idx];
        read_idx = (read_idx + 1) % ring_ctrl->tx_ring_queue_size;
    }

    /* 更新状态 */
    ring_ctrl->tx_ring_read_idx = read_idx;
    ring_ctrl->tx_ring_count -= max_len;

    /* 释放互斥锁 */
    xSemaphoreGive(ring_ctrl->tx_ring_mutex);

    return max_len;
}

/**
 * @brief   环形队列读取（中断安全版本，无锁）
 * @param   ring_ctrl   环形队列控制指针
 * @param   data        数据缓冲区
 * @param   max_len     最大读取长度
 * @return  实际读取的字节数
 * @note    仅用于 FTF 中断回调，使用临界区保护计数器访问
 */
static uint16_t _uart_ring_queue_pop_isr(drv_uart_ring_tx_ctrl_t *ring_ctrl, uint8_t *data, uint16_t max_len)
{
    uint16_t read_idx, count, i;

    if (ring_ctrl == NULL || data == NULL || max_len == 0 ||
        ring_ctrl->tx_ring_queue == NULL)
    {
        return 0;
    }

    /* 进入临界区（关中断） */
    taskENTER_CRITICAL();

    /* 直接读取（中断中不使用互斥锁） */
    read_idx = ring_ctrl->tx_ring_read_idx;
    count = ring_ctrl->tx_ring_count;

    /* 检查数据量 */
    if (max_len > count)
    {
        max_len = count;  /* 截断到可用数据 */
    }

    /* 读取数据 */
    for (i = 0; i < max_len; i++)
    {
        data[i] = ring_ctrl->tx_ring_queue[read_idx];
        read_idx = (read_idx + 1) % ring_ctrl->tx_ring_queue_size;
    }

    /* 更新状态 */
    ring_ctrl->tx_ring_read_idx = read_idx;
    ring_ctrl->tx_ring_count -= max_len;

    /* 退出临界区（开中断） */
    taskEXIT_CRITICAL();

    return max_len;
}

/**
 * @brief   DMA 半传输中断回调（HTF）
 * @note    当前驱动未实现HTF优化，FTF双缓冲已满足绝大多数应用场景
 * @note    HTF优化仅在极端高频场景（>1Mbps持续数据流）下有显著收益
 */
static void uart_dma_ring_tx_htf_callback(void)
{
    /* HTF中断，当前阶段不处理 */
    /* FTF双缓冲方案已能保证数据连续性，DMA停顿时间<100μs，对实际应用无影响 */
}

/**
 * @brief   DMA 全传输中断回调（FTF）
 * @note    DMA 发送完成，自动切换到下一个缓冲区并继续发送
 */
static void uart_dma_ring_tx_ftf_callback(void)
{
    /* 使用静态变量精确定位发送端口 */
    if (s_current_ring_tx_port < DRV_UART_PORT_MAX &&
        s_uart_ctrl[s_current_ring_tx_port].ring_tx_ctrl != NULL)
    {
        drv_uart_port_e port = s_current_ring_tx_port;
        drv_uart_ctrl_t *ctrl = &s_uart_ctrl[port];
        drv_uart_ring_tx_ctrl_t *ring_ctrl = ctrl->ring_tx_ctrl;
        uint8_t next_buf_idx;
        uint16_t data_len;

        /* 切换到下一个缓冲区 */
        next_buf_idx = (ring_ctrl->current_buf_idx + 1) % 2;

        /* 从环形队列预填充下一个缓冲区（中断安全版本） */
        data_len = _uart_ring_queue_pop_isr(ring_ctrl,
                                            &ring_ctrl->dma_tx_buf[next_buf_idx * ring_ctrl->dma_tx_buf_size],
                                            ring_ctrl->dma_tx_buf_size);

        if (data_len > 0)
        {
            /* 有数据，继续发送 */
            ring_ctrl->current_buf_idx = next_buf_idx;
            ring_ctrl->buf_fill_len[next_buf_idx] = data_len;
            ring_ctrl->buf_state[next_buf_idx] = DRV_UART_DMA_BUF_TX;

            DRV_UART_LOGD("FTF: Switch to buf %d, len=%d", next_buf_idx, data_len);

            /* 重新配置 DMA 并启动（不重新初始化） */
            uint32_t usart_base = s_usart_base[port];
            drv_dma_channel_id_e dma_ch = s_uart_tx_dma_ch[port];
            uint32_t dma_request = s_uart_dma_request[port][1];

            /* 检查 DMA 通道有效性 */
            if (dma_ch >= DRV_DMA_MAX)
            {
                ring_ctrl->dma_tx_active = false;
                s_current_ring_tx_port = DRV_UART_PORT_MAX;
                return;
            }

            /* 停止当前 DMA */
            drv_dma_stop(dma_ch);

            /* 重新配置 DMA 传输参数 */
            drv_dma_deinit(dma_ch);

            drv_dma_config_t dma_cfg;
            dma_cfg.request_id = dma_request;
            dma_cfg.periph_addr = usart_base + 0x04U;  /* DATA寄存器 */
            dma_cfg.memory_addr = (uint32_t)&ring_ctrl->dma_tx_buf[next_buf_idx * ring_ctrl->dma_tx_buf_size];
            dma_cfg.periph_width = DRV_DMA_WIDTH_8BIT;
            dma_cfg.memory_width = DRV_DMA_WIDTH_8BIT;
            dma_cfg.transfer_number = data_len;
            dma_cfg.direction = DRV_DMA_DIR_MEMORY_TO_PERIPH;
            dma_cfg.priority = DRV_DMA_PRIORITY_HIGH;
            dma_cfg.mode = DRV_DMA_MODE_NORMAL;
            dma_cfg.periph_inc = false;
            dma_cfg.memory_inc = true;

            if (drv_dma_init(dma_ch, &dma_cfg) != DRV_DMA_ERR_OK)
            {
                DRV_UART_LOGE("FTF: DMA re-init failed for port %d", port);
                usart_dma_transmit_config(usart_base, USART_DENT_DISABLE);
                ring_ctrl->dma_tx_active = false;
                s_current_ring_tx_port = DRV_UART_PORT_MAX;
                return;
            }

            /* 启动 DMA */
            if (drv_dma_start(dma_ch) != DRV_DMA_ERR_OK)
            {
                DRV_UART_LOGE("FTF: DMA start failed for port %d", port);
                usart_dma_transmit_config(usart_base, USART_DENT_DISABLE);
                ring_ctrl->dma_tx_active = false;
                s_current_ring_tx_port = DRV_UART_PORT_MAX;
                return;
            }

            /* 保持 dma_tx_active = true，继续循环发送 */
        }
        else
        {
            /* 无数据，停止发送 */
            DRV_UART_LOGD("FTF: No data, stop DMA for port %d", port);
            ring_ctrl->dma_tx_active = false;
            s_current_ring_tx_port = DRV_UART_PORT_MAX;  /* 清除标记 */
        }
    }
}

/**
 * @brief   中断发送模式（TXE中断，无需DMA）
 * @param   port    UART端口号
 * @param   data    发送数据缓冲区
 * @param   len     发送数据长度
 * @return  实际发送字节数
 * @note    数据写入TX缓冲区，使能TXE中断，由中断逐字节发送
 * @note    TX缓冲区为线性缓冲区（非环形），每次发送必须等待上一次完成
 * @note    适合中等频率发送场景，高频连续发送建议使用DMA_DUAL_BUF模式
 */
static int32_t _uart_send_interrupt(drv_uart_port_e port, const uint8_t *data, uint16_t len)
{
    drv_uart_ctrl_t *ctrl = &s_uart_ctrl[port];
    drv_uart_tx_irq_ctrl_t *tx_ctrl = ctrl->tx_irq_ctrl;
    uint32_t usart_base = s_usart_base[port];
    uint16_t i;

    if (tx_ctrl == NULL)
    {
        DRV_UART_LOGE("TX IRQ control is NULL for port %d", port);
        return DRV_UART_ERR_FAILED;
    }

    /* 检查是否正在发送 */
    if (tx_ctrl->tx_active == true)
    {
        DRV_UART_LOGW("TX IRQ busy for port %d", port);
        return DRV_UART_ERR_NOT_READY;
    }

    /* 检查数据长度 */
    if (len == 0 || len > tx_ctrl->tx_buf_size)
    {
        DRV_UART_LOGE("Invalid TX length %d for port %d (max=%d)", len, port, tx_ctrl->tx_buf_size);
        return DRV_UART_ERR_INVALID_PARAM;
    }

    DRV_UART_LOGD("UART port %d send %d bytes (interrupt mode)", port, len);

    /* 进入临界区（防止中断同时访问） */
    taskENTER_CRITICAL();

    /* 数据写入TX缓冲区 */
    for (i = 0; i < len; i++)
    {
        tx_ctrl->tx_buf[i] = data[i];
    }
    tx_ctrl->tx_write_idx = len;
    tx_ctrl->tx_read_idx = 0;
    tx_ctrl->tx_count = len;
    tx_ctrl->tx_total_len = len;
    tx_ctrl->tx_active = true;

    /* 退出临界区 */
    taskEXIT_CRITICAL();

    /* 使能TXE中断，启动发送 */
    usart_interrupt_enable(usart_base, USART_INT_TBE);

    return len;
}

/**
 * @brief   TXE中断处理（在中断中调用）
 * @param   port    UART端口号
 * @note    逐字节从TX缓冲区取数据写入DR寄存器
 */
static void _uart_tx_irq_handler(drv_uart_port_e port)
{
    drv_uart_ctrl_t *ctrl = &s_uart_ctrl[port];
    drv_uart_tx_irq_ctrl_t *tx_ctrl = ctrl->tx_irq_ctrl;
    uint32_t usart_base = s_usart_base[port];

    if (tx_ctrl == NULL || tx_ctrl->tx_active == false)
    {
        return;
    }

    /* 进入临界区（保护缓冲区访问） */
    taskENTER_CRITICAL();

    if (tx_ctrl->tx_count > 0)
    {
        /* 从缓冲区取1字节写入DR寄存器 */
        USART_DATA(usart_base) = tx_ctrl->tx_buf[tx_ctrl->tx_read_idx];
        tx_ctrl->tx_read_idx = (tx_ctrl->tx_read_idx + 1) % tx_ctrl->tx_buf_size;
        tx_ctrl->tx_count--;
    }
    else
    {
        /* 缓冲区为空，发送完成 */
        tx_ctrl->tx_active = false;

        /* 退出临界区（在关闭中断之前） */
        taskEXIT_CRITICAL();

        /* 关闭TXE中断 */
        usart_interrupt_disable(usart_base, USART_INT_TBE);

        /* 注意：以下代码仍在中断上下文中执行
         * 回调函数必须快速执行，不能阻塞或调用FreeRTOS API（非FromISR版本） */
        if (ctrl->config.tx_callback != NULL)
        {
            ctrl->config.tx_callback(port, tx_ctrl->tx_total_len);
        }
        return;
    }

    /* 退出临界区 */
    taskEXIT_CRITICAL();
}

/**
 * @brief   启动 DMA RING 发送
 * @param   port    UART端口号
 * @return  0=成功，<0=失败
 */
static int32_t _uart_send_ring_start(drv_uart_port_e port)
{
    drv_uart_ctrl_t *ctrl = &s_uart_ctrl[port];
    drv_uart_ring_tx_ctrl_t *ring_ctrl = ctrl->ring_tx_ctrl;
    uint32_t usart_base = s_usart_base[port];
    drv_dma_channel_id_e dma_ch = s_uart_tx_dma_ch[port];
    uint32_t dma_request = s_uart_dma_request[port][1];  /* [0]=RX, [1]=TX */
    drv_dma_config_t dma_cfg;
    uint16_t data_len;

    if (ring_ctrl == NULL)
    {
        return DRV_UART_ERR_FAILED;
    }

    /* 检查是否支持DMA（UART4无DMA） */
    if (dma_ch >= DRV_DMA_MAX)
    {
        DRV_UART_LOGW("Port %d does not support DMA TX, fallback to polling", port);
        return DRV_UART_ERR_FAILED;
    }

    /* 从环形队列取数据到 Buffer 0 */
    data_len = _uart_ring_queue_pop(port, ring_ctrl->dma_tx_buf, ring_ctrl->dma_tx_buf_size);

    if (data_len == 0)
    {
        return 0;  /* 无数据 */
    }

    /* 配置并启动 DMA */
    drv_dma_deinit(dma_ch);

    dma_cfg.request_id = dma_request;
    dma_cfg.periph_addr = usart_base + 0x04U;  /* DATA寄存器 */
    dma_cfg.memory_addr = (uint32_t)ring_ctrl->dma_tx_buf;  /* Buffer 0 */
    dma_cfg.periph_width = DRV_DMA_WIDTH_8BIT;
    dma_cfg.memory_width = DRV_DMA_WIDTH_8BIT;
    dma_cfg.transfer_number = data_len;
    dma_cfg.direction = DRV_DMA_DIR_MEMORY_TO_PERIPH;
    dma_cfg.priority = DRV_DMA_PRIORITY_HIGH;
    dma_cfg.mode = DRV_DMA_MODE_NORMAL;
    dma_cfg.periph_inc = false;
    dma_cfg.memory_inc = true;

    if (drv_dma_init(dma_ch, &dma_cfg) != DRV_DMA_ERR_OK)
    {
        return DRV_UART_ERR_FAILED;
    }

    /* 注册回调 */
    drv_dma_callback_register(dma_ch, DRV_DMA_INT_FTF, uart_dma_ring_tx_ftf_callback);
    drv_dma_callback_register(dma_ch, DRV_DMA_INT_HTF, uart_dma_ring_tx_htf_callback);

    /* 使能 UART DMA 发送 */
    usart_dma_transmit_config(usart_base, USART_DENT_ENABLE);

    /* 使能 DMA 中断 */
    drv_dma_int_enable(dma_ch, DRV_DMA_INT_FTF, 3);
    drv_dma_int_enable(dma_ch, DRV_DMA_INT_HTF, 3);

    /* 启动 DMA */
    if (drv_dma_start(dma_ch) != DRV_DMA_ERR_OK)
    {
        usart_dma_transmit_config(usart_base, USART_DENT_DISABLE);
        drv_dma_int_disable(dma_ch, DRV_DMA_INT_FTF);
        drv_dma_int_disable(dma_ch, DRV_DMA_INT_HTF);
        drv_dma_callback_unregister(dma_ch, DRV_DMA_INT_FTF);
        drv_dma_callback_unregister(dma_ch, DRV_DMA_INT_HTF);
        drv_dma_deinit(dma_ch);
        return DRV_UART_ERR_FAILED;
    }

    /* 设置活动标志 */
    ring_ctrl->dma_tx_active = true;
    ring_ctrl->current_buf_idx = 0;  /* 从 Buffer 0 开始 */
    ring_ctrl->buf_fill_len[0] = data_len;
    ring_ctrl->buf_state[0] = DRV_UART_DMA_BUF_TX;
    ring_ctrl->buf_state[1] = DRV_UART_DMA_BUF_IDLE;
    s_current_ring_tx_port = port;  /* 记录当前端口 */

    return data_len;
}

/*********************************************************************
 * 接口函数实现
 *********************************************************************/

/*********************************************************************
 * @brief   初始化UART端口
 * @param   config  UART配置结构体指针（应用层传入）
 * @return  0表示成功，-1表示失败（参数错误或端口已初始化）
 * @note    应用层需确保配置参数合法，所有内存资源由应用层分配管理
 *********************************************************************/
int drv_uart_init(const drv_uart_config_t *config)
{
    drv_uart_ctrl_t *ctrl;
    drv_uart_port_e port;

    /* 断言检查：配置指针不能为空 */
    DRV_UART_ASSERT(config != NULL);

    /* 参数检查 */
    if (_drv_uart_check_param(config) != 0)
    {
        DRV_UART_LOGE("Invalid config parameter for port");
        return DRV_UART_ERR_FAILED;
    }

    port = config->port;

    /* 检查UART端口是否配置为未使用 */
    switch (port)
    {
        case DRV_UART_PORT_USART0:
#if DRV_USART0_GPIO_SEL == DRV_USART0_NO_USE
            DRV_UART_LOGE("USART0 is not configured (NO_USE), please check DRV_USART0_GPIO_SEL");
            return DRV_UART_ERR_FAILED;
#endif
            break;

        case DRV_UART_PORT_USART1:
#if DRV_USART1_GPIO_SEL == DRV_USART1_NO_USE
            DRV_UART_LOGE("USART1 is not configured (NO_USE), please check DRV_USART1_GPIO_SEL");
            return DRV_UART_ERR_FAILED;
#endif
            break;

        case DRV_UART_PORT_USART2:
#if DRV_UART2_GPIO_SEL == DRV_UART2_NO_USE
            DRV_UART_LOGE("UART2 is not configured (NO_USE), please check DRV_UART2_GPIO_SEL");
            return DRV_UART_ERR_FAILED;
#endif
            break;

        case DRV_UART_PORT_UART3:
#if DRV_UART3_GPIO_SEL == DRV_UART3_NO_USE
            DRV_UART_LOGE("UART3 is not configured (NO_USE), please check DRV_UART3_GPIO_SEL");
            return DRV_UART_ERR_FAILED;
#endif
            break;

        case DRV_UART_PORT_UART4:
#if DRV_UART4_GPIO_SEL == DRV_UART4_NO_USE
            DRV_UART_LOGE("UART4 is not configured (NO_USE), please check DRV_UART4_GPIO_SEL");
            return DRV_UART_ERR_FAILED;
#endif
            break;

        default:
            break;
    }

    ctrl = &s_uart_ctrl[port];

    /* 断言检查：端口号必须合法 */
    DRV_UART_ASSERT(port < DRV_UART_PORT_MAX);

    /* 检查端口是否已注册 */
    if (ctrl->state != DRV_UART_STATE_UNINIT)
    {
        DRV_UART_LOGW("UART port %d already registered, state=%d", port, ctrl->state);
        return DRV_UART_ERR_FAILED;
    }

    /* 保存配置参数 */
    memcpy(&ctrl->config, config, sizeof(drv_uart_config_t));
    ctrl->tx_mode = config->tx_mode;

    /* 初始化接收模式标识 */
    if (config->use_dma_rx == true)
    {
        ctrl->rx_mode = (config->use_ringbuf == true) ? DRV_UART_RX_MODE_DMA_RINGBUF : DRV_UART_RX_MODE_DMA_RXBUF;
    }
    else
    {
        ctrl->rx_mode = (config->use_ringbuf == true) ? DRV_UART_RX_MODE_NODMA_RINGBUF : DRV_UART_RX_MODE_NODMA_RXBUF;
    }

    /* 初始化中断使能标志 */
    ctrl->irq_enabled = DRV_UART_IRQ_RBNE | DRV_UART_IRQ_ERR;  /* RXNE和ERR总是使能 */
    if (config->use_idle == true)
    {
        ctrl->irq_enabled |= DRV_UART_IRQ_IDLE;
    }

    /* 根据配置创建发送互斥锁 */
    if (config->use_tx_mutex == true)
    {
        ctrl->tx_mutex = xSemaphoreCreateMutex();
        if (ctrl->tx_mutex == NULL)
        {
            DRV_UART_LOGE("Failed to create TX mutex for port %d", port);
            return DRV_UART_ERR_FAILED;
        }

        DRV_UART_LOGD("TX mutex created for port %d", port);
    }
    else
    {
        ctrl->tx_mutex = NULL;
        DRV_UART_LOGD("TX mutex disabled for port %d", port);
    }

    /* 根据 TX 模式创建资源 */
    switch (ctrl->tx_mode)
    {
        case UART_TX_MODE_INTERRUPT:
            /* 创建中断 TX 控制结构 */
            if (config->tx_buf == NULL || config->tx_buf_size == 0)
            {
                DRV_UART_LOGE("INTERRUPT mode requires tx_buf from application");
                if (ctrl->tx_mutex != NULL)
                {
                    vSemaphoreDelete(ctrl->tx_mutex);
                }
                return DRV_UART_ERR_FAILED;
            }

            ctrl->tx_irq_ctrl = (drv_uart_tx_irq_ctrl_t *)pvPortMalloc(sizeof(drv_uart_tx_irq_ctrl_t));
            if (ctrl->tx_irq_ctrl == NULL)
            {
                DRV_UART_LOGE("Failed to allocate TX IRQ control for port %d", port);
                if (ctrl->tx_mutex != NULL)
                {
                    vSemaphoreDelete(ctrl->tx_mutex);
                }
                return DRV_UART_ERR_FAILED;
            }

            /* 初始化中断 TX 控制 */
            memset(ctrl->tx_irq_ctrl, 0, sizeof(drv_uart_tx_irq_ctrl_t));
            ctrl->tx_irq_ctrl->tx_buf = config->tx_buf;
            ctrl->tx_irq_ctrl->tx_buf_size = config->tx_buf_size;
            ctrl->tx_irq_ctrl->tx_active = false;

            DRV_UART_LOGI("TX IRQ control created for port %d, buf_size=%d", port, config->tx_buf_size);
            break;

        case UART_TX_MODE_DMA_ASYNC:
            /* 创建全局DMA TX互斥锁（仅首次） */
            if (s_dma_tx_mutex == NULL)
            {
                s_dma_tx_mutex = xSemaphoreCreateMutex();
                if (s_dma_tx_mutex == NULL)
                {
                    DRV_UART_LOGE("Failed to create global DMA TX mutex");
                    return DRV_UART_ERR_FAILED;
                }
                DRV_UART_LOGD("Global DMA TX mutex created");
            }

            /* 创建DMA TX完成信号量 */
            ctrl->tx_sem = xSemaphoreCreateBinary();
            if (ctrl->tx_sem == NULL)
            {
                DRV_UART_LOGE("Failed to create TX semaphore for port %d", port);
                if (ctrl->tx_mutex != NULL)
                {
                    vSemaphoreDelete(ctrl->tx_mutex);
                }
                return DRV_UART_ERR_FAILED;
            }
            DRV_UART_LOGD("TX semaphore created for port %d", port);
            break;

        case UART_TX_MODE_DMA_DUAL_BUF:
            /* 创建环形 TX 控制结构（仅双缓冲模式需要） */
            if (config->use_dma_tx == true)
            {
                /* 检查应用层是否传入了缓冲区 */
                if (config->dma_tx_buf == NULL || config->dma_tx_buf_size == 0 ||
                    config->tx_ring_queue == NULL || config->tx_ring_queue_size == 0)
                {
                    DRV_UART_LOGE("DUAL_BUF mode requires dma_tx_buf and tx_ring_queue from application");
                    if (ctrl->tx_mutex != NULL)
                    {
                        vSemaphoreDelete(ctrl->tx_mutex);
                    }
                    return DRV_UART_ERR_FAILED;
                }

                ctrl->ring_tx_ctrl = (drv_uart_ring_tx_ctrl_t *)pvPortMalloc(sizeof(drv_uart_ring_tx_ctrl_t));
                if (ctrl->ring_tx_ctrl == NULL)
                {
                    DRV_UART_LOGE("Failed to allocate ring TX control for port %d", port);
                    if (ctrl->tx_mutex != NULL)
                    {
                        vSemaphoreDelete(ctrl->tx_mutex);
                    }
                    return DRV_UART_ERR_FAILED;
                }

                /* 初始化环形队列 */
                memset(ctrl->ring_tx_ctrl, 0, sizeof(drv_uart_ring_tx_ctrl_t));
                ctrl->ring_tx_ctrl->dma_tx_buf = config->dma_tx_buf;
                ctrl->ring_tx_ctrl->dma_tx_buf_size = config->dma_tx_buf_size;
                ctrl->ring_tx_ctrl->tx_ring_queue = config->tx_ring_queue;
                ctrl->ring_tx_ctrl->tx_ring_queue_size = config->tx_ring_queue_size;

                ctrl->ring_tx_ctrl->tx_ring_mutex = xSemaphoreCreateMutex();
                if (ctrl->ring_tx_ctrl->tx_ring_mutex == NULL)
                {
                    DRV_UART_LOGE("Failed to create ring queue mutex for port %d", port);
                    vPortFree(ctrl->ring_tx_ctrl);
                    ctrl->ring_tx_ctrl = NULL;
                    if (ctrl->tx_mutex != NULL)
                    {
                        vSemaphoreDelete(ctrl->tx_mutex);
                    }
                    return DRV_UART_ERR_FAILED;
                }

                DRV_UART_LOGI("Ring TX control created for port %d", port);
            }
            break;

        case UART_TX_MODE_POLLING:
        case UART_TX_MODE_DMA_SYNC:
        default:
            /* 轮询和DMA同步模式不需要额外资源（INTERRUPT模式已在上面处理） */
            break;
    }

    /* 初始化状态 */
    ctrl->state = DRV_UART_STATE_INIT;

    /* 硬件初始化 */
    if (_drv_uart_hw_init(port) != 0)
    {
        DRV_UART_LOGE("Hardware init failed for port %d", port);

        /* 释放 ring_tx_ctrl */
        if (ctrl->ring_tx_ctrl != NULL)
        {
            if (ctrl->ring_tx_ctrl->tx_ring_mutex != NULL)
            {
                vSemaphoreDelete(ctrl->ring_tx_ctrl->tx_ring_mutex);
            }
            vPortFree(ctrl->ring_tx_ctrl);
            ctrl->ring_tx_ctrl = NULL;
        }

        vSemaphoreDelete(ctrl->tx_mutex);
        ctrl->state = DRV_UART_STATE_UNINIT;
        return DRV_UART_ERR_FAILED;
    }

    /* 如果启用DMA接收 */
    if (config->use_dma_rx == true)
    {
        if (_drv_uart_enable_dma_rx(port) != 0)
        {
            DRV_UART_LOGE("DMA RX enable failed for port %d", port);
            _drv_uart_hw_deinit(port);

            /* 释放 ring_tx_ctrl */
            if (ctrl->ring_tx_ctrl != NULL)
            {
                if (ctrl->ring_tx_ctrl->tx_ring_mutex != NULL)
                {
                    vSemaphoreDelete(ctrl->ring_tx_ctrl->tx_ring_mutex);
                }
                vPortFree(ctrl->ring_tx_ctrl);
                ctrl->ring_tx_ctrl = NULL;
            }

            vSemaphoreDelete(ctrl->tx_mutex);
            ctrl->state = DRV_UART_STATE_UNINIT;
            return DRV_UART_ERR_FAILED;
        }
    }

    /* 切换到活跃状态（必须在中断使能前！） */
    ctrl->state = DRV_UART_STATE_ACTIVE;

    /* 使能中断 */
    if (_drv_uart_enable_interrupt(port) != 0)
    {
        DRV_UART_LOGE("Interrupt enable failed for port %d", port);
        if (config->use_dma_rx == true)
        {
            _drv_uart_disable_dma_rx(port);
        }
        _drv_uart_hw_deinit(port);

        /* 释放 ring_tx_ctrl */
        if (ctrl->ring_tx_ctrl != NULL)
        {
            if (ctrl->ring_tx_ctrl->tx_ring_mutex != NULL)
            {
                vSemaphoreDelete(ctrl->ring_tx_ctrl->tx_ring_mutex);
            }
            vPortFree(ctrl->ring_tx_ctrl);
            ctrl->ring_tx_ctrl = NULL;
        }

        vSemaphoreDelete(ctrl->tx_mutex);
        ctrl->state = DRV_UART_STATE_UNINIT;
        return DRV_UART_ERR_FAILED;
    }

    DRV_UART_LOGI("UART port %d initialized successfully, baudrate=%d, TX_Mode=%d, DMA_RX=%d, IDLE=%d, RingBuf=%d",
                  port, config->baudrate, config->tx_mode, config->use_dma_rx, config->use_idle, config->use_ringbuf);

    return 0;
}

/*********************************************************************
 * @brief   卸载UART端口
 * @param   port    UART端口号
 * @return  0表示成功，-1表示失败（端口未初始化）
 * @note    释放驱动内部资源，应用层传入的内存由应用层自行管理
 *********************************************************************/
int drv_uart_deinit(drv_uart_port_e port)
{
    drv_uart_ctrl_t *ctrl;

    /* 断言检查：端口号必须合法 */
    DRV_UART_ASSERT(port < DRV_UART_PORT_MAX);

    if (port >= DRV_UART_PORT_MAX)
    {
        DRV_UART_LOGE("Invalid UART port: %d", port);
        return DRV_UART_ERR_FAILED;
    }

    ctrl = &s_uart_ctrl[port];

    /* 检查端口状态 */
    if (ctrl->state == DRV_UART_STATE_UNINIT)
    {
        DRV_UART_LOGW("UART port %d not initialized", port);
        return DRV_UART_ERR_FAILED;
    }

    /* 关闭中断 */
    _drv_uart_disable_interrupt(port);

    /* 关闭DMA */
    if (ctrl->config.use_dma_rx == true)
    {
        _drv_uart_disable_dma_rx(port);
    }

    /* 硬件去初始化 */
    _drv_uart_hw_deinit(port);

    /* 释放互斥锁 */
    if (ctrl->tx_mutex != NULL)
    {
        vSemaphoreDelete(ctrl->tx_mutex);
        ctrl->tx_mutex = NULL;
    }

    /* 释放DMA TX信号量 */
    if (ctrl->tx_sem != NULL)
    {
        vSemaphoreDelete(ctrl->tx_sem);
        ctrl->tx_sem = NULL;
    }

    /* 释放环形 TX 控制结构 */
    if (ctrl->ring_tx_ctrl != NULL)
    {
        if (ctrl->ring_tx_ctrl->tx_ring_mutex != NULL)
        {
            vSemaphoreDelete(ctrl->ring_tx_ctrl->tx_ring_mutex);
        }
        vPortFree(ctrl->ring_tx_ctrl);
        ctrl->ring_tx_ctrl = NULL;
        DRV_UART_LOGD("Ring TX control freed for port %d", port);
    }

    /* 释放中断 TX 控制结构 */
    if (ctrl->tx_irq_ctrl != NULL)
    {
        vPortFree(ctrl->tx_irq_ctrl);
        ctrl->tx_irq_ctrl = NULL;
        DRV_UART_LOGD("TX IRQ control freed for port %d", port);
    }

    /* 清空配置 */
    memset(&ctrl->config, 0, sizeof(drv_uart_config_t));
    memset(&ctrl->context, 0, sizeof(drv_uart_context_t));

    /* 状态重置 */
    ctrl->state = DRV_UART_STATE_UNINIT;

    DRV_UART_LOGI("UART port %d deinitialized successfully", port);

    return 0;
}

/*********************************************************************
 * @brief   发送数据
 * @param   port    UART端口号
 * @param   data    待发送数据指针
 * @param   len     待发送数据长度（字节）
 * @return  实际发送的字节数，-1表示失败
 * @note    线程安全，支持普通发送和DMA发送（根据配置自动选择）
 *********************************************************************/
int drv_uart_send(drv_uart_port_e port, const uint8_t *data, uint16_t len)
{
    drv_uart_ctrl_t *ctrl;
    int send_len = 0;

    /* 断言检查：参数合法性 */
    DRV_UART_ASSERT(port < DRV_UART_PORT_MAX);
    DRV_UART_ASSERT(data != NULL);
    DRV_UART_ASSERT(len > 0);

    /* 参数校验 */
    if (port >= DRV_UART_PORT_MAX)
    {
        DRV_UART_LOGE("Invalid UART port: %d", port);
        return DRV_UART_ERR_FAILED;
    }

    if (data == NULL || len == 0)
    {
        DRV_UART_LOGE("Invalid send parameter");
        return DRV_UART_ERR_FAILED;
    }

    ctrl = &s_uart_ctrl[port];

    /* 状态检查：只有活跃状态才能发送 */
    if (ctrl->state != DRV_UART_STATE_ACTIVE)
    {
        DRV_UART_LOGW("UART port %d not active, state=%d", port, ctrl->state);
        return DRV_UART_ERR_FAILED;
    }

    /* 如果启用了互斥锁，获取互斥锁（线程安全） */
    if (ctrl->tx_mutex != NULL)
    {
        /* 使用超时机制，避免永久阻塞 */
#if DRV_UART_TX_MUTEX_TIMEOUT_MS > 0
        if (xSemaphoreTake(ctrl->tx_mutex, pdMS_TO_TICKS(DRV_UART_TX_MUTEX_TIMEOUT_MS)) != pdTRUE)
        {
            DRV_UART_LOGE("TX mutex timeout for port %d", port);
            return DRV_UART_ERR_FAILED;
        }
#else
        if (xSemaphoreTake(ctrl->tx_mutex, portMAX_DELAY) != pdTRUE)
        {
            DRV_UART_LOGE("Failed to take TX mutex for port %d", port);
            return DRV_UART_ERR_FAILED;
        }
#endif
    }

    /* 根据运行时 TX 模式选择发送方式 */
    switch (ctrl->tx_mode)
    {
        case UART_TX_MODE_POLLING:
            /* 轮询发送模式 */
            send_len = _uart_send_polling(port, data, len);
            break;

        case UART_TX_MODE_INTERRUPT:
            /* 中断发送模式（TXE中断，无需DMA） */
            if (ctrl->tx_irq_ctrl != NULL)
            {
                send_len = _uart_send_interrupt(port, data, len);
            }
            else
            {
                DRV_UART_LOGW("TX IRQ control is NULL for port %d, fallback to polling", port);
                send_len = _uart_send_polling(port, data, len);
            }
            break;

        case UART_TX_MODE_DMA_SYNC:
            /* DMA同步发送模式 */
            if (ctrl->config.use_dma_tx == true && s_uart_tx_dma_ch[port] < DRV_DMA_MAX)
            {
                send_len = _uart_send_dma_sync(port, data, len);
            }
            else
            {
                send_len = _uart_send_polling(port, data, len);
            }
            break;

        case UART_TX_MODE_DMA_ASYNC:
            /* DMA异步发送模式 */
            if (ctrl->config.use_dma_tx == true && s_uart_tx_dma_ch[port] < DRV_DMA_MAX)
            {
                send_len = _uart_send_dma_async(port, data, len);
            }
            else
            {
                send_len = _uart_send_polling(port, data, len);
            }
            break;

        case UART_TX_MODE_DMA_DUAL_BUF:
            /* DMA双缓冲循环发送模式 */
            if (ctrl->config.use_dma_tx == true && ctrl->ring_tx_ctrl != NULL && s_uart_tx_dma_ch[port] < DRV_DMA_MAX)
            {
                /* 写入环形队列（线程安全） */
                send_len = _uart_ring_queue_push(port, data, len);

                if (send_len > 0)
                {
                    /* 检查并启动 DMA（在 tx_mutex 保护下，防止竞态） */
                    /* 注意：tx_mutex 已在函数开头获取 */
                    if (!ctrl->ring_tx_ctrl->dma_tx_active)
                    {
                        _uart_send_ring_start(port);
                    }
                }
            }
            else
            {
                send_len = _uart_send_polling(port, data, len);
            }
            break;

        default:
            /* 未知模式，降级到轮询 */
            DRV_UART_LOGW("Unknown TX mode %d, fallback to polling", ctrl->tx_mode);
            send_len = _uart_send_polling(port, data, len);
            break;
    }

    /* 释放互斥锁 */
    if (ctrl->tx_mutex != NULL)
    {
        xSemaphoreGive(ctrl->tx_mutex);
    }

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
int drv_uart_read(drv_uart_port_e port, uint8_t *data, uint16_t len)
{
    drv_uart_ctrl_t *ctrl;
    int read_len = 0;
    uint32_t ringbuf_used_size;
    uint32_t ringbuf_threshold;

    /* 断言检查：参数合法性 */
    DRV_UART_ASSERT(port < DRV_UART_PORT_MAX);
    DRV_UART_ASSERT(data != NULL);
    DRV_UART_ASSERT(len > 0);

    /* 参数校验 */
    if (port >= DRV_UART_PORT_MAX)
    {
        DRV_UART_LOGE("Invalid UART port: %d", port);
        return DRV_UART_ERR_FAILED;
    }

    if (data == NULL || len == 0)
    {
        DRV_UART_LOGE("Invalid read parameter");
        return DRV_UART_ERR_FAILED;
    }

    ctrl = &s_uart_ctrl[port];

    /* 状态检查 */
    if (ctrl->state != DRV_UART_STATE_ACTIVE)
    {
        DRV_UART_LOGW("UART port %d not active, state=%d", port, ctrl->state);
        return DRV_UART_ERR_FAILED;
    }

    /* 判断是否启用RingBuffer */
    if (ctrl->config.use_ringbuf == true)
    {
        /* 从RingBuffer读取数据 */
        if (ctrl->config.ringbuf == NULL)
        {
            DRV_UART_LOGE("RingBuffer pointer is NULL for port %d", port);
            return DRV_UART_ERR_FAILED;
        }

        read_len = my_rb_read(ctrl->config.ringbuf, data, len);

        /* 读取数据后，如果之前已触发半满回调，现在检查是否可以复位标志 */
        if (ctrl->ringbuf_half_triggered && read_len > 0)
        {
            ringbuf_used_size = my_rb_get_data_size(ctrl->config.ringbuf);
            ringbuf_threshold = ctrl->config.ringbuf->size / 2;

            /* 当使用量低于阈值的 50% 时，复位触发标志，允许下次再次触发 */
            /* 临界区保护：防止与中断中的写操作竞争 */
            if (ringbuf_used_size < ringbuf_threshold / 2)
            {
                taskENTER_CRITICAL();
                ctrl->ringbuf_half_triggered = false;
                taskEXIT_CRITICAL();
            }
        }

        DRV_UART_LOGD("UART port %d read %d bytes from RingBuffer", port, read_len);
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
            DRV_UART_LOGD("UART port %d no data available", port);
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

        DRV_UART_LOGD("UART port %d read %d bytes from RX buffer", port, read_len);
    }

    return read_len;
}

/*********************************************************************
 * @brief   挂起UART端口（低功耗）
 * @param   port    UART端口号
 * @return  0表示成功，-1表示失败（端口状态不允许挂起）
 * @note    唤醒串口仅关闭TX，普通串口彻底关闭硬件
 *********************************************************************/
int drv_uart_suspend(drv_uart_port_e port)
{
    uint32_t usart_base;
    drv_uart_ctrl_t *ctrl;

    /* 断言检查：端口号必须合法 */
    DRV_UART_ASSERT(port < DRV_UART_PORT_MAX);

    /* 参数校验 */
    if (port >= DRV_UART_PORT_MAX)
    {
        DRV_UART_LOGE("Invalid UART port: %d", port);
        return DRV_UART_ERR_FAILED;
    }

    ctrl = &s_uart_ctrl[port];

    /* 状态检查：只有活跃状态才能挂起 */
    if (ctrl->state != DRV_UART_STATE_ACTIVE)
    {
        DRV_UART_LOGW("UART port %d not active, state=%d", port, ctrl->state);
        return DRV_UART_ERR_FAILED;
    }

    usart_base = s_usart_base[port];

    DRV_UART_LOGD("Suspend UART port %d, wakeup_capable=%d", port, ctrl->config.is_wakeup_capable);

    /* 保存现场 */
    ctrl->context.baudrate = ctrl->config.baudrate;
    ctrl->context.stat = USART_STAT0(usart_base);
    ctrl->context.ctrl0 = USART_CTL0(usart_base);
    ctrl->context.ctrl1 = USART_CTL1(usart_base);
    ctrl->context.ctrl2 = USART_CTL2(usart_base);

    /* 差异化处理：唤醒串口 vs 普通串口 */
    if (ctrl->config.is_wakeup_capable == true)
    {
        /* 唤醒串口：仅关闭TX，保持RX和中断 */
        usart_transmit_config(usart_base, USART_TRANSMIT_DISABLE);

        DRV_UART_LOGI("UART port %d suspended (wakeup mode, TX only)", port);
    }
    else
    {
        /* 普通串口：彻底关闭硬件 */
        /* 关闭DMA */
        if (ctrl->config.use_dma_rx == true)
        {
            _drv_uart_disable_dma_rx(port);
        }

        /* 关闭中断 */
        _drv_uart_disable_interrupt(port);

        /* 关闭UART外设 */
        usart_disable(usart_base);

        DRV_UART_LOGI("UART port %d suspended (normal mode, hardware off)", port);
    }

    /* 更新状态 */
    ctrl->state = DRV_UART_STATE_SUSPENDED;

    return 0;
}

/*********************************************************************
 * @brief   恢复UART端口
 * @param   port    UART端口号
 * @return  0表示成功，-1表示失败（端口状态不允许恢复）
 * @note    恢复挂起的UART端口到活跃状态
 *********************************************************************/
int drv_uart_resume(drv_uart_port_e port)
{
    uint32_t usart_base;
    drv_uart_ctrl_t *ctrl;

    /* 断言检查：端口号必须合法 */
    DRV_UART_ASSERT(port < DRV_UART_PORT_MAX);

    /* 参数校验 */
    if (port >= DRV_UART_PORT_MAX)
    {
        DRV_UART_LOGE("Invalid UART port: %d", port);
        return DRV_UART_ERR_FAILED;
    }

    ctrl = &s_uart_ctrl[port];

    /* 状态检查：只有挂起状态才能恢复 */
    if (ctrl->state != DRV_UART_STATE_SUSPENDED)
    {
        DRV_UART_LOGW("UART port %d not suspended, state=%d", port, ctrl->state);
        return DRV_UART_ERR_FAILED;
    }

    usart_base = s_usart_base[port];

    DRV_UART_LOGD("Resume UART port %d, wakeup_capable=%d", port, ctrl->config.is_wakeup_capable);

    /* 差异化恢复：唤醒串口 vs 普通串口 */
    if (ctrl->config.is_wakeup_capable == true)
    {
        /* 唤醒串口：仅恢复TX */
        usart_transmit_config(usart_base, USART_TRANSMIT_ENABLE);

        DRV_UART_LOGI("UART port %d resumed (wakeup mode, TX only)", port);
    }
    else
    {
        /* 普通串口：重新初始化硬件 */
        /* 使能UART外设 */
        usart_enable(usart_base);

        /* 使能中断 */
        _drv_uart_enable_interrupt(port);

        /* 如果启用DMA接收，重新启动 */
        if (ctrl->config.use_dma_rx == true)
        {
            _drv_uart_enable_dma_rx(port);
        }

        DRV_UART_LOGI("UART port %d resumed (normal mode, hardware on)", port);
    }

    /* 更新状态 */
    ctrl->state = DRV_UART_STATE_ACTIVE;

    return 0;
}

/*********************************************************************
 * @brief   关闭UART端口
 * @param   port    UART端口号
 * @return  0表示成功，-1表示失败（端口未初始化）
 * @note    彻底关闭硬件，可通过resume恢复，不释放控制实例
 *********************************************************************/
int drv_uart_shutdown(drv_uart_port_e port)
{
    uint32_t usart_base;
    drv_uart_ctrl_t *ctrl;

    /* 断言检查：端口号必须合法 */
    DRV_UART_ASSERT(port < DRV_UART_PORT_MAX);

    /* 参数校验 */
    if (port >= DRV_UART_PORT_MAX)
    {
        DRV_UART_LOGE("Invalid UART port: %d", port);
        return DRV_UART_ERR_FAILED;
    }

    ctrl = &s_uart_ctrl[port];

    /* 状态检查：未初始化状态不能关闭 */
    if (ctrl->state == DRV_UART_STATE_UNINIT)
    {
        DRV_UART_LOGW("UART port %d not initialized", port);
        return DRV_UART_ERR_FAILED;
    }

    usart_base = s_usart_base[port];

    DRV_UART_LOGD("Shutdown UART port %d, current state=%d", port, ctrl->state);

    /* 关闭DMA */
    if (ctrl->config.use_dma_rx == true)
    {
        _drv_uart_disable_dma_rx(port);
    }

    /* 关闭中断 */
    _drv_uart_disable_interrupt(port);

    /* 关闭UART外设 */
    usart_disable(usart_base);

    /* 更新状态 */
    ctrl->state = DRV_UART_STATE_SHUTDOWN;

    DRV_UART_LOGI("UART port %d shutdown successfully", port);

    return 0;
}

/*********************************************************************
 * @brief   获取UART端口状态
 * @param   port    UART端口号
 * @return  UART状态枚举值，-1表示失败（端口无效）
 * @note    用于调试和问题排查
 *********************************************************************/
int drv_uart_get_state(drv_uart_port_e port)
{
    /* 断言检查：端口号必须合法 */
    DRV_UART_ASSERT(port < DRV_UART_PORT_MAX);

    if (port >= DRV_UART_PORT_MAX)
    {
        DRV_UART_LOGE("Invalid UART port: %d", port);
        return DRV_UART_ERR_FAILED;
    }

    DRV_UART_LOGD("UART port %d state: %d", port, s_uart_ctrl[port].state);

    return (int)s_uart_ctrl[port].state;
}

/*********************************************************************
 * @brief   查询可读取的接收数据长度
 * @param   port    UART端口号
 * @return  可读取的字节数，-1表示失败（端口无效）
 * @note    应用层可通过此接口查询有多少数据待读取，避免无效调用uart_read
 *********************************************************************/
int drv_uart_get_rx_len(drv_uart_port_e port)
{
    drv_uart_ctrl_t *ctrl;
    uint16_t available_len = 0;

    /* 断言检查：端口号必须合法 */
    DRV_UART_ASSERT(port < DRV_UART_PORT_MAX);

    if (port >= DRV_UART_PORT_MAX)
    {
        DRV_UART_LOGE("Invalid UART port: %d", port);
        return DRV_UART_ERR_FAILED;
    }

    ctrl = &s_uart_ctrl[port];

    /* 状态检查 */
    if (ctrl->state != DRV_UART_STATE_ACTIVE)
    {
        DRV_UART_LOGW("UART port %d not active, state=%d", port, ctrl->state);
        return DRV_UART_ERR_FAILED;
    }

    /* 判断是否启用RingBuffer */
    if (ctrl->config.use_ringbuf == true)
    {
        /* 从RingBuffer查询数据长度 */
        if (ctrl->config.ringbuf == NULL)
        {
            DRV_UART_LOGE("RingBuffer pointer is NULL for port %d", port);
            return DRV_UART_ERR_FAILED;
        }

        available_len = (uint16_t)my_rb_get_data_size(ctrl->config.ringbuf);
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

    DRV_UART_LOGD("UART port %d rx available: %d bytes", port, available_len);

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
void drv_uart_irq_handler(drv_uart_port_e port)
{
    uint32_t usart_base;
    drv_uart_ctrl_t *ctrl;
    uint8_t data;
    uint16_t dma_len = 0;
    uint32_t stat0;
    uint32_t int0;
    uint32_t ringbuf_used_size;
    uint32_t ringbuf_threshold;

    /* 断言检查：端口号必须合法 */
    DRV_UART_ASSERT(port < DRV_UART_PORT_MAX);

    if (port >= DRV_UART_PORT_MAX)
    {
        return;
    }

    usart_base = s_usart_base[port];
    ctrl = &s_uart_ctrl[port];

    /* 检查端口状态，非活跃状态不处理中断 */
    if (ctrl->state != DRV_UART_STATE_ACTIVE)
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
            case DRV_UART_RX_MODE_NODMA_RINGBUF:
                /* 非DMA + RingBuffer模式 */
                my_rb_write(ctrl->config.ringbuf, &data, 1);

                /* 检查 RingBuffer 是否半满（触发应用层及时处理，仅触发一次） */
                if (ctrl->config.rx_callback != NULL && !ctrl->ringbuf_half_triggered)
                {
                    ringbuf_used_size = my_rb_get_data_size(ctrl->config.ringbuf);
                    ringbuf_threshold = ctrl->config.ringbuf->size / 2;  /* 半满阈值 */

                    if (ringbuf_used_size >= ringbuf_threshold)
                    {
                        ctrl->ringbuf_half_triggered = true;  /* 标记已触发，防止重复 */
                        ctrl->config.rx_callback(port, ringbuf_used_size);
                    }
                }
                break;

            case DRV_UART_RX_MODE_NODMA_RXBUF:
                /* 非DMA + rx_buf模式（循环缓冲区） */
            {
                /* 计算下一个写指针位置 */
                uint16_t next_write_idx = (ctrl->rx_write_index + 1) % ctrl->config.rx_buf_size;

                /* 检查是否会覆盖未读数据 */
                if (next_write_idx == ctrl->rx_read_index)
                {
                    /* 缓冲区满，触发溢出错误 */
                    if (ctrl->config.error_callback != NULL)
                    {
                        ctrl->config.error_callback(port, DRV_UART_ERROR_OVERRUN);
                    }
                    /* 丢弃当前字节 */
                    break;
                }

                /* 写入数据 */
                ctrl->config.rx_buf[ctrl->rx_write_index] = data;
                ctrl->rx_write_index = next_write_idx;
                break;
            }

            default:
                /* DMA模式：不处理RXNE，由DMA自动搬运 */
                break;
        }
    }

    /* 2. 处理IDLE中断（空闲帧，一帧数据接收完成） */
    if ((ctrl->irq_enabled & DRV_UART_IRQ_IDLE) && (stat0 & USART_STAT0_IDLEF) && (int0 & USART_CTL0_IDLEIE))
    {
        /* 清除IDLE标志：读STAT0 + 读DATA（参考官方示例） */
        (void)usart_data_receive(usart_base);

        /* 如果启用DMA接收，计算DMA已接收的数据长度 */
        if (ctrl->config.use_dma_rx == true)
        {
            drv_dma_channel_id_e dma_ch = s_uart_rx_dma_ch[port];

            /* 获取DMA剩余计数，计算已接收长度 */
            if (dma_ch < DRV_DMA_MAX)
            {
                uint32_t dma_periph;
                dma_channel_enum dma_ch_enum;
                uint16_t remaining_count;

                /* 获取DMA外设基地址和通道枚举 */
                if (dma_ch < DRV_DMA1_CH0)
                {
                    /* DMA0 通道 */
                    dma_periph = DMA0;
                    dma_ch_enum = (dma_channel_enum)dma_ch;
                }
                else
                {
                    /* DMA1 通道 */
                    dma_periph = DMA1;
                    dma_ch_enum = (dma_channel_enum)(dma_ch - DRV_DMA1_CH0);
                }

                /* 计算已接收长度 = 总长度 - 剩余计数 */
                remaining_count = (uint16_t)dma_transfer_number_get(dma_periph, dma_ch_enum);
                dma_len = ctrl->config.dma_rx_buf_size - remaining_count;

            }
            else
            {
                dma_len = 0;  /* UART4无DMA */
            }

            /* 将DMA缓冲区数据写入RingBuffer */
            if (ctrl->config.use_ringbuf == true && ctrl->config.ringbuf != NULL)
            {
                if (dma_len > 0)
                {
                    my_rb_write(ctrl->config.ringbuf, ctrl->config.dma_rx_buf, dma_len);
                }
            }

            /* 重启DMA接收 */
            drv_dma_channel_id_e restart_dma_ch = s_uart_rx_dma_ch[port];
            if (restart_dma_ch < DRV_DMA_MAX)
            {
                /* 先停止 DMA */
                drv_dma_stop(restart_dma_ch);

                /* 重新配置传输数量 */
                uint32_t restart_dma_periph;
                dma_channel_enum restart_dma_ch_enum;

                if (restart_dma_ch < DRV_DMA1_CH0)
                {
                    restart_dma_periph = DMA0;
                    restart_dma_ch_enum = (dma_channel_enum)restart_dma_ch;
                }
                else
                {
                    restart_dma_periph = DMA1;
                    restart_dma_ch_enum = (dma_channel_enum)(restart_dma_ch - DRV_DMA1_CH0);
                }

                /* 直接重新启动 DMA（不需要重新 init） */
                dma_transfer_number_config(restart_dma_periph, restart_dma_ch_enum, ctrl->config.dma_rx_buf_size);
                drv_dma_start(restart_dma_ch);
            }
        }
        else
        {
            /* 非DMA模式：计算接收长度 */
            if (ctrl->config.use_ringbuf == true && ctrl->config.ringbuf != NULL)
            {
                /* RingBuffer模式：查询当前数据量 */
                dma_len = (uint16_t)my_rb_get_data_size(ctrl->config.ringbuf);
            }
            else
            {
                /* rx_buf模式：计算写指针与读指针的差值 */
                if (ctrl->rx_write_index >= ctrl->rx_read_index)
                {
                    dma_len = ctrl->rx_write_index - ctrl->rx_read_index;
                }
                else
                {
                    dma_len = ctrl->config.rx_buf_size - ctrl->rx_read_index + ctrl->rx_write_index;
                }
            }
        }

        /* 调用接收完成回调 */
        if (ctrl->config.rx_callback != NULL)
        {
            ctrl->config.rx_callback(port, dma_len);
        }
    }

    /* 3. 处理错误中断（仅 DMA 接收模式下 ERRIE 有效，覆盖 FERR/ORERR/NERR）
     * 注：PERR 由 CTL0 的 PERRIE 独立控制，不在 ERRIE 范围内。
     * 非 DMA 模式下错误标志通过 RBNE 中断连带报告。
     * 错误标志清除方式：读 STAT0 + 读 DATA。
     */
    if (ctrl->irq_enabled & DRV_UART_IRQ_ERR)
    {
        uint32_t err_flags = stat0 & (USART_STAT0_FERR | USART_STAT0_ORERR |
                                       USART_STAT0_NERR);
        if (err_flags)
        {
            /* 读DATA清除所有错误标志（读STAT0已在ISR入口完成） */
            (void)usart_data_receive(usart_base);

            if (err_flags & USART_STAT0_FERR)
            {
                if (ctrl->config.error_callback != NULL)
                {
                    ctrl->config.error_callback(port, DRV_UART_ERROR_FRAME);
                }
            }

            if (err_flags & USART_STAT0_ORERR)
            {
                if (ctrl->config.error_callback != NULL)
                {
                    ctrl->config.error_callback(port, DRV_UART_ERROR_OVERRUN);
                }
            }

            if (err_flags & USART_STAT0_NERR)
            {
                if (ctrl->config.error_callback != NULL)
                {
                    ctrl->config.error_callback(port, DRV_UART_ERROR_NOISE);
                }
            }
        }
    }

    /* 4. 处理TXE中断（发送数据寄存器空） */
    if ((stat0 & USART_STAT0_TBE) && (int0 & USART_CTL0_TBEIE))
    {
        /* 调用TXE中断处理函数 */
        _uart_tx_irq_handler(port);
    }

    /* 5. 处理TC中断（发送完成） */
    if ((stat0 & USART_STAT0_TC) && (int0 & USART_CTL0_TCIE))
    {
        USART_STAT0(usart_base) = ~USART_STAT0_TC;  /* 写0清除TC */

        /* TODO: 发送完成回调 */
    }
}
