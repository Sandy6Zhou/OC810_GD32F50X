/********************************************************************
**版权所有：         深圳市几米物联有限公司
**文件名称：        main.c
**文件描述：        GD32F505VGT7 FreeRTOS基础项目主文件
**当前版本:        V1.0
**作    者：        伍玉蛟 (wuyujiao@jimiiot.com)
**完成日期：        2026.04.16
*********************************************************************
** 功能描述：       1. 实现系统初始化，配置中断优先级
**                 2. 创建FreeRTOS初始化任务并启动调度器
**                 3. 实现栈溢出和内存分配失败钩子函数
**                 4. 提供干净的FreeRTOS基础框架，供业务开发使用
*********************************************************************/

#include "gd32f50x.h"
#include "gd32f503v_eval.h"

#include "FreeRTOS.h"
#include "task.h"

#include "my_log.h"
#include "my_version.h"
#include "my_main.h"

/* 任务优先级定义 */
#define INIT_TASK_PRIO          (tskIDLE_PRIORITY + 1)

/* 任务函数声明 */
void init_task(void *pvParameters);

/* FreeRTOS钩子函数声明 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName);
void vApplicationMallocFailedHook(void);

/*********************************************************************
 * @brief   主函数，系统入口
 * @param   none
 * @return  none
 * @note    配置中断优先级后创建初始化任务，启动FreeRTOS调度器
 *********************************************************************/
int main(void)
{
    /* 配置4位抢占优先级 */
    nvic_priority_group_set(NVIC_PRIGROUP_PRE4_SUB0);

    /* 创建初始化任务 */
    xTaskCreate(init_task, "INIT", configMINIMAL_STACK_SIZE, NULL, INIT_TASK_PRIO, NULL);

    /* 启动调度器 */
    vTaskStartScheduler();

    /* 正常运行时不会执行到这里 */
    while (1) { }
}

/*********************************************************************
 * @brief   系统初始化任务
 * @param   pvParameters 任务参数（未使用）
 * @return  none
 * @note    初始化日志系统，启动主任务模块后自删除
 *********************************************************************/
void init_task(void *pvParameters)
{
    (void)pvParameters;

    /* 初始化日志系统 */
    my_log_init();

    /* 打印系统信息 */
    MY_LOG_I("========================================");
    MY_LOG_I("GD32F505VGT7 FreeRTOS mDVR Project");
    MY_LOG_I("System Core Clock: %d Hz", SystemCoreClock);
    MY_LOG_I("FreeRTOS Heap Size: %d bytes", configTOTAL_HEAP_SIZE);
    MY_LOG_I("FreeRTOS Version: %s", tskKERNEL_VERSION_NUMBER);
    MY_LOG_I("app Version: %s", MY_SW_VERSION_INFO);
    MY_LOG_I("========================================");

    /* 启动主任务模块（内部会创建所有子任务） */
    if (my_main_init() != 0)
    {
        MY_LOG_E("Main module init failed! System halted.");

        /* 主任务初始化失败，停止系统 */
        while (1) { }
    }

    MY_LOG_I("System initialization complete, init_task deleted.");

    /* 删除初始化任务 */
    vTaskDelete(NULL);
}

/*********************************************************************
 * @brief   栈溢出钩子函数
 * @param   xTask 发生溢出的任务句柄
 * @param   pcTaskName 发生溢出的任务名称
 * @return  none
 * @note    当configCHECK_FOR_STACK_OVERFLOW设置为1或2时调用
 *          栈溢出是严重错误，此函数会关闭中断并进入死循环
 *          调试时在此处设置断点，查看pcTaskName确定溢出任务
 *********************************************************************/
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    volatile uint32_t i;
    volatile const char *s_fault_task = pcTaskName ? pcTaskName : "unknown";
    (void)xTask;

    /* 先输出日志（此时中断还未关闭，日志可以正常输出） */
    MY_LOG_E("Stack overflow in task: %s", s_fault_task);

    /* 等待日志输出完成（约17ms@120MHz，确保多条日志发送完毕） */
    for (i = 0; i < 500000; i++) { }

    /* 日志输出完成后关闭中断，进入死循环 */
    taskDISABLE_INTERRUPTS();

    while (1)
    {
        /* 死循环，等待看门狗溢出复位（若应用层已启动看门狗） */
    }
}

/*********************************************************************
 * @brief   内存分配失败钩子函数
 * @param   none
 * @return  none
 * @note    当configUSE_MALLOC_FAILED_HOOK设置为1时调用
 *          内存分配失败是严重错误，此函数会关闭中断并进入死循环
 *          调试时在此处设置断点，检查xPortGetFreeHeapSize()剩余堆大小
 *********************************************************************/
void vApplicationMallocFailedHook(void)
{
    volatile uint32_t i;
    volatile uint32_t s_free_heap = xPortGetFreeHeapSize();

    /* 先输出日志（此时中断还未关闭，日志可以正常输出） */
    MY_LOG_E("Malloc failed! Free heap: %d bytes", (int)s_free_heap);

    /* 等待日志输出完成（约17ms@120MHz，确保多条日志发送完毕） */
    for (i = 0; i < 500000; i++) { }

    /* 日志输出完成后关闭中断，进入死循环 */
    taskDISABLE_INTERRUPTS();

    while (1)
    {
        /* 死循环，等待看门狗溢出复位（若应用层已启动看门狗） */
    }
}
