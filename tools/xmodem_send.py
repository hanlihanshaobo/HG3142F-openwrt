# -*- coding: utf-8 -*-
"""XMODEM-1K(CRC) 发送工具 —— 配合 U-Boot 的 `loadx 0x02000000`

设备 bootcmd: printenv;run once;sdk boot_img
其中 once=`loadx 0x02000000;bootm 0x02000000`
=> 串口把 uImage 传进去，U-Boot 会自动 bootm 起来。

用法:
  python xmodem_send.py <镜像文件> [日志文件] [等待C的秒数]

说明:
  - 不要先手动发字符，脚本会等 U-Boot 发出 'C' 后开始
  - 传输完成后继续收串口日志（内核启动输出），直到手动 Ctrl-C 或 --tail 秒数到
"""
import os
import sys
import time
import binascii

import serial
import serial.tools.list_ports as lp

BAUD = 115200
SOH, STX, EOT, ACK, NAK, CAN, CRC_REQ = 0x01, 0x02, 0x04, 0x06, 0x15, 0x18, 0x43


def find_port():
    for p in lp.comports():
        d = (p.description or '').upper()
        if any(k in d for k in ('CH340', 'CP210', 'FTDI', 'USB-SERIAL')):
            return p.device
    ps = list(lp.comports())
    if not ps:
        sys.exit('无串口')
    return ps[0].device


def crc16(data):
    return binascii.crc_hqx(data, 0)


def wait_byte(ser, timeout=3.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        b = ser.read(1)
        if b:
            return b[0]
    return None


def send_block(ser, seq, payload, retries=12):
    """payload 已补齐到 1024 字节"""
    frame = bytes([STX, seq & 0xFF, 0xFF - (seq & 0xFF)]) + payload
    c = crc16(payload)
    frame += bytes([(c >> 8) & 0xFF, c & 0xFF])
    for attempt in range(retries):
        ser.write(frame)
        ser.flush()
        r = wait_byte(ser, 5.0)
        if r == ACK:
            return True
        if r in (NAK, CRC_REQ):
            continue
        if r == CAN:
            raise SystemExit('设备发 CAN，传输中止')
        # 超时也重试
    return False


def main():
    path = sys.argv[1]
    log = sys.argv[2] if len(sys.argv) > 2 else 'xmodem_%s.log' % time.strftime('%H%M%S')
    wait_c = float(sys.argv[3]) if len(sys.argv) > 3 else 180.0

    data = open(path, 'rb').read()
    total_blocks = (len(data) + 1023) // 1024
    print('[XMODEM] 文件 %s  %d 字节  %d 块(1K)' % (os.path.basename(path), len(data), total_blocks))

    dev = find_port()
    print('[XMODEM] 串口', dev)
    ser = serial.Serial(dev, BAUD, timeout=0.2)
    ser.reset_input_buffer()

    tail = bytearray()
    # 1) 等 U-Boot 进入 XMODEM 接收态：先看到 "Ready for binary"，再看到连续 'C'
    print('[XMODEM] 等待 U-Boot 请求(## Ready for binary + CCC)...')
    t0 = time.time()
    started = False
    ready = False
    ccount = 0
    last = None
    while time.time() - t0 < wait_c:
        b = ser.read(256)
        if not b:
            continue
        tail.extend(b)
        if not ready and b'Ready for binary' in bytes(tail[-3000:]):
            ready = True
            print('[XMODEM] 已检测到下载提示 (%.1fs)，等 C...' % (time.time() - t0))
        if ready:
            for ch in b:
                if ch == CRC_REQ:
                    ccount += 1
                else:
                    ccount = 0
                if ccount >= 2:
                    started = True
                    break
        if started:
            break
        # 兜底：如果收到的全是 'C'（说明已经在 XMODEM 接收态，只是提示行已滚过去）
        if not ready and len(tail) >= 1 and set(bytes(tail)) <= {CRC_REQ} and time.time() - t0 > 6:
            ready = True
            started = True
            print('[XMODEM] 仅收到 C，判定已在接收态，直接开始 (%.1fs)' % (time.time() - t0))
            break
        last = b[-1]
    if not started:
        print('[XMODEM] 超时/未进入接收态 (ready=%s)，退出' % ready)
        open(log, 'wb').write(bytes(tail))
        return
    print('[XMODEM] 开始传输 (%.1fs)' % (time.time() - t0))
    # 丢掉残留的 C，避免干扰第一块
    time.sleep(0.05)
    ser.reset_input_buffer()

    # 2) 逐块发送
    t_start = time.time()
    for i in range(total_blocks):
        chunk = data[i * 1024:(i + 1) * 1024]
        if len(chunk) < 1024:
            chunk = chunk + b'\x1a' * (1024 - len(chunk))
        if not send_block(ser, (i + 1) & 0xFF, chunk):
            print('\n[XMODEM] 第 %d 块失败，重试耗尽' % (i + 1))
            open(log, 'wb').write(bytes(tail))
            return
        if (i + 1) % 50 == 0 or i + 1 == total_blocks:
            el = time.time() - t_start
            rate = (i + 1) * 1024 / max(el, 0.001)
            eta = (total_blocks - i - 1) * 1024 / max(rate, 1)
            print('  %d/%d 块  %.1f%%  %.1f KB/s  剩余约 %.0f 秒'
                  % (i + 1, total_blocks, 100.0 * (i + 1) / total_blocks,
                     rate / 1024.0, eta), flush=True)

    # 3) EOT
    print('[XMODEM] 发送 EOT')
    for _ in range(6):
        ser.write(bytes([EOT]))
        ser.flush()
        r = wait_byte(ser, 5.0)
        if r == ACK:
            break
    print('[XMODEM] 传输完成，用时 %.1f 秒。继续收日志...' % (time.time() - t_start))

    # 4) 继续抓 U-Boot bootm / 内核输出
    t1 = time.time()
    while time.time() - t1 < 300:
        b = ser.read(4096)
        if b:
            tail.extend(b)
            sys.stdout.write(b.decode('utf-8', 'replace'))
            sys.stdout.flush()
    ser.close()
    open(log, 'wb').write(bytes(tail))
    print('\n[完成] 日志 %s (%d 字节)' % (log, len(tail)))


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        print('\n中断')
