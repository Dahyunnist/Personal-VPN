# Windows desktop client design

## Ownership hierarchy

```text
UIMain
└── VPNClientCore
    └── ClientRuntime (single-use connection lifecycle)
        ├── io_context + resolver + connection deadline
        ├── TLS context + TlsTunnelClient
        │   ├── protocol ClientSessionController
        │   ├── FrameDecoder
        │   └── bounded serialized write queue
        ├── WindowsNetworkBackend
        │   ├── pinned-location Wintun module
        │   ├── adapter + packet ring
        │   └── interrupt event
        ├── ClientNetworkTransaction
        ├── I/O thread
        └── interruptible packet-device reader thread
```

There are no process-global sockets, adapters, routes, I/O contexts, stop flags, or
thread pools. Destruction proceeds from the runtime down and never detaches a thread.

## State model

```text
Stopped -> Starting -> Resolving -> Connecting
                                 -> ConfiguringNetwork -> Connected
any live state -> Stopping -> Stopped
any non-user failure -> Failed
```

`VPNClientCore::Start` reports only that startup was accepted. `UIMain::Update` reads
the runtime's atomic state each frame. This prevents the old false-positive behavior
that marked the UI connected before DNS, TCP, TLS, assignment, or Wintun setup had
completed.

The runtime has a deadline spanning resolution through authenticated IP assignment.
On success, it starts one blocking Wintun reader that is woken by a dedicated Windows
event during shutdown. Downlink delivery remains on the serialized TLS strand.

## Error boundary

Callbacks expose bounded state messages, not certificate or key contents. A remote
protocol error is reported by numeric code. Callback exceptions are contained at the
runtime boundary. Transport, adapter, route, and callback failures all converge on the
same idempotent finish path.

## Removed legacy behavior

- embedded PEM data in JSON;
- client-selected TUN IP and newline address negotiation;
- unframed TLS packet reads;
- hard-coded `10.8.0.1` route gateway;
- global `std::cout`/`std::cerr` replacement;
- command-line construction for ping/curl testing;
- sleep-based connection detection; and
- timeout-driven thread detachment.

The old `client_core_impl.cpp` and duplicated `client/` implementation were deleted
after the new executable built successfully.
