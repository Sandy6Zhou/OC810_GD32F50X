/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       timer_driver.c
**文件描述：       Timer驱动模块实现文件 (UPDATE中断功能)
**当前版本：       V1.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.05.07
*********************************************************************
** 功能描述：       1. 实现Timer基础定时功能
**                 2. 实现UPDATE中断回调注册和执行（溢出/下溢）
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
        if (s_timer_update_ctrl[(id)].state == DRV_TIMER_STATE_IDLE) { \
            DRV_TIMER_LOGE("Timer %d not initialized", (id)); \
            return DRV_TIMER_ERR_NOT_INITIALIZED; \
        } \
    } while(0)

/*********************************************************************
 * 内部数据结构定义
 *********************************************************************/

/** Timer 状态枚举 */
typedef enum
{
    DRV_TIMER_STATE_IDLE = 0,         /**< 未初始化 */
    DRV_TIMER_STATE_INITIALIZED,      /**< 已初始化，未启动 */
    DRV_TIMER_STATE_RUNNING           /**< 运行中 */
} drv_timer_state_e;

/** Timer UPDATE中断控制块（内部使用，应用层不可见） */
typedef struct
{
    drv_timer_state_e state;            /**< Timer状态（IDLE/INITIALIZED/RUNNING） */
    uint32_t timer_periph;              /**< GD32 Timer基地址 */
    uint8_t nvic_priority;              /**< NVIC中断优先级 */
    drv_timer_callback_t callback;      /**< UPDATE中断回调函数 */
} drv_timer_update_ctrl_t;

/** Timer UPDATE 中断映射表（表驱动设计，提高效率） */
typedef struct
{
    uint32_t periph;                    /**< Timer外设基地址 */
    rcu_periph_enum rcu_clk;            /**< RCU时钟标识 */
    IRQn_Type irqn;                     /**< UPDATE中断NVIC中断号 */
} drv_timer_update_map_t;

/** Timer UPDATE 中断控制表 */
static drv_timer_update_ctrl_t s_timer_update_ctrl[DRV_TIMER_MAX] = {0};

/** Timer UPDATE 中断映射表（编译期初始化，零运行时开销） */
static const drv_timer_update_map_t s_timer_update_map[DRV_TIMER_MAX] =
{
    [DRV_TIMER_0]  = {TIMER0,  RCU_TIMER0,  TIMER0_UP_IRQn},      /* 高级定时器用 UPDATE 中断 */
    [DRV_TIMER_1]  = {TIMER1,  RCU_TIMER1,  TIMER1_IRQn},
    [DRV_TIMER_2]  = {TIMER2,  RCU_TIMER2,  TIMER2_IRQn},
    [DRV_TIMER_3]  = {TIMER3,  RCU_TIMER3,  TIMER3_IRQn},
    [DRV_TIMER_4]  = {TIMER4,  RCU_TIMER4,  TIMER4_IRQn},
    [DRV_TIMER_5]  = {TIMER5,  RCU_TIMER5,  TIMER5_IRQn},
    [DRV_TIMER_6]  = {TIMER6,  RCU_TIMER6,  TIMER6_IRQn},
    [DRV_TIMER_7]  = {TIMER7,  RCU_TIMER7,  TIMER7_UP_IRQn},      /* 高级定时器用 UPDATE 中断 */
    [DRV_TIMER_15] = {TIMER15, RCU_TIMER15, TIMER15_IRQn},
    [DRV_TIMER_16] = {TIMER16, RCU_TIMER16, TIMER16_IRQn}
};

/*********************************************************************
 * 内部辅助函数声明
 *********************************************************************/

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
    if (s_timer_update_ctrl[timer_id].state != DRV_TIMER_STATE_IDLE)
    {
        DRV_TIMER_LOGW("Timer %d already initialized", timer_id);
        return DRV_TIMER_ERR_BUSY;
    }

    /* 从映射表获取Timer信息 */
    s_timer_update_ctrl[timer_id].timer_periph = s_timer_update_map[timer_id].periph;

    /* 使能Timer时钟 */
    rcu_periph_clock_enable(s_timer_update_map[timer_id].rcu_clk);

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
    timer_init(s_timer_update_ctrl[timer_id].timer_periph, &timer_param);

    /* 配置自动重载影子 */
    if (config->auto_reload_shadow)
    {
        timer_auto_reload_shadow_enable(s_timer_update_ctrl[timer_id].timer_periph);
    }
    else
    {
        timer_auto_reload_shadow_disable(s_timer_update_ctrl[timer_id].timer_periph);
    }

    /* 高级定时器（TIMER0/7）需要使能主输出，UPDATE中断才能正常工作 */
    if ((timer_id == DRV_TIMER_0) || (timer_id == DRV_TIMER_7))
    {
        timer_primary_output_config(s_timer_update_ctrl[timer_id].timer_periph, ENABLE);
    }

    /* 清除Timer中断标志 */
    timer_interrupt_flag_clear(s_timer_update_ctrl[timer_id].timer_periph, TIMER_INT_FLAG_UP);

    /* 更新状态 */
    s_timer_update_ctrl[timer_id].state = DRV_TIMER_STATE_INITIALIZED;
    s_timer_update_ctrl[timer_id].callback = NULL;
    s_timer_update_ctrl[timer_id].nvic_priority = 0;

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
    if (s_timer_update_ctrl[timer_id].state == DRV_TIMER_STATE_RUNNING)
    {
        timer_disable(s_timer_update_ctrl[timer_id].timer_periph);
    }

    /* 清除回调 */
    s_timer_update_ctrl[timer_id].callback = NULL;

    /* 反初始化Timer（会自动清除所有中断和寄存器） */
    timer_deinit(s_timer_update_ctrl[timer_id].timer_periph);

    /* 关闭Timer时钟 */
    rcu_periph_clock_disable(s_timer_update_map[timer_id].rcu_clk);

    /* 清空控制块 */
    memset(&s_timer_update_ctrl[timer_id], 0, sizeof(drv_timer_update_ctrl_t));
    s_timer_update_ctrl[timer_id].state = DRV_TIMER_STATE_IDLE;

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
    timer_enable(s_timer_update_ctrl[timer_id].timer_periph);
    s_timer_update_ctrl[timer_id].state = DRV_TIMER_STATE_RUNNING;

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
    timer_disable(s_timer_update_ctrl[timer_id].timer_periph);
    s_timer_update_ctrl[timer_id].state = DRV_TIMER_STATE_INITIALIZED;

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
    s_timer_update_ctrl[timer_id].callback = callback;

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
    s_timer_update_ctrl[timer_id].callback = NULL;

    DRV_TIMER_LOGD("Timer %d callback unregistered", timer_id);
    return DRV_TIMER_ERR_OK;
}

/*********************************************************************
 * @brief   使能Timer UPDATE中断（同时配置NVIC）
 * @param   timer_id Timer ID
 * @param   nvic_priority NVIC中断优先级（0-15，数值越小优先级越高）
 * @return  DRV_TIMER_ERR_OK=成功，其他=失败
 *********************************************************************/
int32_t drv_timer_int_enable(drv_timer_id_e timer_id, uint8_t nvic_priority)
{
    /* 参数检查 */
    TIMER_CHECK_ID(timer_id);
    TIMER_CHECK_INIT(timer_id);

    /* 使能UPDATE中断 */
    timer_interrupt_enable(s_timer_update_ctrl[timer_id].timer_periph, TIMER_INT_UP);

    /* 保存优先级并配置NVIC */
    s_timer_update_ctrl[timer_id].nvic_priority = nvic_priority;
    nvic_irq_enable(s_timer_update_map[timer_id].irqn, nvic_priority, 0);

    DRV_TIMER_LOGD("Timer %d UPDATE interrupt enabled, NVIC priority=%d",
                   timer_id, nvic_priority);
    return DRV_TIMER_ERR_OK;
}

/*********************************************************************
 * @brief   禁能Timer UPDATE中断
 * @param   timer_id Timer ID
 * @return  DRV_TIMER_ERR_OK=成功，其他=失败
 *********************************************************************/
int32_t drv_timer_int_disable(drv_timer_id_e timer_id)
{
    /* 参数检查 */
    TIMER_CHECK_ID(timer_id);
    TIMER_CHECK_INIT(timer_id);

    /* 禁能UPDATE中断 */
    timer_interrupt_disable(s_timer_update_ctrl[timer_id].timer_periph, TIMER_INT_UP);

    /* 禁用NVIC中断 */
    nvic_irq_disable(s_timer_update_map[timer_id].irqn);

    DRV_TIMER_LOGD("Timer %d UPDATE interrupt disabled", timer_id);
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
    timer_autoreload_value_config(s_timer_update_ctrl[timer_id].timer_periph, period);

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
    timer_prescaler_config(s_timer_update_ctrl[timer_id].timer_periph, prescaler, TIMER_PSC_RELOAD_NOW);

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
    if (s_timer_update_ctrl[timer_id].state == DRV_TIMER_STATE_IDLE)
    {
        DRV_TIMER_LOGE("Timer %d not initialized", timer_id);
        return 0;
    }

    /* 读取计数值 */
    return timer_counter_read(s_timer_update_ctrl[timer_id].timer_periph);
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

    return (s_timer_update_ctrl[timer_id].state == DRV_TIMER_STATE_RUNNING);
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

    return (s_timer_update_ctrl[timer_id].state != DRV_TIMER_STATE_IDLE);
}

/*********************************************************************
 * 中断处理接口实现
 *
 * 设计说明：
 *   1. gd32f50x_it.c 中的 ISR 检查UPDATE中断标志后调用 drv_timer_run_update_callback
 *   2. 驱动层检查回调有效性后执行回调
 *   3. 保持标准项目架构，ISR 在 it.c 中，驱动层职责单一
 *********************************************************************/

/*********************************************************************
 * @brief   执行 Timer UPDATE 中断回调函数
 * @param   timer_id Timer ID
 * @note    此函数由 gd32f50x_it.c 中的 ISR 直接调用
 *          内部检查回调有效性后执行回调
 *********************************************************************/
void drv_timer_run_update_callback(drv_timer_id_e timer_id)
{
    /* 参数检查 */
    if (timer_id >= DRV_TIMER_MAX)
    {
        return;
    }

    /* 检查回调有效性并执行 */
    if (s_timer_update_ctrl[timer_id].callback != NULL)
    {
        s_timer_update_ctrl[timer_id].callback();
    }
}
