// SPDX-License-Identifier: GPL-2.0
/*
 * BCM96855 PCIe 主控驱动的私有符号 stub
 *
 * pcie-bcm963xx.c 依赖几个由 Broadcom 闭源内核导出的函数：
 *   kerSysGetChipId()          -> chip id
 *   kerSysGetPciePortEnable()  -> 端口是否使能
 *   kerSysGetPciePortMode()    -> 端口模式（1 = Root Complex）
 *   UtilGetChipRev()           -> 芯片版本
 *   ubus_decode_pcie_wnd_cfg() -> 窗口配置解析
 *
 * 驱动的桩分支（CONFIG_BCM_PCIE_PMC_BRD_STUBS）已经能替代 chip id，
 * 这里补上剩余函数的最小可用实现，让驱动能在 6.18 上编译并绑定 RTL8125。
 */
#include <linux/types.h>

#ifndef CONFIG_BCM_CHIP_NUMBER
#define CONFIG_BCM_CHIP_NUMBER 6855
#endif

int kerSysGetChipId(void)
{
	return CONFIG_BCM_CHIP_NUMBER;
}

/* 硬件 PCIe 端口在 6855 上均为 Root Complex 且常开 */
int kerSysGetPciePortEnable(int port)
{
	(void)port;
	return 1;
}

int kerSysGetPciePortMode(int port)
{
	(void)port;
	return 1;	/* RC */
}

unsigned int UtilGetChipRev(void)
{
	return 0;
}

/* 窗口配置：本次移植不需要真实解码，返回 0 表示"使用默认映射" */
int ubus_decode_pcie_wnd_cfg(u32 base, u32 size, u32 core)
{
	(void)base;
	(void)size;
	(void)core;
	return 0;
}

/*
 * 第二轮补充：modpost 报出的未定义符号
 *   pmc_wlan_power_up/down  —— PMC WLAN 电源（闭源）
 *   getMemorySize           —— BCM SDK 取内存大小
 *   bcm_legacy_irq_map      —— BCM 传统中断映射表
 *   of_pci_parse_bus_range  —— 内核函数但未 EXPORT_SYMBOL，模块不可用
 */
#include <linux/of.h>
#include <linux/of_pci.h>
#include <linux/pci.h>

int pmc_wlan_power_up(int unit)
{
	(void)unit;
	return 0;
}

int pmc_wlan_power_down(int unit)
{
	(void)unit;
	return 0;
}

/* 512 MiB（实机 DDR 容量）；仅用于内部换算日志 */
unsigned long getMemorySize(void)
{
	return 0x20000000UL;
}

/* 传统中断映射表：默认恒等映射，足够 PCIe INTx 使用 */
unsigned int bcm_legacy_irq_map[256] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
};

/* 与内核同名同签名，但内核未导出；模块内自带一份实现 */
int of_pci_parse_bus_range(struct device_node *node, struct resource *res)
{
	const __be32 *range;
	int len;

	range = of_get_property(node, "bus-range", &len);
	if (!range || len < 2 * sizeof(*range))
		return -EINVAL;

	res->name = node->name;
	res->start = of_read_number(range, 1);
	res->end = of_read_number(range + 1, 1);
	return 0;
}
