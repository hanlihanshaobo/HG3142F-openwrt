/* SPDX-License-Identifier: GPL-2.0 */
/*
 * 6.x 内核兼容层：让 4.1 时代的 bcm963xx PCIe 驱动能编译。
 *
 * Linux 5.13 起删除了 msi_controller 这套旧 API：
 *   - struct msi_controller 整个类型消失
 *   - pci_msi_mask_irq() / pci_msi_unmask_irq() 被移除
 *   - struct msi_desc 的 nvec_used / msi_attrib.multiple 成员消失
 *   - struct pci_host_bridge::msi、pci_bus::msi 成员消失
 *   - struct device::msi_list 被移入 msi_device_data
 *
 * 本移植策略：**不启用该驱动的 MSI 支持**，让下游设备走传统 INTx 中断
 * （r8169 完全支持 INTx）。因此这里只补一个最小的 msi_controller 占位类型
 * 和两个空的 mask/unmask 回调，运行时的 MSI 注册点已在 pcie_hcd.c 中被裁掉。
 */
#ifndef _PCIE_HCD_MSI_COMPAT_H_
#define _PCIE_HCD_MSI_COMPAT_H_

#include <linux/version.h>
#include <linux/types.h>
#include <linux/pci.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 13, 0)

struct msi_desc;

/* 占位类型：仅用于让 struct pcie_hcd_msi 里的成员和函数原型成立，
 * 不会被注册到 PCI 核心（bridge->msi 的赋值已裁掉）。 */
struct msi_controller {
	struct device *dev;
	int (*setup_irq)(struct msi_controller *chip, struct pci_dev *pdev,
			 struct msi_desc *desc);
	int (*setup_irqs)(struct msi_controller *chip, struct pci_dev *pdev,
			  int nvec, int type);
	void (*teardown_irq)(struct msi_controller *chip, unsigned int irq);
};

static inline void pci_msi_mask_irq(struct irq_data *data) { }
static inline void pci_msi_unmask_irq(struct irq_data *data) { }

#endif /* >= 5.13 */

#endif /* _PCIE_HCD_MSI_COMPAT_H_ */
