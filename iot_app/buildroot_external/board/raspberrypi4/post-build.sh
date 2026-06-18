#!/bin/sh

set -eu

board_directory="$(CDPATH= cd "$(dirname "$0")" && pwd)"
project_root="$(CDPATH= cd "$board_directory/../../../.." && pwd)"
wifi_configuration="$project_root/wpa_supplicant.conf"
ssh_authorized_keys="$project_root/ssh_authorized_keys"

if [ ! -f "$wifi_configuration" ]; then
  echo "Missing private Wi-Fi configuration: $wifi_configuration" >&2
  echo "Run 'make wifi-prepare', then add your network details." >&2
  exit 1
fi

# Install the private Wi-Fi configuration without making it readable by
# unprivileged users in the target image.
install -D -m 0600 "$wifi_configuration" "$TARGET_DIR/etc/wpa_supplicant.conf"

# A developer may provide one or more public keys in the optional root-level
# ssh_authorized_keys file. Dropbear reads this installed copy when root logs
# in over SSH. Remove an old copy during incremental builds when the optional
# source file has been deleted or emptied.
if [ -s "$ssh_authorized_keys" ]; then
  install -d -m 0700 "$TARGET_DIR/root/.ssh"
  install -m 0600 "$ssh_authorized_keys" \
    "$TARGET_DIR/root/.ssh/authorized_keys"
else
  rm -f "$TARGET_DIR/root/.ssh/authorized_keys"
fi

# Buildroot's Raspberry Pi script adds a login prompt on tty1. IoT App uses
# that terminal's framebuffer, so remove its getty to prevent a cursor and
# typed text from appearing over the dashboard. tty2 remains available as a
# password-protected emergency console.
if [ -f "$TARGET_DIR/etc/inittab" ]; then
  sed -i '/^tty1::respawn:/d' "$TARGET_DIR/etc/inittab"

  if ! grep -q '^tty2::respawn:' "$TARGET_DIR/etc/inittab"; then
    printf '%s\n' \
      'tty2::respawn:/sbin/getty -L tty2 0 vt100 # Emergency console' \
      >> "$TARGET_DIR/etc/inittab"
  fi
fi
