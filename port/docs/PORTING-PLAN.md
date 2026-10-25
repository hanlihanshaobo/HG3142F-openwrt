# 移植计划

## 参考项目对照

| 项目 | 平台 | 做法 | 我们能复用什么 |
|---|---|---|---|
| [PonWRT-TYFZA1](https://github.com/LeoooChen/PonWRT-TYFZA1) | Realtek RTL9617C | `port/overlay/target/linux/rtl9617c` 叠在 openwrt main 上，Actions 构建发 Release | 仓库结构、workflow 写法、base-files/uci-defaults 布局、原厂数据迁移 init 脚本思路 |
| [U-Boot-TYFZA1](https://github.com/LeoooChen/U-Boot-TYFZA1) | 同上 | 独立 U-Boot 仓库 + 恢复网页 + TTL 救援文档 | U-Boot 独立流程、`docs/TTL-RECOVERY.md` 的救援步骤模板 |
| [pbs05/ponwrt](https://github.com/pbs05/ponwrt) | Airoha AN7581/7583 | OpenWrt 分支内自带 `target/linux/airoha` + `package/boot/uboot-airoha` | target/image Makefile 写法、U-Boot 作为 package 的集成方式 |
| [pbs05/uboot-an758x](https://github.com/pbs05/uboot-an758x) | 同上 | 完整 U-Boot 树 fork | 光猫 U-Boot 补丁组织（bootmenu/env/readmem） |
| [pbs05/an758x-stock2ubi](https://github.com/pbs05/an758x-stock2ubi) | 同上 | Rust 工具：原厂固件 → UBI 重打包 + 安装器 | **阶段 4 的原厂数据迁移思路**：读原厂分区、重组 UBI、保留校准数据 |

> 三个仓库都是 Airoha/Realtek 平台，**驱动代码不能复用**，但工程方法论（叠层结构、Actions、证据化验证、原厂数据保留）可以直接搬。

## 阶段划分

### 阶段 1：内核能在串口说话（当前）

- [x] `target/linux/bcmbca` 骨架（Makefile / image / dts / base-files）
- [x] 设备 DTS：内存 + NAND 分区表按实机还原
- [ ] Actions 首次构建通过（验证 target 被 Kconfig 识别）
- [ ] 实机：TTL 串口接 115200，U-Boot 下 tftp 加载 initramfs，确认内核启动到 shell
- 需要：主线 `bcm6855.dtsi` 的节点名与 BCM6855x 实机一致（`&nand`、`serial0`）；不一致就补 patch

### 阶段 2：落盘 + sysupgrade

- [ ] 在 `image` 分区建 UBI，卷布局：`kernel` / `rootfs`（+ 预留 `rootfs_data`）
- [ ] U-Boot env 改 bootargs：`ubi.mtd=image root=/dev/ubiblock0_0` 之类
- [ ] 实现 `platform_do_upgrade`（写 UBI 卷）
- [ ] LuCI 能登录，overlay 落在 rootfs_data

### 阶段 3：网络（最硬的一块）

按难度递增：
1. **NAND / GPIO / 串口**：主线 brcmnand + pinctrl，基本可用
2. **以太网**：BCM6855x 是 UniMAC + 内部交换（SF2 系）。主线有 `bcmsysport`/`b53` 但 BCM6855 用哪套要看厂商 GPL 源码
3. **PON**：GPON MAC 驱动**主线没有**，必须从厂商 GPL 源码移植（`gpon`/`ponmac` 相关），或先放弃 PON、当普通 4 口路由用
4. **WiFi**：2.4G+5G 8 SSID，芯片型号待确认（要从原厂 rootfs 找 `wl`/`dhd`/`mt_wifi` 之类），Broadcom 无线驱动**开源不可得**，大概率只能放弃或保留原厂驱动二进制

> 现实预期：阶段 3 的 PON + WiFi 很可能无法完整实现。可行目标是把设备当 **4 口千兆 ARM 路由器**用。

### 阶段 4：原厂数据迁移

- [ ] 从 `factory` 分区读 SN/MAC/光学校准，生成 OpenWrt 侧 `/etc/config` 和 board.d 脚本
- [ ] 参考 `an758x-stock2ubi` 的做法：安装器先备份原厂分区，再重组 UBI
- [ ] 保留回滚路径：原厂 `image` 分区内容先 dump 成 tar 存 U 盘/网络

## 关键风险

| 风险 | 影响 | 缓解 |
|---|---|---|
| loader 无备份 | 改坏即砖 | 已有 `mtd1_loader.bin`，勿覆盖；TTL 救援预案 |
| 无厂商 GPL 源码 | 网口/PON/WiFi 缺驱动 | 优先找 Broadcom BCM6855 GPL 释放包（运营商/ODM 官网）；找到前只做内核+串口 |
| 主线 DTS 与实机不符 | 内核起不来 | 用原厂 dmesg/`/proc/iomem` 对齐地址 |
| UBI 卷布局与厂商不同 | 无法回滚原厂 | 先备份整个 `image` 分区再动 |
