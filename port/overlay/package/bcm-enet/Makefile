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
# enet_framework_stubs.o 补 mac_drivers_set/init、mac_dev_dt_probe（unimac 源码缺失）
# bcm_chip_arch_compat.o 补厂商生成文件 drivers/bcm_chip_arch.c（GPL 包没带）
ENET_OBJS:=enet port runner dt_parsing g9991 mux_index phy_drv_pon sw_common port_types \
           phy_drv_compat enet_stubs enet_framework_stubs bcm_chip_arch_compat bcmkernel_stubs \
           enet_symbol_stubs

define Build/Prepare
	$(INSTALL_DIR) $(PKG_BUILD_DIR)/compat $(PKG_BUILD_DIR)/shim/linux
	$(CP) $(ENET_DIR)/*.c $(ENET_DIR)/*.h $(PKG_BUILD_DIR)/
	$(CP) $(COMPAT_SRC)/*.h $(PKG_BUILD_DIR)/compat/
	$(CP) $(COMPAT_SRC)/phy_drv_compat.c $(PKG_BUILD_DIR)/
	$(CP) $(COMPAT_SRC)/enet_stubs.c $(PKG_BUILD_DIR)/
	$(CP) $(COMPAT_SRC)/enet_framework_stubs.c $(PKG_BUILD_DIR)/
	$(CP) $(COMPAT_SRC)/bcm_chip_arch_compat.c $(PKG_BUILD_DIR)/
	$(CP) $(COMPAT_SRC)/bcmkernel_stubs.c $(PKG_BUILD_DIR)/
	$(CP) $(COMPAT_SRC)/enet_symbol_stubs.c $(PKG_BUILD_DIR)/
	# 6.18 的 sk_buff 没有厂商补丁加的 queue 成员 → 屏蔽 nbuff.h 里对应 accessor
	$(CP) $(SDK_DIR)/sdk/kernel/bcmkernel/include/linux/nbuff.h $(PKG_BUILD_DIR)/shim/linux/nbuff.h
	sed -i '/__BUILD_NBUFF_\(SET\|GET\)_ACCESSOR(.*queue/d' $(PKG_BUILD_DIR)/shim/linux/nbuff.h
	# 6.18 已移除 struct switchdev_ops → 关掉厂商的 switchdev 段
	# g9991.c 也要处理：它的 port_ops 初始化里有 .switchdev_ops 指定初始化，
	# 只改 port.h 不改这里会报 "port_ops_t has no member named switchdev_ops"
	# runner.c / runner_common.h 里的 switchdev 原型也要一起关（否则
	# 'struct switchdev_attr declared inside parameter list' 被 -Werror 拦下）
	sed -i 's/#if defined(CONFIG_NET_SWITCHDEV)/#if 0/; s/#ifdef CONFIG_NET_SWITCHDEV/#if 0/' \
		$(PKG_BUILD_DIR)/enet.c $(PKG_BUILD_DIR)/port.h $(PKG_BUILD_DIR)/g9991.c \
		$(PKG_BUILD_DIR)/runner_common.h
	# dt_parsing.c 会 #include "drivers/bcm_chip_arch.c"（厂商生成文件，GPL 包缺失）
	# 换成仓库里的 compat 数据文件（已作为独立 .o 编进模块）
	sed -i 's|#include "drivers/bcm_chip_arch.c"|#include <bcm_chip_arch.h>|' $(PKG_BUILD_DIR)/dt_parsing.c
	# autogen 的 rdpa_ag_iptv.h 未 include rdpa_mcast.h，rdpa_fc_mcast_flow_t 未定义；
	# 用 rdpa_fc_mcast_flow_key_t 替代（与 CI 探针一致）
	sed -i 's/rdpa_fc_mcast_flow_t/rdpa_fc_mcast_flow_key_t/g' \
		$(SDK_DIR)/sdk/rdp/drivers/rdpa_gpl/include/autogen/rdpa_ag_iptv.h
	# 6.18 的 of_platform.h 不再顺带 include platform_device.h
	sed -i 's|#include <linux/of_platform.h>|#include <linux/of_platform.h>\n#include <linux/platform_device.h>|' $(PKG_BUILD_DIR)/dt_parsing.c
	# runner.c 的 struct ethswctl_data 在 <bcm/bcmswapitypes.h>，runner.h 只在芯片宏下 include
	sed -i 's|#include <net_port.h>|#include <net_port.h>\n#include <bcm/bcmswapitypes.h>|' $(PKG_BUILD_DIR)/runner.c
	# 6.18 的 sk_buff 无 recycle_flags（Broadcom patch 成员）
	sed -i 's|skb->recycle_flags &= SKB_NO_RECYCLE;|/* compat: no recycle_flags in 6.18 */|' $(PKG_BUILD_DIR)/enet.c
	sed -i 's#skb->recycle_flags |= rx_info->extra_skb_flags;#/* compat */#' $(PKG_BUILD_DIR)/enet.c
	# 卸载/初始化失败时停掉 compat 的 PHY 轮询 workqueue（否则模块卸载后定时器还在跑）
	sed -i 's|^    enet_err("ENET system destructed|    phy_drv_compat_exit();\n    enet_err("ENET system destructed|' $(PKG_BUILD_DIR)/enet.c
	# 没有 brcm,enet 节点时 has_dt()=0，原厂会直接 bcm_enet_init_post()，
	# 而 root_sw 还是 NULL -> sw_init(NULL) 空指针 panic。改成安全失败。
	sed -i 's|^        rc = bcm_enet_init_post();|        enet_err("no brcm,enet DT node; refuse init"); return -ENODEV;|' $(PKG_BUILD_DIR)/enet.c
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
	-include $(PKG_BUILD_DIR)/compat/phy_drv_compat.h \
	-include $(PKG_BUILD_DIR)/compat/nbuff_types_stub.h \
	-include $(PKG_BUILD_DIR)/compat/nbuff_outer_stub.h \
	-DCONFIG_BCM_ENET -DENET_DT -DCONFIG_BCM_PON=1 \
	-DCONFIG_BCM_EXT_SWITCH_TYPE=0

# 厂商代码在 6.18 下的告警很多，全部降级为警告而不是错误（与探针一致）
# cast-function-type：enet.c:533 把 enetxapi_buf_recycle(struct sk_buff*, uint32_t, uint32_t)
# 强转成 RecycleFuncP，厂商原代码就这样，6.18 把它算成错误——这里降级而不是改类型，
# 因为 runner.c 里还有另一处签名不同的同类强转，改 typedef 会按下葫芦浮起瓢。
ENET_NOWARN:= \
	-Wno-error=ignored-qualifiers -Wno-error=cpp -Wno-error=cast-function-type \
	-Wno-error=undef \
	-Wno-error=implicit-function-declaration -Wno-error=incompatible-pointer-types \
	-Wno-error=int-conversion -Wno-error=return-mismatch \
	-Wno-error=unused-function -Wno-error=discarded-qualifiers \
	-Wno-error=missing-prototypes -Wno-error=old-style-declaration \
	-Wno-error=unused-variable -Wno-error=override-init -Wno-error=address \
	-Wno-vla-larger-than -Wno-error=vla

define Build/Compile
	+$(KERNEL_MAKE) M=$(PKG_BUILD_DIR) \
		KCFLAGS="$(ENET_INC) $(ENET_DEFS) $(ENET_NOWARN)" \
		KBUILD_MODPOST_WARN=1 modules
endef

$(eval $(call KernelPackage,bcm-enet))
