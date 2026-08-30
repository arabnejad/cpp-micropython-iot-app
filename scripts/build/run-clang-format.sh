#!/usr/bin/env bash

# The root Makefile calls this script for make format and make format-check.
# Keeping the file search here avoids repeating it in both Make targets.

set -euo pipefail

if [ "$#" -lt 3 ]; then
  echo "Usage: $0 <format|check> <clang-format> <source-directory>..." >&2
  exit 1
fi

requested_action="$1"
clang_format_command="$2"
shift 2

if ! command -v "$clang_format_command" >/dev/null 2>&1; then
  echo "$clang_format_command was not found. Install clang-format or set CLANG_FORMAT." >&2
  exit 1
fi

case "$requested_action" in
format)
  clang_format_arguments=(-i)
  ;;
check)
  clang_format_arguments=(--dry-run --Werror)
  ;;
*)
  echo "Unknown formatting action: $requested_action" >&2
  exit 1
  ;;
esac

find "$@" -type f \
  \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' \
  -o -name '*.h' -o -name '*.hh' -o -name '*.hpp' -o -name '*.hxx' \) \
  -print0 | xargs -0 -r "$clang_format_command" "${clang_format_arguments[@]}"
