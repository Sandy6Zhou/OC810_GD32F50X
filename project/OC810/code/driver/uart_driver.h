/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       uart_driver.h
**文件描述：       UART驱动模块接口定义
**当前版本：       V1.3
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.04.22
**修改日期：       2026.05.20
*********************************************************************
** 功能描述：       1. 提供多UART独立管理接口
**                 2. 支持5种TX发送模式（运行时独立配置）
**                 3. 支持DMA接收、IDLE空闲中断、RingBuffer（含半满中断通知防溢出）
**                 4. 支持低功耗挂起/恢复、线程安全
**                 5. 驱动层与应用层完全解耦
**                 6. 所有内存资源由应用层管理
**                 7. 集中化GPIO配置宏表，编译期选择引脚
**                 8. 支持NO_USE选项，未使用UART节省代码空间
*********************************************************************/

#ifndef __DRV_UART_H__
#define __DRV_UART_H__

#include "gd32f50x.h"
#include <stdint.h>
#include <stdbool.h>
#include "my_rb.h"
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
    #define DRV_UART_ASSERT(expr)    configASSERT(expr)
#else
    #define DRV_UART_ASSERT(expr)    ((void)0)
#endif

/*********************************************************************
 * 宏定义
 *********************************************************************/

/**
 * @brief UART驱动错误码
 */
#define DRV_UART_ERR_OK             (0)     /**< 成功 */
#define DRV_UART_ERR_FAILED         (-1)    /**< 失败 */
#define DRV_UART_ERR_TIMEOUT        (-2)    /**< 超时 */
#define DRV_UART_ERR_INVALID_PARAM  (-3)    /**< 参数错误 */
#define DRV_UART_ERR_NOT_READY      (-4)    /**< 未就绪 */

/*********************************************************************
 * UART GPIO引脚配置宏表（集中管理所有UART的GPIO分配）
 *
 * 使用说明：
 * 1. 修改 DRV_USARTx_GPIO_SEL 宏即可切换UART使用的GPIO引脚
 * 2. 设置为 NO_USE 表示该UART未使用，可节省代码空间
 * 3. 配置错误会在编译期报错（#error）
 * 4. 运行时检查：如果应用层尝试初始化NO_USE的UART，drv_uart_init()会返回错误
 *
 * GPIO选项编号规则：
 *   0U - 未使用（NO_USE）
 *   1U - 默认引脚
 *   2U - 复用引脚1
 *   3U - 复用引脚2（部分UART支持）
 *********************************************************************/

/* ========== USART0 GPIO配置选项 ========== */
#define DRV_USART0_NO_USE           (0U)    /**< 未使用（节省代码空间） */
#define DRV_USART0_GPIO_PA9_PA10    (1U)    /**< PA9(TX), PA10(RX) - 复用引脚(TX->AF0 RX->AF0) */
#define DRV_USART0_GPIO_PB6_PB7     (2U)    /**< PB6(TX), PB7(RX) - 复用引脚(TX->AF0 RX->AF0) */

/* ========== USART1 GPIO配置选项 ========== */
#define DRV_USART1_NO_USE           (0U)    /**< 未使用（节省代码空间） */
#define DRV_USART1_GPIO_PA2_PA3     (1U)    /**< PA2(TX), PA3(RX) - 复用引脚(TX->AF0 RX->AF0) */
#define DRV_USART1_GPIO_PD5_PD6     (2U)    /**< PD5(TX), PD6(RX) - 复用引脚(TX->AF0 RX->AF0) */

/* ========== UART2 GPIO配置选项 ========== */
#define DRV_UART2_NO_USE            (0U)    /**< 未使用（节省代码空间） */
#define DRV_UART2_GPIO_PB10_PB11    (1U)    /**< PB10(TX), PB11(RX) - 复用引脚(TX->AF1 RX->AF1) */
#define DRV_UART2_GPIO_PC10_PC11    (2U)    /**< PC10(TX), PC11(RX) - 复用引脚(TX->AF0 RX->AF0) */
#define DRV_UART2_GPIO_PD8_PD9      (3U)    /**< PD8(TX), PD9(RX) - 复用引脚(TX->AF0 RX->AF0) */

/* ========== UART3 GPIO配置选项 ========== */
#define DRV_UART3_NO_USE            (0U)    /**< 未使用（节省代码空间） */
#define DRV_UART3_GPIO_PC10_PC11    (1U)    /**< PC10(TX), PC11(RX) - 复用引脚(TX->AF1 RX->AF1) */

/* ========== UART4 GPIO配置选项 ========== */
#define DRV_UART4_NO_USE            (0U)    /**< 未使用（节省代码空间） */
#define DRV_UART4_GPIO_PC12_PD2     (1U)    /**< PC12(TX), PD2(RX) - 复用引脚(TX->AF1 RX->AF1) */

/*********************************************************************
 * 用户配置区：选择每个UART使用的GPIO引脚组合
 * 修改此处的值即可切换UART的GPIO引脚，无需修改驱动代码
 *********************************************************************/

/** USART0 GPIO选择（修改此值切换引脚） */
#ifndef DRV_USART0_GPIO_SEL
#define DRV_USART0_GPIO_SEL         DRV_USART0_GPIO_PA9_PA10
#endif

/** USART1 GPIO选择 */
#ifndef DRV_USART1_GPIO_SEL
#define DRV_USART1_GPIO_SEL         DRV_USART1_GPIO_PA2_PA3
#endif

/** UART2 GPIO选择 */
#ifndef DRV_UART2_GPIO_SEL
#define DRV_UART2_GPIO_SEL          DRV_UART2_GPIO_PB10_PB11
#endif

/** UART3 GPIO选择 */
#ifndef DRV_UART3_GPIO_SEL
#define DRV_UART3_GPIO_SEL          DRV_UART3_GPIO_PC10_PC11
#endif

/** UART4 GPIO选择 */
#ifndef DRV_UART4_GPIO_SEL
#define DRV_UART4_GPIO_SEL          DRV_UART4_GPIO_PC12_PD2
#endif

/** 发送超时时间（毫秒） */
#ifndef UART_TX_TIMEOUT_MS
#define UART_TX_TIMEOUT_MS      (1000U)   /**< 发送超时时间（毫秒） */
#endif

/** 发送互斥锁超时时间（毫秒），0表示永久等待 */
#ifndef DRV_UART_TX_MUTEX_TIMEOUT_MS
#define DRV_UART_TX_MUTEX_TIMEOUT_MS      (1000U)  /* 默认1秒超时 */
#endif

/** 接收模式定义（注册时确定，中断中使用） */
#define DRV_UART_RX_MODE_DMA_RINGBUF      (0x01U)  /**< DMA接收 + RingBuffer模式 */
#define DRV_UART_RX_MODE_DMA_RXBUF        (0x02U)  /**< DMA接收 + rx_buf模式 */
#define DRV_UART_RX_MODE_NODMA_RINGBUF    (0x03U)  /**< 非DMA接收 + RingBuffer模式 */
#define DRV_UART_RX_MODE_NODMA_RXBUF      (0x04U)  /**< 非DMA接收 + rx_buf模式 */

/* 中断使能标志 */
/** RXNE中断使能标志 */
#define DRV_UART_IRQ_RBNE                 (0x01U)
/** IDLE中断使能标志 */
#define DRV_UART_IRQ_IDLE                 (0x02U)
/** 错误中断使能标志 */
#define DRV_UART_IRQ_ERR                  (0x04U)

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
    DRV_UART_ERROR_DMA,             /**< DMA传输错误（DMA读写异常） */
    DRV_UART_ERROR_TIMEOUT          /**< 超时错误（发送/接收超时） */
} drv_uart_error_e;

/**
 * @brief UART发送模式枚举
 */
typedef enum
{
    UART_TX_MODE_POLLING = 0,       /**< 轮询发送（CPU阻塞等待） */
    UART_TX_MODE_INTERRUPT,         /**< 中断发送（TXE中断，无需DMA通道） */
    UART_TX_MODE_DMA_SYNC,          /**< DMA同步发送（查询硬件标志） */
    UART_TX_MODE_DMA_ASYNC,         /**< DMA异步发送（FreeRTOS信号量，推荐） */
    UART_TX_MODE_DMA_DUAL_BUF       /**< DMA双缓冲循环发送（适合大数据流） */
} drv_uart_tx_mode_e;

/**
 * @brief DMA双缓冲状态枚举
 */
typedef enum
{
    DRV_UART_DMA_BUF_IDLE = 0,        /**< 缓冲区空闲 */
    DRV_UART_DMA_BUF_PREPARING,       /**< 准备数据中 */
    DRV_UART_DMA_BUF_TX,              /**< DMA发送中 */
    DRV_UART_DMA_BUF_WAIT_SWITCH      /**< 等待切换 */
} drv_uart_dma_buf_state_e;

/**
 * @brief DMA双缓冲控制结构（仅DUAL_BUF模式使用）
 */
typedef struct
{
    uint8_t                     *dma_tx_buf;                     /**< DMA TX双缓冲区指针（应用层传入，格式[2][buf_size]） */
    uint16_t                    dma_tx_buf_size;                 /**< 单个缓冲区大小（字节） */
    drv_uart_dma_buf_state_e    buf_state[2];                    /**< 每个缓冲区状态 */
    uint8_t                     current_buf_idx;                 /**< 当前DMA使用的缓冲区索引（0或1） */
    uint16_t                    buf_fill_len[2];                 /**< 每个缓冲区已填充数据长度 */

    uint8_t                     *tx_ring_queue;                  /**< 发送环形队列指针（应用层传入） */
    uint16_t                    tx_ring_queue_size;              /**< 环形队列大小（字节） */
    uint16_t                    tx_ring_write_idx;               /**< 环形队列写指针 */
    uint16_t                    tx_ring_read_idx;                /**< 环形队列读指针 */
    uint16_t                    tx_ring_count;                   /**< 环形队列中数据量 */

    SemaphoreHandle_t           tx_ring_mutex;                   /**< 环形队列互斥锁 */
    bool                        dma_tx_active;                   /**< DMA TX是否正在运行 */
} drv_uart_ring_tx_ctrl_t;

/**
 * @brief UART配置结构体（应用层传入，驱动层仅读取）
 */
typedef struct {
    drv_uart_port_e   port;                     /**< UART端口（DRV_UART_PORT_USART0~DRV_UART_PORT_UART4） */
    uint32_t      baudrate;                 /**< 波特率（如9600、115200） */

    /* 基础接收缓存（必须配置） */
    uint8_t       *rx_buf;                  /**< 应用层分配的基础接收缓存指针（非空） */
    uint16_t      rx_buf_size;              /**< 应用层指定的基础接收缓存大小（>0） */

    /* DMA接收相关（可选，仅use_dma_rx=true时生效） */
    uint8_t       *dma_rx_buf;              /**< 应用层分配的DMA接收缓冲区指针（启用DMA时非空） */
    uint16_t      dma_rx_buf_size;          /**< 应用层指定的DMA接收缓冲区大小（启用DMA时>0） */

    /* RingBuffer相关（可选，仅use_ringbuf=true时生效） */
    my_rb_t       *ringbuf;                 /**< 应用层初始化完成的RingBuffer指针（启用时非空） */

    /* 功能开关（应用层自由选择，默认均为false） */
    bool          use_dma_rx;               /**< 是否启用DMA接收 */
    bool          use_idle;                 /**< 是否启用IDLE空闲帧中断（配合DMA接收效果最佳） */
    bool          use_ringbuf;              /**< 是否启用RingBuffer缓冲 */
    bool          use_dma_tx;               /**< 是否启用DMA发送 */
    drv_uart_tx_mode_e tx_mode;             /**< 发送模式（运行时配置） */
    bool          use_tx_mutex;             /**< 是否启用发送互斥锁（单任务调用时可禁用，节省RAM） */
    bool          is_wakeup_capable;        /**< 是否为低功耗唤醒串口（true=唤醒串口，false=普通串口） */

    /* DMA TX双缓冲相关（仅use_dma_tx=true且tx_mode=UART_TX_MODE_DMA_DUAL_BUF时生效） */
    uint8_t      *dma_tx_buf;               /**< 应用层分配的DMA TX双缓冲区指针 [2][buf_size] */
    uint16_t      dma_tx_buf_size;          /**< 单个DMA TX缓冲区大小（字节） */
    uint8_t      *tx_ring_queue;            /**< 应用层分配的发送环形队列指针 */
    uint16_t      tx_ring_queue_size;       /**< 发送环形队列大小（字节） */

    /* 中断发送相关（仅tx_mode=UART_TX_MODE_INTERRUPT时生效） */
    uint8_t      *tx_buf;                   /**< 应用层分配的TX发送缓冲区指针 */
    uint16_t      tx_buf_size;              /**< TX发送缓冲区大小（字节） */

    /* 回调函数（可选，按需配置，未配置则不触发回调） */
    void (*rx_callback)(drv_uart_port_e port, uint16_t len);    /**< 接收完成回调（一帧数据到达触发）
                                                                 @note 在中断上下文中调用，必须快速执行，不能阻塞或调用FreeRTOS API（非FromISR版本） */
    void (*error_callback)(drv_uart_port_e port, drv_uart_error_e err);  /**< 错误回调（检测到错误时触发）
                                                                     @note 在中断上下文中调用，必须快速执行，不能阻塞或调用FreeRTOS API（非FromISR版本） */
    void (*tx_callback)(drv_uart_port_e port, uint16_t len);    /**< 发送完成回调（仅中断发送模式使用）
                                                                 @note 在中断上下文中调用，必须快速执行，不能阻塞或调用FreeRTOS API（非FromISR版本） */
} drv_uart_config_t;

/*********************************************************************
 * 接口函数声明
 *********************************************************************/

/*********************************************************************
 * @brief   初始化UART端口
 * @param   config  UART配置结构体指针（应用层传入）
 * @return  0表示成功，-1表示失败（参数错误或端口已初始化）
 * @note    应用层需确保配置参数合法，所有内存资源由应用层分配管理
 *********************************************************************/
int drv_uart_init(const drv_uart_config_t *config);

/*********************************************************************
 * @brief   反初始化UART端口
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
 * @note    线程安全，根据配置的tx_mode自动选择发送方式
 *********************************************************************/
int drv_uart_send(drv_uart_port_e port, const uint8_t *data, uint16_t len);

/*********************************************************************
 * @brief   读取数据
 * @param   port    UART端口号
 * @param   data    读取数据存放指针
 * @param   len     期望读取的数据长度（字节）
 * @return  实际读取的字节数，-1表示失败
 * @note    从接收缓冲区或RingBuffer中读取数据
 *********************************************************************/
int drv_uart_read(drv_uart_port_e port, uint8_t *data, uint16_t len);

/*********************************************************************
 * @brief   挂起UART端口（低功耗）
 * @param   port    UART端口号
 * @return  0表示成功，-1表示失败（端口状态不允许挂起）
 * @note    唤醒串口仅关闭TX，普通串口关闭全部硬件
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
 * @note    彻底关闭硬件，可通过drv_uart_resume恢复
 *********************************************************************/
int drv_uart_shutdown(drv_uart_port_e port);

/*********************************************************************
 * @brief   获取UART端口状态
 * @param   port    UART端口号
 * @return  UART状态枚举值，-1表示失败（端口无效）
 * @note    用于调试和状态查询
 *********************************************************************/
int drv_uart_get_state(drv_uart_port_e port);

/*********************************************************************
 * @brief   查询可读取的接收数据长度
 * @param   port    UART端口号
 * @return  可读取的字节数，-1表示失败（端口无效）
 * @note    应用层可通过此接口查询待读取数据量，避免无效调用drv_uart_read
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
