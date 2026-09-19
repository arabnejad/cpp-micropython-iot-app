# Raspberry Pi 4 Buildroot board files

This directory contains the Raspberry Pi 4 Model B customizations used by the
IoT App Buildroot image. The files control four different parts of the build:

- Raspberry Pi firmware and Linux startup settings.
- Linux kernel and BusyBox features.
- Files copied into the target root filesystem.
- Assembly of the final SD-card image.

The main Buildroot configuration is
[`iot_rpi4_defconfig`](../../configs/iot_rpi4_defconfig). It selects each file
in this directory through settings such as `BR2_ROOTFS_OVERLAY`,
`BR2_ROOTFS_POST_BUILD_SCRIPT`, and `BR2_ROOTFS_POST_IMAGE_SCRIPT`.

## Directory contents

```text
raspberrypi4/
├── README.md
├── busybox-iot-app.config
├── cmdline.txt
├── config_4_64bit.txt
├── genimage.cfg.in
├── linux-iot-app.config
├── post-build.sh
├── post-image.sh
└── rootfs-overlay/
    ├── etc/
    │   ├── init.d/
    │   │   ├── S25data-storage
    │   │   ├── S30wifi
    │   │   └── S45time-sync
    │   ├── mdev.conf
    │   └── network/interfaces
    └── usr/share/udhcpc/default.script.d/
        └── 50-refresh-avahi-hostname
```

The following sections explain when Buildroot uses each item.

## Build flow

The board files take part in the build in this order:

```text
iot_rpi4_defconfig
        |
        +--> configures BusyBox with busybox-iot-app.config
        |
        +--> configures Linux with linux-iot-app.config
        |
        +--> selects config_4_64bit.txt and cmdline.txt as boot files
        |
        v
Buildroot installs packages into its target directory
        |
        v
rootfs-overlay is copied over that target directory
        |
        v
post-build.sh adds private configuration and adjusts the console
        |
        v
Buildroot creates the root filesystem, kernel, firmware, and device trees
        |
        v
post-image.sh refreshes config.txt and cmdline.txt, fills in genimage.cfg.in,
and runs genimage
        |
        v
sdcard.img containing boot, root, and data partitions
```

The Buildroot target directory is a normal directory on the development
computer that represents `/` on the finished device. For example, copying a
file to `TARGET_DIR/etc/init.d` places it under `/etc/init.d` in the image.

## Firmware configuration: `config_4_64bit.txt`

This is the project-owned Raspberry Pi firmware configuration. Despite its
source filename, it is installed into the boot filesystem as `config.txt`.
The Raspberry Pi firmware reads `config.txt` before it starts Linux.

The file selects:

- The matching Raspberry Pi 4 firmware files.
- The 64-bit Linux kernel named `Image`.
- The full KMS display driver and framebuffer support.
- Serial-console routing.
- The I2C controller and its 400 kHz bus speed.

Each setting is explained beside the setting in the file. The
[Raspberry Pi config.txt documentation](https://www.raspberrypi.com/documentation/computers/config_txt.html)
describes the complete firmware configuration format.

## Kernel command line: `cmdline.txt`

Buildroot copies this file into the boot filesystem under the same name. The
Raspberry Pi firmware passes its contents to Linux as the kernel command line.

Unlike `config_4_64bit.txt`, `cmdline.txt` cannot contain comments. Every
argument must remain on one line because the firmware ignores anything after
the first line. The
[Raspberry Pi kernel-command-line documentation](https://www.raspberrypi.com/documentation/computers/configuration.html#configure-the-kernel-command-line)
describes this requirement.

The project uses this command line:

```text
root=/dev/mmcblk0p2 rootwait console=tty1 console=ttyAMA0,115200 quiet loglevel=4 video=HDMI-A-1:1920x1080@60 video=HDMI-A-2:1920x1080@60
```

Each space separates one Linux startup argument:

| Argument | Meaning |
|---|---|
| `root=/dev/mmcblk0p2` | Mount the second partition of the first SD/MMC device as `/`. The image layout places the Linux root filesystem in this partition. |
| `rootwait` | Wait until the SD card and root partition appear instead of failing when storage detection takes longer than Linux startup. |
| `console=tty1` | Send kernel console output to the first text terminal shown on the attached monitor. IoT App later reserves this terminal for its framebuffer display. |
| `console=ttyAMA0,115200` | Also send console output to the PL011 hardware serial port at 115,200 bits per second. The `miniuart-bt` overlay in `config_4_64bit.txt` makes this port available as `ttyAMA0`. |
| `quiet` | Set the normal console threshold to warning level, hiding most routine boot messages so they do not cover the dashboard. |
| `loglevel=4` | Explicitly select that same warning-level threshold. It reinforces the `quiet` setting: warning and more serious messages can appear, while informational and debug messages normally stay off the display. |
| `video=HDMI-A-1:1920x1080@60` | Request 1920 by 1080 pixels at 60 Hz on the first HDMI connector. |
| `video=HDMI-A-2:1920x1080@60` | Request the same mode on the second HDMI connector. Setting both connectors gives the framebuffer and IoT App a predictable size regardless of which Raspberry Pi HDMI port is used. |

Linux documents `rootwait`, `console`, `quiet`, and `loglevel` in its
[kernel-parameter reference](https://www.kernel.org/doc/html/latest/admin-guide/kernel-parameters.html).
The display-mode format is described in the
[Linux video-mode documentation](https://docs.kernel.org/6.6/fb/modedb.html).

After boot, this command shows the command line Linux actually received:

```sh
cat /proc/cmdline
```

The displayed result can contain extra values added by the Raspberry Pi
firmware, so it may not be identical to the original `cmdline.txt`.

## BusyBox configuration: `busybox-iot-app.config`

Buildroot starts with its normal BusyBox configuration and merges this small
fragment into it. The fragment enables the BusyBox `ntpd` program used for
network time synchronization and disables NTP features the image does not use.

This is a fragment rather than a complete BusyBox configuration. It contains
only the decisions that are specific to IoT App. Its comments explain the
individual Kconfig settings.

The defconfig connects it to Buildroot with:

```text
BR2_PACKAGE_BUSYBOX_CONFIG_FRAGMENT_FILES="$(BR2_EXTERNAL_IOT_PROJECT_PATH)/board/raspberrypi4/busybox-iot-app.config"
```

## Linux configuration: `linux-iot-app.config`

This is also a Kconfig fragment. Buildroot first uses the Raspberry Pi
`bcm2711` kernel configuration, then merges the settings in this file.

The fragment enables the kernel support needed for:

- `/dev/i2c-1` and the Adafruit I2C gamepad.
- The VC4 display, V3D graphics acceleration, and `/dev/fb0`.
- Hardware-assisted video decoding used by mpv.
- The Raspberry Pi 4 onboard Wi-Fi adapter.

The comments at the top of the file explain the difference between `=y`, `=m`,
and `is not set`.

## Root filesystem overlay: `rootfs-overlay`

A Buildroot overlay mirrors the directory structure of the finished Linux
system. Buildroot copies it over the target filesystem after packages have
installed their files and before it runs `post-build.sh`.

```text
rootfs-overlay/etc/mdev.conf
                  |
                  v
finished image: /etc/mdev.conf
```

This overlay supplies the following files:

| Overlay file | Installed path | Purpose |
|---|---|---|
| `etc/init.d/S25data-storage` | `/etc/init.d/S25data-storage` | Prepares and mounts the optional persistent data partition at `/data`. |
| `etc/init.d/S30wifi` | `/etc/init.d/S30wifi` | Loads the onboard Wi-Fi driver when necessary and starts `wpa_supplicant`. Association continues in the background. |
| `etc/init.d/S45time-sync` | `/etc/init.d/S45time-sync` | Starts BusyBox `ntpd` in the background so the system clock is corrected when the network becomes available. |
| `etc/mdev.conf` | `/etc/mdev.conf` | Gives framebuffer, graphics, video, I2C, and input devices to the groups used by the `iot-app` account. It also asks `mdev` to load kernel modules that match discovered hardware. |
| `etc/network/interfaces` | `/etc/network/interfaces` | Brings up loopback, Wi-Fi, and Ethernet. Wi-Fi and Ethernet both request an address through DHCP. |
| `usr/share/udhcpc/default.script.d/50-refresh-avahi-hostname` | `/usr/share/udhcpc/default.script.d/50-refresh-avahi-hostname` | Runs after the first DHCP lease and refreshes Avahi so the device advertises the expected `rspi-iot-app.local` name. |

### Startup-script order

BusyBox init runs scripts under `/etc/init.d` in filename order. Some scripts
come from this overlay, while others are installed by Buildroot packages:

```text
S25data-storage    prepare optional /data storage
S30wifi            start Wi-Fi association
S40network         start DHCP for Wi-Fi and Ethernet
S45time-sync       start background clock synchronization
S50avahi-daemon    advertise rspi-iot-app.local
S90iot-app         start the dashboard
```

The number controls order; it does not mean a script must wait for the previous
service to finish its background work. Wi-Fi, DHCP, and time synchronization
can continue after later scripts have started.

### DHCP hook

`50-refresh-avahi-hostname` is not an init script. BusyBox `udhcpc` calls its
main DHCP script when an address is assigned. That main script then runs the
executable files in `default.script.d`, including this hook. The `50-` prefix
only orders it relative to other DHCP hooks.

Detailed comments inside the hook show its complete call path.

## Final root-filesystem changes: `post-build.sh`

Buildroot runs this script after packages and the overlay have populated the
target directory, but before it creates `rootfs.ext4`.

The script performs changes that depend on files outside the static overlay:

1. It copies the private project-root `wpa_supplicant.conf` into the image as
   `/etc/wpa_supplicant.conf` with mode `0600`.
2. If the optional project-root `ssh_authorized_keys` file exists and is not
   empty, it installs those public keys for root SSH login.
3. It removes the login prompt from `tty1`, preventing a cursor or typed text
   from appearing over the framebuffer dashboard.
4. It keeps a password-protected login prompt on `tty2` as an emergency local
   console.

The standard Buildroot Raspberry Pi post-build script runs first. The IoT App
defconfig then runs this project-owned script to apply the additional changes.

Buildroot supplies paths such as `TARGET_DIR` in the script environment. The
script is normally called by Buildroot and should not be run directly.

## Partition template: `genimage.cfg.in`

`genimage` combines separate filesystem files into one disk image. This file is
a template rather than the final `genimage` configuration. It contains three
placeholders that `post-image.sh` fills in:

- `#BOOT_FILES#` becomes the list of firmware, Device Tree, configuration, and
  kernel files copied into the boot filesystem.
- `@ROOT_PARTITION_SIZE_MIB@` becomes the configured Linux root-partition size.
- `@DATA_PARTITION_BOOTSTRAP_SIZE_MIB@` becomes the initial data-partition
  size stored in the image.

The template describes this partition layout:

```text
sdcard.img
├── partition 1: 32 MiB FAT boot filesystem
├── partition 2: ext4 Linux root filesystem
└── partition 3: ext4 filesystem labelled iot-data
```

The data partition begins small to keep the generated image reasonably sized.
During first boot, `iot-app-prepare-data-storage` expands the third partition
and its filesystem to use the remaining space on the SD card.

## Final image assembly: `post-image.sh`

Buildroot runs this script after it has created the kernel, firmware files,
Device Trees, and Linux root filesystem.

The script:

1. Reads the root and initial data sizes prepared from `storage_layout.conf`.
2. Finds all files required in the Raspberry Pi boot filesystem.
3. Replaces the placeholders in `genimage.cfg.in`.
4. Writes the generated configuration as `genimage-iot-app.cfg` in the
   Buildroot images directory so it can be inspected later.
5. Creates the initial empty ext4 data filesystem with the `iot-data` label.
6. Runs `genimage` to produce `sdcard.img`.

The script uses Bash because it builds an array of boot filenames and performs
string replacement while filling in the template. Buildroot supplies the
`BASE_DIR`, `BINARIES_DIR`, `BUILD_DIR`, and `HOST_DIR` paths it uses.

The comments inside `post-image.sh` explain each command in more detail. The
[Buildroot root-filesystem customization guide](https://buildroot.org/downloads/manual/manual.html#rootfs-custom)
describes overlays, post-build scripts, and post-image scripts.

## Source files and generated files

Files in this board directory are project source files and should be committed.
Buildroot creates working and generated copies under its output directory,
including:

```text
images/rpi-firmware/config.txt
images/rpi-firmware/cmdline.txt
images/genimage-iot-app.cfg
images/boot.vfat
images/rootfs.ext4
images/data.ext4
images/sdcard.img
```

Do not edit those generated copies as a permanent change. Update the matching
source file in this directory and rebuild the image instead.
