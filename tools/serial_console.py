# -*- coding: utf-8 -*-
"""串口控制台 / 启动日志抓取（CH340 / CP210x / FTDI 自动识别）

用法:
  python serial_console.py list                      # 列出串口
  python serial_console.py capture [秒数] [日志文件]   # 只收不写，抓启动日志
  python serial_console.py send "<文本>"             # 发送一行（\\r\\n 结尾）
  python serial_console.py reboot                    # 抓日志的同时通过 telnet 重启设备
  python serial_console.py console                   # 交互控制台（Ctrl-C 退出）

常用:
  python serial_console.py reboot 90 boot1.log        # 边抓边重启，抓 90 秒
"""
import os
import sys
import time
import socket
import threading
import subprocess

import serial
import serial.tools.list_ports as lp

BAUD = 115200
PREFER = ('CH340', 'CP210', 'FTDI', 'USB-SERIAL', 'USB Serial')


def pick_port(explicit=None):
    ports = list(lp.comports())
    if explicit:
        for p in ports:
            if p.device.upper() == explicit.upper():
                return p.device
        return explicit
    if not ports:
        sys.exit('没有检测到任何串口设备，请检查 USB-TTL 是否插好')
    for p in ports:
        desc = (p.description or '').upper()
        if any(k in desc for k in PREFER):
            return p.device
    return ports[0].device


def open_port(dev):
    s = serial.Serial(dev, BAUD, timeout=0.2)
    s.reset_input_buffer()
    return s


def reader(ser, sink, stop, echo=True):
    while not stop.is_set():
        try:
            data = ser.read(4096)
        except Exception:
            break
        if data:
            sink.write(data)
            sink.flush()
            if echo:
                sys.stdout.write(data.decode('utf-8', 'replace'))
                sys.stdout.flush()


def capture(seconds, logfile, dev=None, reboot=False, banner=True):
    dev = pick_port(dev)
    print('[串口] %s @ %d 8N1  ->  %s' % (dev, BAUD, logfile))
    ser = open_port(dev)
    stop = threading.Event()
    f = open(logfile, 'wb')
    if banner:
        f.write(('\n=== capture start %s port=%s ===\n'
                 % (time.strftime('%H:%M:%S'), dev)).encode())
    t = threading.Thread(target=reader, args=(ser, f, stop), daemon=True)
    t.start()

    if reboot:
        threading.Thread(target=_telnet_reboot, daemon=True).start()

    t0 = time.time()
    try:
        while time.time() - t0 < seconds:
            time.sleep(0.2)
    except KeyboardInterrupt:
        pass
    stop.set()
    time.sleep(0.3)
    ser.close()
    f.write(b'\n=== capture end ===\n')
    f.close()
    print('\n[完成] 日志已存 %s (%d 字节)' % (logfile, os.path.getsize(logfile)))


def _telnet_reboot(host='192.168.1.1', mac6='6DA5C0', wait=4.0):
    """等串口线程起来后，用 telnet 让设备重启，以便抓完整启动日志。"""
    time.sleep(wait)
    try:
        s = socket.socket()
        s.settimeout(8)
        s.connect((host, 23))
        time.sleep(1.0)
        _drain(s, 1.0)
        s.sendall(b'admin\n')
        time.sleep(0.8)
        _drain(s, 0.8)
        s.sendall(b'Fh@' + mac6.encode() + b'\n')
        time.sleep(1.5)
        _drain(s, 1.2)
        s.sendall(b'reboot\n')
        time.sleep(1.0)
        _drain(s, 0.5)
        s.close()
        print('>>> 已通过 telnet 发送 reboot')
    except Exception as e:
        print('>>> telnet reboot 失败: %r（可手动断电重启）' % e)


def _drain(s, secs):
    s.settimeout(0.4)
    t0 = time.time()
    while time.time() - t0 < secs:
        try:
            if not s.recv(4096):
                break
        except Exception:
            pass


def send_text(text, dev=None):
    dev = pick_port(dev)
    ser = open_port(dev)
    ser.write((text + '\r\n').encode())
    ser.flush()
    time.sleep(1.0)
    out = b''
    t0 = time.time()
    while time.time() - t0 < 2.0:
        d = ser.read(4096)
        if d:
            out += d
    ser.close()
    sys.stdout.write(out.decode('utf-8', 'replace'))


def console(dev=None, logfile=None):
    dev = pick_port(dev)
    print('[串口] %s @ %d 8N1  输入内容回车发送，Ctrl-C 退出' % (dev, BAUD))
    ser = open_port(dev)
    stop = threading.Event()
    sink = open(logfile, 'ab') if logfile else open(os.devnull, 'wb')
    threading.Thread(target=reader, args=(ser, sink, stop), daemon=True).start()
    try:
        while True:
            line = sys.stdin.readline()
            if not line:
                break
            ser.write(line.encode())
            ser.flush()
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        ser.close()


if __name__ == '__main__':
    args = sys.argv[1:]
    if not args or args[0] == 'list':
        ps = list(lp.comports())
        print('检测到 %d 个串口:' % len(ps))
        for p in ps:
            print('  %-6s | %-38s | %s' % (p.device, p.description, p.hwid))
        if not ps:
            print('  (无)')
    elif args[0] == 'capture':
        secs = float(args[1]) if len(args) > 1 else 60
        log = args[2] if len(args) > 2 else 'serial_%s.log' % time.strftime('%H%M%S')
        capture(secs, log)
    elif args[0] == 'reboot':
        secs = float(args[1]) if len(args) > 1 else 90
        log = args[2] if len(args) > 2 else 'boot_%s.log' % time.strftime('%H%M%S')
        capture(secs, log, reboot=True)
    elif args[0] == 'send':
        send_text(args[1] if len(args) > 1 else '')
    elif args[0] == 'console':
        console(logfile=args[1] if len(args) > 1 else None)
    else:
        print(__doc__)
