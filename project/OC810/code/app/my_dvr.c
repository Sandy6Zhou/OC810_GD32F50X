/********************************************************************
**版权所有：         深圳市几米物联有限公司
**文件名称：        my_dvr.c
**文件描述：        DVR视频模块驱动任务实现
**当前版本：        V1.0
**作    者：        伍玉蛟 (wuyujiao@jimiiot.com)
**完成日期：        2026.06.08
*********************************************************************
** 功能描述：       1. DVR视频模块通信与控制
**                 2. 视频流状态管理
**                 3. 永久阻塞式消息处理循环
*********************************************************************/
#include "my_comm.h"

/*********************************************************************
 * 内部宏定义
 *********************************************************************/
/** 定义UART接收缓冲区大小 */
#define D_UART_RX_BUF_SIZE 256

/** 定义UART发送缓冲区大小 */
#define D_UART_TX_BUF_SIZE 256

/** 定义RingBuffer缓冲区大小 */
#define D_UART_RING_BUF_SIZE 512

/** 定义心跳包间隔时间 */
#define D_DVR_HEARTBEAT_INTERVAL     1000
#define D_DVR_WAIT_HEARTBEAT_TIMEOUT 1500

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

/*********************************************************************
 * 内部全局变量
 *********************************************************************/

/** 发送缓冲区 */
static uint8_t s_tx_buf[D_UART_TX_BUF_SIZE];

/** RingBuffer缓冲区 */
static uint8_t s_ring_buf[D_UART_RING_BUF_SIZE];

/** RingBuffer控制块 */
static my_rb_t s_ringbuf;

/** 发送队列控制块 */
static my_tq_ctrl_t s_tx_queue;

/** UART配置结构体 */
static drv_uart_config_t uart_cfg = {
        .port = DRV_UART_PORT_USART1,
        .baudrate = 115200,
        .rx_buf = NULL,              /* RingBuffer 模式下无需 rx_buf */
        .rx_buf_size = 0,
        .dma_rx_buf = NULL,
        .dma_rx_buf_size = 0,
        .ringbuf = &s_ringbuf,
        .use_dma_rx = false,
        .use_idle = true,
        .use_ringbuf = true,
        .use_dma_tx = false,
        .tx_mode = UART_TX_MODE_INTERRUPT,    /**< 中断方式发送 */
        .tx_buf = s_tx_buf,
        .tx_buf_size = D_UART_TX_BUF_SIZE,
        .use_tx_mutex = false,    /**< 不使用发送互斥锁 */
        .is_wakeup_capable = false,
        .rx_callback = my_dvr_uart_rx_callback,
        .tx_callback = my_dvr_uart_tx_callback,
        .error_callback = my_dvr_uart_error_callback
    };

/** 等待心跳包超时计数器 */
static uint8_t s_wait_heartbeat_timeout_cnt = 0;
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
 * @brief   发送心跳包回调函数
 * @param   param 回调参数
 * @return  none
 * @note    通过消息队列发送心跳包消息
 *********************************************************************/
static void my_dvr_send_heartbeat_cb(void *param)
{
    (void)param;
    my_msg_t msg = {
        .id = MY_MSG_ID_DVR_SEND_HEARTBEAT,
        .data = NULL,
        .len = 0
    };

    my_msg_send(MSG_QUEUE_DVR, &msg, 0);
}

/*********************************************************************
 * @brief   处理心跳包超时
 * @return  none
 * @note    复位协议解析状态机
 *********************************************************************/
static void my_dvr_wait_heartbeat_cb(void *param)
{
    (void)param;

    my_msg_t msg = {
        .id = MY_MSG_ID_DVR_WAIT_HEARTBEAT_TOUT,
        .data = NULL,
        .len = 0
    };

    my_msg_send(MSG_QUEUE_DVR, &msg, 0);
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
            MY_LOG_I("System activated");
            /* TODO: 初始化视频芯片、启动视频流 */
            break;

        case MY_MSG_ID_SYS_SLEEP:
            MY_LOG_I("System sleep requested");
            /* TODO: 停止视频流、关闭芯片电源 */
            my_task_suspend(NULL);
            break;

        case MY_MSG_ID_SYS_SHUTDOWN:
            MY_LOG_W("Shutdown requested");
            /* TODO: 保存视频配置、关闭芯片 */
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
            MY_LOG_I("UART TX complete, len=%d", msg->len);
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

            read_len = drv_uart_read(DRV_UART_PORT_USART1, temp_buf, msg_size);
            if (read_len > 0)
            {
                // my_dvr_parse_process(temp_buf, read_len);
            }
            break;
        }

        default:
            MY_LOG_W("Unknown message: id=0x%04X", msg->id);
            break;
    }
}

/*********************************************************************
 * @brief   DVR任务初始化
 * @return  none
 * @note    初始化RingBuffer、发送队列、UART驱动
 *********************************************************************/
static void my_dvr_task_init(void)
{
    int ret;

    my_rb_init(&s_ringbuf, s_ring_buf, D_UART_RING_BUF_SIZE);
    my_tq_init(&s_tx_queue, 32);

    ret = drv_uart_init(&uart_cfg);
    if (ret != 0)
    {
        MY_LOG_E("Failed to init UART");
    }

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

    if (data == NULL || len == 0)
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
        MY_LOG_E("Failed to create task(%d)", ret);
        return -1;
    }

    MY_LOG_I("Init OK, task created");

    return 0;
}
