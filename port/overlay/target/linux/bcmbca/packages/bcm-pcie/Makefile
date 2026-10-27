#
# Broadcom BCM963xx PCIe 主控驱动（BCM96855 / HG3142F）
#
# 作用：提供 PCIe 主控总线，使板载 RTL8125(10ec:8125) 2.5G 网卡能由主线 r8169 驱动。
# 源码来自 Broadcom GPL（asuswrt 5.04axhnd.675x / BCM96855 平台）：
#   bcmdrivers/opensource/bus/pci/host/impl1/pcie-bcm963xx.c
# prepare.sh 会把 port/sdk-bcm96855 复制到 target/linux/bcmbca/sdk-bcm96855，
# 因此这里的相对路径 ../../sdk-bcm96855 在 OpenWrt 源码树内成立。
#
include $(TOPDIR)/rules.mk
include $(TOPDIR)/include/kernel.mk

PKG_NAME:=bcm-pcie
PKG_VERSION:=1
PKG_RELEASE:=1

include $(INCLUDE_DIR)/package.mk

define Package/bcm-pcie/description
 Broadcom GPL PCIe host controller driver (brcm,bcm963xx-pcie) for BCM96855.
 It provides the PCIe host bus so that the on-board RTL8125 2.5GbE NIC can be
 driven by the mainline r8169 driver.
endef

define KernelPackage/bcm-pcie
  SUBMENU:=Network Drivers
  TITLE:=Broadcom BCM963xx PCIe host controller
  DEPENDS:=+kmod-pci
  FILES:=$(PKG_BUILD_DIR)/bcm_pcie.ko
  AUTOLOAD:=*
endef

SDK_DIR:=$(CURDIR)/../../sdk-bcm96855
PCIE_DIR:=$(SDK_DIR)/sdk/bcmdrivers/opensource/bus/pci/host/impl1

define Build/Prepare
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	$(CP) $(PCIE_DIR)/pcie-bcm963xx.c $(PCIE_DIR)/pcie-bcm963xx.h \
	      $(PCIE_DIR)/pcie_hcd.c $(PCIE_DIR)/pcie_hcd.h \
	      $(PCIE_DIR)/pcie-vcore.c $(PCIE_DIR)/pcie-vcore.h \
	      $(PCIE_DIR)/pcie-vdev.h \
	      $(PKG_BUILD_DIR)/
	$(CP) $(SDK_DIR)/compat/pcie_stubs.c $(PKG_BUILD_DIR)/
	sed -e 's|@SDK_DIR@|$(SDK_DIR)/sdk|g' $(CURDIR)/src/Makefile.in > $(PKG_BUILD_DIR)/Makefile
endef

define Build/Compile
	$(KERNEL_MAKE) M=$(PKG_BUILD_DIR) modules
endef

$(eval $(call KernelPackage,bcm-pcie))
