/* SPDX-License-Identifier: GPL-2.0 */
/*
 * rdp_c2.c —— C2-a：FPM 池 + QM 全局配置写入/读回（不建 ring、不使能 FPM）
 *
 * 依据 docs/REVERSE-ENET.md §7.2/§7.4 与 docs/C-PLAN.md §2.1：
 *   · QM_GLOBAL_CFG_FPM_BASE_ADDR_REG        QM+0x034
 *   · QM_GLOBAL_CFG_FPM_COHERENT_BASE_ADDR   QM+0x038
 *   · QM_GLOBAL_CFG_DDR_SOP_OFFSET           QM+0x03c
 *   · QM_FPM_POOLS_THR (pool0)               QM+0x200
 *   · QM_FPM_USR_GRP_*_THR (group0)          QM+0x280/0x284/0x288
 * 偏移来自 rdpa.ko 的 AG 表（tools/ko_scan.py regtab）。
 *
 * 只做 C2-a：不写 QM_ENABLE(0x00)、不写 FPM_CONTROL(0x0c)、不建 ring、
 * 不分配描述符、不碰 BBH/RNR/DSPTCHR/UNIMAC。FPM 保持关闭，所有写入
 * 在 rmmod 时恢复原值。
 *
 * 门控：insmod bcm_enet.ko rdp_c2=1（默认 0）。
 */
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/io.h>
#include <linux/moduleparam.h>
#include <asm/memory.h>		/* virt_to_phys（armv7 低端内存） */

static int rdp_c2;
module_param_named(rdp_c2, rdp_c2, int, 0444);
MODULE_PARM_DESC(rdp_c2, "1 = C2-a: program FPM base/thresholds + QM cfg (readback)");

#define QM_BASE		0x82c00000u
#define QM_WINDOW	0x1000u

#define C2_POOL_SIZE	(256u * 1024u)	/* 256 KiB，按 2 KiB/buffer 切 */
#define C2_BUF_SIZE	2048u
#define C2_TOKENS	(C2_POOL_SIZE / C2_BUF_SIZE)

/* rdd_defs.h: RDD_DDR_SOP_OFFSET0=0, RDD_DDR_SOP_OFFSET1=88 */
#define C2_DDR_SOP	((0u & 0x7ffu) | ((88u & 0x7ffu) << 16))

struct c2_reg {
	u32 off;
	const char *name;
	u32 val;
	u32 old;
	int written;
};

static struct c2_reg c2_regs[] = {
	{ 0x034, "FPM_BASE",	0 },		/* 运行时填 pool phys */
	{ 0x038, "FPM_COHERENT", 0 },		/* 同上 */
	{ 0x03c, "DDR_SOP",	C2_DDR_SOP },
	{ 0x200, "FPM_POOLS_THR0", C2_TOKENS },
	{ 0x280, "UG0_LOWER",	C2_TOKENS / 2 },
	{ 0x284, "UG0_MID",	C2_TOKENS * 3 / 4 },
	{ 0x288, "UG0_HIGHER",	C2_TOKENS * 7 / 8 },
};

static void *c2_pool;
static u32 c2_pool_phys;
static int c2_active;

void rdp_c2_restore(void)
{
	void __iomem *qm;
	int i;

	if (!c2_active)
		return;

	qm = ioremap(QM_BASE, QM_WINDOW);
	if (qm) {
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

	free_pages_exact(c2_pool, C2_POOL_SIZE);
	c2_pool = NULL;
	c2_pool_phys = 0;
	c2_active = 0;
	pr_info("C2a: pool released, registers restored\n");
}

void rdp_c2_run(void)
{
	void __iomem *qm;
	int i, stop = 0;

	if (!rdp_c2)
		return;

	c2_pool = alloc_pages_exact(C2_POOL_SIZE, GFP_KERNEL | __GFP_ZERO);
	if (!c2_pool) {
		pr_err("C2a: alloc_pages_exact(%u) failed, skip\n", C2_POOL_SIZE);
		return;
	}
	c2_pool_phys = (u32)virt_to_phys(c2_pool);

	c2_regs[0].val = c2_pool_phys;
	c2_regs[1].val = c2_pool_phys;

	pr_info("C2a: pool=%p phys=0x%08x size=%u buf=%u tokens=%u\n",
		c2_pool, c2_pool_phys, C2_POOL_SIZE, C2_BUF_SIZE, C2_TOKENS);

	qm = ioremap(QM_BASE, QM_WINDOW);
	if (!qm) {
		pr_err("C2a: ioremap QM failed\n");
		rdp_c2_restore();
		return;
	}

	/* 只读记录：确认 FPM/QM 仍是关闭状态 */
	pr_info("C2a: before QM_ENABLE=0x%08x FPM_CTRL=0x%08x\n",
		readl(qm + 0x000), readl(qm + 0x00c));

	for (i = 0; i < ARRAY_SIZE(c2_regs); i++) {
		struct c2_reg *r = &c2_regs[i];
		u32 rb;

		r->old = readl(qm + r->off);
		writel(r->val, qm + r->off);
		rb = readl(qm + r->off);
		r->written = 1;

		pr_info("C2a %-14s 0x%03x wrote=0x%08x readback=0x%08x %s\n",
			r->name, r->off, r->val, rb,
			(rb == r->val) ? "OK" : "MISMATCH");

		if (rb != r->val) {
			/* 立即回滚这一个寄存器，后面的不再写 */
			writel(r->old, qm + r->off);
			pr_err("C2a: mismatch at %s, stop (restored)\n",
			       r->name);
			stop = 1;
			break;
		}
	}

	pr_info("C2a: after QM_ENABLE=0x%08x FPM_CTRL=0x%08x%s\n",
		readl(qm + 0x000), readl(qm + 0x00c),
		stop ? " (stopped)" : "");

	iounmap(qm);

	if (stop)
		rdp_c2_restore();
	else
		c2_active = 1;
}
