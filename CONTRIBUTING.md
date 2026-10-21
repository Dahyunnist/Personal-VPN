# Contributing

The repository currently serves as personal educational and portfolio evidence. Since
the code originated during an internship, external code contributions should not be
accepted until publication and relicensing rights are confirmed. Discussion and
responsible private security reports remain welcome.

For maintainer changes:

1. Create a `codex/` or descriptive feature branch; never commit credentials/builds.
2. Keep platform code behind narrow interfaces and core policy independently testable.
3. Add negative tests for parser, state, permission, timeout, and rollback changes.
4. Run warnings-as-errors builds, all applicable tests, and sanitizer/fuzz smoke tests.
5. Update the protocol, threat model, runbook, and compatibility notes when boundaries
   change.
6. Use focused commits that each build and test; do not mix generated files or secrets.

Minimum local Linux validation:

```bash
cmake -S . -B out/check -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DPERSONAL_VPN_WARNINGS_AS_ERRORS=ON
cmake --build out/check --parallel
ctest --test-dir out/check --output-on-failure
git diff --check
```

Code uses C++17, four-space indentation, Allman braces, RAII ownership, bounded remote
inputs, and exception-safe/idempotent cleanup. Avoid process-global mutable state,
detached threads, shell command construction, and concurrent operations on one TLS
stream.
