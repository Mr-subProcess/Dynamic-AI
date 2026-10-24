# DRNC-Q v1.8 verification report

Verification performed in the Linux x86-64 build environment on 23 October 2026.

## Results

- Strict C++17 build with `-O2 -Wall -Wextra -Wpedantic -Werror`: PASS.
- Release build with `-static-libstdc++ -static-libgcc -s`: PASS. The resulting executable still dynamically links the host glibc/math runtime; it is not a fully static musl/glibc binary.
- Complete `tests/run_tests.sh` regression suite: PASS.
- `make sanitize` with AddressSanitizer and UndefinedBehaviorSanitizer: PASS.
- Explicit-target example validation: 32 `.drq` entry-point files passed `drnq check` (25 PC-target programs and 7 QPC-target programs).
- `qpc-report`: checked declared qubit/classical-bit counts and report boundaries.
- `qpc-optimize`: checked cancellation of adjacent identical self-inverse gates and removal of literal zero-angle rotations.
- `qpc-run --optimize` and `qpc-run-qasm --optimize`: PASS against `tests/mock_qpc_driver.sh`.
- Wrong OpenQASM major version rejected before invoking the driver: PASS.
- Bytecode corruption/checksum regression: PASS.

## Interpretation

The mock driver tests the local process ABI and QASM file hand-off only; it does not emulate hardware calibration, routing, pulse control, measurement electronics, or a physical QPU. No physical QPU was available for this release, so no real-device execution is claimed.

`qpc-report` gives a static source-order resource estimate. Its serialized-depth value is not a hardware-calibrated circuit depth. The optimizer is intentionally conservative and does not perform full circuit synthesis, commutation analysis, topology routing, or proof-producing optimization.
