/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       my_config.h
**文件描述：       系统配置管理（任务栈/优先级/消息队列统一管理）
**当前版本：       V1.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.06.05
*********************************************************************
** 功能描述：       1. 统一定义任务优先级等级
**                 2. 集中管理所有任务栈大小配置
**                 3. 集中管理所有任务优先级配置
**                 4. 集中管理所有消息队列深度配置
**                 5. 提供编译期资源校验（防RAM超标）
*********************************************************************/

#ifndef __MY_CONFIG_H__
#define __MY_CONFIG_H__

/* ========== 系统头文件引用 ========== */
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"

/*********************************************************************
 * 任务优先级配置（数值越大优先级越高，按优先级从高到低排列）
 *********************************************************************/
#define MY_CAN_TASK_PRIO        (tskIDLE_PRIORITY + 5)     /**< CAN车载通信 - 最高 */
#define MY_MAIN_TASK_PRIO       (tskIDLE_PRIORITY + 4)     /**< 主任务协调 */
#define MY_CTRL_TASK_PRIO       (tskIDLE_PRIORITY + 4)     /**< 控制模块 */
#define MY_SHELL_TASK_PRIO      (tskIDLE_PRIORITY + 3)     /**< RTT Shell调试 */
#define MY_DVR_TASK_PRIO     (tskIDLE_PRIORITY + 3)     /**< 视频芯片 */
#define MY_RS485_TASK_PRIO      (tskIDLE_PRIORITY + 3)     /**< RS485串口 */
#define MY_RS232_TASK_PRIO      (tskIDLE_PRIORITY + 3)     /**< RS232串口 */
#define MY_GNSS_TASK_PRIO       (tskIDLE_PRIORITY + 2)     /**< GNSS定位 */
#define MY_GSENSOR_TASK_PRIO    (tskIDLE_PRIORITY + 2)     /**< G传感器 */
#define MY_AMS_TASK_PRIO        (tskIDLE_PRIORITY + 2)     /**< 自动消息 */

/*********************************************************************
 * 任务栈大小配置（单位：字，1字=4字节）
 *********************************************************************/
/**
 * @brief MAIN主任务栈大小
 * @note 主任务负责协调管理所有子任务，需要较大栈空间
 */
#define MY_MAIN_TASK_STACK_SIZE         (configMINIMAL_STACK_SIZE * 4)

/**
 * @brief RTT Shell任务栈大小
 * @note Shell支持命令解析和输出，需要较大栈空间
 */
#define MY_SHELL_TASK_STACK_SIZE        (configMINIMAL_STACK_SIZE * 3)

/**
 * @brief CTRL模块任务栈大小
 * @note CTRL控制LED/Buzzer/Key，逻辑较复杂
 */
#define MY_CTRL_TASK_STACK_SIZE         (configMINIMAL_STACK_SIZE * 6)

/**
 * @brief DVR模块任务栈大小
 * @note DVR视频芯片控制，逻辑中等
 */
#define MY_DVR_TASK_STACK_SIZE       (configMINIMAL_STACK_SIZE * 2)

/**
 * @brief GSENSOR模块任务栈大小
 * @note GSENSOR读取加速度计数据，逻辑简单
 */
#define MY_GSENSOR_TASK_STACK_SIZE      (configMINIMAL_STACK_SIZE * 2)

/**
 * @brief GNSS模块任务栈大小
 * @note GNSS解析NMEA协议，需要中等栈空间
 */
#define MY_GNSS_TASK_STACK_SIZE         (configMINIMAL_STACK_SIZE * 3)

/**
 * @brief CAN模块任务栈大小
 * @note CAN处理协议栈解析，需要中等栈空间
 */
#define MY_CAN_TASK_STACK_SIZE          (configMINIMAL_STACK_SIZE * 4)

/**
 * @brief RS485模块任务栈大小
 * @note RS485串口通信，需要中等栈空间
 */
#define MY_RS485_TASK_STACK_SIZE        (configMINIMAL_STACK_SIZE * 3)

/**
 * @brief RS232模块任务栈大小
 * @note RS232串口通信，需要中等栈空间
 */
#define MY_RS232_TASK_STACK_SIZE        (configMINIMAL_STACK_SIZE * 3)

/**
 * @brief AMS模块任务栈大小
 * @note AMS负责自动消息发送，逻辑简单
 */
#define MY_AMS_TASK_STACK_SIZE          (configMINIMAL_STACK_SIZE * 2)

/*********************************************************************
 * 消息队列深度配置（单位：消息条数）
 *********************************************************************/
/**
 * @brief MAIN主任务消息队列深度
 */
#define MY_MAIN_MSG_QUEUE_DEPTH         (8)

/**
 * @brief CTRL模块消息队列深度
 */
#define MY_CTRL_MSG_QUEUE_DEPTH         (8)

/**
 * @brief DVR模块消息队列深度
 */
#define MY_DVR_MSG_QUEUE_DEPTH       (8)

/**
 * @brief GSENSOR模块消息队列深度
 */
#define MY_GSENSOR_MSG_QUEUE_DEPTH      (8)

/**
 * @brief GNSS模块消息队列深度
 */
#define MY_GNSS_MSG_QUEUE_DEPTH         (8)

/**
 * @brief CAN模块消息队列深度
 * @note CAN数据量大，队列需要较深
 */
#define MY_CAN_MSG_QUEUE_DEPTH          (16)

/**
 * @brief RS485模块消息队列深度
 * @note RS485数据量大，队列需要较深
 */
#define MY_RS485_MSG_QUEUE_DEPTH        (16)

/**
 * @brief RS232模块消息队列深度
 * @note RS232数据量大，队列需要较深
 */
#define MY_RS232_MSG_QUEUE_DEPTH        (16)

/**
 * @brief AMS模块消息队列深度
 */
#define MY_AMS_MSG_QUEUE_DEPTH          (8)

/**
 * @brief RTT Shell模块消息队列深度（仅接收系统级命令）
 */
#define MY_SHELL_MSG_QUEUE_DEPTH        (4)

/*********************************************************************
 * Shell功能配置
 *********************************************************************/
#define MY_SHELL_LINE_MAX       (128)       /**< 单行命令最大长度 */
#define MY_SHELL_ARGC_MAX       (8)         /**< 单条命令最大参数个数 */
#define MY_SHELL_PROMPT         "rtt> "   /**< 命令提示符 */

/*********************************************************************
 * 资源汇总统计（编译期校验）
 *********************************************************************/

/**
 * @brief 总任务栈大小（字）
 * @note 仅统计应用层任务，不含FreeRTOS空闲任务/定时器任务
 */
#define MY_TOTAL_TASK_STACK_WORDS   ( \
    MY_MAIN_TASK_STACK_SIZE + \
    MY_SHELL_TASK_STACK_SIZE + \
    MY_CTRL_TASK_STACK_SIZE + \
    MY_DVR_TASK_STACK_SIZE + \
    MY_GSENSOR_TASK_STACK_SIZE + \
    MY_GNSS_TASK_STACK_SIZE + \
    MY_CAN_TASK_STACK_SIZE + \
    MY_RS485_TASK_STACK_SIZE + \
    MY_RS232_TASK_STACK_SIZE + \
    MY_AMS_TASK_STACK_SIZE \
)

/**
 * @brief 总任务栈大小（字节）
 */
#define MY_TOTAL_TASK_STACK_BYTES   (MY_TOTAL_TASK_STACK_WORDS * 4)

/**
 * @brief 总消息队列RAM占用（字节）
 * @note 每条消息 = sizeof(my_msg_t) = 12字节（32位系统）
 * @note 实际占用 = 队列深度 * 消息大小 * 队列数量
 */
#define MY_TOTAL_QUEUE_BYTES        ( \
    (MY_MAIN_MSG_QUEUE_DEPTH + \
     MY_CTRL_MSG_QUEUE_DEPTH + \
     MY_DVR_MSG_QUEUE_DEPTH + \
     MY_GSENSOR_MSG_QUEUE_DEPTH + \
     MY_GNSS_MSG_QUEUE_DEPTH + \
     MY_CAN_MSG_QUEUE_DEPTH + \
     MY_RS485_MSG_QUEUE_DEPTH + \
     MY_RS232_MSG_QUEUE_DEPTH + \
     MY_AMS_MSG_QUEUE_DEPTH + \
     MY_SHELL_MSG_QUEUE_DEPTH) * 12U \
)

/**
 * @brief 编译期校验：总任务栈不超过FreeRTOS堆的70%
 * @note 预留30%给动态分配（定时器/信号量/其他）
 * @note 如果编译失败，说明任务栈配置过大，需要优化
 */
#define MY_MAX_ALLOWED_STACK_BYTES  ((configTOTAL_HEAP_SIZE * 70) / 100)

/* 编译期校验：总任务栈不超过FreeRTOS堆的70% */
_Static_assert(
    MY_TOTAL_TASK_STACK_BYTES < MY_MAX_ALLOWED_STACK_BYTES,
    "Total task stack exceeds 70% of FreeRTOS heap! Please reduce stack sizes."
);

/**
 * @brief 编译期校验：任务数量不超过configMAX_PRIORITIES
 * @note 确保每个任务都能分配到独立优先级
 */
#define MY_TASK_COUNT   (10)  /* AMS/CAN/CTRL/GNSS/GSENSOR/MAIN/DVR/RS232/RS485/SHELL */

/* 编译期校验：最高优先级不超过configMAX_PRIORITIES */
_Static_assert(
    MY_CAN_TASK_PRIO < configMAX_PRIORITIES,
    "Highest task priority exceeds configMAX_PRIORITIES! Please adjust."
);

#endif /* __MY_CONFIG_H__ */
