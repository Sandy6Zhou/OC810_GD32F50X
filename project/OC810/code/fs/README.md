# LittleFS 文件系统模块

## 模块结构

```
code/fs/
├── param_flash.c/h          # GD32F50x FLASH底层驱动（全量分区+全分区读写擦除）
├── lfs_port.c               # LittleFS移植层（GD32F50x + FreeRTOS）
├── lfs_config.h             # LittleFS配置定义
├── param_manager.c/h        # 参数管理系统（基于LittleFS）
├── param_config.h           # 参数管理配置
└── README_PHASE1.md         # Phase1设计文档
```

## Keil工程添加文件

### 必须添加
```
Third_Party/littlefs/lfs.c
project/OC810/code/fs/param_flash.c
project/OC810/code/fs/lfs_port.c
```

### 可选添加（参数管理功能）
```
project/OC810/code/fs/param_manager.c
```

### 可选添加（测试功能）
```
project/OC810/code/test/main_littlefs_test.c
```

## Include路径配置

在Keil `Options for Target` → `C/C++` → `Include Paths` 中添加：

```
..\code\fs
..\..\Third_Party\littlefs
```

## 编译宏定义

在 `Options for Target` → `C/C++` → `Define` 中添加：

```
PARAM_DEBUG_MODE=1    # 调试阶段（使用Bank1）
或
PARAM_DEBUG_MODE=0    # 正式阶段（使用Bank0）
```

## 模块说明

### 1. param_flash.c/h - FLASH底层驱动
- 提供全分区读/编程/擦除操作
- 地址合法性检查（遍历分区表）
- 支持调试/正式两种模式
- 适配GD32F50x FMC控制器（双Bank自动识别页大小）

### 2. lfs_port.c - LittleFS移植层
- 实现LittleFS block device回调
- FreeRTOS线程安全支持（互斥锁）
- 挂载/卸载/格式化接口

### 3. lfs_config.h - LittleFS配置
- FLASH物理参数配置
- LittleFS性能参数
- 文件系统句柄定义

### 4. param_manager.c/h - 参数管理系统
- 基于LittleFS的参数读写
- ID化参数管理
- CRC32校验保护
- 支持参数分类存储

## 使用示例

```c
#include "lfs_config.h"
#include "param_flash.h"

// 1. 初始化FLASH驱动
param_flash_init();

// 2. 初始化LittleFS
lfs_port_init();
int ret = lfs_port_mount();
if (ret == LFS_ERR_CORRUPT) {
    // 首次使用，先格式化再挂载
    lfs_port_format();
    lfs_port_mount();
}

// 3. 使用LittleFS API
lfs_file_t file;
lfs_file_open(&g_lfs_handle.lfs, &file, "/test.txt",
              LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
lfs_file_write(&g_lfs_handle.lfs, &file, "Hello", 5);
lfs_file_close(&g_lfs_handle.lfs, &file);

// 4. 卸载
lfs_port_unmount();
lfs_port_deinit();
```

## 注意事项

1. **FLASH分区**
   - 调试模式：setting_storage 0x080C7000~0x080FAFFF (208KB, Bank1)
   - 正式模式：setting_storage 0x0800C000~0x0803FFFF (208KB, Bank0零等待)
   - app/mcu_secondary 等大372KB，互为OTA镜像
   - factory_storage 20KB，bootconf 4KB

2. **线程安全**
   - 已启用LFS_THREADSAFE
   - 需要FreeRTOS信号量支持

3. **堆栈大小**
   - LittleFS任务建议至少 `configMINIMAL_STACK_SIZE * 8`

## 相关文档

- [Phase1设计文档](README_PHASE1.md)
- [LittleFS官方文档](https://github.com/littlefs-project/littlefs)
