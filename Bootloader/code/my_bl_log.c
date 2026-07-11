/********************************************************************
**版权所有：       深圳市几米物联有限公司
**文件名称：       my_bl_log_print.c
**文件描述：       Bootloader日志输出实现文件
**当前版本：       V1.0
**作    者：       Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：       2026.06.22
*********************************************************************
** 功能描述：       1. RTT日志（SEGGER_RTT_Write）带时间戳
**                 2. UART日志（格式化输出）带时间戳
**                 3. hex dump输出（OTA调试用）
*********************************************************************/

#include "my_bl.h"

#if (BL_RTT_LOG_ENABLE == 1U)
#include "SEGGER_RTT.h"
#endif

/*********************************************************************
 * 内部缓冲区
 *********************************************************************/
static char s_bl_LogPrintBuffer[512U];
static char s_bl_LogDumpBuffer[1024U];

/*********************************************************************
 * 内部辅助函数
 *********************************************************************/

/*********************************************************************
 * @brief   输出十六进制数据 dump（Hex + ASCII）
 * @param   tag    标签字符串
 * @param   pdata  数据缓冲区指针
 * @param   len    数据长度（字节）
 * @return  None
 * @note    仅当当前日志级别 >= INFO 时输出
 *********************************************************************/
void my_bl_log_dump(char *tag, const void *pdata, uint32_t len)
{
    const uint8_t *bytes = (const uint8_t *)pdata;
    uint32_t offset = 0;
    int header_len = 0;
    uint32_t i;
    char c;

    /* 指针判空 */
    if (pdata == NULL || len == 0)
    {
        return;
    }

    if (MY_BL_LOG_CURRENT_LEVEL < MY_BL_LOG_LEVEL_INFO)
    {
        return;
    }

    /* 打印标题 */
    header_len = snprintf(s_bl_LogDumpBuffer, sizeof(s_bl_LogDumpBuffer), "%s (%d bytes):\r\n", tag, len);
#if (BL_RTT_LOG_ENABLE == 1U)
    SEGGER_RTT_Write(0U, s_bl_LogDumpBuffer, header_len);
#endif

    /* 打印Hex数据 */
    while (offset < len)
    {
        int line_len = 0;

        /* 打印偏移量 */
        line_len = snprintf(s_bl_LogDumpBuffer, sizeof(s_bl_LogDumpBuffer), "%04X: ", offset);

        /* 打印Hex */
        for (i = 0U; i < 16U && offset + i < len; i++)
        {
            line_len += snprintf(s_bl_LogDumpBuffer + line_len, sizeof(s_bl_LogDumpBuffer) - line_len,
                                "%02X ", bytes[offset + i]);
        }

        /* 打印ASCII */
        line_len += snprintf(s_bl_LogDumpBuffer + line_len, sizeof(s_bl_LogDumpBuffer) - line_len, "  ");
        for (i = 0U; i < 16U && offset + i < len; i++)
        {
            c = bytes[offset + i];
            line_len += snprintf(s_bl_LogDumpBuffer + line_len, sizeof(s_bl_LogDumpBuffer) - line_len,
                                "%c", (c >= 0x20 && c <= 0x7E) ? c : '.');
        }

        line_len += snprintf(s_bl_LogDumpBuffer + line_len, sizeof(s_bl_LogDumpBuffer) - line_len, "\r\n");

#if (BL_RTT_LOG_ENABLE == 1U)
        SEGGER_RTT_Write(0U, s_bl_LogDumpBuffer, line_len);
#endif

        offset += 16U;
    }
}

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
void my_bl_log_print(int level, const char *level_str, const char *function, const char *fmt, ...)
{
    va_list args;
    int len = 0;

    if (level > MY_BL_LOG_CURRENT_LEVEL)
    {
        return;  /* 低于当前级别，不输出 */
    }

    va_start(args, fmt);

    /* 生成日志头：[级别] 函数名 */
    if (function != NULL)
    {
        len = snprintf(s_bl_LogPrintBuffer, sizeof(s_bl_LogPrintBuffer), "[%s] %s ", level_str, function);
    }
    else
    {
        len = snprintf(s_bl_LogPrintBuffer, sizeof(s_bl_LogPrintBuffer), "[%s] ", level_str);
    }

    if (len < 0 || len >= sizeof(s_bl_LogPrintBuffer))
    {
        va_end(args);
        return;  /* 格式化失败或溢出 */
    }

    len += vsnprintf(s_bl_LogPrintBuffer + len, sizeof(s_bl_LogPrintBuffer) - len, fmt, args);

    /* 添加换行符 */
    if (len < sizeof(s_bl_LogPrintBuffer) - 2)
    {
        s_bl_LogPrintBuffer[len++] = '\r';
        s_bl_LogPrintBuffer[len++] = '\n';
        s_bl_LogPrintBuffer[len] = '\0';
    }
    else
    {
        len = sizeof(s_bl_LogPrintBuffer) - 3;
        s_bl_LogPrintBuffer[len++] = '\r';
        s_bl_LogPrintBuffer[len++] = '\n';
        s_bl_LogPrintBuffer[len] = '\0';
    }

    va_end(args);

    /* 输出日志 */
#if (BL_RTT_LOG_ENABLE == 1U)
    SEGGER_RTT_Write(0U, s_bl_LogPrintBuffer, len);
#endif
}

/*********************************************************************
 * @brief   初始化日志系统
 * @param   None
 * @return  None
 * @note    根据配置初始化 RTT
 *********************************************************************/
void my_bl_log_init(void)
{
#if (BL_RTT_LOG_ENABLE == 1U)
    SEGGER_RTT_Init();
#endif
}