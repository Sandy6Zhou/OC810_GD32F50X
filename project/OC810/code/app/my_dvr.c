/********************************************************************
**版权所有：         深圳市几米物联有限公司
**文件名称：        my_dvr.c
**文件描述：        DVR视频模块驱动任务实现
**当前版本：        V1.0
**作    者：        伍玉蛟 (wuyujiao@jimiiot.com)
**完成日期：        2026.06.15
*********************************************************************
** 功能描述：       1. DVR视频模块通信与控制
**                 2. 电源管理（PA8控制）
**                 3. 永久阻塞式消息处理循环
*********************************************************************/

#include "my_comm.h"

/*********************************************************************
 * 内部宏定义
 *********************************************************************/

 /** DVR电源控制引脚 */
#define D_DVR_POWER_PORT        DRV_GPIO_PORT_A
#define D_DVR_POWER_PIN         DRV_GPIO_PIN_8

/** DVR电源开启后等待稳定时间（毫秒） */
#define D_DVR_POWER_ON_DELAY_MS (200U)

/** 定义RingBuffer缓冲区大小 */
#define D_UART_RING_BUF_SIZE    512

/** 定义UART发送缓冲区大小（需覆盖最大帧，含大数据1024字节payload）
 *  TX队列节点数据会拷贝到此缓冲区，再由UART中断发送 */
#define D_UART_TX_BUF_SIZE      (D_DVR_TX_ESCAPE_LARGE_BUF_SIZE)

/** 发送队列深度（最多缓存的发送帧数量） */
#define D_DVR_TX_QUEUE_DEPTH    (10U)

/*********************************************************************
 * 内部数据结构定义
 *********************************************************************/

/*********************************************************************
 * 内部辅助函数声明
 *********************************************************************/
static void my_dvr_uart_rx_callback(drv_uart_port_e port, uint16_t len);
static void my_dvr_uart_tx_callback(drv_uart_port_e port, uint16_t len);
static void my_dvr_uart_error_callback(drv_uart_port_e port, drv_uart_error_e err);
static int  my_dvr_uart_send_wrapper(const uint8_t *data, uint16_t len);
static void my_dvr_msg_handler(const my_msg_t *msg);
static void my_dvr_power_on(void);
static void my_dvr_power_off(void);
static void my_dvr_power_restart(void);

/*********************************************************************
 * 内部全局变量
 *********************************************************************/

/** 发送缓冲区（UART驱动使用） */
static uint8_t s_tx_buf[D_UART_TX_BUF_SIZE];

/** RingBuffer缓冲区（UART接收使用） */
static uint8_t s_ring_buf[D_UART_RING_BUF_SIZE];

/** RingBuffer控制块 */
static my_rb_t s_ringbuf;

/** 发送队列控制块（TX动态内存管理） */
static my_tq_ctrl_t s_tx_queue;

/** DVR电源控制GPIO配置 */
static const drv_gpio_config_t s_dvr_power_gpio_cfg = {
        .port = D_DVR_POWER_PORT,
        .pin = D_DVR_POWER_PIN,
        .mode = DRV_GPIO_MODE_OUTPUT,
        .otype = DRV_GPIO_OTYPE_PP,
        .speed = DRV_GPIO_SPEED_LEVEL0,
        .pupd = DRV_GPIO_PUPD_NONE,
        .af = DRV_GPIO_AF_0,
        .initial_state = false
    };

/** UART配置结构体（依赖上述缓冲区和控制块） */
static drv_uart_config_t uart_cfg = {
        .port = DRV_UART_PORT_USART1,
        .baudrate = 115200,
        .rx_buf = NULL,         /* RingBuffer 模式下无需 rx_buf */
        .rx_buf_size = 0,
        .dma_rx_buf = NULL,     /* RingBuffer 模式下无需 dma_rx_buf */
        .dma_rx_buf_size = 0,
        .ringbuf = &s_ringbuf,
        .use_dma_rx = false,    /* RingBuffer 模式下无需 dma_rx_buf */
        .use_idle = true,       /* RingBuffer 模式下需 idle空闲中断 */
        .use_ringbuf = true,    /* 使用 RingBuffer */
        .use_dma_tx = false,
        .tx_mode = UART_TX_MODE_INTERRUPT,   /* 中断方式发送 */
        .tx_buf = s_tx_buf,                  /* 发送缓冲区 */
        .tx_buf_size = D_UART_TX_BUF_SIZE,   /* 发送缓冲区大小 */
        .use_tx_mutex = false,               /* 不使用发送互斥锁 */
        .is_wakeup_capable = false,          /* 不使用唤醒功能 */
        .rx_callback = my_dvr_uart_rx_callback,         /* 接收回调函数 */
        .tx_callback = my_dvr_uart_tx_callback,         /* 发送完成回调函数 */
        .error_callback = my_dvr_uart_error_callback    /* 错误回调函数 */
    };

/*********************************************************************
 * 内部辅助函数实现
 *********************************************************************/

/*********************************************************************
 * @brief   UART发送包装函数
 * @param   data  数据指针
 * @param   len   数据长度
 * @return  实际发送的字节数
 * @note    适配 tx_queue 的发送函数接口
 *********************************************************************/
static int  my_dvr_uart_send_wrapper(const uint8_t *data, uint16_t len)
{
    return drv_uart_send(DRV_UART_PORT_USART1, data, len);
}

/*********************************************************************
 * @brief   UART错误回调函数
 * @param   port UART端口号
 * @param   err  错误类型
 * @return  none
 * @note    处理UART错误事件，中断上下文，使用 my_msg_send_from_isr
 *********************************************************************/
static void my_dvr_uart_error_callback(drv_uart_port_e port, drv_uart_error_e err)
{
    my_msg_t msg = {
        .id = MY_MSG_ID_DVR_UART_ERROR,
        .data = NULL,
        .len = err
    };

    my_msg_send_from_isr(MSG_QUEUE_DVR, &msg, 0);
}

/*********************************************************************
 * @brief   UART接收回调函数
 * @param   port UART端口号
 * @param   len  接收数据长度
 * @return  none
 * @note    中断上半部：仅发消息通知任务，不处理数据
 *          中断上下文，使用 my_msg_send_from_isr
 *********************************************************************/
static void my_dvr_uart_rx_callback(drv_uart_port_e port, uint16_t len)
{
    my_msg_t msg = {
        .id = MY_MSG_ID_DVR_UART_RX_RDY,
        .data = NULL,
        .len = len
    };

    (void)port;
    my_msg_send_from_isr(MSG_QUEUE_DVR, &msg, 0);
}

/*********************************************************************
 * @brief   UART发送完成回调函数
 * @param   port UART端口号
 * @param   len  发送数据长度
 * @return  none
 * @note    中断上下文，使用 my_msg_send_from_isr
 *********************************************************************/
static void my_dvr_uart_tx_callback(drv_uart_port_e port, uint16_t len)
{
    my_msg_t msg = {
        .id = MY_MSG_ID_DVR_UART_TX_DONE,
        .data = NULL,
        .len = len
    };

    my_msg_send_from_isr(MSG_QUEUE_DVR, &msg, 0);
}

/*********************************************************************
 * @brief   DVR模块电源开启
 * @return  none
 * @note    开启流程：PA8拉高 → 等待电源稳定 → 初始化UART
 *          心跳模块在收到DVR版本查询后自动启动
 *********************************************************************/
static void my_dvr_power_on(void)
{
    int ret;

    /* 开启DVR模块电源 */
    drv_gpio_set(D_DVR_POWER_PORT, D_DVR_POWER_PIN);
    MY_LOG_I("DVR power ON");

    /* 等待DVR模块电源稳定 */
    my_task_delay_ms(D_DVR_POWER_ON_DELAY_MS);

    /* 初始化UART通信 */
    ret = drv_uart_init(&uart_cfg);
    if (ret != 0)
    {
        MY_LOG_E("UART init failed after power on");
        return;
    }

    MY_LOG_I("DVR UART ready, start heartbeat monitor");
    my_dvr_heartbeat_start();
}

/*********************************************************************
 * @brief   DVR模块电源关闭
 * @return  none
 * @note    关闭流程：停止心跳 → 反初始化UART（含GPIO） → PA8拉低
 *********************************************************************/
static void my_dvr_power_off(void)
{
    /* 停止心跳模块 */
    my_dvr_heartbeat_stop();

    /* 反初始化UART外设（含GPIO引脚） */
    drv_uart_deinit(DRV_UART_PORT_USART1);

    /* 关闭DVR模块电源 */
    drv_gpio_reset(D_DVR_POWER_PORT, D_DVR_POWER_PIN);
    MY_LOG_I("DVR power OFF");
}

/*********************************************************************
 * @brief   DVR模块电源重启（心跳异常恢复）
 * @return  none
 * @note    关闭流程：断电 → 500ms放电 → 重新上电
 *          上电后等待DVR发版本查询来重新启动心跳
 *********************************************************************/
static void my_dvr_power_restart(void)
{
    MY_LOG_I("DVR power restart...");
    my_dvr_power_off();
    my_task_delay_ms(500);    /* 断电放电时间 */
    my_dvr_power_on();
}

/*********************************************************************
 * @brief   DVR任务消息处理函数
 * @param   msg 消息指针
 * @return  none
 * @note    根据消息ID分发处理DVR控制逻辑
 *********************************************************************/
static void my_dvr_msg_handler(const my_msg_t *msg)
{
    uint16_t read_len;
    uint16_t msg_size;
    uint8_t temp_buf[D_UART_RING_BUF_SIZE / 2];

    switch (msg->id)
    {
        case MY_MSG_ID_SYS_ACTIVE:
            MY_LOG_I("System activated, power on DVR");
            my_dvr_power_on();
            break;

        case MY_MSG_ID_SYS_SLEEP:
            MY_LOG_I("Sleep requested, power off DVR");
            my_tq_flush(&s_tx_queue);
            my_dvr_power_off();
            TASK_STATE_DVR = TASK_STATE_SLEEP;

            my_task_delay_ms(50);    /* 等待日志输出 */
            my_task_suspend(NULL);
            break;

        case MY_MSG_ID_SYS_SHUTDOWN:
            MY_LOG_W("Shutdown requested, power off DVR");
            my_tq_flush(&s_tx_queue);
            my_dvr_power_off();
            TASK_STATE_DVR = TASK_STATE_SHUTDOWN;

            my_task_delay_ms(50);    /* 等待日志输出 */
            my_task_suspend(NULL);
            break;

        case MY_MSG_ID_SYS_STATUS_REQ:
            MY_LOG_I("Status request received");
            /* TODO: 上报视频芯片状态 */
            break;

        case MY_MSG_ID_DVR_UART_ERROR:
            MY_LOG_I("UART error, err=0x%02X", msg->len);
            /* TODO: 处理UART错误 */
            break;

        case MY_MSG_ID_DVR_UART_TX_DONE:
            MY_LOG_D("UART TX complete, len=%d", msg->len);
            /* 释放已发送节点的内存，并触发下一包发送 */
            my_tq_tx_done(&s_tx_queue);
            my_tq_process(&s_tx_queue, my_dvr_uart_send_wrapper);
            break;

        case MY_MSG_ID_DVR_UART_RX_RDY:
        {
            msg_size = msg->len;
            if (msg_size > sizeof(temp_buf))
            {
                msg_size = sizeof(temp_buf);
            }

            /* 从UART读取数据存入临时缓冲区 （UART中读取实际就是从RingBuffer中读取） */
            read_len = drv_uart_read(DRV_UART_PORT_USART1, temp_buf, msg_size);
            if (read_len > 0)
            {
                my_dvr_parse_process(temp_buf, read_len);
            }
            break;
        }

        case MY_MSG_ID_DVR_PARSE_TIMEOUT:
            MY_LOG_W("Parse timeout, resetting state machine");
            my_dvr_parse_reset();
            break;

        case MY_MSG_ID_DVR_SEND_HEARTBEAT:
        case MY_MSG_ID_DVR_WAIT_HEARTBEAT_TOUT:
            my_dvr_heartbeat_on_msg(msg);
            break;

        case MY_MSG_ID_DVR_HEARTBEAT_RESTART:
            my_dvr_power_restart();
            break;

        default:
            MY_LOG_W("Unknown message: id=0x%04X", msg->id);
            break;
    }
}

/*********************************************************************
 * @brief   DVR任务初始化
 * @return  none
 * @note    初始化电源GPIO、RingBuffer、发送队列、协议解析层
 *          UART和心跳在收到 SYS_ACTIVE 消息后启动
 *********************************************************************/
static void my_dvr_task_init(void)
{
    /* 初始化RingBuffer */
    my_rb_init(&s_ringbuf, s_ring_buf, D_UART_RING_BUF_SIZE);

    /* 初始化发送队列 */
    my_tq_init(&s_tx_queue, D_DVR_TX_QUEUE_DEPTH);

    /* 初始化协议解析层 */
    my_dvr_parse_init();

    /* 初始化心跳定时器（未启动，等待电源开启） */
    my_dvr_heartbeat_init();

    /* 初始化DVR电源控制GPIO（默认关闭） */
    drv_gpio_init(&s_dvr_power_gpio_cfg);

    TASK_STATE_DVR = TASK_STATE_ACTIVE;
}
/*********************************************************************
 * @brief   DVR任务入口函数
 * @param   pvParameters 任务参数（未使用）
 * @return  none
 * @note    永久阻塞式消息循环：无事件时任务完全休眠，零CPU占用
 *          事件触发（中断消息/系统消息）时一次性处理
 *********************************************************************/
static void my_dvr_task_entry(void *pvParameters)
{
    my_msg_t msg;

    (void)pvParameters;

    my_dvr_task_init();

    my_task_delay_ms(100);   /* 等待稳定 */

    my_dvr_power_on();       /* 上电默认开启DVR电源 */

    MY_LOG_I("NT98xx task started");

    /* 永久阻塞式消息循环 */
    while (1)
    {
        if (my_msg_recv(MSG_QUEUE_DVR, &msg, portMAX_DELAY) == 0)
        {
            my_dvr_msg_handler(&msg);
        }
    }
}

/*********************************************************************
 *  公开API实现
 *********************************************************************/

/*********************************************************************
 * @brief   发送数据到DVR视频模块
 * @param   data  数据指针（已组装的协议帧）
 * @param   len   数据长度
 * @return  0: 成功  -1: 失败
 * @note    将数据推入发送队列，并触发异步发送
 *********************************************************************/
int my_dvr_send(const uint8_t *data, uint16_t len)
{
    int ret;

    if (data == NULL || len == 0 || TASK_STATE_DVR != TASK_STATE_ACTIVE)
    {
        return -1;
    }

    ret = my_tq_push(&s_tx_queue, data, len);
    if (ret != 0)
    {
        MY_LOG_W("DVR: TX queue full or alloc failed");
        return -1;
    }

    /* 触发发送（如果当前未在发送） */
    my_tq_process(&s_tx_queue, my_dvr_uart_send_wrapper);

    return 0;
}

/*********************************************************************
 * @brief   初始化并启动DVR任务
 * @return  0: 成功  -1: 失败
 * @note    创建FreeRTOS任务和消息队列；重复调用安全返回0
 *********************************************************************/
int my_dvr_init(void)
{
    int32_t ret;

    /* 检查是否已初始化 */
    if (TASK_HANDLE_DVR != NULL)
    {
        return 0;
    }

    /* 创建消息队列 */
    MSG_QUEUE_DVR = my_msg_queue_create(MY_DVR_MSG_QUEUE_DEPTH, sizeof(my_msg_t));
    if (MSG_QUEUE_DVR == NULL)
    {
        MY_LOG_E("Failed to create msg queue");
        return -1;
    }

    /* 创建DVR任务 */
    ret = my_task_create(&TASK_HANDLE_DVR, "DVR",
                          MY_DVR_TASK_STACK_SIZE,
                          my_dvr_task_entry, NULL,
                          MY_DVR_TASK_PRIO);

    /* 检查任务创建结果 */
    if (ret != 0 || TASK_HANDLE_DVR == NULL)
    {
        my_msg_queue_delete(MSG_QUEUE_DVR);
        MSG_QUEUE_DVR = NULL;

        MY_LOG_E("Failed to create task(%d)", ret);
        return -1;
    }

    MY_LOG_I("Init OK, task created");

    return 0;
}
