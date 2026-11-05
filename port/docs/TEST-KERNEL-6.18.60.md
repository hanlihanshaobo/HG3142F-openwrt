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

## 四、未覆盖的测试

- **完整 OpenWrt 镜像**（rootfs / 包 / feeds）未构建：构建机访问 GitHub 受限，无法拉取 feeds。
- **GitHub Actions** 未触发：本地分支尚未 push 到 GitHub（网络限制）。
- **实机启动** 未做：本次只验证编译与模块 ABI，不写 NAND、不改设备槽位。

## 五、结论与下一步

1. 6.18.60 的代码适配和构建链路已经验证通过，可以进入 CI / 实机验证阶段。
2. 把 `kernel-6.18.60` push 到 GitHub：

   ```bash
   cd HG3142F-openwrt
   git push -u origin kernel-6.18.60
   ```

3. 在 Actions 页面选择 `Build OpenWrt firmware` → `Run workflow`，分支选
   `kernel-6.18.60`，先跑一次**完整构建**（不要勾 `module_only`）：
   - 检查 `==> 内核基线: Linux 6.18.60`
   - 检查 `zImage-nodtb` / DTB / `bcm_enet.ko` 产物
   - 检查 `bcm_enet` / `bcm_pcie` probe 的 `error count = 0`
4. CI 和实机都通过后，再把 `kernel-6.18.60` 合并进 `main`：

   ```bash
   git checkout main
   git merge --no-ff kernel-6.18.60
   git push origin main
   ```
