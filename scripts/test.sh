#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$project_dir/build"
for test_name in core camera_profile messiah_projection entity_position entity_identity scene_collections scene_profile; do
  "${CXX:-g++}" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
    -I"$project_dir/include" "$project_dir/tests/${test_name}_test.cpp" \
    -o "$project_dir/build/${test_name}_test"
  "$project_dir/build/${test_name}_test"
done
