# Image storage layout

The Raspberry Pi 4 Buildroot and Yocto images use the same storage plan. Linux
and IoT App stay in a fixed-size root partition. Files that need more space
belong in `/data`.

The sizes are kept in the root-level
[`storage_layout.conf`](../../../storage_layout.conf):

```makefile
ROOT_PARTITION_SIZE_MIB ?= 256
```

MiB means mebibytes. One MiB is 1,048,576 bytes.

The generated image contains three partitions:

```text
SD card
├── Partition 1: Raspberry Pi boot files
├── Partition 2: Linux root filesystem (256 MiB by default)
└── Partition 3: persistent data (all remaining card space)
```

The image contains a small 64 MiB ext4 filesystem so partition 3 is already
formatted when it is flashed. This is an internal image-building value, not a
user setting. On the first boot, Buildroot runs
`/etc/init.d/S25data-storage` and Yocto starts `iot-app-storage.service`.
Both call the same `iot-app-prepare-data-storage` program, which performs
these steps:

1. It finds the SD card that contains the root partition.
2. It checks that partition 3 has the label `iot-data`. This prevents the
   script from resizing an unrelated partition by mistake.
3. GNU Parted moves the end of partition 3 to the end of the card.
4. `partx` tells the running kernel about the new partition size.
5. `resize2fs` expands the ext4 filesystem inside the partition.
6. The filesystem is mounted at `/data` and made writable by the `iot-app`
   user.

The script can run again safely. After the first successful boot, there is
normally nothing left to expand.

For example, a 128 GB card keeps a 256 MiB root partition and gives almost all
remaining space to `/data`. The exact reported capacity is slightly lower
because card manufacturers use decimal units and the image also contains the
boot partition and partition alignment gaps.

Files in `/data` survive a reboot and a new Python application. Flashing a
complete image writes a new partition table and replaces `/data`, so copy any
important data elsewhere before reflashing the card.

## Change the sizes

Edit `storage_layout.conf`, then build a new image:

```makefile
ROOT_PARTITION_SIZE_MIB ?= 2048
```

The root partition should leave enough free space for future packages and
logs. `/data` automatically receives the card space that remains after the
boot and root partitions.

You can also override either value for one command:

```bash
make buildroot-image ROOT_PARTITION_SIZE_MIB=2048
make yocto-image ROOT_PARTITION_SIZE_MIB=2048
```

Run `make storage-check` to check the values without building an image.

## Check the result on the Raspberry Pi

After booting, run:

```bash
df -h / /data
lsblk -o NAME,SIZE,FSTYPE,LABEL,MOUNTPOINTS
```

`/` should keep the configured root size. `/data` should use the space left on
the card.

The storage service also creates `/data/iot-app/development`. The
[development executable guide](../development-executable/README.md) explains
how that directory can be used to test a cross-compiled IoT App executable
without replacing `/usr/bin/iot_app`.

## References

- The [Buildroot `genimage.cfg` section](https://buildroot.org/downloads/manual/manual.html#writing-genimage-cfg)
  explains how Buildroot assembles partitions into `sdcard.img`.
- The [Yocto Wic `part` command reference](https://docs.yoctoproject.org/scarthgap/ref-manual/kickstart.html#command-part-or-partition)
  documents fixed partition sizes and empty filesystem partitions.
- The [GNU Parted `resizepart` documentation](https://www.gnu.org/software/parted/manual/html_node/resizepart.html)
  explains that changing the partition boundary does not resize the
  filesystem itself.
- The [`resize2fs` manual](https://man7.org/linux/man-pages/man8/resize2fs.8.html)
  explains how an ext2, ext3, or ext4 filesystem is grown after its partition
  becomes larger.
- The [`partx` manual](https://man7.org/linux/man-pages/man8/partx.8.html)
  describes how the kernel's view of a partition table is updated.
