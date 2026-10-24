# DRNC-Q v1.8 build and verification information

- Release target: Linux x86-64.
- Language implementation: C++17.
- PC bytecode format: DRNQB7; v1.8 keeps the v1.7 bytecode container layout.
- QPC circuit IR: OpenQASM 3.0.
- Packaged executable: `drnq`, built with `-O2 -Wall -Wextra -Wpedantic -Werror -static-libstdc++ -static-libgcc -s`; it uses the host Linux C runtime (`glibc`) and math library.

## Checks performed for this archive

- `g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror drnq.cpp -o drnq`
- `make release`
- `./tests/run_tests.sh`
- `make sanitize` (AddressSanitizer and UndefinedBehaviorSanitizer regression run)
- QPC report and optimizer example: `examples/qpc/optimizer_demo.drq`
- Local driver contract integration tests for normal QPC source, optimized source, and pre-existing QASM input.
- Clean extraction verification of all entry-point `.drq` examples is recorded in `TEST_REPORT.md`.

The mock QPC driver validates only local process invocation and generated file hand-off. No physical QPU or hardware-specific driver was available, so no physical quantum execution is claimed.
