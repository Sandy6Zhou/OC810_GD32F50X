/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       timer_driver.c
**文件描述：       Timer驱动模块实现文件 (基础定时器功能)
**当前版本：       V1.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.05.07
*********************************************************************
** 功能描述：       1. 实现Timer基础定时功能
**                 2. 实现中断回调注册和统一处理
**                 3. 实现智能时钟管理（自动使能/关闭）
**                 4. 实现与GD32库完全解耦的API接口
*********************************************************************/

#include "timer_driver.h"
#include <string.h>

/*********************************************************************
 * 内部参数校验宏（仅在 .c 文件内使用，对应用层不可见）
 *********************************************************************/

/** 校验 Timer ID 合法性，不合法则返回 ERR_INVALID_ID */
#define TIMER_CHECK_ID(id) \
    do { \
        if ((id) >= DRV_TIMER_MAX) { \
            DRV_TIMER_LOGE("Invalid timer ID: %d", (id)); \
            return DRV_TIMER_ERR_INVALID_ID; \
        } \
    } while(0)

/** 校验 Timer 已初始化，未初始化则返回 ERR_NOT_INITIALIZED */
#define TIMER_CHECK_INIT(id) \
    do { \
        if (!s_timer_ctrl[(id)].is_initialized) { \
            DRV_TIMER_LOGE("Timer %d not initialized", (id)); \
            return DRV_TIMER_ERR_NOT_INITIALIZED; \
        } \
    } while(0)

/*********************************************************************
 * 内部数据结构定义
 *********************************************************************/

/**
 * @brief  Timer控制块（内部使用，应用层不可见）
 */
typedef struct
{
    uint32_t timer_periph;              /**< GD32 Timer基地址 */
    rcu_periph_enum rcu_clk;            /**< RCU时钟标识 */
    bool is_initialized;                /**< 是否已初始化 */
    bool is_running;                    /**< 是否运行中 */
    drv_timer_callback_t callback;      /**< 用户回调函数 */
} drv_timer_ctrl_t;

/** Timer映射表（表驱动设计，提高效率） */
typedef struct
{
    uint32_t periph;                    /**< Timer外设基地址 */
    rcu_periph_enum rcu_clk;            /**< RCU时钟标识 */
} drv_timer_map_t;


/** Timer控制表 */
static drv_timer_ctrl_t s_timer_ctrl[DRV_TIMER_MAX] = {0};

/** Timer映射表（编译期初始化，零运行时开销） */
static const drv_timer_map_t s_timer_map[DRV_TIMER_MAX] =
{
    [DRV_TIMER_0]  = {TIMER0,  RCU_TIMER0},
    [DRV_TIMER_1]  = {TIMER1,  RCU_TIMER1},
    [DRV_TIMER_2]  = {TIMER2,  RCU_TIMER2},
    [DRV_TIMER_3]  = {TIMER3,  RCU_TIMER3},
    [DRV_TIMER_4]  = {TIMER4,  RCU_TIMER4},
    [DRV_TIMER_5]  = {TIMER5,  RCU_TIMER5},
    [DRV_TIMER_6]  = {TIMER6,  RCU_TIMER6},
    [DRV_TIMER_7]  = {TIMER7,  RCU_TIMER7},
    [DRV_TIMER_15] = {TIMER15, RCU_TIMER15},
    [DRV_TIMER_16] = {TIMER16, RCU_TIMER16}
};

/** Timer能力表（编译期初始化） */
static const drv_timer_capability_t s_timer_cap[DRV_TIMER_MAX] =
{
    [DRV_TIMER_0] = {
        .type = DRV_TIMER_TYPE_ADVANCED,
        .bit_width = 16,
        .channel_count = 4,
        .has_complementary = true,
        .has_break = true,
        .has_encoder = true
    },
    [DRV_TIMER_1] = {
        .type = DRV_TIMER_TYPE_GENERAL_L0,
        .bit_width = 32,
        .channel_count = 4,
        .has_complementary = false,
        .has_break = false,
        .has_encoder = true
    },
    [DRV_TIMER_2] = {
        .type = DRV_TIMER_TYPE_GENERAL_L0,
        .bit_width = 16,
        .channel_count = 4,
        .has_complementary = false,
        .has_break = false,
        .has_encoder = true
    },
    [DRV_TIMER_3] = {
        .type = DRV_TIMER_TYPE_GENERAL_L0,
        .bit_width = 16,
        .channel_count = 4,
        .has_complementary = false,
        .has_break = false,
        .has_encoder = true
    },
    [DRV_TIMER_4] = {
        .type = DRV_TIMER_TYPE_GENERAL_L0,
        .bit_width = 16,
        .channel_count = 4,
        .has_complementary = false,
        .has_break = false,
        .has_encoder = true
    },
    [DRV_TIMER_5] = {
        .type = DRV_TIMER_TYPE_BASIC,
        .bit_width = 16,
        .channel_count = 0,
        .has_complementary = false,
        .has_break = false,
        .has_encoder = false
    },
    [DRV_TIMER_6] = {
        .type = DRV_TIMER_TYPE_BASIC,
        .bit_width = 16,
        .channel_count = 0,
        .has_complementary = false,
        .has_break = false,
        .has_encoder = false
    },
    [DRV_TIMER_7] = {
        .type = DRV_TIMER_TYPE_ADVANCED,
        .bit_width = 16,
        .channel_count = 4,
        .has_complementary = true,
        .has_break = true,
        .has_encoder = true
    },
    [DRV_TIMER_15] = {
        .type = DRV_TIMER_TYPE_GENERAL_L3,
        .bit_width = 16,
        .channel_count = 3,          /* 用户手册明确：总通道数3 */
        .has_complementary = true,   /* 含死区时间插入模块，经GD32标准库CH0N确认 */
        .has_break = true,           /* 中止输入功能：BREAK，用户手册确认 */
        .has_encoder = false
    },
    [DRV_TIMER_16] = {
        .type = DRV_TIMER_TYPE_GENERAL_L3,
        .bit_width = 16,
        .channel_count = 3,          /* 用户手册明确：总通道数3 */
        .has_complementary = true,   /* 含死区时间插入模块，经GD32标准库CH0N确认 */
        .has_break = true,           /* 中止输入功能：BREAK，用户手册确认 */
        .has_encoder = false
    }
};

/*********************************************************************
 * 内部辅助函数声明
 *********************************************************************/

/**
 * @brief  检查Timer是否支持刹车中断
 * @param  timer_id Timer ID
 * @return DRV_TIMER_ERR_OK=支持，DRV_TIMER_ERR_UNSUPPORTED=不支持
 */
static inline int32_t check_break_support(drv_timer_id_e timer_id)
{
    /* 通过能力表查询，避免硬编码，移植时只需更新能力表 */
    if (!s_timer_cap[timer_id].has_break)
    {
        DRV_TIMER_LOGW("Timer %d does not support BREAK interrupt", timer_id);
        return DRV_TIMER_ERR_UNSUPPORTED;
    }
    return DRV_TIMER_ERR_OK;
}

/*********************************************************************
 * 初始化和去初始化接口实现
 *********************************************************************/

/*********************************************************************
 * @brief   初始化Timer
 * @param   timer_id Timer ID
 * @param   config 配置结构体指针
 * @return  DRV_TIMER_ERR_OK=成功，其他=失败
 *********************************************************************/
int32_t drv_timer_init(drv_timer_id_e timer_id, drv_timer_config_t *config)
{
    timer_parameter_struct timer_param;

    /* 参数检查 */
    TIMER_CHECK_ID(timer_id);
    if (config == NULL)
    {
        DRV_TIMER_LOGE("Invalid config pointer");
        return DRV_TIMER_ERR_INVALID_PARAM;
    }

    /* 检查是否已初始化 */
    if (s_timer_ctrl[timer_id].is_initialized)
    {
        DRV_TIMER_LOGW("Timer %d already initialized", timer_id);
        return DRV_TIMER_ERR_BUSY;
    }

    /* 从映射表获取Timer信息 */
    s_timer_ctrl[timer_id].timer_periph = s_timer_map[timer_id].periph;
    s_timer_ctrl[timer_id].rcu_clk = s_timer_map[timer_id].rcu_clk;

    /* 使能Timer时钟 */
    rcu_periph_clock_enable(s_timer_ctrl[timer_id].rcu_clk);

    /* 配置Timer参数 */
    timer_param.prescaler = config->prescaler;
    timer_param.period = config->period;
    timer_param.clockdivision = TIMER_CKDIV_DIV1;
    timer_param.repetitioncounter = config->repetition_counter;

    /* 配置计数模式（正确映射所有模式） */
    switch (config->counter_mode)
    {
        case DRV_COUNTER_EDGE:
            timer_param.alignedmode = TIMER_COUNTER_EDGE;
            timer_param.counterdirection = TIMER_COUNTER_UP;
            break;

        case DRV_COUNTER_CENTER_UP:
            timer_param.alignedmode = TIMER_COUNTER_CENTER_BOTH;
            timer_param.counterdirection = TIMER_COUNTER_UP;
            break;

        case DRV_COUNTER_CENTER_DOWN:
            timer_param.alignedmode = TIMER_COUNTER_CENTER_BOTH;
            timer_param.counterdirection = TIMER_COUNTER_DOWN;
            break;

        case DRV_COUNTER_CENTER_UP_DOWN:
            timer_param.alignedmode = TIMER_COUNTER_CENTER_BOTH;
            timer_param.counterdirection = TIMER_COUNTER_UP;  /* 自动上下计数 */
            break;

        default:
            timer_param.alignedmode = TIMER_COUNTER_EDGE;
            timer_param.counterdirection = TIMER_COUNTER_UP;
            DRV_TIMER_LOGW("Invalid counter mode, default to EDGE_UP");
            break;
    }

    /* 初始化Timer */
    timer_init(s_timer_ctrl[timer_id].timer_periph, &timer_param);

    /* 配置自动重载影子 */
    if (config->auto_reload_shadow)
    {
        timer_auto_reload_shadow_enable(s_timer_ctrl[timer_id].timer_periph);
    }
    else
    {
        timer_auto_reload_shadow_disable(s_timer_ctrl[timer_id].timer_periph);
    }

    /* 清除Timer中断标志 */
    timer_interrupt_flag_clear(s_timer_ctrl[timer_id].timer_periph, TIMER_INT_FLAG_UP);

    /* 更新状态 */
    s_timer_ctrl[timer_id].is_initialized = true;
    s_timer_ctrl[timer_id].is_running = false;
    s_timer_ctrl[timer_id].callback = NULL;

    DRV_TIMER_LOGI("Timer %d initialized (period=%lu, prescaler=%u)",
                   timer_id, config->period, config->prescaler);
    return DRV_TIMER_ERR_OK;
}

/*********************************************************************
 * @brief   去初始化Timer
 * @param   timer_id Timer ID
 * @return  DRV_TIMER_ERR_OK=成功，其他=失败
 *********************************************************************/
int32_t drv_timer_deinit(drv_timer_id_e timer_id)
{
    /* 参数检查 */
    TIMER_CHECK_ID(timer_id);
    TIMER_CHECK_INIT(timer_id);

    /* 停止Timer */
    if (s_timer_ctrl[timer_id].is_running)
    {
        timer_disable(s_timer_ctrl[timer_id].timer_periph);
    }

    /* 清除回调 */
    s_timer_ctrl[timer_id].callback = NULL;

    /* 反初始化Timer（会自动清除所有中断和寄存器） */
    timer_deinit(s_timer_ctrl[timer_id].timer_periph);

    /* 关闭Timer时钟 */
    rcu_periph_clock_disable(s_timer_ctrl[timer_id].rcu_clk);

    /* 清空控制块 */
    memset(&s_timer_ctrl[timer_id], 0, sizeof(drv_timer_ctrl_t));

    DRV_TIMER_LOGI("Timer %d deinitialized", timer_id);
    return DRV_TIMER_ERR_OK;
}

/*********************************************************************
 * 启动和停止接口实现
 *********************************************************************/

/*********************************************************************
 * @brief   启动Timer
 * @param   timer_id Timer ID
 * @return  DRV_TIMER_ERR_OK=成功，其他=失败
 *********************************************************************/
int32_t drv_timer_start(drv_timer_id_e timer_id)
{
    /* 参数检查 */
    TIMER_CHECK_ID(timer_id);
    TIMER_CHECK_INIT(timer_id);

    /* 启动Timer */
    timer_enable(s_timer_ctrl[timer_id].timer_periph);
    s_timer_ctrl[timer_id].is_running = true;

    DRV_TIMER_LOGD("Timer %d started", timer_id);
    return DRV_TIMER_ERR_OK;
}

/*********************************************************************
 * @brief   停止Timer
 * @param   timer_id Timer ID
 * @return  DRV_TIMER_ERR_OK=成功，其他=失败
 *********************************************************************/
int32_t drv_timer_stop(drv_timer_id_e timer_id)
{
    /* 参数检查 */
    TIMER_CHECK_ID(timer_id);
    TIMER_CHECK_INIT(timer_id);

    /* 停止Timer */
    timer_disable(s_timer_ctrl[timer_id].timer_periph);
    s_timer_ctrl[timer_id].is_running = false;

    DRV_TIMER_LOGD("Timer %d stopped", timer_id);
    return DRV_TIMER_ERR_OK;
}

/*********************************************************************
 * 中断管理接口实现
 *********************************************************************/

/*********************************************************************
 * @brief   注册Timer中断回调函数
 * @param   timer_id Timer ID
 * @param   callback 回调函数指针
 * @return  DRV_TIMER_ERR_OK=成功，其他=失败
 *********************************************************************/
int32_t drv_timer_callback_register(drv_timer_id_e timer_id,
                                    drv_timer_callback_t callback)
{
    /* 参数检查 */
    TIMER_CHECK_ID(timer_id);
    if (callback == NULL)
    {
        DRV_TIMER_LOGE("Invalid callback pointer");
        return DRV_TIMER_ERR_INVALID_PARAM;
    }
    TIMER_CHECK_INIT(timer_id);

    /* 注册回调 */
    s_timer_ctrl[timer_id].callback = callback;

    DRV_TIMER_LOGD("Timer %d callback registered", timer_id);
    return DRV_TIMER_ERR_OK;
}

/*********************************************************************
 * @brief   注销Timer中断回调函数
 * @param   timer_id Timer ID
 * @return  DRV_TIMER_ERR_OK=成功，其他=失败
 *********************************************************************/
int32_t drv_timer_callback_unregister(drv_timer_id_e timer_id)
{
    /* 参数检查 */
    TIMER_CHECK_ID(timer_id);
    TIMER_CHECK_INIT(timer_id);

    /* 注销回调 */
    s_timer_ctrl[timer_id].callback = NULL;

    DRV_TIMER_LOGD("Timer %d callback unregistered", timer_id);
    return DRV_TIMER_ERR_OK;
}

/*********************************************************************
 * @brief   使能Timer中断
 * @param   timer_id Timer ID
 * @param   int_type 中断类型
 * @return  DRV_TIMER_ERR_OK=成功，其他=失败
 *********************************************************************/
int32_t drv_timer_int_enable(drv_timer_id_e timer_id,
                             drv_timer_int_type_e int_type)
{
    /* 参数检查 */
    TIMER_CHECK_ID(timer_id);
    TIMER_CHECK_INIT(timer_id);

    /* 刹车中断仅高级定时器支持 */
    if (int_type == DRV_TIMER_INT_BREAK)
    {
        int32_t ret = check_break_support(timer_id);
        if (ret != DRV_TIMER_ERR_OK)
        {
            return ret;
        }
    }

    /* 直接使能中断（枚举值已映射到GD32宏） */
    timer_interrupt_enable(s_timer_ctrl[timer_id].timer_periph, (uint32_t)int_type);

    DRV_TIMER_LOGD("Timer %d interrupt type 0x%x enabled", timer_id, (uint32_t)int_type);
    return DRV_TIMER_ERR_OK;
}

/*********************************************************************
 * @brief   禁能Timer中断
 * @param   timer_id Timer ID
 * @param   int_type 中断类型
 * @return  DRV_TIMER_ERR_OK=成功，其他=失败
 *********************************************************************/
int32_t drv_timer_int_disable(drv_timer_id_e timer_id,
                              drv_timer_int_type_e int_type)
{
    /* 参数检查 */
    TIMER_CHECK_ID(timer_id);
    TIMER_CHECK_INIT(timer_id);

    /* 刹车中断仅高级定时器支持 */
    if (int_type == DRV_TIMER_INT_BREAK)
    {
        int32_t ret = check_break_support(timer_id);
        if (ret != DRV_TIMER_ERR_OK)
        {
            return ret;
        }
    }

    /* 直接禁能中断（枚举值已映射到GD32宏） */
    timer_interrupt_disable(s_timer_ctrl[timer_id].timer_periph, (uint32_t)int_type);

    DRV_TIMER_LOGD("Timer %d interrupt type 0x%x disabled", timer_id, (uint32_t)int_type);
    return DRV_TIMER_ERR_OK;
}

/*********************************************************************
 * 运行时配置接口实现
 *********************************************************************/

/*********************************************************************
 * @brief   设置Timer周期值
 * @param   timer_id Timer ID
 * @param   period 新周期值
 * @return  DRV_TIMER_ERR_OK=成功，其他=失败
 *********************************************************************/
int32_t drv_timer_set_period(drv_timer_id_e timer_id, uint32_t period)
{
    /* 参数检查 */
    TIMER_CHECK_ID(timer_id);
    TIMER_CHECK_INIT(timer_id);

    /* 设置周期値 */
    timer_autoreload_value_config(s_timer_ctrl[timer_id].timer_periph, period);

    DRV_TIMER_LOGD("Timer %d period set to %lu", timer_id, period);
    return DRV_TIMER_ERR_OK;
}

/*********************************************************************
 * @brief   设置Timer预分频值
 * @param   timer_id Timer ID
 * @param   prescaler 新预分频值
 * @return  DRV_TIMER_ERR_OK=成功，其他=失败
 *********************************************************************/
int32_t drv_timer_set_prescaler(drv_timer_id_e timer_id, uint16_t prescaler)
{
    /* 参数检查 */
    TIMER_CHECK_ID(timer_id);
    TIMER_CHECK_INIT(timer_id);

    /* 设置预分频値 */
    timer_prescaler_config(s_timer_ctrl[timer_id].timer_periph, prescaler, TIMER_PSC_RELOAD_NOW);

    DRV_TIMER_LOGD("Timer %d prescaler set to %u", timer_id, prescaler);
    return DRV_TIMER_ERR_OK;
}

/*********************************************************************
 * @brief   获取Timer当前计数值
 * @param   timer_id Timer ID
 * @return  当前计数值
 *********************************************************************/
uint32_t drv_timer_get_counter(drv_timer_id_e timer_id)
{
    /* 参数检查 */
    if (timer_id >= DRV_TIMER_MAX)
    {
        DRV_TIMER_LOGE("Invalid timer ID: %d", timer_id);
        return 0;
    }

    /* 检查是否已初始化 */
    if (!s_timer_ctrl[timer_id].is_initialized)
    {
        DRV_TIMER_LOGE("Timer %d not initialized", timer_id);
        return 0;
    }

    /* 读取计数值 */
    return timer_counter_read(s_timer_ctrl[timer_id].timer_periph);
}

/*********************************************************************
 * 状态查询接口实现
 *********************************************************************/

/*********************************************************************
 * @brief   查询Timer是否运行中
 * @param   timer_id Timer ID
 * @return  true=运行中，false=已停止
 *********************************************************************/
bool drv_timer_is_running(drv_timer_id_e timer_id)
{
    if (timer_id >= DRV_TIMER_MAX)
    {
        return false;
    }

    return s_timer_ctrl[timer_id].is_running;
}

/*********************************************************************
 * @brief   查询Timer是否已初始化
 * @param   timer_id Timer ID
 * @return  true=已初始化，false=未初始化
 *********************************************************************/
bool drv_timer_is_initialized(drv_timer_id_e timer_id)
{
    if (timer_id >= DRV_TIMER_MAX)
    {
        return false;
    }

    return s_timer_ctrl[timer_id].is_initialized;
}

/*********************************************************************
 * @brief   获取Timer能力信息
 * @param   timer_id Timer ID
 * @param   cap 能力信息输出指针
 * @return  DRV_TIMER_ERR_OK=成功，其他=失败
 *********************************************************************/
int32_t drv_timer_get_capability(drv_timer_id_e timer_id, drv_timer_capability_t *cap)
{
    /* 参数检查 */
    if (timer_id >= DRV_TIMER_MAX || cap == NULL)
    {
        return DRV_TIMER_ERR_INVALID_PARAM;
    }

    /* 拷贝能力信息 */
    *cap = s_timer_cap[timer_id];

    return DRV_TIMER_ERR_OK;
}

/*********************************************************************
 * 中断处理接口实现
 *
 * 设计说明：
 *   1. ISR 直接定义在驱动层（而非 gd32f50x_it.c），避免额外的函数调用开销
 *   2. 每个 Timer 独立 ISR，编译期确定 periph 和 timer_id，零查表开销
 *   3. 一次性读取 INTF 寄存器，一次性清除所有标志（W1C 机制）
 *   4. 回调函数接收 intf_raw 原始值，应用层自行判断具体中断类型
 *   5. 驱动层不判断优先级、不猜测应用层意图，职责单一
 *********************************************************************/

/*********************************************************************
 * @brief   TIMER0 中断服务函数
 * @note    1. 检查并清除更新中断标志（TIMER_INT_FLAG_UP）
 *          2. 检查初始化状态和回调有效性
 *          3. 调用用户回调，传递 INTF 寄存器原始值
 *********************************************************************/
void TIMER0_IRQHandler(void)
{
    /* 清除更新中断标志 */
    if(SET == timer_interrupt_flag_get(TIMER0, TIMER_INT_FLAG_UP)) {
        timer_interrupt_flag_clear(TIMER0, TIMER_INT_FLAG_UP);
    }

    if (s_timer_ctrl[DRV_TIMER_0].is_initialized &&
        s_timer_ctrl[DRV_TIMER_0].callback != NULL)
    {
        s_timer_ctrl[DRV_TIMER_0].callback(TIMER_INTF(TIMER0));
    }
}

/*********************************************************************
 * @brief   TIMER1 中断服务函数
 * @note    1. 检查并清除更新中断标志（TIMER_INT_FLAG_UP）
 *          2. 检查初始化状态和回调有效性
 *          3. 调用用户回调，传递 INTF 寄存器原始值
 *********************************************************************/
void TIMER1_IRQHandler(void)
{
    if(SET == timer_interrupt_flag_get(TIMER1, TIMER_INT_FLAG_UP)) {
        timer_interrupt_flag_clear(TIMER1, TIMER_INT_FLAG_UP);
    }

    if (s_timer_ctrl[DRV_TIMER_1].is_initialized &&
        s_timer_ctrl[DRV_TIMER_1].callback != NULL)
    {
        s_timer_ctrl[DRV_TIMER_1].callback(TIMER_INTF(TIMER1));
    }
}

/*********************************************************************
 * @brief   TIMER2 中断服务函数
 * @note    1. 检查并清除更新中断标志（TIMER_INT_FLAG_UP）
 *          2. 检查初始化状态和回调有效性
 *          3. 调用用户回调，传递 INTF 寄存器原始值
 *********************************************************************/
void TIMER2_IRQHandler(void)
{
    if(SET == timer_interrupt_flag_get(TIMER2, TIMER_INT_FLAG_UP)) {
        timer_interrupt_flag_clear(TIMER2, TIMER_INT_FLAG_UP);
    }

    if (s_timer_ctrl[DRV_TIMER_2].is_initialized &&
        s_timer_ctrl[DRV_TIMER_2].callback != NULL)
    {
        s_timer_ctrl[DRV_TIMER_2].callback(TIMER_INTF(TIMER2));
    }
}

/*********************************************************************
 * @brief   TIMER3 中断服务函数
 * @note    1. 检查并清除更新中断标志（TIMER_INT_FLAG_UP）
 *          2. 检查初始化状态和回调有效性
 *          3. 调用用户回调，传递 INTF 寄存器原始值
 *********************************************************************/
void TIMER3_IRQHandler(void)
{
    if(SET == timer_interrupt_flag_get(TIMER3, TIMER_INT_FLAG_UP)) {
        timer_interrupt_flag_clear(TIMER3, TIMER_INT_FLAG_UP);
    }

    if (s_timer_ctrl[DRV_TIMER_3].is_initialized &&
        s_timer_ctrl[DRV_TIMER_3].callback != NULL)
    {
        s_timer_ctrl[DRV_TIMER_3].callback(TIMER_INTF(TIMER3));
    }
}

/*********************************************************************
 * @brief   TIMER4 中断服务函数
 * @note    1. 检查并清除更新中断标志（TIMER_INT_FLAG_UP）
 *          2. 检查初始化状态和回调有效性
 *          3. 调用用户回调，传递 INTF 寄存器原始值
 *********************************************************************/
void TIMER4_IRQHandler(void)
{
    if(SET == timer_interrupt_flag_get(TIMER4, TIMER_INT_FLAG_UP)) {
        timer_interrupt_flag_clear(TIMER4, TIMER_INT_FLAG_UP);
    }

    if (s_timer_ctrl[DRV_TIMER_4].is_initialized &&
        s_timer_ctrl[DRV_TIMER_4].callback != NULL)
    {
        s_timer_ctrl[DRV_TIMER_4].callback(TIMER_INTF(TIMER4));
    }
}

/*********************************************************************
 * @brief   TIMER5 中断服务函数
 * @note    1. 检查并清除更新中断标志（TIMER_INT_FLAG_UP）
 *          2. 检查初始化状态和回调有效性
 *          3. 调用用户回调，传递 INTF 寄存器原始值
 *********************************************************************/
void TIMER5_IRQHandler(void)
{
    if(SET == timer_interrupt_flag_get(TIMER5, TIMER_INT_FLAG_UP)) {
        timer_interrupt_flag_clear(TIMER5, TIMER_INT_FLAG_UP);
    }

    if (s_timer_ctrl[DRV_TIMER_5].is_initialized &&
        s_timer_ctrl[DRV_TIMER_5].callback != NULL)
    {
        s_timer_ctrl[DRV_TIMER_5].callback(TIMER_INTF(TIMER5));
    }
}

/*********************************************************************
 * @brief   TIMER6 中断服务函数
 * @note    1. 检查并清除更新中断标志（TIMER_INT_FLAG_UP）
 *          2. 检查初始化状态和回调有效性
 *          3. 调用用户回调，传递 INTF 寄存器原始值
 *********************************************************************/
void TIMER6_IRQHandler(void)
{
    if(SET == timer_interrupt_flag_get(TIMER6, TIMER_INT_FLAG_UP)) {
        timer_interrupt_flag_clear(TIMER6, TIMER_INT_FLAG_UP);
    }

    if (s_timer_ctrl[DRV_TIMER_6].is_initialized &&
        s_timer_ctrl[DRV_TIMER_6].callback != NULL)
    {
        s_timer_ctrl[DRV_TIMER_6].callback(TIMER_INTF(TIMER6));
    }
}

/*********************************************************************
 * @brief   TIMER7 中断服务函数
 * @note    1. 检查并清除更新中断标志（TIMER_INT_FLAG_UP）
 *          2. 检查初始化状态和回调有效性
 *          3. 调用用户回调，传递 INTF 寄存器原始值
 *********************************************************************/
void TIMER7_IRQHandler(void)
{
    if(SET == timer_interrupt_flag_get(TIMER7, TIMER_INT_FLAG_UP)) {
        timer_interrupt_flag_clear(TIMER7, TIMER_INT_FLAG_UP);
    }

    if (s_timer_ctrl[DRV_TIMER_7].is_initialized &&
        s_timer_ctrl[DRV_TIMER_7].callback != NULL)
    {
        s_timer_ctrl[DRV_TIMER_7].callback(TIMER_INTF(TIMER7));
    }
}

/*********************************************************************
 * @brief   TIMER15 中断服务函数
 * @note    1. 检查并清除更新中断标志（TIMER_INT_FLAG_UP）
 *          2. 检查初始化状态和回调有效性
 *          3. 调用用户回调，传递 INTF 寄存器原始值
 *********************************************************************/
void TIMER15_IRQHandler(void)
{
    if(SET == timer_interrupt_flag_get(TIMER15, TIMER_INT_FLAG_UP)) {
        timer_interrupt_flag_clear(TIMER15, TIMER_INT_FLAG_UP);
    }

    if (s_timer_ctrl[DRV_TIMER_15].is_initialized &&
        s_timer_ctrl[DRV_TIMER_15].callback != NULL)
    {
        s_timer_ctrl[DRV_TIMER_15].callback(TIMER_INTF(TIMER15));
    }
}

/*********************************************************************
 * @brief   TIMER16 中断服务函数
 * @note    1. 检查并清除更新中断标志（TIMER_INT_FLAG_UP）
 *          2. 检查初始化状态和回调有效性
 *          3. 调用用户回调，传递 INTF 寄存器原始值
 *********************************************************************/
void TIMER16_IRQHandler(void)
{
    if(SET == timer_interrupt_flag_get(TIMER16, TIMER_INT_FLAG_UP)) {
        timer_interrupt_flag_clear(TIMER16, TIMER_INT_FLAG_UP);
    }

    if (s_timer_ctrl[DRV_TIMER_16].is_initialized &&
        s_timer_ctrl[DRV_TIMER_16].callback != NULL)
    {
        s_timer_ctrl[DRV_TIMER_16].callback(TIMER_INTF(TIMER16));
    }
}
