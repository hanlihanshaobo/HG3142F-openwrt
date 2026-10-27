# -*- coding: utf-8 -*-
"""打断 U-Boot autoboot，并在提示符下依次执行命令

用法:
  python uboot_run.py <总秒数> <日志文件> <命令文件>

命令文件格式（每行一条，可加 "@秒数 " 前缀指定该条命令的等待时间，默认 4 秒）:
  version
  @2 printenv ipaddr serverip
  @30 tftpboot 0x02000000 initramfs.bin
  @20 bootm 0x02000000

只有在检测到 U-Boot 提示符后才会发送命令，避免误发到系统的 Login: 上。
"""
import re
import sys
import time
import socket
import threading

import serial
import serial.tools.list_ports as lp

BAUD = 115200
PROMPT_RE = re.compile(rb'(U-Boot>|U-Boot>|U-Boot #)\s*$', re.M)


def find_port():
    ports = list(lp.comports())
    if not ports:
        sys.exit('没有串口设备')
    for p in ports:
        d = (p.description or '').upper()
        if any(k in d for k in ('CH340', 'CP210', 'FTDI', 'USB-SERIAL')):
            return p.device
    return ports[0].device


def load_cmds(path):
    out = []
    for line in open(path, encoding='utf-8'):
        line = line.rstrip('\n')
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        wait = 4.0
        if line.startswith('@'):
            head, _, rest = line[1:].partition(' ')
            try:
                wait = float(head)
                line = rest
            except ValueError:
                pass
        out.append((wait, line))
    return out


def main():
    total = float(sys.argv[1])
    log = sys.argv[2]
    cmdfile = sys.argv[3]
    cmds = load_cmds(cmdfile)

    dev = find_port()
    print('[串口] %s @ %d -> %s' % (dev, BAUD, log))
    print('[命令] %d 条' % len(cmds))
    ser = serial.Serial(dev, BAUD, timeout=0.1)
    ser.reset_input_buffer()

    buf = bytearray()
    stop = threading.Event()

    def reader():
        while not stop.is_set():
            try:
                d = ser.read(4096)
            except Exception:
                break
            if d:
                buf.extend(d)
                sys.stdout.write(d.decode('utf-8', 'replace'))
                sys.stdout.flush()

    threading.Thread(target=reader, daemon=True).start()

    t0 = time.time()
    prompt_at = None
    # 阶段1：打断 autoboot（全程高频发 Ctrl-C + CR，直到出现提示符）
    last_note = 0
    while time.time() - t0 < 80:
        ser.write(b'\x03\r')
        ser.flush()
        if PROMPT_RE.search(bytes(buf[-500:])):
            prompt_at = time.time() - t0
            print('\n>>> 进入 U-Boot 提示符 (%.1fs)' % prompt_at)
            break
        el = int(time.time() - t0)
        if el and el % 10 == 0 and el != last_note:
            last_note = el
            print('    [%ds] 仍在发送打断字符，已收 %d 字节' % (el, len(buf)))
        time.sleep(0.08)

    if prompt_at is None:
        # 再给最后一次机会
        ser.write(b'\r')
        time.sleep(1.0)
        if PROMPT_RE.search(bytes(buf[-500:])):
            prompt_at = time.time() - t0
            print('\n>>> 进入 U-Boot 提示符 (%.1fs)' % prompt_at)

    if prompt_at is None:
        print('\n>>> 未能进入 U-Boot（可能已过 autoboot 窗口）。不发送任何命令。')
    else:
        time.sleep(0.5)
        for wait, cmd in cmds:
            print('\n>>> 发送: %s' % cmd)
            ser.write(cmd.encode() + b'\r')
            ser.flush()
            time.sleep(wait)

    # 剩余时间继续收日志
    while time.time() - t0 < total:
        time.sleep(0.2)

    stop.set()
    time.sleep(0.3)
    ser.close()
    open(log, 'wb').write(bytes(buf))
    print('\n[完成] %s (%d 字节) prompt=%s' % (log, len(buf), prompt_at is not None))


if __name__ == '__main__':
    main()
