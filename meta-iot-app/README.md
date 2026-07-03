# IoT App Yocto layer

This layer builds IoT App and a complete 64-bit Raspberry Pi 4 image. It uses
the project-owned source together with the pinned Poky, Raspberry Pi, LVGL,
MicroPython, and OpenEmbedded submodules.

The complete setup, build, flash, update, and troubleshooting instructions are
in [`iot_app/docs/yocto/README.md`](../iot_app/docs/yocto/README.md).

The generated image uses the root-level `storage_layout.conf`. Its fixed root
partition and expandable `/data` partition are described in
[`iot_app/docs/storage/README.md`](../iot_app/docs/storage/README.md).

Common files used by both Yocto and Buildroot live in
[`iot_app/image_support`](../iot_app/image_support/README.md). The Yocto recipes
install those files but keep their systemd units and BitBake rules in this
layer.
