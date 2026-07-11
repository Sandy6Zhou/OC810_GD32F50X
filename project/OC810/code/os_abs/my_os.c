/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       my_os.c
**文件描述：       OS抽象层实现文件（任务/信号量/临界区/消息/定时器统一管理）
**当前版本：       V1.1
**作    者：       伍玉蛟 (wuyujiao@jimiiot.com)
**完成日期：       2026.05.06
*********************************************************************
** 功能描述：       1. 实现OS抽象层（任务/信号量/临界区）
**                 2. 实现消息队列管理功能
**                 3. 实现定时器管理功能
**                 4. 实现系统级通用功能
**                 5. 跨平台移植抽象层实现
*********************************************************************/

#include "my_os.h"
#include "my_comm.h"
#include "gd32f50x.h"
#include <string.h>

/*********************************************************************
 * 内部数据结构定义
 *********************************************************************/

/** 定时器控制块 */
typedef struct
{
    TimerHandle_t timer_handle;     /**< FreeRTOS定时器句柄 */
    bool is_active;                 /**< 是否激活 */
} my_timer_ctrl_t;

/** 定时器控制表  */
static my_timer_ctrl_t s_timer_ctrl[MY_TIMER_ID_SLOT_MAX] = {0};

/*********************************************************************
 * 任务管理接口实现
 *********************************************************************/

/*********************************************************************
 * @brief 创建任务
 * @param task_handle 任务句柄指针（输出）
 * @param task_name 任务名称（最大16字符）
 * @param stack_size 堆栈大小（字，1字=4字节）
 * @param task_func 任务函数指针
 * @param param 任务参数
 * @param priority 任务优先级（0=configMAX_PRIORITIES-1）
 * @return 0=成功，-1=失败
 * @note 任务创建后自动进入就绪状态
 *********************************************************************/
int32_t my_task_create(my_task_handle_t *task_handle,
                       const char *task_name,
                       uint32_t stack_size,
                       my_task_func_t task_func,
                       void *param,
                       my_task_priority_t priority)
{
    BaseType_t ret;

    /* 参数检查 */
    if (task_handle == NULL || task_func == NULL)
    {
        MY_OS_LOGE("Invalid task create parameters");
        return -1;
    }

    /* 创建FreeRTOS任务 */
    ret = xTaskCreate(task_func, task_name, stack_size, param, priority, task_handle);
    if (ret != pdPASS)
    {
        MY_OS_LOGE("Failed to create task: %s", task_name);
        return -1;
    }

    return 0;
}

/*********************************************************************
 * @brief 任务延时直至（周期性任务使用）
 * @param period_ms 周期时间（毫秒）
 * @param last_wake_time 上次唤醒时间指针（由调用者维护）
 * @note 用于实现精确周期性任务，避免累积误差
 * @warning last_wake_time 必须由调用者初始化和维护，每个任务使用独立变量
 *********************************************************************/
void my_task_delay_until(uint32_t period_ms, my_tick_type_t *last_wake_time)
{
    /* 参数检查 */
    if (last_wake_time == NULL)
    {
        MY_OS_LOGE("Invalid last_wake_time pointer");
        return;
    }

    /* 首次调用时初始化 */
    if (*last_wake_time == 0)
    {
        *last_wake_time = xTaskGetTickCount();
    }

    vTaskDelayUntil(last_wake_time, pdMS_TO_TICKS(period_ms));
}

/*********************************************************************
 * 信号量接口实现
 *********************************************************************/

/*********************************************************************
 * @brief 创建二值信号量
 * @return 信号量句柄，NULL表示创建失败
 * @note 初始状态为无效（需要先Give才能Take）
 *********************************************************************/
my_sem_t my_sem_binary_create(void)
{
    SemaphoreHandle_t sem;

    /* 创建二值信号量 */
    sem = xSemaphoreCreateBinary();
    if (sem == NULL)
    {
        MY_OS_LOGE("Failed to create binary semaphore");
        return NULL;
    }

    return sem;
}

/*********************************************************************
 * @brief 创建计数信号量
 * @param max_count 最大计数值
 * @param init_count 初始计数值
 * @return 信号量句柄，NULL表示创建失败
 *********************************************************************/
my_sem_t my_sem_counting_create(uint32_t max_count, uint32_t init_count)
{
    SemaphoreHandle_t sem;

    /* 参数检查 */
    if (max_count == 0 || init_count > max_count)
    {
        MY_OS_LOGE("Invalid counting semaphore parameters: max=%lu, init=%lu",
                 max_count, init_count);
        return NULL;
    }

    /* 创建计数信号量 */
    sem = xSemaphoreCreateCounting(max_count, init_count);
    if (sem == NULL)
    {
        MY_OS_LOGE("Failed to create counting semaphore");
        return NULL;
    }

    return sem;
}

/*********************************************************************
 * @brief 创建互斥信号量
 * @return 信号量句柄，NULL表示创建失败
 * @note 互斥信号量支持优先级继承，防止优先级翻转
 *********************************************************************/
my_sem_t my_sem_mutex_create(void)
{
    SemaphoreHandle_t sem;

    /* 创建互斥信号量 */
    sem = xSemaphoreCreateMutex();
    if (sem == NULL)
    {
        MY_OS_LOGE("Failed to create mutex");
        return NULL;
    }

    return sem;
}

/*********************************************************************
 * @brief 获取信号量（任务上下文）
 * @param sem 信号量句柄
 * @param timeout_ms 超时时间（毫秒），0表示立即返回，portMAX_DELAY表示永久等待
 * @return 0=成功，-1=失败（超时）
 * @note 此函数只能从任务上下文调用
 *********************************************************************/
int32_t my_sem_take(my_sem_t sem, uint32_t timeout_ms)
{
    BaseType_t ret;
    TickType_t ticks;

    /* 参数检查 */
    if (sem == NULL)
    {
        MY_OS_LOGE("Invalid semaphore handle");
        return -1;
    }

    /* 转换超时时间 */
    if (timeout_ms == 0)
    {
        ticks = 0;
    }
    else if (timeout_ms == portMAX_DELAY)
    {
        ticks = portMAX_DELAY;
    }
    else
    {
        ticks = pdMS_TO_TICKS(timeout_ms);
    }

    /* 获取信号量 */
    ret = xSemaphoreTake(sem, ticks);
    if (ret != pdTRUE)
    {
        return -1;
    }

    return 0;
}

/*********************************************************************
 * @brief 获取信号量（中断上下文）
 * @param sem 信号量句柄
 * @param higher_priority_task_woken 是否需要触发任务切换
 * @return 0=成功，-1=失败（信号量无效）
 * @note 此函数只能从中断上下文调用
 *********************************************************************/
int32_t my_sem_take_from_isr(my_sem_t sem, my_base_type_t *higher_priority_task_woken)
{
    my_base_type_t ret;

    /* 参数检查 */
    if (sem == NULL)
    {
        return -1;
    }

    /* 从中断获取信号量 */
    ret = xSemaphoreTakeFromISR(sem, higher_priority_task_woken);
    if (ret != pdTRUE)
    {
        return -1;
    }

    return 0;
}

/*********************************************************************
 * @brief 释放信号量（任务上下文）
 * @param sem 信号量句柄
 * @return 0=成功，-1=失败
 *********************************************************************/
int32_t my_sem_give(my_sem_t sem)
{
    BaseType_t ret;

    /* 参数检查 */
    if (sem == NULL)
    {
        MY_OS_LOGE("Invalid semaphore handle");
        return -1;
    }

    /* 释放信号量 */
    ret = xSemaphoreGive(sem);
    if (ret != pdTRUE)
    {
        return -1;
    }

    return 0;
}

/*********************************************************************
 * @brief 释放信号量（中断上下文）
 * @param sem 信号量句柄
 * @param higher_priority_task_woken 是否需要触发任务切换
 * @return 0=成功，-1=失败
 * @note 此函数只能从中断上下文调用
 *********************************************************************/
int32_t my_sem_give_from_isr(my_sem_t sem, my_base_type_t *higher_priority_task_woken)
{
    my_base_type_t ret;

    /* 参数检查 */
    if (sem == NULL)
    {
        return -1;
    }

    /* 从中断释放信号量 */
    ret = xSemaphoreGiveFromISR(sem, higher_priority_task_woken);
    if (ret != pdTRUE)
    {
        return -1;
    }

    return 0;
}

/*********************************************************************
 * 消息队列管理接口实现
 *********************************************************************/

/*********************************************************************
 * @brief 创建消息队列
 * @param queue_len 队列长度（可容纳消息数量）
 * @param item_size 每个消息项的大小（字节）
 * @return 消息队列句柄，NULL表示创建失败
 * @note 队列创建后需保存句柄用于后续操作
 * @warning 队列长度和消息项大小必须根据实际需求选择合适的值
 *********************************************************************/
my_msg_queue_t my_msg_queue_create(uint32_t queue_len, uint32_t item_size)
{
    QueueHandle_t queue;

    /* 参数检查 */
    if (queue_len == 0 || item_size == 0)
    {
        MY_OS_LOGE("Invalid queue parameters: len=%lu, size=%lu", queue_len, item_size);
        return NULL;
    }

    /* 创建FreeRTOS队列 */
    queue = xQueueCreate(queue_len, item_size);
    if (queue == NULL)
    {
        MY_OS_LOGE("Failed to create message queue");
        return NULL;
    }

    return queue;
}

/*********************************************************************
 * @brief 发送消息（从任务上下文调用）
 * @param queue 消息队列句柄
 * @param msg 消息指针
 * @param timeout_ms 超时时间（毫秒），0表示立即返回，portMAX_DELAY表示永久等待
 * @return 0=成功，-1=失败（队列满或超时）
 * @note 此函数只能从任务上下文调用，不能在中断中使用
 *********************************************************************/
int32_t my_msg_send(my_msg_queue_t queue, const my_msg_t *msg, uint32_t timeout_ms)
{
    BaseType_t ret;
    TickType_t ticks;

    /* 参数检查 */
    if (queue == NULL || msg == NULL)
    {
        MY_OS_LOGE("Invalid send parameters");
        return -1;
    }

    /* 转换超时时间 */
    if (timeout_ms == 0)
    {
        ticks = 0;
    }
    else if (timeout_ms == portMAX_DELAY)
    {
        ticks = portMAX_DELAY;
    }
    else
    {
        ticks = pdMS_TO_TICKS(timeout_ms);
    }

    /* 发送消息 */
    ret = xQueueSend(queue, msg, ticks);
    if (ret != pdTRUE)
    {
        MY_OS_LOGW("Message send failed: timeout=%lu", timeout_ms);
        return -1;
    }

    return 0;
}

/*********************************************************************
 * @brief 发送消息（从中断上下文调用）
 * @param queue 消息队列句柄
 * @param msg 消息指针
 * @param higher_priority_task_woken 是否需要触发任务切换
 * @return 0=成功，-1=失败（队列满）
 * @note 此函数只能从中断上下文调用
 *********************************************************************/
int32_t my_msg_send_from_isr(my_msg_queue_t queue, const my_msg_t *msg,
                              my_base_type_t *higher_priority_task_woken)
{
    my_base_type_t ret;

    /* 参数检查 */
    if (queue == NULL || msg == NULL)
    {
        return -1;
    }

    /* 从中断发送消息 */
    ret = xQueueSendFromISR(queue, msg, higher_priority_task_woken);
    if (ret != pdTRUE)
    {
        return -1;
    }

    return 0;
}

/*********************************************************************
 * @brief 接收消息（从任务上下文调用）
 * @param queue 消息队列句柄
 * @param msg 消息存储指针
 * @param timeout_ms 超时时间（毫秒），0表示立即返回，portMAX_DELAY表示永久等待
 * @return 0=成功，-1=失败（队列空或超时）
 * @note 此函数只能从任务上下文调用，不能在中断中使用
 *********************************************************************/
int32_t my_msg_recv(my_msg_queue_t queue, my_msg_t *msg, uint32_t timeout_ms)
{
    BaseType_t ret;
    TickType_t ticks;

    /* 参数检查 */
    if (queue == NULL || msg == NULL)
    {
        MY_OS_LOGE("Invalid receive parameters");
        return -1;
    }

    /* 转换超时时间 */
    if (timeout_ms == 0)
    {
        ticks = 0;
    }
    else if (timeout_ms == portMAX_DELAY)
    {
        ticks = portMAX_DELAY;
    }
    else
    {
        ticks = pdMS_TO_TICKS(timeout_ms);
    }

    /* 接收消息 */
    ret = xQueueReceive(queue, msg, ticks);
    if (ret != pdTRUE)
    {
        return -1;
    }

    return 0;
}

/*********************************************************************
 * @brief 接收消息（从中断上下文调用）
 * @param queue 消息队列句柄
 * @param msg 消息存储指针
 * @param higher_priority_task_woken 是否需要触发任务切换
 * @return 0=成功，-1=失败（队列空）
 * @note 此函数只能从中断上下文调用
 *********************************************************************/
int32_t my_msg_recv_from_isr(my_msg_queue_t queue, my_msg_t *msg,
                              my_base_type_t *higher_priority_task_woken)
{
    my_base_type_t ret;

    /* 参数检查 */
    if (queue == NULL || msg == NULL)
    {
        return -1;
    }

    /* 从中断接收消息 */
    ret = xQueueReceiveFromISR(queue, msg, higher_priority_task_woken);
    if (ret != pdTRUE)
    {
        return -1;
    }

    return 0;
}

/*********************************************************************
 * @brief 查询消息队列中的消息数量
 * @param queue 消息队列句柄
 * @return 消息数量，0表示队列为空
 *********************************************************************/
uint32_t my_msg_queue_get_count(my_msg_queue_t queue)
{
    if (queue == NULL)
    {
        return 0;
    }

    return uxQueueMessagesWaiting(queue);
}

/*********************************************************************
 * @brief 查询消息队列剩余可用空间
 * @param queue 消息队列句柄
 * @return 剩余可容纳的消息数量
 *********************************************************************/
uint32_t my_msg_queue_get_spaces(my_msg_queue_t queue)
{
    if (queue == NULL)
    {
        return 0;
    }

    return uxQueueSpacesAvailable(queue);
}

/*********************************************************************
 * @brief 清空消息队列
 * @param queue 消息队列句柄
 * @return 0=成功，-1=失败
 *********************************************************************/
int32_t my_msg_queue_reset(my_msg_queue_t queue)
{
    if (queue == NULL)
    {
        return -1;
    }

    if (xQueueReset(queue) != pdTRUE)
    {
        return -1;
    }

    return 0;
}

/*********************************************************************
 * 定时器管理接口实现
 *********************************************************************/

#if (MY_OS_TIMER_BRIDGE_ENABLE == 1)
/*********************************************************************
 * @brief FreeRTOS定时器到期回调函数（内部使用）
 * @param timer FreeRTOS定时器句柄
 * @return 无
 * @note 从pvTimerID获取用户注册的回调函数并调用，实现类型安全的函数签名转换
 *********************************************************************/
static void my_timer_expiry_callback(TimerHandle_t timer)
{
    /* 获取用户回调函数（创建时通过pvTimerID传递） */
    my_timer_callback_t user_callback = (my_timer_callback_t)pvTimerGetTimerID(timer);

    /* 调用用户回调 */
    if (user_callback != NULL)
    {
        user_callback((my_timer_handle_t)timer);
    }
}
#endif

/*********************************************************************
 * @brief 创建定时器
 * @param timer_id 定时器ID
 * @param callback 定时器回调函数
 * @param period_ms 定时器周期（毫秒），0表示单次定时器
 * @param param 用户自定义参数（传递给回调函数）
 * @return 0=成功，-1=失败（ID无效或创建失败）
 * @note 定时器创建后处于停止状态，需调用my_timer_start启动
 *********************************************************************/
int32_t my_timer_create(my_timer_id_e timer_id, my_timer_callback_t callback,
                        uint32_t period_ms)
{
    TimerHandle_t timer_handle;
    TickType_t period_ticks;

    /* 参数检查 */
    if (timer_id >= MY_TIMER_ID_SLOT_MAX || callback == NULL)
    {
        MY_OS_LOGE("Invalid timer parameters: id=%d", timer_id);
        return -1;
    }

    /* 检查是否已创建 */
    if (s_timer_ctrl[timer_id].timer_handle != NULL)
    {
        MY_OS_LOGW("Timer %d already created", timer_id);
        return -1;
    }

    /* 转换周期 */
    period_ticks = pdMS_TO_TICKS(period_ms);

#if (MY_OS_TIMER_BRIDGE_ENABLE == 1)
    /* 方案A：使用桥接函数（推荐）- 100%解耦，应用层不依赖FreeRTOS */
    timer_handle = xTimerCreate("Timer", period_ticks,
                                (period_ms > 0) ? pdTRUE : pdFALSE,
                                (void *)callback,  /* 用户回调通过pvTimerID传递 */
                                my_timer_expiry_callback);  /* 桥接函数 */
#else
    /* 方案B：直接传递用户回调（零开销，但暴露FreeRTOS类型给应用层） */
    timer_handle = xTimerCreate("Timer", period_ticks,
                                (period_ms > 0) ? pdTRUE : pdFALSE,
                                NULL,  /* 不传递用户数据 */
                                (TimerCallbackFunction_t)callback);  /* 强制类型转换 */
#endif
    if (timer_handle == NULL)
    {
        MY_OS_LOGE("Failed to create timer %d", timer_id);
        return -1;
    }

    /* 保存控制块 */
    s_timer_ctrl[timer_id].timer_handle = timer_handle;
    s_timer_ctrl[timer_id].is_active = false;

    return 0;
}


/*********************************************************************
 * @brief 删除定时器
 * @param timer_id 定时器ID
 * @return 0=成功，-1=失败（ID无效或未创建）
 * @note 删除后如需使用需重新创建
 *********************************************************************/
int32_t my_timer_delete(my_timer_id_e timer_id)
{
    /* 参数检查 */
    if (timer_id >= MY_TIMER_ID_SLOT_MAX)
    {
        MY_OS_LOGE("Invalid timer ID: %d", timer_id);
        return -1;
    }

    /* 检查是否已创建 */
    if (s_timer_ctrl[timer_id].timer_handle == NULL)
    {
        MY_OS_LOGE("Timer %d not created", timer_id);
        return -1;
    }

    /* 删除定时器 */
    if (xTimerDelete(s_timer_ctrl[timer_id].timer_handle, 0) != pdTRUE)
    {
        MY_OS_LOGE("Failed to delete timer %d", timer_id);
        return -1;
    }

    /* 清空控制块 */
    memset(&s_timer_ctrl[timer_id], 0, sizeof(my_timer_ctrl_t));

    MY_OS_LOGI("Timer %d deleted", timer_id);
    return 0;
}

/*********************************************************************
 * @brief 查询定时器是否运行中
 * @param timer_id 定时器ID
 * @return true=运行中，false=已停止或未创建
 *********************************************************************/
bool my_timer_is_running(my_timer_id_e timer_id)
{
    if (timer_id >= MY_TIMER_ID_SLOT_MAX)
    {
        return false;
    }

    return s_timer_ctrl[timer_id].is_active;
}

/*********************************************************************
 * @brief 启动定时器
 * @param timer_id 定时器ID
 * @param timeout_ms 超时时间（毫秒），仅对单次定时器有效
 * @return 0=成功，-1=失败（ID无效或未创建）
 * @note 周期定时器会按创建时指定的周期重复触发
 *********************************************************************/
int32_t my_timer_start(my_timer_id_e timer_id, uint32_t timeout_ms)
{
    BaseType_t ret;
    TickType_t ticks;

    /* 参数检查 */
    if (timer_id >= MY_TIMER_ID_SLOT_MAX)
    {
        MY_OS_LOGE("Invalid timer ID: %d", timer_id);
        return -1;
    }

    /* 检查是否已创建 */
    if (s_timer_ctrl[timer_id].timer_handle == NULL)
    {
        MY_OS_LOGE("Timer %d not created", timer_id);
        return -1;
    }

    /* 如果已运行，先停止 */
    if (s_timer_ctrl[timer_id].is_active)
    {
        xTimerStop(s_timer_ctrl[timer_id].timer_handle, 0);
    }

    /* 如果是单次定时器且指定了超时时间，修改周期 */
    if (timeout_ms > 0)
    {
        ticks = pdMS_TO_TICKS(timeout_ms);
        xTimerChangePeriod(s_timer_ctrl[timer_id].timer_handle, ticks, 0);
    }

    /* 启动定时器 */
    ret = xTimerStart(s_timer_ctrl[timer_id].timer_handle, 0);
    if (ret != pdTRUE)
    {
        MY_OS_LOGE("Failed to start timer %d", timer_id);
        return -1;
    }

    s_timer_ctrl[timer_id].is_active = true;
    MY_OS_LOGD("Timer %d started", timer_id);
    return 0;
}

/*********************************************************************
 * @brief 在中断中启动定时器
 * @param timer_id 定时器ID
 * @return 0=成功，-1=失败（ID无效或未创建）
 * @note 仅用于中断上下文，不能阻塞
 *********************************************************************/
int32_t my_timer_start_from_isr(my_timer_id_e timer_id)
{
    BaseType_t ret;
    BaseType_t higher_priority_task_woken = pdFALSE;

    /* 参数检查 */
    if (timer_id >= MY_TIMER_ID_SLOT_MAX)
    {
        return -1;
    }

    /* 检查是否已创建 */
    if (s_timer_ctrl[timer_id].timer_handle == NULL)
    {
        return -1;
    }

    /* 启动定时器（FromISR版本） */
    ret = xTimerStartFromISR(s_timer_ctrl[timer_id].timer_handle, &higher_priority_task_woken);
    if (ret != pdTRUE)
    {
        return -1;
    }

    /* 更新状态 */
    s_timer_ctrl[timer_id].is_active = true;

    /* 如果需要，触发上下文切换 */
    portYIELD_FROM_ISR(higher_priority_task_woken);

    return 0;
}

/*********************************************************************
 * @brief 停止定时器
 * @param timer_id 定时器ID
 * @return 0=成功，-1=失败（ID无效或未创建）
 *********************************************************************/
int32_t my_timer_stop(my_timer_id_e timer_id)
{
    BaseType_t ret;

    /* 参数检查 */
    if (timer_id >= MY_TIMER_ID_SLOT_MAX)
    {
        MY_OS_LOGE("Invalid timer ID: %d", timer_id);
        return -1;
    }

    /* 检查是否已创建 */
    if (s_timer_ctrl[timer_id].timer_handle == NULL)
    {
        MY_OS_LOGE("Timer %d not created", timer_id);
        return -1;
    }

    /* 停止定时器 */
    ret = xTimerStop(s_timer_ctrl[timer_id].timer_handle, 0);
    if (ret != pdTRUE)
    {
        MY_OS_LOGW("Failed to stop timer %d", timer_id);
        return -1;
    }

    s_timer_ctrl[timer_id].is_active = false;
    MY_OS_LOGD("Timer %d stopped", timer_id);
    return 0;
}

/*********************************************************************
 * @brief 在中断中停止定时器
 * @param timer_id 定时器ID
 * @return 0=成功，-1=失败（ID无效或未创建）
 * @note 仅用于中断上下文，不能阻塞
 *********************************************************************/
int32_t my_timer_stop_from_isr(my_timer_id_e timer_id)
{
    BaseType_t ret;
    BaseType_t higher_priority_task_woken = pdFALSE;

    /* 参数检查 */
    if (timer_id >= MY_TIMER_ID_SLOT_MAX)
    {
        return -1;
    }

    /* 检查是否已创建 */
    if (s_timer_ctrl[timer_id].timer_handle == NULL)
    {
        return -1;
    }

    /* 停止定时器（FromISR版本） */
    ret = xTimerStopFromISR(s_timer_ctrl[timer_id].timer_handle, &higher_priority_task_woken);
    if (ret != pdTRUE)
    {
        return -1;
    }

    /* 更新状态 */
    s_timer_ctrl[timer_id].is_active = false;

    /* 如果需要，触发上下文切换 */
    portYIELD_FROM_ISR(higher_priority_task_woken);

    return 0;
}

/*********************************************************************
 * @brief 重置定时器计数
 * @param timer_id 定时器ID
 * @return 0=成功，-1=失败（ID无效或未创建）
 * @note 重新开始计数
 *********************************************************************/
int32_t my_timer_reset(my_timer_id_e timer_id)
{
    BaseType_t ret;

    /* 参数检查 */
    if (timer_id >= MY_TIMER_ID_SLOT_MAX)
    {
        MY_OS_LOGE("Invalid timer ID: %d", timer_id);
        return -1;
    }

    /* 检查是否已创建 */
    if (s_timer_ctrl[timer_id].timer_handle == NULL)
    {
        MY_OS_LOGE("Timer %d not created", timer_id);
        return -1;
    }

    /* 重置定时器（无论是否运行都会重新开始计数） */
    ret = xTimerReset(s_timer_ctrl[timer_id].timer_handle, 0);
    if (ret != pdTRUE)
    {
        MY_OS_LOGE("Failed to reset timer %d", timer_id);
        return -1;
    }

    /* 重置后定时器会运行 */
    s_timer_ctrl[timer_id].is_active = true;

    MY_OS_LOGD("Timer %d reset", timer_id);
    return 0;
}

/*********************************************************************
 * @brief 在中断中重置定时器
 * @param timer_id 定时器ID
 * @return 0=成功，-1=失败（ID无效或未创建）
 * @note 仅用于中断上下文，不能阻塞
 *********************************************************************/
int32_t my_timer_reset_from_isr(my_timer_id_e timer_id)
{
    BaseType_t ret;
    BaseType_t higher_priority_task_woken = pdFALSE;

    /* 参数检查 */
    if (timer_id >= MY_TIMER_ID_SLOT_MAX)
    {
        return -1;
    }

    /* 检查是否已创建 */
    if (s_timer_ctrl[timer_id].timer_handle == NULL)
    {
        return -1;
    }

    /* 重置定时器（FromISR版本） */
    ret = xTimerResetFromISR(s_timer_ctrl[timer_id].timer_handle, &higher_priority_task_woken);
    if (ret != pdTRUE)
    {
        return -1;
    }

    /* 重置后定时器会运行 */
    s_timer_ctrl[timer_id].is_active = true;

    /* 如果需要，触发上下文切换 */
    portYIELD_FROM_ISR(higher_priority_task_woken);

    return 0;
}

/*********************************************************************
 * @brief 修改定时器周期
 * @param timer_id 定时器ID
 * @param new_period_ms 新周期（毫秒）
 * @return 0=成功，-1=失败（ID无效或未创建）
 * @note 在任务上下文中调用
 *********************************************************************/
int32_t my_timer_change(my_timer_id_e timer_id, uint32_t new_period_ms)
{
    BaseType_t ret;
    TickType_t ticks;

    /* 参数检查 */
    if (timer_id >= MY_TIMER_ID_SLOT_MAX)
    {
        MY_OS_LOGE("Invalid timer ID: %d", timer_id);
        return -1;
    }

    /* 检查是否已创建 */
    if (s_timer_ctrl[timer_id].timer_handle == NULL)
    {
        MY_OS_LOGE("Timer %d not created", timer_id);
        return -1;
    }

    /* 转换周期 */
    ticks = pdMS_TO_TICKS(new_period_ms);

    /* 修改周期 */
    ret = xTimerChangePeriod(s_timer_ctrl[timer_id].timer_handle, ticks, 0);
    if (ret != pdTRUE)
    {
        MY_OS_LOGE("Failed to change timer %d period", timer_id);
        return -1;
    }

    MY_OS_LOGD("Timer %d period changed to %lu ms", timer_id, new_period_ms);
    return 0;
}

/*********************************************************************
 * @brief 在中断中修改定时器周期
 * @param timer_id 定时器ID
 * @param new_period_ms 新周期（毫秒）
 * @return 0=成功，-1=失败（ID无效或未创建）
 * @note 仅用于中断上下文，不能阻塞
 *********************************************************************/
int32_t my_timer_change_from_isr(my_timer_id_e timer_id, uint32_t new_period_ms)
{
    BaseType_t ret;
    BaseType_t higher_priority_task_woken = pdFALSE;
    TickType_t ticks;

    /* 参数检查 */
    if (timer_id >= MY_TIMER_ID_SLOT_MAX)
    {
        return -1;
    }

    /* 检查是否已创建 */
    if (s_timer_ctrl[timer_id].timer_handle == NULL)
    {
        return -1;
    }

    /* 转换周期 */
    ticks = pdMS_TO_TICKS(new_period_ms);

    /* 修改周期（FromISR版本） */
    ret = xTimerChangePeriodFromISR(s_timer_ctrl[timer_id].timer_handle, ticks, &higher_priority_task_woken);
    if (ret != pdTRUE)
    {
        return -1;
    }

    /* 如果需要，触发上下文切换 */
    portYIELD_FROM_ISR(higher_priority_task_woken);

    return 0;
}

/*********************************************************************
 * 事件组（Event Group）接口实现
 *********************************************************************/

/*********************************************************************
 * @brief 创建事件组
 * @return 事件组句柄，NULL表示创建失败
 *********************************************************************/
my_event_group_t my_event_group_create(void)
{
    EventGroupHandle_t group;

    group = xEventGroupCreate();
    if (group == NULL)
    {
        MY_OS_LOGE("Failed to create event group");
        return NULL;
    }

    return group;
}

/*********************************************************************
 * @brief 设置事件位
 * @param group 事件组句柄
 * @param bits 要设置的事件位
 * @return 设置后的事件位值
 *********************************************************************/
my_event_bits_t my_event_group_set_bits(my_event_group_t group, my_event_bits_t bits)
{
    if (group == NULL)
    {
        return 0;
    }

    return xEventGroupSetBits(group, bits);
}

/*********************************************************************
 * @brief 在中断中设置事件位
 * @param group 事件组句柄
 * @param bits 要设置的事件位
 * @param higher_priority_task_woken 是否需要触发任务切换
 * @return 设置后的事件位值
 *********************************************************************/
my_event_bits_t my_event_group_set_bits_from_isr(my_event_group_t group, my_event_bits_t bits,
                                                   my_base_type_t *higher_priority_task_woken)
{
    BaseType_t ret;

    if (group == NULL)
    {
        return 0;
    }

    ret = xEventGroupSetBitsFromISR(group, bits, higher_priority_task_woken);
    if (ret != pdTRUE)
    {
        return 0;
    }

    return bits;
}

/*********************************************************************
 * @brief 等待事件位
 * @param group 事件组句柄
 * @param bits 要等待的事件位
 * @param clear_on_exit 退出时是否清除事件位
 * @param wait_all 是否等待所有位
 * @param timeout_ms 超时时间（毫秒）
 * @return 实际触发的事件位值
 *********************************************************************/
my_event_bits_t my_event_group_wait_bits(my_event_group_t group, my_event_bits_t bits,
                                          bool clear_on_exit, bool wait_all, uint32_t timeout_ms)
{
    TickType_t ticks;

    if (group == NULL)
    {
        return 0;
    }

    /* 转换超时时间 */
    if (timeout_ms == portMAX_DELAY)
    {
        ticks = portMAX_DELAY;
    }
    else
    {
        ticks = pdMS_TO_TICKS(timeout_ms);
    }

    return xEventGroupWaitBits(group, bits,
                                clear_on_exit ? pdTRUE : pdFALSE,
                                wait_all ? pdTRUE : pdFALSE,
                                ticks);
}

/*********************************************************************
 * @brief 清除事件位
 * @param group 事件组句柄
 * @param bits 要清除的事件位
 * @return 清除后的事件位值
 *********************************************************************/
my_event_bits_t my_event_group_clear_bits(my_event_group_t group, my_event_bits_t bits)
{
    if (group == NULL)
    {
        return 0;
    }

    return xEventGroupClearBits(group, bits);
}

/*********************************************************************
 * 任务通知（Task Notification）接口实现
 *********************************************************************/

/*********************************************************************
 * @brief 发送任务通知（递增方式）
 * @param task 目标任务句柄
 * @return 0=成功，-1=失败
 *********************************************************************/
int32_t my_task_notify_give(my_task_handle_t task)
{
    if (task == NULL)
    {
        return -1;
    }

    xTaskNotifyGive(task);
    return 0;
}

/*********************************************************************
 * @brief 在中断中发送任务通知
 * @param task 目标任务句柄
 * @param higher_priority_task_woken 是否需要触发任务切换
 * @return 0=成功，-1=失败
 *********************************************************************/
int32_t my_task_notify_give_from_isr(my_task_handle_t task,
                                       my_base_type_t *higher_priority_task_woken)
{
    if (task == NULL)
    {
        return -1;
    }

    /* vTaskNotifyGiveFromISR 返回 void，通过参数返回是否需要切换 */
    vTaskNotifyGiveFromISR(task, higher_priority_task_woken);

    return 0;
}

/*********************************************************************
 * @brief 等待任务通知
 * @param clear_count 是否清除计数
 * @param timeout_ms 超时时间（毫秒）
 * @return 收到的通知计数值
 *********************************************************************/
uint32_t my_task_notify_take(bool clear_count, uint32_t timeout_ms)
{
    TickType_t ticks;

    /* 转换超时时间 */
    if (timeout_ms == portMAX_DELAY)
    {
        ticks = portMAX_DELAY;
    }
    else
    {
        ticks = pdMS_TO_TICKS(timeout_ms);
    }

    return ulTaskNotifyTake(clear_count ? pdTRUE : pdFALSE, ticks);
}

/*********************************************************************
 * @brief 设置任务通知值
 * @param task 目标任务句柄
 * @param value 通知值
 * @return 0=成功，-1=失败
 *********************************************************************/
int32_t my_task_notify_set_value(my_task_handle_t task, uint32_t value)
{
    BaseType_t ret;

    if (task == NULL)
    {
        return -1;
    }

    ret = xTaskNotify(task, value, eSetValueWithOverwrite);
    if (ret != pdTRUE)
    {
        return -1;
    }

    return 0;
}

/*********************************************************************
 * @brief 设置任务通知位
 * @param task 目标任务句柄
 * @param bits 要设置的位
 * @return 0=成功，-1=失败
 *********************************************************************/
int32_t my_task_notify_set_bits(my_task_handle_t task, uint32_t bits)
{
    BaseType_t ret;

    if (task == NULL)
    {
        return -1;
    }

    ret = xTaskNotify(task, bits, eSetBits);
    if (ret != pdTRUE)
    {
        return -1;
    }

    return 0;
}

/*********************************************************************
 * 任务状态查询接口实现
 *********************************************************************/

/*********************************************************************
 * @brief 获取任务状态
 * @param task 任务句柄
 * @return 任务状态枚举值
 *********************************************************************/
my_task_state_e my_task_get_state(my_task_handle_t task)
{
    eTaskState state;

    if (task == NULL)
    {
        return MY_TASK_STATE_DELETED;
    }

    state = eTaskGetState(task);

    switch (state)
    {
        case eRunning:
            return MY_TASK_STATE_RUNNING;
        case eReady:
            return MY_TASK_STATE_READY;
        case eBlocked:
            return MY_TASK_STATE_BLOCKED;
        case eSuspended:
            return MY_TASK_STATE_SUSPENDED;
        case eDeleted:
            return MY_TASK_STATE_DELETED;
        default:
            return MY_TASK_STATE_DELETED;
    }
}

/*********************************************************************
 * @brief 获取任务栈水位
 * @param task 任务句柄
 * @return 剩余栈大小（字）
 *********************************************************************/
uint32_t my_task_get_stack_watermark(my_task_handle_t task)
{
    if (task == NULL)
    {
        return 0;
    }

    return uxTaskGetStackHighWaterMark(task);
}

/*********************************************************************
 * @brief 获取任务名称
 * @param task 任务句柄
 * @return 任务名称字符串指针
 *********************************************************************/
const char* my_task_get_name(my_task_handle_t task)
{
    if (task == NULL)
    {
        return "NULL";
    }

    return pcTaskGetName(task);
}

/*********************************************************************
 * 内存管理接口实现
 *********************************************************************/

/*********************************************************************
 * @brief 获取当前空闲堆内存大小
 * @return 空闲堆内存大小（字节）
 *********************************************************************/
uint32_t my_os_get_free_heap_size(void)
{
    return xPortGetFreeHeapSize();
}

/*********************************************************************
 * @brief 获取历史最小空闲堆内存大小
 * @return 历史最小空闲堆内存大小（字节）
 *********************************************************************/
uint32_t my_os_get_min_free_heap_size(void)
{
    return xPortGetMinimumEverFreeHeapSize();
}

/*********************************************************************
 * @brief 获取当前系统中的任务总数
 * @return 任务数量
 *********************************************************************/
uint32_t my_os_get_task_count(void)
{
    return uxTaskGetNumberOfTasks();
}

/*********************************************************************
 * 系统级通用接口实现
 *********************************************************************/

/*********************************************************************
 * @brief 错误处理函数（进入死循环）
 * @note 发生严重错误时调用，系统将停止运行
 *********************************************************************/
void my_error_handler(void)
{
    MY_OS_LOGE("Fatal error occurred, system halted!");

    taskENTER_CRITICAL();

    while (true)
    {
        /* 死循环，等待看门狗复位 */
    }
}

/*********************************************************************
 * @brief 系统复位
 * @param delay_ms 延时时间（毫秒），确保日志输出，0表示不延时
 * @note 延时后执行系统复位
 *********************************************************************/
void my_system_reset(uint32_t delay_ms)
{
    MY_OS_LOGE("System reset!");

    /* 延时以确保日志输出 */
    if (delay_ms > 0)
    {
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }

    NVIC_SystemReset();
}
