# U-Boot（bcm96855 / BCM6855x）

## 现状

- 设备原厂 loader（`mtd1`，2 MiB）是 **Broadcom 定制 U-Boot 2019.07**：SPL + TPL + DDR standalone + U-Boot proper，加载 FIT。
- 主线 U-Boot 自 **v2022.10** 起原生支持该 SoC：
  - `configs/bcm96855_defconfig`
  - `arch/arm/dts/bcm96855.dts` / `arch/arm/dts/bcm6855.dtsi`
  - `arch/arm/mach-bcmbca/bcm6855/`
- 但主线 defconfig 是 **RAM 引导**配置（从 flash/网络加载到 `0x01000000` 再跳转），**不含 SPL/TPL**，不能直接替换 loader 分区。

## 策略

1. **短期（阶段 1-2）**：不动 loader。用原厂 U-Boot 控制台（TTL 115200）通过 `tftp`/`loadb` 手动加载本仓库构建的 `zImage` + dtb + initramfs，验证内核。
2. **中期**：基于主线 U-Boot 增加 BCM6855x 的 NAND 启动链（SPL/TPL），得到可独立启动的 loader，先在 RAM 里验证 `go` 再考虑写 `mtd1`。
3. **长期**：bootmenu + 恢复网页（参考 [U-Boot-TYFZA1](https://github.com/LeoooChen/U-Boot-TYFZA1) 和 [uboot-an758x](https://github.com/pbs05/uboot-an758x) 的做法）。

## 构建

由仓库根目录的 `.github/workflows/build-uboot.yml` 完成（主线 `v2024.04`，`bcm96855_defconfig`）。
板级改动放在 `port/uboot/files/`，会被覆盖进 u-boot 树；自定义 defconfig 放 `port/uboot/bcm96855_defconfig`。

## 串口与救援

- 串口参数：**115200 8N1**，板上有 TTL 焊点（具体位置待拍板确认）
- 上电时按住任意键可进 U-Boot 控制台（原厂一般有 1~3 秒窗口）
- 救援优先用 **TFTP 加载 initramfs**，不改任何 flash

## 环境变量草稿

见 `uEnv.txt`。写入前请先在 RAM 里 `setenv` 调试，确认能启动再 `saveenv`。
