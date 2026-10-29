/*
 * phy_drv_compat.c — Broadcom BCM96855 phy_drv 框架的最小兼容实现
 *
 * 背景（2026-10-29）：
 *   bcm_enet 驱动硬依赖 vendor 的 PHY 框架（phy_drv_find_device / phy_dev_init /
 *   phy_dev_power_set / phy_register_polling_timer …）。原厂把这套框架以
 *   phy/prebuilt/phy_drv.o 的形式发布（无源码），但**接口契约是完整公开的**：
 *
 *     · phy_drv.h 里 struct phy_dev_s / struct phy_drv_s（含完整 ops 表）全部可见
 *     · 86 个 API 包装函数在头文件里是 static inline，已经给好了
 *     · 只有 20 个函数是「非 inline 声明」，需要实现 —— 就是本文件
 *
 *   语义按头文件里 inline 的调用方式反推；有疑义处参考原厂 4.19.235 的
 *   bcm_enet.ko（rootfs2 的 /lib/modules/4.19.235/extra/，177 KB，可反汇编）。
 *   EGPHY/SGMII 的真实寄存器操作不在这里——那些在 phy_drv_6846_egphy.c /
 *   phy_drv_6856_sgmii.c 里，本来就有源码。
 *
 * 设计取舍：
 *   · phy_dev_t 里没有 list_head，也不该改公开头文件 → 旁挂一个 phy_compat_dev
 *     容器（链表节点 + 轮询用的 delayed_work + 回调 + 上次链路状态）。
 *   · 一把全局 mutex 兜住注册表：PHY 数量是个位数，不值得分级锁。
 *   · 本文件编进 bcm_enet.ko（多目标模块），所以没有 module_init/exit。
 *
 * 现阶段刻意**不依赖** bus_drv_get() / dt_access.c：先把簿记层编进模块，
 * 让 probe 能走到下一步，再按报错把总线与 DTS 解析接上（见文件末尾 TODO）。
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/mii.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/stdarg.h>

#include "phy_drv.h"
#include "phy_drv_compat.h"

/* 链路轮询间隔：厂商值未知，先用 200ms —— 够快，又不至于每秒几十次唤醒 */
#define PHY_COMPAT_POLL_MS	200

/* ------------------------------------------------------------------ */
/*  容器结构                                                           */
/* ------------------------------------------------------------------ */

struct phy_compat_dev {
	struct list_head node;
	phy_dev_t *phy;

	/* 链路轮询 */
	struct delayed_work poll;
	int polling;
	int last_link;
	phy_speed_t last_speed;
	phy_duplex_t last_duplex;

	/* link_change 回调 */
	link_change_cb_t cb;
	void *cb_ctx;

	/* phy_dev_queue_work 用 */
	struct work_struct work;
	phy_dev_work_func_t work_func;
};

static LIST_HEAD(compat_dev_list);
static DEFINE_MUTEX(compat_lock);

static phy_drv_t *phy_drv_table[PHY_TYPE_MAX];
static DEFINE_MUTEX(phy_drv_lock);

static struct workqueue_struct *phy_wq;

/* 由 MDIO 驱动（后续接上时）调用，把总线挂到 compat 层 */
static bus_drv_t *phy_compat_bus;

static struct phy_compat_dev *compat_find(phy_dev_t *phy)
{
	struct phy_compat_dev *cd;

	list_for_each_entry(cd, &compat_dev_list, node)
		if (cd->phy == phy)
			return cd;
	return NULL;
}

static phy_dev_t *compat_find_by_handle(dt_handle_t handle)
{
	struct phy_compat_dev *cd;

	if (!handle)
		return NULL;

	mutex_lock(&compat_lock);
	list_for_each_entry(cd, &compat_dev_list, node) {
		if (cd->phy->dt_handle == handle) {
			mutex_unlock(&compat_lock);
			return cd->phy;
		}
	}
	mutex_unlock(&compat_lock);
	return NULL;
}

/* ------------------------------------------------------------------ */
/*  PHY 框架的总线入口                                                  */
/*                                                                     */
/*  原厂 bus_drv.c 只做「按类型查表」，表项由 bus_drv_mdio_v1.c 填。    */
/*  我们暂时没有 MDIO 驱动，先给出同样的入口，总线为空时返回 NULL，    */
/*  phy_bus_read/write 自然退化为 no-op（不会踩寄存器）。               */
/* ------------------------------------------------------------------ */

bus_drv_t *bus_drv_get(bus_type_t bus_type)
{
	if (bus_type == BUS_TYPE_MDIO_V1_INT)
		return phy_compat_bus;
	return NULL;
}

void phy_compat_set_bus(bus_drv_t *bus)
{
	phy_compat_bus = bus;
}

/* ------------------------------------------------------------------ */
/*  PHY 框架占位驱动                                                    */
/*                                                                     */
/*  原厂的 phy_drv_6846_egphy.c / phy_drv_6856_sgmii.c 有源码，但它们  */
/*  依赖 phy_drv_brcm.o（prebuilt，且是 aarch64，本板 armv7 用不了）， */
/*  所以这里按「标准 MII 寄存器」给一套最小 read_status：                */
/*    · 读 BMSR 判 link（latch-low，读两次）                             */
/*    · 读 BMCR 判 speed/duplex（自协商完成后 BMCR 反映当前速率）        */
/*  真正的 EGPHY 影子寄存器/EEE/AFE 校准留给后续移植，不影响 switch0     */
/*  建成与端口 up/down 的基本判定。                                     */
/* ------------------------------------------------------------------ */

static int phy_compat_read_status(phy_dev_t *phy)
{
	uint16_t bmsr = 0, bmcr = 0;
	int rc;

	if (!phy)
		return -EINVAL;

	if (!phy->bus_drv) {
		phy->link = 0;
		phy->speed = PHY_SPEED_UNKNOWN;
		phy->duplex = PHY_DUPLEX_UNKNOWN;
		return -ENODEV;
	}

	rc = phy_bus_read(phy, MII_BMSR, &bmsr);
	if (rc)
		return rc;
	/* BMSR 的 link 位是 latch-low：第一次读清掉旧值，第二次才是当前值 */
	phy_bus_read(phy, MII_BMSR, &bmsr);
	phy_bus_read(phy, MII_BMCR, &bmcr);

	if (!(bmsr & BMSR_LSTATUS)) {
		phy->link = 0;
		phy->speed = PHY_SPEED_UNKNOWN;
		phy->duplex = PHY_DUPLEX_UNKNOWN;
		return 0;
	}

	phy->link = 1;
	phy->duplex = (bmcr & BMCR_FULLDPLX) ? PHY_DUPLEX_FULL : PHY_DUPLEX_HALF;
	if (bmcr & BMCR_SPEED1000)
		phy->speed = PHY_SPEED_1000;
	else if (bmcr & BMCR_SPEED100)
		phy->speed = PHY_SPEED_100;
	else
		phy->speed = PHY_SPEED_10;

	return 0;
}

static phy_drv_t phy_drv_compat_egphy = {
	.phy_type = PHY_TYPE_6846_EGPHY,
	.name = "EGPHY",
	.read_status = phy_compat_read_status,
};

static phy_drv_t phy_drv_compat_sgmii = {
	.phy_type = PHY_TYPE_6856_SGMII,
	.name = "SGMII",
	.read_status = phy_compat_read_status,
};

/* DTS 里的 phy-type 字符串 → phy_type_t（与厂商 phy dt_parsing 一致） */
static const struct {
	const char *name;
	phy_type_t type;
} phy_compat_type_map[] = {
	{ "EGPHY",		PHY_TYPE_6846_EGPHY },
	{ "SGMII",		PHY_TYPE_6856_SGMII },
	{ "PON",		PHY_TYPE_PON },
	{ "EXT1",		PHY_TYPE_EXT1 },
	{ "EXT3",		PHY_TYPE_EXT3 },
	{ "158CLASS_SERDES",	PHY_TYPE_158CLASS_SERDES },
};

static phy_type_t phy_compat_type_from_name(const char *name)
{
	int i;

	if (!name)
		return PHY_TYPE_UNKNOWN;

	for (i = 0; i < ARRAY_SIZE(phy_compat_type_map); i++)
		if (!strcmp(name, phy_compat_type_map[i].name))
			return phy_compat_type_map[i].type;

	return PHY_TYPE_UNKNOWN;
}

/*
 * 按 DTS 节点创建 PHY 对象（对应厂商 phy/dt_parsing.c 的 phy_dev_dt_probe）。
 * 只在 phy_drv_find_device() 找不到时按需创建，避免依赖 phy 框架的
 * late_initcall 探测顺序。
 */
static phy_dev_t *phy_compat_dt_create(dt_handle_t handle)
{
	const char *type_name = NULL;
	struct device_node *parent;
	phy_type_t type;
	phy_dev_t *phy;
	uint32_t addr = 0;

	if (!handle || !of_device_is_compatible(handle, "brcm,bcaphy"))
		return NULL;

	if (of_property_read_string(handle, "phy-type", &type_name))
		return NULL;

	type = phy_compat_type_from_name(type_name);
	if (type == PHY_TYPE_UNKNOWN) {
		pr_warn("bcm-phy: unsupported phy-type '%s' on %pOF\n",
			type_name, handle);
		return NULL;
	}

	of_property_read_u32(handle, "reg", &addr);

	phy = phy_dev_add(type, addr, NULL);
	if (!phy) {
		pr_info("bcm-phy: no driver registered for %s (type %d)\n",
			type_name, type);
		return NULL;
	}

	phy->dt_handle = handle;

	/* mdio 节点的 bus-type 目前只有 MDIO_V1；没有总线驱动时 bus 为 NULL */
	parent = of_get_parent(handle);
	if (parent) {
		const char *bus_type = NULL;

		if (!of_property_read_string(parent, "bus-type", &bus_type) &&
		    !strcmp(bus_type, "MDIO_V1"))
			phy->bus_drv = bus_drv_get(BUS_TYPE_MDIO_V1_INT);
		of_node_put(parent);
	}

	pr_info("bcm-phy: %s addr %u from %pOF\n",
		phy->name, phy->addr, handle);

	return phy;
}

/* ------------------------------------------------------------------ */
/*  链路轮询与回调                                                      */
/* ------------------------------------------------------------------ */

static void phy_compat_poll_work(struct work_struct *work)
{
	struct delayed_work *dw = to_delayed_work(work);
	struct phy_compat_dev *cd = container_of(dw, struct phy_compat_dev, poll);
	phy_dev_t *phy = cd->phy;

	if (!phy || !phy->phy_drv)
		return;

	/* 具体 PHY 驱动自己读状态寄存器并更新 phy->link/speed/duplex */
	if (phy_dev_read_status(phy))
		goto out;

	if (phy->link != cd->last_link ||
	    phy->speed != cd->last_speed ||
	    phy->duplex != cd->last_duplex) {
		cd->last_link = phy->link;
		cd->last_speed = phy->speed;
		cd->last_duplex = phy->duplex;
		phy_dev_link_change_notify(phy);
	}

out:
	if (cd->polling)
		queue_delayed_work(phy_wq, &cd->poll,
				   msecs_to_jiffies(PHY_COMPAT_POLL_MS));
}

static void phy_compat_work(struct work_struct *work)
{
	struct phy_compat_dev *cd = container_of(work, struct phy_compat_dev, work);

	if (cd->work_func && cd->phy)
		cd->work_func(cd->phy);
}

void phy_dev_link_change_register(phy_dev_t *phy_dev, link_change_cb_t cb, void *ctx)
{
	struct phy_compat_dev *cd;

	if (!phy_dev)
		return;

	mutex_lock(&compat_lock);
	cd = compat_find(phy_dev);
	mutex_unlock(&compat_lock);
	if (!cd)
		return;

	cd->cb = cb;
	cd->cb_ctx = ctx;
	phy_dev->link_change_cb = cb;
	phy_dev->link_change_ctx = ctx;

	/* 记下当前状态，避免注册后第一次轮询就误报一次 link 变化 */
	cd->last_link = phy_dev->link;
	cd->last_speed = phy_dev->speed;
	cd->last_duplex = phy_dev->duplex;

	/* 具体 PHY 驱动若自带定时器，交给它；否则用 compat 的轮询 */
	if (phy_dev->phy_drv && phy_dev->phy_drv->link_change_register) {
		if (!phy_dev->phy_drv->link_change_register(phy_dev))
			return;
	}

	if (!cd->polling && phy_wq) {
		cd->polling = 1;
		INIT_DELAYED_WORK(&cd->poll, phy_compat_poll_work);
		queue_delayed_work(phy_wq, &cd->poll,
				   msecs_to_jiffies(PHY_COMPAT_POLL_MS));
	}
}

void phy_dev_link_change_unregister(phy_dev_t *phy_dev)
{
	struct phy_compat_dev *cd;

	if (!phy_dev)
		return;

	mutex_lock(&compat_lock);
	cd = compat_find(phy_dev);
	mutex_unlock(&compat_lock);
	if (!cd)
		return;

	cd->polling = 0;
	cd->cb = NULL;
	cd->cb_ctx = NULL;
	phy_dev->link_change_cb = NULL;
	phy_dev->link_change_ctx = NULL;
	if (cd->poll.work.func)
		cancel_delayed_work_sync(&cd->poll);

	if (phy_dev->phy_drv && phy_dev->phy_drv->link_change_unregister)
		phy_dev->phy_drv->link_change_unregister(phy_dev);
}

void phy_dev_link_change_notify(phy_dev_t *phy_dev)
{
	struct phy_compat_dev *cd;
	link_change_cb_t cb = NULL;
	void *ctx = NULL;

	if (!phy_dev)
		return;

	/* link_change_cb/ctx 是厂商结构体里的正式字段，优先用它们 */
	cb = phy_dev->link_change_cb;
	ctx = phy_dev->link_change_ctx;

	if (!cb) {
		mutex_lock(&compat_lock);
		cd = compat_find(phy_dev);
		if (cd) {
			cb = cd->cb;
			ctx = cd->cb_ctx;
		}
		mutex_unlock(&compat_lock);
	}

	if (!cb)
		return;
	cb(ctx ? ctx : phy_dev);
}

void phy_dev_force_link_reset(phy_dev_t *phy_dev)
{
	struct phy_compat_dev *cd;

	if (!phy_dev)
		return;

	phy_dev->link = 0;
	phy_dev->speed = PHY_SPEED_UNKNOWN;
	phy_dev->duplex = PHY_DUPLEX_UNKNOWN;

	mutex_lock(&compat_lock);
	cd = compat_find(phy_dev);
	if (cd) {
		cd->last_link = 0;
		cd->last_speed = PHY_SPEED_UNKNOWN;
		cd->last_duplex = PHY_DUPLEX_UNKNOWN;
	}
	mutex_unlock(&compat_lock);
}

int phy_dev_queue_work(phy_dev_t *phy_dev, phy_dev_work_func_t func)
{
	struct phy_compat_dev *cd;

	if (!phy_dev || !func)
		return -EINVAL;
	if (!phy_wq)
		return -ENODEV;

	mutex_lock(&compat_lock);
	cd = compat_find(phy_dev);
	if (cd) {
		cd->work_func = func;
		INIT_WORK(&cd->work, phy_compat_work);
	}
	mutex_unlock(&compat_lock);

	if (!cd)
		return -ENODEV;

	queue_work(phy_wq, &cd->work);
	return 0;
}

/* ------------------------------------------------------------------ */
/*  驱动注册表                                                          */
/* ------------------------------------------------------------------ */

int phy_driver_set(phy_drv_t *phy_drv)
{
	if (!phy_drv || phy_drv->phy_type <= PHY_TYPE_UNKNOWN ||
	    phy_drv->phy_type >= PHY_TYPE_MAX)
		return -EINVAL;

	mutex_lock(&phy_drv_lock);
	phy_drv_table[phy_drv->phy_type] = phy_drv;
	mutex_unlock(&phy_drv_lock);
	return 0;
}

int phy_driver_init(phy_type_t phy_type)
{
	phy_drv_t *drv;
	int rc = 0;

	if (phy_type <= PHY_TYPE_UNKNOWN || phy_type >= PHY_TYPE_MAX)
		return -EINVAL;

	mutex_lock(&phy_drv_lock);
	drv = phy_drv_table[phy_type];
	mutex_unlock(&phy_drv_lock);

	if (!drv || drv->initialized)
		return 0;

	if (drv->drv_init)
		rc = drv->drv_init(drv);
	if (!rc)
		drv->initialized = 1;
	return rc;
}

int phy_drivers_init(void)
{
	int i, rc = 0;

	for (i = PHY_TYPE_UNKNOWN + 1; i < PHY_TYPE_MAX; i++)
		if (phy_drv_table[i])
			rc |= phy_driver_init(i);
	return rc;
}

/* 原厂用它按板型挑驱动顺序；我们单一板型，退化为「全部初始化一遍」 */
int phy_drivers_set(void)
{
	return phy_drivers_init();
}

/* ------------------------------------------------------------------ */
/*  设备生命周期                                                        */
/* ------------------------------------------------------------------ */

phy_dev_t *phy_dev_get(phy_type_t phy_type, uint32_t addr)
{
	struct phy_compat_dev *cd;
	phy_dev_t *found = NULL;

	mutex_lock(&compat_lock);
	list_for_each_entry(cd, &compat_dev_list, node) {
		if (cd->phy->phy_drv &&
		    cd->phy->phy_drv->phy_type == phy_type &&
		    cd->phy->addr == addr) {
			found = cd->phy;
			break;
		}
	}
	mutex_unlock(&compat_lock);
	return found;
}

phy_dev_t *phy_dev_add(phy_type_t phy_type, uint32_t addr, void *priv)
{
	struct phy_compat_dev *cd;
	phy_drv_t *drv;
	phy_dev_t *phy;

	if (phy_type <= PHY_TYPE_UNKNOWN || phy_type >= PHY_TYPE_MAX)
		return NULL;

	mutex_lock(&phy_drv_lock);
	drv = phy_drv_table[phy_type];
	mutex_unlock(&phy_drv_lock);
	if (!drv)
		return NULL;

	cd = kzalloc(sizeof(*cd), GFP_KERNEL);
	if (!cd)
		return NULL;
	phy = kzalloc(sizeof(*phy), GFP_KERNEL);
	if (!phy) {
		kfree(cd);
		return NULL;
	}

	phy->phy_drv = drv;
	phy->addr = addr;
	phy->priv = priv;
	phy->mii_type = PHY_MII_TYPE_UNKNOWN;	/* phy_dev_attach() 里确定 */
	phy->speed = PHY_SPEED_UNKNOWN;
	phy->duplex = PHY_DUPLEX_UNKNOWN;
	phy->link = 0;
	if (drv->name)
		strscpy(phy->name, drv->name, PHY_NAME_LEN);

	cd->phy = phy;

	mutex_lock(&compat_lock);
	list_add_tail(&cd->node, &compat_dev_list);
	mutex_unlock(&compat_lock);

	return phy;
}

int phy_dev_del(phy_dev_t *phy_dev)
{
	struct phy_compat_dev *cd = NULL, *iter;

	if (!phy_dev)
		return -EINVAL;

	mutex_lock(&compat_lock);
	list_for_each_entry(iter, &compat_dev_list, node) {
		if (iter->phy == phy_dev) {
			cd = iter;
			list_del(&cd->node);
			break;
		}
	}
	mutex_unlock(&compat_lock);

	if (!cd)
		return -ENOENT;

	cd->polling = 0;
	/* 从未 INIT_WORK 过的 work 直接 cancel 会触发 __flush_work 的 WARN */
	if (cd->poll.work.func)
		cancel_delayed_work_sync(&cd->poll);
	if (cd->work.func)
		cancel_work_sync(&cd->work);
	kfree(cd);
	kfree(phy_dev);
	return 0;
}

phy_dev_t *phy_drv_find_device(dt_handle_t handle)
{
	phy_dev_t *found;

	if (!handle)
		return NULL;

	found = compat_find_by_handle(handle);
	if (found)
		return found;

	/* 没注册过就看它是不是 DTS 里的 brcm,bcaphy，按需创建 */
	return phy_compat_dt_create(handle);
}

/*
 * 把 PHY 挂到 MII 总线上并设定接法。
 *   phy_mode    : phy_mii_type_t（gmii/rgmii/hsgmii/xfi…），来自 DTS 的 phy-mode
 *   delay_rx/tx : RGMII 时钟延迟（ns）
 *   instance    : 同一驱动多实例时区分（EGPHY 的 core/lane 索引）
 *
 * 总线目前只有 MDIO_V1（bus_drv_mdio_v1.c 还没编进来），拿不到总线时
 * bus_drv 为 NULL，phy_bus_read/write 退化为 no-op —— 宁可读不到寄存器，
 * 也不要在 bring-up 阶段乱写 PHY。
 */
void phy_dev_attach(phy_dev_t *phy_dev, uint32_t phy_mode, int delay_rx,
		    int delay_tx, int instance)
{
	if (!phy_dev)
		return;

	phy_dev->mii_type = (phy_mii_type_t)phy_mode;
	phy_dev->delay_rx = delay_rx;
	phy_dev->delay_tx = delay_tx;

	if (instance >= 0) {
		phy_dev->core_index = instance;
		phy_dev->lane_index = instance;
	}

	if (!phy_dev->bus_drv)
		phy_dev->bus_drv = bus_drv_get(BUS_TYPE_MDIO_V1_INT);

	/* 厂商语义：attach 之后让驱动做一次按端口的初始化准备 */
	if (phy_drv_dev_add(phy_dev))
		pr_warn("bcm-phy: %s dev_add failed\n", phy_dev->name);
}

/* ------------------------------------------------------------------ */
/*  调试与工具                                                          */
/* ------------------------------------------------------------------ */

char *phy_dev_mii_type_to_str(phy_mii_type_t mii_type)
{
	return (char *)phy_mii_type(mii_type);
}

char *phy_dev_speed_to_str(phy_speed_t speed)
{
	switch (speed) {
	case PHY_SPEED_10:	return "10";
	case PHY_SPEED_100:	return "100";
	case PHY_SPEED_1000:	return "1000";
	case PHY_SPEED_2500:	return "2500";
	case PHY_SPEED_5000:	return "5000";
	case PHY_SPEED_10000:	return "10000";
	default:		return "auto";
	}
}

char *phy_dev_duplex_to_str(phy_duplex_t duplex)
{
	switch (duplex) {
	case PHY_DUPLEX_HALF:	return "half";
	case PHY_DUPLEX_FULL:	return "full";
	default:		return "unknown";
	}
}

char *phy_dev_flowctrl_to_str(int pause_rx, int pause_tx)
{
	if (pause_rx && pause_tx)
		return "rx/tx";
	if (pause_rx)
		return "rx";
	if (pause_tx)
		return "tx";
	return "off";
}

void phy_dev_print_status(phy_dev_t *phy_dev)
{
	if (!phy_dev)
		return;

	pr_info("bcm-phy %-14s addr=%u link=%d speed=%s duplex=%s flowctrl=%s%s\n",
		phy_dev->name, phy_dev->addr, phy_dev->link,
		phy_dev_speed_to_str(phy_dev->speed),
		phy_dev_duplex_to_str(phy_dev->duplex),
		phy_dev_flowctrl_to_str(phy_dev->pause_rx, phy_dev->pause_tx),
		(phy_dev->flag & PHY_FLAG_INITED) ? "" : " (not inited)");
}

/* 按序列表批量写 PHY 寄存器（prog_entry_t 以 desc == NULL 结尾） */
void phy_dev_prog(phy_dev_t *phy_dev, prog_entry_t *prog_entry)
{
	if (!phy_dev || !phy_dev->phy_drv)
		return;

	for (; prog_entry && prog_entry->desc; prog_entry++) {
		if (phy_dev_write(phy_dev, prog_entry->reg, prog_entry->val))
			pr_warn("bcm-phy: %s prog '%s' failed (reg 0x%04x)\n",
				phy_dev->name, prog_entry->desc, prog_entry->reg);
	}
}

/* 厂商私有操作入口：DSL/SERDES 才会实现 priv_fun，EGPHY 为 NULL */
int phy_priv_fun(phy_dev_t *phy_dev, int op, ...)
{
	va_list ap;
	int rc;

	if (!phy_dev || !phy_dev->phy_drv || !phy_dev->phy_drv->priv_fun)
		return -EOPNOTSUPP;

	va_start(ap, op);
	rc = phy_dev->phy_drv->priv_fun(phy_dev, op, ap);
	va_end(ap);
	return rc;
}

/* ------------------------------------------------------------------ */
/*  初始化 / 退出（由 bcm_enet.ko 的 module_init 调用）                  */
/* ------------------------------------------------------------------ */

int phy_drv_compat_init(void)
{
	if (phy_wq)
		return 0;

	phy_wq = alloc_ordered_workqueue("bcm_phy", 0);
	if (!phy_wq)
		return -ENOMEM;

	/*
	 * 原厂 PHY 驱动模块（bcm_phy.ko）会自己 phy_driver_set()；我们没有
	 * 源码、prebuilt 又是 aarch64，先注册两个标准 MII 占位驱动，保证
	 * phy_dev_add() 能建出对象、DTS 端口能解析下去。
	 */
	mutex_lock(&phy_drv_lock);
	if (!phy_drv_table[PHY_TYPE_6846_EGPHY])
		phy_drv_table[PHY_TYPE_6846_EGPHY] = &phy_drv_compat_egphy;
	if (!phy_drv_table[PHY_TYPE_6856_SGMII])
		phy_drv_table[PHY_TYPE_6856_SGMII] = &phy_drv_compat_sgmii;
	mutex_unlock(&phy_drv_lock);

	return 0;
}

void phy_drv_compat_exit(void)
{
	struct phy_compat_dev *cd, *tmp;
	LIST_HEAD(dead);

	if (!phy_wq)
		return;

	/* 清掉我们注册的占位驱动，避免模块重新加载后表里留着旧地址 */
	mutex_lock(&phy_drv_lock);
	if (phy_drv_table[PHY_TYPE_6846_EGPHY] == &phy_drv_compat_egphy)
		phy_drv_table[PHY_TYPE_6846_EGPHY] = NULL;
	if (phy_drv_table[PHY_TYPE_6856_SGMII] == &phy_drv_compat_sgmii)
		phy_drv_table[PHY_TYPE_6856_SGMII] = NULL;
	mutex_unlock(&phy_drv_lock);

	/*
	 * 不能持 compat_lock 调 cancel_delayed_work_sync()：轮询回调里会走
	 * phy_dev_link_change_notify() → compat_lock，持锁等待会死锁。
	 * 先把节点摘到局部链表，解锁后再停 work、释放。
	 */
	mutex_lock(&compat_lock);
	list_for_each_entry_safe(cd, tmp, &compat_dev_list, node) {
		cd->polling = 0;
		list_move(&cd->node, &dead);
	}
	mutex_unlock(&compat_lock);

	list_for_each_entry_safe(cd, tmp, &dead, node) {
		if (cd->poll.work.func)
			cancel_delayed_work_sync(&cd->poll);
		if (cd->work.func)
			cancel_work_sync(&cd->work);
		list_del(&cd->node);
		kfree(cd->phy);
		kfree(cd);
	}

	destroy_workqueue(phy_wq);
	phy_wq = NULL;
}
