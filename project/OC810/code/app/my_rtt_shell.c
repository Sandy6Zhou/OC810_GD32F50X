/*********************************************************************
**版权所有：         深圳市几米物联有限公司
**文件名称：        my_rtt_shell.c
**文件描述：        RTT交互式命令行Shell核心实现
**当前版本：        V1.0
**作    者：        Harrison Wu (wuyujiao@jimiiot.com)
**完成日期：        2026.06.02
*********************************************************************
** 功能描述：       1. 基于Segger RTT Channel 0 的交互式命令行
**                 2. 行编辑（退格支持）+ 命令解析（argc/argv）
**                 3. 可扩展命令注册表
**                 4. 内置 help / sys reset / sys status 命令
*********************************************************************/
#include "my_comm.h"
#include "SEGGER_RTT.h"
#include <stdarg.h>

/*===========================================================================
 *  模块私有常量
 *===========================================================================*/

#define SHELL_CMD_TABLE_MAX     (16)    /**< 最大注册命令数 */
#define SHELL_OUTPUT_BUF_SIZE   (256)   /**< 输出格式化缓冲区 */
#define SHELL_RTT_CH            (0)     /**< RTT通道号 */
#define SHELL_MSG_RECV_TIMEOUT  (50)    /**< 消息队列接收超时（毫秒，短阻塞） */

/*===========================================================================
 *  模块私有变量
 *===========================================================================*/

/* 命令表 */
static const shell_cmd_t *s_cmd_table[SHELL_CMD_TABLE_MAX];
static uint32_t s_cmd_count = 0;

/* 行编辑缓冲区 */
static char s_line_buf[SHELL_LINE_MAX];
static uint32_t s_line_pos = 0;

/* 系统启动时间记录 */
static uint32_t s_boot_tick = 0;

/*===========================================================================
 *  内部辅助函数
 *===========================================================================*/

/*********************************************************************
 * @brief   向RTT输出字符串
 * @param   str 字符串指针（NULL安全）
 * @return  none
 *********************************************************************/
static void rtt_puts(const char *str)
{
    if (str != NULL)
    {
        SEGGER_RTT_WriteString(SHELL_RTT_CH, str);
    }
}

/*********************************************************************
 * @brief   向RTT输出指定长度的数据
 * @param   data 数据指针
 * @param   len  数据长度
 * @return  none
 *********************************************************************/
static void rtt_write(const char *data, uint32_t len)
{
    SEGGER_RTT_Write(SHELL_RTT_CH, data, len);
}

/*********************************************************************
 * @brief   输出Shell提示符
 * @return  none
 *********************************************************************/
static void shell_show_prompt(void)
{
    rtt_puts(SHELL_PROMPT);
}

/*********************************************************************
 * @brief   命令解析：将行缓冲区拆分为argc/argv
 * @param   line     输入行（会被修改，空格替换为'\0'）
 * @param   argv     参数指针数组（输出）
 * @param   max_argc 最大参数数
 * @return  实际参数个数
 * @note    原地修改，不额外分配内存
 *********************************************************************/
static int shell_parse_line(char *line, char *argv[], int max_argc)
{
    int argc = 0;
    char *p = line;

    while (*p != '\0' && argc < max_argc)
    {
        /* 跳过前导空格 */
        while (*p == ' ' || *p == '\t')
        {
            p++;
        }

        if (*p == '\0')
        {
            break;
        }

        /* 记录参数起始位置 */
        argv[argc++] = p;

        /* 找到参数结尾 */
        while (*p != '\0' && *p != ' ' && *p != '\t')
        {
            p++;
        }

        /* 以'\0'截断 */
        if (*p != '\0')
        {
            *p++ = '\0';
        }
    }

    return argc;
}

/*********************************************************************
 * @brief   查找并执行命令
 * @param   argc 参数个数
 * @param   argv 参数数组
 * @return  none
 * @note    命令未找到时输出提示信息
 *********************************************************************/
static void shell_execute(int argc, char *argv[])
{
    uint32_t i;

    if (argc <= 0)
    {
        return;
    }

    /* 遍历命令表查找匹配 */
    for (i = 0; i < s_cmd_count; i++)
    {
        if (strcmp(argv[0], s_cmd_table[i]->name) == 0)
        {
            int ret = s_cmd_table[i]->handler(argc, argv);

            if (ret != 0)
            {
                shell_printf("Command '%s' returned error: %d\r\n", argv[0], ret);
            }

            return;
        }
    }

    shell_printf("Unknown command: '%s'. Type 'help' for available commands.\r\n", argv[0]);
}

/*********************************************************************
 * @brief   处理一行完整输入
 * @return  none
 * @note    解析→执行→重置缓冲区→重新显示提示符
 *********************************************************************/
static void shell_process_line(void)
{
    char *argv[SHELL_ARGC_MAX];
    int argc;

    rtt_puts("\r\n");  /* 换行回显 */

    /* 空行直接显示提示符 */
    if (s_line_pos == 0)
    {
        shell_show_prompt();
        return;
    }

    /* 确保以'\0'结尾 */
    s_line_buf[s_line_pos] = '\0';

    /* 解析命令 */
    argc = shell_parse_line(s_line_buf, argv, SHELL_ARGC_MAX);

    /* 执行命令 */
    if (argc > 0)
    {
        shell_execute(argc, argv);
    }

    /* 重置行缓冲区 */
    s_line_pos = 0;
    memset(s_line_buf, 0, sizeof(s_line_buf));

    shell_show_prompt();
}

/*********************************************************************
 * @brief   处理单个输入字符（行编辑）
 * @param   ch 输入字符
 * @return  none
 * @note    支持回车提交、退格删除、可打印字符追加
 *********************************************************************/
static void shell_process_char(char ch)
{
    if (ch == '\r' || ch == '\n')
    {
        /* 回车/换行：提交命令 */
        shell_process_line();
    }
    else if (ch == '\b' || ch == 0x7F)
    {
        /* 退格：删除最后一个字符 */
        if (s_line_pos > 0)
        {
            s_line_pos--;
            s_line_buf[s_line_pos] = '\0';
            /* 回显退格序列：BS + Space + BS */
            rtt_write("\b \b", 3);
        }
    }
    else if (ch >= 0x20 && ch < 0x7F)
    {
        /* 可打印字符：追加到缓冲区 */
        if (s_line_pos < (SHELL_LINE_MAX - 1))
        {
            s_line_buf[s_line_pos++] = ch;
            /* 回显字符 */
            rtt_write(&ch, 1);
        }
    }
    /* 忽略其他控制字符 */
}

/*===========================================================================
 *  内置命令实现
 *===========================================================================*/

/*********************************************************************
 * @brief   help 命令：列出所有已注册命令及帮助信息
 * @param   argc 参数个数（未使用）
 * @param   argv 参数数组（未使用）
 * @return  0: 成功
 *********************************************************************/
static int cmd_help(int argc, char *argv[])
{
    uint32_t i;

    (void)argc;
    (void)argv;

    shell_print("\r\n");
    shell_print("========================================\r\n");
    shell_print("  RTT Shell - Available Commands\r\n");
    shell_print("========================================\r\n");

    for (i = 0; i < s_cmd_count; i++)
    {
        shell_printf("  %-16s %s\r\n", s_cmd_table[i]->name, s_cmd_table[i]->help);
    }

    shell_print("========================================\r\n");

    return 0;
}

/*********************************************************************
 * @brief   sys reset 子命令：系统复位
 * @param   argc 参数个数
 * @param   argv 参数数组（argv[2] = 延时毫秒数）
 * @return  0: 成功
 * @note    用法: sys reset [delay_ms]，默认500ms
 *********************************************************************/
static int cmd_sys_reset(int argc, char *argv[])
{
    uint32_t delay_ms = 500;

    if (argc >= 3)
    {
        delay_ms = (uint32_t)atoi(argv[2]);
    }

    shell_printf("System resetting in %d ms...\r\n", delay_ms);

    my_system_reset(delay_ms);

    /* 不会执行到这里 */
    return 0;
}

/*********************************************************************
 * @brief   sys status 子命令：系统状态查询
 * @param   argc 参数个数（未使用）
 * @param   argv 参数数组（未使用）
 * @return  0: 成功
 * @note    输出：内存统计、任务列表、系统运行时间
 *********************************************************************/
static int cmd_sys_status(int argc, char *argv[])
{
    size_t free_heap, min_heap;
    uint8_t usage_pct;
    uint32_t uptime_ms;
    uint32_t uptime_s, uptime_m, uptime_h;
    TaskStatus_t task_status_array[16];
    uint32_t task_count;
    uint32_t i;
    uint32_t ticks_now;

    (void)argc;
    (void)argv;

    /* === 系统运行时间 === */
    ticks_now = my_os_get_tick();
    uptime_ms = my_ticks_to_ms(ticks_now - s_boot_tick);
    uptime_s = uptime_ms / 1000;
    uptime_m = uptime_s / 60;
    uptime_h = uptime_m / 60;

    shell_print("\r\n");
    shell_print("========================================\r\n");
    shell_print("  System Status\r\n");
    shell_print("========================================\r\n");

    shell_printf("  Uptime:      %02d:%02d:%02d (%d ms)\r\n",
                 uptime_h, uptime_m % 60, uptime_s % 60, uptime_ms);
    shell_printf("  Tick Rate:   %d Hz\r\n", (int)configTICK_RATE_HZ);
    shell_printf("  Tick Count:  %d\r\n", (int)ticks_now);

    /* === 内存统计 === */
    my_safe_memory_get_stats(&free_heap, &min_heap, &usage_pct);

    shell_print("\r\n");
    shell_print("  --- Memory ---\r\n");
    shell_printf("  Heap Total:  %d bytes (%.1f KB)\r\n",
                 (int)configTOTAL_HEAP_SIZE,
                 (float)configTOTAL_HEAP_SIZE / 1024.0f);
    shell_printf("  Heap Free:   %d bytes (%.1f KB)\r\n",
                 (int)free_heap, (float)free_heap / 1024.0f);
    shell_printf("  Heap Min:    %d bytes (%.1f KB)\r\n",
                 (int)min_heap, (float)min_heap / 1024.0f);
    shell_printf("  Heap Usage:  %d%%\r\n", usage_pct);

    /* === 任务列表 === */
    shell_print("\r\n");
    shell_print("  --- Tasks ---\r\n");
    shell_printf("  %-16s %-6s %-8s %-6s\r\n", "Name", "State", "Prio", "Stack");
    shell_print("  -------------------------------------------\r\n");

    task_count = uxTaskGetSystemState(task_status_array, 16, NULL);

    for (i = 0; i < task_count && i < 16; i++)
    {
        const char *state_str;

        switch (task_status_array[i].eCurrentState)
        {
            case eRunning:   state_str = "Run";  break;
            case eReady:     state_str = "Rdy";  break;
            case eBlocked:   state_str = "Blk";  break;
            case eSuspended: state_str = "Sus";  break;
            case eDeleted:   state_str = "Del";  break;
            default:         state_str = "?";    break;
        }

        shell_printf("  %-16s %-6s %-8d %-6d\r\n",
                     task_status_array[i].pcTaskName,
                     state_str,
                     (int)task_status_array[i].uxCurrentPriority,
                     (int)task_status_array[i].usStackHighWaterMark);
    }

    shell_printf("  Total tasks: %d\r\n", (int)task_count);
    shell_print("========================================\r\n");

    return 0;
}

/*********************************************************************
 * @brief   sys 命令路由：根据子命令分发到具体处理函数
 * @param   argc 参数个数
 * @param   argv 参数数组（argv[1] = 子命令名）
 * @return  0: 成功  -1: 参数错误
 * @note    用法: sys reset [delay_ms] / sys status
 *********************************************************************/
static int cmd_sys(int argc, char *argv[])
{
    if (argc < 2)
    {
        shell_print("Usage: sys <reset|status> [args...]\r\n");
        shell_print("  sys reset [delay_ms]  - System reset (default 500ms)\r\n");
        shell_print("  sys status            - Show system status\r\n");
        return -1;
    }

    if (strcmp(argv[1], "reset") == 0)
    {
        return cmd_sys_reset(argc, argv);
    }
    else if (strcmp(argv[1], "status") == 0)
    {
        return cmd_sys_status(argc, argv);
    }
    else
    {
        shell_printf("Unknown sub-command: '%s'\r\n", argv[1]);
        shell_print("Usage: sys <reset|status>\r\n");
        return -1;
    }
}

/*===========================================================================
 *  内置命令注册表
 *===========================================================================*/

static const shell_cmd_t s_builtin_cmds[] =
{
    { "help", "Show this help message",         cmd_help },
    { "sys",  "System commands (reset, status)", cmd_sys  },
};

#define BUILTIN_CMD_COUNT   (sizeof(s_builtin_cmds) / sizeof(s_builtin_cmds[0]))

/*===========================================================================
 *  Shell任务
 *===========================================================================*/

/*********************************************************************
 * @brief   Shell系统消息处理函数
 * @param   msg 消息指针
 * @return  none
 * @note    处理系统级状态切换消息，SLEEP/SHUTDOWN后任务将被挂起
 *********************************************************************/
static void shell_handle_sys_msg(const my_msg_t *msg)
{
    switch (msg->id)
    {
        case MY_MSG_ID_SYS_SLEEP:
            TASK_STATE_RTT_SHELL = TASK_STATE_SLEEP;
            shell_print("\r\n[SHELL] Entering sleep mode...\r\n");

            my_task_delay_ms(50);    /**< 短阻塞50ms，等待日志输出 */
            my_task_suspend(NULL);      /**< 挂起，等待外部resume恢复 */
            break;

        case MY_MSG_ID_SYS_SHUTDOWN:
            TASK_STATE_RTT_SHELL = TASK_STATE_SHUTDOWN;
            shell_print("\r\n[SHELL] Shutting down...\r\n");

            my_task_delay_ms(50);    /**< 短阻塞50ms，等待日志输出 */
            my_task_suspend(NULL);      /**< 挂起，等待外部resume恢复 */
            break;

        case MY_MSG_ID_SYS_ACTIVE:
            TASK_STATE_RTT_SHELL = TASK_STATE_ACTIVE;
            shell_print("\r\n[SHELL] Waking up...\r\n");
            shell_show_prompt();
            break;

        case MY_MSG_ID_SYS_STATUS_REQ:
            shell_printf("State: %d\r\n", TASK_STATE_RTT_SHELL);
            break;

        default:
            break;
    }
}

/*********************************************************************
 * @brief   Shell任务初始化
 * @return  none
 * @note    设置Shell模块状态为ACTIVE
 *********************************************************************/
static void my_rtt_shell_task_init(void)
{
    TASK_STATE_RTT_SHELL = TASK_STATE_ACTIVE;
}

/*********************************************************************
 * @brief   Shell任务入口函数
 * @param   pvParameters 任务参数（未使用）
 * @return  none
 * @note    ACTIVE: 50ms间隔轮询消息+RTT输入 SLEEP/SHUTDOWN: 挂起等待外部resume
 *********************************************************************/
static void shell_task(void *pvParameters)
{
    char rx_buf[32];
    unsigned rx_count;
    unsigned i;
    my_msg_t msg;

    (void)pvParameters;

    /* 记录启动tick */
    s_boot_tick = my_os_get_tick();

    /* 注册内置命令 */
    for (i = 0; i < BUILTIN_CMD_COUNT; i++)
    {
        my_rtt_shell_register_cmd(&s_builtin_cmds[i]);
    }

    /* 显示Banner */
    shell_print("\r\n");
    shell_print("========================================\r\n");
    shell_print("  RTT Shell v1.0\r\n");
    shell_print("  Type 'help' for available commands\r\n");
    shell_print("========================================\r\n");
    shell_show_prompt();

    my_rtt_shell_task_init();

    while (1)
    {
        /* 短阻塞接收系统消息（有消息立即处理，无消息最多等10ms） */
        if (my_msg_recv(MSG_QUEUE_RTT_SHELL, &msg, SHELL_MSG_RECV_TIMEOUT) == 0)
        {
            shell_handle_sys_msg(&msg);
        }

        /* ACTIVE状态：正常RTT输入轮询 */
        rx_count = SEGGER_RTT_Read(SHELL_RTT_CH, rx_buf, sizeof(rx_buf));

        if (rx_count > 0)
        {
            for (i = 0; i < rx_count; i++)
            {
                shell_process_char(rx_buf[i]);
            }
        }
    }
}

/*===================================================================
 *  公开API实现
 *==================================================================*/

/*********************************************************************
 * @brief   Shell输出字符串（供命令处理函数使用）
 * @param   str 字符串指针
 * @return  none
 *********************************************************************/
void shell_print(const char *str)
{
    rtt_puts(str);
}

/*********************************************************************
 * @brief   Shell格式化输出（供命令处理函数使用）
 * @param   fmt 格式化字符串
 * @param   ... 可变参数
 * @return  none
 * @note    内部缓冲区256字节，超长会被截断
 *********************************************************************/
void shell_printf(const char *fmt, ...)
{
    char buf[SHELL_OUTPUT_BUF_SIZE];
    va_list args;
    int len;

    va_start(args, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len > 0)
    {
        rtt_write(buf, (uint32_t)len);
    }
}

/*********************************************************************
 * @brief   注册自定义命令到Shell
 * @param   cmd 命令结构体指针（需为静态/全局变量，Shell仅保存指针）
 * @return  0: 成功  -1: 参数无效  -2: 命令表已满  -3: 命令重复
 * @note    线程安全，可在任意时刻调用
 *********************************************************************/
int my_rtt_shell_register_cmd(const shell_cmd_t *cmd)
{
    uint32_t i;

    if (cmd == NULL || cmd->name == NULL || cmd->handler == NULL)
    {
        return -1;
    }

    if (s_cmd_count >= SHELL_CMD_TABLE_MAX)
    {
        MY_LOG_E("Command table full (%d/%d)", s_cmd_count, SHELL_CMD_TABLE_MAX);
        return -2;
    }

    /* 检查重复 */
    for (i = 0; i < s_cmd_count; i++)
    {
        if (strcmp(s_cmd_table[i]->name, cmd->name) == 0)
        {
            MY_LOG_W("Command '%s' already registered", cmd->name);
            return -3;
        }
    }

    s_cmd_table[s_cmd_count++] = cmd;

    return 0;
}

/*********************************************************************
 * @brief   初始化并启动RTT Shell任务
 * @return  0: 成功  -1: 失败
 * @note    创建FreeRTOS任务，开始轮询RTT输入；重复调用安全返回0
 *********************************************************************/
int my_rtt_shell_init(void)
{
    int32_t ret;

    /** 检查是否已初始化 */
    if (TASK_HANDLE_RTT_SHELL != NULL)
    {
        return 0;
    }

    /* 清空行缓冲区 */
    memset(s_line_buf, 0, sizeof(s_line_buf));
    s_line_pos = 0;
    s_cmd_count = 0;

    /* 创建消息队列（仅接收系统级命令，深度4） */
    MSG_QUEUE_RTT_SHELL = my_msg_queue_create(MY_SHELL_MSG_QUEUE_DEPTH, sizeof(my_msg_t));
    if (MSG_QUEUE_RTT_SHELL == NULL)
    {
        MY_LOG_E("Failed to create msg queue");
        return -1;
    }

    /* 创建Shell任务 */
    ret = my_task_create(&TASK_HANDLE_RTT_SHELL, "RTT_SHELL",
                          MY_SHELL_TASK_STACK_SIZE,
                          shell_task, NULL,
                          MY_SHELL_TASK_PRIO);

    /* 检查任务创建结果 */
    if (ret != 0 || TASK_HANDLE_RTT_SHELL == NULL)
    {
        MY_LOG_E("Failed to create task(%d)", ret);
        return -1;
    }

    MY_LOG_I("Init OK, task created");

    return 0;
}

