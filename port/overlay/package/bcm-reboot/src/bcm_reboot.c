// SPDX-License-Identifier: GPL-2.0
/*
 * BCM7038 看门狗复位（HG3142F / BCM96855）
 *
 * 主线 bcm7038_wdt 只做喂狗，没有 restart handler，`reboot` 会走到
 * machine_restart() 后 "Reboot failed -- System halted"，只能断电。
 *
 * 这里注册 restart_handler：复位时先停表、设 1 秒、再启动计数，
 * 让 WDT 硬件把 SoC 复位（厂商 /periph/wdt-reboot 用的就是这条路）。
 *
 * 寄存器（实机 asic 基址 0xff800480，与厂商 bcm7038_wdt.c 一致）：
 *   0x00 WDT_TIMEOUT : 计数值 = rate * seconds
 *   0x04 WDT_CMD     : 0xff00,0x00ff = 启动/喂狗；0xee00,0x00ee = 停止
 */
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/reboot.h>

#define BCM_WDT_BASE		0xff800480UL
#define WDT_TIMEOUT_REG		0x00
#define WDT_CMD_REG		0x04

#define WDT_START_1		0xff00
#define WDT_START_2		0x00ff
#define WDT_STOP_1		0xee00
#define WDT_STOP_2		0x00ee

/* 厂商驱动默认 27MHz；即使实际时钟不同，1 "秒" 也只会变成 0.5~1.x 秒，够用 */
#define WDT_RATE		27000000
#define WDT_RESET_SECS		1

static void __iomem *bcm_wdt_base;
static struct notifier_block bcm_reboot_nb;

static void bcm_wdt_cmd(u32 a, u32 b)
{
	writel(a, bcm_wdt_base + WDT_CMD_REG);
	writel(b, bcm_wdt_base + WDT_CMD_REG);
}

static int bcm_reboot_handler(struct notifier_block *nb,
			      unsigned long action, void *data)
{
	if (!bcm_wdt_base)
		return NOTIFY_DONE;

	pr_emerg("bcm-reboot: arming WDT (%d s) to reset SoC\n", WDT_RESET_SECS);

	/* 表在跑的时候不能改超时值，先停 */
	bcm_wdt_cmd(WDT_STOP_1, WDT_STOP_2);
	writel(WDT_RATE * WDT_RESET_SECS, bcm_wdt_base + WDT_TIMEOUT_REG);
	bcm_wdt_cmd(WDT_START_1, WDT_START_2);

	/* 等硬件复位；不要返回，返回后内核会走 halt */
	mdelay(5000);

	pr_emerg("bcm-reboot: WDT did not fire within 5s\n");
	return NOTIFY_DONE;
}

static int __init bcm_reboot_init(void)
{
	bcm_wdt_base = ioremap(BCM_WDT_BASE, 0x10);
	if (!bcm_wdt_base) {
		pr_err("bcm-reboot: ioremap 0x%lx failed\n", (unsigned long)BCM_WDT_BASE);
		return -ENOMEM;
	}

	bcm_reboot_nb.notifier_call = bcm_reboot_handler;
	bcm_reboot_nb.priority = 255;

	if (register_restart_handler(&bcm_reboot_nb)) {
		iounmap(bcm_wdt_base);
		bcm_wdt_base = NULL;
		pr_err("bcm-reboot: register_restart_handler failed\n");
		return -EINVAL;
	}

	pr_info("bcm-reboot: restart handler ready (WDT @0x%lx)\n",
		(unsigned long)BCM_WDT_BASE);
	return 0;
}

static void __exit bcm_reboot_exit(void)
{
	unregister_restart_handler(&bcm_reboot_nb);
	if (bcm_wdt_base) {
		iounmap(bcm_wdt_base);
		bcm_wdt_base = NULL;
	}
}

module_init(bcm_reboot_init);
module_exit(bcm_reboot_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("BCM7038 watchdog restart handler for BCM96855 (HG3142F)");
