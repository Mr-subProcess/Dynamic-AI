# Dynamic AI bundle build information

- Product: Dynamic AI — DRNC Quantum Systems
- Release: v1.2 Full Bundle
- Primary entry points: `Dynamic_AI_PC.drq` (`%set[PC]`), `Dynamic_AI_QPC.drq` (`%set[QPC]`)
- Additional PC modes: `Dynamic_AI_Infer_PC.drq`, `Dynamic_AI_Online_PC.drq`
- Shared source module: `modules/matrix_core.drq`
- Bundled compiler/runtime: DRNC-Q v1.8 Full Edition (`bin/drnq`)
- Bundled compiler source: `vendor/DRNC-Q-v1.8/src/drnq.cpp` and `qpc_backend.inc`
- PC execution: CSV-driven supervised training, recurrent matrix propagation, DRNC relational modulation, checkpoint persistence and inference
- Online learning: labelled feedback stream with in-place checkpoint updates
- QPC artifact: learned diagonal weights mapped to parameterized unitary angles, compiled to OpenQASM 3
- Platform: Linux x86-64; bundled executable uses host glibc
- Test coverage: compiler regression, training-loss reduction, checkpoint shapes, inference classes, online checkpoint mutation, QPC compile/optimize and mock-driver ABI smoke test
- Physical QPU execution: not performed; no device-specific driver or calibrated control stack is supplied

The bundled compiler can be rebuilt with `./scripts/build_compiler.sh`. Ordinary use does not require a separately installed DRNC-Q executable, Python, Qiskit, or a cloud provider SDK. Rebuilding requires a C++17 compiler and `make`.
