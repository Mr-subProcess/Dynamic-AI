# DRNC-Q examples

All executable entry points start with `%set[PC]` or `%set[QPC]`.

## PC target and simulator

Start with `user_functions.drq` for `fonk`, input/output/inout parameters, list-slot copy-back, return lists and recursion. `quantum_function_composition.drq` shows user-defined functions returning quantum-state handles and composing with built-ins. Other examples cover control flow, classes, standard-library calls, quantum gates, registers, noise, small algorithms, teleportation and educational BB84 simulation.

Run a PC example with `./drnq run examples/path/to/file.drq`. Compile it with `./drnq compile examples/path/to/file.drq -o program.drnb`, then execute it with `./drnq exec program.drnb`.

## QPC target

- `qpc/bell_feedforward.drq`: DRNC-generated Bell pair and measurement-conditioned correction.
- `qpc/modular_zerya.drq`: `superposfonk`, `zeryafonk`, modular phase evolution and classical feed-forward.
- `qpc/reversible_ops.drq`: custom unitary gate, controlled invocation, adjoint and power modifiers.
- `qpc/reversible_parameterized.drq`: angle-parameter inference, nested user-defined gates, multi-angle custom operations and controlled/adjoint/power variants.
- `qpc/modular_global_flow.drq`: global modular-flow lowering from `evrimfonk` and `moduleradim`.
- `qpc/teleportation_feedforward.drq`: logical teleportation circuit lowering and mid-circuit corrections.

QPC examples compile to OpenQASM 3 IR. They require a hardware-specific **local driver** to run on a real QPU. The mock driver in `tests/` tests only the local process interface and is not a physical device.
