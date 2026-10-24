# Bundled compiler integration

Dynamic AI's launch scripts resolve the compiler in this order:

1. `DRNQ_BIN`, if explicitly set to an executable path or command in `PATH`;
2. the bundled executable at `bin/drnq`.

The bundle pins its verification checks to the `DRNC-Q v1.8 Full Edition` CLI version. Keep the compiler and its source pair synchronized: `vendor/DRNC-Q-v1.8/src/drnq.cpp` includes `qpc_backend.inc` from the same source directory.

To rebuild the bundled compiler:

```bash
./scripts/build_compiler.sh
```

That command overwrites `bin/drnq`, so preserve a backup if comparing builds. Rebuild requires a C++17 compiler and `make`. Rebuilding does not alter Dynamic AI's `.drq` entry-point sources.

The bundled binary is a Linux x86-64 ELF executable and uses the host glibc. On an incompatible Linux architecture or non-Linux operating system, rebuild from source using a supported host toolchain or provide a compatible DRNC-Q executable via `DRNQ_BIN`.
