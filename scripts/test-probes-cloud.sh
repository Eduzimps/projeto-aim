#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
runtime_root="${ZYON_WINE_ROOT:-/workspace/.tools/zyon-wine}"
toolchain_root="${ZYON_MINGW_ROOT:-/workspace/.tools/zyon-mingw}"
test -f "$runtime_root/.complete"
mkdir -p "$project_dir/build" /workspace/.cache
test_dir="$(mktemp -d "$project_dir/build/probe-smoke.XXXXXX")"
export WINEPREFIX="$test_dir/prefix" WINEARCH=win64 WINEDEBUG=-all
export XDG_CACHE_HOME="${XDG_CACHE_HOME:-/workspace/.cache}"
export WINEDLLPATH="$runtime_root/usr/lib/x86_64-linux-gnu/wine"
export LD_LIBRARY_PATH="$runtime_root/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# The rootless Debian Wine package needs its separately packaged zlib DLL.
mkdir -p "$WINEPREFIX/drive_c/windows/system32"
cp "$runtime_root/usr/x86_64-w64-mingw32/lib/zlib1.dll" \
  "$WINEPREFIX/drive_c/windows/system32/zlib1.dll"
display_file="$test_dir/display"
"$runtime_root/usr/bin/Xvfb" -displayfd 3 -screen 0 1024x768x24 \
  -nolisten tcp -noreset 3>"$display_file" >"$test_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
cleanup() {
  "$runtime_root/usr/lib/wine/wineserver" -k || true
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
run_probe() {
  local name="$1" scenario="$2" result=0
  "$runtime_root/usr/lib/wine/wine64" "$project_dir/dist/zyon-${name}-probe.exe" \
    >"$test_dir/${name}-${scenario}.json" 2>"$test_dir/${name}-${scenario}.log" || result=$?
  test "$result" -eq 1
}
run_probe camera absent
run_probe scene absent
# This fixture is our own tiny program, never the uploaded game executable.
cat > "$test_dir/fixture.cpp" <<'EOF'
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
int main() {
    std::puts("fixture-ready");
    std::fflush(stdout);
    Sleep(30000);
}
EOF
"$toolchain_root/usr/bin/x86_64-w64-mingw32-g++-posix" \
  -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -static \
  "$test_dir/fixture.cpp" -o "$test_dir/BloodStrike.exe"
"$runtime_root/usr/lib/wine/wine64" "$test_dir/BloodStrike.exe" \
  >"$test_dir/fixture.log" 2>&1 &
fixture_pid=$!
for ((attempt=0; attempt<100; attempt++)); do
  if rg -q 'fixture-ready' "$test_dir/fixture.log"; then break; fi
  kill -0 "$fixture_pid"
  sleep 0.05
done
rg -q 'fixture-ready' "$test_dir/fixture.log"
run_probe camera wrong-build
run_probe scene wrong-build
"$toolchain_root/usr/bin/x86_64-w64-mingw32-g++-posix" \
  -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -DUNICODE -D_UNICODE \
  -I"$project_dir/include" "$project_dir/tests/windows_scene_report_fixture.cpp" \
  -o "$test_dir/scene-report-fixture.exe" -static -static-libgcc -static-libstdc++ \
  -lbcrypt -lshell32 -lole32 -luser32
"$runtime_root/usr/lib/wine/wine64" "$test_dir/scene-report-fixture.exe" \
  >"$test_dir/synthetic-report.json" 2>"$test_dir/synthetic-report.log"
python3 - "$test_dir" <<'PY'
import json, pathlib, sys
root = pathlib.Path(sys.argv[1])
for name in ('camera', 'scene'):
    for scenario, expected in (('absent', 'client_not_running'),
                               ('wrong-build', 'build_file_size_mismatch')):
        report = json.loads((root / f'{name}-{scenario}.json').read_text())
        expected_format = 'zyon-camera-candidate-v1' if name == 'camera' else 'zyon-scene-candidate-v3'
        assert report['format'] == expected_format, report
        assert report['status'] == expected, report
        assert not report['file_sha256_matched'], report
        if scenario == 'wrong-build':
            assert not report['file_size_matched'], report
        print(f'{name}: {scenario} rejected with {expected}')
reports = list((root / 'prefix/drive_c/users').glob('*/Desktop/BloodStrike-*.json'))
assert len(reports) == 4, reports
# stdout and Desktop must contain the same four structured reports.
expected = [json.loads(p.read_text()) for p in root.glob('*-absent.json')]
expected += [json.loads(p.read_text()) for p in root.glob('*-wrong-build.json')]
assert sorted(map(lambda x: json.dumps(x, sort_keys=True), expected)) == sorted(
    json.dumps(json.loads(p.read_text()), sort_keys=True) for p in reports)
print('Four Desktop reports match stdout; uploaded client was not executed.')
fixture = json.loads((root / 'synthetic-report.json').read_text())
assert fixture['status'] == 'synthetic_fixture'
assert fixture['scene']['local_role_present'] and fixture['scene']['local_role_observed']
assert len(fixture['position_samples']) == 16
assert all(not sample['is_local_role'] for sample in fixture['position_samples'])
assert len(fixture['resource_signatures']) == 32 and fixture['resource_signatures_truncated']
assert all(item['resource_identifier'].startswith('actors/body_') for item in fixture['resource_signatures'])
assert fixture['observed_native_types'][0]['character_controller_present'] == 40
assert fixture['observed_native_types'][0]['local_roles'] == 1
assert fixture['observed_skeleton_types'][0]['count'] == 40
print('Synthetic Windows JSON: identity fields, finite projections and output caps passed.')
PY
