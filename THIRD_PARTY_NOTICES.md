# Third-party and bundled component notices

This Dynamic AI v1.2 bundle contains the DRNC-Q v1.8 compiler/runtime, its relevant source files, and the compiler's applicable third-party notice files under `vendor/DRNC-Q-v1.8/`. The DRNC-Q license is preserved unchanged; the package-specific bundling statement is in `BUNDLE_AUTHORIZATION.md`.

The distributed executable includes GCC C++ runtime components under applicable GNU runtime terms. The original GCC copyright notice is included at `vendor/DRNC-Q-v1.8/THIRD_PARTY_NOTICES/GCC-14-BASE-COPYRIGHT.txt`. The executable dynamically links against the host glibc and does not bundle a copy of glibc.

No Python interpreter, Qiskit distribution, cloud-provider SDK, or cloud quantum API client is included or required by the bundled Dynamic AI scripts.
