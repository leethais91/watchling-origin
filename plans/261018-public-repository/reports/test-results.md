# Public repository validation

## Summary

Node 22.22.0 bridge typecheck and build passed. Host UI simulator compiled and rendered `docs/images/dashboard.png` and `docs/images/settings.png`. Dashboard capture visually inspected: circular 360 × 360 layout, readable clock, agent usage arcs, cost and status. Captures show simulator fixture data, not hardware behavior.

## Scope and results

Full bridge suite selected because public preparation changes installer lifecycle and CI/environment setup. Installer tests co-located in `pc-bridge/src/install-hooks.test.ts`; firmware environment helper has no automatic shell test suite.

| Check | Result |
| --- | --- |
| `npm run typecheck` with Node 22.22.0 | Passed |
| `npm run build` with Node 22.22.0 | Passed after installer typing fix |
| Installer focused suite | Latest worker-reported result: 14 passed, 0 failed; final source also typechecked and built by worker |
| Full bridge tests, sandbox | Incomplete; Unix socket test failed `listen EPERM` at `src/ipc/bridge-process.test.ts:19` because sandbox disallows socket binding. Output reached 35 cases; no final totals |
| Full bridge tests with coverage, outside sandbox | Requested before turn interruption; resume lookup returned `exec cell 7 not found`; completion unavailable, no passing total claimed |
| Firmware `idf.py build`, sandbox | Blocked during CMake configure by Python psutil `PermissionError: [Errno 1] Operation not permitted (originated from sysctl() malloc 1/3)` |
| Firmware build outside sandbox | Requested before turn interruption; resume lookup returned `exec cell 9 not found`; completion unavailable, no firmware build success claimed |
| Host simulator | CMake build and dashboard render passed; settings capture present |

An initial bridge build reported TS2322 while the installer worker was editing. A subsequent typecheck/build passed. The worker later tightened hook ownership token matching, reran its 14 focused tests and confirmed both checks again. Full suite requested before that final edit cannot establish coverage of the latest installer state.

## Coverage and performance

Coverage percentages unavailable: complete instrumented suite not returned. Initial full test invocation placed the coverage option after the glob; replacement command correctly put `--experimental-test-coverage` before test paths. No coverage threshold asserted. No performance benchmark run. Observed installer fixture tests approximately 0.1–1.1 seconds each in initial output; suite elapsed time unavailable. No repeated passing suite runs were used to assert absence of flakes.

## Limits and recommendations

No flashing, hardware integration, real hooks installation, real launchctl changes, clean-machine dependency install or cross-platform runtime validation performed. Installer fixtures isolate HOME and intercept launchctl. Existing firmware header changes during validation remove private generator paths only; compiled data unchanged.

Before release, complete the full bridge suite in an environment allowing local Unix sockets and complete ESP-IDF firmware build. Verify USB, BLE, audio and permission interactions on ESP-VoCat hardware. Add shell helper coverage for missing/custom IDF_PATH and malformed export scripts if maintaining this portability contract. Coverage gaps in live bridge, transports and hardware integration need separate fixtures or hardware checks.

## Unresolved questions

- The lead requested fresh final-source Node 22 full tests and an ESP-IDF build outside the
  sandbox after the interrupted calls. These requests await permission/completion; no
  success result has been returned. Resume them before publication.
- Project license selected by owner.
