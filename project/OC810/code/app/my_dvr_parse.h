/********************************************************************
**版权所有：         深圳市几米物联有限公司
**文件名称：        my_dvr_parse.h
**文件描述：        DVR视频模块通信协议解析层接口定义
**                  （参考 JT/T 808 风格：0x7E 双标识 + 转义 + CRC16-CCITT）
**当前版本：        V1.0
**作    者：        伍玉蛟 (wuyujiao@jimiiot.com)
**完成日期：        2026.06.15
*********************************************************************
** 功能描述：       1. 协议帧格式定义（标识/流水号/命令/数据/CRC/标识）
**                 2. 字节流转义与还原（0x7E ↔ 0x7D 0x02, 0x7D ↔ 0x7D 0x01）
**                 3. CRC16-CCITT 校验
**                 4. 帧解析与帧组装
**                 5. 纯协议层：不依赖FreeRTOS，不调用硬件驱动
*********************************************************************/

#ifndef __MY_DVR_PARSE_H__
#define __MY_DVR_PARSE_H__

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 *  协议常量定义
 *
 *  ┌─────────────────────────────────────────────────────────────────┐
 *  │ 原始协议帧（传输格式，含首尾标识符和转义）                       │
 *  └─────────────────────────────────────────────────────────────────┘
 *  +------+-------------------------------------+------+-----+-----+
 *  | FLAG | seq                                 | cmd  | CRC | FLAG|
 *  | 1B   | 2B                                  | 2B   | 2B  | 1B  |
 *  +------+-------------------------------------+------+-----+-----+
 *  | 0x7E | 转义后的帧数据     | (可能含转义序列)|      |     | 0x7E|
 *  +------+-------------------------------------+------+-----+-----+
 *
 *  转义规则（传输前对数据中的特殊字符进行转义）：
 *  - 0x7E → 0x7D 0x02  （帧标识符转义）
 *  - 0x7D → 0x7D 0x01  （转义字符自身转义）
 *
 *  ┌─────────────────────────────────────────────────────────────────┐
 *  │ 去转义后帧（内部处理格式，不含首尾0x7E标识符）                   │
 *  └─────────────────────────────────────────────────────────────────┘
 *  +--------------------+----------+---------+-------------+
 *  | seq                | cmd      | payload | CRC         |
 *  | 2字节              | 2字节    | 0~1024B | 2字节       |
 *  +---------------------+----------+---------+-------------+
 *  | 流水号               | 命令码   | 负载数据| CRC16-CCITT |
 *  +---------------------+----------+---------+-------------+
 *
 *  CRC计算范围：seq + cmd + payload（不含首尾0x7E和CRC本身）
 *  最小帧长度：6字节（无payload）
 *  最大帧长度：1030字节（payload为1024字节）
 *===========================================================================*/

/** 帧标识位（首尾各1字节） */
#define D_DVR_FRAME_FLAG                    (0x7EU)

/** 转义引导字节 */
#define D_DVR_ESCAPE_CHAR                   (0x7DU)

/** 转义填充码：0x7E → 0x7D 0x02 */
#define D_DVR_ESCAPE_FLAG_BYTE              (0x02U)

/** 转义填充码：0x7D → 0x7D 0x01 */
#define D_DVR_ESCAPE_ESCAPE_BYTE            (0x01U)

/** 帧头长度（字节：seq2 + cmd2，不含payload和CRC） */
#define D_DVR_FRAME_HDR_LEN                 (4U)

/** 最小帧数据长度（去转义后，不含首尾0x7E标识符：帧头 + crc2） */
#define D_DVR_FRAME_MIN_LEN                 (D_DVR_FRAME_HDR_LEN + 2U)

/** 最大负载长度（字节，payload纯数据部分，不含seq/cmd/crc） */
#define D_DVR_FRAME_MAX_PAYLOAD             (1024U)

/** 最大接收帧长度（去转义后，不含首尾0x7E标识符：payload + seq + cmd + crc）
 *  用于接收端缓冲区分配和长度校验 */
#define D_DVR_FRAME_MAX_RX_LEN              (D_DVR_FRAME_MAX_PAYLOAD + D_DVR_FRAME_MIN_LEN)

/** 小数据量发送负载长度（字节，单片机发送端限制）
 *  适用场景：心跳、版本响应等常规命令，使用栈缓冲区 */
#define D_DVR_FRAME_MAX_TX_SMALL_PAYLOAD    (256U)

/** 小数据量发送最大发送帧长度（去转义后，不含首尾0x7E标识符） */
#define D_DVR_FRAME_MAX_TX_SMALL_LEN        (D_DVR_FRAME_MAX_TX_SMALL_PAYLOAD + D_DVR_FRAME_MIN_LEN)

/** 小数据量发送转义缓冲区大小（栈分配，适用于≤256字节payload）
 *  计算公式：(帧头4 + payload256 + CRC2) × 2(最坏转义) + 2(首尾标识) = 526字节
 *  使用场景：心跳、版本响应等常规命令，直接使用栈缓冲区 */
#define D_DVR_TX_ESCAPE_SMALL_BUF_SIZE      (D_DVR_FRAME_MAX_TX_SMALL_LEN * 2U + 2U)

/** 大数据量发送负载长度（字节，视频模块→单片机方向）
 *  适用场景：OTA状态、日志上报等大数据，需动态分配内存 */
#define D_DVR_FRAME_MAX_TX_LARGE_PAYLOAD    (D_DVR_FRAME_MAX_PAYLOAD)

/** 大数据量发送最大发送帧长度（去转义后，不含首尾0x7E标识符） */
#define D_DVR_FRAME_MAX_TX_LARGE_LEN        (D_DVR_FRAME_MAX_TX_LARGE_PAYLOAD + D_DVR_FRAME_MIN_LEN)

/** 大数据量发送转义缓冲区大小（堆分配，适用于>256字节payload）
 *  计算公式：(帧头4 + payload1024 + CRC2) × 2(最坏转义) + 2(首尾标识) = 2062字节
 *  使用场景：视频模块主动上报大数据（OTA状态、日志等），需动态分配内存 */
#define D_DVR_TX_ESCAPE_LARGE_BUF_SIZE      (D_DVR_FRAME_MAX_TX_LARGE_LEN * 2U + 2U)

/** 命令响应标志位（命令最高位 0x8000） */
#define D_DVR_CMD_RESPONSE_FLAG      (0x8000U)

/*===========================================================================
 *  命令码定义（详见《mDVR-OC810-视频模块通讯协议》第7章）
 *===========================================================================*/

/** MCU->DVR 命令  */
#define D_MCU_CMD_COMMON                (0x2001U)    /**< MCU通用命令 */
#define D_MCU_CMD_HEARTBEAT             (0x2002U)    /**< MCU心跳包 */
#define D_MCU_CMD_VERSION_RESPONSE      (0x2003U)    /**< MCU版本查询响应 */

/** DVR->MCU 命令  */
#define D_DVR_CMD_COMMON                (0xA001U)    /**< DVR通用命令 */
#define D_DVR_CMD_HEARTBEAT             (0xA002U)    /**< DVR心跳包 */
#define D_DVR_CMD_VERSION_QUERY         (0xA003U)    /**< DVR版本查询命令 */

/*===========================================================================
 *  协议数据结构定义
 *===========================================================================*/

/** 协议解析状态 */
typedef enum {
    DVR_PARSE_INCOMPLETE = 0,    /**< 数据不足，继续等待下一个 0x7E */
    DVR_PARSE_OK,                /**< 成功解析一帧 */
    DVR_PARSE_ERR_TOO_SHORT,     /**< 帧过短（< 最小帧长） */
    DVR_PARSE_ERR_TOO_LONG,      /**< 数据超限（>= 1024） */
    DVR_PARSE_ERR_ESCAPE,        /**< 转义序列错误 */
    DVR_PARSE_ERR_CRC,           /**< CRC 校验失败 */
} dvr_parse_status_e;

/** 解析后的帧结构（协议层输出，纯净无联合体） */
typedef struct {
    uint16_t seq;                   /**< 流水号（主机字节序） */
    uint16_t cmd;                   /**< 命令码（主机字节序） */
    uint16_t payload_len;           /**< 负载长度 */
    uint8_t  payload[D_DVR_FRAME_MAX_PAYLOAD]; /**< 负载数据（真正的payload，不含帧头/CRC） */
} dvr_frame_t;

/*===========================================================================
 *  API接口（按执行流从上往下排列）
 *==========================================================================*/

/*********************************************************************
 * @brief   协议解析层初始化
 * @return  none
 * @note    主模块初始化时调用，创建内部定时器
 *********************************************************************/
void my_dvr_parse_init(void);

/*********************************************************************
 * @brief   复位协议解析状态机
 * @return  none
 * @note    任务初始化时调用；遇到严重错误时也可调用强制重置
 *********************************************************************/
void my_dvr_parse_reset(void);

/*********************************************************************
 * @brief   接收数据处理入口（协议解析 + 错误处理）
 * @param   data  数据缓冲区
 * @param   len   数据长度
 * @return  none
 * @note    封装完整的协议解析流程，供主模块调用
 *********************************************************************/
void my_dvr_parse_process(uint8_t const *data, uint16_t len);

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
                                             dvr_frame_t *out_frame);

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
                                        uint16_t buf_size);

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
                                      uint16_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* __MY_DVR_PARSE_H__ */
