/* SPDX-License-Identifier: GPL-2.0 */
/*
 * bcmkernel_stubs.c —— bcm_enet 依赖的 bcmkernel 小工具函数
 *
 * 这几个符号来自 Broadcom 的内核补丁（board.h 声明），在 4.19 的 bcmkernel
 * 里由 kernel/linux/.../bcm_bootstate / boardparms 提供。上游 6.18 没有，
 * 但 enet 在初始化/建 netdev 时必须调它们，否则 insmod 直接 Unknown symbol。
 *
 * 语义取舍：
 *   · kerSysGetMacAddress：厂商从 NVRAM 的 base MAC + 偏移分配。我们这里
 *     没有 nvram 接口，先返回一个 **本地管理地址**（02:10:18:xx:xx:xx），
 *     由 ulId 区分。TODO: 后续从 DT / factory 分区读真实 MAC。
 *   · kerSysReleaseMacAddress：厂商是归还地址池，我们是静态生成 → no-op。
 *   · DyingGasp：掉电告警回调，需要 GPON/PON 驱动配合，探测阶段先 no-op。
 */
#include <linux/kernel.h>
#include <linux/if_ether.h>
#include <linux/errno.h>

/*
 * 原型照抄 board.h（不直接 include 它：board.h 还会拖进一堆板级外部符号，
 * 这个文件只需要这四个）。签名一旦与 board.h 不一致，链接期也会因为
 * C 无重载而静默错位，所以这里保持逐字一致。
 */
extern int kerSysGetMacAddress(unsigned char *pucaAddr, unsigned long ulId);
extern int kerSysReleaseMacAddress(unsigned char *pucaAddr);
extern void kerSysRegisterDyingGaspHandlerV2(char *devname, void *cbfn,
					     void *context);
extern void kerSysDeregisterDyingGaspHandler(char *devname);

int kerSysGetMacAddress(unsigned char *pucaAddr, unsigned long ulId)
{
	if (!pucaAddr)
		return -EINVAL;

	/* 02:10:18 是本地管理地址（第一字节 bit1=1），不会和厂牌 MAC 冲突 */
	pucaAddr[0] = 0x02;
	pucaAddr[1] = 0x10;
	pucaAddr[2] = 0x18;
	pucaAddr[3] = (ulId >> 16) & 0xff;
	pucaAddr[4] = (ulId >> 8) & 0xff;
	pucaAddr[5] = ulId & 0xff;

	return 0;
}

int kerSysReleaseMacAddress(unsigned char *pucaAddr)
{
	(void)pucaAddr;
	return 0;
}

void kerSysRegisterDyingGaspHandlerV2(char *devname, void *cbfn, void *context)
{
	(void)devname;
	(void)cbfn;
	(void)context;
}

void kerSysDeregisterDyingGaspHandler(char *devname)
{
	(void)devname;
}
