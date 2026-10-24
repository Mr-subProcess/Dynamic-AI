# DRNC-Q v1.8 Full Edition — Release Notes

Release date: 23 October 2026  
Platform package: Linux x86-64 native executable  
PC bytecode: DRNQB7 (unchanged)  
QPC circuit output: OpenQASM 3.0 intermediate representation

## New in v1.8

- `qpc-report PROGRAM.drq` compiles the source and reports statically declared qubits/classical bits, emitted quantum-operation statements, measurements, multi-qubit operations, feed-forward conditionals, custom-gate definitions, an operation histogram, output size, and a source-ordered serialized-depth estimate.
- `qpc-optimize PROGRAM.drq -o FILE.qasm` adds a conservative semantics-preserving peephole pass. It removes numeric zero-angle `p/phase/rx/ry/rz` statements and cancels adjacent identical self-inverse built-in gates (`h/x/y/z/cx/cz/swap/ccx`) in straight-line blocks. It does not optimize across custom gate definitions or classical control blocks.
- `qpc-run ... --optimize` applies that pass before handing the circuit to the explicit local driver.
- `qpc-run-qasm FILE.qasm --driver PATH --shots N [--optimize]` sends a pre-existing OpenQASM 3 file through the local hardware-specific driver ABI, which lets a workflow compile, inspect, optimize, and submit the same artifact.
- Regression coverage for optimizer rewrite counts, resource reports, QPC execution options, mock-driver hand-off, and rejection of non-OpenQASM-3 input.

## Unchanged architecture

- `.drq` source with mandatory `%set[PC]` or `%set[QPC]` in entry-point programs.
- Native C++17 compiler, DRNQB7 PC bytecode/VM, standard language constructs, user-defined functions and QPC operation declarations.
- DRNC state constructors (`superposfonk`, `donfonk`, `zeryafonk`), modular flow, state-vector simulation and OpenQASM 3 lowering.

## Verification

The strict C++17 release build and complete regression suite passed in the build environment. The local driver tests use `tests/mock_qpc_driver.sh`; this verifies the process ABI only and is not a physical-QPU test. AddressSanitizer/UndefinedBehaviorSanitizer status is reported separately in the package build notes and is not inferred from the standard regression run.

## Physical-device boundary

This release still does not bundle a physical QPU, calibrated control stack, FPGA firmware, or real device driver. `qpc-report` estimates static resources; it is not device-native scheduling, runtime, calibration, or fidelity analysis. The package does not call a cloud/provider API.
