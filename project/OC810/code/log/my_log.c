/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       my_log.c
**文件描述：       日志系统实现文件 (RTT/UART双模日志)
**当前版本：       V1.0
**作    者：       伍玉蛟 (wuyujiao@jimiiot.com)
**完成日期：       2026.04.17
*********************************************************************
** 功能描述：       1. 实现日志打印功能（带文件名、函数名、行号）
**                 2. 实现二进制数据Dump功能
**                 3. 实现日志统计功能
**                 4. RTT/UART双模输出切换
*********************************************************************/

#include "my_log.h"
#include "rtt_logger.h"
#include "uart_logger.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <stdarg.h>
#include <string.h>

/* 日志统计实例 */
static log_stats_t sLogStats = {0};

/* 日志互斥锁，保护静态缓冲区和统计变量 */
static SemaphoreHandle_t sLogMutex = NULL;

/* 静态缓冲区，避免栈溢出 */
static char sLogPrintBuffer[384];
static char sLogDumpBuffer[128];

/*********************************************************************
 * @brief   初始化日志系统
 * @param   none
 * @return  none
 * @note    根据配置初始化RTT或串口，清零统计信息，创建互斥锁
 *********************************************************************/
void my_log_init(void)
{
    memset(&sLogStats, 0, sizeof(sLogStats));

    /* 创建日志互斥锁 */
    sLogMutex = xSemaphoreCreateMutex();
    configASSERT(sLogMutex != NULL);

#if LOG_USE_RTT
    rtt_logger_init();
#elif LOG_USE_UART
    uart_logger_init();
#else
    /* 日志已关闭 */
#endif

    /* 注意：这里不使用 MY_LOG_I，避免在初始化期间调用未完全初始化的系统 */
}

/*********************************************************************
 * @brief   无锁日志初始化（仅用于HardFault等严重错误场景）
 * @param   none
 * @return  none
 * @note    跳过互斥锁创建，仅初始化底层输出通道
 *********************************************************************/
void my_log_critical_init(void)
{
    /* 仅初始化底层输出通道，跳过互斥锁创建 */
#if LOG_USE_RTT
    rtt_logger_init();
#elif LOG_USE_UART
    uart_logger_init();
#endif
}

/*********************************************************************
 * @brief   打印日志
 * @param   level 日志级别
 * @param   level_str 日志级别字符串
 * @param   function 函数名
 * @param   fmt 格式化字符串
 * @param   ... 可变参数
 * @return  none
 * @note    使用静态缓冲区+互斥锁保证线程安全
 *********************************************************************/
void my_log_print(int level, const char *level_str, const char *function, const char *fmt, ...)
{
    va_list args;
    int len = 0;

    if (level > MY_LOG_CURRENT_LEVEL)
    {
        return;  /* 低于当前级别，不输出 */
    }

    /* 获取互斥锁，保证线程安全 */
    if (sLogMutex != NULL)
    {
        if (xSemaphoreTake(sLogMutex, portMAX_DELAY) != pdTRUE)
        {
            return;  /* 获取锁失败，直接返回 */
        }
    }

    va_start(args, fmt);

    /* 生成日志头：[级别] 函数名 */
    if (function != NULL)
    {
        len = snprintf(sLogPrintBuffer, sizeof(sLogPrintBuffer), "[%s] %s ", level_str, function);
    }
    else
    {
        len = snprintf(sLogPrintBuffer, sizeof(sLogPrintBuffer), "[%s] ", level_str);
    }

    if (len < 0 || len >= sizeof(sLogPrintBuffer))
    {
        sLogStats.overflow_count++;
        va_end(args);

        if (sLogMutex != NULL)
        {
            xSemaphoreGive(sLogMutex);
        }
        return;  /* 格式化失败或溢出 */
    }

    len += vsnprintf(sLogPrintBuffer + len, sizeof(sLogPrintBuffer) - len, fmt, args);

    /* 添加换行符 */
    if (len < sizeof(sLogPrintBuffer) - 2)
    {
        sLogPrintBuffer[len++] = '\r';
        sLogPrintBuffer[len++] = '\n';
        sLogPrintBuffer[len] = '\0';
    }
    else
    {
        sLogStats.overflow_count++;
        len = sizeof(sLogPrintBuffer) - 3;
        sLogPrintBuffer[len++] = '\r';
        sLogPrintBuffer[len++] = '\n';
        sLogPrintBuffer[len] = '\0';
    }

    va_end(args);

    /* 更新统计信息（已在互斥锁保护范围内） */
    sLogStats.print_count++;

    /* 输出日志 */
#if LOG_USE_RTT
    rtt_logger_write(sLogPrintBuffer, len);
#elif LOG_USE_UART
    uart_logger_write(sLogPrintBuffer, len);
#endif

    /* 释放互斥锁 */
    if (sLogMutex != NULL)
    {
        xSemaphoreGive(sLogMutex);
    }
}

/*********************************************************************
 * @brief   Dump二进制数据
 * @param   tag 数据标签
 * @param   data 数据指针
 * @param   len 数据长度
 * @return  none
 * @note    输出Hex和ASCII格式，仅DEBUG级别有效，使用静态缓冲区+互斥锁
 *********************************************************************/
void my_log_dump(const char *tag, const void *data, uint32_t len)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t offset = 0;
    int header_len = 0;

    /* 指针判空 */
    if (tag == NULL || data == NULL || len == 0)
    {
        return;
    }

    if (MY_LOG_CURRENT_LEVEL < MY_LOG_LEVEL_DEBUG)
    {
        return;
    }

    /* 获取互斥锁，保证线程安全 */
    if (sLogMutex != NULL)
    {
        if (xSemaphoreTake(sLogMutex, portMAX_DELAY) != pdTRUE)
        {
            return;
        }
    }

    /* 打印标题 */
    header_len = snprintf(sLogDumpBuffer, sizeof(sLogDumpBuffer), "[DBG] %s (%d bytes):\r\n", tag, len);
#if LOG_USE_RTT
    rtt_logger_write(sLogDumpBuffer, header_len);
#elif LOG_USE_UART
    uart_logger_write(sLogDumpBuffer, header_len);
#endif

    /* 打印Hex数据 */
    while (offset < len)
    {
        int line_len = 0;

        /* 打印偏移量 */
        line_len = snprintf(sLogDumpBuffer, sizeof(sLogDumpBuffer), "%04d: ", offset);

        /* 打印Hex */
        for (uint32_t i = 0; i < 16 && offset + i < len; i++)
        {
            line_len += snprintf(sLogDumpBuffer + line_len, sizeof(sLogDumpBuffer) - line_len,
                                "%02X ", bytes[offset + i]);
        }

        /* 打印ASCII */
        line_len += snprintf(sLogDumpBuffer + line_len, sizeof(sLogDumpBuffer) - line_len, "  ");
        for (uint32_t i = 0; i < 16 && offset + i < len; i++)
        {
            uint8_t c = bytes[offset + i];
            line_len += snprintf(sLogDumpBuffer + line_len, sizeof(sLogDumpBuffer) - line_len,
                                "%c", (c >= 0x20 && c <= 0x7E) ? c : '.');
        }

        line_len += snprintf(sLogDumpBuffer + line_len, sizeof(sLogDumpBuffer) - line_len, "\r\n");

#if LOG_USE_RTT
        rtt_logger_write(sLogDumpBuffer, line_len);
#elif LOG_USE_UART
        uart_logger_write(sLogDumpBuffer, line_len);
#endif

        offset += 16;
    }

    /* 更新统计信息（已在互斥锁保护范围内） */
    sLogStats.dump_count++;

    /* 释放互斥锁 */
    if (sLogMutex != NULL)
    {
        xSemaphoreGive(sLogMutex);
    }
}

/*********************************************************************
 * @brief   获取日志统计信息
 * @param   stats 统计信息输出指针
 * @return  none
 * @note    指针使用前已判空，使用临界区保护统计变量读取
 *********************************************************************/
void my_log_get_stats(log_stats_t *stats)
{
    if (stats != NULL)
    {
        /* 使用临界区保护统计变量读取 */
        taskENTER_CRITICAL();
        memcpy(stats, &sLogStats, sizeof(sLogStats));
        taskEXIT_CRITICAL();
    }
}

/*********************************************************************
 * @brief   打印日志统计信息
 * @param   none
 * @return  none
 * @note    用于调试和监控日志系统运行状态
 *********************************************************************/
void my_log_print_stats(void)
{
    MY_LOG_I("=== Log Statistics ===");
    MY_LOG_I("Print count: %d", sLogStats.print_count);
    MY_LOG_I("Dump count: %d", sLogStats.dump_count);
    MY_LOG_I("Overflow count: %d", sLogStats.overflow_count);
    MY_LOG_I("=====================");
}

/*********************************************************************
 * @brief   无锁关键日志输出（用于HardFault、ISR等无法使用互斥锁的场景）
 * @param   fmt 格式化字符串
 * @param   ... 可变参数
 * @return  none
 * @note    跳过互斥锁，直接使用静态缓冲区输出到RTT/UART
 *          警告：仅用于无法获取锁的特殊场景，不可在正常流程中使用
 *          注意：调用前一般已发生系统崩溃，需先调用 my_log_critical_init() 初始化底层输出通道
 *          缓冲区大小：128字节，单条日志超过会被截断（但不会丢失，每条日志独立输出）
 *********************************************************************/
void my_log_critical_print(const char *fmt, ...)
{
    va_list args;
    int len;
    static char s_critical_buf[128];  /* 专用关键缓冲区，避免与普通日志冲突 */

    /* 添加关键日志头 */
    len = snprintf(s_critical_buf, sizeof(s_critical_buf), "[CRITICAL] ");
    if (len < 0 || len >= sizeof(s_critical_buf))
    {
        len = 0;
    }

    /* 格式化用户日志 */
    va_start(args, fmt);
    len += vsnprintf(s_critical_buf + len, sizeof(s_critical_buf) - len, fmt, args);
    va_end(args);

    /* 检查溢出：若超过缓冲区，添加截断警告 */
    if (len >= sizeof(s_critical_buf))
    {
        len = sizeof(s_critical_buf) - 1;
        /* 在末尾添加截断标记（覆盖最后几个字符） */
        if (len >= 3)
        {
            s_critical_buf[len - 3] = '.';
            s_critical_buf[len - 2] = '.';
            s_critical_buf[len - 1] = '.';
        }
    }

    /* 添加换行符 */
    if (len + 1 < sizeof(s_critical_buf))
    {
        s_critical_buf[len++] = '\n';
        s_critical_buf[len] = '\0';
    }

    /* 直接输出到底层，跳过互斥锁 */
#if LOG_USE_RTT
    rtt_logger_write(s_critical_buf, len);
#elif LOG_USE_UART
    uart_logger_write(s_critical_buf, len);
#endif
}
