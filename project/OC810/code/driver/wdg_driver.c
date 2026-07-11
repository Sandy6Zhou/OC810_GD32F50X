/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       wdg_driver.c
**文件描述：       看门狗驱动模块实现文件（FWDGT + WWDGT）
**当前版本：       V1.0
**作    者：       伍玉蛟 (wuyujiao@jimiiot.com)
**完成日期：       2026.07.02
*********************************************************************
** 功能描述：       1. 实现GD32F505 FWDGT独立看门狗驱动
**                 2. 实现GD32F505 WWDGT窗口看门狗驱动
**                 3. 两个子系统独立API，可同时使用
**                 4. 自动计算预分频和计数器参数
**                 5. FWDGT支持配置、喂狗、超时时间获取、调试
**                 6. WWDGT支持配置、喂狗、调试
*********************************************************************/

#include "wdg_driver.h"
#include "gd32f50x_fwdgt.h"
#include "gd32f50x_wwdgt.h"
#include "gd32f50x_dbg.h"
#include "my_os.h"

/*********************************************************************
 * 内部宏定义
 *********************************************************************/

/* WWDGT最小窗口步数：CNT与WIN之差不得小于此值，防止喂狗窗口过窄 */
#define DRV_WWDG_MIN_WIN_STEP     (4U)
/* WWDGT步数基准：CNT递减到0x3F触发复位，总步数 = CNT - 0x3F */
#define DRV_WWDG_STEP_BASE        (DRV_WWDG_CNT_MIN - 1U)

/* FWDGT硬件超时极限（P0新增上下限宏） */
#define FWDGT_TIMEOUT_MIN_MS      (1U)
#define FWDGT_TIMEOUT_MAX_MS      (3354624U)

/*********************************************************************
 * 内部全局常量与变量
 *********************************************************************/

/* FWDGT预分频表（14级）：4, 8, 16, ..., 32768 */
static const uint16_t s_fwdg_psc_table[] = {
    4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768
};

#if DRV_WWDG_ENABLE
/* WWDGT预分频表（19级）：索引i = PSC值，对应分频系数2^i */
static const uint32_t s_wwdg_psc_div_table[] = {
    1U,        2U,       4U,       8U,       16U,      32U,      64U,      128U,
    256U,      512U,     1024U,    2048U,    4096U,    8192U,    16384U,   32768U,
    65536U,    131072U,  262144U
};

/* WWDGT预分频值表（19级）：索引i = PSC值，对应分频系数2^i */
static const uint32_t s_wwdg_psc_value_table[] = {
    WWDGT_CFG_PSC_DIV1,    WWDGT_CFG_PSC_DIV2,    WWDGT_CFG_PSC_DIV4,    WWDGT_CFG_PSC_DIV8,
    WWDGT_CFG_PSC_DIV16,   WWDGT_CFG_PSC_DIV32,   WWDGT_CFG_PSC_DIV64,   WWDGT_CFG_PSC_DIV128,
    WWDGT_CFG_PSC_DIV256,  WWDGT_CFG_PSC_DIV512,  WWDGT_CFG_PSC_DIV1024, WWDGT_CFG_PSC_DIV2048,
    WWDGT_CFG_PSC_DIV4096, WWDGT_CFG_PSC_DIV8192, WWDGT_CFG_PSC_DIV16384, WWDGT_CFG_PSC_DIV32768,
    WWDGT_CFG_PSC_DIV65536, WWDGT_CFG_PSC_DIV131072, WWDGT_CFG_PSC_DIV262144
};
#endif /* DRV_WWDG_ENABLE */

/** FWDGT 初始化状态 */
static bool s_drv_fwdg_initialized = false;

/** FWDGT 初始化时选用的预分频系数（用于剩余时间计算） */
static uint32_t s_drv_fwdg_psc_div = 0;

#if DRV_WWDG_ENABLE
/** WWDGT 初始化状态 */
static bool s_drv_wwdg_initialized = false;

/** WWDGT 保存的计数器值（用于喂狗） */
static uint8_t s_drv_wwdg_counter = 0x7F;
#endif /* DRV_WWDG_ENABLE */

/*********************************************************************
 * 内部辅助函数声明
 *********************************************************************/

static int _drv_fwdg_calc_prescaler(uint32_t timeout_ms, uint16_t *psc, uint16_t *rld);

#if DRV_WWDG_ENABLE
static int _drv_wwdg_calc_window(uint32_t timeout_ms, uint32_t window_ms,
                                  uint8_t *win, uint8_t *cnt, uint32_t *psc);
#endif /* DRV_WWDG_ENABLE */

/*********************************************************************
 * FWDGT 内部函数实现
 *********************************************************************/

/*********************************************************************
 * @brief   计算FWDGT预分频和重装载值
 * @param   timeout_ms 超时时间（毫秒）
 * @param   psc 输出参数，预分频寄存器值（0~13）
 * @param   rld 输出参数，重装载值（0x000~0xFFF）
 * @return  0 成功，负数失败
 * @note    公式：超时(ms) = (预分频 × 重装载值) / 40
 * @note    IRC40K = 40kHz，40000/1000 = 40
 * @note    从最大分频向最小分频遍历，优先选大分频+小RLD，预留更多喂狗容错窗口
 * @note    采用向上取整，保证实际超时 ≥ 目标超时
 *********************************************************************/
static int _drv_fwdg_calc_prescaler(uint32_t timeout_ms, uint16_t *psc, uint16_t *rld)
{
    const uint16_t table_size = sizeof(s_fwdg_psc_table) / sizeof(s_fwdg_psc_table[0]);
    int16_t i;              /* 有符号，用于反向遍历终止条件 */
    uint32_t rld_val;
    const uint32_t tick_total = timeout_ms * 40U;

    /* 从最大分频往最小遍历，优先选分频大、RLD小的最优配置 */
    for (i = (int16_t)(table_size - 1); i >= 0; i--)
    {
        /* 向上取整除法，保证实际超时 >= 目标超时 */
        rld_val = (tick_total + s_fwdg_psc_table[i] - 1U) / s_fwdg_psc_table[i];

        /* 允许RLD 0~0xFFF，匹配硬件手册全范围 */
        if (rld_val <= 0xFFFU)
        {
            *psc = i;
            *rld = (uint16_t)rld_val;
            return DRV_FWDG_ERR_OK;
        }
    }

    return DRV_FWDG_ERR_INVALID_PARAM;
}

/*********************************************************************
 * WWDGT 内部函数实现
 *********************************************************************/

#if DRV_WWDG_ENABLE
/*********************************************************************
 * @brief   根据超时时间和窗口时间计算WWDGT参数
 * @param   timeout_ms 超时时间（毫秒），不可为0
 * @param   window_ms 窗口时间（毫秒），0表示不使用窗口限制
 * @param   win 输出参数，窗口寄存器值（0x40~0x7F）
 * @param   cnt 输出参数，计数器初始值（0x40~0x7F）
 * @param   psc 输出参数，预分频寄存器编码值（对应WWDGT_CFG_PSC_DIVx）
 * @return  0 成功，负数失败
 * @note    公式：tWWDGT = tPCLK1 × 4096 × 2^PSC × (CNT[5:0]+1) (ms)
 * @note    GD32F50x WWDGT预分频由PSCH[12:10]+PSCL[8:7]共5位编码，2^PSC范围：2^0~2^18
 * @note    采用靠近法：从小到大遍历PSC，找第一个最大范围覆盖目标的分频，
 *          用CNT=0x7F或计算值匹配目标超时，实际超时宜大不宜小
 * @note    WIN按时间比例计算：WIN = 0x3F + (CNT-0x3F) × window_ms / timeout_ms
 *********************************************************************/
static int _drv_wwdg_calc_window(uint32_t timeout_ms, uint32_t window_ms,
                                  uint8_t *win, uint8_t *cnt, uint32_t *psc)
{
    const uint16_t table_size = sizeof(s_wwdg_psc_div_table) / sizeof(s_wwdg_psc_div_table[0]);

    uint32_t pclk1_hz;
    uint64_t base_tick_us;
    uint64_t target_tick_us;
    uint16_t i;
    uint32_t cnt_val;
    uint32_t win_val;
    uint32_t total_step;
    uint32_t win_step;

    /* 入参合法性校验 */
    if ((win == NULL) || (cnt == NULL) || (psc == NULL))
    {
        return DRV_WWDG_ERR_INVALID_PARAM;
    }

    /* 获取PCLK1频率 */
    pclk1_hz = rcu_clock_freq_get(CK_APB1);
    if (pclk1_hz == 0U)
    {
        return DRV_WWDG_ERR_FAILED;
    }

    if (window_ms > timeout_ms)
    {
        return DRV_WWDG_ERR_INVALID_PARAM;
    }

    target_tick_us = timeout_ms * 1000ULL;

    /* 从小到大遍历预分频，寻找最接近目标超时的配置 */
    for (i = 0; i < table_size; i++)
    {
        /* 当前分频下单步tick耗时(us) = 4096 × 2^PSC × 1000000 / fPCLK1 */
        base_tick_us = 1000000ULL * 4096ULL * s_wwdg_psc_div_table[i] / pclk1_hz;

        /* 条件1：最大计数范围（CNT=0x7F）的总超时 <= 目标超时，当前分频范围不够，跳过 */
        if (target_tick_us >= base_tick_us * (DRV_WWDG_CNT_MAX - DRV_WWDG_CNT_MIN + 1ULL))
        {
            continue;
        }

        /* 条件2：单步耗时 > 目标超时，1步即超出，使用最小计数值 */
        if (base_tick_us > target_tick_us)
        {
            cnt_val = DRV_WWDG_CNT_MIN;
        }
        else
        {
            /* 计算CNT初始值 */
            cnt_val = (target_tick_us - base_tick_us) / base_tick_us + DRV_WWDG_CNT_MIN;
            if (cnt_val > DRV_WWDG_CNT_MAX)
            {
                cnt_val = DRV_WWDG_CNT_MAX;
            }
        }

        /* 计算WIN窗口寄存器值 */
        if (window_ms == 0U)
        {
            win_val = DRV_WWDG_CNT_MIN;
        }
        else
        {
            total_step = cnt_val - DRV_WWDG_STEP_BASE;
            win_step = total_step * window_ms / timeout_ms;
            win_val = DRV_WWDG_STEP_BASE + win_step;
        }

        /* 硬件约束：WIN < CNT */
        if (win_val < cnt_val)
        {
            *psc = s_wwdg_psc_value_table[i];
            *cnt = (uint8_t)cnt_val;
            *win = (uint8_t)win_val;

            return DRV_WWDG_ERR_OK;
        }
    }

    return DRV_WWDG_ERR_INVALID_PARAM;
}
#endif /* DRV_WWDG_ENABLE */

/*********************************************************************
 * FWDGT 公开API实现
 *********************************************************************/

/*********************************************************************
 * @brief   初始化独立看门狗（FWDGT）
 * @param   config 配置结构体指针
 * @return  0 成功，负数失败（见 DRV_FWDG_ERR_*）
 * @note    FWDGT 一旦启用无法关闭，只能复位
 *********************************************************************/
int drv_fwdg_init(const drv_fwdg_config_t *config)
{
    uint16_t psc, rld;
    int ret;

    if (config == NULL)
    {
        return DRV_FWDG_ERR_INVALID_PARAM;
    }

    if (config->timeout_ms == 0U)
    {
        return DRV_FWDG_ERR_INVALID_PARAM;
    }

    /* 硬件超时上下限校验 */
    if ((config->timeout_ms < FWDGT_TIMEOUT_MIN_MS) || (config->timeout_ms > FWDGT_TIMEOUT_MAX_MS))
    {
        DRV_WDG_LOGE("FWDGT timeout out of range! min=%ums, max=%ums, input=%u",
                     FWDGT_TIMEOUT_MIN_MS, FWDGT_TIMEOUT_MAX_MS, config->timeout_ms);
        return DRV_FWDG_ERR_INVALID_PARAM;
    }

    /* 计算预分频和重装载值 */
    ret = _drv_fwdg_calc_prescaler(config->timeout_ms, &psc, &rld);
    if (ret != DRV_FWDG_ERR_OK)
    {
        DRV_WDG_LOGE("FWDGT calc prescaler failed, timeout=%u", config->timeout_ms);
        return ret;
    }

    DRV_WDG_LOGI("FWDGT init: timeout=%ums, psc=%u, rld=0x%03X", config->timeout_ms, psc, rld);

    /* 使能FWDGT */
    fwdgt_enable();

    /* 设置预分频和重装载值（内部已含解锁写保护、等待PUD/RUD同步） */
    if (SUCCESS != fwdgt_config(rld, psc))
    {
        DRV_WDG_LOGE("FWDGT config failed, timeout=%u", config->timeout_ms);
        return DRV_FWDG_ERR_FAILED;
    }

    /* 配置调试模式 */
    if (config->stop_in_debug)
    {
        dbg_periph_enable(DBG_FWDGT_HOLD);
    }

    /* 保存预分频系数，用于剩余时间计算 */
    s_drv_fwdg_psc_div = s_fwdg_psc_table[psc];

    s_drv_fwdg_initialized = true;

    return DRV_FWDG_ERR_OK;
}

/*********************************************************************
 * @brief   FWDGT 喂狗（重装载计数器）
 * @return  0 成功，负数失败
 * @note    必须在超时前调用，否则触发系统复位
 *********************************************************************/
int drv_fwdg_feed(void)
{
    if (!s_drv_fwdg_initialized)
    {
        DRV_WDG_LOGE("FWDGT not initialized");
        return DRV_FWDG_ERR_FAILED;
    }

    fwdgt_counter_reload();

    return DRV_FWDG_ERR_OK;
}

/*********************************************************************
 * @brief   获取 FWDGT 满载超时时间
 * @param   timeout_ms 输出参数，满载超时时间（毫秒）
 * @return  0 成功，负数失败
 * @note    FWDGT硬件不支持读取当前递减计数器值，无法获取实时剩余倒计时。
 *          本函数返回初始化时配置的满载超时时间，供上层参考。
 *          若需剩余时间预估，上层应自行记录喂狗时间戳并计算。
 *********************************************************************/
int drv_fwdg_get_timeout(uint32_t *timeout_ms)
{
    uint16_t rld;

    if (!s_drv_fwdg_initialized)
    {
        DRV_WDG_LOGE("FWDGT not initialized");
        return DRV_FWDG_ERR_FAILED;
    }

    if (timeout_ms == NULL)
    {
        return DRV_FWDG_ERR_INVALID_PARAM;
    }

    /* FWDGT计数器不可读，取重装载值计算满载超时 */
    rld = (uint16_t)(FWDGT_RLD & FWDGT_RLD_RLD);

    /* 满载超时(ms) = 重装载值 × 预分频系数 / 40 */
    *timeout_ms = ((uint32_t)rld * s_drv_fwdg_psc_div) / 40U;

    return DRV_FWDG_ERR_OK;
}

/*********************************************************************
 * @brief   设置 FWDGT 调试模式下是否停止
 * @param   stop true=停止，false=继续
 * @return  0 成功，负数失败
 *********************************************************************/
int drv_fwdg_debug_stop(bool stop)
{
    if (!s_drv_fwdg_initialized)
    {
        DRV_WDG_LOGE("FWDGT not initialized");
        return DRV_FWDG_ERR_FAILED;
    }

    if (stop)
    {
        dbg_periph_enable(DBG_FWDGT_HOLD);
    }
    else
    {
        dbg_periph_disable(DBG_FWDGT_HOLD);
    }

    return DRV_FWDG_ERR_OK;
}

#if DRV_WWDG_ENABLE
/*********************************************************************
 * WWDGT 公开API实现
 *********************************************************************/

/*********************************************************************
 * @brief   初始化窗口看门狗（WWDGT）
 * @param   config 配置结构体指针
 * @return  0 成功，负数失败（见 DRV_WWDG_ERR_*）
 *********************************************************************/
int drv_wwdg_init(const drv_wwdg_config_t *config)
{
    uint8_t win, cnt;
    uint32_t psc;
    int ret;

    if (config == NULL)
    {
        return DRV_WWDG_ERR_INVALID_PARAM;
    }

    if (config->timeout_ms == 0U)
    {
        return DRV_WWDG_ERR_INVALID_PARAM;
    }

    /* 计算窗口、计数器和预分频值 */
    ret = _drv_wwdg_calc_window(config->timeout_ms, config->window_ms, &win, &cnt, &psc);
    if (ret != DRV_WWDG_ERR_OK)
    {
        DRV_WDG_LOGE("WWDGT calc window failed, timeout=%u, window=%u",
                     config->timeout_ms, config->window_ms);
        return ret;
    }

    DRV_WDG_LOGI("WWDGT init: timeout=%ums, window=%ums, cnt=0x%02X, win=0x%02X, psc=0x%03X",
                 config->timeout_ms, config->window_ms, cnt, win, psc);

    my_task_delay_ms(200);

    /* 保存计数器值 */
    s_drv_wwdg_counter = cnt;

    /* 使能WWDGT时钟 */
    rcu_periph_clock_enable(RCU_WWDGT);

    /* 配置计数器、窗口值和预分频 */
    wwdgt_config(cnt, win, psc);

    /* 使能WWDGT */
    wwdgt_enable();

    s_drv_wwdg_initialized = true;

    return DRV_WWDG_ERR_OK;
}

/*********************************************************************
 * @brief   WWDGT 喂狗（重装载计数器）
 * @return  0 成功，负数失败
 * @note    必须在超时前调用，否则触发系统复位
 *********************************************************************/
int drv_wwdg_feed(void)
{
    if (!s_drv_wwdg_initialized)
    {
        DRV_WDG_LOGE("WWDGT not initialized");
        return DRV_WWDG_ERR_FAILED;
    }

    /* 重装载计数器 */
    wwdgt_counter_update(s_drv_wwdg_counter);

    return DRV_WWDG_ERR_OK;
}

/*********************************************************************
 * @brief   设置 WWDGT 调试模式下是否停止
 * @param   stop true=停止，false=继续
 * @return  0 成功
 * @note    stop=true时WWDGT_HOLD置1，调试器暂停内核时WWDGT停止计数
 *********************************************************************/
int drv_wwdg_debug_stop(bool stop)
{
    if (!s_drv_wwdg_initialized)
    {
        DRV_WDG_LOGE("WWDGT not initialized");
        return DRV_WWDG_ERR_FAILED;
    }

    if (stop)
    {
        dbg_periph_enable(DBG_WWDGT_HOLD);
    }
    else
    {
        dbg_periph_disable(DBG_WWDGT_HOLD);
    }

    return DRV_WWDG_ERR_OK;
}
#endif /* DRV_WWDG_ENABLE */
