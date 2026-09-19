# Understanding Buildroot in IoT App

This tutorial explains what Buildroot does and how this project uses it. It is
about the files and ideas behind the image rather than the commands needed to
build and flash one. For those commands, use the
[Buildroot image guide](../buildroot/README.md).

The examples use the Raspberry Pi 4 configuration supplied by this project.
They also show where to make a change when a new program, configuration file,
or startup service is needed.

## 1. What Buildroot builds

Buildroot creates a small Linux system for a target device. From one saved
configuration, it can prepare the cross-compiler, compile selected programs,
build the Linux kernel, create the root filesystem, and assemble the files
needed to boot the board.

The compiler runs on the Ubuntu computer but produces AArch64 code for the
Raspberry Pi. This is called cross-compilation.

```text
Ubuntu build computer
        |
        | Buildroot configuration and package rules
        v
Cross-compiler builds programs for AArch64
        |
        +--> Linux kernel and device trees
        +--> Raspberry Pi boot files
        +--> Linux root filesystem
        |
        v
Bootable SD-card image
```

Buildroot is not a normal Linux distribution with an online package repository
on the Raspberry Pi. Software is selected before the image is built and is
copied into the root filesystem during the build. In this project, changing
the permanent contents of the image means rebuilding the relevant package or
image.

The [Buildroot manual](https://buildroot.org/downloads/manual/manual.html)
describes the complete build system. The
[Bootlin Buildroot training labs](https://bootlin.com/doc/training/buildroot/buildroot-bbb-labs.pdf)
provide practical exercises for packages, overlays, kernel settings, and image
inspection.

## 2. A few Buildroot terms

| Term | Meaning in this project |
|---|---|
| Host | The Ubuntu computer running Buildroot |
| Target | The Raspberry Pi and the Linux system built for it |
| Toolchain | The compiler, linker, C library, and related tools used to build AArch64 programs |
| Configuration | The saved list of Buildroot options and selected packages |
| Package | Build instructions for one program or library |
| Root filesystem | The files that become `/`, such as `/usr/bin/iot_app` and `/etc/init.d/S90iot-app` |
| Root filesystem overlay | A directory copied over the target filesystem near the end of the build |
| Post-build script | A script that adjusts the completed target filesystem before an image is made |
| Post-image script | A script that combines finished filesystems and boot files into a disk image |
| `BR2_EXTERNAL` tree | Project-owned Buildroot configuration kept outside the upstream Buildroot source |

## 3. The build from start to finish

The project runs Buildroot through the root Makefile, but Buildroot still owns
the actual package and image work.

```text
make buildroot-prepare
        |
        +--> load iot_rpi4_defconfig
        +--> write the chosen partition sizes
        +--> refresh Buildroot's output/.config
        |
        v
make buildroot-image
        |
        +--> prepare or reuse the cross-toolchain
        +--> build selected libraries and programs
        +--> install packages into output/target
        +--> copy the root filesystem overlay
        +--> run post-build.sh
        +--> create rootfs.ext4
        +--> run post-image.sh and genimage
        |
        v
/opt/iot-app-builds/images/iot-app-buildroot-rpi4.img
```

The main Buildroot output directories are:

| Directory | What it contains |
|---|---|
| `build/` | A separate build directory for every source package |
| `host/` | The cross-compiler and programs that run on the Ubuntu host |
| `staging/` | Headers and libraries used while cross-compiling target programs |
| `target/` | A working copy of the Raspberry Pi root filesystem |
| `images/` | The kernel, filesystem images, firmware, and final `sdcard.img` |

`target/` is useful for inspection, but it is not the final disk image and
should not be copied directly to a card.

## 4. Why the project has a `buildroot_external` directory

The `buildroot/` submodule is pinned upstream source. Project-specific changes
live in [`iot_app/buildroot_external`](../../buildroot_external/) so updating
Buildroot does not require carrying edits inside the submodule.

```text
Upstream Buildroot submodule
buildroot/
        |
        | BR2_EXTERNAL=.../iot_app/buildroot_external
        v
Project extension
iot_app/buildroot_external/
        |
        +--> saved Raspberry Pi configuration
        +--> IoT App package
        +--> board configuration and rootfs overlay
        +--> final image assembly
```

The top-level external-tree files have small but important jobs:

| File | Purpose |
|---|---|
| [`external.desc`](../../buildroot_external/external.desc) | Names the external tree `IOT_PROJECT`. Buildroot uses that name to create `BR2_EXTERNAL_IOT_PROJECT_PATH`. |
| [`Config.in`](../../buildroot_external/Config.in) | Adds project package choices to Buildroot's configuration menus. It currently includes the IoT App package. |
| [`external.mk`](../../buildroot_external/external.mk) | Includes every package `.mk` file found under `package/`. |
| [`configs/iot_rpi4_defconfig`](../../buildroot_external/configs/iot_rpi4_defconfig) | Stores the reproducible Raspberry Pi 4 Buildroot choices. |

This layout follows Buildroot's
[`BR2_EXTERNAL` model](https://buildroot.org/downloads/manual/manual.html#outside-br-custom).

### 4.1 Complete project tree

This is the complete project-owned Buildroot tree. Generated output is kept
under `/opt`, so it does not appear here.

```text
iot_app/buildroot_external/
├── external.desc
├── Config.in
├── external.mk
├── configs/
│   └── iot_rpi4_defconfig
├── package/
│   └── iot_app/
│       ├── Config.in
│       ├── iot_app.mk
│       └── iot-app
└── board/raspberrypi4/
    ├── README.md
    ├── busybox-iot-app.config
    ├── linux-iot-app.config
    ├── config_4_64bit.txt
    ├── cmdline.txt
    ├── genimage.cfg.in
    ├── post-build.sh
    ├── post-image.sh
    └── rootfs-overlay/
        ├── etc/init.d/S25data-storage
        ├── etc/init.d/S30wifi
        ├── etc/init.d/S45time-sync
        ├── etc/mdev.conf
        ├── etc/network/interfaces
        └── usr/share/udhcpc/default.script.d/
            └── 50-refresh-avahi-hostname
```

The numbered scripts are explained in the service section. `mdev.conf` assigns
the framebuffer, graphics, I2C, video, and input devices to the groups used by
the `iot-app` account. `50-refresh-avahi-hostname` runs after DHCP obtains an
address and refreshes Avahi so the expected `.local` hostname is advertised.

## 5. How the Raspberry Pi configuration is built up

Buildroot does not use one configuration file for everything. The project has
one main Buildroot configuration and smaller configuration files for parts
which Buildroot delegates to other projects.

```text
iot_rpi4_defconfig
        |
        +--> selects architecture, toolchain, packages and image type
        +--> points to the BusyBox configuration fragment
        +--> points to the Linux kernel configuration fragment
        +--> points to the rootfs overlay and board scripts
        +--> points to Raspberry Pi config.txt and cmdline.txt
```

| File | What it controls |
|---|---|
| [`iot_rpi4_defconfig`](../../buildroot_external/configs/iot_rpi4_defconfig) | AArch64/Cortex-A72 target, toolchain, packages, hostname, root password, timezone, filesystem, and board hooks |
| [`busybox-iot-app.config`](../../buildroot_external/board/raspberrypi4/busybox-iot-app.config) | Extra BusyBox commands needed by the image, including the NTP client/server program |
| [`linux-iot-app.config`](../../buildroot_external/board/raspberrypi4/linux-iot-app.config) | Kernel features needed for the framebuffer, graphics, I2C, Wi-Fi, and related hardware |
| [`config_4_64bit.txt`](../../buildroot_external/board/raspberrypi4/config_4_64bit.txt) | Raspberry Pi firmware settings, including KMS, 64-bit boot, and the I2C clock |
| [`cmdline.txt`](../../buildroot_external/board/raspberrypi4/cmdline.txt) | Linux kernel command line, including the root partition, console, quiet logging, and display mode |

`cmdline.txt` must remain one line and cannot contain explanatory comments. The
[Raspberry Pi 4 board README](../../buildroot_external/board/raspberrypi4/README.md#kernel-command-line-cmdlinetxt)
explains every argument separately.

`make buildroot-prepare` reloads the defconfig each time. It then changes the
root filesystem size using `storage_layout.conf` and runs `olddefconfig` so an
existing output directory learns about any new configuration options.

The generated `.config` in the Buildroot output directory is working state.
Permanent choices belong in `iot_rpi4_defconfig` or one of the project
fragments, not only in that generated file.

### 5.1 Which configuration are you changing?

Buildroot, BusyBox, and the Linux kernel each have their own configuration.
Their menus look similar because all three use Kconfig, but a change in one
menu does not update the other two.

```text
make menuconfig
      |
      +--> Buildroot packages, toolchain, filesystem and board settings

make busybox-menuconfig
      |
      +--> Commands and features compiled into BusyBox

make linux-menuconfig
      |
      +--> Linux kernel drivers and kernel features
```

The menu commands edit generated configuration below the Buildroot output
directory. They are useful for trying a setting and finding its exact symbol,
but those generated files are not the project's permanent source.

For this repository, copy the final choices to the matching tracked file:

| Setting | Keep it in |
|---|---|
| Buildroot package or image choice | `configs/iot_rpi4_defconfig` |
| Extra BusyBox setting | `busybox-iot-app.config` |
| Extra Linux kernel setting | `linux-iot-app.config` |

The project uses small BusyBox and kernel fragments on top of upstream base
configurations. Keeping only the required additions in those fragments makes
an upstream update easier to review than storing another complete generated
configuration. The Buildroot manual lists the separate
[component configuration commands](https://buildroot.org/downloads/manual/manual.html#_configuration_of_other_components).

## 6. How the IoT App package works

A normal Buildroot package has a `Config.in` file and a `.mk` file. The IoT App
package also has its BusyBox startup script:

```text
package/iot_app/
├── Config.in       user-visible selection and requirements
├── iot_app.mk      source, dependencies, build options and installation
└── iot-app         service script installed as S90iot-app
```

### 6.1 `Config.in`

[`package/iot_app/Config.in`](../../buildroot_external/package/iot_app/Config.in)
creates `BR2_PACKAGE_IOT_APP`. It checks that the toolchain supports C++ and
threads, then selects libraries required by the application, such as libdrm,
libcurl, OpenSSL, CA certificates, and libjpeg-turbo.

`depends on` controls whether the package choice is valid. `select` turns on a
dependency required by the selected feature. Buildroot explains the difference
in its
[`depends on` versus `select` guidance](https://buildroot.org/downloads/manual/manual.html#depends-on-vs-select).

### 6.2 `iot_app.mk`

[`iot_app.mk`](../../buildroot_external/package/iot_app/iot_app.mk) tells
Buildroot how to build and install the package:

```text
IOT_APP_SITE and IOT_APP_SITE_METHOD
        Where the source comes from

IOT_APP_DEPENDENCIES
        Packages that must be built first

IOT_APP_CONF_OPTS
        Extra CMake options, including LVGL and MicroPython paths

IOT_APP_USERS
        The unprivileged service account and hardware-access groups

IOT_APP_POST_INSTALL_TARGET_HOOKS
        Shared scripts and configuration copied into the target filesystem

IOT_APP_INSTALL_INIT_SYSV
        BusyBox service installed under /etc/init.d

$(eval $(cmake-package))
        Generates the standard configure, build and install rules for CMake
```

The dependency list is a build order, not merely a list of files to place in
the image:

```text
cJSON  curl  libdrm  JPEG  Mosquitto  mpv  OpenSSL
   \     |      |      |       |       |      /
    +----+------+------+-------+-------+-----+
                         |
                         v
                      iot_app
```

Buildroot's
[`cmake-package` infrastructure](https://buildroot.org/downloads/manual/manual.html#cmake-package-tutorial)
runs CMake with the target toolchain and staging paths. The project therefore
does not reproduce those cross-compilation commands in `iot_app.mk`.

### 6.3 Where installed files come from

```text
CMake install rules
        +--> /usr/bin/iot_app
        +--> /usr/lib/libiot_runtime.so
        +--> /usr/share/iot-app/default_python_application

iot_app.mk shared-image hook
        +--> /usr/libexec/iot-app-launcher
        +--> /usr/libexec/iot-app-prepare-data-storage
        +--> /usr/libexec/iot-app-hide-tty1-cursor
        +--> /usr/bin/iot-app-check-video-playback
        +--> /etc/mosquitto/mosquitto.conf

IOT_APP_INSTALL_INIT_SYSV
        +--> /etc/init.d/S90iot-app
        +--> /etc/init.d/iot-app -> S90iot-app
```

The files under [`iot_app/image_support`](../../image_support/README.md) are
shared with the Yocto image. The service definition is not shared because
Buildroot uses a BusyBox shell script and Yocto uses a systemd unit.

### 6.4 What happens when one package is built

Buildroot gives every package a predictable series of steps. A package may use
standard implementations supplied by `cmake-package`, `autotools-package`, or
another package helper instead of writing every step itself.

```text
Find or download source
        |
        v
Copy or extract it into output/build/<package>-<version>
        |
        v
Apply patches
        |
        v
Configure --> compile
        |
        +--> install development files needed by other builds into staging/
        |
        +--> install programs and runtime files into target/
```

Buildroot records completed steps with hidden stamp files in the package build
directory. A normal `make` skips work whose completion stamp is still valid;
it does not inspect every source file in a local package. The root
`make buildroot-app` command deliberately asks Buildroot to reconfigure
`iot_app`. That command synchronizes the local source again and repeats the
application's configure, build, and install steps without deleting the
toolchain or unrelated packages. Buildroot documents what its
[package-specific rebuild targets](https://buildroot.org/downloads/manual/manual.html#rebuild-pkg)
repeat and which targets also recreate the root filesystem.

Dependencies can provide two different kinds of input:

- A target dependency supplies AArch64 headers and libraries through
  `staging/`, and may also install runtime files in `target/`.
- A dependency whose name starts with `host-` supplies a program which runs on
  Ubuntu while the image is being built.

This is why a compiler or code generator found under `host/` does not
automatically appear on the Raspberry Pi.

## 7. Four ways to put files in the image

These mechanisms run at different times. Picking the right one keeps the
Buildroot setup easy to follow.

| Need | Best place | Example in this project |
|---|---|---|
| Install files owned by a software package | Package `.mk` or its normal install step | IoT App binary, launcher, and service |
| Add simple board policy or static configuration | Root filesystem overlay | Network interfaces and early boot scripts |
| Copy private or generated data, or adjust the completed rootfs | Post-build script | Wi-Fi credentials, SSH keys, and tty login changes |
| Combine completed filesystems and boot files | Post-image script | Boot, root, and data partitions in `sdcard.img` |

The order is roughly:

```text
Base root filesystem
        |
Packages install their files
        |
Root filesystem overlay is copied
        |
post-build.sh adjusts output/target
        |
rootfs.ext4 is created
        |
post-image.sh assembles sdcard.img
```

### 7.1 Root filesystem overlay

Everything below
[`rootfs-overlay`](../../buildroot_external/board/raspberrypi4/rootfs-overlay/)
is copied to the same absolute path in the target filesystem. For example:

```text
rootfs-overlay/etc/network/interfaces
                    |
                    v
target/etc/network/interfaces
                    |
                    v
/etc/network/interfaces on the Raspberry Pi
```

The overlay contains the project-owned storage, Wi-Fi, and time startup
scripts, the network configuration, `mdev` device permissions, and the DHCP
hook which refreshes the mDNS hostname.

An overlay is good at adding a file or replacing a file at the same path. It
does not express "remove this file from the generated root filesystem." If a
file created by another package must be removed, first check whether that
package can be configured not to install it. Use `post-build.sh` only when a
late removal or generated adjustment is genuinely board-specific.

Buildroot uses BusyBox `mdev` in this configuration rather than udev. When the
kernel announces a device, `mdev` creates its `/dev` entry and applies the
matching ownership and mode from `mdev.conf`:

```text
Kernel detects I2C adapter
        |
        v
mdev creates /dev/i2c-1
        |
        v
mdev.conf assigns group i2c and mode 0660
        |
        v
iot-app account can open the device through its i2c group
```

### 7.2 Post-build script

[`post-build.sh`](../../buildroot_external/board/raspberrypi4/post-build.sh)
runs after packages and the overlay have populated the target directory. It
installs the private root-level Wi-Fi configuration and optional SSH public
keys. It also removes the login prompt from `tty1` and keeps a recovery login
on `tty2`.

Private inputs are handled here because they are not committed inside the
public overlay.

### 7.3 Post-image script

[`post-image.sh`](../../buildroot_external/board/raspberrypi4/post-image.sh)
runs after Buildroot has produced the kernel, firmware, and root filesystem.
It fills in [`genimage.cfg.in`](../../buildroot_external/board/raspberrypi4/genimage.cfg.in),
creates the initial data filesystem, and asks `genimage` to assemble the final
disk image.

Buildroot documents overlays and both script stages in
[root filesystem customization](https://buildroot.org/downloads/manual/manual.html#rootfs-custom).

## 8. How services start in the Buildroot image

The supplied image uses BusyBox as PID 1. During boot, BusyBox reads
`/etc/inittab`. That file runs `/etc/init.d/rcS`, which calls files matching
`/etc/init.d/S??*` in name order.

```text
Linux starts /sbin/init
        |
        v
BusyBox reads /etc/inittab
        |
        v
/etc/init.d/rcS
        |
        +--> S25data-storage start
        +--> S30wifi start
        +--> other selected package scripts in name order
        +--> S45time-sync start
        +--> S90iot-app start
```

The number is the startup order, not a dependency declaration. `S30wifi`
starting successfully means that `wpa_supplicant` is running; it does not mean
the access point has already accepted the connection. For this reason, the
project starts Wi-Fi, DHCP, and time synchronization in the background. The
dashboard only waits briefly for `/dev/fb0`, which it actually needs.

Project service scripts support at least `start` and `stop`, and normally also
`restart`. The IoT App script uses `start-stop-daemon`, a PID file, and a log
file so it can detach from the console and stop the same process later.

The Buildroot manual gives current naming and service-script guidance in
[The `SNNfoo` start script](https://buildroot.org/downloads/manual/manual.html#adding-packages-start-script).
Bootlin's training material also walks through adding an `S` script with a
root filesystem overlay.

## 9. Adding another Buildroot service

First decide who owns the service.

- If it starts a program supplied by a package, install the service from that
  package. Removing the package will then remove its startup file too.
- If it is board-only boot policy and has no package of its own, putting the
  script in the board overlay is reasonable.

For example, suppose a new `sensor-agent` package must start after normal
network setup. Add this file beside its package recipe:

```text
package/sensor-agent/
├── Config.in
├── sensor-agent.mk
└── S60sensor-agent
```

The package rule installs the script:

```makefile
define SENSOR_AGENT_INSTALL_INIT_SYSV
	$(INSTALL) -D -m 0755 $(SENSOR_AGENT_PKGDIR)/S60sensor-agent \
		$(TARGET_DIR)/etc/init.d/S60sensor-agent
endef
```

A simple service script can use this shape:

```sh
#!/bin/sh

daemon=/usr/bin/sensor-agent
pidfile=/var/run/sensor-agent.pid

case "$1" in
  start)
    start-stop-daemon --start --background --make-pidfile \
      --pidfile "$pidfile" --exec "$daemon"
    ;;
  stop)
    start-stop-daemon --stop --pidfile "$pidfile" --exec "$daemon"
    rm -f "$pidfile"
    ;;
  restart)
    "$0" stop
    "$0" start
    ;;
  *)
    echo "Usage: $0 {start|stop|restart}"
    exit 1
    ;;
esac
```

Before using it, check these points:

1. The script has mode `0755` in the generated root filesystem.
2. Its number places it after the services it needs.
3. Long connection attempts happen in the daemon or background, not inside the
   boot script.
4. `stop` identifies the exact process and removes stale PID files.
5. A failed optional service reports the error without assuming `rcS` will
   solve its dependencies.

To add a board-only service instead, place the executable script at
`board/raspberrypi4/rootfs-overlay/etc/init.d/S60sensor-agent`. No package
install function is then needed.

## 10. Adding another Buildroot package

This example adds a small project script. A compiled CMake program follows the
same layout but ends with `$(eval $(cmake-package))`, as IoT App does.

```text
iot_app/buildroot_external/package/status-reporter/
├── Config.in
├── status-reporter.mk
└── status-reporter
```

`Config.in` provides the choice:

```text
config BR2_PACKAGE_STATUS_REPORTER
	bool "status-reporter"
	help
	  Installs the device status reporting script.
```

`status-reporter.mk` installs the file:

```makefile
STATUS_REPORTER_VERSION = 1.0
STATUS_REPORTER_SITE = $(STATUS_REPORTER_PKGDIR)
STATUS_REPORTER_SITE_METHOD = local

define STATUS_REPORTER_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/status-reporter \
		$(TARGET_DIR)/usr/bin/status-reporter
endef

$(eval $(generic-package))
```

Then add this line to the external tree's `Config.in`:

```text
source "$BR2_EXTERNAL_IOT_PROJECT_PATH/package/status-reporter/Config.in"
```

`external.mk` already finds every `package/*/*.mk`, so it does not need one
line per package. Finally, enable the package in `iot_rpi4_defconfig`:

```text
BR2_PACKAGE_STATUS_REPORTER=y
```

For a library or application with dependencies, put their Buildroot package
names in `STATUS_REPORTER_DEPENDENCIES`. If the new software uses a build
system already supported by Buildroot, use its package infrastructure instead
of writing configure and compiler commands yourself.

If the source is downloaded rather than read from a local project directory,
also add a `.hash` file. It normally records the source archive checksum and
the checksums of the licence files reviewed for that package. Buildroot checks
the downloaded bytes before using them, so an unexpected replacement fails
the build instead of silently producing a different image. The exact format is
described in the [Buildroot `.hash` file guide](https://buildroot.org/downloads/manual/manual.html#adding-packages-hash).

Buildroot includes a style checker for package metadata. Run it against files
you add or change:

```sh
buildroot/utils/check-package -b \
  iot_app/buildroot_external/package/status-reporter/Config.in \
  iot_app/buildroot_external/package/status-reporter/status-reporter.mk
```

The [Buildroot package guide](https://buildroot.org/downloads/manual/manual.html#adding-packages)
and this independent
[`BR2_EXTERNAL` example](https://github.com/pauloserrafh/buildroot-examples)
show further package layouts.

## 11. How the final SD-card image is assembled

The project adds a persistent data partition to Buildroot's normal Raspberry
Pi output.

```text
Raspberry Pi firmware, kernel and device trees
                    |
                    v
                 boot.vfat

Buildroot target filesystem --> rootfs.ext4

post-image.sh --> empty data.ext4 labelled iot-data

boot.vfat + rootfs.ext4 + data.ext4
                    |
                    v
             genimage / sdcard.img
```

The boot and root partitions have fixed image sizes. The data partition begins
at a small bootstrap size so the downloadable image does not contain gigabytes
of empty space. On first boot, `S25data-storage` calls the shared preparation
program, which expands that partition into the unused part of the SD card and
mounts it at `/data`.

The exact partition sizes and first-boot behaviour are documented in the
[storage guide](../storage/README.md). The
[`genimage` configuration documentation](https://github.com/pengutronix/genimage#the-image-config-file)
explains the syntax used by `genimage.cfg.in`.

## 12. Where to make common changes

| Change | Main file or directory | Normal verification |
|---|---|---|
| Change IoT App C++ code | `iot_app/` | `make buildroot-app` |
| Change an IoT App dependency or installed helper | `package/iot_app/iot_app.mk` | Rebuild the package, then the image |
| Add a Buildroot package | `buildroot_external/package/` and `Config.in` | Reload the defconfig and build the image |
| Change package selection | `configs/iot_rpi4_defconfig` | `make buildroot-prepare`, then build the image |
| Change a kernel feature | `linux-iot-app.config` | Rebuild the image |
| Change a BusyBox command | `busybox-iot-app.config` | Rebuild the image |
| Add a static target file | `rootfs-overlay/` | Rebuild the image |
| Change Wi-Fi or SSH build input | Root `wpa_supplicant.conf` or `ssh_authorized_keys` | `make buildroot-prepare`, then build the image |
| Change partition layout | `storage_layout.conf`, `genimage.cfg.in`, or `post-image.sh` | Rebuild and flash the image |
| Change an `S` startup script | Package directory or `rootfs-overlay/etc/init.d` | Rebuild and test boot on the Raspberry Pi |

The detailed commands and rules for incremental builds are in the
[Buildroot image guide](../buildroot/README.md). Do not edit files only under
`/opt/iot-app-builds`; they are generated output.

## 13. Useful inspection commands

These commands help answer questions without modifying the upstream Buildroot
tree:

```sh
# Show the saved project choice after preparation.
grep '^BR2_PACKAGE_IOT_APP=' /opt/iot-app-builds/buildroot-raspberry-pi-4/.config

# See what the package installed into the working root filesystem.
find /opt/iot-app-builds/buildroot-raspberry-pi-4/target \
  -path '*iot-app*' -o -name 'S90iot-app'

# Inspect the startup order that will be present in the image.
find /opt/iot-app-builds/buildroot-raspberry-pi-4/target/etc/init.d \
  -maxdepth 1 -type f -name 'S*' | sort

# Inspect the generated partition configuration and image files.
ls -lh /opt/iot-app-builds/buildroot-raspberry-pi-4/images
```

On the Raspberry Pi:

```sh
ls -1 /etc/init.d/S* | sort
/etc/init.d/iot-app restart
tail -f /var/log/iot_app.log
```

## 14. Further reading

- [Buildroot user manual](https://buildroot.org/downloads/manual/manual.html) -
  the authoritative reference for configuration, packages, external trees,
  filesystem customization, and rebuild behaviour.
- [Buildroot `BR2_EXTERNAL` documentation](https://buildroot.org/downloads/manual/manual.html#outside-br-custom) -
  the structure used by `iot_app/buildroot_external`.
- [Buildroot package instructions](https://buildroot.org/downloads/manual/manual.html#adding-packages) -
  package naming, `Config.in`, `.mk` files, dependencies, and startup scripts.
- [Bootlin Buildroot training slides](https://bootlin.com/doc/training/buildroot/buildroot-slides.pdf)
  and [labs](https://bootlin.com/doc/training/buildroot/buildroot-bbb-labs.pdf) -
  an independent, practical course which follows an embedded board build from
  configuration to a custom package and root filesystem.
- [Buildroot external-package examples](https://github.com/pauloserrafh/buildroot-examples) -
  small examples showing packages kept inside and outside the Buildroot tree.
- [`genimage` README](https://github.com/pengutronix/genimage) - the tool used
  by the project to assemble the final partitioned image.

### More Buildroot examples

These examples use other boards or older Buildroot releases, but their package
and external-tree walkthroughs are still useful. Check current syntax against
the Buildroot manual before copying an example.

- [Bootlin STM32MP1 Buildroot labs](https://bootlin.com/doc/training/buildroot/buildroot-stm32mp1-labs.pdf) -
  another complete board exercise using the same main Buildroot concepts.
- [Bootlin ELCE Buildroot tutorial](https://bootlin.com/pub/conferences/2018/elce/petazzoni-buildroot-tutorial/petazzoni-buildroot-tutorial.pdf) -
  configuration, filesystem customization, packages, and external trees in one
  worked tutorial.
- [DigiKey Buildroot video tutorial](https://www.youtube.com/watch?v=9vsu67uMcko) -
  a visual introduction which follows an image build from start to finish.
- [Mastering Embedded Linux, Buildroot](https://www.thirtythreeforty.net/posts/mastering-embedded-linux-part-3-buildroot/)
  and [customization](https://www.thirtythreeforty.net/posts/mastering-embedded-linux-part-4/) -
  explains why Buildroot is useful and how a basic image grows into a custom
  system.
- [Adding custom packages to Buildroot](https://embeddedinn.com/articles/tutorial/Adding-Custom-Packages-to-Buildroot/) -
  a compact `Config.in` and package `.mk` example.
- [`buildroot_external_example`](https://github.com/moschiel/buildroot_external_example) -
  a small external tree whose complete structure can be inspected easily.
- [Buildroot as a Git submodule](https://github.com/Openwide-Ingenierie/buildroot-submodule) -
  an example of pinning Buildroot while keeping product files outside it.
- [Custom Raspberry Pi Buildroot image](https://siliconwit.com/education/embedded-linux-rpi/buildroot-custom-linux-image/) -
  a Raspberry Pi example which finishes with a flashable image.
- [Bootlin custom PipeWire node](https://bootlin.com/blog/a-custom-pipewire-node/) -
  a real CMake application integrated as a Buildroot package.
- [Bootlin external tree for ST boards](https://github.com/bootlin/buildroot-external-st) -
  a maintained external tree containing board and package customizations.
- [TI Buildroot image guide](https://software-dl.ti.com/processor-sdk-linux-rt/esd/AM62LX/12_00_00_07_04_Buildroot/exports/docs/buildroot/Building_Buildroot_Image.html) -
  a clear example of host setup, out-of-tree output, image building, and
  deployment.
