/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       rtc_driver.c
**文件描述：       RTC驱动模块实现文件
**当前版本：       V1.2
**作    者：       伍玉蛟 (wuyujiao@jimiiot.com)
**完成日期：       2026.06.30
*********************************************************************
** 功能描述：       1. 实现GD32F505 RTC驱动全部功能
**                 2. 自动处理备份域解锁、硬件时序封装
**                 3. 支持Unix时间戳读写（日历转换由应用层使用C标准库time.h实现）
**                 4. 支持秒中断、闹钟中断、溢出中断回调
**                 5. 支持功能裁剪（宏开关控制）
**                 6. 驱动层仅提供核心API，FreeRTOS管理由应用层负责
*********************************************************************/

#include "rtc_driver.h"
#include "gd32f50x_rtc.h"
#include "gd32f50x_rcu.h"
#include "gd32f50x_pmu.h"
#include "gd32f50x_bkp.h"
#include "gd32f50x_misc.h"
#include <string.h>

/*********************************************************************
 * 内部数据结构定义
 *********************************************************************/

/*********************************************************************
 * 内部全局变量
 *********************************************************************/

/** 回调函数存储 */
static void (*s_second_callback)(void) = NULL;
static void (*s_alarm_callback)(void) = NULL;
static void (*s_overflow_callback)(void) = NULL;

/*********************************************************************
 * 内部辅助函数声明
 *********************************************************************/

static void _drv_bkp_access_prepare(void);
static int _drv_irq_config(const drv_rtc_config_t *config);

/*********************************************************************
 * 内部函数实现
 *********************************************************************/

/*********************************************************************
 * @brief   备份域访问前置准备（内部函数，所有API自动调用）
 * @param   无
 * @return  无
 * @note    封装 PMU/BKP 时钟开启、备份域解锁操作
 * @note    所有公开 API 内部第一行调用此函数，确保应用层零硬件操作
 *********************************************************************/
static void _drv_bkp_access_prepare(void)
{
    rcu_periph_clock_enable(RCU_PMU);
    rcu_periph_clock_enable(RCU_BKPI);
    pmu_unlock();
    pmu_backup_write_enable();
}

/*********************************************************************
 * @brief   注册回调、配置RTC中断并启用NVIC（内部函数，init热/冷启动共用）
 * @param   config  RTC配置结构体指针
 * @return  0表示成功，负数表示失败
 * @note    存储回调函数指针，合并中断使能标志后单次写入INTEN寄存器
 *          仅需一次LWOFF等待，最后配置NVIC启用RTC中断
 *********************************************************************/
static int _drv_irq_config(const drv_rtc_config_t *config)
{
    uint32_t interrupt = 0;

    /* 注册回调函数 */
    s_second_callback = config->second_callback;
    s_alarm_callback = config->alarm_callback;
    s_overflow_callback = config->overflow_callback;

    /* 配置中断使能 */
    if (config->enable_second_int)
    {
        interrupt |= RTC_INT_SECOND;
    }

    if (config->enable_alarm_int)
    {
        interrupt |= RTC_INT_ALARM;
    }

    if (config->enable_overflow_int)
    {
        interrupt |= RTC_INT_OVERFLOW;
    }

    rtc_interrupt_enable(interrupt);
    if (ERROR == rtc_lwoff_wait())
    {
        DRV_RTC_LOGE("Interrupt enable timeout");
        return DRV_RTC_ERR_TIMEOUT;
    }

    /* 配置NVIC中断优先级 */
    nvic_irq_enable(RTC_IRQn, 1, 0);

    return DRV_RTC_ERR_OK;
}

/*********************************************************************
 * 公开API实现
 *********************************************************************/

/*********************************************************************
 * @brief   初始化RTC驱动
 * @param   config  RTC配置结构体指针（应用层传入）
 * @return  0表示成功，负数表示失败（错误码见DRV_RTC_ERR_*定义）
 * @note    应用层需确保配置参数合法，冷启动时自动配置LXTAL，热启动时仅同步寄存器
 * @note    内部自动处理备份域解锁，应用层无需手动操作
 *********************************************************************/
int drv_rtc_init(const drv_rtc_config_t *config)
{
    int ret = DRV_RTC_ERR_OK;

    /* 参数检查 */
    if (config == NULL)
    {
        DRV_RTC_LOGE("Invalid config parameter");
        return DRV_RTC_ERR_INVALID_PARAM;
    }

    /* 备份域访问准备 */
    _drv_bkp_access_prepare();

    /* 检查是否已初始化（热启动） */
    if (drv_rtc_is_initialized())
    {
        DRV_RTC_LOGI("RTC hot start, sync registers only");

        /* 仅同步寄存器 */
        rtc_register_sync_wait();
        if (ERROR == rtc_lwoff_wait())
        {
            DRV_RTC_LOGE("LWOFF wait timeout");
            return DRV_RTC_ERR_TIMEOUT;
        }

        /* 热启动仍需注册回调、使能中断、配置NVIC */
        ret = _drv_irq_config(config);
        if (ret != DRV_RTC_ERR_OK)
        {
            return ret;
        }

        DRV_RTC_LOGI("RTC hot start complete");
        return DRV_RTC_ERR_OK;
    }

    /* 冷启动：完整配置 */
    DRV_RTC_LOGI("RTC cold start, full configuration");

    /* 配置时钟源 */
    if (config->clock_src == DRV_RTC_CLK_LXTAL)
    {
        rcu_osci_on(RCU_LXTAL);
        if (ERROR == rcu_osci_stab_wait(RCU_LXTAL))
        {
            DRV_RTC_LOGE("LXTAL start timeout");
            return DRV_RTC_ERR_TIMEOUT;
        }
        rcu_rtc_clock_config(RCU_RTCSRC_LXTAL);
    }
    else
    {
        DRV_RTC_LOGE("Unsupported clock source");
        return DRV_RTC_ERR_INVALID_PARAM;
    }

    /* 使能RTC时钟 */
    rcu_periph_clock_enable(RCU_RTC);

    /* 等待寄存器同步 */
    rtc_register_sync_wait();
    if (ERROR == rtc_lwoff_wait())
    {
        DRV_RTC_LOGE("LWOFF wait timeout");
        return DRV_RTC_ERR_TIMEOUT;
    }

    /* 配置预分频器（rtc_prescaler_set内部已含配置模式进出） */
    rtc_prescaler_set(config->prescaler);
    if (ERROR == rtc_lwoff_wait())
    {
        DRV_RTC_LOGE("Prescaler set timeout");
        return DRV_RTC_ERR_TIMEOUT;
    }

    /* 存储备份域标记 */
    bkp_write_data(DRV_RTC_BKP_MARK_REG, DRV_RTC_BKP_MARK_VAL);

    /* 注册回调、配置中断并启用NVIC */
    ret = _drv_irq_config(config);
    if (ret != DRV_RTC_ERR_OK)
    {
        return ret;
    }

    DRV_RTC_LOGI("RTC init success");
    return DRV_RTC_ERR_OK;
}

/*********************************************************************
 * @brief   反初始化RTC驱动
 * @param   无
 * @return  0表示成功，负数表示失败
 * @note    禁用全部中断，清除回调函数，保留备份域时间不丢失
 *********************************************************************/
int drv_rtc_deinit(void)
{
    _drv_bkp_access_prepare();

    /* 禁用全部中断 */
    rtc_interrupt_disable(RTC_INT_SECOND | RTC_INT_ALARM | RTC_INT_OVERFLOW);
    if (ERROR == rtc_lwoff_wait())
    {
        DRV_RTC_LOGE("Deinit interrupt disable timeout");
        return DRV_RTC_ERR_TIMEOUT;
    }

    /* 清除回调函数 */
    s_second_callback = NULL;
    s_alarm_callback = NULL;
    s_overflow_callback = NULL;

    /* 清除备份域标记，确保重新init走冷启动路径恢复中断配置 */
    bkp_write_data(DRV_RTC_BKP_MARK_REG, 0x0000U);

    /* 禁用 NVIC 中断，防止悬空的中断向量 */
    nvic_irq_disable(RTC_IRQn);

    DRV_RTC_LOGI("RTC deinit success");
    return DRV_RTC_ERR_OK;
}

/*********************************************************************
 * @brief   设置RTC时间（Unix时间戳）
 * @param   timestamp   Unix时间戳（秒，从1970-01-01 00:00:00开始）
 * @return  0表示成功，负数表示失败
 * @note    内部等待LWOFF完成后写入
 * @note    时间戳上限为 0xFFFFFFFF（2106-02-07 06:28:15），超出后硬件自动回绕到0
 *********************************************************************/
int drv_rtc_set_time(uint32_t timestamp)
{
    /* 确保BKP时钟已开启，保证读取初始化标记可靠 */
    rcu_periph_clock_enable(RCU_BKPI);

    /* 检查初始化状态 */
    if (!drv_rtc_is_initialized())
    {
        DRV_RTC_LOGE("RTC not initialized");
        return DRV_RTC_ERR_NOT_READY;
    }

    /* 等待上次写操作完成 */
    if (ERROR == rtc_lwoff_wait())
    {
        DRV_RTC_LOGE("LWOFF wait timeout");
        return DRV_RTC_ERR_TIMEOUT;
    }

    /* 设置计数器（rtc_counter_set内部已含配置模式进出） */
    rtc_counter_set(timestamp);

    if (ERROR == rtc_lwoff_wait())
    {
        DRV_RTC_LOGE("Counter set timeout");
        return DRV_RTC_ERR_TIMEOUT;
    }

    DRV_RTC_LOGD("RTC time set: %u", timestamp);
    return DRV_RTC_ERR_OK;
}

/*********************************************************************
 * @brief   获取RTC时间（Unix时间戳）
 * @param   timestamp   输出参数，存储时间戳
 * @return  0表示成功，负数表示失败
 * @note    读取前等待RSYNF同步标志，确保跨时钟域数据一致性
 *********************************************************************/
int drv_rtc_get_time(uint32_t *timestamp)
{
    if (timestamp == NULL) {
        return DRV_RTC_ERR_INVALID_PARAM;
    }

    /* 等待RSYNF同步（读操作仅需RSYNF，无需LWOFF） */
    rtc_register_sync_wait();

    *timestamp = rtc_counter_get();

    DRV_RTC_LOGD("RTC time get: %u", *timestamp);
    return DRV_RTC_ERR_OK;
}

/*********************************************************************
 * @brief   设置RTC闹钟
 * @param   alarm_timestamp 闹钟时间（Unix时间戳）
 * @return  0表示成功，负数表示失败
 * @note    闹钟值写入RTC_ALRMH/L寄存器，计数器值等于闹钟值时触发中断
 * @note    硬件单次触发特性：触发后需重新设置下一次闹钟时间
 *********************************************************************/
int drv_rtc_set_alarm(uint32_t alarm_timestamp)
{
    /* 确保BKP时钟已开启，保证读取初始化标记可靠 */
    rcu_periph_clock_enable(RCU_BKPI);

    /* 检查初始化状态 */
    if (!drv_rtc_is_initialized())
    {
        DRV_RTC_LOGE("RTC not initialized");
        return DRV_RTC_ERR_NOT_READY;
    }

    /* 等待上次写操作完成 */
    if (ERROR == rtc_lwoff_wait())
    {
        DRV_RTC_LOGE("LWOFF wait timeout");
        return DRV_RTC_ERR_TIMEOUT;
    }

    /* 配置闹钟（rtc_alarm_config内部已含配置模式进出） */
    rtc_alarm_config(alarm_timestamp);

    if (ERROR == rtc_lwoff_wait())
    {
        DRV_RTC_LOGE("Alarm config timeout");
        return DRV_RTC_ERR_TIMEOUT;
    }

    DRV_RTC_LOGD("RTC alarm set: %u", alarm_timestamp);
    return DRV_RTC_ERR_OK;
}

/*********************************************************************
 * @brief   禁用RTC闹钟
 * @param   无
 * @return  0表示成功，负数表示失败
 * @note    清除闹钟寄存器并禁用中断，禁用后需重新调用drv_rtc_set_alarm才能再次触发
 *********************************************************************/
int drv_rtc_disable_alarm(void)
{
    /* 等待上次写操作完成 */
    if (ERROR == rtc_lwoff_wait())
    {
        DRV_RTC_LOGE("LWOFF wait timeout");
        return DRV_RTC_ERR_TIMEOUT;
    }

    rtc_interrupt_disable(RTC_INT_ALARM);

    if (ERROR == rtc_lwoff_wait())
    {
        DRV_RTC_LOGE("Alarm disable timeout");
        return DRV_RTC_ERR_TIMEOUT;
    }

    DRV_RTC_LOGI("RTC alarm disabled");
    return DRV_RTC_ERR_OK;
}

/*********************************************************************
 * @brief   使能RTC中断
 * @param   int_type    中断类型
 * @return  0表示成功，负数表示失败
 *********************************************************************/
int drv_rtc_interrupt_enable(drv_rtc_int_type_e int_type)
{
    /* 等待上次写操作完成 */
    if (ERROR == rtc_lwoff_wait())
    {
        DRV_RTC_LOGE("LWOFF wait timeout");
        return DRV_RTC_ERR_TIMEOUT;
    }

    switch (int_type)
    {
        case DRV_RTC_INT_SECOND:
            rtc_interrupt_enable(RTC_INT_SECOND);
            break;

        case DRV_RTC_INT_ALARM:
            rtc_interrupt_enable(RTC_INT_ALARM);
            break;

        case DRV_RTC_INT_OVERFLOW:
            rtc_interrupt_enable(RTC_INT_OVERFLOW);
            break;

        default:
            return DRV_RTC_ERR_INVALID_PARAM;
    }

    if (ERROR == rtc_lwoff_wait())
    {
        DRV_RTC_LOGE("Interrupt enable timeout");
        return DRV_RTC_ERR_TIMEOUT;
    }

    return DRV_RTC_ERR_OK;
}

/*********************************************************************
 * @brief   禁用RTC中断
 * @param   int_type    中断类型
 * @return  0表示成功，负数表示失败
 *********************************************************************/
int drv_rtc_interrupt_disable(drv_rtc_int_type_e int_type)
{
    /* 等待上次写操作完成 */
    if (ERROR == rtc_lwoff_wait())
    {
        DRV_RTC_LOGE("LWOFF wait timeout");
        return DRV_RTC_ERR_TIMEOUT;
    }

    switch (int_type)
    {
        case DRV_RTC_INT_SECOND:
            rtc_interrupt_disable(RTC_INT_SECOND);
            break;

        case DRV_RTC_INT_ALARM:
            rtc_interrupt_disable(RTC_INT_ALARM);
            break;

        case DRV_RTC_INT_OVERFLOW:
            rtc_interrupt_disable(RTC_INT_OVERFLOW);
            break;

        default:
            return DRV_RTC_ERR_INVALID_PARAM;
    }

    if (ERROR == rtc_lwoff_wait())
    {
        DRV_RTC_LOGE("Interrupt disable timeout");
        return DRV_RTC_ERR_TIMEOUT;
    }

    return DRV_RTC_ERR_OK;
}

/*********************************************************************
 * @brief   检查RTC是否已初始化（通过备份域标记判断）
 * @param   无
 * @return  true表示已初始化，false表示未初始化
 *********************************************************************/
bool drv_rtc_is_initialized(void)
{
    return (bkp_read_data(DRV_RTC_BKP_MARK_REG) == DRV_RTC_BKP_MARK_VAL);
}

/*********************************************************************
 * @brief   RTC全局中断处理函数
 * @param   无
 * @return  无
 * @note    本函数由gd32f50x_it.c中的RTC_IRQHandler调用，应用层不应直接调用
 * @note    内部清除中断标志并调用注册的回调函数
 *********************************************************************/
void drv_rtc_irq_handler(void)
{
    /* 秒中断 */
    if (rtc_flag_get(RTC_FLAG_SECOND) != RESET)
    {
        rtc_flag_clear(RTC_FLAG_SECOND);
        if (s_second_callback != NULL)
        {
            s_second_callback();
        }
    }

    /* 闹钟中断 */
    if (rtc_flag_get(RTC_FLAG_ALARM) != RESET)
    {
        rtc_flag_clear(RTC_FLAG_ALARM);
        if (s_alarm_callback != NULL)
        {
            s_alarm_callback();
        }
    }

    /* 溢出中断 */
    if (rtc_flag_get(RTC_FLAG_OVERFLOW) != RESET)
    {
        rtc_flag_clear(RTC_FLAG_OVERFLOW);
        if (s_overflow_callback != NULL)
        {
            s_overflow_callback();
        }
    }
}
