/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       adc_driver.c
**文件描述：       ADC驱动模块实现文件
**当前版本：       V1.4
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.05.09
**修改日期：       2026.05.18
*********************************************************************
** 功能描述：       1. 实现ADC初始化与去初始化
**                 2. 实现通道配置与转换控制
**                 3. 实现单通道便捷读取接口
**                 4. 支持DMA传输模式
**                 5. 支持模拟看门狗中断（低功耗唤醒）
**                 6. 支持FreeRTOS互斥锁（线程安全，可选）
**
** 应用场景（FreeRTOS系统）：
**   场景1 - 轮询读取：    单次触发 → 读取（无中断开销，会等待读取完成，一般是上电初始化使用或调试时使用）
**   场景2 - DMA中断：     配置DMA → 批量采样 → DMA中断 → 处理数据（很少占用CPU）
**   场景3 - DMA轮询：     配置DMA → 定时读取（间隔足够长，推荐使用，几乎不占用CPU）
**   场景4 - 低功耗唤醒：   配置AWD → 系统睡眠 → 电压异常 → 唤醒（进入sleep模式下需ADC变化唤醒使用）
**   场景5 - 完全关闭：    直接deinit，关闭时钟节省功耗（系统关机或长时间不使用ADC时）
**
** 设计约束：
**   1. EOC/EIC中断不使用（避免频繁中断）
**   2. 无suspend/resume接口（直接deinit/init）
**   3. 内部通道使能需在ADC enable之前（硬件限制）
*********************************************************************/

#include "adc_driver.h"
#include "gd32f50x_rcu.h"
#include "gd32f50x_adc.h"
#include <string.h>

/* ==================== 内部宏定义 ==================== */

/* ADC端口数量 */
#define DRV_ADC_PORT_COUNT        (3U)

/* ADC转换完成标志掩码 */
#define ADC_FLAG_EOC              ADC_FLAG_EORC

/* 默认超时时间（ms） */
#define ADC_DEFAULT_TIMEOUT_MS    (100U)

/* ADC稳定等待时间（ms） */
#define ADC_STABLE_WAIT_MS        (10U)

/* ==================== 内部数据结构 ==================== */

/* ADC控制结构体 */
typedef struct
{
    drv_adc_state_t state;              /**< ADC状态（内部使用） */
    SemaphoreHandle_t mutex;            /**< 互斥锁（线程安全） */
    bool use_mutex;                     /**< 是否启用互斥锁 */
    drv_adc_resolution_e resolution;    /**< 保存的分辨率配置 */
    drv_adc_trigger_e trigger;          /**< 保存的触发源配置 */
    uint8_t internal_channels_enabled;  /**< 内部通道已使能位标记 (bit0=CH16温度, bit1=CH17参考电压) */
    drv_adc_wdg_callback_t wdg_callback; /**< 看门狗中断回调函数 */
} drv_adc_ctrl_t;

/* ADC控制数组 */
static drv_adc_ctrl_t s_adc_ctrl[DRV_ADC_PORT_COUNT] = {0};

/* ==================== 内部辅助函数 ==================== */

/*********************************************************************
 * @brief   获取ADC外设基地址
 * @param   port    ADC端口
 * @return  ADC外设基地址
 * @note    内部函数，端口有效性由调用方保证
 *********************************************************************/
static uint32_t _drv_adc_get_periph(drv_adc_port_e port)
{
    static const uint32_t s_adc_periph[DRV_ADC_PORT_COUNT] = {
        ADC0,
        ADC1,
        ADC2
    };

    return s_adc_periph[port];
}

/*********************************************************************
 * @brief   使能ADC时钟
 * @param   port    ADC端口
 * @return  无
 * @note    内部函数
 *********************************************************************/
static void _drv_adc_enable_clock(drv_adc_port_e port)
{
    switch (port)
    {
        case DRV_ADC0:
            rcu_periph_clock_enable(RCU_ADC0);
            break;

        case DRV_ADC1:
            rcu_periph_clock_enable(RCU_ADC1);
            break;

        case DRV_ADC2:
            rcu_periph_clock_enable(RCU_ADC2);
            break;

        default:
            break;
    }
}

/*********************************************************************
 * @brief   禁能ADC时钟
 * @param   port    ADC端口
 * @return  无
 * @note    内部函数
 *********************************************************************/
static void _drv_adc_disable_clock(drv_adc_port_e port)
{
    switch (port)
    {
        case DRV_ADC0:
            rcu_periph_clock_disable(RCU_ADC0);
            break;

        case DRV_ADC1:
            rcu_periph_clock_disable(RCU_ADC1);
            break;

        case DRV_ADC2:
            rcu_periph_clock_disable(RCU_ADC2);
            break;

        default:
            break;
    }
}

/*********************************************************************
 * @brief   映射分辨率枚举到GD32库定义
 * @param   resolution  分辨率枚举
 * @return  GD32库分辨率定义
 * @note    内部函数
 *********************************************************************/
static uint32_t _drv_adc_map_resolution(drv_adc_resolution_e resolution)
{
    static const uint32_t s_resolution_map[] = {
        ADC_RESOLUTION_12B,
        ADC_RESOLUTION_10B,
        ADC_RESOLUTION_8B,
        ADC_RESOLUTION_6B
    };

    return s_resolution_map[resolution];
}

/*********************************************************************
 * @brief   映射对齐方式枚举到GD32库定义
 * @param   data_align  对齐方式枚举
 * @return  GD32库对齐方式定义
 * @note    内部函数
 *********************************************************************/
static uint32_t _drv_adc_map_dataalign(drv_adc_dataalign_e data_align)
{
    static const uint32_t s_dataalign_map[] = {
        ADC_DATAALIGN_RIGHT,
        ADC_DATAALIGN_LEFT
    };

    return s_dataalign_map[data_align];
}

/*********************************************************************
 * @brief   映射采样时间枚举到GD32库定义
 * @param   sample_time 采样时间枚举
 * @return  GD32库采样时间定义
 * @note    内部函数
 *********************************************************************/
static uint32_t _drv_adc_map_sampletime(drv_adc_sampletime_e sample_time)
{
    static const uint32_t s_sampletime_map[] = {
        ADC_SAMPLETIME_1POINT5,
        ADC_SAMPLETIME_7POINT5,
        ADC_SAMPLETIME_13POINT5,
        ADC_SAMPLETIME_28POINT5,
        ADC_SAMPLETIME_41POINT5,
        ADC_SAMPLETIME_55POINT5,
        ADC_SAMPLETIME_71POINT5,
        ADC_SAMPLETIME_239POINT5
    };

    return s_sampletime_map[sample_time];
}

/* ==================== 公开接口实现 ==================== */

/*********************************************************************
 * @brief   ADC初始化
 * @param   config  ADC配置结构体指针
 * @return  int 错误码
 * @note    自动使能ADC时钟，配置分辨率、对齐方式、转换模式
 * @note    禁止在中断上下文中调用（内部使用vTaskDelay等待ADC稳定）
 *********************************************************************/
int drv_adc_init(const drv_adc_config_t *config)
{
    uint32_t adc_periph;

    /* 参数校验 */
    if (config == NULL)
    {
        DRV_ADC_LOGE("drv_adc_init NULL pointer");
        return DRV_ADC_ERR_FAILED;
    }

    if (config->port >= DRV_ADC_PORT_COUNT)
    {
        DRV_ADC_LOGE("drv_adc_init invalid port %d", config->port);
        return DRV_ADC_ERR_INVALID_PARAM;
    }

    if (config->resolution > DRV_ADC_RESOLUTION_6B)
    {
        DRV_ADC_LOGE("drv_adc_init invalid resolution");
        return DRV_ADC_ERR_FAILED;
    }

    adc_periph = _drv_adc_get_periph(config->port);

    /* 检查是否已初始化 */
    if (s_adc_ctrl[config->port].state.is_init)
    {
        DRV_ADC_LOGW("drv_adc_init port %d already init", config->port);
        return DRV_ADC_ERR_OK;
    }

    /* 使能ADC时钟 */
    _drv_adc_enable_clock(config->port);

    /* 复位ADC */
    adc_deinit(adc_periph);

    /* 配置分辨率 */
    adc_resolution_config(adc_periph, _drv_adc_map_resolution(config->resolution));

    /* 配置数据对齐 */
    adc_data_alignment_config(adc_periph, _drv_adc_map_dataalign(config->data_align));

    /* 配置特殊功能（扫描模式、连续模式） */
    switch (config->mode)
    {
        case DRV_ADC_MODE_SINGLE:
            /* 单次转换：关闭扫描和连续 */
            adc_special_function_config(adc_periph, ADC_SCAN_MODE, DISABLE);
            adc_special_function_config(adc_periph, ADC_CONTINUOUS_MODE, DISABLE);
            break;

        case DRV_ADC_MODE_CONTINUOUS:
            /* 连续转换：关闭扫描，开启连续 */
            adc_special_function_config(adc_periph, ADC_SCAN_MODE, DISABLE);
            adc_special_function_config(adc_periph, ADC_CONTINUOUS_MODE, ENABLE);
            break;

        case DRV_ADC_MODE_SCAN_SINGLE:
            /* 扫描+单次：开启扫描，关闭连续 */
            adc_special_function_config(adc_periph, ADC_SCAN_MODE, ENABLE);
            adc_special_function_config(adc_periph, ADC_CONTINUOUS_MODE, DISABLE);
            break;

        case DRV_ADC_MODE_SCAN_CONTINUOUS:
            /* 扫描+连续：开启扫描和连续 */
            adc_special_function_config(adc_periph, ADC_SCAN_MODE, ENABLE);
            adc_special_function_config(adc_periph, ADC_CONTINUOUS_MODE, ENABLE);
            break;

        default:
            DRV_ADC_LOGE("drv_adc_init invalid mode");
            return DRV_ADC_ERR_FAILED;
    }

    /* 配置触发源 */
    if (config->trigger == DRV_ADC_TRIGGER_SOFTWARE)
    {
        /* 软件触发：禁用外部触发 */
        adc_external_trigger_config(adc_periph, ADC_ROUTINE_CHANNEL, EXTERNAL_TRIGGER_DISABLE);
    }
    else if (config->trigger == DRV_ADC_TRIGGER_EXTERNAL)
    {
        /* 外部触发：开启上升沿触发 */
        adc_external_trigger_config(adc_periph, ADC_ROUTINE_CHANNEL, EXTERNAL_TRIGGER_RISING);
    }
    else
    {
        /* DMA触发：由DMA控制，软件不触发 */
        adc_external_trigger_config(adc_periph, ADC_ROUTINE_CHANNEL, EXTERNAL_TRIGGER_DISABLE);
    }

    /* 使能ADC */
    adc_enable(adc_periph);

    /* 等待ADC稳定（使用FreeRTOS延迟） */
    vTaskDelay(pdMS_TO_TICKS(ADC_STABLE_WAIT_MS));

    /* 更新状态 */
    s_adc_ctrl[config->port].state.is_init = true;
    s_adc_ctrl[config->port].state.is_converting = false;
    s_adc_ctrl[config->port].state.mode = config->mode;
    s_adc_ctrl[config->port].state.channel_count = 0;
    s_adc_ctrl[config->port].resolution = config->resolution;
    s_adc_ctrl[config->port].trigger = config->trigger;
    s_adc_ctrl[config->port].internal_channels_enabled = 0;

    /* 创建互斥锁 */
    s_adc_ctrl[config->port].use_mutex = config->use_mutex;
    if (config->use_mutex)
    {
        s_adc_ctrl[config->port].mutex = xSemaphoreCreateMutex();
        if (s_adc_ctrl[config->port].mutex == NULL)
        {
            DRV_ADC_LOGE("drv_adc_init create mutex failed");
            return DRV_ADC_ERR_FAILED;
        }
    }

    DRV_ADC_LOGD("drv_adc_init port %d success", config->port);

    return DRV_ADC_ERR_OK;
}

/*********************************************************************
 * @brief   ADC去初始化
 * @param   port    ADC端口
 * @return  int 错误码
 * @note    关闭ADC端口，释放资源
 *********************************************************************/
int drv_adc_deinit(drv_adc_port_e port)
{
    uint32_t adc_periph;

    /* 参数校验 */
    if (port >= DRV_ADC_PORT_COUNT)
    {
        DRV_ADC_LOGE("drv_adc_deinit invalid port %d", port);
        return DRV_ADC_ERR_INVALID_PARAM;
    }

    /* 检查是否已初始化 */
    if (!s_adc_ctrl[port].state.is_init)
    {
        DRV_ADC_LOGW("drv_adc_deinit port %d not init", port);
        return DRV_ADC_ERR_NOT_INIT;
    }

    adc_periph = _drv_adc_get_periph(port);

    /* 禁能看门狗中断（防止残留中断触发） */
    adc_interrupt_disable(adc_periph, ADC_INT_WD0E);

    /* 清除看门狗中断标志和状态标志 */
    adc_interrupt_flag_clear(adc_periph, ADC_INT_FLAG_WD0E);
    adc_flag_clear(adc_periph, ADC_FLAG_WD0E);

    /* 清除NVIC挂起的中断 */
    if (port == DRV_ADC0 || port == DRV_ADC1)
    {
        NVIC_ClearPendingIRQ(ADC0_1_IRQn);
    }
    else if (port == DRV_ADC2)
    {
        NVIC_ClearPendingIRQ(ADC2_IRQn);
    }

    /* 清除回调函数 */
    s_adc_ctrl[port].wdg_callback = NULL;

    /* 禁能ADC */
    adc_disable(adc_periph);

    /* 复位ADC */
    adc_deinit(adc_periph);

    /* 禁能时钟 */
    _drv_adc_disable_clock(port);

    /* 删除互斥锁 */
    if (s_adc_ctrl[port].mutex != NULL)
    {
        vSemaphoreDelete(s_adc_ctrl[port].mutex);
        s_adc_ctrl[port].mutex = NULL;
    }

    /* 清除状态 */
    memset(&s_adc_ctrl[port].state, 0, sizeof(drv_adc_state_t));

    DRV_ADC_LOGD("drv_adc_deinit port %d success", port);

    return DRV_ADC_ERR_OK;
}

/*********************************************************************
 * @brief   配置规则通道
 * @param   port            ADC端口
 * @param   channel_config  通道配置结构体指针
 * @return  int 错误码
 * @note    配置规则序列通道及采样时间
 *********************************************************************/
int drv_adc_routine_channel_config(drv_adc_port_e port,
                                   const drv_adc_channel_config_t *channel_config)
{
    uint32_t adc_periph;

    /* 参数校验 */
    if (channel_config == NULL)
    {
        DRV_ADC_LOGE("drv_adc_routine_channel_config NULL pointer");
        return DRV_ADC_ERR_FAILED;
    }

    if (port >= DRV_ADC_PORT_COUNT)
    {
        DRV_ADC_LOGE("drv_adc_routine_channel_config invalid port %d", port);
        return DRV_ADC_ERR_INVALID_PARAM;
    }

    if (!s_adc_ctrl[port].state.is_init)
    {
        DRV_ADC_LOGE("drv_adc_routine_channel_config port %d not init", port);
        return DRV_ADC_ERR_NOT_INIT;
    }

    /* ADC0(18通道): 外部通道0-15 + 内部通道16(温度传感器)、17(参考电压) */
    /* ADC1（18通道): 外部通道0-17 */
    /* ADC2（17通道): 外部通道0-16 */
    if (channel_config->channel >= DRV_ADC_CHANNEL_MAX
        || (port == DRV_ADC2 && channel_config->channel >= DRV_ADC_CHANNEL_17))
    {
        DRV_ADC_LOGE("drv_adc_routine_channel_config invalid channel %d", channel_config->channel);
        return DRV_ADC_ERR_INVALID_PARAM;
    }

    /* 规则通道排名 */
    if (channel_config->rank > 15)
    {
        DRV_ADC_LOGE("drv_adc_routine_channel_config invalid rank %d", channel_config->rank);
        return DRV_ADC_ERR_FAILED;
    }

    adc_periph = _drv_adc_get_periph(port);

    /* 配置规则通道 */
    adc_routine_channel_config(
        adc_periph,
        channel_config->rank,
        channel_config->channel,
        _drv_adc_map_sampletime(channel_config->sample_time)
    );

    /* 更新通道计数 */
    if (channel_config->rank >= s_adc_ctrl[port].state.channel_count)
    {
        s_adc_ctrl[port].state.channel_count = channel_config->rank + 1;
    }

    DRV_ADC_LOGD("drv_adc_routine_channel_config port %d channel %d rank %d",
                 port, channel_config->channel, channel_config->rank);

    return DRV_ADC_ERR_OK;
}

/*********************************************************************
 * @brief   配置插入通道
 * @param   port            ADC端口
 * @param   channel_config  通道配置结构体指针
 * @return  int 错误码
 * @note    配置插入序列通道及采样时间
 *********************************************************************/
int drv_adc_inserted_channel_config(drv_adc_port_e port,
                                    const drv_adc_channel_config_t *channel_config)
{
    uint32_t adc_periph;

    /* 参数校验 */
    if (channel_config == NULL)
    {
        DRV_ADC_LOGE("drv_adc_inserted_channel_config NULL pointer");
        return DRV_ADC_ERR_FAILED;
    }

    if (port >= DRV_ADC_PORT_COUNT)
    {
        DRV_ADC_LOGE("drv_adc_inserted_channel_config invalid port %d", port);
        return DRV_ADC_ERR_INVALID_PARAM;
    }

    if (!s_adc_ctrl[port].state.is_init)
    {
        DRV_ADC_LOGE("drv_adc_inserted_channel_config port %d not init", port);
        return DRV_ADC_ERR_NOT_INIT;
    }

    /* ADC0: 外部通道0-15 + 内部通道16(温度传感器)、17(参考电压) */
    /* ADC1: 外部通道0-17 */
    /* ADC2: 外部通道0-16 */
    if (channel_config->channel >= DRV_ADC_CHANNEL_MAX
        || (port == DRV_ADC2 && channel_config->channel >= DRV_ADC_CHANNEL_17))
    {
        DRV_ADC_LOGE("drv_adc_inserted_channel_config invalid channel %d", channel_config->channel);
        return DRV_ADC_ERR_INVALID_PARAM;
    }

    /* 插入通道排名 */
    if (channel_config->rank > 3)
    {
        DRV_ADC_LOGE("drv_adc_inserted_channel_config invalid rank %d", channel_config->rank);
        return DRV_ADC_ERR_FAILED;
    }

    adc_periph = _drv_adc_get_periph(port);

    /* 配置插入通道 */
    adc_inserted_channel_config(
        adc_periph,
        channel_config->rank,
        channel_config->channel,
        _drv_adc_map_sampletime(channel_config->sample_time)
    );

    DRV_ADC_LOGD("drv_adc_inserted_channel_config port %d channel %d rank %d",
                 port, channel_config->channel, channel_config->rank);

    return DRV_ADC_ERR_OK;
}

/*********************************************************************
 * @brief   启动ADC转换（软件触发）
 * @param   port    ADC端口
 * @return  int 错误码
 * @note    仅适用于软件触发模式
 *********************************************************************/
int drv_adc_start_conversion(drv_adc_port_e port)
{
    uint32_t adc_periph;

    /* 参数校验 */
    if (port >= DRV_ADC_PORT_COUNT)
    {
        DRV_ADC_LOGE("drv_adc_start_conversion invalid port %d", port);
        return DRV_ADC_ERR_INVALID_PARAM;
    }

    if (!s_adc_ctrl[port].state.is_init)
    {
        DRV_ADC_LOGE("drv_adc_start_conversion port %d not init", port);
        return DRV_ADC_ERR_NOT_INIT;
    }

    if (s_adc_ctrl[port].state.is_converting)
    {
        DRV_ADC_LOGW("drv_adc_start_conversion port %d busy", port);
        return DRV_ADC_ERR_BUSY;
    }

    adc_periph = _drv_adc_get_periph(port);

    /* 更新状态 */
    s_adc_ctrl[port].state.is_converting = true;

    /* 启动软件转换 */
    adc_software_trigger_enable(adc_periph, ADC_ROUTINE_CHANNEL);

    return DRV_ADC_ERR_OK;
}

/*********************************************************************
 * @brief   等待转换完成
 * @param   port        ADC端口
 * @param   timeout_ms  超时时间（ms）
 * @return  int 错误码
 * @note    阻塞等待转换完成标志
 *********************************************************************/
int drv_adc_wait_conversion_done(drv_adc_port_e port, uint32_t timeout_ms)
{
    uint32_t adc_periph;
    TickType_t start_tick;

    /* 参数校验 */
    if (port >= DRV_ADC_PORT_COUNT)
    {
        DRV_ADC_LOGE("drv_adc_wait_conversion_done invalid port %d", port);
        return DRV_ADC_ERR_INVALID_PARAM;
    }

    if (!s_adc_ctrl[port].state.is_init)
    {
        DRV_ADC_LOGE("drv_adc_wait_conversion_done port %d not init", port);
        return DRV_ADC_ERR_NOT_INIT;
    }

    adc_periph = _drv_adc_get_periph(port);

    /* 使用FreeRTOS tick实现精确超时 */
    start_tick = xTaskGetTickCount();

    /* 等待转换完成标志 */
    while (RESET == adc_flag_get(adc_periph, ADC_FLAG_EOC))
    {
        TickType_t elapsed = xTaskGetTickCount() - start_tick;

        if (elapsed >= pdMS_TO_TICKS(timeout_ms))
        {
            DRV_ADC_LOGE("drv_adc_wait_conversion_done port %d timeout", port);
            s_adc_ctrl[port].state.is_converting = false;
            return DRV_ADC_ERR_TIMEOUT;
        }

        /* 延迟1ms */
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    /* 清除标志 */
    adc_flag_clear(adc_periph, ADC_FLAG_EOC);

    /* 更新状态 */
    s_adc_ctrl[port].state.is_converting = false;

    return DRV_ADC_ERR_OK;
}

/*********************************************************************
 * @brief   读取规则通道数据
 * @param   port    ADC端口
 * @param   data    数据输出指针
 * @return  int 错误码
 * @note    读取规则序列转换结果
 *********************************************************************/
int drv_adc_routine_data_read(drv_adc_port_e port, uint16_t *data)
{
    uint32_t adc_periph;

    /* 参数校验 */
    if (data == NULL)
    {
        DRV_ADC_LOGE("drv_adc_routine_data_read NULL pointer");
        return DRV_ADC_ERR_FAILED;
    }

    if (port >= DRV_ADC_PORT_COUNT)
    {
        DRV_ADC_LOGE("drv_adc_routine_data_read invalid port %d", port);
        return DRV_ADC_ERR_INVALID_PARAM;
    }

    if (!s_adc_ctrl[port].state.is_init)
    {
        DRV_ADC_LOGE("drv_adc_routine_data_read port %d not init", port);
        return DRV_ADC_ERR_NOT_INIT;
    }

    adc_periph = _drv_adc_get_periph(port);

    /* 读取数据 */
    *data = adc_routine_data_read(adc_periph);

    DRV_ADC_LOGD("drv_adc_routine_data_read port %d data %d", port, *data);

    return DRV_ADC_ERR_OK;
}

/*********************************************************************
 * @brief   读取插入通道数据
 * @param   port    ADC端口
 * @param   data    数据输出指针
 * @return  int 错误码
 * @note    读取插入序列转换结果
 *********************************************************************/
int drv_adc_inserted_data_read(drv_adc_port_e port, uint16_t *data)
{
    uint32_t adc_periph;

    /* 参数校验 */
    if (data == NULL)
    {
        DRV_ADC_LOGE("drv_adc_inserted_data_read NULL pointer");
        return DRV_ADC_ERR_FAILED;
    }

    if (port >= DRV_ADC_PORT_COUNT)
    {
        DRV_ADC_LOGE("drv_adc_inserted_data_read invalid port %d", port);
        return DRV_ADC_ERR_INVALID_PARAM;
    }

    if (!s_adc_ctrl[port].state.is_init)
    {
        DRV_ADC_LOGE("drv_adc_inserted_data_read port %d not init", port);
        return DRV_ADC_ERR_NOT_INIT;
    }

    adc_periph = _drv_adc_get_periph(port);

    /* 读取数据 */
    *data = adc_inserted_data_read(adc_periph);

    DRV_ADC_LOGD("drv_adc_inserted_data_read port %d data %d", port, *data);

    return DRV_ADC_ERR_OK;
}

/*********************************************************************
 * @brief   使能ADC
 * @param   port    ADC端口
 * @return  int 错误码
 * @note    使能ADC开始工作
 *********************************************************************/
int32_t drv_adc_enable(drv_adc_port_e port)
{
    uint32_t adc_periph;

    /* 参数校验 */
    if (port >= DRV_ADC_PORT_COUNT)
    {
        DRV_ADC_LOGE("drv_adc_enable invalid port %d", port);
        return DRV_ADC_ERR_INVALID_PARAM;
    }

    /* 检查是否已初始化 */
    if (!s_adc_ctrl[port].state.is_init)
    {
        DRV_ADC_LOGE("drv_adc_enable port %d not init", port);
        return DRV_ADC_ERR_NOT_INIT;
    }

    adc_periph = _drv_adc_get_periph(port);

    /* 使能ADC */
    adc_enable(adc_periph);

    DRV_ADC_LOGD("drv_adc_enable port %d", port);

    return DRV_ADC_ERR_OK;
}

/*********************************************************************
 * @brief   禁能ADC
 * @param   port    ADC端口
 * @return  int 错误码
 * @note    禁能ADC进入低功耗模式
 *********************************************************************/
int32_t drv_adc_disable(drv_adc_port_e port)
{
    uint32_t adc_periph;

    /* 参数校验 */
    if (port >= DRV_ADC_PORT_COUNT)
    {
        DRV_ADC_LOGE("drv_adc_disable invalid port %d", port);
        return DRV_ADC_ERR_INVALID_PARAM;
    }

    adc_periph = _drv_adc_get_periph(port);

    /* 禁能ADC */
    adc_disable(adc_periph);

    /* 清除转换状态标记（ADC已禁能，转换不可能在进行） */
    s_adc_ctrl[port].state.is_converting = false;

    DRV_ADC_LOGD("drv_adc_disable port %d", port);

    return DRV_ADC_ERR_OK;
}


/*********************************************************************
 * @brief   查询ADC标志状态
 * @param   port    ADC端口
 * @param   flag    标志类型
 * @return  true=标志置位，false=标志复位
 * @note    阻塞轮询模式下查询转换完成或看门狗触发
 *********************************************************************/
bool drv_adc_flag_get(drv_adc_port_e port, drv_adc_flag_e flag)
{
    uint32_t adc_periph;
    uint32_t gd32_flag;

    /* 参数校验 */
    if (port >= DRV_ADC_PORT_COUNT)
    {
        DRV_ADC_LOGE("drv_adc_flag_get invalid port %d", port);
        return false;
    }

    adc_periph = _drv_adc_get_periph(port);

    /* 转换标志枚举为GD32库标志 */
    switch (flag)
    {
        case DRV_ADC_FLAG_EOC:
            gd32_flag = ADC_FLAG_EORC;
            break;
        case DRV_ADC_FLAG_WD0E:
            gd32_flag = ADC_FLAG_WD0E;
            break;
        default:
            DRV_ADC_LOGE("drv_adc_flag_get invalid flag %d", flag);
            return false;
    }

    return (adc_flag_get(adc_periph, gd32_flag) != RESET);
}

/*********************************************************************
 * @brief   清除ADC标志
 * @param   port    ADC端口
 * @param   flag    标志类型
 * @return  int 错误码
 * @note    清除指定的ADC标志，避免重复触发
 *********************************************************************/
int32_t drv_adc_flag_clear(drv_adc_port_e port, drv_adc_flag_e flag)
{
    uint32_t adc_periph;
    uint32_t gd32_flag;

    /* 参数校验 */
    if (port >= DRV_ADC_PORT_COUNT)
    {
        DRV_ADC_LOGE("drv_adc_flag_clear invalid port %d", port);
        return DRV_ADC_ERR_INVALID_PARAM;
    }

    adc_periph = _drv_adc_get_periph(port);

    /* 转换标志枚举为GD32库标志 */
    switch (flag)
    {
        case DRV_ADC_FLAG_EOC:
            /* 清除EOC标志（读取数据时会自动清除，此处显式调用使接口更完整） */
            adc_flag_clear(adc_periph, ADC_FLAG_EOC);
            return DRV_ADC_ERR_OK;

        case DRV_ADC_FLAG_WD0E:
            /* 清除看门狗状态标志和中断标志 */
            adc_flag_clear(adc_periph, ADC_FLAG_WD0E);
            adc_interrupt_flag_clear(adc_periph, ADC_INT_FLAG_WD0E);
            break;

        default:
            DRV_ADC_LOGE("drv_adc_flag_clear invalid flag %d", flag);
            return DRV_ADC_ERR_INVALID_PARAM;
    }

    DRV_ADC_LOGD("drv_adc_flag_clear port %d flag %d", port, flag);

    return DRV_ADC_ERR_OK;
}

/*********************************************************************
 * @brief   读取ADC转换结果
 * @param   port    ADC端口
 * @return  uint16_t ADC值（0-4095，12位分辨率）
 * @note    读取规则通道转换结果，自动清除EORC标志
 *          适用于轮询模式
 *********************************************************************/
uint16_t drv_adc_read(drv_adc_port_e port)
{
    uint32_t adc_periph;

    /* 参数校验 */
    if (port >= DRV_ADC_PORT_COUNT)
    {
        DRV_ADC_LOGE("drv_adc_read invalid port %d", port);
        return 0;
    }

    adc_periph = _drv_adc_get_periph(port);

    /* 读取规则通道数据，自动清除EORC标志 */
    return (uint16_t)adc_routine_data_read(adc_periph);
}

/*********************************************************************
 * @brief   配置规则通道数量
 * @param   port    ADC端口
 * @param   count   通道数量（1-16）
 * @return  int 错误码
 * @note    设置规则序列通道数量
 *********************************************************************/
int32_t drv_adc_channel_count(drv_adc_port_e port, uint8_t count)
{
    uint32_t adc_periph;

    /* 参数校验 */
    if (port >= DRV_ADC_PORT_COUNT)
    {
        DRV_ADC_LOGE("drv_adc_channel_count invalid port %d", port);
        return DRV_ADC_ERR_INVALID_PARAM;
    }

    if (count == 0 || count > 16)
    {
        DRV_ADC_LOGE("drv_adc_channel_count invalid count %d", count);
        return DRV_ADC_ERR_INVALID_PARAM;
    }

    adc_periph = _drv_adc_get_periph(port);

    /* 配置规则通道数量 */
    adc_channel_length_config(adc_periph, ADC_ROUTINE_CHANNEL, count);

    DRV_ADC_LOGD("drv_adc_channel_count port %d count %d", port, count);

    return DRV_ADC_ERR_OK;
}

/*********************************************************************
 * @brief   单通道单次转换（便捷接口）
 * @param   port        ADC端口
 * @param   channel     ADC通道
 * @param   sample_time 采样时间
 * @param   data        数据输出指针
 * @return  int 错误码
 * @note    配置单通道并执行单次转换，读取结果（最常用接口）
 * @note    禁止在中断上下文中调用（内部使用vTaskDelay等待内部通道稳定）
 *********************************************************************/
int drv_adc_single_read(drv_adc_port_e port, drv_adc_channel_e channel,
                        drv_adc_sampletime_e sample_time, uint16_t *data)
{
    int ret;
    drv_adc_channel_config_t channel_config;
    uint32_t timeout_ms = ADC_DEFAULT_TIMEOUT_MS;

    /* 参数校验 */
    if (data == NULL)
    {
        DRV_ADC_LOGE("drv_adc_single_read NULL pointer");
        return DRV_ADC_ERR_FAILED;
    }

    if (port >= DRV_ADC_PORT_COUNT)
    {
        DRV_ADC_LOGE("drv_adc_single_read invalid port %d", port);
        return DRV_ADC_ERR_INVALID_PARAM;
    }

    if (!s_adc_ctrl[port].state.is_init)
    {
        DRV_ADC_LOGE("drv_adc_single_read port %d not init", port);
        return DRV_ADC_ERR_NOT_INIT;
    }

    /* ADC0(18通道): 外部通道0-15 + 内部通道16(温度传感器)、17(参考电压) */
    /* ADC1（18通道): 外部通道0-17 */
    /* ADC2（17通道): 外部通道0-16 */
    if (channel >= DRV_ADC_CHANNEL_MAX
        || (port == DRV_ADC2 && channel >= DRV_ADC_CHANNEL_17))
    {
        DRV_ADC_LOGE("drv_adc_single_read invalid channel %d for port %d", channel, port);
        return DRV_ADC_ERR_INVALID_PARAM;
    }

    /* 获取互斥锁（最多等待1秒，避免死锁） */
    if (s_adc_ctrl[port].use_mutex && s_adc_ctrl[port].mutex != NULL)
    {
        if (xSemaphoreTake(s_adc_ctrl[port].mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
        {
            DRV_ADC_LOGE("drv_adc_single_read mutex timeout port %d", port);
            return DRV_ADC_ERR_TIMEOUT;
        }
    }

    /* 配置通道 */
    channel_config.channel = channel;
    channel_config.sample_time = sample_time;
    channel_config.rank = 0;

    ret = drv_adc_routine_channel_config(port, &channel_config);
    if (ret != DRV_ADC_ERR_OK)
    {
        goto exit;
    }

    /* 对于ADC0的内部通道，按需使能（bit0=CH16温度, bit1=CH17参考电压） */
    if (port == DRV_ADC0 && channel >= DRV_ADC_CHANNEL_16)
    {
        uint32_t adc_periph = _drv_adc_get_periph(port);
        uint8_t ch_bit = (channel == DRV_ADC_CHANNEL_16) ? 0x01 : 0x02;

        /* 该内部通道未使能时才配置 */
        if (!(s_adc_ctrl[port].internal_channels_enabled & ch_bit))
        {
            if (channel == DRV_ADC_CHANNEL_16)
            {
                adc_internal_channel_config(ADC_CHANNEL_INTERNAL_TEMPSENSOR, ENABLE);
            }
            else
            {
                adc_internal_channel_config(ADC_CHANNEL_INTERNAL_VREFINT, ENABLE);
            }

            /* 重新使能ADC以应用内部通道配置 */
            adc_disable(adc_periph);
            adc_enable(adc_periph);

            /* 标记该通道已使能 */
            s_adc_ctrl[port].internal_channels_enabled |= ch_bit;
        }

        /* 内部通道需要更长的稳定时间（温度传感器需要约10us，这里给10ms） */
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    /* 配置通道长度为1 */
    adc_channel_length_config(_drv_adc_get_periph(port), ADC_ROUTINE_CHANNEL, 1);

    /* 启动转换 */
    ret = drv_adc_start_conversion(port);
    if (ret != DRV_ADC_ERR_OK)
    {
        goto exit;
    }

    /* 等待转换完成 */
    ret = drv_adc_wait_conversion_done(port, timeout_ms);
    if (ret != DRV_ADC_ERR_OK)
    {
        goto exit;
    }

    /* 读取数据 */
    ret = drv_adc_routine_data_read(port, data);
    if (ret != DRV_ADC_ERR_OK)
    {
        goto exit;
    }

    DRV_ADC_LOGD("drv_adc_single_read port %d channel %d data %d", port, channel, *data);

exit:
    /* 释放互斥锁 */
    if (s_adc_ctrl[port].use_mutex && s_adc_ctrl[port].mutex != NULL)
    {
        xSemaphoreGive(s_adc_ctrl[port].mutex);
    }

    return ret;
}

/*********************************************************************
 * @brief   配置模拟看门狗（含中断回调）
 * @param   port           ADC端口
 * @param   channel        监控通道
 * @param   low_threshold  低阈值
 * @param   high_threshold 高阈值
 * @param   callback       看门狗中断回调函数（可为NULL）
 * @return  int 错误码
 * @note    当转换结果超出阈值范围时触发看门狗中断
 *          callback为NULL时不使能中断，仅配置阈值
 *          主要用于低功耗唤醒场景
 *********************************************************************/
int drv_adc_watchdog_config(drv_adc_port_e port, drv_adc_channel_e channel,
                            uint32_t low_threshold, uint32_t high_threshold,
                            drv_adc_wdg_callback_t callback)
{
    uint32_t adc_periph;

    /* 参数校验 */
    if (port >= DRV_ADC_PORT_COUNT)
    {
        DRV_ADC_LOGE("drv_adc_watchdog_config invalid port %d", port);
        return DRV_ADC_ERR_INVALID_PARAM;
    }

    if (!s_adc_ctrl[port].state.is_init)
    {
        DRV_ADC_LOGE("drv_adc_watchdog_config port %d not init", port);
        return DRV_ADC_ERR_NOT_INIT;
    }

    /* ADC0: 外部通道0-15 + 内部通道16(温度传感器)、17(参考电压) */
    /* ADC1: 外部通道0-17 */
    /* ADC2: 外部通道0-16 */
    if (channel >= DRV_ADC_CHANNEL_MAX
        || (port == DRV_ADC2 && channel >= DRV_ADC_CHANNEL_17))
    {
        DRV_ADC_LOGE("drv_adc_watchdog_config invalid channel %d", channel);
        return DRV_ADC_ERR_INVALID_PARAM;
    }

    /* 阈值校验 */
    if (low_threshold > high_threshold)
    {
        DRV_ADC_LOGE("drv_adc_watchdog_config invalid threshold");
        return DRV_ADC_ERR_FAILED;
    }

    /* 获取ADC外设 */
    adc_periph = _drv_adc_get_periph(port);

    /* 配置看门狗阈值（必须先于通道使能） */
    adc_watchdog0_threshold_config(adc_periph, low_threshold, high_threshold);

    /* 使能看门狗单通道 */
    adc_watchdog0_single_channel_enable(adc_periph, channel);

    /* 保存回调函数 */
    s_adc_ctrl[port].wdg_callback = callback;

    /* 如果提供了回调函数，使能中断 */
    if (callback != NULL)
    {
        /* 清除看门狗中断标志和状态标志 */
        adc_interrupt_flag_clear(adc_periph, ADC_INT_FLAG_WD0E);
        adc_flag_clear(adc_periph, ADC_FLAG_WD0E);

        /* 清除NVIC挂起的中断（防止残留中断立即触发） */
        if (port == DRV_ADC0 || port == DRV_ADC1)
        {
            NVIC_ClearPendingIRQ(ADC0_1_IRQn);
        }
        else if (port == DRV_ADC2)
        {
            NVIC_ClearPendingIRQ(ADC2_IRQn);
        }

        /* 使能看门狗中断 */
        adc_interrupt_enable(adc_periph, ADC_INT_WD0E);

        /* 配置NVIC（优先级6） */
        if (port == DRV_ADC0 || port == DRV_ADC1)
        {
            nvic_irq_enable(ADC0_1_IRQn, 6, 0);
        }
        else if (port == DRV_ADC2)
        {
            nvic_irq_enable(ADC2_IRQn, 6, 0);
        }

        DRV_ADC_LOGD("drv_adc_watchdog_config port %d channel %d low %d high %d (interrupt enabled)",
                     port, channel, low_threshold, high_threshold);
    }
    else
    {
        DRV_ADC_LOGD("drv_adc_watchdog_config port %d channel %d low %d high %d (no interrupt)",
                     port, channel, low_threshold, high_threshold);
    }

    return DRV_ADC_ERR_OK;
}

/*********************************************************************
 * @brief   使能ADC DMA模式
 * @param   port    ADC端口
 * @return  int 错误码
 * @note    使能规则通道DMA请求
 *********************************************************************/
int drv_adc_dma_mode_enable(drv_adc_port_e port)
{
    uint32_t adc_periph;

    /* 参数校验 */
    if (port >= DRV_ADC_PORT_COUNT)
    {
        DRV_ADC_LOGE("drv_adc_dma_mode_enable invalid port %d", port);
        return DRV_ADC_ERR_INVALID_PARAM;
    }

    /* 检查是否已初始化 */
    if (!s_adc_ctrl[port].state.is_init)
    {
        DRV_ADC_LOGE("drv_adc_dma_mode_enable port %d not init", port);
        return DRV_ADC_ERR_NOT_INIT;
    }

    /* 获取ADC外设 */
    adc_periph = _drv_adc_get_periph(port);

    /* 使能DMA请求 */
    adc_dma_mode_enable(adc_periph, ADC_ROUTINE_CHANNEL);

    DRV_ADC_LOGD("drv_adc_dma_mode_enable port %d", port);

    return DRV_ADC_ERR_OK;
}

/*********************************************************************
 * @brief   禁能ADC DMA模式
 * @param   port    ADC端口
 * @return  int 错误码
 * @note    禁能规则通道DMA请求
 *********************************************************************/
int drv_adc_dma_mode_disable(drv_adc_port_e port)
{
    uint32_t adc_periph;

    /* 参数校验 */
    if (port >= DRV_ADC_PORT_COUNT)
    {
        DRV_ADC_LOGE("drv_adc_dma_mode_disable invalid port %d", port);
        return DRV_ADC_ERR_INVALID_PARAM;
    }

    if (!s_adc_ctrl[port].state.is_init)
    {
        DRV_ADC_LOGE("drv_adc_dma_mode_disable port %d not init", port);
        return DRV_ADC_ERR_NOT_INIT;
    }

    adc_periph = _drv_adc_get_periph(port);

    /* 禁能DMA请求 */
    adc_dma_mode_disable(adc_periph, ADC_ROUTINE_CHANNEL);

    DRV_ADC_LOGD("drv_adc_dma_mode_disable port %d", port);

    return DRV_ADC_ERR_OK;
}

/*********************************************************************
 * @brief   ADC中断处理函数（由ISR调用）
 * @param   port    ADC端口
 * @return  none
 * @note    在gd32f50x_it.c的ISR中调用，仅处理看门狗中断
 *********************************************************************/
void drv_adc_irq_handler(drv_adc_port_e port)
{
    uint32_t adc_periph;

    /* 参数校验 */
    if (port >= DRV_ADC_PORT_COUNT)
    {
        return;
    }

    adc_periph = _drv_adc_get_periph(port);

    /* 检查看门狗中断标志 */
    if (RESET != adc_interrupt_flag_get(adc_periph, ADC_INT_FLAG_WD0E))
    {
        /* 清除中断标志（防止重复进入） */
        adc_interrupt_flag_clear(adc_periph, ADC_INT_FLAG_WD0E);

        /* 执行回调函数 */
        if (s_adc_ctrl[port].wdg_callback != NULL)
        {
            s_adc_ctrl[port].wdg_callback(port);
        }
    }
}
