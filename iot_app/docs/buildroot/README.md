# Build and run the Raspberry Pi 4 image

This guide builds a complete 64-bit Linux image for a Raspberry Pi 4 Model B.
The image contains IoT App, its embedded MicroPython runtime, LVGL, Wi-Fi
support, the Mosquitto broker, and the Dropbear SSH server.

The instructions assume the repository is on an Ubuntu computer and that the
target uses a microSD card. Run all build commands from the repository root.

## Image storage

The image contains a Raspberry Pi boot partition, a fixed-size Linux root
partition, and an ext4 data partition mounted at `/data`. The data partition
expands to use the rest of the card on first boot. The root partition size
comes from the root-level `storage_layout.conf` file.

The complete layout, size overrides, first-boot process, verification commands,
and upstream references are in the [shared storage guide](../storage/README.md).
Files that must behave the same in Buildroot and Yocto are described in the
[shared image-support guide](../../image_support/README.md).

## 1. What the development image does

The `iot_rpi4_defconfig` configuration prepares the Pi to:

- boot without a desktop environment;
- reserve `tty1` for the IoT App display;
- provide a password-protected emergency root login on `tty2`;
- connect to the configured Wi-Fi network when it is available;
- request IPv4 addresses for `wlan0` and `eth0` using DHCP;
- synchronize the system clock from internet NTP servers;
- accept SSH connections as `root`;
- start Mosquitto on IPv4 port 1883 for development deployments from the
  local network;
- expand and mount the persistent data partition at `/data`;
- start IoT App automatically near the end of boot; and
- provide `/etc/init.d/iot-app` for manually starting, stopping, or restarting
  the service.

The development SSH login is:

```text
Username: root
Password: root
```

The root password is deliberately simple because this image is for initial
testing. Do not use this setup on a production device. Use an SSH key, disable
password login, and avoid direct root access there.

### Change the root password

The current Buildroot configuration contains the development password as
plain text:

```makefile
BR2_TARGET_GENERIC_ROOT_PASSWD="root"
```

Buildroot converts a plain value to a Linux password hash while creating the
root filesystem. Generate a hash as described in the
[shared password instructions](../device-image/README.md#change-the-development-password)
to avoid storing the new password as plain text.

Open
`iot_app/buildroot_external/configs/iot_rpi4_defconfig` and replace the
existing password value with the generated hash. Double every `$` character:

```makefile
BR2_TARGET_GENERIC_ROOT_PASSWD="$$6$$random-salt$$password-hash"
```

Buildroot needs `$$` here because Make treats a single `$` as the start of a
variable reference. Buildroot recognizes hashes beginning with `$1$`, `$5$`,
or `$6$` and writes them to the target password file without hashing them a
second time. The same rules are documented in Buildroot's
[`BR2_TARGET_GENERIC_ROOT_PASSWD` configuration help](https://gitlab.com/buildroot.org/buildroot/-/blob/2026.05.1/system/Config.in).

The development MQTT listener also accepts anonymous, unencrypted connections
from the local network. Use this only on a trusted test network. Before using
the image in production, add TLS, per-device credentials, topic ACLs, and
application-package signature verification.

The normal local login is on `tty2`, not on the IoT App screen. If networking
is unavailable, connect a keyboard and press `Alt+F2` or `Ctrl+Alt+F2`, then
log in with the development password above.

## 2. Install the Ubuntu build tools

Install the tools required by Buildroot:

```bash
sudo apt update
sudo apt install \
  build-essential \
  bc \
  bzip2 \
  cpio \
  file \
  git \
  gzip \
  libncurses-dev \
  openssl \
  patch \
  perl \
  rsync \
  tar \
  unzip \
  wget \
  which
```

Buildroot downloads source archives during the first build, so the Ubuntu
computer must have internet access.

## 3. Initialize the pinned source submodules

Run:

```bash
git submodule update --init --recursive
```

This checks out every pinned submodule, including Buildroot, LVGL,
MicroPython, and the Yocto layers. They are upstream source dependencies and
should not be modified.

You can confirm their revisions with:

```bash
git submodule status
```

## 4. Add the private Wi-Fi configuration

Create the private configuration shared by Buildroot and Yocto:

```bash
make wifi-prepare
```

Add the network name and password before building. The
[shared device-image guide](../device-image/README.md#2-configure-wi-fi)
explains the file format, Git exclusion, and optional SSH key setup.

## 5. Choose the Buildroot output directory

The Buildroot output path must not contain an `@` character. This matters on
computers where the home-directory name contains a company or domain suffix.

The easiest approach is to run this from the repository root:

```bash
make submodules
make buildroot-prepare
```

The root Makefile creates this persistent directory, asking for `sudo` only
when it needs permission to create it:

```text
/opt/iot-app-builds/buildroot-raspberry-pi-4
```

It stores the Buildroot configuration, cross-toolchain, compiled packages,
target filesystem, and images. These files remain available after reboot.

Set `BUILDROOT_OUTPUT` when another persistent location is preferred:

```bash
make buildroot-prepare \
  BUILDROOT_OUTPUT=/opt/iot-app-builds-custom/buildroot-raspberry-pi-4
```

The remaining commands use this persistent output directory.

Each `make buildroot-prepare` call reloads `iot_rpi4_defconfig`. This makes
changes to the tracked configuration available when an existing output
directory is reused. The preparation script then applies the root-partition
size from `storage_layout.conf`.

## 6. Load the Raspberry Pi 4 configuration

Run:

```bash
make -C buildroot \
  BR2_EXTERNAL="$PWD/iot_app/buildroot_external" \
  O=/opt/iot-app-builds/buildroot-raspberry-pi-4 \
  iot_rpi4_defconfig
```

This creates `/opt/iot-app-builds/buildroot-raspberry-pi-4/.config` from the
project-owned Raspberry Pi 4 configuration.

The important selections include:

- AArch64 and Cortex-A72;
- the `bcm2711-rpi-4-b` device tree;
- VC4/KMS and framebuffer support;
- I²C support;
- Raspberry Pi Wi-Fi firmware;
- `wpa_supplicant`;
- Dropbear SSH;
- Mosquitto; and
- the tools that expand and mount `/data`; and
- the project-owned `iot_app` Buildroot package.

### Set the startup display resolution

The Raspberry Pi 4 image starts HDMI-A-1 at 1920x1080 and 60 Hz. The setting
is stored in the project-owned boot command line:

```text
iot_app/buildroot_external/board/raspberrypi4/cmdline.txt
```

Its display option is:

```text
video=HDMI-A-1:1920x1080@60
```

Linux applies this mode while it creates the display and framebuffer. IoT App
then uses the existing `/dev/fb0` resolution; it does not change the display
mode itself.

The same command line contains `quiet loglevel=4`. These options keep normal
kernel status messages off the LVGL dashboard. Serious kernel messages may
still be printed. Nothing is removed from the kernel log; read it over SSH
with:

```sh
dmesg
```

See the [Raspberry Pi KMS display documentation](https://www.raspberrypi.com/documentation/computers/configuration.html#set-the-kms-display-mode)
for the `video=` setting. The separate
[Linux kernel parameter reference](https://docs.kernel.org/admin-guide/kernel-parameters.html)
documents `quiet` and `loglevel=`.

To use another resolution, replace only the value after `video=` with a mode
supported by the monitor. Keep the complete command line on one line. For
example:

```text
video=HDMI-A-1:1280x720@60
```

After changing this file, regenerate and flash `sdcard.img`. On the running
Pi, confirm the applied setting with:

```sh
cat /proc/cmdline
cat /sys/class/graphics/fb0/virtual_size
```

If an older image prints kernel messages over the dashboard, this command
applies the same console filter until the next reboot:

```sh
dmesg -n 4
```

### Change the timezone

The image uses `Europe/London`. To use another timezone, change
`BR2_TARGET_LOCALTIME` in `iot_rpi4_defconfig`. Also set
`BR2_TARGET_TZ_ZONELIST` to the lowercase tzdata source group containing that
timezone. For example, `Europe/London` is in the `europe` group. The group name
is not the full timezone name.

## 7. Build the complete image

The short project command rebuilds IoT App, creates the image, and copies it to
a stable path for Raspberry Pi Imager:

```bash
make buildroot-image
```

The Makefile runs the following Buildroot command once for
`iot_app-dirclean`, then again for the full parallel build:

```bash
env -u LD_LIBRARY_PATH \
  make -C buildroot \
  BR2_EXTERNAL="$PWD/iot_app/buildroot_external" \
  O=/opt/iot-app-builds/buildroot-raspberry-pi-4 \
  iot_app-dirclean

env -u LD_LIBRARY_PATH \
  make -C buildroot \
  BR2_EXTERNAL="$PWD/iot_app/buildroot_external" \
  O=/opt/iot-app-builds/buildroot-raspberry-pi-4 \
  all -j"$(getconf _NPROCESSORS_ONLN)"
```

The first build takes a while because Buildroot must download and compile the
cross-toolchain, Linux kernel, libraries, and application dependencies.

`env -u LD_LIBRARY_PATH` removes `LD_LIBRARY_PATH` only for this command. It
does not change the current terminal. Buildroot needs this because a host
library path containing the current directory can cause host tools to load the
wrong libraries and stop with this error:

```text
You seem to have the current working directory in your
LD_LIBRARY_PATH environment variable. This doesn't work.
```

Use the same `env -u LD_LIBRARY_PATH` prefix on later Buildroot rebuild
commands if the Ubuntu shell sets that variable.

Buildroot already compiles the contents of each package in parallel. The
number of those jobs defaults to the number of host CPU threads plus one, so
the command above uses the available CPU without a top-level `-j` option.

Buildroot also has experimental support for building independent packages at
the same time. That feature requires `BR2_PER_PACKAGE_DIRECTORIES=y`, which the
current `iot_rpi4_defconfig` does not enable. If that option is
enabled later, request one top-level job per available CPU thread with:

```bash
env -u LD_LIBRARY_PATH \
  make -C buildroot \
  BR2_EXTERNAL="$PWD/iot_app/buildroot_external" \
  O=/opt/iot-app-builds/buildroot-raspberry-pi-4 \
  -j"$(nproc)"
```

Without `BR2_PER_PACKAGE_DIRECTORIES=y`, Buildroot serializes top-level package
work even if `-j"$(nproc)"` is passed. A message such as `-j1 forced in
submake` during `syncconfig` is not the `LD_LIBRARY_PATH` failure.

Do not run the Buildroot build with `sudo`. Only the later microSD flashing
command needs administrator access.

When the build finishes, check the bootable image:

```bash
ls -lh /opt/iot-app-builds/buildroot-raspberry-pi-4/images/sdcard.img
```

You can also record its checksum:

```bash
sha256sum /opt/iot-app-builds/buildroot-raspberry-pi-4/images/sdcard.img
```

The file `sdcard.img` contains the Raspberry Pi boot partition, the Linux root
filesystem, and the expandable data filesystem. Do not copy `rootfs.ext4` by
itself to the microSD card.

When `make buildroot-image` is used, the same image is also available as:

```text
/opt/iot-app-builds/images/iot-app-buildroot-rpi4.img
```

## 8. Flash the microSD card

Flashing erases the selected device. Carefully confirm the microSD device name
before continuing.

### Option A: Raspberry Pi Imager

This is the safer method:

1. Open Raspberry Pi Imager on Ubuntu.
2. Choose the Raspberry Pi 4 as the device.
3. Choose **Use custom** as the operating system.
4. Select `/opt/iot-app-builds/images/iot-app-buildroot-rpi4.img`. If the
   image was built with the full Buildroot command instead of the root
   Makefile, select
   `/opt/iot-app-builds/buildroot-raspberry-pi-4/images/sdcard.img`.
5. Select the microSD card.
6. Confirm the write.

Do not use Raspberry Pi Imager's OS customization dialog to configure Wi-Fi or
SSH. Those settings are already part of this Buildroot image, and the Imager
customization mechanism is intended for Raspberry Pi OS.

### Option B: `dd`

Before inserting the microSD card, list the current storage devices:

```bash
lsblk -p -o NAME,SIZE,MODEL,TRAN,MOUNTPOINTS
```

Insert the card and run the command again:

```bash
lsblk -p -o NAME,SIZE,MODEL,TRAN,MOUNTPOINTS
```

Identify the newly added whole device. It may be `/dev/sdb`, `/dev/sdc`, or an
SD-reader device such as `/dev/mmcblk0`. A name ending in `1` or `2` is a
partition, not the whole device.

Unmount any automatically mounted partitions using their exact names. For
example:

```bash
sudo umount /dev/sdX1
sudo umount /dev/sdX2
```

Replace `/dev/sdX` below with the verified whole microSD device:

```bash
sudo dd \
  if=/opt/iot-app-builds/buildroot-raspberry-pi-4/images/sdcard.img \
  of=/dev/sdX \
  bs=4M \
  status=progress \
  conv=fsync
```

Wait for the command to finish, then run:

```bash
sync
```

Remove the microSD card safely.

## 9. Start and troubleshoot the device

After flashing, use the [shared device-image guide](../device-image/README.md)
for first boot, mDNS, SSH, service commands, hardware checks, MQTT, and runtime
troubleshooting. That guide shows the appropriate Buildroot and Yocto commands
side by side.

## 10. Rebuild and deploy changes to IoT App

During development, an IoT App source change does not require rebuilding and
flashing the complete Buildroot image. Build only the `iot_app` package with
Buildroot's cross-compiler, copy the new executable to the Raspberry Pi, and
restart the service.

Do not copy an executable from the normal Ubuntu build directory. A file such
as `build/iot_app/iot_app` was built for the Ubuntu computer and may be an
x86-64 executable. The Raspberry Pi needs the AArch64 executable produced by
Buildroot.

### 10.1 Rebuild only the IoT App package

Buildroot does not automatically notice every change in a package that uses a
local source directory. First remove Buildroot's old copy of the IoT App
source:

```bash
env -u LD_LIBRARY_PATH \
  make -C buildroot \
  BR2_EXTERNAL="$PWD/iot_app/buildroot_external" \
  O=/opt/iot-app-builds/buildroot-raspberry-pi-4 \
  iot_app-dirclean
```

Then build and install only the `iot_app` package into Buildroot's target
directory:

```bash
env -u LD_LIBRARY_PATH \
  make -C buildroot \
  BR2_EXTERNAL="$PWD/iot_app/buildroot_external" \
  O=/opt/iot-app-builds/buildroot-raspberry-pi-4 \
  iot_app
```

Buildroot reuses the existing cross-toolchain and already-built dependencies.
It does not rebuild the Linux kernel or generate a new filesystem image in
this step.

Confirm that the new executable is for the Raspberry Pi:

```bash
file /opt/iot-app-builds/buildroot-raspberry-pi-4/target/usr/bin/iot_app
```

The result should identify an ARM AArch64 executable. Do not deploy it if the
result says `x86-64`.

### 10.2 Copy the executable to a running Raspberry Pi

The image keeps development executables under `/data`. Copy the rebuilt file
there instead of replacing `/usr/bin/iot_app`:

```bash
scp -O /opt/iot-app-builds/buildroot-raspberry-pi-4/target/usr/bin/iot_app \
  root@rspi-iot-app.local:/data/iot-app/development/iot_app.new
```

The `-O` option asks `scp` to use its original SCP protocol. This works with
the Dropbear SSH server in the Buildroot image even when the Ubuntu `scp`
command normally prefers SFTP.

Activate the completely uploaded file and restart the service:

```bash
ssh root@rspi-iot-app.local '
  chmod 0755 /data/iot-app/development/iot_app.new &&
  mv /data/iot-app/development/iot_app.new \
     /data/iot-app/development/iot_app &&
  /etc/init.d/iot-app restart
'
```

Check that the application is running:

```bash
ssh root@rspi-iot-app.local "ps | grep '[i]ot_app'"
```

To see startup logs directly, stop the service and run the executable in the
SSH terminal:

```bash
ssh root@rspi-iot-app.local
/etc/init.d/iot-app stop
/data/iot-app/development/iot_app
```

Press `Ctrl+C` when the test is complete, then restore normal service
operation:

```sh
/etc/init.d/iot-app start
```

If the development executable does not work, remove it and restart the
service. The launcher then returns to the copy installed in the image:

```bash
ssh root@rspi-iot-app.local '
  rm -f /data/iot-app/development/iot_app &&
  /etc/init.d/iot-app restart
'
```

The development file remains under `/data` after reboot. The complete
[development executable guide](../development-executable/README.md) explains
launcher selection, logging, recovery, and which changes still require a new
image.

### 10.3 Deploy only a changed default Python application

Changing only the shipped default Python application does not require C++
compilation. Copy its two files to the Pi:

```bash
scp -O \
  iot_app/default_python_application/app.json \
  iot_app/default_python_application/main.py \
  root@rspi-iot-app.local:/tmp/
```

Stop IoT App, install the files, and start it again:

```bash
ssh root@rspi-iot-app.local \
  '/etc/init.d/iot-app stop &&
   install -m 0644 /tmp/app.json \
     /usr/share/iot-app/default_python_application/app.json &&
   install -m 0644 /tmp/main.py \
     /usr/share/iot-app/default_python_application/main.py &&
   /etc/init.d/iot-app start'
```

### 10.4 Update the final Buildroot image

The SCP workflow changes the writable filesystem on the running Raspberry Pi.
It does not update
`/opt/iot-app-builds/buildroot-raspberry-pi-4/images/sdcard.img`, so those changes will
be lost the next time the old image is flashed.

Before producing or flashing a release image, rebuild the package and
regenerate the image:

```bash
env -u LD_LIBRARY_PATH \
  make -C buildroot \
  BR2_EXTERNAL="$PWD/iot_app/buildroot_external" \
  O=/opt/iot-app-builds/buildroot-raspberry-pi-4 \
  iot_app-dirclean all
```

Reflash the newly generated `sdcard.img` to verify the final image.

Rebuilding only `iot_app` is suitable when its C++ code or default Python
application changes. Rebuild the complete image when changing the Buildroot
configuration, toolchain, Linux kernel, device tree, root filesystem overlay,
startup scripts, or shared-library configuration.

If the Buildroot configuration, toolchain, or Linux configuration changes,
start from a clean output directory and repeat the configuration and full-build
steps. Buildroot does not support safely switching major configurations inside
an existing output tree.

Use these commands for that clean rebuild:

```bash
env -u LD_LIBRARY_PATH \
  make -C buildroot \
  BR2_EXTERNAL="$PWD/iot_app/buildroot_external" \
  O=/opt/iot-app-builds/buildroot-raspberry-pi-4 \
  clean

env -u LD_LIBRARY_PATH \
  make -C buildroot \
  BR2_EXTERNAL="$PWD/iot_app/buildroot_external" \
  O=/opt/iot-app-builds/buildroot-raspberry-pi-4 \
  iot_rpi4_defconfig

env -u LD_LIBRARY_PATH \
  make -C buildroot \
  BR2_EXTERNAL="$PWD/iot_app/buildroot_external" \
  O=/opt/iot-app-builds/buildroot-raspberry-pi-4
```

## 11. Update the pinned submodules

Buildroot, LVGL, and MicroPython are separate Git repositories stored as
submodules. This repository records one exact commit for each dependency, so
another checkout uses the same source versions. The Yocto submodules are
covered separately in the [Yocto guide](../yocto/README.md).

This command does not look for newer versions:

```bash
git submodule update --init --recursive
```

It only checks out the revisions already recorded by the main project. Updating
a dependency requires selecting and testing a new tag or commit.

### 11.1 Check the current revisions

Before changing anything, make sure the project and submodules do not contain
uncommitted work:

```bash
git status --short
git submodule status
git -C buildroot status --short
git -C lvgl status --short
git -C micropython status --short
```

Do not update a submodule while it contains local source changes. The project
treats Buildroot, LVGL, and MicroPython as read-only upstream dependencies.

Update one dependency at a time. If the build then fails, it is much easier to
tell which update caused it.

### 11.2 Select a new revision

For example, to inspect available Buildroot versions:

```bash
git -C buildroot fetch --tags
git -C buildroot tag --list | tail
```

After choosing a release, check out its exact tag or commit:

```bash
git -C buildroot checkout BUILDROOT_TAG_OR_COMMIT
```

Use the same process for LVGL or MicroPython:

```bash
git -C lvgl fetch --tags
git -C lvgl checkout LVGL_TAG_OR_COMMIT

git -C micropython fetch --tags
git -C micropython checkout MICROPYTHON_TAG_OR_COMMIT
```

Replace the uppercase placeholder with the chosen tag or commit. Do not use
`git submodule update --remote` for a release update because it can select a
new upstream revision without an explicit compatibility review.

The main repository will now show the changed submodule revision:

```bash
git status --short
git submodule status
```

The `.gitmodules` file does not need to change unless a submodule path or
repository URL changes.

### 11.3 Checks required after updating Buildroot

The Buildroot update can change package names, configuration symbols,
toolchains, board scripts, firmware, and the Linux kernel selected for the
Raspberry Pi. Compare the old and new built-in Raspberry Pi configurations:

```bash
git -C buildroot diff \
  OLD_BUILDROOT_COMMIT \
  NEW_BUILDROOT_COMMIT \
  -- \
  configs/raspberrypi4_64_defconfig \
  board/raspberrypi4-64 \
  board/raspberrypi/patches/linux/linux.hash
```

Review that output and update the project-owned `iot_rpi4_defconfig` where
needed. In particular, check:

- architecture and toolchain selections;
- Raspberry Pi firmware options;
- Linux device-tree names;
- post-build and post-image script paths;
- renamed or removed package symbols; and
- the Linux kernel commit and its matching SHA-256 checksum.

The value in `BR2_LINUX_KERNEL_CUSTOM_TARBALL_LOCATION` is a Raspberry Pi Linux
Git commit, not a Buildroot commit. For example:

```make
BR2_LINUX_KERNEL_CUSTOM_TARBALL_LOCATION="$(call github,raspberrypi,linux,21b410140c47ffab5668399f6f143c7d7b935c8b)/linux-21b410140c47ffab5668399f6f143c7d7b935c8b.tar.gz"
```

The 40-character value selects one exact commit from
`github.com/raspberrypi/linux`. It appears twice because it is part of both the
download URL and the archive filename.

Buildroot verifies that archive using:

```text
board/raspberrypi/patches/linux/linux.hash
```

When adopting the kernel selected by a newer Buildroot release:

1. Read the kernel commit from the new built-in
   `configs/raspberrypi4_64_defconfig`.
2. Copy that commit into the project `iot_rpi4_defconfig`.
3. Confirm the new archive filename has a SHA-256 entry in
   `board/raspberrypi/patches/linux/linux.hash`.
4. Do a clean build.

Do not replace the Linux commit with the Buildroot submodule commit. If the old
Linux version is intentionally retained, its matching hash must also be kept
in a project-owned hash location. A newer Buildroot release may remove the old
archive entry from its built-in `linux.hash` file.

Load the project configuration through the new Buildroot version:

```bash
make -C buildroot \
  BR2_EXTERNAL="$PWD/iot_app/buildroot_external" \
  O=/opt/iot-app-builds/buildroot-raspberry-pi-4 \
  iot_rpi4_defconfig
```

Buildroot can silently drop a configuration symbol that no longer exists.
Create a normalized copy and compare it with the project defconfig:

```bash
make -C buildroot \
  BR2_EXTERNAL="$PWD/iot_app/buildroot_external" \
  O=/opt/iot-app-builds/buildroot-raspberry-pi-4 \
  BR2_DEFCONFIG=/tmp/iot_rpi4_saved_defconfig \
  savedefconfig

diff -u \
  iot_app/buildroot_external/configs/iot_rpi4_defconfig \
  /tmp/iot_rpi4_saved_defconfig
```

Some differences only remove settings that now have the same default value.
Review every difference instead of copying the generated file automatically.

Finally, perform the clean build shown in section 10.4. A Buildroot update
must not reuse packages or a toolchain built by the previous version.

### 11.4 Checks required after updating LVGL

LVGL is compiled directly into IoT App. After selecting a new LVGL revision:

1. Read the LVGL release notes for API and configuration changes.
2. Check `iot_app/config/lv_conf.h` for renamed or removed settings.
3. Check the framebuffer calls in
   `iot_app/src/ui/lvgl_framebuffer_render_backend.cpp` against the new LVGL
   API.
4. Check that the built-in Montserrat font sizes used by IoT App are still
   enabled.
5. Update documentation links and examples that contain an old LVGL version.
6. Perform a clean CMake build and a clean Buildroot image build.
7. Test startup, normal text drawing, moving and deleting text boxes, timers,
   the default dashboard, and the emergency error screen on the Pi.

Find project text that still mentions the old version with:

```bash
rg -n "OLD_LVGL_VERSION" iot_app
```

Replace `OLD_LVGL_VERSION` with the previous version number, such as `9.5.0`.
Do not update a version shown in historical notes unless the text claims it is
the current version.

### 11.5 Checks required after updating MicroPython

MicroPython is also compiled directly into the executable. The project uses
its C embedding API and project-owned native modules, so an interpreter update
needs more than a Python syntax check.

After selecting a new MicroPython revision:

1. Read the MicroPython release notes for changes to the embedding and module
   APIs.
2. Check `iot_app/micropython_config/mpconfigport.h` and
   `iot_app/cmake/micropython.cmake`.
3. Rebuild all files under `iot_app/micropython_iot_modules` against the new
   headers.
4. Check that traceback capture and C/C++ bridge error handling still work.
5. Test the default application, scheduler timers, display module, system
   module, gamepad module, external deployment, startup failure, and scheduled
   callback failure.
6. Update current-version examples in the documentation and dashboard.
7. Perform a clean CMake build and a clean Buildroot image build.

Find old version references with:

```bash
rg -n "OLD_MICROPYTHON_VERSION" iot_app
```

The current applications are sent and stored as `.py` source files. Therefore,
the MicroPython `.mpy` bytecode version is not currently a deployment
compatibility issue. Recheck this if precompiled `.mpy` applications are added
later.

### 11.6 Final checks after any submodule update

Before recording a new submodule revision:

1. Confirm all three submodule working trees are clean.
2. Run a clean local CMake build when LVGL or MicroPython changed.
3. Run a clean Buildroot image build.
4. Flash the image and boot the Raspberry Pi 4.
5. Confirm Wi-Fi, SSH, `/dev/fb0`, DRM, I²C, Mosquitto, and IoT App startup.
6. Run the default Python application and at least one external application.
7. Test both startup and scheduled-callback failure screens.
8. Record the final revisions:

```bash
git submodule status
```

The main project commit records the selected submodule commits. Anyone who
later runs `git submodule update --init --recursive` will receive those same
tested versions.

## 12. Files that implement the Buildroot image

`iot_app/buildroot_external` is a `BR2_EXTERNAL` tree. It keeps project files
outside the Buildroot submodule, which remains an unmodified upstream
dependency.

| Path | Purpose |
|---|---|
| `iot_app/buildroot_external/configs/iot_rpi4_defconfig` | Selects the Raspberry Pi 4 kernel, packages, filesystem options, and project package |
| `iot_app/buildroot_external/package/iot_app/` | Builds IoT App with Buildroot's CMake package support, creates the `iot-app` user, and installs the service and shared image-support files |
| `iot_app/buildroot_external/board/raspberrypi4/` | Provides the boot command line, filesystem overlay, partition template, and post-image processing |
| `iot_app/image_support/` | Holds helpers, device permissions, and Mosquitto configuration shared with Yocto |
| `Makefile` | Provides the short prepare, package-build, and image-build commands |

The package links IoT App with the selected cJSON, DRM, Mosquitto, OpenSSL,
LVGL, and MicroPython dependencies. Its runtime account belongs to the
`video`, `render`, `i2c`, and `input` groups so it can use the required device
files.
