# OC810 固件构建与 OTA 测试工具

## 📁 工具目录结构

```
tools/
├── build_firmware.bat          # 统一构建脚本（编译 + 合并）
├── merge_bin.py                # BIN 合并 Python 脚本
├── ota_test_helper.py          # OTA 测试工具（完整版）
├── nt98xx_tool.py              # NT98XX 协议工具
└── README.md                   # 工具使用说明
```

---

## 🚀 固件编译与合并

### 方法 1：Keil 自动合并（推荐）

**配置位置：** Keil → Options for Target → User → After Build/Rebuild

**APP 工程配置：**

**Run User Program #1（生成 BIN）：**
```
fromelf --bin -o .\output\Project.bin .\output\Project.axf
```

**Run User Program #2（自动合并）：**
```
..\..\..\tools\build_firmware.bat auto
```

**说明：**
- `auto` 模式会自动检测运行环境
- 编译 APP 后自动执行，无需手动操作
- 自动复制 APP 文件到 `firmware/` 目录并合并

**Keil 输出示例：**
```
linking...
Program Size: Code=81472 RO-data=8668 RW-data=88 ZI-data=80144
FromELF: creating hex file...
After Build - User command #1: fromelf --bin -o .\output\Project.bin .\output\Project.axf
After Build - User command #2: ..\..\..\tools\build_firmware.bat auto
========================================
 OC810 Firmware Build Tool
========================================

[Mode] Keil After Build Auto Merge

[1/3] Copying APP HEX to firmware...

[2/3] Merging HEX files...
[OK] Merged HEX: E:\JIMI\mDVR\OC810_GD32F50X\firmware\merged_full.hex

[3/3] Merging BIN files...
Creating merged image...
  Bootloader: 0x08000000 - 0x080060FF
  APP:        0x08040000 - 0x08056077
[OK] Merged BIN: E:\JIMI\mDVR\OC810_GD32F50X\firmware\merged_full.bin

========================================
 Complete!
========================================
```

---

### 方法 2：命令行手动合并

**在项目根目录或 tools 目录运行：**

```bash
# 合并 HEX + BIN（默认）
tools\build_firmware.bat all

# 只合并 HEX
tools\build_firmware.bat hex

# 只合并 BIN
tools\build_firmware.bat bin
```

**输出文件位置：**
```
firmware/
├── app.hex              # APP HEX（从 output 复制）
├── app.bin              # APP BIN（从 output 复制）
├── Bootloader.hex       # Bootloader HEX（编译 Bootloader 生成）
├── Bootloader.bin       # Bootloader BIN（编译 Bootloader 生成）
├── merged_full.hex      # Bootloader + APP 合并 HEX
└── merged_full.bin      # Bootloader + APP 合并 BIN
```

**命令行输出示例：**
```
========================================
 OC810 Firmware Build Tool
========================================

[Mode] Manual Merge (all)

[1/2] Merging HEX files...
[OK] Merged HEX: E:\JIMI\mDVR\OC810_GD32F50X\firmware\merged_full.hex

[2/2] Merging BIN files...
Creating merged image...
  Bootloader: 0x08000000 - 0x080060FF
  APP:        0x08040000 - 0x08056077
[OK] Merged BIN: E:\JIMI\mDVR\OC810_GD32F50X\firmware\merged_full.bin

========================================
 Complete!
========================================
```

---

## 🔧 OTA 测试工具

### OTA 测试准备

OTA 测试需要模拟从旧版本 (v1) 升级到新版本 (v2) 的场景，需要准备两个不同版本的 APP 固件。

#### 步骤 1：编译 APP v1 固件

1. 修改 `project/OC810/code/inc/my_version.h` 中的版本号：
   ```c
   #define MY_SW_VERSION_STRING    "JM-OC810MCU-STD-V1.0.260625.01"
   ```

2. 在 Keil 中编译 APP 工程

3. 编译完成后，固件自动输出到 `firmware/app.bin`

4. 复制并重命名为 APP v1：
   ```bash
   copy firmware\app.bin firmware\ota_test\app_v1.bin
   ```

#### 步骤 2：编译 APP v2 固件

1. 修改 `my_version.h` 中的版本号：
   ```c
   #define MY_SW_VERSION_STRING    "JM-OC810MCU-STD-V1.0.260625.02"
   ```

2. 在 Keil 中编译 APP 工程

3. 编译完成后，固件自动输出到 `firmware/app.bin`

4. 复制并重命名为 APP v2：
   ```bash
   copy firmware\app.bin firmware\ota_test\app_v2.bin
   ```

#### 步骤 3：准备 Bootloader 固件

> **注意**：Bootloader.bin 直接使用 `firmware/Bootloader.bin`（编译 Bootloader 时自动生成），**无需复制到 ota_test 目录**。

如果还没有编译 Bootloader：
1. 打开 Workspace `OC810 MDK-workspace.uvmpw`（推荐）或单独打开 `Bootloader/project/Bootloader.uvprojx`
2. 编译 Bootloader 工程
3. 固件自动输出到 `firmware/Bootloader.bin`

---

### ota_test_helper.py（完整版）

**用途：**
1. 计算 BIN 文件的 CRC16-CCITT 和 MD5
2. 生成 bootconf 二进制数据（设置 OTA_FLAG_PENDING）
3. 生成 J-Flash 脚本用于烧录 OTA 固件和 bootconf
4. 生成完整合并 BIN 文件（Bootloader + APP v1 + APP v2 + bootconf）

---

#### 模式 1：简单模式（分区烧录）

**用途：** 生成 bootconf 数据，用于 J-Flash 分区烧录测试

**使用方法：**

```bash
# 基本用法（使用默认 file_id = 0x12345678）
python tools/ota_test_helper.py firmware/ota_test/app_v2.bin

# 指定 file_id
python tools/ota_test_helper.py firmware/ota_test/app_v2.bin 0x22222222
```

**输出文件：**
```
firmware/ota_test/
├── app_v2_bootconf.bin   # bootconf 二进制文件（76 字节）
├── app_v2_bootconf.hex   # bootconf HEX 文件（用于 J-Flash）
└── app_v2_jflash.jlink   # J-Flash 烧录脚本
```

**输出示例：**
```
File ID: 0x12345678 -> 78563412
Reading: firmware/ota_test/app_v2.bin

=== APP Firmware Info ===
File size:    90232 bytes (88.12 KB)
CRC16:        0x558A
MD5:          4F1E09E49AA71946191FA82B4E22B3F9

=== Bootconf Data ===
Magic:        0x42434F4E
OTA Flag:     0x01 (PENDING)
File ID:      0x78563412
File Size:    90232 bytes
File CRC16:   0x558A
File MD5:     4F1E09E49AA71946191FA82B4E22B3F9
Bootconf CRC: 0xB51D

=== Bootconf Hex Dump (76 bytes) ===
0000: 4E 4F 43 42 01 00 00 00 00 00 00 00 78 56 34 12  NOCB........xV4.
0010: 4F 1E 09 E4 9A A7 19 46 19 1F A8 2B 4E 22 B3 F9  O......F...+N"..
0020: 78 60 01 00 8A 55 00 00 76 31 2E 30 2E 30 20 4A  x`...U..v1.0.0 J
0030: 75 6E 20 32 35 20 32 30 32 36 20 31 30 3A 33 35  un 25 2026 10:35
0040: 3A 34 38 00 00 00 00 00 1D B5 00 00              :48.........

Bootconf BIN saved to: firmware/ota_test/app_v2_bootconf.bin
Bootconf HEX saved to: firmware/ota_test/app_v2_bootconf.hex
J-Flash script saved to: firmware/ota_test/app_v2_jflash.jlink
```

**J-Flash 烧录步骤：**

1. 全片擦除：**Target → Manual Programming → Erase Chip**
2. 烧录 Bootloader：`firmware/Bootloader.bin` → `0x08000000`
3. 烧录 APP v1：`firmware/ota_test/app_v1.bin` → `0x08040000`
4. 烧录 APP v2：`firmware/ota_test/app_v2.bin` → `0x0809E000`
5. 烧录 bootconf：`firmware/ota_test/app_v2_bootconf.hex`（HEX 自带地址）
6. 关闭 J-Flash，打开 RTT Viewer，复位芯片

---

#### 模式 2：完整模式（一次性烧录）

**用途：** 生成包含所有分区的完整 Flash BIN 文件（1MB），用于一次性烧录

**使用方法：**

```bash
python tools/ota_test_helper.py firmware/ota_test/app_v2.bin 0x22222222 --full-bin firmware/Bootloader.bin firmware/ota_test/app_v1.bin
```

**参数说明：**
| 参数 | 说明 | 示例 |
|------|------|------|
| 第 1 个参数 | APP v2 固件文件 | `firmware/ota_test/app_v2.bin` |
| 第 2 个参数 | file_id（可选，默认 0x12345678） | `0x22222222` |
| `--full-bin` | 生成完整合并 BIN 的标志 | - |
| 第 4 个参数 | Bootloader 文件 | `firmware/Bootloader.bin` |
| 第 5 个参数 | APP v1 文件 | `firmware/ota_test/app_v1.bin` |

**输出文件：**
```
firmware/ota_test/
├── app_v2_bootconf.bin      # bootconf 二进制文件
├── app_v2_bootconf.hex      # bootconf HEX 文件
├── app_v2_jflash.jlink      # J-Flash 烧录脚本
└── ota_v1_to_v2.bin         # 完整 Flash BIN（1MB）
```

**输出示例：**
```
File ID: 0x22222222 -> 22222222
Loaded Bootloader: firmware/Bootloader.bin (24832 bytes)
Loaded APP v1: firmware/ota_test/app_v1.bin (90232 bytes)
Reading: firmware/ota_test/app_v2.bin

=== APP Firmware Info ===
File size:    90232 bytes (88.12 KB)
CRC16:        0x558A
MD5:          4F1E09E49AA71946191FA82B4E22B3F9

=== Bootconf Data ===
Magic:        0x42434F4E
OTA Flag:     0x01 (PENDING)
File ID:      0x22222222
File Size:    90232 bytes
File CRC16:   0x558A
File MD5:     4F1E09E49AA71946191FA82B4E22B3F9
Bootconf CRC: 0xA012

=== Bootconf Hex Dump (76 bytes) ===
0000: 4E 4F 43 42 01 00 00 00 00 00 00 00 22 22 22 22  NOCB........""""
0010: 4F 1E 09 E4 9A A7 19 46 19 1F A8 2B 4E 22 B3 F9  O......F...+N"..
0020: 78 60 01 00 8A 55 00 00 76 31 2E 30 2E 30 20 4A  x`...U..v1.0.0 J
0030: 75 6E 20 32 35 20 32 30 32 36 20 31 30 3A 33 35  un 25 2026 10:35
0040: 3A 34 38 00 00 00 00 00 12 A0 00 00              :48.........

Bootconf BIN saved to: firmware/ota_test/app_v2_bootconf.bin
Bootconf HEX saved to: firmware/ota_test/app_v2_bootconf.hex
J-Flash script saved to: firmware/ota_test/app_v2_jflash.jlink

=== Generating Full Flash BIN ===
Writing Bootloader (24832 bytes) to 0x08000000...
Writing APP v1 (90232 bytes) to 0x08040000...
Writing APP v2 (90232 bytes) to 0x0809E000...
Writing bootconf (76 bytes) to 0x080FC000...
Full Flash BIN saved to: firmware/ota_test\ota_v1_to_v2.bin
Total size: 1048576 bytes (1024.00 KB)

=== Flash Layout Summary ===
0x08000000 - Bootloader (48 KB)
0x08040000 - APP v1 (90232 bytes)
0x0809E000 - APP v2/OTA (90232 bytes)
0x080FC000 - bootconf (76 bytes, OTA pending)
0x080FD000 - factory (empty, 0xFF)

Now you can flash 'firmware/ota_test\ota_v1_to_v2.bin' with J-Flash directly!
```

**完整 BIN 文件布局：**
```
地址            分区              大小      说明
0x08000000     Bootloader        48 KB     引导程序
0x0800C000     setting_storage   208 KB    配置存储（填充 0xFF）
0x08040000     APP v1            376 KB    当前运行固件
0x0809E000     APP v2/OTA        376 KB    OTA 升级固件
0x080FC000     bootconf          4 KB      启动配置（OTA pending）
0x080FD000     factory_storage   12 KB     工厂数据（填充 0xFF）
```

**J-Flash 烧录步骤（简化版）：**
1. 全片擦除
2. 烧录 `firmware/ota_test/ota_v1_to_v2.bin` → `0x08000000`
3. 关闭 J-Flash，打开 RTT Viewer，复位芯片
4. 观察 OTA 升级日志

---

## 📊 Flash 分区布局

| 分区 | 地址范围 | 大小 | 说明 |
|------|----------|------|------|
| Bootloader | 0x08000000 ~ 0x0800BFFF | 48 KB | 引导程序 |
| setting_storage | 0x0800C000 ~ 0x0803FFFF | 208 KB | 参数存储（LittleFS） |
| APP | 0x08040000 ~ 0x0809DFFF | 376 KB | 应用程序 |
| OTA | 0x0809E000 ~ 0x080FBFFF | 376 KB | OTA 固件镜像 |
| bootconf | 0x080FC000 ~ 0x080FCFFF | 4 KB | 启动配置 |
| factory_storage | 0x080FD000 ~ 0x080FFFFF | 12 KB | 工厂数据 |

---

## 🔍 OTA 升级验证流程

### 1. 生成 OTA 测试文件

```bash
# 方法 1：简单模式（分区烧录）
python tools/ota_test_helper.py firmware/ota_test/app_v2.bin 0x22222222

# 方法 2：完整模式（一次性烧录）
python tools/ota_test_helper.py firmware/ota_test/app_v2.bin 0x22222222 --full-bin firmware/Bootloader.bin firmware/ota_test/app_v1.bin
```

### 2. 烧录固件

**分区烧录（方法 1）：**
- Bootloader → `0x08000000`
- APP v1 → `0x08040000`
- APP v2 → `0x0809E000`
- bootconf → `0x080FC000`（使用 HEX 文件）

**完整烧录（方法 2）：**
- `ota_v1_to_v2.bin` → `0x08000000`

### 3. 验证 OTA 升级

**预期日志：**
```
[INF] main ==============================
[INF] main Bootloader:v1.0.0 Jun 26 2026 10:52:49
[INF] main ==============================
[INF] main bootconf OK
[INF] main OTA upgrade pending, file_id=0x22222222, size=90232, crc16=0x558A
[INF] main OTA MD5: 4F1E09E49AA71946191FA82B4E22B3F9
[INF] my_bl_ota_upgrade OTA: verifying source firmware CRC16...
[INF] my_bl_ota_upgrade OTA: source CRC16 OK
[INF] my_bl_ota_upgrade OTA: verifying source firmware MD5...
[INF] my_bl_ota_upgrade OTA: source MD5 OK
[INF] my_bl_ota_upgrade OTA: erasing APP area (376 KB)...
[INF] my_bl_ota_upgrade OTA: copying firmware...
[INF] my_bl_ota_upgrade OTA: copy complete
[INF] my_bl_ota_upgrade OTA: verifying copy...
[INF] my_bl_ota_upgrade OTA: upgrade completed successfully
[INF] main OTA success
[INF] main Jumping to APP @ 0x08040000...
[INF] main ==============================


[INF] init_task ========================================
[INF] init_task GD32F505VGT7 FreeRTOS mDVR Project
[INF] init_task System Core Clock: 120000000 Hz
[INF] init_task FreeRTOS Heap Size: 65536 bytes
[INF] init_task FreeRTOS Version: V10.3.1
[INF] init_task app Version: JM-OC810MCU-STD-V1.0.260626.02 (Build: Jun 26 2026 10:52:02)
[INF] init_task ========================================
[INF] my_main_init Main task module initialized
[INF] init_task System initialization complete, init_task deleted.
[INF] main_task_entry Main task started
...
```

### 4. 验证要点

- ✅ CRC16 校验通过（Python 端与 C 端一致）
- ✅ MD5 校验通过（RFC 1321 标准）
- ✅ 固件复制成功
- ✅ OTA 成功后直接跳转 APP（无需重启）
- ✅ RTT 日志连续输出（Bootloader → APP 无断连）

---

## 🔧 依赖环境

| 工具 | 用途 | 必需性 |
|------|------|--------|
| **Python 3.6+** | 合并 BIN、OTA 测试工具 | ✅ 必需 |
| **Keil MDK V6.15+** | 编译固件 | ✅ 必需 |
| **J-Flash** | 烧录固件 | ✅ 调试必需 |
| **J-Link RTT Viewer** | 查看日志 | ✅ 调试必需 |

---

## ⚠️ 注意事项

1. **编译顺序** - 必须先编译 Bootloader，再编译 APP
2. **J-Flash 与 RTT Viewer 互斥** - 不能同时运行，会独占 J-Link
3. **Flash 填充** - BIN 合并时用 `0xFF` 填充空白区域
4. **bootconf checksum** - 修改 file_id 等字段后必须重新生成 bootconf
5. **OTA 跳转优化** - OTA 成功后直接跳转 APP，无需软件复位
6. **文件编码** - 批处理文件必须使用 ANSI 编码，避免 CMD 乱码

---

## 🐛 常见问题

### Q1: 提示 "Python not found"
**解决：** 安装 Python 3.6+ 并添加到系统 PATH

### Q2: 合并后 BIN 文件过大（1MB）
**原因：** BIN 文件包含整个 Flash 空间
**正常：** 1MB 是预期大小

### Q3: RTT Viewer 看不到 APP 日志
**原因：** J-Flash 未关闭，独占 J-Link
**解决：** 关闭 J-Flash 后重新打开 RTT Viewer

### Q4: OTA 升级后 ota_flag 为 0
**原因：** OTA 成功后自动清除标志
**正常：** 这是预期行为

### Q5: 提示 "Bootloader HEX not found"
**解决：** 确保先编译了 Bootloader 工程

### Q6: Keil After Build 脚本不执行
**原因：** 路径配置错误
**解决：** 检查 After Build 命令，确保使用相对路径 `..\..\..\tools\build_firmware.bat auto`

---

## 📝 版本历史

- **2026-06-25** - 统一构建脚本 build_firmware.bat（支持 Keil 自动合并）
- **2026-06-25** - 合并 OTA 测试工具为单一脚本 ota_test_helper.py
- **2026-06-25** - 新增 OTA 测试工具（CRC16 + MD5 双重校验）
- **2026-06-25** - 优化 OTA 跳转流程（直接跳转，无需重启）
- **2026-06-24** - 初始版本，支持 HEX/BIN 合并
- 工具位置从 `project/OC810/` 迁移到 `tools/`
