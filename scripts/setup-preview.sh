#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
runtime_root="${ZYON_WINE_ROOT:-/workspace/.tools/zyon-wine}"
apt_root="${ZYON_APT_ROOT:-/workspace/.cache/zyon-apt}"
if [[ ! -f "$apt_root/apt.conf" ]]; then
  bash "$project_dir/scripts/setup-cloud.sh"
fi
if [[ ! -f "$runtime_root/.complete" ]]; then
  APT_CONFIG="$apt_root/apt.conf" apt-get update
  download_dir="$(mktemp -d "$apt_root/preview-download.XXXXXX")"
  (
    cd "$download_dir"
    APT_CONFIG="$apt_root/apt.conf" apt-get download libcapi20-3t64 \
      libpcap0.8t64 libxkbregistry0 libz-mingw-w64 libwine wine64 xvfb
  )
  mkdir -p "$runtime_root"
  for package_file in "$download_dir/"*.deb; do
    dpkg-deb --extract "$package_file" "$runtime_root"
  done
  # Wine supports /server + /nls as an uninstalled build layout. Keep the
  # authenticated ELF unchanged; only its filesystem location is different.
  mkdir -p "$runtime_root-build/server"
  if ! cmp -s "$runtime_root/usr/lib/wine/wineserver64" "$runtime_root-build/server/wineserver"; then
    cp "$runtime_root/usr/lib/wine/wineserver64" "$runtime_root-build/server/wineserver.new"
    mv "$runtime_root-build/server/wineserver.new" "$runtime_root-build/server/wineserver"
  fi
  ln -sfn "$runtime_root/usr/share/wine/nls" "$runtime_root-build/nls"
  if [[ ! -f "$runtime_root/usr/lib/wine/wineserver.debian-wrapper" ]]; then
    cp "$runtime_root/usr/lib/wine/wineserver" "$runtime_root/usr/lib/wine/wineserver.debian-wrapper"
  fi
  ln -sfn "$runtime_root-build/server/wineserver" "$runtime_root/usr/lib/wine/wineserver"
  "$runtime_root/usr/lib/wine/wine64" --version
  touch "$runtime_root/.complete"
fi
