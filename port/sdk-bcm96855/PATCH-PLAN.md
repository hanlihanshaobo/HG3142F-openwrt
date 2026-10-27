# bcmkernel patch 移植方案（A 路线）

日期：2026-10-27
目标：把 Broadcom 对 Linux 内核核心结构的改造，移植到 OpenWrt 的 6.18 内核

## 一、Broadcom 的机制（已确认）

Broadcom 不是"用标准 API 写驱动"，而是**改造内核本身**，通过 `CONFIG_BCM_KF_*` 宏控制：

### 1. struct sk_buff 被整体替换
- asuswrt `kernel/linux-4.19/include/linux/skbuff.h:599` → `#include <linux/bcm_skbuff.h>`
- 注释（679 行）：`struct sk_buff is moved to bcm_skbuff.h. Here is no longer used.`
- `bcm_skbuff.h` 定义了**完整的 sk_buff**（4.1 结构 + Broadcom 扩展）

### 2. struct net_device 被加字段
- asuswrt `kernel/linux-4.19/include/linux/netdevice.h:1811`
  ```c
  #if defined(CONFIG_BCM_KF_NETDEV_EXT)
      struct bcm_netdev_ext bcm_nd_ext;
  #endif
  ```

## 二、需要移植的内容清单

### A. sk_buff 扩展字段（`kernel/bcmkernel/include/linux/bcm_skbuff.h`）
| 字段 | 类型 | 条件 |
|---|---|---|
| `recycle_and_rnr_flags` / `recycle_flags` | `unsigned int` | CONFIG_BCM_KF_NBUFF |
| `fkbInSkb` | `void *` | 同上 |
| `blog_p` | `struct blog_t *` | CONFIG_BLOG |
| `fkb`（联合体：fkb_mark/fkb_len/wl 等） | union | 同上 |
| `wl` | `wlFlowInf_t` | 同上 |
| `recycle_hook` | `RecycleFuncP` | 同上 |
| `recycle_context` | `unsigned long` | 同上 |
| `bcm_ext` | `struct bcm_skb_ext` | 同上 |

### B. net_device 扩展（`kernel/bcmkernel/include/linux/bcm_netdevice.h:88`）
```c
struct bcm_netdev_ext {
    unsigned int iff_flags;
    struct netdev_path path;
#if defined(CONFIG_BLOG)
    BlogStats_t blog_stats;
    unsigned int blog_stats_flags;
    wlan_client_get_info_t wlan_client_get_info;  /* CONFIG_BCM_WLAN_MODULE */
#endif
    struct bcm_br_ext bcm_br_ext;   /* CONFIG_BCM_RDPA_BRIDGE */
    struct mac_limit mac_limit;
    bcm_netdev_priv_info_get_cb_fn_t bcm_netdev_cb_fn;
};
```

### C. 依赖类型（需一并提供）
- `wlFlowInf_t`（bcm_skbuff.h 里是 union，~30 行）
- `struct netdev_path`（bcm_netdev_path.h）
- `struct mac_limit`、`struct bcm_br_ext`、`BlogStats_t`
- `RecycleFuncP`（已有 stub）
- `wlFlowInf_t` 与我之前的 stub 冲突，需删掉 stub 里的 typedef

## 三、实施步骤（建议）

### 步骤 1：写内核补丁（改 6.18 内核源码）
在 OpenWrt 的 `target/linux/bcmbca/patches-6.18/` 加 patch，或 CI 内脚本化：

1. **skbuff.h**：在 `struct sk_buff { ... };` 的末尾（`};` 前）插入 Broadcom 字段：
   ```c
   unsigned int recycle_flags;
   struct blog_t *blog_p;
   wlFlowInf_t wl;
   RecycleFuncP recycle_hook;
   unsigned long recycle_context;
   ```
2. **netdevice.h**：在 `struct net_device { ... };` 末尾插入：
   ```c
   struct bcm_netdev_ext bcm_nd_ext;
   ```

### 步骤 2：提供 `struct bcm_netdev_ext` 等定义
- 从 SDK 的 `bcm_netdevice.h` 提取（但要剥离对 bcmkernel 其他头的依赖）
- 放一个 patch 或 compat 头，在 netdevice.h 之前 include

### 步骤 3：探针验证
- CI 探针里先 patch 内核头，再 `make M=`
- 观察错误是否下降

## 四、风险与注意
1. **改内核源码是侵入性的**：字段加在结构末尾可降低 ABI 影响，但 sk_buff 是热路径，字段位置影响 cache 行为（功能不受影响，性能可能）
2. **`CONFIG_BCM_KF_*` 宏未定义**：enet.c 无条件用 `skb->recycle_flags`，所以字段必须加
3. **`struct netdev_path` 依赖链**：可能牵出更多结构（path 的 hw_port 等）
4. **不能 include `bcm_skbuff.h`**（会整体替换 sk_buff 导致冲突），只能取字段
5. **之前 sed 加 include 需回退**

## 五、当前进度
- 分析：✅ 完成（机制、字段、依赖全部摸清）
- 实施：⬜ 待做（步骤 1-3）
- 预计工作量：中-大，需 2-5 轮 CI 迭代
