#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
runtime_root="${ZYON_WINE_ROOT:-/workspace/.tools/zyon-wine}"
prefix_dir="${ZYON_WINE_PREFIX:-/workspace/.cache/zyon-preview-prefix}"
if [[ ! -f "$runtime_root/.complete" || ! -f "$project_dir/dist/zyon-preview.exe" ]]; then
  printf '%s\n' 'Execute setup-cloud.sh e setup-preview.sh primeiro.' >&2
  exit 1
fi
export XDG_CACHE_HOME="${XDG_CACHE_HOME:-/workspace/.cache}"
export WINEPREFIX="$prefix_dir" WINEARCH=win64 WINEDEBUG=-all
export WINEDLLPATH="$runtime_root/usr/lib/x86_64-linux-gnu/wine"
export LD_LIBRARY_PATH="$runtime_root/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# Debian's native zlib DLL is a dependency of Wine's user32, not of our EXE.
mkdir -p "$prefix_dir/drive_c/windows/system32"
cp "$runtime_root/usr/x86_64-w64-mingw32/lib/zlib1.dll" "$prefix_dir/drive_c/windows/system32/zlib1.dll"
display_file="$(mktemp)"
xvfb_pid=""
cleanup() {
  if [[ -n "$xvfb_pid" ]]; then
    "$runtime_root/usr/lib/wine/wineserver" -k || true
    kill "$xvfb_pid" 2>/dev/null || true
  fi
  rm -f "$display_file"
}
trap cleanup EXIT
if [[ -z "${DISPLAY:-}" ]]; then
  "$runtime_root/usr/bin/Xvfb" -displayfd 3 -screen 0 1440x960x24 \
    -nolisten tcp -noreset 3>"$display_file" >"$prefix_dir/xvfb.log" 2>&1 &
  xvfb_pid=$!
  for ((attempt = 0; attempt < 100; attempt++)); do
    if [[ -s "$display_file" ]]; then break; fi
    if ! kill -0 "$xvfb_pid" 2>/dev/null; then
      cat "$prefix_dir/xvfb.log" >&2
      exit 1
    fi
    sleep 0.05
  done
  test -s "$display_file"
  export DISPLAY=":$(cat "$display_file")"
fi
printf 'Prévia iniciando no display %s; dados sintéticos, sem conexão com jogo.\n' "$DISPLAY"
"$runtime_root/usr/lib/wine/wine64" "$project_dir/dist/zyon-preview.exe"
