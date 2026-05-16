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
#include "timer_driver.h"

/* 任务优先级定义 */
#define INIT_TASK_PRIO          (tskIDLE_PRIORITY + 1)
#define TIMER_TEST_TASK_PRIO    (tskIDLE_PRIORITY + 2)

/* 任务函数声明 */
void init_task(void *pvParameters);
void timer_test_task(void *pvParameters);

/* FreeRTOS钩子函数声明 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName);
void vApplicationMallocFailedHook(void);

/*********************************************************************
 * Timer 测试相关定义
 *********************************************************************/

/* 测试用 Timer 选择（使用 TIMER2，通用定时器，无引脚冲突） */
#define TEST_TIMER_ID           DRV_TIMER_2

/* 测试周期：1秒（假设系统时钟 280MHz，prescaler=27999，period=9999） */
#define TEST_PRESCALER          (28000U - 1U)   /* 280MHz / 28000 = 10kHz */
#define TEST_PERIOD             (10000U - 1U)   /* 10kHz / 10000 = 1Hz (1秒) */

/* 测试计数 */
static volatile uint32_t s_test_callback_count = 0;
static volatile uint32_t s_test_intf_flags = 0;

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

    /* 创建 Timer 测试任务 */
    xTaskCreate(timer_test_task, "TIMER_TEST", 512, NULL, TIMER_TEST_TASK_PRIO, NULL);

    /* 删除初始化任务 */
    vTaskDelete(NULL);
}

/*********************************************************************
 * @brief   Timer 中断回调函数
 * @param   intf_raw  INTF 寄存器原始值
 * @note    统计中断次数，记录中断标志
 *********************************************************************/
static void test_timer_callback(uint32_t intf_raw)
{
    s_test_callback_count++;
    s_test_intf_flags = intf_raw;
}

/*********************************************************************
 * @brief   Timer 驱动测试任务
 * @param   pvParameters 任务参数（未使用）
 * @return  none
 * @note    按顺序执行各项测试用例
 *********************************************************************/
void timer_test_task(void *pvParameters)
{
    int32_t ret;
    drv_timer_config_t config;
    drv_timer_capability_t cap;

    /* 等待系统稳定 */
    vTaskDelay(pdMS_TO_TICKS(100));

    MY_LOG_I("========== Timer Driver Test Start ==========");

    /*===========================================================
     * 测试 1: 错误处理 - 非法 Timer ID
     *===========================================================*/
    MY_LOG_I("[TEST 1] Invalid timer ID");
    ret = drv_timer_init(DRV_TIMER_MAX, NULL);
    MY_LOG_I(ret == DRV_TIMER_ERR_INVALID_ID ? "  PASS" : "  FAIL");

    /*===========================================================
     * 测试 2: 错误处理 - NULL config
     *===========================================================*/
    MY_LOG_I("[TEST 2] NULL config");
    ret = drv_timer_init(TEST_TIMER_ID, NULL);
    MY_LOG_I(ret == DRV_TIMER_ERR_INVALID_PARAM ? "  PASS" : "  FAIL");

    /*===========================================================
     * 测试 3: 未初始化操作
     *===========================================================*/
    MY_LOG_I("[TEST 3] Operation before init");
    ret = drv_timer_start(TEST_TIMER_ID);
    MY_LOG_I(ret == DRV_TIMER_ERR_NOT_INITIALIZED ? "  PASS" : "  FAIL");

    /*===========================================================
     * 测试 4: Timer 初始化
     *===========================================================*/
    MY_LOG_I("[TEST 4] Timer init");
    config.period = TEST_PERIOD;
    config.prescaler = TEST_PRESCALER;
    config.counter_mode = DRV_COUNTER_EDGE;
    config.auto_reload_shadow = true;
    config.repeat_enable = false;
    config.repetition_counter = 0;

    ret = drv_timer_init(TEST_TIMER_ID, &config);
    MY_LOG_I(ret == DRV_TIMER_ERR_OK ? "  PASS" : "  FAIL");
    vTaskDelay(pdMS_TO_TICKS(50));

    /*===========================================================
     * 测试 5: 重复初始化
     *===========================================================*/
    MY_LOG_I("[TEST 5] Double init");
    ret = drv_timer_init(TEST_TIMER_ID, &config);
    MY_LOG_I(ret == DRV_TIMER_ERR_BUSY ? "  PASS" : "  FAIL");
    vTaskDelay(pdMS_TO_TICKS(50));

    /*===========================================================
     * 测试 6: 能力查询
     *===========================================================*/
    MY_LOG_I("[TEST 6] Capability");
    ret = drv_timer_get_capability(TEST_TIMER_ID, &cap);
    MY_LOG_I(ret == DRV_TIMER_ERR_OK ? "  PASS" : "  FAIL");
    vTaskDelay(pdMS_TO_TICKS(50));

    /*===========================================================
     * 测试 7: 回调注册
     *===========================================================*/
    MY_LOG_I("[TEST 7] Callback register");
    ret = drv_timer_callback_register(TEST_TIMER_ID, test_timer_callback);
    MY_LOG_I(ret == DRV_TIMER_ERR_OK ? "  PASS" : "  FAIL");
    vTaskDelay(pdMS_TO_TICKS(50));

    /*===========================================================
     * 测试 8: 中断使能 + NVIC
     *===========================================================*/
    MY_LOG_I("[TEST 8] Int enable + NVIC");
    ret = drv_timer_int_enable(TEST_TIMER_ID, DRV_TIMER_INT_UPDATE);
    MY_LOG_I(ret == DRV_TIMER_ERR_OK ? "  PASS" : "  FAIL");
    nvic_irq_enable(TIMER2_IRQn, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(50));

    /*===========================================================
     * 测试 9: 启动 Timer
     *===========================================================*/
    MY_LOG_I("[TEST 9] Timer start");
    ret = drv_timer_start(TEST_TIMER_ID);
    MY_LOG_I(ret == DRV_TIMER_ERR_OK ? "  PASS" : "  FAIL");
    vTaskDelay(pdMS_TO_TICKS(50));

    /*===========================================================
     * 测试 10: 等待中断回调（5秒）
     *===========================================================*/
    MY_LOG_I("[TEST 10] Wait 5s for callback...");
    s_test_callback_count = 0;
    vTaskDelay(pdMS_TO_TICKS(5000));

    if (s_test_callback_count > 0)
    {
        MY_LOG_I("  PASS: count=%lu, INTF=0x%08lX",
                 s_test_callback_count, s_test_intf_flags);
    }
    else
    {
        MY_LOG_E("  FAIL: No callback");
    }

    /*===========================================================
     * 测试 11-17: 快速测试剩余功能
     *===========================================================*/
    MY_LOG_I("[TEST 11-17] Runtime config, stop, deinit...");

    drv_timer_set_period(TEST_TIMER_ID, 4999);
    drv_timer_set_prescaler(TEST_TIMER_ID, 13999);
    vTaskDelay(pdMS_TO_TICKS(1000));
    MY_LOG_I("  Callbacks: %lu", s_test_callback_count);

    uint32_t count_before = s_test_callback_count;
    drv_timer_stop(TEST_TIMER_ID);
    vTaskDelay(pdMS_TO_TICKS(1000));
    MY_LOG_I(s_test_callback_count == count_before ? "  PASS: Stop" : "  FAIL: Running");

    drv_timer_callback_unregister(TEST_TIMER_ID);
    drv_timer_int_disable(TEST_TIMER_ID, DRV_TIMER_INT_UPDATE);

    ret = drv_timer_deinit(TEST_TIMER_ID);
    MY_LOG_I(ret == DRV_TIMER_ERR_OK ? "  PASS: Deinit" : "  FAIL");

    ret = drv_timer_start(TEST_TIMER_ID);
    MY_LOG_I(ret == DRV_TIMER_ERR_NOT_INITIALIZED ? "  PASS: Post-deinit" : "  FAIL");

    MY_LOG_I("========== Timer Driver Test Complete ==========");

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
