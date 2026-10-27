# -*- coding: utf-8 -*-
"""烽火光猫 /fh_tool/api 工具（AES-CBC，key/iv 由 SHA256(MAC) 派生）

用法:
  python fh_api.py telnet_on            # 开启 telnet
  python fh_api.py telnet_off
  python fh_api.py admin                # 读超管账号密码
  python fh_api.py info                 # 设备信息
  python fh_api.py call <Func> [k=v ...]

说明: 内部强制绕过代理（本机环境有 HTTP_PROXY 指向本地代理，会干扰设备直连）。
"""
import os
import sys
import json
import base64
import hashlib

# 必须在 urllib 使用前清掉代理
for _k in ('HTTP_PROXY', 'HTTPS_PROXY', 'http_proxy', 'https_proxy',
           'ALL_PROXY', 'all_proxy'):
    os.environ.pop(_k, None)
os.environ['no_proxy'] = '*'
os.environ['NO_PROXY'] = '*'

import http.client  # noqa: E402
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes  # noqa: E402
from cryptography.hazmat.primitives.padding import PKCS7  # noqa: E402

IP = os.environ.get('DEV_IP', '192.168.1.1')
MAC = os.environ.get('DEV_MAC', '68E9056DA5C0').upper().replace(':', '').replace('-', '')



def _derive(mac):
    mac = mac.strip().upper().replace(':', '').replace('-', '')
    d = hashlib.sha256(mac.encode()).hexdigest()
    key = ''.join(d[2 * i + 2] for i in range(16)).encode()
    iv = ''.join(d[3 * i + 3] for i in range(16)).encode()
    return key, iv


KEY, IV = _derive(MAC)


def _enc(obj):
    p = PKCS7(128).padder()
    data = p.update(json.dumps(obj, separators=(',', ':')).encode()) + p.finalize()
    e = Cipher(algorithms.AES(KEY), modes.CBC(IV)).encryptor()
    return base64.b64encode(e.update(data) + e.finalize()).decode()


def _dec(body):
    raw = base64.b64decode(body)
    d = Cipher(algorithms.AES(KEY), modes.CBC(IV)).decryptor()
    out = d.update(raw) + d.finalize()
    u = PKCS7(128).unpadder()
    return json.loads((u.update(out) + u.finalize()).decode())


def call(func, port=80, timeout=8, **params):
    payload = {"index": "1", "func": func}
    payload.update(params)
    body = _enc(payload).encode()
    conn = http.client.HTTPConnection(IP, port, timeout=timeout)
    try:
        conn.request('POST', '/fh_tool/api', body=body,
                     headers={'Content-Type': 'text/plain',
                              'Content-Length': str(len(body))})
        resp = conn.getresponse()
        raw = resp.read()
        if resp.status != 200:
            raise RuntimeError('HTTP %d: %r' % (resp.status, raw[:120]))
        return _dec(raw)
    finally:
        conn.close()


def call_auto(func, **params):
    last = None
    for port in (80, 8080):
        try:
            return call(func, port=port, **params), port
        except Exception as e:
            last = e
    raise last


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return
    act = args[0]
    if act == 'telnet_on':
        r, p = call_auto('TelnetEnable', telnet=1)
        print('TelnetEnable(1) ->', r, 'port', p)
    elif act == 'telnet_off':
        r, p = call_auto('TelnetEnable', telnet=0)
        print('TelnetEnable(0) ->', r, 'port', p)
    elif act == 'admin':
        print(call_auto('GetAdminAccount')[0])
    elif act == 'info':
        print(json.dumps(call_auto('GetDevInfo')[0], ensure_ascii=False, indent=1))
    elif act == 'call':
        func = args[1]
        kv = {}
        for a in args[2:]:
            k, _, v = a.partition('=')
            kv[k] = int(v) if v.isdigit() else v
        print(call_auto(func, **kv)[0])
    else:
        print(__doc__)


if __name__ == '__main__':
    main()
