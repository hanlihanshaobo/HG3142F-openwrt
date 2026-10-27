# -*- coding: utf-8 -*-
"""极简 TFTP 服务器，用于 U-Boot tftpboot（tftpy 实现）

用法:
  python tftp_server.py [根目录] [绑定IP] [端口]
默认: 根目录=../test_images  绑定=0.0.0.0  端口=69
"""
import os
import sys
import time

import tftpy

root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'test_images')
root = os.path.abspath(root)
bind = sys.argv[2] if len(sys.argv) > 2 else '0.0.0.0'
port = int(sys.argv[3]) if len(sys.argv) > 3 else 69

print('[TFTP] root=%s bind=%s:%d' % (root, bind, port))
print('[TFTP] 文件:', ', '.join(sorted(os.listdir(root))))
server = tftpy.TftpServer(root)
try:
    server.listen(bind, port)
except KeyboardInterrupt:
    print('[TFTP] 退出')
