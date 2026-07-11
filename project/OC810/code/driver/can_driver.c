/*******************************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       can_driver.c
**文件描述：       CAN/CAN FD驱动模块实现
**当前版本：       V1.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.04.20
*******************************************************************************
** 功能描述：       1. 实现双CAN端口独立管理
**                 2. 实现CAN 2.0B和CAN FD双协议支持
**                 3. 实现轮询模式和中断模式
**                 4. 实现GPIO配置、电源管理
**                 5. 实现过滤器配置
**                 6. 实现TDC传输延迟补偿
*******************************************************************************/

#include "can_driver.h"
#include "gd32f50x_can.h"
#include "gd32f50x_rcu.h"
#include "gd32f50x_exti.h"
#include "gpio_driver.h"
#include <string.h>

/*******************************************************************************
 * 内部数据结构
 ******************************************************************************/

/**
 * @brief CAN控制块结构
 */
typedef struct
{
    drv_can_state_e         state;              /**< CAN状态 */
    uint32_t                can_periph;         /**< CAN外设基地址 */
    uint32_t                rcu_can;            /**< CAN时钟使能位 */
    drv_can_config_t        config;             /**< CAN配置 */
    SemaphoreHandle_t       mutex;              /**< 互斥锁 */

    /* CAN FD TDC配置（持久化存储，避免局部变量生命周期问题） */
    can_fd_tdc_struct       tdc_config;         /**< TDC传输延迟补偿配置 */

    /* 回调函数 */
    void (*rx_callback)(drv_can_port_e, drv_can_frame_t *, uint8_t);  /**< 接收回调 */
    void (*tx_callback)(drv_can_port_e, uint8_t);                      /**< 发送回调 */
    void (*err_callback)(drv_can_port_e, drv_can_err_type_e);          /**< 错误回调 */
} drv_can_ctrl_t;

/*******************************************************************************
 * 内部常量定义
 ******************************************************************************/

/* CAN控制块数组 */
static drv_can_ctrl_t s_can_ctrl[DRV_CAN_PORT_MAX];

/*******************************************************************************
 * GPIO配置映射表
 ******************************************************************************/

 /**
  * @brief CAN GPIO配置结构体
  */
typedef struct
{
    drv_gpio_port_e gpio_port;
    drv_gpio_pin_e  gpio_pin_rx;
    drv_gpio_pin_e  gpio_pin_tx;
    drv_gpio_af_e   gpio_af;
} drv_can_gpio_cfg_t;

/**
 * @brief CAN0 GPIO配置表
 */
static const drv_can_gpio_cfg_t s_can0_gpio_table[] =
{
    {DRV_GPIO_PORT_D, DRV_GPIO_PIN_0, DRV_GPIO_PIN_1, DRV_GPIO_AF_0},   /* PD0(RX), PD1(TX) - AF0 */
    {DRV_GPIO_PORT_A, DRV_GPIO_PIN_11, DRV_GPIO_PIN_12, DRV_GPIO_AF_1}, /* PA11(RX), PA12(TX) - AF1 */
    {DRV_GPIO_PORT_B, DRV_GPIO_PIN_8, DRV_GPIO_PIN_9, DRV_GPIO_AF_1}    /* PB8(RX), PB9(TX) - AF1 */
};

/**
 * @brief CAN1 GPIO配置表
 */
static const drv_can_gpio_cfg_t s_can1_gpio_table[] =
{
    {DRV_GPIO_PORT_B, DRV_GPIO_PIN_5, DRV_GPIO_PIN_6, DRV_GPIO_AF_3},   /* PB5(RX), PB6(TX) - AF3 */
    {DRV_GPIO_PORT_B, DRV_GPIO_PIN_12, DRV_GPIO_PIN_13, DRV_GPIO_AF_3}  /* PB12(RX), PB13(TX) - AF3 */
};

/*******************************************************************************
 * 波特率配置表（车载常用波特率）
 *
 * 计算公式：
 *   CAN波特率 = APB1_FREQ / (PSC × TQ_NUM)
 *   其中：TQ_NUM = 1(Sync_Seg) + TSEG1(Prop_Seg+Phase_Seg1) + TSEG2(Phase_Seg2)
 *   采样点 = (1 + TSEG1) / TQ_NUM × 100%
 *
 * 推荐采样点：
 *   - CAN 2.0B: 75%~87.5%（推荐80%）
 *   - CAN FD仲裁段: 75%~87.5%
 *   - CAN FD数据段: 75%~80%
 *
 * 开发者指南（适配不同APB1频率）：
 *   1. 确定APB1频率（GD32F50x通常为60MHz）
 *   2. 选择采样点（推荐80%，即TSEG1:TSEG2 = 7:2）
 *   3. 计算TQ总数：TQ_NUM = 1 + TSEG1 + TSEG2
 *   4. 计算分频器：PSC = APB1_FREQ / (目标波特率 × TQ_NUM)
 *   5. 验证PSC范围：1 ≤ PSC ≤ 1024（仲裁段），1 ≤ PSC ≤ 32（数据段）
 *   6. 验证实际波特率误差：< 1%为优秀，< 3%可接受
 *
 * 示例（APB1=60MHz，目标500kbps，采样点80%）：
 *   TQ_NUM = 1+7+2 = 10
 *   PSC = 60000000 / (500000 × 10) = 12
 *   实际波特率 = 60000000 / (12 × 10) = 500000 bps（误差0%）
 *   采样点 = (1+7) / 10 = 80%
 ******************************************************************************/

/**
 * @brief CAN仲裁段波特率配置结构体
 */
typedef struct
{
    drv_can_bitrate_e bitrate_enum;  /**< 波特率枚举 */
    uint32_t          bitrate_bps;   /**< 波特率值（bps） */
    uint32_t          prescaler;     /**< 分频器（基于APB1=60MHz） */
    uint32_t          tseg1;         /**< 时间段1 */
    uint32_t          tseg2;         /**< 时间段2 */
    uint32_t          sjw;           /**< 同步跳转宽度 */
    uint8_t           sample_point;  /**< 采样点百分比（如80表示80%） */
} drv_can_arb_timing_t;

/**
 * @brief CAN 2.0B/CAN FD仲裁段波特率表（APB1=60MHz，采样点80%）
 */
static const drv_can_arb_timing_t s_arb_timing_table[] =
{
    /* 枚举值,           波特率(bps),  PSC, TSEG1, TSEG2, SJW, 采样点 */
    {DRV_CAN_BITRATE_10K,    10000,   600,    7,     2,    2,    80},
    {DRV_CAN_BITRATE_20K,    20000,   300,    7,     2,    2,    80},
    {DRV_CAN_BITRATE_50K,    50000,   120,    7,     2,    2,    80},
    {DRV_CAN_BITRATE_100K,  100000,    60,    7,     2,    2,    80},
    {DRV_CAN_BITRATE_125K,  125000,    48,    7,     2,    2,    80},
    {DRV_CAN_BITRATE_250K,  250000,    24,    7,     2,    2,    80},
    {DRV_CAN_BITRATE_500K,  500000,    12,    7,     2,    2,    80},
    {DRV_CAN_BITRATE_800K,  800000,     7,    8,     1,    1,    90},  /* 特殊配置，采样点90% */
    {DRV_CAN_BITRATE_1M,   1000000,     6,    7,     2,    2,    80},
};

#define DRV_CAN_ARB_TIMING_COUNT (sizeof(s_arb_timing_table) / sizeof(s_arb_timing_table[0]))

/**
 * @brief CAN FD数据段波特率表（APB1=60MHz，采样点75%~87%）
 */
typedef struct
{
    drv_can_fd_bitrate_e bitrate_enum;  /**< 波特率枚举 */
    uint32_t             bitrate_bps;   /**< 波特率值（bps） */
    uint32_t             prescaler;     /**< 分频器（基于APB1=60MHz） */
    uint32_t             tseg1;         /**< 时间段1 */
    uint32_t             tseg2;         /**< 时间段2 */
    uint32_t             sjw;           /**< 同步跳转宽度 */
    uint8_t              sample_point;  /**< 采样点百分比（如80表示80%） */
} drv_can_data_timing_t;

/**
 * @brief CAN FD数据段波特率表（APB1=60MHz，采样点75%~87%）
 * @note：车载CAN FD标准为2Mbps（功能消息）和5Mbps（刷写编程），符合ISO 11898-1:2015
 */
static const drv_can_data_timing_t s_data_timing_table[] =
{
    /* 枚举值,              波特率(bps), PSC, TSEG1, TSEG2, SJW, 采样点 */
    {DRV_CAN_FD_BITRATE_1M,  1000000,    7,    5,     2,    2,    75},  /* TQ=8, 实际=1.071Mbps(+7.1%误差) */
    {DRV_CAN_FD_BITRATE_2M,  2000000,    3,    7,     2,    2,    80},  /* TQ=10, 实际=2Mbps(0%误差) 车载标准 */
    {DRV_CAN_FD_BITRATE_4M,  4000000,    1,   12,     2,    2,    87},  /* TQ=15, 实际=4Mbps(0%误差) */
    {DRV_CAN_FD_BITRATE_5M,  5000000,    1,    9,     2,    2,    83},  /* TQ=12, 实际=5Mbps(0%误差) 车载标准最高 */
};

#define DRV_CAN_DATA_TIMING_COUNT (sizeof(s_data_timing_table) / sizeof(s_data_timing_table[0]))

/*******************************************************************************
 * 内部辅助函数声明
 ******************************************************************************/

static int _drv_can_gpio_init(drv_can_port_e port);
static void _drv_can_gpio_deinit(drv_can_port_e port);
static int _drv_can_periph_init(drv_can_port_e port, const drv_can_config_t *config);
static void _drv_can_periph_deinit(drv_can_port_e port);
static int _drv_can_calc_arb_timing(drv_can_bitrate_e bitrate, uint32_t *prescaler, uint32_t *tseg1, uint32_t *tseg2, uint32_t *sjw);
static int _drv_can_calc_data_timing(drv_can_fd_bitrate_e bitrate, uint32_t *prescaler, uint32_t *tseg1, uint32_t *tseg2, uint32_t *sjw);
static uint32_t _drv_can_get_apb1_freq(void);
static int _drv_can_wait_flag(drv_can_port_e port, uint32_t flag, uint32_t timeout_ms);

/*******************************************************************************
 * 内部辅助函数实现
 ******************************************************************************/

/*******************************************************************************
 * @brief   获取APB1总线频率
 * @return  APB1频率（Hz）
 ******************************************************************************/
static uint32_t _drv_can_get_apb1_freq(void)
{
    return rcu_clock_freq_get(CK_APB1);
}

/*******************************************************************************
 * @brief   初始化CAN GPIO
 * @param   port    CAN端口号
 * @return  DRV_CAN_ERR_OK: 成功，其他: 失败
 * @note    使用gpio_driver统一管理GPIO资源
 ******************************************************************************/
static int _drv_can_gpio_init(drv_can_port_e port)
{
    const drv_can_gpio_cfg_t *gpio_cfg = NULL;
    uint8_t gpio_sel;
    drv_gpio_config_t gpio_init_struct = {0};

    if (port == DRV_CAN_PORT_CAN0)
    {
#if (DRV_CAN0_GPIO_SEL == DRV_CAN0_GPIO_PD0_PD1)
        gpio_sel = 0;
#elif (DRV_CAN0_GPIO_SEL == DRV_CAN0_GPIO_PA11_PA12)
        gpio_sel = 1;
#elif (DRV_CAN0_GPIO_SEL == DRV_CAN0_GPIO_PB8_PB9)
        gpio_sel = 2;
#else
        DRV_CAN_LOGE("CAN0 invalid GPIO selection");
        return DRV_CAN_ERR_INVALID_PARAM;
#endif
        gpio_cfg = &s_can0_gpio_table[gpio_sel];
    }
    else if (port == DRV_CAN_PORT_CAN1)
    {
#if (DRV_CAN1_GPIO_SEL == DRV_CAN1_GPIO_PB5_PB6)
        gpio_sel = 0;
#elif (DRV_CAN1_GPIO_SEL == DRV_CAN1_GPIO_PB12_PB13)
        gpio_sel = 1;
#else
        DRV_CAN_LOGE("CAN1 invalid GPIO selection");
        return DRV_CAN_ERR_INVALID_PARAM;
#endif
        gpio_cfg = &s_can1_gpio_table[gpio_sel];
    }
    else
    {
        return DRV_CAN_ERR_INVALID_PARAM;
    }

    /* 使能复用功能时钟（GPIO AF必须） */
    rcu_periph_clock_enable(RCU_AF);

    /* 配置CAN_RX引脚：复用功能、推挽输入、高速、上拉 */
    gpio_init_struct.port = gpio_cfg->gpio_port;
    gpio_init_struct.pin = gpio_cfg->gpio_pin_rx;
    gpio_init_struct.mode = DRV_GPIO_MODE_AF;
    gpio_init_struct.otype = DRV_GPIO_OTYPE_PP;
    gpio_init_struct.speed = DRV_GPIO_SPEED_LEVEL3;
    gpio_init_struct.pupd = DRV_GPIO_PUPD_PULLUP;
    gpio_init_struct.af = gpio_cfg->gpio_af;
    gpio_init_struct.initial_state = false;

    if (drv_gpio_init(&gpio_init_struct) != DRV_GPIO_OK)
    {
        DRV_CAN_LOGE("CAN%d RX GPIO init failed", port);
        return DRV_CAN_ERR_FAILED;
    }

    /* 配置CAN_TX引脚：复用功能、推挽输出、高速、上拉 */
    gpio_init_struct.pin = gpio_cfg->gpio_pin_tx;
    if (drv_gpio_init(&gpio_init_struct) != DRV_GPIO_OK)
    {
        DRV_CAN_LOGE("CAN%d TX GPIO init failed", port);
        drv_gpio_deinit(gpio_cfg->gpio_port, gpio_cfg->gpio_pin_rx);  /* 回滚RX配置 */
        return DRV_CAN_ERR_FAILED;
    }

    return DRV_CAN_ERR_OK;
}

/*******************************************************************************
 * @brief   反初始化CAN GPIO
 * @param   port    CAN端口号
 * @note    使用gpio_driver统一管理GPIO资源
 ******************************************************************************/
static void _drv_can_gpio_deinit(drv_can_port_e port)
{
    const drv_can_gpio_cfg_t *gpio_cfg = NULL;
    uint8_t gpio_sel;

    if (port == DRV_CAN_PORT_CAN0)
    {
#if (DRV_CAN0_GPIO_SEL == DRV_CAN0_GPIO_PD0_PD1)
        gpio_sel = 0;
#elif (DRV_CAN0_GPIO_SEL == DRV_CAN0_GPIO_PA11_PA12)
        gpio_sel = 1;
#elif (DRV_CAN0_GPIO_SEL == DRV_CAN0_GPIO_PB8_PB9)
        gpio_sel = 2;
#else
        return;
#endif
        gpio_cfg = &s_can0_gpio_table[gpio_sel];
    }
    else if (port == DRV_CAN_PORT_CAN1)
    {
#if (DRV_CAN1_GPIO_SEL == DRV_CAN1_GPIO_PB5_PB6)
        gpio_sel = 0;
#elif (DRV_CAN1_GPIO_SEL == DRV_CAN1_GPIO_PB12_PB13)
        gpio_sel = 1;
#else
        return;
#endif
        gpio_cfg = &s_can1_gpio_table[gpio_sel];
    }
    else
    {
        return;
    }

    /* 反初始化RX和TX引脚 */
    drv_gpio_deinit(gpio_cfg->gpio_port, gpio_cfg->gpio_pin_rx);
    drv_gpio_deinit(gpio_cfg->gpio_port, gpio_cfg->gpio_pin_tx);
}

/*******************************************************************************
 * @brief   计算仲裁段波特率参数
 * @param   bitrate     目标波特率
 * @param   prescaler   输出：分频器
 * @param   tseg1       输出：时间段1
 * @param   tseg2       输出：时间段2
 * @param   sjw         输出：同步跳转宽度
 * @return  DRV_CAN_ERR_OK: 成功，其他: 失败
 ******************************************************************************/
static int _drv_can_calc_arb_timing(drv_can_bitrate_e bitrate, uint32_t *prescaler, uint32_t *tseg1, uint32_t *tseg2, uint32_t *sjw)
{
    uint32_t apb1_freq = _drv_can_get_apb1_freq();
    uint32_t i;
    uint32_t tq_num;              /* 查表计算相关变量 */
    uint32_t psc;
    const drv_can_arb_timing_t *cfg;

    /* 查表获取波特率配置 */
    for (i = 0; i < DRV_CAN_ARB_TIMING_COUNT; i++)
    {
        if (s_arb_timing_table[i].bitrate_enum == bitrate)
        {
            cfg = &s_arb_timing_table[i];

            /* 如果APB1频率不是60MHz，需要重新计算分频器 */
            if (apb1_freq != 60000000)
            {
                tq_num = 1 + cfg->tseg1 + cfg->tseg2;
                psc = apb1_freq / (cfg->bitrate_bps * tq_num);

                if (psc == 0 || psc > 1024)
                {
                    DRV_CAN_LOGE("Arb bitrate %lu bps: PSC=%lu out of range", cfg->bitrate_bps, psc);
                    return DRV_CAN_ERR_FAILED;
                }

                *prescaler = psc;
            }
            else
            {
                *prescaler = cfg->prescaler;
            }

            *tseg1 = cfg->tseg1;
            *tseg2 = cfg->tseg2;
            *sjw = cfg->sjw;

            return DRV_CAN_ERR_OK;
        }
    }

    DRV_CAN_LOGE("Unsupported arb bitrate: %d", bitrate);
    return DRV_CAN_ERR_INVALID_PARAM;
}

/*******************************************************************************
 * @brief   计算CAN FD数据段波特率参数
 * @param   bitrate     目标波特率
 * @param   prescaler   输出：分频器
 * @param   tseg1       输出：时间段1
 * @param   tseg2       输出：时间段2
 * @param   sjw         输出：同步跳转宽度
 * @return  DRV_CAN_ERR_OK: 成功，其他: 失败
 ******************************************************************************/
static int _drv_can_calc_data_timing(drv_can_fd_bitrate_e bitrate, uint32_t *prescaler, uint32_t *tseg1, uint32_t *tseg2, uint32_t *sjw)
{
    uint32_t apb1_freq = _drv_can_get_apb1_freq();
    uint32_t i;
    uint32_t tq_num;              /* 查表计算相关变量 */
    uint32_t psc;
    const drv_can_data_timing_t *cfg;

    /* 查表获取波特率配置 */
    for (i = 0; i < DRV_CAN_DATA_TIMING_COUNT; i++)
    {
        if (s_data_timing_table[i].bitrate_enum == bitrate)
        {
            cfg = &s_data_timing_table[i];

            /* 如果APB1频率不是60MHz，需要重新计算分频器 */
            if (apb1_freq != 60000000)
            {
                tq_num = 1 + cfg->tseg1 + cfg->tseg2;
                psc = apb1_freq / (cfg->bitrate_bps * tq_num);

                if (psc == 0 || psc > 32)
                {
                    DRV_CAN_LOGE("Data bitrate %lu bps: PSC=%lu out of range", cfg->bitrate_bps, psc);
                    return DRV_CAN_ERR_FAILED;
                }

                *prescaler = psc;
            }
            else
            {
                *prescaler = cfg->prescaler;
            }

            *tseg1 = cfg->tseg1;
            *tseg2 = cfg->tseg2;
            *sjw = cfg->sjw;

            return DRV_CAN_ERR_OK;
        }
    }

    DRV_CAN_LOGE("Unsupported data bitrate: %d", bitrate);
    return DRV_CAN_ERR_INVALID_PARAM;
}

/*******************************************************************************
 * @brief   等待标志位（带超时）
 * @param   port        CAN端口号
 * @param   flag        等待的标志
 * @param   timeout_ms  超时时间（ms）
 * @return  DRV_CAN_ERR_OK: 成功，其他: 失败
 ******************************************************************************/
static int _drv_can_wait_flag(drv_can_port_e port, uint32_t flag, uint32_t timeout_ms)
{
    uint32_t tick_start = xTaskGetTickCount();
    uint32_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    uint32_t spin_count = 0;  /* 忙等待计数器 */

    while (can_flag_get(s_can_ctrl[port].can_periph, flag) == RESET)
    {
        if ((xTaskGetTickCount() - tick_start) >= timeout_ticks)
        {
            DRV_CAN_LOGE("CAN%d wait flag 0x%08lX timeout", port, flag);
            return DRV_CAN_ERR_TIMEOUT;
        }

        /* 忙等待50次后再让出CPU（约50us），提升CAN通信性能 */
        spin_count++;
        if (spin_count >= 50)
        {
            spin_count = 0;
            taskYIELD();  /* 主动让出，延迟极短 */
        }
    }

    return DRV_CAN_ERR_OK;
}

/*******************************************************************************
 * @brief   初始化CAN外设
 * @param   port    CAN端口号
 * @param   config  指向CAN配置结构体的指针
 * @return  DRV_CAN_ERR_OK: 成功，其他: 失败
 ******************************************************************************/
static int _drv_can_periph_init(drv_can_port_e port, const drv_can_config_t *config)
{
    uint32_t prescaler, tseg1, tseg2, sjw;
    can_parameter_struct can_init_struct;
    can_fdframe_struct can_fd_struct;

    /* Step 1: 使能CAN时钟 */
    rcu_periph_clock_enable(s_can_ctrl[port].rcu_can);

    /* Step 2: 配置CAN GPIO（所有模式都需要配置GPIO，包括LOOPBACK） */
    if (_drv_can_gpio_init(port) != DRV_CAN_ERR_OK)
    {
        DRV_CAN_LOGE("CAN%d GPIO init failed", port);
        return DRV_CAN_ERR_FAILED;
    }

    /* Step 3: 使用官方can_deinit复位CAN硬件（在GPIO配置之后） */
    can_deinit(s_can_ctrl[port].can_periph);

    /* 配置CAN参数 */
    can_struct_para_init(CAN_INIT_STRUCT, &can_init_struct);

    /* 工作模式 */
    can_init_struct.working_mode = CAN_NORMAL_MODE;
    if (config->mode == DRV_CAN_MODE_LOOPBACK)
    {
        can_init_struct.working_mode = CAN_LOOPBACK_MODE;
    }
    else if (config->mode == DRV_CAN_MODE_SILENT)
    {
        can_init_struct.working_mode = CAN_SILENT_MODE;
    }
    else if (config->mode == DRV_CAN_MODE_SILENT_LOOPBACK)
    {
        can_init_struct.working_mode = CAN_SILENT_LOOPBACK_MODE;
    }

    /* 仲裁段波特率 */
    if (_drv_can_calc_arb_timing(config->arb_bitrate, &prescaler, &tseg1, &tseg2, &sjw) != DRV_CAN_ERR_OK)
    {
        DRV_CAN_LOGE("CAN%d calc arb bitrate failed", port);
        return DRV_CAN_ERR_FAILED;
    }

    /* 仲裁段参数 */
    can_init_struct.resync_jump_width = sjw;
    can_init_struct.time_segment_1 = tseg1;
    can_init_struct.time_segment_2 = tseg2;
    can_init_struct.prescaler = prescaler;

    /* 自动重传、自动唤醒、睡眠模式等 */
    can_init_struct.auto_retrans = ENABLE;
    can_init_struct.auto_wake_up = DISABLE;
    can_init_struct.time_triggered = DISABLE;
    can_init_struct.trans_fifo_order = DISABLE;

    /* 初始化CAN */
    if (can_init(s_can_ctrl[port].can_periph, &can_init_struct) == ERROR)
    {
        DRV_CAN_LOGE("CAN%d init failed (can_init returned ERROR)", port);
        return DRV_CAN_ERR_FAILED;
    }

    /* Step 4: CAN过滤器分配（CAN0使用0-13，CAN1使用14-27） */
    if (port == DRV_CAN_PORT_CAN1)
    {
        can1_filter_start_bank(14);

    }

    /* Step 5: 配置默认过滤器（允许所有消息通过） */
    can_filter_parameter_struct can_filter;
    can_struct_para_init(CAN_FILTER_STRUCT, &can_filter);

    if (port == DRV_CAN_PORT_CAN0)
    {
        can_filter.filter_number = 0;
    }
    else
    {
        can_filter.filter_number = 14;
    }

    can_filter.filter_mode = CAN_FILTERMODE_MASK;
    can_filter.filter_bits = CAN_FILTERBITS_32BIT;
    can_filter.filter_list_high = 0x0000;
    can_filter.filter_list_low = 0x0000;
    can_filter.filter_mask_high = 0x0000;
    can_filter.filter_mask_low = 0x0000;
    can_filter.filter_fifo_number = CAN_FIFO0;
    can_filter.filter_enable = ENABLE;
    can_filter_init(&can_filter);

    /* CAN FD配置 */
    if (config->protocol == DRV_CAN_PROTOCOL_CANFD)
    {
        can_struct_para_init(CAN_FD_FRAME_STRUCT, &can_fd_struct);

        /* 使能CAN FD */
        can_fd_struct.fd_frame = ENABLE;
        can_fd_struct.excp_event_detect = ENABLE;

        /* 数据段波特率 */
        if (_drv_can_calc_data_timing(config->data_bitrate, &prescaler, &tseg1, &tseg2, &sjw) != DRV_CAN_ERR_OK)
        {
            DRV_CAN_LOGE("CAN%d calc data bitrate failed", port);
            return DRV_CAN_ERR_FAILED;
        }

        can_fd_struct.data_prescaler = prescaler;
        can_fd_struct.data_resync_jump_width = sjw;
        can_fd_struct.data_time_segment_1 = tseg1;
        can_fd_struct.data_time_segment_2 = tseg2;

        /* TDC配置（CAN FD > 2Mbps时推荐启用，符合ISO 11898-1:2015） */
        if (config->enable_tdc && config->data_bitrate > DRV_CAN_FD_BITRATE_2M)
        {
            can_fd_struct.delay_compensation = ENABLE;

            /* 使用控制块成员，避免局部变量生命周期问题 */
            s_can_ctrl[port].tdc_config.tdc_mode = CAN_TDCMOD_CALC_AND_OFFSET;
            s_can_ctrl[port].tdc_config.tdc_filter = 0x04;
            s_can_ctrl[port].tdc_config.tdc_offset = 0x04;
            can_fd_struct.p_delay_compensation = &s_can_ctrl[port].tdc_config;
        }
        else
        {
            can_fd_struct.delay_compensation = DISABLE;
            can_fd_struct.p_delay_compensation = NULL;
        }

        can_fd_struct.edge_filter_disable = DISABLE;
        can_fd_struct.iso_bosch = CAN_FDMOD_ISO;  /* 使用ISO 11898-1:2015标准（推荐） */
        can_fd_struct.esi_mode = DISABLE;

        can_fd_init(s_can_ctrl[port].can_periph, &can_fd_struct);

        /* 使能CAN FD功能（仅在CAN FD模式下） */
        can_fd_function_enable(s_can_ctrl[port].can_periph);
    }

    /* 配置中断（如果使用中断模式） */
    if (config->use_interrupt)
    {
        /* 使能接收中断 */
        can_interrupt_enable(s_can_ctrl[port].can_periph, CAN_INT_RFNE0);
        can_interrupt_enable(s_can_ctrl[port].can_periph, CAN_INT_RFNE1);

        /* 使能发送中断 */
        can_interrupt_enable(s_can_ctrl[port].can_periph, CAN_INT_TME);

        /* 使能错误中断 */
        can_interrupt_enable(s_can_ctrl[port].can_periph, CAN_INT_ERR);

        /* 使能NVIC */
        if (port == DRV_CAN_PORT_CAN0)
        {
            nvic_irq_enable(CAN0_RX0_IRQn, 2, 0);
            nvic_irq_enable(CAN0_RX1_IRQn, 2, 0);
            nvic_irq_enable(CAN0_TX_IRQn, 2, 0);
            nvic_irq_enable(CAN0_EWMC_IRQn, 2, 0);
        }
        else
        {
            nvic_irq_enable(CAN1_RX0_IRQn, 2, 0);
            nvic_irq_enable(CAN1_RX1_IRQn, 2, 0);
            nvic_irq_enable(CAN1_TX_IRQn, 2, 0);
            nvic_irq_enable(CAN1_EWMC_IRQn, 2, 0);
        }
    }

    return DRV_CAN_ERR_OK;
}

/*******************************************************************************
 * @brief   反初始化CAN外设
 * @param   port    CAN端口号
 ******************************************************************************/
static void _drv_can_periph_deinit(drv_can_port_e port)
{
    /* Step 1: 禁用NVIC中断（仅在使用中断时） */
    if (s_can_ctrl[port].config.use_interrupt)
    {
        if (port == DRV_CAN_PORT_CAN0)
        {
            nvic_irq_disable(CAN0_RX0_IRQn);
            nvic_irq_disable(CAN0_RX1_IRQn);
            nvic_irq_disable(CAN0_TX_IRQn);
            nvic_irq_disable(CAN0_EWMC_IRQn);
        }
        else
        {
            nvic_irq_disable(CAN1_RX0_IRQn);
            nvic_irq_disable(CAN1_RX1_IRQn);
            nvic_irq_disable(CAN1_TX_IRQn);
            nvic_irq_disable(CAN1_EWMC_IRQn);
        }
    }

    /* Step 2: 禁用CAN FD功能 */
    can_fd_function_disable(s_can_ctrl[port].can_periph);

    /* Step 3: 禁用CAN时钟 */
    rcu_periph_clock_disable(s_can_ctrl[port].rcu_can);

    /* Step 4: GPIO反初始化（所有模式都需要反初始化，包括LOOPBACK） */
    _drv_can_gpio_deinit(port);

    DRV_CAN_LOGI("CAN%d periph deinit", port);
}

/*******************************************************************************
 * 公开API函数实现
 ******************************************************************************/

/*******************************************************************************
 * @brief   初始化CAN端口
 * @param   config  指向CAN配置结构体的指针
 * @return  DRV_CAN_ERR_OK: 成功，其他: 失败
 ******************************************************************************/
int drv_can_init(const drv_can_config_t *config)
{
    drv_can_port_e port;
    int ret;

    /* 参数校验 */
    if (config == NULL)
    {
        DRV_CAN_LOGE("NULL config pointer");
        return DRV_CAN_ERR_INVALID_PARAM;
    }

    port = config->port;
    if (port >= DRV_CAN_PORT_MAX)
    {
        DRV_CAN_LOGE("Invalid port: %d", port);
        return DRV_CAN_ERR_INVALID_PARAM;
    }

    /* 检查是否已初始化 */
    if (s_can_ctrl[port].state != DRV_CAN_STATE_UNINIT)
    {
        DRV_CAN_LOGW("CAN%d already initialized, state=%d", port, s_can_ctrl[port].state);
        return DRV_CAN_ERR_FAILED;
    }

    /* 初始化控制块 */
    memset(&s_can_ctrl[port], 0, sizeof(drv_can_ctrl_t));
    s_can_ctrl[port].config = *config;
    s_can_ctrl[port].state = DRV_CAN_STATE_UNINIT;

    /* 设置CAN外设基地址和时钟 */
    if (port == DRV_CAN_PORT_CAN0)
    {
        s_can_ctrl[port].can_periph = CAN0;
        s_can_ctrl[port].rcu_can = RCU_CAN0;
    }
    else
    {
        s_can_ctrl[port].can_periph = CAN1;
        s_can_ctrl[port].rcu_can = RCU_CAN1;
    }

    /* 创建互斥锁 */
    if (config->use_mutex)
    {
        s_can_ctrl[port].mutex = xSemaphoreCreateMutex();
        if (s_can_ctrl[port].mutex == NULL)
        {
            DRV_CAN_LOGE("CAN%d create mutex failed", port);
            return DRV_CAN_ERR_FAILED;
        }
    }

    /* 初始化CAN外设（GPIO初始化在_drv_can_periph_init内部根据mode决定） */
    ret = _drv_can_periph_init(port, config);
    if (ret != DRV_CAN_ERR_OK)
    {
        DRV_CAN_LOGE("CAN%d periph init failed", port);
        goto cleanup_mutex;
    }

    s_can_ctrl[port].state = DRV_CAN_STATE_ACTIVE;

    DRV_CAN_LOGI("CAN%d init OK", port);
    return DRV_CAN_ERR_OK;

cleanup_mutex:
    if (s_can_ctrl[port].mutex != NULL)
    {
        vSemaphoreDelete(s_can_ctrl[port].mutex);
        s_can_ctrl[port].mutex = NULL;
    }

    return DRV_CAN_ERR_FAILED;
}

/*******************************************************************************
 * @brief   反初始化CAN端口
 * @param   port    CAN端口号
 * @return  DRV_CAN_ERR_OK: 成功，其他: 失败
 ******************************************************************************/
int drv_can_deinit(drv_can_port_e port)
{
    /* 参数校验 */
    if (port >= DRV_CAN_PORT_MAX)
    {
        DRV_CAN_LOGE("Invalid port: %d", port);
        return DRV_CAN_ERR_INVALID_PARAM;
    }

    /* 快速检查状态 */
    if (s_can_ctrl[port].state == DRV_CAN_STATE_UNINIT)
    {
        DRV_CAN_LOGW("CAN%d not initialized", port);
        return DRV_CAN_ERR_NOT_READY;
    }

    /* 获取互斥锁 */
    if (s_can_ctrl[port].mutex != NULL)
    {
        if (xSemaphoreTake(s_can_ctrl[port].mutex, portMAX_DELAY) != pdTRUE)
        {
            return DRV_CAN_ERR_FAILED;
        }
    }

    /* 二次检查状态 */
    if (s_can_ctrl[port].state == DRV_CAN_STATE_UNINIT)
    {
        DRV_CAN_LOGW("CAN%d state changed during deinit", port);
        goto exit;
    }

    /* 反初始化CAN外设（包含GPIO反初始化） */
    _drv_can_periph_deinit(port);

    /* 删除互斥锁 */
    if (s_can_ctrl[port].mutex != NULL)
    {
        vSemaphoreDelete(s_can_ctrl[port].mutex);
        s_can_ctrl[port].mutex = NULL;
    }

    /* 清除控制块 */
    memset(&s_can_ctrl[port], 0, sizeof(drv_can_ctrl_t));
    s_can_ctrl[port].state = DRV_CAN_STATE_UNINIT;

    DRV_CAN_LOGI("CAN%d deinit OK", port);
    return DRV_CAN_ERR_OK;

exit:
    if (s_can_ctrl[port].mutex != NULL)
    {
        xSemaphoreGive(s_can_ctrl[port].mutex);
    }

    return DRV_CAN_ERR_FAILED;
}

/*******************************************************************************
 * @brief   发送CAN帧（阻塞，轮询模式）
 * @param   port    CAN端口号
 * @param   frame   指向CAN帧结构体的指针
 * @return  DRV_CAN_ERR_OK: 成功，其他: 失败
 ******************************************************************************/
int drv_can_send(drv_can_port_e port, const drv_can_frame_t *frame)
{
    can_transmit_message_struct tx_message;
    uint8_t mailbox;
    int ret = DRV_CAN_ERR_OK;

    /* 参数校验 */
    if (port >= DRV_CAN_PORT_MAX || frame == NULL)
    {
        DRV_CAN_LOGE("Invalid param");
        return DRV_CAN_ERR_INVALID_PARAM;
    }

    /* 获取互斥锁 */
    if (s_can_ctrl[port].mutex != NULL)
    {
        if (xSemaphoreTake(s_can_ctrl[port].mutex, portMAX_DELAY) != pdTRUE)
        {
            return DRV_CAN_ERR_FAILED;
        }
    }

    /* 检查状态 */
    if (s_can_ctrl[port].state != DRV_CAN_STATE_ACTIVE)
    {
        DRV_CAN_LOGW("CAN%d not active", port);
        ret = DRV_CAN_ERR_NOT_READY;
        goto exit;
    }

    /* 构造发送消息 */
    can_struct_para_init(CAN_TX_MESSAGE_STRUCT, &tx_message);

    /* 设置帧格式 */
    if (frame->frame_type == DRV_CAN_FRAME_STANDARD)
    {
        tx_message.tx_sfid = frame->id & 0x7FF;
        tx_message.tx_ft = CAN_FT_DATA;
    }
    else
    {
        tx_message.tx_efid = frame->id & 0x1FFFFFFF;
        tx_message.tx_ff = CAN_FF_EXTENDED;
        tx_message.tx_ft = CAN_FT_DATA;
    }

    /* 验证DLC范围 */
    if (frame->format == DRV_CAN_FORMAT_CANFD)
    {
        /* CAN FD: DLC 0-64字节，但DLC编码为0-15 */
        if (frame->dlc > 64)
        {
            DRV_CAN_LOGE("CAN%d invalid DLC %d for CAN FD", port, frame->dlc);
            ret = DRV_CAN_ERR_INVALID_PARAM;
            goto exit;
        }
    }
    else
    {
        /* CAN 2.0B: DLC 0-8字节 */
        if (frame->dlc > 8)
        {
            DRV_CAN_LOGE("CAN%d invalid DLC %d for CAN 2.0B", port, frame->dlc);
            ret = DRV_CAN_ERR_INVALID_PARAM;
            goto exit;
        }
    }

    /* 设置数据长度 */
    tx_message.tx_dlen = frame->dlc;

    /* 复制数据 */
    memcpy(tx_message.tx_data, frame->data, frame->dlc);

    /* CAN FD标志 */
    if (frame->format == DRV_CAN_FORMAT_CANFD)
    {
        tx_message.fd_flag = 1;
        tx_message.fd_brs = frame->fd_brs;
        tx_message.fd_esi = frame->fd_esi;
    }

    /* 发送消息 */
    mailbox = can_message_transmit(s_can_ctrl[port].can_periph, &tx_message);
    if (mailbox == CAN_TRANSMIT_NOMAILBOX)
    {
        DRV_CAN_LOGE("CAN%d no free mailbox", port);
        ret = DRV_CAN_ERR_FAILED;
        goto exit;
    }

    /* 轮询模式：等待发送完成 */
    if (!s_can_ctrl[port].config.use_interrupt)
    {
        /* 检查发送 mailbox 为空 */
        ret = _drv_can_wait_flag(port, CAN_FLAG_TME0 + mailbox, s_can_ctrl[port].config.timeout_ms);
        if (ret != DRV_CAN_ERR_OK)
        {
            DRV_CAN_LOGE("CAN%d send timeout, mailbox=%d", port, mailbox);
            goto exit;
        }
    }

exit:
    if (s_can_ctrl[port].mutex != NULL)
    {
        xSemaphoreGive(s_can_ctrl[port].mutex);
    }

    return ret;
}

/*******************************************************************************
 * @brief   内部无锁接收函数（仅供ISR调用）
 * @param   port        CAN端口号
 * @param   frame       指向CAN帧结构体的指针
 * @param   fifo_number FIFO编号（0或1）
 * @return  DRV_CAN_ERR_OK: 成功，其他: 失败
 * @note    此函数不获取互斥锁，仅用于中断服务函数
 ******************************************************************************/
int _drv_can_receive_no_lock(drv_can_port_e port, drv_can_frame_t *frame, uint8_t fifo_number)
{
    can_receive_message_struct rx_message;
    int ret = DRV_CAN_ERR_OK;

    /* 基本参数校验（ISR调用也需保证安全性） */
    if (port >= DRV_CAN_PORT_MAX || frame == NULL || fifo_number > 1)
    {
        return DRV_CAN_ERR_INVALID_PARAM;
    }

    /* 检查状态（防止suspend期间ISR触发） */
    if (s_can_ctrl[port].state != DRV_CAN_STATE_ACTIVE)
    {
        return DRV_CAN_ERR_NOT_READY;
    }

    /* 检查FIFO是否有数据 */
    if (can_receive_message_length_get(s_can_ctrl[port].can_periph, fifo_number) == 0)
    {
        return DRV_CAN_ERR_TIMEOUT;
    }

    /* 接收消息 */
    can_struct_para_init(CAN_RX_MESSAGE_STRUCT, &rx_message);
    can_message_receive(s_can_ctrl[port].can_periph, fifo_number, &rx_message);

    /* 解析帧 */
    if (rx_message.rx_ff == CAN_FF_STANDARD)
    {
        frame->id = rx_message.rx_sfid;
        frame->frame_type = DRV_CAN_FRAME_STANDARD;
    }
    else
    {
        frame->id = rx_message.rx_efid;
        frame->frame_type = DRV_CAN_FRAME_EXTENDED;
    }

    frame->dlc = rx_message.rx_dlen;
    frame->format = (rx_message.fd_flag == 1) ? DRV_CAN_FORMAT_CANFD : DRV_CAN_FORMAT_CAN20B;
    frame->fd_brs = rx_message.fd_brs;
    frame->fd_esi = rx_message.fd_esi;
    memcpy(frame->data, rx_message.rx_data, rx_message.rx_dlen);

    /* 释放FIFO空间 */
    can_fifo_release(s_can_ctrl[port].can_periph, fifo_number);

    return ret;
}

/*******************************************************************************
 * @brief   接收CAN帧（轮询模式）
 * @param   port        CAN端口号
 * @param   frame       指向CAN帧结构体的指针
 * @param   fifo_number FIFO编号（0或1）
 * @return  DRV_CAN_ERR_OK: 成功，其他: 失败
 ******************************************************************************/
int drv_can_receive(drv_can_port_e port, drv_can_frame_t *frame, uint8_t fifo_number)
{
    int ret = DRV_CAN_ERR_OK;

    /* 参数校验 */
    if (port >= DRV_CAN_PORT_MAX || frame == NULL || fifo_number > 1)
    {
        DRV_CAN_LOGE("Invalid param");
        return DRV_CAN_ERR_INVALID_PARAM;
    }

    /* 获取互斥锁 */
    if (s_can_ctrl[port].mutex != NULL)
    {
        if (xSemaphoreTake(s_can_ctrl[port].mutex, portMAX_DELAY) != pdTRUE)
        {
            return DRV_CAN_ERR_FAILED;
        }
    }

    /* 检查状态 */
    if (s_can_ctrl[port].state != DRV_CAN_STATE_ACTIVE)
    {
        DRV_CAN_LOGW("CAN%d not active", port);
        ret = DRV_CAN_ERR_NOT_READY;
        goto exit;
    }

    /* 调用无锁接收函数 */
    ret = _drv_can_receive_no_lock(port, frame, fifo_number);

    DRV_CAN_LOGD("CAN%d recv OK, ID=0x%08lX, DLC=%d, FIFO=%d", port, frame->id, frame->dlc, fifo_number);

exit:
    if (s_can_ctrl[port].mutex != NULL)
    {
        xSemaphoreGive(s_can_ctrl[port].mutex);
    }

    return ret;
}

/*******************************************************************************
 * @brief   配置CAN过滤器
 * @param   port            CAN端口号
 * @param   filter_config   指向过滤器配置结构体的指针
 * @return  DRV_CAN_ERR_OK: 成功，其他: 失败
 ******************************************************************************/
int drv_can_config_filter(drv_can_port_e port, const drv_can_filter_config_t *filter_config)
{
    can_filter_parameter_struct filter_param;
    uint32_t can_periph;
    int ret = DRV_CAN_ERR_OK;

    /* 参数校验 */
    if (port >= DRV_CAN_PORT_MAX || filter_config == NULL)
    {
        DRV_CAN_LOGE("Invalid param");
        return DRV_CAN_ERR_INVALID_PARAM;
    }

    /* 获取互斥锁 */
    if (s_can_ctrl[port].mutex != NULL)
    {
        if (xSemaphoreTake(s_can_ctrl[port].mutex, portMAX_DELAY) != pdTRUE)
        {
            return DRV_CAN_ERR_FAILED;
        }
    }

    /* 检查状态 */
    if (s_can_ctrl[port].state != DRV_CAN_STATE_ACTIVE)
    {
        DRV_CAN_LOGW("CAN%d not active", port);
        ret = DRV_CAN_ERR_NOT_READY;
        goto exit;
    }

    /* 检查过滤器编号 */
    if (port == DRV_CAN_PORT_CAN0 && filter_config->filter_bank > 13)
    {
        DRV_CAN_LOGE("CAN0 filter bank must be 0-13");
        ret = DRV_CAN_ERR_INVALID_PARAM;
        goto exit;
    }
    if (port == DRV_CAN_PORT_CAN1 && filter_config->filter_bank < 14)
    {
        DRV_CAN_LOGE("CAN1 filter bank must be 14-27");
        ret = DRV_CAN_ERR_INVALID_PARAM;
        goto exit;
    }

    can_periph = s_can_ctrl[port].can_periph;

    /* 配置过滤器 */
    can_struct_para_init(CAN_FILTER_STRUCT, &filter_param);

    filter_param.filter_list_high = filter_config->filter_id_high & 0xFFFF;
    filter_param.filter_list_low = filter_config->filter_id_low & 0xFFFF;
    filter_param.filter_mask_high = filter_config->filter_mask_high & 0xFFFF;
    filter_param.filter_mask_low = filter_config->filter_mask_low & 0xFFFF;
    filter_param.filter_fifo_number = filter_config->fifo_number;
    filter_param.filter_number = filter_config->filter_bank;
    filter_param.filter_mode = (filter_config->filter_mode == DRV_CAN_FILTER_MODE_ID_MASK) ?
                               CAN_FILTERMODE_MASK : CAN_FILTERMODE_LIST;
    filter_param.filter_bits = CAN_FILTERBITS_32BIT;
    filter_param.filter_enable = ENABLE;

    can_filter_init(&filter_param);

    DRV_CAN_LOGI("CAN%d filter %d configured", port, filter_config->filter_bank);

exit:
    if (s_can_ctrl[port].mutex != NULL)
    {
        xSemaphoreGive(s_can_ctrl[port].mutex);
    }

    return ret;
}

/*******************************************************************************
 * @brief   禁用CAN过滤器
 * @param   port        CAN端口号
 * @param   filter_bank 过滤器组编号
 * @return  DRV_CAN_ERR_OK: 成功，其他: 失败
 ******************************************************************************/
int drv_can_disable_filter(drv_can_port_e port, uint8_t filter_bank)
{
    int ret = DRV_CAN_ERR_OK;

    if (port >= DRV_CAN_PORT_MAX)
    {
        DRV_CAN_LOGE("Invalid port");
        return DRV_CAN_ERR_INVALID_PARAM;
    }

    /* 获取互斥锁 */
    if (s_can_ctrl[port].mutex != NULL)
    {
        if (xSemaphoreTake(s_can_ctrl[port].mutex, portMAX_DELAY) != pdTRUE)
        {
            return DRV_CAN_ERR_FAILED;
        }
    }

    /* 检查状态 */
    if (s_can_ctrl[port].state != DRV_CAN_STATE_ACTIVE)
    {
        DRV_CAN_LOGW("CAN%d not active", port);
        ret = DRV_CAN_ERR_NOT_READY;
        goto exit;
    }

    /* GD32库没有直接的filter disable API，通过配置filter_enable=DISABLE实现 */
    can_filter_parameter_struct filter_param;
    can_struct_para_init(CAN_FILTER_STRUCT, &filter_param);
    filter_param.filter_enable = DISABLE;
    filter_param.filter_number = filter_bank;
    can_filter_init(&filter_param);

    DRV_CAN_LOGI("CAN%d filter %d disabled", port, filter_bank);

exit:
    if (s_can_ctrl[port].mutex != NULL)
    {
        xSemaphoreGive(s_can_ctrl[port].mutex);
    }

    return ret;
}

/*******************************************************************************
 * @brief 注册接收回调函数（中断模式）
 * @param port CAN端口号
 * @param callback 回调函数指针
 * @return DRV_CAN_ERR_OK: 成功，其他: 失败
 * @note 回调函数原型：void can_rx_callback(drv_can_port_e port, drv_can_frame_t *frame, uint8_t fifo)
 ******************************************************************************/
 int drv_can_register_rx_callback(drv_can_port_e port, void (*callback)(drv_can_port_e, drv_can_frame_t *, uint8_t))
{
    if (port >= DRV_CAN_PORT_MAX)
    {
        return DRV_CAN_ERR_INVALID_PARAM;
    }

    s_can_ctrl[port].rx_callback = callback;
    return DRV_CAN_ERR_OK;
}

/*******************************************************************************
 * @brief 注册发送完成回调函数（中断模式）
 * @param port CAN端口号
 * @param callback 回调函数指针
 * @return DRV_CAN_ERR_OK: 成功，其他: 失败
 * @note 回调函数原型：void can_tx_callback(drv_can_port_e port, uint8_t mailbox)
 ******************************************************************************/
int drv_can_register_tx_callback(drv_can_port_e port, void (*callback)(drv_can_port_e, uint8_t))
{
    if (port >= DRV_CAN_PORT_MAX)
    {
        return DRV_CAN_ERR_INVALID_PARAM;
    }

    s_can_ctrl[port].tx_callback = callback;
    return DRV_CAN_ERR_OK;
}

/*******************************************************************************
 * @brief 注册错误回调函数
 * @param port CAN端口号
 * @param callback 回调函数指针
 * @return DRV_CAN_ERR_OK: 成功，其他: 失败
 * @note 回调函数原型：void can_err_callback(drv_can_port_e port, drv_can_err_type_e err_type)
 ******************************************************************************/
int drv_can_register_err_callback(drv_can_port_e port, void (*callback)(drv_can_port_e, drv_can_err_type_e))
{
    if (port >= DRV_CAN_PORT_MAX)
    {
        return DRV_CAN_ERR_INVALID_PARAM;
    }

    s_can_ctrl[port].err_callback = callback;
    return DRV_CAN_ERR_OK;
}

/*******************************************************************************
 * @brief 查询发送邮箱空闲状态
 * @param port CAN端口号
 * @return 空闲邮箱数量（0-3），负数表示错误
 ******************************************************************************/
int drv_can_get_tx_mailbox_free(drv_can_port_e port)
{
    uint32_t tstat_reg;
    int free_count = 0;

    if (port >= DRV_CAN_PORT_MAX)
    {
        return DRV_CAN_ERR_INVALID_PARAM;
    }

    /* 直接读取TSTAT寄存器一次，检查3个邮箱状态位 */
    tstat_reg = CAN_TSTAT(s_can_ctrl[port].can_periph);

    if (tstat_reg & CAN_TSTAT_TME0)
    {
        free_count++;
    }

    if (tstat_reg & CAN_TSTAT_TME1)
    {
        free_count++;
    }

    if (tstat_reg & CAN_TSTAT_TME2)
    {
        free_count++;
    }

    return free_count;
}

/*******************************************************************************
 * @brief 查询CAN总线状态
 * @param port CAN端口号
 * @return CAN总线状态枚举值
 ******************************************************************************/
drv_can_state_e drv_can_get_bus_status(drv_can_port_e port)
{
    if (port >= DRV_CAN_PORT_MAX)
    {
        return DRV_CAN_STATE_UNINIT;
    }

    return s_can_ctrl[port].state;
}

/*******************************************************************************
 * @brief 查询接收FIFO中的消息数量
 * @param port CAN端口号
 * @param fifo_number FIFO编号（0或1）
 * @return FIFO中的消息数量（0-3），负数表示错误
 ******************************************************************************/
int drv_can_get_rx_message_count(drv_can_port_e port, uint8_t fifo_number)
{
    if (port >= DRV_CAN_PORT_MAX || fifo_number > 1)
    {
        return DRV_CAN_ERR_INVALID_PARAM;
    }

    if (fifo_number == 0)
    {
        return can_receive_message_length_get(s_can_ctrl[port].can_periph, CAN_FIFO0);
    }

    return can_receive_message_length_get(s_can_ctrl[port].can_periph, CAN_FIFO1);
}

/*******************************************************************************
 * @brief 挂起CAN端口
 * @param port CAN端口号
 * @return DRV_CAN_ERR_OK: 成功，其他: 失败
 ******************************************************************************/
int drv_can_suspend(drv_can_port_e port)
{
    int ret = DRV_CAN_ERR_FAILED;

    if (port >= DRV_CAN_PORT_MAX)
    {
        return DRV_CAN_ERR_INVALID_PARAM;
    }

    if (s_can_ctrl[port].state != DRV_CAN_STATE_ACTIVE)
    {
        return DRV_CAN_ERR_NOT_READY;
    }

    if (s_can_ctrl[port].mutex != NULL)
    {
        if (xSemaphoreTake(s_can_ctrl[port].mutex, portMAX_DELAY) != pdTRUE)
        {
            return DRV_CAN_ERR_FAILED;
        }
    }

    if (s_can_ctrl[port].state != DRV_CAN_STATE_ACTIVE)
    {
        ret = DRV_CAN_ERR_NOT_READY;
        goto exit;
    }

    /* 统一调用periph_deinit，确保完整清理资源（包括NVIC、CAN功能、时钟、GPIO） */
    _drv_can_periph_deinit(port);
    s_can_ctrl[port].state = DRV_CAN_STATE_SUSPENDED;

    DRV_CAN_LOGI("CAN%d suspended", port);
    ret = DRV_CAN_ERR_OK;

exit:
    if (s_can_ctrl[port].mutex != NULL)
    {
        xSemaphoreGive(s_can_ctrl[port].mutex);
    }

    return ret;
}

/*******************************************************************************
 * @brief 恢复CAN端口
 * @param port CAN端口号
 * @return DRV_CAN_ERR_OK: 成功，其他: 失败
 ******************************************************************************/
int drv_can_resume(drv_can_port_e port)
{
    int ret;

    if (port >= DRV_CAN_PORT_MAX)
    {
        return DRV_CAN_ERR_INVALID_PARAM;
    }

    if (s_can_ctrl[port].state != DRV_CAN_STATE_SUSPENDED)
    {
        return DRV_CAN_ERR_NOT_READY;
    }

    if (s_can_ctrl[port].mutex != NULL)
    {
        if (xSemaphoreTake(s_can_ctrl[port].mutex, portMAX_DELAY) != pdTRUE)
        {
            return DRV_CAN_ERR_FAILED;
        }
    }

    if (s_can_ctrl[port].state != DRV_CAN_STATE_SUSPENDED)
    {
        ret = DRV_CAN_ERR_NOT_READY;
        goto exit;
    }

    /* 调用periph_init，内部会处理GPIO初始化和CAN外设配置 */
    ret = _drv_can_periph_init(port, &s_can_ctrl[port].config);
    if (ret != DRV_CAN_ERR_OK)
    {
        DRV_CAN_LOGE("CAN%d resume periph init failed", port);
        _drv_can_periph_deinit(port);  /* 统一调用periph_deinit，确保完整回滚 */
        goto exit;
    }

    s_can_ctrl[port].state = DRV_CAN_STATE_ACTIVE;

    DRV_CAN_LOGI("CAN%d resumed", port);
    ret = DRV_CAN_ERR_OK;

exit:
    if (s_can_ctrl[port].mutex != NULL)
    {
        xSemaphoreGive(s_can_ctrl[port].mutex);
    }

    return ret;
}

/*******************************************************************************
 * @brief 内部回调执行函数（供ISR调用）
 * @param port CAN端口号
 * @param frame 接收帧指针
 * @param fifo FIFO编号
 ******************************************************************************/
void drv_can_run_rx_callback(drv_can_port_e port, drv_can_frame_t *frame, uint8_t fifo)
{
    if (port < DRV_CAN_PORT_MAX && s_can_ctrl[port].rx_callback != NULL)
    {
        s_can_ctrl[port].rx_callback(port, frame, fifo);
    }
}

/*******************************************************************************
 * @brief 内部回调执行函数（供ISR调用）
 * @param port CAN端口号
 * @param mailbox 发送邮箱编号
 ******************************************************************************/
void drv_can_run_tx_callback(drv_can_port_e port, uint8_t mailbox)
{
    if (port < DRV_CAN_PORT_MAX && s_can_ctrl[port].tx_callback != NULL)
    {
        s_can_ctrl[port].tx_callback(port, mailbox);
    }
}

/*******************************************************************************
 * @brief 内部回调执行函数（供ISR调用）
 * @param port CAN端口号
 * @param err_type 错误类型
 ******************************************************************************/
void drv_can_run_err_callback(drv_can_port_e port, drv_can_err_type_e err_type)
{
    if (port < DRV_CAN_PORT_MAX && s_can_ctrl[port].err_callback != NULL)
    {
        s_can_ctrl[port].err_callback(port, err_type);
    }
}
