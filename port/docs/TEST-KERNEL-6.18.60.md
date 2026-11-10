# Linux 6.18.60 构建测试报告

> 日期：2026-11-05
> 分支：`kernel-6.18.60`
> 上游 pin：`5fb9d9cd90ca751b5066a37517caa9fbc158ded7`（OpenWrt main，kernel bump 6.18.60）
> 结论：**内核、板级 DTB、三个树外模块全部构建通过**

## 一、测试环境

| 项目 | 值 |
|---|---|
| 构建机 | Ubuntu 24.04（x86_64，4 核，3.8 GiB RAM） |
| 交叉工具链 | OpenWrt SDK 25.12.6 / ipq40xx generic，GCC 14.3.0 musl eabi |
| 工具链目录 | `toolchain-arm_cortex-a7+neon-vfpv4_gcc-14.3.0_musl_eabi` |
| 内核源码 | OpenWrt main @ `5fb9d9c`（Gitee 镜像拉取） |
| 内核版本 | `6.18.60` |
| 目标 | `bcmbca/generic`，`fiberhome_mb4784w`，ARMv7 Cortex-A7 |

> 选择 ipq40xx 的 SDK 工具链是因为它的 CPU 三元组与本机完全一致
> （`arm_cortex-a7+neon-vfpv4_musl_eabi`），且同为 OpenWrt 25.12 / 6.18 LTS 基线。

## 二、执行步骤与结果

### 1. 源码准备与版本自检

```sh
sh port/scripts/prepare.sh sources/openwrt
```

输出：

```text
==> 内核基线: Linux 6.18.60
==> 叠加 overlay（target/linux/bcmbca、base-files 等）
==> 复制 SDK 源码树 -> target/linux/bcmbca/sdk-bcm96855
    5fb9d9cd... 2026-10-31T23:24:46+01:00 kernel: bump 6.18 to 6.18.60
    target/linux/bcmbca 已就位
```

### 2. 内核准备与配置生成

```sh
make target/linux/prepare V=s
# 复刻 workflow 的 generic + target config 合并，然后 olddefconfig
make ARCH=arm olddefconfig
make -s ARCH=arm kernelversion
```

结果：补丁全部应用成功，关键配置保持：

```text
CONFIG_ARCH_BCMBCA=y
CONFIG_ARCH_BCMBCA_CORTEXA7=y
CONFIG_MTD_NAND_BRCMNAND_BCMBCA=y
CONFIG_MTD_UBI_BLOCK=y
CONFIG_UBIFS_FS=y
CONFIG_CMDLINE="ubi.mtd=image ubi.block=0,4 root=/dev/ubiblock0_4 rootfstype=squashfs"
CONFIG_CMDLINE_EXTEND=y
```

`make kernelversion` → **6.18.60**

### 3. 内核 zImage

```sh
make -j2 ARCH=arm CROSS_COMPILE=... zImage
```

结果：

```text
Kernel: arch/arm/boot/zImage is ready
Linux version 6.18.60 (zhang@ubuntu-op) ... #1 SMP
```

| 产物 | 大小 | sha256 |
|---|---:|---|
| `zImage` | 3,620,632 B | `5dfbe6fbccd52ca62f18f91748f22a074ea5e6eb08c6a3e4cff0897f4a8517f5` |
| `bcm96855-mb4784w.dtb` | 8,432 B | `0e74ae2844eed14f4cc1a343a301a6b76c468d280e5825807de14d1eb1303298` |

DTB 只有 `unit_address_vs_reg` / `interrupt_map` 警告，无错误（与设备树原有写法一致）。

### 4. 树外模块（workflow 的两条 SDK probe + bcm-reboot）

| 模块 | 大小 | 编译错误 | 未决符号 | vermagic |
|---|---:|---:|---:|---|
| `bcm_enet.ko` | 688,568 B | 0 | 0 | `6.18.60 SMP mod_unload ARMv7 p2v8` |
| `bcm_pcie.ko` | 235,228 B | 0 | — | `6.18.60 SMP mod_unload ARMv7 p2v8` |
| `bcm_reboot.ko` | 25,272 B | 0 | — | `6.18.60 SMP mod_unload ARMv7 p2v8` |

`bcm_enet.ko` 使用 workflow 同款 `bcm_enet-y` 对象列表（含
`phy_drv_compat` / `enet_stubs` / `enet_framework_stubs` / `bcm_chip_arch_compat` /
`bcmkernel_stubs` / `enet_symbol_stubs` / `unimac_mac` / `rdp_probe` / `rdp_c2` / `rdp_c3`），
说明 6.18.55 → 6.18.60 的 stable 更新没有破坏现有 compat 层。

## 三、产物

已回传到工作区：

```text
test_images/kernel-6.18.60/
├── zImage
├── bcm96855-mb4784w.dtb
├── bcm_enet.ko
├── bcm_pcie.ko
├── bcm_reboot.ko
└── SHA256SUMS
```

## 四、CI 结果（GitHub Actions #101）

- 分支：`kernel-6.18.60`
- 提交：`629ebfc`
- 运行：https://github.com/hanlihanshaobo/HG3142F-openwrt/actions/runs/42052084085
- 结论：**success**
- 完整构建耗时：**2629 s**（约 44 分钟）
- 重复触发的 #102 已取消

关键步骤全部成功：

```text
15 Build                      success   ← 完整镜像
18 SDK compile probe (bcm_enet) success
19 Package probe (kmod-bcm-enet) success
20 SDK compile probe (bcm_pcie)  success
21 Export kernel Image/rootfs    success
25 Upload artifact               success
```

CI 产物 `openwrt-mb4784w-101`（36,728,120 B）中确认：

| 产物 | 大小 | sha256 |
|---|---:|---|
| `Image` | 27,860,320 B | `549d7a95d9475cf3491de5463781f288fc1fec431557298ea22b5e96fbcbf69d` |
| `Image.lzo` | 10,437,286 B | `111c49d9abde0841446e5d3d324abc80aef28dd958a5394bc3516c9aca28d99b` |
| `bcm_enet.ko` | 697,068 B | `a8c280622f4ef7f02d1e16fcf47ef0e5fa73c2e3b9a1c70d44ea07fd604003eb` |
| `root.squashfs` | 3,310,478 B | `451586d02ce3efb0337d1f25edd686f22f99bc80a279c9020a4bfa61d41bbf26` |
| `overlay.ubifs` | 1,650,688 B | `2f9b745b0ab52a092ec931d0316270d636027f6b2d0f083e3e87ebc0f97543c6` |
| `...-initramfs-kernel.bin` | 6,735,576 B | `c723f18d4c6608836dcd60fe60e83fbbeeb5b9d6c5e5abd058351ae26f92bce3` |
| `...-squashfs-sysupgrade.bin` | 7,340,302 B | `a638ec1f8919b879bee4358f5a16b8e8310b99228068291f73004c905707a653` |

二进制核对：

```text
Image:      Linux version 6.18.60 ... OpenWrt GCC 14.4.0
bcm_enet.ko vermagic=6.18.60 SMP mod_unload ARMv7 p2v8
```

`probe_enet_sdk.log` 里的内核路径是 `.../linux-6.18.60`，并出现 `LD [M] bcm_enet.ko`。

### module-only 通道（#103）

- 运行：https://github.com/hanlihanshaobo/HG3142F-openwrt/actions/runs/42060408988
- 结论：**success**（约 3.7 分钟）
- 产物：`bcm-enet-module-103`（6,912,245 B）

| 产物 | 大小 | 说明 |
|---|---:|---|
| `zImage-nodtb` | **6,656,816 B** | 裸 zImage |
| `bcm96855-mb4784w.dtb` | 8,432 B | 板级 DTB |
| `bcm_enet.ko` | 697,068 B | 与 #101 同尺寸 |
| `undefined_symbols.txt` | 0 B | 无未决符号 |

> **尺寸注意**：CI 的 `zImage-nodtb` 是 **6.65 MB**，比本地构建的 3.62 MB 大 3 MB。
> 探针确认两者都是 XZ 压缩（`zImage` 内偏移 0x65E4 处有 `FD 37 7A 58 5A 00`），
> 所以不是压缩算法差异，而是 CI 内核本身更大（大概率与
> `CONFIG_DEBUG_INFO=y` / 内置 initramfs 有关，待查）。
>
> 直接拿它打 bootfs1 FIT 会和 ATF + U-Boot（3.1 MB）一起超出槽容量，
> 因此本次落盘验证用的是本地 `test_images/kernel-6.18.60/zImage`。
> 后续若要“整包自洽”（CI 内核 + CI rootfs），需要先把这个 3 MB 差查清。

## 五、实机 RAM 启动验证（2026-11-09）

用本地产物打 FIT：

```text
test_images/kernel-6.18.60/kernel-6.18.60.itb   3,629,858 B
  kernel: zImage 3,620,632 B（6.18.60）
  fdt:    bcm96855-mb4784w.dtb 8,432 B
```

启动方式：**不改 NAND、不写槽位**。设置 U-Boot：

```text
once=run rz_win
rz_win=echo XMODEM-WINDOW;loadx 0x02000000;bootm 0x02000000
```

重启后通过串口 XMODEM 把 FIT 传到 `0x02000000`，再 `bootm`。

> TFTP 这次没有走通：`sdk wait_link_status 20` 返回后 `sdk active_port 2` 仍报
> `link is down`，U-Boot 自动切到 port 4，而 PC 接在 lan2 → TFTP ARP 超时。
> XMODEM 是当前更可靠的通道（28,359 块 / 385 秒）。
传输 28,359 块 / 385 秒，EOT ACK，串口日志：
[`test_logs/kernel-6.18.60-xmodem.log`](../../../test_logs/kernel-6.18.60-xmodem.log)。

关键证据：

```text
## Total Size      = 0x00376322 = 3629858 Bytes
Starting kernel ...
[    0.000000] Linux version 6.18.60 (zhang@ubuntu-op) ... #1 SMP
[    1.239201] VFS: Mounted root (squashfs filesystem) readonly on device 254:0.
[    1.241016] Run /sbin/init as init process
[    8.979014] procd: - early -
[    9.574603] procd: - init -
```

启动后串口 shell 实测：

```text
root@HG3142F:/# uname -a
Linux HG3142F 6.18.60 #1 SMP Thu Nov 5 22:40:34 CST 2026 armv7l GNU/Linux
root@HG3142F:/# cat /proc/version
Linux version 6.18.60 (zhang@ubuntu-op) ...
root@HG3142F:/# cat /proc/cmdline
... ubi.mtd=image ubi.block=0,4 root=/dev/ubiblock0_4 rootfstype=squashfs
```

说明：

- 根文件系统用的是设备现有的 **OpenWrt 21.02.7**（B1-b 的 rootfs），不是 6.18 配套 rootfs；
  因此 `/etc/modules.d` 里的 4.19 模块大量 `Unknown symbol`，包括 `bcm_enet` / `bcm_pcie`，
  本次没有网络，只有串口 shell。内核本身启动、挂 rootfs、procd、init 全部正常。
- 这次验证的是 **6.18.60 内核 + 厂商 U-Boot + 现有 rootfs** 的最小启动链。
- 恢复：在 6.18 shell 执行 `echo b > /proc/sysrq-trigger`，设备回到 U-Boot；
  `rz_win` 窗口超时后启动 slot2 厂商固件，再用 `/fh_tool/api` + telnet root 把
  `once` 改回 `run rz_img1`，重启回到 B1-b（4.19.235）。恢复日志：
  [`test_logs/kernel-6.18.60-recover.log`](../../../test_logs/kernel-6.18.60-recover.log)。

## 六、完整 6.18 rootfs 尝试（未通过 initramfs）

尝试把 CI 的 6.18 `root.squashfs` 解包成 cpio.gz，走 **kernel + DTB + initramfs**
的 FIT，全 RAM 启动，避免写 NAND。FIT 8,607,566 B，XMODEM 67247 块 / 911 秒。

结果：内核启动，但 initramfs 解包失败：

```text
Unpacking initramfs...
compression method gzip not configured
Initramfs unpacking failed: decompressor failed
```

随后内核退回 `root=/dev/ubiblock0_4`（B1-b 21.02 rootfs），并进入了内核里自带的
**HG3142F flash initramfs (2nd stage)** 救援 shell，而不是 6.18 rootfs。

排查发现：

- 构建树 `.config` 里已经有 `CONFIG_RD_GZIP=y`、`CONFIG_DECOMPRESS_GZIP=y`，
  但实际启动的内核 `uname` 构建时间仍是 `Nov 5 22:40`，说明发出去的 zImage
  没有包含重新构建的 vmlinux。
- `.config` 的 `CONFIG_INITRAMFS_SOURCE` 已置空，但内核里仍带着旧的
  `HG3142F flash initramfs (2nd stage)`，说明 `usr/initramfs_data.cpio` 没有清掉，
  内核镜像没有真正重建。

结论：这不是 6.18 内核的问题，是 **内核镜像构建缓存/内置 initramfs 没清干净**。
本次在 Ubuntu 上强制重建（删 `vmlinux` / `zImage` 后重跑）因磁盘 I/O 超时被中断，
没有产出新的 zImage；后续建议用 `nohup` 跑完整重建并确认构建时间。

下次重试前需要：

```sh
rm -f usr/initramfs_data.cpio usr/initramfs_data.cpio.*
# 确认 CONFIG_INITRAMFS_SOURCE=""、RD_GZIP/DECOMPRESS_GZIP=y
make -j2 ARCH=arm CROSS_COMPILE=... zImage
# 启动日志里的 Linux version 构建时间必须是新时间
```

### 6.1 U-Boot 直接刷写评估

从 bootfs1 提取 U-Boot 后确认有：

```text
flash_bootfs_raw <bootfs binary> [img]
flash_rootfs_raw <rootfs binary> [img]
```

但实现是：

```text
tftp <addr> <file>
ubi check rootfs%d
ubi remove rootfs%d
ubi create rootfs%d <size> dynamic <id>
ubi write <addr> rootfs%d <size>
ubi detach
```

问题：

- 底层仍是 U-Boot `ubi write`，之前实测会静默产生 `ECC error -74`；
- 必须先 TFTP；当前 `sdk active_port 2` 在 100M 链路上仍报 link down，PC 在 lan2 时 TFTP 过不去；
- remove/create 可能改变卷 ID，和内核 `root=/dev/ubiblock0_4` 冲突。

因此**没有用这条路径**写 rootfs1。

### 6.2 c_noubi initramfs 写入 rootfs1（成功）

把 PC 网线从 LAN2 换到 **2.5G 口（RTL8125）** 后，c_noubi initramfs
（`c_noubi.fit`，6.18.55，XMODEM 713 秒）的网络可用：

```text
bcm-pcie: Core [1] Link UP - GEN2
r8169 0002:01:00.0 eth0: RTL8125B
br-lan: 192.168.1.1/24
```

在 initramfs 里执行：

```sh
ubiattach -m 1
wget -q http://192.168.1.113:9000/root.squashfs -O /tmp/root.sq
sha256sum /tmp/root.sq
# 451586d02ce3efb0337d1f25edd686f22f99bc80a279c9020a4bfa61d41bbf26
ubiupdatevol /dev/ubi0_4 /tmp/root.sq
dd if=/dev/ubi0_4 bs=3310478 count=1 | sha256sum
# 451586d02ce3efb0337d1f25edd686f22f99bc80a279c9020a4bfa61d41bbf26
```

**rootfs1 已成功写入 CI 的 6.18 rootfs，并回读校验一致。**

### 6.3 bootfs1 写入与 boot loop（失败 → 恢复）

最初把 CI 的 6.65 MB `zImage` FIT 写进 bootfs1 时，`bootm` 后反复重启：slot1 的
bootfs1 仍是厂商 4.19，但 rootfs1 已经是 6.18 rootfs，4.19 内核挂不上 6.18
squashfs（`ubi.block=0,4` 内容已变）→ 内核 panic → WDT 复位，形成 boot loop。

> 教训：**bootfs 和 rootfs 必须成对替换**。只换 rootfs 会让旧内核挂新文件系统。

恢复靠 U-Boot env 的免网入口（上电按住 RESET）：

```text
rz_rec=echo RECOVERY-WINDOW;ubi part image;
       if ubi read 0x02000000 recovery-bootfs; then bootm 0x02000000;
       else echo NO-RECOVERY;loadx 0x02000000;bootm 0x02000000; fi
```

`recovery-bootfs`（ubi0_8）里的 initramfs（4.19.235）能起来，带 `ubiupdatevol`
和 `nc`，是本次实际使用的救援通道。

注意 recovery initramfs 里的两个坑：

- `ubiattach -m 1` 会把已 attach 的 mtd1 再 attach 成 ubi1 并报 `ECC error -74`；
  **不要重复 attach**，直接用 `/dev/ubi0_3` / `/dev/ubi0_4`；
- `wget` / `uclient-fetch` 连 192.168.1.113 一律 `Failed to send request: Operation not permitted`
  （ping 正常、`nc` 正常），最终用 `nc 192.168.1.113 9002 > b1.fit` 裸流传输，
  sha256 与 PC 侧一致。

### 6.4 bootfs1 写入 6.18 FIT（成功）

用本地 3.62 MB `zImage` 打的 `bootfs1-6.18.60.fit`（8,516,082 B，
sha `9cefa0243a7796048017daa125b484022ec860242315ae981e0c7f0e9a3ea602`）：

```sh
nc 192.168.1.113 9002 < /dev/null > /tmp/b1.fit
sha256sum /tmp/b1.fit
ubiupdatevol /dev/ubi0_3 /tmp/b1.fit
dd if=/dev/ubi0_3 bs=8516082 count=1 | sha256sum   # 与上面一致
echo 'once=run rz_img1' > /proc/nvram/set
sync
reboot -f
```

### 6.5 完整 6.18.60 从 NAND 启动（成功）

```text
Linux version 6.18.60 (zhang@ubuntu-op) ... #1 SMP Thu Nov  5 22:40:34 CST 2026
OpenWrt SNAPSHOT, r0-5fb9d9c
DISTRIB_TARGET='bcmbca/generic'
VFS: Mounted root (squashfs filesystem) readonly on device 254:0.
mount_root: switching to ubifs overlay
bcm-pcie: Core [0]/[1] Link UP - [1] lanes, [GEN2] speed
```

日志：[`test_logs/kernel-6.18.60-overlay-boot.log`](../../../test_logs/kernel-6.18.60-overlay-boot.log)
（第一次落盘启动的完整串口记录是 `test_logs/kernel-6.18.60-boot-from-nand.log`，
但那次抓取脚本在 `reboot` 后只收到 shell 回显，实际启动日志以上面这份为准）。

### 6.6 模块不匹配（已定位 → 已修）

CI rootfs 里没有 `realtek.ko` / `hwmon.ko` / `i2c-core.ko`，导致：

```text
r8169 ... no dedicated PHY driver found for PHY ID 0x001cc840
hwmon: Unknown symbol i2c_verify_client (err -2)
```

根因不是内核配置漂移，而是 **`DEVICE_PACKAGES` 漏了 `kmod-phy-realtek`**：
该包 `DEPENDS=+kmod-libphy +kmod-hwmon-core`，而 `kmod-hwmon-core`
`DEPENDS=+kmod-i2c-core`，三个模块必须一起进 rootfs。已在
`port/overlay/target/linux/bcmbca/image/Makefile` 补上。

现场验证时先用“救急”方式把本地同配置构建的四个 ko 塞进 overlay：

```sh
mount -t ubifs /dev/ubi0_0 /mnt
mkdir -p /mnt/upper/lib/modules/6.18.60
cp i2c-core.ko hwmon.ko realtek.ko r8169.ko /mnt/upper/lib/modules/6.18.60/
sync; umount /mnt; reboot -f
```

启动后：

```text
hwmon     20480  1 realtek
i2c_core  40960  1 hwmon
realtek   45056  1
r8169     90112  0
Realtek Internal NBASE-T PHY r8169-2-100:00: attached PHY driver
r8169 0002:01:00.0 eth0: RTL8125B
```

### 6.7 网络实测（成功）

```text
eth0 carrier=1
r8169 ... Link is Up - 100Mbps/Full (downshifted) - flow control rx/tx
  ↑ "Downshift occurred from negotiated speed 1Gbps to actual speed 100Mbps, check cabling!"

LuCI : HTTP 200 / root 从 192.168.1.113 登录成功
SSH  : root@192.168.1.1 免密登录，uname=6.18.60
ubus : model="FiberHome HG3142F", board_name="fiberhome,mb4784w"
吞吐 : dd 52,428,800 B / 4.46 s = 11.76 MB/s ≈ 94.1 Mbps（100M 线速）
```

100M 是**本次线缆/水晶头问题**（同一根线插 LAN2 时是 1000/full，插 2.5G 口时
PHY 报 1G 协商后降速到 100M），不是驱动问题；PHY 已正常挂上
`Realtek Internal NBASE-T PHY`，换线后可再验 1G/2.5G。

## 七、未覆盖的测试

- **2.5G 速率**：PC 侧只有千兆网卡，且当前线缆在 2.5G 口降速到 100M，缺 2.5G 对端。
- **LAN1-4 / PON**：`bcm_enet` 交换与 PON 未在本轮启用/验证。
- **`bcm_enet`/`bcm_pcie`/`bcm_reboot` 树外模块**：已编译、vermagic 正确，但没有装进
  6.18 rootfs 做实机加载验证（本轮 2.5G 口走的是主线 `r8169` + `kmod-bcm-pcie`
  的 PCIe 主控，未加载 `bcm_pcie.ko`）。

## 九、CI #104：packaging 修复验证（成功）

- 提交：`d31b0d6`
- 运行：https://github.com/hanlihanshaobo/HG3142F-openwrt/actions/runs/42744351073
- 结论：**success**（29m32s，缓存命中）
- 产物：`openwrt-mb4784w-104`

关键核对：`DEVICE_PACKAGES` 已含 `kmod-phy-realtek`（构建日志里可见），
解包 `root.squashfs` 确认：

```text
/lib/modules/6.18.60/realtek.ko   55,236 B
/lib/modules/6.18.60/hwmon.ko     19,964 B
/etc/modules.d/18-phy-realtek
/etc/modules.d/51-i2c-core
```

### 9.1 CI 模块在实机内核上的加载验证

把上面的 `realtek.ko` / `hwmon.ko` 从 `root.squashfs` 取出，`nc` 传到设备 `/tmp`，
先 `rmmod r8169 realtek hwmon`，再按顺序 `insmod`：

```text
hwmon_rc=0
realtek_rc=0
r8169_rc=0
Realtek Internal NBASE-T PHY r8169-2-100:00: attached PHY driver
r8169 0002:01:00.0 eth0: Link is Up - 100Mbps/Full
br-lan: port 1(eth0) entered forwarding state
```

结论：

- **CI rootfs 自带的 `realtek.ko` + `hwmon.ko` 能直接挂 PHY**，不再需要往 overlay
  塞本地模块；
- 唯一还需要的是 `i2c-core.ko`，因为本机测试内核是 `CONFIG_I2C=m`
  （CI 内核按仓库 `config-6.18` 是 `CONFIG_I2C=y`，符号在内核里，不需要模块）。
  这也说明“CI 模块在本机内核缺 `i2c_verify_client`”是**本机内核配置偏差**，
  不是仓库问题。

### 9.2 尚未做的一步

`root.squashfs`（sha `3e7e7ad2…`）还没重新写进 rootfs1；当前设备跑的仍是
#101 的 rootfs + overlay 里的本地模块。因为 9.1 已经验证模块可用，重刷只是
把 overlay 补丁去掉，功能上等价，可留到下次维护窗口。

## 十、结论与下一步

1. 6.18.60 的本地构建、GitHub Actions 完整构建、**实机 RAM 启动**、
   **bootfs1 + rootfs1 成对落盘启动**、**PCIe/2.5G 口网络（LuCI/SSH/94 Mbps）**、
   **CI rootfs 自带 PHY 驱动加载**均已通过。
2. 待完善（不影响合并）：
   - 2.5G 口当前只协商 100M，dmesg 报 `check cabling!`，需换线/换口复测；
   - 用 #104 的 rootfs 重刷一次，去掉 overlay 里的本地模块；
   - 追查 CI `zImage-nodtb` 比本地大 3 MB 的原因（两者都是 XZ）。
3. 合并：

   ```bash
   git checkout main
   git merge --no-ff kernel-6.18.60
   git push origin main
   ```

> 本文档的 CI 结果对应代码提交 `d31b0d6`（#104）；后续文档提交不改变内核/驱动代码。
