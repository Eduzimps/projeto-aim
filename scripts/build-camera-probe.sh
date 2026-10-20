#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
toolchain_root="${ZYON_MINGW_ROOT:-/workspace/.tools/zyon-mingw}"
compiler="${CXX_WINDOWS:-}"
if [[ -z "$compiler" ]]; then
  if command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
    compiler="$(command -v x86_64-w64-mingw32-g++)"
  else
    compiler="$toolchain_root/usr/bin/x86_64-w64-mingw32-g++-posix"
  fi
fi
if [[ ! -x "$compiler" ]]; then
  printf '%s\n' 'MinGW ausente. Execute bash scripts/setup-cloud.sh primeiro.' >&2
  exit 1
fi
mkdir -p "$project_dir/dist"
"$compiler" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
  -DUNICODE -D_UNICODE -I"$project_dir/include" \
  "$project_dir/src/windows_camera_probe.cpp" -o "$project_dir/dist/zyon-camera-probe.exe" \
  -static -static-libgcc -static-libstdc++ \
  -lbcrypt -lshell32 -lole32 -luser32
printf '%s\n' "$project_dir/dist/zyon-camera-probe.exe"
