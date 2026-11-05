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

## 五、未覆盖的测试

- **实机启动** 未做：本次只验证编译、镜像和模块 ABI，不写 NAND、不改设备槽位。
- 2.5G / LAN / PON 等运行时功能需要在设备上单独验证。

## 六、结论与下一步

1. 6.18.60 的本地构建和 GitHub Actions 完整构建均已通过。
2. 目前只差实机启动验证；建议先在 slot1 或 RAM 中验证 `Linux version 6.18.60`
   和 `bcm_enet.ko` 加载，再决定是否合并。
3. 实机通过后合并：

   ```bash
   git checkout main
   git merge --no-ff kernel-6.18.60
   git push origin main
   ```

> 本文档的 CI 结果对应代码提交 `629ebfc`；后续文档提交不改变内核/驱动代码。
