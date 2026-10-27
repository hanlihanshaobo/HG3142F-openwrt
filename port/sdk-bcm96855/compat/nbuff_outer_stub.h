/* HG3142F compat: Broadcom bcmkernel L2 outer-cache helpers missing in 6.18 upstream
 *
 * nbuff.h（CONFIG_OUTER_CACHE 打开时）调用以下函数，它们是 Broadcom 4.1 内核
 * <asm/outercache.h> 的私有扩展，6.18 上游没有：
 *   outer_spin_lock_irqsave / outer_spin_unlock_irqrestore
 *   outer_flush_line_no_lock / outer_inv_line_no_lock / outer_sync_no_lock
 *
 * 探针/编译阶段先给 no-op stub，让编译继续推进；真机运行前必须替换为
 * 真正的 L2 cache 操作（否则 DMA 一致性会出问题）。
 * 通过 KCFLAGS="-include .../nbuff_outer_stub.h" 强制前置包含。
 */
#ifndef __HG3142F_COMPAT_NBUFF_OUTER_STUB_H
#define __HG3142F_COMPAT_NBUFF_OUTER_STUB_H

static inline void outer_spin_lock_irqsave(void) { }
static inline void outer_spin_unlock_irqrestore(void) { }
static inline void outer_flush_line_no_lock(unsigned long p) { (void)p; }
static inline void outer_inv_line_no_lock(unsigned long p) { (void)p; }
static inline void outer_sync_no_lock(void) { }

#endif /* __HG3142F_COMPAT_NBUFF_OUTER_STUB_H */
