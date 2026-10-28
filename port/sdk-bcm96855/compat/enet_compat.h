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

static inline void fkb_flush(struct fkb *fkb, void *data, unsigned int len,
			     int flags)
{
	(void)fkb; (void)data; (void)len; (void)flags;
}

/* ---- netdev 私有信息（RDPA 侧） ---- */
typedef enum {
	BCM_NETDEV_PRIV_INFO_NONE = 0,
} bcm_netdev_priv_info_type_t;

typedef struct {
	void *rdpa_if;
	int   index;
} bcm_netdev_priv_info_out_t;

#endif /* _BCM_ENET_COMPAT_H_ */
