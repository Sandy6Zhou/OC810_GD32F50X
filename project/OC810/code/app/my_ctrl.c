/********************************************************************
**版权所有：         深圳市几米物联有限公司
**文件名称：        my_ctrl.c
**文件描述：        控制任务 - 硬件外设管理与系统控制中枢
**当前版本：        V1.0
**作    者：        伍玉蛟 (wuyujiao@jimiiot.com)
**完成日期：        2026.06.08
*********************************************************************
** 架构概述：
**   单任务事件驱动架构，通过消息队列接收外部事件，永久阻塞等待消息。
**
** 硬件管理：
**   - 输出控制（11路）：通用输出、电源开关、LED指示灯、蜂鸣器
**     统一枚举表 s_output_list[]，通过 my_ctrl_set_output/ctrl_toggle_output 控制
**   - 数字输入（10路）：EXTI双沿触发 + 定时器轮询消抖（20ms）
**     统一枚举表 s_input_list[]，ACC共用EXTI回调
**   - ADC检测（3路）：TIMER5定时触发 + DMA循环搬运，启停模式低功耗
**     统一枚举表 s_adc_list[]，浮点换算含分压比校正（732K/75K）
**
** 消息接口（定义于 my_comm.h）：
**   - MY_MSG_ID_CTRL_ACC_CHANGE      ACC状态变化（立即）
**   - MY_MSG_ID_CTRL_INPUT_CHANGE     数字输入变化（消抖后）
**   - MY_MSG_ID_CTRL_START_ADC        定时器触发启动ADC采样
**   - MY_MSG_ID_CTRL_ADC_DONE         ADC DMA转换完成
**   - MY_MSG_ID_CTRL_OUTPUT_SET       通用输出控制（len=端口ID, data=状态值）
**   - MY_MSG_ID_CTRL_PWR_SET          电源开关控制
**   - MY_MSG_ID_CTRL_LED_SET          LED控制
**   - MY_MSG_ID_CTRL_BUZZER_SET       蜂鸣器控制
**   - MY_MSG_ID_CTRL_STATUS_REQ       状态查询请求
*********************************************************************/
#include "my_comm.h"

/*********************************************************************
 * 硬件定义
 *********************************************************************/

/*===========================================================================
 * 输出控制（11路：通用输出 + 电源 + LED + 蜂鸣器）
 *===========================================================================*/
/* 输出引脚映射表（11路，顺序须与 ctrl_out_id_e 一致） */
typedef struct {
    drv_gpio_port_e port;           /**< GPIO端口 */
    drv_gpio_pin_e  pin;            /**< 引脚 */
    bool            default_state;  /**< 默认输出电平 */
} ctrl_gpio_out_t;

static const ctrl_gpio_out_t s_output_list[] = {
    [CTRL_OUT_IO1]        = { DRV_GPIO_PORT_E, DRV_GPIO_PIN_2,  false },
    [CTRL_OUT_IO2]        = { DRV_GPIO_PORT_E, DRV_GPIO_PIN_3,  false },
    [CTRL_OUT_PWR_SYS5V]  = { DRV_GPIO_PORT_D, DRV_GPIO_PIN_5,  false },
    [CTRL_OUT_PWR_VCC3V3] = { DRV_GPIO_PORT_D, DRV_GPIO_PIN_4,  true  },
    [CTRL_OUT_PWR_12V]    = { DRV_GPIO_PORT_E, DRV_GPIO_PIN_5,  false },
    [CTRL_OUT_PWR_5V]     = { DRV_GPIO_PORT_D, DRV_GPIO_PIN_10, false },
    [CTRL_OUT_LED_REC]    = { DRV_GPIO_PORT_D, DRV_GPIO_PIN_11, false },
    [CTRL_OUT_LED_SYS]    = { DRV_GPIO_PORT_D, DRV_GPIO_PIN_12, false },
    [CTRL_OUT_LED_GNSS]   = { DRV_GPIO_PORT_D, DRV_GPIO_PIN_13, false },
    [CTRL_OUT_LED_NET]    = { DRV_GPIO_PORT_D, DRV_GPIO_PIN_14, false },
    [CTRL_OUT_BUZZER]     = { DRV_GPIO_PORT_A, DRV_GPIO_PIN_1,  false },
};

/*===========================================================================
 * 数字输入（10路）
 *===========================================================================*/

 typedef struct {
    drv_gpio_port_e port;           /**< GPIO端口 */
    drv_gpio_pin_e  pin;            /**< 引脚 */
    drv_gpio_pupd_e pupd;           /**< 上拉/下拉 */
} ctrl_input_dev_t;

static const ctrl_input_dev_t s_input_list[] = {
    [CTRL_IN_IO1]      = { DRV_GPIO_PORT_B, DRV_GPIO_PIN_14, DRV_GPIO_PUPD_PULLUP },
    [CTRL_IN_IO2]      = { DRV_GPIO_PORT_C, DRV_GPIO_PIN_5,  DRV_GPIO_PUPD_PULLUP },
    [CTRL_IN_IO3]      = { DRV_GPIO_PORT_C, DRV_GPIO_PIN_6,  DRV_GPIO_PUPD_PULLUP },
    [CTRL_IN_IO4]      = { DRV_GPIO_PORT_C, DRV_GPIO_PIN_7,  DRV_GPIO_PUPD_PULLUP },
    [CTRL_IN_IO5]      = { DRV_GPIO_PORT_C, DRV_GPIO_PIN_8,  DRV_GPIO_PUPD_PULLUP },
    [CTRL_IN_IO6]      = { DRV_GPIO_PORT_C, DRV_GPIO_PIN_9,  DRV_GPIO_PUPD_PULLUP },
    [CTRL_IN_L1]       = { DRV_GPIO_PORT_B, DRV_GPIO_PIN_12, DRV_GPIO_PUPD_NONE   },
    [CTRL_IN_L2]       = { DRV_GPIO_PORT_B, DRV_GPIO_PIN_13, DRV_GPIO_PUPD_NONE   },
    [CTRL_IN_ELEC_SW]  = { DRV_GPIO_PORT_B, DRV_GPIO_PIN_2,  DRV_GPIO_PUPD_PULLUP },
    [CTRL_IN_ACC]      = { DRV_GPIO_PORT_A, DRV_GPIO_PIN_0,  DRV_GPIO_PUPD_PULLUP },
};

/*===========================================================================
 * ADC检测（3路）
 *===========================================================================*/
typedef enum {
    CTRL_ADC_EXT_VOLT1 = 0,         /* PC0 CH10 外部电压1 */
    CTRL_ADC_EXT_VOLT2,             /* PC1 CH11 外部电压2 */
    CTRL_ADC_PWR_VOLT,              /* PC2 CH12 电源电压 */
    CTRL_ADC_MAX
} ctrl_adc_id_e;

typedef struct {
    drv_adc_channel_e channel;      /**< ADC通道 */
    drv_adc_sampletime_e sample_time; /**< 采样时间 */
} ctrl_adc_dev_t;

static const ctrl_adc_dev_t s_adc_list[] = {
    [CTRL_ADC_EXT_VOLT1] = { DRV_ADC_CHANNEL_10, DRV_ADC_SAMPLETIME_239POINT5 },
    [CTRL_ADC_EXT_VOLT2] = { DRV_ADC_CHANNEL_11, DRV_ADC_SAMPLETIME_239POINT5 },
    [CTRL_ADC_PWR_VOLT]  = { DRV_ADC_CHANNEL_12, DRV_ADC_SAMPLETIME_239POINT5 },
};

#define CTRL_ADC_PORT           DRV_ADC0          /**< ADC端口 */
#define CTRL_ADC_DMA_CH         DRV_DMA0_CH0      /**< ADC DMA通道（硬件固定CH0，USART0已改用CH7避开冲突） */
#define CTRL_ADC_VREF_MV        (3300U)           /**< ADC参考电压(mV) */
#define CTRL_ADC_DIV_R1         (732U)            /**< 分压电阻R1(kΩ) - 上臂 */
#define CTRL_ADC_DIV_R2         (75U)             /**< 分压电阻R2(kΩ) - 下臂 */

/*********************************************************************
 * 内部常量
 *********************************************************************/
#define CTRL_EXTI_PRIORITY        (5U)    /**< EXTI中断优先级（高于ADC/TIMER/DMA，仅次于RTC/CAN） */
#define CTRL_DMA_PRIORITY         (5U)    /**< DMA中断优先级 */
#define CTRL_TIMER5_ADC_SAMPLE_PERIOD_MS (1000U) /**< ADC采样周期（ms） */
#define CTRL_INPUT_DEBOUNCE_COUNT (5U)    /**< 输入去抖计数（20ms×5=100ms） */

#define CTRL_ADC_SAMPLES_PER_CH   (16U)   /**< 每通道DMA采样数 */
#define CTRL_ADC_DISCARD_FIRST    (4U)    /**< 丢弃前4个采样（MUX稳定） */
#define CTRL_ADC_VALID_COUNT      (CTRL_ADC_SAMPLES_PER_CH - CTRL_ADC_DISCARD_FIRST) /**< 有效采样12个 */
#define CTRL_ADC_TRIM_COUNT       (2U)    /**< 去掉1个最大+1个最小 */
#define CTRL_ADC_AVG_COUNT        (CTRL_ADC_VALID_COUNT - CTRL_ADC_TRIM_COUNT) /**< 均值采样10个 */
#define CTRL_ADC_DMA_BUF_SIZE     (CTRL_ADC_MAX * CTRL_ADC_SAMPLES_PER_CH) /**< DMA缓冲区大小(48) */

/*********************************************************************
 * 静态数据
 *********************************************************************/
static volatile bool s_input_irq_happened = false;
static volatile uint16_t s_input_disabled_mask = 0;  /**< 记录消抖期间被禁用IRQ的引脚 */
static uint16_t s_input_debounce = 0;

static uint16_t s_output_state = 0;  /**< 输出口当前状态位掩码 */

static uint16_t s_adc_dma_buf[CTRL_ADC_DMA_BUF_SIZE] __attribute__((aligned(4)));

/* 控制模块全局状态 */
static my_ctrl_state_t s_state;

/*********************************************************************
 * 内部函数前向声明
 *********************************************************************/
static void ctrl_exti_callback(drv_gpio_port_e port, uint32_t pin);
static void ctrl_msg_handler(const my_msg_t *msg);
static void ctrl_task_entry(void *pvParameters);
static int32_t ctrl_gpio_init(void);
static void ctrl_toggle_output(ctrl_out_id_e id);
static int32_t ctrl_adc_init(void);
static void    ctrl_start_pipeline(void);
static void    ctrl_stop_pipeline(void);
static void    ctrl_start_adc(void);
static void    ctrl_stop_adc(void);
static void    ctrl_process_input(void);
static void    ctrl_process_adc_data(void);
static void    ctrl_adc_dma_complete_cb(drv_dma_channel_id_e channel_id);
static void    ctrl_adc_timer_cb(my_timer_handle_t timer_handle);

/*********************************************************************
 * @brief   EXTI回调 - 数字输入（10路统一）
 * @param   port  触发中断的GPIO端口
 * @param   pin   触发中断的GPIO引脚
 * @return  none
 * @note    在中断上下文执行：屏蔽该引脚IRQ→标记事件→发送INPUT_CHANGE消息；
 *          若为ACC引脚则额外发送ACC_CHANGE消息并唤醒CTRL任务
 *********************************************************************/
static void ctrl_exti_callback(drv_gpio_port_e port, uint32_t pin)
{
    uint8_t i;
    my_base_type_t xHigherPriorityTaskWoken = pdFALSE;
    my_msg_t msg = {
        .id = MY_MSG_ID_CTRL_INPUT_CHANGE,
        .data = NULL,
        .len = 0
    };

    /* 查找设备索引 */
    for (i = 0; i < CTRL_IN_MAX; i++)
    {
        if ((s_input_list[i].port == port) && ((uint32_t)s_input_list[i].pin == pin))
        {
            break;
        }
    }

    if (i >= CTRL_IN_MAX)
    {
        return;
    }

    /* 屏蔽该引脚EXTI（防抖期间不进中断） */
    drv_gpio_exti_disable(port, (drv_gpio_pin_e)pin);
    s_input_disabled_mask |= (1U << i);

    if (TASK_HANDLE_CTRL != NULL)
    {
        /* 标记事件发生 */
        s_input_irq_happened = true;

        (void)my_msg_send_from_isr(MSG_QUEUE_CTRL, &msg, &xHigherPriorityTaskWoken);

        if (i == CTRL_IN_ACC)
        {
            msg.id = MY_MSG_ID_CTRL_ACC_CHANGE;
            if (TASK_STATE_CTRL != TASK_STATE_ACTIVE && TASK_STATE_CTRL != TASK_STATE_NOT_INIT)
            {
                (void)my_task_resume_from_isr(TASK_HANDLE_CTRL);
            }
        }
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/*********************************************************************
 * @brief   输入去抖回调（定时器上下文）
 * @param   timer_handle  定时器句柄
 * @return  none
 * @note    在定时器任务中执行，发送输入变化消息到任务队列
 *********************************************************************/
static void ctrl_input_debounce_cb(my_timer_handle_t timer_handle)
{
    (void)timer_handle;

    my_msg_t msg = {
        .id = MY_MSG_ID_CTRL_INPUT_CHANGE,
        .data = NULL,
        .len = 0
    };

    my_msg_send(MSG_QUEUE_CTRL, &msg, 0);
}

/*********************************************************************
 * @brief   更新输入状态位域
 * @param   input_state  10位输入状态掩码（bit0~bit9对应s_input_list索引）
 * @return  none
 * @note    将bitmask映射到s_state.input各字段；ACC变化时额外发送ACC_CHANGE消息
 *********************************************************************/
static void ctrl_update_input_state(uint16_t input_state)
{
    static uint16_t s_last_input_state = 0;
    uint16_t changed = input_state ^ s_last_input_state;

    s_state.input.hdet_input1 = (input_state >> CTRL_IN_IO1) & 1U;
    s_state.input.hdet_input2 = (input_state >> CTRL_IN_IO2) & 1U;
    s_state.input.hdet_input3 = (input_state >> CTRL_IN_IO3) & 1U;
    s_state.input.hdet_input4 = (input_state >> CTRL_IN_IO4) & 1U;
    s_state.input.hdet_input5 = (input_state >> CTRL_IN_IO5) & 1U;
    s_state.input.hdet_input6 = (input_state >> CTRL_IN_IO6) & 1U;
    s_state.input.ldet_input1 = (input_state >> CTRL_IN_L1) & 1U;
    s_state.input.ldet_input2 = (input_state >> CTRL_IN_L2) & 1U;
    s_state.input.elec_sw     = (input_state >> CTRL_IN_ELEC_SW) & 1U;
    s_state.input.acc_in      = (input_state >> CTRL_IN_ACC) & 1U;

    /* ACC变化时发送消息（立即通知业务层） */
    if (changed & (1U << CTRL_IN_ACC))
    {
        my_msg_t msg = {
            .id = MY_MSG_ID_CTRL_ACC_CHANGE,
            .data = NULL,
            .len = s_state.input.acc_in
        };

        MY_LOG_I("ACC %s", s_state.input.acc_in ? "ON" : "OFF");
        (void)my_msg_send(MSG_QUEUE_CTRL, &msg, 0);
    }

    s_last_input_state = input_state;
}

/*********************************************************************
 * @brief   输入消抖处理
 * @param   none
 * @return  none
 * @note    在CTRL任务上下文调用；读取全部输入电平，通过定时器轮询消抖，
 *          连续CTRL_INPUT_DEBOUNCE_COUNT次稳定后更新状态并重开EXTI
 *********************************************************************/
static void ctrl_process_input(void)
{
    uint16_t i;
    uint16_t bit;
    uint16_t input_current = 0;
    static uint16_t input_last = 0;

    for (i = 0; i < CTRL_IN_MAX; i++)
    {
        bit = drv_gpio_read_input(s_input_list[i].port, s_input_list[i].pin)? (1 << i) : 0;
        input_current |= bit;
    }

    if (s_input_irq_happened)
    {
        MY_LOG_I("Input IRQ happened");
        s_input_irq_happened = false;
        s_input_debounce = 0;
        input_last = input_current;
        if (!my_timer_is_running(MY_TIMER_ID_CTRL_INPUT_DEBOUNCE))
        {
            my_timer_start(MY_TIMER_ID_CTRL_INPUT_DEBOUNCE, 0);
        }
    }
    else if (input_last != input_current)
    {
        s_input_debounce = 0;
        if (!my_timer_is_running(MY_TIMER_ID_CTRL_INPUT_DEBOUNCE))
        {
            my_timer_start(MY_TIMER_ID_CTRL_INPUT_DEBOUNCE, 0);
        }
    }
    else if (++s_input_debounce >= CTRL_INPUT_DEBOUNCE_COUNT)
    {
        my_timer_stop(MY_TIMER_ID_CTRL_INPUT_DEBOUNCE);

        /* 仅重开消抖期间被禁用的引脚EXTI */
        for (i = 0; i < CTRL_IN_MAX; i++)
        {
            if (s_input_disabled_mask & (1U << i))
            {
                drv_gpio_exti_enable(s_input_list[i].port, s_input_list[i].pin);
                s_input_disabled_mask &= ~(1U << i);
            }
        }

        /* 更新输入状态 */
        ctrl_update_input_state(input_current);
    }
}

/*********************************************************************
 * @brief   翻转输出口状态
 * @param   id  输出口枚举ID
 * @return  none
 *********************************************************************/
static void ctrl_toggle_output(ctrl_out_id_e id)
{
    bool new_state = false;

    if (id >= CTRL_OUT_MAX)
    {
        return;
    }

    new_state = !(s_output_state & (1U << id));
    my_ctrl_set_output(id, new_state);
}

/*********************************************************************
 * @brief   GPIO初始化（10路输入 + EXTI配置 + 去抖定时器创建）
 * @param   none
 * @return  0=成功, -1=失败
 * @note    配置所有输入引脚GPIO+EXTI（初始禁用），并创建20ms去抖定时器
 *********************************************************************/
static int32_t ctrl_gpio_init(void)
{
    uint8_t i;
    drv_gpio_config_t cfg;
    uint16_t input_state = 0;
    int32_t ret = 0;

    /* 输出控制引脚（11路：通用输出 + 电源 + LED + 蜂鸣器） */
    for (i = 0; i < (uint8_t)CTRL_OUT_MAX; i++)
    {
        cfg.port = s_output_list[i].port;
        cfg.pin  = s_output_list[i].pin;
        cfg.pupd = DRV_GPIO_PUPD_NONE;
        cfg.mode = DRV_GPIO_MODE_OUTPUT;
        cfg.otype = DRV_GPIO_OTYPE_PP;
        cfg.speed = DRV_GPIO_SPEED_LEVEL0;
        cfg.af   = DRV_GPIO_AF_0;
        cfg.initial_state = s_output_list[i].default_state;

        if (drv_gpio_init(&cfg))
        {
            MY_LOG_E("GPIO output[%u] init failed", i);
            ret = -1;
        }
    }

    /* 数字输入引脚（10路，输入+上拉/下拉+EXTI配置） */
    for (i = 0; i < (uint8_t)CTRL_IN_MAX; i++)
    {
        cfg.port = s_input_list[i].port;
        cfg.pin  = s_input_list[i].pin;
        cfg.pupd = s_input_list[i].pupd;
        cfg.mode = DRV_GPIO_MODE_INPUT;
        cfg.otype = DRV_GPIO_OTYPE_PP;
        cfg.speed = DRV_GPIO_SPEED_LEVEL0;
        cfg.af   = DRV_GPIO_AF_0;
        cfg.initial_state = false;

        /* 初始化GPIO */
        if (drv_gpio_init(&cfg))
        {
            MY_LOG_E("GPIO input[%u] init failed", i);
            ret = -1;
        }

        /* 配置EXTI */
        if (drv_gpio_exti_configure(s_input_list[i].port, s_input_list[i].pin,
                                       DRV_EXTI_MODE_INTERRUPT, DRV_EXTI_TRIG_BOTH,
                                       ctrl_exti_callback, CTRL_EXTI_PRIORITY))
        {
            MY_LOG_E("EXTI[%u] config failed", i);
            ret = -1;
        }

        /* 初始化先禁用EXTI，并记录到禁用掩码 */
        (void)drv_gpio_exti_disable(s_input_list[i].port, s_input_list[i].pin);
        s_input_disabled_mask |= (1U << i);
    }

    /* 创建输入去抖定时器（20ms周期） */
    my_timer_create(MY_TIMER_ID_CTRL_INPUT_DEBOUNCE, ctrl_input_debounce_cb, 20);

    /* 同步所有输入口初始状态 */
    for (i = 0; i < (uint8_t)CTRL_IN_MAX; i++)
    {
        if (drv_gpio_read_input(s_input_list[i].port, s_input_list[i].pin))
        {
            input_state |= (1U << i);
        }
    }
    ctrl_update_input_state(input_state);

    return ret;
}

/*********************************************************************
 * @brief   ADC数据处理（任务上下文）
 * @param   none
 * @return  none
 * @note    停止ADC/DMA→计算16点均值→更新s_state→启动1秒定时器等待下一轮
 *********************************************************************/
static void ctrl_process_adc_data(void)
{
    uint8_t ch;
    uint8_t s;
    uint32_t sum;
    uint16_t max_val, min_val, val, raw_avg;
    float volt_10mv;

    for (ch = 0; ch < CTRL_ADC_MAX; ch++)
    {
        sum = 0;
        max_val = 0;
        min_val = 0xFFFF;

        /* 跳过前4个采样（MUX稳定），取后12个 */
        for (s = CTRL_ADC_DISCARD_FIRST; s < CTRL_ADC_SAMPLES_PER_CH; s++)
        {
            val = s_adc_dma_buf[ch + s * CTRL_ADC_MAX];
            sum += val;
            if (val > max_val) max_val = val;
            if (val < min_val) min_val = val;
        }

        /* 去掉1个最大值 + 1个最小值，取剩余10个均值 */
        sum -= max_val;
        sum -= min_val;

        /* 原始均值 → 10mV单位（浮点计算）：avg × Vref × (R1+R2)/R2 / 4096 / 10 */
        raw_avg = (uint16_t)(sum / CTRL_ADC_AVG_COUNT);
        volt_10mv = (float)raw_avg * CTRL_ADC_VREF_MV * (CTRL_ADC_DIV_R1 + CTRL_ADC_DIV_R2) / CTRL_ADC_DIV_R2 / 4096.0f / 10.0f;
        val = (uint16_t)(volt_10mv + 0.5f);  /* 四舍五入 */

        switch (ch)
        {
            case CTRL_ADC_EXT_VOLT1:
                s_state.adc_ext_volt1 = val;
                break;

            case CTRL_ADC_EXT_VOLT2:
                s_state.adc_ext_volt2 = val;
                break;

            case CTRL_ADC_PWR_VOLT:
                s_state.adc_pwr_volt  = val;
                break;

            default:
                break;
        }
    }

    MY_LOG_D("ADC: volt1=%u volt2=%u pwr=%u",
             s_state.adc_ext_volt1, s_state.adc_ext_volt2, s_state.adc_pwr_volt);
}

/*********************************************************************
 * @brief   ADC采样定时器回调（软件定时器上下文）
 * @param   timer_handle  定时器句柄
 * @return  none
 * @note    每1秒触发一次，启动DMA+ADC开始新一轮采样
 *********************************************************************/
static void ctrl_adc_timer_cb(my_timer_handle_t timer_handle)
{
    (void)timer_handle;
    my_msg_t msg = {
        .id = MY_MSG_ID_CTRL_START_ADC,
        .data = NULL,
        .len = 0
    };

    (void)my_msg_send(MSG_QUEUE_CTRL, &msg, 0);
}

/*********************************************************************
 * @brief   DMA传输完成回调（中断上下文）
 * @param   none
 * @return  none
 * @note    DMA收集完48个样本后触发，发送ADC_DONE消息到CTRL任务
 *********************************************************************/
static void ctrl_adc_dma_complete_cb(drv_dma_channel_id_e channel_id)
{
    my_base_type_t xHigherPriorityTaskWoken = pdFALSE;
    my_msg_t msg = {
        .id = MY_MSG_ID_CTRL_ADC_DONE,
        .data = NULL,
        .len = 0
    };

    /* 立即停止ADC（ISR安全，无日志/延时）
     * ADC停止后不再产生DMA请求，DMA自然停止
     * DMA由任务通过drv_dma_stop()停止，保持驱动状态一致 */
    drv_adc_disable(CTRL_ADC_PORT);

    (void)my_msg_send_from_isr(MSG_QUEUE_CTRL, &msg, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/*********************************************************************
 * @brief   停止ADC DMA采样
 * @param   none
 * @return  none
 * @note    先停ADC再停DMA，避免ADC数据丢失
 *********************************************************************/
static void ctrl_stop_adc(void)
{
    drv_adc_disable(CTRL_ADC_PORT);
    drv_dma_stop(CTRL_ADC_DMA_CH);
}

/*********************************************************************
 * @brief   启动ADC DMA采样
 * @param   none
 * @return  none
 * @note    启动DMA→启动ADC，DMA收集48个样本后触发FTF中断
 *********************************************************************/
static void ctrl_start_adc(void)
{
    drv_dma_start(CTRL_ADC_DMA_CH);
    drv_adc_enable(CTRL_ADC_PORT);
    if (drv_adc_start_conversion(CTRL_ADC_PORT) != DRV_ADC_ERR_OK)
    {
        MY_LOG_E("ADC start conversion failed");
    }
}

/*********************************************************************
 * @brief   ADC DMA初始化（ADC0 + DMA0_CH0）
 * @param   none
 * @return  0=成功, -1=失败
 * @note    配置ADC0扫描+连续模式，DMA循环搬运，软件触发
 *********************************************************************/
static int32_t ctrl_adc_init(void)
{
    int ret;
    drv_adc_config_t adc_cfg;
    drv_adc_channel_config_t ch_cfg;
    drv_dma_config_t dma_cfg;
    uint8_t i;

    /* 初始化ADC0（扫描+连续模式，软件触发） */
    adc_cfg.port = CTRL_ADC_PORT;
    adc_cfg.resolution = DRV_ADC_RESOLUTION_12B;
    adc_cfg.data_align = DRV_ADC_DATAALIGN_RIGHT;
    adc_cfg.mode = DRV_ADC_MODE_SCAN_CONTINUOUS;
    adc_cfg.trigger = DRV_ADC_TRIGGER_SOFTWARE;
    adc_cfg.timeout_ms = 100;
    adc_cfg.use_mutex = false;

    ret = drv_adc_init(&adc_cfg);
    if (ret != DRV_ADC_ERR_OK)
    {
        MY_LOG_E("ADC init failed (err=%d)", ret);
        return -1;
    }

    /* 使能ADC DMA模式 */
    ret = drv_adc_dma_mode_enable(CTRL_ADC_PORT);
    if (ret != DRV_ADC_ERR_OK)
    {
        MY_LOG_E("ADC DMA mode enable failed (err=%d)", ret);
        return -1;
    }

    /* 使能ADC（必须在通道配置前完成，ADON位需先置位） */
    ret = drv_adc_enable(CTRL_ADC_PORT);
    if (ret != DRV_ADC_ERR_OK)
    {
        MY_LOG_E("ADC enable failed (err=%d)", ret);
        return -1;
    }
    my_task_delay_ms(1);  /* 等待ADC稳定 */

    /* 配置3个规则通道 */
    for (i = 0; i < CTRL_ADC_MAX; i++)
    {
        ch_cfg.channel = s_adc_list[i].channel;
        ch_cfg.sample_time = s_adc_list[i].sample_time;
        ch_cfg.rank = i;

        ret = drv_adc_routine_channel_config(CTRL_ADC_PORT, &ch_cfg);
        if (ret != DRV_ADC_ERR_OK)
        {
            MY_LOG_E("ADC channel[%u] config failed (err=%d)", i, ret);
            return -1;
        }
    }

    /* 配置规则通道数量 */
    ret = drv_adc_channel_count(CTRL_ADC_PORT, CTRL_ADC_MAX);
    if (ret != DRV_ADC_ERR_OK)
    {
        MY_LOG_E("ADC channel count config failed (err=%d)", ret);
        return -1;
    }

    /* 配置DMA0_CH0（ADC0→缓冲区，循环模式） */
    dma_cfg.request_id = DMA_REQUEST_ADC0_ROUTINE;
    dma_cfg.periph_addr = (uint32_t)&ADC_RDATA(ADC0);
    dma_cfg.memory_addr = (uint32_t)s_adc_dma_buf;
    dma_cfg.periph_width = DRV_DMA_WIDTH_16BIT;
    dma_cfg.memory_width = DRV_DMA_WIDTH_16BIT;
    dma_cfg.transfer_number = CTRL_ADC_DMA_BUF_SIZE;
    dma_cfg.direction = DRV_DMA_DIR_PERIPH_TO_MEMORY;
    dma_cfg.priority = DRV_DMA_PRIORITY_HIGH;
    dma_cfg.mode = DRV_DMA_MODE_CIRCULAR;
    dma_cfg.periph_inc = false;
    dma_cfg.memory_inc = true;

    ret = drv_dma_init(CTRL_ADC_DMA_CH, &dma_cfg);
    if (ret != DRV_DMA_ERR_OK)
    {
        MY_LOG_E("DMA init failed (err=%d)", ret);
        return -1;
    }

    /* 注册DMA传输完成回调 */
    ret = drv_dma_callback_register(CTRL_ADC_DMA_CH, DRV_DMA_INT_FTF, ctrl_adc_dma_complete_cb);
    if (ret != DRV_DMA_ERR_OK)
    {
        MY_LOG_E("DMA callback register failed (err=%d)", ret);
        return -1;
    }

    /* 使能DMA传输完成中断 */
    ret = drv_dma_int_enable(CTRL_ADC_DMA_CH, DRV_DMA_INT_FTF, CTRL_DMA_PRIORITY);
    if (ret != DRV_DMA_ERR_OK)
    {
        MY_LOG_E("DMA int enable failed (err=%d)", ret);
        return -1;
    }

    /* 创建ADC采样定时器（1秒周期） */
    my_timer_create(MY_TIMER_ID_CTRL_ADC_SAMPLE_INTERVAL, ctrl_adc_timer_cb, CTRL_TIMER5_ADC_SAMPLE_PERIOD_MS);

    MY_LOG_I("ADC DMA init OK (3ch x %u samples, 1s period)", CTRL_ADC_SAMPLES_PER_CH);
    return 0;
}

/*********************************************************************
 * @brief   启动数据采集管道
 * @param   none
 * @return  none
 * @note    使能全部10路输入EXTI
 *********************************************************************/
static void ctrl_start_pipeline(void)
{
    uint8_t i;

    /* 使能10路数字输入EXTI，清除禁用掩码 */
    for (i = 0; i < (uint8_t)CTRL_IN_MAX; i++)
    {
        drv_gpio_exti_enable(s_input_list[i].port, s_input_list[i].pin);
    }
    s_input_disabled_mask = 0;

    /* 启动ADC DMA采样 */
    ctrl_start_adc();

    MY_LOG_I("Pipeline started");
}

/*********************************************************************
 * @brief   停止数据采集管道
 * @param   none
 * @return  none
 * @note    禁用9路数字输入EXTI，保留ACC始终使能以支持唤醒
 *********************************************************************/
static void ctrl_stop_pipeline(void)
{
    uint8_t i;

    /* 禁用数字输入EXTI（保留ACC始终使能，支持唤醒） */
    for (i = 0; i < CTRL_IN_ACC; i++)
    {
        drv_gpio_exti_disable(s_input_list[i].port, s_input_list[i].pin);
    }

    /* 停止去抖定时器，清除禁用掩码和状态标志 */
    my_timer_stop(MY_TIMER_ID_CTRL_INPUT_DEBOUNCE);
    s_input_disabled_mask = 0;
    s_input_irq_happened = false;
    s_input_debounce = 0;

    /* 停止ADC DMA采样和采样定时器 */
    my_timer_stop(MY_TIMER_ID_CTRL_ADC_SAMPLE_INTERVAL);
    ctrl_stop_adc();

    MY_LOG_I("Pipeline stopped");
}

/*********************************************************************
 * @brief   CTRL消息分发处理
 * @param   msg  指向接收到的消息结构体
 * @return  none
 * @note    处理生命周期消息（ACTIVE/SLEEP/SHUTDOWN）及输入变化事件
 *********************************************************************/
static void ctrl_msg_handler(const my_msg_t *msg)
{
    uint8_t idx;
    bool value;

    switch (msg->id)
    {
        case MY_MSG_ID_SYS_ACTIVE:
            MY_LOG_I("System activated");
            TASK_STATE_CTRL = TASK_STATE_ACTIVE;
            ctrl_start_pipeline();
            break;

        case MY_MSG_ID_SYS_SLEEP:
            MY_LOG_I("System sleep");
            ctrl_stop_pipeline();
            TASK_STATE_CTRL = TASK_STATE_SLEEP;
            my_task_suspend(NULL);
            break;

        case MY_MSG_ID_SYS_SHUTDOWN:
            MY_LOG_W("Shutdown");
            ctrl_stop_pipeline();
            TASK_STATE_CTRL = TASK_STATE_SHUTDOWN;
            my_task_suspend(NULL);
            break;

        case MY_MSG_ID_SYS_STATUS_REQ:
            MY_LOG_I("Status request");
            break;

        case MY_MSG_ID_CTRL_ACC_CHANGE:
            value = (msg->len != 0U) ? true : false;
            s_state.input.acc_in = value ? 1U : 0U;
            MY_LOG_I("ACC %s", value ? "ON" : "OFF");
            // To do: wakeup system
            break;

        case MY_MSG_ID_CTRL_INPUT_CHANGE:
            /* 输入变化处理（消抖+读电平+重开IRQ） */
            ctrl_process_input();
            break;

        case MY_MSG_ID_CTRL_START_ADC:
            /* 定时器触发：启动新一轮采样 */
            ctrl_start_adc();
            break;

        case MY_MSG_ID_CTRL_ADC_DONE:
            /* ADC DMA完成：停止ADC(驱动状态) → 处理数据 → 启动定时器 */
            ctrl_stop_adc();
            ctrl_process_adc_data();
            my_timer_start(MY_TIMER_ID_CTRL_ADC_SAMPLE_INTERVAL, CTRL_TIMER5_ADC_SAMPLE_PERIOD_MS);
            break;

        case MY_MSG_ID_CTRL_OUTPUT_SET:
            idx = msg->len;
            value = (uint32_t)msg->data;
            if (idx < CTRL_OUT_MAX)
            {
        my_ctrl_set_output((ctrl_out_id_e)idx, value ? true : false);
            }
            break;

        default:
            MY_LOG_W("Unknown msg: id=0x%04X", msg->id);
            break;
    }
}

/*********************************************************************
 * @brief   CTRL任务入口函数
 * @param   pvParameters  FreeRTOS任务参数（未使用）
 * @return  none
 * @note    依次完成GPIO初始化→启动管道→进入消息循环，永不返回
 *********************************************************************/
static void ctrl_task_entry(void *pvParameters)
{
    my_msg_t msg;
    int32_t ret;

    (void)pvParameters;

    /* GPIO初始化 */
    if (ctrl_gpio_init() != 0)
    {
        MY_LOG_E("GPIO init failed");
    }

    /* ADC DMA初始化 */
    if (ctrl_adc_init() != 0)
    {
        MY_LOG_E("ADC init failed");
    }

    /* 标记ACTIVE并启动管道 */
    TASK_STATE_CTRL = TASK_STATE_ACTIVE;
    MY_LOG_I("CTRL task init done");

    ctrl_start_pipeline();

    /* 消息循环 */
    while (1)
    {
        if (my_msg_recv(MSG_QUEUE_CTRL, &msg, portMAX_DELAY) == 0)
        {
            ctrl_msg_handler(&msg);
        }
    }
}

/*===========================================================================
 *  公开API实现
 *===========================================================================*/

/*********************************************************************
 * @brief   获取当前输入状态（快照）
 * @param   none
 * @return  ctrl_input_t  当前输入状态结构体（位域）
 * @note    直接读取s_state.input的快照，非线程安全但足够快；
 *          若需原子读取，应在CTRL任务内通过消息获取
 *********************************************************************/
ctrl_input_t my_ctrl_get_input_state(void)
{
    return s_state.input;
}

/*********************************************************************
 * @brief   设置输出口状态
 * @param   id     输出口枚举ID
 * @param   state  true=开, false=关
 * @return  none
 * @note    供 LED/PWR/BUZZER 等子模块调用
 *********************************************************************/
void my_ctrl_set_output(ctrl_out_id_e id, bool state)
{
    if (id >= CTRL_OUT_MAX)
    {
        return;
    }

    if (state)
    {
        s_output_state |= (1U << id);
        drv_gpio_set(s_output_list[id].port, s_output_list[id].pin);
    }
    else
    {
        s_output_state &= ~(1U << id);
        drv_gpio_reset(s_output_list[id].port, s_output_list[id].pin);
    }
}

/*********************************************************************
 * @brief   初始化并启动控制任务
 * @return  0: 成功  -1: 失败
 *********************************************************************/
int my_ctrl_init(void)
{
    int32_t ret;

    /* 检查任务是否已创建 */
    if (TASK_HANDLE_CTRL != NULL)
    {
        return 0;
    }

    /* 创建消息队列 */
    MSG_QUEUE_CTRL = my_msg_queue_create(MY_CTRL_MSG_QUEUE_DEPTH, sizeof(my_msg_t));
    if (MSG_QUEUE_CTRL == NULL)
    {
        MY_LOG_E("Failed to create msg queue");
        return -1;
    }

    /* 创建任务 */
    ret = my_task_create(&TASK_HANDLE_CTRL, "CTRL",
                          MY_CTRL_TASK_STACK_SIZE,
                          ctrl_task_entry, NULL,
                          MY_CTRL_TASK_PRIO);

    /* 检查任务创建是否成功 */
    if (ret != 0 || TASK_HANDLE_CTRL == NULL)
    {
        my_msg_queue_delete(MSG_QUEUE_CTRL);
        MSG_QUEUE_CTRL = NULL;
        MY_LOG_E("Failed to create task(%d)", ret);
        return -1;
    }

    MY_LOG_I("Init OK, task created");
    return 0;
}
