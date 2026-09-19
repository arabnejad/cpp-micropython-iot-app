# Shared image support files

Buildroot and Yocto use different package and service formats, but several
installed files must behave in the same way. This directory keeps one copy of
those files.

| File | Installed location | Purpose |
|---|---|---|
| `iot-app-launcher` | `/usr/libexec/iot-app-launcher` | Runs the development executable from a mounted `/data`; otherwise runs `/usr/bin/iot_app` |
| `iot-app-prepare-data-storage` | `/usr/libexec/iot-app-prepare-data-storage` | Prepares the optional persistent `/data` partition when it exists |
| `iot-app-hide-tty1-cursor` | `/usr/libexec/iot-app-hide-tty1-cursor` | Hides the terminal cursor before the framebuffer dashboard starts |
| `iot-app-check-video-playback` | `/usr/bin/iot-app-check-video-playback` | Tests the Raspberry Pi video path and reports hardware decoding and dropped frames |
| `mosquitto.conf` | `/etc/mosquitto/mosquitto.conf` | Opens the development MQTT listener used by the sender |

The Buildroot package and Yocto recipes install these files into their images.
Change a file here so the next Buildroot and Yocto images both receive the same
behavior.

Startup and device-permission files belong to each build system. This project's
Buildroot image uses a SysV script and BusyBox `mdev`; Yocto uses systemd and
udev. Partition descriptions also remain separate because Buildroot uses
genimage and Yocto uses Wic.

The private Wi-Fi configuration remains at the repository root. The Yocto
preparation script copies it into the Yocto build directory. Buildroot's
post-build script installs it directly into the target filesystem while the
image is assembled. Neither path adds the private file to this public
directory.

## Why the helpers use `/usr/libexec`

The main program and the video check are installed under `/usr/bin` because a
user may run them directly while diagnosing a problem. The files under
`/usr/libexec` are small executable helpers called by startup scripts and
systemd services. They are programs, not shared libraries, but users do not
normally run them by hand.

The services use each helper's complete path, so the helpers do not need to be
placed in the normal command search path. The GNU directory conventions
describe [`libexecdir`](https://www.gnu.org/prep/standards/html_node/Directory-Variables.html#index-libexecdir)
as the location for executable programs intended to be run by other programs.

## Persistent data

Both images contain a third partition labelled `iot-data`. On first boot,
`iot-app-prepare-data-storage` expands that partition to the end of the SD card,
grows its ext4 filesystem, and mounts it at `/data`. Later boots keep the files
already stored there. The complete partition layout is in the
[storage guide](../docs/storage/README.md).

The runtime does not require this partition. A custom image may omit it. If it
is missing or cannot be mounted, the storage helper does not create application
directories on the root filesystem, and IoT App uses its installed executable.

## Development executable override

Both startup services run `iot-app-launcher`. It normally starts the executable
installed at `/usr/bin/iot_app`. For development, it first checks:

```text
/data/iot-app/development/iot_app
```

The launcher uses that file only when `/data` is mounted and the file is a
regular, non-symlink executable. Removing it makes the next service start use
`/usr/bin/iot_app` again. The Buildroot and Yocto guides contain the commands
for uploading a new executable without exposing a partly copied file to the
service.

## Private Wi-Fi configuration

`wpa_supplicant.conf` remains at the repository root because it normally holds
a real network name and password and is excluded from Git. `make wifi-prepare`
creates it from the public example when it is missing. `make yocto-prepare`
then copies it to the Yocto build directory. For Buildroot, `post-build.sh`
installs the root copy in `/etc/wpa_supplicant.conf` inside the target
filesystem during the image build. The public `wpa_supplicant.conf.example`
file shows the required format.
