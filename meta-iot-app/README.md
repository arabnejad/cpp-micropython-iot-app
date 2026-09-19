# IoT App Yocto layer

`meta-iot-app` is the project-owned Yocto layer. It tells BitBake how to build
IoT App, which packages belong in the Raspberry Pi image, how its services
start, and how the SD-card image is partitioned.

The layer works with the pinned `poky`, `meta-openembedded`, and
`meta-raspberrypi` submodules. Those upstream layers provide the Linux system,
the Raspberry Pi 4 hardware support, and most packages. This layer contains
only the choices and files that are specific to IoT App.

Use the root Makefile to prepare and build the image. The complete setup,
build, flash, update, and troubleshooting instructions are in the
[Yocto image guide](../iot_app/docs/yocto/README.md). The
[Yocto tutorial](../iot_app/docs/yocto-tutorial/README.md) introduces recipes,
recipe appends, layers, tasks, packages, systemd services, and Wic in more
detail.

## How the layer is used

```text
make yocto-prepare
        |
        +--> creates the persistent Yocto build configuration
        +--> copies private Wi-Fi and optional SSH key files
        +--> generates the Wic partition layout
        |
        v
build/conf/bblayers.conf includes meta-iot-app
        |
        v
meta-iot-app/conf/layer.conf registers this layer's recipes
        |
        v
bitbake iot-app-image
        |
        +--> builds the IoT App package
        +--> builds the system-configuration package
        +--> extends Mosquitto, wpa_supplicant, and mpv
        +--> installs all selected packages into the root filesystem
        +--> creates the boot, root, and data partitions with Wic
        |
        v
Bootable Raspberry Pi 4 .wic.xz image
```

`meta-raspberrypi` supplies the `raspberrypi4-64` machine configuration and
board support. `meta-iot-app` adds the application and turns the general Yocto
system into the IoT App image.

## Directory map

```text
meta-iot-app/
├── conf/
│   ├── layer.conf
│   ├── distro/
│   │   └── iot-app-linux.conf
│   └── templates/raspberrypi4-64/
│       ├── bblayers.conf.sample
│       ├── local.conf.sample
│       └── conf-notes.txt
├── recipes-connectivity/
│   ├── mosquitto/
│   │   └── mosquitto_%.bbappend
│   └── wpa-supplicant/
│       └── wpa-supplicant_%.bbappend
├── recipes-core/
│   ├── images/
│   │   └── iot-app-image.bb
│   └── iot-app-system-config/
│       ├── iot-app-system-config_1.0.bb
│       └── files/
│           ├── 10-eth0.network
│           ├── 20-wlan0.network
│           ├── iot-app-refresh-mdns-hostname
│           ├── iot-app-refresh-mdns-hostname.service
│           ├── iot-app-storage.service
│           ├── iot-app-wifi.service
│           └── mosquitto.service
├── recipes-iot/
│   └── iot-app/
│       ├── iot-app_0.1.0.bb
│       └── files/
│           ├── 70-iot-app-access.rules
│           └── iot-app.service
├── recipes-multimedia/
│   └── mpv/
│       └── mpv_0.35.1.bbappend
└── wic/
    └── iot-app-raspberrypi.wks.in
```

The directory names such as `recipes-core` and `recipes-iot` group related
recipes for people reading the layer. BitBake finds the actual `.bb` and
`.bbappend` files through the patterns in `conf/layer.conf`.

## Configuration files

### `conf/layer.conf`

This is the entry point for the layer. It:

- Adds this directory to BitBake's search path.
- Tells BitBake where this layer keeps `.bb` and `.bbappend` files.
- Registers the layer under the name `iot_app` with priority 8.
- Declares compatibility with the Yocto Scarthgap release.
- Calculates `IOT_APP_PROJECT_ROOT`, which recipes use to reach the application
  source and shared files in the parent repository.

Without this file, adding `meta-iot-app` to `bblayers.conf` would not give
BitBake enough information to find the project's recipes.

### `conf/distro/iot-app-linux.conf`

This file defines the `iot-app-linux` distribution. A Yocto distribution is a
set of policy choices shared by every image built with it. The file:

- Starts with Poky's standard distribution settings.
- Uses systemd as the init and service manager.
- Removes desktop, audio, Bluetooth, NFC, mobile broadband, NFS, ptest, and
  other features that this image does not use.
- Keeps OpenGL and Zeroconf because video playback uses direct OpenGL/DRM and
  Avahi advertises `rspi-iot-app.local`.
- Uses IPK as the package format.
- Disables SPDX generation for local development builds.

The image recipe decides which packages are installed. The distribution file
sets broader policy that can affect many recipes.

### `conf/templates/raspberrypi4-64/`

`scripts/build/prepare-yocto.sh` points `TEMPLATECONF` at this directory the
first time it creates a build directory.

| File | Purpose |
|---|---|
| `bblayers.conf.sample` | Lists Poky, OpenEmbedded, Raspberry Pi, and IoT App layers that BitBake must read. It becomes `build/conf/bblayers.conf` when the build directory is first created. |
| `local.conf.sample` | Selects the Raspberry Pi 4 machine and IoT App distribution, configures image output, kernel modules, accepted recipe license flags, hostname, timezone, I2C, and a 1920x1080 mode for both HDMI connectors. `make yocto-prepare` copies it to `build/conf/local.conf` on every preparation run. |
| `conf-notes.txt` | Prints the short project-specific build hints after `oe-init-build-env` enters the build environment. |

The preparation script also creates files that are not stored in this layer:

```text
meta-iot-app configuration templates
        |
        | make yocto-prepare
        v
/opt/iot-app-builds/yocto-raspberry-pi-4/build/conf/
├── bblayers.conf
├── local.conf
├── iot-app-build-paths.conf
├── iot-app-raspberrypi.wks
├── wpa_supplicant.conf
└── ssh_authorized_keys
```

`iot-app-build-paths.conf` holds generated host paths such as `COREBASE`,
`DL_DIR`, and `SSTATE_DIR`. `local.conf` loads it with a `require` statement.
The private Wi-Fi and optional SSH key files are copied into the build
directory so secrets and machine-specific files do not have to be committed to
the layer.

## Recipes and recipe appends

A `.bb` file defines a package or image owned by this project. A `.bbappend`
file adds project settings to a recipe supplied by another layer.

| File | What it does |
|---|---|
| `recipes-core/images/iot-app-image.bb` | Defines the complete bootable image and selects its packages. |
| `recipes-core/iot-app-system-config/iot-app-system-config_1.0.bb` | Installs network, storage, mDNS, console, SSH-key, time, and service configuration. |
| `recipes-iot/iot-app/iot-app_0.1.0.bb` | Builds and installs the C++ application, runtime account, device permissions, helper programs, and service. |
| `recipes-connectivity/mosquitto/mosquitto_%.bbappend` | Installs the project's broker configuration into the upstream Mosquitto package. |
| `recipes-connectivity/wpa-supplicant/wpa-supplicant_%.bbappend` | Installs the private Wi-Fi configuration into the upstream wpa_supplicant package. |
| `recipes-multimedia/mpv/mpv_0.35.1.bbappend` | Enables the shared libmpv library and direct DRM, GBM, EGL, and OpenGL video output. |

The `%` in a `.bbappend` filename means it applies to any selected version of
that recipe. The mpv append names version `0.35.1` deliberately because its
build options are specific to that recipe version.

### The image recipe

`iot-app-image.bb` creates the final root filesystem. It starts with Yocto's
minimal boot package group, enables OpenSSH with root login for development,
and installs the packages needed for:

- IoT App and its system configuration.
- Wi-Fi, Ethernet, firmware, and network tools.
- Mosquitto and Avahi.
- Framebuffer, OpenGL, hardware video decoding, and I2C access.
- HTTPS certificate checking.
- Filesystem checks, resizing, partition growth, and persistent storage.
- Timezone data for Europe/London.

It also defines the development root-password hash and reserves free space in
the fixed root filesystem. The partition sizes themselves come from
`storage_layout.conf`, not from this recipe.

### The IoT App recipe

`iot-app_0.1.0.bb` uses `externalsrc`, so CMake builds the source in the current
repository instead of downloading a source archive. Its main jobs are:

1. Declare the libraries needed to compile IoT App.
2. Pass the checked-out LVGL and MicroPython directories to CMake.
3. Install the C++ executable and default Python application through CMake.
4. Install the launcher, cursor helper, video diagnostic, systemd unit, and
   udev rules.
5. Create the unprivileged `iot-app` user and the hardware-access groups used
   by the service.
6. Enable `iot-app.service` for normal boot.

Some installed helper files come from
[`iot_app/image_support`](../iot_app/image_support/README.md), where they are
shared with Buildroot. The systemd unit and udev rules remain in this layer
because Buildroot uses SysV init and BusyBox mdev instead.

### The system-configuration recipe

`iot-app-system-config_1.0.bb` contains operating-system setup that does not
belong to the application executable. It installs and enables:

- DHCP configuration for `eth0` and `wlan0` through systemd-networkd.
- The service that starts wpa_supplicant for `wlan0`.
- The helper and retrying service that refresh Avahi after an address appears.
- The service that prepares and mounts the optional `/data` partition.
- The project Mosquitto service definition.
- systemd-networkd, systemd-resolved, and systemd-timesyncd startup links.
- An optional root SSH `authorized_keys` file prepared from the repository
  root.

It also disables the login service on `tty1`, which belongs to the framebuffer
dashboard, and enables a password-protected recovery console on `tty2`.

## Files installed by the layer

The table below shows where the main project-owned files appear in the target
image.

| Source | Installed path | Used by |
|---|---|---|
| `recipes-iot/iot-app/files/iot-app.service` | `/usr/lib/systemd/system/iot-app.service` | systemd starts and restarts IoT App |
| `recipes-iot/iot-app/files/70-iot-app-access.rules` | `/usr/lib/udev/rules.d/70-iot-app-access.rules` | udev assigns framebuffer, DRM, video, I2C, and input devices to the service groups |
| `iot_app/image_support/iot-app-launcher` | `/usr/libexec/iot-app-launcher` | Chooses the development override or installed executable |
| `iot_app/image_support/iot-app-hide-tty1-cursor` | `/usr/libexec/iot-app-hide-tty1-cursor` | Hides the console cursor before the dashboard starts |
| `iot_app/image_support/iot-app-check-video-playback` | `/usr/bin/iot-app-check-video-playback` | Checks direct video playback on the target |
| `recipes-core/iot-app-system-config/files/iot-app-wifi.service` | `/usr/lib/systemd/system/iot-app-wifi.service` | Starts wpa_supplicant on `wlan0` |
| `recipes-core/iot-app-system-config/files/iot-app-storage.service` | `/usr/lib/systemd/system/iot-app-storage.service` | Prepares optional persistent storage before IoT App |
| `iot_app/image_support/iot-app-prepare-data-storage` | `/usr/libexec/iot-app-prepare-data-storage` | Expands and mounts the optional data partition |
| `recipes-core/iot-app-system-config/files/iot-app-refresh-mdns-hostname.service` | `/usr/lib/systemd/system/iot-app-refresh-mdns-hostname.service` | Runs the mDNS refresh helper after networking starts |
| `recipes-core/iot-app-system-config/files/iot-app-refresh-mdns-hostname` | `/usr/libexec/iot-app-refresh-mdns-hostname` | Waits for an address and restarts Avahi |
| `recipes-core/iot-app-system-config/files/mosquitto.service` | `/etc/systemd/system/mosquitto.service` | Replaces the broker service definition used by this image |
| `recipes-core/iot-app-system-config/files/10-eth0.network` | `/etc/systemd/network/10-eth0.network` | Requests an Ethernet address with DHCP |
| `recipes-core/iot-app-system-config/files/20-wlan0.network` | `/etc/systemd/network/20-wlan0.network` | Requests a Wi-Fi address with DHCP |
| `iot_app/image_support/mosquitto.conf` | `/etc/mosquitto/mosquitto.conf` | Configures the MQTT broker used for deployments |
| Prepared `wpa_supplicant.conf` | `/etc/wpa_supplicant.conf` | Supplies the private Wi-Fi settings |
| Prepared `ssh_authorized_keys` when non-empty | `/root/.ssh/authorized_keys` | Allows root SSH login with the selected public keys |

A file under a recipe's `files/` directory is not installed merely because it
exists there. The recipe must list it in `SRC_URI` and copy it into `${D}` from
an install task. `${D}` is the temporary directory that becomes that package's
part of the target root filesystem.

## SD-card partition layout

`wic/iot-app-raspberrypi.wks.in` is the source template for the SD-card layout.
It describes three partitions:

1. A 100 MiB FAT boot partition containing Raspberry Pi firmware and the
   kernel.
2. A fixed-size ext4 Linux root partition.
3. A small initial ext4 partition labelled `iot-data`.

`make yocto-prepare` reads the sizes from the root-level
`storage_layout.conf`, replaces the placeholders in the template, and writes
`build/conf/iot-app-raspberrypi.wks`. BitBake gives the generated file to Wic
after the root filesystem is ready. On first boot, `iot-app-storage.service`
expands `iot-data` into the remaining SD-card space and mounts it at `/data`.

The [storage guide](../iot_app/docs/storage/README.md) explains the resulting
layout and what happens when the optional data partition is unavailable.

## Where to make a change

| Change | File or directory |
|---|---|
| Add or remove a package from the image | `recipes-core/images/iot-app-image.bb` |
| Add a dependency needed while compiling IoT App | `DEPENDS` in `recipes-iot/iot-app/iot-app_0.1.0.bb` |
| Add a program needed by IoT App on the device | `RDEPENDS` in the IoT App recipe, or add its package to the image recipe |
| Add an IoT App systemd service | Put the unit in the appropriate `files/` directory, add it to that recipe's `SRC_URI`, install it in `do_install`, and add it to `SYSTEMD_SERVICE` |
| Change Ethernet or Wi-Fi DHCP behaviour | `recipes-core/iot-app-system-config/files/*.network` |
| Change the installed Wi-Fi configuration flow | `wpa-supplicant_%.bbappend` and `scripts/build/prepare-yocto.sh` |
| Change the MQTT listener configuration | `iot_app/image_support/mosquitto.conf` and `mosquitto_%.bbappend` |
| Change IoT App hardware permissions | `recipes-iot/iot-app/files/70-iot-app-access.rules` and the user/group declarations in the IoT App recipe |
| Change broad distribution features or the init system | `conf/distro/iot-app-linux.conf` |
| Change Raspberry Pi kernel modules, boot arguments, hostname, or timezone | `conf/templates/raspberrypi4-64/local.conf.sample` |
| Change the root or data partition layout | `wic/iot-app-raspberrypi.wks.in`, `storage_layout.conf`, and the preparation script |
| Change how mpv is built | `recipes-multimedia/mpv/mpv_0.35.1.bbappend` |

Edit tracked templates and recipes rather than generated files below
`/opt/iot-app-builds`. `make yocto-prepare` deliberately recreates several
files in `build/conf`, so direct changes there may disappear on the next build.

## Related documentation

- [Yocto image guide](../iot_app/docs/yocto/README.md): commands, flashing,
  incremental deployment, and troubleshooting.
- [Yocto tutorial](../iot_app/docs/yocto-tutorial/README.md): how Yocto concepts
  and BitBake syntax work.
- [Device image guide](../iot_app/docs/device-image/README.md): behaviour shared
  by the Buildroot and Yocto images.
- [Storage guide](../iot_app/docs/storage/README.md): root and persistent data
  partitions.
- [Shared image-support files](../iot_app/image_support/README.md): helpers and
  configuration installed by both image systems.
