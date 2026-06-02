#!/usr/bin/env bash

set -Eeuo pipefail

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly REMOTE_HOST="hamid@192.168.0.67"
readonly REMOTE_DIR="/home/hamid/IOT_project"

if ! command -v rsync >/dev/null 2>&1; then
  echo "Error: rsync is not installed locally." >&2
  echo "Install it with: sudo apt install rsync" >&2
  exit 1
fi

echo "Checking the Raspberry Pi connection..."
ssh -o BatchMode=yes -o ConnectTimeout=5 "${REMOTE_HOST}" \
  "command -v rsync >/dev/null 2>&1 && mkdir -p '${REMOTE_DIR}'" || {
  echo "Error: SSH failed or rsync is not installed on ${REMOTE_HOST}." >&2
  echo "On the Pi, install it with: sudo apt install rsync" >&2
  exit 1
}

echo "Synchronizing ${SCRIPT_DIR}/"
echo "             -> ${REMOTE_HOST}:${REMOTE_DIR}/"

rsync \
  --archive \
  --compress \
  --human-readable \
  --info=progress2,stats2 \
  --exclude='/.git/' \
  --exclude='/**/.git/' \
  --exclude='/.vscode/' \
  --exclude='/build/' \
  --exclude='/buildroot/output/' \
  --exclude='/build-*/' \
  --exclude='/out/' \
  --exclude='/iot_app/build/' \
  --exclude='/iot_app/build-*/' \
  --exclude='/iot_app/out/' \
  --exclude='/iot_app/generated/' \
  --exclude='/**/CMakeFiles/' \
  --exclude='/**/CMakeCache.txt' \
  --exclude='/**/cmake_install.cmake' \
  --exclude='/**/compile_commands.json' \
  --exclude='/**/__pycache__/' \
  --exclude='/**/.cache/' \
  --exclude='/**/.venv/' \
  --exclude='*.a' \
  --exclude='*.dll' \
  --exclude='*.exe' \
  --exclude='*.log' \
  --exclude='*.mpy' \
  --exclude='*.o' \
  --exclude='*.obj' \
  --exclude='*.pyc' \
  --exclude='*.so' \
  --exclude='*~' \
  --exclude='*.swp' \
  --exclude='*.swo' \
  "${SCRIPT_DIR}/" \
  "${REMOTE_HOST}:${REMOTE_DIR}/"

echo "Synchronization complete."
