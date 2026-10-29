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




