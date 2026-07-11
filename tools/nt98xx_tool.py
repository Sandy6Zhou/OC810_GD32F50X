#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
DVR 协议指令生成与解析工具

功能：
1. 生成协议帧（自动转义 + CRC16）
2. 解析接收到的协议帧（去转义 + CRC校验）
3. 支持版本查询等常用指令

作者：伍玉蛟 (wuyujiao@jimiiot.com)
日期：2026.06.08
"""

import struct
import sys
from typing import Tuple, Optional, List

# ============================================================================
# 协议常量定义
# ============================================================================

FRAME_FLAG = 0x7E
ESCAPE_CHAR = 0x7D
ESCAPE_FLAG_BYTE = 0x02
ESCAPE_ESCAPE_BYTE = 0x01

# 帧长度常量
FRAME_HDR_LEN = 4   # 帧头长度（seq2 + cmd2）
FRAME_CRC_LEN = 2   # CRC长度
FRAME_MIN_LEN = FRAME_HDR_LEN + FRAME_CRC_LEN  # 最小帧长（不含payload）

# 命令码
# MCU → DVR（单片机发送/应答）
CMD_MCU_COMMON            = 0x2001   # MCU通用命令
CMD_MCU_HEARTBEAT         = 0x2002   # MCU心跳包
CMD_MCU_VERSION_RESPONSE  = 0x2003   # MCU版本查询响应

# DVR → MCU（DVR发送，单片机解析）
CMD_DVR_COMMON            = 0xA001   # DVR通用命令
CMD_DVR_HEARTBEAT         = 0xA002   # DVR心跳包
CMD_DVR_VERSION_QUERY     = 0xA003   # DVR版本查询命令


# ============================================================================
# CRC16-CCITT 计算
# ============================================================================

def calc_crc16(data: bytes, poly: int = 0x1021, init: int = 0xFFFF) -> int:
    """
    计算 CRC16-CCITT

    Args:
        data: 数据字节流
        poly: CRC 多项式（默认 0x1021）
        init: 初始值（默认 0xFFFF）

    Returns:
        CRC16 校验值
    """
    crc = init
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ poly
            else:
                crc = crc << 1
            crc &= 0xFFFF
    return crc


# ============================================================================
# 转义处理
# ============================================================================

def escape_frame(data: bytes) -> bytes:
    """
    对帧数据进行转义

    Args:
        data: 原始帧数据（不含首尾标识符）

    Returns:
        转义后的数据
    """
    result = bytearray()
    for byte in data:
        if byte == FRAME_FLAG:
            result.append(ESCAPE_CHAR)
            result.append(ESCAPE_FLAG_BYTE)
        elif byte == ESCAPE_CHAR:
            result.append(ESCAPE_CHAR)
            result.append(ESCAPE_ESCAPE_BYTE)
        else:
            result.append(byte)
    return bytes(result)


def unescape_frame(data: bytes) -> bytes:
    """
    对接收数据进行去转义

    Args:
        data: 转义后的数据（不含首尾标识符）

    Returns:
        原始帧数据

    Raises:
        ValueError: 转义序列错误
    """
    result = bytearray()
    i = 0
    while i < len(data):
        if data[i] == ESCAPE_CHAR:
            if i + 1 >= len(data):
                raise ValueError("Incomplete escape sequence")
            next_byte = data[i + 1]
            if next_byte == ESCAPE_FLAG_BYTE:
                result.append(FRAME_FLAG)
            elif next_byte == ESCAPE_ESCAPE_BYTE:
                result.append(ESCAPE_CHAR)
            else:
                raise ValueError(f"Invalid escape sequence: 0x{ESCAPE_CHAR:02X} 0x{next_byte:02X}")
            i += 2
        else:
            result.append(data[i])
            i += 1
    return bytes(result)


# ============================================================================
# 帧生成
# ============================================================================

def build_frame(seq: int, cmd: int, payload: bytes = b'') -> bytes:
    """
    生成完整的协议帧（含转义和首尾标识符）

    Args:
        seq: 流水号（0-65535）
        cmd: 命令码（0-65535）
        payload: 负载数据

    Returns:
        完整的协议帧字节流
    """
    # 构建帧数据（不含首尾标识符）：seq(2) + cmd(2) + payload
    frame_data = struct.pack('>HH', seq, cmd) + payload

    # 计算 CRC（覆盖 seq + cmd + payload）
    crc = calc_crc16(frame_data)
    frame_data += struct.pack('>H', crc)

    # 转义
    escaped_data = escape_frame(frame_data)

    # 添加首尾标识符
    frame = bytes([FRAME_FLAG]) + escaped_data + bytes([FRAME_FLAG])

    return frame


def build_version_query(seq: int = 1) -> bytes:
    """
    生成版本查询指令（DVR → MCU）

    Args:
        seq: 流水号（默认 1）

    Returns:
        版本查询协议帧
    """
    return build_frame(seq, CMD_DVR_VERSION_QUERY)


def build_version_response(seq: int = 1, sw_ver: str = "", hw_ver: str = "") -> bytes:
    """
    生成版本查询响应帧（MCU → DVR）

    Args:
        seq: 流水号（必须与请求一致）
        sw_ver: 软件版本字符串（自动补\0至40字节）
        hw_ver: 硬件版本字符串（自动补\0至40字节）

    Returns:
        版本查询响应协议帧
    """
    payload = bytearray(80)  # 全0填充
    sw_bytes = sw_ver.encode('utf-8')
    hw_bytes = hw_ver.encode('utf-8')
    payload[0:min(len(sw_bytes), 40)] = sw_bytes[:40]
    payload[40:40+min(len(hw_bytes), 40)] = hw_bytes[:40]
    return build_frame(seq, CMD_MCU_VERSION_RESPONSE, bytes(payload))


def build_dvr_heartbeat(seq: int = 1, heart_cnt: int = 0, status: bytes = b'\x00' * 8) -> bytes:
    """
    生成DVR心跳帧（DVR → MCU）

    Args:
        seq: 流水号（自动递增）
        heart_cnt: DVR累计收到MCU心跳的次数（BE16）
        status: DVR状态（8字节位域，默认全0）

    Returns:
        DVR心跳协议帧
    """
    payload = struct.pack('>H', heart_cnt) + status
    return build_frame(seq, CMD_DVR_HEARTBEAT, payload)


# ============================================================================
# 帧解析
# ============================================================================

class FrameParser:
    """协议帧解析器"""

    def __init__(self):
        self.buffer = bytearray()

    def feed(self, data: bytes):
        """
        喂入接收数据

        Args:
            data: 接收到的字节数据
        """
        self.buffer.extend(data)

    def parse_frames(self) -> List[dict]:
        """
        从缓冲区解析所有完整帧

        Returns:
            帧列表，每个帧包含：
            - seq: 流水号
            - cmd: 命令码
            - payload: 负载数据
            - crc_ok: CRC校验是否通过
        """
        frames = []

        while True:
            # 查找帧开始标识
            start_idx = self._find_frame_start()
            if start_idx == -1:
                break

            # 查找帧结束标识
            end_idx = self._find_frame_end(start_idx + 1)
            if end_idx == -1:
                break

            # 提取帧数据（不含首尾标识符）
            frame_escaped = self.buffer[start_idx + 1:end_idx]

            # 删除已处理的帧
            del self.buffer[:end_idx + 1]

            try:
                # 解析帧
                frame = self._parse_single_frame(frame_escaped)
                frames.append(frame)
            except Exception as e:
                print(f"[WARN] Frame parse error: {e}")

        return frames

    def _find_frame_start(self) -> int:
        """查找帧开始标识"""
        try:
            return self.buffer.index(FRAME_FLAG)
        except ValueError:
            return -1

    def _find_frame_end(self, start: int) -> int:
        """查找帧结束标识"""
        try:
            return self.buffer.index(FRAME_FLAG, start)
        except ValueError:
            return -1

    def _parse_single_frame(self, escaped_data: bytes) -> dict:
        """
        解析单个帧

        Args:
            escaped_data: 转义后的帧数据（不含首尾标识符）

        Returns:
            帧信息字典
        """
        # 去转义
        frame_data = unescape_frame(escaped_data)

        # 最小帧长检查：帧头 + CRC
        if len(frame_data) < FRAME_MIN_LEN:
            raise ValueError(f"Frame too short: {len(frame_data)} bytes")

        # 解析字段
        seq, cmd = struct.unpack('>HH', frame_data[:FRAME_HDR_LEN])
        crc_received = struct.unpack('>H', frame_data[-FRAME_CRC_LEN:])[0]
        payload = frame_data[FRAME_HDR_LEN:-FRAME_CRC_LEN]

        # CRC 校验
        crc_calc = calc_crc16(frame_data[:-FRAME_CRC_LEN])
        crc_ok = (crc_calc == crc_received)

        return {
            'seq': seq,
            'cmd': cmd,
            'payload': payload,
            'crc_ok': crc_ok,
            'raw': frame_data
        }


# ============================================================================
# 命令行工具
# ============================================================================

def bytes_to_hex(data: bytes) -> str:
    """字节流转为十六进制字符串"""
    return ' '.join(f'{b:02X}' for b in data)


def hex_to_bytes(hex_str: str) -> bytes:
    """十六进制字符串转为字节流"""
    hex_str = hex_str.replace(' ', '').replace(':', '')
    return bytes.fromhex(hex_str)


def cmd_generate_version_query(seq: int = 1):
    """生成版本查询指令"""
    frame = build_version_query(seq)
    print(f"\n{'='*60}")
    print(f"版本查询指令 (seq={seq})")
    print(f"{'='*60}")
    print(f"帧长度: {len(frame)} 字节")
    print(f"十六进制: {bytes_to_hex(frame)}")
    print(f"\n字段解析:")
    print(f"  帧标识: 0x{FRAME_FLAG:02X}")
    print(f"  流水号: {seq} (0x{seq:04X})")
    print(f"  命令码:   0x{CMD_DVR_VERSION_QUERY:04X}")
    print(f"  负载:   无")
    print(f"{'='*60}\n")


def cmd_generate_version_response(seq: int = 1, sw_ver: str = "", hw_ver: str = ""):
    """生成版本查询响应指令"""
    frame = build_version_response(seq, sw_ver, hw_ver)
    print(f"\n{'='*60}")
    print(f"版本查询响应 (seq={seq})")
    print(f"{'='*60}")
    print(f"帧长度: {len(frame)} 字节")
    print(f"十六进制: {bytes_to_hex(frame)}")
    print(f"\n字段解析:")
    print(f"  帧标识: 0x{FRAME_FLAG:02X}")
    print(f"  流水号: {seq} (0x{seq:04X})")
    print(f"  命令码:   0x{CMD_MCU_VERSION_RESPONSE:04X}")
    print(f"  负载长度: 80 字节")
    print(f"  软件版本: {sw_ver[:40]}")
    print(f"  硬件版本: {hw_ver[:40]}")
    print(f"{'='*60}\n")


def cmd_generate_dvr_heartbeat(seq: int = 1, heart_cnt: int = 0, status: bytes = b'\x00' * 8):
    """生成DVR心跳指令"""
    frame = build_dvr_heartbeat(seq, heart_cnt, status)
    print(f"\n{'='*60}")
    print(f"DVR心跳帧 (seq={seq}, heart_cnt={heart_cnt})")
    print(f"{'='*60}")
    print(f"帧长度: {len(frame)} 字节")
    print(f"十六进制: {bytes_to_hex(frame)}")
    print(f"\n字段解析:")
    print(f"  帧标识: 0x{FRAME_FLAG:02X}")
    print(f"  流水号: {seq} (0x{seq:04X})")
    print(f"  命令码:   0x{CMD_DVR_HEARTBEAT:04X}")
    print(f"  负载长度: {2 + len(status)} 字节")
    print(f"  heart_cnt: {heart_cnt} (0x{heart_cnt:04X})")
    print(f"  status: {bytes_to_hex(status)}")
    print(f"{'='*60}\n")


def cmd_parse_frame(hex_data: str):
    """解析十六进制帧数据"""
    try:
        data = hex_to_bytes(hex_data)
    except ValueError as e:
        print(f"[ERROR] Invalid hex data: {e}")
        return

    print(f"\n{'='*60}")
    print(f"帧解析")
    print(f"{'='*60}")
    print(f"输入数据: {bytes_to_hex(data)}")
    print(f"帧长度:   {len(data)} 字节")

    parser = FrameParser()
    parser.feed(data)
    frames = parser.parse_frames()

    if not frames:
        print("\n[WARN] No valid frame found")
        return

    for i, frame in enumerate(frames, 1):
        print(f"\n--- 帧 {i} ---")
        print(f"  流水号:   {frame['seq']} (0x{frame['seq']:04X})")
        print(f"  命令码:   0x{frame['cmd']:04X}")
        print(f"  负载长度: {len(frame['payload'])} 字节")

        if frame['payload']:
            print(f"  负载数据: {bytes_to_hex(frame['payload'])}")
            # 尝试按字符串解析
            try:
                payload_str = frame['payload'].decode('utf-8').rstrip('\x00')
                if all(32 <= c < 127 or c == 0 for c in frame['payload']):
                    print(f"  负载文本: \"{payload_str}\"")
            except:
                pass

        print(f"  CRC校验:  {'通过' if frame['crc_ok'] else '失败'}")

    print(f"{'='*60}\n")


def cmd_interactive():
    """交互模式"""
    print("\n" + "="*60)
    print("DVR 协议指令工具 - 交互模式")
    print("="*60)
    print("命令:")
    print("  1          - 生成版本查询指令 (DVR→MCU)")
    print("  2 <seq> <sw_ver> <hw_ver>  - 生成版本查询响应 (MCU→DVR)")
    print("  3 <hex>    - 解析十六进制帧数据")
    print("  4 [count]  - 生成DVR心跳帧（自动递增seq和heart_cnt）")
    print("  q          - 退出")
    print("="*60 + "\n")

    seq_counter = 1
    heart_cnt = 0

    while True:
        try:
            user_input = input("> ").strip()
        except (EOFError, KeyboardInterrupt):
            print("\n退出")
            break

        if not user_input:
            continue

        if user_input == 'q':
            print("退出")
            break
        elif user_input == '1':
            cmd_generate_version_query(seq_counter)
            seq_counter += 1
        elif user_input.startswith('2 '):
            parts = user_input[2:].strip().split()
            if len(parts) >= 3:
                seq = int(parts[0])
                sw_ver = parts[1]
                hw_ver = parts[2]
                cmd_generate_version_response(seq, sw_ver, hw_ver)
            else:
                print("[ERROR] Usage: 2 <seq> <sw_ver> <hw_ver>")
        elif user_input.startswith('3 '):
            hex_data = user_input[2:].strip()
            cmd_parse_frame(hex_data)
        elif user_input == '4':
            # 解析可选参数：生成多少个心跳帧
            parts = user_input[1:].strip().split()
            count = int(parts[0]) if parts else 1
            for i in range(count):
                cmd_generate_dvr_heartbeat(seq_counter, heart_cnt)
                seq_counter += 1
                heart_cnt += 1
        else:
            print("[ERROR] Unknown command")


def main():
    """主函数"""
    if len(sys.argv) < 2:
        print("DVR 协议指令生成与解析工具")
        print("\n用法:")
        print("  python nt98xx_tool.py gen-query [seq]         - 生成版本查询指令 (DVR→MCU)")
        print("  python nt98xx_tool.py gen-resp <seq> <sw> <hw> - 生成版本查询响应 (MCU→DVR)")
        print("  python nt98xx_tool.py gen-hb [seq] [cnt]      - 生成DVR心跳帧 (DVR→MCU)")
        print("  python nt98xx_tool.py parse <hex>             - 解析十六进制帧数据")
        print("  python nt98xx_tool.py                        - 交互模式")
        print("\n示例:")
        print("  python nt98xx_tool.py gen-query")
        print("  python nt98xx_tool.py gen-query 123")
        print("  python nt98xx_tool.py gen-resp 1 'JM-OC810MCU-STD-V1.0.260608.01' 'HW:OC810-M-V1.0'")
        print("  python nt98xx_tool.py gen-hb")
        print("  python nt98xx_tool.py gen-hb 10 5")
        print("  python nt98xx_tool.py parse '7E 00 01 A0 03 9E ED 7E'")
        return

    cmd = sys.argv[1]

    if cmd == 'gen-query':
        seq = int(sys.argv[2]) if len(sys.argv) > 2 else 1
        cmd_generate_version_query(seq)
    elif cmd == 'gen-resp':
        if len(sys.argv) < 5:
            print("[ERROR] Usage: gen-resp <seq> <sw_ver> <hw_ver>")
            return
        seq = int(sys.argv[2])
        sw_ver = sys.argv[3]
        hw_ver = sys.argv[4]
        cmd_generate_version_response(seq, sw_ver, hw_ver)
    elif cmd == 'gen-hb':
        seq = int(sys.argv[2]) if len(sys.argv) > 2 else 1
        heart_cnt = int(sys.argv[3]) if len(sys.argv) > 3 else 0
        cmd_generate_dvr_heartbeat(seq, heart_cnt)
    elif cmd == 'parse':
        if len(sys.argv) < 3:
            print("[ERROR] Please provide hex data")
            return
        hex_data = ' '.join(sys.argv[2:])
        cmd_parse_frame(hex_data)
    else:
        print(f"[ERROR] Unknown command: {cmd}")


if __name__ == '__main__':
    if len(sys.argv) < 2:
        cmd_interactive()
    else:
        main()
