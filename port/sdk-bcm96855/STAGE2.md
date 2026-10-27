# 阶段 2 报告：CI 编译探针真实结果

日期：2026-10-27
运行：run #19（workflow_dispatch，success；探针 continue-on-error）

## 一、探针链路已打通（修了 3 个坑）
| 坑 | 现象 | 修复 |
|---|---|---|
| 内核目录找不到 | `kernel dir not found` | `find build_dir` → `find $TOP/sources/openwrt/build_dir` |
| 交叉编译器找不到 | `arm-openwrt-linux-muslgnueabi-gcc: not found` | find 用绝对路径（相对 PATH 在 make 切目录后失效） |
| include 全失效 | `board.h: No such file` | `EXTRA_CFLAGS` 在 6.x 已弃用 → 改 `KCFLAGS`，并 `export STAGING_DIR` |

修复后 `probe rc=2`，**enet.c 真正开始编译**，暴露以下错误。

## 二、真实错误分类

### A 类：编译器警告被 `-Werror`（可快速绕过）
1. **`dt_access.h` 的 `const` 返回值**（8 处）
   `const bool dt_is_available(...)` → `-Werror=ignored-qualifiers`
2. **`nbuff.h` 的 `#warning`**
   `#warning L1 Cache line size is different from L2 cache line size!` → `-Werror=cpp`
3. **隐式函数声明 / 指针类型**
   `-Werror=implicit-function-declaration`、`-Werror=incompatible-pointer-types`

→ 加 `-Wno-error=ignored-qualifiers -Wno-error=cpp -Wno-error=implicit-function-declaration -Wno-error=incompatible-pointer-types` 可让编译继续，暴露更深错误。

### B 类：bcmkernel L2 cache 函数缺失（**硬骨头**）
`nbuff.h` 引用以下函数，6.18 上游内核没有，来自 Broadcom bcmkernel 补丁：
- `outer_spin_lock_irqsave()` / `outer_spin_unlock_irqrestore()`
- `outer_flush_line_no_lock()`
- `outer_sync_no_lock()`
- `outer_inv_line_no_lock()`

→ 需要移植 Broadcom 的 L2 cache 管理补丁（`arch/arm/mm/` 相关），或写等价实现。

### C 类：内核 API 不兼容（需改代码）
- `nbuff.h:313`：`set_bit(PG_dcache_clean, &phys_to_page(...)->flags)`
  6.18 的 `page->flags` 类型为 `memdesc_flags_t`（带 proxy），与 `unsigned long *` 不兼容
- 后续预计还有 `skb`、`net_device`、`timer` 等 API 差异（静态扫描已识别 `do_gettimeofday`×2、`ndo_do_ioctl`×2）

## 三、结论
- ✅ **探针机制可用**：以后每次构建都能自动编译 enet 并输出错误
- ✅ **A 类**：加几个 `-Wno-error` 即可
- ⚠️ **B 类**：需要 bcmkernel 的 L2 cache 补丁（查 asuswrt 的 `arch/arm/mm/` / `bcm_cache`）
- ⚠️ **C 类**：逐个适配 6.18 API（约 10~20 处，静态扫描已预估）

## 四、下一步
1. 探针加 `-Wno-error=...`，再跑一轮，把 A 类清掉，看 B/C 类的完整清单
2. 从 asuswrt 找 bcmkernel 的 L2 cache 补丁（`arch/arm/mm/cache-*.c` 或 `bcm_outer_cache`）
3. 逐个修 C 类 API 差异
4. 全绿后，DTS overlay + 真机验证

## 五、阶段 2.1（run #20）：A/B 类已过，收敛到 14 个具体错误
**已生效的修复**（commit `89f9a49`）：
- `-Wno-error=ignored-qualifiers/cpp/implicit-function-declaration/incompatible-pointer-types/int-conversion/return-mismatch/unused-function` → A 类全部降为 warning
- `compat/nbuff_outer_stub.h`（`-include` 前置）→ B 类 `outer_*` 5 个函数 no-op，编译通过

**run #20 剩余 14 个 error（全部集中在 nbuff.h / port.h / rdpa_api.h）**：
| # | 错误 | 根因 |
|---|---|---|
| 1 | `rdpa_api.h:38 fatal: bdmf_interface.h: No such file` | **缺 bdmf include 路径**（文件其实在 `bdmf/impl1/framework/`） |
| 2 | `nbuff.h:1425/1437 'struct sk_buff' has no member named 'queue'` | 6.18 移除 `skb->queue`（4.1 由 Broadcom patch 加） |
| 3 | `port.h:354 field 'switchdev_ops' has incomplete type` | 6.18 移除/重构 switchdev_ops |
| 4 | `nbuff.h:733 unknown type 'wlFlowInf_t'` | WiFi flow info 类型（注释：就是 uint32_t） |
| 5 | `nbuff.h:737/915/1136 unknown type 'RecycleFuncP'` | 定义在 `<linux/bcm_skbuff.h>`，nbuff.h 未 include |
| 6 | `nbuff.h:143 'NULL_STMT' undeclared`（多处） | Broadcom 内核补丁的空语句宏 |

**阶段 2.2 修复（commit `34d49aa`，run #21 验证中）**：
1. INC 补 `$SDK/bcmdrivers/opensource/char/bdmf/impl1/{framework,system,system/linux,system/linux/cm}`
2. `compat/nbuff_types_stub.h`：补 `NULL_STMT`、`wlFlowInf_t`、`RecycleFuncP`
3. CI 内 `sed` 临时删除 `nbuff.h` 的 `__BUILD_NBUFF_*_ACCESSOR(...queue)` 行（6.18 无该成员）
4. 仍未处理：`switchdev_ops`（port.h:354），预计下一轮暴露

## 六、当前判断
- 错误已从"未知量"收敛到**可枚举的十几个点**，且集中在 bcmkernel 的 nbuff.h（WiFi/包缓冲层）
- 剩余主要是 6.18 对 4.1 的 API 删除/重构：`skb->queue`、`switchdev_ops`、L2 cache 扩展
- 工作量可控，但**仍非"改几行就好"**——nbuff.h 是 Broadcom 包缓冲核心，改动需谨慎

## 七、阶段 2.3（run #21）：14 → 2 个错误
commit `34d49aa` 的修复生效：bdmf include 路径、`nbuff_types_stub.h`（NULL_STMT/wlFlowInf_t/RecycleFuncP）、sed 去 `skb->queue` accessor —— **错误从 14 降到 2**：

| 剩余 error | 根因 | 解法 |
|---|---|---|
| `port.h:354 switchdev_ops incomplete type` | 6.18 移除 `struct switchdev_ops`；源码里是 `#if defined(CONFIG_NET_SWITCHDEV)` 条件编译 | CI 内 `sed 's/#if defined(CONFIG_NET_SWITCHDEV)/#if 0/'` |
| `rdpa_api.h:56 fatal: rdpa_ag_system.h: No such file` | `rdpa_gpl/include/autogen/` 子目录未下载 | 补下 autogen 35 个头 |

**阶段 2.4 修复（commit `3385154`，run #22 验证中）**：补 autogen、sed 掉 switchdev。

## 八、预期与下一步
- 若 run #22 编译**通过**，下一步会遇**链接错误**：`bcm_enet.ko` 依赖 `bdmf`、`rdpa` 的大量符号（`rdpa_port_create`、`bdmf_dev_open` 等），需要把 bdmf/rdp 框架也编成模块并链接
- 之后是**运行期**：DTS overlay（switch0/mdio/unimac/rdpa）+ 真机验证
- 结论：**编译层已基本打通**，剩下是链接 + 运行期移植，比编译适配更重

## 九、阶段 2.5（run #22）：2 → 5（深入一层，正常现象）
前 2 个已修，编译继续深入后暴露新的 2 类：
| 错误 | 根因 | 解法 |
|---|---|---|
| `rdpa_ag_iptv.h:340/355/370/415 unknown type 'rdpa_fc_mcast_flow_t'` ×4 | autogen 的 rdpa_ag_iptv.h 未 include `rdpa_mcast.h`（该类型在 rdpa_mcast.h:66 typedef） | sed 替换为 `rdpa_fc_mcast_flow_key_t`（编译器建议） |
| `bcm_log.h:73 fatal: linux/bcm_colors.h` | 该头在 `bcmkernel/include/uapi/linux/`，INC 缺 `uapi` 一级 | INC 加 `-I$SDK/kernel/bcmkernel/include/uapi` |

**阶段 2.6 修复（commit `f639c35`，run #23 验证中）**

> 说明：错误数从 2 变 5 不是退步——修掉表层的 2 个后，编译继续往 headers 深处走，暴露了 next layer。这是正常的收敛过程。

## 十、累计修复清单（探针侧）
1. 内核目录/工具链路径（绝对路径）
2. `EXTRA_CFLAGS` → `KCFLAGS` + `STAGING_DIR`
3. `-Wno-error=...` 抑制 A 类警告
4. `outer_*` L2 cache stub
5. bdmf include 路径（framework/system/system/linux/cm）
6. `nbuff_types_stub.h`（NULL_STMT/wlFlowInf_t/RecycleFuncP）
7. sed 去 `skb->queue` accessor、`switchdev_ops`
8. rdpa autogen 35 头
9. bcmkernel `uapi` 路径 + sed rdpa_iptv 类型

## 十一、阶段 2.7（run #23）：5 → 29，**突破进入 enet.c 本体**
> 这是关键里程碑：**头文件层全部通过**，编译器第一次真正编译 `enet.c` 的代码体，暴露 29 个 `.c` 层错误。

**29 个错误分三类**：
| 类别 | 明细 | 处理 |
|---|---|---|
| 警告被 -Werror（约 11 处） | `discarded-qualifiers`×4、`missing-prototypes`×4、`old-style-declaration`×1、`unused-variable`×2 | 加 `-Wno-error=` 即可 |
| 缺 include（约 10 处） | `fwdcb_t`/`enet_fwdcb_register`/`bcm_netdev_priv_info_*`/`FKB_FRM_GSO` → `bcm_netdevice.h`；`SKB_NO_RECYCLE` → `bcm_skbuff.h`；`PKT_DROP` → `blog.h` | sed enet.c 补 include |
| 真代码差异（约 8 处） | `skb->recycle_flags`×2（Broadcom patch 的 sk_buff 扩展，6.18 无）；`dev_set_mac_address` 少参；`switchdev_ops`（enet.c:2150） | sed 注释 / 待适配 |

**阶段 2.8 修复（commit `aab2350`，run #24 验证中）**：
- `-Wno-error=` 补 `discarded-qualifiers`/`missing-prototypes`/`old-style-declaration`/`unused-variable`/`override-init`/`address`
- sed enet.c：`#include <bcmnet.h>` 后插 `bcm_skbuff.h`/`blog.h`/`bcm_netdevice.h`；`CONFIG_NET_SWITCHDEV`→`#if 0`；注释 `skb->recycle_flags` 两行

**预期**：29 → 个位数（剩余主要是 `dev_set_mac_address` 签名等少量真差异）。

## 十二、阶段 2.9（run #24）：29 → 15，**撞上架构级障碍**
错误降到 15，但暴露出比 API 差异严重得多的问题：

| 错误 | 真相 |
|---|---|
| `bcm_skbuff.h:262 redefinition of 'struct sk_buff'` | **bcm_skbuff.h 定义了完整的 `struct sk_buff`**（4.1 结构 + Broadcom 扩展 `recycle_flags`/`wl`），与 6.18 内核的 sk_buff **直接冲突** |
| `bcm_skbuff.h:405/429 CLONED_OFFSET/PKT_TYPE_OFFSET redefined` | 同上，Broadcom 重定义了内核的偏移宏 |
| `bcm_netdevice.h:121-123 'struct net_device' has no member 'bcm_nd_ext'`（多处） | **bcm_netdevice.h 依赖 `net_device->bcm_nd_ext`**——Broadcom 给 net_device 加了扩展成员，6.18 没有 |
| `bcm_skbuff.h:141 conflicting types for 'wlFlowInf_t'` | 与我加的 stub 冲突（bcm_skbuff.h 定义的是 union） |
| `enet.c:515 conflicting types for 'skb_header_alloc'` | 6.18 有同名函数，签名不同 |
| `enet.c:544 skb->recycle_flags` | sed 只删了 540/541，544 还有 |

**核心结论**：
> Broadcom 的 enet 驱动**深度依赖 bcmkernel patch**——它不是在标准内核 API 上写驱动，而是**改造了内核的 `sk_buff` 和 `net_device` 结构体**（加 `recycle_flags`、`bcm_nd_ext` 等）。这些扩展无法通过 include/`-Wno-error`/sed 绕过。

**两条真正的路**：
1. **移植 bcmkernel patch 到 6.18**：从 asuswrt `kernel/linux-4.19/` 提取对 `skbuff.h`/`netdevice.h` 的改动，适配 6.18 打进去。工作量：中-大（要处理 4.1→6.18 的结构差异），但路径明确。
2. **改写 enet 驱动避开扩展**：把 `skb->recycle_flags`、`net_device->bcm_nd_ext` 等全部换成标准 API（如 `skb->cb`、netdev_priv）。工作量：大，且可能影响 Broadcom 的包转发逻辑（blog/pktfwd）。

**注意**：之前所有 sed 修复（加 include bcm_skbuff/bcm_netdevice）方向是错的——这两个头会覆盖内核核心结构，不能 include。需回退。

## 十三、阶段进度更新
- 头文件层：已通（但绕道 include bcmkernel 头是错的，需改）
- 代码层：15 个错误，其中 **5+ 个源于 bcmkernel 内核 patch 缺失**（sk_buff/net_device 扩展）
- 结论：**编译适配的真实难度 = 移植 Broadcom 内核 patch**，不再是"改几行"





