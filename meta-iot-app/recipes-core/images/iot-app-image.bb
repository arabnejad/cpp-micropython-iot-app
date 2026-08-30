SUMMARY = "Bootable Raspberry Pi image for IoT App"
DESCRIPTION = "Console image with IoT App, embedded MicroPython, framebuffer output, Wi-Fi, Ethernet, SSH, MQTT, and time synchronization."
LICENSE = "MIT"

inherit core-image extrausers

# core-image normally installs packagegroup-base-extended. That package group
# is useful for a general Linux image, but it also brings in services such as
# Bluetooth, NFC, mobile broadband, and NFS. IoT App only needs the minimal
# boot packages plus the services listed below.
CORE_IMAGE_BASE_INSTALL = "packagegroup-core-boot"

IMAGE_FEATURES += "ssh-server-openssh allow-root-login"

# The dashboard and command-line tools use the C locale. Timezone data is
# selected separately below, so translated locale packages are not needed.
IMAGE_LINGUAS = ""

IMAGE_INSTALL:append = " \
    avahi-daemon \
    e2fsprogs-e2fsck \
    e2fsprogs-resize2fs \
    e2fsprogs-tune2fs \
    iot-app \
    iot-app-system-config \
    i2c-tools \
    iproute2 \
    kernel-module-brcmfmac \
    kernel-module-brcmfmac-wcc \
    linux-firmware-rpidistro-bcm43455 \
    linux-firmware-rpidistro-bcm43456 \
    mosquitto \
    parted \
    tzdata-core \
    tzdata-europe \
    util-linux-partx \
    wireless-regdb-static \
    wpa-supplicant \
"

# wpa_supplicant can connect using the installed configuration on its own.
# The command-line helpers are useful on a general-purpose Linux system but
# are not used by the Wi-Fi service in this image.
BAD_RECOMMENDATIONS += " \
    wpa-supplicant-cli \
    wpa-supplicant-passphrase \
    wpa-supplicant-plugins \
"

# This development image uses root/root to match the Buildroot image. Replace
# the password and disable direct root login before deploying a product.
# Escape the dollar signs so they survive the shell task created by
# extrausers. Otherwise, the shell expands values such as $6 and $iot and
# corrupts the hash.
ROOT_PASSWORD_HASH = "\$6\$iot-app\$dItYh5pGokmxs3xvj03.1Za.o0Tcj10gqzSuWR1lcHKlui3dlDPuRiqtm7LaW6dUuffjT/5N9vW4ZlJzr3xU5."
EXTRA_USERS_PARAMS = "usermod -p '${ROOT_PASSWORD_HASH}' root;"

# Keep 128 MiB free inside the fixed root filesystem for package updates and
# logs. This does not set the partition size. storage_layout.conf controls the
# root partition, while files that need more space belong in /data. Yocto
# measures IMAGE_ROOTFS_EXTRA_SPACE in KiB.
IMAGE_ROOTFS_EXTRA_SPACE = "131072"

COMPATIBLE_MACHINE = "^raspberrypi4-64$"
