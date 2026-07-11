/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       my_comm.h
**文件描述：       应用层公共定义（消息/定时器/枚举/全局变量/模块汇总）
**当前版本：       V1.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.06.05
*********************************************************************
** 功能描述：       1. 定义消息ID枚举和消息结构体
**                 2. 定义应用层枚举（task_state_e/task_module_e）
**                 3. 集中管理全局任务句柄/消息队列/模块状态
**                 4. 汇总包含所有应用模块头文件（只需 #include "my_comm.h"）
*********************************************************************/

#ifndef __MY_COMM_H__
#define __MY_COMM_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "my_os.h"
#include "my_config.h"

/*********************************************************************
 * 系统任务状态枚举
 *********************************************************************/
typedef enum
{
    TASK_STATE_NOT_INIT = 0,
    TASK_STATE_ACTIVE,
    TASK_STATE_SLEEP,
    TASK_STATE_SHUTDOWN
}task_state_e;

/*********************************************************************
 * 系统任务模块枚举
 *********************************************************************/
typedef enum
{
    TASK_MOD_MAIN = 0,    /**< 主模块 */
    TASK_MOD_RTT_SHELL,   /**< RTT Shell模块 */
    TASK_MOD_CTRL,        /**< 控制模块 */
    TASK_MOD_DVR,      /**< DVR模块 */
    TASK_MOD_GSENSOR,     /**< G传感器模块 */
    TASK_MOD_GNSS,        /**< GNSS模块 */
    TASK_MOD_CAN,         /**< CAN模块 */
    TASK_MOD_RS485,       /**< RS485模块 */
    TASK_MOD_RS232,       /**< RS232模块 */
    TASK_MOD_AMS,         /**< AMS模块 */

    TASK_MOD_MAX          /**< 模块数量最大值 */
} task_module_e;

/*********************************************************************
 * 消息ID枚举（应用层）
 * @note 所有任务的消息ID统一在此管理
 *********************************************************************/
typedef enum
{
    MY_MSG_ID_BASE = 0,             /**< 消息ID基值 */
    MY_MSG_ID_TEST,                 /**< 测试消息 */

   /* 系统生命周期消息（MAIN广播 → 所有子任务响应） */
    MY_MSG_ID_SYS_ACTIVE,           /**< 进入正常工作（ACC ON / 退出休眠后） */
    MY_MSG_ID_SYS_SLEEP,            /**< 进入低功耗休眠（ACC OFF / 定时休眠） */
    MY_MSG_ID_SYS_SHUTDOWN,         /**< 即将关机（保存数据、清理资源） */
    MY_MSG_ID_SYS_STATUS_REQ,       /**< 系统状态请求 */

    /* MY_MAIN模块消息 */
    MY_MSG_ID_ONE_MINUTE,           /**< 1分钟定时器消息 */

    /* MY_RTT_SHELL模块消息 */

    /* MY_DVR模块消息 */
    MY_MSG_ID_DVR_UART_SEND,              /**< DVR UART发送请求 */
    MY_MSG_ID_DVR_UART_ERROR,             /**< DVR UART错误 */
    MY_MSG_ID_DVR_UART_TX_DONE,           /**< DVR UART发送完成 */
    MY_MSG_ID_DVR_UART_RX_RDY,            /**< DVR UART接收完成 */
    MY_MSG_ID_DVR_PARSE_TIMEOUT,          /**< DVR 协议解析超时（2秒未完成） */
    MY_MSG_ID_DVR_SEND_HEARTBEAT,         /**< 向DVR发送心跳包（1秒） */
    MY_MSG_ID_DVR_WAIT_HEARTBEAT_TOUT,    /**< 接收DVR心跳包超时（90秒未收到） */
    MY_MSG_ID_DVR_HEARTBEAT_RESTART,      /**< 心跳异常，请求重启DVR模块 */

    /* AMS/GNSS/CAN等模块消息按需扩展 */

    MY_MSG_ID_MAX                   /**< 消息ID最大值 */
} my_msg_id_e;

/**
 * @brief 消息结构体定义（与 my_os.h 前向声明 struct my_msg 对应）
 * @note  所有模块使用统一的消息格式
 */
typedef struct my_msg
{
    my_msg_id_e id;             /**< 消息ID */
    void        *data;          /**< 消息数据指针 */
    uint32_t    len;            /**< 消息数据长度（字节） */
} my_msg_t;

/*********************************************************************
 * 定时器ID应用层映射（映射到 my_os.h 的槽位，最多支持50个）
 * 新增定时器请依次映射 SLOT_3 ~ SLOT_49
 * 使用方法：
 *   1. 新增定时器：在末尾追加，映射到下一个可用的 MY_TIMER_ID_SLOT_X
 *   2. 溢出检查：超过 MY_TIMER_ID_SLOT_49 时编译报错（枚举值不存在）
 *********************************************************************/
#define MY_TIMER_ID_TEST                  MY_TIMER_ID_SLOT_0   /**< 测试定时器 */
#define MY_TIMER_ID_ONE_MINUTE            MY_TIMER_ID_SLOT_1   /**< 1分钟定时器（核心） */
#define MY_TIMER_ID_DVR_PARSE_TIMEOUT     MY_TIMER_ID_SLOT_2   /**< DVR协议解析超时（2秒） */
#define MY_TIMER_ID_DVR_SEND_HEARTBEAT    MY_TIMER_ID_SLOT_3   /**< DVR发送心跳包（1秒） */
#define MY_TIMER_ID_DVR_WAIT_HEARTBEAT    MY_TIMER_ID_SLOT_4   /**< DVR心跳超时检测（90秒） */

/*********************************************************************
 * 公共数据结构定义（预留扩展）
 *********************************************************************/

/* TODO: 添加跨模块使用的公共结构体定义 */
/* 示例： */
/* typedef struct { */
/*     uint32_t timestamp;          // 时间戳 */
/*     uint8_t  status;             // 状态码 */
/* } my_common_status_t; */


/*********************************************************************
 * 集中管理公共变量（定义在 my_main.c）
 *********************************************************************/

extern my_task_handle_t          g_task_handle[TASK_MOD_MAX];
extern my_msg_queue_t            g_msg_queue[TASK_MOD_MAX];
extern task_state_e              g_task_state[TASK_MOD_MAX];

/* 定义主任务和RTT Shell任务的句柄和消息队列 */
#define TASK_HANDLE_MAIN         g_task_handle[TASK_MOD_MAIN]
#define MSG_QUEUE_MAIN           g_msg_queue[TASK_MOD_MAIN]
#define TASK_STATE_MAIN          g_task_state[TASK_MOD_MAIN]

/* 定义RTT Shell任务的句柄和消息队列 */
#define TASK_HANDLE_RTT_SHELL    g_task_handle[TASK_MOD_RTT_SHELL]
#define MSG_QUEUE_RTT_SHELL      g_msg_queue[TASK_MOD_RTT_SHELL]
#define TASK_STATE_RTT_SHELL     g_task_state[TASK_MOD_RTT_SHELL]

/* 定义控制任务的句柄和消息队列 */
#define TASK_HANDLE_CTRL         g_task_handle[TASK_MOD_CTRL]
#define MSG_QUEUE_CTRL           g_msg_queue[TASK_MOD_CTRL]
#define TASK_STATE_CTRL          g_task_state[TASK_MOD_CTRL]

/* 定义DVR任务的句柄和消息队列 */
#define TASK_HANDLE_DVR          g_task_handle[TASK_MOD_DVR]
#define MSG_QUEUE_DVR            g_msg_queue[TASK_MOD_DVR]
#define TASK_STATE_DVR           g_task_state[TASK_MOD_DVR]

/* 定义G-Sensor任务的句柄和消息队列 */
#define TASK_HANDLE_GSENSOR      g_task_handle[TASK_MOD_GSENSOR]
#define MSG_QUEUE_GSENSOR        g_msg_queue[TASK_MOD_GSENSOR]
#define TASK_STATE_GSENSOR       g_task_state[TASK_MOD_GSENSOR]

/* 定义GNSS任务的句柄和消息队列 */
#define TASK_HANDLE_GNSS         g_task_handle[TASK_MOD_GNSS]
#define MSG_QUEUE_GNSS           g_msg_queue[TASK_MOD_GNSS]
#define TASK_STATE_GNSS          g_task_state[TASK_MOD_GNSS]

/* 定义CAN任务的句柄和消息队列 */
#define TASK_HANDLE_CAN          g_task_handle[TASK_MOD_CAN]
#define MSG_QUEUE_CAN            g_msg_queue[TASK_MOD_CAN]
#define TASK_STATE_CAN           g_task_state[TASK_MOD_CAN]

/* 定义RS485任务的句柄和消息队列 */
#define TASK_HANDLE_RS485        g_task_handle[TASK_MOD_RS485]
#define MSG_QUEUE_RS485          g_msg_queue[TASK_MOD_RS485]
#define TASK_STATE_RS485         g_task_state[TASK_MOD_RS485]

/* 定义RS232任务的句柄和消息队列 */
#define TASK_HANDLE_RS232        g_task_handle[TASK_MOD_RS232]
#define MSG_QUEUE_RS232          g_msg_queue[TASK_MOD_RS232]
#define TASK_STATE_RS232         g_task_state[TASK_MOD_RS232]

/* 定义AMS任务的句柄和消息队列 */
#define TASK_HANDLE_AMS          g_task_handle[TASK_MOD_AMS]
#define MSG_QUEUE_AMS            g_msg_queue[TASK_MOD_AMS]
#define TASK_STATE_AMS           g_task_state[TASK_MOD_AMS]

/*********************************************************************
 * 工具库头文件
 *********************************************************************/
#include "my_log.h"
#include "my_mem.h"
#include "my_version.h"
#include "my_rb.h"
#include "my_tq.h"
#include "my_tool.h"
#include "param_manager.h"

/*********************************************************************
 * 驱动库头文件
 *********************************************************************/
#include "adc_driver.h"
#include "can_driver.h"
#include "dma_driver.h"
#include "gpio_driver.h"
#include "i2c_driver.h"
#include "timer_driver.h"
#include "uart_driver.h"

/*********************************************************************
 * 应用模块头文件（包含 guard 防止循环依赖）
 *********************************************************************/
#include "my_main.h"
#include "my_rtt_shell.h"
#include "my_ctrl.h"
#include "my_dvr.h"
#include "my_dvr_parse.h"
#include "my_dvr_cmd.h"
#include "my_dvr_heartbeat.h"
#include "my_gsensor.h"
#include "my_gnss.h"
#include "my_can.h"
#include "my_rs485.h"
#include "my_rs232.h"
#include "my_ams.h"

#endif /* __MY_COMM_H__ */
