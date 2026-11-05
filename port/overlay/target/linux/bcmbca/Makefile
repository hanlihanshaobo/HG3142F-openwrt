# SPDX-License-Identifier: GPL-2.0-only
#
# Broadcom BCMBCA (ARMv7, BCM6855x PON gateway)
# 上游 OpenWrt 无此 target（只有 bcm4908/ARM64 与 bcm63xx/MIPS），本目录为自建骨架。

include $(TOPDIR)/rules.mk

ARCH:=arm
BOARD:=bcmbca
BOARDNAME:=Broadcom BCMBCA (ARMv7 PON gateway)
SUBTARGETS:=generic
FEATURES:=squashfs nand ubi ramdisk gpio fpu

# 内核基线：6.18.60（见 port/sources.lock.json 的 kernel_version）。
# 升级 major（如 7.x）时需同步 workflow 里的 linux-6.18* / config-6.18 路径。
KERNEL_PATCHVER:=6.18
KERNELNAME:=zImage dtbs

include $(INCLUDE_DIR)/target.mk

DEFAULT_PACKAGES += \
	kmod-leds-gpio \
	kmod-gpio-button-hotplug \
	kmod-mtd-rw \
	kmod-nand-brcmnand \
	mtd-utils \
	ubi-utils

$(eval $(call BuildTarget))
