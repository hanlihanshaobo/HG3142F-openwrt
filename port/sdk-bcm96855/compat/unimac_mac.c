/* SPDX-License-Identifier: GPL-2.0 */
/*
 * unimac_mac.c —— 最小 unimac MAC 驱动（只为让 lan0..lan4 netdev 建出来）
 *
 * 背景：原厂 mac_drv_unimac.c / unimac_drv_impl1.c 在这份 GPL 包里只有
 * aarch64 的 .o，armv7 无法链接。这里按 mac_drv.h 的 ops 契约给出最小实现：
 *
 *   · init/enable/disable/mtu_set/eee_set/stats_* 全部返回 0（不做寄存器操作）
 *   · dev_add/dev_del 只做记账
 *   · 不注册中断、不做 DMA、不碰 unimac 寄存器
 *
 * 这样 port.c:port_init() 能顺利走到 enet_create_netdevice()，
 * register_netdevice() 把 lanX 建出来（DOWN 状态）。
 * TX/RX 仍然是 fail-closed（enet 的 ndo_start_xmit 在未 open 时不会跑）。
 *
 * TODO(下一关)：读 unimac 版本寄存器、配置 RX/TX DMA 环、注册中断、
 * 接 rdpa/runner 的收发路径。
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "mac_drv.h"

#define UNIMAC_COMPAT_MAX	8

struct unimac_compat_priv {
	int id;
};

static struct unimac_compat_priv unimac_privs[UNIMAC_COMPAT_MAX];

static int unimac_compat_init(mac_dev_t *self)
{
	pr_info("unimac-compat: init mac %d (no register access yet)\n",
		self->mac_id);
	return 0;
}

static int unimac_compat_enable(mac_dev_t *self)
{
	(void)self;
	return 0;
}

static int unimac_compat_disable(mac_dev_t *self)
{
	(void)self;
	return 0;
}

static int unimac_compat_mtu_set(mac_dev_t *self, int mtu)
{
	(void)self;
	(void)mtu;
	return 0;
}

static int unimac_compat_eee_set(mac_dev_t *self, int enable)
{
	(void)self;
	(void)enable;
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

static int unimac_compat_dev_add(mac_dev_t *self)
{
	pr_info("unimac-compat: dev_add mac %d\n", self->mac_id);
	return 0;
}

static int unimac_compat_dev_del(mac_dev_t *self)
{
	pr_info("unimac-compat: dev_del mac %d\n", self->mac_id);
	return 0;
}

mac_drv_t mac_drv_unimac = {
	.mac_type = MAC_TYPE_UNIMAC,
	.name = "UNIMAC(compat)",
	.init = unimac_compat_init,
	.enable = unimac_compat_enable,
	.disable = unimac_compat_disable,
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
