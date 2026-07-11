/*********************************************************************
** 版权所有：       深圳市几米物联有限公司
** 文件名称：       my_dvr_cmd.c
** 文件描述：       DVR视频模块命令分发层实现
**                  （静态命令表 + dispatch 分发机制）
** 当前版本：       V1.0
** 作    者：       伍玉蛟 (wuyujiao@jimiiot.com)
** 完成日期：       2026.06.16
*********************************************************************
** 功能描述：       1. 静态命令表定义（cmd → handler 映射）
**                 2. 命令分发逻辑（查表 + 调用 handler）
**                 3. 响应/请求帧组装与发送（含小数据栈分配/大数据堆分配策略）
*********************************************************************/

#include "my_comm.h"

/*********************************************************************
 * 内部宏定义
 *********************************************************************/

/** 命令表数组元素个数计算宏 */
#define D_CMD_TABLE_SIZE(tbl)   (sizeof(tbl) / sizeof((tbl)[0]))

/** 发送缓冲区大小（与 my_dvr_parse.h 中小数据量缓冲区保持一致） */
#define D_CMD_TX_BUF_SIZE       (D_DVR_TX_ESCAPE_SMALL_BUF_SIZE)

/*********************************************************************
 * 内部全局变量
 *********************************************************************/
/** 小数据量发送缓冲区（栈分配，适用于≤256字节payload的常规命令）
 *  大数据量（>256字节）使用动态内存分配，不依赖此缓冲区 */
static uint8_t s_cmd_tx_buf[D_DVR_TX_ESCAPE_SMALL_BUF_SIZE];

/*********************************************************************
 * 静态函数定义（被调用者在上，调用者在下）
 *********************************************************************/

 /*********************************************************************
 * @brief   处理版本查询命令（DVR → MCU）
 * @param   frame  解析后的协议帧
 * @return  none
 * @note    请求命令：D_DVR_CMD_VERSION_QUERY = 0xA003
 *          应答命令：D_MCU_CMD_VERSION_RESPONSE = 0x2003
 *          流水号：直接用 DVR 的流水号
 *
 *          【请求 Payload】（DVR→MCU）
 *          无 payload（payload_len = 0）
 *
 *          【应答 Payload】（MCU→DVR，80字节）
 *          偏移  类型        字段          说明
 *          0     BYTE[40]   sw_version    软件版本ASCII码，不满40字节时自动补'\0'
 *          40    BYTE[40]   hw_version    硬件版本ASCII码，不满40字节时自动补'\0'
 *********************************************************************/
static void my_dvr_cmd_handler_version_query(const dvr_frame_t *frame)
{
    uint16_t payload_len = 0;
    uint8_t payload[80] = { 0 };
    size_t len;

    MY_LOG_I("CMD[%04X]: Version Query (seq=%u)", frame->cmd, frame->seq);

    /* 安全拷贝版本字符串（限制最大40字节） */
    len = sizeof(MY_SW_VERSION_STRING);
    if (len > 40)
    {
        len = 40;
    }

    (void)memcpy(payload, MY_SW_VERSION_STRING, len);
    payload_len += 40;

    len = sizeof(MY_HW_VERSION_STRING);
    if (len > 40)
    {
        len = 40;
    }

    (void)memcpy(&payload[40], MY_HW_VERSION_STRING, len);
    payload_len += 40;

    my_dvr_cmd_send_response(frame->seq, D_MCU_CMD_VERSION_RESPONSE, payload, payload_len);

    /* DVR已通讯，启动心跳发送 */
    my_dvr_heartbeat_on_contact();
}

/*********************************************************************
 * @brief   处理心跳包命令（DVR → MCU）
 * @param   frame  解析后的协议帧
 * @return  none
 * @note    接收命令：D_DVR_CMD_HEARTBEAT = 0xA002
 *
 *          【接收 Payload】（DVR→MCU，10字节）
 *          偏移  类型        字段          说明
 *          0     WORD(BE)    heart_cnt     DVR累计收到MCU心跳的次数
 *          2     BYTE[8]     status        DVR自身状态（位域定义见DVR侧）
 *          发送侧Payload定义见 my_dvr_heartbeat_send()
 *********************************************************************/
static void my_dvr_cmd_handler_heartbeat(const dvr_frame_t *frame)
{
    uint16_t mcu_hb_cnt = 0;

    /* 确认DVR通讯已建立，启动心跳发送 */
    my_dvr_heartbeat_on_contact();

    /* 解析DVR心跳 payload: heart_cnt(2B) + status(8B) */
    /* 注：无 payload 时 payload[0-1] = 0，heart_cnt = 0，调用也无副作用 */
    mcu_hb_cnt = my_tool_be16_to_u16(frame->payload);
    my_dvr_heartbeat_on_rcv(mcu_hb_cnt);
}

/*********************************************************************
 * @brief   处理通用命令（DVR → MCU）
 * @param   frame  解析后的协议帧
 * @return  none
 * @note    接收命令：D_DVR_CMD_COMMON = 0xA001
 *          发送命令：D_MCU_CMD_COMMON = 0x2001
 *
 *          【Payload】
 *          待定（预留通用数据通道）
 *********************************************************************/
static void my_dvr_cmd_handler_common(const dvr_frame_t *frame)
{
    MY_LOG_I("CMD: Common (seq=%u)", frame->seq);
}

/*********************************************************************
 * DVR 接收命令映射表（DVR → MCU，单片机解析侧）
 * 用途：my_dvr_cmd_dispatch() 查表分发
 *********************************************************************/
static const dvr_cmd_entry_t s_dvr_rx_cmd_table[] = {
    { D_DVR_CMD_COMMON,          my_dvr_cmd_handler_common },
    { D_DVR_CMD_HEARTBEAT,       my_dvr_cmd_handler_heartbeat },
    { D_DVR_CMD_VERSION_QUERY,   my_dvr_cmd_handler_version_query },
};

/*********************************************************************
 * 公共API实现
 *********************************************************************/

/*********************************************************************
 * @brief   组装并发送响应帧
 * @param   seq          流水号（必须与请求一致）
 * @param   cmd          响应命令码（已含 0x8000 标志位）
 * @param   payload      负载数据（可为NULL）
 * @param   payload_len  负载长度
 * @return  none
 * @note    组装帧后调用 my_dvr_send 发送
 *          内部使用统一的内存策略（小数据静态缓冲，大数据动态分配）
 *********************************************************************/
void my_dvr_cmd_send_response(uint16_t seq, uint16_t cmd, const uint8_t *payload, uint16_t payload_len)
{
    uint16_t frame_len;
    uint16_t max_frame_len = sizeof(s_cmd_tx_buf);  /* 默认：小数据使用静态缓冲区 */
    uint8_t *tx_buf = s_cmd_tx_buf;
    bool    dynamic_alloc = false;

    if (payload == NULL)
    {
        payload_len = 0;
    }

    /* 小数据：使用静态缓冲区, 大数据：使用动态内存 */
    if (payload_len > D_DVR_FRAME_MAX_TX_SMALL_PAYLOAD)
    {
        /* 大数据：使用动态内存 */
        max_frame_len = (uint16_t)((payload_len + D_DVR_FRAME_MIN_LEN) * 2U + 2U);

        MY_SAFE_MALLOC(tx_buf, max_frame_len);
        if (tx_buf == NULL)
        {
            return;
        }
        dynamic_alloc = true;
    }

    /* 组装帧（流水号由调用方指定，不自动递增） */
    frame_len = my_dvr_parse_build_response(seq, cmd, payload, payload_len, tx_buf, max_frame_len);
    if (frame_len > 0)
    {
        if (my_dvr_send(tx_buf, frame_len))
        {
            MY_LOG_W("CMD: Send response failed (seq=%u, cmd=0x%04X)", seq, cmd);
        }
    }
    else
    {
        MY_LOG_W("CMD: Build response failed (seq=%u, cmd=0x%04X)", seq, cmd);
    }

    /* 释放动态内存（静态缓冲区无需释放） */
    if (dynamic_alloc)
    {
        MY_SAFE_FREE(tx_buf);
    }
}

/*********************************************************************
 * @brief   主动发送请求帧
 * @param   cmd          请求命令码
 * @param   payload      负载数据（可为NULL）
 * @param   payload_len  负载长度
 * @return  none
 * @note    主动发起请求（流水号自动递增）
 *********************************************************************/
void my_dvr_cmd_send_request(uint16_t cmd, const uint8_t *payload, uint16_t payload_len)
{
    uint16_t frame_len;
    uint16_t max_frame_len = sizeof(s_cmd_tx_buf);  /* 默认：小数据使用静态缓冲区 */
    uint8_t *tx_buf = s_cmd_tx_buf;
    bool    dynamic_alloc = false;

    if (payload == NULL)
    {
        payload_len = 0;
    }

    /* 小数据：使用静态缓冲区, 大数据：使用动态内存 */
    if (payload_len > D_DVR_FRAME_MAX_TX_SMALL_PAYLOAD)
    {
        /* 大数据：使用动态内存 */
        max_frame_len = (uint16_t)((payload_len + D_DVR_FRAME_MIN_LEN) * 2U + 2U);

        MY_SAFE_MALLOC(tx_buf, max_frame_len);
        if (tx_buf == NULL)
        {
            return;
        }
        dynamic_alloc = true;
    }

    /* 组装帧（流水号由 build_request 自动递增） */
    frame_len = my_dvr_parse_build_request(cmd, payload, payload_len, tx_buf, max_frame_len);
    if (frame_len > 0)
    {
        if (my_dvr_send(tx_buf, frame_len))
        {
            MY_LOG_W("CMD: Send request failed (cmd=0x%04X)", cmd);
        }
    }
    else
    {
        MY_LOG_W("CMD: Build request failed (cmd=0x%04X)", cmd);
    }

    /* 释放动态内存（静态缓冲区无需释放） */
    if (dynamic_alloc)
    {
        MY_SAFE_FREE(tx_buf);
    }
}

/*********************************************************************
 * @brief   命令分发入口
 * @param   frame  解析后的协议帧
 * @return  none
 *********************************************************************/
void my_dvr_cmd_dispatch(const dvr_frame_t *frame)
{
    uint16_t i;

    if (frame == NULL)
    {
        return;
    }

    /* 1. 查静态表 */
    for (i = 0; i < D_CMD_TABLE_SIZE(s_dvr_rx_cmd_table); i++)
    {
        if (s_dvr_rx_cmd_table[i].cmd == frame->cmd)
        {
            MY_LOG_D("CMD: Dispatch 0x%04X (static)", frame->cmd);
            s_dvr_rx_cmd_table[i].handler(frame);
            return;
        }
    }

    /* 2. 未定义命令 */
    MY_LOG_W("CMD: Unknown 0x%04X seq=%u", frame->cmd, frame->seq);
}
