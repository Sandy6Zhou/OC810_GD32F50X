/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       my_bl_log_print.h
**文件描述：       Bootloader日志输出头文件
**当前版本：       V1.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.06.22
*********************************************************************
** 功能描述：       RTT + UART 双通道日志输出接口
**                 支持时间戳前缀与 hex dump
*********************************************************************/

#ifndef __MY_BL_LOG_H__
#define __MY_BL_LOG_H__

#include "my_bl_config.h"
#include <stdio.h>
#include <stdint.h>

/* ==================== 日志宏定义 ==================== */

#if (MY_BL_LOG_CURRENT_LEVEL >= MY_BL_LOG_LEVEL_DEBUG)
    #define MY_LOG_D(fmt, ...)     my_bl_log_print(MY_BL_LOG_LEVEL_DEBUG, "DBG", __FUNCTION__, fmt, ##__VA_ARGS__)
#else
    #define MY_LOG_D(fmt, ...)
#endif

#if (MY_BL_LOG_CURRENT_LEVEL >= MY_BL_LOG_LEVEL_INFO)
    #define MY_LOG_I(fmt, ...)     my_bl_log_print(MY_BL_LOG_LEVEL_INFO, "INF", __FUNCTION__, fmt, ##__VA_ARGS__)
#else
    #define MY_LOG_I(fmt, ...)
#endif

#if (MY_BL_LOG_CURRENT_LEVEL >= MY_BL_LOG_LEVEL_WARN)
    #define MY_LOG_W(fmt, ...)     my_bl_log_print(MY_BL_LOG_LEVEL_WARN, "WRN", __FUNCTION__, fmt, ##__VA_ARGS__)
#else
    #define MY_LOG_W(fmt, ...)
#endif

#if (MY_BL_LOG_CURRENT_LEVEL >= MY_BL_LOG_LEVEL_ERROR)
    #define MY_LOG_E(fmt, ...)     my_bl_log_print(MY_BL_LOG_LEVEL_ERROR, "ERR", __FUNCTION__, fmt, ##__VA_ARGS__)
#else
    #define MY_LOG_E(fmt, ...)
#endif

#if (MY_BL_LOG_CURRENT_LEVEL >= MY_BL_LOG_LEVEL_DEBUG)
    #define MY_LOG_DBG_DUMP(tag, data, len)    my_bl_log_dump(tag, data, len)
#else
    #define MY_LOG_DBG_DUMP(tag, data, len)
#endif

#if (MY_BL_LOG_CURRENT_LEVEL >= MY_BL_LOG_LEVEL_INFO)
    #define MY_LOG_INF_DUMP(tag, data, len)    my_bl_log_dump(tag, data, len)
#else
    #define MY_LOG_INF_DUMP(tag, data, len)
#endif

/*********************************************************************
 * @brief   初始化日志系统
 * @param   None
 * @return  None
 * @note    根据配置初始化 RTT
 *********************************************************************/
void my_bl_log_init(void);

/*********************************************************************
 * @brief   打印日志
 * @param   level      日志级别
 * @param   level_str  日志级别字符串
 * @param   function   函数名
 * @param   fmt        格式化字符串
 * @param   ...        可变参数
 * @return  None
 * @note    使用静态缓冲区，根据级别过滤输出
 *********************************************************************/
void my_bl_log_print(int level, const char *level_str, const char *function, const char *fmt, ...);

/*********************************************************************
 * @brief   输出十六进制数据 dump（Hex + ASCII）
 * @param   tag    标签字符串
 * @param   pdata  数据缓冲区指针
 * @param   len    数据长度（字节）
 * @return  None
 * @note    仅当当前日志级别 >= DEBUG 时输出
 *********************************************************************/
void my_bl_log_dump(char *tag, const void *pdata, uint32_t len);

#endif
