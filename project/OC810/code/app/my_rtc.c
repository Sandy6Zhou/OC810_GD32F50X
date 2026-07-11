/********************************************************************
**版权所有：        深圳市几米物联有限公司
**文件名称：        my_rtc.c
**文件描述：        RTC应用模块实现
**当前版本：        V1.0
**作    者：        伍玉蛟 (wuyujiao@jimiiot.com)
**完成日期：        2026.06.30
*********************************************************************
** 功能描述：       1. RTC应用层管理（创建任务、处理中断回调）
**                 2. 通过消息机制接收RTC中断事件
**                 3. 读操作（my_rtc_get_timestamp/my_rtc_get_calendar）无锁直接调用
**                 4. 写操作（my_rtc_set_timestamp/my_rtc_set_calendar）异步消息化，调用者立即返回
**                 5. 永久阻塞式消息处理循环
*********************************************************************/

#include "my_comm.h"

/*===========================================================================
 *  内部辅助函数声明
 *===========================================================================*/

static void my_rtc_second_callback(void);
static void my_rtc_alarm_callback(void);
static void my_rtc_overflow_callback(void);
static void my_rtc_task_init(void);
static void my_rtc_msg_handler(const my_msg_t *msg);
static void my_rtc_task_entry(void *pvParameters);

/*===========================================================================
 *  内部全局变量
 *===========================================================================*/

/* （无全局变量 — 读操作无锁，写操作通过消息异步处理） */

/*===========================================================================
 *  内部辅助函数实现
 *===========================================================================*/

/*********************************************************************
 * @brief   RTC秒中断回调函数
 * @return  none
 * @note    中断上下文，仅发消息通知任务，使用 my_msg_send_from_isr
 *********************************************************************/
static void my_rtc_second_callback(void)
{
    my_msg_t msg = {
        .id = MY_MSG_ID_RTC_SECOND_TICK,
        .data = NULL,
        .len = 0
    };

    my_msg_send_from_isr(MSG_QUEUE_RTC, &msg, 0);
}

/*********************************************************************
 * @brief   RTC闹钟中断回调函数
 * @return  none
 * @note    中断上下文，仅发消息通知任务，使用 my_msg_send_from_isr
 *********************************************************************/
static void my_rtc_alarm_callback(void)
{
    my_msg_t msg = {
        .id = MY_MSG_ID_RTC_ALARM_EVENT,
        .data = NULL,
        .len = 0
    };

    my_msg_send_from_isr(MSG_QUEUE_RTC, &msg, 0);
}

/*********************************************************************
 * @brief   RTC溢出中断回调函数
 * @return  none
 * @note    中断上下文，仅发消息通知任务，使用 my_msg_send_from_isr
 *********************************************************************/
static void my_rtc_overflow_callback(void)
{
    my_msg_t msg = {
        .id = MY_MSG_ID_RTC_OVERFLOW_EVENT,
        .data = NULL,
        .len = 0
    };

    my_msg_send_from_isr(MSG_QUEUE_RTC, &msg, 0);
}

/*********************************************************************
 * @brief   RTC任务消息处理函数
 * @param   msg 消息指针
 * @return  none
 * @note    根据消息ID分发处理RTC事件
 *********************************************************************/
static void my_rtc_msg_handler(const my_msg_t *msg)
{
    switch (msg->id)
    {
        case MY_MSG_ID_RTC_SECOND_TICK:
            MY_LOG_D("RTC second tick");
            /* TODO: 处理秒中断事件（更新时间显示等） */
            break;

        case MY_MSG_ID_RTC_ALARM_EVENT:
            MY_LOG_I("RTC alarm event triggered");
            /* TODO: 处理闹钟事件
             * 注意：如需循环闹钟，需在此重新设置下一次闹钟时间
             * uint32_t current_ts;
             * my_rtc_get_timestamp(&current_ts);
             * drv_rtc_set_alarm(current_ts + 3600);  // 1小时后再次触发
             */
            break;

        case MY_MSG_ID_RTC_OVERFLOW_EVENT:
            MY_LOG_W("RTC overflow! Counter wrapped to 0");
            /* TODO: 处理溢出事件
             * 32位时间戳将在2106年溢出，应通过NTP/网络重新同步时间
             * 该中断功能一般不启用
             */
            break;

        case MY_MSG_ID_RTC_SET_TIME:
        {
            uint32_t new_time = (uint32_t)(uintptr_t)msg->data;
            if (drv_rtc_set_time(new_time) != DRV_RTC_ERR_OK)
            {
                MY_LOG_E("RTC set time failed: %u", (unsigned int)new_time);
            }
            else
            {
                MY_LOG_D("RTC time set: %u", (unsigned int)new_time);
            }
            break;
        }

        case MY_MSG_ID_RTC_SET_CALENDAR:
        {
            struct tm *tm_ptr = (struct tm *)msg->data;
            time_t result = mktime(tm_ptr);
            if (result == (time_t)-1)
            {
                MY_LOG_E("RTC set calendar: mktime conversion failed");
            }
            else if (drv_rtc_set_time((uint32_t)result) != DRV_RTC_ERR_OK)
            {
                MY_LOG_E("RTC set calendar failed");
            }
            else
            {
                MY_LOG_D("RTC calendar set OK");
            }
            MY_FREE(tm_ptr);
            break;
        }

        case MY_MSG_ID_SYS_ACTIVE:
            MY_LOG_I("System activated, RTC task ready");
            /* 恢复秒中断 */
            drv_rtc_interrupt_enable(DRV_RTC_INT_SECOND);
            break;

        case MY_MSG_ID_SYS_SLEEP:
            MY_LOG_I("RTC task sleep requested");
            /* 关闭秒中断，保留闹钟中断用于唤醒 */
            drv_rtc_interrupt_disable(DRV_RTC_INT_SECOND);
            my_task_suspend(NULL);
            break;

        case MY_MSG_ID_SYS_SHUTDOWN:
            MY_LOG_W("RTC task shutdown requested");
            /* 关闭全部中断，但保留备份域（时间不丢失） */
            drv_rtc_interrupt_disable(DRV_RTC_INT_SECOND);
            drv_rtc_interrupt_disable(DRV_RTC_INT_ALARM);
            drv_rtc_interrupt_disable(DRV_RTC_INT_OVERFLOW);
            nvic_irq_disable(RTC_IRQn);
            my_task_suspend(NULL);
            break;

        default:
            MY_LOG_W("Unknown RTC message: id=0x%04X", msg->id);
            break;
    }
}

/*********************************************************************
 * @brief   RTC任务初始化
 * @return  none
 * @note    初始化RTC驱动，注册回调函数
 *********************************************************************/
static void my_rtc_task_init(void)
{
    int ret;
    drv_rtc_config_t rtc_cfg = {
        .clock_src = DRV_RTC_CLK_LXTAL,
        .prescaler = DRV_RTC_1HZ_PSC,
        .enable_second_int = true,
        .enable_alarm_int = true,
        .enable_overflow_int = false,
        .second_callback = my_rtc_second_callback,
        .alarm_callback = my_rtc_alarm_callback,
        .overflow_callback = NULL,
    };

    /* 初始化RTC驱动 */
    ret = drv_rtc_init(&rtc_cfg);
    if (ret != DRV_RTC_ERR_OK)
    {
        MY_LOG_E("RTC driver init failed: %d", ret);
        return;
    }

    MY_LOG_I("RTC driver init success");
    TASK_STATE_RTC = TASK_STATE_ACTIVE;
}

/*********************************************************************
 * @brief   RTC任务入口函数
 * @param   pvParameters 任务参数（未使用）
 * @return  none
 * @note    永久阻塞式消息循环：无事件时任务完全休眠，零CPU占用
 *          事件触发（中断消息/系统消息）时一次性处理
 *********************************************************************/
static void my_rtc_task_entry(void *pvParameters)
{
    my_msg_t msg;

    (void)pvParameters;

    my_rtc_task_init();

    MY_LOG_I("RTC task started");

    /* 永久阻塞式消息循环 */
    while (1)
    {
        if (my_msg_recv(MSG_QUEUE_RTC, &msg, portMAX_DELAY) == 0)
        {
            my_rtc_msg_handler(&msg);
        }
    }
}

/*===========================================================================
 *  公开API实现
 *===========================================================================*/

/*********************************************************************
 * @brief   初始化并启动RTC任务
 * @return  0: 成功  -1: 失败
 * @note    创建FreeRTOS任务和消息队列；重复调用安全返回0
 *********************************************************************/
int my_rtc_init(void)
{
    int32_t ret;

    /* 检查是否已初始化 */
    if (TASK_HANDLE_RTC != NULL)
    {
        return 0;
    }

    /* 创建消息队列 */
    MSG_QUEUE_RTC = my_msg_queue_create(MY_RTC_MSG_QUEUE_DEPTH, sizeof(my_msg_t));
    if (MSG_QUEUE_RTC == NULL)
    {
        MY_LOG_E("Failed to create RTC msg queue");
        return -1;
    }

    /* 创建RTC任务 */
    ret = my_task_create(&TASK_HANDLE_RTC, "RTC",
                          MY_RTC_TASK_STACK_SIZE,
                          my_rtc_task_entry, NULL,
                          MY_RTC_TASK_PRIO);

    /* 检查任务创建结果 */
    if (ret != 0 || TASK_HANDLE_RTC == NULL)
    {
        my_msg_queue_delete(MSG_QUEUE_RTC);
        MSG_QUEUE_RTC = NULL;

        MY_LOG_E("Failed to create RTC task(%d)", (int)ret);
        return -1;
    }

    MY_LOG_I("RTC init OK, task created");
    return 0;
}

/*********************************************************************
 * @brief   获取当前时间戳
 * @param   timestamp   输出参数，存储时间戳
 * @return  0: 成功  -1: 失败
 * @note    [并发安全] 纯读操作，不修改共享状态；栈变量隔离 + 硬件
 *          寄存器原子读 + 无读-改-写序列 → 多任务可安全并发调用
 * @note    [延时保障] 底层RSYNF同步仅~61µs，不会造成任务阻塞，
 *          与set_timestamp的SCIF秒级等待（已异步化）完全不同
 *********************************************************************/
int my_rtc_get_timestamp(uint32_t *timestamp)
{
    int ret;

    if (timestamp == NULL)
    {
        return -1;
    }

    ret = drv_rtc_get_time(timestamp);
    return (ret == DRV_RTC_ERR_OK) ? 0 : -1;
}

/*********************************************************************
 * @brief   设置时间戳（异步，立即返回）
 * @param   timestamp   Unix时间戳
 * @return  0: 成功  -1: 失败
 * @note    通过消息队列发送给RTC任务异步处理，调用者不阻塞
 * @note    timestamp通过msg.data指针传递（uintptr_t转换）
 *********************************************************************/
int my_rtc_set_timestamp(uint32_t timestamp)
{
    my_msg_t msg = {
        .id = MY_MSG_ID_RTC_SET_TIME,
        .data = (void *)(uintptr_t)timestamp,
        .len = 0
    };

    return my_msg_send(MSG_QUEUE_RTC, &msg, 0);
}

/*********************************************************************
 * @brief   获取日历（使用C标准库time.h）
 * @param   calendar    输出参数，存储struct tm结构体
 * @return  0: 成功  -1: 失败
 * @note    [并发安全] localtime_r()可重入，输出写入调用者私有buffer；
 *          底层硬件读无共享状态修改 → 多任务可安全并发调用
 * @note    [延时保障] 底层drv_rtc_get_time中RSYNF等待仅~61µs，
 *          localtime_r为纯CPU计算（无IO），整体延时在微秒级
 *********************************************************************/
int my_rtc_get_calendar(void *calendar)
{
    int ret;
    uint32_t timestamp;
    struct tm *calendar_out;

    if (calendar == NULL)
    {
        return -1;
    }

    calendar_out = (struct tm *)calendar;

    ret = drv_rtc_get_time(&timestamp);
    if (ret != DRV_RTC_ERR_OK)
    {
        return -1;
    }

    if (localtime_r((const time_t *)&timestamp, calendar_out) == NULL)
    {
        return -1;
    }

    return 0;
}

/*********************************************************************
 * @brief   设置日历（异步，立即返回）
 * @param   calendar    struct tm结构体指针
 * @return  0: 成功  -1: 失败
 * @note    动态分配struct tm拷贝通过消息发送给RTC任务，
 *          mktime()转换在RTC任务上下文执行，保证线程安全
 *********************************************************************/
int my_rtc_set_calendar(const void *calendar)
{
    my_msg_t msg;
    struct tm *tm_copy;

    if (calendar == NULL)
    {
        return -1;
    }

    /* 动态分配 struct tm 拷贝，避免 mktime 的线程安全问题 */
    MY_MALLOC(tm_copy, sizeof(struct tm));
    if (tm_copy == NULL)
    {
        MY_LOG_E("RTC set calendar: malloc failed");
        return -1;
    }

    memcpy(tm_copy, (const struct tm *)calendar, sizeof(struct tm));

    msg.id = MY_MSG_ID_RTC_SET_CALENDAR;
    msg.data = (void *)tm_copy;
    msg.len = 0;

    if (my_msg_send(MSG_QUEUE_RTC, &msg, 0) != 0)
    {
        MY_FREE(tm_copy);
        return -1;
    }

    return 0;
}
