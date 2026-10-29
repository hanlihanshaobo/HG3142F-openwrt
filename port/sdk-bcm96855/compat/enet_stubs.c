/*
 * enet_stubs.c — bcm_enet 在主线 6.18 上缺的厂商符号
 *
 * 背景：
 *   厂商的 bcm_enet 依赖 bcmkernel 给内核打的补丁（struct sk_buff 加 recycle 字段、
 *   struct net_device 加 bcm_nd_ext、一堆 netdev_wan_* / PKT*FCDONE 辅助宏）。
 *   那套头文件（bcm_skbuff.h / bcm_netdevice.h）会**整体替换** sk_buff 和 net_device，
 *   在 6.18 上根本不能用，所以驱动里对这些东西的调用就变成了「隐式函数声明」，
 *   编译能过，链接时全 undefined。
 *
 *   这里把它们逐个补成真实函数。默认参数提升后 ABI 一致（都是 32 位），
 *   所以即便原型和厂商写法有细微差别也不会错位。
 *
 * 语义取舍（都不在数据转发主路径上，或者用等价实现）：
 *   · skb_headerinit  —— 厂商是「把外部 DMA buffer 挂到 skb 上」，主线不能这么干
 *                        （skb->head 必须是自己 kmalloc 的），这里改成 memcpy 拷贝。
 *                        代价是一次内存拷贝，换来的是不会踩坏内存。
 *   · netdev_path_get_root —— 主线没有 netdev_path 概念，直接返回 dev。
 *   · *_wan_* / PKT*FCDONE / IQOS —— 我们全端口都是 LAN，且不开 iQoS，退化为常量。
 *   · sw_rreg/sw_wreg —— SF2 交换芯片专用，6855 是 Runner，不会走到，返回错误。
 */

#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/string.h>
#include <linux/errno.h>

#include "phy_drv.h"	/* RecycleFuncP */

struct blog_t;		/* 厂商日志结构，这里只按指针透传 */

/* ------------------------------------------------------------------ */
/*  sk_buff 相关                                                       */
/* ------------------------------------------------------------------ */

/*
 * 把一块已分配好的数据缓冲「挂」到 skb 上。
 * 主线不允许把 skb->head 指到外部内存（kfree_skb 会按自己的分配规则释放），
 * 所以这里退化成拷贝：先 reserve headroom，再 put 数据。
 */
void skb_headerinit(unsigned int headroom, unsigned int datalen,
		    struct sk_buff *skb, unsigned char *data,
		    RecycleFuncP recycle_hook, unsigned long recycle_context,
		    struct blog_t *blog_p)
{
	unsigned int room;

	(void)recycle_hook;
	(void)recycle_context;
	(void)blog_p;

	if (!skb)
		return;

	/* 已经预留过就不要再 reserve，避免把 tail 顶出边界 */
	if (skb_headroom(skb) < headroom)
		skb_reserve(skb, headroom - skb_headroom(skb));

	room = skb_tailroom(skb);
	if (datalen > room) {
		pr_warn_once("bcm_enet: rx frame %u > tailroom %u, truncated\n",
			     datalen, room);
		datalen = room;
	}

	if (data && datalen) {
		memcpy(skb_put(skb, datalen), data, datalen);
	} else if (datalen) {
		skb_put(skb, datalen);
	}
}

/* 厂商用它对 BCM_SKB_ALIGNED_SIZE 做一致性校验（enet.c:2456） */
size_t skb_aligned_size(void)
{
	return (sizeof(struct sk_buff) + 0x0f) & ~0x0fUL;
}

/* ------------------------------------------------------------------ */
/*  net_device 相关                                                    */
/* ------------------------------------------------------------------ */

/* 主线没有 netdev_path，链路上直接就是自己 */
struct net_device *netdev_path_get_root(struct net_device *dev)
{
	return dev;
}

/* 厂商版本是从 netdev_notifier_changeupper_info 里取 upper_dev，
 * 主线等价物是直接查 master。 */
struct net_device *netdev_get_bridge_master(struct net_device *dev, void *ptr)
{
	(void)ptr;
	return netdev_master_upper_dev_get(dev);
}

int is_netdev_wan(struct net_device *dev)
{
	(void)dev;
	return 0;		/* 本机全部端口都是 LAN */
}

void netdev_wan_set(struct net_device *dev)
{
	(void)dev;
}

void netdev_wan_unset(struct net_device *dev)
{
	(void)dev;
}

int is_netdev_tx_pad(struct net_device *dev)
{
	(void)dev;
	return 0;
}

/* ------------------------------------------------------------------ */
/*  流控 / iQoS 标记                                                   */
/* ------------------------------------------------------------------ */

int PKTISFCDONE(struct sk_buff *skb)
{
	(void)skb;
	return 0;
}

void PKTCLRFCDONE(struct sk_buff *skb)
{
	(void)skb;
}

int BROADSTREAM_IQOS_ENABLE(void)
{
	return 0;		/* 不开 Broadstream iQoS */
}

struct sk_buff *bcm_iqoshdl_wrapper(struct net_device *dev, void *pNBuff)
{
	(void)dev;
	(void)pNBuff;
	return NULL;
}

/* ------------------------------------------------------------------ */
/*  SF2 交换芯片（6855 是 Runner，走不到这里）                          */
/* ------------------------------------------------------------------ */

int sw_rreg(int unit, int page, int reg, uintptr_t val, int len)
{
	(void)unit; (void)page; (void)reg; (void)val; (void)len;
	return -EOPNOTSUPP;
}

int sw_wreg(int unit, int page, int reg, uintptr_t val, int len)
{
	(void)unit; (void)page; (void)reg; (void)val; (void)len;
	return -EOPNOTSUPP;
}

int IS_UNIT_SF2(unsigned int unit)
{
	(void)unit;
	return 0;
}
