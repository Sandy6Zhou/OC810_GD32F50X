# RTT日志与串口日志方案

**设计者**: 伍玉蛟 (wuyujiao@jimiiot.com)
**创建日期**: 2026-04-16
**更新日期**: 2026-04-17
**适用项目**: GD32F505VGT7 mDVR项目
**当前状态**: ✅ 已完整实现

---

## 一、方案概述

支持 **RTT 日志** 和 **串口日志** 两种输出方式，编译时通过宏定义切换。

| 特性 | RTT 日志 | 串口日志 |
|------|---------|---------|
| **输出速度** | 极快（~10MB/s） | 较慢（115200bps ≈ 11KB/s） |
| **CPU占用** | 极低 | 中等（轮询发送） |
| **硬件依赖** | 需要 J-Link 调试器 | 需要串口线 |
| **适用场景** | 开发调试阶段 | 现场部署/无调试器 |
| **代码占用** | ~3KB Flash | ~2KB Flash |

---

## 二、文件结构

```
project/OC810/code/log/
├── my_log_config.h          # 日志配置文件
├── my_log.h                 # 日志接口头文件
├── my_log.c                 # 日志实现文件
├── rtt_logger.h             # RTT日志接口
├── rtt_logger.c             # RTT日志实现
├── uart_logger.h            # 串口日志接口
└── uart_logger.c            # 串口日志实现
```

---

## 三、配置说明

### 3.1 日志配置 (my_log_config.h)

```c
/* 日志输出方式选择（二选一） */
#define LOG_USE_RTT         1   // 使用 Segger RTT（开发调试推荐）
#define LOG_USE_UART        0   // 使用 USART0（工厂测试/现场部署）

/* 日志级别 */
#define MY_LOG_LEVEL_NONE      0   // 关闭所有日志
#define MY_LOG_LEVEL_ERROR     1   // 错误级别
#define MY_LOG_LEVEL_WARN      2   // 警告级别
#define MY_LOG_LEVEL_INFO      3   // 信息级别
#define MY_LOG_LEVEL_DEBUG     4   // 调试级别

/* 根据编译模式设置默认日志级别 */
#ifndef MY_LOG_CURRENT_LEVEL
    #ifdef DEBUG
        #define MY_LOG_CURRENT_LEVEL   MY_LOG_LEVEL_DEBUG
    #else
        #define MY_LOG_CURRENT_LEVEL   MY_LOG_LEVEL_INFO
    #endif
#endif
```

### 3.2 配置切换

**开发调试**（RTT + 全部日志）:
```c
#define LOG_USE_RTT         1
#define LOG_USE_UART        0
#define MY_LOG_CURRENT_LEVEL   MY_LOG_LEVEL_DEBUG
```

**工厂测试**（串口 + INFO及以上）:
```c
#define LOG_USE_RTT         0
#define LOG_USE_UART        1
#define MY_LOG_CURRENT_LEVEL   MY_LOG_LEVEL_INFO
```

**量产版本**（关闭日志）:
```c
#define LOG_USE_RTT         0
#define LOG_USE_UART        0
#define MY_LOG_CURRENT_LEVEL   MY_LOG_LEVEL_NONE
```

---

## 四、使用示例

### 4.1 初始化

```c
#include "my_log.h"

int main(void)
{
    /* 初始化日志系统 */
    my_log_init();

    MY_LOG_I("System starting...");
    MY_LOG_I("System Core Clock: %d Hz", SystemCoreClock);
}
```

### 4.2 日志打印

```c
void task_example(void *pvParameters)
{
    MY_LOG_I("Task started");
    MY_LOG_D("Debug info: %d", value);
    MY_LOG_W("Warning: %s", msg);
    MY_LOG_E("Error code: %d", error);

    /* 二进制数据Dump */
    MY_LOG_DUMP("RX Data", buffer, length);
}
```

### 4.3 日志统计

```c
void task_monitor(void *pvParameters)
{
    while (1)
    {
        my_log_print_stats();  // 打印日志统计
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
```

---

## 五、日志接口

### 5.1 日志宏

| 宏 | 级别 | 说明 |
|----|------|------|
| `MY_LOG_D(fmt, ...)` | DEBUG | 调试信息 |
| `MY_LOG_I(fmt, ...)` | INFO | 一般信息 |
| `MY_LOG_W(fmt, ...)` | WARN | 警告信息 |
| `MY_LOG_E(fmt, ...)` | ERROR | 错误信息 |
| `MY_LOG_DUMP(tag, data, len)` | DEBUG | 二进制数据Dump |

### 5.2 日志格式

```
[级别] 文件名:函数名:行号 内容
示例：[INF] main.c:main:45 System starting...
```

### 5.3 函数声明

```c
void my_log_init(void);
void my_log_print(int level, const char *level_str, const char *file, const char *function, int line, const char *fmt, ...);
void my_log_dump(const char *tag, const void *data, uint32_t len);
void my_log_get_stats(log_stats_t *stats);
void my_log_print_stats(void);
```

---

## 六、实现特性

### 6.1 线程安全

使用 FreeRTOS 互斥锁保护，多任务安全调用：
```c
static SemaphoreHandle_t sLogMutex = NULL;
```

### 6.2 静态缓冲区

避免栈溢出：
```c
static char sLogPrintBuffer[384];  // 日志打印缓冲区
static char sLogDumpBuffer[128];   // Dump缓冲区
```

### 6.3 自动提取文件名

自动去掉路径，仅保留文件名：
```c
/* main.c:vTaskFunction:45 */
```

### 6.4 日志统计

```c
typedef struct {
    uint32_t print_count;      // 打印次数
    uint32_t dump_count;       // Dump次数
    uint32_t overflow_count;   // 缓冲区溢出次数
} log_stats_t;
```

---

## 七、底层实现

### 7.1 RTT 日志 (rtt_logger.c)

```c
void rtt_logger_init(void)
{
    SEGGER_RTT_Init();
}

int rtt_logger_write(const char *data, int len)
{
    return SEGGER_RTT_Write(0, data, len);
}
```

### 7.2 串口日志 (uart_logger.c)

- **引脚**: PA9 (USART0_TX)
- **波特率**: 115200
- **方式**: 轮询发送（阻塞）
- **功能**: 仅发送，不接收

```c
void uart_logger_init(void)
{
    /* 配置 PA9 为 USART0_TX，115200波特率 */
}

int uart_logger_write(const char *data, int len)
{
    /* 轮询发送数据 */
}
```

---

## 八、Keil 工程配置

### 8.1 添加文件

**LOG组**:
- my_log.c
- rtt_logger.c
- uart_logger.c
- SEGGER_RTT.c (Third_Party/Segger_RTT/RTT/RTT/)
- SEGGER_RTT_printf.c (Third_Party/Segger_RTT/RTT/RTT/)

### 8.2 Include Paths

```
..\code\log
..\..\Third_Party\Segger_RTT\RTT\RTT
```

---

## 九、资源占用

| 配置 | Flash | RAM | CPU |
|------|-------|-----|-----|
| **RTT 日志（DEBUG）** | ~6KB | ~1.5KB | <1% |
| **串口日志（DEBUG）** | ~5KB | ~1.5KB | 3-5% |
| **关闭日志** | 0KB | 0KB | 0% |

**实际编译数据**（基础版含日志模块）:
- Code-Flash: 11.23KB (8.8%)
- SRAM: 49.42KB (25.4%)

---

## 十、注意事项

### 10.1 RTT 日志

- 需要 J-Link 调试器
- 使用 J-Link RTT Viewer 查看
- 不要在中断中调用

### 10.2 串口日志

- 使用 USART0，PA9 引脚
- 轮询方式发送，阻塞等待
- 大量日志可能影响实时性

### 10.3 通用

- 日志字符串使用 ROM 存储
- 高频调用函数中减少日志输出
- 避免复杂格式化

---

## 附录：RTT Viewer 使用

1. 连接 J-Link 调试器
2. 打开 J-Link RTT Viewer
3. 选择目标芯片 GD32F505VGT7
4. 连接到目标板

或命令行：
```bash
JLinkRTTClient.exe
```

---

**文档版本**: V1.1
**最后更新**: 2026-04-17
