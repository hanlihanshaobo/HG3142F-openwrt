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

/*
 * ============================================================
 *  最小 PMC(PMB) 实现：给 PCIe 核心上电
 *
 *  实机证明：不做上电时，读 PCIe 核心寄存器会产生
 *  "synchronous external abort"（核心未上电/无时钟）。
 *
 *  机制（源自 Broadcom GPL 的 pmc 驱动）：
 *    1) 通过 PMC 的 PMB keyhole 寄存器访问 PMB 总线
 *    2) PowerOnZone(pcie): 读 BPCM zone0 control，置
 *       dpg_ctl_en / pwr_up_req / mem_pwr_ctl_en / blk_reset_assert，
 *       清 pwr_dn_req
 *
 *  地址来源：实机 DT /pmc = 0xffb20060 (compatible brcm,bca-pmc-lite)
 *            PMB_ADDR_PCIE0/1 = 0x1011 / 0x1012 (6855/pmc_addr.h)
 *            BPCM zones[] 起始 0x80，每 zone 0x20 字节 (6855/BPCM.h)
 * ============================================================
 */
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/slab.h>

#define PMC_PHYS_BASE		0xffb20060UL
#define PMC_PMB_CFG_OFF		0x100	/* PmbBus.config */
#define PMC_KEYHOLE_CTRL_OFF	0x110	/* keyhole[0].control */
#define PMC_KEYHOLE_WR_OFF	0x114
#define PMC_KEYHOLE_RD_OFF	0x11c

#define PMC_PMBM_START		(1u << 31)
#define PMC_PMBM_TIMEOUT	(1u << 30)
#define PMC_PMBM_BUSY		(1u << 28)
#define PMC_PMBM_BUS_SHIFT	20
#define PMC_PMBM_WRITE		(1u << 24)

#define PMB_ADDR_PCIE0		(17 | (1 << 12))
#define PMB_ADDR_PCIE1		(18 | (1 << 12))

/* BPCM zones[] 从 0x80 开始，每 zone 0x20 字节；control 是 zone 内第 0 个字 */
#define BPCM_ZONE_CTRL_WORD(zone)	(0x80 / 4 + (zone) * 8)

/* BPCM_PWR_ZONE_N_CONTROL 位定义 */
#define ZC_DPG_CTL_EN		(1u << 8)
#define ZC_PWR_DN_REQ		(1u << 9)
#define ZC_PWR_UP_REQ		(1u << 10)
#define ZC_MEM_PWR_CTL_EN	(1u << 11)
#define ZC_BLK_RESET_ASSERT	(1u << 12)
#define ZC_PWR_ON_STATE		(1u << 26)

static void __iomem *bcm_pmc_base;

static int bcm_pmc_map(void)
{
	if (bcm_pmc_base)
		return 0;
	bcm_pmc_base = ioremap(PMC_PHYS_BASE, 0x200);
	if (!bcm_pmc_base) {
		pr_err("bcm-pcie: ioremap PMC 0x%lx failed\n", PMC_PHYS_BASE);
		return -ENOMEM;
	}
	return 0;
}

/* devAddr: PMB 设备地址；word_off: 以 32bit 字为单位的寄存器偏移 */
static int bcm_pmb_access(u32 dev, u32 word_off, u32 *val, bool write)
{
	u32 bus, addr, ctl, num_regs;
	int tries = 1000;

	if (bcm_pmc_map())
		return -ENODEV;

	bus = (dev >> 12) & 0x3;
	num_regs = (readl(bcm_pmc_base + PMC_PMB_CFG_OFF) >> 20) & 0x3ff;
	addr = ((dev & 0xff) * num_regs) | word_off;

	if (write) {
		writel(*val, bcm_pmc_base + PMC_KEYHOLE_WR_OFF);
		ctl = PMC_PMBM_START | (bus << PMC_PMBM_BUS_SHIFT) |
		      PMC_PMBM_WRITE | addr;
	} else {
		ctl = PMC_PMBM_START | (bus << PMC_PMBM_BUS_SHIFT) | addr;
	}

	writel(ctl, bcm_pmc_base + PMC_KEYHOLE_CTRL_OFF);
	while ((readl(bcm_pmc_base + PMC_KEYHOLE_CTRL_OFF) & PMC_PMBM_BUSY) &&
	       --tries)
		cpu_relax();

	ctl = readl(bcm_pmc_base + PMC_KEYHOLE_CTRL_OFF);
	if (ctl & PMC_PMBM_TIMEOUT)
		return -ETIMEDOUT;

	if (!write)
		*val = readl(bcm_pmc_base + PMC_KEYHOLE_RD_OFF);
	return 0;
}

static void bcm_pcie_zone_power_on(u32 dev)
{
	u32 v = 0;
	int rc;

	rc = bcm_pmb_access(dev, BPCM_ZONE_CTRL_WORD(0), &v, false);
	if (rc) {
		pr_err("bcm-pcie: PMB read zone ctrl (dev 0x%x) failed %d\n", dev, rc);
		return;
	}
	if (v & ZC_PWR_ON_STATE) {
		pr_info("bcm-pcie: PMB dev 0x%x already powered (ctrl=0x%08x)\n", dev, v);
		return;
	}

	v &= ~ZC_PWR_DN_REQ;
	v |= ZC_DPG_CTL_EN | ZC_PWR_UP_REQ | ZC_MEM_PWR_CTL_EN | ZC_BLK_RESET_ASSERT;

	rc = bcm_pmb_access(dev, BPCM_ZONE_CTRL_WORD(0), &v, true);
	pr_info("bcm-pcie: PMB dev 0x%x power-up -> ctrl=0x%08x rc=%d\n", dev, v, rc);

	mdelay(10);
}

/* 供 pcie_hcd.c 的桩调用：真正给 PCIe 核心上电 */
void pmc_pcie_power_up(int unit, int is_dual_lane)
{
	u32 dev;

	(void)is_dual_lane;

	if (unit == 0)
		dev = PMB_ADDR_PCIE0;
	else if (unit == 1)
		dev = PMB_ADDR_PCIE1;
	else
		return;

	bcm_pcie_zone_power_on(dev);
}

void pmc_pcie_power_down(int unit, int is_dual_lane)
{
	(void)unit;
	(void)is_dual_lane;
}
