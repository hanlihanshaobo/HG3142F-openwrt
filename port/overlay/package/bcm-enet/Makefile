#
# Broadcom BCM96855（Archer/Runner）以太网驱动 bcm_enet
#
# 作用：驱动 SoC 里的 unimac 以太网 MAC + Runner 交换芯片，把 4 个千兆 LAN 口
#       （switch0 上的 lan1..lan4）拉起来。目前系统只有 PCIe 上的 RTL8125 能用。
#
# 源码来自 Broadcom GPL（BCM96855 SDK）：
#   bcmdrivers/opensource/net/enet/impl7/
# prepare.sh 会把 port/sdk-bcm96855 复制到 target/linux/bcmbca/sdk-bcm96855，
# 因此下面的相对路径 ../../sdk-bcm96855 在 OpenWrt 源码树内成立。
#
# 编译参数与 .github/workflows 里 "SDK compile probe (bcm_enet)" 那一步**逐项一致**，
# 差别只有两个（都是把 CI 里的临时 sed 固化成可复现的构建步骤）：
#   · nbuff.h 的 queue 访问器：拷一份到 shim/ 里删掉，不改仓库里的 SDK 头
#   · switchdev 段：在拷进 PKG_BUILD_DIR 的 enet.c / port.h 上关掉
#
include $(TOPDIR)/rules.mk
include $(TOPDIR)/include/kernel.mk

PKG_NAME:=bcm-enet
PKG_VERSION:=1
PKG_RELEASE:=1

include $(INCLUDE_DIR)/package.mk

define Package/bcm-enet/description
 Broadcom GPL Ethernet driver (bcm_enet, impl7) for BCM96855 / HG3142F.
 Covers the unimac MAC and the Runner switch, i.e. the four 1GbE LAN ports.
 The module is NOT autoloaded: insert it by hand with insmod while validating,
 so a broken probe cannot make the box unbootable.
endef

define KernelPackage/bcm-enet
  SUBMENU:=Network Devices
  TITLE:=Broadcom BCM96855 Ethernet (unimac + Runner switch)
  DEPENDS:=@TARGET_bcmbca
  FILES:=$(PKG_BUILD_DIR)/bcm_enet.ko
endef

SDK_DIR:=$(CURDIR)/../../target/linux/bcmbca/sdk-bcm96855
ENET_DIR:=$(SDK_DIR)/sdk/bcmdrivers/opensource/net/enet/impl7
COMPAT_SRC:=$(SDK_DIR)/compat
BDMF_DIR:=$(SDK_DIR)/sdk/bcmdrivers/opensource/char/bdmf/impl1

# 与 CI 探针的 bcm_enet-y 完全一致（enit_ingress / archer_enet 等不走这条路径）
# phy_drv_compat.o 是我们自己补的 PHY 框架实现（原厂只有 prebuilt phy_drv.o，无源码）
ENET_OBJS:=enet port runner dt_parsing g9991 mux_index phy_drv_pon sw_common port_types phy_drv_compat

define Build/Prepare
	$(INSTALL_DIR) $(PKG_BUILD_DIR)/compat $(PKG_BUILD_DIR)/shim/linux
	$(CP) $(ENET_DIR)/*.c $(ENET_DIR)/*.h $(PKG_BUILD_DIR)/
	$(CP) $(COMPAT_SRC)/*.h $(PKG_BUILD_DIR)/compat/
	$(CP) $(COMPAT_SRC)/phy_drv_compat.c $(PKG_BUILD_DIR)/
	# 6.18 的 sk_buff 没有厂商补丁加的 queue 成员 → 屏蔽 nbuff.h 里对应 accessor
	$(CP) $(SDK_DIR)/sdk/kernel/bcmkernel/include/linux/nbuff.h $(PKG_BUILD_DIR)/shim/linux/nbuff.h
	sed -i '/__BUILD_NBUFF_\(SET\|GET\)_ACCESSOR(.*queue/d' $(PKG_BUILD_DIR)/shim/linux/nbuff.h
	# 6.18 已移除 struct switchdev_ops → 关掉厂商的 switchdev 段
	sed -i 's/#if defined(CONFIG_NET_SWITCHDEV)/#if 0/' $(PKG_BUILD_DIR)/enet.c $(PKG_BUILD_DIR)/port.h
	printf '%s\n' \
		'obj-m := bcm_enet.o' \
		'bcm_enet-y := $(addsuffix .o,$(ENET_OBJS))' \
		> $(PKG_BUILD_DIR)/Kbuild
endef

ENET_INC:= \
	-I$(PKG_BUILD_DIR)/shim \
	-I$(PKG_BUILD_DIR)/compat \
	-I$(PKG_BUILD_DIR) \
	-I$(SDK_DIR)/sdk/bcmdrivers/opensource/phy \
	-I$(SDK_DIR)/sdk/bcmdrivers/opensource/net/enet/shared \
	-I$(SDK_DIR)/sdk/bcmdrivers/opensource/include/bcm963xx \
	-I$(SDK_DIR)/sdk/bcmdrivers/broadcom/include/bcm963xx \
	-I$(SDK_DIR)/sdk/shared/opensource/include/bcm963xx \
	-I$(SDK_DIR)/sdk/rdp/drivers/rdpa_gpl/include \
	-I$(SDK_DIR)/sdk/rdp/drivers/rdpa_gpl/include/autogen \
	-I$(SDK_DIR)/sdk/kernel/bcmkernel/include \
	-I$(SDK_DIR)/sdk/kernel/bcmkernel/include/uapi \
	-I$(SDK_DIR)/sdk/bcmdrivers/opensource/phy \
	-I$(BDMF_DIR)/framework \
	-I$(BDMF_DIR)/system \
	-I$(BDMF_DIR)/system/linux \
	-I$(BDMF_DIR)/system/linux/cm

ENET_DEFS:= \
	-include $(PKG_BUILD_DIR)/compat/enet_compat.h \
	-include $(PKG_BUILD_DIR)/compat/nbuff_types_stub.h \
	-include $(PKG_BUILD_DIR)/compat/nbuff_outer_stub.h \
	-DCONFIG_BCM_ENET -DENET_DT -DCONFIG_BCM_PON=1

# 厂商代码在 6.18 下的告警很多，全部降级为警告而不是错误（与探针一致）
# cast-function-type：enet.c:533 把 enetxapi_buf_recycle(struct sk_buff*, uint32_t, uint32_t)
# 强转成 RecycleFuncP，厂商原代码就这样，6.18 把它算成错误——这里降级而不是改类型，
# 因为 runner.c 里还有另一处签名不同的同类强转，改 typedef 会按下葫芦浮起瓢。
ENET_NOWARN:= \
	-Wno-error=ignored-qualifiers -Wno-error=cpp -Wno-error=cast-function-type \
	-Wno-error=implicit-function-declaration -Wno-error=incompatible-pointer-types \
	-Wno-error=int-conversion -Wno-error=return-mismatch \
	-Wno-error=unused-function -Wno-error=discarded-qualifiers \
	-Wno-error=missing-prototypes -Wno-error=old-style-declaration \
	-Wno-error=unused-variable -Wno-error=override-init -Wno-error=address

define Build/Compile
	+$(KERNEL_MAKE) M=$(PKG_BUILD_DIR) \
		KCFLAGS="$(ENET_INC) $(ENET_DEFS) $(ENET_NOWARN)" modules
endef

$(eval $(call KernelPackage,bcm-enet))
