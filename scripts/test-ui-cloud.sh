#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
runtime_root="${ZYON_WINE_ROOT:-/workspace/.tools/zyon-wine}"
toolchain_root="${ZYON_MINGW_ROOT:-/workspace/.tools/zyon-mingw}"
test -f "$runtime_root/.complete"
test -f "$project_dir/dist/zyon-preview.exe"
mkdir -p "$project_dir/build" /workspace/.cache
"$toolchain_root/usr/bin/x86_64-w64-mingw32-g++-posix" \
  -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -DUNICODE -D_UNICODE \
  "$project_dir/tests/windows_ui_smoke.cpp" -o "$project_dir/build/windows_ui_smoke.exe" \
  -municode -static -luser32 -lgdi32 -lshell32
export ZYON_WINE_PREFIX="$(mktemp -d /workspace/.cache/zyon-ui-test.XXXXXX)"
export WINEPREFIX="$ZYON_WINE_PREFIX" WINEARCH=win64 WINEDEBUG=-all ZYON_UI_TEST=1
export XDG_CACHE_HOME="${XDG_CACHE_HOME:-/workspace/.cache}"
export WINEDLLPATH="$runtime_root/usr/lib/x86_64-linux-gnu/wine"
export LD_LIBRARY_PATH="$runtime_root/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
display_file="$ZYON_WINE_PREFIX/display"
"$runtime_root/usr/bin/Xvfb" -displayfd 3 -screen 0 1440x960x24 -dpi 96 \
  -nolisten tcp -noreset 3>"$display_file" >"$ZYON_WINE_PREFIX/xvfb.log" 2>&1 &
xvfb_pid=$!
preview_pid=""
cleanup() {
  "$runtime_root/usr/lib/wine/wineserver" -k || true
  if [[ -n "$preview_pid" ]]; then wait "$preview_pid" 2>/dev/null || true; fi
  kill "$xvfb_pid" 2>/dev/null || true
}
trap cleanup EXIT
for ((attempt=0; attempt<100; attempt++)); do
  if [[ -s "$display_file" ]]; then break; fi
  kill -0 "$xvfb_pid"
  sleep 0.05
done
test -s "$display_file"
export DISPLAY=":$(cat "$display_file")"
bash "$project_dir/scripts/start-preview-cloud.sh" >"$ZYON_WINE_PREFIX/preview.log" 2>&1 &
preview_pid=$!
settings_file="$ZYON_WINE_PREFIX/drive_c/users/$(id -un)/AppData/Local/Zyon/settings.ini"
for ((attempt=0; attempt<450; attempt++)); do
  if [[ -f "$settings_file" ]]; then break; fi
  if ! kill -0 "$preview_pid" 2>/dev/null; then
    cat "$ZYON_WINE_PREFIX/preview.log" >&2
    exit 1
  fi
  sleep 0.1
done
if [[ ! -f "$settings_file" ]]; then
  cat "$ZYON_WINE_PREFIX/preview.log" >&2
  printf '%s\n' 'A prévia não concluiu a inicialização em 45 segundos.' >&2
  exit 1
fi
windows_exe="Z:${project_dir//\//\\}\\dist\\zyon-preview.exe"
"$runtime_root/usr/lib/wine/wine64" "$project_dir/build/windows_ui_smoke.exe" "$windows_exe"
