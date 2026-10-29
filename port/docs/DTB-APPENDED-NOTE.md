# DTS 改动必须用 zImage-nodtb 重打包 FIT

日期：2026-10-29
适用：BCM96855 / HG3142F，厂商 U-Boot 2019.07 + FIT 启动

## 现象

把新 DTB 打进 FIT 的 `fdt_968552CREF_CM_XG`，U-Boot 日志也确认：

```
Using 'conf_lx_968552CREF_CM_XG' configuration
Trying 'fdt_968552CREF_CM_XG' fdt subimage
Data Size: 8432 Bytes
Verifying Hash Integrity ... sha256+ OK
Booting using the fdt blob at 0x27eaecc
```

但内核启动后 `/proc/device-tree` 里**没有**新加的 switch0/mdio 节点，
`/sys/firmware/fdt` 的 sha256 也不是我们塞进去的那份。

## 根因

OpenWrt 给内核镜像做了 `append-dtb`（`KERNEL := kernel-bin | append-dtb | ...`），
`v47-zImage.bin` 尾部自带一份 DTB（最后 6,449 字节）。ARM zImage 的
decompress 代码会优先使用自带的 appended DTB，bootloader 通过 r2 传入的
FIT fdt 被忽略。

验证：

```
/ # sha256sum /sys/firmware/fdt
993d57c2...   # 等于 v47-zImage.bin 尾部 0x37e278 处的 DTB
```

## 正确做法

用内核构建树里**未 append** 的 `arch/arm/boot/zImage`
（比镜像里的 zImage 正好小 6,449 字节），再和 FIT fdt 一起打包：

```
python tools/fit_rebuild.py <原厂 mtd7_bootfs1.bin> <zImage-nodtb> <board.dtb> \
       <out.fit> 968552CREF_CM_XG none 0x01000000
python tools/fit_setdefault.py <out.fit> <out2.fit> conf_lx_968552CREF_CM_XG
```

module-only CI 已把该文件导出为 artifact 里的 `zImage-nodtb`。

## 检查清单（每次改 DTS）

1. 确认 `arch/arm/boot/zImage` 尾部无 FDT magic（`d0 0d fe ed`）；
2. FIT 打包后核对 `fdt` 子镜像 hash 与本地 dtb 一致；
3. 启动后 `ls /proc/device-tree/switch0` 存在；
4. `sha256sum /sys/firmware/fdt` 应等于 FIT 里 fdt 子镜像（不是 appended DTB）。
