# Device image setup and troubleshooting

This guide covers tasks that are the same for the IoT App Buildroot and Yocto
images. Use it after building and flashing either image.

For commands used to create an image, see the [Buildroot guide](../buildroot/README.md)
or the [Yocto guide](../yocto/README.md).

## 1. Development image defaults

Both images use these defaults:

```text
Hostname: rspi-iot-app
mDNS name: rspi-iot-app.local
SSH user: root
SSH password: root
MQTT port: 1883
```

The simple password and anonymous MQTT listener are intended for testing on a
trusted local network. Before using the image elsewhere, disable password login
and configure SSH keys, MQTT TLS, separate credentials, topic permissions, and
signed application packages.

### Change the development password

Generate a Linux password hash on the Ubuntu build computer:

```bash
openssl passwd -6
```

The command asks for the password without placing it in shell history. Its
output has this form:

```text
$6$random-salt$password-hash
```

The `$6$` prefix selects the SHA-512 `crypt` format. The salt is random, so the
same password can produce different hashes. Do not pass the password directly
on the command line because it may appear in shell history or the process list.

Buildroot and Yocto store the generated hash differently. Follow the
[Buildroot instructions](../buildroot/README.md#change-the-root-password) or
[Yocto instructions](../yocto/README.md#change-the-root-password) after
generating it.

For reference, this command reproduces the checked-in Yocto development hash
for the password `root`:

```bash
openssl passwd -6 -salt iot-app root
```

Use the interactive command for a real password. The command above is only for
checking where the existing development hash came from.

## 2. Configure Wi-Fi

Buildroot and Yocto use the same private file at the repository root. Create it
with:

```bash
make wifi-prepare
```

Edit `wpa_supplicant.conf` and replace the example values:

```conf
update_config=0
country=GB

network={
    ssid="YOUR_WIFI_NAME"
    psk="YOUR_WIFI_PASSWORD"
}
```

Set `country` to the correct two-letter country code. Do not add a
`ctrl_interface` entry; the images do not enable the optional control interface
because IoT App does not use `wpa_cli`.

The private file is excluded from Git. Check this without printing its
contents:

```bash
git check-ignore wpa_supplicant.conf
stat -c '%a %U:%G %n' wpa_supplicant.conf
```

The expected file mode is `600`. Both image commands stop before building if
the file still contains the example network name or password.
This check also applies when the Raspberry Pi will normally use Ethernet.

## 3. Add an SSH public key

The optional root-level `ssh_authorized_keys` file is installed in both images
when it exists and is not empty. Copy an existing public key:

```bash
cp ~/.ssh/id_ed25519.pub ssh_authorized_keys
```

To create a separate key for the Raspberry Pi instead:

```bash
ssh-keygen -t ed25519 -f ~/.ssh/id_ed25519_iot_app
cp ~/.ssh/id_ed25519_iot_app.pub ssh_authorized_keys
```

Never copy a private key into the repository. The file may contain several
public keys, one per line, and is excluded from Git.

When the Ubuntu computer has several private keys, add this entry to
`~/.ssh/config`:

```sshconfig
Host rspi-iot-app.local
    User root
    IdentityFile ~/.ssh/id_ed25519_iot_app
    IdentitiesOnly yes
```

Change `IdentityFile` if a different key was copied. You can then connect with:

```bash
ssh rspi-iot-app.local
```

`authorized_keys` controls who can log in to the Raspberry Pi. It is different
from the Ubuntu computer's `known_hosts` file, which records the identity of
servers contacted in the past.

## 4. First boot

Insert the microSD card, connect HDMI, and power on the Raspberry Pi. Connect
the I2C gamepad before boot when it is needed for the test. Both images perform
the same main jobs:

```text
Linux starts
    |
    +--> Prepare and mount /data
    +--> Start Wi-Fi and Ethernet
    +--> Start SSH, time synchronization, and Mosquitto
    +--> Create /dev/fb0
             |
             v
        Start IoT App
             |
             v
        Show the default dashboard
```

The exact service order differs. Buildroot runs BusyBox/SysV startup scripts
one after another. The relevant scripts prepare `/data`, start Wi-Fi and DHCP,
start time synchronization, start Dropbear and Mosquitto, and finally start
IoT App. Their names show that order: `S25data-storage`, `S30wifi`,
`S40network`, `S45time-sync`, the two `S50` services, and `S90iot-app`.
`S45time-sync` starts `ntpd`, waits up to 30 seconds for the first valid time,
and leaves it running in the background. `S90iot-app` waits up to 10 seconds
for `/dev/fb0` before starting IoT App.

Yocto starts independent systemd services in parallel. `iot-app.service` is
ordered after the storage and Mosquitto services and requires
`dev-fb0.device`. It does not wait for `network-online.target`.

Neither image waits for an IP address before starting IoT App. The dashboard
can start offline, and its MQTT client reconnects when the local broker becomes
available.

The first Yocto boot can take longer while systemd initializes the device and
SSH creates its host keys.

The image reserves `tty1` for the framebuffer dashboard. It hides the terminal
cursor and does not run a login prompt there, so keyboard input is not printed
over the dashboard. A normal emergency login remains available on `tty2`.

## 5. Find the Raspberry Pi

The hostname is `rspi-iot-app`. Avahi advertises it through multicast DNS, or
mDNS, as:

```text
rspi-iot-app.local
```

Check it from Ubuntu:

```bash
getent hosts rspi-iot-app.local
ping rspi-iot-app.local
```

The `.local` name keeps working when DHCP assigns a different IP address. The
DHCP request also includes the `rspi-iot-app` hostname, so some routers resolve
this shorter name:

```bash
ping rspi-iot-app
```

The shorter name depends on the router and is less reliable than the `.local`
name.

If mDNS does not work, look for `rspi-iot-app` in the router's DHCP-client
page. From a Raspberry Pi console, use:

```sh
ip -4 address show
hostname -I
```

Ignore `127.0.0.1`; it is the device's internal loopback address.

## 6. Connect through SSH

Use:

```bash
ssh root@rspi-iot-app.local
```

SSH uses the matching private key when one was added to the image. Otherwise,
enter the development password `root`.

Reflashing creates a new SSH server key. If SSH warns that the stored key has
changed, remove only the old entry for this device and connect again:

```bash
ssh-keygen -R rspi-iot-app.local
```

### Emergency console without networking

Connect a keyboard and press `Alt+F2` or `Ctrl+Alt+F2`. Log in on `tty2` as
`root` with the development password.

Use the command for the running image to stop IoT App while working on the
console:

| Image | Stop IoT App | Start IoT App |
|---|---|---|
| Buildroot | `/etc/init.d/iot-app stop` | `/etc/init.d/iot-app start` |
| Yocto | `systemctl stop iot-app` | `systemctl start iot-app` |

Run `chvt 1` to return to the dashboard terminal.

## 7. Manage IoT App and read its logs

Buildroot commands:

```sh
/etc/init.d/iot-app start
/etc/init.d/iot-app stop
/etc/init.d/iot-app restart
cat /var/log/iot_app.log
```

Yocto commands:

```sh
systemctl start iot-app
systemctl stop iot-app
systemctl restart iot-app
systemctl status iot-app --no-pager
journalctl -b -u iot-app --no-pager
journalctl -u iot-app -f
```

Check all important Yocto services with:

```sh
systemctl status iot-app --no-pager
systemctl status iot-app-wifi --no-pager
systemctl status avahi-daemon --no-pager
systemctl status systemd-networkd --no-pager
systemctl status systemd-timesyncd --no-pager
systemctl status mosquitto --no-pager
systemctl status sshd.socket --no-pager
```

On Buildroot, check the main background processes with:

```sh
ps | grep '[i]ot_app'
ps | grep '[w]pa_supplicant'
ps | grep '[n]tpd'
ps | grep '[d]ropbear'
ps | grep '[m]osquitto'
```

The Buildroot startup script waits one second after launching IoT App and
checks that the process is still running. An immediate startup failure is
reported as `FAIL (see /var/log/iot_app.log)`.

Do not start a second copy while the service is running. Both processes would
try to use the same framebuffer, Python application, and MQTT client identity.
Stopping the service does not start a shell on `tty1`, and pressing `Ctrl+C` in
an SSH terminal does not stop a service running in the background.

## 8. Check the device

Check networking:

```sh
ip -4 address show
ip route
```

Check the display and I2C devices:

```sh
ls -l /dev/fb0
ls -l /dev/dri/card*
ls -l /dev/i2c-1
```

The expected framebuffer size is `1920,1080`. On Yocto it can be read with:

```sh
cat /sys/class/graphics/fb0/virtual_size
cat /sys/class/graphics/fb0/bits_per_pixel
```

The images request this mode with the Linux `video=` kernel argument. Raspberry
Pi documents that syntax in its [KMS command-line guide](https://www.raspberrypi.com/documentation/computers/configuration.html#set-the-kms-display-mode).

The Yocto image also includes `i2c-tools`, so it can scan bus 1:

```sh
i2cdetect -y 1
```

The Adafruit Mini I2C gamepad normally appears as `50`. The current Buildroot
image does not install `i2cdetect`; the existence and permissions of
`/dev/i2c-1` can still be checked there.

Check the runtime user's device groups with:

```sh
id iot-app
```

The expected groups include `video`, `render`, `i2c`, and `input`.

Check the installed runtime and default Python application with:

```sh
ls -l /usr/bin/iot_app
ls -l /usr/share/iot-app/default_python_application
```

Check the persistent partitions after the first boot:

```sh
df -h / /data

# Yocto
lsblk -o NAME,SIZE,FSTYPE,LABEL,MOUNTPOINTS

# Buildroot
cat /proc/partitions
grep ' /data ' /proc/mounts
```

The [storage guide](../storage/README.md) explains the partition layout and
first-boot expansion.

## 9. Check MQTT deployment

The Mosquitto listener must accept connections through the Raspberry Pi's
network interfaces. Check it with:

```sh
ss -lntp 2>/dev/null | grep ':1883' || netstat -lnt | grep ':1883'
```

The expected address is:

```text
0.0.0.0:1883
```

From Ubuntu, check the port before running the sender:

```bash
nc -vz rspi-iot-app.local 1883
```

Use `rspi-iot-app.local` as the broker host in
`iot_app_sender/sender_config.json`. The [sender guide](../../../iot_app_sender/README.md)
contains the deployment commands and explains the status replies.

## 10. Troubleshooting

### Wi-Fi does not connect

First check whether `wlan0` exists:

```sh
ip link show wlan0
ip -4 address show wlan0
```

Check the Wi-Fi service for the running image:

```sh
# Buildroot
dmesg | grep -i brcm
cat /sys/class/net/wlan0/operstate
ps | grep '[w]pa_supplicant'
/etc/init.d/S30wifi restart
udhcpc -i wlan0
ls -l /etc/wpa_supplicant.conf

# Yocto
systemctl status iot-app-wifi --no-pager
journalctl -b -u iot-app-wifi --no-pager
```

On Buildroot, `S30wifi` starts `wpa_supplicant` and waits briefly for
association. `S40network` then requests an address using DHCP. A successful
Wi-Fi restart reports `Starting Wi-Fi: connected`. If it reports `started, but
not associated`, check the network name, country, password, signal, and power
supply.

Verify the country and network name without displaying the password:

```sh
grep -E '^[[:space:]]*(country|ssid)=' /etc/wpa_supplicant.conf
```

Do not print the whole file when sharing logs because it contains the Wi-Fi
password.

If `wlan0` does not exist, inspect the driver and firmware:

```sh
modprobe brcmfmac
lsmod | grep -E 'brcmfmac|brcmutil|cfg80211|rfkill'
ls -la /sys/bus/sdio/devices/
dmesg | grep -Ei 'brcm|brcmfmac|mmc|sdio|firmware|cfg80211'
```

The Yocto image includes both `kernel-module-brcmfmac` and
`kernel-module-brcmfmac-wcc`. An older image may report:

```text
brcmf_fwvid_request_module: mod=wcc: failed 256
brcmf_fwvid_attach failed
```

That image must be rebuilt and reflashed.

### SSH is refused

Confirm that the device has an address and that port 22 is listening:

```sh
ip -4 address show

# Buildroot
ps | grep '[d]ropbear'
/etc/init.d/S50dropbear restart

# Yocto
systemctl status sshd.socket --no-pager
ss -lntp | grep ':22'
```

The images contain the `root` login. They do not copy Ubuntu or Raspberry Pi OS
user accounts into the image.

An older Yocto image that rejects the documented development password can be
repaired from its automatic root console with:

```sh
passwd root
```

Enter `root` twice. Current images contain the corrected password hash and do
not need this repair.

### MQTT connection is refused

Check the broker and listener:

```sh
# Buildroot
ps | grep '[m]osquitto'
netstat -lnt | grep ':1883'
cat /etc/mosquitto/mosquitto.conf
/etc/init.d/S50mosquitto restart

# Yocto
systemctl status mosquitto --no-pager
journalctl -b -u mosquitto --no-pager
ss -lntp | grep ':1883'
```

The listener must be `0.0.0.0:1883`, not `127.0.0.1:1883`. The first address
accepts connections from Ubuntu; the second accepts connections only from the
Raspberry Pi itself.

The development configuration contains:

```text
listener 1883 0.0.0.0
allow_anonymous true
```

### IoT App does not start

Check `/dev/fb0`, the runtime account, and the application log:

```sh
ls -l /dev/fb0 /dev/dri /dev/i2c-1
id iot-app

# Buildroot
cat /var/log/iot_app.log
/etc/init.d/iot-app restart

# Yocto
systemctl status iot-app --no-pager
journalctl -b -u iot-app --no-pager
systemctl status dev-fb0.device --no-pager
```

If the framebuffer is missing, inspect the kernel display messages:

```sh
dmesg | grep -Ei 'drm|vc4|framebuffer|fb0'
cat /proc/cmdline
```

Also check whether the launcher selected a development executable:

```sh
ls -l /data/iot-app/development/iot_app
```

Remove a broken development executable and restart the service to return to
the installed `/usr/bin/iot_app`.

If the Yocto executable works when started manually but its service is not
started during boot, check the boot target and enablement:

```sh
systemctl get-default
systemctl is-active multi-user.target
systemctl is-enabled iot-app.service
ls -l /etc/systemd/system/multi-user.target.wants/iot-app.service
journalctl -b -u iot-app --no-pager
```

To keep startup errors in the terminal, stop the background service and run
the installed program directly:

```sh
# Buildroot
/etc/init.d/iot-app stop
/usr/bin/iot_app

# Yocto
systemctl stop iot-app
/usr/bin/iot_app
```

Press `Ctrl+C` after the test, then start the normal service again.

### The clock starts near 1970

The Raspberry Pi 4 has no battery-backed real-time clock by default. It needs
network access to reach an NTP server.

Buildroot checks:

```sh
date
cat /var/run/ntpd.pid
ps | grep '[n]tpd'
nslookup 0.pool.ntp.org
/etc/init.d/S45time-sync restart
date
```

Yocto checks:

```sh
timedatectl status
systemctl status systemd-timesyncd --no-pager
systemctl restart systemd-timesyncd
journalctl -b -u systemd-timesyncd --no-pager
```

The dashboard reads the Linux clock and corrects itself after synchronization
succeeds. Both project images currently use the `Europe/London` timezone.
Buildroot sets it with `BR2_TARGET_LOCALTIME` and
`BR2_TARGET_TZ_ZONELIST` in `iot_rpi4_defconfig`. Yocto sets it with
`DEFAULT_TIMEZONE` in
`meta-iot-app/conf/templates/raspberrypi4-64/local.conf.sample`.

### Kernel text appears over the dashboard

LVGL and the Linux console both use `/dev/fb0`. A serious kernel message can
therefore appear over the dashboard even though the images use the `quiet` and
`loglevel=4` kernel arguments.

The messages remain in the log:

```sh
dmesg
journalctl -k 2>/dev/null || true
```

On an older image, reduce console output until the next reboot with:

```sh
dmesg -n 4
```

This changes what the kernel prints on the console. It does not delete any log
messages. See the Linux kernel [`quiet` and `loglevel=` parameters](https://docs.kernel.org/admin-guide/kernel-parameters.html)
and the [`printk` guide](https://docs.kernel.org/core-api/printk-basics.html).

### `/data` is missing or has not expanded

Check the third partition and storage service:

```sh
# Yocto
lsblk -o NAME,SIZE,FSTYPE,LABEL,MOUNTPOINTS

# Buildroot
cat /proc/partitions
grep ' /data ' /proc/mounts

# Buildroot
/etc/init.d/S25data-storage restart

# Yocto
systemctl status iot-app-storage --no-pager
journalctl -b -u iot-app-storage --no-pager
```

Partition 3 must have the `iot-data` label. The storage helper refuses to
resize an unexpected partition. See the [storage guide](../storage/README.md)
for the complete checks.

### Yocto starts IoT App later than Buildroot

Buildroot runs startup scripts one after another. Yocto starts independent
systemd services in parallel and starts IoT App when `/dev/fb0` exists. The
first Yocto boot can also spend time generating SSH host keys.

Check the systemd timing when the delay is unexpected:

```sh
systemd-analyze critical-chain iot-app.service
journalctl -b -o short-monotonic \
  -u iot-app-wifi.service \
  -u systemd-networkd.service \
  -u mosquitto.service \
  -u iot-app.service
```

The [systemd network-target notes](https://systemd.io/NETWORK_ONLINE/) explain
why IoT App does not wait for `network-online.target`.
