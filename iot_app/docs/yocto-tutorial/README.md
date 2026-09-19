# Understanding Yocto in IoT App

This tutorial explains the Yocto and BitBake files used by IoT App. It focuses
on how the pieces work together and where a package, service, or image change
belongs. For host setup, build commands, flashing, and troubleshooting, use the
[Yocto image guide](../yocto/README.md).

The project uses the Yocto Project 5.0 `scarthgap` release and builds a
64-bit Raspberry Pi 4 image.

## 1. Yocto, OpenEmbedded, BitBake, and Poky

These names describe different parts of the same build environment:

| Name | Job |
|---|---|
| Yocto Project | The wider project which publishes tools, documentation, reference metadata, and release practices for custom Linux systems |
| OpenEmbedded | The metadata and build framework used to describe software and images |
| BitBake | The program which reads that metadata, makes a task graph, and runs the tasks |
| OpenEmbedded Core, or OE-Core | The central set of classes, recipes, and configuration shared by Yocto builds |
| Poky | A working reference distribution containing BitBake, OE-Core, and example distribution metadata |
| Layer | A directory which groups related recipes and configuration |
| Recipe | A `.bb` file describing how one piece of software or one image is produced |

The [Yocto technical overview](https://www.yoctoproject.org/development/technical-overview/)
explains how Poky, BitBake, recipes, and layers relate. Bootlin's
[Yocto training slides](https://bootlin.com/doc/training/yocto/yocto-slides.pdf)
and [hands-on labs](https://bootlin.com/doc/training/yocto/yocto-bbb-labs.pdf) are
useful practical companions to the official manuals.

## 2. The basic mental model

Yocto is driven by descriptions rather than one top-level list of compiler
commands. A recipe says what source it needs and how to build and install it.
An image recipe says which resulting packages belong in the Linux image.
BitBake connects all of those statements.

```text
Configuration chooses a machine and distribution
                         |
Layers provide recipes and changes to recipes
                         |
                         v
BitBake parses all active metadata
                         |
                         v
BitBake creates a dependency and task graph
                         |
            +------------+-------------+
            |                          |
            v                          v
     Build recipes                Reuse valid sstate
            |                          |
            +------------+-------------+
                         |
                         v
              Create installable packages
                         |
                         v
              Assemble the root filesystem
                         |
                         v
               Create the Wic disk image
```

A recipe and a package are related but are not the same thing:

```text
iot-app_0.1.0.bb          Recipe read by BitBake
        |
        +--> compiles and installs files into a temporary area
        |
        +--> creates one or more .ipk packages
        |
        v
iot-app-image.bb          Selects the iot-app package for the image
```

This distinction matters when adding software. Creating a recipe makes the
software buildable. Adding its output package to `IMAGE_INSTALL` puts it in
the final image.

The official
[Yocto concepts guide](https://docs.yoctoproject.org/scarthgap/overview-manual/concepts.html)
describes recipes, classes, configuration, tasks, packages, images, and shared
state in more detail.

## 3. The layers used by this project

`bblayers.conf` tells BitBake which layers take part in the build. The template
at [`bblayers.conf.sample`](../../../meta-iot-app/conf/templates/raspberrypi4-64/bblayers.conf.sample)
selects these layers:

```text
poky/meta                  OpenEmbedded Core recipes and classes
poky/meta-poky             Poky distribution policy used as a starting point
poky/meta-yocto-bsp        reference board-support metadata

meta-openembedded/meta-oe          additional general recipes
meta-openembedded/meta-python      additional Python recipes
meta-openembedded/meta-networking  additional network recipes

meta-raspberrypi           Raspberry Pi machine, kernel and boot support

meta-iot-app               this project's distribution, recipes, services,
                           package changes, and image definition
```

```text
Upstream and community layers
        |
        +--> provide Linux, systemd, wpa_supplicant, Mosquitto, mpv, etc.
        |
        v
meta-iot-app
        |
        +--> selects and adjusts those parts for this product
        +--> builds the local C++ application
        +--> adds project services and configuration
        |
        v
iot-app-image
```

Keeping project changes in `meta-iot-app` avoids editing Poky,
`meta-openembedded`, or `meta-raspberrypi`. It also makes the difference
between upstream metadata and project policy visible in Git.

The Yocto documentation covers
[creating and enabling layers](https://docs.yoctoproject.org/scarthgap/dev-manual/layers.html).
The independent
[Toradex layer and recipe tutorial](https://developer.toradex.com/linux-bsp/os-development/build-yocto/custom-meta-layers-recipes-and-images-in-yocto-project-hello-world-examples/)
shows the same layer pattern in a complete vendor build.

### 3.1 What layer priority does

[`conf/layer.conf`](../../../meta-iot-app/conf/layer.conf) gives this layer a
priority of `8`. A higher priority matters when two layers provide recipes with
the same name: BitBake prefers the recipe from the higher-priority layer. It
also affects the order in which multiple `.bbappend` files modify one recipe.

Priority is not a general "this layer always wins" rule. It does not decide the
order of every `.conf` or `.bbclass` file, and it does not make an append work
unless its filename matches an active recipe. These commands show the result
BitBake is actually using:

```sh
bitbake-layers show-layers
bitbake-layers show-overlayed
bitbake-layers show-appends
```

The Yocto layer guide gives the exact rules in
[Prioritizing Your Layer](https://docs.yoctoproject.org/scarthgap/dev-manual/layers.html#prioritizing-your-layer).

## 4. Common Yocto file types

The filename suffix usually tells you what kind of metadata you are reading.

| Suffix or name | Meaning | Example in this project |
|---|---|---|
| `.conf` | Configuration values for a layer, distribution, machine, or build | `layer.conf`, `iot-app-linux.conf`, `local.conf` |
| `.bb` | A recipe for software, configuration, or an image | `iot-app_0.1.0.bb`, `iot-app-image.bb` |
| `.bbappend` | Extra instructions applied to an existing recipe with the matching name | `mosquitto_%.bbappend` |
| `.bbclass` | Reusable task logic shared by recipes | Upstream `cmake`, `systemd`, `useradd`, and `core-image` classes |
| `.service` | A systemd unit installed by a recipe; it is target content, not BitBake syntax | `iot-app.service` |
| `.network` | A systemd-networkd interface configuration installed in the image | `10-eth0.network` |
| `.rules` | A udev rule installed in the image | `70-iot-app-access.rules` |
| `.wks` | A Wic disk-partition description | Generated `iot-app-raspberrypi.wks` |
| `files/` | A conventional recipe directory for local files named by `file://` entries | Scripts, units, and network files |

### 4.1 Recipe names

This filename:

```text
iot-app_0.1.0.bb
   |       |
   |       +--> recipe version
   +----------> recipe name
```

sets the normal package name `${PN}` to `iot-app` and the version `${PV}` to
`0.1.0`.

This append filename:

```text
mosquitto_%.bbappend
```

means “apply these changes to any selected Mosquitto recipe version.” Replacing
`%` with an exact version restricts the append to that version. BitBake reports
an error for an append which matches no active recipe, which helps catch stale
customizations after an upgrade.

## 5. How BitBake reads `meta-iot-app`

[`conf/layer.conf`](../../../meta-iot-app/conf/layer.conf) is the entry point for
the layer.

```text
bblayers.conf includes meta-iot-app
                |
                v
BitBake reads meta-iot-app/conf/layer.conf
                |
                +--> BBPATH makes layer configuration discoverable
                +--> BBFILES finds recipes-*/*/*.bb
                +--> BBFILES finds recipes-*/*/*.bbappend
                +--> layer priority and scarthgap compatibility are declared
                |
                v
BitBake parses the matching recipes and appends
```

The layer also defines `IOT_APP_PROJECT_ROOT`, which points to the repository
root. Recipes use it to reach the checked-out application, LVGL, MicroPython,
the licence, and files shared with Buildroot.

## 6. BitBake variables and syntax used here

BitBake syntax looks similar to shell or Make syntax, but it has its own rules.
The complete rules are in the
[BitBake syntax manual](https://docs.yoctoproject.org/bitbake/2.8/bitbake-user-manual/bitbake-user-manual-metadata.html).

### 6.1 Assigning and changing values

| Form | Meaning |
|---|---|
| `A = "value"` | Set a value which is expanded when it is used |
| `A := "value"` | Set a value and expand it immediately |
| `A ?= "value"` | Set a default only if no value has already been assigned |
| `A ??= "value"` | Set a weak default which a normal assignment can replace |
| `A += "item"` | Add an item now and insert a separating space |
| `A:append = " item"` | Add text when overrides are applied; spacing must be written explicitly |
| `A:prepend = "item "` | Add text at the beginning when overrides are applied |
| `A:remove = "item"` | Remove matching items when the value is expanded |

For example:

```text
IMAGE_INSTALL:append = " iot-app"
```

adds the `iot-app` output package to whatever packages the image already
contains. The leading space is intentional.

### 6.2 Package overrides

This statement:

```text
RDEPENDS:${PN} += "mpv"
```

means “add `mpv` to the runtime dependencies of the main output package from
this recipe.” `${PN}` expands to the recipe's main package name.

The older `_append` and `_class-target` style can still appear in old online
tutorials. This project uses the current colon form, such as `:append` and
`:${PN}`.

### 6.3 Four kinds of feature settings

Several Yocto variables contain lists of "features", but they work at different
levels:

| Variable | Question it answers | Example here |
|---|---|---|
| `MACHINE_FEATURES` | What hardware can this machine support? | Raspberry Pi capabilities come mainly from `meta-raspberrypi` |
| `DISTRO_FEATURES` | What should this Linux distribution support across its images? | `iot-app-linux.conf` removes X11, Wayland, audio, and other unused platform features |
| `IMAGE_FEATURES` | What convenience or image-level behaviour should this particular image contain? | `iot-app-image.bb` enables OpenSSH and root login |
| `PACKAGECONFIG` | Which optional parts of one recipe should be compiled? | The mpv append enables DRM, GBM, EGL, and OpenGL; the Mosquitto append removes WebSocket support |

`PACKAGECONFIG` feature names are defined by the original recipe. Enabling one
can add configure options and build or runtime dependencies; removing one can
add the matching disable option. Inspect the original recipe or its expanded
value before choosing names:

```sh
bitbake-getvar -r mpv PACKAGECONFIG
```

The mpv append uses `PACKAGECONFIG = "drm gbm egl opengl"`, so it deliberately
chooses the graphics features needed for direct video output in this image.
The Mosquitto append uses `PACKAGECONFIG:remove = "websockets"`, so all of
Mosquitto's other upstream choices remain untouched.

The release-matched references for these settings are the
[Yocto feature list](https://docs.yoctoproject.org/scarthgap/ref-manual/features.html)
and the [`PACKAGECONFIG` variable description](https://docs.yoctoproject.org/scarthgap/ref-manual/variables.html#term-PACKAGECONFIG).

### 6.4 Paths seen in recipes

| Variable | Meaning while a recipe is built |
|---|---|
| `${WORKDIR}` | Private working directory for this recipe and version |
| `${S}` | Source directory used by the recipe |
| `${B}` | Directory where compilation writes generated files and objects |
| `${D}` | Temporary directory representing the files this recipe wants to install into `/` |
| `${bindir}` | Target executable directory, normally `/usr/bin` |
| `${libexecdir}` | Target helper-program directory, normally `/usr/libexec` |
| `${sysconfdir}` | Target system configuration directory, normally `/etc` |
| `${systemd_system_unitdir}` | Target directory for packaged systemd units |
| `${TOPDIR}` | Current Yocto build directory |
| `${LAYERDIR}` | Directory containing the current `layer.conf` |

`${D}` is easy to misunderstand. During `do_install`, this command:

```sh
install -D -m 0755 "${WORKDIR}/my-helper" "${D}${libexecdir}/my-helper"
```

does not write to the Ubuntu host's `/usr/libexec`. It writes to a temporary
package area. BitBake later packages that file and places it in `/usr/libexec`
when building the target root filesystem.

## 7. The normal recipe task flow

Classes provide standard implementations for many tasks. A typical source
recipe follows this path:

```text
do_fetch
   |
   v
do_unpack
   |
   v
do_patch
   |
   v
do_configure
   |
   v
do_compile
   |
   v
do_install             files are copied into ${D}
   |
   v
do_package             installed files are split into packages
   |
   v
do_package_write_ipk   .ipk files are created
   |
   v
image do_rootfs        selected packages are installed into the image rootfs
   |
   v
do_image_wic           bootable partitions are assembled
```

`DEPENDS` adds build-time dependencies whose headers, libraries, or tools must
be ready before the recipe builds. `RDEPENDS:${PN}` adds packages which must be
installed when the resulting target package is installed.

BitBake calculates a signature from a task's inputs. If the signature and a
matching shared-state result are still valid, it can restore that result from
`SSTATE_DIR` instead of repeating the task. This is why later Yocto builds are
normally much faster than the first one. The
[Yocto shared-state explanation](https://docs.yoctoproject.org/scarthgap/overview-manual/concepts.html#shared-state-cache)
covers this process.

### 7.1 Why each recipe has its own sysroot

A recipe does not compile against every library which happens to have been
built earlier. Before configuration, BitBake prepares two private views of its
declared build dependencies:

```text
Dependencies built for the Raspberry Pi
        |
        v
${WORKDIR}/recipe-sysroot
        headers and AArch64 libraries used by the cross-compiler

Dependencies built for Ubuntu
        |
        v
${WORKDIR}/recipe-sysroot-native
        tools which run on the build computer
```

This isolation makes missing `DEPENDS` entries visible. For example, if the
IoT App recipe forgot `libdrm` in `DEPENDS`, its compiler should not find DRM
headers merely because another unrelated recipe had already built libdrm.
`RDEPENDS:${PN}` is different: it controls packages required on the Raspberry
Pi at runtime.

The Yocto concepts guide describes both directories in
[Configuration, Compilation, and Staging](https://docs.yoctoproject.org/scarthgap/overview-manual/concepts.html#configuration-compilation-and-staging).

## 8. The complete `meta-iot-app` map

The following sections explain every project-owned Yocto file by group.

```text
meta-iot-app/
├── README.md
├── conf/
│   ├── layer.conf
│   ├── distro/iot-app-linux.conf
│   └── templates/raspberrypi4-64/
│       ├── bblayers.conf.sample
│       ├── local.conf.sample
│       └── conf-notes.txt
├── recipes-connectivity/
│   ├── mosquitto/mosquitto_%.bbappend
│   └── wpa-supplicant/wpa-supplicant_%.bbappend
├── recipes-core/
│   ├── images/iot-app-image.bb
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
├── recipes-iot/iot-app/
│   ├── iot-app_0.1.0.bb
│   └── files/
│       ├── 70-iot-app-access.rules
│       └── iot-app.service
├── recipes-multimedia/mpv/mpv_0.35.1.bbappend
└── wic/iot-app-raspberrypi.wks.in
```

### 8.1 Layer and distribution configuration

| File | Purpose |
|---|---|
| [`README.md`](../../../meta-iot-app/README.md) | Short entry point for the layer |
| [`conf/layer.conf`](../../../meta-iot-app/conf/layer.conf) | Registers the layer's recipes and appends, declares `scarthgap` compatibility, and finds the repository root |
| [`conf/distro/iot-app-linux.conf`](../../../meta-iot-app/conf/distro/iot-app-linux.conf) | Defines the `iot-app-linux` distribution, selects systemd and IPK packages, and removes unused distribution features |

The distribution starts with `poky.conf` and then states product-wide policy.
It removes X11, Wayland, audio, Bluetooth, and other unused features, while
keeping OpenGL and zeroconf because video playback and mDNS need them.

### 8.2 Build-configuration template

| File | Purpose |
|---|---|
| [`bblayers.conf.sample`](../../../meta-iot-app/conf/templates/raspberrypi4-64/bblayers.conf.sample) | Lists the layers which must be parsed |
| [`local.conf.sample`](../../../meta-iot-app/conf/templates/raspberrypi4-64/local.conf.sample) | Selects the Raspberry Pi machine, project distribution, Wic output, hardware features, kernel command line, timezone, and hostname |
| [`conf-notes.txt`](../../../meta-iot-app/conf/templates/raspberrypi4-64/conf-notes.txt) | Short notes printed after the build environment is initialized |

[`scripts/build/prepare-yocto.sh`](../../../scripts/build/prepare-yocto.sh)
uses this template to create the real build configuration under
`/opt/iot-app-builds/yocto-raspberry-pi-4/build/conf`. It also writes the
download and shared-state paths, copies private Wi-Fi and SSH inputs, and
creates the final Wic file from the storage template.

### 8.3 Image recipe

[`iot-app-image.bb`](../../../meta-iot-app/recipes-core/images/iot-app-image.bb)
describes the root filesystem as a whole. It:

- starts from `core-image` and the minimal boot package group;
- enables OpenSSH and development root login;
- installs IoT App, its system configuration, hardware tools, firmware,
  Mosquitto, Wi-Fi support, storage tools, certificates, and timezone data;
- removes recommended wpa_supplicant command-line extras which the image does
  not use;
- sets the development root password through `extrausers`;
- reserves free space inside the root filesystem; and
- restricts the image to the `raspberrypi4-64` machine.

```text
iot-app-image.bb
        |
        +--> IMAGE_FEATURES chooses image-level features
        +--> IMAGE_INSTALL chooses target packages
        +--> extrausers sets the development root password
        |
        v
Complete target root filesystem
```

### 8.4 Main application recipe

[`iot-app_0.1.0.bb`](../../../meta-iot-app/recipes-iot/iot-app/iot-app_0.1.0.bb)
builds and packages the C++ application.

It inherits these classes:

| Class | Why it is used |
|---|---|
| `cmake` | Runs the normal CMake configure, compile, and install tasks with Yocto's cross-toolchain |
| `pkgconfig` | Makes target library metadata available to CMake checks |
| `systemd` | Packages and enables `iot-app.service` |
| `useradd` | Creates the unprivileged `iot-app` user and hardware-access groups as part of this package |
| `externalsrc` | Builds the repository checkout directly instead of fetching a separate source archive |

`EXTERNALSRC` points to the repository and `OECMAKE_SOURCEPATH` selects its
`iot_app` subdirectory. With `externalsrc`, the normal fetch, unpack, and patch
stages are skipped for this recipe. The official
[`externalsrc` documentation](https://docs.yoctoproject.org/scarthgap/ref-manual/classes.html#externalsrc)
explains this development workflow.

The recipe's `do_install:append()` adds files not installed by CMake:

```text
Shared image_support files
        +--> launcher and console helpers
        +--> video check command

Yocto-specific recipe files
        +--> iot-app.service
        +--> 70-iot-app-access.rules
```

The `FILES:${PN}` list tells packaging that these paths belong to the main
`iot-app` output package.

The two files kept beside the recipe are specific to this Yocto image:

| File | Purpose |
|---|---|
| `iot-app.service` | Starts the application through the shared launcher, waits for the framebuffer device, and restarts an unexpected failure |
| `70-iot-app-access.rules` | Gives the `video`, `render`, `i2c`, and `input` groups access to the hardware device files used by the application |

Yocto uses udev to apply `70-iot-app-access.rules`. This serves the same purpose
as `mdev.conf` in the Buildroot image, but the file syntax differs because the
device managers differ.

### 8.5 Licence checksums

Both project recipes set `LIC_FILES_CHKSUM`. Yocto requires recipes to state
their licence and to identify the text which was reviewed:

```text
LIC_FILES_CHKSUM = "file://LICENSE;md5=..."
```

The MD5 value is a change detector, not a security check. If the licence text
changes, BitBake stops and asks the developer to review it. After an intentional
change, calculate the new value from the repository root:

```sh
md5sum LICENSE
```

Then replace the old value in both project recipes. Do not update the number
before reading the licence change. Yocto explains the reason for this field in
[tracking licence changes](https://docs.yoctoproject.org/scarthgap/dev-manual/licenses.html#tracking-license-changes).

### 8.6 Device configuration recipe

[`iot-app-system-config_1.0.bb`](../../../meta-iot-app/recipes-core/iot-app-system-config/iot-app-system-config_1.0.bb)
installs device-wide configuration which does not belong to the C++ program:

- Ethernet and Wi-Fi systemd-networkd files;
- the Wi-Fi, optional storage, and mDNS refresh services;
- the project Mosquitto unit;
- optional root SSH public keys;
- the `tty1` login mask and `tty2` recovery login; and
- links which enable networkd, resolved, and timesyncd.

Its `files/` directory contains:

| File | Installed role |
|---|---|
| `10-eth0.network` | Requests DHCP configuration for Ethernet |
| `20-wlan0.network` | Requests DHCP configuration for Wi-Fi |
| `iot-app-wifi.service` | Runs wpa_supplicant for `wlan0` |
| `iot-app-storage.service` | Calls the shared optional `/data` preparation program once during boot |
| `iot-app-refresh-mdns-hostname` | Waits for an address and restarts Avahi if its advertised name needs refreshing |
| `iot-app-refresh-mdns-hostname.service` | Runs the mDNS refresh helper under systemd |
| `mosquitto.service` | Starts the local broker without waiting for an external network address |

The recipe depends at runtime on the programs used by those services. For
example, a shell script which calls `parted` needs the target `parted` package
in `RDEPENDS`, even though nothing links to it during compilation.

### 8.7 Changes to recipes from other layers

Three `.bbappend` files adjust upstream recipes without copying them into this
layer:

| Append | Change made for IoT App |
|---|---|
| [`mosquitto_%.bbappend`](../../../meta-iot-app/recipes-connectivity/mosquitto/mosquitto_%.bbappend) | Installs the shared broker configuration and removes unused WebSocket support |
| [`wpa-supplicant_%.bbappend`](../../../meta-iot-app/recipes-connectivity/wpa-supplicant/wpa-supplicant_%.bbappend) | Installs the private Wi-Fi configuration copied into the build directory by `make yocto-prepare` |
| [`mpv_0.35.1.bbappend`](../../../meta-iot-app/recipes-multimedia/mpv/mpv_0.35.1.bbappend) | Selects DRM/EGL/GBM/OpenGL support and enables the shared `libmpv` library needed by IoT App |

The first two use this common flow:

```text
Project file
     |
     | FILESEXTRAPATHS lets BitBake find it
     v
SRC_URI adds file://name as a recipe input
     |
     v
do_install:append copies it below ${D}
     |
     v
The upstream package contains the project file
```

The Yocto manual explains
[how a layer extends another layer with `.bbappend`](https://docs.yoctoproject.org/scarthgap/dev-manual/layers.html#appending-other-layers-metadata-with-your-layer).

The mpv append names version 0.35.1 instead of using `%`. That is the mpv
version in the checked-out Scarthgap recipe, and it uses Waf options such as
`--enable-libmpv-shared`. A later recipe may use different option names. Its
exact filename makes BitBake stop applying the append after an mpv upgrade so
the project settings can be reviewed first.

### 8.8 Disk layout

[`iot-app-raspberrypi.wks.in`](../../../meta-iot-app/wic/iot-app-raspberrypi.wks.in)
is the project's Wic template. Preparation replaces its root and data size
placeholders and saves the generated `.wks` file in the build configuration.

```text
iot-app-raspberrypi.wks.in + storage_layout.conf
                         |
                         | make yocto-prepare
                         v
build/conf/iot-app-raspberrypi.wks
                         |
                         | BitBake image tasks and Wic
                         v
boot partition + root partition + initial iot-data partition
                         |
                         v
compressed .wic.xz image
```

The
[Yocto Wic guide](https://docs.yoctoproject.org/scarthgap/dev-manual/wic.html)
explains how `.wks` files turn build artifacts into partitioned images. The
[Wic `part` reference](https://docs.yoctoproject.org/scarthgap/ref-manual/kickstart.html#command-part-or-partition)
documents the options used in the template.

## 9. How services start in the Yocto image

This image uses systemd. A `.service` file describes a service, its program,
its ordering, and how failures should be handled. A recipe must also install
the file and enable it.

The IoT App service follows this path:

```text
iot-app.service in meta-iot-app
            |
            | SRC_URI and do_install:append
            v
${D}/usr/lib/systemd/system/iot-app.service
            |
            | do_package
            v
iot-app output package
            |
            | IMAGE_INSTALL in iot-app-image.bb
            v
Raspberry Pi root filesystem
            |
            | SYSTEMD_AUTO_ENABLE = "enable"
            v
multi-user.target starts iot-app.service during boot
```

The main service relationships are:

```text
multi-user.target
   |
   +--> iot-app-wifi.service --> wpa_supplicant
   |
   +--> iot-app-storage.service --> prepare optional /data
   |
   +--> mosquitto.service --> local MQTT broker
   |
   +--> iot-app-refresh-mdns-hostname.service --> Avahi name refresh
   |
   +--> iot-app.service
           |
           +-- Requires --> /dev/fb0 must exist
           +-- Wants ----> storage and Mosquitto are requested but optional
           +-- ExecStart -> shared iot-app-launcher
```

Unlike Buildroot's numbered startup scripts, systemd does not simply run every
service one at a time. Units without an ordering relationship can start in
parallel. In a unit file:

- `After=` controls ordering but does not pull another unit into the boot;
- `Wants=` requests another unit but does not fail this unit when the wanted
  unit fails;
- `Requires=` creates a stronger requirement; and
- `WantedBy=multi-user.target` gives the `systemd` class a target under which
  the service can be enabled.

The
[Yocto `systemd` class](https://docs.yoctoproject.org/scarthgap/ref-manual/classes.html#systemd)
handles service enablement for recipes. Unit dependency behaviour is defined
by the
[systemd unit documentation](https://www.freedesktop.org/software/systemd/man/latest/systemd.unit.html).

## 10. Adding a new Yocto service

Choose the recipe based on ownership:

| New service | Best home |
|---|---|
| Runs the IoT App executable or a helper shipped with it | `iot-app_0.1.0.bb` and its `files/` directory |
| Configures the complete device, network, console, or storage | `iot-app-system-config_1.0.bb` and its `files/` directory |
| Belongs to a separate program with its own version and dependencies | A new recipe |

### 10.1 Add a device-wide service to the existing configuration recipe

Suppose `iot-app-health-check.service` runs a new helper during boot.

1. Add both files under
   `meta-iot-app/recipes-core/iot-app-system-config/files/`.
2. Add them to the recipe's `SRC_URI`:

```text
SRC_URI = " \
    ... \
    file://iot-app-health-check \
    file://iot-app-health-check.service \
"
```

3. Add the unit to the enabled service list:

```text
SYSTEMD_SERVICE:${PN} = "... iot-app-health-check.service"
```

4. Install the helper and unit during `do_install()`:

```sh
install -D -m 0755 "${WORKDIR}/iot-app-health-check" \
    "${D}${libexecdir}/iot-app-health-check"
install -D -m 0644 "${WORKDIR}/iot-app-health-check.service" \
    "${D}${systemd_system_unitdir}/iot-app-health-check.service"
```

5. Add their installed paths to `FILES:${PN}` if the existing directory
   patterns do not already include them.
6. Add target commands used by the helper to `RDEPENDS:${PN}`.

A small oneshot unit could be:

```ini
[Unit]
Description=Check IoT App device health
After=local-fs.target

[Service]
Type=oneshot
ExecStart=/usr/libexec/iot-app-health-check

[Install]
WantedBy=multi-user.target
```

Use `Type=simple` for a program which stays running. Do not put a background
`&` in `ExecStart`; systemd should track the foreground process directly.

### 10.2 Add a service with a new recipe

Use a separate recipe when the program has its own source, version, build
dependencies, or release cycle:

```text
meta-iot-app/recipes-support/status-reporter/
├── status-reporter_1.0.bb
└── files/
    ├── status-reporter
    └── status-reporter.service
```

A minimal project-local recipe could contain:

```text
SUMMARY = "IoT App status reporter"
LICENSE = "PolyForm-Noncommercial-1.0.0"
LIC_FILES_CHKSUM = "file://${IOT_APP_PROJECT_ROOT}/LICENSE;md5=b2a551156d047ff7f73d0c43858d552a"

inherit systemd

SRC_URI = "file://status-reporter file://status-reporter.service"

SYSTEMD_SERVICE:${PN} = "status-reporter.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

do_install() {
    install -D -m 0755 "${WORKDIR}/status-reporter" \
        "${D}${bindir}/status-reporter"
    install -D -m 0644 "${WORKDIR}/status-reporter.service" \
        "${D}${systemd_system_unitdir}/status-reporter.service"
}
```

Finally, add the output package to the image:

```text
IMAGE_INSTALL:append = " status-reporter"
```

Yocto's development manual has a full section on
[enabling system services from recipes](https://docs.yoctoproject.org/scarthgap/dev-manual/new-recipe.html#enabling-system-services).
The practical
[Toradex systemd service example](https://community.toradex.com/t/how-to-create-custom-folders-and-run-an-executable-on-boot-in-a-yocto-built-image-verdin-imx8m-plus/28653)
shows the same recipe-to-image path on an embedded target.

## 11. Adding another software package

Before writing a recipe, check whether an active layer already provides one:

```sh
source poky/oe-init-build-env /opt/iot-app-builds/yocto-raspberry-pi-4/build
bitbake-layers show-recipes | grep -i package-name
```

If a recipe exists, add its output package to `IMAGE_INSTALL`. If it needs a
small IoT App-specific change, add a `.bbappend` in `meta-iot-app`. Write a new
recipe only when no suitable one exists.

A source-building recipe normally provides:

```text
SUMMARY and DESCRIPTION       what the software is
LICENSE and LIC_FILES_CHKSUM  licence and tracked licence text
SRC_URI                       source archive, Git repository, patches, or files
DEPENDS                       build-time dependencies
inherit ...                   shared build logic such as cmake or meson
S and B                       source and build directories when defaults differ
do_install                    files which become output packages
FILES                         package ownership when defaults are not enough
RDEPENDS                      target runtime dependencies
```

For example, a CMake program can inherit `cmake`; it should not manually call
the cross-compiler. A program from Git should pin `SRCREV` so another build
uses the same source. Every tracked licence file needs a reviewed checksum.

The official [new recipe guide](https://docs.yoctoproject.org/scarthgap/dev-manual/new-recipe.html)
covers fetching, licensing, dependencies, compiling, installing, packaging,
and testing. The Toradex tutorial linked earlier includes a small `hello-world`
recipe and shows how its package is added to an image.

## 12. Extending an existing recipe with `.bbappend`

Use an append when upstream already owns the main recipe and this layer only
needs to adjust it.

For example, this pattern adds a local configuration file:

```text
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI += "file://my-program.conf"

do_install:append() {
    install -D -m 0644 "${WORKDIR}/my-program.conf" \
        "${D}${sysconfdir}/my-program.conf"
}
```

The flow is:

```text
Original recipe from another layer
                |
                +--> normal source, tasks and packages
                |
meta-iot-app matching .bbappend
                +--> adds or changes selected metadata and task steps
                |
                v
One combined recipe seen by BitBake
```

Do not copy the complete upstream recipe just to change one option. A short
append makes future upstream changes easier to see. After changing an append,
verify which recipe it matched:

```sh
bitbake-layers show-appends
bitbake -e mosquitto | less
```

## 13. Configuration: layer, distribution, machine, image, or local build?

Several files can set the same variable, but each level has a different
meaning.

| Scope | Put the setting here when... | IoT App example |
|---|---|---|
| Layer | It controls how BitBake finds and combines this layer | `conf/layer.conf` |
| Distribution | It is product-wide Linux policy independent of one board | systemd and removed desktop/audio features in `iot-app-linux.conf` |
| Machine/BSP | It describes Raspberry Pi hardware and boot support | `raspberrypi4-64` from `meta-raspberrypi` |
| Image recipe | It chooses packages and image-level features | `iot-app-image.bb` |
| Recipe | It belongs to one program or configuration package | `iot-app_0.1.0.bb` |
| `local.conf` | It is specific to one build directory or developer | persistent cache paths generated during preparation |

The project template contains repeatable product settings even though the
generated file is named `local.conf`. Host-specific absolute output and cache
paths are placed in a separate generated `iot-app-build-paths.conf`.

## 14. How preparation and output directories work

The root Makefile gives developers stable commands while the preparation
script creates Yocto's normal build environment.

```text
make yocto-prepare
        |
        +--> check Poky and layer submodules
        +--> create persistent /opt directories
        +--> initialize the build from meta-iot-app's template
        +--> regenerate local.conf
        +--> set DL_DIR and SSTATE_DIR
        +--> copy private Wi-Fi and optional SSH key files
        +--> generate the .wks partition file
```

The important persistent paths are:

```text
/opt/iot-app-builds/
├── yocto-raspberry-pi-4/build/   generated configuration and tmp work
├── yocto-downloads/              downloaded source archives and repositories
├── yocto-sstate-cache/           reusable completed task results
└── images/                       convenient final image copies
```

BitBake's normal deploy result is below the build directory at
`tmp/deploy/images/raspberrypi4-64/`. The root Makefile copies the selected
compressed image to `/opt/iot-app-builds/images/iot-app-yocto-rpi4.img.xz`.

The official
[Yocto building guide](https://docs.yoctoproject.org/scarthgap/dev-manual/building.html)
describes build directory initialization and `tmp/deploy/images`.

## 15. Where to make common changes

| Change | Main file or directory | Normal verification |
|---|---|---|
| Change IoT App C++ code | `iot_app/` | `make yocto-app` |
| Change IoT App build dependencies or installed helpers | `iot-app_0.1.0.bb` | Build the app recipe, then the image |
| Add a package to the target | `iot-app-image.bb` | Build the image |
| Remove a feature across the distribution | `iot-app-linux.conf` | Parse-check, then build the image |
| Change Raspberry Pi boot settings | `local.conf.sample` or the relevant BSP append | `make yocto-prepare`, then build the image |
| Change Wi-Fi or SSH build input | Root `wpa_supplicant.conf` or `ssh_authorized_keys` | `make yocto-prepare`, then build the image |
| Add device-wide configuration | `iot-app-system-config_1.0.bb` and `files/` | Build the recipe, then the image |
| Adjust an upstream package | A matching `.bbappend` | `bitbake-layers show-appends`, then build the affected recipe |
| Change partitions | `storage_layout.conf` or the Wic template | `make yocto-prepare`, then build and flash the image |
| Add a service | The recipe which owns it plus a `.service` file | Build the recipe and test a complete boot |

The exact project commands and safe incremental deployment steps remain in the
[Yocto image guide](../yocto/README.md).

## 16. Useful commands for understanding a build

Run these after entering the Yocto environment:

```sh
source poky/oe-init-build-env /opt/iot-app-builds/yocto-raspberry-pi-4/build
```

```sh
# Show every active layer and its priority.
bitbake-layers show-layers

# Find which layer provides a recipe.
bitbake-layers show-recipes iot-app

# Show every .bbappend and the recipe it changes.
bitbake-layers show-appends

# Show recipes hidden by recipes with the same name in higher-priority layers.
bitbake-layers show-overlayed

# Print the final expanded value of one variable.
bitbake-getvar -r iot-app DEPENDS

# Show the available tasks for one recipe.
bitbake -c listtasks iot-app

# Open a shell with the recipe's cross-build environment.
bitbake -c devshell iot-app

# Find the output package which owns a path in the target filesystem.
oe-pkgdata-util find-path /usr/bin/iot_app

# Write recipe and task dependency graphs into the current directory.
bitbake -g iot-app
```

`bitbake -g` writes `pn-buildlist` and `task-depends.dot`. Complete image graphs
can be very large, so start with one recipe and read the text files before
trying to render the graph.

The official debugging guide explains the generated files and their limits in
[Viewing Dependencies Between Recipes and Tasks](https://docs.yoctoproject.org/scarthgap/dev-manual/debugging.html#viewing-dependencies-between-recipes-and-tasks).

On an older BitBake version without `bitbake-getvar`, the equivalent expanded
metadata can be searched with:

```sh
bitbake -e iot-app | grep '^DEPENDS='
```

Useful generated locations include:

```text
tmp/work/.../iot-app/0.1.0/        recipe work, logs, build output, and ${D}
tmp/deploy/ipk/                    generated IPK packages
tmp/deploy/images/raspberrypi4-64/ boot and disk images
```

When a task fails, BitBake prints the exact `log.do_taskname` path. Read that
file before cleaning the recipe; it normally contains the first useful compiler
or install error above the final task failure.

After a successful image build, the image manifest is a quick way to confirm
that an output package was included. It is stored beside the Wic image under
`tmp/deploy/images/raspberrypi4-64/`. The recipe's `packages-split/`
directories show which files were assigned to each output package.

Avoid starting troubleshooting with `cleanall`. BitBake normally detects
metadata and source changes itself. `clean` removes built work for a recipe,
`cleansstate` also removes its reusable shared-state result, and `cleanall`
additionally removes downloaded source. Read the failed task log first and use
the narrowest cleanup which addresses the problem.

The exact scope and cautions for these commands are in the
[Yocto task reference](https://docs.yoctoproject.org/scarthgap/ref-manual/tasks.html#do-cleanall).

## 17. Further reading

- [Yocto Project 5.0 documentation](https://docs.yoctoproject.org/scarthgap/) -
  the release-matched starting point for this repository.
- [Yocto concepts](https://docs.yoctoproject.org/scarthgap/overview-manual/concepts.html) -
  BitBake, metadata, recipes, tasks, packages, images, dependencies, and sstate.
- [Yocto development tasks](https://docs.yoctoproject.org/scarthgap/dev-manual/index.html) -
  practical official instructions for layers, image changes, recipes, services,
  and upgrades.
- [BitBake 2.8 manual](https://docs.yoctoproject.org/bitbake/2.8/) - parser,
  task execution, operators, overrides, and command details for the BitBake
  series used by `scarthgap`.
- [Bootlin Yocto training slides](https://bootlin.com/doc/training/yocto/yocto-slides.pdf)
  and [labs](https://bootlin.com/doc/training/yocto/yocto-bbb-labs.pdf) - an
  independent course which builds up the ideas through exercises.
- [Toradex custom layers, recipes, and images](https://developer.toradex.com/linux-bsp/os-development/build-yocto/custom-meta-layers-recipes-and-images-in-yocto-project-hello-world-examples/) -
  a board-vendor tutorial with complete layer and recipe examples.
- [Hands-on introduction to BitBake](https://kobimedrish.com/posts/hands_on_introduction_to_bitbake/) -
  an independent explanation of parsing, recipes, tasks, and the dependency
  graph.
- [OpenEmbedded Layer Index](https://layers.openembedded.org/) - search for
  existing layers and recipes before adding another copy to this project.
- [systemd unit manual](https://www.freedesktop.org/software/systemd/man/latest/systemd.unit.html) -
  the dependency and installation rules used by the project's service files.

### Focused Yocto references

- [Yocto technical overview](https://www.yoctoproject.org/development/technical-overview/) -
  how Yocto Project, OpenEmbedded, BitBake, OE-Core, Poky, layers, and recipes
  fit together.
- [Understanding and creating layers](https://docs.yoctoproject.org/scarthgap/dev-manual/layers.html) -
  `layer.conf`, priorities, recipe appends, and maintaining a layer.
- [Creating a new recipe](https://docs.yoctoproject.org/scarthgap/dev-manual/new-recipe.html) -
  source retrieval, licences, tasks, dependencies, installation, and packaging.
- [Building a simple image](https://docs.yoctoproject.org/scarthgap/dev-manual/building.html) -
  the build directory, image target, and generated output.
- [Creating partitioned images with Wic](https://docs.yoctoproject.org/scarthgap/dev-manual/wic.html) -
  the `.wks` format used to describe disk partitions.
- [BitBake syntax and metadata](https://docs.yoctoproject.org/bitbake/2.8/bitbake-user-manual/bitbake-user-manual-metadata.html) -
  assignments, overrides, tasks, dependencies, and variable expansion.
- [Yocto debugging tools](https://docs.yoctoproject.org/scarthgap/dev-manual/debugging.html) -
  task logs, package-data queries, dependency graphs, and signature checks.
- [Yocto task reference](https://docs.yoctoproject.org/scarthgap/ref-manual/tasks.html) -
  task definitions and the differences between `clean`, `cleansstate`, and
  `cleanall`.
- [Yocto feature reference](https://docs.yoctoproject.org/scarthgap/ref-manual/features.html) -
  machine, distribution, and image features.
- [`PACKAGECONFIG` reference](https://docs.yoctoproject.org/scarthgap/ref-manual/variables.html#term-PACKAGECONFIG) -
  selecting optional features and dependencies for one recipe.

### More Yocto examples

The following tutorials use different boards and release versions. They are
useful worked examples, but use the `scarthgap` manuals above when their syntax
differs from this repository.

- [Bootlin STM32MP1 Yocto labs](https://bootlin.com/doc/training/yocto/yocto-stm32mp1-labs.pdf)
  and [BeaglePlay labs](https://bootlin.com/doc/training/yocto/yocto-beagleplay-labs.pdf) -
  complete exercises covering layers, recipes, images, and vendor BSPs.
- [Mender systemd-service recipe](https://hub.mender.io/t/how-to-create-your-first-recipe-and-enable-auto-start-using-systemd/1195) -
  a small recipe which installs and enables a service.
- [PHYTEC custom-layer tutorial](https://phytec.github.io/kivy-demo/tutorials/creating-my-layer.html)
  and [BSP guide](https://phytec.github.io/doc-bsp-yocto/yocto/master.html) -
  layer creation followed by broader image and BSP customization.
- [Embedded Artists Yocto customization](https://developer.embeddedartists.com/docs-app/yocto/customization/) -
  image additions, recipes, and custom layers on a maintained BSP.
- [Leon Anavi Raspberry Pi layer example](https://anavi.org/article/308/) -
  a Raspberry Pi custom layer with a recipe and append file.
- [Creating a custom Yocto layer](https://www.smallstepsystems.com/creating-a-custom-yocto-layer/) -
  a short layer-creation walkthrough.
- [`ready-set-yocto`](https://github.com/jynik/ready-set-yocto) -
  an example of a reproducible workspace with pinned layer revisions.
- [Working with Yocto Project](https://www.embeddedartists.com/wp-content/uploads/2018/04/iMX_Working_with_Yocto.pdf) -
  a broad guide to recipes, images, SDKs, and vendor BSPs. It predates the
  current colon override syntax.
- [Yocto production images for Raspberry Pi](https://siliconwit.com/education/embedded-linux-rpi/yocto-production-images/) -
  a Raspberry Pi example focused on explicit production-image choices.
