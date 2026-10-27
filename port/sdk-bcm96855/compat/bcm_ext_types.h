/* HG3142F compat: Broadcom bcmkernel 结构扩展（精简版）
 *
 * 目标：让 6.18 内核能编译 bcm_enet 驱动，而不整体替换 sk_buff/net_device。
 * 只含 enet 实际使用的最小定义（基于 asuswrt bcmkernel 头分析）。
 *
 * 用法：KCFLAGS="-include .../bcm_ext_types.h"，配合 patch_kernel.py 给
 *       struct sk_buff 加 recycle_flags、struct net_device 加 bcm_nd_ext。
 */
#ifndef __HG3142F_BCM_EXT_TYPES_H
#define __HG3142F_BCM_EXT_TYPES_H

#include <linux/types.h>

struct net_device;   /* 前向声明，避免循环依赖 */

/* 来自 bcmkernel/include/linux/bcm_netdev_path.h:38 */
struct netdev_path {
	int refcount;
	unsigned int hw_port;
	unsigned int hw_tx_port;
	unsigned int hw_port_type;
	unsigned int hw_subport_mcast_idx;
};

/* 来自 bcmkernel/include/uapi/linux/bcm_maclimit.h */
struct mac_limit {
	unsigned int enable;
	unsigned int max;
	unsigned int max_zero_drop;
	unsigned int drop_count;
	unsigned int min;
	unsigned int reserve;
	unsigned int learning_count;
};

/* 来自 bcmkernel/include/linux/bcm_netdevice.h:88（精简：去掉 blog/wlan/br 子结构） */
struct bcm_netdev_ext {
	unsigned int iff_flags;
	struct netdev_path path;
	unsigned int blog_stats_flags;
	struct mac_limit mac_limit;
};

/* 来自 bcmkernel/include/linux/bcm_skbuff.h */
typedef void (*RecycleFuncP)(void *nbuff_p, unsigned long context, uint32_t flags);

#ifndef __hg3142f_wlFlowInf_t_defined
typedef unsigned int wlFlowInf_t;   /* enet 不访问其成员，简化为 uint */
#define __hg3142f_wlFlowInf_t_defined
#endif

#endif /* __HG3142F_BCM_EXT_TYPES_H */
