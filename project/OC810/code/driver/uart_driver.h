/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       uart_driver.h
**文件描述：       UART驱动模块接口定义
**当前版本：       V1.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.04.22
*********************************************************************
** 功能描述：       1. 提供多UART独立管理接口
**                 2. 支持DMA接收、IDLE空闲中断、RingBuffer
**                 3. 支持低功耗挂起/恢复、线程安全
**                 4. 驱动层与应用层完全解耦
*********************************************************************/

#ifndef __DRV_UART_H__
#define __DRV_UART_H__

#include "gd32f50x.h"
#include <stdint.h>
#include <stdbool.h>
#include "ringbuffer.h"
#include "FreeRTOS.h"
#include "semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * 日志宏定义（便于移植和独立控制）
 *********************************************************************/

/* 日志开关（1=开启，0=关闭） */
#define DRV_UART_LOG_ENABLE      (1U)

/* 日志级别定义 */
#define DRV_UART_LOG_LEVEL_ERROR        (0U)    /**< 错误日志 */
#define DRV_UART_LOG_LEVEL_WARN         (1U)    /**< 警告日志 */
#define DRV_UART_LOG_LEVEL_INFO         (2U)    /**< 信息日志 */
#define DRV_UART_LOG_LEVEL_DEBUG        (3U)    /**< 调试日志 */

/* 当前日志级别（可通过修改此值控制日志输出详细程度） */
#define DRV_UART_LOG_CURRENT_LEVEL      (DRV_UART_LOG_LEVEL_INFO)

/* 日志输出宏（可根据项目实际情况修改底层实现） */
#if DRV_UART_LOG_ENABLE == 1U

/* 根据项目实际使用的日志系统修改此处 */
#include "my_log.h"

#define DRV_UART_LOGE(fmt, ...)    MY_LOG_E(fmt, ##__VA_ARGS__)
#define DRV_UART_LOGW(fmt, ...)    MY_LOG_W(fmt, ##__VA_ARGS__)
#define DRV_UART_LOGI(fmt, ...)    MY_LOG_I(fmt, ##__VA_ARGS__)
#define DRV_UART_LOGD(fmt, ...)    do { \
                                        if (DRV_UART_LOG_CURRENT_LEVEL >= DRV_UART_LOG_LEVEL_DEBUG) \
                                        { \
                                            MY_LOG_D(fmt, ##__VA_ARGS__); \
                                        } \
                                    } while(0)

#else

#define DRV_UART_LOGE(fmt, ...)
#define DRV_UART_LOGW(fmt, ...)
#define DRV_UART_LOGI(fmt, ...)
#define DRV_UART_LOGD(fmt, ...)

#endif /* DRV_UART_LOG_ENABLE */

/*********************************************************************
 * 断言宏定义（开发阶段捕获严重错误）
 *********************************************************************/

/* 断言开关（1=启用，0=禁用） */
#ifndef DRV_UART_ASSERT_ENABLE
#define DRV_UART_ASSERT_ENABLE     (0U)
#endif

#if DRV_UART_ASSERT_ENABLE == 1U
    /* 使用FreeRTOS的configASSERT */
    #define DRV_UART_ASSERT(expr)         configASSERT(expr)
#else
    #define DRV_UART_ASSERT(expr)
#endif

/* 互斥锁超时时间（毫秒），0表示永久等待 */
#ifndef DRV_UART_TX_MUTEX_TIMEOUT_MS
#define DRV_UART_TX_MUTEX_TIMEOUT_MS      (1000U)  /* 默认1秒超时 */
#endif

/* 接收模式定义（注册时确定，中断中使用） */
#define DRV_UART_RX_MODE_DMA_RINGBUF      (0x01U)  /**< DMA + RingBuffer */
#define DRV_UART_RX_MODE_DMA_RXBUF        (0x02U)  /**< DMA + rx_buf */
#define DRV_UART_RX_MODE_NODMA_RINGBUF    (0x03U)  /**< 非DMA + RingBuffer */
#define DRV_UART_RX_MODE_NODMA_RXBUF      (0x04U)  /**< 非DMA + rx_buf */

/* 中断使能标志 */
#define DRV_UART_IRQ_RBNE                 (0x01U)  /**< RXNE中断使能 */
#define DRV_UART_IRQ_IDLE                 (0x02U)  /**< IDLE中断使能 */
#define DRV_UART_IRQ_ERR                  (0x04U)  /**< 错误中断使能 */

/*********************************************************************
 * 数据结构定义
 *********************************************************************/

/**
 * @brief UART端口枚举（与GD32F505VGT7外设一一对应）
 */
typedef enum {
    DRV_UART_PORT_USART0 = 0,       /**< USART0端口 */
    DRV_UART_PORT_USART1,           /**< USART1端口 */
    DRV_UART_PORT_USART2,           /**< USART2端口 */
    DRV_UART_PORT_UART3,            /**< UART3端口 */
    DRV_UART_PORT_UART4,            /**< UART4端口 */
    DRV_UART_PORT_MAX               /**< 端口数量上限，用于参数校验 */
} drv_uart_port_e;

/**
 * @brief UART状态枚举（驱动内部状态机管理）
 */
typedef enum {
    DRV_UART_STATE_UNINIT = 0,      /**< 未初始化（默认状态） */
    DRV_UART_STATE_INIT,            /**< 已初始化（注册完成，未激活） */
    DRV_UART_STATE_ACTIVE,          /**< 活跃状态（可正常收发数据） */
    DRV_UART_STATE_SUSPENDED,       /**< 挂起状态（低功耗，可快速恢复） */
    DRV_UART_STATE_SHUTDOWN         /**< 关闭状态（硬件关闭，需恢复） */
} drv_uart_state_e;

/**
 * @brief UART错误枚举（错误回调上报类型）
 */
typedef enum {
    DRV_UART_ERROR_NONE = 0,        /**< 无错误 */
    DRV_UART_ERROR_OVERRUN,         /**< 数据溢出错误（接收缓存满未及时读取） */
    DRV_UART_ERROR_FRAME,           /**< 帧错误（数据帧格式异常） */
    DRV_UART_ERROR_PARITY,          /**< 奇偶校验错误 */
    DRV_UART_ERROR_NOISE,           /**< 噪声错误（接收数据受干扰） */
    DRV_UART_ERROR_DMA              /**< DMA传输错误（DMA读写异常） */
} drv_uart_error_e;

/**
 * @brief UART配置结构体（应用层传入，驱动层仅读取）
 */
typedef struct {
    drv_uart_port_e   port;                     /**< 必选：UART端口（DRV_UART_PORT_USART0~DRV_UART_PORT_UART4） */
    uint32_t      baudrate;                 /**< 必选：波特率（如9600、115200、1000000） */

    /* 基础接收缓存（必选，无论是否启用DMA/RingBuffer） */
    uint8_t       *rx_buf;                  /**< 应用层分配的基础接收缓存指针（非空） */
    uint16_t      rx_buf_size;              /**< 应用层指定的基础接收缓存大小（>0） */

    /* DMA接收相关（可选，仅use_dma_rx=true时生效） */
    uint8_t       *dma_rx_buf;              /**< 应用层分配的DMA接收缓冲区指针（启用DMA时非空） */
    uint16_t      dma_rx_buf_size;          /**< 应用层指定的DMA接收缓冲区大小（启用DMA时>0） */

    /* RingBuffer相关（可选，仅use_ringbuf=true时生效） */
    ringbuf_t     *ringbuf;                 /**< 应用层初始化完成的RingBuffer指针（启用时非空） */

    /* 功能开关（应用层自由选择，默认均为false） */
    bool          use_dma_rx;               /**< 是否启用DMA接收 */
    bool          use_idle;                 /**< 是否启用IDLE空闲帧中断（配合DMA接收效果最佳） */
    bool          use_ringbuf;              /**< 是否启用RingBuffer缓冲 */
    bool          use_dma_tx;               /**< 是否启用DMA发送 */
    bool          use_tx_mutex;             /**< 是否启用发送互斥锁（单任务调用时可禁用，节省RAM） */
    bool          is_wakeup_capable;        /**< 是否为低功耗唤醒串口（true=唤醒串口，false=普通串口） */

    /* 回调函数（可选，按需配置，未配置则不触发回调） */
    void (*rx_callback)(drv_uart_port_e port, uint16_t len);    /**< 接收完成回调（一帧数据到达触发）
                                                                 @note 在中断上下文中调用，必须快速执行，不能阻塞或调用FreeRTOS API（非FromISR版本） */
    void (*error_callback)(drv_uart_port_e port, drv_uart_error_e err);  /**< 错误回调（检测到错误时触发）
                                                                     @note 在中断上下文中调用，必须快速执行，不能阻塞或调用FreeRTOS API（非FromISR版本） */
} drv_uart_config_t;

/*********************************************************************
 * 接口函数声明
 *********************************************************************/

/*********************************************************************
 * @brief   注册UART端口
 * @param   config  UART配置结构体指针（应用层传入）
 * @return  0表示成功，-1表示失败（参数错误或端口已注册）
 * @note    应用层需确保配置参数合法，所有内存资源由应用层分配管理
 *********************************************************************/
int drv_uart_register(const drv_uart_config_t *config);

/*********************************************************************
 * @brief   卸载UART端口
 * @param   port    UART端口号
 * @return  0表示成功，-1表示失败（端口未初始化）
 * @note    释放驱动内部资源，应用层传入的内存由应用层自行管理
 *********************************************************************/
int drv_uart_deinit(drv_uart_port_e port);

/*********************************************************************
 * @brief   发送数据
 * @param   port    UART端口号
 * @param   data    待发送数据指针
 * @param   len     待发送数据长度（字节）
 * @return  实际发送的字节数，-1表示失败
 * @note    线程安全，支持普通发送和DMA发送（根据配置自动选择）
 *********************************************************************/
int drv_uart_send(drv_uart_port_e port, const uint8_t *data, uint16_t len);

/*********************************************************************
 * @brief   读取数据
 * @param   port    UART端口号
 * @param   data    读取数据存放指针
 * @param   len     期望读取的数据长度（字节）
 * @return  实际读取的字节数，-1表示失败
 * @note    从RingBuffer或接收缓存中读取数据
 *********************************************************************/
int drv_uart_read(drv_uart_port_e port, uint8_t *data, uint16_t len);

/*********************************************************************
 * @brief   挂起UART端口（低功耗）
 * @param   port    UART端口号
 * @return  0表示成功，-1表示失败（端口状态不允许挂起）
 * @note    唤醒串口仅关闭TX，普通串口彻底关闭硬件
 *********************************************************************/
int drv_uart_suspend(drv_uart_port_e port);

/*********************************************************************
 * @brief   恢复UART端口
 * @param   port    UART端口号
 * @return  0表示成功，-1表示失败（端口状态不允许恢复）
 * @note    恢复挂起的UART端口到活跃状态
 *********************************************************************/
int drv_uart_resume(drv_uart_port_e port);

/*********************************************************************
 * @brief   关闭UART端口
 * @param   port    UART端口号
 * @return  0表示成功，-1表示失败（端口未初始化）
 * @note    彻底关闭硬件，可通过resume恢复，不释放控制实例
 *********************************************************************/
int drv_uart_shutdown(drv_uart_port_e port);

/*********************************************************************
 * @brief   获取UART端口状态
 * @param   port    UART端口号
 * @return  UART状态枚举值，-1表示失败（端口无效）
 * @note    用于调试和问题排查
 *********************************************************************/
int drv_uart_get_state(drv_uart_port_e port);

/*********************************************************************
 * @brief   查询可读取的接收数据长度
 * @param   port    UART端口号
 * @return  可读取的字节数，-1表示失败（端口无效）
 * @note    应用层可通过此接口查询有多少数据待读取，避免无效调用uart_read
 *********************************************************************/
int drv_uart_get_rx_len(drv_uart_port_e port);

/*********************************************************************
 * @brief   UART中断处理函数（统一入口）
 * @param   port    UART端口号
 * @return  无
 * @note    本函数由gd32f50x_it.c中的官方中断服务函数调用，应用层不应直接调用
 *********************************************************************/
void drv_uart_irq_handler(drv_uart_port_e port);

#ifdef __cplusplus
}
#endif

#endif /* __UART_DRIVER_H__ */
