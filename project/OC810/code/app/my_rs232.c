/********************************************************************
**版权所有：         深圳市几米物联有限公司
**文件名称：        my_rs232.c
**文件描述：        RS232串口通信任务实现
**当前版本：        V1.0
**作    者：        伍玉蛟 (wuyujiao@jimiiot.com)
**完成日期：        2026.06.08
*********************************************************************
** 功能描述：       1. RS232串口数据收发
**                 2. 全双工通信管理
**                 3. 永久阻塞式消息处理循环
*********************************************************************/
#include "my_comm.h"

/*===========================================================================
 *  模块初始化
 *===========================================================================*/

/*********************************************************************
 * @brief   RS232任务初始化
 * @return  none
 * @note    设置RS232模块状态为ACTIVE
 *********************************************************************/
static void my_rs232_task_init(void)
{
    TASK_STATE_RS232 = TASK_STATE_ACTIVE;
}

/*===========================================================================
 *  消息处理
 *===========================================================================*/

/*********************************************************************
 * @brief   RS232任务消息处理函数
 * @param   msg 消息指针
 * @return  none
 * @note    根据消息ID分发处理RS232控制逻辑
 *********************************************************************/
static void rs232_msg_handler(const my_msg_t *msg)
{
    switch (msg->id)
    {
        case MY_MSG_ID_SYS_ACTIVE:
            MY_LOG_I("System activated");
            /* TODO: 初始化RS232串口、启动数据接收 */
            break;

        case MY_MSG_ID_SYS_SLEEP:
            MY_LOG_I("System sleep requested");
            /* TODO: 停止串口接收、降低RS232功耗 */
            my_task_suspend(NULL);
            break;

        case MY_MSG_ID_SYS_SHUTDOWN:
            MY_LOG_W("Shutdown requested");
            /* TODO: 清空发送缓冲区、关闭串口 */
            my_task_suspend(NULL);
            break;

        case MY_MSG_ID_SYS_STATUS_REQ:
            MY_LOG_I("Status request received");
            /* TODO: 上报串口状态（收发统计、错误计数等） */
            break;

        default:
            MY_LOG_W("Unknown message: id=0x%04X", msg->id);
            break;
    }
}

/*===========================================================================
 *  RS232任务
 *===========================================================================*/

/*********************************************************************
 * @brief   RS232任务入口函数
 * @param   pvParameters 任务参数（未使用）
 * @return  none
 * @note    永久阻塞式消息循环
 *********************************************************************/
static void rs232_task_entry(void *pvParameters)
{
    my_msg_t msg;

    (void)pvParameters;

    my_rs232_task_init();

    MY_LOG_I("RS232 task started");

    /* 永久阻塞式消息循环 */
    while (1)
    {
        if (my_msg_recv(MSG_QUEUE_RS232, &msg, portMAX_DELAY) == 0)
        {
            rs232_msg_handler(&msg);
        }
    }
}

/*===========================================================================
 *  公开API实现
 *===========================================================================*/

/*********************************************************************
 * @brief   初始化并启动RS232任务
 * @return  0: 成功  -1: 失败
 * @note    创建FreeRTOS任务和消息队列；重复调用安全返回0
 *********************************************************************/
int my_rs232_init(void)
{
    int32_t ret;

    /* 检查是否已初始化 */
    if (TASK_HANDLE_RS232 != NULL)
    {
        return 0;
    }

    /* 创建消息队列 */
    MSG_QUEUE_RS232 = my_msg_queue_create(MY_RS232_MSG_QUEUE_DEPTH, sizeof(my_msg_t));
    if (MSG_QUEUE_RS232 == NULL)
    {
        MY_LOG_E("Failed to create msg queue");
        return -1;
    }

    /* 创建RS232任务 */
    ret = my_task_create(&TASK_HANDLE_RS232, "RS232",
                          MY_RS232_TASK_STACK_SIZE,
                          rs232_task_entry, NULL,
                          MY_RS232_TASK_PRIO);

    /* 检查任务创建结果 */
    if (ret != 0 || TASK_HANDLE_RS232 == NULL)
    {
        my_msg_queue_delete(MSG_QUEUE_RS232);
        MSG_QUEUE_RS232 = NULL;

        MY_LOG_E("Failed to create task(%d)", ret);
        return -1;
    }

    MY_LOG_I("Init OK, task created");

    return 0;
}
