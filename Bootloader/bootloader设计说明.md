# OC810 mDVR Bootloader 设计说明

## 文档信息

| 项目 | 内容 |
|------|------|
| **项目名称** | OC810 mDVR Bootloader |
| **文档版本** | V1.4 |
| **作者** | Harrison Wu (wuyujiao@jimiiot.com) |
| **创建日期** | 2026-06-10 |
| **最后更新** | 2026-06-23 |
| **目标平台** | GD32F505VGT7 (ARM Cortex-M33, 120MHz) |
| **FLASH** | 1MB Main Flash (0x08000000~0x080FFFFF) |
| **实现方式** | 裸机（无 RTOS） |
| **开发工具** | Keil MDK-ARM (ARM Compiler 6) |
| **参考库** | GD32F50x Firmware Library V1.0.4 |

---

## 1. 概述

### 1.1 功能定位

Bootloader 是系统上电后最先运行的程序，负责：
1. **OTA 固件升级**：检查 bootconf 标志，将 mcu_secondary 区固件拷贝到 app 区（CRC16 + MD5 双重校验）
2. **跳转 APP**：无升级时直接跳转到应用程序入口

### 1.2 设计原则

| **原则** | **说明** |
|------|------|
| **极简高效** | 裸机实现，无升级时启动时间 < 10ms |
| **安全可靠** | 多重校验，防变砖设计 |
| **共享库架构** | 与 APP 共享 Library 和 Segger_RTT，避免冗余 |

---

## 2. FLASH 分区

### 2.1 分区表

| 分区 | 大小 | 地址范围 | 用途 |
|------|------|---------|------|
| mcuboot | 48KB | 0x08000000~0x0800BFFF | Bootloader |
| setting_storage | 208KB | 0x0800C000~0x0803FFFF | 参数存储（LittleFS） |
| app | 376KB | 0x08040000~0x0809DFFF | 主应用 |
| mcu_secondary | 376KB | 0x0809E000~0x080FBFFF | OTA 固件暂存 |
| bootconf | 4KB | 0x080FC000~0x080FCFFF | 启动配置（OTA标志） |
| factory_storage | 12KB | 0x080FD000~0x080FFFFF | 工厂参数 |

> **总计**：1024KB，全部使用。所有分区地址均为 2KB 页对齐。
>
> **分区顺序说明**：bootconf 放在 factory_storage 前面，便于批量烧录时跳过 factory 区域（保护工厂参数）。

### 2.2 bootconf 数据结构

```c
#define BOOTCONF_VERSION_MAX_LEN  32  /* Bootloader版本信息最大长度 */

typedef struct {
    uint32_t magic;            /* 固定 0x42434F4E ("BCON") */
    uint32_t ota_flag;         /* OTA标志: 0=无, 1=待升级, 2=失败 */
    uint32_t last_boot_status; /* 上次启动状态 */

    uint8_t  file_id[4];       /* 固件文件ID */
    uint8_t  file_md5[16];     /* 固件MD5校验值（防篡改） */
    uint32_t file_size;        /* 固件大小（字节） */
    uint16_t file_crc16;       /* 固件CRC16校验值（快速检错） */
    uint16_t reserved;         /* 保留字段（4字节对齐） */

    char     version[BOOTCONF_VERSION_MAX_LEN]; /* Bootloader版本信息 */
    uint32_t checksum;         /* bootconf自身CRC16 */
} bootconf_t;                  /* 总大小: 76字节 */

#define BOOTCONF_MAGIC          0x42434F4E
#define OTA_FLAG_NONE           0x00
#define OTA_FLAG_PENDING        0x01
#define OTA_FLAG_FAILED         0x02

#define BOOT_STATUS_NORMAL      0x00
#define BOOT_STATUS_UPGRADING   0x01
#define BOOT_STATUS_UPGRADED    0x02
```

---

## 3. 系统架构

### 3.1 分层架构

```
┌──────────────────────────────────────────────────────────┐
│                    启动流程控制层                           │
│                  my_bl_main.c (主入口)                     │
└────────────────────────────┬─────────────────────────────┘
                             │
┌────────────────────────────▼─────────────────────────────┐
│                    功能模块层                              │
│  my_bl_ota (OTA升级)                                      │
└────────────────────────────┬─────────────────────────────┘
                             │
┌────────────────────────────▼─────────────────────────────┐
│                    基础服务层                              │
│  my_bl_bootconf (配置读写) │ my_bl_flash (FLASH操作)     │
│  my_bl_utils (CRC16/MD5/延时) │ my_bl_log_print (RTT日志) │
└────────────────────────────┬─────────────────────────────┘
                             │
┌────────────────────────────▼─────────────────────────────┐
│                    硬件层                                  │
│        GD32F50x Internal FLASH (FMC)              │
└──────────────────────────────────────────────────────────┘
```

### 3.2 目录结构

```
Bootloader/
├── project/
│   └── Bootloader.uvprojx              ← Keil工程
├── code/
│   ├── my_bl_main.c                    ← 主入口 + 启动流程
│   ├── my_bl_flash.c / .h              ← FLASH操作封装
│   ├── my_bl_bootconf.c / .h           ← bootconf读写管理
│   ├── my_bl_ota.c / .h               ← OTA升级逻辑
│   ├── my_bl_log_print.c / .h         ← 日志输出（RTT）
│   ├── my_bl_utils.c / .h             ← CRC16/MD5/延时等工具
│   ├── my_bl_config.h                 ← 配置宏定义
│   └── my_bl_version.h                ← 版本信息
└── bootloader设计说明.md               ← 本文件

共享库（与APP共用）：
OC810_GD32F50X/
├── Library/                            ← GD32标准库（共享）
│   ├── Firmware/CMSIS/
│   └── Firmware/GD32F50x_standard_peripheral/
└── Third_Party/
    └── Segger_RTT/                     ← RTT日志库（共享）
        └── RTT/
            ├── RTT/SEGGER_RTT.c
            ├── RTT/SEGGER_RTT.h
            ├── RTT/SEGGER_RTT_printf.c
            └── Config/SEGGER_RTT_Conf.h
```

---

## 3.3 共享库架构

Bootloader 与 APP 工程共享库文件，避免冗余和维护成本。

### 共享库清单

| 库名称 | 路径 | 说明 |
|--------|------|------|
| **GD32 外设库** | `../../Library/` | GPIO/UART/CAN/I2C/ADC 等标准外设驱动 |
| **Segger RTT** | `../../Third_Party/Segger_RTT/` | RTT 高速日志输出 |
| **CMSIS** | `../../Library/Firmware/CMSIS/` | ARM Cortex-M 核心支持文件 |

### Keil Include 路径配置

```xml
<!-- Bootloader.uvprojx -->
<IncludePath>
  ..\code;
  ..\..\Library\Firmware\CMSIS;
  ..\..\Library\Firmware\CMSIS\GD\GD32F50x\Include;
  ..\..\Library\Firmware\GD32F50x_standard_peripheral\Include;
  ..\..\Third_Party\Segger_RTT\RTT\RTT;
  ..\..\Third_Party\Segger_RTT\RTT\Config
</IncludePath>
```

### RTT 配置说明

Bootloader 和 APP 使用相同的 RTT 缓冲区配置：

| 配置项 | 值 | 说明 |
|--------|-----|------|
| `BUFFER_SIZE_UP` | 2048 | 上行缓冲区（日志输出），Bootloader SRAM 充足 |
| `BUFFER_SIZE_DOWN` | 256 | 下行缓冲区（Shell 输入），支持 RTT Shell |
| `SEGGER_RTT_MAX_NUM_UP_BUFFERS` | 3 | 上行缓冲区数量 |
| `SEGGER_RTT_MAX_NUM_DOWN_BUFFERS` | 3 | 下行缓冲区数量 |

**优势**：
- ✅ IAP 跳转时 RTT 连接不中断
- ✅ 日志连续输出，便于调试
- ✅ 配置一致，避免兼容性问题

### 架构优势

| 优势 | 说明 |
|------|------|
| **版本一致性** | Bootloader 和 APP 使用相同的外设库版本 |
| **维护简化** | 升级库文件只需修改一处 |
| **空间节省** | 减少磁盘占用和 Git 仓库体积（约 50MB+） |
| **日志连续** | 共享 RTT 配置确保 IAP 跳转时日志不中断 |

---

## 4. 启动流程

### 4.1 主流程

```
上电/复位
    │
    ▼
┌──────────────────────────────────┐
│ 1. 硬件初始化                     │
│    - SystemInit() 120MHz PLL    │
│    - RTT 日志初始化              │
└────────────────────┬─────────────┘
                     │
                     ▼
┌──────────────────────────────────┐
│ 2. 读取 bootconf                 │
│    - 校验 magic + checksum       │
│    - 无效→初始化默认值并写入     │
│    - 有效→继续使用               │
└────────────────────┬─────────────┘
                     │
                     ▼
              ┌──────┴──────┐
              │ota_flag ==  │
              │ PENDING ?   │
              └──────┬──────┘
           YES       │       NO
        ┌────────────┼────────────┐
        ▼            │            │
┌───────────────┐    │            │
│ 3. OTA升级流程 │    │            │
│  - 校验CRC16+MD5│    │            │
│  - 擦除APP区  │    │            │
│  - 拷贝固件   │    │            │
│  - 校验拷贝   │    │            │
│  - 清除标志   │    │            │
└───────┬───────┘    │            │
        │            │            │
   ┌────┴────┐       │            │
   │ 成功？  │       │            │
   └────┬────┘       │            │
   YES  │  NO        │            │
   ┌────┴────┐       │            │
   ▼         ▼       │            │
  重启     标记失败  │            │
                    │            │
                    ▼            ▼
          ┌──────────────────────────┐
          │ 4. 校验APP区有效性       │
          │    - 栈指针有效性检查    │
          └────────────┬─────────────┘
                       │
                       ▼
          ┌──────────────────────────┐
          │ 5. 跳转到APP             │
          │    - 关闭所有中断        │
          │    - 关闭SysTick         │
          │    - 清除NVIC挂起        │
          │    - 设置MSP             │
          │    - 跳转复位向量        │
          └──────────────────────────┘
```

### 4.2 异常处理（Fault Handler）

Bootloader 实现了完整的 HardFault/MemManage/BusFault/UsageFault 诊断机制：

| 异常类型 | 处理策略 |
|---------|---------|
| HardFault | 读取 SCB 寄存器（CFSR/HFSR/MMFAR/BFAR），解析 CFSR 位字段，输出栈帧快照（R0-R3/R12/LR/PC/xPSR），延时确保日志输出后系统复位 |
| MemManage | 记录 MMFAR 地址和 CFSR，复位系统 |
| BusFault | 记录 BFAR 地址和 CFSR，复位系统 |
| UsageFault | 记录 CFSR，复位系统 |

**Fault 安全机制**：
1. Fault 发生时重新初始化 RTT 日志系统（`SEGGER_RTT_Init()`）
2. 使用 `volatile` 延时循环（500万次）确保 RTT 缓冲区数据输出完成
3. 调用 `NVIC_SystemReset()` 系统复位，避免 Bootloader 死锁导致变砖
4. Fault 日志函数为 fault-safe 实现：不使用中断、不分配内存、不调用 snprintf

### 4.3 常规异常处理

| 异常场景 | 处理策略 |
|---------|---------|
| bootconf 损坏/首次启动 | 写入默认值，继续跳转 APP |
| OTA 固件 CRC 校验失败 | 标记 ota_flag=FAILED，跳转 APP（旧固件可运行） |
| FLASH 擦写失败 | 标记 ota_flag=FAILED，跳转 APP |
| APP 区栈指针无效 | 日志告警，进入死循环 |
| 升级过程中掉电 | 重启后 ota_flag 仍为 PENDING，自动重试 |

---

## 5. 核心模块设计

### 5.1 my_bl_config.h — 配置定义

```c
/* FLASH 分区地址 */
#define BL_FLASH_BOOT_BASE      0x08000000U
#define BL_FLASH_BOOT_SIZE      (48U * 1024U)
#define BL_FLASH_SETTING_BASE   0x0800C000U
#define BL_FLASH_SETTING_SIZE   (208U * 1024U)
#define BL_FLASH_APP_BASE       0x08040000U
#define BL_FLASH_APP_SIZE       (376U * 1024U)
#define BL_FLASH_OTA_BASE       0x0809E000U
#define BL_FLASH_OTA_SIZE       (376U * 1024U)
#define BL_FLASH_BOOTCONF_BASE  0x080FC000U  /* 启动配置（Factory前面） */
#define BL_FLASH_BOOTCONF_SIZE  (4U * 1024U)
#define BL_FLASH_FACTORY_BASE   0x080FD000U  /* 工厂参数（最后） */
#define BL_FLASH_FACTORY_SIZE   (12U * 1024U)

/* UART（与 my_dvr 共用 USART1） */
#define BL_UART_PERIPH          USART1
#define BL_UART_BAUDRATE        115200U

/* 日志通道开关 */
#define BL_RTT_LOG_ENABLE           1       /* RTT日志总开关 */

/* 日志等级 */
#define MY_BL_LOG_LEVEL_NONE      0   // 关闭所有日志
#define MY_BL_LOG_LEVEL_ERROR     1   // 错误级别
#define MY_BL_LOG_LEVEL_WARN      2   // 警告级别
#define MY_BL_LOG_LEVEL_INFO      3   // 信息级别
#define MY_BL_LOG_LEVEL_DEBUG     4   // 调试级别

/* 当前日志级别 */
#define MY_BL_LOG_CURRENT_LEVEL   MY_BL_LOG_LEVEL_INFO

/* 错误码 */
#define BL_OK                   0
#define BL_ERR_FLASH           -1
#define BL_ERR_CRC             -2
#define BL_ERR_VERIFY          -3
#define BL_ERR_INVALID         -4
#define BL_ERR_TIMEOUT         -5
```

### 5.2 my_bl_flash — FLASH操作

```c
int my_bl_flash_erase(uint32_t addr, uint32_t size);
int my_bl_flash_program(uint32_t addr, const uint8_t *data, uint32_t size);
int my_bl_flash_read(uint32_t addr, uint8_t *buf, uint32_t size);
```

**实现要点**：
- 使用 GD32 FMC Bank0 标准接口（fmc_page_erase / fmc_word_program）
- 编程后逐字验证写入正确性
- 操作前后自动 unlock/lock FMC

### 5.3 my_bl_bootconf — 配置管理

```c
int my_bl_bootconf_read(my_bl_bootconf_t *bconf);
int my_bl_bootconf_write(const my_bl_bootconf_t *bconf);
void my_bl_bootconf_init_default(my_bl_bootconf_t *bconf);
```

**实现要点**：
- 读取时校验 magic 和 checksum
- 写入时自动计算 checksum（排除 checksum 字段本身）
- 擦写整个 4KB 页

### 5.4 my_bl_ota — OTA升级

```c
int my_bl_ota_upgrade(my_bl_bootconf_t *bconf);
```

**升级流程**：
1. 计算 mcu_secondary 区 CRC16（查表法），与 bootconf.file_crc16 比对
2. 计算 mcu_secondary 区 MD5，与 bootconf.file_md5 比对
3. 擦除 APP 区（376KB，约 188 页）
4. 按 4KB 块从 mcu_secondary 读取→写入 APP 区
5. 计算 APP 区 CRC16，再次校验

### 5.5 my_bl_log_print — 日志输出

```c
void my_bl_log_init(void);
void my_bl_log_print(int level, const char *level_str, const char *function, const char *fmt, ...);
void my_bl_log_dump(char *tag, const void *pdata, uint32_t len);
```

**日志级别**：
| 级别 | 宏 | 值 | 说明 |
|------|-----|-----|------|
| NONE | MY_BL_LOG_LEVEL_NONE | 0 | 关闭所有日志 |
| ERROR | MY_BL_LOG_LEVEL_ERROR | 1 | 错误级别 |
| WARN | MY_BL_LOG_LEVEL_WARN | 2 | 警告级别 |
| INFO | MY_BL_LOG_LEVEL_INFO | 3 | 信息级别（当前默认） |
| DEBUG | MY_BL_LOG_LEVEL_DEBUG | 4 | 调试级别 |

**使用宏**（编译期级别过滤）：
```c
MY_LOG_I("bootconf OK");  // INFO级别
MY_LOG_E("APP stack invalid (0x%08X)", sp);     // ERROR级别
MY_LOG_DBG_DUMP("OTA", buf, len);               // DEBUG级别hex dump
```

**实现要点**：
- RTT 输出：调用 `SEGGER_RTT_Write()`，通过 J-Link 调试器查看
- 日志级别由 `MY_BL_LOG_CURRENT_LEVEL` 控制，低于该级别的消息编译为空操作
- hex dump 支持 Hex+ASCII 格式输出，偏移量为十六进制显示

**RTT 配置（SEGGER_RTT_Conf.h 关键项）**：
```c
#define BUFFER_SIZE_UP                  4096
#define BUFFER_SIZE_DOWN                16
#define SEGGER_RTT_MAX_NUM_UP_BUFFERS   1
#define SEGGER_RTT_MAX_NUM_DOWN_BUFFERS 1
#define SEGGER_RTT_MODE_DEFAULT         SEGGER_RTT_MODE_NO_BLOCK_SKIP
```

### 5.6 my_bl_utils — 工具函数

```c
/* CRC16-CCITT 查表法（OTA 固件校验使用） */
uint16_t my_bl_crc16_table(const uint8_t *data, uint16_t len);

/* MD5 流式计算（RFC 1321 标准） */
void my_bl_md5_init(my_bl_md5_ctx_t *ctx);
void my_bl_md5_update(my_bl_md5_ctx_t *ctx, const uint8_t *data, uint32_t len);
void my_bl_md5_final(my_bl_md5_ctx_t *ctx, uint8_t *output);

/* 系统控制 */
void my_bl_system_reset(void);
void my_bl_delay_ms(uint32_t ms);
```

**CRC16 实现**：
- 算法：CCITT 标准，多项式 0x1021
- 方式：256 项查找表法（512 字节 ROM）
- 初始值：0xFFFF，输出取反
- 速度：每字节只需一次查表+异或操作

**MD5 实现**：
- 标准：RFC 1321
- 接口：流式计算（Init → Update → Final）
- 输出：16 字节摘要（小端序）

### 5.7 my_bl_main — 主入口

**跳转前操作**：
1. `__disable_irq()` — 关闭所有中断
2. 关闭 SysTick（CTRL=0, LOAD=0, VAL=0）
3. 清除所有 NVIC 挂起标志
4. `__set_MSP(app_stack)` — 设置主栈指针
5. 跳转到复位向量地址

---

## 6. 安全机制

### 6.1 防变砖设计

| 机制 | 说明 |
|------|------|
| APP 有效性校验 | 跳转前检查栈指针在 RAM 范围内 |
| OTA 双重校验 | 升级前校验源固件 + 升级后校验目标固件 |
| 升级失败回退 | 失败后标记 FAILED，继续运行旧固件 |
| 掉电自动重试 | ota_flag=PENDING 保持，重启后自动重试 |

### 6.2 校验策略

| 校验点 | 方法 | 耗时 |
|--------|------|------|
| OTA 源固件 | CRC16 + MD5 双重校验 | ~25ms |
| APP 拷贝结果 | CRC16 校验 | ~10ms |
| APP 跳转前 | 栈指针有效性 | < 1ms |

---

## 7. 关键设计决策

### 7.1 时钟初始化策略

**问题**：GD32F50x 的 RCU API 命名与其他系列不同（如 RCU_PLL0_CK 而非 RCU_PLL_CK），直接操作寄存器容易出错。

**方案**：复用 `system_gd32f50x.c` 中已配置的 `__SYSTEM_CLOCK_120M_PLL_HXTAL` 宏。SystemInit() 在 main() 之前自动完成 120MHz 时钟配置（HXTAL 25MHz → PREDIV0/5 → PLL0×24 = 120MHz）。

`_bl_clock_init()` 仅调用 `SystemCoreClockUpdate()` 更新全局时钟变量。

### 7.2 NULL 指针检查

**方案**：统一使用 `((const void *)ptr == 0U)` 替代 `ptr == NULL`，避免裸机环境依赖问题。

### 7.3 CRC16 与 MD5 实现

**CRC16-CCITT**：
- 多项式：0x1021（CCITT 标准）
- 实现：256 项查找表法（512 字节 ROM）
- 初始值：0xFFFF，输出取反
- 速度：比位运算法快约 8 倍

**MD5**：
- 标准：RFC 1321
- 实现：流式计算接口（Init → Update → Final）
- 用途：OTA 固件完整性校验，防篡改
- 输出：16 字节摘要

### 7.4 CRC 统一策略

| 场景 | 函数 | 算法 | 多项式 |
|------|------|------|--------|
| OTA 固件校验 | `my_bl_crc16_table()` | 查表法 | 0x1021 |
| bootconf 自校验 | `my_bl_crc16_table()` | 查表法 | 0x1021 |
| Python 工具 | `crc16_table()` | 查表法 | 0x1021 |

**设计理由**：
- CRC16 足够检测常见错误（位翻转、字节交换等）
- MD5 提供强完整性保护，防恶意篡改
- 查表法速度快，适合 OTA 大文件校验

### 7.5 bootconf 回写策略

仅在 bootconf 无效（初始化默认值）或 OTA 状态变更时回写 FLASH，避免频繁擦写延长 Flash 寿命。

---

## 8. Keil 工程配置

### 8.1 预处理宏

```
GD32F50X, GD32F50X_HD, GD32F505, USE_STDPERIPH_DRIVER
```

### 8.2 Include Path

```
..\code
..\Library\Firmware\CMSIS\GD\GD32F50x\Include
..\Library\Firmware\CMSIS\Core\Include
..\Library\Firmware\GD32F50x_standard_peripheral\Include
..\Segger_RTT\RTT\RTT
..\Segger_RTT\RTT\Config
```

### 8.3 链接配置

- **IROM1**: 0x08000000, 0xC000 (48KB)
- **IRAM1**: 0x20000000, 0x30000 (192KB)

### 8.4 Debug 配置

- J-Link/J-Trace 调试器
- RTT 日志通过 J-Link RTT Viewer 查看

### 8.5 After Build 配置

Bootloader 编译完成后，Keil 自动执行以下命令：

```
Run #1  fromelf --bin -o .\Objects\Bootloader.bin .\Objects\Bootloader.axf

Run #2  ..\..\tools\build_bootloader.bat
          ├─ 检查/创建 firmware 目录
          ├─ 复制 Bootloader.hex → firmware/Bootloader.hex
          └─ 复制 Bootloader.bin → firmware/Bootloader.bin
```

**说明：**
- `build_bootloader.bat` 会自动检测路径并创建 `firmware/` 目录（如果不存在）
- Bootloader 固件输出到 `firmware/` 目录，供后续 APP 合并使用

---

## 9. 与 APP 的交互协议

### 9.1 APP → Bootloader

APP 通过以下方式触发 Bootloader 升级模式：
1. 设置 bootconf.ota_flag = OTA_FLAG_PENDING
2. 将新固件写入 mcu_secondary 区
3. 设置 bootconf.file_size 和 file_crc16
4. 设置 bootconf.file_md5（16 字节）
4. 系统复位

### 9.2 Bootloader → APP

Bootloader 通过以下方式传递状态给 APP：
1. bootconf.last_boot_status — 上次启动状态

---

## 10. 硬件验证记录

### 11.1 START 板测试（2026-06-22）

| 测试项 | 结果 | 说明 |
|--------|------|------|
| RTT 日志输出 | ✅ | SEGGER_RTT_Write 正常 |
| 系统时钟 120MHz | ✅ | HXTAL + PLL0 正常 |
| FLASH 读取 | ✅ | bootconf 区域读写正确 |
| FLASH 擦除+写入 | ✅ | fmc_page_erase + fmc_word_program |
| CRC16 校验 | ✅ | CCITT 0x1021 查表法 |
| MD5 校验 | ✅ | RFC 1321 流式计算 |
| bootconf 持久化 | ✅ | 异常时初始化默认值，正常时不频繁擦写 |
| OTA 标志判断 | ✅ | 无升级时正确跳过 |
| APP 有效性检测 | ✅ | 正确检测到无效 APP 并进入安全循环 |

**测试日志输出**：
```
==============================
Bootloader v1.0.0
Build: Jun 22 2026 14:54:27
==============================
bootconf OK
ERROR: APP stack invalid (0xFFFFFFFF)
```

---

## 11. 资源评估

### 11.1 代码体积

| 模块 | 预估大小 |
|------|---------|
| my_bl_main | ~500B |
| my_bl_flash | ~800B |
| my_bl_bootconf | ~400B |
| my_bl_ota | ~800B |
| my_bl_log_print (RTT) | ~2KB |
| my_bl_utils (CRC16查表) | 512B |
| my_bl_utils (MD5) | ~2KB |
| GD32 标准库 | ~3KB |
| 启动代码 | ~200B |
| **总计** | **~9.3KB** |

### 11.2 RAM 使用

| 用途 | 大小 |
|------|------|
| 栈 | 2KB |
| OTA 拷贝缓冲 | 4KB |
| RTT 上行缓冲 | 4KB |
| 全局变量 | ~200B |
| **总计** | **~7.2KB** |

> **48KB FLASH 空间充裕**，剩余 ~39KB 可用于功能扩展。

---

## 12. 开发计划

| 阶段 | 内容 | 状态 |
|------|------|------|
| P1 | 基础框架：初始化、bootconf、跳转 APP | ✅ 已完成 |
| P2 | OTA 升级：固件校验、拷贝、状态管理 | ✅ 已完成 |
| P3 | 安全加固：异常处理、LED 告警 | 待实现 |
| P4 | 联调测试 | 进行中 |

---

## 附录A：错误码定义

| 错误码 | 值 | 含义 |
|--------|-----|------|
| BL_OK | 0 | 成功 |
| BL_ERR_FLASH | -1 | FLASH 操作失败 |
| BL_ERR_CRC | -2 | CRC16 校验失败 |
| BL_ERR_VERIFY | -3 | 写入验证失败 |
| BL_ERR_INVALID | -4 | 参数无效 |
| BL_ERR_TIMEOUT | -5 | 操作超时 |

---

## 附录B：版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| V1.0 | 2026-06-10 | 初始设计文档 + 裸机框架实现 |
| V1.1 | 2026-06-22 | 合并实现记录，更新 Keil 配置，添加硬件验证结果 |
| V1.3 | 2026-06-23 | 日志系统升级为分级日志（DEBUG/INFO/WARN/ERROR）+ hex dump，新增 Fault Handler 完整诊断机制（HardFault/MemManage/BusFault/UsageFault），RTT 缓冲区调整为 4KB |
| V1.4 | 2026-06-23 | CRC32 迁移至 CRC16-CCITT 查表法，新增 MD5 流式计算（RFC 1321），移除 my_bl_uart 模块（预留未使用），bootconf 结构体优化（file_crc16 + reserved） |
