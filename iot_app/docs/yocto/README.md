# Build and run the Yocto Raspberry Pi 4 image

This guide builds a complete 64-bit Linux image for a Raspberry Pi 4 Model B
using Yocto. The image contains IoT App, embedded MicroPython, LVGL, Wi-Fi,
Ethernet, OpenSSH, the Mosquitto broker, I2C tools, and systemd services.

The Buildroot image remains supported. Both images run the same C++ executable
and the same default Python application. The difference is how the Linux
system is assembled.

Run the commands in this guide from the repository root unless a section says
otherwise.

## Image storage

The image contains a Raspberry Pi boot partition, a fixed-size Linux root
partition, and an ext4 data partition mounted at `/data`. The data partition
expands to use the rest of the card on first boot. `make yocto-prepare` reads
the root size from `storage_layout.conf` and
creates the Wic layout in the Yocto build directory.

The complete layout, size overrides, first-boot process, verification commands,
and upstream references are in the [shared storage guide](../storage/README.md).
Files that must behave the same in Yocto and Buildroot are described in the
[shared image-support guide](../../image_support/README.md).

## 1. What the development image does

The Yocto image is configured to:

- boot a 64-bit Raspberry Pi 4 system without a desktop;
- use systemd for services, networking, logs, and time synchronization;
- create a 1920x1080 framebuffer at 60 Hz;
- enable `/dev/fb0`, DRM/KMS, and `/dev/i2c-1`;
- reserve `tty1` for the IoT App display;
- provide a password-protected emergency root login on `tty2`;
- connect through Wi-Fi or Ethernet using DHCP;
- listen for SSH connections;
- run a local Mosquitto broker on IPv4 port 1883;
- expand and mount the persistent data partition at `/data`;
- start IoT App automatically after the framebuffer is available;
- keep running offline if the network is unavailable; and
- reconnect to Mosquitto when the broker becomes available.

The development SSH login is:

```text
Username: root
Password: root
```

The normal local login is on `tty2`, not on the IoT App screen. If networking
is unavailable, connect a keyboard and press `Alt+F2` or `Ctrl+Alt+F2`, then
log in with the development password above.

The password and anonymous MQTT listener are deliberately simple for testing
on a trusted network. Before using an image outside that environment, disable
password-based root login and add SSH keys, MQTT TLS, per-device credentials,
topic ACLs, and signed application packages.

### Change the root password

This image recipe uses Yocto's `extrausers` class to set an already-calculated
Linux password hash. Generate one using the
[shared password instructions](../device-image/README.md#change-the-development-password).

Open `meta-iot-app/recipes-core/images/iot-app-image.bb` and replace
`ROOT_PASSWORD_HASH` with the generated value. Add a backslash before every
`$` character:

```bitbake
ROOT_PASSWORD_HASH = "\$6\$random-salt\$password-hash"
EXTRA_USERS_PARAMS = "usermod -p '${ROOT_PASSWORD_HASH}' root;"
```

The backslashes preserve the dollar signs when BitBake places this value in the
shell task used by `extrausers`. Without the backslashes, the shell would treat
parts such as `$6` and `$random` as shell parameters or variables and damage
the hash. `EXTRA_USERS_PARAMS` passes the preserved hash to `usermod`, which
sets it as the root account password in the generated image.

The [Yocto `extrausers` class documentation](https://docs.yoctoproject.org/scarthgap/ref-manual/classes.html#extrausers)
shows the same escaped-hash pattern.

## 2. Yocto sources used by this project

The repository pins four Yocto-related parts:

| Path | Purpose |
|---|---|
| `poky/` | Yocto reference distribution, OpenEmbedded Core, and BitBake |
| `meta-openembedded/` | cJSON, Mosquitto, and their supporting recipes |
| `meta-raspberrypi/` | Raspberry Pi machine, kernel, firmware, and Wic image support |
| `meta-iot-app/` | Project-owned application, image, service, and configuration recipes |

The three upstream directories are Git submodules. `meta-iot-app` belongs to
this project and is changed alongside the C++ source.

All upstream Yocto layers use the `scarthgap` release series. Git records an
exact commit for every submodule, so two checkouts of the same project commit
use the same layer revisions.

For reference, the submodules were first registered with these commands:

```bash
git submodule add -b scarthgap \
  https://git.yoctoproject.org/poky poky
git submodule add -b scarthgap \
  https://git.openembedded.org/meta-openembedded meta-openembedded
git submodule add -b scarthgap \
  https://github.com/agherzan/meta-raspberrypi.git meta-raspberrypi
```

Do not run those commands in an existing checkout. Use `make submodules`
instead; Git already knows which revisions this project needs.

Do not modify files inside the upstream submodules. Put project changes in
`meta-iot-app`.

## 3. Build-host requirements

Use a native Linux build host. The current setup is tested from Ubuntu. A
Yocto build needs considerably more storage and memory than Buildroot. Plan
for at least 90 GB of free disk space and 8 GB of RAM. More CPU cores, memory,
and disk space will make the first build faster.

The official host requirements are listed in the
[Yocto Quick Build guide](https://docs.yoctoproject.org/brief-yoctoprojectqs/index.html).

Install the Ubuntu packages used by Poky and this project:

```bash
sudo apt update
sudo apt install \
  build-essential \
  chrpath \
  cpio \
  debianutils \
  diffstat \
  file \
  gawk \
  gcc \
  git \
  iputils-ping \
  libacl1 \
  liblz4-tool \
  locales \
  lz4 \
  openssl \
  python3 \
  python3-git \
  python3-jinja2 \
  python3-pexpect \
  python3-pip \
  python3-subunit \
  socat \
  texinfo \
  unzip \
  wget \
  xz-utils \
  zstd
```

During the initial configuration check on Ubuntu, these three missing tools
were installed with:

```bash
sudo apt-get install -y chrpath gawk lz4
```

Make sure the `en_US.UTF-8` locale is available:

```bash
locale -a | grep en_US.utf8
```

If it is missing, enable it:

```bash
sudo dpkg-reconfigure locales
```

Do not run BitBake or the root Makefile with `sudo`. The Makefile asks for
`sudo` only when it needs to create a directory under `/opt`.

## 4. Initialize the pinned submodules

Run:

```bash
make submodules
```

This is equivalent to:

```bash
git submodule sync --recursive
git submodule update --init --recursive
```

Confirm the pinned revisions:

```bash
git submodule status
```

The output should include `poky`, `meta-openembedded`, `meta-raspberrypi`,
`buildroot`, `lvgl`, and `micropython`.

## 5. Persistent build directories

The root Makefile uses this layout:

```text
/opt/iot-app-builds/
├── buildroot-raspberry-pi-4/
├── yocto-raspberry-pi-4/
│   └── build/
├── yocto-downloads/
├── yocto-sstate-cache/
├── yocto-sources/
│   └── poky -> repository Poky submodule
└── images/
```

The directories have separate jobs:

- `yocto-raspberry-pi-4/build` contains configuration, package work
  directories, logs, and generated images.
- `yocto-downloads` keeps downloaded upstream source.
- `yocto-sstate-cache` keeps reusable task results. It makes later builds much
  faster.
- `yocto-sources/poky` is a link to the pinned Poky submodule using a path
  without special characters.
- `images` contains files with stable names for Raspberry Pi Imager.

Yocto's path check rejects `@`, spaces, `+`, and `%` in `COREBASE`. Some
company home directories contain `@`. `make yocto-prepare` creates the clean
`/opt/iot-app-builds/yocto-sources/poky` link and sets `COREBASE` to that path.
It does not create another Poky checkout. The check is implemented in Poky's
[`sanity.bbclass`](https://git.yoctoproject.org/poky/tree/meta/classes-global/sanity.bbclass?h=scarthgap#n895).

The default paths can be changed when necessary:

```bash
make yocto-prepare \
  PERSISTENT_BUILD_ROOT=/opt/iot-app-builds-custom \
  YOCTO_OUTPUT=/opt/iot-app-builds-custom/yocto-raspberry-pi-4
```

Use the same variable values on later Make commands for that build.

## 6. Prepare the Yocto configuration

Run:

```bash
make yocto-prepare
```

The command:

1. checks that the three Yocto submodules are initialized;
2. creates the persistent directories;
3. creates the clean Poky source link;
4. loads the project template through `oe-init-build-env`;
5. creates `bblayers.conf` for a new build and refreshes `local.conf` from the
   project template;
6. creates the shared private Wi-Fi file when it is missing;
7. copies the Wi-Fi and optional SSH key files into the build configuration;
8. reads `storage_layout.conf`; and
9. creates the Wic partition layout used by the image recipe.

The generated Yocto configuration and its private Wi-Fi copy are under:

```text
/opt/iot-app-builds/yocto-raspberry-pi-4/build/conf
```

Running `make yocto-prepare` again keeps the private file in the repository
root and refreshes the Yocto build copy from it. It also refreshes `local.conf`
from the tracked template and writes the selected output and cache paths to
`iot-app-build-paths.conf`.

## 7. Configure Wi-Fi

`make yocto-prepare` copies the root-level `wpa_supplicant.conf` and optional
`ssh_authorized_keys` files into the private build configuration. Do not edit
the generated copies because the next preparation command replaces them.

The [shared device-image guide](../device-image/README.md#2-configure-wi-fi)
explains how to configure Wi-Fi and add an SSH public key.

## 8. Check the layers without building

Run:

```bash
make yocto-check
```

This command runs:

```bash
bitbake-layers show-layers
bitbake -e iot-app-image
```

It verifies that BitBake can find the machine, distro, image, recipes, and all
required providers. It does not compile a package or create an image. Poky may
download its small `uninative` support archive the first time BitBake starts.

The layer list should include:

```text
core
yocto
yoctobsp
openembedded-layer
meta-python
networking-layer
raspberrypi
iot_app
```

The Raspberry Pi Wi-Fi firmware is marked with the
`synaptics-killswitch` license flag by `meta-raspberrypi`. The project accepts
that specific flag in `local.conf` so the firmware needed by `wlan0` can be
included. See the
[`meta-raspberrypi` compliance note](https://github.com/agherzan/meta-raspberrypi/blob/scarthgap/docs/ipcompliance.md)
before redistributing an image.

### 8.1 Optional task-graph dry run

The following commands ask BitBake to walk the complete task graph without
executing the build tasks:

```bash
source poky/oe-init-build-env \
  /opt/iot-app-builds/yocto-raspberry-pi-4/build

bitbake -n iot-app
bitbake -n iot-app-image
```

BitBake still prints lines beginning with `Running task` during a dry run.
Those lines show the order it would use; the compile and image tasks are not
actually executed.

### 8.2 Build-time optimizations

The project disables several Poky features that are not used by this image:

| Disabled work | Reason |
|---|---|
| SPDX generation | Local development builds skip package-by-package SBOM generation to save time and storage. Re-enable `create-spdx` when an image needs an SPDX SBOM. Third-party license requirements still apply. |
| `ptest` suites | IoT App runs its own unit tests on the development computer and does not run package test suites on the Raspberry Pi. |
| OpenGL, Wayland, Vulkan, and X11 | LVGL draws directly to `/dev/fb0`; the image has no desktop or window system. |
| Audio, Bluetooth, 3G, NFC, and NFS | The current hardware and Python API do not use these services. Some of them also start background services during boot. |
| General-purpose base package group | The standard `core-image` base supports many kinds of hardware. This image starts with the minimal boot group and lists its required packages directly. |
| Complete kernel-module package | Installing every Raspberry Pi kernel module added more than 1,800 packages to the test image. The image selects the Broadcom Wi-Fi driver, its small WCC vendor module, and the I2C modules directly. The Wi-Fi and I2C drivers are loaded during boot. |
| Extra `wpa_supplicant` tools | The Wi-Fi service needs the daemon, but it does not use `wpa_cli`, `wpa_passphrase`, or optional plugins. |
| Timezone regions outside Europe | The image uses `Europe/London`, so it installs the timezone core and Europe data instead of every region. |
| Translated locale packages | The dashboard and command-line tools use the C locale. This is separate from the `Europe/London` timezone setting. |
| Build statistics | The project does not use BitBake's build-statistics reports. |
| Wic bmap output | Raspberry Pi Imager can write the compressed `.img.xz` file directly. |

IPK package creation and Yocto package QA remain enabled. BitBake uses the IPK
packages to assemble the root filesystem, and the QA checks catch invalid
package dependencies and installation mistakes.

Yocto documents the generated files and the setting used here in its
[software bill of materials guide](https://docs.yoctoproject.org/scarthgap/dev-manual/sbom.html).

The smaller package list still supports the features used by this project:
framebuffer output, Raspberry Pi Wi-Fi and Ethernet, I2C, MQTT, SSH/SFTP,
systemd networking and time synchronization, mDNS discovery through Avahi, the
local console, and ext4 filesystem checks. `i2c-tools` is also kept for
hardware troubleshooting. If a new peripheral needs another kernel module,
add that module to
`meta-iot-app/recipes-core/images/iot-app-image.bb`.

## 9. Build only IoT App

To cross-compile the application and create its Yocto package without building
the complete SD-card image, run:

```bash
make yocto-app
```

This runs:

```bash
bitbake iot-app
```

The first application-only build still needs to create the cross-toolchain and
compile application dependencies. Later builds reuse the download and
shared-state caches.

The generated IPK packages are placed below:

```text
/opt/iot-app-builds/yocto-raspberry-pi-4/build/tmp/deploy/ipk
```

## 10. Build the complete image

Run:

```bash
make yocto-image
```

The target sources Poky's build environment and runs:

```bash
bitbake iot-app-image
```

The first build compiles the toolchain, kernel, firmware, systemd, libraries,
Mosquitto, OpenSSH, IoT App, and the root filesystem. It can take a long time.
Do not close the terminal or restart the build computer during this first
build.

The root Makefile removes `LD_LIBRARY_PATH` only for Yocto commands. A host
library path can otherwise make build tools load libraries from the wrong
location.

The normal Yocto output is:

```text
/opt/iot-app-builds/yocto-raspberry-pi-4/build/tmp/deploy/images/raspberrypi4-64/
```

The Wic image produced by BitBake is:

```text
iot-app-image-raspberrypi4-64.rootfs.wic.xz
```

Wic creates a complete disk image containing the Raspberry Pi boot partition,
the Linux root filesystem, and the expandable data filesystem. The Makefile
copies the same compressed image
to this stable name:

```text
/opt/iot-app-builds/images/iot-app-yocto-rpi4.img.xz
```

No manual `bzip2`, `xz`, or rename command is needed.

The [Yocto Wic guide](https://docs.yoctoproject.org/scarthgap/dev-manual/wic.html)
explains how Wic creates partitioned images that can be written to removable
media.

Check the image:

```bash
ls -lh /opt/iot-app-builds/images/iot-app-yocto-rpi4.img.xz
sha256sum /opt/iot-app-builds/images/iot-app-yocto-rpi4.img.xz
```

## 11. Write the image with Raspberry Pi Imager

1. Insert the microSD card into the Ubuntu computer.
2. Open Raspberry Pi Imager.
3. Choose the Raspberry Pi 4 device.
4. Select **Choose OS**, then **Use custom**.
5. Select:

   ```text
   /opt/iot-app-builds/images/iot-app-yocto-rpi4.img.xz
   ```

6. Select the correct microSD card.
7. Start writing and confirm the erase warning.
8. Wait for the write and verification steps to finish.

Do not use Raspberry Pi Imager's operating-system customization dialog to add
Wi-Fi or a user. Those settings are already part of the image.

`bmaptool` is also supported by the upstream Raspberry Pi Yocto layer, but it
is optional for this project. Raspberry Pi Imager can write the `.img.xz`
artifact directly.

## 12. Start and troubleshoot the device

After flashing, use the [shared device-image guide](../device-image/README.md)
for first boot, mDNS, SSH, service commands, hardware checks, MQTT, and runtime
troubleshooting. That guide shows the appropriate Buildroot and Yocto commands
side by side.

## 13. Deploy a rebuilt application without flashing

Build the updated application package:

```bash
make yocto-app
```

The service uses `/usr/libexec/iot-app-launcher`. When a development executable
exists under `/data`, the launcher runs it instead of `/usr/bin/iot_app`. The
installed executable remains unchanged.

Load the Yocto environment and find the executable produced by the recipe:

```bash
source poky/oe-init-build-env \
  /opt/iot-app-builds/yocto-raspberry-pi-4/build

iot_app_install_directory="$(
  bitbake -e iot-app |
    sed -n 's/^D="\([^"]*\)"$/\1/p'
)"

test -x "$iot_app_install_directory/usr/bin/iot_app"
```

Copy the new executable to a temporary name under `/data`:

```bash
scp "$iot_app_install_directory/usr/bin/iot_app" \
  root@rspi-iot-app.local:/data/iot-app/development/iot_app.new
```

Activate the completely uploaded file and restart the service:

```bash
ssh root@rspi-iot-app.local '
  set -eu
  chmod 0755 /data/iot-app/development/iot_app.new
  mv /data/iot-app/development/iot_app.new \
     /data/iot-app/development/iot_app
  systemctl restart iot-app
'
```

Check that the new process is running:

```sh
ssh root@rspi-iot-app.local \
  'systemctl status iot-app --no-pager'
```

To return to the executable installed in the image, remove the development
copy and restart the service:

```bash
ssh root@rspi-iot-app.local '
  rm -f /data/iot-app/development/iot_app &&
  systemctl restart iot-app
'
```

The development copy remains under `/data` after reboot. The complete
[development executable guide](../development-executable/README.md) explains
launcher selection, logging, recovery, and which changes still require a new
image.

### Why the current recipe does not use `devtool deploy-target`

In a normal devtool workspace, `devtool deploy-target` reads the recipe's
`do_install` output. Before installing it on the target, devtool moves the
original files into `/.devtool/<recipe>.preserve`. It then installs the new
files and records their paths in `/.devtool/<recipe>.list`.

The running process continues using the old executable until the service is
restarted. `devtool undeploy-target` removes the development files and restores
the preserved originals. If a deployed systemd unit changed, run
`systemctl daemon-reload` before restarting its service.

In the pinned Poky version, `devtool deploy-target` accepts only recipes that
are registered in the devtool workspace. The current IoT App recipe already
uses `externalsrc` to build this repository directly. Poky's `devtool modify`
command refuses to register a recipe while that setting is active.

This means `devtool deploy-target iot-app ...` cannot be used directly with
the current recipe. It could be enabled by restructuring the recipe so that
devtool manages its external source tree. For the current source layout,
copying the rebuilt executable is the simpler development workflow. The
[Yocto devtool reference](https://docs.yoctoproject.org/scarthgap/ref-manual/devtool-reference.html#deploying-your-software-on-the-target-machine)
describes the standard workspace and deployment process.

Reflash a complete image before final testing so the device is tested from a
clean, reproducible filesystem.

## 14. Rebuild the image after source changes

For normal C++ or Python source changes, run:

```bash
make yocto-image
```

The `iot-app` recipe uses the current project working tree. BitBake detects
source changes, rebuilds the required tasks, and reuses unchanged work from
the shared-state cache.

When troubleshooting a stale application build, clean only that recipe:

```bash
source poky/oe-init-build-env \
  /opt/iot-app-builds/yocto-raspberry-pi-4/build

bitbake -c clean iot-app
bitbake iot-app
```

For a stronger reset of the application recipe and its cached output:

```bash
bitbake -c cleansstate iot-app
bitbake iot-app-image
```

Do not delete `yocto-downloads` or `yocto-sstate-cache` for an ordinary source
change.

## 15. Change generated configuration

`make yocto-prepare` recreates `local.conf` from
`meta-iot-app/conf/templates/raspberrypi4-64/local.conf.sample`. This makes the
tracked template the single place for project image settings. Do not edit the
generated `local.conf`, because the next preparation command replaces it.

Machine-specific build paths are kept separately in the generated
`iot-app-build-paths.conf`. Change the root Makefile variables when another
output or cache location is needed:

```bash
make yocto-prepare \
  YOCTO_OUTPUT=/opt/iot-app-builds-custom/yocto-raspberry-pi-4 \
  YOCTO_DOWNLOAD_DIRECTORY=/opt/iot-app-builds-custom/yocto-downloads \
  YOCTO_SSTATE_DIRECTORY=/opt/iot-app-builds-custom/yocto-sstate-cache
```

Use the same values for later commands that use this build directory.

## 16. Update the Yocto submodules

Poky, `meta-openembedded`, and `meta-raspberrypi` must stay on compatible
release series. This project currently uses `scarthgap` for all three.

Before an update, check the repository and every submodule:

```bash
git status --short
git submodule status
git -C poky status --short
git -C meta-openembedded status --short
git -C meta-raspberrypi status --short
```

Fetch the selected release branches:

```bash
git -C poky fetch origin scarthgap
git -C meta-openembedded fetch origin scarthgap
git -C meta-raspberrypi fetch origin scarthgap
```

Inspect the available commits and choose tested revisions. Check out the exact
commit in each submodule rather than blindly following the latest branch head:

```bash
git -C poky checkout POKY_COMMIT
git -C meta-openembedded checkout META_OPENEMBEDDED_COMMIT
git -C meta-raspberrypi checkout META_RASPBERRYPI_COMMIT
```

Then run:

```bash
make yocto-check
make yocto-image
```

Test Wi-Fi, Ethernet, SSH, Mosquitto, time synchronization, framebuffer
output, I2C, IoT App startup, and MQTT deployment on a real Raspberry Pi before
recording the new submodule commits.

Do not use `git submodule update --remote` as a release-update procedure. It
selects branch heads without showing whether those three commits were tested
together.

Changing from one Yocto release series to another also requires updating:

- the branch values in `.gitmodules`;
- `LAYERSERIES_COMPAT_iot_app`;
- the distro codename;
- image and package configuration changed by the new release; and
- this guide after a clean image test.

## 17. Yocto build troubleshooting

### Missing host tools

An error such as this means the Ubuntu host packages are incomplete:

```text
The following required tools appear to be unavailable in PATH:
chrpath gawk lz4c
```

Install them and rerun the check:

```bash
sudo apt-get install -y chrpath gawk lz4
make yocto-check
```

### Invalid `@` character in `COREBASE`

Run:

```bash
make yocto-prepare
```

Then check:

```bash
grep '^COREBASE' \
  /opt/iot-app-builds/yocto-raspberry-pi-4/build/conf/local.conf
```

It should show:

```text
COREBASE = "/opt/iot-app-builds/yocto-sources/poky"
```

### Wi-Fi firmware license flag

If BitBake says `linux-firmware-rpidistro` was skipped because of
`synaptics-killswitch`, rerun:

```bash
make yocto-prepare
```

The generated `local.conf` should contain:

```text
LICENSE_FLAGS_ACCEPTED += "synaptics-killswitch"
```

### Device startup and runtime problems

The [shared device-image troubleshooting guide](../device-image/README.md#10-troubleshooting)
covers Wi-Fi, SSH, MQTT, IoT App startup, time synchronization, console
messages, persistent storage, and differences in Buildroot and Yocto startup
timing.

### The image file is missing

Check BitBake's deploy directory:

```bash
find \
  /opt/iot-app-builds/yocto-raspberry-pi-4/build/tmp/deploy/images/raspberrypi4-64 \
  -maxdepth 1 \( -type f -o -type l \)
```

When the build completed but the stable copy was not created, rerun:

```bash
make yocto-image
```

The expected Raspberry Pi Imager file is:

```text
/opt/iot-app-builds/images/iot-app-yocto-rpi4.img.xz
```

## 18. Files that implement the Yocto image

| Path | Purpose |
|---|---|
| `meta-iot-app/conf/layer.conf` | Registers the project layer and local source root |
| `meta-iot-app/conf/distro/iot-app-linux.conf` | Selects Poky policy and systemd |
| `meta-iot-app/conf/templates/raspberrypi4-64/` | Creates `local.conf` and `bblayers.conf` |
| `meta-iot-app/recipes-iot/iot-app/` | Builds the C++ runtime, creates its service account and device groups, and installs the systemd unit and shared image-support files |
| `meta-iot-app/recipes-core/iot-app-system-config/` | Adds network units, Wi-Fi startup, time services, and the `tty2` emergency login |
| `meta-iot-app/recipes-connectivity/` | Installs the private Wi-Fi file and shared development Mosquitto configuration without making two recipes own the same files |
| `meta-iot-app/recipes-core/images/iot-app-image.bb` | Selects SSH, Mosquitto, Wi-Fi firmware, I2C tools, timezone data, and IoT App packages for the bootable image |
| `Makefile` | Creates persistent paths and provides the short build commands |
| `scripts/build/prepare-yocto.sh` | Creates the build directories and generated configuration files |

The upstream Yocto layers are read-only dependencies. All project-specific
behaviour remains in `meta-iot-app`.
