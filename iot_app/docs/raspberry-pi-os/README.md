# Raspberry Pi OS development notes

These notes describe development on Raspberry Pi OS. They do not describe the
complete images produced by Buildroot or Yocto. See the
[Buildroot guide](../buildroot/README.md) or
[Yocto guide](../yocto/README.md) for those workflows.

## 1. Can you compile locally and copy the binary?

Only if you cross-compile for the Raspberry Pi’s ARM architecture.

Your Ubuntu desktop is probably:

```bash
uname -m
# likely: x86_64
```

Your Raspberry Pi 4 will be one of:

```bash
uname -m
# aarch64  -> 64-bit Raspberry Pi OS
# armv7l   -> 32-bit Raspberry Pi OS
```

An `x86_64` executable copied to an ARM Pi will fail with:

```text
cannot execute binary file: Exec format error
```

You have three options.

### Recommended first stage: compile on the Pi

This is the easiest way to begin while C++, MicroPython, DRM, LVGL, and the
hardware are still being tested together.

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build git
```

Copy or clone the project, then build:

```bash
cmake -S iot_app -B build/iot_app -G Ninja
cmake --build build/iot_app
```

VS Code Remote SSH lets you edit from Ubuntu while compiling and debugging on
the Pi.

### Cross-compile on Ubuntu

For 64-bit Raspberry Pi OS, the compiler is generally:

```text
aarch64-linux-gnu-g++
```

For 32-bit Raspberry Pi OS:

```text
arm-linux-gnueabihf-g++
```

However, matching the CPU is only part of the problem. The build must also match the target’s:

- 32/64-bit ABI.
- C library and `libstdc++`.
- Installed library versions.
- Header files.
- DRM, LVGL, TLS, and other dependencies.

A binary built against a newer Ubuntu glibc may fail on Raspberry Pi OS with an error such as:

```text
version `GLIBC_2.xx' not found
```

A reliable cross-build needs a Raspberry Pi OS-compatible sysroot and CMake
toolchain file. Buildroot and Yocto create their own matched toolchains and
system images.

### Practical recommendation

Use this progression:

1. Compile directly on the Pi during initial development.
2. Develop over SSH or VS Code Remote SSH.
3. Optionally establish a Raspberry Pi OS cross-toolchain after the application works.
4. Use the selected Buildroot or Yocto image build for final testing.

Once you have an ARM-compatible binary, copying it is fine:

```bash
scp build/iot_app/iot_app pi@raspberrypi.local:~/
ssh pi@raspberrypi.local
chmod +x ~/iot_app
~/iot_app
```

Check a binary before copying:

```bash
file build/iot_app/iot_app
```

For 64-bit Pi OS, it should report something similar to:

```text
ELF 64-bit LSB pie executable, ARM aarch64
```

## 2. Should the Pi boot without a GUI?

IoT App draws through the Linux framebuffer at `/dev/fb0` and uses DRM/KMS only
to discover the connected display. Booting into console mode is recommended so
the desktop compositor does not redraw over the framebuffer.

Modern Raspberry Pi OS uses DRM/KMS. Do not disable the KMS driver in
`/boot/firmware/config.txt` because IoT App needs it for direct display access.
Recent Raspberry Pi OS releases no longer support the old legacy graphics
mode. See [Raspberry Pi display configuration](https://www.raspberrypi.com/documentation/configuration/config-txt/memory.md).

Raspberry Pi OS Lite is not required. The normal desktop image can boot into
console mode, and the GUI remains available if it is needed later.

### Permanently boot into console mode

Run this over SSH or from a terminal:

```bash
sudo systemctl set-default multi-user.target
sudo reboot
```

Verify the configured mode:

```bash
systemctl get-default
```

Expected result:

```text
multi-user.target
```

Systemd stores this choice in the `/etc/systemd/system/default.target` symlink.
Use `systemctl` to change it rather than editing the link by hand.

### Permanently switch back to the GUI

```bash
sudo systemctl set-default graphical.target
sudo reboot
```

Verify:

```bash
systemctl get-default
# graphical.target
```

This works as long as you installed a Raspberry Pi OS desktop image. Raspberry Pi OS Lite does not contain a desktop environment to restore.

### Switch temporarily without rebooting

Stop the GUI and enter console mode:

```bash
sudo systemctl isolate multi-user.target
```

Start the GUI again:

```bash
sudo systemctl isolate graphical.target
```

Run the first command through SSH because it will terminate the current graphical session and applications.

You can also use the officially supported interactive configuration:

```bash
sudo raspi-config
```

Then select:

```text
System Options --> Boot --> Console Text console
```

Select Desktop from the same menu to restore graphical boot. [Raspberry Pi boot-to-console documentation](https://www.raspberrypi.com/documentation/computers/configuration.html)

## Display-device access

IoT App renders through the framebuffer and uses DRM for display discovery.
Confirm that both device types exist:

```bash
ls -l /dev/dri/
kmsprint
```

Your application user will normally need display and input permissions:

```bash
sudo usermod -aG video,render,input "$USER"
```

Log out and back in after changing group membership.

The best first-stage configuration is therefore:

- Raspberry Pi OS Desktop installed.
- SSH enabled.
- Default boot target set to `multi-user.target`.
- KMS left enabled.
- Build and run the application directly on the Pi.
- Restore the desktop at any time with `graphical.target`.
