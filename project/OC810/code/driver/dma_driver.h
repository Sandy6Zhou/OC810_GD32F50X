/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       dma_driver.h
**文件描述：       DMA驱动模块头文件
**当前版本：       V1.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.05.08
*********************************************************************
** 功能描述：       1. 基于GD32标准库的轻量级DMA封装
**                 2. 提供DMA通道配置、启动/停止、中断回调功能
**                 3. 支持传输完成/半传输/错误三种中断
**                 4. 支持循环模式和内存到内存传输
**                 5. 使用驱动层类型枚举，实现与GD32库类型隔离
*********************************************************************/

#ifndef __DMA_DRIVER_H__
#define __DMA_DRIVER_H__

#include "gd32f50x_dma.h"
#include "gd32f50x_rcu.h"
#include <stdbool.h>
#include <stdint.h>

/*********************************************************************
 * 日志宏定义（便于移植和独立控制）
 *********************************************************************/

/* 日志开关（1=开启，0=关闭） */
#define DRV_DMA_LOG_ENABLE      (1U)  /* 驱动日志使能 */

/* 日志级别定义 */
#define DRV_DMA_LOG_LEVEL_ERROR        (0U)    /**< 错误日志 */
#define DRV_DMA_LOG_LEVEL_WARN         (1U)    /**< 警告日志 */
#define DRV_DMA_LOG_LEVEL_INFO         (2U)    /**< 信息日志 */
#define DRV_DMA_LOG_LEVEL_DEBUG        (3U)    /**< 调试日志 */

/* 当前日志级别（可通过修改此值控制日志输出详细程度） */
#define DRV_DMA_LOG_CURRENT_LEVEL      (DRV_DMA_LOG_LEVEL_INFO)

/* 日志输出宏（可根据项目实际情况修改底层实现） */
#if DRV_DMA_LOG_ENABLE == 1U

/* 根据项目实际使用的日志系统修改此处 */
#include "my_log.h"

#define DRV_DMA_LOGE(fmt, ...)    MY_LOG_E(fmt, ##__VA_ARGS__)
#define DRV_DMA_LOGW(fmt, ...)    MY_LOG_W(fmt, ##__VA_ARGS__)
#define DRV_DMA_LOGI(fmt, ...)    MY_LOG_I(fmt, ##__VA_ARGS__)
#define DRV_DMA_LOGD(fmt, ...)    do { \
                                        if (DRV_DMA_LOG_CURRENT_LEVEL >= DRV_DMA_LOG_LEVEL_DEBUG) \
                                        { \
                                            MY_LOG_D(fmt, ##__VA_ARGS__); \
                                        } \
                                    } while(0)

#else

#define DRV_DMA_LOGE(fmt, ...)
#define DRV_DMA_LOGW(fmt, ...)
#define DRV_DMA_LOGI(fmt, ...)
#define DRV_DMA_LOGD(fmt, ...)

#endif /* DRV_DMA_LOG_ENABLE */

/*********************************************************************
 * 错误码定义
 *********************************************************************/

/** DMA 驱动错误码 */
typedef enum
{
    DRV_DMA_ERR_OK = 0,                 /**< 成功 */
    DRV_DMA_ERR_INVALID_CHANNEL,        /**< 无效的通道 ID */
    DRV_DMA_ERR_INVALID_PARAM,          /**< 无效的参数 */
    DRV_DMA_ERR_NOT_INITIALIZED,        /**< 通道未初始化 */
    DRV_DMA_ERR_BUSY,                   /**< 通道忙 */
    DRV_DMA_ERR_TRANSFER_COMPLETE       /**< 传输已完成 */
} drv_dma_err_e;

/*********************************************************************
 * 数据类型定义
 *********************************************************************/

/** DMA 通道 ID 枚举 */
typedef enum
{
    /* DMA0 通道 */
    DRV_DMA0_CH0 = 0,          /**< DMA0 Channel 0 */
    DRV_DMA0_CH1,              /**< DMA0 Channel 1 */
    DRV_DMA0_CH2,              /**< DMA0 Channel 2 */
    DRV_DMA0_CH3,              /**< DMA0 Channel 3 */
    DRV_DMA0_CH4,              /**< DMA0 Channel 4 */
    DRV_DMA0_CH5,              /**< DMA0 Channel 5 */
    DRV_DMA0_CH6,              /**< DMA0 Channel 6 */

    /* DMA1 通道 */
    DRV_DMA1_CH0,              /**< DMA1 Channel 0 */
    DRV_DMA1_CH1,              /**< DMA1 Channel 1 */
    DRV_DMA1_CH2,              /**< DMA1 Channel 2 */
    DRV_DMA1_CH3,              /**< DMA1 Channel 3 */
    DRV_DMA1_CH4,              /**< DMA1 Channel 4 */

    DRV_DMA_MAX                /**< 最大通道数（12） */
} drv_dma_channel_id_e;

/** DMA 传输方向枚举 */
typedef enum
{
    DRV_DMA_DIR_PERIPH_TO_MEMORY = 0,   /**< 外设到内存 */
    DRV_DMA_DIR_MEMORY_TO_PERIPH,       /**< 内存到外设 */
    DRV_DMA_DIR_MEMORY_TO_MEMORY        /**< 内存到内存 */
} drv_dma_direction_e;

/** DMA 数据宽度枚举 */
typedef enum
{
    DRV_DMA_WIDTH_8BIT = 0,      /**< 8-bit */
    DRV_DMA_WIDTH_16BIT,         /**< 16-bit */
    DRV_DMA_WIDTH_32BIT          /**< 32-bit */
} drv_dma_width_e;

/** DMA 通道优先级枚举 */
typedef enum
{
    DRV_DMA_PRIORITY_LOW = 0,    /**< 低优先级 */
    DRV_DMA_PRIORITY_MEDIUM,     /**< 中优先级 */
    DRV_DMA_PRIORITY_HIGH,       /**< 高优先级 */
    DRV_DMA_PRIORITY_ULTRA_HIGH  /**< 超高优先级 */
} drv_dma_priority_e;

/** DMA 中断类型枚举（位掩码格式，支持组合） */
typedef enum
{
    DRV_DMA_INT_FTF = (1 << 0),      /**< 传输完成中断（Full Transfer Finish）= 0x01 */
    DRV_DMA_INT_HTF = (1 << 1),      /**< 半传输中断（Half Transfer Finish）= 0x02 */
    DRV_DMA_INT_ERR = (1 << 2)       /**< 错误中断（Error）= 0x04 */
} drv_dma_int_type_e;

/** DMA 传输模式枚举 */
typedef enum
{
    DRV_DMA_MODE_NORMAL = 0,     /**< 正常模式（传输完成后停止） */
    DRV_DMA_MODE_CIRCULAR        /**< 循环模式（自动重载） */
} drv_dma_mode_e;

/** DMA 通道配置结构体 */
typedef struct
{
    uint32_t request_id;                    /**< DMAMUX 请求源 ID（如 DMA_REQUEST_USART0_RX） */
    uint32_t periph_addr;                   /**< 外设地址 */
    uint32_t memory_addr;                   /**< 内存地址 */
    drv_dma_width_e periph_width;           /**< 外设数据宽度 */
    drv_dma_width_e memory_width;           /**< 内存数据宽度 */
    uint16_t transfer_number;               /**< 传输数量（1-65535） */
    drv_dma_direction_e direction;          /**< 传输方向 */
    drv_dma_priority_e priority;            /**< 通道优先级 */
    drv_dma_mode_e mode;                    /**< 传输模式（正常/循环） */
    bool periph_inc;                        /**< 外设地址递增（true=递增，false=固定） */
    bool memory_inc;                        /**< 内存地址递增（true=递增，false=固定） */
} drv_dma_config_t;

/** DMA 中断回调函数类型（参数为触发中断的通道 ID） */
typedef void (*drv_dma_callback_t)(drv_dma_channel_id_e channel_id);

/*********************************************************************
 * API 接口声明
 *********************************************************************/

/**
 * @brief  初始化 DMA 通道
 * @param  channel_id DMA 通道 ID
 * @param  config 通道配置参数
 * @return DRV_DMA_ERR_OK=成功，其他=失败
 */
int32_t drv_dma_init(drv_dma_channel_id_e channel_id, drv_dma_config_t *config);

/**
 * @brief  去初始化 DMA 通道
 * @param  channel_id DMA 通道 ID
 * @return DRV_DMA_ERR_OK=成功，其他=失败
 */
int32_t drv_dma_deinit(drv_dma_channel_id_e channel_id);

/**
 * @brief  启动 DMA 传输
 * @param  channel_id DMA 通道 ID
 * @return DRV_DMA_ERR_OK=成功，其他=失败
 */
int32_t drv_dma_start(drv_dma_channel_id_e channel_id);

/**
 * @brief  停止 DMA 传输
 * @param  channel_id DMA 通道 ID
 * @return DRV_DMA_ERR_OK=成功，其他=失败
 */
int32_t drv_dma_stop(drv_dma_channel_id_e channel_id);

/**
 * @brief  禁能 DMA 通道（寄存器操作，ISR 安全）
 * @param  channel_id DMA 通道 ID
 * @return none
 * @note   仅操作寄存器，不更新状态、不打印日志，适用于中断上下文
 */
void drv_dma_channel_disable(drv_dma_channel_id_e channel_id);

/**
 * @brief  注册 DMA 中断回调函数
 * @param  channel_id DMA 通道 ID
 * @param  int_type 中断类型（FTF/HTF/ERR）
 * @param  callback 回调函数指针
 * @return DRV_DMA_ERR_OK=成功，其他=失败
 */
int32_t drv_dma_callback_register(drv_dma_channel_id_e channel_id,
                                  drv_dma_int_type_e int_type,
                                  drv_dma_callback_t callback);

/**
 * @brief  注销 DMA 中断回调函数
 * @param  channel_id DMA 通道 ID
 * @param  int_type 中断类型
 * @return DRV_DMA_ERR_OK=成功，其他=失败
 */
int32_t drv_dma_callback_unregister(drv_dma_channel_id_e channel_id,
                                    drv_dma_int_type_e int_type);

/**
 * @brief  使能 DMA 中断
 * @param  channel_id DMA 通道 ID
 * @param  int_type 中断类型（可组合：DRV_DMA_INT_FTF | DRV_DMA_INT_HTF | DRV_DMA_INT_ERR）
 * @param  nvic_priority NVIC 中断优先级（0-15）
 * @return DRV_DMA_ERR_OK=成功，其他=失败
 */
int32_t drv_dma_int_enable(drv_dma_channel_id_e channel_id,
                           uint8_t int_type,
                           uint8_t nvic_priority);

/**
 * @brief  禁能 DMA 中断
 * @param  channel_id DMA 通道 ID
 * @param  int_type 中断类型
 * @return DRV_DMA_ERR_OK=成功，其他=失败
 */
int32_t drv_dma_int_disable(drv_dma_channel_id_e channel_id,
                            uint8_t int_type);

/**
 * @brief  设置传输数量
 * @param  channel_id DMA 通道 ID
 * @param  number 传输数量（1-65535）
 * @return DRV_DMA_ERR_OK=成功，其他=失败
 */
int32_t drv_dma_set_transfer_number(drv_dma_channel_id_e channel_id, uint16_t number);

/**
 * @brief  获取剩余传输数量
 * @param  channel_id DMA 通道 ID
 * @return 剩余传输数量（0 表示传输完成）
 */
uint16_t drv_dma_get_transfer_number(drv_dma_channel_id_e channel_id);

/**
 * @brief  设置内存地址（运行时动态修改）
 * @param  channel_id DMA 通道 ID
 * @param  addr 新内存地址
 * @return DRV_DMA_ERR_OK=成功，其他=失败
 */
int32_t drv_dma_set_memory_address(drv_dma_channel_id_e channel_id, uint32_t addr);

/**
 * @brief  设置外设地址（运行时动态修改）
 * @param  channel_id DMA 通道 ID
 * @param  addr 新外设地址
 * @return DRV_DMA_ERR_OK=成功，其他=失败
 */
int32_t drv_dma_set_periph_address(drv_dma_channel_id_e channel_id, uint32_t addr);

/**
 * @brief  查询 DMA 通道是否运行中
 * @param  channel_id DMA 通道 ID
 * @return true=运行中，false=未运行
 */
bool drv_dma_is_running(drv_dma_channel_id_e channel_id);

/**
 * @brief  查询 DMA 通道是否已初始化
 * @param  channel_id DMA 通道 ID
 * @return true=已初始化，false=未初始化
 */
bool drv_dma_is_initialized(drv_dma_channel_id_e channel_id);

/**
 * @brief  运行 DMA 中断回调（由 ISR 调用）
 * @param  channel_id DMA 通道 ID
 * @param  int_flag 中断标志（FTF/HTF/ERR）
 * @note   此函数由 gd32f50x_it.c 中的 ISR 调用
 */
void drv_dma_run_callback(drv_dma_channel_id_e channel_id, drv_dma_int_type_e int_flag);

/**
 * @brief  DMA 快速重配置（ISR 安全）
 * @param  channel_id DMA 通道 ID
 * @param  memory_addr 新内存地址
 * @param  transfer_number 新传输数量
 * @return DRV_DMA_ERR_OK=成功，其他=失败
 * @note   仅更新内存地址和传输数量，清除中断标志并重启通道
 * @note   适用于 ISR 中的 DMA 缓冲区切换场景，无日志输出，极简快速
 */
int32_t drv_dma_reconfig_fast(drv_dma_channel_id_e channel_id,
                              uint32_t memory_addr,
                              uint16_t transfer_number);

#endif /* __DMA_DRIVER_H__ */
