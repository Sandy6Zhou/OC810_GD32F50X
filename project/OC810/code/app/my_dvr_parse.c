/********************************************************************
**版权所有：         深圳市几米物联有限公司
**文件名称：        my_dvr_parse.c
**文件描述：        DVR视频模块通信协议解析层实现
**                  （参考 JT/T 808 风格：0x7E 双标识 + 转义 + CRC16-CCITT）
**当前版本：        V1.0
**作    者：        伍玉蛟 (wuyujiao@jimiiot.com)
**完成日期：        2026.06.15
*********************************************************************
** 功能描述：       1. 流式字节流解析（寻找0x7E边界、边解析边去转义、CRC校验）
**                 2. 请求/响应帧组装（流水号管理、CRC计算、转义处理）
**                 3. 协议解析超时检测（2秒未完成帧解析则触发超时消息）
*********************************************************************/
#include "my_comm.h"

/*********************************************************************
 * 内部宏定义
 *********************************************************************/

/** CRC16-CCITT 多项式 (poly=0x1021, init=0xFFFF, xorout=0x0000) */
#define D_CRC16_CCITT_POLY      (0x1021U)

/** 协议解析状态机阶段 */
typedef enum {
    D_STATE_WAIT_START = 0,       /**< 等待起始 0x7E */
    D_STATE_IN_FRAME,             /**< 在帧内接收数据（流式去转义） */
    D_STATE_ESCAPE_PENDING        /**< 等待转义字节（0x7D 已接收，等待 0x01/0x02） */
} parse_state_e;

/*********************************************************************
 * 内部数据结构定义
 *********************************************************************/

/** 解析状态上下文（文件作用域，联合体在内部，用户不可见） */
typedef struct {
    parse_state_e  state;                       /**< 状态机当前阶段 */
    uint16_t unesc_len;                         /**< 已接收去转义字节数 */

    /** 内部缓冲联合体（复用内存，区分不同阶段用途） */
    union {
        uint8_t raw[D_DVR_FRAME_MAX_RX_LEN];       /**< 原始数据缓冲（seq+cmd+payload+CRC） */
        dvr_frame_t frame;                      /**< 解析后的帧 */
    } frame_buf;
} proto_ctx_t;

/*********************************************************************
 * 内部辅助函数声明
 *********************************************************************/

/*********************************************************************
 * 内部全局变量
 *********************************************************************/

/** 解析上下文 */
static proto_ctx_t s_ctx;

/** 流水号计数器（自动递增，回绕 0xFFFF → 0x0000） */
static uint16_t s_seq_counter = 0;

/*********************************************************************
 * 内部辅助函数实现
 *********************************************************************/

/*********************************************************************
 * @brief   反向转义处理（发送侧，支持原地转义）
 * @param   src      原始数据指针（不含首尾 0x7E）
 * @param   src_len  原始数据长度
 * @param   dst      转义后缓冲区（可与 src 相同，支持原地操作）
 * @param   dst_size 转义缓冲区容量
 * @return  转义后数据长度；0表示缓冲区不足
 * @note    从末尾向前处理，已读字节在低地址、扩展写入在高地址，
 *          因此 src == dst 时不会覆盖未读数据
 *********************************************************************/
static uint16_t my_dvr_parse_escape(const uint8_t *src, uint16_t src_len,
                              uint8_t *dst, uint16_t dst_size)
{
    uint16_t esc_count = 0;
    uint16_t dst_len;
    uint16_t read_pos;
    uint16_t write_pos;
    uint16_t i;

    if (src == NULL || dst == NULL)
    {
        return 0;
    }

    /* 第一遍：统计需要转义的字节数 */
    for (i = 0; i < src_len; i++)
    {
        if (src[i] == D_DVR_FRAME_FLAG || src[i] == D_DVR_ESCAPE_CHAR)
        {
            esc_count++;
        }
    }

    dst_len = src_len + esc_count;
    if (dst_len > dst_size)
    {
        return 0;
    }

    /* 第二遍：从末尾向前处理（支持 src/dst 重叠的原地转义） */
    read_pos = src_len;
    write_pos = dst_len;

    while (read_pos > 0)
    {
        read_pos--;
        if (src[read_pos] == D_DVR_FRAME_FLAG)
        {
            dst[--write_pos] = D_DVR_ESCAPE_FLAG_BYTE;
            dst[--write_pos] = D_DVR_ESCAPE_CHAR;
        }
        else if (src[read_pos] == D_DVR_ESCAPE_CHAR)
        {
            dst[--write_pos] = D_DVR_ESCAPE_ESCAPE_BYTE;
            dst[--write_pos] = D_DVR_ESCAPE_CHAR;
        }
        else
        {
            dst[--write_pos] = src[read_pos];
        }
    }

    return dst_len;
}

/*********************************************************************
 *  协议API实现（按执行流从下往上排列）
 *********************************************************************/

/*********************************************************************
 * @brief   组装协议帧（通用，手动指定流水号）
 * @param   seq         流水号（调用方指定，不自动递增）
 * @param   cmd         命令码
 * @param   payload     负载数据（可为NULL）
 * @param   payload_len 负载长度
 * @param   out_buf     输出缓冲区（容纳转义后完整帧）
 * @param   buf_size    输出缓冲区容量
 * @return  实际写入字节数（含转义、标识位、CRC）；0表示失败
 * @note    适用于响应帧组装（需保持请求流水号一致）
 *          调用方负责提供缓冲区（栈/堆/static 由调用方决定）
 *          自动计算CRC16-CCITT并填充
 *          自动执行转义处理并添加首尾0x7E标识
 *********************************************************************/
uint16_t my_dvr_parse_build_response(uint16_t seq,
                                      uint16_t cmd,
                                      const uint8_t *payload,
                                      uint16_t payload_len,
                                      uint8_t *out_buf,
                                      uint16_t buf_size)
{
    uint16_t crc;
    uint16_t body_len = 0;
    uint16_t esc_len;
    uint8_t hdr_buf[D_DVR_FRAME_HDR_LEN];  /* seq(2) + cmd(2) 帧头缓存 */

    if (out_buf == NULL)
    {
        return 0;
    }

    if (payload == NULL)
    {
        payload_len = 0;
    }

    /* 1. 构造 body 到 out_buf 用于 CRC 计算：seq(2)+cmd(2)+payload(N) */
    my_tool_u16_to_be16(seq, &out_buf[body_len]); body_len += 2;
    my_tool_u16_to_be16(cmd, &out_buf[body_len]); body_len += 2;

    if (payload_len > 0)
    {
        (void)memcpy(&out_buf[body_len], payload, payload_len);
        body_len += payload_len;
    }

    /* 2. 计算 CRC16-CCITT */
    crc = my_tool_crc16_bitwise(out_buf, body_len, D_CRC16_CCITT_POLY);

    /* 3. 备份帧头（后续写 FLAG 会覆盖 out_buf[0]） */
    (void)memcpy(hdr_buf, out_buf, D_DVR_FRAME_HDR_LEN);

    /* 4. 从头构造转义帧：FLAG + 帧头 + payload + CRC + FLAG */
    body_len = 0;
    out_buf[body_len++] = D_DVR_FRAME_FLAG;

    /* 5. 帧头转义写入 */
    esc_len = my_dvr_parse_escape(hdr_buf, D_DVR_FRAME_HDR_LEN, &out_buf[body_len], buf_size - body_len);
    if (esc_len == 0)
    {
        return 0;
    }
    body_len += esc_len;

    /* 6. payload 转义写入 */
    if (payload_len > 0)
    {
        esc_len = my_dvr_parse_escape(payload, payload_len, &out_buf[body_len], buf_size - body_len);
        if (esc_len == 0)
        {
            return 0;
        }
        body_len += esc_len;
    }

    /* 7. CRC 转义写入 */
    my_tool_u16_to_be16(crc, hdr_buf);
    esc_len = my_dvr_parse_escape(hdr_buf, 2, &out_buf[body_len], buf_size - body_len);
    if (esc_len == 0)
    {
        return 0;
    }
    body_len += esc_len;

    /* 8. 写尾标识 */
    out_buf[body_len++] = D_DVR_FRAME_FLAG;

    MY_LOG_DUMP("TX frame", out_buf, body_len);

    return body_len;
}

/*********************************************************************
 *  协议API实现（按执行流从下往上排列）
 *********************************************************************/

/*********************************************************************
 * @brief   组装请求帧（单片机 → 视频模块）
 * @param   cmd         命令码
 * @param   payload     负载数据（可为NULL）
 * @param   payload_len 负载长度
 * @param   out_buf     输出缓冲区（容纳转义后完整帧）
 * @param   buf_size    输出缓冲区容量
 * @return  实际写入字节数（含转义、标识位、CRC）；0表示失败
 * @note    自动递增内部流水号计数器
 *          调用方负责提供缓冲区（栈/堆/static 由调用方决定）
 *          自动计算CRC16-CCITT并填充
 *          自动执行转义处理并添加首尾0x7E标识
 *********************************************************************/
uint16_t my_dvr_parse_build_request(uint16_t cmd,
                                        const uint8_t *payload,
                                        uint16_t payload_len,
                                        uint8_t *out_buf,
                                        uint16_t buf_size)
{
    uint16_t ret;
    uint16_t seq = s_seq_counter;

    ret = my_dvr_parse_build_response(seq, cmd, payload, payload_len, out_buf, buf_size);
    if (ret > 0)
    {
        /* 发送成功后流水号递增（回绕 0xFFFF → 0x0000） */
        s_seq_counter = (uint16_t)(s_seq_counter + 1U);
    }

    return ret;
}

/*********************************************************************
 * @brief   解析超时回调（定时器上下文）
 * @param   timer_handle  定时器句柄
 * @return  none
 * @note    在定时器任务中执行，发送超时消息到任务队列
 *********************************************************************/
static void my_dvr_parse_timeout_cb(my_timer_handle_t timer_handle)
{
    (void)timer_handle;

    my_msg_t msg = {
        .id = MY_MSG_ID_DVR_PARSE_TIMEOUT,
        .data = NULL,
        .len = 0
    };

    my_msg_send(MSG_QUEUE_DVR, &msg, 0);
}

/*********************************************************************
 * @brief   解析字节流中的协议帧
 * @param   buf       输入缓冲区指针（来自RingBuffer的原始字节）
 * @param   len       输入缓冲区长度
 * @param   consumed  输出：本次已消费的字节数（调用方据此推进RingBuffer读指针）
 * @param   out_frame 输出：解析成功的帧结构（仅当返回 PARSE_OK 时有效）
 * @return  dvr_parse_status_e
 *          - INCOMPLETE: 未检测到完整帧，应等待后续数据
 *          - OK:         成功解析一帧
 *          - ERR_*:      解析错误，调用方已跳过 consumed 字节
 * @note    内部处理：寻找首0x7E → 读至尾0x7E → 转义还原 → CRC校验 → 字段提取
 *          支持跨多次调用拼接一帧（有限状态机）
 *********************************************************************/
dvr_parse_status_e my_dvr_parse_frame(const uint8_t *buf,
                                             uint16_t len,
                                             uint16_t *consumed,
                                             dvr_frame_t *out_frame)
{
    uint16_t i;
    uint8_t byte;
    uint16_t calc_crc, recv_crc;
    uint16_t payload_len = 0;
    dvr_parse_status_e status = DVR_PARSE_INCOMPLETE;

    if (buf == NULL || out_frame == NULL || consumed == NULL || len == 0)
    {
        return DVR_PARSE_INCOMPLETE;
    }

    *consumed = 0;

    for (i = 0; i < len; i++)
    {
        byte = buf[i];
        (*consumed)++;

        switch (s_ctx.state)
        {
            case D_STATE_WAIT_START:
                if (byte == D_DVR_FRAME_FLAG)
                {
                    s_ctx.unesc_len = 0;
                    s_ctx.state = D_STATE_IN_FRAME;
                    /* 检测到帧头，启动超时检测 */
                    my_timer_start(MY_TIMER_ID_DVR_PARSE_TIMEOUT, 0);
                }
                break;

            case D_STATE_ESCAPE_PENDING:
                /* 处理转义序列：0x7D 后必须跟 0x01 或 0x02 */
                if (byte == D_DVR_ESCAPE_FLAG_BYTE)
                {
                    /* 0x7D 0x02 → 还原为 0x7E */
                    if (s_ctx.unesc_len >= D_DVR_FRAME_MAX_RX_LEN)
                    {
                        my_timer_stop(MY_TIMER_ID_DVR_PARSE_TIMEOUT);
                        s_ctx.state = D_STATE_WAIT_START;
                        return DVR_PARSE_ERR_TOO_LONG;
                    }
                    s_ctx.frame_buf.raw[s_ctx.unesc_len++] = D_DVR_FRAME_FLAG;
                }
                else if (byte == D_DVR_ESCAPE_ESCAPE_BYTE)
                {
                    /* 0x7D 0x01 → 还原为 0x7D */
                    if (s_ctx.unesc_len >= D_DVR_FRAME_MAX_RX_LEN)
                    {
                        my_timer_stop(MY_TIMER_ID_DVR_PARSE_TIMEOUT);
                        s_ctx.state = D_STATE_WAIT_START;
                        return DVR_PARSE_ERR_TOO_LONG;
                    }
                    s_ctx.frame_buf.raw[s_ctx.unesc_len++] = D_DVR_ESCAPE_CHAR;
                }
                else
                {
                    /* 非法转义序列 */
                    my_timer_stop(MY_TIMER_ID_DVR_PARSE_TIMEOUT);
                    s_ctx.state = D_STATE_WAIT_START;
                    return DVR_PARSE_ERR_ESCAPE;
                }
                s_ctx.state = D_STATE_IN_FRAME;
                break;

            case D_STATE_IN_FRAME:
                if (byte == D_DVR_ESCAPE_CHAR)
                {
                    /* 检测到转义引导字节，进入等待状态 */
                    s_ctx.state = D_STATE_ESCAPE_PENDING;
                }
                else if (byte == D_DVR_FRAME_FLAG)
                {
                    /* 检测到结束标识，解析完整帧 */
                    /* 最小帧长：seq(2)+cmd(2)+crc(2) = 6 */
                    if (s_ctx.unesc_len < D_DVR_FRAME_MIN_LEN)
                    {
                        my_timer_stop(MY_TIMER_ID_DVR_PARSE_TIMEOUT);
                        s_ctx.state = D_STATE_WAIT_START;
                        return DVR_PARSE_ERR_TOO_SHORT;
                    }

                    /* CRC 校验（CRC范围：seq + cmd + payload） */
                    calc_crc = my_tool_crc16_bitwise(s_ctx.frame_buf.raw, s_ctx.unesc_len - 2, D_CRC16_CCITT_POLY);
                    recv_crc = my_tool_be16_to_u16(&s_ctx.frame_buf.raw[s_ctx.unesc_len - 2]);
                    if (calc_crc != recv_crc)
                    {
                        my_timer_stop(MY_TIMER_ID_DVR_PARSE_TIMEOUT);
                        s_ctx.state = D_STATE_WAIT_START;
                        return DVR_PARSE_ERR_CRC;
                    }

                    /* 提取字段到 frame */
                    out_frame->seq = my_tool_be16_to_u16(&s_ctx.frame_buf.raw[0]);
                    out_frame->cmd = my_tool_be16_to_u16(&s_ctx.frame_buf.raw[2]);

                    /* !!!注意 --- payload 长度处理，这里容易出错，务必小心!!!
                       因为payload_len并不在协议帧中，所以在这里不能直接写入 payload_len，会污染 raw 数据，导致后续解析错误
                       这里先用 payload_len临时保存，待所有数据处理完毕后再写入 */
                    payload_len = s_ctx.unesc_len - D_DVR_FRAME_MIN_LEN;

                    /* 检查 payload 长度是否超出限制 */
                    if (payload_len > D_DVR_FRAME_MAX_PAYLOAD)
                    {
                        my_timer_stop(MY_TIMER_ID_DVR_PARSE_TIMEOUT);
                        s_ctx.state = D_STATE_WAIT_START;
                        return DVR_PARSE_ERR_TOO_LONG;
                    }

                    if (payload_len > 0)
                    {
                        /* 将真正的payload数据从 frame_buf.raw[HDR] 拷贝到 out_frame->payload */
                        (void)memmove(out_frame->payload,
                                        &s_ctx.frame_buf.raw[D_DVR_FRAME_HDR_LEN],
                                        payload_len);
                    }

                    /* 数据全部处理完成，在这里可以安全的写入payload_len */
                    out_frame->payload_len = payload_len;

                    /* 解析成功，停止超时检测 */
                    my_timer_stop(MY_TIMER_ID_DVR_PARSE_TIMEOUT);
                    s_ctx.state = D_STATE_WAIT_START;
                    return DVR_PARSE_OK;
                }
                else
                {
                    /* 普通字节（包括0x7E帧头帧尾、0x7D转义后的正常字节） */
                    if (s_ctx.unesc_len >= D_DVR_FRAME_MAX_RX_LEN)
                    {
                        my_timer_stop(MY_TIMER_ID_DVR_PARSE_TIMEOUT);
                        s_ctx.state = D_STATE_WAIT_START;
                        return DVR_PARSE_ERR_TOO_LONG;
                    }
                    s_ctx.frame_buf.raw[s_ctx.unesc_len++] = byte;
                }
                break;

            default:
                s_ctx.state = D_STATE_WAIT_START;
                break;
        }
    }

    return status;
}

/*********************************************************************
 * @brief   接收数据处理入口（协议解析 + 错误处理）
 * @param   data  数据缓冲区
 * @param   len   数据长度
 * @return  none
 * @note    封装完整的协议解析流程，供主模块调用
 *********************************************************************/
void my_dvr_parse_process(uint8_t const *data, uint16_t len)
{
    uint16_t consumed = 0;
    uint16_t remaining = len;
    dvr_parse_status_e status;

    if ((data == NULL) || (len == 0))
    {
        return;
    }

    /* 循环处理直到所有数据都被消费 */
    while (remaining > 0)
    {
        /* 调用协议解析（使用静态 frame_buf.frame，减少栈占用） */
        status = my_dvr_parse_frame(data, remaining, &consumed, &s_ctx.frame_buf.frame);

        switch (status)
        {
            case DVR_PARSE_OK:
                /* 解析成功，分发给命令处理层 */
                MY_LOG_D("Frame: seq=%u, cmd=0x%04X, payload_len=%u",
                         s_ctx.frame_buf.frame.seq,
                         s_ctx.frame_buf.frame.cmd,
                         s_ctx.frame_buf.frame.payload_len);
                MY_LOG_DUMP("payload:", s_ctx.frame_buf.frame.payload, s_ctx.frame_buf.frame.payload_len);

                my_dvr_cmd_dispatch(&s_ctx.frame_buf.frame);
                break;

            case DVR_PARSE_INCOMPLETE:
                /* 数据不完整，等待更多数据（本次已消费的字节会保留在状态机中） */
                MY_LOG_D("Proto: incomplete, consumed %u/%u bytes", consumed, len);
                return;  /* 退出循环，等待更多数据 */

            case DVR_PARSE_ERR_TOO_SHORT:
                MY_LOG_W("Proto: frame too short (%u bytes)", consumed);
                break;  /* 继续循环，处理剩余数据 */

            case DVR_PARSE_ERR_TOO_LONG:
                MY_LOG_W("Proto: frame too long (%u bytes)", consumed);
                break;  /* 继续循环，处理剩余数据 */

            case DVR_PARSE_ERR_ESCAPE:
                MY_LOG_W("Proto: escape error");
                break;  /* 继续循环，处理剩余数据 */

            case DVR_PARSE_ERR_CRC:
                MY_LOG_W("Proto: CRC mismatch");
                break;  /* 继续循环，处理剩余数据 */

            default:
                MY_LOG_W("Proto: unknown status %d", status);
                break;  /* 继续循环，处理剩余数据 */
        }

        /* 移动指针到未处理的数据 */
        data += consumed;
        remaining -= consumed;
    }
}

/*********************************************************************
 * @brief   复位协议解析状态机
 * @return  none
 * @note    任务初始化时调用；遇到严重错误时也可调用强制重置
 *********************************************************************/
void my_dvr_parse_reset(void)
{
    my_timer_stop(MY_TIMER_ID_DVR_PARSE_TIMEOUT);
    (void)memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.state = D_STATE_WAIT_START;
}

/*********************************************************************
 * @brief   协议解析初始化
 * @return  none
 * @note    主模块初始化时调用，创建内部定时器并复位解析状态机
 *********************************************************************/
void my_dvr_parse_init(void)
{
    int ret;

    /* 创建并启动1分钟定时器 */
    if (my_timer_create(MY_TIMER_ID_DVR_PARSE_TIMEOUT, my_dvr_parse_timeout_cb, 2000))
    {
        MY_LOG_E("my_dvr_parse_timeout_cb create failed");
    }

    /* 复位解析状态机 */
    my_dvr_parse_reset();
}