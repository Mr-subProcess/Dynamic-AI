# DRNC-Q Change Log

## DRNC-Q v1.8 Full Edition — 23 October 2026

- Added `qpc-report` for static OpenQASM resource accounting: declared qubits/classical bits, measurement and operation statement counts, multi-qubit operations, feed-forward conditionals, custom gate definitions, operation histogram, QASM bytes, and a source-ordered serialized-depth estimate.
- Added conservative `qpc-optimize`: removes numeric zero-angle `p/phase/rx/ry/rz` instructions and cancels adjacent, identical self-inverse built-in gates (`h/x/y/z/cx/cz/swap/ccx`) only in straight-line code, never across gate definitions or classical control blocks.
- Added optional `--optimize` to `qpc-run` and `qpc-run-qasm` for the explicit local driver ABI. Added `qpc-run-qasm` for submitting an existing OpenQASM 3 file through a local, hardware-specific driver.
- Kept physical-device execution explicit: no physical QPU, device driver, calibration model, or cloud API is bundled. Static resource reports are estimates, not a hardware scheduler or fidelity guarantee.
- Retained the DRNQB7 bytecode layout; the PC VM bytecode ABI is unchanged in this release.

---

# Changelog

## DRNC-Q v1.7 Full Edition — 23 October 2026

- Hardened QPC custom-operation signature inference: angle and qubit parameters preserve the source-signature order at call sites while compiling to OpenQASM parameter/qubit ordering.
- Added parameterized custom-operation composition. An operation can call a previously defined parameterized operation; forward references are explicitly rejected to preserve valid gate-definition order.
- Extended `controlledgate`, `adjointgate`, and `powergate` to support parameterized custom unitary operations using separate qubit-target and angle arguments. Negative powers lower to inverse modifiers for supported custom operations.
- Fixed missing semicolons in inverse custom gate statements and added nested/custom-rotation regression examples.
- Corrected QPC modular lowering to match the global modular-flow model: `evrimfonk(q, ds)` validates a relational anchor but applies the configured phase rule to all allocated sectors, consistently with PC semantics; `moduleradim(ds)` advances the same global parameter.
- Added `examples/qpc/reversible_parameterized.drq` and `examples/qpc/modular_global_flow.drq`; regression tests assert custom angle ordering, nested gate calls, controlled/inverse/power output, and global modular-phase coverage.
- Retained `DRNQB7` PC bytecode ABI; v1.7 changes QPC lowering/semantics and does not change the PC bytecode layout.

## DRNC-Q v1.6 Full Edition

- Added explicit `%set[PC]` / `%set[QPC]` target selection, QPC-to-OpenQASM 3 lowering, DRNC-native constructor handling and a local hardware-driver process ABI.
- Added local-driver hand-off without a bundled provider/cloud SDK; mock-driver tests validate only process hand-off, not physical hardware execution.
- Added QPC examples for relational Bell preparation, Zerya branching, measurement feed-forward, teleportation and reversible custom operations.

## DRNC-Q v1.4 Full Edition

- Added `operation`/`kuantumfonk`, controlled/adjoint/power modifiers, basis measurement, state analysis, multi-controlled gates and a three-qubit bit-flip QEC simulator example.

## DRNC-Q v1.2 Full Edition

- Added shot sampling, Bloch-vector diagnostics, entanglement entropy and stochastic pure-state noise trajectories.

## Implementation boundary

The PC quantum engine is a classical state-vector simulator. The QPC backend generates circuit IR and can invoke a user-installed local device driver; no physical QPU or calibrated hardware driver is bundled. Modular `s` is dimensionless and is not claimed as physical SI time.
