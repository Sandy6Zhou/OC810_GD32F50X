/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       i2c_driver.c
**文件描述：       I2C驱动模块实现文件
**当前版本：       V1.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.05.20
*********************************************************************
** 功能描述：       1. I2C Master模式驱动实现
**                 2. 支持轮询传输模式
**                 3. 支持GPIO配置宏表
**                 4. 支持电源管理
*********************************************************************/

#include "i2c_driver.h"
#include "gd32f50x_i2c.h"
#include "gd32f50x_rcu.h"
#include "gd32f50x_gpio.h"
#include "gpio_driver.h"
#include "my_log.h"
#include <string.h>

/*********************************************************************
 * 内部宏定义
 *********************************************************************/

#if DRV_I2C0_GPIO_SEL == DRV_I2C0_GPIO_PB6_PB7
    #define DRV_I2C0_SCL_PIN        DRV_GPIO_PIN_6
    #define DRV_I2C0_SDA_PIN        DRV_GPIO_PIN_7
    #define DRV_I2C0_GPIO_PORT      DRV_GPIO_PORT_B
    #define DRV_I2C0_GPIO_AF        DRV_GPIO_AF_1
#elif DRV_I2C0_GPIO_SEL == DRV_I2C0_GPIO_PB8_PB9
    #define DRV_I2C0_SCL_PIN        DRV_GPIO_PIN_8
    #define DRV_I2C0_SDA_PIN        DRV_GPIO_PIN_9
    #define DRV_I2C0_GPIO_PORT      DRV_GPIO_PORT_B
    #define DRV_I2C0_GPIO_AF        DRV_GPIO_AF_0
#endif

#if DRV_I2C1_GPIO_SEL == DRV_I2C1_GPIO_PB10_PB11
    #define DRV_I2C1_SCL_PIN        DRV_GPIO_PIN_10
    #define DRV_I2C1_SDA_PIN        DRV_GPIO_PIN_11
    #define DRV_I2C1_GPIO_PORT      DRV_GPIO_PORT_B
    #define DRV_I2C1_GPIO_AF        DRV_GPIO_AF_2
#endif

/*********************************************************************
 * 内部数据结构
 *********************************************************************/

/**
 * @brief I2C端口控制块
 */
typedef struct
{
    drv_i2c_state_e     state;          /**< 端口状态 */
    drv_i2c_speed_e     speed;          /**< 通信速率 */
    drv_i2c_reg_addr_mode_e reg_addr_mode;  /**< 寄存器地址模式 */
    uint32_t            timeout_ms;     /**< 超时时间 */
    SemaphoreHandle_t   mutex;          /**< 互斥锁 */

    /* GD32硬件资源 */
    uint32_t            i2c_periph;     /**< I2C外设基地址 */
    uint32_t            rcu_i2c;        /**< I2C时钟 */

    /* 错误记录 */
    int                 last_error;     /**< 最后一次错误码 */
} drv_i2c_ctrl_t;

/*********************************************************************
 * 全局变量
 *********************************************************************/

/** I2C端口控制块数组 */
static drv_i2c_ctrl_t s_i2c_ctrl[DRV_I2C_PORT_MAX];

/*********************************************************************
 * 内部辅助函数声明
 *********************************************************************/

static int _drv_i2c_gpio_init(drv_i2c_port_e port);
static int _drv_i2c_gpio_deinit(drv_i2c_port_e port);
static int _drv_i2c_periph_init(drv_i2c_port_e port, const drv_i2c_config_t *config);
static int _drv_i2c_wait_flag(drv_i2c_port_e port, uint32_t flag, int set_state, uint32_t timeout_ms);
static int _drv_i2c_check_busy(drv_i2c_port_e port);
static int _drv_i2c_transfer_send_only(uint32_t i2c_periph, drv_i2c_port_e port,
                                       uint8_t slave_addr, const uint8_t *data, uint16_t len,
                                       bool send_stop);
static int _drv_i2c_transfer_recv_only(uint32_t i2c_periph, drv_i2c_port_e port,
                                       uint8_t slave_addr, uint8_t *data, uint16_t len,
                                       bool send_stop);
static int _drv_i2c_transfer_send_recv(uint32_t i2c_periph, drv_i2c_port_e port,
                                       uint8_t slave_addr, const uint8_t *send_data, uint16_t send_len,
                                       uint8_t *recv_data, uint16_t recv_len,
                                       bool send_stop);
static int _drv_i2c_master_transfer(drv_i2c_port_e port, uint8_t slave_addr,
                            const uint8_t *send_data, uint16_t send_len,
                            uint8_t *recv_data, uint16_t recv_len,
                            bool send_stop);

/*********************************************************************
 * 内部辅助函数实现
 *********************************************************************/

/*********************************************************************
 * @brief   初始化I2C GPIO
 * @param   port    I2C端口号
 * @return  DRV_I2C_ERR_OK: 成功，其他: 失败
 * @note    使用gpio_driver统一管理GPIO资源
 *********************************************************************/
static int _drv_i2c_gpio_init(drv_i2c_port_e port)
{
    drv_gpio_config_t gpio_cfg = {0};
    drv_gpio_pin_e scl_pin, sda_pin;
    drv_gpio_af_e gpio_af;

    /* 根据端口选择引脚和复用功能 */
    if (port == DRV_I2C_PORT_I2C0)
    {
#if DRV_I2C0_GPIO_SEL == DRV_I2C0_NO_USE
        DRV_I2C_LOGE("I2C0 GPIO not configured (NO_USE)");
        return DRV_I2C_ERR_INVALID_PARAM;
#endif
        scl_pin = DRV_I2C0_SCL_PIN;
        sda_pin = DRV_I2C0_SDA_PIN;
        gpio_cfg.port = DRV_I2C0_GPIO_PORT;  /* GPIOB -> DRV_GPIO_PORT_B */
        gpio_af = DRV_I2C0_GPIO_AF;
    }
    else if (port == DRV_I2C_PORT_I2C1)
    {
#if DRV_I2C1_GPIO_SEL == DRV_I2C1_NO_USE
        DRV_I2C_LOGE("I2C1 GPIO not configured (NO_USE)");
        return DRV_I2C_ERR_INVALID_PARAM;
#endif
        scl_pin = DRV_I2C1_SCL_PIN;
        sda_pin = DRV_I2C1_SDA_PIN;
        gpio_cfg.port = (drv_gpio_port_e)DRV_I2C1_GPIO_PORT;  /* GPIOB -> DRV_GPIO_PORT_B */
        gpio_af = DRV_I2C1_GPIO_AF;
    }
    else
    {
        DRV_I2C_LOGE("Invalid I2C port: %d", port);
        return DRV_I2C_ERR_INVALID_PARAM;
    }

    /* 配置SCL引脚：复用功能、开漏输出、高速、上拉 */
    gpio_cfg.pin = scl_pin;
    gpio_cfg.mode = DRV_GPIO_MODE_AF;
    gpio_cfg.otype = DRV_GPIO_OTYPE_OD;
    gpio_cfg.speed = DRV_GPIO_SPEED_LEVEL3;
    gpio_cfg.pupd = DRV_GPIO_PUPD_PULLUP;
    gpio_cfg.af = gpio_af;
    if (drv_gpio_init(&gpio_cfg) != DRV_GPIO_OK)
    {
        DRV_I2C_LOGE("I2C%d SCL GPIO init failed", port);
        drv_gpio_deinit(gpio_cfg.port, scl_pin);  /* 回滚SCL配置 */
        return DRV_I2C_ERR_FAILED;
    }

    /* 配置SDA引脚：复用功能、开漏输出、高速、上拉 */
    gpio_cfg.pin = sda_pin;
    if (drv_gpio_init(&gpio_cfg) != DRV_GPIO_OK)
    {
        DRV_I2C_LOGE("I2C%d SDA GPIO init failed", port);
        drv_gpio_deinit(gpio_cfg.port, sda_pin);  /* 回滚SDA配置 */
        return DRV_I2C_ERR_FAILED;
    }

    DRV_I2C_LOGI("I2C%d GPIO initialized (SCL=0x%X, SDA=0x%X)", port, scl_pin, sda_pin);
    return DRV_I2C_ERR_OK;
}

/*********************************************************************
 * @brief   反初始化I2C GPIO
 * @param   port    I2C端口号
 * @return  DRV_I2C_ERR_OK: 成功，其他: 失败
 * @note    使用gpio_driver统一管理GPIO资源
 *********************************************************************/
static int _drv_i2c_gpio_deinit(drv_i2c_port_e port)
{
    drv_gpio_pin_e scl_pin, sda_pin;
    drv_gpio_port_e gpio_port;

    if (port == DRV_I2C_PORT_I2C0)
    {
#if DRV_I2C0_GPIO_SEL == DRV_I2C0_NO_USE
        DRV_I2C_LOGE("I2C0 GPIO not configured (NO_USE)");
        return DRV_I2C_ERR_INVALID_PARAM;
#endif
        scl_pin = (drv_gpio_pin_e)DRV_I2C0_SCL_PIN;
        sda_pin = (drv_gpio_pin_e)DRV_I2C0_SDA_PIN;
        gpio_port = (drv_gpio_port_e)DRV_I2C0_GPIO_PORT;  /* GPIOB -> DRV_GPIO_PORT_B */
    }
    else if (port == DRV_I2C_PORT_I2C1)
    {
#if DRV_I2C1_GPIO_SEL == DRV_I2C1_NO_USE
        DRV_I2C_LOGE("I2C1 GPIO not configured (NO_USE)");
        return DRV_I2C_ERR_INVALID_PARAM;
#endif
        scl_pin = (drv_gpio_pin_e)DRV_I2C1_SCL_PIN;
        sda_pin = (drv_gpio_pin_e)DRV_I2C1_SDA_PIN;
        gpio_port = (drv_gpio_port_e)DRV_I2C1_GPIO_PORT;  /* GPIOB -> DRV_GPIO_PORT_B */
    }
    else
    {
        return DRV_I2C_ERR_INVALID_PARAM;
    }

    /* 使用gpio_driver反初始化SCL和SDA引脚（恢复复位状态） */
    drv_gpio_deinit(gpio_port, scl_pin);
    drv_gpio_deinit(gpio_port, sda_pin);

    DRV_I2C_LOGI("I2C%d GPIO deinitialized (SCL=0x%X, SDA=0x%X)", port, scl_pin, sda_pin);
    return DRV_I2C_ERR_OK;
}

/*********************************************************************
 * @brief   根据APB1时钟和I2C速度动态计算时序参数
 * @param   i2c_periph      I2C外设基地址
 * @param   apb1_clock_mhz  APB1时钟频率（MHz）
 * @param   speed           I2C速度模式
 *
 * @note    GD32F50x I2C时序计算公式：
 *          - I2CCLK = APB1 / (PRESC + 1)，要求 2MHz ≤ I2CCLK ≤ 50MHz
 *          - SCL_FREQ = I2CCLK / (SCLH + SCLL + 2)
 *          - t_SCLH = (SCLH + 1) / I2CCLK
 *          - t_SCLL = (SCLL + 1) / I2CCLK
 *          - t_SDADEL = (SDADEL + 1) / I2CCLK
 *          - t_SCLDEL = (SCLDEL + 1) / I2CCLK
 *
 * @note    I2C规范要求：
 *          - 标准模式（100K）：t_SCLH≥4.0μs, t_SCLL≥4.7μs, t_SDADEL≥250ns
 *          - 快速模式（400K）：t_SCLH≥0.6μs, t_SCLL≥1.3μs, t_SDADEL≥50ns
 *
 * @note    本函数基于APB1=60MHz精确计算，采用查表法：
 *          - 100KHz: PRESC=9, SCLH=26, SCLL=32, SDADEL=1, SCLDEL=2
 *          - 400KHz: PRESC=4, SCLH=10, SCLL=18, SDADEL=1, SCLDEL=2
 *********************************************************************/
static void _drv_i2c_config_timing(uint32_t i2c_periph, uint32_t apb1_clock_mhz, drv_i2c_speed_e speed)
{
    /* 时序参数表（基于APB1=60MHz） */
    static const struct {
        uint32_t presc;
        uint32_t sclh;
        uint32_t scll;
        uint32_t sdadel;
        uint32_t scldel;
        uint32_t min_sdadel_ns;
    } timing_table[] = {
        /* 100KHz: PRESC=9, I2CCLK=6MHz, 周期=166.7ns */
        {9,  26, 32, 1, 2, 250},   /* t_SCLH=4.50μs, t_SCLL=5.50μs, t_SDADEL=333ns */
        /* 400KHz: PRESC=4, I2CCLK=12MHz, 周期=83.3ns */
        {4,  10, 18, 1, 2, 60}     /* t_SCLH=916ns, t_SCLL=1583ns, t_SDADEL=166ns */
    };

    uint32_t target_freq;
    uint32_t i2cclk;
    uint32_t idx;
    uint32_t apb1_hz = apb1_clock_mhz * 1000000U;

    /* 参数校验 */
    if (apb1_clock_mhz == 0)
    {
        DRV_I2C_LOGE("Invalid APB1 clock: 0 MHz");
        return;
    }

    /* 选择时序参数 */
    if (speed == DRV_I2C_SPEED_400K)
    {
        idx = 1;
        target_freq = 400000;
    }
    else
    {
        idx = 0;
        target_freq = 100000;
    }

    /* 应用时序参数 */
    i2cclk = apb1_hz / (timing_table[idx].presc + 1);

    /* 验证I2CCLK范围 */
    if (i2cclk < 2000000 || i2cclk > 50000000)
    {
        DRV_I2C_LOGW("I2CCLK=%luHz out of recommended range [2MHz, 50MHz]", i2cclk);
    }

    /* 配置硬件寄存器 */
    i2c_timing_config(i2c_periph,
                      timing_table[idx].presc,
                      timing_table[idx].scldel,
                      timing_table[idx].sdadel);
    i2c_master_clock_config(i2c_periph,
                            timing_table[idx].sclh,
                            timing_table[idx].scll);
}

/*********************************************************************
 * @brief   初始化I2C外设
 * @param   port    I2C端口号
 * @param   config  配置结构体指针
 * @return  DRV_I2C_ERR_OK: 成功，其他: 失败
 *********************************************************************/
static int _drv_i2c_periph_init(drv_i2c_port_e port, const drv_i2c_config_t *config)
{
    uint32_t i2c_periph;
    uint32_t rcu_i2c;
    uint32_t apb1_clock;

    /* 选择I2C外设 */
    if (port == DRV_I2C_PORT_I2C0)
    {
        i2c_periph = I2C0;
        rcu_i2c = RCU_I2C0;
    }
    else if (port == DRV_I2C_PORT_I2C1)
    {
        i2c_periph = I2C1;
        rcu_i2c = RCU_I2C1;
    }
    else
    {
        return DRV_I2C_ERR_INVALID_PARAM;
    }

    /* 使能I2C时钟 */
    rcu_periph_clock_enable(rcu_i2c);

    /* 获取APB1时钟频率（MHz） */
    apb1_clock = rcu_clock_freq_get(CK_APB1) / 1000000U;

    /* 根据APB1时钟和I2C速度动态计算时序参数 */
    _drv_i2c_config_timing(i2c_periph, apb1_clock, config->speed);

    /* 配置I2C地址模式（7位地址）- 必须在i2c_enable之前 */
    i2c_address_config(i2c_periph, 0x00, I2C_ADDFORMAT_7BITS);

    /* 使能I2C（必须在所有配置完成后） */
    i2c_enable(i2c_periph);

    DRV_I2C_LOGI("I2C%d peripheral initialized, speed=%d", port, config->speed);
    return DRV_I2C_ERR_OK;
}

/*********************************************************************
 * @brief   等待I2C标志
 * @param   port        I2C端口号
 * @param   flag        等待的标志
 * @param   set_state   期望的状态（SET/RESET）
 * @param   timeout     超时计数值
 * @return  DRV_I2C_ERR_OK: 成功，DRV_I2C_ERR_TIMEOUT: 超时
 *********************************************************************/
static int _drv_i2c_wait_flag(drv_i2c_port_e port, uint32_t flag,
                               int set_state, uint32_t timeout_ms)
{
    uint32_t i2c_periph;
    TickType_t start_tick;
    TickType_t elapsed_tick;
    uint32_t spin_count = 0;  /* 忙等待计数器 */

    /* 运行时参数校验 */
    if (port >= DRV_I2C_PORT_MAX)
    {
        DRV_I2C_LOGE("Invalid port: %d", port);
        return DRV_I2C_ERR_INVALID_PARAM;
    }

    i2c_periph = s_i2c_ctrl[port].i2c_periph;
    start_tick = xTaskGetTickCount();

    if (timeout_ms == 0)
    {
        timeout_ms = 100;  /* 默认超时时间为100ms，适应大数据量传输 */
    }

    while (1)
    {
        FlagStatus status = i2c_flag_get(i2c_periph, flag);

        if ((set_state && status == SET) || (!set_state && status == RESET))
        {
            return DRV_I2C_ERR_OK;
        }

        /* 检查超时 */
        elapsed_tick = xTaskGetTickCount() - start_tick;
        if (elapsed_tick >= pdMS_TO_TICKS(timeout_ms))
        {
            DRV_I2C_LOGE("I2C%d wait flag timeout: 0x%08X, timeout=%lums",
                         port, flag, timeout_ms);
            return DRV_I2C_ERR_TIMEOUT;
        }

        /* 忙等待100次后再让出CPU，提升I2C读写性能 */
        spin_count++;
        if (spin_count >= 100)  /* 约100us后让出CPU */
        {
            vTaskDelay(pdMS_TO_TICKS(1));
            spin_count = 0;
        }
    }
}

/*********************************************************************
 * @brief   检查I2C总线是否忙
 * @param   port    I2C端口号
 * @return  DRV_I2C_ERR_OK: 空闲，DRV_I2C_ERR_BUS_BUSY: 忙
 *********************************************************************/
static int _drv_i2c_check_busy(drv_i2c_port_e port)
{
    uint32_t i2c_periph;

    /* 运行时参数校验 */
    if (port >= DRV_I2C_PORT_MAX)
    {
        DRV_I2C_LOGE("Invalid port: %d", port);
        return DRV_I2C_ERR_INVALID_PARAM;
    }

    i2c_periph = s_i2c_ctrl[port].i2c_periph;

    if (i2c_flag_get(i2c_periph, I2C_FLAG_I2CBSY) == SET)
    {
        return DRV_I2C_ERR_BUS_BUSY;
    }

    return DRV_I2C_ERR_OK;
}

/*********************************************************************
 * @brief   I2C纯发送传输（只有发送，没有接收）
 * @param   i2c_periph    I2C外设地址
 * @param   port          I2C端口号
 * @param   slave_addr    从机地址（7位）
 * @param   data          发送数据缓冲区
 * @param   len           发送数据长度
 * @param   send_stop     是否产生STOP条件
 * @return  DRV_I2C_ERR_OK: 成功，其他: 失败
 *********************************************************************/
static int _drv_i2c_transfer_send_only(uint32_t i2c_periph, drv_i2c_port_e port,
                                       uint8_t slave_addr, const uint8_t *data, uint16_t len,
                                       bool send_stop)
{
    int ret = DRV_I2C_ERR_OK;
    uint16_t i;

    /* 配置从机地址和字节数 */
    i2c_master_addressing(i2c_periph, slave_addr << 1, I2C_MASTER_TRANSMIT);
    i2c_transfer_byte_number_config(i2c_periph, len);

    /* 产生START条件 */
    i2c_start_on_bus(i2c_periph);

    /* 清除TBE标志（必须在等待TBE之前，参考官方Demo） */
    I2C_STAT(i2c_periph) |= I2C_STAT_TBE;

    /* 等待TBE */
    ret = _drv_i2c_wait_flag(port, I2C_FLAG_TBE, SET, s_i2c_ctrl[port].timeout_ms);
    if (ret != DRV_I2C_ERR_OK)
    {
        return ret;
    }

    /* 发送数据 */
    for (i = 0; i < len; i++)
    {
        i2c_data_transmit(i2c_periph, data[i]);

        /* 等待TI */
        ret = _drv_i2c_wait_flag(port, I2C_FLAG_TI, SET, s_i2c_ctrl[port].timeout_ms);
        if (ret != DRV_I2C_ERR_OK)
        {
            return ret;
        }
    }

    /* 等待TC */
    ret = _drv_i2c_wait_flag(port, I2C_FLAG_TC, SET, s_i2c_ctrl[port].timeout_ms);
    if (ret != DRV_I2C_ERR_OK)
    {
        return ret;
    }

    /* 根据send_stop决定是否产生STOP */
    if (send_stop)
    {
        i2c_stop_on_bus(i2c_periph);

        /* 等待STOP生成 */
        ret = _drv_i2c_wait_flag(port, I2C_FLAG_STPDET, SET, s_i2c_ctrl[port].timeout_ms);
        if (ret == DRV_I2C_ERR_OK)
        {
            i2c_flag_clear(i2c_periph, I2C_FLAG_STPDET);
        }
    }

    return ret;
}

/*********************************************************************
 * @brief   I2C纯接收传输（只有接收，没有发送）
 * @param   i2c_periph    I2C外设地址
 * @param   port          I2C端口号
 * @param   slave_addr    从机地址（7位）
 * @param   data          接收数据缓冲区
 * @param   len           接收数据长度
 * @param   send_stop     是否产生STOP条件
 * @return  DRV_I2C_ERR_OK: 成功，其他: 失败
 *********************************************************************/
static int _drv_i2c_transfer_recv_only(uint32_t i2c_periph, drv_i2c_port_e port,
                                       uint8_t slave_addr, uint8_t *data, uint16_t len,
                                       bool send_stop)
{
    int ret = DRV_I2C_ERR_OK;
    uint16_t i;

    /* 配置从机地址和字节数 */
    i2c_master_addressing(i2c_periph, (slave_addr << 1) | 0x01, I2C_MASTER_RECEIVE);
    i2c_transfer_byte_number_config(i2c_periph, len);

    /* 产生START条件 */
    i2c_start_on_bus(i2c_periph);

    /* 清除TBE标志（必须在等待TBE之前，参考官方Demo） */
    I2C_STAT(i2c_periph) |= I2C_STAT_TBE;

    /* 等待TBE */
    ret = _drv_i2c_wait_flag(port, I2C_FLAG_TBE, SET, s_i2c_ctrl[port].timeout_ms);
    if (ret != DRV_I2C_ERR_OK)
    {
        return ret;
    }

    /* 接收数据 */
    for (i = 0; i < len; i++)
    {
        /* 等待RBNE */
        ret = _drv_i2c_wait_flag(port, I2C_FLAG_RBNE, SET, s_i2c_ctrl[port].timeout_ms);
        if (ret != DRV_I2C_ERR_OK)
        {
            return ret;
        }

        data[i] = i2c_data_receive(i2c_periph);
    }

    /* 等待TC */
    ret = _drv_i2c_wait_flag(port, I2C_FLAG_TC, SET, s_i2c_ctrl[port].timeout_ms);
    if (ret != DRV_I2C_ERR_OK)
    {
        return ret;
    }

    /* 根据send_stop决定是否产生STOP */
    if (send_stop)
    {
        i2c_stop_on_bus(i2c_periph);

        /* 等待STOP生成 */
        ret = _drv_i2c_wait_flag(port, I2C_FLAG_STPDET, SET, s_i2c_ctrl[port].timeout_ms);
        if (ret == DRV_I2C_ERR_OK)
        {
            i2c_flag_clear(i2c_periph, I2C_FLAG_STPDET);
        }
    }

    return ret;
}

/*********************************************************************
 * @brief   I2C发送+接收传输（Repeated START）
 * @param   i2c_periph    I2C外设地址
 * @param   port          I2C端口号
 * @param   slave_addr    从机地址（7位）
 * @param   send_data     发送数据缓冲区
 * @param   send_len      发送数据长度
 * @param   recv_data     接收数据缓冲区
 * @param   recv_len      接收数据长度
 * @param   send_stop     是否产生STOP条件
 * @return  DRV_I2C_ERR_OK: 成功，其他: 失败
 *********************************************************************/
static int _drv_i2c_transfer_send_recv(uint32_t i2c_periph, drv_i2c_port_e port,
                                       uint8_t slave_addr, const uint8_t *send_data, uint16_t send_len,
                                       uint8_t *recv_data, uint16_t recv_len,
                                       bool send_stop)
{
    int ret = DRV_I2C_ERR_OK;
    uint16_t i;

    /* ===== 发送阶段 ===== */
    /* 配置从机地址和字节数 */
    i2c_master_addressing(i2c_periph, slave_addr << 1, I2C_MASTER_TRANSMIT);
    i2c_transfer_byte_number_config(i2c_periph, send_len);

    /* 产生START条件 */
    i2c_start_on_bus(i2c_periph);

    /* 清除TBE标志（必须在等待TBE之前，参考官方Demo） */
    I2C_STAT(i2c_periph) |= I2C_STAT_TBE;

    /* 等待TBE */
    ret = _drv_i2c_wait_flag(port, I2C_FLAG_TBE, SET, s_i2c_ctrl[port].timeout_ms);
    if (ret != DRV_I2C_ERR_OK)
    {
        return ret;
    }

    /* 发送数据 */
    for (i = 0; i < send_len; i++)
    {
        i2c_data_transmit(i2c_periph, send_data[i]);

        /* 等待TI */
        ret = _drv_i2c_wait_flag(port, I2C_FLAG_TI, SET, s_i2c_ctrl[port].timeout_ms);
        if (ret != DRV_I2C_ERR_OK)
        {
            return ret;
        }
    }

    /* 等待TC */
    ret = _drv_i2c_wait_flag(port, I2C_FLAG_TC, SET, s_i2c_ctrl[port].timeout_ms);
    if (ret != DRV_I2C_ERR_OK)
    {
        return ret;
    }

    /* ===== Repeated START阶段 ===== */
    /* 配置从机地址和字节数 */
    i2c_master_addressing(i2c_periph, (slave_addr << 1) | 0x01, I2C_MASTER_RECEIVE);
    i2c_transfer_byte_number_config(i2c_periph, recv_len);

    /* 产生Repeated START条件 */
    i2c_start_on_bus(i2c_periph);

    /* 清除TBE标志 */
    I2C_STAT(i2c_periph) |= I2C_STAT_TBE;

    /* 等待TBE */
    ret = _drv_i2c_wait_flag(port, I2C_FLAG_TBE, SET, s_i2c_ctrl[port].timeout_ms);
    if (ret != DRV_I2C_ERR_OK)
    {
        return ret;
    }

    /* ===== 接收阶段 ===== */
    for (i = 0; i < recv_len; i++)
    {
        /* 等待RBNE */
        ret = _drv_i2c_wait_flag(port, I2C_FLAG_RBNE, SET, s_i2c_ctrl[port].timeout_ms);
        if (ret != DRV_I2C_ERR_OK)
        {
            return ret;
        }

        recv_data[i] = i2c_data_receive(i2c_periph);
    }

    /* 等待TC */
    ret = _drv_i2c_wait_flag(port, I2C_FLAG_TC, SET, s_i2c_ctrl[port].timeout_ms);
    if (ret != DRV_I2C_ERR_OK)
    {
        return ret;
    }

    /* 根据send_stop决定是否产生STOP */
    if (send_stop)
    {
        i2c_stop_on_bus(i2c_periph);

        /* 等待STOP生成 */
        ret = _drv_i2c_wait_flag(port, I2C_FLAG_STPDET, SET, s_i2c_ctrl[port].timeout_ms);
        if (ret == DRV_I2C_ERR_OK)
        {
            i2c_flag_clear(i2c_periph, I2C_FLAG_STPDET);
        }
    }

    return ret;
}

/*********************************************************************
 * 公开接口函数实现
 *********************************************************************/

/*********************************************************************
 * @brief   初始化I2C端口
 * @param   config  配置结构体指针
 * @return  DRV_I2C_ERR_OK: 成功，其他: 失败
 *********************************************************************/
int drv_i2c_init(const drv_i2c_config_t *config)
{
    drv_i2c_port_e port;
    int ret;

    /* 运行时参数校验 */
    if (config == NULL)
    {
        DRV_I2C_LOGE("Invalid config: NULL");
        return DRV_I2C_ERR_INVALID_PARAM;
    }

    if (config->port >= DRV_I2C_PORT_MAX)
    {
        DRV_I2C_LOGE("Invalid port: %d", config->port);
        return DRV_I2C_ERR_INVALID_PARAM;
    }

    port = config->port;

    /* 检查是否重复初始化 */
    if (s_i2c_ctrl[port].state != DRV_I2C_STATE_UNINIT)
    {
        DRV_I2C_LOGW("I2C%d already initialized", port);
        return DRV_I2C_ERR_FAILED;
    }

    /* 初始化控制块 */
    memset(&s_i2c_ctrl[port], 0, sizeof(drv_i2c_ctrl_t));
    s_i2c_ctrl[port].state = DRV_I2C_STATE_INIT;
    s_i2c_ctrl[port].speed = config->speed;
    s_i2c_ctrl[port].reg_addr_mode = config->reg_addr_mode;
    s_i2c_ctrl[port].timeout_ms = config->timeout_ms;

    /* 设置硬件资源映射 */
    if (port == DRV_I2C_PORT_I2C0)
    {
        s_i2c_ctrl[port].i2c_periph = I2C0;
        s_i2c_ctrl[port].rcu_i2c = RCU_I2C0;
    }
    else
    {
        s_i2c_ctrl[port].i2c_periph = I2C1;
        s_i2c_ctrl[port].rcu_i2c = RCU_I2C1;
    }

    /* 初始化GPIO */
    ret = _drv_i2c_gpio_init(port);
    if (ret != DRV_I2C_ERR_OK)
    {
        DRV_I2C_LOGE("I2C%d GPIO init failed", port);
        memset(&s_i2c_ctrl[port], 0, sizeof(drv_i2c_ctrl_t));    /* 清空控制块 */
        s_i2c_ctrl[port].state = DRV_I2C_STATE_UNINIT;
        return DRV_I2C_ERR_FAILED;
    }

    /* 初始化I2C外设 */
    ret = _drv_i2c_periph_init(port, config);
    if (ret != DRV_I2C_ERR_OK)
    {
        DRV_I2C_LOGE("I2C%d peripheral init failed", port);
        _drv_i2c_gpio_deinit(port);                              /* 回滚 GPIO */
        memset(&s_i2c_ctrl[port], 0, sizeof(drv_i2c_ctrl_t));    /* 清空控制块 */
        s_i2c_ctrl[port].state = DRV_I2C_STATE_UNINIT;
        return DRV_I2C_ERR_FAILED;
    }

    /* 创建互斥锁 */
    if (config->use_mutex)
    {
        s_i2c_ctrl[port].mutex = xSemaphoreCreateMutex();
        if (s_i2c_ctrl[port].mutex == NULL)
        {
            DRV_I2C_LOGE("I2C%d mutex create failed", port);
            i2c_disable(s_i2c_ctrl[port].i2c_periph);            /* 禁用 I2C */
            rcu_periph_clock_disable(s_i2c_ctrl[port].rcu_i2c);  /* 禁用时钟 */
            _drv_i2c_gpio_deinit(port);                          /* 回滚 GPIO */
            memset(&s_i2c_ctrl[port], 0, sizeof(drv_i2c_ctrl_t));
            s_i2c_ctrl[port].state = DRV_I2C_STATE_UNINIT;
            return DRV_I2C_ERR_FAILED;
        }
    }

    s_i2c_ctrl[port].state = DRV_I2C_STATE_ACTIVE;
    DRV_I2C_LOGI("I2C%d init success", port);
    return DRV_I2C_ERR_OK;
}

/*********************************************************************
 * @brief   反初始化I2C端口
 * @param   port    I2C端口号
 * @return  DRV_I2C_ERR_OK: 成功，其他: 失败
 *********************************************************************/
int drv_i2c_deinit(drv_i2c_port_e port)
{
    /* 运行时参数校验 */
    if (port >= DRV_I2C_PORT_MAX)
    {
        DRV_I2C_LOGE("Invalid port: %d", port);
        return DRV_I2C_ERR_INVALID_PARAM;
    }

    if (s_i2c_ctrl[port].state == DRV_I2C_STATE_UNINIT)
    {
        return DRV_I2C_ERR_NOT_READY;
    }

    /* 禁用I2C */
    i2c_disable(s_i2c_ctrl[port].i2c_periph);

    /* 禁用时钟 */
    rcu_periph_clock_disable(s_i2c_ctrl[port].rcu_i2c);

    /* 反初始化GPIO */
    _drv_i2c_gpio_deinit(port);

    /* 删除互斥锁 */
    if (s_i2c_ctrl[port].mutex != NULL)
    {
        vSemaphoreDelete(s_i2c_ctrl[port].mutex);
        s_i2c_ctrl[port].mutex = NULL;
    }

    /* 清空控制块 */
    memset(&s_i2c_ctrl[port], 0, sizeof(drv_i2c_ctrl_t));
    s_i2c_ctrl[port].state = DRV_I2C_STATE_UNINIT;

    DRV_I2C_LOGI("I2C%d deinit success", port);
    return DRV_I2C_ERR_OK;
}

/*********************************************************************
 * @brief   Master发送数据
 * @param   port        I2C端口号
 * @param   slave_addr  从机地址（7位原始地址）
 * @param   data        发送数据缓冲区
 * @param   len         数据长度
 * @return  DRV_I2C_ERR_OK: 成功，其他: 失败
 *********************************************************************/
int drv_i2c_master_send(drv_i2c_port_e port, uint8_t slave_addr,
                        const uint8_t *data, uint16_t len)
{
    uint32_t i2c_periph;
    int ret = DRV_I2C_ERR_OK;  /* 初始化为成功 */
    uint16_t i;

    /* 运行时参数校验 */
    if (port >= DRV_I2C_PORT_MAX)
    {
        DRV_I2C_LOGE("Invalid port: %d", port);
        return DRV_I2C_ERR_INVALID_PARAM;
    }

    /* 状态检查 */
    if (s_i2c_ctrl[port].state != DRV_I2C_STATE_ACTIVE)
    {
        DRV_I2C_LOGE("I2C%d not active, state=%d", port, s_i2c_ctrl[port].state);
        return DRV_I2C_ERR_NOT_READY;
    }

    if (data == NULL)
    {
        DRV_I2C_LOGE("Invalid data: NULL");
        return DRV_I2C_ERR_INVALID_PARAM;
    }

    if (len == 0)
    {
        DRV_I2C_LOGE("Invalid len: 0");
        return DRV_I2C_ERR_INVALID_PARAM;
    }

    /* 获取互斥锁 */
    if (s_i2c_ctrl[port].mutex != NULL)
    {
        if (xSemaphoreTake(s_i2c_ctrl[port].mutex, portMAX_DELAY) != pdTRUE)
        {
            return DRV_I2C_ERR_FAILED;
        }
    }

    /* 检查总线忙 */
    ret = _drv_i2c_check_busy(port);
    if (ret != DRV_I2C_ERR_OK)
    {
        goto exit;
    }

    /* 获取I2C外设句柄 */
    i2c_periph = s_i2c_ctrl[port].i2c_periph;

    /* 配置从机地址和字节数 */
    i2c_master_addressing(i2c_periph, slave_addr << 1, I2C_MASTER_TRANSMIT);
    i2c_transfer_byte_number_config(i2c_periph, len);

    /* 产生START条件 */
    i2c_start_on_bus(i2c_periph);

    /* 清除TBE标志（必须在等待TBE之前，参考官方Demo） */
    I2C_STAT(i2c_periph) |= I2C_STAT_TBE;

    /* 等待TBE（发送缓冲区空） */
    ret = _drv_i2c_wait_flag(port, I2C_FLAG_TBE, SET, s_i2c_ctrl[port].timeout_ms);
    if (ret != DRV_I2C_ERR_OK)
    {
        goto exit;
    }

    /* 发送数据 */
    for (i = 0; i < len; i++)
    {
        i2c_data_transmit(i2c_periph, data[i]);

        /* 等待TI（传输中断标志） */
        ret = _drv_i2c_wait_flag(port, I2C_FLAG_TI, SET, s_i2c_ctrl[port].timeout_ms);
        if (ret != DRV_I2C_ERR_OK)
        {
            goto exit;
        }
    }

    /* 等待TC（传输完成） */
    ret = _drv_i2c_wait_flag(port, I2C_FLAG_TC, SET, s_i2c_ctrl[port].timeout_ms);
    if (ret != DRV_I2C_ERR_OK)
    {
        goto exit;
    }

    /* 产生STOP条件 */
    i2c_stop_on_bus(i2c_periph);

    /* 等待STOP生成 */
    ret = _drv_i2c_wait_flag(port, I2C_FLAG_STPDET, SET, s_i2c_ctrl[port].timeout_ms);
    if (ret == DRV_I2C_ERR_OK)
    {
        i2c_flag_clear(i2c_periph, I2C_FLAG_STPDET);
    }

exit:
    /* 释放互斥锁 */
    if (s_i2c_ctrl[port].mutex != NULL)
    {
        xSemaphoreGive(s_i2c_ctrl[port].mutex);
    }

    /* 记录错误码 */
    s_i2c_ctrl[port].last_error = ret;

    if (ret != DRV_I2C_ERR_OK)
    {
        DRV_I2C_LOGE("I2C%d send failed, ret=%d", port, ret);
    }

    return ret;
}

/*********************************************************************
 * @brief   Master接收数据
 * @param   port        I2C端口号
 * @param   slave_addr  从机地址（7位原始地址）
 * @param   data        接收数据缓冲区
 * @param   len         数据长度
 * @return  DRV_I2C_ERR_OK: 成功，其他: 失败
 *********************************************************************/
int drv_i2c_master_recv(drv_i2c_port_e port, uint8_t slave_addr,
                        uint8_t *data, uint16_t len)
{
    uint32_t i2c_periph;
    int ret = DRV_I2C_ERR_OK;  /* 初始化为成功 */
    uint16_t i;

    /* 运行时参数校验 */
    if (port >= DRV_I2C_PORT_MAX)
    {
        DRV_I2C_LOGE("Invalid port: %d", port);
        return DRV_I2C_ERR_INVALID_PARAM;
    }

    /* 状态检查 */
    if (s_i2c_ctrl[port].state != DRV_I2C_STATE_ACTIVE)
    {
        DRV_I2C_LOGE("I2C%d not active, state=%d", port, s_i2c_ctrl[port].state);
        return DRV_I2C_ERR_NOT_READY;
    }

    if (data == NULL)
    {
        DRV_I2C_LOGE("Invalid data: NULL");
        return DRV_I2C_ERR_INVALID_PARAM;
    }

    if (len == 0)
    {
        DRV_I2C_LOGE("Invalid len: 0");
        return DRV_I2C_ERR_INVALID_PARAM;
    }

    /* 获取互斥锁 */
    if (s_i2c_ctrl[port].mutex != NULL)
    {
        if (xSemaphoreTake(s_i2c_ctrl[port].mutex, portMAX_DELAY) != pdTRUE)
        {
            return DRV_I2C_ERR_FAILED;
        }
    }

    /* 检查总线忙 */
    ret = _drv_i2c_check_busy(port);
    if (ret != DRV_I2C_ERR_OK)
    {
        goto exit;
    }

    /* 获取I2C外设句柄 */
    i2c_periph = s_i2c_ctrl[port].i2c_periph;

    /* 配置从机地址和字节数 */
    i2c_master_addressing(i2c_periph, (slave_addr << 1) | 0x01, I2C_MASTER_RECEIVE);
    i2c_transfer_byte_number_config(i2c_periph, len);

    /* 产生START条件 */
    i2c_start_on_bus(i2c_periph);

    /* 清除TBE标志（必须在等待TBE之前，参考官方Demo） */
    I2C_STAT(i2c_periph) |= I2C_STAT_TBE;

    /* 等待TBE */
    ret = _drv_i2c_wait_flag(port, I2C_FLAG_TBE, SET, s_i2c_ctrl[port].timeout_ms);
    if (ret != DRV_I2C_ERR_OK)
    {
        goto exit;
    }

    for (i = 0; i < len; i++)
    {
        /* 等待RBNE（接收缓冲区非空） */
        ret = _drv_i2c_wait_flag(port, I2C_FLAG_RBNE, SET, s_i2c_ctrl[port].timeout_ms);
        if (ret != DRV_I2C_ERR_OK)
        {
            goto exit;
        }

        data[i] = i2c_data_receive(i2c_periph);
    }

    /* 等待TC（传输完成） */
    ret = _drv_i2c_wait_flag(port, I2C_FLAG_TC, SET, s_i2c_ctrl[port].timeout_ms);
    if (ret != DRV_I2C_ERR_OK)
    {
        goto exit;
    }

    /* 产生STOP条件 */
    i2c_stop_on_bus(i2c_periph);

    /* 等待STOP生成 */
    ret = _drv_i2c_wait_flag(port, I2C_FLAG_STPDET, SET, s_i2c_ctrl[port].timeout_ms);
    if (ret == DRV_I2C_ERR_OK)
    {
        i2c_flag_clear(i2c_periph, I2C_FLAG_STPDET);
    }

exit:
    /* 释放互斥锁 */
    if (s_i2c_ctrl[port].mutex != NULL)
    {
        xSemaphoreGive(s_i2c_ctrl[port].mutex);
    }

    /* 记录错误码 */
    s_i2c_ctrl[port].last_error = ret;

    if (ret != DRV_I2C_ERR_OK)
    {
        DRV_I2C_LOGE("I2C%d recv failed, ret=%d", port, ret);
    }

    return ret;
}

/*********************************************************************
 * @brief   Master写寄存器
 * @param   port        I2C端口号
 * @param   slave_addr  从机地址（7位原始地址）
 * @param   reg_addr    寄存器地址
 * @param   data        发送数据缓冲区
 * @param   len         数据长度
 * @return  DRV_I2C_ERR_OK: 成功，其他: 失败
 *********************************************************************/
int drv_i2c_write_reg(drv_i2c_port_e port, uint8_t slave_addr,
                      uint32_t reg_addr, const uint8_t *data, uint16_t len)
{
    uint8_t reg_buf[2];
    uint16_t reg_len;
    uint8_t tx_buf[DRV_I2C_MAX_WRITE_LEN];  /* 栈缓冲区 */
    uint16_t tx_len;
    int ret = DRV_I2C_ERR_OK;  /* 初始化为成功 */

    /* 运行时参数校验 */
    if (port >= DRV_I2C_PORT_MAX)
    {
        DRV_I2C_LOGE("Invalid port: %d", port);
        return DRV_I2C_ERR_INVALID_PARAM;
    }

    /* 状态检查 */
    if (s_i2c_ctrl[port].state != DRV_I2C_STATE_ACTIVE)
    {
        DRV_I2C_LOGE("I2C%d not active, state=%d", port, s_i2c_ctrl[port].state);
        return DRV_I2C_ERR_NOT_READY;
    }

    if (data == NULL)
    {
        DRV_I2C_LOGE("Invalid data: NULL");
        return DRV_I2C_ERR_INVALID_PARAM;
    }

    if (len == 0)
    {
        DRV_I2C_LOGE("Invalid len: 0");
        return DRV_I2C_ERR_INVALID_PARAM;
    }

    /* 准备寄存器地址 */
    if (s_i2c_ctrl[port].reg_addr_mode == DRV_I2C_REG_ADDR_16BIT)
    {
        reg_buf[0] = (reg_addr >> 8) & 0xFF;
        reg_buf[1] = reg_addr & 0xFF;
        reg_len = 2;
    }
    else
    {
        reg_buf[0] = reg_addr & 0xFF;
        reg_len = 1;
    }

    /* 长度检查 */
    tx_len = reg_len + len;
    if (tx_len > DRV_I2C_MAX_WRITE_LEN)
    {
        DRV_I2C_LOGE("Write len too large: %d (max=%d)", tx_len, DRV_I2C_MAX_WRITE_LEN);
        return DRV_I2C_ERR_INVALID_PARAM;
    }

    /* 合并寄存器地址和数据 */
    memcpy(tx_buf, reg_buf, reg_len);
    memcpy(tx_buf + reg_len, data, len);

    /* 发送 */
    ret = drv_i2c_master_send(port, slave_addr, tx_buf, tx_len);

    return ret;
}

/*********************************************************************
 * @brief   Master读寄存器
 * @param   port        I2C端口号
 * @param   slave_addr  从机地址（7位原始地址）
 * @param   reg_addr    寄存器地址
 * @param   data        接收数据缓冲区
 * @param   len         数据长度
 * @return  DRV_I2C_ERR_OK: 成功，其他: 失败
 *********************************************************************/
int drv_i2c_read_reg(drv_i2c_port_e port, uint8_t slave_addr,
                     uint32_t reg_addr, uint8_t *data, uint16_t len)
{
    uint8_t reg_buf[2];
    uint16_t reg_len;
    int ret = DRV_I2C_ERR_OK;  /* 初始化为成功 */

    /* 运行时参数校验 */
    if (port >= DRV_I2C_PORT_MAX)
    {
        DRV_I2C_LOGE("Invalid port: %d", port);
        return DRV_I2C_ERR_INVALID_PARAM;
    }

    /* 状态检查 */
    if (s_i2c_ctrl[port].state != DRV_I2C_STATE_ACTIVE)
    {
        DRV_I2C_LOGE("I2C%d not active, state=%d", port, s_i2c_ctrl[port].state);
        return DRV_I2C_ERR_NOT_READY;
    }

    if (data == NULL)
    {
        DRV_I2C_LOGE("Invalid data: NULL");
        return DRV_I2C_ERR_INVALID_PARAM;
    }

    if (len == 0)
    {
        DRV_I2C_LOGE("Invalid len: 0");
        return DRV_I2C_ERR_INVALID_PARAM;
    }

    /* 准备寄存器地址 */
    if (s_i2c_ctrl[port].reg_addr_mode == DRV_I2C_REG_ADDR_16BIT)
    {
        reg_buf[0] = (reg_addr >> 8) & 0xFF;
        reg_buf[1] = reg_addr & 0xFF;
        reg_len = 2;
    }
    else
    {
        reg_buf[0] = reg_addr & 0xFF;
        reg_len = 1;
    }

    /* 发送寄存器地址，接收数据 */
    ret = _drv_i2c_master_transfer(port, slave_addr, reg_buf, reg_len, data, len, true);
    if (ret != DRV_I2C_ERR_OK)
    {
        DRV_I2C_LOGE("I2C%d read_reg failed, ret=%d", port, ret);
        return ret;
    }

    return ret;
}

/*********************************************************************
 * @brief   Master灵活传输（支持Repeated START）
 * @param   port        I2C端口号
 * @param   slave_addr  从机地址（7位原始地址）
 * @param   send_data   发送数据缓冲区（可为NULL）
 * @param   send_len    发送数据长度
 * @param   recv_data   接收数据缓冲区（可为NULL）
 * @param   recv_len    接收数据长度
 * @param   send_stop   true=产生STOP，false=产生Repeated START
 * @return  DRV_I2C_ERR_OK: 成功，其他: 失败
 * @note    内部函数，仅供drv_i2c_read_reg使用
 *********************************************************************/
static int _drv_i2c_master_transfer(drv_i2c_port_e port, uint8_t slave_addr,
                            const uint8_t *send_data, uint16_t send_len,
                            uint8_t *recv_data, uint16_t recv_len,
                            bool send_stop)
{
    uint32_t i2c_periph;
    int ret = DRV_I2C_ERR_OK;  /* 初始化为成功 */

    /* 运行时参数校验 */
    if (port >= DRV_I2C_PORT_MAX)
    {
        DRV_I2C_LOGE("Invalid port: %d", port);
        return DRV_I2C_ERR_INVALID_PARAM;
    }

    /* 如果发送和接收都为空，直接返回 */
    if ((send_data == NULL || send_len == 0) && (recv_data == NULL || recv_len == 0))
    {
        return DRV_I2C_ERR_OK;
    }

    i2c_periph = s_i2c_ctrl[port].i2c_periph;

    /* 获取互斥锁 */
    if (s_i2c_ctrl[port].mutex != NULL)
    {
        if (xSemaphoreTake(s_i2c_ctrl[port].mutex, portMAX_DELAY) != pdTRUE)
        {
            return DRV_I2C_ERR_FAILED;
        }
    }

    /* 检查总线忙 */
    ret = _drv_i2c_check_busy(port);
    if (ret != DRV_I2C_ERR_OK)
    {
        goto exit;
    }

    if (send_data != NULL && send_len > 0 && (recv_data == NULL || recv_len == 0))
    {
        /* 情况1：只有发送，没有接收 */
        ret = _drv_i2c_transfer_send_only(i2c_periph, port, slave_addr, send_data, send_len, send_stop);
    }
    else if ((send_data == NULL || send_len == 0) && recv_data != NULL && recv_len > 0)
    {
        /* 情况2：只有接收，没有发送 */
        ret = _drv_i2c_transfer_recv_only(i2c_periph, port, slave_addr, recv_data, recv_len, send_stop);
    }
    else
    {
        /* 情况3：既有发送又有接收（Repeated START） */
        ret = _drv_i2c_transfer_send_recv(i2c_periph, port, slave_addr, send_data, send_len,
                                          recv_data, recv_len, send_stop);
    }

exit:
    /* 释放互斥锁 */
    if (s_i2c_ctrl[port].mutex != NULL)
    {
        xSemaphoreGive(s_i2c_ctrl[port].mutex);
    }

    /* 记录错误码 */
    s_i2c_ctrl[port].last_error = ret;

    if (ret != DRV_I2C_ERR_OK)
    {
        DRV_I2C_LOGE("I2C%d transfer failed, ret=%d", port, ret);
    }

    return ret;
}

/*********************************************************************
 * @brief   挂起I2C
 * @param   port    I2C端口号
 * @return  DRV_I2C_ERR_OK: 成功，其他: 失败
 *********************************************************************/
int drv_i2c_suspend(drv_i2c_port_e port)
{
    int ret = DRV_I2C_ERR_FAILED;

    /* 运行时参数校验 */
    if (port >= DRV_I2C_PORT_MAX)
    {
        DRV_I2C_LOGE("Invalid port: %d", port);
        return DRV_I2C_ERR_INVALID_PARAM;
    }

    /* 快速检查状态（无需锁） */
    if (s_i2c_ctrl[port].state != DRV_I2C_STATE_ACTIVE)
    {
        DRV_I2C_LOGW("I2C%d not active, state=%d", port, s_i2c_ctrl[port].state);
        return DRV_I2C_ERR_NOT_READY;
    }

    /* 获取互斥锁 */
    if (s_i2c_ctrl[port].mutex != NULL)
    {
        if (xSemaphoreTake(s_i2c_ctrl[port].mutex, portMAX_DELAY) != pdTRUE)
        {
            return DRV_I2C_ERR_FAILED;
        }
    }

    /* 二次检查状态（防止并发修改） */
    if (s_i2c_ctrl[port].state != DRV_I2C_STATE_ACTIVE)
    {
        DRV_I2C_LOGW("I2C%d state changed during suspend", port);
        ret = DRV_I2C_ERR_NOT_READY;
        goto exit;
    }

    /* 禁用I2C */
    i2c_disable(s_i2c_ctrl[port].i2c_periph);

    /* 禁用时钟 */
    rcu_periph_clock_disable(s_i2c_ctrl[port].rcu_i2c);

    /* GPIO配置为模拟输入 */
    _drv_i2c_gpio_deinit(port);

    s_i2c_ctrl[port].state = DRV_I2C_STATE_SUSPENDED;

    DRV_I2C_LOGI("I2C%d suspended", port);

exit:
    /* 释放互斥锁 */
    if (s_i2c_ctrl[port].mutex != NULL)
    {
        xSemaphoreGive(s_i2c_ctrl[port].mutex);
    }

    return (s_i2c_ctrl[port].state == DRV_I2C_STATE_SUSPENDED) ? DRV_I2C_ERR_OK : DRV_I2C_ERR_FAILED;
}

/*********************************************************************
 * @brief   恢复I2C
 * @param   port    I2C端口号
 * @return  DRV_I2C_ERR_OK: 成功，其他: 失败
 *********************************************************************/
int drv_i2c_resume(drv_i2c_port_e port)
{
    drv_i2c_config_t config = {0};
    int ret;

    /* 运行时参数校验 */
    if (port >= DRV_I2C_PORT_MAX)
    {
        DRV_I2C_LOGE("Invalid port: %d", port);
        return DRV_I2C_ERR_INVALID_PARAM;
    }

    /* 快速检查状态（无需锁） */
    if (s_i2c_ctrl[port].state != DRV_I2C_STATE_SUSPENDED)
    {
        DRV_I2C_LOGW("I2C%d not suspended, state=%d", port, s_i2c_ctrl[port].state);
        return DRV_I2C_ERR_NOT_READY;
    }

    /* 获取互斥锁 */
    if (s_i2c_ctrl[port].mutex != NULL)
    {
        if (xSemaphoreTake(s_i2c_ctrl[port].mutex, portMAX_DELAY) != pdTRUE)
        {
            return DRV_I2C_ERR_FAILED;
        }
    }

    /* 二次检查状态（防止并发修改） */
    if (s_i2c_ctrl[port].state != DRV_I2C_STATE_SUSPENDED)
    {
        DRV_I2C_LOGW("I2C%d state changed during resume", port);
        ret = DRV_I2C_ERR_NOT_READY;
        goto exit;
    }

    /* 恢复GPIO */
    ret = _drv_i2c_gpio_init(port);
    if (ret != DRV_I2C_ERR_OK)
    {
        DRV_I2C_LOGE("I2C%d resume GPIO init failed", port);
        goto exit;
    }

    /* 恢复I2C外设 */
    config.port = port;
    config.speed = s_i2c_ctrl[port].speed;
    config.reg_addr_mode = s_i2c_ctrl[port].reg_addr_mode;
    config.timeout_ms = s_i2c_ctrl[port].timeout_ms;
    config.use_mutex = (s_i2c_ctrl[port].mutex != NULL);

    ret = _drv_i2c_periph_init(port, &config);
    if (ret != DRV_I2C_ERR_OK)
    {
        DRV_I2C_LOGE("I2C%d resume periph init failed", port);
        _drv_i2c_gpio_deinit(port);  /* 回滚GPIO */
        goto exit;
    }

    s_i2c_ctrl[port].state = DRV_I2C_STATE_ACTIVE;

    DRV_I2C_LOGI("I2C%d resumed", port);
    ret = DRV_I2C_ERR_OK;

exit:
    /* 释放互斥锁 */
    if (s_i2c_ctrl[port].mutex != NULL)
    {
        xSemaphoreGive(s_i2c_ctrl[port].mutex);
    }

    return ret;
}
