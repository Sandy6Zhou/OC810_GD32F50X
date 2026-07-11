/*********************************************************************
**版权所有：         深圳市几米物联有限公司
**文件名称：        my_main.c
**文件描述：        主任务模块 - 系统主任务入口与协调管理
**当前版本：        V1.0
**作    者：        伍玉蛟 (wuyujiao@jimiiot.com)
**完成日期：        2026.06.08
*********************************************************************
** 功能描述：       1. 系统主任务入口，负责创建所有子任务
**                 2. 主任务消息循环（永久阻塞式等待）
**                 3. 协调管理各子任务生命周期
********************************************************************/
#include "my_comm.h"

/*===========================================================================
 *  模块私有变量
 *==========================================================================*/

/** 任务句柄数组 */
my_task_handle_t g_task_handle[TASK_MOD_MAX] = { NULL };

/** 消息队列数组 */
my_msg_queue_t g_msg_queue[TASK_MOD_MAX] = { NULL };

/** 工作状态数组 */
task_state_e g_task_state[TASK_MOD_MAX] = { TASK_STATE_NOT_INIT };

/*===========================================================================
 *  子任务初始化
 *==========================================================================*/

/*********************************************************************
 * @brief   初始化所有子任务模块
 * @return  0:全部成功  -1:有模块初始化失败
 * @note    按依赖顺序依次初始化各子任务模块
 *          任一模块失败会打印错误但继续初始化其他模块
 *********************************************************************/
static int sub_tasks_init(void)
{
    int ret = 0;
    int fail_count = 0;

    MY_LOG_I("Initializing sub tasks...");

    /* 1. RTT Shell（开发调试用，优先启动） */
    if (my_rtt_shell_init() != 0)
    {
        MY_LOG_E("RTT Shell init failed!");
        fail_count++;
    }

    /* 2. 控制任务（依赖其他模块状态，最后启动） */
    if (my_ctrl_init() != 0)
    {
        MY_LOG_E("Control init failed!");
        fail_count++;
    }

    /* 3. NT98xx视频模块 */
    if (my_nt98xx_init() != 0)
    {
        MY_LOG_E("NT98xx init failed!");
        fail_count++;
    }

    /* 4. G-Sensor（传感器优先于控制逻辑） */
    if (my_gsensor_init() != 0)
    {
        MY_LOG_E("G-Sensor init failed!");
        fail_count++;
    }

    /* 5. GNSS定位 */
    if (my_gnss_init() != 0)
    {
        MY_LOG_E("GNSS init failed!");
        fail_count++;
    }

    /* 6. CAN总线（两路CAN） */
    if (my_can_init() != 0)
    {
        MY_LOG_E("CAN init failed!");
        fail_count++;
    }

    /* 7. RS485通讯 */
    if (my_rs485_init() != 0)
    {
        MY_LOG_E("RS485 init failed!");
        fail_count++;
    }

    /* 8. RS232通讯 */
    if (my_rs232_init() != 0)
    {
        MY_LOG_E("RS232 init failed!");
        fail_count++;
    }

    /* 9. AMS平台管理 */
    if (my_ams_init() != 0)
    {
        MY_LOG_E("AMS init failed!");
        fail_count++;
    }


    if (fail_count > 0)
    {
        MY_LOG_W("%d sub task(s) init failed!", fail_count);
        ret = -1;
    }
    else
    {
        MY_LOG_I("All sub tasks initialized successfully!");
    }

    return ret;
}

/*===========================================================================
 *  系统初始化
 *==========================================================================*/

/*********************************************************************
 * @brief   1分钟定时器回调函数
 * @param   timer_handle 定时器句柄（未使用）
 * @return  none
 * @note    每分钟触发一次，向主任务发送心跳消息
 *********************************************************************/
static void one_minute_timer_cb(my_timer_handle_t timer_handle)
{
    (void)timer_handle;

    my_msg_t msg = {
        .id   = MY_MSG_ID_ONE_MINUTE,
        .data = NULL,
        .len  = 0
    };

    my_msg_send(MSG_QUEUE_MAIN, &msg, 0);
}

/*********************************************************************
 * @brief   系统初始化（SYS_INIT消息触发）
 * @return  none
 * @note    创建并启动系统级定时器，执行系统级初始化
 *********************************************************************/
static void my_main_task_init(void)
{
    /* 创建并启动1分钟定时器 */
    if (my_timer_create(MY_TIMER_ID_ONE_MINUTE, one_minute_timer_cb, 60000) == 0)
    {
        my_timer_start(MY_TIMER_ID_ONE_MINUTE, 0);
        MY_LOG_I("1-minute timer started");
    }
    else
    {
        MY_LOG_E("1-minute timer create failed!");
    }

    /* 设置系统初始状态为活动 */
    TASK_STATE_MAIN = TASK_STATE_ACTIVE;
}

/*===========================================================================
 *  主任务处理
 *==========================================================================*/

/*********************************************************************
 * @brief   主任务消息处理函数
 * @param   msg 消息指针
 * @return  none
 * @note    根据消息ID分发处理
 *********************************************************************/
static void main_msg_handler(const my_msg_t *msg)
{
    switch (msg->id)
    {
        case MY_MSG_ID_SYS_ACTIVE:
            MY_LOG_I("System activated");
            break;

        case MY_MSG_ID_SYS_SLEEP:
            MY_LOG_I("System sleep requested");
            break;

        case MY_MSG_ID_SYS_SHUTDOWN:
            MY_LOG_W("Shutdown requested");
            /* TODO: 执行有序关机流程 */
            break;

        case MY_MSG_ID_SYS_STATUS_REQ:
            MY_LOG_I("Status request received");
            /* TODO: 汇总各模块状态并上报 */
            break;

        case MY_MSG_ID_ONE_MINUTE:
            MY_LOG_I("1-minute timer tick");
            break;

        default:
            MY_LOG_W("Unknown message: id=0x%04X", msg->id);
            break;
    }
}

/*********************************************************************
 * @brief   主任务入口函数
 * @param   pvParameters 任务参数（未使用）
 * @return  none
 * @note    永久阻塞式消息循环
 *********************************************************************/
static void main_task_entry(void *pvParameters)
{
    my_msg_t msg;

    (void)pvParameters;

    my_task_delay_ms(100);    // 延迟100毫秒,等待RTT及外设就绪

    MY_LOG_I("Main task started");

    /* 初始化文件系统/参数管理（基础设施，优先于子任务） */
    if (param_manager_init() != PARAM_OK)
    {
        MY_LOG_E("Param manager init failed!");
    }

    /* 初始化所有子任务 */
    sub_tasks_init();

    my_main_task_init();

    MY_LOG_I("Main task entering message loop...");

    /* 永久阻塞式消息循环 */
    while (1)
    {
        if (my_msg_recv(MSG_QUEUE_MAIN, &msg, portMAX_DELAY) == 0)
        {
            main_msg_handler(&msg);
        }
    }
}

/*===========================================================================
 *  公共工具函数
 *==========================================================================*/

/*********************************************************************
 * @brief   广播消息到所有已创建的子任务
 * @param   msg 消息指针
 * @return  0:全部成功  -1:有发送失败
 * @note    跳过未创建队列的模块和MAIN自身
 *********************************************************************/
int my_main_broadcast_msg(const my_msg_t *msg)
{
    int fail_count = 0;

    for (int i = 0; i < TASK_MOD_MAX; i++)
    {
        if (i == TASK_MOD_MAIN)
        {
            continue;  /* 跳过自己 */
        }
        if (g_msg_queue[i] != NULL)
        {
            if (my_msg_send(g_msg_queue[i], msg, 0) != 0)
            {
                MY_LOG_W("Broadcast failed to module %d", i);
                fail_count++;
            }
        }
    }

    return (fail_count > 0) ? -1 : 0;
}

/*********************************************************************
 * @brief   发送消息到指定模块
 * @param   mod 目标模块枚举
 * @param   msg 消息指针
 * @return  0:成功  -1:失败（队列未创建或发送失败）
 *********************************************************************/
int my_main_send_to_module(task_module_e mod, const my_msg_t *msg)
{
    if (mod >= TASK_MOD_MAX || g_msg_queue[mod] == NULL)
    {
        MY_LOG_W("Module %d queue not available", mod);
        return -1;
    }

    return my_msg_send(g_msg_queue[mod], msg, 0);
}

/*********************************************************************
 * @brief   切换系统状态并广播到所有子任务
 * @param   state 目标状态
 * @return  0:成功  -1:广播失败
 * @note    更新 g_task_state[TASK_MOD_MAIN] 后发送对应系统消息
 *********************************************************************/
int my_main_set_system_state(task_state_e state)
{
    /* 状态转换检查 */
    if (state <= TASK_STATE_NOT_INIT || state > TASK_STATE_SHUTDOWN)
    {
        MY_LOG_E("Invalid system state: %d", state);
        return -1;
    }

    /* 状态转换 */
    static const my_msg_id_e state_to_msg[] = {
        [TASK_STATE_ACTIVE]  = MY_MSG_ID_SYS_ACTIVE,
        [TASK_STATE_SLEEP]    = MY_MSG_ID_SYS_SLEEP,
        [TASK_STATE_SHUTDOWN] = MY_MSG_ID_SYS_SHUTDOWN,
    };

    /* 创建消息 */
    my_msg_t msg = {
        .id = state_to_msg[state],
        .data = NULL,
        .len = 0
    };

    /* 更新状态 */
    TASK_STATE_MAIN = state;

    MY_LOG_I("System state -> %d, broadcasting...", state);
    return my_main_broadcast_msg(&msg);
}

/*===========================================================================
 *  公共API实现
 *==========================================================================*/

/*********************************************************************
 * @brief   初始化主任务模块
 * @return  0:成功  -1:失败
 * @note    创建消息队列和主任务
 *********************************************************************/
int my_main_init(void)
{
    int32_t ret = 0;

    /* 清零任务句柄, 避免未初始化的句柄导致错误 */
    memset(g_task_handle, 0, sizeof(g_task_handle));
    memset(g_msg_queue, 0, sizeof(g_msg_queue));
    memset(g_task_state, 0, sizeof(g_task_state));

    /* 创建消息队列 */
    MSG_QUEUE_MAIN = my_msg_queue_create(MY_MAIN_MSG_QUEUE_DEPTH, sizeof(my_msg_t));
    if (MSG_QUEUE_MAIN == NULL)
    {
        MY_LOG_E("Message queue create failed!");
        return -1;
    }

    /* 创建主任务 */
    ret = my_task_create(&TASK_HANDLE_MAIN, "MY_MAIN",
                           MY_MAIN_TASK_STACK_SIZE,
                           main_task_entry,
                           NULL,
                           MY_MAIN_TASK_PRIO);

    /* 检查任务创建结果 */
    if (ret != 0 || TASK_HANDLE_MAIN == NULL)
    {
        MY_LOG_E("Task create failed!");
        return -1;
    }

    MY_LOG_I("Main task module initialized");
    return 0;
}
