# 内核基线升级：6.18.55 → 6.18.60

> 日期：2026-11-05
> 上游：OpenWrt main `5fb9d9cd90ca751b5066a37517caa9fbc158ded7`
> 内核：Linux **6.18.60**（`LINUX_VERSION-6.18 = .60`）

## 一、为什么是 6.18.60，而不是 7.3

截至 2026-11-05，OpenWrt main 的 `target/linux/generic/` 仍然只有 **6.18**：

```
target/linux/generic/config-6.18
target/linux/generic/kernel-6.18   →  LINUX_VERSION-6.18 = .60
```

Linux 7.3 虽然已经发布，但 OpenWrt 尚未把它合入 main（main 继续使用 6.18 LTS）。
强行把本 target 切到 7.3 会同时失去 OpenWrt 的 generic config、backport/pending 补丁集和
`KERNEL_PATCHVER` 支持，属于另一次平台移植，不在本次“跟随上游内核”范围内。

因此本次升级到 **OpenWrt main 当前实际使用的最新内核 6.18.60**。

## 二、改动

| 文件 | 改动 |
|---|---|
| `port/sources.lock.json` | OpenWrt pin 从 `a27b155`（6.18.55）更新到 `5fb9d9c`（kernel bump 6.18.60）；新增 `kernel_version` 字段 |
| `port/scripts/prepare.sh` | 新增内核基线自检：锁文件的 `kernel_version` 与 pin 到的 `target/linux/generic/kernel-6.18` 不一致时直接失败 |
| `tools/fit_build.py` | FIT 的 kernel 节点描述从 `Linux-6.18.55` 改为 `Linux-6.18.60` |
| `README.md` | 记录构建基线 |
| `port/overlay/target/linux/bcmbca/config-6.18` | 不需要改；新增符号由 CI 的 `olddefconfig` 自动补默认值（见下） |

`KERNEL_PATCHVER` 仍是 `6.18`，所以 workflow 里的 `linux-6.18*`、`config-6.18`
路径全部保持有效，不需要改构建流水线。

## 三、兼容性核对

### 1. generic config：只有新增，没有删除

对比 `a27b155` 与 `5fb9d9c` 的 `target/linux/generic/config-6.18`：

```
1 file changed, 31 insertions(+)
```

新增项全部是默认关闭的驱动/Kconfig 符号（`AX88796B_RUST_PHY`、`CHARLCD_BL_FLASH`、
`NOVA_CORE`、`SEG_LED_GPIO` 等）。
CI 在 `Generate complete kernel config` 步骤里执行 `olddefconfig`，会自动补齐，
不会出现交互式提问。

### 2. 6.18.56 的补丁刷新已经包含在 pin 里

6.18.56 的 bump commit 同时刷新/删除了 OpenWrt 的 generic backport/pending 补丁，
并更新了 `config-filter`（新增 `CONFIG_CC_OPT_INLINE_MEMSET` 过滤）。
本次直接 pin 到 6.18.60 的 bump commit，拿到的是完整的 `.56`→`.60` 补丁刷新链，
不需要在 `port/patches/` 里重复这些改动。

### 3. 树外模块（bcm_enet / bcm_pcie / bcm_reboot）

6.18.55 → 6.18.60 是 stable 系列内的更新，内核不破坏 KABI：
`sk_buff`、`net_device`、`pci_host_bridge`、watchdog/MTD 接口均未变化。
`port/sdk-bcm96855/compat/` 里针对 6.18 的兼容层无需修改。

真正的编译验证仍由 CI 的两条探针完成：

- `SDK compile probe (bcm_enet)`：`bcm_enet.ko` + `zImage-nodtb` + DTB
- `SDK compile probe (bcm_pcie)`：`bcm_pcie.ko`

## 四、验证

本地无法运行 OpenWrt 构建时，用 CI 验证：

1. 触发 `Build OpenWrt firmware`（完整构建）
2. 确认日志里出现：

   ```
   ==> 内核基线: Linux 6.18.60
   Linux version 6.18.60 ...
   ```

3. 检查完整构建：
   - `out/Image`、`out/root.squashfs`、`out/overlay.ubifs`
   - `SDK compile probe (bcm_enet)` / `(bcm_pcie)` 的 `rc=0`，且没有未决符号
4. 如果走 `module_only` 快速通道，额外检查：
   - `zImage-nodtb`（FIT 用裸内核）
   - `bcm96855-mb4784w.dtb`
   - `bcm_enet.ko`

> 这只改了**构建基线**；设备是否生效取决于当前槽位。只有把新 `bootfs1` FIT
> 写入对应槽位后，`uname -r` 才会变成 6.18.60。

## 五、下次再升级

1. 在 OpenWrt main 找到 `kernel: bump 6.18 to 6.18.xx` 提交
2. 更新 `port/sources.lock.json` 的 `commit` / `kernel_version`
3. 跑一次完整 CI；`prepare.sh` 会在源码 checkout 后立即校验版本是否匹配
4. 如果上游切到新的 major（例如 7.x），需要同时处理：
   - `port/overlay/target/linux/bcmbca/Makefile` 的 `KERNEL_PATCHVER`
   - workflow 里所有 `linux-6.18*` / `config-6.18` 路径
   - `target/linux/generic/config-*` 的基线
