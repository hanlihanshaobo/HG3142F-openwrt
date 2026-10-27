/* HG3142F OpenWrt compat stub for "br_private.h"
 *
 * 背景：bcmdrivers/opensource/net/enet/impl7/enet.c 无条件 #include "br_private.h"，
 * 但它并未使用任何内核 bridge 私有 API：
 *   - `struct net_device *br_dev` 只是局部变量
 *   - netdev_get_bridge_master() 定义在 <linux/bcm_netdevice.h>（SDK 自带）
 *   - bridge_update_pbvlan() 是 enet.c 自己的 static 函数
 * 因此提供一个空 stub 即可，避免依赖 Linux 4.1 的 net/bridge/br_private.h
 * （该头在 6.18 已重构且不对外导出）。
 */
#ifndef __HG3142F_COMPAT_BR_PRIVATE_H
#define __HG3142F_COMPAT_BR_PRIVATE_H

#include <linux/netdevice.h>

#endif /* __HG3142F_COMPAT_BR_PRIVATE_H */
