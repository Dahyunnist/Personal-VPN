# Personal-VPN Windows client

The desktop client is a Win32/DirectX 11/Dear ImGui shell around the redesigned
`ClientRuntime`. It no longer compiles the legacy duplicated client, accepts embedded
private keys, asks the user to choose a TUN address, or assumes that one TLS read is
one IP packet.

## Runtime behavior

- The profile is strictly validated before any privileged operation.
- The exact configured DNS/IP identity is verified during mutual TLS.
- The server assigns the client address, gateway, prefix, and negotiated MTU.
- Wintun address, MTU, and all profile routes are applied transactionally.
- The UI reports real runtime states; a successful `Start` call is not displayed as a
  successful connection until TLS, protocol assignment, and Windows configuration all
  complete.
- Disconnect sends protocol `CLOSE`, interrupts the Wintun wait, joins both runtime
  threads, and rolls back only network objects created by this connection.

The former shell-based ping/curl test was removed because it combined user-controlled
text into a command line and did not prove tunnel protocol health. Connection status
now comes from the authenticated protocol state machine.

## Build

Build from the repository root. A CMake toolchain (for example vcpkg) must provide
Boost headers and OpenSSL for the selected Windows compiler.

```powershell
cmake -S . -B out/windows-client -A x64 `
  -DPERSONAL_VPN_BUILD_CLIENT_TRANSPORT=ON `
  -DPERSONAL_VPN_BUILD_WINDOWS_UI=ON `
  -DPERSONAL_VPN_WINTUN_DLL=C:/deps/wintun/bin/amd64/wintun.dll
cmake --build out/windows-client --config Release --parallel
```

Dear ImGui is fetched at the pinned `v1.91.9b` tag. Set
`PERSONAL_VPN_IMGUI_DIR` to an existing checkout for offline builds. The Wintun DLL is
not fetched implicitly: supply a pinned, independently verified DLL path and CMake
copies it beside `personal-vpn-client.exe`.

## Run

The application requires an elevated token to create the Wintun interface and IP
Helper entries. Import a schema-version-1 profile such as
`vpn_base/client/config.example.json`, then click **连接VPN** once. Routes come from the
profile; the client address is intentionally not editable.

Private keys must be stored outside the repository with a per-user ACL. See
`docs/security/client-configuration.md`.
