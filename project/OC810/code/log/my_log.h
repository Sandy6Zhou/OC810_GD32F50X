/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       my_log.h
**文件描述：       日志系统接口头文件 (RTT/UART双模日志)
**当前版本：       V1.0
**作    者：       伍玉蛟 (wuyujiao@jimiiot.com)
**完成日期：       2026.04.17
*********************************************************************
** 功能描述：       1. 提供统一日志接口（MY_LOG_D/I/W/E/DUMP）
**                 2. 支持文件名、函数名、行号自动捕获
**                 3. 提供日志统计功能
**                 4. 支持二进制数据Dump
*********************************************************************/

#ifndef __MY_LOG_H__
#define __MY_LOG_H__

#include "my_log_config.h"
#include <stdio.h>
#include <stdint.h>

/* ==================== 日志宏定义 ==================== */

#if (MY_LOG_CURRENT_LEVEL >= MY_LOG_LEVEL_DEBUG)
    #define MY_LOG_D(fmt, ...)     my_log_print(MY_LOG_LEVEL_DEBUG, "DBG", __FUNCTION__, fmt, ##__VA_ARGS__)
#else
    #define MY_LOG_D(fmt, ...)
#endif

#if (MY_LOG_CURRENT_LEVEL >= MY_LOG_LEVEL_INFO)
    #define MY_LOG_I(fmt, ...)     my_log_print(MY_LOG_LEVEL_INFO, "INF", __FUNCTION__, fmt, ##__VA_ARGS__)
#else
    #define MY_LOG_I(fmt, ...)
#endif

#if (MY_LOG_CURRENT_LEVEL >= MY_LOG_LEVEL_WARN)
    #define MY_LOG_W(fmt, ...)     my_log_print(MY_LOG_LEVEL_WARN, "WRN", __FUNCTION__, fmt, ##__VA_ARGS__)
#else
    #define MY_LOG_W(fmt, ...)
#endif

#if (MY_LOG_CURRENT_LEVEL >= MY_LOG_LEVEL_ERROR)
    #define MY_LOG_E(fmt, ...)     my_log_print(MY_LOG_LEVEL_ERROR, "ERR", __FUNCTION__, fmt, ##__VA_ARGS__)
#else
    #define MY_LOG_E(fmt, ...)
#endif

/**
 * @brief 二进制数据Dump日志
 * @param tag 数据标签
 * @param data 数据指针
 * @param len 数据长度
 */
#if (MY_LOG_CURRENT_LEVEL >= MY_LOG_LEVEL_DEBUG)
    #define MY_LOG_DUMP(tag, data, len)    my_log_dump(tag, data, len)
#else
    #define MY_LOG_DUMP(tag, data, len)
#endif

/* ==================== 日志统计 ==================== */

typedef struct {
    uint32_t print_count;      // 打印次数
    uint32_t dump_count;       // Dump次数
    uint32_t overflow_count;   // 缓冲区溢出次数
} log_stats_t;

/* ==================== 函数声明 ==================== */

/*********************************************************************
 * @brief   初始化日志系统
 * @param   none
 * @return  none
 * @note    根据配置初始化RTT或串口
 *********************************************************************/
void my_log_init(void);

/*********************************************************************
 * @brief   打印日志
 * @param   level 日志级别
 * @param   level_str 日志级别字符串
 * @param   function 函数名
 * @param   fmt 格式化字符串
 * @param   ... 可变参数
 * @return  none
 * @note    使用局部缓冲区避免多任务竞争
 *********************************************************************/
void my_log_print(int level, const char *level_str, const char *function, const char *fmt, ...);

/*********************************************************************
 * @brief   Dump二进制数据
 * @param   tag 数据标签
 * @param   data 数据指针
 * @param   len 数据长度
 * @return  none
 * @note    输出Hex和ASCII格式，仅DEBUG级别有效
 *********************************************************************/
void my_log_dump(const char *tag, const void *data, uint32_t len);

/*********************************************************************
 * @brief   获取日志统计信息
 * @param   stats 统计信息输出指针
 * @return  none
 * @note    指针使用前已判空
 *********************************************************************/
void my_log_get_stats(log_stats_t *stats);

/*********************************************************************
 * @brief   打印日志统计信息
 * @param   none
 * @return  none
 * @note    用于调试和监控日志系统运行状态
 *********************************************************************/
void my_log_print_stats(void);

#endif /* __MY_LOG_H__ */
