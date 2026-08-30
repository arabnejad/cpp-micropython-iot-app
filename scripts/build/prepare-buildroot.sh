#!/usr/bin/env bash

# make buildroot-prepare calls this script before a Buildroot application or
# image build. It prepares directories and configuration but does not compile
# packages or create an image.

set -euo pipefail

: "${PROJECT_ROOT:?PROJECT_ROOT is required}"
: "${BUILDROOT_OUTPUT:?BUILDROOT_OUTPUT is required}"
: "${IMAGE_OUTPUT_DIRECTORY:?IMAGE_OUTPUT_DIRECTORY is required}"
: "${ROOT_PARTITION_SIZE_MIB:?ROOT_PARTITION_SIZE_MIB is required}"
: "${DATA_PARTITION_BOOTSTRAP_SIZE_MIB:?DATA_PARTITION_BOOTSTRAP_SIZE_MIB is required}"

buildroot_source_directory="$PROJECT_ROOT/buildroot"
buildroot_external_directory="$PROJECT_ROOT/iot_app/buildroot_external"
buildroot_storage_layout="$BUILDROOT_OUTPUT/iot-app-storage-layout.conf"
ssh_authorized_keys="$PROJECT_ROOT/ssh_authorized_keys"

ensure_writable_directory() {
  local directory_path="$1"

  if [ ! -d "$directory_path" ]; then
    echo "Creating persistent directory: $directory_path"
    mkdir -p "$directory_path" 2>/dev/null ||
      sudo install -d -m 0755 -o "$(id -u)" -g "$(id -g)" "$directory_path"
  fi

  if [ ! -w "$directory_path" ]; then
    echo "Changing ownership of $directory_path to the current user"
    sudo chown "$(id -u):$(id -g)" "$directory_path"
  fi
}

run_buildroot_make() {
  env -u LD_LIBRARY_PATH make -C "$buildroot_source_directory" \
    BR2_EXTERNAL="$buildroot_external_directory" O="$BUILDROOT_OUTPUT" "$@"
}

if [[ "$BUILDROOT_OUTPUT" == *"@"* ]]; then
  echo "BUILDROOT_OUTPUT must not contain '@': $BUILDROOT_OUTPUT" >&2
  exit 1
fi

if [ ! -f "$buildroot_source_directory/Makefile" ]; then
  echo "The Buildroot submodule is missing. Run: make submodules" >&2
  exit 1
fi

ensure_writable_directory "$BUILDROOT_OUTPUT"
ensure_writable_directory "$IMAGE_OUTPUT_DIRECTORY"

if [ -s "$ssh_authorized_keys" ]; then
  echo "The Buildroot image will include SSH public keys from $ssh_authorized_keys"
else
  echo "No non-empty ssh_authorized_keys file was found; password SSH login will remain available"
fi

# Reload the project defconfig every time so a reused output directory receives
# configuration changes committed to the repository.
echo "Loading the Raspberry Pi 4 Buildroot configuration"
run_buildroot_make iot_rpi4_defconfig

sed -i \
  "s|^BR2_TARGET_ROOTFS_EXT2_SIZE=.*|BR2_TARGET_ROOTFS_EXT2_SIZE=\"${ROOT_PARTITION_SIZE_MIB}M\"|" \
  "$BUILDROOT_OUTPUT/.config"
run_buildroot_make olddefconfig

printf 'ROOT_PARTITION_SIZE_MIB=%s\nDATA_PARTITION_BOOTSTRAP_SIZE_MIB=%s\n' \
  "$ROOT_PARTITION_SIZE_MIB" "$DATA_PARTITION_BOOTSTRAP_SIZE_MIB" \
  >"$buildroot_storage_layout"
