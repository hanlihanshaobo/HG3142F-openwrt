/* HG3142F compat: types/macros used by bcmkernel nbuff.h but missing in 6.18
 *
 * 来源说明：
 *   RecycleFuncP  -> <linux/bcm_skbuff.h>（SDK 内已有，include 即可）
 *   NULL_STMT     -> Broadcom 内核补丁里的空语句宏
 *   wlFlowInf_t   -> WiFi flow info，nbuff.h 注释写明 "define wl as just uint32_t"
 */
#ifndef __HG3142F_COMPAT_NBUFF_TYPES_STUB_H
#define __HG3142F_COMPAT_NBUFF_TYPES_STUB_H

#include <linux/types.h>

/* RecycleFuncP: 与 <linux/bcm_skbuff.h> 中定义一致，直接 typedef 避免早期 include 依赖 */
#ifndef __hg3142f_RecycleFuncP_defined
typedef void (*RecycleFuncP)(void *nbuff_p, unsigned long context, uint32_t flags);
#define __hg3142f_RecycleFuncP_defined
#endif

#ifndef NULL_STMT
#define NULL_STMT do { } while (0)
#endif

#ifndef __hg3142f_wlFlowInf_t_defined
typedef uint32_t wlFlowInf_t;   /* nbuff.h: TODO define wl as just uint32_t */
#define __hg3142f_wlFlowInf_t_defined
#endif

#endif /* __HG3142F_COMPAT_NBUFF_TYPES_STUB_H */
