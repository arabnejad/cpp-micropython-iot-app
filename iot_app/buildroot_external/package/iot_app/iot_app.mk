################################################################################
#
# iot_app
#
################################################################################

IOT_APP_VERSION = 0.1.0
IOT_APP_SITE = $(BR2_EXTERNAL_IOT_PROJECT_PATH)/..
IOT_APP_SITE_METHOD = local
IOT_APP_SUPPORTS_IN_SOURCE_BUILD = NO
IOT_APP_DEPENDENCIES = cjson libdrm mosquitto openssl
IOT_APP_CONF_OPTS = \
	-DLVGL_DIR=$(abspath $(BR2_EXTERNAL_IOT_PROJECT_PATH)/../../lvgl) \
	-DMICROPYTHON_DIR=$(abspath $(BR2_EXTERNAL_IOT_PROJECT_PATH)/../../micropython)

define IOT_APP_USERS
	iot-app -1 iot-app -1 * - - video,render,i2c,input IoT App runtime user
endef

IOT_APP_IMAGE_SUPPORT_DIR = $(BR2_EXTERNAL_IOT_PROJECT_PATH)/../image_support

define IOT_APP_INSTALL_SHARED_IMAGE_SUPPORT
	$(INSTALL) -D -m 0755 \
		$(IOT_APP_IMAGE_SUPPORT_DIR)/iot-app-prepare-data-storage \
		$(TARGET_DIR)/usr/libexec/iot-app-prepare-data-storage
	$(INSTALL) -D -m 0755 \
		$(IOT_APP_IMAGE_SUPPORT_DIR)/iot-app-launcher \
		$(TARGET_DIR)/usr/libexec/iot-app-launcher
	$(INSTALL) -D -m 0755 \
		$(IOT_APP_IMAGE_SUPPORT_DIR)/iot-app-hide-tty1-cursor \
		$(TARGET_DIR)/usr/libexec/iot-app-hide-tty1-cursor
	$(INSTALL) -D -m 0644 \
		$(IOT_APP_IMAGE_SUPPORT_DIR)/mosquitto.conf \
		$(TARGET_DIR)/etc/mosquitto/mosquitto.conf
endef
IOT_APP_POST_INSTALL_TARGET_HOOKS += IOT_APP_INSTALL_SHARED_IMAGE_SUPPORT

define IOT_APP_INSTALL_INIT_SYSV
	$(RM) $(TARGET_DIR)/etc/init.d/S90iot-app \
		$(TARGET_DIR)/etc/init.d/iot-app
	$(INSTALL) -D -m 0755 $(IOT_APP_PKGDIR)/iot-app \
		$(TARGET_DIR)/etc/init.d/S90iot-app
	ln -s S90iot-app $(TARGET_DIR)/etc/init.d/iot-app
endef

define IOT_APP_INSTALL_INIT_SYSTEMD
	$(INSTALL) -D -m 0644 $(IOT_APP_PKGDIR)/iot-app.service \
		$(TARGET_DIR)/usr/lib/systemd/system/iot-app.service
	$(INSTALL) -D -m 0644 $(IOT_APP_IMAGE_SUPPORT_DIR)/70-iot-app-access.rules \
		$(TARGET_DIR)/usr/lib/udev/rules.d/70-iot-app-access.rules
endef

$(eval $(cmake-package))
