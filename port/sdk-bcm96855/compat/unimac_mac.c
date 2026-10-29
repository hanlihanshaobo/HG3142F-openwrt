/* SPDX-License-Identifier: GPL-2.0 */
/*
 * unimac_mac.c —— BCM96855 unimac MAC 最小驱动（按逆向结果实现寄存器序列）
 *
 * 依据：docs/REVERSE-ENET.md
 *   §3.1  实例基址 0x828a8000 + 0x1000*n，MISC 0x828b0000 + 0x400*n
 *   §4.2  mac_hwapi_init_emac：CMD / FRM_LEN / RXERR_MASK
 *   §4.3  port_unimac_init：+ set_external_conf(0) + tx_min_pkt_size(0x40)
 *   §4.4  set_speed / set_duplex（bit22 写保护、bit13 sw_reset 包夹）
 *   §4.6  set_flow_control / pause
 *   §4.7  MAC 地址：0x0c = MAC[31:0]，0x10 = MAC[63:48]（本机只用于 pause SA）
 *   §4.9  复位/时钟由 bootloader 负责，驱动不碰
 *   §5.1  unimac 在本机**没有 DMA、没有中断**，数据面在 RDP
 *
 * 说明：本文件只做 MAC 层（路线 B，REVERSE-ENET.md §9.2）。数据面仍
 * fail-closed：ndo_start_xmit 走 enet 原厂路径，最终由我们的 RDPA 桩丢弃。
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/string.h>

#include "mac_drv.h"

#define UNIMAC_COMPAT_MAX	8

/* §3.1：头文件约定 base = 0x828a8000 + 0x1000*n；MISC = 0x828b0000 + 0x400*n */
#define UNIMAC_RDP_BASE(n)	(0x828a8000u + 0x1000u * (u32)(n))
#define UNIMAC_MISC_BASE(n)	(0x828b0000u + 0x400u * (u32)(n))

/* §3.2 寄存器偏移（unimac_drv.h 约定） */
#define UMAC_REG_IPG		0x004	/* [6:2] RX IPG */
#define UMAC_REG_CMD		0x008	/* 主控 */
#define UMAC_REG_MAC0		0x00c	/* pause SA[31:0] */
#define UMAC_REG_MAC1		0x010	/* pause SA[63:48] */
#define UMAC_REG_FRM_LEN	0x014	/* RX 最大帧长 */
#define UMAC_REG_PAUSE_Q	0x018	/* pause quanta */
#define UMAC_REG_TX_IPG		0x05c	/* [6:0] TX IPG，[13:8] min pkt */
#define UMAC_REG_EEE_CTRL	0x064	/* bit3 EEE enable */
#define UMAC_REG_PAUSE_CTRL	0x330	/* [17] en，[16:0] quant */
#define UMAC_REG_REV		0x780
#define UMAC_REG_RXERR_MASK	0x784
#define UMAC_REG_MIB_MAX_PKT	0x788

/* §4.2：上电默认值 0x008000d8（1000M full + crc/pause 透传 + cntl_frm_ena） */
#define UMAC_CMD_DEFAULT	0x008000d8u
#define UMAC_CMD_KEEP_MASK	0x6c3f7c00u
/* bit24：接交换机模式（逆向里由 DT 的 multi-imp/phandle 决定，本板为 1） */
#define UMAC_CMD_SW_ATTACHED	(1u << 24)
#define UMAC_CMD_SW_RESET	(1u << 13)
#define UMAC_CMD_EXT_CONF_LOCK	(1u << 22)
#define UMAC_CMD_HD_ENA		(1u << 10)
#define UMAC_CMD_RX_ENA		(1u << 1)
#define UMAC_CMD_TX_ENA		(1u << 0)

#define UMAC_SPEED_10		0
#define UMAC_SPEED_100		1
#define UMAC_SPEED_1000		2
#define UMAC_SPEED_2500		3
#define UMAC_SPEED_10000		4
#define UMAC_SPEED_5000		5

struct unimac_compat_priv {
	int id;
	void __iomem *base;
	void __iomem *misc;
	int hw_ok;
};

static struct unimac_compat_priv unimac_privs[UNIMAC_COMPAT_MAX];

#define UMAC_PRIV(self)	((struct unimac_compat_priv *)(self)->priv)

static u32 umac_rd(struct unimac_compat_priv *u, u32 off)
{
	return u->hw_ok ? readl(u->base + off) : 0;
}

static void umac_wr(struct unimac_compat_priv *u, u32 off, u32 val)
{
	if (u->hw_ok)
		writel(val, u->base + off);
}

/* §4.4：mac_speed_t → CMD 速率编码 */
static int umac_speed_code(mac_speed_t speed)
{
	switch (speed) {
	case MAC_SPEED_10:	return UMAC_SPEED_10;
	case MAC_SPEED_100:	return UMAC_SPEED_100;
	case MAC_SPEED_1000:	return UMAC_SPEED_1000;
	case MAC_SPEED_2500:	return UMAC_SPEED_2500;
	case MAC_SPEED_5000:	return UMAC_SPEED_5000;
	case MAC_SPEED_10000:	return UMAC_SPEED_10000;
	default:		return UMAC_SPEED_1000;	/* 未知先按 1G */
	}
}

/* §4.4：IPG 表：10M/100M = 0x0a，1G 及以上 = 0x05 */
static u32 umac_ipg_for_speed(int code)
{
	return (code <= UMAC_SPEED_100) ? 0x0a : 0x05;
}

static void umac_set_speed(struct unimac_compat_priv *u, int code)
{
	u32 cmd = umac_rd(u, UMAC_REG_CMD);
	u32 ipg;

	if (!u->hw_ok || (cmd & UMAC_CMD_EXT_CONF_LOCK))
		return;

	umac_wr(u, UMAC_REG_CMD, cmd | UMAC_CMD_SW_RESET);

	cmd = (cmd & ~0xcu) | ((code & 3) << 2);
	cmd = ((code & 4) << 29) | (cmd & 0x7fffffffu);
	umac_wr(u, UMAC_REG_CMD, cmd);

	ipg = umac_ipg_for_speed(code);
	umac_wr(u, UMAC_REG_IPG,
		(umac_rd(u, UMAC_REG_IPG) & ~0x7cu) | ((ipg & 0x1f) << 2));

	umac_wr(u, UMAC_REG_CMD, cmd & ~UMAC_CMD_SW_RESET);
}

static void umac_set_duplex(struct unimac_compat_priv *u, int full)
{
	u32 cmd = umac_rd(u, UMAC_REG_CMD);

	if (!u->hw_ok || (cmd & UMAC_CMD_EXT_CONF_LOCK))
		return;

	umac_wr(u, UMAC_REG_CMD, cmd | UMAC_CMD_SW_RESET);
	if (full)
		cmd &= ~UMAC_CMD_HD_ENA;
	else
		cmd |= UMAC_CMD_HD_ENA;
	umac_wr(u, UMAC_REG_CMD, cmd);
	umac_wr(u, UMAC_REG_CMD, cmd & ~UMAC_CMD_SW_RESET);
}

/* §4.2 + §4.3：port_unimac_init（接交换机模式的 5 次写） */
static int unimac_compat_init(mac_dev_t *self)
{
	struct unimac_compat_priv *u = UMAC_PRIV(self);
	u32 cmd, tx_ipg;

	if (!u || !u->hw_ok) {
		pr_err("unimac-compat: mac %d not mapped, skip init\n",
		       self->mac_id);
		return 0;
	}

	/* ① CMD：保留旧值里 CMD[30:26]/[21:16]/[14:10]，其它按上电默认 */
	cmd = umac_rd(u, UMAC_REG_CMD);
	cmd = (cmd & UMAC_CMD_KEEP_MASK) | UMAC_CMD_DEFAULT |
	      UMAC_CMD_SW_ATTACHED;
	umac_wr(u, UMAC_REG_CMD, cmd);

	/* ② RX 最大帧长 */
	umac_wr(u, UMAC_REG_FRM_LEN, 0xffff);

	/* ③ 接交换机模式：RXERR_MASK |= bit5 */
	umac_wr(u, UMAC_REG_RXERR_MASK,
		umac_rd(u, UMAC_REG_RXERR_MASK) | 0x20);

	/* ④ set_external_conf(0)：清 bit22，解除速率/双工写保护 */
	umac_wr(u, UMAC_REG_CMD,
		umac_rd(u, UMAC_REG_CMD) & ~UMAC_CMD_EXT_CONF_LOCK);

	/* ⑤ set_tx_min_pkt_size(0x40)：TX_IPG[13:8] = 64 */
	tx_ipg = umac_rd(u, UMAC_REG_TX_IPG);
	tx_ipg = (tx_ipg & ~0x3f00u) | (0x40u << 8);
	umac_wr(u, UMAC_REG_TX_IPG, tx_ipg);

	pr_info("unimac-compat: mac %d rev=0x%08x cmd=0x%08x (base=0x%x)\n",
		self->mac_id, umac_rd(u, UMAC_REG_REV),
		umac_rd(u, UMAC_REG_CMD), UNIMAC_RDP_BASE(self->mac_id));

	return 0;
}

/* §4.3 / §8 item7：enable 对应 set_rxtx_enable(mac, tx=1, rx=1) */
static int unimac_compat_enable(mac_dev_t *self)
{
	struct unimac_compat_priv *u = UMAC_PRIV(self);

	if (u && u->hw_ok)
		umac_wr(u, UMAC_REG_CMD,
			umac_rd(u, UMAC_REG_CMD) |
			UMAC_CMD_TX_ENA | UMAC_CMD_RX_ENA);
	return 0;
}

static int unimac_compat_disable(mac_dev_t *self)
{
	struct unimac_compat_priv *u = UMAC_PRIV(self);

	if (u && u->hw_ok)
		umac_wr(u, UMAC_REG_CMD,
			umac_rd(u, UMAC_REG_CMD) &
			~(UMAC_CMD_TX_ENA | UMAC_CMD_RX_ENA));
	return 0;
}

static int unimac_compat_cfg_set(mac_dev_t *self, mac_cfg_t *cfg)
{
	struct unimac_compat_priv *u = UMAC_PRIV(self);
	int code;

	if (!u || !u->hw_ok || !cfg)
		return 0;

	code = umac_speed_code(cfg->speed);
	umac_set_speed(u, code);
	umac_set_duplex(u, cfg->duplex == MAC_DUPLEX_FULL);

	return 0;
}

/* §4.6：flow control。fc[0]=rx，fc[1]=tx；src_addr 只用于 pause 帧 SA */
static int unimac_compat_pause_set(mac_dev_t *self, int rx_enable,
				   int tx_enable, char *src_addr)
{
	struct unimac_compat_priv *u = UMAC_PRIV(self);
	u32 cmd;

	if (!u || !u->hw_ok)
		return 0;

	cmd = umac_rd(u, UMAC_REG_CMD);
	cmd |= (1u << 18) | (1u << 17);		/* sw_override_rx/tx */

	if (rx_enable)
		cmd &= ~(1u << 8);		/* rx_pause_ignore = 0 */
	else
		cmd |= (1u << 8);

	if (tx_enable)
		cmd &= ~(1u << 28);		/* tx_pause_ignore = 0 */
	else
		cmd |= (1u << 28);
	umac_wr(u, UMAC_REG_CMD, cmd);

	/* MISC + 0x08：§4.6 的 top 寄存器 */
	writel((rx_enable || tx_enable) ?
	       (readl(u->misc + 0x08) | (1u << 16)) :
	       (readl(u->misc + 0x08) & 0xfffe0000u),
	       u->misc + 0x08);

	/* §4.7：pause 帧源地址（48 位放在 u64 的 [63:16]） */
	if (src_addr) {
		u32 lo = (u8)src_addr[2] << 24 | (u8)src_addr[3] << 16 |
			 (u8)src_addr[4] << 8 | (u8)src_addr[5];
		u32 hi = ((u8)src_addr[0] << 8) | (u8)src_addr[1];

		umac_wr(u, UMAC_REG_MAC0, lo);
		umac_wr(u, UMAC_REG_MAC1, hi);
	}

	/* §4.6 set_pause_params：0x330[17]=en，[16:0]=quant */
	umac_wr(u, UMAC_REG_PAUSE_CTRL,
		(rx_enable || tx_enable) ? (0xffffu | (1u << 17)) : 0);

	return 0;
}

static int unimac_compat_mtu_set(mac_dev_t *self, int mtu)
{
	struct unimac_compat_priv *u = UMAC_PRIV(self);

	if (!u || !u->hw_ok)
		return 0;

	umac_wr(u, UMAC_REG_FRM_LEN, (u32)mtu & 0x3fff);
	umac_wr(u, UMAC_REG_MIB_MAX_PKT,
		(umac_rd(u, UMAC_REG_MIB_MAX_PKT) & ~0x3fffu) |
		((u32)mtu & 0x3fff));

	return 0;
}

static int unimac_compat_eee_set(mac_dev_t *self, int enable)
{
	struct unimac_compat_priv *u = UMAC_PRIV(self);
	u32 v;

	if (!u || !u->hw_ok)
		return 0;

	v = umac_rd(u, UMAC_REG_EEE_CTRL);
	v = enable ? (v | (1u << 3)) : (v & ~(1u << 3));
	umac_wr(u, UMAC_REG_EEE_CTRL, v);
	return 0;
}

static int unimac_compat_stats_get(mac_dev_t *self, mac_stats_t *stats)
{
	(void)self;
	if (stats)
		memset(stats, 0, sizeof(*stats));
	return 0;
}

static int unimac_compat_stats_clear(mac_dev_t *self)
{
	(void)self;
	return 0;
}

/* mac_dev_t 创建时调用：ioremap MAC + MISC 块（§3.1） */
static int unimac_compat_dev_add(mac_dev_t *self)
{
	struct unimac_compat_priv *u = UMAC_PRIV(self);

	if (!u)
		return -EINVAL;

	if (!u->base) {
		u->base = ioremap(UNIMAC_RDP_BASE(self->mac_id), 0x1000);
		u->misc = ioremap(UNIMAC_MISC_BASE(self->mac_id), 0x400);
		if (!u->base || !u->misc) {
			pr_err("unimac-compat: ioremap mac %d failed\n",
			       self->mac_id);
			if (u->base)
				iounmap(u->base);
			if (u->misc)
				iounmap(u->misc);
			u->base = NULL;
			u->misc = NULL;
			return 0;	/* 不阻塞 netdev 建成 */
		}
		u->hw_ok = 1;
	}

	pr_info("unimac-compat: mac %d mapped 0x%x/0x%x\n", self->mac_id,
		UNIMAC_RDP_BASE(self->mac_id), UNIMAC_MISC_BASE(self->mac_id));
	return 0;
}

static int unimac_compat_dev_del(mac_dev_t *self)
{
	struct unimac_compat_priv *u = UMAC_PRIV(self);

	if (!u)
		return 0;

	if (u->base) {
		iounmap(u->base);
		u->base = NULL;
	}
	if (u->misc) {
		iounmap(u->misc);
		u->misc = NULL;
	}
	u->hw_ok = 0;
	return 0;
}

mac_drv_t mac_drv_unimac = {
	.mac_type = MAC_TYPE_UNIMAC,
	.name = "UNIMAC(compat)",
	.init = unimac_compat_init,
	.enable = unimac_compat_enable,
	.disable = unimac_compat_disable,
	.cfg_set = unimac_compat_cfg_set,
	.pause_set = unimac_compat_pause_set,
	.mtu_set = unimac_compat_mtu_set,
	.eee_set = unimac_compat_eee_set,
	.stats_get = unimac_compat_stats_get,
	.stats_clear = unimac_compat_stats_clear,
	.dev_add = unimac_compat_dev_add,
	.dev_del = unimac_compat_dev_del,
};

/* mac_dev_dt_probe() 里用：按 port_index 取一个稳定的小 priv */
struct unimac_compat_priv *unimac_compat_priv_get(uint32_t id)
{
	return &unimac_privs[id % UNIMAC_COMPAT_MAX];
}
