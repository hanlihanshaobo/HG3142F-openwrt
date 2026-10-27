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
  FILES:=$(PKG_BUILD_DIR)/bcm_pcie.ko
  AUTOLOAD:=*
endef

SDK_DIR:=$(CURDIR)/../../target/linux/bcmbca/sdk-bcm96855
PCIE_DIR:=$(SDK_DIR)/sdk/bcmdrivers/opensource/bus/pci/host/impl1

define Build/Prepare
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	$(CP) $(PCIE_DIR)/*.c $(PCIE_DIR)/*.h $(PKG_BUILD_DIR)/
	$(CP) $(SDK_DIR)/compat/pcie_stubs.c $(PKG_BUILD_DIR)/
	sed -e 's|@SDK_DIR@|$(SDK_DIR)/sdk|g' -e 's|@BUILD_DIR@|$(PKG_BUILD_DIR)|g' $(CURDIR)/src/Makefile.in > $(PKG_BUILD_DIR)/Makefile
endef

# 与 CI 探针完全一致的编译参数（ccflags-y 在本套 kbuild 下未生效，改用 KCFLAGS）
PCIE_KCFLAGS := \
	-I$(PKG_BUILD_DIR) \
	-I$(SDK_DIR)/sdk/bcmdrivers/opensource/include/bcm963xx \
	-I$(SDK_DIR)/sdk/bcmdrivers/opensource/include/bcm963xx/pmc \
	-I$(SDK_DIR)/sdk/shared/opensource/include/bcm963xx \
	-I$(SDK_DIR)/sdk/bcmdrivers/broadcom/include/bcm963xx \
	-I$(SDK_DIR)/sdk/kernel/bcmkernel/include \
	-I$(SDK_DIR)/sdk/kernel/bcmkernel/include/uapi \
	-DCONFIG_BCM96855 -DCONFIG_BCM_CHIP_NUMBER=6855 \
	-DCONFIG_BCM_PCIE_PMC_BRD_STUBS -DCONFIG_BCM_PCIE_HCD_IMPL=1 \
	-DCONFIG_BCM_PCIE_VCD \
	-Wno-error=implicit-function-declaration -Wno-error=int-conversion \
	-Wno-error=incompatible-pointer-types -Wno-error=return-mismatch \
	-Wno-error=unused-function -Wno-error=unused-variable \
	-Wno-error=unused-but-set-variable -Wno-error=missing-prototypes \
	-Wno-error=old-style-declaration -Wno-error=discarded-qualifiers \
	-Wno-error=address -Wno-error=override-init -Wno-error=uninitialized

define Build/Compile
	+$(KERNEL_MAKE) M=$(PKG_BUILD_DIR) KCFLAGS="$(PCIE_KCFLAGS)" modules
endef

$(eval $(call KernelPackage,bcm-pcie))
