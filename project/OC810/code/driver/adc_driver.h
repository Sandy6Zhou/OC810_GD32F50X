/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       adc_driver.h
**文件描述：       ADC驱动接口头文件
**当前版本：       V1.4
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.05.09
*********************************************************************
** 功能描述：       1. 封装GD32F505 ADC硬件操作
**                 2. 提供单通道/多通道ADC转换接口
**                 3. 支持DMA传输模式
**                 4. 支持模拟看门狗功能
*********************************************************************/

#ifndef __DRV_ADC_H__
#define __DRV_ADC_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "gd32f50x.h"
#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "semphr.h"

/* ==================== 日志配置 ==================== */

/* 日志开关（1=开启，0=关闭） */
#define DRV_ADC_LOG_ENABLE      (1U)

/* 日志级别定义 */
#define DRV_ADC_LOG_LEVEL_ERROR        (0U)    /**< 错误日志 */
#define DRV_ADC_LOG_LEVEL_WARN         (1U)    /**< 警告日志 */
#define DRV_ADC_LOG_LEVEL_INFO         (2U)    /**< 信息日志 */
#define DRV_ADC_LOG_LEVEL_DEBUG        (3U)    /**< 调试日志 */

/* 当前日志级别（可通过修改此值控制日志输出详细程度） */
#define DRV_ADC_LOG_CURRENT_LEVEL      (DRV_ADC_LOG_LEVEL_INFO)

/* 日志输出宏（可根据项目实际情况修改底层实现） */
#if DRV_ADC_LOG_ENABLE == 1U

/* 根据项目实际使用的日志系统修改此处 */
#include "my_log.h"

#define DRV_ADC_LOGE(fmt, ...)    MY_LOG_E(fmt, ##__VA_ARGS__)
#define DRV_ADC_LOGW(fmt, ...)    MY_LOG_W(fmt, ##__VA_ARGS__)
#define DRV_ADC_LOGI(fmt, ...)    MY_LOG_I(fmt, ##__VA_ARGS__)
#define DRV_ADC_LOGD(fmt, ...)    do { \
                                        if (DRV_ADC_LOG_CURRENT_LEVEL >= DRV_ADC_LOG_LEVEL_DEBUG) \
                                        { \
                                            MY_LOG_D(fmt, ##__VA_ARGS__); \
                                        } \
                                    } while(0)

#else

#define DRV_ADC_LOGE(fmt, ...)
#define DRV_ADC_LOGW(fmt, ...)
#define DRV_ADC_LOGI(fmt, ...)
#define DRV_ADC_LOGD(fmt, ...)

#endif /* DRV_ADC_LOG_ENABLE */

/*********************************************************************
 * 断言宏定义（开发阶段捕获严重错误）
 *********************************************************************/

/* 断言开关（1=启用，0=禁用） */
#ifndef DRV_ADC_ASSERT_ENABLE
#define DRV_ADC_ASSERT_ENABLE      (0U)
#endif

#if DRV_ADC_ASSERT_ENABLE == 1U
    /* 使用FreeRTOS的configASSERT */
    #define DRV_ADC_ASSERT(expr)   configASSERT(expr)
#else
    #define DRV_ADC_ASSERT(expr)   ((void)0)
#endif

/* ==================== ADC 端口定义 ==================== */

typedef enum {
    DRV_ADC0 = 0,                     /**< ADC0 实例 */
    DRV_ADC1,                         /**< ADC1 实例 */
    DRV_ADC2,                         /**< ADC2 实例 */
    DRV_ADC_MAX                       /**< ADC 最大数量 */
} drv_adc_port_e;

/* ==================== ADC 通道定义 ==================== */

typedef enum {
    DRV_ADC_CHANNEL_0 = 0,            /**< ADC 通道 0 */
    DRV_ADC_CHANNEL_1,                /**< ADC 通道 1 */
    DRV_ADC_CHANNEL_2,                /**< ADC 通道 2 */
    DRV_ADC_CHANNEL_3,                /**< ADC 通道 3 */
    DRV_ADC_CHANNEL_4,                /**< ADC 通道 4 */
    DRV_ADC_CHANNEL_5,                /**< ADC 通道 5 */
    DRV_ADC_CHANNEL_6,                /**< ADC 通道 6 */
    DRV_ADC_CHANNEL_7,                /**< ADC 通道 7 */
    DRV_ADC_CHANNEL_8,                /**< ADC 通道 8 */
    DRV_ADC_CHANNEL_9,                /**< ADC 通道 9 */
    DRV_ADC_CHANNEL_10,               /**< ADC 通道 10 */
    DRV_ADC_CHANNEL_11,               /**< ADC 通道 11 */
    DRV_ADC_CHANNEL_12,               /**< ADC 通道 12 */
    DRV_ADC_CHANNEL_13,               /**< ADC 通道 13 */
    DRV_ADC_CHANNEL_14,               /**< ADC 通道 14 */
    DRV_ADC_CHANNEL_15,               /**< ADC 通道 15 */
    DRV_ADC_CHANNEL_16,               /**< 通道16（ADC0:温度传感器, ADC1/2:外部通道） */
    DRV_ADC_CHANNEL_17,               /**< 通道17（ADC0:内部参考电压, ADC1:外部通道, ADC2:不支持） */
    DRV_ADC_CHANNEL_MAX
} drv_adc_channel_e;

/* ==================== ADC 分辨率定义 ==================== */

typedef enum {
    DRV_ADC_RESOLUTION_12B = 0,       /**< 12位分辨率 */
    DRV_ADC_RESOLUTION_10B,           /**< 10位分辨率 */
    DRV_ADC_RESOLUTION_8B,            /**< 8位分辨率 */
    DRV_ADC_RESOLUTION_6B             /**< 6位分辨率 */
} drv_adc_resolution_e;

/* ==================== ADC 数据对齐方式定义 ==================== */

typedef enum {
    DRV_ADC_DATAALIGN_RIGHT = 0,      /**< 右对齐（LSB） */
    DRV_ADC_DATAALIGN_LEFT            /**< 左对齐（MSB） */
} drv_adc_dataalign_e;

/* ==================== ADC 采样时间定义 ==================== */

typedef enum {
    DRV_ADC_SAMPLETIME_1POINT5 = 0,   /**< 1.5个周期 */
    DRV_ADC_SAMPLETIME_7POINT5,       /**< 7.5个周期 */
    DRV_ADC_SAMPLETIME_13POINT5,      /**< 13.5个周期 */
    DRV_ADC_SAMPLETIME_28POINT5,      /**< 28.5个周期 */
    DRV_ADC_SAMPLETIME_41POINT5,      /**< 41.5个周期 */
    DRV_ADC_SAMPLETIME_55POINT5,      /**< 55.5个周期 */
    DRV_ADC_SAMPLETIME_71POINT5,      /**< 71.5个周期 */
    DRV_ADC_SAMPLETIME_239POINT5      /**< 239.5个周期 */
} drv_adc_sampletime_e;

/* ==================== ADC 转换模式定义 ==================== */

typedef enum {
    DRV_ADC_MODE_SINGLE = 0,          /**< 单次转换模式 */
    DRV_ADC_MODE_CONTINUOUS,          /**< 连续转换模式 */
    DRV_ADC_MODE_SCAN_SINGLE,         /**< 扫描模式+单次转换 */
    DRV_ADC_MODE_SCAN_CONTINUOUS      /**< 扫描模式+连续转换 */
} drv_adc_mode_e;

/* ==================== ADC 触发源定义 ==================== */

typedef enum {
    DRV_ADC_TRIGGER_SOFTWARE = 0,     /**< 软件触发 */
    DRV_ADC_TRIGGER_EXTERNAL,         /**< 外部触发 */
    DRV_ADC_TRIGGER_DMA               /**< DMA触发 */
} drv_adc_trigger_e;

/* ==================== ADC 标志定义 ==================== */

/**
 * @brief ADC 标志枚举
 * @note  用于 drv_adc_flag_get() 和 drv_adc_flag_clear()
 */
typedef enum {
    DRV_ADC_FLAG_EOC,    /**< 转换完成标志（EORC：End Of Routine Conversion） */
    DRV_ADC_FLAG_WD0E    /**< 看门狗触发标志（WD0E：Watchdog0 Event） */
} drv_adc_flag_e;

/** ADC 标志状态 */
#define DRV_ADC_FLAG_SET              SET     /**< 标志置位 */
#define DRV_ADC_FLAG_RESET            RESET   /**< 标志复位 */

/* ==================== ADC 看门狗中断回调函数类型 ==================== */

/**
 * @brief ADC看门狗中断回调函数类型
 * @param port      ADC端口
 * @note  在中断上下文中执行，不能使用阻塞API
 *        仅用于低功耗唤醒场景，正常工作时不使用ADC中断
 */
typedef void (*drv_adc_wdg_callback_t)(drv_adc_port_e port);

/**
 * @brief ADC驱动错误码
 */
#define DRV_ADC_ERR_OK             (0)     /**< 成功 */
#define DRV_ADC_ERR_FAILED         (-1)    /**< 失败 */
#define DRV_ADC_ERR_TIMEOUT        (-2)    /**< 超时 */
#define DRV_ADC_ERR_INVALID_PARAM  (-3)    /**< 参数错误 */
#define DRV_ADC_ERR_NOT_READY      (-4)    /**< 未就绪 */
#define DRV_ADC_ERR_BUSY           (-5)    /**< 忙 */
#define DRV_ADC_ERR_NOT_INIT       (-6)    /**< 未初始化 */

/* ==================== ADC 配置结构体定义 ==================== */

typedef struct {
    drv_adc_port_e port;                  /**< ADC端口（DRV_ADC0/1/2） */
    drv_adc_resolution_e resolution;      /**< 分辨率 */
    drv_adc_dataalign_e data_align;       /**< 数据对齐方式 */
    drv_adc_mode_e mode;                  /**< 转换模式 */
    drv_adc_trigger_e trigger;            /**< 触发源 */
    uint32_t timeout_ms;                  /**< 转换超时时间（ms） */
    bool use_mutex;                       /**< 启用互斥锁（线程安全） */
} drv_adc_config_t;

/* ==================== ADC 通道配置结构体定义 ==================== */

typedef struct {
    drv_adc_channel_e channel;            /**< ADC通道 */
    drv_adc_sampletime_e sample_time;     /**< 采样时间 */
    uint8_t rank;                         /**< 规则通道序列位置（0-15） */
} drv_adc_channel_config_t;

/* ==================== ADC 状态结构体定义 ==================== */

typedef struct {
    bool is_init;                         /**< 是否已初始化 */
    bool is_converting;                   /**< 是否正在转换 */
    drv_adc_mode_e mode;                  /**< 当前转换模式 */
    uint8_t channel_count;                /**< 配置的通道数量 */
} drv_adc_state_t;

/* ==================== 函数声明 ==================== */

/*********************************************************************
 * @brief   ADC初始化
 * @param   config  ADC配置结构体指针
 * @return  int 错误码
 * @note    自动使能ADC时钟，配置分辨率、对齐方式、转换模式
 * @note    禁止在中断上下文中调用（内部使用vTaskDelay等待ADC稳定）
 *********************************************************************/
int drv_adc_init(const drv_adc_config_t *config);

/*********************************************************************
 * @brief   ADC去初始化
 * @param   port    ADC端口
 * @return  int 错误码
 * @note    关闭ADC端口，释放资源
 *********************************************************************/
int drv_adc_deinit(drv_adc_port_e port);

/*********************************************************************
 * @brief   配置规则通道
 * @param   port            ADC端口
 * @param   channel_config  通道配置结构体指针
 * @return  int 错误码
 * @note    配置规则序列通道及采样时间
 *********************************************************************/
int drv_adc_routine_channel_config(drv_adc_port_e port,
                                   const drv_adc_channel_config_t *channel_config);

/*********************************************************************
 * @brief   配置插入通道
 * @param   port            ADC端口
 * @param   channel_config  通道配置结构体指针
 * @return  int 错误码
 * @note    配置插入序列通道及采样时间
 *********************************************************************/
int drv_adc_inserted_channel_config(drv_adc_port_e port,
                                   const drv_adc_channel_config_t *channel_config);

/*********************************************************************
 * @brief   启动ADC转换（软件触发）
 * @param   port    ADC端口
 * @return  int 错误码
 * @note    仅适用于软件触发模式
 *********************************************************************/
int drv_adc_start_conversion(drv_adc_port_e port);

/*********************************************************************
 * @brief   等待转换完成
 * @param   port        ADC端口
 * @param   timeout_ms  超时时间（ms）
 * @return  int 错误码
 * @note    阻塞等待转换完成标志
 *********************************************************************/
int drv_adc_wait_conversion_done(drv_adc_port_e port, uint32_t timeout_ms);

/*********************************************************************
 * @brief   读取规则通道数据
 * @param   port    ADC端口
 * @param   data    数据输出指针
 * @return  int 错误码
 * @note    读取规则序列转换结果
 *********************************************************************/
int drv_adc_routine_data_read(drv_adc_port_e port, uint16_t *data);

/*********************************************************************
 * @brief   读取插入通道数据
 * @param   port    ADC端口
 * @param   data    数据输出指针
 * @return  int 错误码
 * @note    读取插入序列转换结果
 *********************************************************************/
int drv_adc_inserted_data_read(drv_adc_port_e port, uint16_t *data);

/*********************************************************************
 * @brief   使能ADC
 * @param   port    ADC端口
 * @return  int 错误码
 * @note    使能ADC开始工作
 *********************************************************************/
int32_t drv_adc_enable(drv_adc_port_e port);

/*********************************************************************
 * @brief   禁能ADC
 * @param   port    ADC端口
 * @return  int 错误码
 * @note    禁能ADC进入低功耗模式
 *********************************************************************/
int32_t drv_adc_disable(drv_adc_port_e port);


/*********************************************************************
 * @brief   查询ADC标志状态
 * @param   port    ADC端口
 * @param   flag    标志类型
 * @return  true=标志置位，false=标志复位
 * @note    阻塞轮询模式下查询转换完成或看门狗触发
 *********************************************************************/
bool drv_adc_flag_get(drv_adc_port_e port, drv_adc_flag_e flag);

/*********************************************************************
 * @brief   清除ADC标志
 * @param   port    ADC端口
 * @param   flag    标志类型
 * @return  int 错误码
 * @note    清除指定的ADC标志，避免重复触发
 *********************************************************************/
int32_t drv_adc_flag_clear(drv_adc_port_e port, drv_adc_flag_e flag);

/*********************************************************************
 * @brief   读取ADC转换结果
 * @param   port    ADC端口
 * @return  uint16_t ADC值（0-4095，12位分辨率）
 * @note    读取规则通道转换结果，自动清除EORC标志
 *          适用于轮询模式
 *********************************************************************/
uint16_t drv_adc_read(drv_adc_port_e port);

/*********************************************************************
 * @brief   配置规则通道数量
 * @param   port    ADC端口
 * @param   count   通道数量（1-16）
 * @return  int 错误码
 * @note    设置规则序列通道数量
 *********************************************************************/
int32_t drv_adc_channel_count(drv_adc_port_e port, uint8_t count);

/*********************************************************************
 * @brief   单通道单次转换（便捷接口）
 * @param   port        ADC端口
 * @param   channel     ADC通道
 * @param   sample_time 采样时间
 * @param   data        数据输出指针
 * @return  int 错误码
 * @note    配置单通道并执行单次转换，读取结果（最常用接口）
 * @note    禁止在中断上下文中调用（内部使用vTaskDelay等待内部通道稳定）
 *********************************************************************/
int drv_adc_single_read(drv_adc_port_e port, drv_adc_channel_e channel,
                        drv_adc_sampletime_e sample_time, uint16_t *data);

/*********************************************************************
 * @brief   配置模拟看门狗（含中断回调）
 * @param   port           ADC端口
 * @param   channel        监控通道
 * @param   low_threshold  低阈值
 * @param   high_threshold 高阈值
 * @param   callback       看门狗中断回调函数（可为NULL）
 * @return  int 错误码
 * @note    当转换结果超出阈值范围时触发看门狗中断
 *          callback为NULL时不使能中断，仅配置阈值
 *          主要用于低功耗唤醒场景
 *********************************************************************/
int drv_adc_watchdog_config(drv_adc_port_e port, drv_adc_channel_e channel,
                            uint32_t low_threshold, uint32_t high_threshold,
                            drv_adc_wdg_callback_t callback);

/*********************************************************************
 * @brief   ADC中断处理函数（由ISR调用）
 * @param   port    ADC端口
 * @return  none
 * @note    在gd32f50x_it.c的ISR中调用，仅处理看门狗中断
 *********************************************************************/
void drv_adc_irq_handler(drv_adc_port_e port);

/*********************************************************************
 * @brief   使能ADC DMA模式
 * @param   port    ADC端口
 * @return  int 错误码
 * @note    使能规则通道DMA请求
 *********************************************************************/
int drv_adc_dma_mode_enable(drv_adc_port_e port);

/*********************************************************************
 * @brief   禁能ADC DMA模式
 * @param   port    ADC端口
 * @return  int 错误码
 * @note    禁能规则通道DMA请求
 *********************************************************************/
int drv_adc_dma_mode_disable(drv_adc_port_e port);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_ADC_H__ */
