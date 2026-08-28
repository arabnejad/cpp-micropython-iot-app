# Build and run the Raspberry Pi 4 image

This guide builds a complete 64-bit Linux image for a Raspberry Pi 4 Model B.
The image contains IoT App, its embedded MicroPython runtime, LVGL, Wi-Fi
support, the Mosquitto broker, and the Dropbear SSH server.

The instructions assume the repository is on an Ubuntu computer and that the
target uses a microSD card. Run all build commands from the repository root.

## 1. What the development image does

The `iot_rpi4_defconfig` configuration prepares the Pi to:

- boot without a desktop environment;
- log in as `root` automatically on the local HDMI console;
- connect to the configured Wi-Fi network when it is available;
- request IPv4 addresses for `wlan0` and `eth0` using DHCP;
- synchronize the system clock from internet NTP servers;
- accept SSH connections as `root`;
- start Mosquitto on IPv4 port 1883 for development deployments from the
  local network;
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

The development MQTT listener also accepts anonymous, unencrypted connections
from the local network. Use this only on a trusted test network. Before using
the image in production, add TLS, per-device credentials, topic ACLs, and
application-package signature verification.

The local HDMI console logs in as `root` automatically and does not ask for
this password. Automatic login applies only to `tty1`; SSH still requires the
password above.

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

This checks out the pinned Buildroot, LVGL, and MicroPython revisions. These
submodules are upstream source dependencies and should not be modified.

You can confirm their revisions with:

```bash
git submodule status
```

## 4. Add the private Wi-Fi configuration

Copy the provided example:

```bash
cp \
  iot_app/buildroot_external/board/raspberrypi4/wpa_supplicant.conf.example \
  iot_app/buildroot_external/board/raspberrypi4/wpa_supplicant.conf
```

Open the new file in an editor:

```bash
nano iot_app/buildroot_external/board/raspberrypi4/wpa_supplicant.conf
```

Replace the sample network name and password:

```conf
update_config=0
country=GB

network={
    ssid="YOUR_WIFI_NAME"
    psk="YOUR_WIFI_PASSWORD"
}
```

Set `country` to the correct two-letter country code for the location where
the Pi will operate. Save the file after replacing `YOUR_WIFI_NAME` and
`YOUR_WIFI_PASSWORD`.

Do not add a `ctrl_interface` line. This image does not enable
`wpa_supplicant`'s optional control interface because IoT App does not use
`wpa_cli`. If that line is present, `wpa_supplicant` reports it as an unknown
configuration field and does not connect.

The real `wpa_supplicant.conf` is ignored by Git. Check that it is not tracked:

```bash
git check-ignore \
  iot_app/buildroot_external/board/raspberrypi4/wpa_supplicant.conf
```

The command should print the file path. The Buildroot build stops with a clear
error if this private file is missing.

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
/opt/iot-app-builds/raspberry-pi-4
```

It stores the Buildroot configuration, cross-toolchain, compiled packages,
target filesystem, and images. These files remain available after reboot.

Set `BUILDROOT_OUTPUT` when another persistent location is preferred:

```bash
make buildroot-prepare \
  BUILDROOT_OUTPUT=/mnt/builds/raspberry-pi-4
```

The remaining commands use this persistent output directory.

## 6. Load the Raspberry Pi 4 configuration

Run:

```bash
make -C buildroot \
  BR2_EXTERNAL="$PWD/iot_app/buildroot_external" \
  O=/opt/iot-app-builds/raspberry-pi-4 \
  iot_rpi4_defconfig
```

This creates `/opt/iot-app-builds/raspberry-pi-4/.config` from the
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

## 7. Build the complete image

Run:

```bash
env -u LD_LIBRARY_PATH \
  make -C buildroot \
  BR2_EXTERNAL="$PWD/iot_app/buildroot_external" \
  O=/opt/iot-app-builds/raspberry-pi-4
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
  O=/opt/iot-app-builds/raspberry-pi-4 \
  -j"$(nproc)"
```

Without `BR2_PER_PACKAGE_DIRECTORIES=y`, Buildroot serializes top-level package
work even if `-j"$(nproc)"` is passed. A message such as `-j1 forced in
submake` during `syncconfig` is not the `LD_LIBRARY_PATH` failure.

Do not run the Buildroot build with `sudo`. Only the later microSD flashing
command needs administrator access.

When the build finishes, check the bootable image:

```bash
ls -lh /opt/iot-app-builds/raspberry-pi-4/images/sdcard.img
```

You can also record its checksum:

```bash
sha256sum /opt/iot-app-builds/raspberry-pi-4/images/sdcard.img
```

The file `sdcard.img` contains both the Raspberry Pi boot partition and the
Linux root filesystem. Do not copy `rootfs.ext4` by itself to the microSD card.

## 8. Flash the microSD card

Flashing erases the selected device. Carefully confirm the microSD device name
before continuing.

### Option A: Raspberry Pi Imager

This is the safer method:

1. Open Raspberry Pi Imager on Ubuntu.
2. Choose the Raspberry Pi 4 as the device.
3. Choose **Use custom** as the operating system.
4. Select `/opt/iot-app-builds/raspberry-pi-4/images/sdcard.img`.
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
  if=/opt/iot-app-builds/raspberry-pi-4/images/sdcard.img \
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

## 9. First boot

1. Insert the microSD card into the powered-off Raspberry Pi 4.
2. Connect the HDMI monitor.
3. Connect the I²C gamepad if it is needed for the test.
4. Power on the Pi.

The first boot should perform these operations automatically:

```text
Load Wi-Fi driver
        |
        v
Start Wi-Fi and configure Wi-Fi or Ethernet
        |
        v
Request IPv4 addresses using DHCP
        |
        v
Synchronize the system clock with NTP
        |
        v
Start Dropbear and Mosquitto
        |
        v
Open the automatic root login on the HDMI console
        |
        v
Start IoT App as the S90iot-app service
```

After networking starts, `S45time-sync` starts BusyBox `ntpd` and waits up to
30 seconds for the clock to become valid. The NTP client remains in the
background to keep the clock synchronized. Near the end of boot,
`S90iot-app` waits for the network and Mosquitto for up to 30 seconds and then
starts IoT App. It still starts in offline mode when those services are not
ready before the timeout.

## 10. Find the Raspberry Pi address

The Buildroot configuration sets the Raspberry Pi hostname to `iot-app`. When
Wi-Fi or Ethernet requests an address through DHCP, the network configuration
sends this hostname to the router:

```text
hostname $(hostname)
```

Many home routers register DHCP hostnames in their local DNS service. On those
networks, `ping iot-app` works as follows:

1. Ubuntu asks its configured DNS server for the address of `iot-app`.
2. The router finds the hostname from the Raspberry Pi's DHCP request.
3. The router returns the assigned address, such as `192.168.0.67`.
4. `ping` sends its packets to that address.

Check the name without sending ping packets:

```bash
getent hosts iot-app
```

A successful result may include the router's local search domain:

```text
192.168.0.67    iot-app.Home
```

You can then test connectivity:

```bash
ping iot-app
```

`ping` does not discover the Raspberry Pi by itself; hostname resolution must
succeed first. The current image does not include Avahi or another mDNS
service, so `iot-app.local` is not guaranteed to work. If `iot-app` does not
resolve, open the router's connected-device or DHCP-client page and find the
device named `iot-app`. A DHCP reservation or local DNS record can keep its
address stable.

## 11. Connect through SSH

Use the address assigned by the router:

```bash
ssh root@192.168.0.67
```

Replace `192.168.0.67` with the actual address. Enter this password when SSH
asks for it:

```text
root
```

After reflashing, SSH may report that the host key changed. This is expected
because the new image generates a new Dropbear host key. Remove only the old
entry for that Raspberry Pi address:

```bash
ssh-keygen -R 192.168.0.67
```

Then connect again.

## 12. Check the system and IoT App

After connecting through SSH, check the Wi-Fi interface:

```sh
ip address show wlan0
```

Check the important display and I²C devices:

```sh
ls -l /dev/fb0
ls -l /dev/dri/card*
ls -l /dev/i2c-1
```

Check the current time and the NTP process:

```sh
date
cat /var/run/ntpd.pid
ps | grep '[n]tpd'
```

If the date still shows 1970, confirm that the Pi has an IP address and DNS
works, then restart time synchronization:

```sh
ip -4 address show wlan0
nslookup 0.pool.ntp.org
/etc/init.d/S45time-sync restart
date
```

The restart command waits up to 30 seconds for the first valid time. If the
network is not ready during that period, `ntpd` stays active and continues
trying in the background. The dashboard reads the Linux system clock, so its
display corrects itself after NTP succeeds. The reference Raspberry Pi 4 image
uses the `Europe/London` timezone, including automatic GMT and BST changes. To
deploy elsewhere, change `BR2_TARGET_LOCALTIME` in `iot_rpi4_defconfig` and
make sure `BR2_TARGET_TZ_ZONELIST` contains that timezone's lowercase tzdata
source group. For example, `Europe/London` is provided by the `europe` group;
the source-group value is not the timezone name itself.

IoT App should be running after boot:

```sh
ps | grep '[i]ot_app'
```

The background service writes its C++ and MicroPython output to:

```sh
cat /var/log/iot_app.log
```

The startup script now waits briefly after launching IoT App. If the process
exits immediately because a library, device, or configuration is missing, boot
shows `FAIL` and points to this log file.

Check the installed executable and default application:

```sh
ls -l /usr/bin/iot_app
ls -l /usr/share/iot-app/default_python_application
```

BusyBox starts `/etc/init.d/S90iot-app` automatically. The shorter `iot-app`
path is an alias for managing the same service manually:

```sh
/etc/init.d/iot-app start
/etc/init.d/iot-app stop
/etc/init.d/iot-app restart
```

## 13. Manage IoT App and view its logs

Stop the background service before running the program directly for
diagnostics:

```sh
/etc/init.d/iot-app stop
/usr/bin/iot_app
```

Its logs remain visible in the console or SSH terminal. Press `Ctrl+C` to stop
it, then start the background service again:

```sh
/etc/init.d/iot-app start
```

Do not start a second copy while the service is already running. Both copies
would try to use the same framebuffer, MicroPython application, and MQTT
connection.

## 14. Useful troubleshooting commands

If Wi-Fi does not connect, use the root shell that opens automatically on the
HDMI console. Then run:

```sh
dmesg | grep -i brcm
cat /sys/class/net/wlan0/operstate
ps | grep '[w]pa_supplicant'
/etc/init.d/S30wifi restart
udhcpc -i wlan0
```

`S30wifi` starts `wpa_supplicant` and waits briefly for the Wi-Fi association.
`S40network` then requests the IPv4 address using DHCP. A successful restart
therefore looks similar to:

```text
Starting Wi-Fi: connected
```

If it reports `started, but not associated`, verify the configured SSID and
country without printing the Wi-Fi password:

```sh
grep -E '^[[:space:]]*(country|ssid)=' /etc/wpa_supplicant.conf
```

Check that the private configuration was installed:

```sh
ls -l /etc/wpa_supplicant.conf
```

Avoid printing that file while sharing logs because it contains the Wi-Fi
password.

If SSH does not start:

```sh
ps | grep '[d]ropbear'
/etc/init.d/S50dropbear restart
```

If the Ubuntu application sender reports `Connection refused`, check the
Mosquitto process and listener:

```sh
ps | grep '[m]osquitto'
netstat -lnt | grep ':1883'
cat /etc/mosquitto/mosquitto.conf
```

The development configuration should contain:

```text
listener 1883 0.0.0.0
allow_anonymous true
```

Restart the broker after changing its configuration:

```sh
/etc/init.d/S50mosquitto restart
```

The listener address should be `0.0.0.0:1883`, not `127.0.0.1:1883`. The
first address accepts connections from the Ubuntu sender; the second accepts
connections only from the Raspberry Pi itself.

If the display remains blank:

```sh
dmesg | grep -i -E 'drm|vc4|framebuffer|fb0'
ls -l /dev/fb0 /dev/dri/card*
cat /var/log/iot_app.log
/etc/init.d/iot-app stop
/usr/bin/iot_app
```

The last command keeps IoT App in the foreground so startup errors remain
visible.

## 15. Rebuild and deploy changes to IoT App

During development, an IoT App source change does not require rebuilding and
flashing the complete Buildroot image. Build only the `iot_app` package with
Buildroot's cross-compiler, copy the new executable to the Raspberry Pi, and
restart the service.

Do not copy an executable from the normal Ubuntu build directory. A file such
as `build/iot_app/iot_app` was built for the Ubuntu computer and may be an
x86-64 executable. The Raspberry Pi needs the AArch64 executable produced by
Buildroot.

### 15.1 Rebuild only the IoT App package

Buildroot does not automatically notice every change in a package that uses a
local source directory. First remove Buildroot's old copy of the IoT App
source:

```bash
env -u LD_LIBRARY_PATH \
  make -C buildroot \
  BR2_EXTERNAL="$PWD/iot_app/buildroot_external" \
  O=/opt/iot-app-builds/raspberry-pi-4 \
  iot_app-dirclean
```

Then build and install only the `iot_app` package into Buildroot's target
directory:

```bash
env -u LD_LIBRARY_PATH \
  make -C buildroot \
  BR2_EXTERNAL="$PWD/iot_app/buildroot_external" \
  O=/opt/iot-app-builds/raspberry-pi-4 \
  iot_app
```

Buildroot reuses the existing cross-toolchain and already-built dependencies.
It does not rebuild the Linux kernel or generate a new filesystem image in
this step.

Confirm that the new executable is for the Raspberry Pi:

```bash
file /opt/iot-app-builds/raspberry-pi-4/target/usr/bin/iot_app
```

The result should identify an ARM AArch64 executable. Do not deploy it if the
result says `x86-64`.

### 15.2 Copy the executable to a running Raspberry Pi

Use the Pi's address in place of `RASPBERRY_PI_IP` in these commands.
Upload the executable under a temporary name so a running executable is not
overwritten:

```bash
scp -O /opt/iot-app-builds/raspberry-pi-4/target/usr/bin/iot_app \
  root@RASPBERRY_PI_IP:/tmp/iot_app.new
```

The `-O` option asks `scp` to use its original SCP protocol. This works with
the Dropbear SSH server in the Buildroot image even when the Ubuntu `scp`
command normally prefers SFTP.

Stop the service and keep a temporary copy of the current executable:

```bash
ssh root@RASPBERRY_PI_IP \
  '/etc/init.d/iot-app stop &&
   cp -p /usr/bin/iot_app /tmp/iot_app.previous'
```

Install the new executable and start the service:

```bash
ssh root@RASPBERRY_PI_IP \
  'install -m 0755 /tmp/iot_app.new /usr/bin/iot_app &&
   /etc/init.d/iot-app start'
```

Check that the application is running:

```bash
ssh root@RASPBERRY_PI_IP "ps | grep '[i]ot_app'"
```

To see startup logs directly, stop the service and run the executable in the
SSH terminal:

```bash
ssh root@RASPBERRY_PI_IP
/etc/init.d/iot-app stop
/usr/bin/iot_app
```

Press `Ctrl+C` when the test is complete, then restore normal service
operation:

```sh
/etc/init.d/iot-app start
```

If the new executable does not work, restore the temporary backup:

```bash
ssh root@RASPBERRY_PI_IP \
  '/etc/init.d/iot-app stop &&
   install -m 0755 /tmp/iot_app.previous /usr/bin/iot_app &&
   /etc/init.d/iot-app start'
```

Files under `/tmp` disappear after a reboot, so this backup is only intended
for the current development session.

### 15.3 Deploy only a changed default Python application

Changing only the shipped default Python application does not require C++
compilation. Copy its two files to the Pi:

```bash
scp -O \
  iot_app/default_python_application/app.json \
  iot_app/default_python_application/main.py \
  root@RASPBERRY_PI_IP:/tmp/
```

Stop IoT App, install the files, and start it again:

```bash
ssh root@RASPBERRY_PI_IP \
  '/etc/init.d/iot-app stop &&
   install -m 0644 /tmp/app.json \
     /usr/share/iot-app/default_python_application/app.json &&
   install -m 0644 /tmp/main.py \
     /usr/share/iot-app/default_python_application/main.py &&
   /etc/init.d/iot-app start'
```

### 15.4 Update the final Buildroot image

The SCP workflow changes the writable filesystem on the running Raspberry Pi.
It does not update
`/opt/iot-app-builds/raspberry-pi-4/images/sdcard.img`, so those changes will
be lost the next time the old image is flashed.

Before producing or flashing a release image, rebuild the package and
regenerate the image:

```bash
env -u LD_LIBRARY_PATH \
  make -C buildroot \
  BR2_EXTERNAL="$PWD/iot_app/buildroot_external" \
  O=/opt/iot-app-builds/raspberry-pi-4 \
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
  O=/opt/iot-app-builds/raspberry-pi-4 \
  clean

env -u LD_LIBRARY_PATH \
  make -C buildroot \
  BR2_EXTERNAL="$PWD/iot_app/buildroot_external" \
  O=/opt/iot-app-builds/raspberry-pi-4 \
  iot_rpi4_defconfig

env -u LD_LIBRARY_PATH \
  make -C buildroot \
  BR2_EXTERNAL="$PWD/iot_app/buildroot_external" \
  O=/opt/iot-app-builds/raspberry-pi-4
```

## 16. Update the pinned submodules

Buildroot, LVGL, and MicroPython are separate Git repositories stored as
submodules. This repository records one exact commit for each dependency, so
another checkout uses the same source versions.

This command does not look for newer versions:

```bash
git submodule update --init --recursive
```

It only checks out the revisions already recorded by the main project. Updating
a dependency requires selecting and testing a new tag or commit.

### 16.1 Check the current revisions

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
treats all three submodules as read-only upstream dependencies.

Update one dependency at a time. If the build then fails, it is much easier to
tell which update caused it.

### 16.2 Select a new revision

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

### 16.3 Checks required after updating Buildroot

The Buildroot update can change package names, configuration symbols,
toolchains, board scripts, firmware, and the Linux kernel selected for the
Raspberry Pi. Compare the old and new built-in Raspberry Pi configurations:

```bash
git -C buildroot diff \
  OLD_BUILDROOT_COMMIT \
  NEW_BUILDROOT_COMMIT \
  -- \
  configs/raspberrypi4_64_defconfig \
  configs/raspberrypicm4io_64_defconfig \
  board/raspberrypi4-64 \
  board/raspberrypicm4io-64 \
  board/raspberrypi/patches/linux/linux.hash
```

Review that output and update the project-owned `iot_rpi4_defconfig` and
`iot_cm4_defconfig` where needed. In particular, check:

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
3. Update `iot_cm4_defconfig` from the matching built-in CM4 configuration if
   CM4 support is still required.
4. Confirm the new archive filename has a SHA-256 entry in
   `board/raspberrypi/patches/linux/linux.hash`.
5. Do a clean build.

Do not replace the Linux commit with the Buildroot submodule commit. If the old
Linux version is intentionally retained, its matching hash must also be kept
in a project-owned hash location. A newer Buildroot release may remove the old
archive entry from its built-in `linux.hash` file.

Load the project configuration through the new Buildroot version:

```bash
make -C buildroot \
  BR2_EXTERNAL="$PWD/iot_app/buildroot_external" \
  O=/opt/iot-app-builds/raspberry-pi-4 \
  iot_rpi4_defconfig
```

Buildroot can silently drop a configuration symbol that no longer exists.
Create a normalized copy and compare it with the project defconfig:

```bash
make -C buildroot \
  BR2_EXTERNAL="$PWD/iot_app/buildroot_external" \
  O=/opt/iot-app-builds/raspberry-pi-4 \
  BR2_DEFCONFIG=/tmp/iot_rpi4_saved_defconfig \
  savedefconfig

diff -u \
  iot_app/buildroot_external/configs/iot_rpi4_defconfig \
  /tmp/iot_rpi4_saved_defconfig
```

Some differences only remove settings that now have the same default value.
Review every difference instead of copying the generated file automatically.

Finally, perform the clean build shown in section 15. A Buildroot update must
not reuse packages or a toolchain built by the previous version.

### 16.4 Checks required after updating LVGL

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

### 16.5 Checks required after updating MicroPython

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

### 16.6 Final checks after any submodule update

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
