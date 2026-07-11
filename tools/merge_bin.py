#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
Bootloader + APP BIN 文件合并工具
用法: python merge_bin.py <bootloader_bin> <app_bin> <app_base_addr_hex> <output_bin>
示例: python merge_bin.py bootloader.bin app.bin 0x08040000 merged.bin
"""

import sys
import os

def merge_bin(bootloader_bin, app_bin, app_base_addr, output_bin):
    """
    合并 Bootloader 和 APP 的 BIN 文件

    参数:
        bootloader_bin: Bootloader BIN 文件路径
        app_bin: APP BIN 文件路径
        app_base_addr: APP 的 Flash 起始地址（十六进制字符串，如 0x08040000）
        output_bin: 输出文件路径
    """
    # 解析 APP 基地址
    app_base = int(app_base_addr, 16)

    # 读取 Bootloader BIN
    # print(f"Reading Bootloader: {bootloader_bin}")
    with open(bootloader_bin, 'rb') as f:
        bl_data = f.read()
    # print(f"  Size: {len(bl_data)} bytes")

    # 读取 APP BIN
    # print(f"Reading APP: {app_bin}")
    with open(app_bin, 'rb') as f:
        app_data = f.read()
    # print(f"  Size: {len(app_data)} bytes")

    # 计算输出文件大小（APP 基地址 + APP 大小）
    output_size = app_base + len(app_data)
    # print(f"Output size: {output_size} bytes ({output_size / 1024:.1f} KB)")

    # 创建输出缓冲区，用 0xFF 填充（Flash 擦除后的值）
    print("Creating merged image...")
    merged_data = bytearray([0xFF] * output_size)

    # 复制 Bootloader 数据到起始位置
    merged_data[0:len(bl_data)] = bl_data
    print(f"  Bootloader: 0x08000000 - 0x{0x08000000 + len(bl_data)-1:08X}")

    # 复制 APP 数据到指定地址
    merged_data[app_base:app_base + len(app_data)] = app_data
    print(f"  APP:        0x{app_base:08X} - 0x{app_base + len(app_data)-1:08X}")

    # 写入输出文件
    # print(f"Writing output: {output_bin}")
    with open(output_bin, 'wb') as f:
        f.write(merged_data)

    # print(f"Merge complete! Output: {output_bin}")
    return 0

def main():
    if len(sys.argv) != 5:
        print("Usage: python merge_bin.py <bootloader_bin> <app_bin> <app_base_addr> <output_bin>")
        print("Example: python merge_bin.py bootloader.bin app.bin 0x08040000 merged.bin")
        return 1

    bootloader_bin = sys.argv[1]
    app_bin = sys.argv[2]
    app_base_addr = sys.argv[3]
    output_bin = sys.argv[4]

    # 检查输入文件
    if not os.path.exists(bootloader_bin):
        print(f"Error: Bootloader file not found: {bootloader_bin}")
        return 1

    if not os.path.exists(app_bin):
        print(f"Error: APP file not found: {app_bin}")
        return 1

    try:
        return merge_bin(bootloader_bin, app_bin, app_base_addr, output_bin)
    except Exception as e:
        print(f"Error: {e}")
        return 1

if __name__ == '__main__':
    sys.exit(main())
