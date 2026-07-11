/********************************************************************
**版权所有：         深圳市几米物联有限公司
**文件名称：        my_gnss.c
**文件描述：        GNSS定位模块任务实现
**当前版本：        V1.0
**作    者：        伍玉蛟 (wuyujiao@jimiiot.com)
**完成日期：        2026.06.08
*********************************************************************
** 功能描述：       1. GNSS定位数据接收与NMEA解析
**                 2. 定位状态管理
**                 3. 永久阻塞式消息处理循环
*********************************************************************/
#include "my_comm.h"

/*===========================================================================
 *  模块初始化
 *===========================================================================*/

/*********************************************************************
 * @brief   GNSS任务初始化
 * @return  none
 * @note    设置GNSS模块状态为ACTIVE
 *********************************************************************/
static void my_gnss_task_init(void)
{
    TASK_STATE_GNSS = TASK_STATE_ACTIVE;
}

/*===========================================================================
 *  消息处理
 *===========================================================================*/

/*********************************************************************
 * @brief   GNSS任务消息处理函数
 * @param   msg 消息指针
 * @return  none
 * @note    根据消息ID分发处理GNSS控制逻辑
 *********************************************************************/
static void gnss_msg_handler(const my_msg_t *msg)
{
    switch (msg->id)
    {
        case MY_MSG_ID_SYS_ACTIVE:
            MY_LOG_I("System activated");
            /* TODO: 初始化GNSS模块、启动NMEA数据接收 */
            break;

        case MY_MSG_ID_SYS_SLEEP:
            MY_LOG_I("System sleep requested");
            /* TODO: 停止NMEA接收、降低GNSS功耗 */
            my_task_suspend(NULL);
            break;

        case MY_MSG_ID_SYS_SHUTDOWN:
            MY_LOG_W("Shutdown requested");
            /* TODO: 保存定位数据、关闭GNSS模块 */
            my_task_suspend(NULL);
            break;

        case MY_MSG_ID_SYS_STATUS_REQ:
            MY_LOG_I("Status request received");
            /* TODO: 上报定位状态（经纬度、卫星数、定位精度等） */
            break;

        default:
            MY_LOG_W("Unknown message: id=0x%04X", msg->id);
            break;
    }
}

/*===========================================================================
 *  GNSS任务
 *===========================================================================*/

/*********************************************************************
 * @brief   GNSS任务入口函数
 * @param   pvParameters 任务参数（未使用）
 * @return  none
 * @note    永久阻塞式消息循环
 *********************************************************************/
static void gnss_task_entry(void *pvParameters)
{
    my_msg_t msg;

    (void)pvParameters;

    my_gnss_task_init();

    MY_LOG_I("GNSS task started");

    /* 永久阻塞式消息循环 */
    while (1)
    {
        if (my_msg_recv(MSG_QUEUE_GNSS, &msg, portMAX_DELAY) == 0)
        {
            gnss_msg_handler(&msg);
        }
    }
}

/*===========================================================================
 *  公开API实现
 *===========================================================================*/

/*********************************************************************
 * @brief   初始化并启动GNSS任务
 * @return  0: 成功  -1: 失败
 * @note    创建FreeRTOS任务和消息队列；重复调用安全返回0
 *********************************************************************/
int my_gnss_init(void)
{
    int32_t ret;

    /* 检查是否已初始化 */
    if (TASK_HANDLE_GNSS != NULL)
    {
        return 0;
    }

    /* 创建消息队列 */
    MSG_QUEUE_GNSS = my_msg_queue_create(MY_GNSS_MSG_QUEUE_DEPTH, sizeof(my_msg_t));
    if (MSG_QUEUE_GNSS == NULL)
    {
        MY_LOG_E("Failed to create msg queue");
        return -1;
    }

    /* 创建GNSS任务 */
    ret = my_task_create(&TASK_HANDLE_GNSS, "GNSS",
                          MY_GNSS_TASK_STACK_SIZE,
                          gnss_task_entry, NULL,
                          MY_GNSS_TASK_PRIO);

    /* 检查任务创建结果 */
    if (ret != 0 || TASK_HANDLE_GNSS == NULL)
    {
        my_msg_queue_delete(MSG_QUEUE_GNSS);
        MSG_QUEUE_GNSS = NULL;

        MY_LOG_E("Failed to create task(%d)", ret);
        return -1;
    }

    MY_LOG_I("Init OK, task created");

    return 0;
}
