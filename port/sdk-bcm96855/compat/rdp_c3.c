/* SPDX-License-Identifier: GPL-2.0 */
/*
 * rdp_c3.c —— C3 骨架：CPU ring 表 + BBH_TX 端口配置（默认关闭）
 *
 * 依据 docs/REVERSE-ENET.md §5.2/§7.2/§7.4。当前只实现「可安全验证」的部分，
 * 未知项显式留 TODO，不猜值、不写硬件。
 *
 * 门控（位掩码，默认 0）：
 *   rdp_c3=0  关闭
 *   bit0 (1)  写 CPU ring 表项（16 B，物理地址大端），读回校验
 *   bit1 (2)  写 BBH_TX 每端口配置（EEE + DFIFOCTRL），读回校验
 *   bit2 (4)  DSPTCHR credit（值未还原，暂不实现，置位会报错退出）
 *   bit3 (8)  RNR 端口使能/学习（表地址未还原，暂不实现）
 *
 * 占位参数（分支 6 定位后填入）：
 *   rdp_c3_ring_base=<物理地址>  CPU ring 硬件表基址（16 B 对齐）
 *   rdp_c3_ring_id=<n>           环号，默认 0
 *   rdp_c3_ring_phys=<物理地址>  ring 数据缓冲，0 = 模块内部分配 4 KiB
 *   rdp_c3_port=<0..2>           BBH_TX 实例 / 端口号，默认 0
 *
 * 边界：不写 BBH_RX、不发包、不注册中断；所有写入在 rmmod 时恢复。
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/io.h>
#include <linux/swab.h>
#include <linux/moduleparam.h>
#include <asm/memory.h>

static int rdp_c3;
module_param_named(rdp_c3, rdp_c3, int, 0444);
MODULE_PARM_DESC(rdp_c3, "C3 bitmask: 1=ring table, 2=BBH_TX, 4=DSPTCHR, 8=RNR");

static uint rdp_c3_ring_base;
module_param_named(rdp_c3_ring_base, rdp_c3_ring_base, uint, 0444);
MODULE_PARM_DESC(rdp_c3_ring_base, "CPU ring descriptor table physical base");

static uint rdp_c3_ring_id;
module_param_named(rdp_c3_ring_id, rdp_c3_ring_id, uint, 0444);
MODULE_PARM_DESC(rdp_c3_ring_id, "CPU ring id (default 0)");

static uint rdp_c3_ring_phys;
module_param_named(rdp_c3_ring_phys, rdp_c3_ring_phys, uint, 0444);
MODULE_PARM_DESC(rdp_c3_ring_phys, "ring data buffer phys, 0=allocate 4KiB");

static uint rdp_c3_port;
module_param_named(rdp_c3_port, rdp_c3_port, uint, 0444);
MODULE_PARM_DESC(rdp_c3_port, "BBH_TX instance / port (0..2)");

#define C3_RING_ENTRIES		8
#define C3_RING_ENTRY_SIZE	16
#define C3_RING_BYTES		(C3_RING_ENTRIES * C3_RING_ENTRY_SIZE)

/* §7.1：BBH_TX 三个实例 */
#define C3_BBH_TX_BASE(p)	(0x82890000u + 0x2000u * (u32)(p))
#define C3_BBH_TX_EEE		0x90c	/* ag_drv_bbh_tx_unified_configurations_eee_set */
#define C3_BBH_TX_DFIFOCTRL	0x03c	/* ag_drv_bbh_tx_common_configurations_dfifoctrl_set */

/*
 * DFIFOCTRL 打包（AG setter）：
 *   v = p2 | (p4 << 20) | (p5 << 30) | (p3 << 10)
 *   bbh_tx_lan_init 调 (0x5e, 0x11f, 0x120, 1) -> 0x05247c5e
 */
#define C3_DFIFOCTRL_VAL	0x05247c5eu
#define C3_EEE_VAL		0x3fu

static void *c3_ring;
static uint c3_ring_phys;
static u32 c3_ring_tbl_save[4];
static int c3_ring_written;
static u32 c3_bbh_save[2];
static int c3_bbh_written;

void rdp_c3_restore(void)
{
	void __iomem *p;

	if (c3_ring_written && rdp_c3_ring_base) {
		p = ioremap(rdp_c3_ring_base + rdp_c3_ring_id * 0x10, 0x10);
		if (p) {
			writel(c3_ring_tbl_save[0], p + 0x0);
			writel(c3_ring_tbl_save[1], p + 0x4);
			writel(c3_ring_tbl_save[2], p + 0x8);
			writel(c3_ring_tbl_save[3], p + 0xc);
			iounmap(p);
			pr_info("C3: ring table[%u] restored\n", rdp_c3_ring_id);
		}
		c3_ring_written = 0;
	}

	if (c3_bbh_written) {
		p = ioremap(C3_BBH_TX_BASE(rdp_c3_port), 0x1000);
		if (p) {
			writel(c3_bbh_save[0], p + C3_BBH_TX_EEE);
			writel(c3_bbh_save[1], p + C3_BBH_TX_DFIFOCTRL);
			iounmap(p);
			pr_info("C3: BBH_TX[%u] restored\n", rdp_c3_port);
		}
		c3_bbh_written = 0;
	}

	if (c3_ring) {
		free_pages_exact(c3_ring, 4096);
		c3_ring = NULL;
	}
	c3_ring_phys = 0;
}

static int c3_write_ring_table(void)
{
	void __iomem *tbl;
	u32 d[4], rb[4];
	u16 v0;
	u32 n32 = (C3_RING_ENTRIES + 31) >> 5;

	if (!rdp_c3_ring_base) {
		pr_err("C3: rdp_c3_ring_base=0, ring table address unknown -> skip\n");
		return -EINVAL;
	}
	if (rdp_c3_ring_base & 0xf) {
		pr_err("C3: ring base 0x%x not 16B aligned\n", rdp_c3_ring_base);
		return -EINVAL;
	}

	c3_ring = alloc_pages_exact(4096, GFP_KERNEL | __GFP_ZERO);
	if (!c3_ring) {
		pr_err("C3: alloc ring buffer failed\n");
		return -ENOMEM;
	}
	c3_ring_phys = (u32)virt_to_phys(c3_ring);

	/* §5.2 16 B 环表项 */
	v0 = (u16)((rdp_c3_ring_id & 7) | (0 << 3) | ((n32 & 0xff) << 8));
	d[0] = v0 | ((u32)swab16((u16)C3_RING_ENTRY_SIZE) << 16);
	d[1] = 0;
	d[2] = swab32(c3_ring_phys);
	d[3] = 0;

	tbl = ioremap(rdp_c3_ring_base + rdp_c3_ring_id * 0x10, 0x10);
	if (!tbl) {
		pr_err("C3: ioremap ring table failed\n");
		return -ENOMEM;
	}

	c3_ring_tbl_save[0] = readl(tbl + 0x0);
	c3_ring_tbl_save[1] = readl(tbl + 0x4);
	c3_ring_tbl_save[2] = readl(tbl + 0x8);
	c3_ring_tbl_save[3] = readl(tbl + 0xc);
	pr_info("C3 ring[%u] old: %08x %08x %08x %08x\n", rdp_c3_ring_id,
		c3_ring_tbl_save[0], c3_ring_tbl_save[1],
		c3_ring_tbl_save[2], c3_ring_tbl_save[3]);

	writel(d[0], tbl + 0x0);
	writel(d[1], tbl + 0x4);
	writel(d[2], tbl + 0x8);
	writel(d[3], tbl + 0xc);

	rb[0] = readl(tbl + 0x0);
	rb[1] = readl(tbl + 0x4);
	rb[2] = readl(tbl + 0x8);
	rb[3] = readl(tbl + 0xc);
	iounmap(tbl);

	pr_info("C3 ring[%u] wrote: %08x %08x %08x %08x\n", rdp_c3_ring_id,
		d[0], d[1], d[2], d[3]);
	pr_info("C3 ring[%u] readback: %08x %08x %08x %08x %s\n",
		rdp_c3_ring_id, rb[0], rb[1], rb[2], rb[3],
		(rb[0] == d[0] && rb[1] == d[1] &&
		 rb[2] == d[2] && rb[3] == d[3]) ? "OK" : "MISMATCH");

	if (rb[0] != d[0] || rb[1] != d[1] ||
	    rb[2] != d[2] || rb[3] != d[3]) {
		/* 失败：立刻恢复该 16B */
		tbl = ioremap(rdp_c3_ring_base + rdp_c3_ring_id * 0x10, 0x10);
		if (tbl) {
			writel(c3_ring_tbl_save[0], tbl + 0x0);
			writel(c3_ring_tbl_save[1], tbl + 0x4);
			writel(c3_ring_tbl_save[2], tbl + 0x8);
			writel(c3_ring_tbl_save[3], tbl + 0xc);
			iounmap(tbl);
		}
		return -EIO;
	}

	c3_ring_written = 1;
	pr_info("C3: ring[%u] data phys=0x%08x desc_phys=0x%08x\n",
		rdp_c3_ring_id, c3_ring_phys, rdp_c3_ring_base + rdp_c3_ring_id * 0x10);
	return 0;
}

static int c3_write_bbh_tx(void)
{
	void __iomem *p;
	u32 eee, dfifo;

	if (rdp_c3_port > 2) {
		pr_err("C3: port %u out of range (BBH_TX has 3 instances)\n",
		       rdp_c3_port);
		return -EINVAL;
	}

	p = ioremap(C3_BBH_TX_BASE(rdp_c3_port), 0x1000);
	if (!p) {
		pr_err("C3: ioremap BBH_TX[%u] failed\n", rdp_c3_port);
		return -ENOMEM;
	}

	c3_bbh_save[0] = readl(p + C3_BBH_TX_EEE);
	c3_bbh_save[1] = readl(p + C3_BBH_TX_DFIFOCTRL);

	writel(C3_EEE_VAL, p + C3_BBH_TX_EEE);
	writel(C3_DFIFOCTRL_VAL, p + C3_BBH_TX_DFIFOCTRL);

	eee = readl(p + C3_BBH_TX_EEE);
	dfifo = readl(p + C3_BBH_TX_DFIFOCTRL);
	iounmap(p);

	pr_info("C3 BBH_TX[%u] +0x90c old=0x%08x wrote=0x%08x readback=0x%08x %s\n",
		rdp_c3_port, c3_bbh_save[0], C3_EEE_VAL, eee,
		(eee == C3_EEE_VAL) ? "OK" : "MISMATCH");
	pr_info("C3 BBH_TX[%u] +0x03c old=0x%08x wrote=0x%08x readback=0x%08x %s\n",
		rdp_c3_port, c3_bbh_save[1], C3_DFIFOCTRL_VAL, dfifo,
		(dfifo == C3_DFIFOCTRL_VAL) ? "OK" : "MISMATCH");

	if (eee != C3_EEE_VAL || dfifo != C3_DFIFOCTRL_VAL)
		return -EIO;

	c3_bbh_written = 1;
	return 0;
}

void rdp_c3_run(void)
{
	int rc = 0;

	if (!rdp_c3)
		return;

	pr_info("C3: start mask=0x%x ring_base=0x%x ring_id=%u port=%u\n",
		rdp_c3, rdp_c3_ring_base, rdp_c3_ring_id, rdp_c3_port);

	if (rdp_c3 & 4) {
		pr_err("C3: DSPTCHR credit 值未还原（分支 6 TODO），拒绝执行\n");
		rc = -EOPNOTSUPP;
	}
	if (rdp_c3 & 8) {
		pr_err("C3: RNR 端口表地址未还原（分支 6 TODO），拒绝执行\n");
		rc = -EOPNOTSUPP;
	}
	if (rc)
		return;

	if (rdp_c3 & 1)
		rc = c3_write_ring_table();
	if (!rc && (rdp_c3 & 2))
		rc = c3_write_bbh_tx();

	if (rc) {
		pr_err("C3: failed rc=%d, restoring\n", rc);
		rdp_c3_restore();
		return;
	}
	pr_info("C3: done (ring=%d bbh=%d)\n", !!(rdp_c3 & 1), !!(rdp_c3 & 2));
}
