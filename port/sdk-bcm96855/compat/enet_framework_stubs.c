/* SPDX-License-Identifier: GPL-2.0 */
/*
 * enet_framework_stubs.c —— bcm_enet 依赖的 PHY/MAC 框架入口
 *
 * 原厂把 PHY/MAC 框架编成独立的 bcm_phy.ko（bcmdrivers/opensource/phy/），
 * bcm_enet 只调用它的几个入口：
 *
 *   mac_drivers_set()     注册板上的 MAC 驱动（unimac）
 *   mac_drivers_init()    初始化已注册的 MAC 驱动
 *   mac_dev_dt_probe()    按 DTS port 节点创建 mac_dev
 *
 * 其中 unmac 的实现（mac_drv_unimac.c / unimac_drv_impl1.c）在这份 GPL 包里
 * **只有 aarch64 的 .o，没有源码**，本板是 armv7，链接不了。所以：
 *
 *   · mac_drivers_set() 退化为「初始化 PHY compat 层」，返回 0
 *   · mac_drivers_init() 返回 0（没有 MAC 驱动可初始化）
 *   · mac_dev_dt_probe() 明确报错并返回 NULL —— 让端口解析在
 *     "Failed to create mac for port N" 处干净地停下，而不是拿一个假
 *     mac_dev 往下跑、在 sw_init 里空指针崩溃。
 *
 * 等 MAC 驱动补上（自写 unimac 或从别的 GPL 版本取源码）时，把这三个
 * 函数删掉、改编厂商 mac_drv.c 即可。
 */
#include <linux/kernel.h>
#include <linux/errno.h>

#include "mac_drv.h"
#include "phy_drv_compat.h"

int mac_drivers_set(void)
{
	int rc = phy_drv_compat_init();

	if (rc)
		pr_err("bcm_enet: phy compat init failed (%d)\n", rc);
	return rc;
}

int mac_drivers_init(void)
{
	/* 没有 MAC 驱动（unimac 源码缺失），这里无事可做 */
	return 0;
}

extern mac_drv_t mac_drv_unimac;
struct unimac_compat_priv *unimac_compat_priv_get(uint32_t id);

mac_dev_t *mac_dev_dt_probe(const dt_handle_t handle, uint32_t port_index)
{
	mac_dev_t *mac_dev;

	mac_dev = kzalloc(sizeof(*mac_dev), GFP_KERNEL);
	if (!mac_dev)
		return NULL;

	mac_dev->mac_drv = &mac_drv_unimac;
	mac_dev->mac_id = port_index;
	mac_dev->priv = unimac_compat_priv_get(port_index);

	if (mac_drv_dev_add(mac_dev)) {
		kfree(mac_dev);
		return NULL;
	}

	pr_info("bcm_enet: registered UNIMAC:%u for %pOF\n",
		port_index, handle);
	return mac_dev;
}
