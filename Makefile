include $(TOPDIR)/rules.mk

PKG_NAME:=subnet_overlap
PKG_VERSION:=1.0.0
PKG_RELEASE:=1

PKG_BUILD_DIR := $(BUILD_DIR)/$(PKG_NAME)

include $(INCLUDE_DIR)/package.mk

define Package/subnet_overlap
  SECTION:=net
  CATEGORY:=Network
  TITLE:=Subnet Overlap Detector
  DEPENDS:=+libubus +libubox +libblobmsg-json +libnl-tiny
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR) \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS)" \
		LDFLAGS="$(TARGET_LDFLAGS)" \
		STAGING_DIR="$(STAGING_DIR)"
endef

define Package/subnet_overlap/install
	# Backend
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/subnet_overlap $(1)/usr/bin/
	
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/subnet_overlap.init $(1)/etc/init.d/subnet_overlap
endef

$(eval $(call BuildPackage,subnet_overlap))