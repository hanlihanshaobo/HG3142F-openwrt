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

## 八、编译探针进展（2026-10-29，run 51→55）

| run | sha | 结果 |
|---|---|---|
| #51 | `d5bfecd` | 8 error（switchdev / PHY_OP_* / EXT_SWITCH_TYPE） |
| #52 | `db0b6aa` | 继续暴露更深层错误 |
| #53 | `aef2f4e` | 仍停在编译，无链接（此前"15 个 undefined"记录有误） |
| #54 | `643104a` | 3 个真实阻塞：runner_common switchdev 原型 / `<stdarg.h>` / `drivers/bcm_chip_arch.c` |
| #55 | `a6498a6` | 13 个 .o 中 11 个编过；仅 `runner.o`（`struct ethswctl_data` 未定义）与 `dt_parsing.o`（6.18 的 `of_platform.h` 不再带 `platform_device.h`）失败；**仍无 .ko** |
| #56 | `eda9b74` | 14 个 .o 中 13 个编过；只剩 `runner.o`：`BDMF_MATTR` 展开成 VLA，被 `-Werror=vla-larger-than` 拦下（2 处）；**仍无 .ko** |

run #55 追加结论：
- 我们新加的 `phy_drv_compat.o` / `enet_framework_stubs.o` / `bcm_chip_arch_compat.o` 全部编译通过；
- 未定义符号清单要等首次链接成功才有（modpost 在编译全过之后才跑）；
- prebuilt `phy_drv.o`/`phy_drv_brcm.o`/`mac_drv_unimac.o`/`unimac_drv_impl1.o` 均为 **aarch64**，armv7 不可用 → unimac MAC 驱动必须自写或换 32 位源码来源。

对应修复（本次提交）：
1. `dt_parsing.c` 显式 `#include <linux/platform_device.h>`；
2. `runner.c` 显式 `#include <bcm/bcmswapitypes.h>`（`struct ethswctl_data` 的来源；
   `runner.h` 只在 `CONFIG_BCM96855` 等芯片宏下 include 它）；
3. 新增 `compat/bcmkernel_stubs.c`：`kerSysGetMacAddress`（本地管理 MAC 占位）、
   `kerSysReleaseMacAddress`、DyingGasp 注册/注销（no-op）。

run #56 追加修复：`BDMF_MATTR()` 用函数返回值当数组长度 → VLA，GCC 的
`-Wvla-larger-than` 在 6.18 的 `-Werror` 下变成错误。加
`-Wno-error=vla-larger-than -Wno-error=vla`（运行时长度由 attr 数决定，很小）。

> 更正：GCC 14 不接受 `-Wno-error=vla-larger-than`（参数化警告要求 `=value`），
> 会直接报 `no option '-Wvla-larger-than'`，run #57 因此全部 .o 未产出。
> 最终用 `-Wno-vla-larger-than`（关闭整个警告）。

## 九、模块快速通道（module_only）

完整构建约 35-40 分钟，其中 90% 与 bcm_enet 无关。为此给 workflow 加：

1. **内核构建树缓存**（`openwrt-kernel-<lock 哈希>-<bcmbca overlay 哈希>`）
   - `build_dir/target-*/linux-*/`（内核树、.config、Module.symvers、已编对象）
   - `staging_dir/target-*/`
   - 首次保存 1,505 MB；只在完整构建成功或自愈构建成功时保存
   - 顺带修掉 dl/tc 缓存每次重复写一份的问题（save 前判断 cache-hit），
     历史重复项清理后仓库缓存 6.9 GB → 1.4 GB
2. **`workflow_dispatch` 新输入 `module_only`（默认 false）**
   - 跳过完整 make、镜像导出、发布与三个探针
   - `make package/bcm-enet/{clean,compile} V=s`（先精确选中
     `CONFIG_PACKAGE_kmod-bcm-enet=m`；之前用 grep 子串判断会命中
     `# ... is not set` 注释，导致 0.03 秒空操作）
   - 缓存未命中时自动跑一次完整构建并保存缓存（自愈）
   - 产物：`bcm_enet.ko`、`undefined_symbols.txt`（已剔除内核导出符号）、
     `undefined_symbols_raw.txt`、`module_build.log`、`timing.txt`
   - artifact 名：`bcm-enet-module-<run>`

### 实测（2026-10-29）

| run | 模式 | 结果 | 耗时 |
|---|---|---|---|
| #57 | full | 失败：GCC 不接受 `-Wno-error=vla-larger-than` | — |
| #58 | full | 成功，内核缓存 1,505 MB 写入 | ~39 min |
| #59 | module-only | 失败：`./scripts/config` 是目录 | 8 min |
| #60 | module-only | 失败：包构建缺 rdpa_ag_iptv.h 的 sed | 236 s |
| #61 | module-only | 失败：modpost 把未决符号当错误 | 208 s |
| #62 | module-only | **成功，首次产出 bcm_enet.ko（503,420 B）** | 263 s |
| #63 | module-only | 成功，输出过滤后的未决清单 | 237 s |

module-only 各阶段（#63）：feeds 133s / configure 152s / kernel_config 174s /
module build 开始 189s / 编译+链接完成 237s。即内核树缓存命中后，**约 4 分钟
拿到 .ko**（目标 3-6 分钟）。

### 首次链接结果

`KBUILD_MODPOST_WARN=1` 下 modpost 只告警，`.ko` 能产出，但 **有 46 个内核
未导出的符号**（insmod 会被拒），按类：
- `rdpa_*` 11 个（cpu/port/filter/system/egress_tm）
- `bdmf_*` 11 个（attrelem/link/put/new_and_set/sysb_*/type_num_attrs）
- `unimac/mac`：`mac_dev_del`、`mac_dev_rgmii_attach`
- Runner 端口/队列：`runner_get_pkt_from_ring`、`runner_ring_create_delete`、
  `_demux_id_runner_port`、`link_pbit_tc_to_q_to_rdpa_lan_port` 等
- PHY/DT：`crossbar_phy_dev_active`、`dt_get_phy_mode_mii_type`、
  `gpiod_get_from_of_node`、`bcmbca_pinctrl_get_pins_by_state`
- 数据/其他：`unit_port_array`、`unit_port_oam_idx_array`、`eee_enabled`、
  `extsw_reg_config`、`nbuff_flushfree`、`nbuff_free`、`skb_header_alloc` 等

下一步按这份清单补 fail-closed 桩（返回错误/NULL，避免假成功），再 insmod。

### 补桩结果（run #64）

新增 `compat/enet_symbol_stubs.c`，46 个符号全部有实现/占位：
- **真实现 3 个**：`dt_get_phy_mode_mii_type`（解析 DT `phy-mode`）、
  `port_by_netdev`（遍历 `root_sw`）、`skb_header_alloc`（`alloc_skb`）
- **数据 4 个**：`unit_port_array`、`unit_port_oam_idx_array`（-1）、
  `eee_enabled=0`、`extsw_reg_config`（空自旋锁）
- **fail-closed 39 个**：rdpa 查询返回 -1 且不写输出参数、`rdpa_*_drv` 返回 NULL；
  bdmf attrelem/new/link/destroy 返回 -1；mac/runner/PHY 入口返回 -1/NULL；
  回收与中断类 void no-op（会泄漏，仅探测期）

结果（module-only #64，212 秒）：
```
bcm_enet.ko            531,848 B
undefined_symbols.txt  0 行   ← 已无内核未导出符号
undefined_symbols_raw  96 行  ← 全是 vmlinux 导出（kmalloc/printk/of_* 等）
```
即 **insmod 不会再因 Unknown symbol 被拒**，可以进入设备侧 probe 验证。
预期 probe 会在第一个 `rdpa_system_get()`/-1 处停下（fail-closed 设计）。



