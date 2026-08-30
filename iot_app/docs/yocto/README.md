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
Linux password hash. Generate one on the Ubuntu build computer with:

```bash
openssl passwd -6
```

The command asks for the password without putting it in the shell history. Its
output has this form:

```text
$6$random-salt$password-hash
```

The `$6$` prefix selects the SHA-512 `crypt` password format. The middle part
is a random salt, and the final part is the calculated password hash. The salt
means that generating the same password twice can produce different text;
both results still accept the same password.

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

The checked-in development hash for the password `root` can be reproduced
with:

```bash
openssl passwd -6 -salt iot-app root
```

Use the interactive command for a real password. Supplying a password on the
command line can leave it in the shell history and briefly expose it through
the process list. A password hash also does not make a weak password safe in a
public repository. Production images should normally use SSH keys and disable
password-based root login.

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

Open the shared private configuration created by the preparation command:

```bash
nano wpa_supplicant.conf
```

Replace the example values:

```conf
update_config=0
country=GB

network={
    ssid="YOUR_WIFI_NAME"
    psk="YOUR_WIFI_PASSWORD"
}
```

Set `country` to the correct two-letter wireless country code. Git ignores the
private file, and `make wifi-prepare` gives it mode `0600`. Buildroot and Yocto
both use this file, so the network details only need to be maintained once.

Check its permissions without printing the password:

```bash
stat -c '%a %U:%G %n' wpa_supplicant.conf
```

The first value should be `600`.

`make yocto-prepare` copies this file to
`/opt/iot-app-builds/yocto-raspberry-pi-4/build/conf/wpa_supplicant.conf` for
BitBake. Do not edit that generated copy because the next preparation command
replaces it.

`make yocto-image` stops before BitBake starts if the shared file still
contains `YOUR_WIFI_NAME` or `YOUR_WIFI_PASSWORD`. This avoids accidentally
building an image with the example network settings. The current development
workflow requires a completed file even when the device will normally use
Ethernet.

### Optional passwordless SSH access

To include your SSH public key in the image, copy it to the optional
root-level file. If you already have a key that you want to use, run:

```bash
cp ~/.ssh/id_ed25519.pub ssh_authorized_keys
```

Replace the source path if your public key has a different name. If you have
several keys and prefer a separate one for IoT App, create it and copy its
public half:

```bash
ssh-keygen -t ed25519 -f ~/.ssh/id_ed25519_iot_app
cp ~/.ssh/id_ed25519_iot_app.pub ssh_authorized_keys
```

Never copy a private key into the repository. The `ssh_authorized_keys` file
may contain several public keys, with one key on each line.

When several private keys exist on the Ubuntu computer, tell SSH exactly which
one belongs to this device. Add this entry to `~/.ssh/config`:

```sshconfig
Host rspi-iot-app.local
    User root
    IdentityFile ~/.ssh/id_ed25519_iot_app
    IdentitiesOnly yes
```

Change `IdentityFile` if you selected a different existing key. You can then
connect without specifying the user or key on every command:

```bash
ssh rspi-iot-app.local
```

`make yocto-prepare` checks whether `ssh_authorized_keys` exists and is not
empty. It copies a non-empty file into the private Yocto build configuration,
and the image installs those keys for the root account. When the file is
missing or empty, no authorized key is installed and the `root` password
remains available.

The personal `ssh_authorized_keys` file is ignored by Git. It is not the same
as `known_hosts`: `authorized_keys` controls who may log in to the Pi, while
`known_hosts` belongs to the SSH client and records which server it connected
to.

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

## 12. First boot

Connect HDMI and power, then boot the Raspberry Pi. The first startup can take
longer while systemd initializes the machine and SSH creates host keys.

Several services start at the same time. The relevant order is:

```text
Kernel and systemd start
        |
        +--> Create /dev/fb0
        +--> Start Wi-Fi and Ethernet; request DHCP addresses
        +--> Start Mosquitto
        +--> Start time synchronization
        |
        v
Start IoT App when /dev/fb0 is available
        |
        v
Default Python dashboard appears on /dev/fb0
```

The systemd service requires `/dev/fb0`, so it starts as soon as the
framebuffer device is available. It does not wait for an IPv4 address or a
working MQTT connection. IoT App uses the active 1920x1080 framebuffer and
does not change the monitor resolution. Raspberry Pi documents the `video=`
syntax used by this image in its
[KMS command-line guide](https://www.raspberrypi.com/documentation/computers/configuration.html#set-the-kms-display-mode).

If networking is unavailable, the dashboard starts in offline mode. Its
network panel refreshes after startup, and the MQTT receiver reconnects when
the broker becomes reachable.

## 13. Find the Raspberry Pi address

The image uses `rspi-iot-app` as its hostname and runs Avahi. Avahi advertises
this mDNS address on the local network:

```bash
getent hosts rspi-iot-app.local
ping rspi-iot-app.local
```

The `.local` suffix tells Ubuntu to use mDNS. The name keeps working when DHCP
assigns a different IP address. Some routers may also resolve the shorter
`rspi-iot-app` hostname, but that behaviour depends on the router.

Other ways to find the address are:

- open the router's DHCP client list and look for `rspi-iot-app`;
- connect a keyboard, stop IoT App if necessary, and run `ip -4 address`; or
- scan the local subnet from Ubuntu with a network tool you already trust.

When a shell is available on the Pi, run:

```sh
ip -4 address show
hostname -I
```

Ignore `127.0.0.1`; it is the local loopback address and cannot be reached from
Ubuntu.

## 14. Connect through SSH

The preferred SSH command uses mDNS:

```bash
ssh root@rspi-iot-app.local
```

When the image contains your public key, SSH uses the matching private key
from the Ubuntu computer and does not ask for a password. Otherwise, the
development password is:

```text
root
```

After reflashing, the image creates a new SSH host key. If SSH reports that the
stored key has changed, remove only the old entry for this device and connect
again:

```bash
ssh-keygen -R rspi-iot-app.local
```

### Use the emergency console without a network

The image keeps a normal login on `tty2`. Connect a keyboard and press
`Alt+F2`. Some keyboards require `Ctrl+Alt+F2` instead. Log in as `root` with
the development password.

If IoT App makes the emergency terminal difficult to read, stop it first:

```sh
systemctl stop iot-app
```

After finishing the repair, restart IoT App and switch back to its terminal:

```sh
systemctl start iot-app
chvt 1
```

## 15. Check the system services

Check the main services:

```sh
systemctl status iot-app --no-pager
systemctl status iot-app-wifi --no-pager
systemctl status avahi-daemon --no-pager
systemctl status systemd-networkd --no-pager
systemctl status systemd-timesyncd --no-pager
systemctl status mosquitto --no-pager
systemctl status sshd.socket --no-pager
```

Follow IoT App logs:

```sh
journalctl -u iot-app -f
```

Show logs from the current boot:

```sh
journalctl -b -u iot-app --no-pager
```

Stop, start, or restart the application:

```sh
systemctl stop iot-app
systemctl start iot-app
systemctl restart iot-app
```

Stopping the service stops dashboard updates, but it does not start a shell on
`tty1`. Use SSH or the `tty2` emergency console for command-line work. Pressing
`Ctrl+C` in an SSH terminal does not stop the background service.

## 16. Check the display and I2C

Check the framebuffer:

```sh
ls -l /dev/fb0
cat /sys/class/graphics/fb0/virtual_size
cat /sys/class/graphics/fb0/bits_per_pixel
```

The expected size is:

```text
1920,1080
```

Check the I2C bus and scan it:

```sh
ls -l /dev/i2c-1
i2cdetect -y 1
```

The Adafruit Mini I2C gamepad normally appears as `50`.

The `iot-app` account belongs to the `video`, `render`, `i2c`, and `input`
groups. Check it with:

```sh
id iot-app
```

## 17. Check time synchronization

Raspberry Pi 4 does not have a battery-backed real-time clock by default. The
image uses `systemd-timesyncd` after the network comes up.

Check it with:

```sh
timedatectl status
systemctl status systemd-timesyncd --no-pager
journalctl -b -u systemd-timesyncd --no-pager
```

The configured time zone is `Europe/London`.

## 18. Check MQTT deployment

Confirm that Mosquitto listens on every IPv4 interface:

```sh
ss -lntp | grep ':1883'
```

The expected local address is:

```text
0.0.0.0:1883
```

From Ubuntu, test the port:

```bash
nc -vz rspi-iot-app.local 1883
```

Use `rspi-iot-app.local` as the broker host in
`iot_app_sender/sender_config.json`, then send a sample application as
described in the
[sender guide](../../../iot_app_sender/README.md).

## 19. Deploy a rebuilt application without flashing

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

## 20. Rebuild the image after source changes

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

## 21. Change generated configuration

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

## 22. Update the Yocto submodules

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

## 23. Troubleshooting

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

### Wi-Fi does not connect

On the Raspberry Pi, run:

```sh
systemctl status iot-app-wifi --no-pager
journalctl -b -u iot-app-wifi --no-pager
ip link show wlan0
ip -4 address show wlan0
```

Check the country code, network name, password, signal, and power supply. The
private configuration used by both image builders is:

```text
wpa_supplicant.conf
```

After correcting it, rebuild and reflash the image.

If `wlan0` does not exist at all, check the driver before checking the Wi-Fi
name or password:

```sh
modprobe brcmfmac
lsmod | grep -E 'brcmfmac|brcmutil|cfg80211|rfkill'
ls -la /sys/bus/sdio/devices/
dmesg | grep -Ei 'brcm|brcmfmac|mmc|sdio|firmware|cfg80211'
```

The Raspberry Pi 4 used during development reported this error when the main
driver was present but its small WCC vendor module was missing:

```text
brcmf_fwvid_request_module: mod=wcc: failed 256
brcmf_fwvid_attach failed
```

The image recipe installs both `kernel-module-brcmfmac` and
`kernel-module-brcmfmac-wcc`. It also loads `brcmfmac` during boot. Rebuild and
reflash an older image if it shows the error above.

### SSH connection is refused

Check the target from its local console:

```sh
systemctl status sshd.socket --no-pager
ss -lntp | grep ':22'
ip -4 address
```

Also confirm that the Ubuntu computer can resolve `rspi-iot-app.local`.

The Yocto image contains the `root` login and the non-login `iot-app` service
account. It does not copy user accounts from Raspberry Pi OS. Connect with:

```sh
ssh root@rspi-iot-app.local
```

If mDNS is unavailable, use the current address shown by `ip -4 address` as a
temporary fallback.

If an older image rejects the documented `root` password, use the automatic
root console login to replace it:

```sh
passwd root
```

Enter `root` twice. The current image recipe escapes the password hash
correctly, so newly generated images do not need this repair.

### MQTT connection is refused

Check the broker:

```sh
systemctl status mosquitto --no-pager
journalctl -b -u mosquitto --no-pager
ss -lntp | grep ':1883'
```

The project configuration should produce `0.0.0.0:1883`, not only
`127.0.0.1:1883`.

### IoT App does not start

Check the framebuffer and service:

```sh
ls -l /dev/fb0
systemctl status iot-app --no-pager
journalctl -b -u iot-app --no-pager
```

Check the runtime account:

```sh
id iot-app
ls -l /dev/fb0 /dev/dri /dev/i2c-1
```

The application needs `/dev/fb0` to draw the screen. The systemd service
therefore requires `dev-fb0.device`; it does not wait for network access or an
MQTT connection. If `/dev/fb0` never appears, inspect the device unit, kernel
log, and Raspberry Pi KMS configuration:

```sh
systemctl status dev-fb0.device --no-pager
dmesg | grep -Ei 'drm|vc4|framebuffer|fb0'
cat /proc/cmdline
```

If `/usr/bin/iot_app` runs successfully from the terminal but the service does
not start automatically, first check whether the launcher is selecting a
development executable:

```sh
ls -l /data/iot-app/development/iot_app
journalctl -b -u iot-app --no-pager
```

Remove a broken development executable to return to the installed copy. If no
override is active, check that the service belongs to the normal boot target:

```sh
systemctl get-default
systemctl is-active multi-user.target
systemctl is-enabled iot-app.service
ls -l /etc/systemd/system/multi-user.target.wants/iot-app.service
journalctl -b -u iot-app --no-pager
```

The expected result is an enabled service and a link from
`multi-user.target.wants` to `iot-app.service`.

### IoT App starts later than it does on Buildroot

Buildroot starts the `S90iot-app` script near the end of its sequential boot
process. The script waits for `/dev/fb0` only when that device has not appeared
yet, with a maximum wait of 10 seconds.

Yocto lets systemd start independent services in parallel. Its IoT App unit
requires `dev-fb0.device` and is ordered after storage and Mosquitto, but it
does not wait for Wi-Fi, DHCP, or `network-online.target`. A missing network
address therefore does not delay the dashboard. The MQTT client connects
through `127.0.0.1` and reconnects if the local broker is not ready.

The [systemd network-target notes](https://systemd.io/NETWORK_ONLINE/)
explain why normal services should not wait for `network-online.target` unless
they cannot work without a configured network.

If startup is still delayed, use these commands to see which required or
ordered unit took time:

```sh
systemd-analyze critical-chain iot-app.service
journalctl -b -o short-monotonic \
  -u iot-app-wifi.service \
  -u systemd-networkd.service \
  -u mosquitto.service \
  -u iot-app.service
```

### Clock starts near 1970

Check network access and time synchronization:

```sh
timedatectl status
systemctl restart systemd-timesyncd
journalctl -b -u systemd-timesyncd --no-pager
```

The clock cannot become correct until the Raspberry Pi reaches an NTP server,
unless an external real-time clock has been fitted.

### Kernel messages appear over the dashboard

LVGL and the Linux console both use `/dev/fb0`. A kernel message printed on
the console can therefore appear over the dashboard.

The image adds `quiet loglevel=4` to the kernel command line. This keeps normal
kernel status messages off the display. Serious kernel messages may still
appear. All messages remain available in the kernel log; inspect it over SSH
with:

```sh
dmesg
journalctl -k
```

On an older image, apply the same console filter until the next reboot with:

```sh
dmesg -n 4
```

This command changes what is printed on the console. It does not delete the
kernel messages.

The [Linux kernel parameter reference](https://docs.kernel.org/admin-guide/kernel-parameters.html)
documents `quiet` and `loglevel=`. The kernel's
[`printk` guide](https://docs.kernel.org/core-api/printk-basics.html)
explains how the console log level decides which messages appear on screen.

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

## 24. Files that implement the Yocto image

| Path | Purpose |
|---|---|
| `meta-iot-app/conf/layer.conf` | Registers the project layer and local source root |
| `meta-iot-app/conf/distro/iot-app-linux.conf` | Selects Poky policy and systemd |
| `meta-iot-app/conf/templates/raspberrypi4-64/` | Creates `local.conf` and `bblayers.conf` |
| `meta-iot-app/recipes-iot/iot-app/` | Builds and installs the C++ runtime and service |
| `meta-iot-app/recipes-core/iot-app-system-config/` | Adds network units, Wi-Fi startup, time services, and console login |
| `meta-iot-app/recipes-connectivity/` | Applies the development Mosquitto and private Wi-Fi configurations |
| `meta-iot-app/recipes-core/images/iot-app-image.bb` | Selects packages and creates the bootable image |
| `Makefile` | Creates persistent paths and provides the short build commands |
| `scripts/build/prepare-yocto.sh` | Creates the build directories and generated configuration files |

The upstream Yocto layers are read-only dependencies. All project-specific
behaviour remains in `meta-iot-app`.
