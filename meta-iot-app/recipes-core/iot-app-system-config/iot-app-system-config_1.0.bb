SUMMARY = "Network and console configuration for the IoT App image"
LICENSE = "PolyForm-Noncommercial-1.0.0"

# This recipe installs configuration files and does not use EXTERNALSRC.
# IOT_APP_PROJECT_ROOT is defined in meta-iot-app/conf/layer.conf, so the path
# below points directly to the LICENSE file at the repository root.
#
# The MD5 value is produced with md5sum LICENSE. Yocto checks it during the
# build and reports an error when the licence text changes. It is a change
# detector, not a security check. Review an intentional licence change before
# updating the checksum in this recipe.
#
# Yocto reference:
# https://docs.yoctoproject.org/scarthgap/dev-manual/licenses.html#tracking-license-changes
LIC_FILES_CHKSUM = "file://${IOT_APP_PROJECT_ROOT}/LICENSE;md5=b2a551156d047ff7f73d0c43858d552a"

inherit systemd

# yocto-prepare places an optional developer key file in TOPDIR/conf. An empty
# file means the image should keep password login without adding a key.
FILESEXTRAPATHS:prepend := "${IOT_APP_PROJECT_ROOT}/iot_app/image_support:${TOPDIR}/conf:"

SRC_URI = " \
    file://10-eth0.network \
    file://20-wlan0.network \
    file://iot-app-wifi.service \
    file://iot-app-prepare-data-storage \
    file://iot-app-storage.service \
    file://mosquitto.service \
    file://ssh_authorized_keys \
"

SYSTEMD_SERVICE:${PN} = "iot-app-wifi.service iot-app-storage.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

do_install() {
    install -D -m 0644 "${WORKDIR}/10-eth0.network" \
        "${D}${sysconfdir}/systemd/network/10-eth0.network"
    install -D -m 0644 "${WORKDIR}/20-wlan0.network" \
        "${D}${sysconfdir}/systemd/network/20-wlan0.network"
    install -D -m 0644 "${WORKDIR}/iot-app-wifi.service" \
        "${D}${systemd_system_unitdir}/iot-app-wifi.service"
    install -D -m 0755 "${WORKDIR}/iot-app-prepare-data-storage" \
        "${D}${libexecdir}/iot-app-prepare-data-storage"
    install -D -m 0644 "${WORKDIR}/iot-app-storage.service" \
        "${D}${systemd_system_unitdir}/iot-app-storage.service"
    install -D -m 0644 "${WORKDIR}/mosquitto.service" \
        "${D}${sysconfdir}/systemd/system/mosquitto.service"

    if [ -s "${WORKDIR}/ssh_authorized_keys" ]; then
        install -d -m 0700 "${D}${ROOT_HOME}/.ssh"
        install -m 0600 "${WORKDIR}/ssh_authorized_keys" \
            "${D}${ROOT_HOME}/.ssh/authorized_keys"
    fi

    install -d "${D}${sysconfdir}/systemd/system/multi-user.target.wants"
    install -d "${D}${sysconfdir}/systemd/system/getty.target.wants"

    # tty1 belongs to IoT App. Mask its login service so keyboard input and a
    # terminal cursor cannot be drawn over the framebuffer. tty2 keeps a
    # normal password-protected login for recovery without a network.
    ln -s /dev/null \
        "${D}${sysconfdir}/systemd/system/getty@tty1.service"
    ln -s "${systemd_system_unitdir}/getty@.service" \
        "${D}${sysconfdir}/systemd/system/getty.target.wants/getty@tty2.service"

    ln -s "${systemd_system_unitdir}/systemd-networkd.service" \
        "${D}${sysconfdir}/systemd/system/multi-user.target.wants/systemd-networkd.service"
    ln -s "${systemd_system_unitdir}/systemd-resolved.service" \
        "${D}${sysconfdir}/systemd/system/multi-user.target.wants/systemd-resolved.service"
    ln -s "${systemd_system_unitdir}/systemd-timesyncd.service" \
        "${D}${sysconfdir}/systemd/system/multi-user.target.wants/systemd-timesyncd.service"
}

FILES:${PN} += " \
    ${sysconfdir}/systemd/network \
    ${sysconfdir}/systemd/system \
    ${systemd_system_unitdir}/iot-app-wifi.service \
    ${systemd_system_unitdir}/iot-app-storage.service \
    ${libexecdir}/iot-app-prepare-data-storage \
    ${ROOT_HOME}/.ssh \
"

RDEPENDS:${PN} += "e2fsprogs-resize2fs e2fsprogs-tune2fs mosquitto parted systemd util-linux-partx wpa-supplicant"
