/* SPDX-License-Identifier: GPL-2.0 */
/*
 * bcm_chip_arch_compat.c —— 补上厂商生成文件 drivers/bcm_chip_arch.c
 *
 * dt_parsing.c:75 有一行
 *     #include "drivers/bcm_chip_arch.c"
 * 这个文件是 Broadcom 在完整 SDK 里按芯片型号生成的（GPL 包里没带），
 * 内容就是 bcm_chip_arch.h 声明的那几个"端口能力位图"。缺了它
 * dt_parsing.c 直接 fatal error。
 *
 * 这里按实机 DTS（orig_968552CREF_CM_XG.dts）填 BCM96855 + HG3142F：
 *   unit 0（唯一交换单元）的端口布局
 *     0..3  GPHY0..3   LAN
 *     4     RGMII      disabled
 *     5     SGMII      LAN4
 *     6     PON/XFI    WAN
 *
 * 语义只影响 port_cap_get()（LAN/WAN 能力判定），不影响寄存器访问；
 * 真正的端口开关仍由 DTS 的 status / is-wan 决定。
 */
#include <linux/types.h>

/* 每项按 switch unit 索引；本板只有 unit 0，保留 unit 1 以防多单元 */
u32 chip_arch_lan_only_portmap[] = { 0x3f, 0x00 };
u32 chip_arch_wan_only_portmap[] = { 0x40, 0x00 };
u32 chip_arch_wan_pref_portmap[] = { 0x00, 0x00 };
u32 chip_arch_all_portmap[]     = { 0x7f, 0x00 };
u32 chip_arch_mgmt_portmap[]    = { 0x00, 0x00 };

/* 仅 bp_parsing.c（boardparms 启动路径）使用；DT 路径不读，填 0 即可 */
int chip_arch_serdes_core_num[] = { 0, 0, 0, 0, 0, 0, 0 };
