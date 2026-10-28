#
# BCM7038 看门狗复位模块（HG3142F / BCM96855）
#
# 背景：主线 bcm7038_wdt 只做喂狗，不注册 restart handler，所以 `reboot` 会走到
#       machine_restart() 后直接 "Reboot failed -- System halted"（只能断电）。
# 做法：注册 restart_handler，复位时把 WDT 设成 ~1 秒并启动计数，让硬件自己把 SoC 复位。
#
# 寄存器（与厂商 bcm7038_wdt.c 一致，asic 0xff800480）：
#   0x00 WDT_TIMEOUT : 计数值 = rate * seconds
#   0x04 WDT_CMD     : 写 0xff00 再写 0x00ff = 启动/喂狗；0xee00/0x00ee = 停止
#
include $(TOPDIR)/rules.mk
include $(TOPDIR)/include/kernel.mk

PKG_NAME:=bcm-reboot
PKG_VERSION:=1
PKG_RELEASE:=1

include $(INCLUDE_DIR)/package.mk

define KernelPackage/bcm-reboot
  SUBMENU:=Other modules
  TITLE:=Broadcom BCM7038 WDT restart handler (reboot support)
  FILES:=$(PKG_BUILD_DIR)/bcm_reboot.ko
  AUTOLOAD:=$(call AutoLoad,20,bcm_reboot)
endef

define KernelPackage/bcm-reboot/description
 Registers a restart handler that arms the BCM7038 watchdog with a ~1s timeout,
 so that `reboot` actually resets the SoC instead of halting.
endef

define Build/Prepare
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Compile
	+$(KERNEL_MAKE) M=$(PKG_BUILD_DIR) modules
endef

$(eval $(call KernelPackage,bcm-reboot))
