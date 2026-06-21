include $(TOPDIR)/rules.mk

PKG_NAME:=mt76-standalone
PKG_RELEASE=1

PKG_LICENSE:=BSD-3-Clause-Clear
PKG_LICENSE_FILES:=

PKG_SOURCE_URL:=https://github.com/AstromanGaming/mt76-standalone
PKG_SOURCE_PROTO:=git
PKG_SOURCE_DATE:=2026-06-21
PKG_SOURCE_VERSION:=20407f2401074780d83a1c9a51bc6ffbb1fe1164
# optional: remove PKG_SOURCE_DATE and PKG_MIRROR_HASH when tracking a moving branch
PKG_MIRROR_HASH:=

PKG_MAINTAINER:=Sam Bélanger <github@astromangaming.ca>
PKG_USE_NINJA:=0
PKG_BUILD_PARALLEL:=1

PKG_CONFIG_DEPENDS += \
	CONFIG_PACKAGE_kmod-mt76-usb-standalone \

STAMP_CONFIGURED_DEPENDS := $(STAGING_DIR)/usr/include/mac80211-backport/backport/autoconf.h

include $(INCLUDE_DIR)/kernel.mk
include $(INCLUDE_DIR)/package.mk

define KernelPackage/mt76-default-standalone
	SUBMENU:=Wireless Drivers
	DEPENDS:= \
	+kmod-mac80211 \
	+@DRIVER_11AC_SUPPORT \
	+@KERNEL_PAGE_POOL \
	@!TARGET_uml
endef

define KernelPackage/mt76-core-standalone
	$(KernelPackage/mt76-default-standalone)
	TITLE:=Standalone MediaTek MT76xx wireless driver
	HIDDEN:=1
endef

define KernelPackage/mt76-usb-standalone
	$(KernelPackage/mt76-default-standalone)
	TITLE:=Standalone MediaTek MT76xx wireless driver USB support
	DEPENDS += +kmod-usb-core +kmod-mt76-core-standalone
	HIDDEN:=1
endef

define KernelPackage/mt76-connac-standalone
	$(KernelPackage/mt76-default-standalone)
	TITLE:=Standalone MediaTek MT7615/STANDALONE-MT79xx wireless driver common code
	HIDDEN:=1
	DEPENDS+=+kmod-mt76-core-standalone
endef

define KernelPackage/mt7921-firmware-standalone
	$(KernelPackage/mt76-default-standalone)
	TITLE:=Standalone MediaTek MT7921 firmware
	PROVIDES:=kmod-mt7921-firmware
	REPLACES:=kmod-mt7921-firmware
	CONFLICTS:=kmod-mt7921-firmware
endef

define KernelPackage/mt792x-common-standalone
	$(KernelPackage/mt76-default-standalone)
	TITLE:=Standalone MediaTek MT792x wireless driver common code
	HIDDEN:=1
	DEPENDS+=+kmod-mt76-connac-standalone +@DRIVER_11AX_SUPPORT
	PROVIDES:=kmod-mt792x-common
	REPLACES:=kmod-mt792x-common
	CONFLICTS:=kmod-mt792x-common
endef

define KernelPackage/mt792x-usb-standalone
	$(KernelPackage/mt76-default-standalone)
	TITLE:=Standalone MediaTek MT792x wireless driver USB code
	HIDDEN:=1
	DEPENDS+=+kmod-mt792x-common-standalone +kmod-mt76-usb-standalone +@DRIVER_11AX_SUPPORT
	PROVIDES:=kmod-mt792x-usb
	REPLACES:=kmod-mt792x-usb
	CONFLICTS:=kmod-mt792x-usb
endef

define KernelPackage/mt7921-common-standalone
	$(KernelPackage/mt76-default-standalone)
	TITLE:=Standalone MediaTek MT7921 wireless driver common code
	HIDDEN:=1
	DEPENDS+=+kmod-mt792x-common-standalone +kmod-mt7921-firmware-standalone +@DRIVER_11AX_SUPPORT +kmod-hwmon-core
	PROVIDES:=kmod-mt7921-common
	REPLACES:=kmod-mt7921-common
	CONFLICTS:=kmod-mt7921-common
endef

define KernelPackage/mt7921u-standalone
	$(KernelPackage/mt76-default-standalone)
	TITLE:=Standalone MediaTek MT7921U wireless driver
	DEPENDS+=+kmod-mt792x-usb-standalone +kmod-mt7921-common-standalone
	PROVIDES:=kmod-mt7921u
	REPLACES:=kmod-mt7921u
	CONFLICTS:=kmod-mt7921u
	AUTOLOAD:=$(call AutoProbe,standalone-mt7921u)
endef

NOSTDINC_FLAGS := \
	$(KERNEL_NOSTDINC_FLAGS) \
	-I$(PKG_BUILD_DIR) \
	-I$(STAGING_DIR)/usr/include/mac80211-backport/uapi \
	-I$(STAGING_DIR)/usr/include/mac80211-backport \
	-I$(STAGING_DIR)/usr/include/mac80211/uapi \
	-I$(STAGING_DIR)/usr/include/mac80211 \
	-include backport/autoconf.h \
	-include backport/backport.h

ifdef CONFIG_PACKAGE_MAC80211_MESH
	NOSTDINC_FLAGS += -DCONFIG_MAC80211_MESH
endif

ifdef CONFIG_PACKAGE_CFG80211_TESTMODE
  NOSTDINC_FLAGS += -DCONFIG_NL80211_TESTMODE
  PKG_MAKE_FLAGS += CONFIG_NL80211_TESTMODE=y
endif

ifdef CONFIG_PACKAGE_kmod-mt76-usb-standalone
	PKG_MAKE_FLAGS += CONFIG_STANDALONE_MT76_USB=m
endif

ifdef CONFIG_PACKAGE_kmod-mt76-connac-standalone
	PKG_MAKE_FLAGS += CONFIG_STANDALONE_MT76_CONNAC_LIB=m
endif

ifdef CONFIG_PACKAGE_kmod-mt792x-common-standalone
	PKG_MAKE_FLAGS += CONFIG_STANDALONE_MT792x_LIB=m
endif

ifdef CONFIG_PACKAGE_kmod-mt792x-usb-standalone
	PKG_MAKE_FLAGS += CONFIG_STANDALONE_MT792x_USB=m
endif

ifdef CONFIG_PACKAGE_kmod-mt7921-common-standalone
	PKG_MAKE_FLAGS += CONFIG_STANDALONE_MT7921_COMMON=m
endif

ifdef CONFIG_PACKAGE_kmod-mt7921u-standalone
	PKG_MAKE_FLAGS += CONFIG_STANDALONE_MT7921U=m
endif

define Build/Compile
	+$(KERNEL_MAKE) $(PKG_JOBS) \
		$(PKG_MAKE_FLAGS) \
		M="$(PKG_BUILD_DIR)" \
		NOSTDINC_FLAGS="$(NOSTDINC_FLAGS)" \
		modules
endef

define Build/Install
	:
endef

define Package/kmod-mt76-standalone/install
	true
endef

define KernelPackage/mt7921-firmware-standalone/install
	$(INSTALL_DIR) $(1)/lib/firmware/mediatek/mt76-standalone
	cp \
		$(PKG_BUILD_DIR)/firmware/WIFI_MT7961_patch_mcu_1_2_hdr.bin \
		$(PKG_BUILD_DIR)/firmware/WIFI_RAM_CODE_MT7961_1.bin \
		$(1)/lib/firmware/mediatek/mt76-standalone
endef

define KernelPackage/mt76-core-standalone/install
	$(INSTALL_DIR) $(1)/lib/modules/$(LINUX_VERSION)/mt76-standalone
	cp \
		$(PKG_BUILD_DIR)/standalone-mt76.ko \
		$(1)/lib/modules/$(LINUX_VERSION)/mt76-standalone
endef

define KernelPackage/mt76-usb-standalone/install
	$(INSTALL_DIR) $(1)/lib/modules/$(LINUX_VERSION)/mt76-standalone
	cp \
		$(PKG_BUILD_DIR)/standalone-mt76-usb.ko \
		$(1)/lib/modules/$(LINUX_VERSION)/mt76-standalone
endef

define KernelPackage/mt76-connac-standalone/install
	$(INSTALL_DIR) $(1)/lib/modules/$(LINUX_VERSION)/mt76-standalone
	cp \
		$(PKG_BUILD_DIR)/standalone-mt76-connac-lib.ko \
		$(1)/lib/modules/$(LINUX_VERSION)/mt76-standalone
endef

define KernelPackage/mt792x-common-standalone/install
	$(INSTALL_DIR) $(1)/lib/modules/$(LINUX_VERSION)/mt76-standalone
	cp \
		$(PKG_BUILD_DIR)/standalone-mt792x-lib.ko \
		$(1)/lib/modules/$(LINUX_VERSION)/mt76-standalone
endef

define KernelPackage/mt792x-usb-standalone/install
	$(INSTALL_DIR) $(1)/lib/modules/$(LINUX_VERSION)/mt76-standalone
	cp \
		$(PKG_BUILD_DIR)/standalone-mt792x-usb.ko \
		$(1)/lib/modules/$(LINUX_VERSION)/mt76-standalone
endef

define KernelPackage/mt7921-common-standalone/install
	$(INSTALL_DIR) $(1)/lib/modules/$(LINUX_VERSION)/mt76-standalone
	cp \
		$(PKG_BUILD_DIR)/mt7921/standalone-mt7921-common.ko \
		$(1)/lib/modules/$(LINUX_VERSION)/mt76-standalone
endef

define KernelPackage/mt7921u-standalone/install
	$(INSTALL_DIR) $(1)/lib/modules/$(LINUX_VERSION)/mt76-standalone
	cp \
		$(PKG_BUILD_DIR)/mt7921/standalone-mt7921u.ko \
		$(1)/lib/modules/$(LINUX_VERSION)/mt76-standalone
endef

$(eval $(call KernelPackage,mt76-core-standalone))
$(eval $(call KernelPackage,mt76-usb-standalone))
$(eval $(call KernelPackage,mt76-connac-standalone))
$(eval $(call KernelPackage,mt7921-firmware-standalone))
$(eval $(call KernelPackage,mt792x-common-standalone))
$(eval $(call KernelPackage,mt792x-usb-standalone))
$(eval $(call KernelPackage,mt7921-common-standalone))
$(eval $(call KernelPackage,mt7921u-standalone))