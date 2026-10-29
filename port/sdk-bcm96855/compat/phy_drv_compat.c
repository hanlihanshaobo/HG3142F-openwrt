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

#include "phy_drv.h"

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

static struct phy_compat_dev *compat_find(phy_dev_t *phy)
{
	struct phy_compat_dev *cd;

	list_for_each_entry(cd, &compat_dev_list, node)
		if (cd->phy == phy)
			return cd;
	return NULL;
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
	if (phy->phy_drv->read_status)
		phy->phy_drv->read_status(phy);

	if (phy->link != cd->last_link ||
	    phy->speed != cd->last_speed ||
	    phy->duplex != cd->last_duplex) {
		cd->last_link = phy->link;
		cd->last_speed = phy->speed;
		cd->last_duplex = phy->duplex;
		phy_dev_link_change_notify(phy);
	}

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

	/* 记下当前状态，避免注册后第一次轮询就误报一次 link 变化 */
	cd->last_link = phy_dev->link;
	cd->last_speed = phy_dev->speed;
	cd->last_duplex = phy_dev->duplex;

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
	cancel_delayed_work_sync(&cd->poll);
}

void phy_dev_link_change_notify(phy_dev_t *phy_dev)
{
	struct phy_compat_dev *cd;
	link_change_cb_t cb = NULL;
	void *ctx = NULL;

	if (!phy_dev)
		return;

	mutex_lock(&compat_lock);
	cd = compat_find(phy_dev);
	if (cd) {
		cb = cd->cb;
		ctx = cd->cb_ctx;
	}
	mutex_unlock(&compat_lock);

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
	cancel_delayed_work_sync(&cd->poll);
	cancel_work_sync(&cd->work);
	kfree(cd);
	kfree(phy_dev);
	return 0;
}

phy_dev_t *phy_drv_find_device(dt_handle_t handle)
{
	struct phy_compat_dev *cd;
	phy_dev_t *found = NULL;

	if (!handle)
		return NULL;

	mutex_lock(&compat_lock);
	list_for_each_entry(cd, &compat_dev_list, node) {
		if (cd->phy->dt_handle == handle) {
			found = cd->phy;
			break;
		}
	}
	mutex_unlock(&compat_lock);
	return found;
}

/*
 * 把 PHY 挂到 MII 总线上并设定接法。
 *   phy_mode    : phy_mii_type_t（gmii/rgmii/hsgmii/xfi…），来自 DTS 的 phy-mode
 *   delay_rx/tx : RGMII 时钟延迟（ns）
 *   instance    : 同一驱动多实例时区分（EGPHY 的 core/lane 索引）
 *
 * TODO(下一轮)：这里应该按 DTS 的 mdio 节点找已注册总线
 *   （bus_drv_get(BUS_TYPE_MDIO_V1_INT) 或对应 bus_drv_mdio_v1.c 注册的那条），
 *   目前先留空 —— 总线接上之前，PHY 的 read/write 只能靠驱动自己的 ops。
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

/* ------------------------------------------------------------------ */
/*  初始化 / 退出（由 bcm_enet.ko 的 module_init 调用）                  */
/* ------------------------------------------------------------------ */

int phy_drv_compat_init(void)
{
	phy_wq = alloc_ordered_workqueue("bcm_phy", 0);
	if (!phy_wq)
		return -ENOMEM;
	return 0;
}

void phy_drv_compat_exit(void)
{
	struct phy_compat_dev *cd, *tmp;

	if (!phy_wq)
		return;

	mutex_lock(&compat_lock);
	list_for_each_entry_safe(cd, tmp, &compat_dev_list, node) {
		cd->polling = 0;
		cancel_delayed_work_sync(&cd->poll);
		cancel_work_sync(&cd->work);
		list_del(&cd->node);
		kfree(cd);
	}
	mutex_unlock(&compat_lock);

	destroy_workqueue(phy_wq);
	phy_wq = NULL;
}
