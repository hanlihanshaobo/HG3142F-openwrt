# 阶段 4：核实 CI 真实进展 + 兼容层补齐（2026-10-29 下午）

## 一、先纠正上一轮记录里的一个错误

commit `643104a` 的提交信息写的是「#53 链接失败，15 个厂商符号 undefined」。
本次用 token 拉取 run 53 / run 54 的**原始日志**核对，结论不是这样：

| run | sha | 实际结果 |
|---|---|---|
| #51 | `d5bfecd` | 8 个 error（switchdev / PHY_OP_* / EXT_SWITCH_TYPE） |
| #52 | `db0b6aa` | 那些已修，继续暴露更深错误 |
| #53 | `aef2f4e` | **仍然编译失败**，没有进入链接（没有 `LD [M] bcm_enet.ko`） |
| #54 | `643104a` | 同上：11 个 .o 里 9 个编过，`runner.o` / `dt_parsing.o` 失败 |

所以「15 个 undefined」不是 #53 的链接结果（该轮根本没链接）；可能是本地/更早
的某次尝试。`enet_stubs.c` 本身没问题，但当时**还没走到需要它的那一步**。

run 54 artifacts（`openwrt-mb4784w-54`）里也确实没有 `bcm_enet.ko`。

## 二、run 54 的真实 3 个编译阻塞

1. `runner_common.h:57-59`：`#ifdef CONFIG_NET_SWITCHDEV` 包着
   `struct switchdev_attr/trans` 的原型，6.18 没有这些结构 →
   `declared inside parameter list` 被 `-Werror` 拦下。
   之前只 sed 了 `#if defined(...)` 形式和 enet/port/g9991 三个文件。
2. `bdmf_session.h:37`：`#include <stdarg.h>`，内核 `-nostdinc` 下找不到。
3. `dt_parsing.c:75`：`#include "drivers/bcm_chip_arch.c"` —— 这是厂商在完整
   SDK 里**按芯片生成**的文件，GPL 包里没有。

## 三、本轮改动

### 新增
| 文件 | 作用 |
|---|---|
| `compat/stdarg.h` | 转 `#include <linux/stdarg.h>`，解决 -nostdinc |
| `compat/bcm_chip_arch_compat.c` | 补 `chip_arch_*_portmap[]`（6855：0-5 LAN、6 WAN、all=0x7f） |
| `compat/enet_framework_stubs.c` | `mac_drivers_set/init`、`mac_dev_dt_probe`（unimac 无源码，明确失败） |
| `compat/phy_drv_compat.h` | compat 层 init/exit 声明 |

### `compat/phy_drv_compat.c`（核心）
对照 `phy_drv.h`：**21 个非 inline 函数 + 2 个 typedef**（此前口径说 20/23），
现在 21 个全部有实现 —— 本轮补上了唯一缺失的 `phy_priv_fun()`。
其余改动：

- `phy_dev_add()`：保留框架语义（驱动必须已注册、按类型+地址可查）；
- `phy_drv_find_device()`：查不到且节点是 `brcm,bcaphy` 时**按 DTS 现场创建**
  PHY 对象（读 `phy-type` / `reg` / 父节点 `bus-type`），对应厂商
  phy/dt_parsing.c 的 probe，但不依赖它的 late_initcall 顺序；
- `phy_dev_attach()`：设置 mii_type/延迟/instance、取 MDIO_V1 总线、调
  `phy_drv->dev_add`；
- link 轮询：优先用驱动自带 `link_change_register`，没有才用 compat 的
  200ms delayed_work；回调指针同时写进 `phy_dev->link_change_cb/ctx`
  （厂商结构体正式字段），notify 优先用它们；
- 注册两个**标准 MII 占位 PHY 驱动**（EGPHY/SGMII）：读 BMSR/BMCR 判
  link/speed/duplex。原厂 `phy_drv_6846_egphy.c` 依赖 `phy_drv_brcm.o`，
  而它是 **aarch64** 的 prebuilt，armv7 用不了；
- `bus_drv_get()`：compat 版总线入口，暂无 MDIO 驱动时返回 NULL，
  `phy_bus_read/write` 退化为 no-op（bring-up 阶段不乱写 PHY 寄存器）；
- `phy_drv_compat_exit()`：连 phy 对象一起释放。

### 构建脚本
- CI 探针 + 包 Makefile 都加了两个新 .o、`#ifdef CONFIG_NET_SWITCHDEV`→`#if 0`
  的多文件 sed、`dt_parsing.c` 的 include 替换、recycle_flags 两处 sed；
- 探针日志新增 **modpost undefined 符号清单**输出（链接允许未解析符号，
  insmod 才报，先把清单拿到手）。

## 四、关键发现（影响后续路线）

`port/sdk-bcm96855/sdk/bcmdrivers/opensource/phy/` 下的 prebuilt：
`phy_drv.o`、`phy_drv_brcm.o`、`mac_drv_unimac.o`、`unimac_drv_impl1.o`
**全部是 ELF64 aarch64**（`e_machine=183`），而 HG3142F 是 armv7l。
→ 这些 .o 一个都不能链接，unimac MAC 驱动必须自己写或换来源。

## 五、下一步
1. 触发一轮 CI，确认 11 个 .o 全过、`LD [M] bcm_enet.ko` 出现；
2. 拿 modpost 清单补桩，再 insmod 看 `Unknown symbol` 与 probe 停在哪；
3. 写最小 unimac MAC 驱动（或找 32 位源码），否则端口解析必然停在
   `Failed to create mac for port 0`；
4. MDIO：DTS 的 `mdio` 节点是 `bus-type="MDIO_V1"`，但 6855 的寄存器
   基址驱动（`mdio_drv_impl5` 只匹配 `brcm,mdio5`）与实机 `brcm,mdio1`
   对不上，需要确认 6855 的 MDIO 实现；
5. DTS：OpenWrt 板级 DTS 目前**没有** switch0/mdio/phy 节点，需按
   `orig_968552CREF_CM_XG.dts` 补（等 MDIO/MAC 有着落再加，避免空节点误导）。
