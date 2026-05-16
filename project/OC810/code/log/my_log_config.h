/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       my_log_config.h
**文件描述：       日志系统配置文件 (RTT/UART输出方式、日志级别)
**当前版本：       V1.0
**作    者：       伍玉蛟 (wuyujiao@jimiiot.com)
**完成日期：       2026.04.17
*********************************************************************
** 功能描述：       1. 配置日志输出方式（RTT或串口）
**                 2. 定义日志级别及默认输出级别
**                 3. 定义日志颜色（RTT支持）
*********************************************************************/

#ifndef __MY_LOG_CONFIG_H__
#define __MY_LOG_CONFIG_H__

/* 先包含 GD32 系统头文件，提供 uint32_t 类型定义 */
#include "gd32f50x.h"
#include "FreeRTOSConfig.h"

/* ==================== 日志输出方式选择 ==================== */
/* 二选一，不可同时启用 */
#define LOG_USE_RTT         1   // 使用 Segger RTT（开发调试推荐）
#define LOG_USE_UART        0   // 使用 USART0（工厂测试/现场部署）

#if (LOG_USE_RTT && LOG_USE_UART)
    #error "LOG_USE_RTT and LOG_USE_UART cannot be enabled at the same time!"
#endif

/* ==================== 日志级别定义 ==================== */
#define MY_LOG_LEVEL_NONE      0   // 关闭所有日志
#define MY_LOG_LEVEL_ERROR     1   // 错误级别
#define MY_LOG_LEVEL_WARN      2   // 警告级别
#define MY_LOG_LEVEL_INFO      3   // 信息级别
#define MY_LOG_LEVEL_DEBUG     4   // 调试级别

/* 根据编译模式设置默认日志级别 */
#ifndef MY_LOG_CURRENT_LEVEL
    #ifdef DEBUG
        #define MY_LOG_CURRENT_LEVEL   MY_LOG_LEVEL_DEBUG  // DEBUG模式：输出所有日志
    #else
        #define MY_LOG_CURRENT_LEVEL   MY_LOG_LEVEL_INFO   // RELEASE模式：仅INFO及以上
    #endif
#endif

/* ==================== 日志格式 ==================== */
/* 固定格式：[级别] 函数名 内容 */
/* 示例：[INF] drv_uart_init Port 0 initialized successfully */

/* ==================== 日志颜色定义（RTT支持，已禁用） ==================== */
/* 颜色功能已禁用，如需启用请取消下方注释 */
/*
#define MY_LOG_COLOR_RESET     "\033[0m"
#define MY_LOG_COLOR_RED       "\033[31m"
#define MY_LOG_COLOR_YELLOW    "\033[33m"
#define MY_LOG_COLOR_GREEN     "\033[32m"
#define MY_LOG_COLOR_BLUE      "\033[34m"
*/

#endif /* __MY_LOG_CONFIG_H__ */
