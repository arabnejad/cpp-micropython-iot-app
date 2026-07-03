# Shared image support files

Buildroot and Yocto use different package and service formats, but several
installed files must behave in the same way. This directory keeps one copy of
those files.

| File | Installed location | Purpose |
|---|---|---|
| `iot-app-launcher` | `/usr/libexec/iot-app-launcher` | Runs the development executable from `/data` when it is available; otherwise runs `/usr/bin/iot_app` |
| `iot-app-prepare-data-storage` | `/usr/libexec/iot-app-prepare-data-storage` | Expands, mounts, and prepares the persistent `/data` partition |
| `iot-app-hide-tty1-cursor` | `/usr/libexec/iot-app-hide-tty1-cursor` | Hides the terminal cursor before the framebuffer dashboard starts |
| `mosquitto.conf` | `/etc/mosquitto/mosquitto.conf` | Opens the development MQTT listener used by the sender |
| `70-iot-app-access.rules` | `/usr/lib/udev/rules.d/70-iot-app-access.rules` | Gives the `iot-app` user group access to framebuffer, DRM, I2C, and input devices |

The Buildroot package recipe and Yocto recipes install these files into their
images. Change a shared file here so the next Buildroot and Yocto images both
receive the same behavior.

The startup service still belongs to each build system. Buildroot may use a
SysV script, while Yocto uses systemd. Partition descriptions also remain
separate because Buildroot uses genimage and Yocto uses Wic.

The private Wi-Fi configuration remains at the repository root. Preparation
commands copy it into the appropriate Buildroot or Yocto build input without
adding it to this public directory.

## Persistent data

Both images contain a third partition labelled `iot-data`. On first boot,
`iot-app-prepare-data-storage` expands that partition to the end of the SD card,
grows its ext4 filesystem, and mounts it at `/data`. Later boots keep the files
already stored there. The complete partition layout is in the
[storage guide](../docs/storage/README.md).

## Development executable override

Both startup services run `iot-app-launcher`. It normally starts the executable
installed at `/usr/bin/iot_app`. For development, it first checks:

```text
/data/iot-app/development/iot_app
```

The launcher uses that file only when it is a regular, non-symlink executable.
Removing it makes the next service start use `/usr/bin/iot_app` again. The
Buildroot and Yocto guides contain the commands for uploading a new executable
without exposing a partly copied file to the service.

## Private Wi-Fi configuration

`wpa_supplicant.conf` remains at the repository root because it normally holds
a real network name and password and is excluded from Git. The preparation
targets, `make buildroot-prepare` and `make yocto-prepare`, copy it to the
location expected by each build system. The public
`wpa_supplicant.conf.example` file shows the required format.
