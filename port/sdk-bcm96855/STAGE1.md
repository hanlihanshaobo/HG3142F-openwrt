# 阶段 1 报告：SDK 源码树搭建 + 编译摸底

日期：2026-10-27
目标：验证 BCM96855 enet 驱动在 OpenWrt 6.18 下的可编译性

## 一、已完成：SDK 源码树（951 文件 / 38MB）
位置：`port/sdk-bcm96855/sdk/`

| 目录 | 文件数 | 来源 |
|---|---|---|
| `rdp/firmware/` | 85 | asuswrt 675x（Runner firmware：rdd/rdp/cpu/tm/gso） |
| `rdp/projects/PON_6855_FC/` | 64 | asuswrt（BCM6855 板级配置） |
| `rdp/drivers/rdpa_gpl/include/` | 50 | asuswrt（RDPA API 头） |
| `shared/opensource/include/` | 88 | asuswrt |
| `bcmdrivers/broadcom/include/` | 17 | asuswrt |
| `bcmdrivers/opensource/include/` | 113 | asuswrt |
| `bcmdrivers/opensource/phy/` | 298 | asuswrt（PHY 驱动） |
| `bcmdrivers/opensource/net/enet/impl7/` | 59 | asuswrt（enet 驱动） |
| `bcmdrivers/opensource/net/enet/shared/` | 17 | asuswrt（bcmenet.h/bcmmii.h 等） |
| `bcmdrivers/opensource/char/rdpa_drv/` | 26 | jclehner |
| `bcmdrivers/opensource/char/rdpa_gpl_ext/` | 36 | jclehner |
| `bcmdrivers/opensource/char/bdmf/` | 44 | T3200M（bdmf 框架源码） |
| `kernel/bcmkernel/include/` | 54 | asuswrt（Broadcom 内核扩展头） |

## 二、include 静态解析（核心文件）
| 源文件 | 缺失 SDK 头 |
|---|---|
| `port.c` | **0** ✅ |
| `dt_parsing.c` | **0** ✅ |
| `g9991.c` | **0** ✅ |
| `sw_common.c` | **0** ✅ |
| `runner.c` | 1（`ptp_1588.h`） |
| `bcmenet_ioctl_compat.c` | 1（`ptp_1588.h`） |
| `enet.c` | 3（`ptp_1588.h`/`osl.h`/`br_private.h`） |
| `sf2.c` | 2（`osl.h`/`sf2_dbg_proc.c`） |

剩余 3 个边角头：
- `ptp_1588.h`：PTP 1588，在更高版本 SDK（5.04behnd.4916）或 ptp 目录
- `osl.h`：WiFi 驱动的 OSL 抽象层（enet 可 stub 或从 wl 驱动拿）
- `br_private.h`：**Linux 4.1 内核 bridge 私有头**（6.18 已重构，需适配）

## 三、内核 4.1 → 6.18 API 风险扫描（33 个 .c）
| 风险点 | 次数 | 处理方式 |
|---|---|---|
| `do_gettimeofday`（6.x 移除） | 2 | 改 `ktime_get_*` |
| `ndo_do_ioctl`（6.x 改 `ndo_siocdevprivate`） | 2 | 改回调名 |
| `bcm_pktfwd`（Broadcom 内核补丁） | 6 | 移植 patch 或 stub |
| `br_private.h`（内核 bridge 私有头） | 1 | 适配 6.18 bridge API |

**结论：适配量约 10~20 处，属"可控"级别**（不是推倒重来）。skb/EXPORT_SYMBOL 等命中是正常用法。

## 四、下一步（阶段 2）
1. **补最后 3 个头**（ptp_1588.h 从 5.04behnd 拿；osl.h 从 wl 驱动拿；br_private.h 需写 6.18 适配层）
2. **真编译**：在 OpenWrt CI（6.18 内核树 + 交叉编译器）里按 Kbuild 编 `bcm_enet`，统计真实编译错误
3. **B 方案（REA）保留**：若 bdmf/RDP 框架适配成本爆炸，回退反编译

## 五、风险与判断
- ✅ 源码可得：已解决（951 文件）
- ✅ include 可解析：核心文件 0 缺失
- ✅ 内核 API 风险：小（~11 处）
- ⚠️ 最大不确定：`rdp/firmware`（858 行级的 Runner 固件逻辑）是否依赖 bcmkernel 深度补丁（`bcm_pktfwd` 等）
- ⚠️ `bcm_bp3drv` RSA license：若运行必需仍卡（待实测）

## 六、阶段 1.5：3 个缺失头已全部解决
| 头 | 性质 | 处理 |
|---|---|---|
| `ptp_1588.h` | `#ifdef CONFIG_BCM_PTP_1588` 条件编译 | 不定义该宏即可，无需文件 |
| `osl.h` | `#ifdef PKTC` 条件编译 | 不定义 PKTC 即可，无需文件 |
| `br_private.h` | **冗余 include**（enet.c 只用局部变量 `br_dev` + 自有函数） | 已建 `compat/br_private.h` 空 stub，编译时 `-Icompat` 优先 |

补证：`netdev_get_bridge_master()` 实际定义在 `kernel/bcmkernel/include/linux/bcm_netdevice.h`，`bridge_update_pbvlan()` 是 enet.c 自身 static 函数，均不依赖内核 bridge 私有头。

**结论：include 层已闭环，可进入真编译（阶段 2）。**
