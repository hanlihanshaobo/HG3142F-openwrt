# 硬件事实（HG3142F）

> 全部来自实机全量备份（`firmware_backup/HG3142F_SN071604000590814_20261025/`）与 root 权限下采集的 `device_info_root.txt`，非推测。

## 基本信息

| 项目 | 值 | 证据来源 |
|---|---|---|
| 厂商/型号 | FiberHome **HG3142F** | `/fh_tool` GetDevInfo |
| SN | `071604000590814` | GetDevInfo |
| 硬件版本 | `WEK2.094.493` | GetDevInfo |
| 软件版本 | `RP05C0`（2024-11-12 构建） | GetDevInfo |
| LAN MAC | `68:E9:05:6D:A5:C0` | `ifconfig br0` |
| SoC | Broadcom **BCM6855x**（`brcm,bcm96855`） | kernel 构建主机名 + U-Boot 字符串 |
| CPU | 3 × ARMv7 Cortex-A7（part 0xc07 rev 5），NEON/VFPv4 | `/proc/cpuinfo` |
| 内存 | 512 MiB（MemTotal 507,928 kB） | `/proc/meminfo` |
| NAND | 256 MiB（brcmnand.0，128 KiB 擦除块） | `/proc/mtd` |
| 原厂内核 | Linux 4.19.235，gcc 10.3，Buildroot 2021.02.4 | `uname -a` / `/proc/version` |

## 引导链（关键）

`mtd1`（loader, 2 MiB）字符串分析结果：

```
Broadcom BCMBCA HS SPI bus driver
brcm,bcm96855 / broadcom,bcm96855
U-Boot DDR standalone 2019.07 (Dec 15 2022) Build: 50404p1@424599
U-Boot SPL 2019.07-svn17541 (Nov 17 2024)
U-Boot TPL 2019.07-svn17541 (Nov 17 2024)
Found FIT format
can't append uboot env to dtb, / node not found
```

结论：
1. 引导程序是 **U-Boot 2019.07**（Broadcom 定制，字符串被替换为 "U-Boot"），不是 CFE。
2. 结构为 **SPL → TPL → DDR standalone → U-Boot proper**，加载 **FIT** 镜像。
3. 设备树 `brcm,bcm96855`，与主线 U-Boot `configs/bcm96855_defconfig` 同源，但主线版为 RAM 引导，NAND 启动依赖厂商 SPL/TPL。

> ⚠️ **loader 分区全机只有一份，没有 A/B**。任何改动前必须保留 `mtd1_loader.bin`，并准备 TTL 串口救援。

## 分区表（`/proc/mtd` 实测）

| mtd | 名称 | 大小 | 偏移 | 说明 |
|---|---|---|---|---|
| 0 | brcmnand.0 | 256 MiB | 0 | 全片 |
| 1 | loader | 2 MiB | 0x0000000 | 厂商 SPL+TPL+U-Boot |
| 2 | image | 164 MiB | 0x0200000 | UBI：bootfs1/rootfs1/bootfs2/rootfs2/felix1/felix2 |
| 3 | ipg | 87 MiB | 0x0A600000 | 厂商 IPG 区 |
| 4 | factory | 2 MiB | 0x0FD00000 | 出厂校准/SN/MAC/jffs2 |
| 5/6 | metadata1/2 | 272 B | — | 双槽元数据（内容一致） |
| 7/9 | bootfs1/2 | 11.1 MiB | — | A/B 内核槽 |
| 8/10 | rootfs1/2 | 59.3 MiB | — | A/B 根文件系统槽 |
| 11/12 | felix1/2 | 8.6 MiB | — | 厂商插件槽 |
| 13 | Apps | 78.7 MiB | — | UBI vol → /osgi |
| 14 | data | 7.0 MiB | — | UBI vol → /fhconf |

mtdparts 原文：
```
brcmnand.0:2097152(loader),171966464@2097152(image),91226112@174063616(ipg),2097152@265289728(factory)
```

## 原厂启动参数

```
root=/dev/ubiblock0_4 ubi.mtd=image rootfstype=squashfs img_index=1 felix_index=1 hwcfg=0x106
```

说明原厂在 `image` 这一个 UBI 设备上切了多个卷，`ubiblock0_4` 是第 5 个卷（rootfs）。移植时不必照抄，但要保留 `ubi.mtd=image` 的物理位置。

## 原厂 rootfs 情况

- `/` = squashfs（`/dev/ubiblock0_4`，只读）
- `/osgi` = ubifs（插件/应用，可写）
- `/fhconf` = ubifs（配置，可写）
- `/fhdata` = jffs2 只读（factory 分区）
- `/var`、`/mnt` = tmpfs
- root 提权通道（研究记录，移植时不再需要）：ubus `sysmgr.process_start`

## 对移植的影响

1. **好消息**：引导程序是 U-Boot（有主线源码可参考），A/B 双槽 + UBI 让刷机失败可回滚（除 loader）。
2. **难点**：BCM6855x 在 OpenWrt 上游无 target；网口/PON/WiFi 驱动都在厂商 GPL 源码里，主线没有。
3. **必须保住**：`factory` 分区（光学校准 `LanOpticTxCalib`、SN、MAC），OpenWrt 侧只读挂载读取。
