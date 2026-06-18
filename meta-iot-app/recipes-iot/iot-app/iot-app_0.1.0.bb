SUMMARY = "C++ runtime for embedded MicroPython IoT applications"
DESCRIPTION = "IoT App runs one embedded MicroPython application and exposes native display, system, scheduling, and gamepad services."
HOMEPAGE = "https://github.com/arabnejad/cpp-micropython-iot-app"
LICENSE = "PolyForm-Noncommercial-1.0.0"

# Yocto requires a checksum of the licence text for every recipe unless its
# licence is CLOSED. This recipe uses EXTERNALSRC, so file://LICENSE refers to
# the LICENSE file in the project root through the recipe source directory S.
#
# The MD5 value below is the output of:
#
#   md5sum LICENSE
#
# Yocto compares it with the file during the build and reports an error if the
# licence text changes. MD5 is only a change detector here; it is not used as
# a security check. After an intentional licence change, review the new text
# and update this value.
#
# Yocto reference:
# https://docs.yoctoproject.org/scarthgap/dev-manual/licenses.html#tracking-license-changes
LIC_FILES_CHKSUM = "file://LICENSE;md5=b2a551156d047ff7f73d0c43858d552a"

inherit cmake pkgconfig systemd useradd externalsrc

FILESEXTRAPATHS:prepend := "${IOT_APP_PROJECT_ROOT}/iot_app/image_support:"

# Build the source already checked out beside this layer. The root repository
# pins LVGL and MicroPython as submodules, so the recipe always uses the same
# revisions as a normal local build.
EXTERNALSRC = "${IOT_APP_PROJECT_ROOT}"
EXTERNALSRC_BUILD = "${WORKDIR}/build"
OECMAKE_SOURCEPATH = "${S}/iot_app"

DEPENDS = "cjson libdrm mosquitto openssl python3-native"

EXTRA_OECMAKE = " \
    -DLVGL_DIR=${S}/lvgl \
    -DMICROPYTHON_DIR=${S}/micropython \
    -DIOT_BUILD_TESTS=OFF \
"

SRC_URI = " \
    file://iot-app-launcher \
    file://iot-app.service \
    file://iot-app-hide-tty1-cursor \
    file://iot-app-wait-ready \
    file://70-iot-app-access.rules \
"

SYSTEMD_SERVICE:${PN} = "iot-app.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

USERADD_PACKAGES = "${PN}"
GROUPADD_PARAM:${PN} = "--system iot-app; --system render; --system i2c; --system input"
USERADD_PARAM:${PN} = "--system --no-create-home --home-dir /nonexistent \
                       --shell /sbin/nologin --gid iot-app \
                       --groups video,render,i2c,input iot-app"

do_install:append() {
    install -D -m 0755 "${WORKDIR}/iot-app-launcher" \
        "${D}${libexecdir}/iot-app-launcher"
    install -D -m 0755 "${WORKDIR}/iot-app-hide-tty1-cursor" \
        "${D}${libexecdir}/iot-app-hide-tty1-cursor"
    install -D -m 0755 "${WORKDIR}/iot-app-wait-ready" \
        "${D}${libexecdir}/iot-app-wait-ready"
    install -D -m 0644 "${WORKDIR}/iot-app.service" \
        "${D}${systemd_system_unitdir}/iot-app.service"
    install -D -m 0644 "${WORKDIR}/70-iot-app-access.rules" \
        "${D}${nonarch_base_libdir}/udev/rules.d/70-iot-app-access.rules"
}

FILES:${PN} += " \
    ${libexecdir}/iot-app-launcher \
    ${libexecdir}/iot-app-hide-tty1-cursor \
    ${libexecdir}/iot-app-wait-ready \
    ${systemd_system_unitdir}/iot-app.service \
    ${nonarch_base_libdir}/udev/rules.d/70-iot-app-access.rules \
"

RDEPENDS:${PN} += "iproute2"
