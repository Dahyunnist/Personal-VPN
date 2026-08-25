# Windows network transaction

The redesigned Windows client treats adapter, address, route, and packet-ring setup as
one transaction. `ClientNetworkTransaction` owns the ordering while
`WindowsNetworkBackend` owns the operating-system and Wintun calls.

## Apply and rollback order

```text
open adapter
  -> start Wintun packet session
    -> add assigned interface address
      -> apply negotiated interface MTU
        -> add configured routes in profile order

rollback:
created routes in reverse order
  -> restore prior MTU when changed
    -> created address
      -> packet session
        -> adapter handle
```

Every backend mutation reports whether this process actually created the object. An
already-existing address or route is usable but is not recorded, so shutdown cannot
delete state owned by an administrator or another process. Exact route keys retain
the destination prefix, server-assigned gateway, and interface LUID; cleanup never
relies on one global route string.

`activate` rolls back automatically if any operation throws. The destructor and an
explicit `rollback` use the same idempotent, non-throwing path. Fault-injection tests
fail each setup step in turn and assert that only completed work is unwound.

## Wintun boundary

The Windows backend:

- loads only `wintun.dll` from the application directory or System32, avoiding the
  current working-directory DLL search path;
- resolves and validates every required export before opening the adapter;
- reuses the stable `PersonalVPN` adapter name instead of leaking timestamp-named
  adapters across reconnects;
- requests a bounded 4 MiB packet ring;
- copies received packets before releasing Wintun-owned memory and transfers each
  allocated send buffer exactly once;
- configures interface address, prefix, route gateway, and MTU inputs only from the
  authenticated `IP_ASSIGN` plus the validated route policy; and
- exposes the Wintun read event for an interruptible runtime loop rather than using
  polling sleeps or detached threads.

The DLL itself is not committed. Packaging must fetch a pinned, checksum-verified
Wintun release and place it beside the signed executable.

## Verification

The portable transaction and fault-injection suite runs on Linux, MinGW, and MSVC.
The Windows backend is compiled with `/W4 /WX /permissive-` under MSVC and equivalent
strict warnings under MinGW. A privileged Windows integration job will later exercise
the real driver; unit tests intentionally require no administrator access or machine
route mutation.
