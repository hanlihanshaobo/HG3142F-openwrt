/* SPDX-License-Identifier: GPL-2.0 */
/*
 * bcm_enet 移植兼容层
 *
 * 厂商 enet 驱动（impl7）里有少量 Broadcom SDK 侧的类型/钩子，
 * 在纯 OpenWrt 6.18 环境下没有对应实现。这里给出最小可用替代：
 *
 *   fwdcb_t            —— 流控/丢包回调（可选特性，置空即可）
 *   FKB_*              —— 快速内核缓冲（Fast Kernel Buffer）API
 *   PKT_DROP / blog_*  —— Broadcom 日志与丢包动作标记
 *   bcm_netdev_priv_info_* —— 私有信息查询（RDPA 侧，暂不支持）
 *
 * 这些都不在数据转发主路径上（主路径是 unimac DMA + 交换芯片端口表），
 * 所以退化实现不影响网口基本收发。
 */
#ifndef _BCM_ENET_COMPAT_H_
#define _BCM_ENET_COMPAT_H_

#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>

/* ---- blog / 丢包动作 ---- */
#ifndef PKT_ACCEPT
#define PKT_ACCEPT	0
#endif
#ifndef PKT_DROP
#define PKT_DROP	1
#endif
#ifndef PKT_STOLEN
#define PKT_STOLEN	2
#endif

/* ---- fwdcb：厂商的流控回调（无实现时按“接受”处理） ---- */
typedef int (*fwdcb_t)(struct sk_buff *skb, struct net_device *dev);

/* ---- FKB（Fast Kernel Buffer）：本移植不使用，给出空实现 ---- */
#ifndef FKB_CACHE_FLUSH
#define FKB_CACHE_FLUSH	0
#endif
/* 驱动里用 `skb == FKB_FRM_GSO` 判断“GSO 帧”这一特殊标记，
 * 我们的路径不会产生该标记，用不可能的地址即可。 */
#ifndef FKB_FRM_GSO
#define FKB_FRM_GSO	((struct sk_buff *)ERR_PTR(-4095))
#endif

struct fkb;

/* ---- nbuff.h 依赖的厂商类型（纯 SDK 侧，仅做类型占位） ---- */
#ifndef _WLFLOWINF_T_DEFINED
#define _WLFLOWINF_T_DEFINED
/* nbuff.h 里作为 union 成员使用，实际内容在 WiFi 路径才用到 */
typedef struct { uint32_t _opaque; } wlFlowInf_t;
#endif

#ifndef _RECYCLEFUNCP_DEFINED
#define _RECYCLEFUNCP_DEFINED
/* FKB 回收回调；驱动里只做函数指针强制转换，签名不影响编译 */
typedef void (*RecycleFuncP)(void *ctx, void *buf, int len);
#endif

/* 注意：fkb_flush 不要在这里定义 —— nbuff.h 自己声明了
 *   extern void fkb_flush(FkBuff_t *, uint8_t *, int, int);
 * 我们早先的定义签名不同，会报 conflicting types。此处交给 SDK 头声明，
 * 真正的实现放到 enet_stubs.c（链接期补齐）。 */

/* ---- netdev 私有信息（RDPA 侧） ---- */
typedef enum {
	BCM_NETDEV_PRIV_INFO_NONE = 0,
	BCM_NETDEV_TO_RDPA_IF,		/* enet_priv_info_get 的 case 需要 */
} bcm_netdev_priv_info_type_t;

typedef struct {
	void *rdpa_if;
	int   index;
	/* enet.c 在 #ifdef RUNNER 下访问 .bcm_netdev_to_rdpa_if.rdpa_if，
	 * 我们不开 RUNNER，但结构体成员要存在才能过编译 */
	struct { int rdpa_if; } bcm_netdev_to_rdpa_if;
} bcm_netdev_priv_info_out_t;

#endif /* _BCM_ENET_COMPAT_H_ */
