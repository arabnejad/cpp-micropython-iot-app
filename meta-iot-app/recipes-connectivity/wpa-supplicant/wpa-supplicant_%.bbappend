# This file extends Yocto's existing wpa-supplicant recipe. The % in the file
# name means that the changes apply to any wpa-supplicant version selected by
# Yocto.
#
# The project keeps the real Wi-Fi name and password in the private
# wpa_supplicant.conf file at the repository root. make yocto-prepare copies
# that file to the Yocto build directory as conf/wpa_supplicant.conf.
#
# TOPDIR is the Yocto build directory. Reading the copied file from TOPDIR
# lets the image include the Wi-Fi settings without storing the password in
# this public layer.
#
# Configuration flow:
#
#   Project root:
#   wpa_supplicant.conf
#           |
#           | make yocto-prepare copies the file
#           v
#   Yocto build directory:
#   build/conf/wpa_supplicant.conf
#           |
#           | wpa-supplicant_%.bbappend installs the file
#           v
#   Generated image:
#   /etc/wpa_supplicant.conf
#           |
#           v
#   iot-app-wifi.service reads it to connect wlan0
FILESEXTRAPATHS:prepend := "${TOPDIR}/conf:"

# Add the copied Wi-Fi configuration to the package build inputs.
SRC_URI += "file://wpa_supplicant.conf"

# Install the configuration in the target image. The custom
# iot-app-wifi.service reads this file when it starts wpa_supplicant for wlan0.
# Permission 0600 allows only root to read or change the Wi-Fi password.
do_install:append() {
    install -D -m 0600 "${WORKDIR}/wpa_supplicant.conf" \
        "${D}${sysconfdir}/wpa_supplicant.conf"
}
