# SPDX-License-Identifier: GPL-2.0-only
include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/image.mk

#
# BCM6855x / bcm96855 镜像布局
#   loader  2MiB  @0x0000000  厂商 SPL+TPL+U-Boot(2019.07)，本固件不动
#   image   164MiB@0x0200000  UBI（内核 + squashfs rootfs）
#   ipg     87MiB @0x0A600000 厂商 IPG 区（本固件不动）
#   factory 2MiB  @0x0FD00000  出厂校准/SN/MAC（本固件只读）
#
# 阶段 1（当前）：sysupgrade.tar 形式的 kernel+rootfs 包，配合 U-Boot 从
#                 网络/TFTP 加载 initramfs 验证内核；落盘启动在阶段 2 换成 UBI 卷。
#
define Device/Default
  PROFILES := Default $$(DEVICE_NAME)
  KERNEL_NAME := zImage
  KERNEL := kernel-bin | append-dtb | lzma | uImage lzma
  KERNEL_INITRAMFS := kernel-bin | append-dtb | lzma | uImage lzma
  KERNEL_LOADADDR := 0x01000000
  DEVICE_DTS_DIR := ../dts
  FILESYSTEMS := squashfs
  IMAGES := sysupgrade.bin
  IMAGE/sysupgrade.bin := append-kernel | pad-to 128k | append-rootfs | pad-rootfs | append-metadata
endef

define Device/fiberhome_mb4784w
  DEVICE_VENDOR := FiberHome
  DEVICE_MODEL := HG3142F
  DEVICE_DTS := bcm96855-mb4784w
  SUPPORTED_DEVICES := fiberhome,mb4784w
  # kmod-r8169: 板上 PCIe RTL8125(10ec:8125) 2.5G 网口，主线驱动可直接用
  # watchdog : 喂 /dev/watchdog（brcm,bcm6345-wdt），否则 U-Boot 开的 80s 看门狗会把板子复位
  DEVICE_PACKAGES := kmod-mtd-rw kmod-nand-brcmnand kmod-r8169 watchdog ubi-utils mtd-utils
  IMAGE_SIZE := 60000k
endef
TARGET_DEVICES += fiberhome_mb4784w

$(eval $(call BuildImage))
