#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OTA 测试辅助工具（完整版）
功能：
1. 计算 BIN 文件的 CRC16-CCITT 和 MD5
2. 生成 bootconf 二进制数据（设置 OTA_FLAG_PENDING）
3. 生成 J-Flash 脚本用于烧录 OTA 固件和 bootconf
4. 生成完整合并 BIN 文件（Bootloader + APP v1 + APP v2 + bootconf）

使用示例：
  # 简单模式：仅生成 bootconf 和 J-Flash 脚本
  python ota_test_helper.py firmware/ota_test/app_v2.bin 0x12345678

  # 完整模式：生成包含所有分区的 1MB Flash BIN
  python ota_test_helper.py firmware/ota_test/app_v2.bin 0x12345678 --full-bin firmware/Bootloader.bin firmware/ota_test/app_v1.bin
"""

import struct
import sys
import os
import hashlib

# Flash 分区地址
FLASH_BOOTLOADER_BASE = 0x08000000    # Bootloader 分区起始地址
FLASH_SETTING_BASE = 0x0800C000       # setting_storage 分区起始地址
FLASH_APP_BASE = 0x08040000           # APP 分区起始地址
FLASH_OTA_BASE = 0x0809E000           # OTA 分区起始地址
FLASH_BOOTCONF_BASE = 0x080FC000      # bootconf 分区起始地址
FLASH_FACTORY_BASE = 0x080FD000       # factory_storage 分区起始地址
FLASH_END = 0x08100000                # Flash 结束地址（1MB）

# bootconf 常量
BOOTCONF_MAGIC = 0x42434F4E    # "BCON"
OTA_FLAG_PENDING = 0x01
OTA_FLAG_NONE = 0x00

# bootconf 结构体大小（76 字节）
# magic(4) + ota_flag(4) + last_boot_status(4) + file_id(4) + file_md5(16)
# + file_size(4) + file_crc16(2) + reserved(2) + version[32](32) + checksum(4) = 76
BOOTCONF_SIZE = 76


def md5_calc(data):
    """计算 MD5（与 C 端一致）"""
    return hashlib.md5(data).digest()  # 返回 16 字节原始数据


def crc16_table(data):
    """计算 CRC16-CCITT（查表法，与 GD32 Bootloader 一致）"""
    # CRC16-CCITT 查找表（多项式 0x1021）
    crc16_table = [
        0X0000, 0X1189, 0X2312, 0X329B, 0X4624, 0X57AD, 0X6536, 0X74BF,
        0X8C48, 0X9DC1, 0XAF5A, 0XBED3, 0XCA6C, 0XDBE5, 0XE97E, 0XF8F7,
        0X1081, 0X0108, 0X3393, 0X221A, 0X56A5, 0X472C, 0X75B7, 0X643E,
        0X9CC9, 0X8D40, 0XBFDB, 0XAE52, 0XDAED, 0XCB64, 0XF9FF, 0XE876,
        0X2102, 0X308B, 0X0210, 0X1399, 0X6726, 0X76AF, 0X4434, 0X55BD,
        0XAD4A, 0XBCC3, 0X8E58, 0X9FD1, 0XEB6E, 0XFAE7, 0XC87C, 0XD9F5,
        0X3183, 0X200A, 0X1291, 0X0318, 0X77A7, 0X662E, 0X54B5, 0X453C,
        0XBDCB, 0XAC42, 0X9ED9, 0X8F50, 0XFBEF, 0XEA66, 0XD8FD, 0XC974,
        0X4204, 0X538D, 0X6116, 0X709F, 0X0420, 0X15A9, 0X2732, 0X36BB,
        0XCE4C, 0XDFC5, 0XED5E, 0XFCD7, 0X8868, 0X99E1, 0XAB7A, 0XBAF3,
        0X5285, 0X430C, 0X7197, 0X601E, 0X14A1, 0X0528, 0X37B3, 0X263A,
        0XDECD, 0XCF44, 0XFDDF, 0XEC56, 0X98E9, 0X8960, 0XBBFB, 0XAA72,
        0X6306, 0X728F, 0X4014, 0X519D, 0X2522, 0X34AB, 0X0630, 0X17B9,
        0XEF4E, 0XFEC7, 0XCC5C, 0XDDD5, 0XA96A, 0XB8E3, 0X8A78, 0X9BF1,
        0X7387, 0X620E, 0X5095, 0X411C, 0X35A3, 0X242A, 0X16B1, 0X0738,
        0XFFCF, 0XEE46, 0XDCDD, 0XCD54, 0XB9EB, 0XA862, 0X9AF9, 0X8B70,
        0X8408, 0X9581, 0XA71A, 0XB693, 0XC22C, 0XD3A5, 0XE13E, 0XF0B7,
        0X0840, 0X19C9, 0X2B52, 0X3ADB, 0X4E64, 0X5FED, 0X6D76, 0X7CFF,
        0X9489, 0X8500, 0XB79B, 0XA612, 0XD2AD, 0XC324, 0XF1BF, 0XE036,
        0X18C1, 0X0948, 0X3BD3, 0X2A5A, 0X5EE5, 0X4F6C, 0X7DF7, 0X6C7E,
        0XA50A, 0XB483, 0X8618, 0X9791, 0XE32E, 0XF2A7, 0XC03C, 0XD1B5,
        0X2942, 0X38CB, 0X0A50, 0X1BD9, 0X6F66, 0X7EEF, 0X4C74, 0X5DFD,
        0XB58B, 0XA402, 0X9699, 0X8710, 0XF3AF, 0XE226, 0XD0BD, 0XC134,
        0X39C3, 0X284A, 0X1AD1, 0X0B58, 0X7FE7, 0X6E6E, 0X5CF5, 0X4D7C,
        0XC60C, 0XD785, 0XE51E, 0XF497, 0X8028, 0X91A1, 0XA33A, 0XB2B3,
        0X4A44, 0X5BCD, 0X6956, 0X78DF, 0X0C60, 0X1DE9, 0X2F72, 0X3EFB,
        0XD68D, 0XC704, 0XF59F, 0XE416, 0X90A9, 0X8120, 0XB3BB, 0XA232,
        0X5AC5, 0X4B4C, 0X79D7, 0X685E, 0X1CE1, 0X0D68, 0X3FF3, 0X2E7A,
        0XE70E, 0XF687, 0XC41C, 0XD595, 0XA12A, 0XB0A3, 0X8238, 0X93B1,
        0X6B46, 0X7ACF, 0X4854, 0X59DD, 0X2D62, 0X3CEB, 0X0E70, 0X1FF9,
        0XF78F, 0XE606, 0XD49D, 0XC514, 0XB1AB, 0XA022, 0X92B9, 0X8330,
        0X7BC7, 0X6A4E, 0X58D5, 0X495C, 0X3DE3, 0X2C6A, 0X1EF1, 0X0F78
    ]

    fcs = 0xFFFF
    for byte in data:
        fcs = (fcs >> 8) ^ crc16_table[(fcs ^ byte) & 0xFF]
    return (~fcs) & 0xFFFF  # 输出取反，限制为 16 位



def generate_bootconf(image_size, image_crc16, image_md5, file_id=None):
    """生成 bootconf 二进制数据"""
    # bootconf 结构体（与 C 语言完全一致，76 字节）：
    # uint32_t magic;                     // 0x42434F4E
    # uint32_t ota_flag;                  // 0x01 = PENDING
    # uint32_t last_boot_status;          // 0x00 = NORMAL
    # uint8_t  file_id[4];                // 固件文件 ID
    # uint8_t  file_md5[16];              // 固件 MD5 校验值
    # uint32_t file_size;                 // 固件大小
    # uint16_t file_crc16;                // 固件 CRC16
    # uint16_t reserved;                  // 保留字段（4字节对齐）
    # char     version[32];               // Bootloader 版本
    # uint32_t checksum;                  // bootconf 自身 CRC16

    if file_id is None:
        file_id = b'\x00\x00\x00\x00'  # 默认全 0

    version_str = b"v1.0.0 Jun 25 2026 10:35:48"  # 与 Bootloader 打印一致
    version_padded = version_str.ljust(32, b'\x00')[:32]

    # 打包前 60 字节（不含 checksum）
    data_no_crc = b''
    data_no_crc += struct.pack('<I', BOOTCONF_MAGIC)      # magic (4)
    data_no_crc += struct.pack('<I', OTA_FLAG_PENDING)     # ota_flag (4)
    data_no_crc += struct.pack('<I', 0x00)                 # last_boot_status (4)
    data_no_crc += file_id[:4].ljust(4, b'\x00')          # file_id[4] (4)
    data_no_crc += image_md5[:16].ljust(16, b'\x00')      # file_md5[16] (16)
    data_no_crc += struct.pack('<I', image_size)           # file_size (4)
    data_no_crc += struct.pack('<H', image_crc16)          # file_crc16 (2)
    data_no_crc += struct.pack('<H', 0x0000)               # reserved (2)
    data_no_crc += version_padded                          # version[32] (32)
    # 总计: 4+4+4+4+16+4+2+2+32 = 72 字节

    # 计算 bootconf 自身 CRC16（前 72 字节）
    checksum = crc16_table(data_no_crc[:72])

    # 完整 bootconf 数据（追加 checksum）
    bootconf_data = data_no_crc[:72] + struct.pack('<I', checksum)

    assert len(bootconf_data) == BOOTCONF_SIZE, f"bootconf size mismatch: {len(bootconf_data)} != {BOOTCONF_SIZE}"

    return bootconf_data, checksum


def generate_full_bin(bootloader_bin, app_v1_bin, app_v2_bin, bootconf_data, output_file):
    """
    生成完整的 Flash BIN 文件（包含所有分区）

    分区布局：
    - Bootloader (48KB):  0x08000000 ~ 0x0800BFFF
    - setting_storage:    0x0800C000 ~ 0x0803FFFF (填充 0xFF)
    - APP v1 (376KB):     0x08040000 ~ 0x0809DFFF
    - APP v2/OTA (376KB): 0x0809E000 ~ 0x080FBFFF
    - bootconf (4KB):     0x080FC000 ~ 0x080FCFFF
    - factory (12KB):     0x080FD000 ~ 0x080FFFFF (填充 0xFF)
    """
    print(f"\n=== Generating Full Flash BIN ===")

    # 创建完整 Flash 镜像（1MB，初始化为 0xFF）
    flash_size = FLASH_END - FLASH_BOOTLOADER_BASE  # 1MB
    flash_data = bytearray(b'\xFF' * flash_size)

    # 1. 写入 Bootloader (0x08000000)
    print(f"Writing Bootloader ({len(bootloader_bin)} bytes) to 0x{FLASH_BOOTLOADER_BASE:08X}...")
    offset = FLASH_BOOTLOADER_BASE - FLASH_BOOTLOADER_BASE
    flash_data[offset:offset + len(bootloader_bin)] = bootloader_bin

    # 2. APP v1 保持 0xFF（首次烧录不需要 APP）
    # 如果需要包含 APP v1，取消下面的注释
    if app_v1_bin:
        print(f"Writing APP v1 ({len(app_v1_bin)} bytes) to 0x{FLASH_APP_BASE:08X}...")
        offset = FLASH_APP_BASE - FLASH_BOOTLOADER_BASE
        flash_data[offset:offset + len(app_v1_bin)] = app_v1_bin

    # 3. 写入 APP v2 到 OTA 分区 (0x0809E000)
    print(f"Writing APP v2 ({len(app_v2_bin)} bytes) to 0x{FLASH_OTA_BASE:08X}...")
    offset = FLASH_OTA_BASE - FLASH_BOOTLOADER_BASE
    flash_data[offset:offset + len(app_v2_bin)] = app_v2_bin

    # 4. 写入 bootconf (0x080FC000)
    print(f"Writing bootconf ({len(bootconf_data)} bytes) to 0x{FLASH_BOOTCONF_BASE:08X}...")
    offset = FLASH_BOOTCONF_BASE - FLASH_BOOTLOADER_BASE
    flash_data[offset:offset + len(bootconf_data)] = bootconf_data

    # 5. 保存完整 BIN 文件
    with open(output_file, 'wb') as f:
        f.write(flash_data)

    print(f"Full Flash BIN saved to: {output_file}")
    print(f"Total size: {len(flash_data)} bytes ({len(flash_data) / 1024:.2f} KB)")

    return output_file


def main():
    if len(sys.argv) < 2:
        print("Usage: python ota_test_helper.py <app_v2.bin> [file_id] [--full-bin bootloader.bin app_v1.bin]")
        print("Example 1: python ota_test_helper.py firmware/ota_test/app_v2.bin 0x12345678")
        print("Example 2: python ota_test_helper.py firmware/ota_test/app_v2.bin 0x12345678 --full-bin firmware/Bootloader.bin firmware/ota_test/app_v1.bin")
        sys.exit(1)

    bin_file = sys.argv[1]

    # 解析 file_id（可选参数，默认 0x12345678）
    file_id_hex = sys.argv[2] if len(sys.argv) >= 3 and sys.argv[2] != '--full-bin' else "0x12345678"
    file_id_val = int(file_id_hex, 16)
    file_id = struct.pack('<I', file_id_val)  # 小端序打包
    print(f"File ID: {file_id_hex} -> {file_id.hex().upper()}")

    # 解析 --full-bin 参数（可选）
    bootloader_bin = None
    app_v1_bin = None
    if '--full-bin' in sys.argv:
        idx = sys.argv.index('--full-bin')
        if idx + 2 < len(sys.argv):
            bootloader_file = sys.argv[idx + 1]
            app_v1_file = sys.argv[idx + 2]

            if os.path.exists(bootloader_file):
                with open(bootloader_file, 'rb') as f:
                    bootloader_bin = f.read()
                print(f"Loaded Bootloader: {bootloader_file} ({len(bootloader_bin)} bytes)")
            else:
                print(f"Warning: Bootloader file not found: {bootloader_file}")

            if os.path.exists(app_v1_file):
                with open(app_v1_file, 'rb') as f:
                    app_v1_bin = f.read()
                print(f"Loaded APP v1: {app_v1_file} ({len(app_v1_bin)} bytes)")
            else:
                print(f"Warning: APP v1 file not found: {app_v1_file}")
        else:
            print("Error: --full-bin requires two arguments: <bootloader.bin> <app_v1.bin>")
            sys.exit(1)

    if not os.path.exists(bin_file):
        print(f"Error: File not found: {bin_file}")
        sys.exit(1)

    # 1. 读取 BIN 文件
    print(f"Reading: {bin_file}")
    with open(bin_file, 'rb') as f:
        bin_data = f.read()

    image_size = len(bin_data)
    image_crc16 = crc16_table(bin_data)
    image_md5 = md5_calc(bin_data)

    print(f"\n=== APP Firmware Info ===")
    print(f"File size:    {image_size} bytes ({image_size / 1024:.2f} KB)")
    print(f"CRC16:        0x{image_crc16:04X}")
    print(f"MD5:          {image_md5.hex().upper()}")

    # 2. 生成 bootconf
    bootconf_data, bootconf_crc = generate_bootconf(image_size, image_crc16, image_md5, file_id)

    print(f"\n=== Bootconf Data ===")
    print(f"Magic:        0x{BOOTCONF_MAGIC:08X}")
    print(f"OTA Flag:     0x{OTA_FLAG_PENDING:02X} (PENDING)")
    print(f"File ID:      0x{file_id.hex().upper()}")
    print(f"File Size:    {image_size} bytes")
    print(f"File CRC16:   0x{image_crc16:04X}")
    print(f"File MD5:     {image_md5.hex().upper()}")
    print(f"Bootconf CRC: 0x{bootconf_crc:04X}")

    # 输出十六进制 dump（方便与 J-Flash 读回的数据对比）
    print(f"\n=== Bootconf Hex Dump (76 bytes) ===")
    for i in range(0, len(bootconf_data), 16):
        chunk = bootconf_data[i:i+16]
        hex_str = ' '.join(f'{b:02X}' for b in chunk)
        ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
        print(f"{i:04X}: {hex_str:<48s} {ascii_str}")

    # 3. 保存 bootconf 二进制文件（用于查看）
    bootconf_bin_file = bin_file.replace('.bin', '_bootconf.bin')
    with open(bootconf_bin_file, 'wb') as f:
        f.write(bootconf_data)
    print(f"\nBootconf BIN saved to: {bootconf_bin_file}")

    # 4. 生成 bootconf HEX 文件（用于 J-Flash 烧录）
    bootconf_hex_file = bin_file.replace('.bin', '_bootconf.hex')
    with open(bootconf_hex_file, 'w') as f:
        # Intel HEX 格式
        # 扩展线性地址记录（高 16 位地址）
        high_addr = FLASH_BOOTCONF_BASE >> 16
        checksum = (0x02 + 0x00 + 0x04 + (high_addr >> 8) + (high_addr & 0xFF)) & 0xFF
        checksum = (0x100 - checksum) & 0xFF
        f.write(f":02000004{high_addr:04X}{checksum:02X}\n")

        # 数据记录（64 字节，分 4 行，每行 16 字节）
        offset = FLASH_BOOTCONF_BASE & 0xFFFF
        for i in range(0, BOOTCONF_SIZE, 16):
            chunk = bootconf_data[i:i+16]
            length = len(chunk)
            addr = offset + i
            record_type = 0x00  # 数据记录

            # 计算校验和
            checksum = (length + (addr >> 8) + (addr & 0xFF) + record_type) & 0xFF
            checksum += sum(chunk)
            checksum = (0x100 - checksum) & 0xFF

            # 写入记录
            data_str = ''.join(f'{b:02X}' for b in chunk)
            f.write(f":{length:02X}{addr:04X}{record_type:02X}{data_str}{checksum:02X}\n")

        # 结束记录
        f.write(":00000001FF\n")

    print(f"Bootconf HEX saved to: {bootconf_hex_file}")

    # 5. 生成 J-Flash 脚本
    jflash_script = f"""// J-Link Flash Script for OTA Test
// Auto-generated by ota_test_helper.py

// 1. 擦除 OTA 分区 (376KB)
erase {FLASH_OTA_BASE}, {FLASH_OTA_BASE + 0x5E000 - 1}

// 2. 写入 APP v2 固件到 OTA 分区
loadbin "{os.path.abspath(bin_file)}", {FLASH_OTA_BASE:#010X}

// 3. 写入 bootconf HEX（设置 OTA_FLAG_PENDING）
// 注意：HEX 文件自带地址信息，无需指定地址
loadhex "{os.path.abspath(bootconf_hex_file)}"

// 4. 验证写入（可选）
// verifybin "{os.path.abspath(bin_file)}", {FLASH_OTA_BASE:#010X}

// 完成后手动复位芯片触发 OTA 升级
"""

    jflash_file = bin_file.replace('.bin', '_jflash.jlink')
    with open(jflash_file, 'w') as f:
        f.write(jflash_script)
    print(f"J-Flash script saved to: {jflash_file}")

    # 6. 如果提供了 --full-bin 参数，生成完整合并 BIN
    if bootloader_bin:
        # 生成输出文件名：从 app_v2.bin 提取路径，生成 ota_v1_to_v2.bin
        bin_dir = os.path.dirname(bin_file)
        output_full_bin = os.path.join(bin_dir, 'ota_v1_to_v2.bin')

        generate_full_bin(
            bootloader_bin=bootloader_bin,
            app_v1_bin=app_v1_bin,
            app_v2_bin=bin_data,
            bootconf_data=bootconf_data,
            output_file=output_full_bin
        )

        print(f"\n=== Flash Layout Summary ===")
        print(f"0x{FLASH_BOOTLOADER_BASE:08X} - Bootloader (48 KB)")
        if app_v1_bin:
            print(f"0x{FLASH_APP_BASE:08X} - APP v1 ({len(app_v1_bin)} bytes)")
        else:
            print(f"0x{FLASH_APP_BASE:08X} - APP (empty, 0xFF)")
        print(f"0x{FLASH_OTA_BASE:08X} - APP v2/OTA ({len(bin_data)} bytes)")
        print(f"0x{FLASH_BOOTCONF_BASE:08X} - bootconf (76 bytes, OTA pending)")
        print(f"0x{FLASH_FACTORY_BASE:08X} - factory (empty, 0xFF)")
        print(f"\nNow you can flash '{output_full_bin}' with J-Flash directly!")


if __name__ == '__main__':
    main()
