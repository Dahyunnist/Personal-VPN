# Release engineering

Personal-VPN uses CMake/CPack to create a Linux server archive and a Windows desktop
client archive. A release is accepted only after core tests, Linux integration tests,
ASan/UBSan, protocol fuzzing, the benchmark smoke test, and the Windows package build
have passed in CI. A separate pinned CodeQL workflow analyzes C++ changes and runs on a
weekly schedule.

## Supply-chain controls

- Dear ImGui v1.91.9b is fetched by its full commit ID
  `f5befd2d29e66809cd1110a152e375a7f1981f06` during the Windows build.
- Wintun is pinned to 0.14.1 and its archive must match SHA-256
  `07c256185d6ee3652e09fa55c0b673e2624b565e02c4b9091c79ca7d2f24ef51`.
- Windows Boost/OpenSSL resolution uses the signed vcpkg 2026.07.29 registry baseline
  `9e593bb18ea69cc5095e012465dcd675a822ed0d`; generated dependency SPDX records are
  published beside the Windows package.
- The release workflow accepts only a tag equal to the CMake project version.
- Linux and Windows archives receive SHA-256 checksum sidecars.
- Repository policy rejects private keys, certificate bundles, local profiles, DLLs,
  executables, logs, and build trees from source control.

The Wintun downloader is opt-in (`PERSONAL_VPN_FETCH_WINTUN=ON`) so offline developer
builds do not unexpectedly access the network. An explicit local
`PERSONAL_VPN_WINTUN_DLL` remains supported.

## Versioning

Use semantic versioning. Increment:

- major for incompatible wire protocol or configuration changes;
- minor for backward-compatible capabilities;
- patch for compatible fixes and packaging/documentation changes.

The wire version and product version are deliberately separate. A product release may
retain protocol v1.

## Release checklist

1. Confirm the worktree is clean and CI is green on the release commit.
2. Update `project(PersonalVPN VERSION ...)`, changelog/release notes, and compatibility
   documentation.
3. Verify no development PKI or packet capture contains secrets or personal data.
4. Create and push the matching signed or annotated `vX.Y.Z` tag.
5. Let `.github/workflows/release.yml` build, test, checksum, and upload both archives.
6. Download both assets on clean hosts, verify checksums, inspect archive contents, and
   run the server `--help` plus a client smoke connection using disposable PKI.
7. Record the release test result and known limitations before announcing it.

Release assets are engineering artifacts, not a claim that this educational prototype
has received an external security audit or is suitable for production use.
