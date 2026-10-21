# Deferred verification and release evidence SOP

This checklist records completed local gates and gives the remaining verification
steps. It does not require production credentials; use generated development PKI only.
Keep build directories under the repository so outputs remain ignored.

## Completed local gates (2026-10-20)

- Linux Release, GCC 13.3.0, Boost 1.88.0, OpenSSL 3.0.13: 18/18 CTest tests passed,
  including initial TCP refusal, TLS handshake timeout, stale-callback rejection,
  reconnect, route rollback, server recovery, and stop-during-backoff coverage. The
  expanded reconnect integration case passed three repeated direct executions.
  `personal-vpn-server --version` returned `personal-vpn-server 0.1.0`.
- After server TLS hot reload changes, Linux Release and Clang ASan/UBSan Debug each
  passed 18/18 CTest tests. Server integration tests cover valid context replacement,
  rejection of an invalid candidate without listener outage, and continued traffic for
  established sessions after replacement.
- After metrics schema/export changes, Linux Release and Clang ASan/UBSan Debug each
  passed 19/19 tests. Exporter tests check JSON schema v1, Prometheus counter/gauge
  output, byte and queue counters, and absence of client-specific labels. CLI help lists
  both metrics formats.
- The local MinGW Windows ZIP passed the shared package verifier. A Linux archive was
  rejected for missing the Windows executable, proving the verifier's negative path.
- The SBOM archiver regression test used two `vcpkg.spdx.json` files under different
  package paths; the output retained both paths and passed archive count checks.
- Linux Release with client transport disabled and warnings-as-errors enabled: 15/15
  CTest tests passed.
- Clang ASan/UBSan Debug suite: 18/18 CTest tests passed with leak detection and
  halt-on-error enabled, including the reconnect lifecycle tests.
- Protocol libFuzzer smoke run: 928,913 executions in 26 seconds, no findings. This is
  a bounded smoke test, not proof that the decoder is vulnerability-free.
- MSVC core Release and MinGW client-core configurations: 8/8 tests passed each.
- Windows desktop Release package built with MinGW GCC 14.2.0 after reconnect changes:
  client compiled and 8/8 CTest tests passed.
  Wintun 0.14.1 was fetched with the pinned SHA-256 check; the ZIP contains the client,
  Wintun DLL and license, ImGui license, example profile, and README. ZIP SHA-256:
  `917A629638730F7044B97A97B04B2D8D4BBA18FB8A6C19B78520590D279BBB03`.

## Additional host verification (2026-10-21)

- Rebuilt the current MinGW client-core Release tree with GCC 14.2.0; 8/8 CTest tests
  passed when `C:\msys64\mingw64\bin` was on `PATH`. Without that runtime path, the
  tests fail to load MinGW runtime DLLs; this is a host invocation issue, not a code
  failure.
- Rebuilt the current MSVC core Debug tree with MSVC 14.51.36231; 8/8 CTest tests
  passed.
- Windows Release desktop package rebuilt with MinGW GCC 14.2.0 after private-key ACL
  hardening; 9/9 CTest tests passed, including current-user-only and 11 broad-principal
  DACL cases. The ZIP passed `cmake/verify-windows-package.cmake`.
- Release workflow YAML parsed successfully, and `git diff --check` passed after
  adding release-time checksum and provenance verification. The release-checksum
  verifier passed 6/6 tests for Windows CRLF input, Linux/Windows archives, tampered or
  missing checksums, path traversal, and unexpected assets. Workflow YAML parsed with
  repository-scoped attestation-read permission and source checkout; publish
  verification binds signer workflow and tag, and rejects self-hosted runners.
- Linux Release and sanitizer CTest could not run from this Windows host: the cached
  build trees use `/mnt/e/vpn`, and WSL launch was denied with `WSL_E_ACCESSDENIED`.
  Do not count those Linux gates as reverified by this host run.

These checks do not verify a real Windows VPN tunnel or establish end-to-end performance.
The remote Windows package gate and controlled two-host evidence remain pending; the
local ZIP check does not replace the vcpkg SBOM gate. See
[the authenticated GitHub SOP](authenticated-github-sop.md) and
[`../performance/benchmarking.md`](../performance/benchmarking.md).

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
cmake -S . -B out/final-msvc-client -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static `
  -DVCPKG_INSTALLED_DIR="$PWD/out/final-msvc-client/vcpkg_installed" `
  '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>' `
  -DPERSONAL_VPN_BUILD_LINUX_TRANSPORT=OFF `
  -DPERSONAL_VPN_BUILD_CLIENT_TRANSPORT=ON `
  -DPERSONAL_VPN_BUILD_WINDOWS_UI=ON `
  -DPERSONAL_VPN_FETCH_WINTUN=ON
cmake --build out/final-msvc-client --config Release --parallel 2
ctest --test-dir out/final-msvc-client -C Release --output-on-failure --timeout 45
cmake --build out/final-msvc-client --config Release --target package
Get-ChildItem out/final-msvc-client/vcpkg_installed -Recurse -Filter '*.spdx.json'
```

Extract the ZIP into a new directory. It must contain the client executable, Wintun
DLL, example profile, documentation, and both ImGui and Wintun licenses. Verify that
the Wintun archive download reports no hash mismatch. Run the same package gate used by
CI and releases:

```powershell
$package = Get-ChildItem out/final-msvc-client -Filter 'personal-vpn-*.zip' |
    Select-Object -First 1
cmake "-DPACKAGE_ARCHIVE=$($package.FullName)" -P cmake/verify-windows-package.cmake
```

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
