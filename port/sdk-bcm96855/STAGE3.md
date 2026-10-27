# 阶段 3：网络驱动路线重选（2026-10-27 夜）

## 背景
真机已能启动 OpenWrt（内核 + 用户态 + MTD/UBI 全通，看门狗已修），**但没有任何网口**。
此前唯一方向是移植 Broadcom `bcm_enet`（Runner/RDPA 交换芯片），但它依赖 Broadcom 对
`sk_buff` / `net_device` 的内核结构体改造，编译探针卡在架构级障碍（见 STAGE2.md）。

## 重新评估：三条路
| 路线 | 结论 |
|---|---|
| ① `bcm_enet` 交换芯片（4 个 GPHY + RGMII + SGMII） | 最正统，但需移植 bcmkernel 内核 patch，**几天量级** |
| ② 板上 **RTL8125 (10ec:8125) 2.5G 网口** + 主线 `r8169` | 只要 PCIe 主控能枚举，**一个包就出网口**，最省事 |
| ③ PON 光口 / WiFi | 都是闭源（`bcmgpon`、`wl_6756`/`wl_6710`），放弃 |

## 路线 ② 的关键突破
- 实机 PCIe 三颗：`14e4:6756`(WiFi) / `14e4:6710`(WiFi) / **`10ec:8125`(RTL8125)**
- `r8169.ko` 在内核里（主线驱动），缺的只是 **PCIe 主控**
- 主线 `pcie-brcmstb.c` 支持 `brcm,bcm4908-enet`/`brcm,bcm4908-switch`，但**没有 bcm6855/963xx**
- **但是**：asuswrt 5.04axhnd.675x（正是 BCM96855 平台）的 GPL 源码里有
  **`bcmdrivers/opensource/bus/pci/host/impl1/pcie-bcm963xx.c`（3013 行）** —— 就是厂商
  `brcm,bcm963xx-pcie` 驱动的完整源码！
- 该驱动有 `CONFIG_BCM_PCIE_PMC_BRD_STUBS` 开关：**用驱动内置 stub 即可绕过闭源的 PMC 模块**
- 只依赖标准内核 API（pci.h / of_* / platform_device），**不碰 sk_buff / net_device 结构体**
  → 移植难度远低于 bcm_enet

## 已完成的准备
1. **驱动源码纳入 SDK 树**：`port/sdk-bcm96855/sdk/bcmdrivers/opensource/bus/pci/host/impl1/`
   （pcie-bcm963xx.c/.h、pcie_hcd.c/.h、pcie-vcore.c/.h、pcie-vdev.h）
2. **私有符号 stub**：`port/sdk-bcm96855/compat/pcie_stubs.c`，只需 5 个函数
   ```
   kerSysGetChipId()          -> 6855
   kerSysGetPciePortEnable()  -> 1
   kerSysGetPciePortMode()    -> 1 (RC)
   UtilGetChipRev()           -> 0
   ubus_decode_pcie_wnd_cfg() -> 0
   ```
   （声明在 `board.h` / `shared_utils.h`，SDK 树里都有，只是缺实现）
3. **CI 编译探针**：workflow 新增 `SDK compile probe (bcm_pcie)` 步骤
4. **DTS 节点**（数据取自实机原厂 DTB `orig_968552CREF_CM_XG.dts`）：
   ```dts
   pcie@0: reg <0x80040000 0xa000>, ranges 0xc0000000, IRQ 43, coreid 0, domain 1
   pcie@1: reg <0x80050000 0xa000>, ranges 0xd0000000, IRQ 44, coreid 1, domain 2
   ```
   RTL8125 挂在 pcie@1（0002:01:00.0）

## 预期与风险
- **预期**：PCIe 主控能枚举 → `r8169` 绑定 RTL8125 → 出现 `eth0`（2.5G）
- **风险**：驱动按 4.19 写，PCI 子系统 API 在 6.18 有变化（`pci_alloc_irq_vectors`、
  `reset_control`、`of_device_get_match_data` 等），需要适配若干处
- **兜底**：若 PCIe 也走不通，回到路线 ①（bcm_enet + 内核 patch）

## 真机测试流程（已验证可重复）
```
1. CI 构建 → 取 initramfs → 解出 zImage + DTB
2. tools/fit_build.py 打包 FIT（kernel + fdt + crc32）
3. tools/xmodem_send.py 通过 XMODEM 传到 0x02000000（115200，约 11 KB/s，6.2MB 约 9.5 分钟）
4. U-Boot 自动 bootm → OpenWrt 启动（串口可见）
```
