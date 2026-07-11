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
 * 任务优先级等级定义
 *********************************************************************/

/**
 * @brief 任务优先级等级（数值越大优先级越高）
 * @note 基于 configMAX_PRIORITIES 划分等级
 * @note 实际优先级 = tskIDLE_PRIORITY + 等级偏移
 */
#define MY_TASK_PRIORITY_IDLE       (tskIDLE_PRIORITY + 0)    /**< 空闲级（最低） */
#define MY_TASK_PRIORITY_LOW        (tskIDLE_PRIORITY + 1)    /**< 低优先级 */
#define MY_TASK_PRIORITY_NORMAL     (tskIDLE_PRIORITY + 2)    /**< 普通优先级 */
#define MY_TASK_PRIORITY_HIGH       (tskIDLE_PRIORITY + 3)    /**< 高优先级 */
#define MY_TASK_PRIORITY_CRITICAL   (tskIDLE_PRIORITY + 4)    /**< 关键级（最高） */

/*********************************************************************
 * 任务栈大小配置（单位：字，1字=4字节）
 *********************************************************************/

/**
 * @brief AMS模块任务栈大小
 * @note AMS负责自动消息发送，逻辑简单
 */
#define MY_AMS_TASK_STACK_SIZE          (configMINIMAL_STACK_SIZE * 2)

/**
 * @brief CAN模块任务栈大小
 * @note CAN处理协议栈解析，需要中等栈空间
 */
#define MY_CAN_TASK_STACK_SIZE          (configMINIMAL_STACK_SIZE * 4)

/**
 * @brief CTRL模块任务栈大小
 * @note CTRL控制LED/Buzzer/Key，逻辑较复杂
 */
#define MY_CTRL_TASK_STACK_SIZE         (configMINIMAL_STACK_SIZE * 6)

/**
 * @brief GNSS模块任务栈大小
 * @note GNSS解析NMEA协议，需要中等栈空间
 */
#define MY_GNSS_TASK_STACK_SIZE         (configMINIMAL_STACK_SIZE * 3)

/**
 * @brief GSENSOR模块任务栈大小
 * @note GSENSOR读取加速度计数据，逻辑简单
 */
#define MY_GSENSOR_TASK_STACK_SIZE      (configMINIMAL_STACK_SIZE * 2)

/**
 * @brief MAIN主任务栈大小
 * @note 主任务负责协调管理所有子任务，需要较大栈空间
 */
#define MY_MAIN_TASK_STACK_SIZE         (configMINIMAL_STACK_SIZE * 4)

/**
 * @brief NT98XX模块任务栈大小
 * @note NT98XX视频芯片控制，逻辑中等
 */
#define MY_NT98XX_TASK_STACK_SIZE       (configMINIMAL_STACK_SIZE * 2)

/**
 * @brief RS232模块任务栈大小
 * @note RS232串口通信，需要中等栈空间
 */
#define MY_RS232_TASK_STACK_SIZE        (configMINIMAL_STACK_SIZE * 3)

/**
 * @brief RS485模块任务栈大小
 * @note RS485串口通信，需要中等栈空间
 */
#define MY_RS485_TASK_STACK_SIZE        (configMINIMAL_STACK_SIZE * 3)

/**
 * @brief RTT Shell任务栈大小
 * @note Shell支持命令解析和输出，需要较大栈空间
 */
#define MY_SHELL_TASK_STACK_SIZE        (configMINIMAL_STACK_SIZE * 3)

/*********************************************************************
 * 任务优先级配置
 *********************************************************************/

/**
 * @brief AMS模块任务优先级
 */
#define MY_AMS_TASK_PRIO                (MY_TASK_PRIORITY_LOW)

/**
 * @brief CAN模块任务优先级
 * @note CAN为车载通信关键模块，优先级最高
 */
#define MY_CAN_TASK_PRIO                (MY_TASK_PRIORITY_HIGH)

/**
 * @brief CTRL模块任务优先级
 * @note 控制任务需要响应按键，优先级略高于普通
 */
#define MY_CTRL_TASK_PRIO               (MY_TASK_PRIORITY_NORMAL + 1)

/**
 * @brief GNSS模块任务优先级
 */
#define MY_GNSS_TASK_PRIO               (MY_TASK_PRIORITY_LOW)

/**
 * @brief GSENSOR模块任务优先级
 */
#define MY_GSENSOR_TASK_PRIO            (MY_TASK_PRIORITY_LOW)

/**
 * @brief MAIN主任务优先级
 * @note 主任务负责协调，优先级高于普通任务
 */
#define MY_MAIN_TASK_PRIO               (MY_TASK_PRIORITY_NORMAL + 1)

/**
 * @brief NT98XX模块任务优先级
 */
#define MY_NT98XX_TASK_PRIO             (MY_TASK_PRIORITY_NORMAL)

/**
 * @brief RS232模块任务优先级
 */
#define MY_RS232_TASK_PRIO              (MY_TASK_PRIORITY_NORMAL)

/**
 * @brief RS485模块任务优先级
 */
#define MY_RS485_TASK_PRIO              (MY_TASK_PRIORITY_NORMAL)

/**
 * @brief RTT Shell任务优先级
 * @note Shell为调试工具，优先级较低
 */
#define MY_SHELL_TASK_PRIO              (MY_TASK_PRIORITY_NORMAL)

/*********************************************************************
 * 消息队列深度配置（单位：消息条数）
 *********************************************************************/

/**
 * @brief AMS模块消息队列深度
 */
#define MY_AMS_MSG_QUEUE_DEPTH          (8)

/**
 * @brief CAN模块消息队列深度
 * @note CAN数据量大，队列需要较深
 */
#define MY_CAN_MSG_QUEUE_DEPTH          (16)

/**
 * @brief CTRL模块消息队列深度
 */
#define MY_CTRL_MSG_QUEUE_DEPTH         (8)

/**
 * @brief GNSS模块消息队列深度
 */
#define MY_GNSS_MSG_QUEUE_DEPTH         (8)

/**
 * @brief GSENSOR模块消息队列深度
 */
#define MY_GSENSOR_MSG_QUEUE_DEPTH      (8)

/**
 * @brief MAIN主任务消息队列深度
 */
#define MY_MAIN_MSG_QUEUE_DEPTH         (8)

/**
 * @brief NT98XX模块消息队列深度
 */
#define MY_NT98XX_MSG_QUEUE_DEPTH       (8)

/**
 * @brief RS232模块消息队列深度
 * @note RS232数据量大，队列需要较深
 */
#define MY_RS232_MSG_QUEUE_DEPTH        (16)

/**
 * @brief RS485模块消息队列深度
 * @note RS485数据量大，队列需要较深
 */
#define MY_RS485_MSG_QUEUE_DEPTH        (16)

/*********************************************************************
 * 资源汇总统计（编译期校验）
 *********************************************************************/

/**
 * @brief 总任务栈大小（字）
 * @note 仅统计应用层任务，不含FreeRTOS空闲任务/定时器任务
 */
#define MY_TOTAL_TASK_STACK_WORDS   ( \
    MY_AMS_TASK_STACK_SIZE + \
    MY_CAN_TASK_STACK_SIZE + \
    MY_CTRL_TASK_STACK_SIZE + \
    MY_GNSS_TASK_STACK_SIZE + \
    MY_GSENSOR_TASK_STACK_SIZE + \
    MY_MAIN_TASK_STACK_SIZE + \
    MY_NT98XX_TASK_STACK_SIZE + \
    MY_RS232_TASK_STACK_SIZE + \
    MY_RS485_TASK_STACK_SIZE + \
    MY_SHELL_TASK_STACK_SIZE \
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
    (MY_AMS_MSG_QUEUE_DEPTH + \
     MY_CAN_MSG_QUEUE_DEPTH + \
     MY_CTRL_MSG_QUEUE_DEPTH + \
     MY_GNSS_MSG_QUEUE_DEPTH + \
     MY_GSENSOR_MSG_QUEUE_DEPTH + \
     MY_MAIN_MSG_QUEUE_DEPTH + \
     MY_NT98XX_MSG_QUEUE_DEPTH + \
     MY_RS232_MSG_QUEUE_DEPTH + \
     MY_RS485_MSG_QUEUE_DEPTH) * 12U \
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
#define MY_TASK_COUNT   (10)  /* AMS/CAN/CTRL/GNSS/GSENSOR/MAIN/NT98XX/RS232/RS485/SHELL */

/* 编译期校验：任务优先级不超过configMAX_PRIORITIES */
_Static_assert(
    MY_TASK_PRIORITY_CRITICAL < configMAX_PRIORITIES,
    "Highest task priority exceeds configMAX_PRIORITIES! Please adjust priority levels."
);

#endif /* __MY_CONFIG_H__ */
