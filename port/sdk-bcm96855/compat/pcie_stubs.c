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
