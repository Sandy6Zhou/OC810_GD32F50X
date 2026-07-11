/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       rtc_driver.h
**文件描述：       RTC驱动模块接口定义
**当前版本：       V1.2
**作    者：       伍玉蛟 (wuyujiao@jimiiot.com)
**完成日期：       2026.06.30
*********************************************************************
** 功能描述：       1. 提供GD32F505 RTC驱动接口
**                 2. 支持Unix时间戳读写（日历转换由应用层使用C标准库time.h实现）
**                 3. 支持秒中断、闹钟中断、溢出中断回调
**                 4. 自动处理备份域解锁、硬件时序
**                 5. 支持功能裁剪（宏开关控制）
**                 6. 驱动层仅提供核心API，FreeRTOS管理由应用层负责
*********************************************************************/

#ifndef __DRV_RTC_H__
#define __DRV_RTC_H__

#include "gd32f50x.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * 日志宏定义（便于移植和独立控制）
 *********************************************************************/

/* 日志开关（1=开启，0=关闭） */
#define DRV_RTC_LOG_ENABLE      (1U)

/* 日志级别定义 */
#define DRV_RTC_LOG_LEVEL_ERROR        (0U)    /**< 错误日志 */
#define DRV_RTC_LOG_LEVEL_WARN         (1U)    /**< 警告日志 */
#define DRV_RTC_LOG_LEVEL_INFO         (2U)    /**< 信息日志 */
#define DRV_RTC_LOG_LEVEL_DEBUG        (3U)    /**< 调试日志 */

/* 当前日志级别（可通过修改此值控制日志输出详细程度） */
#define DRV_RTC_LOG_CURRENT_LEVEL      (DRV_RTC_LOG_LEVEL_INFO)

/* 日志输出宏（可根据项目实际情况修改底层实现） */
#if DRV_RTC_LOG_ENABLE == 1U

/* 根据项目实际使用的日志系统修改此处 */
#include "my_log.h"

#define DRV_RTC_LOGE(fmt, ...)    MY_LOG_E(fmt, ##__VA_ARGS__)
#define DRV_RTC_LOGW(fmt, ...)    MY_LOG_W(fmt, ##__VA_ARGS__)
#define DRV_RTC_LOGI(fmt, ...)    MY_LOG_I(fmt, ##__VA_ARGS__)
#define DRV_RTC_LOGD(fmt, ...)    do { \
                                        if (DRV_RTC_LOG_CURRENT_LEVEL >= DRV_RTC_LOG_LEVEL_DEBUG) \
                                        { \
                                            MY_LOG_D(fmt, ##__VA_ARGS__); \
                                        } \
                                    } while(0)

#else

#define DRV_RTC_LOGE(fmt, ...)
#define DRV_RTC_LOGW(fmt, ...)
#define DRV_RTC_LOGI(fmt, ...)
#define DRV_RTC_LOGD(fmt, ...)

#endif /* DRV_RTC_LOG_ENABLE */

/*********************************************************************
 * 断言宏定义（开发阶段捕获严重错误）
 *********************************************************************/

/* 断言开关（1=启用，0=禁用） */
#ifndef DRV_RTC_ASSERT_ENABLE
#define DRV_RTC_ASSERT_ENABLE     (0U)
#endif

#if DRV_RTC_ASSERT_ENABLE == 1U
    /* 使用FreeRTOS的configASSERT */
    #define DRV_RTC_ASSERT(expr)    configASSERT(expr)
#else
    #define DRV_RTC_ASSERT(expr)    ((void)0)
#endif

/*********************************************************************
 * 宏定义
 *********************************************************************/

/**
 * @brief RTC驱动错误码
 */
#define DRV_RTC_ERR_OK             (0)     /**< 成功 */
#define DRV_RTC_ERR_FAILED         (-1)    /**< 失败 */
#define DRV_RTC_ERR_TIMEOUT        (-2)    /**< 超时 */
#define DRV_RTC_ERR_INVALID_PARAM  (-3)    /**< 参数错误 */
#define DRV_RTC_ERR_NOT_READY      (-4)    /**< 未就绪 */

/*********************************************************************
 * 芯片移植宏（更换MCU仅修改此处）
 *********************************************************************/

/** 备份域标记寄存器 */
#define DRV_RTC_BKP_MARK_REG        BKP_DATA_0

/** 备份域标记值（魔数） */
#define DRV_RTC_BKP_MARK_VAL        (0xA5A5U)

/** RSYNF同步超时计数（已委托GD库rtc_register_sync_wait处理） */

/** 1Hz预分频器值（LXTAL 32768Hz - 1） */
#define DRV_RTC_1HZ_PSC             (32767U)

/*********************************************************************
 * 数据结构定义
 *********************************************************************/

/**
 * @brief RTC时钟源枚举
 */
typedef enum {
    DRV_RTC_CLK_LXTAL = 0,          /**< 外部32.768KHz晶振（推荐） */
    DRV_RTC_CLK_IRC40K,             /**< 内部40KHz RC（降级备用） */
    DRV_RTC_CLK_HXTAL_DIV128,       /**< 高速晶振除以128 */
    DRV_RTC_CLK_AHB_DIV10           /**< AHB时钟除以10 */
} drv_rtc_clock_src_e;

/**
 * @brief RTC中断类型枚举
 */
typedef enum {
    DRV_RTC_INT_SECOND = 0,         /**< 秒中断 */
    DRV_RTC_INT_ALARM,              /**< 闹钟中断 */
    DRV_RTC_INT_OVERFLOW,           /**< 溢出中断 */
    DRV_RTC_INT_MAX                 /**< 中断类型数量上限 */
} drv_rtc_int_type_e;

/**
 * @brief RTC配置结构体（应用层传入，驱动层仅读取）
 */
typedef struct {
    drv_rtc_clock_src_e clock_src;          /**< 时钟源选择 */
    uint32_t prescaler;                     /**< 预分频器值（LXTAL: 32767） */
    bool enable_second_int;                 /**< 使能秒中断 */
    bool enable_alarm_int;                  /**< 使能闹钟中断 */
    bool enable_overflow_int;               /**< 使能溢出中断 */

    /* 回调函数（可选，按需配置，未配置则不触发回调） */
    void (*second_callback)(void);          /**< 秒中断回调（中断上下文中调用，必须快速执行） */
    void (*alarm_callback)(void);           /**< 闹钟中断回调（中断上下文中调用，必须快速执行） */
    void (*overflow_callback)(void);        /**< 溢出中断回调（中断上下文中调用，必须快速执行） */
} drv_rtc_config_t;

/*********************************************************************
 * 接口函数声明
 *********************************************************************/

/*********************************************************************
 * @brief   初始化RTC驱动
 * @param   config  RTC配置结构体指针（应用层传入）
 * @return  0表示成功，负数表示失败（错误码见DRV_RTC_ERR_*定义）
 * @note    应用层需确保配置参数合法，冷启动时自动配置LXTAL，热启动时仅同步寄存器
 * @note    内部自动处理备份域解锁，应用层无需手动操作
 *********************************************************************/
int drv_rtc_init(const drv_rtc_config_t *config);

/*********************************************************************
 * @brief   反初始化RTC驱动
 * @param   无
 * @return  0表示成功，负数表示失败
 * @note    禁用全部中断，清除回调函数，保留备份域时间不丢失
 *********************************************************************/
int drv_rtc_deinit(void);

/*********************************************************************
 * @brief   设置RTC时间（Unix时间戳）
 * @param   timestamp   Unix时间戳（秒，从1970-01-01 00:00:00开始）
 * @return  0表示成功，负数表示失败
 * @note    内部等待LWOFF完成后写入
 * @note    时间戳上限为 0xFFFFFFFF（2106-02-07 06:28:15），超出后硬件自动回绕到0
 *********************************************************************/
int drv_rtc_set_time(uint32_t timestamp);

/*********************************************************************
 * @brief   获取RTC时间（Unix时间戳）
 * @param   timestamp   输出参数，存储时间戳
 * @return  0表示成功，负数表示失败
 * @note    读取前等待RSYNF同步标志，确保数据一致性
 *********************************************************************/
int drv_rtc_get_time(uint32_t *timestamp);

/*********************************************************************
 * @brief   设置RTC闹钟
 * @param   alarm_timestamp 闹钟时间（Unix时间戳）
 * @return  0表示成功，负数表示失败
 * @note    闹钟值写入RTC_ALRMH/L寄存器，计数器值等于闹钟值时触发中断
 * @note    硬件单次触发特性：触发后需重新设置下一次闹钟时间
 *********************************************************************/
int drv_rtc_set_alarm(uint32_t alarm_timestamp);

/*********************************************************************
 * @brief   禁用RTC闹钟
 * @param   无
 * @return  0表示成功，负数表示失败
 * @note    清除闹钟寄存器并禁用中断，禁用后需重新调用drv_rtc_set_alarm才能再次触发
 *********************************************************************/
int drv_rtc_disable_alarm(void);

/*********************************************************************
 * @brief   使能RTC中断
 * @param   int_type    中断类型
 * @return  0表示成功，负数表示失败
 *********************************************************************/
int drv_rtc_interrupt_enable(drv_rtc_int_type_e int_type);

/*********************************************************************
 * @brief   禁用RTC中断
 * @param   int_type    中断类型
 * @return  0表示成功，负数表示失败
 *********************************************************************/
int drv_rtc_interrupt_disable(drv_rtc_int_type_e int_type);

/*********************************************************************
 * @brief   检查RTC是否已初始化（通过备份域标记判断）
 * @param   无
 * @return  true表示已初始化，false表示未初始化
 *********************************************************************/
bool drv_rtc_is_initialized(void);

/*********************************************************************
 * @brief   RTC全局中断处理函数
 * @param   无
 * @return  无
 * @note    本函数由gd32f50x_it.c中的RTC_IRQHandler调用，应用层不应直接调用
 * @note    内部清除中断标志并调用注册的回调函数
 *********************************************************************/
void drv_rtc_irq_handler(void);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_RTC_H__ */
