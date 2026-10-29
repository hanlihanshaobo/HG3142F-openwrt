/* SPDX-License-Identifier: GPL-2.0 */
/*
 * rdp_c3.c —— C3：CPU ring 表（0x82703000）+ 只读探测骨架
 *
 * 依据 docs/REVERSE-ENET.md §5.2/§7.2 与分支 6 的定位结论：
 *   ring 表基址 = rdp_runner_core_addr[0] (0x82700000) +
 *                 RDD_CPU_RING_DESCRIPTORS_TABLE_ADDRESS_ARR[0] (0x3000)
 *               = 0x82703000；0x82800000 是 RNR_REGS 寄存器块，不是表。
 *
 * 门控（位掩码，默认 0）：
 *   rdp_c3=0   关闭
 *   bit0 (1)   写 8 项 CPU ring 表（16 B/项，物理地址大端），逐字节回读
 *   bit1 (2)   BBH_TX 每端口配置（EEE + DFIFOCTRL），本轮不使用
 *   bit2 (4)   DSPTCHR credit（值未还原，拒绝执行）
 *   bit3 (8)   RNR 端口使能/学习（表未还原，拒绝执行）
 *   bit4 (16)  只读探测（0x82700000/0x82703000/0x82702f00/... ），读完即返回
 *
 * 占位参数：
 *   rdp_c3_ring_base=<物理地址>  ring 表基址（16 B 对齐；本轮 0x82703000）
 *   rdp_c3_ring_id=<n>           起始环号（默认 0，写 0..7）
 *   rdp_c3_ring_phys=<物理地址>  0 = 模块分配 8×4 KiB 数据缓冲
 *   rdp_c3_port=<0..2>           BBH_TX 实例（bit1 用）
 *
 * 边界：不写 BBH/DSPTCHR/RNR（bit1 保留但本轮不置位）、不发包、不注册中断；
 * 所有写入在 rmmod 时按备份恢复。
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
MODULE_PARM_DESC(rdp_c3, "C3 bitmask: 1=ring table, 2=BBH_TX, 4=DSPTCHR, 8=RNR, 16=probe");

static uint rdp_c3_ring_base;
module_param_named(rdp_c3_ring_base, rdp_c3_ring_base, uint, 0444);
MODULE_PARM_DESC(rdp_c3_ring_base, "CPU ring descriptor table physical base");

static uint rdp_c3_ring_id;
module_param_named(rdp_c3_ring_id, rdp_c3_ring_id, uint, 0444);
MODULE_PARM_DESC(rdp_c3_ring_id, "first ring id (writes id..id+7)");

static uint rdp_c3_ring_phys;
module_param_named(rdp_c3_ring_phys, rdp_c3_ring_phys, uint, 0444);
MODULE_PARM_DESC(rdp_c3_ring_phys, "ring data buffer phys, 0=allocate 8x4KiB");

static uint rdp_c3_port;
module_param_named(rdp_c3_port, rdp_c3_port, uint, 0444);
MODULE_PARM_DESC(rdp_c3_port, "BBH_TX instance / port (0..2)");

#define C3_RING_COUNT		8
#define C3_RING_ENTRIES		8
#define C3_RING_ENTRY_SIZE	16
#define C3_RING_BUF_SIZE	4096
#define C3_RING_DATA_SIZE	(C3_RING_COUNT * C3_RING_BUF_SIZE)

#define C3_PROBE_BIT		16
#define C3_RING_BIT		1
#define C3_BBH_BIT		2

/* §7.1：BBH_TX 三个实例 */
#define C3_BBH_TX_BASE(p)	(0x82890000u + 0x2000u * (u32)(p))
#define C3_BBH_TX_EEE		0x90c
#define C3_BBH_TX_DFIFOCTRL	0x03c
#define C3_DFIFOCTRL_VAL	0x05247c5eu
#define C3_EEE_VAL		0x3fu

static void *c3_ring;
static uint c3_ring_phys;
static u32 c3_ring_tbl_save[C3_RING_COUNT][4];
static int c3_ring_written;
static u32 c3_bbh_save[2];
static int c3_bbh_written;

/* ------------------------------------------------------------------ */
/* 只读探测                                                            */
/* ------------------------------------------------------------------ */

static void c3_dump(const char *name, u32 base, u32 words)
{
	void __iomem *p;
	u32 i;

	p = ioremap(base, 0x1000);
	if (!p) {
		pr_err("C3 probe %-14s 0x%08x ioremap failed\n", name, base);
		return;
	}

	pr_info("C3 probe %-14s 0x%08x:", name, base);
	for (i = 0; i < words; i++)
		pr_cont(" %08x", readl(p + i * 4));
	pr_cont("\n");
	iounmap(p);
}

static void c3_probe(void)
{
	int i;

	pr_info("C3 probe: read-only start\n");

	c3_dump("RNR_MEM", 0x82700000, 4);
	c3_dump("feed_cache?", 0x82702f00, 4);
	c3_dump("recycle?", 0x82703930, 4);

	/* ring 表：8 项 × 16 B；每项打印 4 个字 */
	for (i = 0; i < C3_RING_COUNT; i++)
		c3_dump("ring_table", 0x82703000 + i * 0x10, 4);

	c3_dump("RXQ_buf_cfg?", 0x82703870, 4);
	c3_dump("data_buf_type?", 0x82703a40, 4);
	c3_dump("RNR_REGS(对照)", 0x82803000, 4);

	pr_info("C3 probe: done (no writes)\n");
}

/* ------------------------------------------------------------------ */
/* ring 表写入                                                          */
/* ------------------------------------------------------------------ */

static void c3_restore_ring_table(void)
{
	void __iomem *tbl;
	int i;

	if (!c3_ring_written || !rdp_c3_ring_base)
		return;

	tbl = ioremap(rdp_c3_ring_base, C3_RING_COUNT * 0x10);
	if (!tbl) {
		pr_err("C3: ioremap ring table for restore failed\n");
		return;
	}
	for (i = 0; i < C3_RING_COUNT; i++) {
		writel(c3_ring_tbl_save[i][0], tbl + i * 0x10 + 0x0);
		writel(c3_ring_tbl_save[i][1], tbl + i * 0x10 + 0x4);
		writel(c3_ring_tbl_save[i][2], tbl + i * 0x10 + 0x8);
		writel(c3_ring_tbl_save[i][3], tbl + i * 0x10 + 0xc);
	}
	iounmap(tbl);
	c3_ring_written = 0;
	pr_info("C3: ring table[0..%d] restored\n", C3_RING_COUNT - 1);
}

static int c3_write_ring_table(void)
{
	void __iomem *tbl;
	u32 n32 = (C3_RING_ENTRIES + 31) >> 5;
	int i, j, bad = 0;

	if (!rdp_c3_ring_base) {
		pr_err("C3: rdp_c3_ring_base=0, ring table address unknown -> skip\n");
		return -EINVAL;
	}
	if (rdp_c3_ring_base & 0xf) {
		pr_err("C3: ring base 0x%x not 16B aligned\n", rdp_c3_ring_base);
		return -EINVAL;
	}
	if (rdp_c3_ring_id + C3_RING_COUNT > 32) {
		pr_err("C3: ring id %u..%u out of range\n", rdp_c3_ring_id,
		       rdp_c3_ring_id + C3_RING_COUNT - 1);
		return -EINVAL;
	}

	if (!rdp_c3_ring_phys) {
		c3_ring = alloc_pages_exact(C3_RING_DATA_SIZE,
					    GFP_KERNEL | __GFP_ZERO);
		if (!c3_ring) {
			pr_err("C3: alloc %u bytes ring data failed\n",
			       C3_RING_DATA_SIZE);
			return -ENOMEM;
		}
		c3_ring_phys = (u32)virt_to_phys(c3_ring);
	}
	pr_info("C3: ring data phys=0x%08x size=%u\n", c3_ring_phys,
		C3_RING_DATA_SIZE);

	tbl = ioremap(rdp_c3_ring_base, C3_RING_COUNT * 0x10);
	if (!tbl) {
		pr_err("C3: ioremap ring table failed\n");
		return -ENOMEM;
	}

	/* 备份 8 项 */
	for (i = 0; i < C3_RING_COUNT; i++) {
		c3_ring_tbl_save[i][0] = readl(tbl + i * 0x10 + 0x0);
		c3_ring_tbl_save[i][1] = readl(tbl + i * 0x10 + 0x4);
		c3_ring_tbl_save[i][2] = readl(tbl + i * 0x10 + 0x8);
		c3_ring_tbl_save[i][3] = readl(tbl + i * 0x10 + 0xc);
	}
	for (i = 0; i < C3_RING_COUNT; i++)
		pr_info("C3 ring[%d] old: %08x %08x %08x %08x\n", i,
			c3_ring_tbl_save[i][0], c3_ring_tbl_save[i][1],
			c3_ring_tbl_save[i][2], c3_ring_tbl_save[i][3]);

	/* 写 8 项（§5.2） */
	for (i = 0; i < C3_RING_COUNT; i++) {
		u32 id = rdp_c3_ring_id + i;
		u32 phys = c3_ring_phys + i * C3_RING_BUF_SIZE;
		u32 w0 = (id & 7) | ((n32 & 0xff) << 8);
		u32 w1 = swab16((u16)C3_RING_ENTRY_SIZE);
		u32 w2 = swab32(phys);
		u32 w3 = 0;

		writel(w0, tbl + i * 0x10 + 0x0);
		writel(w1, tbl + i * 0x10 + 0x4);
		writel(w2, tbl + i * 0x10 + 0x8);
		writel(w3, tbl + i * 0x10 + 0xc);

		pr_info("C3 ring[%d] wrote: %08x %08x %08x %08x phys=0x%08x\n",
			i, w0, w1, w2, w3, phys);
	}

	/* 逐字节回读（按 u32 比较） */
	c3_ring_written = 1;
	for (i = 0; i < C3_RING_COUNT; i++) {
		u32 id = rdp_c3_ring_id + i;
		u32 phys = c3_ring_phys + i * C3_RING_BUF_SIZE;
		u32 exp[4], got[4];

		exp[0] = (id & 7) | ((n32 & 0xff) << 8);
		exp[1] = swab16((u16)C3_RING_ENTRY_SIZE);
		exp[2] = swab32(phys);
		exp[3] = 0;
		for (j = 0; j < 4; j++)
			got[j] = readl(tbl + i * 0x10 + j * 4);

		if (exp[0] != got[0] || exp[1] != got[1] ||
		    exp[2] != got[2] || exp[3] != got[3]) {
			pr_err("C3 ring[%d] MISMATCH exp %08x %08x %08x %08x got %08x %08x %08x %08x\n",
			       i, exp[0], exp[1], exp[2], exp[3],
			       got[0], got[1], got[2], got[3]);
			bad = 1;
		} else {
			pr_info("C3 ring[%d] readback OK\n", i);
		}
	}
	iounmap(tbl);

	if (bad)
		return -EIO;

	pr_info("C3: ring table 8/8 OK\n");
	return 0;
}

/* ------------------------------------------------------------------ */
/* BBH_TX（骨架保留，本轮不置 bit1）                                     */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/* 入口 / 恢复                                                          */
/* ------------------------------------------------------------------ */

void rdp_c3_restore(void)
{
	void __iomem *p;

	c3_restore_ring_table();

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
		free_pages_exact(c3_ring, C3_RING_DATA_SIZE);
		c3_ring = NULL;
	}
	c3_ring_phys = 0;
}

void rdp_c3_run(void)
{
	int rc = 0;

	if (!rdp_c3)
		return;

	pr_info("C3: start mask=0x%x ring_base=0x%x ring_id=%u port=%u\n",
		rdp_c3, rdp_c3_ring_base, rdp_c3_ring_id, rdp_c3_port);

	if (rdp_c3 & C3_PROBE_BIT)
		c3_probe();

	if (rdp_c3 & 4) {
		pr_err("C3: DSPTCHR credit 值未还原（分支 6 TODO），拒绝执行\n");
		return;
	}
	if (rdp_c3 & 8) {
		pr_err("C3: RNR 端口表地址未还原（分支 6 TODO），拒绝执行\n");
		return;
	}

	if (rdp_c3 & C3_RING_BIT)
		rc = c3_write_ring_table();
	if (!rc && (rdp_c3 & C3_BBH_BIT))
		rc = c3_write_bbh_tx();

	if (rc) {
		pr_err("C3: failed rc=%d, restoring\n", rc);
		rdp_c3_restore();
		return;
	}
	if (rdp_c3 & (C3_RING_BIT | C3_BBH_BIT))
		pr_info("C3: done (ring=%d bbh=%d)\n",
			!!(rdp_c3 & C3_RING_BIT), !!(rdp_c3 & C3_BBH_BIT));
}
