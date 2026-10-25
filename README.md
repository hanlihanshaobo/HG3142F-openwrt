# HG3142F-lede

烽火 **HG3142F**（ONU 光猫，Broadcom **BCM6855x** / `bcm96855`）的 OpenWrt 适配仓库。

基于 **openwrt/openwrt 主线（锁定提交）** + `port/overlay` 叠层，GitHub Actions 在线构建。工程结构参考了几个已成功的光猫移植项目（见下方「参考项目」）。

---

## 当前状态

| 阶段 | 内容 | 状态 |
|---|---|---|
| 0 | 仓库骨架 / Actions 流水线 / `target/linux/bcmbca` 骨架 | ✅ 已搭建 |
| 1 | 内核串口启动（主线 `bcm6855.dtsi` + 本机 DTS + initramfs） | ⏳ 待实机验证 |
| 2 | NAND + UBI + squashfs 落盘启动，LuCI 可登录 | ⏳ |
| 3 | 网口 / PON / WiFi 驱动 | ⏳ 难度最高，见移植计划 |
| 4 | 原厂数据迁移（factory 校准 / SN / MAC）、sysupgrade 稳定 | ⏳ |

> ⚠️ **当前不是可用固件**。OpenWrt 上游没有 BCM6855x 的 target（只有 `bcm4908`/ARM64 与 `bcm63xx`/MIPS），`target/linux/bcmbca` 是本仓库自建骨架，需要逐轮迭代到实机可启动。

## 硬件（实测）

| 项目 | 值 |
|---|---|
| SoC | Broadcom BCM6855x（`brcm,bcm96855`，BCMCA 家族，3×Cortex-A7） |
| 内存 / 闪存 | 512 MiB DDR / 256 MiB NAND（128 KiB 擦除块） |
| 引导 | **U-Boot 2019.07**（Broadcom 定制，SPL+TPL+DDR+FIT） |
| 原厂内核 | Linux 4.19.235，Buildroot 2021.02.4 |
| 分区 | loader 2M / image 164M(UBI 双槽) / ipg 87M / factory 2M |

完整证据链见 [`port/docs/HARDWARE.md`](port/docs/HARDWARE.md)，移植路线与风险见 [`port/docs/PORTING-PLAN.md`](port/docs/PORTING-PLAN.md)。

## 目录结构

```
.github/workflows/
  build-openwrt.yml       # 主固件：拉 openwrt 主线 + 叠 port/overlay + 构建 + 发 Release
  build-uboot.yml         # 主线 U-Boot bcm96855 构建（RAM 引导用）
port/
  sources.lock.json       # 上游源码与设备信息（pin 提交，保证可复现）
  scripts/prepare.sh      # 拉源码 + 叠 overlay + 打补丁
  configs/mb4784w.config  # 设备 .config 片段
  overlay/                # 直接覆盖到 openwrt 源码树
    target/linux/bcmbca/  # 自建 target：Makefile / image / dts / base-files
  uboot/                  # U-Boot 板级改动与环境变量草稿
  patches/                # 对 openwrt 源码的补丁（当前为空）
  docs/                   # 硬件事实、移植计划
```

## 构建

Actions 页面 → `Build OpenWrt firmware` → Run workflow，或打 `v*` 标签自动构建并发 Release（prerelease）。

本地复现：

```sh
sh port/scripts/prepare.sh sources/openwrt
cd sources/openwrt
./scripts/feeds update -a && ./scripts/feeds install -a
cp ../../port/configs/mb4784w.config .config
make defconfig
make -j$(nproc) V=s
```

产物：`bin/targets/bcmbca/generic/`

## 参考项目

| 项目 | 复用什么 |
|---|---|
| [LeoooChen/PonWRT-TYFZA1](https://github.com/LeoooChen/PonWRT-TYFZA1) | 仓库结构（`port/overlay` + Actions + base-files/uci-defaults）、原厂数据迁移思路 |
| [LeoooChen/U-Boot-TYFZA1](https://github.com/LeoooChen/U-Boot-TYFZA1) | U-Boot 独立流程、TTL 救援文档模板 |
| [pbs05/ponwrt](https://github.com/pbs05/ponwrt) | target/image Makefile 写法、U-Boot 作为 package 的集成 |
| [pbs05/uboot-an758x](https://github.com/pbs05/uboot-an758x) | 光猫 U-Boot 补丁组织方式 |
| [pbs05/an758x-stock2ubi](https://github.com/pbs05/an758x-stock2ubi) | 原厂固件 → UBI 重打包 / 安装器 / 校准数据保留 |
| [u-boot/u-boot](https://github.com/u-boot/u-boot) | 原生 `bcm96855_defconfig` + `bcm6855.dtsi`（2022-08 Broadcom 上游化） |

> 这些项目是 Airoha / Realtek 平台，**驱动代码无法复用**，复用的是工程方法与流程。

## 免责声明

仅用于自有设备的合法研究、备份与互操作。刷机有变砖风险：本机虽有 A/B 双槽，但 **loader 只有一份**，改动引导程序前必须保留 `mtd1` 原始镜像并备好 TTL 救援。

## License

GPL-2.0（与 OpenWrt 一致）
