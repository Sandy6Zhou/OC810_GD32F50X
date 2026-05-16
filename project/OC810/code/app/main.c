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
    while (1)
    {
    }
}

/*********************************************************************
 * @brief   系统初始化任务
 * @param   pvParameters 任务参数（未使用）
 * @return  none
 * @note    初始化日志系统并打印系统信息，完成后自删除
 *********************************************************************/
void init_task(void *pvParameters)
{
    /* 初始化日志系统 */
    my_log_init();

    /* 打印系统信息 */
    MY_LOG_I("========================================");
    MY_LOG_I("GD32F505VGT7 FreeRTOS Base Project");
    MY_LOG_I("System Core Clock: %d Hz", SystemCoreClock);
    MY_LOG_I("FreeRTOS Heap Size: %d bytes", configTOTAL_HEAP_SIZE);
    MY_LOG_I("FreeRTOS Version: %s", tskKERNEL_VERSION_NUMBER);
    MY_LOG_I("========================================");
    MY_LOG_I("FreeRTOS scheduler started successfully!");

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
    /* 栈溢出是严重错误，关闭中断并进入死循环 */
    taskDISABLE_INTERRUPTS();

    /* 调试建议：
     * 1. 在此处设置断点
     * 2. 查看pcTaskName确定哪个任务溢出
     * 3. 查看xTask获取任务句柄
     * 4. 增加该任务的栈大小
     */

    for (;;)
    {
        /* 死循环 - 需要调试时在此处设断点 */
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
    /* 内存分配失败是严重错误，关闭中断并进入死循环 */
    taskDISABLE_INTERRUPTS();

    /* 调试建议：
     * 1. 在此处设置断点
     * 2. 调用xPortGetFreeHeapSize()查看剩余堆大小
     * 3. 检查是否有内存泄漏
     * 4. 考虑增加configTOTAL_HEAP_SIZE
     */

    for (;;)
    {
        /* 死循环 - 需要调试时在此处设断点 */
    }
}
