# GD32F505 FreeRTOS 移植教程

**版本**: V1.1
**日期**: 2026-04-17
**适用对象**: 嵌入式开发人员
**难度**: 中级

---

## 📖 教程概述

本教程介绍如何将FreeRTOS从GD32E503Z参考项目移植到GD32F505V开发板。

**你将学习**:
- ✅ FreeRTOS移植的完整流程
- ✅ GD32系列芯片差异处理
- ✅ Keil工程配置方法
- ✅ 常见问题排查技巧

---

## 📋 目录

1. [芯片对比](#1-芯片对比)
2. [移植前准备](#2-移植前准备)
3. [移植步骤](#3-移植步骤)
4. [Keil工程配置](#4-keil工程配置)
5. [编译与验证](#5-编译与验证)
6. [常见问题](#6-常见问题)

---

## 1. 芯片对比

| 参数 | GD32E503Z (参考) | GD32F505V (目标) | 影响 |
|------|------------------|------------------|------|
| **内核** | Cortex-M33 | Cortex-M33 | ✅ 无影响 |
| **主频** | 252MHz | 280MHz | ⚠️ 需配置时钟 |
| **Flash** | 256KB | 1024KB | ✅ 更充足 |
| **SRAM** | 96KB | 192KB | ✅ 增加 |

**好消息**:
- ✅ 内核相同，端口层可直接复用
- ✅ 固件库架构相同
- ✅ 参考项目完整

---

## 2. 移植前准备

### 2.1 目录结构

```
mDVR_MCU/
├── Docs/                    ← 项目文档
├── Library/                 ← GD32固件库
├── Third_Party/             ← 第三方库
│   ├── FreeRTOSv10.3.1/
│   └── Segger_RTT/
└── project/
    └── OC810/               ← OC810项目
        ├── code/
        │   ├── system/      ← 系统核心代码
        │   ├── log/         ← 日志模块
        │   └── memory/      ← 内存管理模块
        └── MDK-ARM/         ← Keil工程
```

### 2.2 Memory规格（重要！）

```
Flash总量: 1024KB (1MB)
  ├─ Code-Flash: 128KB (0x08000000 ~ 0x0801FFFF) ← 零等待
  └─ Data-Flash: 896KB (0x08020000 ~ 0x080FFFFF)

SRAM: 192KB (0x20000000 ~ 0x2002FFFF)
```

**⚠️ 常见误区**:
- ❌ 错误：SRAM=128KB → ✅ 正确：SRAM=192KB
- ❌ 错误：Code-Flash=192KB → ✅ 正确：Code-Flash=128KB

---

## 3. 移植步骤

### 3.1 第一步：复制FreeRTOS源码

**关键文件**:
```
Third_Party/FreeRTOSv10.3.1/FreeRTOS/Source/
├── include/              ← FreeRTOS头文件
├── portable/
│   ├── GCC/ARM_CM33_NTZ/non_secure/  ← Cortex-M33端口层
│   │   ├── port.c
│   │   └── portasm.c
│   └── MemMang/
│       └── heap_4.c      ← 内存管理
├── tasks.c
├── queue.c
└── list.c
```

**关键点**: 使用`ARM_CM33_NTZ/non_secure`端口层（NTZ=Non-TrustZone）

### 3.2 第二步：配置FreeRTOSConfig.h

```c
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include "gd32f50x.h"

// CPU时钟
#define configCPU_CLOCK_HZ              SystemCoreClock  // 280MHz
#define configTICK_RATE_HZ              ((TickType_t)1000)  // 1ms

// 任务配置
#define configMAX_PRIORITIES            ((UBaseType_t)8)
#define configMINIMAL_STACK_SIZE        ((unsigned short)128)
#define configTOTAL_HEAP_SIZE           ((size_t)(48 * 1024))  // 48KB（量产）

// 功能使能
#define configUSE_PREEMPTION            1
#define configENABLE_FPU                1  // GD32F505有FPU
#define configENABLE_TRUSTZONE          0

// 中断配置
#define configPRIO_BITS                 4
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 2

// 钩子函数
#define configUSE_MALLOC_FAILED_HOOK    1
#define configCHECK_FOR_STACK_OVERFLOW  2

// 软件定时器
#define configUSE_TIMERS                1
#define configTIMER_TASK_PRIORITY       3
#define configTIMER_QUEUE_LENGTH        20
#define configTIMER_TASK_STACK_DEPTH    (configMINIMAL_STACK_SIZE * 2)

#endif
```

### 3.3 第三步：修改中断服务程序

**gd32f50x_it.c**:

```c
#include "gd32f50x_it.h"
#include "FreeRTOS.h"
#include "task.h"

/* 注意：以下三个Handler由FreeRTOS port层实现，不要重复定义！ */
// void SVC_Handler(void)          // FreeRTOS接管
// void PendSV_Handler(void)       // FreeRTOS接管
// void SysTick_Handler(void)      // FreeRTOS接管
```

### 3.4 第四步：配置系统时钟

**system_gd32f50x.c**:

```c
/* 选择280MHz配置 */
#define __SYSTEM_CLOCK_280M_PLL_HXTAL     // ← 启用
// #define __SYSTEM_CLOCK_252M_PLL_HXTAL  // ← 注释
```

**时钟树**:
```
外部晶振 (HXTAL) 8MHz
  ↓
PLL (×70, ÷2)
  ↓
系统时钟 280MHz
  ↓
AHB总线 140MHz (÷2)
APB1总线 70MHz (÷4)
APB2总线 140MHz (÷2)
```

### 3.5 第五步：编写main.c

```c
#include "gd32f50x.h"
#include "FreeRTOS.h"
#include "task.h"
#include "my_log.h"

/* 任务优先级 */
#define INIT_TASK_PRIORITY      (tskIDLE_PRIORITY + 1)
#define LED_TASK_PRIORITY       (tskIDLE_PRIORITY + 2)

/* 函数声明 */
static void init_task(void *pvParameters);
static void led_task(void *pvParameters);

int main(void)
{
    /* 创建初始化任务 */
    xTaskCreate(init_task, "INIT", 256, NULL, INIT_TASK_PRIORITY, NULL);

    /* 启动FreeRTOS */
    vTaskStartScheduler();

    /* 如果执行到这里，说明启动失败 */
    while (1) { }
}

/* 初始化任务 */
static void init_task(void *pvParameters)
{
    /* 初始化日志 */
    my_log_init();

    MY_LOG_I("System starting...");
    MY_LOG_I("System Core Clock: %d Hz", SystemCoreClock);
    MY_LOG_I("FreeRTOS Heap Size: %d bytes", configTOTAL_HEAP_SIZE);

    /* 创建LED任务 */
    xTaskCreate(led_task, "LED", 128, NULL, LED_TASK_PRIORITY, NULL);

    /* 删除初始化任务 */
    vTaskDelete(NULL);
}

/* LED闪烁任务 */
static void led_task(void *pvParameters)
{
    TickType_t xLastWakeTime;
    const TickType_t xFrequency = pdMS_TO_TICKS(500);  // 500ms

    xLastWakeTime = xTaskGetTickCount();

    MY_LOG_I("LED Task Started!");

    while (1)
    {
        /* 翻转LED1 */
        gd_eval_led_toggle(LED1);

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}
```

---

## 4. Keil工程配置

### 4.1 Target配置

**Options for Target (Alt+F7)**:

```
IROM1:
  Start: 0x08000000
  Size:  0x100000  ← 1024KB (1MB)

IRAM1:
  Start: 0x20000000
  Size:  0x30000   ← 192KB ✅
```

**⚠️ 重要**: IRAM1 Size必须设置为**0x30000** (192KB)

### 4.2 C/C++配置

**Define (宏定义)**:
```
GD32F50X,GD32F50X_HD,GD32F505,USE_STDPERIPH_DRIVER
```

**Include Paths**:
```
..\code\system
..\code\log
..\code\memory
..\..\..\Library\Firmware\CMSIS
..\..\..\Library\Firmware\CMSIS\GD\GD32F50x\Include
..\..\..\Library\Firmware\GD32F50x_standard_peripheral\Include
..\..\Third_Party\FreeRTOSv10.3.1\FreeRTOS\Source\include
..\..\Third_Party\FreeRTOSv10.3.1\FreeRTOS\Source\portable\GCC\ARM_CM33_NTZ\non_secure
..\..\Third_Party\Segger_RTT\RTT\RTT
```

### 4.3 添加文件到工程

```
Application:
  └── main.c

FreeRTOS:
  ├── tasks.c
  ├── queue.c
  ├── list.c
  ├── timers.c
  ├── port.c        ← ARM_CM33_NTZ/non_secure/
  ├── portasm.c     ← ARM_CM33_NTZ/non_secure/
  └── heap_4.c      ← MemMang/

LOG:
  ├── my_log.c
  ├── rtt_logger.c
  ├── uart_logger.c
  ├── SEGGER_RTT.c
  └── SEGGER_RTT_printf.c

CMSIS:
  └── system_gd32f50x.c

Peripheral:
  ├── gd32f50x_gpio.c
  ├── gd32f50x_rcu.c
  ├── gd32f50x_usart.c
  └── gd32f50x_misc.c

Startup:
  └── startup_gd32f50x.s
```

---

## 5. 编译与验证

### 5.1 编译工程

```
按F7编译，期望结果:
  ".\output\Project.axf" - 0 Error(s), 0 Warning(s)
```

### 5.2 功能验证

**LED测试**:
- LED1应该每500ms闪烁一次

**串口测试** (115200, 8N1):
```
========================================
GD32F505V EVAL FreeRTOS Demo
System Core Clock: 280000000 Hz
FreeRTOS Heap Size: 49152 bytes
========================================
LED1 toggling, FreeRTOS running...
```

**RTT日志**:
- 使用 J-Link RTT Viewer 查看

### 5.3 调试器验证

**查看任务列表** (Keil调试模式):
```
View → Serial Windows → Tasks

应该看到:
  Task Name    State     Priority    Stack
  LED          Blocked   3           128
  IDLE         Ready     0           128
```

---

## 6. 常见问题

### Q1: vTaskStartScheduler()后程序跑飞

**解决**:
```c
// 1. 检查FreeRTOS Heap
#define configTOTAL_HEAP_SIZE  ((size_t)(48 * 1024))

// 2. 启用栈溢出检测
#define configCHECK_FOR_STACK_OVERFLOW  2

// 3. 检查中断优先级
NVIC_SetPriorityGrouping(NVIC_PRIGROUP_PRE4_SUB0);
```

### Q2: fputc重复定义

```
Error: L6200E: Symbol fputc multiply defined
```

**解决**: 删除main.c中的fputc函数，使用库中的实现。

### Q3: 找不到头文件

```
Error: cannot open source input file "FreeRTOS.h"
```

**解决**: 检查Include Paths是否正确配置。

### Q4: LED不亮

**检查清单**:
- [ ] 引脚配置正确？
- [ ] 时钟使能？
- [ ] 输出模式？

### Q5: 串口无输出

**检查清单**:
- [ ] 引脚配置正确？（PA9=TX）
- [ ] 波特率匹配？（115200）
- [ ] LOG_USE_UART = 1？

---

## 7. 最佳实践

### 7.1 任务设计原则

| 原则 | 说明 | 示例 |
|------|------|------|
| **单一职责** | 每个任务只负责一个功能 | LED任务只控制LED |
| **优先级合理** | 实时任务高优先级 | 控制>通信 |
| **栈大小合适** | 不要浪费也不要溢出 | 简单任务128-256字 |
| **避免阻塞** | 不要在任务中死等 | 使用vTaskDelay() |

### 7.2 中断使用规范

```c
// ✅ 正确：在中断中使用FromISR版本
void EXTI0_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xTaskNotifyFromISR(xTaskHandle, VALUE, eSetBits,
                       &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// ❌ 错误：在中断中使用普通版本
void EXTI0_IRQHandler(void)
{
    xTaskNotify(xTaskHandle, VALUE, eSetBits);  // 错误！
}
```

---

## 8. 学习路径建议

### 新手路线（2-3周）

```
第1周: 基础学习
  ├─ 完成本教程移植
  ├─ 理解FreeRTOS任务概念
  └─ 学会查看调试信息

第2周: 功能扩展
  ├─ 添加队列通信
  ├─ 添加信号量同步
  └─ 添加软件定时器

第3周: 实际应用
  ├─ 移植硬件驱动（SPI、I2C）
  ├─ 开发实际业务逻辑
  └─ 性能优化
```

---

## 附录：常用FreeRTOS API

| 函数 | 功能 | 使用场景 |
|------|------|----------|
| `xTaskCreate()` | 创建任务 | 初始化阶段 |
| `vTaskDelay()` | 相对延时 | 周期性任务 |
| `vTaskDelayUntil()` | 绝对延时 | 精确定时 |
| `xQueueCreate()` | 创建队列 | 任务间通信 |
| `xSemaphoreCreateBinary()` | 创建信号量 | 同步、中断通知 |

---

**教程版本**: V1.1
**更新日期**: 2026-04-17
