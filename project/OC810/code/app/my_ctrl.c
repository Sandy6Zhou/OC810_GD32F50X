/********************************************************************
**版权所有：         深圳市几米物联有限公司
**文件名称：        my_ctrl.c
**文件描述：        控制任务 - 系统整体控制逻辑管理
**当前版本：        V1.0
**作    者：        伍玉蛟 (wuyujiao@jimiiot.com)
**完成日期：        2026.06.08
*********************************************************************
** 功能描述：       1. 系统整体控制逻辑（IO控制、电源管理、模式切换）
**                 2. 跨模块协调控制
**                 3. 永久阻塞式消息处理循环
*********************************************************************/
#include "my_comm.h"

/*===========================================================================
 *  模块初始化
 *===========================================================================*/

/*********************************************************************
 * @brief   控制任务初始化
 * @return  none
 * @note    设置控制模块状态为ACTIVE
 *********************************************************************/
static void my_ctrl_task_init(void)
{
    TASK_STATE_CTRL = TASK_STATE_ACTIVE;
}

/*===========================================================================
 *  消息处理
 *===========================================================================*/

/*********************************************************************
 * @brief   控制任务消息处理函数
 * @param   msg 消息指针
 * @return  none
 * @note    根据消息ID分发处理控制逻辑
 *********************************************************************/
static void ctrl_msg_handler(const my_msg_t *msg)
{
    switch (msg->id)
    {
        case MY_MSG_ID_SYS_ACTIVE:
            MY_LOG_I("System activated");
            break;

        case MY_MSG_ID_SYS_SLEEP:
            MY_LOG_I("System sleep requested");
            /* TODO: 执行休眠前准备工作（关闭外设、保存状态） */
            my_task_suspend(NULL);
            break;

        case MY_MSG_ID_SYS_SHUTDOWN:
            MY_LOG_W("Shutdown requested");
            /* TODO: 执行有序关机流程（保存数据、关闭电源） */
            my_task_suspend(NULL);
            break;

        case MY_MSG_ID_SYS_STATUS_REQ:
            MY_LOG_I("Status request received");
            /* TODO: 汇总各模块状态并上报 */
            break;

        default:
            MY_LOG_W("Unknown message: id=0x%04X", msg->id);
            break;
    }
}

/*===========================================================================
 *  控制任务
 *===========================================================================*/

/*********************************************************************
 * @brief   控制任务入口函数
 * @param   pvParameters 任务参数（未使用）
 * @return  none
 * @note    永久阻塞式消息循环
 *********************************************************************/
static void ctrl_task_entry(void *pvParameters)
{
    my_msg_t msg;

    (void)pvParameters;

    my_ctrl_task_init();

    MY_LOG_I("Control task started");

    /* 永久阻塞式消息循环 */
    while (1)
    {
        if (my_msg_recv(MSG_QUEUE_CTRL, &msg, portMAX_DELAY) == 0)
        {
            ctrl_msg_handler(&msg);
        }
    }
}

/*===========================================================================
 *  公开API实现
 *===========================================================================*/

/*********************************************************************
 * @brief   初始化并启动控制任务
 * @return  0: 成功  -1: 失败
 * @note    创建FreeRTOS任务和消息队列；重复调用安全返回0
 *********************************************************************/
int my_ctrl_init(void)
{
    int32_t ret;

    /* 检查是否已初始化 */
    if (TASK_HANDLE_CTRL != NULL)
    {
        return 0;
    }

    /* 创建消息队列 */
    MSG_QUEUE_CTRL = my_msg_queue_create(MY_CTRL_MSG_QUEUE_DEPTH, sizeof(my_msg_t));
    if (MSG_QUEUE_CTRL == NULL)
    {
        MY_LOG_E("Failed to create msg queue");
        return -1;
    }

    /* 创建控制任务 */
    ret = my_task_create(&TASK_HANDLE_CTRL, "CTRL",
                          MY_CTRL_TASK_STACK_SIZE,
                          ctrl_task_entry, NULL,
                          MY_CTRL_TASK_PRIO);

    /* 检查任务创建结果 */
    if (ret != 0 || TASK_HANDLE_CTRL == NULL)
    {
        MY_LOG_E("Failed to create task(%d)", ret);
        return -1;
    }

    MY_LOG_I("Init OK, task created");

    return 0;
}
