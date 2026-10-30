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

## 十、首次设备 insmod（2026-10-29）

- **基线**：uptime 1497s；唯一网口 `eth0`（RTL8125B，r8169）；`/proc/device-tree`
  只有 pcie/nand/uart，**没有** switch0/mdio/enet 节点；无 pstore。
- **第 1 次**（run #64 的 .ko，531,848 B，md5 两端一致）：
  `insmod` 后设备直接 panic，SSH 断开；看门狗约 70 秒重启，重连成功
  （uptime 92s），`lsmod` 已无 bcm_enet，eth0 正常。无 pstore 可取证。
- **根因**：`enet.c:has_dt()` 找不到 `brcm,enet` 节点返回 0 →
  `bcm_enet_init()` 走 `!has_dt()` 分支直接 `bcm_enet_init_post()`；
  此时 DT 解析没跑，`root_sw` 仍是 NULL → `sw_init(NULL)` 空指针。
- **修复**（commit `6fd600b`）：无节点时
  `enet_err("no brcm,enet DT node; refuse init"); return -ENODEV;`
  不再调用 `init_post`。
- **第 2 次**（run #65 的 .ko，531,928 B）：`insmod` 返回 255，
  `dmesg` 出现 `bcm_enet_init:L2479 no brcm,enet DT node; refuse init`；
  **无 panic**，设备稳定，模块未加载（insmod 失败后自动回滚）。
- **结论**：DTS 里的 `switch0`（compatible `brcm,enet`）是硬前提 ——
  没有它 `has_dt()` 永远为 0。下一步按 `orig_968552CREF_CM_XG.dts` 给板级
  DTS 补 switch0 + mdio + egphy/sgmii 节点，再验证 probe 停点。
- 日志：`test_images/insmod_baseline.txt`、`insmod_run1.txt`、
  `insmod_run2.txt`、`insmod_run2b.txt`

## 十一、板级 DTS 补 switch0 + module-only 出 DTB（2026-10-29 晚）

- **DTS 新增**（均挂在根节点下）：
  - `unimac3`（brcm,unimac3，双 reg 窗口 0x828a8000/0x828b0000）
  - `egphy`（0x828ba204）、`sgmii`（0x828ba280）、`mdio1`（0xff802060）
  - `mdio`（simple-bus + `bus-type="MDIO_V1"`）：`ethernet-phy@1..4` = EGPHY，
    `ethernet-phy@6` = SGMII
  - `switch0`（`brcm,enet` / `sw-type="RUNNER_SW"` / `label="bcmsw"`）：
    `ports/port@0..3` = gmii lan0-3，`port@5` = hsgmii lan4；
    `port@6`（PON/XFI WAN）先 `disabled`（PON PHY 要到 init_post 才注册）
  - 节点名加 `bcm_` 前缀避免与上游 `bcm6855.dtsi` 冲突；上游确认无 enet/mdio 节点
- **DTB 构建**：module-only 里加了一步。第一版用 `make target/linux/compile`
  会触发整棵内核重编（run #67：20 分钟且没产出 board dtb，失败）；改成
  `gcc -E` + `$KDIR/scripts/dtc/dtc` 直接编单个 DTS，**1 秒**完成。
- **run #68**：module-only **239 秒（3m59s）**，artifact `bcm-enet-module-68`：
  `bcm_enet.ko` 531,928 B + `bcm96855-mb4784w.dtb` 8,432 B（含 switch0/ports/5 phy）
- **FIT（未刷机）**：`test_images/bootfs1-dts-switch.fit`
  - 输入：原厂 `mtd7_bootfs1.bin` + `v47-zImage.bin` + run #68 的 dtb
  - `default = conf_lx_968552CREF_CM_XG`（fit_setdefault 设置）
  - kernel sha256 `d69da356…`、fdt sha256 `0e74ae28…`，均与 `hash-1` 一致
  - 内嵌 FDT 可解析到 `switch0`、`ports/port@0..5`、`mdio/ethernet-phy@*`
  - 大小 8,564,358 B；md5 `8d6ea0f075e2431ef676655e35fc9203`；
    sha256 `f27d75ff972732cae1c5f4b69f1e4a9c99b3120ffceb72e9f748b75578171a9a`
- **下一步**：等设备窗口，把该 FIT 写到 bootfs1 后启动；insmod 预期停在
  `Failed to create mac for port 0`（unimac 源码缺失），再决定自写 MAC 驱动。

## 十二、真机刷写 + insmod（2026-10-29 20:00-20:40）

- **刷写**：`ubiupdatevol /dev/ubi0_3` 写入 FIT，回读 sha256 一致；
  U-Boot 日志 `Read 8564358 bytes from volume bootfs1`、
  `Using 'conf_lx_968552CREF_CM_XG'`、kernel/fdt hash `sha256+ OK`、
  `Starting kernel`，无 panic，SSH 正常。
- **但 switch0 没生效**：`/sys/firmware/fdt` 的 sha256 等于 `v47-zImage.bin`
  尾部 appended DTB（`993d57c2…`）。ARM zImage 自带的 DTB 覆盖了 FIT fdt。
- **修复**：module-only 顺手导出内核树里未 append 的
  `arch/arm/boot/zImage`（3,662,456 B，比 v47 少 6,449 B = 原 appended DTB）。
  重新打包 `test_images/bootfs1-dts-switch2.fit`（8,557,906 B，md5
  `988a04533b906b6e1b231a3a39b8920f`，sha256 `bce2dc9d…`）再刷。
- **第二次启动成功**：U-Boot 用 `conf_lx_…`、fdt hash OK；
  起来后 `/proc/device-tree/switch0` 存在（compatible/label/ports/sw-type），
  `/proc/device-tree/mdio/ethernet-phy@1..4,6` 齐全。
- **insmod（run #69 的 .ko，无 RUNNER）**：加载成功但 probe 停在
  ```
  bcmenet DT
  _assign_port_class:L185 failed to create port type 0 port_class -1
  of_sw_probe:L341 failed to create switch 0 (RUNNER_SW)
  of_bcmenet switch0: probe with driver of_bcmenet failed with error -22
  ```
  根因：`port_types.c` 的 `case PORT_TYPE_RUNNER_SW` 在 `#ifdef RUNNER` 内。
- **修复**：加 `-DRUNNER`。run #70 出 .ko（541,028 B）但缺
  `port_runner_sw` / `port_runner_port`（在 runner_with_switch.c）。
  run #71 直接编该文件失败（缺 `rdpa_cpu_tx_extra_info_t`、`sf2_sw`、
  `port_runner_sw_config_trunk` 等）。run #72 改为在 compat 里给最小 ops 表
  （`.init` 打印错误并返回 -1），.ko 542,136 B、未决 0。
- **run #72 的 .ko 仍 panic**：insmod 后 SSH 断开，看门狗重启（uptime 99s），
  模块未加载；本轮未开串口抓取，panic 原文待下一轮定位。
- **当前设备状态**：vol3 = `bootfs1-dts-switch2.fit`，正常启动、switch0 在 DT 中；
  模块不自动加载，不影响正常使用。未回滚。
- **下一步**：用串口抓 run #72 .ko 的 panic 栈，定位是 sw_init 的端口遍历
  还是 port_init 的 netdev 注册；再决定补 ops 表字段还是继续 fail-closed。

### run #72 panic 定位与修复（同日 20:50）

串口日志 `test_images/insmod_panic_20261029_205022.log`：

```
Unable to handle kernel NULL pointer dereference at virtual address 00000000 when execute
PC is at 0x0
LR is at port_type_resolve+0x3c/0xbc [bcm_enet]
Call trace:
 port_type_resolve <- port_create+0xe8/0x188
 port_create <- of_port_probe+0x108/0x644
 of_port_probe <- of_sw_probe+0x108/0x1b0
 of_sw_probe <- enet_probe+0x38/0xac
 enet_probe <- platform_probe
 driver_register <- init_module
```

定位到 `port.c:646`：`parent_sw->s.ops->port_id_on_sw(...)` 为空 ——
run #72 的最小 `sw_ops_t` 只填了 `.init`。

修复（commit `fa3e97a`）：补 `runner_sw_port_id_on_sw()`，
`*port_id = port_info->port; *port_type = PORT_TYPE_RUNNER_PORT;`。

**run #73 结果（里程碑）**：`.ko` 543,364 B、未决 0；insmod 后：

```
bcmenet DT
bcm-phy: EGPHY addr 1 from /mdio/ethernet-phy@1
bcm_enet: no MAC driver for port 0 (/switch0/ports/port@0), unimac source missing
of_port_probe:L280 Failed to create mac for port 0
of_bcmenet switch0: probe with driver of_bcmenet failed with error -22
```

**probe 走到了端口 0 的 MAC 创建**（PHY 框架、DT 解析、switch 创建全部正常），
干净失败无 panic，与预期停点完全一致。`rmmod` 成功，但 `phy_dev_del()`
对从未 INIT_WORK 的 work 调 cancel，触发两条 `__flush_work` WARN；
已加 `work.func` 判空（待下一轮验证）。

- DTS appended-DTB 的坑与检查清单：`port/docs/DTB-APPENDED-NOTE.md`
- run #74：cancel 判空修复构建成功（bcm-enet-module-74，.ko 543,4xx B、未决 0），
  未再上设备验证（改动仅消除 rmmod WARN）。
- 下一步：自写最小 unimac MAC 驱动（或找 32 位源码），替换
  `mac_dev_dt_probe` 的 fail-closed 桩，才能真正建出 lan0..lan4 netdev。






## 2026-10-29：最小 unimac MAC 驱动 → lan0..lan4 建成（run #76-#78）

**源码搜索**：asuswrt-sparse 与 SDK 树都没有 `mac_drv_unimac.c` /
`unimac_drv_impl1.c`，只有 aarch64 的 `.o`（`mac_drv_unimac.o`、
`unimac_drv_impl1.o`），armv7 链接不了。因此按 `mac_drv.h` 的 ops 契约手写
最小实现 `compat/unimac_mac.c`：init/enable/disable/mtu_set/eee_set/
stats_*/dev_add/dev_del 全部返回 0，不碰寄存器、不做 DMA；`mac_dev_dt_probe`
用 `kzalloc` 造 `mac_dev_t` 并挂到 `mac_drv_unimac`。

**run #76（首个带 MAC 的 .ko）**：insmod 直接 panic，但栈不在 probe：

```
Error: Driver 'of_bcmenet' is already registered, aborting...
bcm_enet_init:L2489 Failed to inititialize, exiting
Unable to handle kernel NULL pointer dereference at virtual address 0000002c
PC is at bcm_enet_exit+0x14/0x74 [bcm_enet]
```

根因：`of_platform_enet_driver` 注册后从不注销，run #73/#74 的 rmmod 把
driver 留在 platform bus 上 → 本次 `platform_driver_register()` 返回
-EBUSY → `bcm_enet_init` 错误路径调 `bcm_enet_exit()`，`root_sw==NULL` 时
`if (root_sw->dev)` 读 0x2c 崩溃。

**修复（#77）**：driver 注册状态记账 + `unregister_enet_platform_device()`；
module_exit / 初始化失败路径成对注销；`bcm_enet_exit` 对 root_sw、
enetx_channels 判空且可重入；初始化失败不再 BUG()；sw_ops/port_ops 补
`.uninit`；port init 按 DT port-cap 设置 LAN/WAN 角色；补 `.mtu_set`。

**run #77 设备结果**：不再 panic，probe 走到最后一个卡口：

```
_create_rdpa_queues:L493 No RDPA RXQ configuration compiled-in
enetxapi_queues_init:L545 failed to configure RDPA CPU queues
bcm_enet_init_post:L2440 Failed to inititialize, exiting (rc=-1089392496)
```

`rc` 是未初始化栈变量：本板没有 RDPA 队列配置，`_create_rdpa_queues()`
返回 NULL 后直接 `goto exit`，`rc` 从未赋值。端口/netdev 已在 sw_init 里
建好，又被 bcm_enet_exit 拆掉。rmmod 干净。

**修复（#78）**：runner.c 的 `enetxapi_queues_init()` 在 `chan==NULL` 时
直接返回 0（数据面仍 fail-closed）。

**run #78 设备结果**（`.ko` 563,912 B、未决 0）：

```
enetxapi_queues_init:L507 compat: no RDPA CPU queue, RX disabled
```

`ls /sys/class/net` → `bcmsw lan0 lan1 lan2 lan3 lan4`（全部 DOWN），
`ip -br link` 显示 mtu 1500 / qdisc noop / state DOWN。insmod → rmmod →
再 insmod 两轮均成功，无 -EBUSY、无 WARN、无 panic；rmmod 后设备只剩
eth0/br-lan/lo。

**当前能力边界**：只有 netdev 对象；ndo_open/xmit/RX/DMA/中断仍是原厂路径
+ fail-closed 桩，没有真实收发。下一步：DMA 环 + unimac 寄存器初始化 +
中断，再接 runner/RDPA 收发。



## 2026-10-29：Spike A/B（link-up 可行性 + 交换机初始化评估）

**Spike A：link-up（成功）**

实现（commit c83fa06 / 2b8ee64 / eb98e06）：
- `compat/phy_drv_compat.c`：新增 MDIO_V1 C22 总线。base 从 DTS
  `brcm,mdio1`（0xff802060）解析，CMD/CFG 布局取自 SDK
  `mdio_drv_common.c`；cfg 只置 clause-22 + free-run，分频保留
  bootloader 的值。
- `port_runner_port` 增加 `.open/.stop`：`ndo_open` 时立刻读一次 PHY
  并触发 `phy_link_change_cb`（负责 `netif_carrier_on/off`），之后由
  200ms 轮询继续跟踪。
- `enet_set_mac_addr()` 改用 `eth_hw_addr_set()`，消掉 open 时
  6.18 的 `Incorrect netdev->dev_addr` WARN。

设备结果（run #79 / #81，同一启动会话）：

```
bcm-phy: MDIO_V1 at 0xff802060 cfg=0x000020c1, C22 polling
bcm-phy: port_0:0 mdio=0 id=ae02:5371 bmsr=79c9 bmcr=1940   (port 0..3 相同)
bcm-phy: port_0:5 mdio=0 id=0143:bff0 bmsr=0109 bmcr=0940   (SGMII)
```

- PHY ID `ae02:5371`（Broadcom OUI）可读 → **MDIO C22 读写通路成立**。
- BMSR=0x79c9 → bit2(link)=0；5 个口都没有插网线。
- `ip link set lan0 up` → `<NO-CARRIER,BROADCAST,MULTICAST,UP>`，
  carrier=0 / operstate=down（5 口一致），与 link 位一致。
- 无 panic；rmmod 干净。

遗留（纯 cosmetic，未再上设备）：`enet_dev_mac_set(p, 0)` 里
`memset(dev->dev_addr, 0, ETH_ALEN)` 绕过 addr_shadow，rmmod 时
`free_netdev()` 会报 5 条 `Incorrect netdev->dev_addr`。改成
`eth_hw_addr_set(dev, zero_addr)` 即可。

复测方法：插 LAN1 网线 → `ip link set lan1 up`，预期 `bmsr` bit2=1、
carrier=1。

**Spike B：交换机最小初始化 —— 不可做（时间盒内）**

证据：
- `runner.c`：354 处 `rdpa_*`、119 处 `bdmf_*`、**0 处直接寄存器访问**。
- `port_runner_sw_init()` 只是
  `create_rdpa_port(rdpa_if_switch, ...)` → `bdmf_new_and_set()`；
  端口 SAL/DAL、miss action、egress TM、VLAN/转发全由闭源 rdpa 按
  BDMF 属性配置。
- 整个 GPL 包里没有 RDP 寄存器定义（搜 `RDP_*` 只有 `RDPA_*` 宏）。
- `sf2.c` / `sw_common.c` 是**外部 SF2 交换机**路径
  （`#ifdef SF2_DEVICE`，`SF2SW_RREG/WREG = extsw_rreg_wrap`），
  与内部 Runner 无关，本 build 也未编译。
- 结论：要做 dumb-switch 必须自己写 RDP 的 port-map / VLAN / ARL /
  IMP 寄存器序列，无文档、无头文件，估计 ≥ 数周逆向且高风险，
  不是 3 轮 module-only 能覆盖的。
- 建议：数据面停在 netdev 里程碑；现实路径只有拿到 RDPA/RDP 源码，
  或与当前内核匹配的 armv7 `rdpa.ko`（目前都没有）。

构建：run #79 成功（571,204 B）、#80 编译失败
（`MII_PHYSIDR1` 应为 `MII_PHYSID1`）、#81 成功（572,088 B，未决 0）。



## 2026-10-29：按逆向结果接入真实 unimac MAC 初始化（run #82）

依据 `docs/REVERSE-ENET.md` 路线 B（§9.2）：只做 MAC + PHY，不动数据面。

实现（commit 719d5d4）：
- `compat/unimac_mac.c` 从 0 寄存器占位改成真实读写：
  · ioremap 实例 `0x828a8000 + 0x1000*n` 与 MISC `0x828b0000 + 0x400*n`（§3.1）
  · init = CMD `0x008000d8 | bit24`（接交换机）、FRM_LEN=`0xffff`、
    RXERR_MASK|=bit5、清 bit22 解速率锁、TX_IPG[13:8]=0x40（§4.2/§4.3）
  · enable/disable 用 CMD bit0/1（§4.3、§8 item7）
  · cfg_set = set_speed/set_duplex（bit13 sw_reset 包夹 + IPG 表，§4.4）
  · pause_set = CMD[18:17]/[8]/[28] + MISC+0x08 + 0x330 + pause SA（§4.6/§4.7）
  · mtu_set = FRM_LEN/MIB_MAX_PKT；eee_set = 0x64 bit3
  · `mac_dev_del` 先调 `.dev_del`（iounmap）再 kfree
- 数据面仍 fail-closed：unimac 无 DMA/中断（§5.1/§6），TX/RX 走原厂路径
  最终由 RDPA 桩丢弃。

设备结果（同一个启动会话，run #82，`.ko` 586,000 B、未决 0）：

```
unimac-compat: mac 0 mapped 0x828a8000/0x828b0000
...
unimac-compat: mac 5 mapped 0x828ad000/0x828b1400
unimac-compat: mac 0 rev=0x00000100 cmd=0x018000d8 (base=0x828a8000)
...
unimac-compat: mac 5 rev=0x00000100 cmd=0x018000d8 (base=0x828ad000)
bcm-phy: port_0:0 mdio=0 id=ae02:5371 bmsr=79c9 bmcr=1940
```

- `rev=0x100` 非 0/全 F → MAC 块已上电可访问（§4.9 的复位前提成立）。
- `cmd=0x018000d8` = §4.2 的 `0x008000d8 | (1<<24)`，读回与写入一致。
- 5 个实例全部初始化；`lan0 up` → `<NO-CARRIER,...,UP>`，BMSR bit2=0
  （未插网线），carrier=0；无 panic，rmmod 干净。
- 遗留：`enet_dev_mac_set(p,0)` 的 memset 仍会触发 rmmod 时 5 条
  dev_addr WARN（cosmetic，修法见上一节）。

下一步（仍属路线 B）：插 LAN1 网线复测 carrier=1 + BMSR bit2=1，
再把 set_speed/set_duplex 的 reset 时序按 §4.4 伪 C 复核一遍
（当前用 §4.10 参考实现：置 reset → 写速率 → 清 reset）。



## 2026-10-29：PHY Power Down 修复 → lan2 carrier=1（run #83/#84）

**现场现象**（run #82，LAN 口已插线）：4 个 EGPHY 都是
`BMCR=0x1940`（bit11 POWER DOWN=1）、`BMSR=0x79c9`（bit2/bit5=0），
`lan0..lan4` 全 `NO-CARRIER`。

**根因（两层）**：
1. compat 的 EGPHY/SGMII 驱动没有 `.power_set`，`phy_dev_power_set()`
   是空操作（厂商 EGPHY 的 `.power_set = mii_power_set`）。
2. `port_open()` 分派到我们的 `.open` 桩，覆盖了
   `port_generic_open()` 里的 `phy_dev_power_set(phy, 1)`。

**修复（commit 1202456）**：
- `phy_drv_compat.c` 增加 `phy_compat_power_set()`：上电清 BMCR bit11
  并置 ANRESTART，掉电置 bit11。
- `runner_port_open_stub()` 在读状态前显式 `phy_dev_power_set(phy, 1)`。

**run #83 回归**：PHY 上电、carrier 起来后设备重启。根因是
`enet.c:1196` 直接调用 `egress_port->p.ops->dispatch_pkt()`，而
`port_runner_port` 没实现该 op → NULL 调用。已在
`enet_symbol_stubs.c` 补 `runner_port_dispatch_pkt_stub`（释放 skb +
返回 -1，与厂商 `runner_with_switch.c:185-189` 一致）。

**run #84 设备结果**（`.ko` 594,352 B、未决 0）：

```
bcm-phy: port_0:2 mdio=0 id=ae02:5371 bmsr=79c9 bmcr=1140
lan2: <BROADCAST,MULTICAST,UP,LOWER_UP> ... state UP
lan2 carrier=1 / operstate=up
```

- BMCR：`0x1940 → 0x1140`，bit11(POWER DOWN) 已清零（bit9 ANRESTART
  已自行释放）。
- BMSR 在 open 瞬间仍是 0x79c9（刚重启 AN）；200ms 轮询在 ~2-4s 后
  看到 link，lan2 变成 `LOWER_UP`、carrier=1（DT port@2 / PHY addr 3）。
- lan0/1/3/4 无网线仍为 NO-CARRIER；insmod/rmmod 正常，无 panic。
- 备注：当前 `power_set(1)` 每次 open 都会重启 AN，`ifdown/ifup` 会有
  2-3s 链路抖动；后续可改成「仅当 BMCR bit11 原为 1 时才 ANRESTART」。



## 2026-10-29：C1 —— RDP 块只读探测（run #85）

可观测通道：串口 COM7（CH340）已恢复可用，C1 日志
`test_images/c1_probe_20261029_234003.log`；netconsole 不可用
（6.18.55 未编 netconsole，且无 `/dev/mem`/`devmem`）。

实现：`compat/rdp_probe.c`（`rdp_probe=1` 才执行，只有 `readl`，无
`writel`），从 `mac_drivers_set()` 最前面调用；run #85 `.ko` 608,624 B、
未决 0；insmod/rmmod 正常，无 panic。

关键读数：
- `UBUS_MSTR 0x8289c000[+4]=deadbeef`、`UBUS_SLV 0x828a0000[+0]=deadbeef`、
  `PSRAM 0x828a2800` 的 `1a2b3c4d/98765432` → §7.1 块地址表现场成立。
- `FPM 0x82a00000` 全 0、`DQM 0x82c80034` 为 `0xccccdddd` → 未初始化
  （rdpa.ko 没加载），与 §7.4 的依赖顺序吻合，C2 要补。
- UNIMAC 6 个实例 `rev=0x100`，基址与 §3.1 逐项一致；MISC `0x828b0000 +
  0x400*n` 读回 `00000000/00e03fff`。lan2 的 CMD 为 `0x118601d8`
  （link up 后 cfg/pause 已下发），其余为 `0x018004d8`。

产出：`docs/C-PLAN.md`（C1 表 + C2~C4 分阶段计划，含串口前置条件）。



## 2026-10-30：C2-a —— FPM 池 + QM 全局配置写读回（run #86/#87）

实现 `compat/rdp_c2.c`（`rdp_c2=1`，默认关）：256 KiB `alloc_pages_exact`
池；写 QM+0x034/0x038（FPM base/coherent）、0x03c（DDR_SOP=0|88<<16）、
0x280/0x284/0x288（UG0 阈值 64/96/112）、0x200（POOLS_THR0=64）。
每次写后读回，不一致立即恢复并停止；rmmod 经 `phy_drv_compat_exit()`
恢复全部寄存器并释放池；不写 QM_ENABLE/FPM_CTRL，不建 ring。

**run #86**：前 3 个 OK，`0x200` 写 0x80 读回 0 -> 停止。根因：该寄存器
是 7 bit 字段（AG getter 掩 0x7f、setter 校验最高位），128 越界。

**run #87**（`.ko` 625,808 B、未决 0）：7/7 写读回一致：

```
FPM_BASE     0x034 old=0x03580000 wrote=0x03600000 OK
FPM_COHERENT 0x038 old=0x03580000 wrote=0x03600000 OK
DDR_SOP      0x03c old=0x00580000 wrote=0x00580000 OK
UG0_LOWER    0x280 old=0x0000ff79 wrote=0x00000040 OK
UG0_MID      0x284 old=0x0000ff79 wrote=0x00000060 OK
UG0_HIGHER   0x288 old=0x0000ffff wrote=0x00000070 OK
FPM_POOLS_THR0 0x200 old=0x00000808 wrote=0x00000040 OK
```

- `lan2` up 后 `carrier=1 / operstate=up`，link 未受影响；
- `rmmod` 7 个寄存器 restore 成功、池释放；无 Oops、无 panic、uptime 连续。
- 备注：`old=0x03580000` 是 #86 遗留值（首次失败时 restore 未生效，已修）；
  重启后回到硬件默认。

**C2-a 通过，可进入 C2-b（FPM 使能 + ring/描述符）。**



## 2026-10-30：C2-b —— FPM 使能 + RX buffer（run #89）

`compat/rdp_c2.c` 扩展（仍 `rdp_c2=1`）：C2-a 的 7 个寄存器之后，
1) `QM+0x00c` bit0 置 1 使能 FPM（`0x00100188 → 0x00100189`，读回一致）；
2) 8 个 RX buffer（2 KiB）按 §5.3 格式 1 建描述符（`bswap32(buf)|0x40000`、
   偏移大端），8/8 回读校验通过；
3) 8 项 CPU ring 描述符（16 B，物理地址大端）在本地内存构建但**未写硬件**；
4) 0x82803000 只读探测得 `0101f300 ...`，与 RNR_REGS 同款 → 不是 ring 表。

设备：`lan2` carrier=1 保持；无 panic（uptime 1866→1885s）；rmmod 后
FPM_CTRL 恢复 `0x00100188`、7 个寄存器恢复、三块内存释放。
`.ko` 631,788 B、未决 0。

**遗留**：CPU ring 硬件表基址 = `0x3000 + rdp_runner_core_addr[0]`，
后者是 rdpa.ko 运行时 .bss；现场 0x82803000 不像表，C3 前需先定位。



## 2026-10-30：C3 ring 表落地（run #90）

分支 6 给出 ring 表基址 `0x82703000`（`rdp_runner_core_addr[0]=0x82700000`
+ `RDD_CPU_RING_DESCRIPTORS_TABLE_ADDRESS_ARR[0]=0x3000`）。本轮：

- 只读探测（`rdp_c3=16`）：`0x82700000` 可读；`0x82703000` 8×16 B 全 0；
  feed/recycle/RXQ buf/data buf type 全 0；`0x82803000` 为 `0101f300`
  （RNR 寄存器，禁写）。
- 写入（`rdp_c3=1 rdp_c3_ring_base=0x82703000`）：8 项 16 B 全部写入、
  **8/8 回读一致**（ring[0] = `00000100 00001000 00004b03 00000000`，
  ring[7] = `00000107 00001000 00704b03 ...`，phys 0x034b0000+i*0x1000）。
- `lan2` carrier=1 保持；无 panic；`rmmod` 后 `ring table[0..7] restored`，
  再探测 `0x82703000` 回到全 0。
- `.ko` 657,780 B、未决 0。

**下一步 C3：** `rdp_c3=3`（ring + BBH_TX）验证 `+0x90c=0x3f`、
`+0x03c=0x05247c5e`；DSPTCHR/RNR 仍等分支 6 给值。



## 2026-10-30：C3 mask=3 —— ring + BBH_TX（run #90 复用）

`rdp_c2=1 rdp_c3=3 rdp_c3_ring_base=0x82703000`：

- ring 8 项写读回 8/8 OK（phys 0x03470000+i*0x1000）；
- `BBH_TX[0] +0x90c` old=0x3f → 0x3f，readback OK；
- `BBH_TX[0] +0x03c` old=0x52047c5e → 0x05247c5e，readback OK；
- FPM_CTRL 0x00100188 → 0x00100189 OK；lan2 carrier=1；无 panic；
- rmmod：ring table[0..7] + BBH_TX[0] restored，模块卸载干净。

下一步需要分支 6 提供 DSPTCHR credit 值与 RNR 端口表编码，否则 C3 只能
停在 ring+BBH。



## 2026-10-30：默认参数回归（link/insmod 循环）

- `link_cycle.sh 10 lan2`：10/10 成功，恢复 3030–3050ms（avg 3042ms）；
- `insmod_cycle.sh 5`：5/5 rc=0、carrier=1、无未知 WARN（每轮 6 条已知
  dev_addr WARN）；Oops/BUG/panic=0；uptime 连续。
- 修复 `insmod_cycle.sh`：dmesg 环形缓冲会回绕（before=1450/after=1437），
  行号切片失效导致 WARN 计数为 0；改为 /dev/kmsg 标记 + awk 切片，
  重跑 3 轮计数正确（known=6/轮）。
- 设备结束状态：rmmod，仅 eth0/br-lan/lo。



## 2026-10-30：C3 bit2/bit3 实现（run #91，离线构建）

- DSPTCHR `0x82880000+0x300+4*v`：RMW 清 bit[31:20]（credit=0），保留
  低 20 位阈值；v=0..4（参数 `rdp_c3_dsp_ports`）。
- RNR `0x82800000+0x1000*core` bit0=1 RMW；core=0..5（参数 `rdp_c3_rnr_cores`）。
- BBH_RX `0x82898000+0x400*p`：+0x04=p|(p<<8)、+0x30=0x12、+0x34=0xff、
  +0x3c=0x3（最后写）；p=0..6（参数 `rdp_c3_rx_ports`）。
- 每项写后读回；restore 顺序 BBH_RX(先关) → RNR → DSPTCHR → ring → BBH_TX。
- run #91 `.ko` 667,820 B、未决 0；**未上设备**。
- 上设备用 `rdp_c2=1 rdp_c3=15 rdp_c3_ring_base=0x82703000`；关键验收是
  `0x82703000+ring_id*0x10+6` 的大端写指针是否增长。



## 2026-10-30：C3 mask=15 设备验证（run #92）

- 读回：ring 8/8、BBH_TX 2/2、DSPTCHR 5/5、RNR 6/6、BBH_RX 7/7 全部 OK；
- lan2 carrier=1；无 Oops/panic（uptime 615→635s）；
- **ring 写指针 t=1..14s 全 0，未增长**（Q_DEST=0 → 走转发不上送 CPU）；
- rmmod：BBH_RX→RNR→DSPTCHR→ring→BBH_TX 全部 restore，卸载干净。
- `.ko` 672,556 B、未决 0。

下一步：Q_DEST/feed-cache 表（推测项，需分支 6）或 C3-b 端口/VLAN。



## 2026-10-30：Q_DEST punt 实验（run #93）

- 基线：0x82703870=0、0x82703a40=0、0x828804c0=0x00001000（bit12）、
  0x82880600=0，但 0x82880604=0x3f（掩码可能在 +4）。
- `rdp_c3=31`（port=2）：0x4c0 0x1000→0x1004 读回 OK；0x600 未变。
- ring 写指针 20 个采样全 0；**lan2 rx_packets=0** —— 测试期间无流量，
  实验无效（不能判定 punt 失败）。
- 无 panic；rmmod 后 Q_DEST 回 0x1000、其余全部 restore。
- `.ko` 676,204 B、未决 0（probe 移到 bit5=32）。

下一步：先给 LAN2 造流量（对端发包/配 IP ping），再复测写指针；
并向分支 6 确认掩码寄存器是 0x600 还是 0x604。



## 2026-10-30：LAN2 流量检查（阻塞）+ 修正版 Q_DEST（run #94）

- mask=15 + `10.99.0.1/24`：`lan2 carrier=0`（无插线/无对端），rx 0→0（20s），
  ring 指针全 0；寄存器读回全 OK；无 panic；rmmod 全 restore。
- 实验卡在物理层，三段式 Q_DEST 未执行。
- 修正版按分支 6：VQ_EN(0x82880004) 关门 → 0x4C0 置 VIQ 位 → 开门，
  每步读回，restore 逆序；probe 增加 VQ_EN 基线。
- run #94 `.ko` 677,396 B、未决 0，未上设备。
- 下次：LAN2 插对端 + 对端 ping → mask=31 → 看 rx 与 ring2 指针。



## 2026-10-30：RNR 修复 + mask=63（run #95/#96）

- run #95：RNR 只断言 bit0；现场发现 RNR[1] bit1 被硬件清、RNR[5] bit0 写不进（core 不存在）。
- run #96：逐核重试、不可写 SKIP、默认 cores=5；新增 `rdp_c3_watch_secs`。
  `.ko` 678,516 B、未决 0、md5 e03ff8cef02ade1270a4f510fba0c7dc。
- mask=63 全部读回 OK（ring 8/8、BBH_TX、DSPTCHR 0..4、RNR 0..4 bit0=1、BBH_RX 0..6）；
  Q_DEST/VQ_EN 三段式 0x1fbf→0x1fbb→0x1fbf、0x4c0 0x1000→0x1004，0x604 未动。
- watch 70s：写指针 r0..r7 全 0；carrier 20s 起 =1；PC 有线网卡窗口内
  SentBroadcast +164 / SentMulticast +548，ARP 确有发出；rx_packets=0（驱动无 RX 上送路径）。
- 无 panic；rmmod 全部 restore（含 VQ_EN/Q_DEST）。
- 结论：寄存器层全通，**punt 未被观测到**；下一步 C3-b 诊断（ring dump/UNIMAC CMD/MIB）
  → RXQ 表（0x3870/0x3a40）→ B1（RDP switch 初始化）。



## 2026-10-30：C3-b 诊断 —— UNIMAC MIB 确认 LAN2 收包（run #97/#98）

- run #97（683,516 B、md5 bb299959…）：dump 0x700–0x77c 发现不是计数器
  （多为 deaddead），CMD link up 后 0x118601db（TX/RX 使能）；ring 无变化。
- run #98（685,032 B、md5 b2381757…）：按 §4.8 dump RMON 0x400–0x61c；
  PC 在 60s 窗口发 2430 帧 UDP 广播（192.168.1.255），MIB：
  0x408 0x76d→0xf8d、0x450 0xedc→0x1738、0x458 0xd1aa9→0x109f6b（+230,594 B）、
  0x468 0x574→0xdce；字节/包 ≈107.8 B，与 106 B 广播帧吻合。
- ring 表项 tick30/90 逐字一致，写指针 r0..r7 95 采样全 0；BBH_RX2/VQ_EN/Q_DEST
  前后不变；lan2 sysfs 统计全 0（驱动无 RX 上送）。
- 结论：MAC 确实收包；问题在 punt/RXQ/VIQ 映射（runner ingress 未把包送入
  CPU ring）。下一步 C3-c：RXQ 表 0x82703870/0x82703a40 + VIQ2→ring；必要时 B1。
- 无 panic；rmmod 全部 restore；设备已释放。

