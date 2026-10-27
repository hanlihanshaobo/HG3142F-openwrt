# BCM96855 网络驱动移植评估（A 方案成果）

> **⚠️ 板级基准更正（2026-10-27 10:36）**：实机启动 DTB 是 **`968552CREF_CM_XG`**（`OF: fdt: Machine model: 968552CREF_CM_XG`，SoC `compatible="brcm,bcm96855"`），
> 不是最初误选的 `968550XSV`。正确 DTS 见 `re_artifacts/orig_968552CREF_CM_XG.dts`。
> 两者核心网络寄存器/端口**完全一致**，仅 2 处差异：`/egphy base-addr`（实机无）、`/xfp_sfp compatible`（实机 `brcm,sff`）。
> 移植以 **968552CREF_CM_XG** 为准。

## 一、源码来源（全部来自公开 GPL 仓库）
| 组件 | 来源仓库 | 路径 |
|---|---|---|
| enet 驱动 (impl7) | `RMerl/asuswrt-merlin.ng` | `release/src-rt-5.04axhnd.675x/bcmdrivers/opensource/net/enet/impl7/` |
| RDPA API 头 (50 个) | 同上 | `.../rdp/drivers/rdpa_gpl/include/` |
| 6855 板级配置 | 同上 | `.../rdp/projects/PON_6855_FC/drivers/{rdd,rdpa}` |
| rdpa_drv 驱动 | `jclehner/bcmdrivers-gpl-bcm963xx` | `bcmdrivers/opensource/char/rdpa_drv/impl1/` |
| rdpa_gpl_ext | 同上 | `bcmdrivers/opensource/char/rdpa_gpl_ext/impl1/` |

> asuswrt 的 `BRCM_CHIP` 支持列表含 **6855**；`rdp/projects` 含 **`PON_6855_FC`**，与本机 BCM6855x 精确对应。

## 二、已下载资产（`port/sdk-bcm96855/`，171 文件 / 2.2MB）
| 目录 | 文件数 | 内容 |
|---|---|---|
| `enet-impl7/` | 59 | `enet.c`(86K) `port.c` `runner.c`(64K) `dt_parsing.c` `g9991.c` `phy_drv_pon.c` `mux_index.c` `sw_common.c` `sf2.c` `bcmenet_ioctl_compat.c`(128K) `bcmenet-bindings.txt` |
| `rdpa_drv-impl1/` | 26 | RDPA 字符设备驱动（port/sys/tm/filter/ic…） |
| `rdpa_gpl-include/` | 50 | `rdpa_api.h` `rdpa_gem.h` `rdpa_cpu.h` `rdpa_egress_tm.h` 等 |
| `rdpa_gpl_ext-impl1/` | 36 | RDPA GPL 扩展 + `examples/*.bdmf` |

## 三、DTS 属性已由驱动源码确认（`bcmenet-bindings.txt`）
- switch 节点：`ports`(必需) `sw-type`(必需) `label` `unit` `switch-reset`(gpio)
- port 节点：`reg`(必需) `mac-type`(必需) `phy-handle` `phy-mode` `mii-pinctrl-state` `rgmii-1p8v/3p3v/disabled` `rgmii-intf` `tx-delay` `rx-delay` `is-wan` `wan-only/lan-only/wan-preferred` `detect`
- 与 `orig_968550XSV.dts` 中提取的属性 **逐条吻合** → DTS 可直接移植

## 四、仍缺失（完整编译所需）
1. **rdp 框架实现**：`rdp/drivers/`（rdd、rdpa 核心、bdmf 实现）——数千文件
2. **bdmf 框架**：`rdp/projects/PON_6855_FC/.../bdmf` 或 `rdp/drivers/bdmf`
3. **bcm963xx 通用头**：`bcmdrivers/broadcom/include/bcm963xx/`（17 头，如 `bcm_pktfwd.h`）
4. **shared 头**：`shared/opensource/include/bcm963xx/`

## 五、最大风险：内核版本差
- 原厂 / asuswrt SDK 面向 **Linux 4.1**（asuswrt `kernel/linux-4.1`）
- 目标 OpenWrt 用 **Linux 6.18**
- 4.1 → 6.18 之间大量内核 API 变更（`net_device`、NAPI、`sk_buff`、`procfs`、`timer`、`tasklet`、`ksmbd`/`bcm_pktfwd` 依赖的私有 hook 等），预计需大量适配补丁

## 六、务实路径（分阶段）
- **阶段 1（可控）**：补齐缺失头 + rdp 框架源码，建立 Kbuild 骨架，在 6.18 下尝试 `make M=` 编译 `bcm_enet`，先统计编译错误量级
- **阶段 2**：按错误逐个适配 6.18 API（预计数十~上百处）
- **阶段 3**：DTS overlay（switch0/mdio/rgmii/egphy/sgmii/unimac/gpon/rdpa）接入，真机验证 link up
- **备选 B**：若编译适配成本过高，转 REA 反编译 `bcm_enet.ko` 重建最小驱动（但 `bcm_bp3drv` RSA license 仍是死结）

## 七、结论
- **源码可得性：已解决**（171 文件在手，路径全部明确）
- **可编译性：待验证**（缺 rdp/bdmf 框架，且需 6.18 内核适配）
- **license 风险**：`bcm_enet`（opensource 目录）可用；`bcm_bp3drv`（RSA 绑定）若运行必需则是硬障碍——需实测确认是否可绕过
