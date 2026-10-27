#!/usr/bin/env python3
"""在 6.18 内核头里注入 Broadcom (bcmkernel) 的结构扩展。

用法: python3 patch_kernel.py <kernel_include_dir>
  e.g. python3 patch_kernel.py $KDIR/include

做的事:
  1. include/linux/skbuff.h : struct sk_buff 末尾加 unsigned int recycle_flags;
  2. include/linux/netdevice.h : struct net_device 末尾加 struct bcm_netdev_ext bcm_nd_ext;
  （struct bcm_netdev_ext 等定义由 compat/bcm_ext_types.h 通过 -include 提供）
"""
import sys, os, re


def find_body_end(src, brace_idx):
    """从 '{' 位置做括号匹配，返回匹配的 '}' 下标；跳过注释与字符串。"""
    i = brace_idx
    depth = 0
    in_str = None
    in_line = False
    in_block = False
    n = len(src)
    while i < n:
        c = src[i]
        nxt = src[i + 1] if i + 1 < n else ''
        if in_line:
            if c == '\n':
                in_line = False
        elif in_block:
            if c == '*' and nxt == '/':
                in_block = False
                i += 1
        elif in_str:
            if c == '\\':
                i += 1
            elif c == in_str:
                in_str = None
        else:
            if c == '/' and nxt == '/':
                in_line = True
                i += 1
            elif c == '/' and nxt == '*':
                in_block = True
                i += 1
            elif c in '"\'':
                in_str = c
            elif c == '{':
                depth += 1
            elif c == '}':
                depth -= 1
                if depth == 0:
                    return i
        i += 1
    return -1


def patch_struct(path, name, insert, marker):
    s = open(path, encoding='utf-8', errors='replace').read()
    if marker in s:
        print('  [skip] already patched:', name)
        return True
    m = re.search(r'\bstruct\s+%s\s*\{' % re.escape(name), s)
    if not m:
        print('  [FAIL] struct not found:', name)
        return False
    brace = s.index('{', m.start())
    end = find_body_end(s, brace)
    if end < 0:
        print('  [FAIL] no body end:', name)
        return False
    s = s[:end] + insert + s[end:]
    open(path, 'w', encoding='utf-8').write(s)
    print('  [ok] %s patched at offset %d' % (name, end))
    return True


def main():
    if len(sys.argv) < 2:
        print('usage: patch_kernel.py <kernel_include_dir>')
        return 1
    kinc = sys.argv[1]
    print('patching kernel headers under', kinc)

    skb = os.path.join(kinc, 'linux', 'skbuff.h')
    net = os.path.join(kinc, 'linux', 'netdevice.h')

    ok1 = patch_struct(
        skb, 'sk_buff',
        '\n\t/* Broadcom (bcmkernel) extension */\n\tunsigned int recycle_flags;\n',
        'unsigned int recycle_flags;')

    ok2 = patch_struct(
        net, 'net_device',
        '\n\t/* Broadcom (bcmkernel) extension */\n\tstruct bcm_netdev_ext bcm_nd_ext;\n',
        'struct bcm_netdev_ext bcm_nd_ext;')

    print('result: skbuff=%s netdevice=%s' % (ok1, ok2))
    return 0 if (ok1 and ok2) else 2


if __name__ == '__main__':
    sys.exit(main())
