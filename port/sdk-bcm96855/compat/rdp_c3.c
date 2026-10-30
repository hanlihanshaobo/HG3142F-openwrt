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
 *   bit2 (4)   DSPTCHR credit：0x82880000+0x300+4*v，RMW 清 bit[31:20]
 *              （credit=0，保留低 20 位阈值），v=0..rdp_c3_dsp_ports-1
 *   bit3 (8)   RNR 全局 enable（0x82800000+0x1000*core bit0=1，RMW）
 *              + BBH_RX 端口（0x82898000+0x400*p：+0x00/+0x04/+0x30/+0x34/+0x64/+0x3c）
 *   bit4 (16)  Q_DEST punt 实验：0x828804C0 bit[port]=1、0x82880600 解对应位
 *   bit5 (32)  只读探测（0x82703000/0x82703870/0x828804C0/0x82880600 ...）
 *   bit6 (64)  RXQ 表（0x82703870 / 0x82703a40，表地址/长度/值全部由参数给出；
 *              默认不写，等分支 6 确认表格式后再填默认值）
 *
 * 占位参数：
 *   rdp_c3_ring_base=<物理地址>  ring 表基址（16 B 对齐；本轮 0x82703000）
 *   rdp_c3_ring_id=<n>           起始环号（默认 0，写 0..7）
 *   rdp_c3_ring_phys=<物理地址>  0 = 模块分配 8×4 KiB 数据缓冲
 *   rdp_c3_port=<0..2>           BBH_TX 实例（bit1 用）
 *   rdp_c3_rxq_buf_base/count    RXQ buf cfg 表地址/长度（默认 0x82703870/0）
 *   rdp_c3_rxq_buf_cfg=<v0,...>  RXQ buf cfg 表项值（逗号分隔 u32，空=不写）
 *   rdp_c3_rxq_dtype_base/count  RXQ data type 表地址/长度（默认 0x82703a40/0）
 *   rdp_c3_rxq_data_type=<v0,..> RXQ data type 表项值（逗号分隔 u32，空=不写）
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
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/kstrtox.h>
#include <asm/memory.h>

static int rdp_c3;
module_param_named(rdp_c3, rdp_c3, int, 0444);
MODULE_PARM_DESC(rdp_c3, "C3 bitmask: 1=ring table, 2=BBH_TX, 4=DSPTCHR, 8=RNR+BBH_RX, 16=Q_DEST, 32=probe, 64=RXQ table");

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

static uint rdp_c3_dsp_ports = 5;
module_param_named(rdp_c3_dsp_ports, rdp_c3_dsp_ports, uint, 0444);
MODULE_PARM_DESC(rdp_c3_dsp_ports, "DSPTCHR credit entries v=0..n-1 (default 5)");

static uint rdp_c3_rnr_cores = 5;
module_param_named(rdp_c3_rnr_cores, rdp_c3_rnr_cores, uint, 0444);
MODULE_PARM_DESC(rdp_c3_rnr_cores, "RNR global enable cores (default 5; core 5 is absent)");

static uint rdp_c3_rx_ports = 7;
module_param_named(rdp_c3_rx_ports, rdp_c3_rx_ports, uint, 0444);
MODULE_PARM_DESC(rdp_c3_rx_ports, "BBH_RX ports p=0..n-1 (default 7)");

static uint rdp_c3_qdest_port = 2;
module_param_named(rdp_c3_qdest_port, rdp_c3_qdest_port, uint, 0444);
MODULE_PARM_DESC(rdp_c3_qdest_port, "Q_DEST punt port bit (default 2 = LAN2)");

static uint rdp_c3_watch_secs = 20;
module_param_named(rdp_c3_watch_secs, rdp_c3_watch_secs, uint, 0444);
MODULE_PARM_DESC(rdp_c3_watch_secs, "ring write pointer watch duration in seconds (default 20)");

/* bit6：RXQ 表（离线骨架；格式等分支 6 确认后再填默认值） */
static uint rdp_c3_rxq_buf_base = 0x82703870u;
module_param_named(rdp_c3_rxq_buf_base, rdp_c3_rxq_buf_base, uint, 0444);
MODULE_PARM_DESC(rdp_c3_rxq_buf_base, "RXQ buf cfg table base (default 0x82703870)");

static uint rdp_c3_rxq_buf_count;
module_param_named(rdp_c3_rxq_buf_count, rdp_c3_rxq_buf_count, uint, 0444);
MODULE_PARM_DESC(rdp_c3_rxq_buf_count, "RXQ buf cfg entries (0 = derive from value list)");

static char *rdp_c3_rxq_buf_cfg = "";
module_param_named(rdp_c3_rxq_buf_cfg, rdp_c3_rxq_buf_cfg, charp, 0444);
MODULE_PARM_DESC(rdp_c3_rxq_buf_cfg, "RXQ buf cfg values, comma-separated u32 (empty = no write)");

static uint rdp_c3_rxq_dtype_base = 0x82703a40u;
module_param_named(rdp_c3_rxq_dtype_base, rdp_c3_rxq_dtype_base, uint, 0444);
MODULE_PARM_DESC(rdp_c3_rxq_dtype_base, "RXQ data type table base (default 0x82703a40)");

static uint rdp_c3_rxq_dtype_count;
module_param_named(rdp_c3_rxq_dtype_count, rdp_c3_rxq_dtype_count, uint, 0444);
MODULE_PARM_DESC(rdp_c3_rxq_dtype_count, "RXQ data type entries (0 = derive from value list)");

static char *rdp_c3_rxq_data_type = "";
module_param_named(rdp_c3_rxq_data_type, rdp_c3_rxq_data_type, charp, 0444);
MODULE_PARM_DESC(rdp_c3_rxq_data_type, "RXQ data type values, comma-separated u32 (empty = no write)");

#define C3_RING_COUNT		8
#define C3_RING_ENTRIES		8
#define C3_RING_ENTRY_SIZE	16
#define C3_RING_BUF_SIZE	4096
#define C3_RING_DATA_SIZE	(C3_RING_COUNT * C3_RING_BUF_SIZE)

#define C3_PROBE_BIT		32	/* bit5：只读探测 */
#define C3_RXQ_BIT		64	/* bit6：RXQ 表（值由参数提供，默认不写） */
#define C3_QDEST_BIT		16	/* bit4：Q_DEST punt 实验 */
#define C3_RING_BIT		1
#define C3_BBH_BIT		2
#define C3_DSP_BIT		4
#define C3_RNR_BIT		8

#define C3_DSPTCHR_BASE		0x82880000u
#define C3_DSPTCHR_OFF		0x300u
#define C3_DSP_CREDIT_MASK	0xfff00000u	/* bit[31:20] = credit */
#define C3_DSP_RMW_KEEP		0x000fffffu	/* 低 20 位阈值必须保留 */

#define C3_RNR_BASE		0x82800000u
#define C3_RNR_ENABLE		(1u << 0)

#define C3_QDEST_BASE		0x82880000u
#define C3_QDEST_REG		0x4c0u	/* 32 位 Q_DEST 位图（bit=VIQ） */
#define C3_QDEST_MASK_REG	0x600u	/* 0x604=0x3f 是 RNR group 掩码，不要动 */
#define C3_VQEN_REG		0x004u	/* VQ_EN：bit=VIQ 的使能门 */

#define C3_BBH_RX_BASE		0x82898000u
#define C3_BBH_RX_STRIDE	0x400u
#define C3_BBH_RX_SBPM_ID	0x00		/* SBPM BB id（分支 6：0x00381200） */
#define C3_BBH_RX_VIQ		0x04
#define C3_BBH_RX_0X30		0x30
#define C3_BBH_RX_0X34		0x34
#define C3_BBH_RX_0X64		0x64		/* sbpmcfg（分支 6：0x0f） */
#define C3_BBH_RX_CTRL		0x3c		/* 最后写，bit0/1 使能 */
#define C3_BBH_RX_SBPM_ID_VAL	0x00381200u
#define C3_BBH_RX_0X64_VAL	0x0fu

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

#define C3_DSP_MAX	8
#define C3_RNR_MAX	6
#define C3_RX_MAX	7

static u32 c3_dsp_save[C3_DSP_MAX];
static int c3_dsp_written;

static u32 c3_rnr_save[C3_RNR_MAX];
static int c3_rnr_written;

static u32 c3_rx_save[C3_RX_MAX][6];
static int c3_rx_written;

static u32 c3_qdest_save[2];	/* [0]=VQ_EN old, [1]=Q_DEST old */
static u32 c3_vqen_step[2];	/* 中间步骤读回 */
static int c3_qdest_written;

/* ring 写指针观察（1s 一次，20 次；用于 mask=15 现场验证） */
static struct delayed_work c3_watch;
static int c3_watch_ticks;
static int c3_watch_started;

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
	c3_dump("VQ_EN 0x004", 0x82880004, 4);
	c3_dump("QDEST 0x4c0", 0x828804c0, 4);
	c3_dump("QDEST 0x600", 0x82880600, 4);

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
/* bit2：DSPTCHR credit（RMW，只清 bit[31:20]）                          */
/* ------------------------------------------------------------------ */

static int c3_write_dsptchr(void)
{
	void __iomem *p;
	u32 v, old, new, rb;
	int rc = 0;

	if (rdp_c3_dsp_ports > C3_DSP_MAX) {
		pr_err("C3: dsp_ports %u > %u\n", rdp_c3_dsp_ports, C3_DSP_MAX);
		return -EINVAL;
	}

	p = ioremap(C3_DSPTCHR_BASE, 0x1000);
	if (!p)
		return -ENOMEM;

	for (v = 0; v < rdp_c3_dsp_ports; v++) {
		u32 off = C3_DSPTCHR_OFF + 4 * v;

		old = readl(p + off);
		new = old & C3_DSP_RMW_KEEP;	/* credit=0，保留低 20 位阈值 */
		c3_dsp_save[v] = old;
		c3_dsp_written = v + 1;

		writel(new, p + off);
		rb = readl(p + off);
		pr_info("C3 DSPTCHR[%u] 0x%03x old=0x%08x wrote=0x%08x readback=0x%08x %s\n",
			v, off, old, new, rb, (rb == new) ? "OK" : "MISMATCH");
		if (rb != new) {
			rc = -EIO;
			break;
		}
	}
	iounmap(p);
	return rc;
}

static void c3_restore_dsptchr(void)
{
	void __iomem *p;
	u32 v;

	if (!c3_dsp_written)
		return;

	p = ioremap(C3_DSPTCHR_BASE, 0x1000);
	if (p) {
		for (v = 0; v < c3_dsp_written; v++)
			writel(c3_dsp_save[v],
			       p + C3_DSPTCHR_OFF + 4 * v);
		iounmap(p);
	}
	c3_dsp_written = 0;
	pr_info("C3: DSPTCHR credits restored\n");
}

/* ------------------------------------------------------------------ */
/* bit3a：RNR 全局 enable（每 core RMW bit0）                            */
/* ------------------------------------------------------------------ */

static int c3_write_rnr(void)
{
	void __iomem *p;
	u32 core, old, new, rb;
	int rc = 0;

	if (rdp_c3_rnr_cores > C3_RNR_MAX) {
		pr_err("C3: rnr_cores %u > %u\n", rdp_c3_rnr_cores, C3_RNR_MAX);
		return -EINVAL;
	}

	p = ioremap(C3_RNR_BASE, 0x1000 * C3_RNR_MAX);
	if (!p)
		return -ENOMEM;

	for (core = 0; core < rdp_c3_rnr_cores; core++) {
		u32 off = core * 0x1000;
		u32 tries;

		old = readl(p + off);
		if (old == 0xffffffffu) {
			/* core 不存在：不要写 */
			pr_info("C3 RNR[%u] 0x%05x absent (0xffffffff), skip\n",
				core, off);
			continue;
		}

		new = old | C3_RNR_ENABLE;
		c3_rnr_save[core] = old;
		c3_rnr_written = core + 1;

		/*
		 * 现场（run #95）：RNR[1] old=0x0101f302 写 0x0101f303 后
		 * 读回 0x0101f301 —— 硬件清 bit1（状态位）；RNR[3]/[5]
		 * 偶发写不进 bit0（core 不存在或离线）。因此：
		 *  - 写后只断言 bit0（enable），其他位允许被硬件改写；
		 *  - 重试 3 次；仍不行则记录并跳过（core 0 除外）。
		 */
		for (tries = 0; tries < 3; tries++) {
			writel(new, p + off);
			rb = readl(p + off);
			if (rb & C3_RNR_ENABLE)
				break;
			mdelay(1);
		}
		pr_info("C3 RNR[%u] 0x%05x old=0x%08x wrote=0x%08x readback=0x%08x bit0=%d %s\n",
			core, off, old, new, rb, !!(rb & C3_RNR_ENABLE),
			(rb & C3_RNR_ENABLE) ? "OK" : "SKIP");
		if (!(rb & C3_RNR_ENABLE)) {
			if (core == 0) {
				rc = -EIO;
				break;
			}
			pr_info("C3 RNR[%u] not writable, skip (absent/offline)\n",
				core);
		}
	}
	iounmap(p);
	return rc;
}

static void c3_restore_rnr(void)
{
	void __iomem *p;
	u32 core;

	if (!c3_rnr_written)
		return;

	p = ioremap(C3_RNR_BASE, 0x1000 * C3_RNR_MAX);
	if (p) {
		for (core = 0; core < c3_rnr_written; core++)
			writel(c3_rnr_save[core], p + core * 0x1000);
		iounmap(p);
	}
	c3_rnr_written = 0;
	pr_info("C3: RNR global enable restored\n");
}

/* ------------------------------------------------------------------ */
/* bit3b：BBH_RX 端口（+0x04/+0x30/+0x34，最后 +0x3c 使能）              */
/* ------------------------------------------------------------------ */

static int c3_write_bbh_rx(void)
{
	void __iomem *p;
	u32 port;
	int rc = 0;

	if (rdp_c3_rx_ports > C3_RX_MAX) {
		pr_err("C3: rx_ports %u > %u\n", rdp_c3_rx_ports, C3_RX_MAX);
		return -EINVAL;
	}

	p = ioremap(C3_BBH_RX_BASE, C3_BBH_RX_STRIDE * C3_RX_MAX);
	if (!p)
		return -ENOMEM;

	for (port = 0; port < rdp_c3_rx_ports; port++) {
		u32 base = port * C3_BBH_RX_STRIDE;
		u32 viq = port | (port << 8);
		u32 r_id, r_viq, r_30, r_34, r_64, r_ctrl;
		int target = (port == (rdp_c3_qdest_port & 0x1f));
		int ok;

		c3_rx_save[port][0] = readl(p + base + C3_BBH_RX_SBPM_ID);
		c3_rx_save[port][1] = readl(p + base + C3_BBH_RX_VIQ);
		c3_rx_save[port][2] = readl(p + base + C3_BBH_RX_0X30);
		c3_rx_save[port][3] = readl(p + base + C3_BBH_RX_0X34);
		c3_rx_save[port][4] = readl(p + base + C3_BBH_RX_0X64);
		c3_rx_save[port][5] = readl(p + base + C3_BBH_RX_CTRL);
		c3_rx_written = port + 1;

		/*
		 * 分支 6 完整顺序（目标端口 p=rdp_c3_qdest_port，默认 2）：
		 * +0x00 → +0x04 → +0x30 → +0x34 → +0x64 → +0x3c（最后）。
		 * 其他端口只写原来的 4 项，避免把同一 SBPM BB id 写到所有端口。
		 */
		if (target)
			writel(C3_BBH_RX_SBPM_ID_VAL,
			       p + base + C3_BBH_RX_SBPM_ID);
		writel(viq, p + base + C3_BBH_RX_VIQ);
		writel(0x12, p + base + C3_BBH_RX_0X30);
		writel(0xff, p + base + C3_BBH_RX_0X34);
		if (target)
			writel(C3_BBH_RX_0X64_VAL,
			       p + base + C3_BBH_RX_0X64);
		writel(0x3, p + base + C3_BBH_RX_CTRL);	/* 最后写 */

		r_id = readl(p + base + C3_BBH_RX_SBPM_ID);
		r_viq = readl(p + base + C3_BBH_RX_VIQ);
		r_30 = readl(p + base + C3_BBH_RX_0X30);
		r_34 = readl(p + base + C3_BBH_RX_0X34);
		r_64 = readl(p + base + C3_BBH_RX_0X64);
		r_ctrl = readl(p + base + C3_BBH_RX_CTRL);

		ok = (r_viq == viq && r_30 == 0x12 && r_34 == 0xff &&
		      r_ctrl == 0x3);
		if (target)
			ok = ok && (r_id == C3_BBH_RX_SBPM_ID_VAL &&
				    r_64 == C3_BBH_RX_0X64_VAL);

		pr_info("C3 BBH_RX[%u]%s +0x00=0x%08x +0x04=0x%08x +0x30=0x%08x +0x34=0x%08x +0x64=0x%08x +0x3c=0x%08x %s\n",
			port, target ? "(target)" : "", r_id, r_viq, r_30,
			r_34, r_64, r_ctrl, ok ? "OK" : "MISMATCH");

		if (!ok) {
			rc = -EIO;
			break;
		}
	}
	iounmap(p);
	return rc;
}

static void c3_restore_bbh_rx(void)
{
	void __iomem *p;
	u32 port;

	if (!c3_rx_written)
		return;

	p = ioremap(C3_BBH_RX_BASE, C3_BBH_RX_STRIDE * C3_RX_MAX);
	if (p) {
		for (port = 0; port < c3_rx_written; port++) {
			u32 base = port * C3_BBH_RX_STRIDE;
			int target = (port == (rdp_c3_qdest_port & 0x1f));

			/* 先关使能，再按写入逆序恢复 */
			writel(c3_rx_save[port][5], p + base + C3_BBH_RX_CTRL);
			if (target)
				writel(c3_rx_save[port][4],
				       p + base + C3_BBH_RX_0X64);
			writel(c3_rx_save[port][3], p + base + C3_BBH_RX_0X34);
			writel(c3_rx_save[port][2], p + base + C3_BBH_RX_0X30);
			writel(c3_rx_save[port][1], p + base + C3_BBH_RX_VIQ);
			if (target)
				writel(c3_rx_save[port][0],
				       p + base + C3_BBH_RX_SBPM_ID);
		}
		iounmap(p);
	}
	c3_rx_written = 0;
	pr_info("C3: BBH_RX ports restored\n");
}

/* ------------------------------------------------------------------ */
/* bit4：Q_DEST punt（只动一个 port 位）                                */
/* ------------------------------------------------------------------ */

static int c3_write_qdest(void)
{
	void __iomem *p;
	u32 old_en, old_dest, new_en, new_dest, rb;
	u32 bit = 1u << (rdp_c3_qdest_port & 0x1f);

	p = ioremap(C3_QDEST_BASE, 0x1000);
	if (!p)
		return -ENOMEM;

	old_en = readl(p + C3_VQEN_REG);
	old_dest = readl(p + C3_QDEST_REG);
	c3_qdest_save[0] = old_en;
	c3_qdest_save[1] = old_dest;
	c3_qdest_written = 1;

	pr_info("C3 QDEST baseline: VQ_EN(0x004)=0x%08x Q_DEST(0x4c0)=0x%08x port=%u bit=0x%08x\n",
		old_en, old_dest, rdp_c3_qdest_port, bit);

	/* ① 先关 VIQ 的使能门 */
	new_en = old_en & ~bit;
	writel(new_en, p + C3_VQEN_REG);
	rb = readl(p + C3_VQEN_REG);
	pr_info("C3 QDEST step1 VQ_EN 0x004 old=0x%08x wrote=0x%08x readback=0x%08x %s\n",
		old_en, new_en, rb, (rb == new_en) ? "OK" : "MISMATCH");
	if (rb != new_en) {
		iounmap(p);
		return -EIO;
	}
	c3_vqen_step[0] = rb;

	/* ② 改 Q_DEST（bit=VIQ） */
	new_dest = old_dest | bit;
	writel(new_dest, p + C3_QDEST_REG);
	rb = readl(p + C3_QDEST_REG);
	pr_info("C3 QDEST step2 0x4c0 old=0x%08x wrote=0x%08x readback=0x%08x %s\n",
		old_dest, new_dest, rb, (rb == new_dest) ? "OK" : "MISMATCH");
	if (rb != new_dest) {
		iounmap(p);
		return -EIO;
	}

	/* ③ 再开 VIQ 的使能门 */
	writel(new_en | bit, p + C3_VQEN_REG);
	rb = readl(p + C3_VQEN_REG);
	pr_info("C3 QDEST step3 VQ_EN 0x004 wrote=0x%08x readback=0x%08x %s\n",
		new_en | bit, rb, (rb == (new_en | bit)) ? "OK" : "MISMATCH");
	if (rb != (new_en | bit)) {
		iounmap(p);
		return -EIO;
	}
	c3_vqen_step[1] = rb;

	/* 只读 0x604（RNR group 掩码），确认没有动过 */
	pr_info("C3 QDEST info: 0x600=0x%08x 0x604=0x%08x (read-only)\n",
		readl(p + C3_QDEST_MASK_REG),
		readl(p + C3_QDEST_MASK_REG + 4));

	iounmap(p);
	return 0;
}

static void c3_restore_qdest(void)
{
	void __iomem *p;

	if (!c3_qdest_written)
		return;

	p = ioremap(C3_QDEST_BASE, 0x1000);
	if (p) {
		/* 先关门，再恢复 Q_DEST，最后恢复 VQ_EN 原值 */
		writel(c3_vqen_step[0], p + C3_VQEN_REG);
		writel(c3_qdest_save[1], p + C3_QDEST_REG);
		writel(c3_qdest_save[0], p + C3_VQEN_REG);
		iounmap(p);
	}
	c3_qdest_written = 0;
	pr_info("C3: VQ_EN/Q_DEST restored\n");
}

/* ------------------------------------------------------------------ */
/* bit6：RXQ 表（离线骨架；格式等分支 6 确认后再填默认值）              */
/* ------------------------------------------------------------------ */

#define C3_RXQ_MAX	16

static u32 c3_rxq_buf_val[C3_RXQ_MAX];
static u32 c3_rxq_buf_save[C3_RXQ_MAX];
static int c3_rxq_buf_written;
static u32 c3_rxq_dtype_val[C3_RXQ_MAX];
static u32 c3_rxq_dtype_save[C3_RXQ_MAX];
static int c3_rxq_dtype_written;

static int c3_parse_u32_list(const char *s, u32 *out, int max)
{
	char *buf, *p, *tok;
	int n = 0;

	if (!s || !*s)
		return 0;

	buf = kstrdup(s, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	p = buf;
	while ((tok = strsep(&p, ",")) != NULL) {
		u32 v;

		if (!*tok)
			continue;
		if (kstrtou32(tok, 0, &v)) {
			kfree(buf);
			return -EINVAL;
		}
		if (n >= max) {
			kfree(buf);
			return -E2BIG;
		}
		out[n++] = v;
	}
	kfree(buf);
	return n;
}

static int c3_write_rxq_table(const char *name, uint base, uint count,
			      const char *values, u32 *val, u32 *save,
			      int *written)
{
	void __iomem *p;
	int n, i, rc = 0;

	n = c3_parse_u32_list(values, val, C3_RXQ_MAX);
	if (n < 0) {
		pr_err("C3 RXQ: %s value parse failed (%d)\n", name, n);
		return n;
	}
	if (!n) {
		pr_info("C3 RXQ: %s no values, skip (format pending)\n", name);
		return 0;
	}
	if (count && n != (int)count) {
		pr_err("C3 RXQ: %s value count %d != count param %u\n",
			name, n, count);
		return -EINVAL;
	}

	p = ioremap(base, n * 4);
	if (!p)
		return -ENOMEM;

	*written = 0;
	for (i = 0; i < n; i++) {
		u32 rb;

		save[i] = readl(p + i * 4);
		writel(val[i], p + i * 4);
		*written = i + 1;
		rb = readl(p + i * 4);
		pr_info("C3 RXQ: %s[%d] 0x%05x old=0x%08x wrote=0x%08x readback=0x%08x %s\n",
			name, i, base + i * 4, save[i], val[i], rb,
			(rb == val[i]) ? "OK" : "MISMATCH");
		if (rb != val[i]) {
			rc = -EIO;
			break;
		}
	}
	iounmap(p);
	return rc;
}

static int c3_write_rxq(void)
{
	int rc;

	/*
	 * 顺序：先建 RXQ 表，再由 bit4 打开 Q_DEST/VQ_EN 的门。
	 * 注意：表项格式/长度尚未由分支 6 确认，本函数在值为空时只打印、
	 * 不写任何寄存器（避免猜写）。
	 */
	rc = c3_write_rxq_table("buf_cfg", rdp_c3_rxq_buf_base,
				rdp_c3_rxq_buf_count, rdp_c3_rxq_buf_cfg,
				c3_rxq_buf_val, c3_rxq_buf_save,
				&c3_rxq_buf_written);
	if (rc)
		return rc;

	return c3_write_rxq_table("data_type", rdp_c3_rxq_dtype_base,
				  rdp_c3_rxq_dtype_count, rdp_c3_rxq_data_type,
				  c3_rxq_dtype_val, c3_rxq_dtype_save,
				  &c3_rxq_dtype_written);
}

static void c3_restore_rxq(void)
{
	void __iomem *p;
	int i;

	if (!c3_rxq_buf_written && !c3_rxq_dtype_written)
		return;

	if (c3_rxq_buf_written) {
		p = ioremap(rdp_c3_rxq_buf_base, c3_rxq_buf_written * 4);
		if (p) {
			for (i = 0; i < c3_rxq_buf_written; i++)
				writel(c3_rxq_buf_save[i], p + i * 4);
			iounmap(p);
		}
		c3_rxq_buf_written = 0;
	}

	if (c3_rxq_dtype_written) {
		p = ioremap(rdp_c3_rxq_dtype_base, c3_rxq_dtype_written * 4);
		if (p) {
			for (i = 0; i < c3_rxq_dtype_written; i++)
				writel(c3_rxq_dtype_save[i], p + i * 4);
			iounmap(p);
		}
		c3_rxq_dtype_written = 0;
	}

	pr_info("C3: RXQ tables restored\n");
}

/* ------------------------------------------------------------------ */
/* ring 写指针观察：0x82703000 + id*0x10 + 6（大端 u16）                 */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* C3-b 诊断：ring 表项 / UNIMAC mac2 / BBH_RX2 / VQ_EN/Q_DEST          */
/* ------------------------------------------------------------------ */

static void c3_diag_dump(int tick)
{
	void __iomem *p;
	int i;

	if (!rdp_c3_ring_base)
		return;

	pr_info("C3 DIAG[%d] BEGIN\n", tick);

	p = ioremap(rdp_c3_ring_base, C3_RING_COUNT * 0x10);
	if (p) {
		for (i = 0; i < C3_RING_COUNT; i++)
			pr_info("C3 DIAG[%d] ring%d: %08x %08x %08x %08x\n",
				tick, i,
				readl(p + i * 0x10),
				readl(p + i * 0x10 + 0x4),
				readl(p + i * 0x10 + 0x8),
				readl(p + i * 0x10 + 0xc));
		iounmap(p);
	}

	/* UNIMAC mac2 (LAN2)：0x828aa000 */
	p = ioremap(0x828aa000, 0x1000);
	if (p) {
		pr_info("C3 DIAG[%d] mac2 CMD=%08x IPG=%08x MAC0=%08x MAC1=%08x FRM=%08x PAUSEQ=%08x\n",
			tick, readl(p + 0x008), readl(p + 0x004),
			readl(p + 0x00c), readl(p + 0x010),
			readl(p + 0x014), readl(p + 0x018));
		for (i = 0; i < 32; i += 4)
			pr_info("C3 DIAG[%d] mac2 %03x: %08x %08x %08x %08x\n",
				tick, 0x700 + i * 4,
				readl(p + 0x700 + i * 4),
				readl(p + 0x700 + i * 4 + 0x4),
				readl(p + 0x700 + i * 4 + 0x8),
				readl(p + 0x700 + i * 4 + 0xc));
		/* §4.8：RMON 计数器在 0x400–0x61c（read-to-clear） */
		for (i = 0; i < 0x220; i += 0x10)
			pr_info("C3 MIB[%d] %03x: %08x %08x %08x %08x\n",
				tick, 0x400 + i,
				readl(p + 0x400 + i),
				readl(p + 0x400 + i + 0x4),
				readl(p + 0x400 + i + 0x8),
				readl(p + 0x400 + i + 0xc));
		pr_info("C3 MIB[%d] STATUS=%08x CONTROL=%08x MIBCTRL=%08x\n",
			tick, readl(p + 0x758), readl(p + 0x740),
			readl(p + 0x680));
		iounmap(p);
	}

	/* BBH_RX port 2：0x82898000 + 2*0x400；PM 计数 +0x100/+0x11c/+0x124 */
	p = ioremap(0x82898000, 0x1000);
	if (p) {
		pr_info("C3 DIAG[%d] BBH_RX2 +04=%08x +3c=%08x PM +100=%08x +11c=%08x +124=%08x\n",
			tick, readl(p + 0x800 + 0x04), readl(p + 0x800 + 0x3c),
			readl(p + 0x800 + 0x100), readl(p + 0x800 + 0x11c),
			readl(p + 0x800 + 0x124));
		iounmap(p);
	}

	p = ioremap(0x82880000, 0x1000);
	if (p) {
		pr_info("C3 DIAG[%d] VQ_EN=%08x QDEST=%08x 600=%08x 604=%08x\n",
			tick, readl(p + 0x004), readl(p + 0x4c0),
			readl(p + 0x600), readl(p + 0x604));
		iounmap(p);
	}

	pr_info("C3 DIAG[%d] END\n", tick);
}

static void c3_watch_fn(struct work_struct *work)
{
	void __iomem *p;
	int i;

	(void)work;

	if (!rdp_c3_ring_base)
		return;

	p = ioremap(rdp_c3_ring_base, C3_RING_COUNT * 0x10);
	if (!p)
		return;

	pr_info("C3 watch[%d] wrptr:", c3_watch_ticks);
	for (i = 0; i < C3_RING_COUNT; i++) {
		u32 w1 = readl(p + i * 0x10 + 0x4);
		u16 be = (u16)(w1 >> 16);	/* +0x06 的大端 u16 */
		u16 ptr = swab16(be);

		pr_cont(" r%d=%04x", i, ptr);
	}
	pr_cont("\n");
	iounmap(p);

	if (c3_watch_ticks == 0 || c3_watch_ticks == 30 ||
	    c3_watch_ticks + 5 == rdp_c3_watch_secs)
		c3_diag_dump(c3_watch_ticks);

	if (++c3_watch_ticks < rdp_c3_watch_secs)
		queue_delayed_work(system_wq, &c3_watch,
				   msecs_to_jiffies(1000));
}

static void c3_watch_start(void)
{
	if (c3_watch_started || !rdp_c3_ring_base)
		return;

	INIT_DELAYED_WORK(&c3_watch, c3_watch_fn);
	c3_watch_ticks = 0;
	c3_watch_started = 1;
	queue_delayed_work(system_wq, &c3_watch, msecs_to_jiffies(1000));
}

static void c3_watch_stop(void)
{
	if (!c3_watch_started)
		return;

	cancel_delayed_work_sync(&c3_watch);
	c3_watch_started = 0;
}

/* ------------------------------------------------------------------ */
/* 入口 / 恢复                                                          */
/* ------------------------------------------------------------------ */

void rdp_c3_restore(void)
{
	void __iomem *p;

	c3_watch_stop();

	c3_restore_qdest();
	c3_restore_rxq();
	c3_restore_bbh_rx();
	c3_restore_rnr();
	c3_restore_dsptchr();

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

	if (rdp_c3 & C3_RING_BIT)
		rc = c3_write_ring_table();
	if (!rc && (rdp_c3 & C3_BBH_BIT))
		rc = c3_write_bbh_tx();
	if (!rc && (rdp_c3 & C3_DSP_BIT))
		rc = c3_write_dsptchr();
	if (!rc && (rdp_c3 & C3_RNR_BIT))
		rc = c3_write_rnr();
	if (!rc && (rdp_c3 & C3_RNR_BIT))
		rc = c3_write_bbh_rx();
	if (!rc && (rdp_c3 & C3_RXQ_BIT))
		rc = c3_write_rxq();
	if (!rc && (rdp_c3 & C3_QDEST_BIT))
		rc = c3_write_qdest();

	if (rc) {
		pr_err("C3: failed rc=%d, restoring\n", rc);
		rdp_c3_restore();
		return;
	}
	if (rdp_c3 & (C3_RING_BIT | C3_BBH_BIT | C3_DSP_BIT | C3_RNR_BIT |
		      C3_RXQ_BIT | C3_QDEST_BIT))
		pr_info("C3: done (ring=%d bbh_tx=%d dsp=%d rnr/rx=%d rxq=%d qdest=%d)\n",
			!!(rdp_c3 & C3_RING_BIT), !!(rdp_c3 & C3_BBH_BIT),
			!!(rdp_c3 & C3_DSP_BIT), !!(rdp_c3 & C3_RNR_BIT),
			!!(rdp_c3 & C3_RXQ_BIT), !!(rdp_c3 & C3_QDEST_BIT));

	/* ring 写指针观察：insmod 返回后每秒打印，持续 rdp_c3_watch_secs 秒 */
	if (!rc && (rdp_c3 & C3_RING_BIT))
		c3_watch_start();
}
