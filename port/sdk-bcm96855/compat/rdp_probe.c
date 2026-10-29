/* SPDX-License-Identifier: GPL-2.0 */
/*
 * rdp_probe.c —— C1：RDP 块只读探测（不写任何寄存器）
 *
 * 依据 docs/REVERSE-ENET.md §7.1 的块地址表。做法：ioremap 每个块的
 * 第一页，readl 前 4 个字（+ UNIMAC 的 REV），然后 iounmap。
 * 只在 insmod 时带 rdp_probe=1 才执行，默认关闭。
 *
 * 严禁写：本文件只有 readl()，没有 writel()。
 */
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/moduleparam.h>
#include <linux/string.h>

static int rdp_probe;
module_param_named(rdp_probe, rdp_probe, int, 0444);
MODULE_PARM_DESC(rdp_probe, "1 = dump RDP block ID/rev registers (read-only)");

struct rdp_blk {
	const char *name;
	u32 base;
};

/* §7.1 块地址表（每个类型取第一个实例；UNIMAC/MISC 单独全量） */
static const struct rdp_blk rdp_blocks[] = {
	{ "RNR_REGS",	0x82800000 },
	{ "RNR_INST",	0x82710000 },
	{ "RNR_MEM",	0x82700000 },
	{ "RNR_CNTXT",	0x82718000 },
	{ "RNR_PRED",	0x8271c000 },
	{ "RNR_QUAD",	0x82808400 },
	{ "DSPTCHR",	0x82880000 },
	{ "BBH_TX",	0x82890000 },
	{ "UBUS_MSTR",	0x8289c000 },
	{ "UBUS_SLV",	0x828a0000 },
	{ "SBPM",	0x828a1000 },
	{ "DMA",	0x828a1800 },
	{ "PSRAM",	0x828a2800 },
	{ "FPM",	0x82a00000 },
	{ "QM",		0x82c00000 },
	{ "DQM",	0x82c80034 },
	{ "HASH",	0x82920000 },
	{ "TCAM",	0x82900000 },
	{ "NATC",	0x82950000 },
	{ "BAC_IF",	0x82940000 },
};

static void rdp_probe_one(const char *name, u32 base)
{
	void __iomem *p;
	u32 w0, w1, w2, w3;

	p = ioremap(base, 0x1000);
	if (!p) {
		pr_err("C1: ioremap 0x%08x (%s) failed\n", base, name);
		return;
	}

	w0 = readl(p + 0x00);
	w1 = readl(p + 0x04);
	w2 = readl(p + 0x08);
	w3 = readl(p + 0x0c);
	pr_info("C1 %-10s 0x%08x: %08x %08x %08x %08x\n",
		name, base, w0, w1, w2, w3);
	iounmap(p);
}

void rdp_probe_run(void)
{
	int i;

	if (!rdp_probe)
		return;

	pr_info("C1: RDP read-only probe start (no writes)\n");

	for (i = 0; i < ARRAY_SIZE(rdp_blocks); i++)
		rdp_probe_one(rdp_blocks[i].name, rdp_blocks[i].base);

	/* §3.1：UNIMAC 5 个实例（0..3、5，基址 = 0x828a8000 + 0x1000*n）
	 * MISC 同实例号（0x828b0000 + 0x400*n），读 REV 做交叉核对。 */
	for (i = 0; i < 6; i++) {
		u32 base = 0x828a8000u + 0x1000u * i;
		void __iomem *p = ioremap(base, 0x1000);

		if (!p)
			continue;
		pr_info("C1 UNIMAC[%d] 0x%08x rev=0x%08x cmd=0x%08x\n",
			i, base, readl(p + 0x780), readl(p + 0x008));
		iounmap(p);
	}

	for (i = 0; i < 6; i++) {
		u32 base = 0x828b0000u + 0x400u * i;
		void __iomem *p = ioremap(base, 0x400);

		if (!p)
			continue;
		pr_info("C1 MISC[%d]   0x%08x: %08x %08x\n",
			i, base, readl(p + 0x00), readl(p + 0x04));
		iounmap(p);
	}

	pr_info("C1: RDP read-only probe done\n");
}
