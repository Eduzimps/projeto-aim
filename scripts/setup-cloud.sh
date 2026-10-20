#!/usr/bin/env bash
set -euo pipefail
# Downloads authenticated Debian packages and extracts them without root.
# No package installation, repository changes, or verification bypasses.
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
toolchain_root="${ZYON_MINGW_ROOT:-/workspace/.tools/zyon-mingw}"
apt_root="${ZYON_APT_ROOT:-/workspace/.cache/zyon-apt}"
if [[ ! -f "$toolchain_root/.complete" ]]; then
  for command_name in apt-get dpkg-deb g++; do
    command -v "$command_name" >/dev/null
  done
  test -r /usr/share/keyrings/debian-archive-keyring.gpg
  mkdir -p "$apt_root"/{lists/partial,archives/partial,sourceparts,confparts,log}
  cat > "$apt_root/sources.list" <<'EOF'
deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian trixie main
EOF
  cat > "$apt_root/apt.conf" <<EOF
Dir::Etc::main "-";
Dir::Etc::parts "$apt_root/confparts";
Dir::Etc::sourcelist "$apt_root/sources.list";
Dir::Etc::sourceparts "$apt_root/sourceparts";
Dir::State::lists "$apt_root/lists";
Dir::Cache::archives "$apt_root/archives";
Dir::Log "$apt_root/log";
APT::Sandbox::User "$(id -un)";
Acquire::Languages "none";
EOF
  APT_CONFIG="$apt_root/apt.conf" apt-get update
  download_dir="$(mktemp -d "$apt_root/download.XXXXXX")"
  (
    cd "$download_dir"
    APT_CONFIG="$apt_root/apt.conf" apt-get download \
      binutils-mingw-w64-x86-64 g++-mingw-w64-x86-64-posix \
      gcc-mingw-w64-base gcc-mingw-w64-x86-64-posix \
      gcc-mingw-w64-x86-64-posix-runtime mingw-w64-common \
      mingw-w64-x86-64-dev
  )
  mkdir -p "$toolchain_root"
  for package_file in "$download_dir/"*.deb; do
    dpkg-deb --extract "$package_file" "$toolchain_root"
  done
fi
bash "$project_dir/scripts/test.sh"
bash "$project_dir/scripts/build-windows.sh"
touch "$toolchain_root/.complete"
