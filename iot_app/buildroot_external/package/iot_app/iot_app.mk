################################################################################
#
# iot_app
#
################################################################################

# Buildroot package variables use the package name as their prefix. For this
# package, every setting starts with IOT_APP_. Most of this file only describes
# the package. Buildroot reads those descriptions when cmake-package is expanded
# at the end of the file.

# This is the version of IoT App represented by this Buildroot package.
IOT_APP_VERSION = 0.1.0

# BR2_EXTERNAL_IOT_PROJECT_PATH points to iot_app/buildroot_external. Moving one
# directory up selects the iot_app source directory containing CMakeLists.txt.
IOT_APP_SITE = $(BR2_EXTERNAL_IOT_PROJECT_PATH)/..

# local means the source already exists on this computer. Buildroot copies it
# into its package work directory instead of downloading an archive or cloning
# a repository.
IOT_APP_SITE_METHOD = local

# Keep generated CMake files in a buildroot-build subdirectory rather than
# mixing them with the copied source files.
IOT_APP_SUPPORTS_IN_SOURCE_BUILD = NO

# Keep the separate CMake build directory and Buildroot's bookkeeping files
# when the local source tree is copied again during an incremental build.
#
# --delete removes a copied source file when that file has been deleted from
# the original project. The three exclusions protect files that Buildroot or
# CMake created inside the work directory; they are not project source files.
IOT_APP_OVERRIDE_SRCDIR_RSYNC_EXCLUSIONS = \
	--delete \
	--exclude=/buildroot-build/ \
	--exclude=/.stamp_* \
	--exclude=/.files-list*

# These are Buildroot package names, not Linux library file names. Buildroot
# builds and stages them before it configures IoT App, which makes their headers
# and libraries available to the cross-compiler:
#
#   cjson       parses deployment and application metadata
#   jpeg        decodes JPEG images
#   libcurl     downloads files over HTTP and HTTPS
#   libdrm      discovers the connected display and its current mode
#   mosquitto   receives application deployments over MQTT
#   mpv         plays video through libmpv
#   openssl     calculates SHA-256 checksums and supports secure connections
IOT_APP_DEPENDENCIES = cjson jpeg libcurl libdrm mosquitto mpv openssl

# Buildroot already supplies CMake with its cross-compiler, sysroot, build type,
# and installation directory. These two extra options tell this project where
# to find the pinned LVGL and MicroPython source submodules beside iot_app.
# abspath changes each path into an absolute path before passing it to CMake.
IOT_APP_CONF_OPTS = \
	-DLVGL_DIR=$(abspath $(BR2_EXTERNAL_IOT_PROJECT_PATH)/../../lvgl) \
	-DMICROPYTHON_DIR=$(abspath $(BR2_EXTERNAL_IOT_PROJECT_PATH)/../../micropython)

# Buildroot reads this row when it creates the target's user database. Its
# fields have this order:
#
#   username uid group gid password home shell extra-groups comment
#
# The -1 values ask Buildroot to choose a system UID and GID. The * disables
# password login. The two - values mean that no home directory is created and
# /bin/false is used as the login shell. Membership in video, render, i2c, and
# input gives the service access to the hardware device files it needs without
# running the application as root.
define IOT_APP_USERS
	iot-app -1 iot-app -1 * - - video,render,i2c,input IoT App runtime user
endef

# Buildroot and Yocto install the same helper scripts and Mosquitto policy. The
# original copies live in image_support so the two image systems do not need to
# maintain different versions of the same files.
IOT_APP_IMAGE_SUPPORT_DIR = $(BR2_EXTERNAL_IOT_PROJECT_PATH)/../image_support

# define stores this group of commands under the name
# IOT_APP_INSTALL_SHARED_IMAGE_SUPPORT. Defining it does not run the commands.
#
# TARGET_DIR is the temporary directory that becomes / in the finished image.
# The INSTALL command uses -D to create missing parent directories. Mode 0755
# makes a script executable; mode 0644 is used for the non-executable Mosquitto
# configuration file.
#
# The installed files have these jobs:
#
#   /usr/libexec/iot-app-prepare-data-storage
#       prepares the optional persistent /data partition
#   /usr/libexec/iot-app-launcher
#       chooses the development executable or the installed executable
#   /usr/libexec/iot-app-hide-tty1-cursor
#       stops the text-console cursor appearing over the application display
#   /usr/bin/iot-app-check-video-playback
#       checks whether this image can play video through libmpv
#   /etc/mosquitto/mosquitto.conf
#       configures the local MQTT broker used for application deployment
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
	$(INSTALL) -D -m 0755 \
		$(IOT_APP_IMAGE_SUPPORT_DIR)/iot-app-check-video-playback \
		$(TARGET_DIR)/usr/bin/iot-app-check-video-playback
	$(INSTALL) -D -m 0644 \
		$(IOT_APP_IMAGE_SUPPORT_DIR)/mosquitto.conf \
		$(TARGET_DIR)/etc/mosquitto/mosquitto.conf
endef

# This line registers the command group above as a post-install hook. The +=
# appends its name to Buildroot's list of actions; it does not execute the
# commands here. Later, after CMake and the init-system setup have installed the
# package into TARGET_DIR, Buildroot visits this list and calls each named hook.
# The shared scripts and configuration then become part of the final root
# filesystem image.
IOT_APP_POST_INSTALL_TARGET_HOOKS += IOT_APP_INSTALL_SHARED_IMAGE_SUPPORT

# cmake-package recognizes IOT_APP_INSTALL_INIT_SYSV as the package's SysV init
# installation step. The supplied Raspberry Pi 4 image uses BusyBox init, so
# Buildroot runs these commands while it assembles that image.
#
# S90iot-app is the boot script; S90 places it late in the ordered SysV startup
# sequence. The iot-app symbolic link gives users a readable manual command such
# as /etc/init.d/iot-app restart. Removing both destinations first also makes
# repeated incremental installations safe.
#
# IOT_APP_PKGDIR is the directory containing this recipe and its iot-app init
# script. TARGET_DIR is still the future target filesystem, not the host's /.
define IOT_APP_INSTALL_INIT_SYSV
	$(RM) $(TARGET_DIR)/etc/init.d/S90iot-app \
		$(TARGET_DIR)/etc/init.d/iot-app
	$(INSTALL) -D -m 0755 $(IOT_APP_PKGDIR)/iot-app \
		$(TARGET_DIR)/etc/init.d/S90iot-app
	ln -s S90iot-app $(TARGET_DIR)/etc/init.d/iot-app
endef

# This must remain at the end. It asks Buildroot's CMake package infrastructure
# to turn all IOT_APP_ settings and command groups above into the actual source
# synchronization, configure, compile, install, and clean Make targets.
$(eval $(cmake-package))
