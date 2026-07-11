# RTT 配置说明

本文档说明 Segger RTT 在 Bootloader 和 APP 之间的共享配置策略。

## 配置文件位置

**统一配置文件**: `Third_Party/Segger_RTT/RTT/RTT/SEGGER_RTT_Conf.h`

Bootloader 和 APP 工程都使用此配置文件，确保配置一致性。

---

## 关键配置项

### 1. 缓冲区数量

```c
#define SEGGER_RTT_MAX_NUM_UP_BUFFERS    (3)  // 上行缓冲区数量（Target → Host）
#define SEGGER_RTT_MAX_NUM_DOWN_BUFFERS  (3)  // 下行缓冲区数量（Host → Target）
```

**说明**:
- 上行缓冲区：用于日志输出（RTT Viewer 显示）
- 下行缓冲区：用于键盘输入（RTT Shell 交互）
- Bootloader 和 APP 使用相同的数量配置

### 2. 缓冲区大小

```c
#define BUFFER_SIZE_UP                   (1024)  // 上行缓冲区大小（字节）
#define BUFFER_SIZE_DOWN                 (16)    // 下行缓冲区大小（字节）
```

**说明**:
- **BUFFER_SIZE_UP = 1024**: 足够容纳大部分日志消息
- **BUFFER_SIZE_DOWN = 16**: Shell 输入通常很短
- ⚠️ **重要**: 不要在 Bootloader 或 APP 中修改此值，否则可能导致 IAP 跳转时 RTT 连接中断

### 3. Printf 缓冲区

```c
#define SEGGER_RTT_PRINTF_BUFFER_SIZE    (64u)  // printf 批量发送缓冲区
```

**说明**:
- 用于 `SEGGER_RTT_printf()` 函数
- 64 字节足够大多数 printf 调用

### 4. 默认传输模式

```c
#define SEGGER_RTT_MODE_DEFAULT          SEGGER_RTT_MODE_NO_BLOCK_SKIP
```

**模式说明**:
- `SEGGER_RTT_MODE_NO_BLOCK_SKIP`: 缓冲区满时跳过新数据（不阻塞）
- 适用于日志输出场景，避免日志堵塞系统

**其他模式**（供参考）:
- `SEGGER_RTT_MODE_NO_BLOCK_TRIM`: 缓冲区满时截断新数据
- `SEGGER_RTT_MODE_BLOCK_IF_FIFO_FULL`: 缓冲区满时阻塞等待（不推荐用于日志）

---

## IAP 跳转 RTT 连续性

### 为什么能保持连续？

1. **RTT 控制块在 RAM 中** - 跳转时不清零
2. **缓冲区配置一致** - Bootloader 和 APP 使用相同的 `SEGGER_RTT_Conf.h`
3. **不重新初始化** - APP 启动时检测 RTT 已初始化，直接使用

### 跳转流程

```
Bootloader 运行
    ↓
SEGGER_RTT_Init() 初始化 RTT 控制块
    ↓
输出日志: "Boootloader starting..."
    ↓
跳转到 APP (0x08040000)
    ↓
APP 启动
    ↓
SEGGER_RTT_Init() 检测已初始化，跳过
    ↓
继续输出日志: "APP starting..."
    ↓
RTT Viewer 看到连续日志（无中断）
```

### 注意事项

⚠️ **禁止操作**:
1. 不要在跳转前调用 `SEGGER_RTT_Terminate()`
2. 不要修改 Bootloader 或 APP 的 RTT 缓冲区大小
3. 不要在跳转时清零 SRAM（RTT 控制块会被清除）

✅ **推荐操作**:
1. 使用统一的 `SEGGER_RTT_Conf.h` 配置文件
2. 跳转前延时 100ms，确保 RTT 缓冲区数据被 J-Link 读取
3. APP 启动时检查 RTT 是否已初始化，避免重复初始化

---

## 多缓冲区使用

### 缓冲区分配建议

| 缓冲区 | 用途 | 方向 | 使用场景 |
|--------|------|------|----------|
| **Buffer 0** | RTT 日志 | Up | 主要日志输出（ERROR/WARN/INFO/DEBUG） |
| **Buffer 1** | SystemView | Up | 性能分析（可选） |
| **Buffer 2** | Shell 输入 | Down | RTT Shell 交互式命令行 |

### 添加自定义缓冲区

如果需要添加自定义缓冲区（例如：单独的错误日志通道）：

```c
// 1. 增加缓冲区数量（SEGGER_RTT_Conf.h）
#define SEGGER_RTT_MAX_NUM_UP_BUFFERS  (4)  // 增加到 4 个

// 2. 在代码中创建缓冲区
static char my_error_buffer[512];
int error_channel = SEGGER_RTT_AllocUpBuffer(512, my_error_buffer, SEGGER_RTT_MODE_NO_BLOCK_SKIP);

// 3. 使用自定义缓冲区发送数据
SEGGER_RTT_Write(error_channel, "Error: ...", 10);
```

⚠️ **注意**: 修改缓冲区数量后，Bootloader 和 APP 必须同步更新！

---

## 中断优先级配置

```c
#define SEGGER_RTT_MAX_INTERRUPT_PRIORITY  (0x20)  // RTT 锁中断优先级
```

**说明**:
- 用于保护 RTT 写入操作的临界区
- Cortex-M33 使用 BASEPRI 寄存器实现
- 0x20 = 优先级 2（高于此优先级的中断不会被屏蔽）

**与 FreeRTOS 的关系**:
- FreeRTOS 的 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 通常为 5
- RTT 的 0x20 优先级高于 FreeRTOS 系统调用优先级
- 确保 RTT 日志不会被 FreeRTOS 中断打断

---

## 调试技巧

### 1. 检查 RTT 是否正常工作

```c
// 在代码中添加测试输出
SEGGER_RTT_printf(0, "RTT Test: %d\n", 123);
```

如果 RTT Viewer 能看到输出，说明 RTT 正常工作。

### 2. 检查缓冲区使用情况

```c
// 获取缓冲区状态
SEGGER_RTT_BUFFERUP* pBuffer = &_SEGGER_RTT.aUp[0];
uint32_t write_pos = pBuffer->WrOff;
uint32_t read_pos = pBuffer->RdOff;
uint32_t used = (write_pos >= read_pos) ?
                (write_pos - read_pos) :
                (pBuffer->SizeOfBuffer - read_pos + write_pos);

SEGGER_RTT_printf(0, "RTT Buffer 0: %lu/%lu used\n", used, pBuffer->SizeOfBuffer);
```

### 3. RTT Viewer 连接问题

如果 RTT Viewer 无法连接：

1. **检查 J-Link 驱动** - 确保安装了最新版 J-Link Software
2. **检查目标芯片** - 确保芯片已供电且正常运行
3. **检查 SWD 连接** - 确保 SWDIO/SWCLK/GND 连接正确
4. **重启 RTT Viewer** - 有时需要重新连接

---

## 版本信息

- **RTT 版本**: V7.58b
- **配置文件**: `SEGGER_RTT_Conf.h`
- **更新日期**: 2026-06-24

---

**维护者**: 伍玉蛟 (wuyujiao@jimiiot.com)
