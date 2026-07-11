/********************************************************************
**版权所有：         深圳市几米物联有限公司
**文件名称：        my_can.c
**文件描述：        CAN总线通信任务实现
**当前版本：        V1.0
**作    者：        伍玉蛟 (wuyujiao@jimiiot.com)
**完成日期：        2026.06.08
*********************************************************************
** 功能描述：       1. CAN总线数据收发与协议解析
**                 2. 两路CAN通道管理
**                 3. 永久阻塞式消息处理循环
*********************************************************************/
#include "my_comm.h"

/*===========================================================================
 *  模块初始化
 *===========================================================================*/

/*********************************************************************
 * @brief   CAN任务初始化
 * @return  none
 * @note    设置CAN模块状态为ACTIVE
 *********************************************************************/
static void my_can_task_init(void)
{
    TASK_STATE_CAN = TASK_STATE_ACTIVE;
}

/*===========================================================================
 *  消息处理
 *===========================================================================*/

/*********************************************************************
 * @brief   CAN任务消息处理函数
 * @param   msg 消息指针
 * @return  none
 * @note    根据消息ID分发处理CAN控制逻辑
 *********************************************************************/
static void can_msg_handler(const my_msg_t *msg)
{
    switch (msg->id)
    {
        case MY_MSG_ID_SYS_ACTIVE:
            MY_LOG_I("System activated");
            /* TODO: 初始化CAN控制器、启动双路CAN接收 */
            break;

        case MY_MSG_ID_SYS_SLEEP:
            MY_LOG_I("System sleep requested");
            /* TODO: 停止CAN接收、进入低功耗模式 */
            my_task_suspend(NULL);
            break;

        case MY_MSG_ID_SYS_SHUTDOWN:
            MY_LOG_W("Shutdown requested");
            /* TODO: 清空CAN缓冲区、关闭CAN控制器 */
            my_task_suspend(NULL);
            break;

        case MY_MSG_ID_SYS_STATUS_REQ:
            MY_LOG_I("Status request received");
            /* TODO: 上报CAN状态（收发统计、总线状态等） */
            break;

        default:
            MY_LOG_W("Unknown message: id=0x%04X", msg->id);
            break;
    }
}

/*===========================================================================
 *  CAN任务
 *===========================================================================*/

/*********************************************************************
 * @brief   CAN任务入口函数
 * @param   pvParameters 任务参数（未使用）
 * @return  none
 * @note    永久阻塞式消息循环
 *********************************************************************/
static void can_task_entry(void *pvParameters)
{
    my_msg_t msg;

    (void)pvParameters;

    my_can_task_init();

    MY_LOG_I("CAN task started");

    /* 永久阻塞式消息循环 */
    while (1)
    {
        if (my_msg_recv(MSG_QUEUE_CAN, &msg, portMAX_DELAY) == 0)
        {
            can_msg_handler(&msg);
        }
    }
}

/*===========================================================================
 *  公开API实现
 *===========================================================================*/

/*********************************************************************
 * @brief   初始化并启动CAN任务
 * @return  0: 成功  -1: 失败
 * @note    创建FreeRTOS任务和消息队列；重复调用安全返回0
 *********************************************************************/
int my_can_init(void)
{
    int32_t ret;

    /* 检查是否已初始化 */
    if (TASK_HANDLE_CAN != NULL)
    {
        return 0;
    }

    /* 创建消息队列 */
    MSG_QUEUE_CAN = my_msg_queue_create(MY_CAN_MSG_QUEUE_DEPTH, sizeof(my_msg_t));
    if (MSG_QUEUE_CAN == NULL)
    {
        MY_LOG_E("Failed to create msg queue");
        return -1;
    }

    /* 创建CAN任务 */
    ret = my_task_create(&TASK_HANDLE_CAN, "CAN",
                          MY_CAN_TASK_STACK_SIZE,
                          can_task_entry, NULL,
                          MY_CAN_TASK_PRIO);

    /* 检查任务创建结果 */
    if (ret != 0 || TASK_HANDLE_CAN == NULL)
    {
        MY_LOG_E("Failed to create task(%d)", ret);
        return -1;
    }

    MY_LOG_I("Init OK, task created");

    return 0;
}
