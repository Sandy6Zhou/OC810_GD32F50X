/********************************************************************
**版权所有：         深圳市几米物联有限公司
**文件名称：        my_gsensor.c
**文件描述：        G-Sensor加速度传感器任务实现
**当前版本：        V1.0
**作    者：        伍玉蛟 (wuyujiao@jimiiot.com)
**完成日期：        2026.06.08
*********************************************************************
** 功能描述：       1. G-Sensor加速度数据采集与处理
**                 2. 碰撞检测、姿态识别
**                 3. 永久阻塞式消息处理循环
*********************************************************************/
#include "my_comm.h"

/*===========================================================================
 *  模块初始化
 *===========================================================================*/

/*********************************************************************
 * @brief   G-Sensor任务初始化
 * @return  none
 * @note    设置G-Sensor模块状态为ACTIVE
 *********************************************************************/
static void my_gsensor_task_init(void)
{
    TASK_STATE_GSENSOR = TASK_STATE_ACTIVE;
}

/*===========================================================================
 *  消息处理
 *===========================================================================*/

/*********************************************************************
 * @brief   G-Sensor任务消息处理函数
 * @param   msg 消息指针
 * @return  none
 * @note    根据消息ID分发处理G-Sensor控制逻辑
 *********************************************************************/
static void gsensor_msg_handler(const my_msg_t *msg)
{
    switch (msg->id)
    {
        case MY_MSG_ID_SYS_ACTIVE:
            MY_LOG_I("System activated");
            /* TODO: 初始化G-Sensor、启动数据采集 */
            break;

        case MY_MSG_ID_SYS_SLEEP:
            MY_LOG_I("System sleep requested");
            /* TODO: 停止数据采集、降低传感器功耗 */
            my_task_suspend(NULL);
            break;

        case MY_MSG_ID_SYS_SHUTDOWN:
            MY_LOG_W("Shutdown requested");
            /* TODO: 保存传感器配置、关闭传感器 */
            my_task_suspend(NULL);
            break;

        case MY_MSG_ID_SYS_STATUS_REQ:
            MY_LOG_I("Status request received");
            /* TODO: 上报传感器状态（校准状态、最新数据等） */
            break;

        default:
            MY_LOG_W("Unknown message: id=0x%04X", msg->id);
            break;
    }
}

/*===========================================================================
 *  G-Sensor任务
 *===========================================================================*/

/*********************************************************************
 * @brief   G-Sensor任务入口函数
 * @param   pvParameters 任务参数（未使用）
 * @return  none
 * @note    永久阻塞式消息循环
 *********************************************************************/
static void gsensor_task_entry(void *pvParameters)
{
    my_msg_t msg;

    (void)pvParameters;

    my_gsensor_task_init();

    MY_LOG_I("G-Sensor task started");

    /* 永久阻塞式消息循环 */
    while (1)
    {
        if (my_msg_recv(MSG_QUEUE_GSENSOR, &msg, portMAX_DELAY) == 0)
        {
            gsensor_msg_handler(&msg);
        }
    }
}

/*===========================================================================
 *  公开API实现
 *===========================================================================*/

/*********************************************************************
 * @brief   初始化并启动G-Sensor任务
 * @return  0: 成功  -1: 失败
 * @note    创建FreeRTOS任务和消息队列；重复调用安全返回0
 *********************************************************************/
int my_gsensor_init(void)
{
    int32_t ret;

    /* 检查是否已初始化 */
    if (TASK_HANDLE_GSENSOR != NULL)
    {
        return 0;
    }

    /* 创建消息队列 */
    MSG_QUEUE_GSENSOR = my_msg_queue_create(MY_GSENSOR_MSG_QUEUE_DEPTH, sizeof(my_msg_t));
    if (MSG_QUEUE_GSENSOR == NULL)
    {
        MY_LOG_E("Failed to create msg queue");
        return -1;
    }

    /* 创建G-Sensor任务 */
    ret = my_task_create(&TASK_HANDLE_GSENSOR, "GSENSOR",
                          MY_GSENSOR_TASK_STACK_SIZE,
                          gsensor_task_entry, NULL,
                          MY_GSENSOR_TASK_PRIO);

    /* 检查任务创建结果 */
    if (ret != 0 || TASK_HANDLE_GSENSOR == NULL)
    {
        MY_LOG_E("Failed to create task(%d)", ret);
        return -1;
    }

    MY_LOG_I("Init OK, task created");

    return 0;
}
