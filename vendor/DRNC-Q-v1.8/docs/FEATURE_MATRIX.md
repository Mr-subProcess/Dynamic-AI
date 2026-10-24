# DRNC-Q v1.8 Feature Matrix


| Area | Implemented in this package | Important boundary |
|---|---|---|
| Target selection | Mandatory `%set[PC]` / `%set[QPC]` in entry-point files | QPC target is not physical execution without a local hardware driver |
| Native language | Variables, expressions, lists, index updates, scopes, functions, recursion, output parameters, classes/methods, imports, exceptions, loops and conditionals | No claimed parity with every language feature in every production language |
| PC compiler/runtime | Native C++17 compiler, DRNQB7 bytecode, VM, CRC-32 checks | CRC-32 is not a signature or sandbox |
| PC quantum simulation | State vector up to 16 qubits per shared register, gates, measurement, reset, observables, samples and stochastic noise trajectories | Not a physical device and not an exact arbitrary mixed-state simulator |
| DRNC constructors | `superposfonk`, `donfonk`, `zeryafonk`, `qregfonk`, `qubitfonk`, `partner` | Logical sector labels are mapped internally; entanglement depends on the operation and input state |
| Modular flow | Dimensionless `modular_s`, `moduleradim`, `evrimfonk`; QPC phase lowering for compile-time step sizes | Does not prove modular parameter is SI time or that chosen gaps are uniquely derived from DRNC axioms |
| Gates | H/X/Y/Z/S/T, phase/rotation gates, CX/CZ/SWAP, controlled phase, Toffoli, multi-control gates | QPC driver must route/decompose to hardware-native instructions |
| Reversible/controlled operations | `operation`/`kuantumfonk`, nested parameterized custom gates, `controlledgate`, `adjointgate`, `powergate`, `mcxgate`, `mcphasegate`, `mczgate` | Gate bodies are restricted to supported unitary gate calls; no formal unitarity theorem prover or hardware-specific decomposition |
| Measurements/feed-forward | Z measurement, X/Y/Z basis rotations, whole-register measurement, `if/else` on measurement results in QPC | Real-time measurement and feed-forward are target-specific device capabilities |
| Teleportation | Built-in logical teleportation circuit with two measurements and conditional Pauli corrections | Protocol lowering only; no physical QPU measurement has been run by this package |
| QPC compiler | DRNC-Q AST to OpenQASM 3 IR, including parameterized custom gates and measurement feed-forward, output to `.qasm` | OpenQASM 3 is an IR; the specification does not define a universal execution environment |
| Local physical hand-off | `qpc-run` launches a local driver through `DRNC-Q local physical-QPU driver ABI v1` | Actual device driver, hardware calibration, pulse firmware and real QPU are not bundled |
| External quantum provider API | None included | Local driver may be developed for a device, but provider/cloud API use is not built into this compiler |
| Cryptographic randomness | None | Simulator PRNG is not suitable for production keys or production QKD |

| QPC resource analysis | `qpc-report` reports declared register sizes, operation/measurement statement counts, multi-qubit operation statements, classical conditionals, custom gate definitions, histogram and serial-depth estimate | This is a static compiler report; not physical topology mapping, calibrated depth, runtime, fidelity or an execution result |
| QPC peephole optimizer | `qpc-optimize` removes literal zero-angle single-qubit rotations and cancels adjacent identical involutions in straight-line code; `qpc-run --optimize` uses the same pass | Intentionally conservative: no optimization inside gate definitions or control-flow blocks; no global-phase, commutation, synthesis or hardware-specific optimization |
| QASM artifact execution | `qpc-run-qasm` passes an existing OpenQASM 3 file to a specified local driver | Driver must validate input and be compatible with the selected physical control stack; the mock driver is not hardware |
