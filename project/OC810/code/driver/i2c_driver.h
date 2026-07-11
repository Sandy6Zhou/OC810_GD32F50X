/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       i2c_driver.h
**文件描述：       I2C驱动模块接口定义
**当前版本：       V1.1
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.05.20
*********************************************************************
** 功能描述：       1. 提供多I2C独立管理接口
**                 2. 支持Master轮询模式（FreeRTOS taskYIELD优化）
**                 3. 支持7位地址、8位/16位寄存器地址
**                 4. 支持GPIO配置宏表、电源管理
**                 5. 驱动层与应用层完全解耦
**                 6. 所有内存资源由应用层管理
**                 7. 适用于传感器读取（Gsensor、温湿度等）
*********************************************************************/

#ifndef __I2C_DRIVER_H__
#define __I2C_DRIVER_H__

#include "gd32f50x.h"
#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * 配置参数
 *********************************************************************/

/* I2C写寄存器最大长度（含寄存器地址） */
#define DRV_I2C_MAX_WRITE_LEN      (32U)   /**< 最大写寄存器长度（字节） */

/*********************************************************************
 * 日志宏定义（便于移植和独立控制）
 *********************************************************************/

/* 日志开关（1=开启，0=关闭） */
#define DRV_I2C_LOG_ENABLE         (1U)

/* 日志级别定义 */
#define DRV_I2C_LOG_LEVEL_ERROR    (0U)    /**< 错误日志 */
#define DRV_I2C_LOG_LEVEL_WARN     (1U)    /**< 警告日志 */
#define DRV_I2C_LOG_LEVEL_INFO     (2U)    /**< 信息日志 */
#define DRV_I2C_LOG_LEVEL_DEBUG    (3U)    /**< 调试日志 */

/* 当前日志级别（可通过修改此值控制日志输出详细程度） */
#define DRV_I2C_LOG_CURRENT_LEVEL  (DRV_I2C_LOG_LEVEL_INFO)

/* 日志输出宏（可根据项目实际情况修改底层实现） */
#if DRV_I2C_LOG_ENABLE == 1U

/* 根据项目实际使用的日志系统修改此处 */
#include "my_log.h"

#define DRV_I2C_LOGE(fmt, ...)    MY_LOG_E(fmt, ##__VA_ARGS__)
#define DRV_I2C_LOGW(fmt, ...)    MY_LOG_W(fmt, ##__VA_ARGS__)
#define DRV_I2C_LOGI(fmt, ...)    MY_LOG_I(fmt, ##__VA_ARGS__)
#define DRV_I2C_LOGD(fmt, ...)    do { \
                                        if (DRV_I2C_LOG_CURRENT_LEVEL >= DRV_I2C_LOG_LEVEL_DEBUG) \
                                        { \
                                            MY_LOG_D(fmt, ##__VA_ARGS__); \
                                        } \
                                    } while(0)

#else

#define DRV_I2C_LOGE(fmt, ...)
#define DRV_I2C_LOGW(fmt, ...)
#define DRV_I2C_LOGI(fmt, ...)
#define DRV_I2C_LOGD(fmt, ...)

#endif /* DRV_I2C_LOG_ENABLE */

/*********************************************************************
 * 宏定义
 *********************************************************************/

/**
 * @brief I2C驱动错误码
 */
#define DRV_I2C_ERR_OK             (0)     /**< 成功 */
#define DRV_I2C_ERR_FAILED         (-1)    /**< 失败 */
#define DRV_I2C_ERR_TIMEOUT        (-2)    /**< 超时 */
#define DRV_I2C_ERR_INVALID_PARAM  (-3)    /**< 参数错误 */
#define DRV_I2C_ERR_NOT_READY      (-4)    /**< 未就绪 */
#define DRV_I2C_ERR_BUS_BUSY       (-5)    /**< 总线忙 */
#define DRV_I2C_ERR_NACK           (-6)    /**< NACK错误 */

/*********************************************************************
 * GPIO配置宏表（集中管理）
 *********************************************************************/

/** I2C0 GPIO配置选项 */
#define DRV_I2C0_NO_USE            (0U)    /**< 未使用 */
#define DRV_I2C0_GPIO_PB6_PB7      (1U)    /**< PB6(SCL), PB7(SDA) - AF1 */
#define DRV_I2C0_GPIO_PB8_PB9      (2U)    /**< PB8(SCL), PB9(SDA) - AF0 */

/** I2C1 GPIO配置选项 */
#define DRV_I2C1_NO_USE            (0U)    /**< 未使用 */
#define DRV_I2C1_GPIO_PB10_PB11    (1U)    /**< PB10(SCL), PB11(SDA) - AF2 */

/*********************************************************************
 * 用户配置区：选择每个I2C使用的GPIO引脚组合
 * 修改此处的值即可切换I2C的GPIO引脚，无需修改驱动代码
 *********************************************************************/

/** I2C0 GPIO选择（修改此值切换引脚） */
#ifndef DRV_I2C0_GPIO_SEL
#define DRV_I2C0_GPIO_SEL          DRV_I2C0_GPIO_PB6_PB7
#endif

/** I2C1 GPIO选择 */
#ifndef DRV_I2C1_GPIO_SEL
#define DRV_I2C1_GPIO_SEL          DRV_I2C1_GPIO_PB10_PB11
#endif

/*********************************************************************
 * 枚举类型定义
 *********************************************************************/

/**
 * @brief I2C端口枚举
 */
typedef enum
{
    DRV_I2C_PORT_I2C0 = 0,         /**< I2C0端口 */
    DRV_I2C_PORT_I2C1,             /**< I2C1端口 */
    DRV_I2C_PORT_MAX               /**< I2C端口最大值 */
} drv_i2c_port_e;

/**
 * @brief I2C速率枚举
 */
typedef enum
{
    DRV_I2C_SPEED_100K = 0,        /**< 标准模式 100kHz */
    DRV_I2C_SPEED_400K             /**< 快速模式 400kHz */
} drv_i2c_speed_e;

/**
 * @brief 寄存器地址模式枚举
 */
typedef enum
{
    DRV_I2C_REG_ADDR_8BIT = 0,     /**< 8位寄存器地址（如AT24C02、SHT30） */
    DRV_I2C_REG_ADDR_16BIT         /**< 16位寄存器地址（如AT24C64/128/256） */
} drv_i2c_reg_addr_mode_e;

/**
 * @brief I2C错误类型枚举（用于中断回调）
 */
typedef enum
{
    DRV_I2C_ERR_TYPE_NONE = 0,     /**< 无错误 */
    DRV_I2C_ERR_TYPE_BUS,          /**< 总线错误 */
    DRV_I2C_ERR_TYPE_ARBITRATION,  /**< 仲裁丢失 */
    DRV_I2C_ERR_TYPE_NACK,         /**< NACK错误 */
    DRV_I2C_ERR_TYPE_OVERRUN       /**< 溢出错误 */
} drv_i2c_error_type_e;

/**
 * @brief I2C状态枚举
 */
typedef enum
{
    DRV_I2C_STATE_UNINIT = 0,      /**< 未初始化 */
    DRV_I2C_STATE_INIT,            /**< 已初始化 */
    DRV_I2C_STATE_ACTIVE,          /**< 活跃状态 */
    DRV_I2C_STATE_SUSPENDED        /**< 挂起状态 */
} drv_i2c_state_e;

/*********************************************************************
 * 数据结构定义
 *********************************************************************/

/**
 * @brief I2C配置结构体
 */
typedef struct
{
    drv_i2c_port_e          port;           /**< I2C端口号 */
    drv_i2c_speed_e         speed;          /**< 通信速率 */
    drv_i2c_reg_addr_mode_e reg_addr_mode;  /**< 寄存器地址长度（8位/16位） */

    uint32_t                timeout_ms;     /**< 超时时间（毫秒） */
    bool                    use_mutex;      /**< 启用互斥锁 */
} drv_i2c_config_t;

/*********************************************************************
 * 接口函数声明
 *********************************************************************/

/**
 * @brief   初始化I2C端口
 * @param   config  配置结构体指针
 * @return  DRV_I2C_ERR_OK: 成功，其他: 失败
 * @note    应用层必须确保config指针有效
 */
int drv_i2c_init(const drv_i2c_config_t *config);

/**
 * @brief   反初始化I2C端口
 * @param   port    I2C端口号
 * @return  DRV_I2C_ERR_OK: 成功，其他: 失败
 */
int drv_i2c_deinit(drv_i2c_port_e port);

/**
 * @brief   Master发送数据（阻塞）
 * @param   port        I2C端口号
 * @param   slave_addr  从机地址（7位原始地址）
 * @param   data        发送数据缓冲区
 * @param   len         数据长度
 * @return  DRV_I2C_ERR_OK: 成功，其他: 失败
 * @note    slave_addr为7位地址，驱动内部自动左移1位
 */
int drv_i2c_master_send(drv_i2c_port_e port, uint8_t slave_addr,
                        const uint8_t *data, uint16_t len);

/**
 * @brief   Master接收数据（阻塞）
 * @param   port        I2C端口号
 * @param   slave_addr  从机地址（7位原始地址）
 * @param   data        接收数据缓冲区
 * @param   len         数据长度
 * @return  DRV_I2C_ERR_OK: 成功，其他: 失败
 */
int drv_i2c_master_recv(drv_i2c_port_e port, uint8_t slave_addr,
                        uint8_t *data, uint16_t len);

/**
 * @brief   Master写寄存器（自动处理8位/16位寄存器地址）
 * @param   port        I2C端口号
 * @param   slave_addr  从机地址（7位原始地址）
 * @param   reg_addr    寄存器地址
 * @param   data        发送数据缓冲区
 * @param   len         数据长度
 * @return  DRV_I2C_ERR_OK: 成功，其他: 失败
 */
int drv_i2c_write_reg(drv_i2c_port_e port, uint8_t slave_addr,
                      uint32_t reg_addr, const uint8_t *data, uint16_t len);

/**
 * @brief   Master读寄存器（自动处理8位/16位寄存器地址）
 * @param   port        I2C端口号
 * @param   slave_addr  从机地址（7位原始地址）
 * @param   reg_addr    寄存器地址
 * @param   data        接收数据缓冲区
 * @param   len         数据长度
 * @return  DRV_I2C_ERR_OK: 成功，其他: 失败
 */
int drv_i2c_read_reg(drv_i2c_port_e port, uint8_t slave_addr,
                     uint32_t reg_addr, uint8_t *data, uint16_t len);

/**
 * @brief   挂起I2C（低功耗）
 * @param   port    I2C端口号
 * @return  DRV_I2C_ERR_OK: 成功，其他: 失败
 */
int drv_i2c_suspend(drv_i2c_port_e port);

/**
 * @brief   恢复I2C
 * @param   port    I2C端口号
 * @return  DRV_I2C_ERR_OK: 成功，其他: 失败
 */
int drv_i2c_resume(drv_i2c_port_e port);

#ifdef __cplusplus
}
#endif

#endif /* __I2C_DRIVER_H__ */
