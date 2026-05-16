# GD32F505VGT7 FreeRTOS 移植计划

**状态**: ✅ 已完成
**完成日期**: 2026-04-16
**FreeRTOS版本**: v10.3.1
**目标芯片**: GD32F505VGT7 (Cortex-M33, 280MHz)

---

## 项目概述

本项目已成功将FreeRTOS v10.3.1移植到GD32F505VGT7，包含LED闪烁示例任务。

---

## 已完成的工作

### ✅ 第一阶段: 准备FreeRTOS源码
- 从GD32E503Z_EVAL复制FreeRTOS v10.3.1源码
- 目录: `Third_Party/FreeRTOSv10.3.1/`
- 使用Cortex-M33端口层: `GCC/ARM_CM33_NTZ/non_secure/`

### ✅ 第二阶段: 创建基础工程文件
- 创建 `project/OC810/code/system/` 目录
- 复制并修改: main.c, gd32f50x_it.c/h, gd32f50x_libopt.h

### ✅ 第三阶段: 配置FreeRTOSConfig.h
- configCPU_CLOCK_HZ = SystemCoreClock (280MHz)
- configTICK_RATE_HZ = 1000 (1ms)
- configTOTAL_HEAP_SIZE = 48KB (量产配置)
- configENABLE_FPU = 1
- configMAX_PRIORITIES = 8

### ✅ 第四阶段: 修改中断服务程序
- 移除SVC_Handler、PendSV_Handler、SysTick_Handler（由FreeRTOS接管）
- 保留异常处理函数

### ✅ 第五阶段: 创建示例程序
- init_task: 初始化任务（完成后删除）
- led_task: LED闪烁任务（500ms）
- 串口输出调试信息（115200波特率）

### ✅ 第六阶段: 配置系统时钟
- 280MHz (PLL_HXTAL, HXTAL=8MHz)

### ✅ 第七阶段: 添加日志模块
- RTT日志 + 串口日志双模
- 目录: `project/OC810/code/log/`

### ✅ 第八阶段: 添加内存管理模块
- 安全内存分配接口
- 目录: `project/OC810/code/memory/`

---

## 目录结构

```
mDVR_MCU/
├── Docs/                             ← 项目文档
├── Library/                          ← GD32固件库
├── Third_Party/                      ← 第三方库
│   ├── FreeRTOSv10.3.1/
│   └── Segger_RTT/
└── project/
    └── OC810/                        ← OC810项目
        ├── code/
        │   ├── system/               ← 系统核心代码
        │   ├── log/                  ← 日志模块
        │   └── memory/               ← 内存管理模块
        └── MDK-ARM/                  ← Keil工程
```

---

## 关键配置参数

### 系统配置
| 参数 | 值 | 说明 |
|------|-----|------|
| 芯片 | GD32F505VGT7 | Cortex-M33 |
| 主频 | 280MHz | PLL_HXTAL |
| Flash | 1024KB | Code-Flash 128KB + Data-Flash 896KB |
| SRAM | 192KB | 0x20000000-0x2002FFFF |

### FreeRTOS配置
| 参数 | 值 | 说明 |
|------|-----|------|
| 版本 | v10.3.1 | - |
| Tick Rate | 1000Hz | 1ms |
| Heap Size | 48KB | 量产配置 |
| Max Priorities | 8 | - |
| FPU | 启用 | - |

---

## 验证功能

- **LED1**: 每500ms闪烁
- **串口**: 115200波特率，输出调试信息
- **RTT日志**: 使用J-Link RTT Viewer查看

---

## 参考资源

- **GD32F50x固件库**: `Library/`
- **FreeRTOS源码**: `Third_Party/FreeRTOSv10.3.1/`
- **FreeRTOS官方文档**: https://www.freertos.org/

---

**文档版本**: V1.1
**最后更新**: 2026-04-17
