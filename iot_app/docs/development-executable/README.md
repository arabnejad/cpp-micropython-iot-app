# Test a rebuilt executable from `/data`

During development, a rebuilt IoT App executable can be tested without
rebuilding and flashing the complete image. The installed executable stays at
`/usr/bin/iot_app`, while the test executable is stored here:

```text
/data/iot-app/development/iot_app
```

The storage service creates `/data/iot-app/development` during boot. Buildroot
and Yocto then start `/usr/libexec/iot-app-launcher` instead of calling the
installed executable directly.

The launcher makes this choice:

```text
IoT App service starts
        |
        v
Is /data/iot-app/development/iot_app
a regular executable file?
        |
   +----+----+
   |         |
  Yes        No
   |         |
   v         v
Run the      Run /usr/bin/iot_app
development
executable
```

A symbolic link is not accepted as the development executable. If the path
exists but is not a regular executable file, the launcher reports a warning
and uses `/usr/bin/iot_app`.

## 1. Build for the correct image

The Raspberry Pi needs an AArch64 executable produced by the image's own
toolchain. Do not copy `build/iot_app/iot_app` from a normal Ubuntu build; that
file may be an x86-64 executable.

For Buildroot, run:

```bash
make buildroot-app
```

The executable is:

```text
/opt/iot-app-builds/buildroot-raspberry-pi-4/target/usr/bin/iot_app
```

For Yocto, run:

```bash
make yocto-app
```

Use the executable from the recipe's `do_install` directory, as described in
the [Yocto build guide](../yocto/README.md#13-deploy-a-rebuilt-application-without-flashing).

Buildroot and Yocto use different toolchains. Build the executable for the
image that is currently running on the Raspberry Pi.

## 2. Copy the executable

The examples below use `iot_app_path` for the cross-compiled executable:

```bash
iot_app_path=/path/to/the/aarch64/iot_app
```

Copy it under a temporary name:

```bash
scp "$iot_app_path" \
  root@rspi-iot-app.local:/data/iot-app/development/iot_app.new
```

The Buildroot image uses Dropbear. If the local `scp` command tries SFTP and
the upload fails, select the original SCP protocol:

```bash
scp -O "$iot_app_path" \
  root@rspi-iot-app.local:/data/iot-app/development/iot_app.new
```

After a complete upload, make the file executable and rename it:

```bash
ssh root@rspi-iot-app.local '
  chmod 0755 /data/iot-app/development/iot_app.new &&
  mv /data/iot-app/development/iot_app.new \
     /data/iot-app/development/iot_app
'
```

Both names are on the same filesystem. If the upload is interrupted, the old
development executable remains unchanged. The final rename replaces it only
after the new file has arrived completely.

## 3. Start the development executable

A reboot will select the development executable automatically:

```bash
ssh root@rspi-iot-app.local reboot
```

A full reboot is not required when only the executable changed. Restart the
service instead.

Buildroot:

```bash
ssh root@rspi-iot-app.local '/etc/init.d/iot-app restart'
```

Yocto:

```bash
ssh root@rspi-iot-app.local 'systemctl restart iot-app'
```

The launcher writes the selected path to the normal service log.

Buildroot:

```bash
ssh root@rspi-iot-app.local 'tail -n 50 /var/log/iot_app.log'
```

Yocto:

```bash
ssh root@rspi-iot-app.local \
  'journalctl -u iot-app -b --no-pager -n 50'
```

When the override is active, the log contains:

```text
[IOT_APP][WARNING]   [iot-app-launcher] Running development executable: /data/iot-app/development/iot_app
```

## 4. Return to the installed executable

Remove the development file and restart or reboot:

```bash
ssh root@rspi-iot-app.local '
  rm -f /data/iot-app/development/iot_app &&
  reboot
'
```

The next start uses `/usr/bin/iot_app`. A failing development executable is
not removed automatically because its failure may be the reason for the test.
Use SSH or the recovery login on `tty2` to remove it.

## 5. What this workflow updates

IoT App's project-owned C++ libraries are linked statically into the
executable, so one file is normally enough for testing C++ changes. The
executable still uses system libraries supplied by the image.

This workflow does not update:

- the default Python application;
- service or access-rule files;
- system libraries;
- Buildroot or Yocto package selections; or
- the boot and partition configuration.

Rebuild and flash a complete image when any of those parts change. Also build
a complete image before final testing so the installed copy of IoT App matches
the source repository.

## Security note

The development directory is writable by the `iot-app` user. This project
currently treats received Python applications as trusted code, so an
application can also write there and make native code persist across a reboot.
Remove this launcher behavior or protect the directory before using the image
with untrusted applications.

## References

- The [`systemd.service` manual](https://www.freedesktop.org/software/systemd/man/latest/systemd.service.html#ExecStart=)
  describes how systemd starts and monitors the process configured with
  `ExecStart=`.
