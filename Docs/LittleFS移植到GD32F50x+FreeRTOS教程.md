# LittleFS 移植到 GD32F50x + FreeRTOS 教程

## 1. 概述

本文档记录了将 [LittleFS v2.11](https://github.com/littlefs-project/littlefs) 移植到 GD32F50x（ARM Cortex-M33）平台、运行在 FreeRTOS 上的完整过程和踩坑经验。

**适用场景**：嵌入式设备参数持久化存储（小文件、频繁读写、掉电安全）。

**最终成果**：
- LittleFS 基础层：46项测试全部通过
- 参数管理层：98项测试全部通过（功能50 + 掉电17 + RAM评估10 + 压力21）

---

## 2. 前置条件

| 组件 | 版本/要求 |
|---|---|
| MCU | GD32F505VGT7（Cortex-M33, 1MB FLASH） |
| RTOS | FreeRTOS v10.3.1 |
| 编译器 | ARM Compiler V6.15 (ARMCLANG) |
| LittleFS | v2.11.3 |
| FLASH驱动 | 已实现 param_flash_read/prog/erase |

---

## 3. 移植步骤

### Step 1: 添加 LittleFS 源码到工程

从 [LittleFS GitHub](https://github.com/littlefs-project/littlefs/releases) 下载源码，仅保留嵌入式所需的核心文件：

```
Third_Party/littlefs/
├── lfs.c          # 核心实现
├── lfs.h          # 公共API（含版本号 LFS_VERSION）
├── lfs_util.c     # 工具函数
├── lfs_util.h     # 内部宏和工具
└── LICENSE.md     # BSD-3-Clause 许可证
```

> 删除不需要的目录：`bd/`（参考块设备）、`benches/`、`runners/`、`scripts/`、`tests/`、`Makefile`、`DESIGN.md`、`SPEC.md`、`README.md`。

将 `Third_Party/littlefs` 添加到 Keil Include Path。

### Step 2: 创建平台适配头文件 `lfs_config.h`

这是**整个移植的核心**，所有平台适配都在此文件集中管理：

```c
#ifndef __LFS_CONFIG_H__
#define __LFS_CONFIG_H__

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "my_log.h"          // 你的日志系统
#include "my_safe_memory.h"  // FreeRTOS内存管理封装
#include "param_flash.h"     // FLASH驱动接口

/*******************************************************************************
 * 日志/断言重定向（利用 lfs_util.h 的 #ifndef 守卫）
 * 必须在 #include "lfs.h" 之前定义！
 ******************************************************************************/

#define LFS_LOG_ENABLE      1   // 1=调试模式, 0=发布模式

#if LFS_LOG_ENABLE
#define LFS_ERROR(fmt, ...)   MY_LOG_E("[LFS] " fmt, ##__VA_ARGS__)
#define LFS_WARN(fmt, ...)    MY_LOG_W("[LFS] " fmt, ##__VA_ARGS__)
#define LFS_DEBUG(fmt, ...)   MY_LOG_D("[LFS] " fmt, ##__VA_ARGS__)
#define LFS_TRACE(fmt, ...)   MY_LOG_D("[LFS_TRACE] " fmt, ##__VA_ARGS__)
#define LFS_ASSERT(test) do { \
    if (!(test)) { \
        MY_LOG_E("[LFS ASSERT FAIL] %s:%d: %s", __FILE__, __LINE__, #test); \
        while (1) {} \
    } \
} while (0)
#else
#define LFS_TRACE(...)   ((void)0)
#define LFS_ERROR(...)   ((void)0)
#define LFS_WARN(...)    ((void)0)
#define LFS_DEBUG(...)   ((void)0)
#define LFS_ASSERT(test) ((void)0)
#endif

/* 提供__aeabi_assert实现，防止标准库assert链接错误 */
static inline void __aeabi_assert(const char *expr, const char *file, int line)
{
    MY_LOG_E("[ASSERT FAIL] %s:%d: %s", file, line, expr);
    while (1) {}
}

/* 内存分配重定向到FreeRTOS堆管理 */
static inline void *lfs_safe_malloc(size_t size)
{
    void *p = pvPortMalloc(size);
    if (p == NULL) { MY_LOG_E("[LFS] Alloc failed: %d bytes", (int)size); }
    return p;
}
static inline void lfs_safe_free(void **p)
{
    if (p != NULL && *p != NULL) { vPortFree(*p); *p = NULL; }
}
#define LFS_MALLOC(size)  lfs_safe_malloc(size)
#define LFS_FREE(p)       lfs_safe_free((void **)&(p))

/*******************************************************************************
 * LittleFS 核心配置
 ******************************************************************************/
#define LFS_THREADSAFE      1       // FreeRTOS多线程安全
#define LFS_ENABLE_REOPEN   1
#define LFS_NAME_MAX        (64U)   // 文件名最大长度
#define LFS_FILE_MAX        (4096U) // 单文件最大4KB（根据业务需求调整）
#define LFS_ATTR_MAX        (64U)

#include "lfs.h"  // 必须在所有宏定义之后！

/*******************************************************************************
 * FLASH物理参数
 ******************************************************************************/
#define LFS_FLASH_BASE_ADDR  PARAM_PARTITION_SETTING_BASE
#define LFS_FLASH_SIZE       PARAM_PARTITION_SETTING_SIZE
#define LFS_BLOCK_SIZE       PARAM_PARTITION_SETTING_SECTOR_SIZE  // 与FLASH扇区对齐
#define LFS_BLOCK_COUNT      PARAM_PARTITION_SETTING_SECTOR_COUNT
#define LFS_READ_SIZE        (4U)    // 最小读单位
#define LFS_PROG_SIZE        (4U)    // 最小写单位
#define LFS_BLOCK_CYCLES     (1024U) // 磨损均衡
#define LFS_CACHE_SIZE       (256U)  // 缓存大小
#define LFS_LOOKAHEAD_SIZE   (32U)   // 预读位图大小

/*******************************************************************************
 * 文件系统句柄（含静态缓冲区）
 ******************************************************************************/
typedef struct {
    lfs_t              lfs;
    struct lfs_config  cfg;
    bool               mounted;
    uint8_t            read_buffer[LFS_CACHE_SIZE];      // 静态读缓存
    uint8_t            prog_buffer[LFS_CACHE_SIZE];      // 静态写缓存
    uint8_t            lookahead_buffer[LFS_LOOKAHEAD_SIZE]; // 静态预读
} lfs_handle_t;

extern lfs_handle_t g_lfs_handle;

#endif /* __LFS_CONFIG_H__ */
```

### Step 3: 实现 Block Device 回调 `lfs_port.c`

```c
#include "lfs_config.h"
#include "param_flash.h"
#include "FreeRTOS.h"
#include "semphr.h"

lfs_handle_t g_lfs_handle = {0};
static SemaphoreHandle_t s_lfs_mutex = NULL;

/* Block Device回调 */
static int lfs_block_read(const struct lfs_config *c, lfs_block_t block,
                          lfs_off_t off, void *buffer, lfs_size_t size)
{
    if (block >= c->block_count) return LFS_ERR_INVAL;  // 必须校验！
    uint32_t addr = LFS_FLASH_BASE_ADDR + (block * c->block_size) + off;
    return param_flash_read(addr, buffer, size);
}

static int lfs_block_prog(const struct lfs_config *c, lfs_block_t block,
                          lfs_off_t off, const void *buffer, lfs_size_t size)
{
    if (block >= c->block_count) return LFS_ERR_INVAL;
    uint32_t addr = LFS_FLASH_BASE_ADDR + (block * c->block_size) + off;
    return param_flash_program(addr, buffer, size);
}

static int lfs_block_erase(const struct lfs_config *c, lfs_block_t block)
{
    if (block >= c->block_count) return LFS_ERR_INVAL;
    uint32_t addr = LFS_FLASH_BASE_ADDR + (block * c->block_size);
    return param_flash_erase(addr, c->block_size);
}

static int lfs_block_sync(const struct lfs_config *c) { (void)c; return 0; }

#ifdef LFS_THREADSAFE
static int lfs_block_lock(const struct lfs_config *c) {
    (void)c;
    if (s_lfs_mutex) xSemaphoreTake(s_lfs_mutex, portMAX_DELAY);
    return 0;
}
static int lfs_block_unlock(const struct lfs_config *c) {
    (void)c;
    if (s_lfs_mutex) xSemaphoreGive(s_lfs_mutex);
    return 0;
}
#endif

/* 初始化 */
int lfs_port_init(void)
{
    int ret = param_flash_init();
    if (ret != 0) return ret;

#ifdef LFS_THREADSAFE
    if (s_lfs_mutex == NULL) {
        s_lfs_mutex = xSemaphoreCreateMutex();
        if (s_lfs_mutex == NULL) return -1;
    }
#endif

    /* 配置回调 */
    g_lfs_handle.cfg.read  = lfs_block_read;
    g_lfs_handle.cfg.prog  = lfs_block_prog;
    g_lfs_handle.cfg.erase = lfs_block_erase;
    g_lfs_handle.cfg.sync  = lfs_block_sync;
#ifdef LFS_THREADSAFE
    g_lfs_handle.cfg.lock   = lfs_block_lock;
    g_lfs_handle.cfg.unlock = lfs_block_unlock;
#endif

    /* 配置参数 */
    g_lfs_handle.cfg.read_size     = LFS_READ_SIZE;
    g_lfs_handle.cfg.prog_size     = LFS_PROG_SIZE;
    g_lfs_handle.cfg.block_size    = LFS_BLOCK_SIZE;
    g_lfs_handle.cfg.block_count   = LFS_BLOCK_COUNT;
    g_lfs_handle.cfg.block_cycles  = LFS_BLOCK_CYCLES;
    g_lfs_handle.cfg.cache_size    = LFS_CACHE_SIZE;
    g_lfs_handle.cfg.lookahead_size = LFS_LOOKAHEAD_SIZE;
    g_lfs_handle.cfg.name_max      = LFS_NAME_MAX;
    g_lfs_handle.cfg.file_max      = LFS_FILE_MAX;
    g_lfs_handle.cfg.attr_max      = LFS_ATTR_MAX;

    /* 关键：配置静态缓冲区！ */
    g_lfs_handle.cfg.read_buffer      = g_lfs_handle.read_buffer;
    g_lfs_handle.cfg.prog_buffer      = g_lfs_handle.prog_buffer;
    g_lfs_handle.cfg.lookahead_buffer = g_lfs_handle.lookahead_buffer;

    g_lfs_handle.mounted = false;
    return 0;
}

/* 挂载（首次不自动格式化，由上层决定） */
int lfs_port_mount(void)
{
    if (g_lfs_handle.mounted) return 0;

    int ret = lfs_mount(&g_lfs_handle.lfs, &g_lfs_handle.cfg);
    if (ret == LFS_ERR_CORRUPT) {
        /* 首次使用或损坏，返回错误由上层处理 */
        return LFS_ERR_CORRUPT;
    }
    if (ret != 0) return ret;

    g_lfs_handle.mounted = true;
    return 0;
}

/* 格式化（供上层主动调用） */
int lfs_port_format(void)
{
    if (g_lfs_handle.mounted) lfs_port_unmount();
    int ret = lfs_format(&g_lfs_handle.lfs, &g_lfs_handle.cfg);
    return ret;
}
```

### Step 4: Keil 工程配置

**Include Path 添加**：
```
Third_Party/littlefs
code/fs  (lfs_config.h所在目录)
```

**Define（全局宏）**：
```
GD32F50X,GD32F50X_HD,GD32F505,USE_STDPERIPH_DRIVER
```

**MiscControls（C编译器）**：
```
--include lfs_config.h
```

> `--include` 是 ARMCLANG 的强制包含选项，会在编译**每个源文件之前**自动插入 `#include "lfs_config.h"`。
> 这样 **无需修改任何 LittleFS 第三方源码**，保持其完整性，升级版本时不会丢失配置。

> **不要在 Keil Define 中添加任何 LFS 相关宏！** 所有 LittleFS 适配都在 `lfs_config.h` 中管理。

---

## 4. 踩坑记录（重点！）

### 坑1：`lfs_mount()` 卡死不动（printf 陷阱）

**现象**：系统调用 `lfs_mount()` 后完全卡住，无任何输出。

**根因**：LittleFS 默认的 `LFS_ERROR`/`LFS_WARN`/`LFS_DEBUG` 宏展开为 `printf()`。嵌入式系统如果没有配置标准输出（无 semihosting、无 UART 重定向），`printf()` 会**永久阻塞**。

当 `lfs_mount()` 扫描空FLASH时，内部会调用 `LFS_ERROR("Corrupted dir pair at...")` → `printf()` → 卡死。

**解决**：在 `lfs_config.h` 中重定向所有日志宏到自有的日志系统（RTT/UART）。

```c
// [X] 错误做法：用 LFS_NO_ERROR 禁用日志（丢失调试信息）
// [V] 正确做法：重定向到自有日志系统
#define LFS_ERROR(fmt, ...)  MY_LOG_E("[LFS] " fmt, ##__VA_ARGS__)
```

**原理**：`lfs_util.h` 使用 `#ifndef LFS_ERROR` 守卫，如果我们在 `#include "lfs.h"` 之前定义了 `LFS_ERROR`，默认的 `printf` 版本就会被跳过。

---

### 坑2：`LFS_ERR_NOMEM (-12)` 返回

**现象**：`lfs_mount()` 返回 -12（LFS_ERR_NOMEM）。

**根因**：没有为 LittleFS 配置静态缓冲区（`read_buffer`、`prog_buffer`、`lookahead_buffer`）。LittleFS 在 mount 时会调用 `lfs_malloc()` 分配这些缓冲区，如果分配失败就返回 NOMEM。

**解决**：在 `lfs_handle_t` 结构体中定义静态缓冲区，并在 `lfs_port_init()` 中赋值给 `cfg`：

```c
// 在 lfs_config.h 的 handle 中
uint8_t read_buffer[LFS_CACHE_SIZE];       // 256字节
uint8_t prog_buffer[LFS_CACHE_SIZE];       // 256字节
uint8_t lookahead_buffer[LFS_LOOKAHEAD_SIZE]; // 32字节

// 在 lfs_port_init() 中
g_lfs_handle.cfg.read_buffer      = g_lfs_handle.read_buffer;
g_lfs_handle.cfg.prog_buffer      = g_lfs_handle.prog_buffer;
g_lfs_handle.cfg.lookahead_buffer = g_lfs_handle.lookahead_buffer;
```

> 注意：这与 FreeRTOS 堆总量无关！即使堆很大，未配置静态缓冲区也会导致此错误。

---

### 坑3：`lfs_mlist_isopen` 链接错误

**现象**：
```
Error: L6218E: Undefined symbol lfs_mlist_isopen (referred from lfs.o)
```

**根因**：`LFS_NO_ASSERT` 宏在 `lfs.c` 中同时控制两件事：
1. 阻止 `<assert.h>` 引入
2. 阻止 `lfs_mlist_isopen` 等断言辅助函数编译（`#ifndef LFS_NO_ASSERT`）

如果定义 `LFS_NO_ASSERT` 但自定义 `LFS_ASSERT` 为非空宏，那些辅助函数就不会被编译，但调用仍然存在 → 链接失败。

**解决**：**不要**定义 `LFS_NO_ASSERT`，改为提供自己的 `__aeabi_assert` 实现：

```c
// 在 lfs_config.h 中
static inline void __aeabi_assert(const char *expr, const char *file, int line)
{
    MY_LOG_E("[ASSERT FAIL] %s:%d: %s", file, line, expr);
    while (1) {}
}
```

这样 `<assert.h>` 可以被安全引入，`lfs_mlist_isopen` 正常编译，但断言失败时走我们的处理逻辑。

---

### 坑4：`LFS_FILE_MAX` 必须满足业务需求

**现象**：写入超过 `LFS_FILE_MAX` 的数据时，`lfs_file_write()` 返回写入字节数不足。

**根因**：`LFS_FILE_MAX` 是 LittleFS 对**单个文件大小**的硬性上限。写入超过此限制的数据会被截断。

**解决**：根据实际业务场景设置：

```c
#define LFS_FILE_MAX  (4096U)   // 参数存储场景，单文件≤4KB
// 或
#define LFS_FILE_MAX  (16384U)  // 日志存储场景，单文件≤16KB
```

> 注意：`LFS_FILE_MAX` 越大，文件系统元数据开销越大。根据实际需求平衡。

---

### 坑5：FLASH 垃圾数据导致 mount 无限循环

**现象**：在做过 FLASH 读写测试（如 T1）的分区上直接 mount，LittleFS 陷入无限循环。

**根因**：`lfs_mount()` 扫描 FLASH 时，遇到半有效的脏数据（如 0x00-0xFF 交替模式）可能无法正确判断目录状态，导致死循环。

**解决**：在首次 mount 之前，确保目标分区处于**全 0xFF 干净状态**：

```c
// 在调用 lfs_port_mount() 之前
param_flash_erase(PARTITION_BASE, PARTITION_SIZE);
```

> 这是调试阶段的必要操作。生产环境中，新出厂的设备FLASH本身就是全0xFF。

---

### 坑6：Block Device 回调未校验 block 索引

**现象**：LittleFS 传入越界的 block 编号，导致计算出非法 FLASH 地址，触发 HardFault。

**根因**：`lfs_block_read`/`prog`/`erase` 未校验 `block < c->block_count`。

**解决**：每个回调函数开头必须加边界检查：

```c
static int lfs_block_read(const struct lfs_config *c, lfs_block_t block, ...)
{
    if (block >= c->block_count) return LFS_ERR_INVAL;  // 必须有！
    // ...
}
```

---

### 坑7：GD32F50x 双 Bank FLASH 操作注意事项

**现象**：Bank1（0x08080000之后）的 FLASH 操作失败或超时。

**根因**：GD32F50x 的 1MB FLASH 分为 Bank0（0x08000000-0x0807FFFF）和 Bank1（0x08080000-0x080FFFFF）。标准外设库的 `fmc_page_erase()` 和 `fmc_word_program()` 会根据地址自动选择 Bank，但**状态标志和等待函数需要区分 Bank**。

**解决**：确保 FLASH 驱动正确处理双 Bank：
- 擦除/编程前清除对应 Bank 的标志
- 使用正确的 ready wait 函数（Bank0 用 `fmc_bank0_ready_wait`，Bank1 用 `fmc_bank1_ready_wait`）

---

### 坑8：平台适配不应分散在 Keil Define 中

**现象**：把 `LFS_NO_ASSERT`、`LFS_NO_DEBUG` 等宏放在 Keil 全局 Define 中。

**问题**：
- 配置分散，难以维护
- 切换调试/发布模式需要修改工程文件
- 不符合"单一源头"原则

**解决**：所有 LittleFS 平台适配**只在 `lfs_config.h` 中管理**，通过一个开关宏控制：

```c
#define LFS_LOG_ENABLE  1   // 改为0即为发布模式
```

Keil Define 保持干净，不包含任何 LFS 相关宏。

---

## 5. 掉电安全验证方案

### 5.1 逻辑掉电测试（Force Deinit）

模拟"写完数据但没正常关闭文件"的场景：

```c
// 写入 + sync（确保数据落盘）
lfs_file_write(&lfs, &file, data, len);
lfs_file_sync(&lfs, &file);
// 不调用 close，直接 deinit
lfs_port_deinit();  // 模拟掉电
// 重新挂载后验证数据完整性
```

### 5.2 硬件复位测试（NVIC_SystemReset）

更真实的掉电模拟：

```c
// Phase 1: 写数据 + sync → 写标记文件 → NVIC_SystemReset()
// Phase 2: 重启后检测标记文件 → 验证数据内容 → 清理
```

> `lfs_file_sync()` 是关键！LittleFS 是 copy-on-write 文件系统，未 sync 的数据不会持久化。

---

## 6. 关键配置参数说明

| 参数 | 推荐值 | 说明 |
|---|---|---|
| `LFS_BLOCK_SIZE` | FLASH扇区大小 | 必须与物理扇区对齐（GD32F50x: 调试4KB/正式2KB） |
| `LFS_CACHE_SIZE` | 256 | 读写缓存，影响性能和RAM占用 |
| `LFS_LOOKAHEAD_SIZE` | 32 | 可管理 block_count ≤ 256 |
| `LFS_BLOCK_CYCLES` | 1024 | 磨损均衡轮换阈值 |
| `LFS_FILE_MAX` | 根据业务 | 单文件大小上限 |
| `LFS_NAME_MAX` | 64 | 文件名长度（含路径） |

---

## 7. 测试结果

### 7.1 LittleFS 基础层测试（main_littlefs_test.c）

```
T1: FLASH底层驱动测试        → 7/7  PASS
T2: LittleFS挂载与格式化     → 7/7  PASS（首次擦除+format+mount）
T3: 文件创建与写入           → 6/6  PASS
T4: 文件读取与验证           → 5/5  PASS
T5: 文件删除与目录操作       → 6/6  PASS
T6: 多文件并发操作           → 6/6  PASS
T7: 掉电安全测试（逻辑掉电） → 5/5  PASS（内容校验通过）
T8: 性能测试                 → 2/2  PASS（写146KB/s, 读4MB/s）
T9: 硬件复位掉电测试         → 2/2  PASS（NVIC复位后数据完整）

Total: 46/46 ALL TESTS PASSED
```

### 7.2 参数管理层测试

在 LittleFS 基础层之上，参数管理系统（param_manager）额外通过了 98 项测试：

| 测试套件 | 用例数 | 覆盖内容 |
|----------|--------|----------|
| 功能测试 | 50 | 全部API正确性、边界条件、幂等性、未初始化防护 |
| 掉电安全 | 17 | 逻辑掉电 + NVIC硬件复位（单参数+批量） |
| RAM评估 | 10 | 静态768B、堆88B、零泄漏零碎片 |
| 压力恢复 | 21 | 磁盘满、并发79写、损坏恢复、快速init/deinit |

> 详细测试报告见 `TEST/参数管理系统测试报告.md`

---

## 8. 文件目录结构

### 8.1 总体结构

```
OC810_GD32F50X/
├── Third_Party/
│   └── littlefs/                    # LittleFS 第三方源码（仅保留核心文件）
│       ├── lfs.c                    # 文件系统核心实现
│       ├── lfs.h                    # 公共 API 接口定义（含版本号）
│       ├── lfs_util.c               # 内部工具函数（CRC等）
│       ├── lfs_util.h               # 内部宏和工具（含 #ifndef 守卫机制）
│       └── LICENSE.md               # BSD-3-Clause 许可证
│
├── project/OC810/
│   ├── code/
│   │   ├── fs/                      # 文件系统移植层（我们编写的适配代码）
│   │   │   ├── lfs_config.h         #   平台适配配置（唯一配置入口）
│   │   │   ├── lfs_port.c           #   Block Device 回调 + 初始化/挂载/卸载
│   │   │   ├── param_flash.h        #   全量FLASH分区定义 + 驱动接口
│   │   │   ├── param_flash.c        #   FLASH底层驱动（FMC操作，支持全分区）
│   │   │   ├── param_config.h       #   参数 ID 分配与存储格式配置
│   │   │   ├── param_manager.h      #   参数管理系统 API
│   │   │   └── param_manager.c      #   参数管理系统实现
│   │   │
│   │   ├── log/                     # 日志系统（被 lfs_config.h 引用）
│   │   │   ├── my_log.h             #   统一日志接口
│   │   │   ├── my_log.c             #   日志核心实现
│   │   │   ├── my_log_config.h      #   日志通道配置
│   │   │   ├── rtt_logger.h/c       #   RTT 日志通道
│   │   │   └── uart_logger.h/c      #   UART 日志通道
│   │   │
│   │   ├── memory/                  # 安全内存管理（被 lfs_config.h 引用）
│   │   │   ├── my_safe_memory.h     #   内存管理接口
│   │   │   └── my_safe_memory.c     #   基于 FreeRTOS 堆的安全封装
│   │   │
│   │   └── test/                    # 测试用例
│   │       ├── main_littlefs_test.c       # LittleFS 基础层测试（46项）
│   │       ├── main_param_manager_test.c  # 参数管理功能测试（50项）
│   │       ├── main_param_powerloss_test.c # 掉电安全测试（17项）
│   │       ├── main_param_ram_test.c      # RAM评估测试（10项）
│   │       └── main_param_stress_test.c   # 压力恢复测试（21项）
│   │
│   └── MDK-ARM/
│       └── OC810_GD32F505V.uvprojx  # Keil 工程文件
│
└── Docs/
    └── LittleFS移植到GD32F50x+FreeRTOS教程.md  # 本文档
```

### 8.2 各文件职责说明

| 文件 | 层级 | 职责 |
|---|---|---|
| `lfs_config.h` | 平台适配层 | **唯一配置入口**。日志/断言/内存重定向、LittleFS 参数、`lfs_handle_t` 定义 |
| `lfs_port.c` | 平台适配层 | Block Device 回调实现、互斥锁、初始化/挂载/卸载 |
| `param_flash.h/c` | 驱动层 | 全量分区定义 + FMC操作封装，支持全分区读/编程/擦除 |
| `param_config.h` | 配置层 | 参数 ID 分配策略、存储格式定义 |
| `param_manager.h/c` | 应用层 | 基于 LittleFS 的参数管理系统 API（上层业务调用） |
| `my_log.h/c` | 基础设施 | 统一日志输出（RTT/UART 双通道） |
| `my_safe_memory.h/c` | 基础设施 | FreeRTOS 堆管理的安全封装（分配失败日志+NULL保护） |

### 8.3 依赖关系

```
业务代码
   │
   ▼
param_manager.c      ← 参数管理 API（上层调用）
   │
   ▼
lfs_port.c           ← Block Device 回调 + 挂载管理
   │
   ├── lfs_config.h  ← 平台配置（日志/断言/内存/参数）
   │     │
   │     ├── my_log.h           ← 日志系统
   │     └── my_safe_memory.h   ← 内存管理
   │
   ├── param_flash.c ← FLASH 底层驱动（FMC操作）
   │
   └── lfs.c / lfs_util.c     ← LittleFS 第三方源码
```

### 8.4 Keil 工程配置总结

| 配置项 | 值 |
|---|---|
| **Include Path** | `code/fs`, `code/log`, `code/memory`, `Third_Party/littlefs`, ... |
| **Define** | `GD32F50X,GD32F50X_HD,GD32F505,USE_STDPERIPH_DRIVER` |
| **MiscControls** | `--include lfs_config.h`（ARMCLANG 强制包含） |

> 注意：Define 中**不包含**任何 LFS 相关宏。所有 LittleFS 适配集中在 `lfs_config.h` 中管理。

---

## 9. 注意事项清单

- [ ] Keil MiscControls 必须添加 `--include lfs_config.h`（确保在 `lfs.h` 之前生效）
- [ ] 不要修改 LittleFS 第三方源码，通过编译器强制包含实现配置注入
- [ ] 静态缓冲区必须配置（read_buffer/prog_buffer/lookahead_buffer）
- [ ] Block Device 回调必须做 block 索引边界检查
- [ ] 不要在 Keil Define 中定义 LFS 相关宏
- [ ] 首次 mount 前确保 FLASH 分区为全 0xFF（或使用 `lfs_port_format()` 格式化）
- [ ] `lfs_port_mount()` 不自动格式化，首次使用需调用 `lfs_port_format()` + `lfs_port_mount()`
- [ ] 通过 `param_manager_init()` 调用时无需关心格式化（内部自动处理）
- [ ] `LFS_FILE_MAX` 必须 ≥ 实际最大单文件大小
- [ ] FreeRTOS 环境下必须启用 `LFS_THREADSAFE`
- [ ] 提供 `__aeabi_assert` 实现（不要定义 `LFS_NO_ASSERT`）
- [ ] `lfs_file_sync()` 是数据持久化的关键，掉电前必须调用

---

## 10. 相关文档

| 文档 | 说明 |
|------|------|
| `Docs/参数保存应用设计方案.md` | 参数管理系统设计方案 + 开发者使用指南 |
| `TEST/参数管理系统测试报告.md` | 98项测试详细报告 |
