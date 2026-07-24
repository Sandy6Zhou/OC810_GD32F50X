/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       dma_driver.c
**文件描述：       DMA驱动模块实现文件
**当前版本：       V1.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.05.08
*********************************************************************
** 功能描述：       1. 实现DMA通道初始化、启动/停止
**                 2. 实现中断回调注册与管理
**                 3. 实现运行时配置（传输数量、地址修改）
**                 4. 自动管理时钟、DMAMUX、NVIC配置
*********************************************************************/

#include "dma_driver.h"

/*********************************************************************
 * 内部数据类型定义
 *********************************************************************/

/** DMA 通道状态枚举（内部使用） */
typedef enum
{
    DRV_DMA_STATE_IDLE = 0,          /**< 未初始化 */
    DRV_DMA_STATE_INITIALIZED,       /**< 已初始化，未启动 */
    DRV_DMA_STATE_RUNNING            /**< 运行中 */
} drv_dma_state_e;

/** DMA 通道控制块（内部使用，应用层不可见） */
typedef struct
{
    drv_dma_state_e state;                    /**< 通道状态 */
    uint32_t dma_periph;                      /**< DMA 控制器基地址（DMA0/DMA1） */
    uint8_t channel_index;                    /**< 通道索引（0-6） */
    uint8_t nvic_irqn;                        /**< NVIC 中断号 */
    uint8_t nvic_priority;                    /**< NVIC 中断优先级 */
    drv_dma_callback_t ftf_callback;          /**< 传输完成中断回调 */
    drv_dma_callback_t htf_callback;          /**< 半传输中断回调 */
    drv_dma_callback_t err_callback;          /**< 错误中断回调 */
} drv_dma_ctrl_t;

/** DMA 通道映射表（编译期初始化，存储硬件静态信息） */
typedef struct
{
    uint32_t dma_periph;                      /**< DMA 控制器基地址 */
    uint8_t channel_index;                    /**< 通道索引 */
    uint8_t nvic_irqn;                        /**< NVIC 中断号 */
} drv_dma_map_t;

/*********************************************************************
 * 内部宏定义
 *********************************************************************/

/** 通道 ID 合法性检查 */
#define DMA_CHECK_ID(ch) \
    do { \
        if ((ch) >= DRV_DMA_MAX) { \
            DRV_DMA_LOGE("Invalid channel ID: %d", ch); \
            return DRV_DMA_ERR_INVALID_CHANNEL; \
        } \
    } while(0)

/** 初始化状态检查 */
#define DMA_CHECK_INIT(ch) \
    do { \
        if (s_dma_ctrl[ch].state == DRV_DMA_STATE_IDLE) { \
            DRV_DMA_LOGE("Channel %d not initialized", ch); \
            return DRV_DMA_ERR_NOT_INITIALIZED; \
        } \
    } while(0)

/*********************************************************************
 * 内部全局变量
 *********************************************************************/

/** DMA 通道映射表（编译期初始化） */
static const drv_dma_map_t s_dma_map[DRV_DMA_MAX] =
{
    /* DMA0 通道 */
    {DMA0, DMA_CH0, DMA0_Channel0_IRQn},
    {DMA0, DMA_CH1, DMA0_Channel1_IRQn},
    {DMA0, DMA_CH2, DMA0_Channel2_IRQn},
    {DMA0, DMA_CH3, DMA0_Channel3_IRQn},
    {DMA0, DMA_CH4, DMA0_Channel4_IRQn},
    {DMA0, DMA_CH5, DMA0_Channel5_IRQn},
    {DMA0, DMA_CH6, DMA0_Channel6_IRQn},

    /* DMA1 通道 */
    {DMA1, DMA_CH0, DMA1_Channel0_IRQn},
    {DMA1, DMA_CH1, DMA1_Channel1_IRQn},
    {DMA1, DMA_CH2, DMA1_Channel2_IRQn},
    {DMA1, DMA_CH3, DMA1_Channel3_IRQn},
    {DMA1, DMA_CH4, DMA1_Channel4_IRQn}
};

/** DMA 通道控制块数组 */
static drv_dma_ctrl_t s_dma_ctrl[DRV_DMA_MAX] = {0};

/*********************************************************************
 * 内部辅助函数
 *********************************************************************/

/**
 * @brief  使能 DMA 控制器时钟
 * @param  dma_periph DMA 控制器基地址
 * @return 无
 */
static void _drv_dma_enable_clock(uint32_t dma_periph)
{
    /* 使能 DMAMUX 时钟（必须先于 DMA 时钟使能） */
    rcu_periph_clock_enable(RCU_DMAMUX);

    if (dma_periph == DMA0)
    {
        rcu_periph_clock_enable(RCU_DMA0);
    }
    else
    {
        rcu_periph_clock_enable(RCU_DMA1);
    }
}

/**
 * @brief  配置 DMAMUX 请求源
 * @param  channel_index DMA 通道索引（0-6）
 * @param  request_id DMAMUX 请求源 ID
 * @param  dma_periph DMA 控制器基地址
 * @return 无
 */
static void _drv_dma_config_dmamux(uint8_t channel_index, uint32_t request_id, uint32_t dma_periph)
{
    /* DMA0 通道 0-6 对应 DMAMUX 0-6 */
    /* DMA1 通道 0-4 对应 DMAMUX 7-11 */
    dmamux_multiplexer_channel_enum dmamux_ch;

    if (dma_periph == DMA0)
    {
        dmamux_ch = (dmamux_multiplexer_channel_enum)channel_index;  /* 0-6 */
    }
    else
    {
        dmamux_ch = (dmamux_multiplexer_channel_enum)(channel_index + 7);  /* 7-11 */
    }

    dmamux_request_id_config(dmamux_ch, request_id);
}

/**
 * @brief  转换传输方向为 GD32 库格式
 * @param  direction 驱动层传输方向
 * @return GD32 库传输方向
 */
static uint8_t _drv_dma_convert_direction(drv_dma_direction_e direction)
{
    switch (direction)
    {
        case DRV_DMA_DIR_PERIPH_TO_MEMORY:
            return DMA_PERIPHERAL_TO_MEMORY;

        case DRV_DMA_DIR_MEMORY_TO_PERIPH:
            return DMA_MEMORY_TO_PERIPHERAL;

        case DRV_DMA_DIR_MEMORY_TO_MEMORY:
            return DMA_PERIPHERAL_TO_MEMORY;  /* M2M 需要额外设置 M2M 位 */

        default:
            return DMA_PERIPHERAL_TO_MEMORY;
    }
}

/**
 * @brief  转换数据宽度为 GD32 库格式
 * @param  width 驱动层数据宽度
 * @param  is_memory 是否为内存宽度（true=内存，false=外设）
 * @return GD32 库数据宽度
 */
static uint32_t _drv_dma_convert_width(drv_dma_width_e width, bool is_memory)
{
    if (is_memory)
    {
        switch (width)
        {
            case DRV_DMA_WIDTH_8BIT:
                return DMA_MEMORY_WIDTH_8BIT;

            case DRV_DMA_WIDTH_16BIT:
                return DMA_MEMORY_WIDTH_16BIT;

            case DRV_DMA_WIDTH_32BIT:
                return DMA_MEMORY_WIDTH_32BIT;

            default:
                return DMA_MEMORY_WIDTH_8BIT;
        }
    }
    else
    {
        switch (width)
        {
            case DRV_DMA_WIDTH_8BIT:
                return DMA_PERIPHERAL_WIDTH_8BIT;

            case DRV_DMA_WIDTH_16BIT:
                return DMA_PERIPHERAL_WIDTH_16BIT;

            case DRV_DMA_WIDTH_32BIT:
                return DMA_PERIPHERAL_WIDTH_32BIT;

            default:
                return DMA_PERIPHERAL_WIDTH_8BIT;
        }
    }
}

/**
 * @brief  转换优先级为 GD32 库格式
 * @param  priority 驱动层优先级
 * @return GD32 库优先级
 */
static uint32_t _drv_dma_convert_priority(drv_dma_priority_e priority)
{
    switch (priority)
    {
        case DRV_DMA_PRIORITY_LOW:
            return DMA_PRIORITY_LOW;

        case DRV_DMA_PRIORITY_MEDIUM:
            return DMA_PRIORITY_MEDIUM;

        case DRV_DMA_PRIORITY_HIGH:
            return DMA_PRIORITY_HIGH;

        case DRV_DMA_PRIORITY_ULTRA_HIGH:
            return DMA_PRIORITY_ULTRA_HIGH;

        default:
            return DMA_PRIORITY_LOW;
    }
}

/*********************************************************************
 * API 接口实现
 *********************************************************************/

/*********************************************************************
 * @brief   初始化 DMA 通道
 * @param   channel_id DMA 通道 ID
 * @param   config 通道配置参数
 * @return  DRV_DMA_ERR_OK=成功，其他=失败
 *********************************************************************/
int32_t drv_dma_init(drv_dma_channel_id_e channel_id, drv_dma_config_t *config)
{
    dma_parameter_struct dma_param;

    /* 参数检查 */
    DMA_CHECK_ID(channel_id);
    if (config == NULL)
    {
        DRV_DMA_LOGE("Invalid config pointer");
        return DRV_DMA_ERR_INVALID_PARAM;
    }

    /* 检查是否已初始化 */
    if (s_dma_ctrl[channel_id].state != DRV_DMA_STATE_IDLE)
    {
        DRV_DMA_LOGW("Channel %d already initialized", channel_id);
        return DRV_DMA_ERR_BUSY;
    }

    /* 从映射表获取通道信息 */
    s_dma_ctrl[channel_id].dma_periph = s_dma_map[channel_id].dma_periph;
    s_dma_ctrl[channel_id].channel_index = s_dma_map[channel_id].channel_index;
    s_dma_ctrl[channel_id].nvic_irqn = s_dma_map[channel_id].nvic_irqn;

    /* 使能 DMA 控制器时钟 */
    _drv_dma_enable_clock(s_dma_ctrl[channel_id].dma_periph);

    /* 配置 DMAMUX 请求源 */
    _drv_dma_config_dmamux(s_dma_ctrl[channel_id].channel_index, config->request_id, s_dma_ctrl[channel_id].dma_periph);

    /* 初始化 DMA 参数结构体 */
    dma_struct_para_init(&dma_param);

    dma_param.request = config->request_id;
    dma_param.periph_addr = config->periph_addr;
    dma_param.memory_addr = config->memory_addr;
    dma_param.periph_width = _drv_dma_convert_width(config->periph_width, false);
    dma_param.memory_width = _drv_dma_convert_width(config->memory_width, true);
    dma_param.number = config->transfer_number;
    dma_param.priority = _drv_dma_convert_priority(config->priority);
    dma_param.periph_inc = config->periph_inc ? DMA_PERIPH_INCREASE_ENABLE : DMA_PERIPH_INCREASE_DISABLE;
    dma_param.memory_inc = config->memory_inc ? DMA_MEMORY_INCREASE_ENABLE : DMA_MEMORY_INCREASE_DISABLE;
    dma_param.direction = _drv_dma_convert_direction(config->direction);

    /* 初始化 DMA 通道 */
    dma_init(s_dma_ctrl[channel_id].dma_periph,
             (dma_channel_enum)s_dma_ctrl[channel_id].channel_index,
             &dma_param);

    /* 配置循环模式 */
    if (config->mode == DRV_DMA_MODE_CIRCULAR)
    {
        dma_circulation_enable(s_dma_ctrl[channel_id].dma_periph,
                               (dma_channel_enum)s_dma_ctrl[channel_id].channel_index);
    }

    /* 配置内存到内存模式 */
    if (config->direction == DRV_DMA_DIR_MEMORY_TO_MEMORY)
    {
        dma_memory_to_memory_enable(s_dma_ctrl[channel_id].dma_periph,
                                    (dma_channel_enum)s_dma_ctrl[channel_id].channel_index);
    }

    /* 更新状态 */
    s_dma_ctrl[channel_id].state = DRV_DMA_STATE_INITIALIZED;

    DRV_DMA_LOGD("Channel %d initialized", channel_id);
    return DRV_DMA_ERR_OK;
}

/*********************************************************************
 * @brief   去初始化 DMA 通道
 * @param   channel_id DMA 通道 ID
 * @return  DRV_DMA_ERR_OK=成功，其他=失败
 *********************************************************************/
int32_t drv_dma_deinit(drv_dma_channel_id_e channel_id)
{
    /* 参数检查 */
    DMA_CHECK_ID(channel_id);
    DMA_CHECK_INIT(channel_id);

    /* 停止通道 */
    dma_channel_disable(s_dma_ctrl[channel_id].dma_periph,
                        (dma_channel_enum)s_dma_ctrl[channel_id].channel_index);

    /* 去初始化 DMA 通道 */
    dma_deinit(s_dma_ctrl[channel_id].dma_periph,
               (dma_channel_enum)s_dma_ctrl[channel_id].channel_index);

    /* 清除控制块 */
    s_dma_ctrl[channel_id].state = DRV_DMA_STATE_IDLE;
    s_dma_ctrl[channel_id].ftf_callback = NULL;
    s_dma_ctrl[channel_id].htf_callback = NULL;
    s_dma_ctrl[channel_id].err_callback = NULL;
    s_dma_ctrl[channel_id].nvic_priority = 0;

    DRV_DMA_LOGD("Channel %d deinitialized", channel_id);
    return DRV_DMA_ERR_OK;
}

/*********************************************************************
 * @brief   启动 DMA 传输
 * @param   channel_id DMA 通道 ID
 * @return  DRV_DMA_ERR_OK=成功，其他=失败
 *********************************************************************/
int32_t drv_dma_start(drv_dma_channel_id_e channel_id)
{
    /* 参数检查 */
    DMA_CHECK_ID(channel_id);
    DMA_CHECK_INIT(channel_id);

    /* 检查是否已在运行 */
    if (s_dma_ctrl[channel_id].state == DRV_DMA_STATE_RUNNING)
    {
        DRV_DMA_LOGW("Channel %d already running", channel_id);
        return DRV_DMA_ERR_BUSY;
    }

    /* 使能 DMA 通道 */
    dma_channel_enable(s_dma_ctrl[channel_id].dma_periph,
                       (dma_channel_enum)s_dma_ctrl[channel_id].channel_index);

    /* 更新状态 */
    s_dma_ctrl[channel_id].state = DRV_DMA_STATE_RUNNING;

    DRV_DMA_LOGD("Channel %d started", channel_id);
    return DRV_DMA_ERR_OK;
}

/*********************************************************************
 * @brief   停止 DMA 传输
 * @param   channel_id DMA 通道 ID
 * @return  DRV_DMA_ERR_OK=成功，其他=失败
 *********************************************************************/
int32_t drv_dma_stop(drv_dma_channel_id_e channel_id)
{
    /* 参数检查 */
    DMA_CHECK_ID(channel_id);
    DMA_CHECK_INIT(channel_id);

    /* 检查是否已停止 */
    if (s_dma_ctrl[channel_id].state == DRV_DMA_STATE_INITIALIZED)
    {
        DRV_DMA_LOGW("Channel %d already stopped", channel_id);
        return DRV_DMA_ERR_OK;
    }

    /* 禁能 DMA 通道 */
    dma_channel_disable(s_dma_ctrl[channel_id].dma_periph,
                        (dma_channel_enum)s_dma_ctrl[channel_id].channel_index);

    /* 更新状态 */
    s_dma_ctrl[channel_id].state = DRV_DMA_STATE_INITIALIZED;

    DRV_DMA_LOGD("Channel %d stopped", channel_id);
    return DRV_DMA_ERR_OK;
}

/*********************************************************************
 * @brief   禁能 DMA 通道（寄存器操作，ISR 安全）
 * @param   channel_id DMA 通道 ID
 * @return  none
 * @note    仅操作寄存器，不更新状态、不打印日志，适用于中断上下文
 *********************************************************************/
void drv_dma_channel_disable(drv_dma_channel_id_e channel_id)
{
    if (channel_id >= DRV_DMA_MAX)
    {
        return;
    }

    dma_channel_disable(s_dma_ctrl[channel_id].dma_periph,
                        (dma_channel_enum)s_dma_ctrl[channel_id].channel_index);
}

/*********************************************************************
 * @brief   注册 DMA 中断回调函数
 * @param   channel_id DMA 通道 ID
 * @param   int_type 中断类型（FTF/HTF/ERR）
 * @param   callback 回调函数指针
 * @return  DRV_DMA_ERR_OK=成功，其他=失败
 *********************************************************************/
int32_t drv_dma_callback_register(drv_dma_channel_id_e channel_id,
                                  drv_dma_int_type_e int_type,
                                  drv_dma_callback_t callback)
{
    /* 参数检查 */
    DMA_CHECK_ID(channel_id);
    DMA_CHECK_INIT(channel_id);

    if (callback == NULL)
    {
        DRV_DMA_LOGE("Invalid callback pointer");
        return DRV_DMA_ERR_INVALID_PARAM;
    }

    /* 注册回调 */
    switch (int_type)
    {
        case DRV_DMA_INT_FTF:
            s_dma_ctrl[channel_id].ftf_callback = callback;
            break;

        case DRV_DMA_INT_HTF:
            s_dma_ctrl[channel_id].htf_callback = callback;
            break;

        case DRV_DMA_INT_ERR:
            s_dma_ctrl[channel_id].err_callback = callback;
            break;

        default:
            DRV_DMA_LOGE("Invalid interrupt type: %d", int_type);
            return DRV_DMA_ERR_INVALID_PARAM;
    }

    DRV_DMA_LOGD("Channel %d callback registered (type=%d)", channel_id, int_type);
    return DRV_DMA_ERR_OK;
}

/*********************************************************************
 * @brief   注销 DMA 中断回调函数
 * @param   channel_id DMA 通道 ID
 * @param   int_type 中断类型
 * @return  DRV_DMA_ERR_OK=成功，其他=失败
 *********************************************************************/
int32_t drv_dma_callback_unregister(drv_dma_channel_id_e channel_id,
                                    drv_dma_int_type_e int_type)
{
    /* 参数检查 */
    DMA_CHECK_ID(channel_id);
    DMA_CHECK_INIT(channel_id);

    /* 注销回调 */
    switch (int_type)
    {
        case DRV_DMA_INT_FTF:
            s_dma_ctrl[channel_id].ftf_callback = NULL;
            break;

        case DRV_DMA_INT_HTF:
            s_dma_ctrl[channel_id].htf_callback = NULL;
            break;

        case DRV_DMA_INT_ERR:
            s_dma_ctrl[channel_id].err_callback = NULL;
            break;

        default:
            DRV_DMA_LOGE("Invalid interrupt type: %d", int_type);
            return DRV_DMA_ERR_INVALID_PARAM;
    }

    DRV_DMA_LOGD("Channel %d callback unregistered (type=%d)", channel_id, int_type);
    return DRV_DMA_ERR_OK;
}

/*********************************************************************
 * @brief   使能 DMA 中断
 * @param   channel_id DMA 通道 ID
 * @param   int_type 中断类型（可组合）
 * @param   nvic_priority NVIC 中断优先级（0-15）
 * @return  DRV_DMA_ERR_OK=成功，其他=失败
 *********************************************************************/
int32_t drv_dma_int_enable(drv_dma_channel_id_e channel_id,
                           uint8_t int_type,
                           uint8_t nvic_priority)
{
    uint32_t dma_int = 0;

    /* 参数检查 */
    DMA_CHECK_ID(channel_id);
    DMA_CHECK_INIT(channel_id);

    /* 保存 NVIC 优先级 */
    s_dma_ctrl[channel_id].nvic_priority = nvic_priority;

    /* 转换中断类型 */
    if (int_type & DRV_DMA_INT_FTF)
    {
        dma_int |= DMA_INT_FTF;
    }

    if (int_type & DRV_DMA_INT_HTF)
    {
        dma_int |= DMA_INT_HTF;
    }

    if (int_type & DRV_DMA_INT_ERR)
    {
        dma_int |= DMA_INT_ERR;
    }

    /* 使能 DMA 中断 */
    dma_interrupt_enable(s_dma_ctrl[channel_id].dma_periph,
                         (dma_channel_enum)s_dma_ctrl[channel_id].channel_index,
                         dma_int);

    /* 配置 NVIC 中断优先级 */
    nvic_irq_enable(s_dma_ctrl[channel_id].nvic_irqn, nvic_priority, 0);

    DRV_DMA_LOGD("Channel %d interrupt enabled (type=0x%02X, priority=%d)",
                 channel_id, int_type, nvic_priority);
    return DRV_DMA_ERR_OK;
}

/*********************************************************************
 * @brief   禁能 DMA 中断
 * @param   channel_id DMA 通道 ID
 * @param   int_type 中断类型
 * @return  DRV_DMA_ERR_OK=成功，其他=失败
 *********************************************************************/
int32_t drv_dma_int_disable(drv_dma_channel_id_e channel_id,
                            uint8_t int_type)
{
    uint32_t dma_int = 0;

    /* 参数检查 */
    DMA_CHECK_ID(channel_id);
    DMA_CHECK_INIT(channel_id);

    /* 转换中断类型 */
    if (int_type & DRV_DMA_INT_FTF)
    {
        dma_int |= DMA_INT_FTF;
    }

    if (int_type & DRV_DMA_INT_HTF)
    {
        dma_int |= DMA_INT_HTF;
    }

    if (int_type & DRV_DMA_INT_ERR)
    {
        dma_int |= DMA_INT_ERR;
    }

    /* 禁能 DMA 中断 */
    dma_interrupt_disable(s_dma_ctrl[channel_id].dma_periph,
                          (dma_channel_enum)s_dma_ctrl[channel_id].channel_index,
                          dma_int);

    DRV_DMA_LOGD("Channel %d interrupt disabled (type=0x%02X)", channel_id, int_type);
    return DRV_DMA_ERR_OK;
}

/*********************************************************************
 * @brief   设置传输数量
 * @param   channel_id DMA 通道 ID
 * @param   number 传输数量（1-65535）
 * @return  DRV_DMA_ERR_OK=成功，其他=失败
 *********************************************************************/
int32_t drv_dma_set_transfer_number(drv_dma_channel_id_e channel_id, uint16_t number)
{
    /* 参数检查 */
    DMA_CHECK_ID(channel_id);
    DMA_CHECK_INIT(channel_id);

    if (number == 0)
    {
        DRV_DMA_LOGE("Invalid transfer number: 0");
        return DRV_DMA_ERR_INVALID_PARAM;
    }

    /* 设置传输数量 */
    dma_transfer_number_config(s_dma_ctrl[channel_id].dma_periph,
                               (dma_channel_enum)s_dma_ctrl[channel_id].channel_index,
                               number);

    DRV_DMA_LOGD("Channel %d transfer number set to %u", channel_id, number);
    return DRV_DMA_ERR_OK;
}

/*********************************************************************
 * @brief   获取剩余传输数量
 * @param   channel_id DMA 通道 ID
 * @return  剩余传输数量（0 表示传输完成）
 *********************************************************************/
uint16_t drv_dma_get_transfer_number(drv_dma_channel_id_e channel_id)
{
    /* 参数检查 */
    if (channel_id >= DRV_DMA_MAX)
    {
        DRV_DMA_LOGE("Invalid channel ID: %d", channel_id);
        return 0;
    }

    /* 检查是否已初始化 */
    if (s_dma_ctrl[channel_id].state == DRV_DMA_STATE_IDLE)
    {
        DRV_DMA_LOGE("Channel %d not initialized", channel_id);
        return 0;
    }

    /* 读取剩余传输数量 */
    return (uint16_t)dma_transfer_number_get(s_dma_ctrl[channel_id].dma_periph,
                                              (dma_channel_enum)s_dma_ctrl[channel_id].channel_index);
}

/*********************************************************************
 * @brief   设置内存地址（运行时动态修改）
 * @param   channel_id DMA 通道 ID
 * @param   addr 新内存地址
 * @return  DRV_DMA_ERR_OK=成功，其他=失败
 *********************************************************************/
int32_t drv_dma_set_memory_address(drv_dma_channel_id_e channel_id, uint32_t addr)
{
    /* 参数检查 */
    DMA_CHECK_ID(channel_id);
    DMA_CHECK_INIT(channel_id);

    if (addr == 0)
    {
        DRV_DMA_LOGE("Invalid memory address: 0");
        return DRV_DMA_ERR_INVALID_PARAM;
    }

    /* 设置内存地址 */
    dma_memory_address_config(s_dma_ctrl[channel_id].dma_periph,
                              (dma_channel_enum)s_dma_ctrl[channel_id].channel_index,
                              addr);

    DRV_DMA_LOGD("Channel %d memory address set to 0x%08lX", channel_id, addr);
    return DRV_DMA_ERR_OK;
}

/*********************************************************************
 * @brief   设置外设地址（运行时动态修改）
 * @param   channel_id DMA 通道 ID
 * @param   addr 新外设地址
 * @return  DRV_DMA_ERR_OK=成功，其他=失败
 *********************************************************************/
int32_t drv_dma_set_periph_address(drv_dma_channel_id_e channel_id, uint32_t addr)
{
    /* 参数检查 */
    DMA_CHECK_ID(channel_id);
    DMA_CHECK_INIT(channel_id);

    if (addr == 0)
    {
        DRV_DMA_LOGE("Invalid peripheral address: 0");
        return DRV_DMA_ERR_INVALID_PARAM;
    }

    /* 设置外设地址 */
    dma_periph_address_config(s_dma_ctrl[channel_id].dma_periph,
                              (dma_channel_enum)s_dma_ctrl[channel_id].channel_index,
                              addr);

    DRV_DMA_LOGD("Channel %d peripheral address set to 0x%08lX", channel_id, addr);
    return DRV_DMA_ERR_OK;
}

/*********************************************************************
 * @brief   查询 DMA 通道是否运行中
 * @param   channel_id DMA 通道 ID
 * @return  true=运行中，false=未运行
 *********************************************************************/
bool drv_dma_is_running(drv_dma_channel_id_e channel_id)
{
    /* 参数检查 */
    if (channel_id >= DRV_DMA_MAX)
    {
        DRV_DMA_LOGE("Invalid channel ID: %d", channel_id);
        return false;
    }

    return (s_dma_ctrl[channel_id].state == DRV_DMA_STATE_RUNNING);
}

/*********************************************************************
 * @brief   查询 DMA 通道是否已初始化
 * @param   channel_id DMA 通道 ID
 * @return  true=已初始化，false=未初始化
 *********************************************************************/
bool drv_dma_is_initialized(drv_dma_channel_id_e channel_id)
{
    /* 参数检查 */
    if (channel_id >= DRV_DMA_MAX)
    {
        DRV_DMA_LOGE("Invalid channel ID: %d", channel_id);
        return false;
    }

    return (s_dma_ctrl[channel_id].state != DRV_DMA_STATE_IDLE);
}

/*********************************************************************
 * @brief   运行 DMA 中断回调（由 ISR 调用）
 * @param   channel_id DMA 通道 ID
 * @param   int_flag 中断标志（FTF/HTF/ERR）
 * @note    此函数由 gd32f50x_it.c 中的 ISR 调用
 *********************************************************************/
void drv_dma_run_callback(drv_dma_channel_id_e channel_id, drv_dma_int_type_e int_flag)
{
    /* 参数检查 */
    if (channel_id >= DRV_DMA_MAX)
    {
        return;
    }

    /* 检查是否已初始化 */
    if (s_dma_ctrl[channel_id].state == DRV_DMA_STATE_IDLE)
    {
        return;
    }

    /* 执行回调 */
    switch (int_flag)
    {
        case DRV_DMA_INT_FTF:
            if (s_dma_ctrl[channel_id].ftf_callback != NULL)
            {
                s_dma_ctrl[channel_id].ftf_callback(channel_id);
            }
            break;

        case DRV_DMA_INT_HTF:
            if (s_dma_ctrl[channel_id].htf_callback != NULL)
            {
                s_dma_ctrl[channel_id].htf_callback(channel_id);
            }
            break;

        case DRV_DMA_INT_ERR:
            if (s_dma_ctrl[channel_id].err_callback != NULL)
            {
                s_dma_ctrl[channel_id].err_callback(channel_id);
            }
            break;

        default:
            break;
    }
}

/*********************************************************************
 * @brief   DMA 快速重配置（ISR 安全）
 * @param   channel_id DMA 通道 ID
 * @param   memory_addr 新内存地址
 * @param   transfer_number 新传输数量
 * @return  DRV_DMA_ERR_OK=成功，其他=失败
 * @note    仅更新内存地址和传输数量，无日志输出，适用于 ISR 中的 DMA 缓冲区切换
 *********************************************************************/
int32_t drv_dma_reconfig_fast(drv_dma_channel_id_e channel_id,
                              uint32_t memory_addr,
                              uint16_t transfer_number)
{
    uint32_t dma_periph;
    dma_channel_enum dma_ch;

    /* 最小化参数检查（不使用宏，避免日志输出） */
    if (channel_id >= DRV_DMA_MAX)
    {
        return DRV_DMA_ERR_INVALID_CHANNEL;
    }
    if (s_dma_ctrl[channel_id].state < DRV_DMA_STATE_INITIALIZED || memory_addr == 0 || transfer_number == 0)
    {
        return DRV_DMA_ERR_INVALID_PARAM;
    }

    dma_periph = s_dma_ctrl[channel_id].dma_periph;
    dma_ch = (dma_channel_enum)s_dma_ctrl[channel_id].channel_index;

    /* 停止 DMA 通道 */
    dma_channel_disable(dma_periph, dma_ch);

    /* 直接更新内存地址和传输数量 */
    dma_memory_address_config(dma_periph, dma_ch, memory_addr);
    dma_transfer_number_config(dma_periph, dma_ch, transfer_number);

    /* 重新启动 DMA 通道 */
    dma_channel_enable(dma_periph, dma_ch);

    return DRV_DMA_ERR_OK;
}
