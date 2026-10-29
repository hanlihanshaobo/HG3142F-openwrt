/* SPDX-License-Identifier: GPL-2.0 */
/*
 * rdp_c2.c —— C2-a/C2-b：FPM 池 + QM 全局配置 + RX 缓冲/ring 准备
 *
 * 依据 docs/REVERSE-ENET.md §5.2/§5.3/§7.2/§7.4 与 docs/C-PLAN.md §2：
 *
 * C2-a（已验证）：QM+0x034/0x038 FPM base/coherent、0x03c DDR_SOP、
 *                 0x280/0x284/0x288 UG0 阈值、0x200 POOLS_THR0（7bit）。
 * C2-b（本轮）：使能 FPM（QM+0x0c bit0）、建 8 个 RX buffer descriptor
 *               （格式 1，8B，地址/偏移大端 + owner bit）、准备 8 项
 *               CPU ring（16B 表项，物理地址大端）。
 *
 * 边界：不写 BBH_TX/DSPTCHR/RNR（0x82803000 的 ring 表只做只读探测，
 * 因为它的基址 0x3000 + rdp_runner_core_addr[0] 依赖 rdpa.ko 运行时
 * 变量，未确认前不写）；不发包、不注册中断、不建 QM 队列上下文。
 *
 * 门控：insmod bcm_enet.ko rdp_c2=1（默认 0）。
 */
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/io.h>
#include <linux/swab.h>
#include <linux/moduleparam.h>
#include <asm/memory.h>		/* virt_to_phys（armv7 低端内存） */

static int rdp_c2;
module_param_named(rdp_c2, rdp_c2, int, 0444);
MODULE_PARM_DESC(rdp_c2, "1 = C2: FPM/QM cfg + RX buffers + ring prep (readback)");

#define QM_BASE		0x82c00000u
#define QM_WINDOW	0x1000u

#define C2_POOL_SIZE	(256u * 1024u)	/* 256 KiB，按 2 KiB/buffer 切 */
#define C2_BUF_SIZE	2048u
#define C2_TOKENS	(C2_POOL_SIZE / C2_BUF_SIZE)

/* rdd_defs.h: RDD_DDR_SOP_OFFSET0=0, RDD_DDR_SOP_OFFSET1=88 */
#define C2_DDR_SOP	((0u & 0x7ffu) | ((88u & 0x7ffu) << 16))

/* CPU ring 准备（§5.2）：8 项、16B 表项；表地址待确认，只读探测 */
#define C2_RING_ENTRIES		8
#define C2_RING_ENTRY_SIZE	16
#define C2_RING_BYTES		(C2_RING_ENTRIES * C2_RING_ENTRY_SIZE)
#define C2_RING_TABLE_BASE	0x82803000u	/* 0x3000 + core_addr[0] 推测 */

struct c2_reg {
	u32 off;
	const char *name;
	u32 val;
	u32 old;
	int written;
};

static struct c2_reg c2_regs[] = {
	{ 0x034, "FPM_BASE",	0 },
	{ 0x038, "FPM_COHERENT", 0 },
	{ 0x03c, "DDR_SOP",	C2_DDR_SOP },
	{ 0x280, "UG0_LOWER",	C2_TOKENS / 2 },
	{ 0x284, "UG0_MID",	C2_TOKENS * 3 / 4 },
	{ 0x288, "UG0_HIGHER",	C2_TOKENS * 7 / 8 },
	{ 0x200, "FPM_POOLS_THR0", 64 },	/* 7 bit 字段，最大 127 */
};

static void *c2_pool;
static u32 c2_pool_phys;
static void *c2_ring;
static u32 c2_ring_phys;
static void *c2_rxdesc;
static u32 c2_rxdesc_phys;
static u32 c2_fpm_ctrl_old;
static int c2_fpm_enabled;
static int c2_active;

void rdp_c2_restore(void)
{
	void __iomem *qm;
	int i;

	if (!c2_active && !c2_pool)
		return;

	qm = ioremap(QM_BASE, QM_WINDOW);
	if (qm) {
		/* 先关 FPM，再恢复其余寄存器 */
		if (c2_fpm_enabled) {
			writel(c2_fpm_ctrl_old, qm + 0x00c);
			pr_info("C2b restore FPM_CTRL 0x00c -> 0x%08x\n",
				readl(qm + 0x00c));
			c2_fpm_enabled = 0;
		}
		for (i = 0; i < ARRAY_SIZE(c2_regs); i++) {
			if (c2_regs[i].written) {
				writel(c2_regs[i].old, qm + c2_regs[i].off);
				pr_info("C2a restore %-14s 0x%03x -> 0x%08x\n",
					c2_regs[i].name, c2_regs[i].off,
					readl(qm + c2_regs[i].off));
			}
		}
		iounmap(qm);
	}

	if (c2_rxdesc) {
		free_pages_exact(c2_rxdesc, 4096);
		c2_rxdesc = NULL;
	}
	if (c2_ring) {
		free_pages_exact(c2_ring, 4096);
		c2_ring = NULL;
	}
	if (c2_pool) {
		free_pages_exact(c2_pool, C2_POOL_SIZE);
		c2_pool = NULL;
	}
	c2_pool_phys = 0;
	c2_active = 0;
	pr_info("C2: pools released, registers restored\n");
}

/* §5.3 格式 1（8 B）：+0 = bswap32(buf_phys) | 0x40000(OWN)，+4 = bswap32(fpm_off) */
static void c2_build_rx_descriptors(void)
{
	u8 *d;
	int i;

	c2_rxdesc = alloc_pages_exact(4096, GFP_KERNEL | __GFP_ZERO);
	if (!c2_rxdesc) {
		pr_err("C2b: alloc rxdesc failed\n");
		return;
	}
	c2_rxdesc_phys = (u32)virt_to_phys(c2_rxdesc);
	d = c2_rxdesc;

	for (i = 0; i < 8; i++) {
		u32 buf = c2_pool_phys + i * C2_BUF_SIZE;
		u32 off = i * C2_BUF_SIZE;
		u32 w0 = swab32(buf) | 0x40000u;
		u32 w1 = swab32(off);
		u8 *p = d + i * 8;

		*(u32 *)(p + 0) = w0;
		*(u32 *)(p + 4) = w1;
	}

	/* 回读校验：地址/偏移/owner 位 */
	for (i = 0; i < 8; i++) {
		u8 *p = d + i * 8;
		u32 w0 = *(u32 *)(p + 0);
		u32 w1 = *(u32 *)(p + 4);
		u32 buf = swab32(w0 & ~0x40000u);
		u32 off = swab32(w1);
		u32 exp_buf = c2_pool_phys + i * C2_BUF_SIZE;

		pr_info("C2b rxdesc[%d] w0=0x%08x w1=0x%08x buf=0x%08x off=0x%08x own=%d %s\n",
			i, w0, w1, buf, off, !!(w0 & 0x40000),
			(buf == exp_buf && off == (u32)(i * C2_BUF_SIZE)) ?
			"OK" : "MISMATCH");

		if (buf != exp_buf || !(w0 & 0x40000))
			return;
	}
	pr_info("C2b: 8 RX descriptors built at %p phys=0x%08x\n",
		c2_rxdesc, c2_rxdesc_phys);
}

static void c2_enable_fpm(void __iomem *qm)
{
	u32 old = readl(qm + 0x00c);
	u32 new = old | 1u;		/* bit0 = FPM enable */
	u32 rb;

	c2_fpm_ctrl_old = old;
	writel(new, qm + 0x00c);
	rb = readl(qm + 0x00c);

	pr_info("C2b FPM_CTRL 0x00c old=0x%08x wrote=0x%08x readback=0x%08x %s\n",
		old, new, rb, (rb == new) ? "OK" : "MISMATCH");

	if (rb == new)
		c2_fpm_enabled = 1;
}

/* 只读探测疑似 ring 表位置（0x3000 + core_addr[0]），不写 */
static void c2_peek_ring_table(void)
{
	void __iomem *p;
	u32 w[4];

	p = ioremap(C2_RING_TABLE_BASE, 0x1000);
	if (!p) {
		pr_err("C2b: ioremap ring table 0x%x failed\n",
		       C2_RING_TABLE_BASE);
		return;
	}

	w[0] = readl(p + 0x00);
	w[1] = readl(p + 0x04);
	w[2] = readl(p + 0x08);
	w[3] = readl(p + 0x0c);
	pr_info("C2b ring-table? 0x%08x: %08x %08x %08x %08x (read-only)\n",
		C2_RING_TABLE_BASE, w[0], w[1], w[2], w[3]);
	iounmap(p);
}

/* 准备 ring 描述符（在本地内存里，不写 0x82803000）并打印 */
static void c2_build_ring_desc(void)
{
	u16 *r;
	u32 n32 = (C2_RING_ENTRIES + 31) >> 5;

	c2_ring = alloc_pages_exact(4096, GFP_KERNEL | __GFP_ZERO);
	if (!c2_ring) {
		pr_err("C2b: alloc ring failed\n");
		return;
	}
	c2_ring_phys = (u32)virt_to_phys(c2_ring);
	r = c2_ring;

	r[0] = (u16)(0 | (0 << 3) | ((n32 & 0xff) << 8));	/* ring 0, prio 0 */
	r[1] = (u16)swab16((u16)C2_RING_ENTRY_SIZE);		/* entry size, BE */
	r[3] = 0;						/* write ptr */
	*(u32 *)(r + 4) = swab32(c2_ring_phys);			/* phys base, BE */
	r[6] = 0;						/* read ptr */
	*((u8 *)r + 0x0f) = 0;

	pr_info("C2b ring: entries=%u entry_size=%u phys=0x%08x desc=%08x %08x %08x %08x (not programmed)\n",
		C2_RING_ENTRIES, C2_RING_ENTRY_SIZE, c2_ring_phys,
		((u32 *)r)[0], ((u32 *)r)[1], ((u32 *)r)[2], ((u32 *)r)[3]);
}

void rdp_c2_run(void)
{
	void __iomem *qm;
	int i, stop = 0;

	if (!rdp_c2)
		return;

	c2_pool = alloc_pages_exact(C2_POOL_SIZE, GFP_KERNEL | __GFP_ZERO);
	if (!c2_pool) {
		pr_err("C2: alloc_pages_exact(%u) failed, skip\n", C2_POOL_SIZE);
		return;
	}
	c2_pool_phys = (u32)virt_to_phys(c2_pool);
	c2_active = 1;

	c2_regs[0].val = c2_pool_phys;
	c2_regs[1].val = c2_pool_phys;

	pr_info("C2: pool=%p phys=0x%08x size=%u buf=%u tokens=%u\n",
		c2_pool, c2_pool_phys, C2_POOL_SIZE, C2_BUF_SIZE, C2_TOKENS);

	qm = ioremap(QM_BASE, QM_WINDOW);
	if (!qm) {
		pr_err("C2: ioremap QM failed\n");
		rdp_c2_restore();
		return;
	}

	pr_info("C2a: before QM_ENABLE=0x%08x FPM_CTRL=0x%08x\n",
		readl(qm + 0x000), readl(qm + 0x00c));

	for (i = 0; i < ARRAY_SIZE(c2_regs); i++) {
		struct c2_reg *r = &c2_regs[i];
		u32 rb;

		r->old = readl(qm + r->off);
		writel(r->val, qm + r->off);
		rb = readl(qm + r->off);
		r->written = 1;

		pr_info("C2a %-14s 0x%03x old=0x%08x wrote=0x%08x readback=0x%08x %s\n",
			r->name, r->off, r->old, r->val, rb,
			(rb == r->val) ? "OK" : "MISMATCH");

		if (rb != r->val) {
			writel(r->old, qm + r->off);
			pr_err("C2a: mismatch at %s, stop (restored)\n",
			       r->name);
			stop = 1;
			break;
		}
	}

	if (!stop) {
		/* C2-b：FPM 使能 + RX buffer/ring 准备 */
		c2_enable_fpm(qm);
		c2_build_rx_descriptors();
		c2_build_ring_desc();
		c2_peek_ring_table();
	}

	pr_info("C2: after QM_ENABLE=0x%08x FPM_CTRL=0x%08x%s\n",
		readl(qm + 0x000), readl(qm + 0x00c),
		stop ? " (stopped)" : "");

	iounmap(qm);

	if (stop)
		rdp_c2_restore();
}
