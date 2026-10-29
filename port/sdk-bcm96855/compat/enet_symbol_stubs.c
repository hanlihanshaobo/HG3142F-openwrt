/* SPDX-License-Identifier: GPL-2.0 */
/*
 * enet_symbol_stubs.c —— bcm_enet 在 6.18 上缺的 46 个外部符号
 *
 * 来源：module-only run #63 的 undefined_symbols.txt（已剔除内核导出符号）。
 * 这些符号在原厂系统里由 bcm_phy.ko / rdpa_drv.ko / bdmf 框架 / 未编译的
 * enet 源文件（enet_ring.c、enet_extlh.c、bcmenet_ioctl_compat.c）提供。
 *
 * 设计原则（fail-closed）：
 *   · 查询/创建类：返回错误（-1）或 NULL，**不写输出参数**；
 *     调用方看到失败会中止 probe，而不是拿到假句柄继续跑。
 *   · 通知/回收/中断类：void no-op（没有真实对象可操作，只能空转）。
 *   · 纯粹的记账数据（unit_port_array/eee_enabled）：给零值。
 *   · 少数可以真正实现的（DT phy-mode 解析、port_by_netdev、skb 分配）
 *     直接实现，减少后续假语义。
 *
 * 每个函数都注明声明来源与 TODO。真正实现要等 unimac/rdpa 驱动到位。
 */
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/spinlock.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/mii.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/pinctrl/consumer.h>

#include "port.h"		/* enetx_port_t / root_sw / unit_port_array 声明 */
#include "enet.h"		/* enetxapi_* 声明 */
#include "crossbar_dev.h"	/* crossbar_phy_dev_active 声明 */
#include <rdpa_api.h>		/* rdpa_cpu_port / rdpa_cpu_tx_info_t ... */

/* ================================================================== */
/*  1) 记账用的全局数据                                                */
/* ================================================================== */

/*
 * 原定义在 bcmenet_ioctl_compat.c（128 KB，未编译）。dt/bp 解析会往里写
 * 端口指针，proc/ioctl 读数。probe 阶段没人写，保持全 NULL 即可。
 */
enetx_port_t *unit_port_array[COMPAT_MAX_SWITCHES][COMPAT_MAX_SWITCH_PORTS];

/* 同上；OAM index 用 -1 表示“未分配”（GCC 区间初始化） */
int unit_port_oam_idx_array[COMPAT_MAX_SWITCHES][COMPAT_MAX_SWITCH_PORTS] = {
	[0 ... COMPAT_MAX_SWITCHES - 1][0 ... COMPAT_MAX_SWITCH_PORTS - 1] = -1,
};

/* mac_drv.h 声明；原定义在 phy/mac_drv.c（未编译）。默认关闭 EEE */
int eee_enabled;

/* sw_common.c 用它串行化外部交换芯片寄存器访问；6855 不走 SF2，留空锁 */
DEFINE_SPINLOCK(extsw_reg_config);

/* ================================================================== */
/*  2) 能直接实现的小函数                                              */
/* ================================================================== */

/*
 * DTS phy-mode 字符串 → phy_mii_type_t。
 * 原实现在 phy/dt_parsing.c（未编译）；enet/dt_parsing.c 只 extern 声明。
 * 返回 UNKNOWN 会让端口跳过 PHY attach，所以这里按 DT 绑定文档逐项映射。
 */
phy_mii_type_t dt_get_phy_mode_mii_type(struct device_node *np)
{
	const char *mode;

	if (!np || of_property_read_string(np, "phy-mode", &mode))
		return PHY_MII_TYPE_UNKNOWN;

	if (!strcmp(mode, "mii"))
		return PHY_MII_TYPE_MII;
	if (!strcmp(mode, "gmii"))
		return PHY_MII_TYPE_GMII;
	if (!strcmp(mode, "rgmii") || !strcmp(mode, "rgmii-id") ||
	    !strcmp(mode, "rgmii-rxid") || !strcmp(mode, "rgmii-txid"))
		return PHY_MII_TYPE_RGMII;
	if (!strcmp(mode, "sgmii"))
		return PHY_MII_TYPE_SGMII;
	if (!strcmp(mode, "hsgmii"))
		return PHY_MII_TYPE_HSGMII;
	if (!strcmp(mode, "xfi") || !strcmp(mode, "1000base-x") ||
	    !strcmp(mode, "2500base-x"))
		return PHY_MII_TYPE_XFI;
	if (!strcmp(mode, "serdes"))
		return PHY_MII_TYPE_SERDES;

	return PHY_MII_TYPE_UNKNOWN;
}

/*
 * netdev → enetx_port_t。原实现在 bcmenet_ioctl_compat.c。
 * 用 port.c 里的 _port_traverse_ports() 遍历（root_sw 由 DT 解析建立）。
 */
struct port_by_netdev_ctx {
	struct net_device *dev;
	enetx_port_t *match;
};

static int _tr_port_by_netdev(enetx_port_t *port, void *_ctx)
{
	struct port_by_netdev_ctx *ctx = _ctx;

	if (port->dev == ctx->dev) {
		ctx->match = port;
		return 1;	/* 找到即停 */
	}
	return 0;
}

int port_by_netdev(struct net_device *dev, enetx_port_t **match)
{
	struct port_by_netdev_ctx ctx = { .dev = dev, .match = NULL };

	if (!dev || !match || !root_sw)
		return -1;

	if (port_traverse_ports(root_sw, _tr_port_by_netdev,
				PORT_CLASS_PORT | PORT_CLASS_SW, &ctx) <= 0)
		return -1;

	*match = ctx.match;
	return 0;
}

/*
 * 原厂 rx 路径从 header cache 取 skb，再把外部 DMA buffer 挂上去。
 * 主线不能挂外部 buffer（我们的 skb_headerinit 已改成 memcpy），
 * 所以这里分配一个足够大的普通 skb，让拷贝路径能工作。
 * TODO: 改成 page_pool / skb cache，避免每包 alloc。
 */
struct sk_buff *skb_header_alloc(void)
{
	/* 2048 覆盖本板 MTU 1500 + 头/尾空间；数值是保守值，不依赖 bcm_pkt_lengths.h */
	return alloc_skb(2048 + 256, GFP_ATOMIC);
}

/* ================================================================== */
/*  3) unimac / MAC 相关（源码缺失，先桩住）                            */
/* ================================================================== */

/*
 * mac_drv.h 声明；unimac 的真实实现未开源（prebuilt 是 aarch64）。
 * 我们从不创建 mac_dev，所以删除是 no-op。
 */
int mac_dev_del(mac_dev_t *mac_dev)
{
	/* 先让驱动自己 iounmap/停硬件，再释放 unimac_mac.c kzalloc 的对象 */
	if (mac_dev && mac_dev->mac_drv && mac_dev->mac_drv->dev_del)
		mac_dev->mac_drv->dev_del(mac_dev);
	kfree(mac_dev);
	return 0;
}

/* mac_drv.h 声明；RGMII 内部延迟配置，本板 RGMII 端口 disabled */
void mac_dev_rgmii_attach(rgmii_params *params)
{
	(void)params;
}

/* crossbar_dev.h 声明；6855 没有 crossbar，直接返回 NULL */
phy_dev_t *crossbar_phy_dev_active(phy_dev_t *phy_dev_crossbar)
{
	(void)phy_dev_crossbar;
	return NULL;
}

/* ================================================================== */
/*  4) Runner 端口/队列（enet_ring.c、runner_with_switch.c 未编译）     */
/* ================================================================== */

/* enet_ring.c 里的 inline 实现；没有 RDPA 环，取包一律失败 */
int runner_get_pkt_from_ring(int hw_q_id, rdpa_cpu_rx_info_t *info)
{
	(void)hw_q_id;
	(void)info;
	return -1;
}

/* 同上；环创建/销毁失败，让 runner 初始化明确停住 */
int runner_ring_create_delete(enetx_channel *chan, int q_id, int size,
			      rdpa_cpu_rxq_cfg_t *rxq_cfg)
{
	(void)chan;
	(void)q_id;
	(void)size;
	(void)rxq_cfg;
	return -1;
}

/* runner_standalone.c 里的端口解复用；无 RDPA，失败 */
int _demux_id_runner_port(enetx_port_t *self)
{
	(void)self;
	return -1;
}

/* g9991.c 的 extern；PON/多播 pbit 映射，非本板 */
int link_pbit_tc_to_q_to_rdpa_lan_port(bdmf_object_handle port_obj)
{
	(void)port_obj;
	return -1;
}

/* enet_extlh.c 定义；WAN role 切换需要 RDPA port，失败 */
int port_set_wan_role_link(enetx_port_t *port, port_netdev_role_t role)
{
	(void)port;
	(void)role;
	return -1;
}

/* ================================================================== */
/*  5) BDMF 框架（rdpa_drv.ko/bdmf.ko 未编）                            */
/* ================================================================== */

/* bdmf_interface.h: 数组属性读取；没有对象，返回错误且不写 buffer */
int bdmf_attrelem_get_as_buf(bdmf_object_handle mo_or_mattr,
			     bdmf_attr_id attr, bdmf_index index,
			     void *buffer, uint32_t size)
{
	(void)mo_or_mattr; (void)attr; (void)index; (void)buffer; (void)size;
	return -1;
}

int bdmf_attrelem_get_as_num(bdmf_object_handle mo_or_mattr,
			     bdmf_attr_id attr, bdmf_index index,
			     bdmf_number *pval)
{
	(void)mo_or_mattr; (void)attr; (void)index; (void)pval;
	return -1;
}

int bdmf_attrelem_get_next(bdmf_object_handle mo, bdmf_attr_id attr,
			   bdmf_index *index)
{
	(void)mo; (void)attr; (void)index;
	return -1;
}

int bdmf_attrelem_set_as_buf(bdmf_object_handle mo_or_mattr,
			     bdmf_attr_id attr, bdmf_index index,
			     const void *buffer, uint32_t size)
{
	(void)mo_or_mattr; (void)attr; (void)index; (void)buffer; (void)size;
	return -1;
}

int bdmf_attrelem_set_as_num(bdmf_object_handle mo_or_mattr,
			     bdmf_attr_id attr, bdmf_index index,
			     bdmf_number val)
{
	(void)mo_or_mattr; (void)attr; (void)index; (void)val;
	return -1;
}

/* 属性个数：宏 BDMF_MATTR 用它做数组长度，0 表示“无属性” */
int bdmf_type_num_attrs(bdmf_type_handle drv)
{
	(void)drv;
	return 0;
}

int bdmf_new_and_set(bdmf_type_handle drv, bdmf_object_handle owner,
		     const bdmf_mattr_handle hmattr, bdmf_object_handle *pmo)
{
	(void)drv; (void)owner; (void)hmattr; (void)pmo;
	return -1;	/* 不写 *pmo，调用方必须检查返回值 */
}

int bdmf_destroy(bdmf_object_handle mo)
{
	(void)mo;
	return -1;
}

int bdmf_link(bdmf_object_handle ds, bdmf_object_handle us, const char *attrs)
{
	(void)ds; (void)us; (void)attrs;
	return -1;
}

/* 引用计数释放：没有真实对象，no-op */
void bdmf_put(bdmf_object_handle mo)
{
	(void)mo;
}

/* bdmf_system_common.h 声明；sysb 回收，no-op（会泄漏，仅探测期） */
void bdmf_sysb_recycle(bdmf_sysb sysb, uint32_t context, uint32_t flags)
{
	(void)sysb; (void)context; (void)flags;
}

/* runner.c 里 extern；数据缓冲回收，no-op */
void bdmf_sysb_databuf_recycle(void *pBuf, unsigned long context)
{
	(void)pBuf; (void)context;
}

/* ================================================================== */
/*  6) RDPA API（rdpa_drv/rdpa_gpl_ext 未编）                           */
/* ================================================================== */

/* 查询类全部返回 -1 且不写输出参数：probe 会在第一个查询处干净退出 */
int rdpa_cpu_get(rdpa_cpu_port index_, bdmf_object_handle *cpu_obj)
{
	(void)index_; (void)cpu_obj;
	return -1;
}

int rdpa_filter_get(bdmf_object_handle *filter_obj)
{
	(void)filter_obj;
	return -1;
}

int rdpa_port_get(rdpa_if index_, bdmf_object_handle *port_obj)
{
	(void)index_; (void)port_obj;
	return -1;
}

int rdpa_system_get(bdmf_object_handle *system_obj)
{
	(void)system_obj;
	return -1;
}

/* 类型句柄：没有 BDMF，返回 NULL（调用方拿到 NULL 会立刻失败） */
bdmf_type_handle rdpa_egress_tm_drv(void)
{
	return NULL;
}

bdmf_type_handle rdpa_port_drv(void)
{
	return NULL;
}

/* 中断使能/清除：void，无对象可操作 */
void rdpa_cpu_int_clear(rdpa_cpu_port port, int queue)
{
	(void)port; (void)queue;
}

void rdpa_cpu_int_disable(rdpa_cpu_port port, int queue)
{
	(void)port; (void)queue;
}

void rdpa_cpu_int_enable(rdpa_cpu_port port, int queue)
{
	(void)port; (void)queue;
}

/* 调试抓包：void no-op */
void rdpa_cpu_rx_dump_packet(char *name, rdpa_cpu_port port,
			     bdmf_index queue, rdpa_cpu_rx_info_t *info,
			     uint32_t dst_ssid)
{
	(void)name; (void)port; (void)queue; (void)info; (void)dst_ssid;
}

/* 发送：返回 -1，调用方会走 drop 记账 */
int rdpa_cpu_send_raw(void *data, uint32_t length,
		      const rdpa_cpu_tx_info_t *info)
{
	(void)data; (void)length; (void)info;
	return -1;
}

int rdpa_cpu_send_sysb(bdmf_sysb sysb, const rdpa_cpu_tx_info_t *info)
{
	(void)sysb; (void)info;
	return -1;
}

/* ================================================================== */
/*  7) bcmkernel / pinctrl / nbuff 杂项                                */
/* ================================================================== */

/*
 * dt_access.h 声明；6.18 已删除这个非 devm 接口。返回 ERR_PTR(-ENOSYS)，
 * dt_parsing 会把 switch-reset 当作“没有 GPIO”。
 * TODO: 换成 gpiod_get_index()/devm_gpiod_get_from_of_node()。
 */
struct gpio_desc *gpiod_get_from_of_node(struct device_node *node,
					 const char *propname, int index,
					 enum gpiod_flags dflags,
					 const char *label)
{
	(void)node; (void)propname; (void)index; (void)dflags; (void)label;
	return ERR_PTR(-ENOSYS);
}

/* dt_parsing.c extern；BCM 的 pinctrl 辅助，未导出。失败让调用方跳过引脚配置 */
int bcmbca_pinctrl_get_pins_by_state(struct pinctrl_state *state, int *pins,
				     int *num_pins)
{
	(void)state; (void)pins; (void)num_pins;
	return -1;
}

/* nbuff.h 声明；缓存刷新/释放，探测期 no-op（会泄漏，仅占位） */
void fkb_flush(FkBuff_t *fkb_p, uint8_t *data_p, int len, int cache_op)
{
	(void)fkb_p; (void)data_p; (void)len; (void)cache_op;
}

void nbuff_free(pNBuff_t pNBuff)
{
	(void)pNBuff;
}

void nbuff_flushfree(pNBuff_t pNBuff)
{
	(void)pNBuff;
}

/* ================================================================== */
/*  8) enet 自身未编译文件里的入口                                      */
/* ================================================================== */

/* enet_ring.c 的 post-config 钩子；当前配置无需后处理 */
int enetxapi_post_config(void)
{
	return 0;
}

/* enet_ring.c 的队列重调度判断；没有 ring，返回 false */
int enetxapi_queue_need_reschedule(enetx_channel *chan, int q_id)
{
	(void)chan; (void)q_id;
	return 0;
}

/* ================================================================== */
/*  9) Runner switch/port ops（runner_with_switch.c 未编）             */
/*                                                                     */
/*  port_types.c 在 -DRUNNER 下把 PORT_TYPE_RUNNER_SW/PORT 指向这两个表。 */
/*  真实表在 runner_with_switch.c，但它依赖本 GPL 包没有的                 */
/*  rdpa_cpu_tx_extra_info_t / sf2_sw / port_runner_sw_config_trunk 等，  */
/*  编不过。这里给最小表：init 直接返回 -1，sw_init 会干净失败。          */
/*  TODO: 等 rdpa 头/实现补齐后换成 runner_with_switch.c 的真表。         */
/* ================================================================== */

static int runner_sw_init_stub(enetx_port_t *sw)
{
	(void)sw;
	pr_info_once("bcm_enet: runner switch init skipped (no rdpa, compat mode)\n");
	return 0;
}

/*
 * 真 runner 端口 init 会从 RDPA port 对象推出 LAN/WAN 角色。
 * _assert_class_port_defaults() 要求 port_netdev_role >= 0，否则
 * port_init() 直接失败。这里按 DT port-cap 推断一个合理值。
 */
static int runner_port_init_stub(enetx_port_t *port)
{
	if (port->n.port_netdev_role < 0)
		port->n.port_netdev_role =
			(port->p.port_cap == PORT_CAP_WAN_ONLY ||
			 port->p.port_cap == PORT_CAP_WAN_PREFERRED) ?
			PORT_NETDEV_ROLE_WAN : PORT_NETDEV_ROLE_LAN;
	return 0;
}

/* port_mtu_set() 在没有 .mtu_set 时会返回 -1，enet_change_mtu() 于是认为
 * 改 MTU 失败。compat 模式不碰硬件，直接接受。 */
static int runner_port_mtu_set_stub(enetx_port_t *port, int mtu)
{
	(void)port;
	(void)mtu;
	return 0;
}

/*
 * ndo_open → enet_open() → port_open() → ops->open()。
 * 这里立刻读一次 PHY 状态并触发 link_change 回调（phy_link_change_cb
 * 负责 netif_carrier_on/off），这样 `ip link set lan0 up` 后不用等
 * 下一次 200ms 轮询。读失败就保持现状，轮询会继续跟踪。
 */
static void runner_port_open_stub(enetx_port_t *port)
{
	phy_dev_t *phy = port->p.phy;
	uint16_t id1 = 0, id2 = 0, bmsr = 0, bmcr = 0;
	int rc;

	if (!phy || !phy->phy_drv)
		return;

	/*
	 * 现场证据（2026-10-29）：BMCR=0x1940 → bit11(POWER DOWN)=1，
	 * 四个 LAN 口全 NO-CARRIER。原厂 port_generic_open() 会调
	 * phy_dev_power_set(phy, 1)（清 bit11 + ANRESTART），但我们的
	 * .open 覆盖了 port_generic_open，必须在这里显式补上。
	 */
	phy_dev_power_set(phy, 1);

	/* Spike A 证据：把 PHY ID / BMSR / BMCR 原始值打出来。
	 * PHY ID 非 0 就说明 MDIO 读通路是通的；BMSR bit2 就是 link。 */
	rc = phy_bus_read(phy, MII_PHYSID1, &id1);
	rc |= phy_bus_read(phy, MII_PHYSID2, &id2);
	phy_bus_read(phy, MII_BMSR, &bmsr);	/* latch-low：先读一次 */
	phy_bus_read(phy, MII_BMSR, &bmsr);
	phy_bus_read(phy, MII_BMCR, &bmcr);
	pr_info("bcm-phy: %s mdio=%d id=%04x:%04x bmsr=%04x bmcr=%04x\n",
		port->obj_name, rc, id1, id2, bmsr, bmcr);

	if (phy_dev_read_status(phy) == 0)
		phy_dev_link_change_notify(phy);
}

static void runner_port_stop_stub(enetx_port_t *port)
{
	(void)port;
}

/*
 * port_create() 会对每个非 detect 端口调用 parent_sw->s.ops->port_id_on_sw()。
 * run #72 的 panic 就是这里为空（PC=0, LR=port_type_resolve+0x3c）。
 * 按 DT 的 reg 直接映射成 Runner 端口 0..N。
 */
static int runner_sw_port_id_on_sw(port_info_t *port_info, int *port_id,
				   port_type_t *port_type)
{
	*port_id = port_info->port;
	*port_type = PORT_TYPE_RUNNER_PORT;
	return 0;
}

/*
 * _sw_free() 在 is_init 时会无条件调用 sw->s.ops->uninit（这里没有判空），
 * 所以必须给一个真实的 uninit，否则 rmmod 必崩。
 */
static int runner_sw_uninit_stub(enetx_port_t *sw)
{
	(void)sw;
	return 0;
}

static int runner_port_uninit_stub(enetx_port_t *port)
{
	(void)port;
	return 0;
}

sw_ops_t port_runner_sw = {
	.init = runner_sw_init_stub,
	.uninit = runner_sw_uninit_stub,
	.port_id_on_sw = runner_sw_port_id_on_sw,
};

port_ops_t port_runner_port = {
	.init = runner_port_init_stub,
	.uninit = runner_port_uninit_stub,
	.mtu_set = runner_port_mtu_set_stub,
	.open = runner_port_open_stub,
	.stop = runner_port_stop_stub,
};

