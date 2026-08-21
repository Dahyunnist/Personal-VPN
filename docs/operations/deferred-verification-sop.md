# Deferred local verification SOP

Run this checklist after the local Windows/WSL permission issue is resolved. It does
not require production credentials; use generated development PKI only. Keep build
directories under the repository so cleanup is scoped and outputs remain ignored.

## 1. Final Linux and end-to-end suite

Open WSL and run:

```bash
set -euo pipefail
cd /mnt/e/vpn
cmake -S . -B out/final-linux -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DPERSONAL_VPN_WARNINGS_AS_ERRORS=ON
cmake --build out/final-linux --parallel 2
ctest --test-dir out/final-linux --output-on-failure --timeout 45
out/final-linux/personal-vpn-server --version
```

Expected: 18 tests pass and the version output is `personal-vpn-server 0.1.0`.

Also verify the supported server-only option combination:

```bash
cmake -S . -B out/final-server-only -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DPERSONAL_VPN_BUILD_CLIENT_TRANSPORT=OFF \
  -DPERSONAL_VPN_WARNINGS_AS_ERRORS=ON
cmake --build out/final-server-only --parallel 2
ctest --test-dir out/final-server-only --output-on-failure --timeout 45
```

Client-only TLS tests are intentionally omitted in this configuration; CMake must not
reference a disabled target.

## 2. Sanitizers

```bash
set -euo pipefail
cd /mnt/e/vpn
CC=clang CXX=clang++ cmake -S . -B out/final-sanitized -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DPERSONAL_VPN_ENABLE_SANITIZERS=ON \
  -DPERSONAL_VPN_WARNINGS_AS_ERRORS=ON
cmake --build out/final-sanitized --parallel 2
ASAN_OPTIONS=detect_leaks=1:strict_string_checks=1:abort_on_error=1 \
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
ctest --test-dir out/final-sanitized --output-on-failure --timeout 60
```

Any sanitizer report fails the phase even if the test process exits zero for another
reason.

## 3. Protocol fuzz target

```bash
set -euo pipefail
cd /mnt/e/vpn
CC=clang CXX=clang++ cmake -S . -B out/final-fuzz -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DPERSONAL_VPN_BUILD_LINUX_TRANSPORT=OFF \
  -DPERSONAL_VPN_BUILD_CLIENT_TRANSPORT=OFF \
  -DPERSONAL_VPN_BUILD_TESTS=OFF \
  -DPERSONAL_VPN_BUILD_FUZZING=ON
cmake --build out/final-fuzz --parallel 2
mkdir -p out/final-fuzz/corpus
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
out/final-fuzz/frame-decoder-fuzz \
  -dict=fuzz/protocol.dict -max_total_time=60 -timeout=5 \
  out/final-fuzz/corpus
```

Retain any crash input before changing code. A time-limited smoke test is a CI gate,
not proof that the parser has no vulnerability.

## 4. Native MSVC client and package

Open **Developer PowerShell for VS 2022 (x64)**, set `VCPKG_ROOT` to a trusted vcpkg
checkout, then run:

```powershell
Set-Location E:\vpn
vcpkg install --triplet x64-windows-static
cmake -S . -B out/final-msvc-client -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static `
  '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>' `
  -DPERSONAL_VPN_BUILD_LINUX_TRANSPORT=OFF `
  -DPERSONAL_VPN_BUILD_CLIENT_TRANSPORT=ON `
  -DPERSONAL_VPN_BUILD_WINDOWS_UI=ON `
  -DPERSONAL_VPN_FETCH_WINTUN=ON
cmake --build out/final-msvc-client --config Release --parallel 2
ctest --test-dir out/final-msvc-client -C Release --output-on-failure --timeout 45
cmake --build out/final-msvc-client --config Release --target package
```

Extract the ZIP into a new directory. It must contain the client executable, Wintun
DLL, example profile, documentation, and both ImGui and Wintun licenses. Verify that
the Wintun archive download reports no hash mismatch.

## 5. Controlled tunnel evidence

Use the two-host setup and exact direct-vs-VPN method in
[`../performance/benchmarking.md`](../performance/benchmarking.md). Run three trials per
condition plus 1-hour, 6-hour, and 24-hour soaks. Retain raw iperf3 JSON, RTT samples,
CPU/RSS, metrics, build IDs, route snapshots, and sanitized environment metadata.

Do not add throughput, latency, concurrency, uptime, or optimization percentages to
the resume until this step is complete and repeatable.

## 6. Record the result

Create a dated, sanitized Markdown summary containing commands, commits, environment,
pass/fail counts, raw-result hashes, and known deviations. Then update README evidence
and the changelog in a focused commit. GitHub authentication/push/release steps are in
[`authenticated-github-sop.md`](authenticated-github-sop.md).
