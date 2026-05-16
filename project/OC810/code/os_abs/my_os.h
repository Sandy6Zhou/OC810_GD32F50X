/********************************************************************
**版权所有：        深圳市几米物联有限公司
**文件名称：        my_os.h
**文件描述：        OS抽象层头文件（任务/信号量/临界区/消息/定时器统一管理）
**当前版本：        V1.1
**作    者：        伍玉蛟 (wuyujiao@jimiiot.com)
**完成日期：        2026.05.06
*********************************************************************
** 功能描述：       1. OS抽象层（任务/信号量/临界区）
**                 2. 统一消息队列管理接口
**                 3. 统一定时器管理接口
**                 4. 跨平台移植抽象层
**                 5. 系统级通用功能接口
*********************************************************************/

#ifndef __MY_OS_H__
#define __MY_OS_H__

/* ========== 系统头文件引用 ========== */
/* 标准C库 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* FreeRTOS头文件 */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"
#include "semphr.h"

/*********************************************************************
 * OS抽象层日志配置
 *********************************************************************/

/**
 * @brief OS抽象层日志使能开关
 * @note 定义为1启用日志，定义为0禁用日志（节省代码空间）
 */
#ifndef MY_OS_LOG_ENABLE
#define MY_OS_LOG_ENABLE    1
#endif

/**
 * @brief OS抽象层日志级别定义
 * @note 0=关闭, 1=ERROR, 2=WARNING, 3=DEBUG
 */
#ifndef MY_OS_LOG_LEVEL
#define MY_OS_LOG_LEVEL     3   /**< 默认输出所有级别日志 */
#endif

/* 日志级别枚举 */
#define MY_OS_LOG_LEVEL_OFF     0
#define MY_OS_LOG_LEVEL_ERROR   1
#define MY_OS_LOG_LEVEL_WARN    2
#define MY_OS_LOG_LEVEL_DEBUG   3

/* 日志宏定义 */
#if (MY_OS_LOG_ENABLE == 1)

/* 需要包含日志系统头文件 */
#include "my_log.h"

/**
 * @brief DEBUG级别日志
 */
#if (MY_OS_LOG_LEVEL >= MY_OS_LOG_LEVEL_DEBUG)
#define MY_OS_LOGD(fmt, ...)   MY_LOG_D("[OS] " fmt, ##__VA_ARGS__)
#else
#define MY_OS_LOGD(fmt, ...)
#endif

/**
 * @brief WARNING级别日志
 */
#if (MY_OS_LOG_LEVEL >= MY_OS_LOG_LEVEL_WARN)
#define MY_OS_LOGW(fmt, ...)   MY_LOG_W("[OS] " fmt, ##__VA_ARGS__)
#else
#define MY_OS_LOGW(fmt, ...)
#endif

/**
 * @brief ERROR级别日志
 */
#if (MY_OS_LOG_LEVEL >= MY_OS_LOG_LEVEL_ERROR)
#define MY_OS_LOGE(fmt, ...)   MY_LOG_E("[OS] " fmt, ##__VA_ARGS__)
#else
#define MY_OS_LOGE(fmt, ...)
#endif

/**
 * @brief INFO级别日志
 */
#define MY_OS_LOGI(fmt, ...)   MY_LOG_I("[OS] " fmt, ##__VA_ARGS__)

#else /* MY_OS_LOG_ENABLE == 0 */

/* 日志禁用，定义为空 */
#define MY_OS_LOGD(fmt, ...)
#define MY_OS_LOGW(fmt, ...)
#define MY_OS_LOGE(fmt, ...)
#define MY_OS_LOGI(fmt, ...)

#endif /* MY_OS_LOG_ENABLE */

/*********************************************************************
 * OS抽象层基础类型定义
 *********************************************************************/

/**
 * @brief 基础类型（FreeRTOS BaseType_t）
 * @note 用于函数返回值、状态标志等
 */
typedef BaseType_t my_base_type_t;

/**
 * @brief 无符号基础类型（FreeRTOS UBaseType_t）
 * @note 用于优先级、计数器等无符号值
 */
typedef UBaseType_t my_ubase_type_t;

/**
 * @brief 系统滴答类型（FreeRTOS TickType_t）
 * @note 用于时间计算、延时、超时等
 */
typedef TickType_t my_tick_type_t;

/*********************************************************************
 * 任务管理类型定义
 *********************************************************************/

/**
 * @brief 任务句柄类型（FreeRTOS TaskHandle_t）
 */
typedef TaskHandle_t my_task_handle_t;

/**
 * @brief 任务优先级类型
 * @note 范围：0 到 (configMAX_PRIORITIES - 1)，数值越大优先级越高
 */
typedef my_ubase_type_t my_task_priority_t;

/**
 * @brief 任务函数类型
 * @param param 任务参数
 */
typedef void (*my_task_func_t)(void *param);

/*********************************************************************
 * 信号量类型定义
 *********************************************************************/

/**
 * @brief 信号量句柄类型（FreeRTOS SemaphoreHandle_t）
 */
typedef SemaphoreHandle_t my_sem_t;

/*********************************************************************
 * 消息类型定义
 *********************************************************************/

/**
 * @brief 消息队列句柄类型（FreeRTOS Queue）
 */
typedef QueueHandle_t my_msg_queue_t;

/**
 * @brief 消息ID枚举定义
 */
typedef enum
{
    MY_MSG_ID_BASE = 0,         /**< 消息ID基值 */
    MY_MSG_ID_TEST,             /**< 测试消息 */
    MY_MSG_ID_ISR_TEST,         /**< 中断测试消息 */

    MY_MSG_ID_MAX               /**< 消息ID最大值 */
} my_msg_id_e;

/**
 * @brief 消息结构体定义
 */
typedef struct
{
    my_msg_id_e msg_id;         /**< 消息ID */
    void *msg_data;             /**< 消息数据指针 */
    uint32_t msg_len;           /**< 消息数据长度（字节） */
} my_msg_t;

/*********************************************************************
 * 定时器类型定义
 *********************************************************************/

/**
 * @brief 定时器ID枚举定义
 */
typedef enum
{
    MY_TIMER_ID_ONE_MINUTE = 0, /**< 1分钟定时器（核心定时器） */
    MY_TIMER_ID_TEST,           /**< 测试定时器 */
    MY_TIMER_ID_ISR_TEST,       /**< 中断安全API测试定时器 */

    MY_TIMER_ID_MAX             /**< 定时器ID最大值 */
} my_timer_id_e;

/**
 * @brief 定时器句柄类型（透明指针，隐藏RTOS细节）
 * @note 应用层只需保存此句柄，无需关心内部实现
 */
typedef void* my_timer_handle_t;

/**
 * @brief 定时器回调函数类型
 * @param timer_handle 定时器句柄
 * @note 每个定时器使用独立的回调函数，无需区分timer_id
 */
typedef void (*my_timer_callback_t)(my_timer_handle_t timer_handle);

/*********************************************************************
 * 任务管理接口
 *********************************************************************/

/**
 * @brief 创建任务
 * @param task_handle 任务句柄指针（输出）
 * @param task_name 任务名称（最大16字符）
 * @param stack_size 堆栈大小（字，1字=4字节）
 * @param task_func 任务函数指针
 * @param param 任务参数
 * @param priority 任务优先级（0=configMAX_PRIORITIES-1）
 * @return 0=成功，-1=失败
 * @note 任务创建后自动进入就绪状态
 * @example
 * @code
 * void my_task(void *param)
 * {
 *     while (1) {
 *         // 任务处理逻辑
 *     }
 * }
 *
 * my_task_handle_t task_handle;
 * my_task_create(&task_handle, "my_task", 1024, my_task, NULL, 1);
 * @endcode
 */
int32_t my_task_create(my_task_handle_t *task_handle,
                       const char *task_name,
                       uint32_t stack_size,
                       my_task_func_t task_func,
                       void *param,
                       my_task_priority_t priority);

/**
 * @brief 删除任务
 * @param task_handle 任务句柄，NULL表示删除当前任务
 * @note 删除任务后会自动释放任务堆栈和TCB内存
 */
#define my_task_delete(task_handle)     vTaskDelete(task_handle)

/**
 * @brief 挂起任务
 * @param task_handle 任务句柄，NULL表示挂起当前任务
 */
#define my_task_suspend(task_handle)    vTaskSuspend(task_handle)

/**
 * @brief 恢复任务
 * @param task_handle 任务句柄
 * @note 任务句柄不能为NULL
 */
#define my_task_resume(task_handle)     vTaskResume(task_handle)

/**
 * @brief 获取当前任务句柄
 * @return 当前任务句柄
 */
#define my_task_get_current()           xTaskGetCurrentTaskHandle()

/**
 * @brief 获取系统滴答计数（任务上下文）
 * @return 当前系统滴答值（tick）
 * @note 用于计算时间差、超时检测等
 */
#define my_os_get_tick()                xTaskGetTickCount()

/**
 * @brief 获取系统滴答计数（中断上下文）
 * @return 当前系统滴答值（tick）
 * @note 只能在中断服务函数中调用
 */
#define my_os_get_tick_from_isr()       xTaskGetTickCountFromISR()

/**
 * @brief 毫秒转系统滴答
 * @param ms 毫秒值
 * @return 对应的系统滴答值
 * @note 便捷宏，用于超时计算和时间转换
 */
#define my_ms_to_ticks(ms)              pdMS_TO_TICKS(ms)

/**
 * @brief 系统滴答转毫秒
 * @param ticks 系统滴答值
 * @return 对应的毫秒值
 * @note 便捷宏，用于时间差值转换
 * @example
 * @code
 * // 计算实际延时时间
 * TickType_t tick_before = my_os_get_tick();
 * my_task_delay_ms(1000);
 * TickType_t tick_after = my_os_get_tick();
 * uint32_t delay_ms = my_ticks_to_ms(tick_after - tick_before);
 * @endcode
 */
#define my_ticks_to_ms(ticks)           ((ticks) * portTICK_PERIOD_MS)

/**
 * @brief 任务延时（毫秒）
 * @param delay_ms 延时时间（毫秒）
 * @note 任务进入阻塞态，让出CPU
 */
#define my_task_delay_ms(delay_ms)      vTaskDelay(pdMS_TO_TICKS(delay_ms))

/**
 * @brief 任务延时直至（周期性任务使用）
 * @param period_ms 周期时间（毫秒）
 * @param last_wake_time 上次唤醒时间指针（由调用者维护）
 * @note 用于实现精确周期性任务，避免累积误差
 * @example
 * @code
 * // 在任务中维护 last_wake_time 变量
 * static my_tick_type_t last_wake_time = 0;
 * void my_task(void *param)
 * {
 *     // 初始化
 *     last_wake_time = my_os_get_tick();
 *
 *     while (1) {
 *         do_periodic_work();
 *         my_task_delay_until(100, &last_wake_time);  // 100ms周期
 *     }
 * }
 * @endcode
 */
void my_task_delay_until(uint32_t period_ms, my_tick_type_t *last_wake_time);

/*********************************************************************
 * 信号量接口
 *********************************************************************/

/**
 * @brief 创建二值信号量
 * @return 信号量句柄，NULL表示创建失败
 * @note 初始状态为无效（需要先Give才能Take）
 * @example
 * @code
 * my_sem_t sem = my_sem_binary_create();
 * my_sem_give(sem);  // 先释放，使信号量有效
 * my_sem_take(sem, 1000);  // 获取，超时1000ms
 * @endcode
 */
my_sem_t my_sem_binary_create(void);

/**
 * @brief 创建计数信号量
 * @param max_count 最大计数值
 * @param init_count 初始计数值
 * @return 信号量句柄，NULL表示创建失败
 */
my_sem_t my_sem_counting_create(uint32_t max_count, uint32_t init_count);

/**
 * @brief 创建互斥信号量
 * @return 信号量句柄，NULL表示创建失败
 * @note 互斥信号量支持优先级继承，防止优先级翻转
 */
my_sem_t my_sem_mutex_create(void);

/**
 * @brief 获取信号量（任务上下文）
 * @param sem 信号量句柄
 * @param timeout_ms 超时时间（毫秒），0表示立即返回，portMAX_DELAY表示永久等待
 * @return 0=成功，-1=失败（超时）
 * @note 此函数只能从任务上下文调用
 */
int32_t my_sem_take(my_sem_t sem, uint32_t timeout_ms);

/**
 * @brief 获取信号量（中断上下文）
 * @param sem 信号量句柄
 * @param higher_priority_task_woken 是否需要触发任务切换
 * @return 0=成功，-1=失败（信号量无效）
 * @note 此函数只能从中断上下文调用
 */
int32_t my_sem_take_from_isr(my_sem_t sem, my_base_type_t *higher_priority_task_woken);

/**
 * @brief 释放信号量（任务上下文）
 * @param sem 信号量句柄
 * @return 0=成功，-1=失败
 */
int32_t my_sem_give(my_sem_t sem);

/**
 * @brief 释放信号量（中断上下文）
 * @param sem 信号量句柄
 * @param higher_priority_task_woken 是否需要触发任务切换
 * @return 0=成功，-1=失败
 * @note 此函数只能从中断上下文调用
 */
int32_t my_sem_give_from_isr(my_sem_t sem, my_base_type_t *higher_priority_task_woken);

/**
 * @brief 删除信号量
 * @param sem 信号量句柄
 * @note 删除前请确保没有任务正在等待该信号量
 */
#define my_sem_delete(sem)              vSemaphoreDelete(sem)

/*********************************************************************
 * 临界区保护接口
 *********************************************************************/

/**
 * @brief 进入临界区（任务上下文）
 * @note 与 my_critical_exit() 配对使用
 * @note 仅用于任务上下文，不能在中断中使用
 * @example
 * @code
 * my_critical_enter();
 * // 临界区代码（访问共享资源）
 * shared_variable++;
 * my_critical_exit();
 * @endcode
 */
#define my_critical_enter()                 taskENTER_CRITICAL()

/**
 * @brief 退出临界区（任务上下文）
 * @note 与 my_critical_enter() 配对使用
 * @note 仅用于任务上下文，不能在中断中使用
 */
#define my_critical_exit()                  taskEXIT_CRITICAL()

/**
 * @brief 进入临界区并保存中断状态（中断安全）
 * @return 中断状态值（用于退出时恢复）
 * @note 与 my_critical_exit_from_isr() 配对使用
 * @note 可用于任务上下文和中断上下文
 * @note 支持嵌套使用（多次进入会保存不同的状态值）
 * @note 命名带 _from_isr 是为了与 FreeRTOS 原生 API 保持一致
 * @example
 * @code
 * // 在任务中使用
 * UBaseType_t state = my_critical_enter_from_isr();
 * shared_variable++;
 * my_critical_exit_from_isr(state);
 *
 * // 在中断中使用
 * void EXTI_IRQHandler(void)
 * {
 *     UBaseType_t state = my_critical_enter_from_isr();
 *     shared_variable++;
 *     my_critical_exit_from_isr(state);
 * }
 * @endcode
 */
#define my_critical_enter_from_isr()        taskENTER_CRITICAL_FROM_ISR()

/**
 * @brief 退出临界区并恢复中断状态（中断安全）
 * @param state 之前保存的中断状态值（由 my_critical_enter_from_isr() 返回）
 * @note 必须与 my_critical_enter_from_isr() 配对使用
 * @note 可用于任务上下文和中断上下文
 */
#define my_critical_exit_from_isr(state)    taskEXIT_CRITICAL_FROM_ISR(state)

/*********************************************************************
 * 消息队列管理接口
 *********************************************************************/

/**
 * @brief 创建消息队列
 * @param queue_len 队列长度（可容纳消息数量）
 * @param item_size 每个消息项的大小（字节）
 * @return 消息队列句柄，NULL表示创建失败
 * @note 队列创建后需保存句柄用于后续操作
 * @warning 队列长度和消息项大小必须根据实际需求选择合适的值
 * @example
 * @code
 * // 创建可容纳10条my_msg_t消息的队列
 * my_msg_queue_t queue = my_msg_queue_create(10, sizeof(my_msg_t));
 *
 * // 发送消息
 * my_msg_t msg = {.msg_id = MY_MSG_ID_TEST, .msg_data = NULL, .msg_len = 0};
 * my_msg_send(queue, &msg, 100);
 *
 * // 接收消息
 * my_msg_t recv_msg;
 * my_msg_recv(queue, &recv_msg, 100);
 * @endcode
 */
my_msg_queue_t my_msg_queue_create(uint32_t queue_len, uint32_t item_size);

/**
 * @brief 删除消息队列
 * @param queue 消息队列句柄
 * @note 删除前请确保没有任务正在使用该队列
 */
#define my_msg_queue_delete(queue)          vQueueDelete(queue)

/**
 * @brief 发送消息（从任务上下文调用）
 * @param queue 消息队列句柄
 * @param msg 消息指针
 * @param timeout_ms 超时时间（毫秒），0表示立即返回，portMAX_DELAY表示永久等待
 * @return 0=成功，-1=失败（队列满或超时）
 * @note 此函数只能从任务上下文调用，不能在中断中使用
 */
int32_t my_msg_send(my_msg_queue_t queue, const my_msg_t *msg, uint32_t timeout_ms);

/**
 * @brief 发送消息（从中断上下文调用）
 * @param queue 消息队列句柄
 * @param msg 消息指针
 * @param higher_priority_task_woken 是否需要触发任务切换
 * @return 0=成功，-1=失败（队列满）
 * @note 此函数只能从中断上下文调用
 */
int32_t my_msg_send_from_isr(my_msg_queue_t queue, const my_msg_t *msg,
                              my_base_type_t *higher_priority_task_woken);

/**
 * @brief 接收消息（从任务上下文调用）
 * @param queue 消息队列句柄
 * @param msg 消息存储指针
 * @param timeout_ms 超时时间（毫秒），0表示立即返回，portMAX_DELAY表示永久等待
 * @return 0=成功，-1=失败（队列空或超时）
 * @note 此函数只能从任务上下文调用，不能在中断中使用
 */
int32_t my_msg_recv(my_msg_queue_t queue, my_msg_t *msg, uint32_t timeout_ms);

/**
 * @brief 接收消息（从中断上下文调用）
 * @param queue 消息队列句柄
 * @param msg 消息存储指针
 * @param higher_priority_task_woken 是否需要触发任务切换
 * @return 0=成功，-1=失败（队列空）
 * @note 此函数只能从中断上下文调用
 */
int32_t my_msg_recv_from_isr(my_msg_queue_t queue, my_msg_t *msg,
                              my_base_type_t *higher_priority_task_woken);

/**
 * @brief 查询消息队列中的消息数量
 * @param queue 消息队列句柄
 * @return 消息数量，0表示队列为空
 */
uint32_t my_msg_queue_get_count(my_msg_queue_t queue);

/**
 * @brief 清空消息队列
 * @param queue 消息队列句柄
 * @return 0=成功，-1=失败
 */
int32_t my_msg_queue_reset(my_msg_queue_t queue);

/*********************************************************************
 * 定时器管理接口
 *********************************************************************/

/**
 * @brief 创建定时器
 * @param timer_id 定时器ID
 * @param callback 定时器回调函数
 * @param period_ms 定时器周期（毫秒），0表示单次定时器
 * @return 0=成功，-1=失败（ID无效或创建失败）
 * @note 定时器创建后处于停止状态，需调用my_timer_start启动
 * @example
 * @code
 * // 每个定时器使用独立的回调函数
 * void one_minute_timer_callback(my_timer_handle_t timer_handle)
 * {
 *     // 1分钟定时处理（不需要判断timer_id）
 * }
 *
 * void test_timer_callback(my_timer_handle_t timer_handle)
 * {
 *     // 测试定时处理
 * }
 *
 * // 在初始化时创建
 * my_timer_create(MY_TIMER_ID_ONE_MINUTE, one_minute_timer_callback, 60000);
 * my_timer_create(MY_TIMER_ID_TEST, test_timer_callback, 1000);
 *
 * // 需要时启动
 * my_timer_start(MY_TIMER_ID_ONE_MINUTE, 0);
 * my_timer_start(MY_TIMER_ID_TEST, 0);
 * @endcode
 */
int32_t my_timer_create(my_timer_id_e timer_id, my_timer_callback_t callback,
                        uint32_t period_ms);

/**
 * @brief 删除定时器
 * @param timer_id 定时器ID
 * @return 0=成功，-1=失败（ID无效或未创建）
 * @note 删除后如需使用需重新创建
 * @warning 此函数不能在中断中调用！FreeRTOS不提供xTimerDeleteFromISR，
 *          因为删除操作需要释放内存，在中断中不安全。
 *          如需在中断中删除定时器，请设置标志位，在任务中检查并执行删除。
 * @example
 * @code
 * // 错误用法（在中断中调用）：
 * void USART_IRQHandler(void)
 * {
 *     my_timer_delete(MY_TIMER_ID_TEST);  // 危险！错误用法
 * }
 *
 * // 正确用法（使用标志位）：
 * volatile bool delete_timer_flag = false;
 *
 * void USART_IRQHandler(void)
 * {
 *     delete_timer_flag = true;  // 设置标志，正确用法
 * }
 *
 * void app_task(void)
 * {
 *     if (delete_timer_flag)
 *     {
 *         my_timer_delete(MY_TIMER_ID_TEST);  // 在任务中删除，正确用法
 *         delete_timer_flag = false;
 *     }
 * }
 * @endcode
 */
int32_t my_timer_delete(my_timer_id_e timer_id);

/**
 * @brief 查询定时器是否运行中
 * @param timer_id 定时器ID
 * @return true=运行中，false=已停止或未创建
 */
bool my_timer_is_running(my_timer_id_e timer_id);

/**
 * @brief 启动定时器
 * @param timer_id 定时器ID
 * @param timeout_ms 超时时间（毫秒），仅对单次定时器有效
 * @return 0=成功，-1=失败（ID无效或未创建）
 * @note 周期定时器会按创建时指定的周期重复触发
 */
int32_t my_timer_start(my_timer_id_e timer_id, uint32_t timeout_ms);

/**
 * @brief 在中断中启动定时器
 * @param timer_id 定时器ID
 * @return 0=成功，-1=失败（ID无效或未创建）
 * @note 仅用于中断上下文，不能阻塞
 */
int32_t my_timer_start_from_isr(my_timer_id_e timer_id);

/**
 * @brief 停止定时器
 * @param timer_id 定时器ID
 * @return 0=成功，-1=失败（ID无效或未创建）
 */
int32_t my_timer_stop(my_timer_id_e timer_id);

/**
 * @brief 在中断中停止定时器
 * @param timer_id 定时器ID
 * @return 0=成功，-1=失败（ID无效或未创建）
 * @note 仅用于中断上下文，不能阻塞
 */
int32_t my_timer_stop_from_isr(my_timer_id_e timer_id);

/**
 * @brief 重置定时器计数
 * @param timer_id 定时器ID
 * @return 0=成功，-1=失败（ID无效或未创建）
 * @note 重新开始计数
 */
int32_t my_timer_reset(my_timer_id_e timer_id);

/**
 * @brief 在中断中重置定时器
 * @param timer_id 定时器ID
 * @return 0=成功，-1=失败（ID无效或未创建）
 * @note 仅用于中断上下文，不能阻塞
 */
int32_t my_timer_reset_from_isr(my_timer_id_e timer_id);

/**
 * @brief 修改定时器周期
 * @param timer_id 定时器ID
 * @param new_period_ms 新周期（毫秒）
 * @return 0=成功，-1=失败（ID无效或未创建）
 * @note 在任务上下文中调用
 */
int32_t my_timer_change(my_timer_id_e timer_id, uint32_t new_period_ms);

/**
 * @brief 在中断中修改定时器周期
 * @param timer_id 定时器ID
 * @param new_period_ms 新周期（毫秒）
 * @return 0=成功，-1=失败（ID无效或未创建）
 * @note 仅用于中断上下文，不能阻塞
 */
int32_t my_timer_change_from_isr(my_timer_id_e timer_id, uint32_t new_period_ms);

/*********************************************************************
 * 系统级通用接口
 *********************************************************************/

/**
 * @brief 错误处理函数（进入死循环）
 * @note 发生严重错误时调用，系统将停止运行
 */
void my_error_handler(void);

/**
 * @brief 系统复位
 * @param delay_ms 延时时间（毫秒），确保日志输出，0表示不延时
 * @note 延时后执行系统复位
 */
void my_system_reset(uint32_t delay_ms);

#endif /* __MY_OS_H__ */
