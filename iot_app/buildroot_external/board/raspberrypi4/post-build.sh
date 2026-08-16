#!/bin/sh

set -eu

board_directory="$(dirname "$0")"
wifi_configuration="$board_directory/wpa_supplicant.conf"

if [ ! -f "$wifi_configuration" ]; then
  echo "Missing private Wi-Fi configuration: $wifi_configuration" >&2
  echo "Copy wpa_supplicant.conf.example to wpa_supplicant.conf and add your network details." >&2
  exit 1
fi

# Install the private Wi-Fi configuration without making it readable by
# unprivileged users in the target image.
install -D -m 0600 "$wifi_configuration" "$TARGET_DIR/etc/wpa_supplicant.conf"

# Buildroot's Raspberry Pi script adds a normal login prompt on tty1. Replace
# it with automatic root login for the local HDMI console. SSH continues to
# use Dropbear's normal password authentication.
if [ -f "$TARGET_DIR/etc/inittab" ]; then
  sed -i \
    's|^tty1::respawn:.*# HDMI console$|tty1::respawn:/sbin/getty -L -n -l /usr/sbin/iot-app-console-autologin tty1 0 vt100 # HDMI console|' \
    "$TARGET_DIR/etc/inittab"
fi
