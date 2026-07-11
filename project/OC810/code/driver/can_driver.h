/*******************************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       can_driver.h
**文件描述：       CAN/CAN FD驱动模块接口定义
**当前版本：       V1.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.04.20
*******************************************************************************
** 功能描述：       1. 提供双CAN端口独立管理（CAN0/CAN1）
**                 2. 支持CAN 2.0B和CAN FD双协议
**                 3. 支持轮询模式和中断模式
**                 4. 支持GPIO配置宏表、电源管理
**                 5. 驱动层与应用层完全解耦
**                 6. 所有内存资源由应用层管理
**                 7. 支持TDC传输延迟补偿（CAN FD高速模式）
*******************************************************************************/

#ifndef __CAN_DRIVER_H__
#define __CAN_DRIVER_H__

#include "gd32f50x.h"
#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * 日志宏定义（便于移植和独立控制）
 ******************************************************************************/

/* 日志开关（1=开启，0=关闭） */
#define DRV_CAN_LOG_ENABLE         (1U)

/* 日志级别定义 */
#define DRV_CAN_LOG_LEVEL_ERROR    (0U)    /**< 错误日志 */
#define DRV_CAN_LOG_LEVEL_WARN     (1U)    /**< 警告日志 */
#define DRV_CAN_LOG_LEVEL_INFO     (2U)    /**< 信息日志 */
#define DRV_CAN_LOG_LEVEL_DEBUG    (3U)    /**< 调试日志 */

/* 当前日志级别（可通过修改此值控制日志输出详细程度） */
#define DRV_CAN_LOG_CURRENT_LEVEL  (DRV_CAN_LOG_LEVEL_DEBUG)  /* 临时改为DEBUG排查问题 */

/* 日志输出宏（可根据项目实际情况修改底层实现） */
#if DRV_CAN_LOG_ENABLE == 1U

/* 根据项目实际使用的日志系统修改此处 */
#include "my_log.h"

#define DRV_CAN_LOGE(fmt, ...)    MY_LOG_E(fmt, ##__VA_ARGS__)
#define DRV_CAN_LOGW(fmt, ...)    MY_LOG_W(fmt, ##__VA_ARGS__)
#define DRV_CAN_LOGI(fmt, ...)    MY_LOG_I(fmt, ##__VA_ARGS__)
#define DRV_CAN_LOGD(fmt, ...)    do { \
                                        if (DRV_CAN_LOG_CURRENT_LEVEL >= DRV_CAN_LOG_LEVEL_DEBUG) \
                                        { \
                                            MY_LOG_D(fmt, ##__VA_ARGS__); \
                                        } \
                                    } while(0)

#else

#define DRV_CAN_LOGE(fmt, ...)
#define DRV_CAN_LOGW(fmt, ...)
#define DRV_CAN_LOGI(fmt, ...)
#define DRV_CAN_LOGD(fmt, ...)

#endif /* DRV_CAN_LOG_ENABLE */

/*******************************************************************************
 * 宏定义
 ******************************************************************************/

/**
 * @brief CAN驱动错误码
 */
#define DRV_CAN_ERR_OK             (0)     /**< 成功 */
#define DRV_CAN_ERR_FAILED         (-1)    /**< 失败 */
#define DRV_CAN_ERR_TIMEOUT        (-2)    /**< 超时 */
#define DRV_CAN_ERR_INVALID_PARAM  (-3)    /**< 参数错误 */
#define DRV_CAN_ERR_NOT_READY      (-4)    /**< 未就绪 */
#define DRV_CAN_ERR_BUS_OFF        (-5)    /**< 总线关闭 */
#define DRV_CAN_ERR_OVERRUN        (-6)    /**< 数据覆盖 */
#define DRV_CAN_ERR_ARB_LOST       (-7)    /**< 仲裁丢失 */

/*******************************************************************************
 * GPIO配置宏表（集中管理）
 ******************************************************************************/

/**
 * @brief CAN0 GPIO配置选项
 */
#define DRV_CAN0_NO_USE            (0U)    /**< 未使用 */
#define DRV_CAN0_GPIO_PD0_PD1      (1U)    /**< PD0(RX), PD1(TX) - AF8 */
#define DRV_CAN0_GPIO_PA11_PA12    (2U)    /**< PA11(RX), PA12(TX) - AF8 */
#define DRV_CAN0_GPIO_PB8_PB9      (3U)    /**< PB8(RX), PB9(TX) - AF8 */

/**
 * @brief CAN1 GPIO配置选项
 */
#define DRV_CAN1_NO_USE            (0U)    /**< 未使用 */
#define DRV_CAN1_GPIO_PB5_PB6      (1U)    /**< PB5(RX), PB6(TX) - AF8 */
#define DRV_CAN1_GPIO_PB12_PB13    (2U)    /**< PB12(RX), PB13(TX) - AF8 */

/*******************************************************************************
 * 用户配置区：选择每个CAN使用的GPIO引脚组合
 * 修改此处的值即可切换CAN的GPIO引脚，无需修改驱动代码
 ******************************************************************************/

/**
 * @brief CAN0 GPIO选择（修改此值切换引脚）
 */
#ifndef DRV_CAN0_GPIO_SEL
#define DRV_CAN0_GPIO_SEL          DRV_CAN0_GPIO_PD0_PD1
#endif

/**
 * @brief CAN1 GPIO选择（修改此值切换引脚）
 */
#ifndef DRV_CAN1_GPIO_SEL
#define DRV_CAN1_GPIO_SEL          DRV_CAN1_GPIO_PB5_PB6
#endif

/*******************************************************************************
 * 枚举类型定义
 ******************************************************************************/

/**
 * @brief CAN端口枚举
 */
typedef enum
{
    DRV_CAN_PORT_CAN0 = 0,         /**< CAN0端口 */
    DRV_CAN_PORT_CAN1,             /**< CAN1端口 */
    DRV_CAN_PORT_MAX               /**< CAN端口最大值 */
} drv_can_port_e;

/**
 * @brief CAN协议类型枚举
 */
typedef enum
{
    DRV_CAN_PROTOCOL_CAN20B = 0,   /**< CAN 2.0B协议（传统CAN） */
    DRV_CAN_PROTOCOL_CANFD         /**< CAN FD协议 */
} drv_can_protocol_e;

/**
 * @brief CAN通信模式枚举
 */
typedef enum
{
    DRV_CAN_MODE_NORMAL = 0,       /**< 正常模式 */
    DRV_CAN_MODE_LOOPBACK,         /**< 环回模式（自发自收） */
    DRV_CAN_MODE_SILENT,           /**< 静默模式（只收不发） */
    DRV_CAN_MODE_SILENT_LOOPBACK   /**< 静默环回模式 */
} drv_can_mode_e;

/**
 * @brief CAN波特率枚举（仲裁段）
 * @note 包含车载CAN常用波特率，支持CAN 2.0B和CAN FD仲裁段
 */
typedef enum
{
    DRV_CAN_BITRATE_10K = 0,       /**< 10 kbps（低速CAN，特殊应用） */
    DRV_CAN_BITRATE_20K,           /**< 20 kbps（低速CAN，特殊应用） */
    DRV_CAN_BITRATE_50K,           /**< 50 kbps（低速CAN，商用车） */
    DRV_CAN_BITRATE_100K,          /**< 100 kbps（低速CAN） */
    DRV_CAN_BITRATE_125K,          /**< 125 kbps（CAN 2.0B常用） */
    DRV_CAN_BITRATE_250K,          /**< 250 kbps（CAN 2.0B常用，商用车） */
    DRV_CAN_BITRATE_500K,          /**< 500 kbps（CAN 2.0B最常用，乘用车） */
    DRV_CAN_BITRATE_800K,          /**< 800 kbps（高速CAN，特殊应用） */
    DRV_CAN_BITRATE_1M             /**< 1 Mbps（CAN 2.0B最高速率） */
} drv_can_bitrate_e;

/**
 * @brief CAN FD数据段波特率枚举
 * @note CAN FD数据段速率通常高于仲裁段，支持BRS（Bit Rate Switching）
 * @note 车载标准：2Mbps（功能消息）、5Mbps（刷写编程），符合ISO 11898-1:2015
 */
typedef enum
{
    DRV_CAN_FD_BITRATE_1M = 0,     /**< 1 Mbps（低速CAN FD） */
    DRV_CAN_FD_BITRATE_2M,         /**< 2 Mbps（车载常用，功能消息） */
    DRV_CAN_FD_BITRATE_4M,         /**< 4 Mbps（高速CAN FD） */
    DRV_CAN_FD_BITRATE_5M          /**< 5 Mbps（车载标准最高，刷写编程） */
} drv_can_fd_bitrate_e;

/**
 * @brief CAN帧类型枚举
 */
typedef enum
{
    DRV_CAN_FRAME_STANDARD = 0,    /**< 标准帧（11位ID） */
    DRV_CAN_FRAME_EXTENDED         /**< 扩展帧（29位ID） */
} drv_can_frame_type_e;

/**
 * @brief CAN帧格式枚举
 */
typedef enum
{
    DRV_CAN_FORMAT_CAN20B = 0,     /**< CAN 2.0B格式（最大8字节） */
    DRV_CAN_FORMAT_CANFD           /**< CAN FD格式（最大64字节） */
} drv_can_format_e;

/**
 * @brief CAN总线状态枚举
 */
typedef enum
{
    DRV_CAN_STATE_UNINIT = 0,      /**< 未初始化 */
    DRV_CAN_STATE_ACTIVE,          /**< 活动状态 */
    DRV_CAN_STATE_BUS_OFF,         /**< 总线关闭 */
    DRV_CAN_STATE_SUSPENDED        /**< 挂起状态 */
} drv_can_state_e;

/**
 * @brief CAN过滤器模式枚举
 */
typedef enum
{
    DRV_CAN_FILTER_MODE_ID_MASK = 0,  /**< 标识符屏蔽模式 */
    DRV_CAN_FILTER_MODE_ID_LIST       /**< 标识符列表模式 */
} drv_can_filter_mode_e;

/**
 * @brief CAN总线错误类型枚举
 */
typedef enum
{
    DRV_CAN_ERR_TYPE_NONE = 0,     /**< 无错误 */
    DRV_CAN_ERR_TYPE_BIT,          /**< 位错误 */
    DRV_CAN_ERR_TYPE_STUFF,        /**< 填充错误 */
    DRV_CAN_ERR_TYPE_CRC,          /**< CRC错误 */
    DRV_CAN_ERR_TYPE_FORM,         /**< 格式错误 */
    DRV_CAN_ERR_TYPE_ACK           /**< ACK错误 */
} drv_can_err_type_e;

/*******************************************************************************
 * 数据结构定义
 ******************************************************************************/

/**
 * @brief CAN帧数据结构
 */
typedef struct
{
    uint32_t            id;             /**< CAN ID（11位或29位） */
    drv_can_frame_type_e frame_type;    /**< 帧类型（标准/扩展） */
    drv_can_format_e    format;         /**< 帧格式（CAN20B/CANFD） */
    uint8_t             dlc;            /**< 数据长度码（0-8或0-15） */
    uint8_t             data[64];       /**< 数据域（CAN 2.0B最大8字节，CAN FD最大64字节） */
    uint8_t             fd_brs;         /**< CAN FD波特率切换标志（0=仲裁段速率，1=数据段速率） */
    uint8_t             fd_esi;         /**< CAN FD错误状态指示 */
} drv_can_frame_t;

/**
 * @brief CAN过滤器配置结构
 */
typedef struct
{
    uint8_t                 filter_bank;      /**< 过滤器组编号（0-27） */
    drv_can_filter_mode_e   filter_mode;      /**< 过滤器模式 */
    uint32_t                filter_id_high;   /**< 过滤器ID高16位（或标识符1） */
    uint32_t                filter_id_low;    /**< 过滤器ID低16位（或标识符2） */
    uint32_t                filter_mask_high; /**< 过滤器掩码高16位（屏蔽模式） */
    uint32_t                filter_mask_low;  /**< 过滤器掩码低16位（屏蔽模式） */
    uint8_t                 fifo_number;      /**< 关联的FIFO编号（0或1） */
} drv_can_filter_config_t;

/**
 * @brief CAN配置结构体
 */
typedef struct
{
    drv_can_port_e      port;               /**< CAN端口号 */
    drv_can_mode_e      mode;               /**< 通信模式 */
    drv_can_protocol_e  protocol;           /**< 协议类型 */
    drv_can_bitrate_e   arb_bitrate;        /**< 仲裁段波特率 */
    drv_can_fd_bitrate_e data_bitrate;      /**< 数据段波特率（CAN FD） */
    bool                use_interrupt;      /**< 启用中断模式 */
    uint32_t            timeout_ms;         /**< 超时时间（ms） */
    bool                use_mutex;          /**< 启用互斥锁（多任务访问） */
    bool                enable_tdc;         /**< 启用TDC（CAN FD > 2Mbps） */
} drv_can_config_t;

/*******************************************************************************
 * 公开API函数声明
 ******************************************************************************/

/*******************************************************************************
 * @brief 初始化CAN端口
 * @param config 指向CAN配置结构体的指针
 * @return DRV_CAN_ERR_OK: 成功，其他: 失败
 ******************************************************************************/
int drv_can_init(const drv_can_config_t *config);

/*******************************************************************************
 * @brief 反初始化CAN端口
 * @param port CAN端口号
 * @return DRV_CAN_ERR_OK: 成功，其他: 失败
 ******************************************************************************/
int drv_can_deinit(drv_can_port_e port);

/*******************************************************************************
 * @brief 发送CAN帧（阻塞，轮询模式）
 * @param port CAN端口号
 * @param frame 指向CAN帧结构体的指针
 * @return DRV_CAN_ERR_OK: 成功，其他: 失败
 ******************************************************************************/
int drv_can_send(drv_can_port_e port, const drv_can_frame_t *frame);

/*******************************************************************************
 * @brief 接收CAN帧（轮询模式）
 * @param port CAN端口号
 * @param frame 指向CAN帧结构体的指针（用于存储接收到的帧）
 * @param fifo_number FIFO编号（0或1）
 ******************************************************************************/
 int drv_can_receive(drv_can_port_e port, drv_can_frame_t *frame, uint8_t fifo_number);

/*******************************************************************************
 * @brief 配置CAN过滤器
 * @param port CAN端口号
 * @param filter_config 指向过滤器配置结构体的指针
 * @return DRV_CAN_ERR_OK: 成功，其他: 失败
 ******************************************************************************/
int drv_can_config_filter(drv_can_port_e port, const drv_can_filter_config_t *filter_config);

/*******************************************************************************
 * @brief 禁用CAN过滤器
 * @param port CAN端口号
 * @param filter_bank 过滤器组编号
 * @return DRV_CAN_ERR_OK: 成功，其他: 失败
 ******************************************************************************/
int drv_can_disable_filter(drv_can_port_e port, uint8_t filter_bank);

/*******************************************************************************
 * @brief 注册接收回调函数（中断模式）
 * @param port CAN端口号
 * @param callback 回调函数指针
 * @return DRV_CAN_ERR_OK: 成功，其他: 失败
 * @note 回调函数原型：void can_rx_callback(drv_can_port_e port, drv_can_frame_t *frame, uint8_t fifo)
 ******************************************************************************/
int drv_can_register_rx_callback(drv_can_port_e port, void (*callback)(drv_can_port_e, drv_can_frame_t *, uint8_t));

/*******************************************************************************
 * @brief 注册发送完成回调函数（中断模式）
 * @param port CAN端口号
 * @param callback 回调函数指针
 * @return DRV_CAN_ERR_OK: 成功，其他: 失败
 * @note 回调函数原型：void can_tx_callback(drv_can_port_e port, uint8_t mailbox)
 ******************************************************************************/
int drv_can_register_tx_callback(drv_can_port_e port, void (*callback)(drv_can_port_e, uint8_t));

/*******************************************************************************
 * @brief 注册错误回调函数
 * @param port CAN端口号
 * @param callback 回调函数指针
 * @return DRV_CAN_ERR_OK: 成功，其他: 失败
 * @note 回调函数原型：void can_err_callback(drv_can_port_e port, drv_can_err_type_e err_type)
 ******************************************************************************/
int drv_can_register_err_callback(drv_can_port_e port, void (*callback)(drv_can_port_e, drv_can_err_type_e));

/*******************************************************************************
 * @brief 查询发送邮箱空闲状态
 * @param port CAN端口号
 * @return 空闲邮箱数量（0-3），负数表示错误
 ******************************************************************************/
int drv_can_get_tx_mailbox_free(drv_can_port_e port);

/*******************************************************************************
 * @brief 查询CAN总线状态
 * @param port CAN端口号
 * @return CAN总线状态枚举值
 ******************************************************************************/
drv_can_state_e drv_can_get_bus_status(drv_can_port_e port);

/*******************************************************************************
 * @brief 查询接收FIFO中的消息数量
 * @param port CAN端口号
 * @param fifo_number FIFO编号（0或1）
 * @return FIFO中的消息数量（0-3），负数表示错误
 ******************************************************************************/
int drv_can_get_rx_message_count(drv_can_port_e port, uint8_t fifo_number);

/*******************************************************************************
 * @brief 挂起CAN端口（电源管理）
 * @param port CAN端口号
 * @return DRV_CAN_ERR_OK: 成功，其他: 失败
 ******************************************************************************/
int drv_can_suspend(drv_can_port_e port);

/*******************************************************************************
 * @brief 恢复CAN端口（电源管理）
 * @param port CAN端口号
 * @return DRV_CAN_ERR_OK: 成功，其他: 失败
 ******************************************************************************/
int drv_can_resume(drv_can_port_e port);

/*******************************************************************************
 * 内部函数：中断处理用（不对外公开，仅供ISR调用）
 ******************************************************************************/

/*******************************************************************************
 * @brief  无锁接收函数（仅供ISR调用）
 * @param  port        CAN端口号
 * @param  frame       指向CAN帧结构体的指针（用于存储接收到的帧）
 * @param  fifo_number FIFO编号（0或1）
 * @return DRV_CAN_ERR_OK: 成功，其他: 失败
 * @note   此函数不获取互斥锁，仅用于中断服务函数中读取FIFO数据
 *         应用层不应直接调用，应使用drv_can_receive()（带锁版本）
 ******************************************************************************/
 int _drv_can_receive_no_lock(drv_can_port_e port, drv_can_frame_t *frame, uint8_t fifo_number);

/*******************************************************************************
 * @brief  执行接收回调函数（仅供ISR调用）
 * @param  port  CAN端口号
 * @param  frame 指向接收到的CAN帧结构体的指针
 * @param  fifo  FIFO编号（0或1）
 * @note   此函数在中断服务函数中调用，用于执行应用层注册的接收回调
 *         回调函数必须快速执行，不能包含阻塞操作（如vTaskDelay、xSemaphoreTake等）
 *         回调函数原型：void can_rx_callback(drv_can_port_e port, drv_can_frame_t *frame, uint8_t fifo)
 ******************************************************************************/
void drv_can_run_rx_callback(drv_can_port_e port, drv_can_frame_t *frame, uint8_t fifo);

/*******************************************************************************
 * @brief  执行发送完成回调函数（仅供ISR调用）
 * @param  port     CAN端口号
 * @param  mailbox  发送完成的邮箱编号（0-2）
 * @note   此函数在中断服务函数中调用，用于执行应用层注册的发送完成回调
 *         回调函数必须快速执行，不能包含阻塞操作
 *         回调函数原型：void can_tx_callback(drv_can_port_e port, uint8_t mailbox)
 ******************************************************************************/
void drv_can_run_tx_callback(drv_can_port_e port, uint8_t mailbox);

/*******************************************************************************
 * @brief  执行错误回调函数（仅供ISR调用）
 * @param  port     CAN端口号
 * @param  err_type 错误类型（位错误、填充错误、CRC错误等）
 * @note   此函数在中断服务函数中调用，用于执行应用层注册错误回调
 *         回调函数必须快速执行，不能包含阻塞操作
 *         回调函数原型：void can_err_callback(drv_can_port_e port, drv_can_err_type_e err_type)
 ******************************************************************************/
void drv_can_run_err_callback(drv_can_port_e port, drv_can_err_type_e err_type);

#ifdef __cplusplus
}
#endif

#endif /* __CAN_DRIVER_H__ */
