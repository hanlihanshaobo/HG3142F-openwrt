/* HG3142F compat: nbuff.h 需要的宏
 * RecycleFuncP / wlFlowInf_t 已移到 bcm_ext_types.h（-include 更早）
 * NULL_STMT: Broadcom 内核补丁的空语句宏
 */
#ifndef __HG3142F_COMPAT_NBUFF_TYPES_STUB_H
#define __HG3142F_COMPAT_NBUFF_TYPES_STUB_H

#include <linux/types.h>

#ifndef NULL_STMT
#define NULL_STMT do { } while (0)
#endif

#endif /* __HG3142F_COMPAT_NBUFF_TYPES_STUB_H */
