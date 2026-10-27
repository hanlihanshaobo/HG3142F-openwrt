# -*- coding: utf-8 -*-
"""把 zImage + DTB 打包成 U-Boot FIT 镜像（.itb）

目的：这台 U-Boot 的 bootm 只有在拿到 FDT 时才走 FDT 路径；
      legacy uImage 不给 FDT -> 报 "FDT and ATAGS support not compiled in"。
      FIT 内含 fdt 节点，bootm 就会正常把 DTB 传给内核。

用法:
  python fit_build.py <zImage> <dtb> <输出.itb> [load_addr=0x01000000]
"""
import os
import sys
import struct
import binascii

FDT_MAGIC = 0xD00DFEED
FDT_BEGIN_NODE, FDT_END_NODE, FDT_PROP, FDT_NOP, FDT_END = 1, 2, 3, 4, 9


def _pad4(b):
    return b + b'\x00' * ((4 - len(b) % 4) % 4)


class FdtBuilder:
    def __init__(self):
        self._struct = bytearray()
        self._strings = bytearray()
        self._stroff = {}

    def _name_off(self, name):
        if name in self._stroff:
            return self._stroff[name]
        off = len(self._strings)
        self._strings += name.encode() + b'\x00'
        self._stroff[name] = off
        return off

    def begin(self, name=''):
        self._struct += struct.pack('>I', FDT_BEGIN_NODE) + _pad4(name.encode() + b'\x00')
        return self

    def end(self):
        self._struct += struct.pack('>I', FDT_END_NODE)
        return self

    def prop(self, name, value):
        self._struct += struct.pack('>III', FDT_PROP, len(value), self._name_off(name))
        self._struct += _pad4(value)
        return self

    def prop_str(self, name, s):
        return self.prop(name, s.encode() + b'\x00')

    def prop_u32(self, name, v):
        return self.prop(name, struct.pack('>I', v))

    def build(self):
        self._struct += struct.pack('>I', FDT_END)
        rsv = b'\x00' * 16
        off_rsv = 40
        off_struct = off_rsv + len(rsv)
        off_strings = off_struct + len(self._struct)
        total = off_strings + len(self._strings)
        hdr = struct.pack('>10I', FDT_MAGIC, total, off_struct, off_strings,
                          off_rsv, 17, 16, 0, len(self._strings), len(self._struct))
        return hdr + rsv + bytes(self._struct) + bytes(self._strings)


def build_fit(zimg, dtb, load, entry, out, desc='OpenWrt BCM96855 FIT'):
    kernel_crc = binascii.crc32(zimg) & 0xFFFFFFFF
    fdt_crc = binascii.crc32(dtb) & 0xFFFFFFFF

    b = FdtBuilder()
    b.begin('')
    b.prop_str('description', desc)
    b.prop_u32('#address-cells', 1)

    # ---- images ----
    b.begin('images')

    b.begin('kernel@1')
    b.prop_str('description', 'OpenWrt Linux-6.18.55 (zImage)')
    b.prop('data', zimg)
    b.prop_str('type', 'kernel')
    b.prop_str('arch', 'arm')
    b.prop_str('os', 'linux')
    b.prop_str('compression', 'none')
    b.prop_u32('load', load)
    b.prop_u32('entry', entry)
    b.begin('hash@1')
    b.prop_str('algo', 'crc32')
    b.prop_u32('value', kernel_crc)
    b.end()
    b.end()

    b.begin('fdt@1')
    b.prop_str('description', 'BCM96855 / HG3142F device tree')
    b.prop('data', dtb)
    b.prop_str('type', 'flat_dt')
    b.prop_str('arch', 'arm')
    b.prop_str('compression', 'none')
    b.begin('hash@1')
    b.prop_str('algo', 'crc32')
    b.prop_u32('value', fdt_crc)
    b.end()
    b.end()

    b.end()  # images

    # ---- configurations ----
    b.begin('configurations')
    b.prop_str('default', 'conf@1')
    b.begin('conf@1')
    b.prop_str('description', 'OpenWrt kernel + dtb')
    b.prop_str('kernel', 'kernel@1')
    b.prop_str('fdt', 'fdt@1')
    b.end()
    b.end()

    b.end()  # root

    fit = b.build()
    open(out, 'wb').write(fit)
    print('[FIT] 写出 %s  %d 字节 (%.2f MB)' % (out, len(fit), len(fit) / 1048576.0))
    print('       kernel %d 字节 crc32=%08x' % (len(zimg), kernel_crc))
    print('       fdt    %d 字节 crc32=%08x' % (len(dtb), fdt_crc))
    print('       load=0x%08x entry=0x%08x' % (load, entry))
    return fit


def verify(path):
    d = open(path, 'rb').read()
    magic, total, os_, ost, orsv, ver, lc, cpu, ss, ssz = struct.unpack('>10I', d[:40])
    assert magic == FDT_MAGIC, 'bad magic'
    assert total == len(d), 'size mismatch %d vs %d' % (total, len(d))
    strings = d[ost:ost + ss]
    pos, end = os_, os_ + ssz
    path_nodes = []
    found = []
    while pos < end:
        tok = struct.unpack('>I', d[pos:pos + 4])[0]
        pos += 4
        if tok == FDT_BEGIN_NODE:
            e = d.find(b'\x00', pos)
            nm = d[pos:e].decode('latin-1')
            pos = e + 1
            pos = (pos + 3) & ~3
            path_nodes.append(nm)
        elif tok == FDT_END_NODE:
            if path_nodes:
                path_nodes.pop()
        elif tok == FDT_PROP:
            ln, no = struct.unpack('>II', d[pos:pos + 8])
            pos += 8
            e = strings.find(b'\x00', no)
            pn = strings[no:e].decode('latin-1')
            val = d[pos:pos + ln]
            pos += ln
            pos = (pos + 3) & ~3
            p = '/' + '/'.join([x for x in path_nodes if x])
            if len(val) <= 64:
                txt = val.rstrip(b'\x00')
                if pn in ('description', 'type', 'arch', 'os', 'compression', 'algo',
                          'default', 'kernel', 'fdt'):
                    print('   %-24s %-14s = %s' % (p, pn, txt.decode('latin-1')))
                elif pn == 'value':
                    print('   %-24s %-14s = %08x' % (p, pn, struct.unpack('>I', val)[0]))
            else:
                print('   %-24s %-14s = <%d bytes>' % (p, pn, ln))
            found.append((p, pn))
        elif tok == FDT_END:
            break
    print('[verify] 解析 %d 个属性，结构完整' % len(found))


if __name__ == '__main__':
    zimg = open(sys.argv[1], 'rb').read()
    dtb = open(sys.argv[2], 'rb').read()
    out = sys.argv[3]
    load = int(sys.argv[4], 0) if len(sys.argv) > 4 else 0x01000000
    build_fit(zimg, dtb, load, load, out)
    verify(out)
