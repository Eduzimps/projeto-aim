#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$project_dir/build"
"${CXX:-g++}" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
  -I"$project_dir/include" "$project_dir/tests/core_test.cpp" \
  -o "$project_dir/build/core_test"
"$project_dir/build/core_test"
"${CXX:-g++}" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
  -I"$project_dir/include" "$project_dir/tests/camera_profile_test.cpp" \
  -o "$project_dir/build/camera_profile_test"
"$project_dir/build/camera_profile_test"
