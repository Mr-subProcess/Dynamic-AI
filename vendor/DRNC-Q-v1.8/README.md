# DRNC-Q v1.8 Full Edition

**DRNC-Q** is an independently implemented DRNC-oriented programming language with `.drq` source files, a native C++17 compiler, a custom `DRNQB7` bytecode/virtual machine for the PC target, a state-vector simulator, and a separate QPC circuit-lowering backend. It uses DRNC-style built-ins to construct relational sectors internally rather than asking the programmer to write `0`, `1`, `01`, or `10` as source-level state literals.

This distribution contains no Python runtime, no Qiskit dependency, and no third-party quantum-provider SDK or cloud API. PC execution is local. QPC source is compiled into OpenQASM 3 as a hardware-independent circuit IR and can be passed to a **local hardware-specific driver** through the documented process ABI. No physical QPU, calibrated pulse stack, FPGA/control card, or production device driver is included, so physical execution is not claimed or pre-verified by this package.

## 1. Target selection is explicit

The first non-comment code line in each entry-point source file must be one of:

```text
%set[PC]
```

or

```text
%set[QPC]
```

- `%set[PC]`: execute the DRNC-Q language and its classical/state-vector runtime on the host CPU.
- `%set[QPC]`: compile the source as a physical quantum circuit. `check`, `ir`, and `qpc-compile` validate/lower the circuit; **`run` requires a configured local QPU driver and will not silently run the PC simulator instead**.

Imported modules can omit a selector and inherit the entry-point target. If they contain one, it is parsed, but the entry point remains the target selector for the build.

## 2. Quick start — PC target

```sh
./drnq --version
./drnq check examples/user_functions.drq
./drnq run examples/user_functions.drq
./drnq run examples/quantum_function_composition.drq
./drnq run examples/quantum_algorithms/grover_2q.drq
```

Compile to `DRNQB7` bytecode and execute it:

```sh
./drnq compile examples/user_functions.drq -o user_functions.drnb
./drnq exec user_functions.drnb
```

Show the compiled DRNC-IR/stack bytecode listing:

```sh
./drnq ir examples/user_functions.drq
```

The supplied Linux executable does not need a separate C++ compiler to run programs. Rebuilding from the included source requires C++17:

```sh
make clean all
make check
make sanitize
make release
```

## 3. Quick start — QPC target

QPC is the physical-quantum-circuit target. The DRNC-Q source remains DRNC-style; the compiler lowers its register, gate, measurement, classical feed-forward, and modular-phase instructions into an OpenQASM 3 circuit representation.

```sh
./drnq qpc-check examples/qpc/bell_feedforward.drq
./drnq qpc-compile examples/qpc/bell_feedforward.drq -o bell.qasm
./drnq ir examples/qpc/reversible_ops.drq
```

To request execution on a real, locally attached QPU, install a driver written for that concrete device/control stack, then invoke it explicitly:

```sh
./drnq qpc-run examples/qpc/bell_feedforward.drq \
  --driver /absolute/path/to/your-local-qpu-driver \
  --shots 1024
```

Alternatively, set `DRNC_QPC_DRIVER=/absolute/path/to/your-local-qpu-driver` and run the source with `./drnq run file.drq`. The driver is a **local executable contract**, not a provider SDK or cloud API. It receives `--drncq-qpc-driver-v1 --qasm PATH --shots N`, must synchronously read the QASM file before exiting, returns a nonzero exit status on failure, and writes result/diagnostic output to stdout/stderr. See [`hardware/LOCAL_QPU_DRIVER_ABI.md`](hardware/LOCAL_QPU_DRIVER_ABI.md). Example [`examples/qpc/reversible_parameterized.drq`](examples/qpc/reversible_parameterized.drq) demonstrates parameterized nested operations and controlled/adjoint/power modifiers.

A mock driver is included only to test the process ABI. It explicitly reports that it is not a physical QPU. Do not use it as a hardware driver.

### Why a local device driver is still needed

A source-language target selector cannot create the physical qubits, microwave/laser control pulses, readout electronics, gate calibration, topology mapping, or device firmware. These vary by QPU technology and device. OpenQASM 3 is a circuit/intermediate representation; its specification deliberately does not define a universal execution environment and permits hardware-specific feature restrictions. See the [official OpenQASM 3 introduction](https://openqasm.com/intro.html). DRNC-Q ships a QPC compiler and local driver ABI, but not a fictitious “universal QPU driver.”

## 3A. QPC optimization and resource report (v1.8)

Create a static circuit resource report without running the circuit:

```sh
./drnq qpc-report examples/qpc/reversible_parameterized.drq
```

The report includes declared logical qubits and classical bits, emitted quantum-operation statements, measurements, multi-qubit operations, classical feed-forward conditionals, custom gate definitions, an operation histogram, QASM bytes, and a source-ordered serialized-depth estimate. The estimate is not a calibrated hardware depth or execution-time prediction.

Apply the conservative peephole optimizer:

```sh
./drnq qpc-optimize examples/qpc/optimizer_demo.drq -o optimized.qasm
```

It removes literal zero-angle `p/phase/rx/ry/rz` operations and cancels adjacent identical self-inverse built-in gates (`h`, `x`, `y`, `z`, `cx`, `cz`, `swap`, `ccx`) only in straight-line code. It does not rewrite inside custom gate definitions or classical-control blocks. This is intentionally conservative; it is not a general quantum compiler optimizer.

Submit a source file through an explicitly installed local driver, optionally optimized first:

```sh
./drnq qpc-run examples/qpc/bell_feedforward.drq --driver /path/to/local-qpu-driver --shots 1024 --optimize
```

Or submit a previously generated QASM artifact (with optional local optimization):

```sh
./drnq qpc-run-qasm optimized.qasm --driver /path/to/local-qpu-driver --shots 1024 --optimize
```

These commands invoke the documented local driver ABI. They do not imply that a driver or a physical QPU is included.

## 4. The DRNC relational quantum model

Developers do not declare quantum states with bit-string literals like `0`, `1`, `01`, or `10`. The runtime/target compiler creates the logical EVET/HAYIR sectors and, when relevant, DAL+/DAL− branch sectors. In QPC lowering these sectors are mapped to computational basis values internally (`EVET`/`DAL+` → `|0⟩`; `HAYIR`/`DAL−` → `|1⟩`). The labels are DRNC-Q's logical vocabulary; the mapping is a compiler convention rather than a claim that the labels are new physical observables.

Core constructors:

- `qubitfonk()`: one logical qubit initialized to the internally generated EVET sector.
- `qregfonk(n)`: a compile-time-sized register; indexed by `q[0]`, `q[1]`, etc.
- `superposfonk([phase])`: prepares a qubit in a superposition, with optional relative phase in radians.
- `donfonk()`: creates a two-qubit Bell-type register using H and CX; `partner(pair)` refers to the other qubit.
- `donfonk(q0, q1)`: applies the corresponding entangling preparation to two existing qubits.
- `zeryafonk(q)`: allocates a branch sector and couples it to `q`; a superposed source can thereby become entangled with the branch.
- `evrimfonk(q[, ds])` / `moduleradim([ds])`: explicitly advance the global dimensionless modular parameter and lower the documented sector-phase rule across all allocated QPC sectors; `evrimfonk` also requires a relational anchor.
- `teleportfonk(source, destination)`: emits a logical teleportation circuit and measurement-based corrections. It is a protocol operation, not faster-than-light transport or evidence of physical-hardware testing.

Entanglement does not need a manual “entangled bit string” declaration. It emerges from the unitary relation created by `donfonk`, `zeryafonk`, or ordinary entangling gates. Not every call to a branching constructor guarantees entanglement for every possible input; the input state and operation determine the result.

## 5. User functions and controlled reversible operations

The normal function keyword is `fonk` (`fn` and `function` remain aliases). The `operation` and `kuantumfonk` keywords declare reusable quantum gate routines for QPC lowering. For example:

```text
%set[QPC]
program drnc_reversible_ops;

operation drncRZ(girdi theta, girdi target) {
    rzgate(target, theta);
}

operation drncPhaseEntangler(girdi phi, girdi control, girdi target) {
    drncRZ(phi, control);
    cxgate(control, target);
    rzgate(target, phi / 2);
}

DRNCQ = "q", qregfonk(3);
hgate(q[0]);
drncPhaseEntangler(pi()/2, q[0], q[1]);
controlledgate("drncRZ", q[2], [q[0]], pi()/5);
adjointgate("drncRZ", pi()/5, q[0]);
powergate("drncRZ", [q[0]], -1, pi()/5);
DRNCQ = "m", measure_all(q);
```

Implemented modifier forms include:

- `controlledgate("xgate", control, target)` and, for user-defined gate routines, `controlledgate("drncCNOT", control, [target0, target1])`.
- `adjointgate("drncCNOT", q0, q1)` to request the inverse of a gate routine.
- `powergate("drncCNOT", [q0, q1], n)` to repeat a supported unitary `n` times; negative powers request the inverse for supported forms.
- `mcxgate([controls...], target)`, `mcphasegate([controls...], target, theta)`, and `mczgate([controls...], target)`.

The QPC compiler emits a gate definition for each supported user `operation` and uses the OpenQASM `ctrl`, `inv`, and other applicable modifiers in the output. These operations are syntax-level circuit transforms; `operation` bodies are restricted to unitary gate calls by the QPC backend, but this release does not prove the unitarity of arbitrary user-written mathematics or provide a formal proof system for quantum code.

## 6. Quantum operations, measurement, and classical feed-forward

QPC supported operations include H, X, Y, Z, S, T, phase, RX/RY/RZ, CX, CZ, SWAP, controlled phase, Toffoli/CCX, multi-controlled operations, user-defined unitary gate routines, reset, barrier, and measurement in Z or by basis rotation in X/Y/Z. `measure(q)`, `measure_basis(q, "X"|"Y"|"Z")`, and `measure_all(register)` must be assigned to a `DRNCQ` binding so their results can be used later.

The QPC compiler maps measurement labels to classical result bits internally. Thus source code can write:

```text
DRNCQ = "bell", donfonk();
DRNCQ = "m0", measure(bell);
if (m0 == "HAYIR") {
    xgate(partner(bell));
}
DRNCQ = "m1", measure(partner(bell));
```

without manually writing physical classical-bit values. The compiler lowers the branch to a measurement-dependent classical conditional circuit. Real-time feed-forward must be supported by the selected hardware/controller; a compiler cannot force a device to support a feature it lacks.

## 7. General-purpose language core

The PC target includes:

- dynamic `DRNCQ = "name", expression;` bindings;
- arithmetic, comparisons, short-circuit logic, math functions;
- `if / else if / else`, `for`, `while`, `break`, `continue`, block scopes;
- `fonk` functions, `girdi`/`cikti`/`girdicikti` arguments, returns, recursion;
- classes, constructors and methods; lists and indexing;
- module imports, `try/catch/throw`, console I/O and local file I/O;
- compiler diagnostics, `DRNQB7` bytecode round-trip and checksum validation.

The PC target's state-vector quantum simulator supports 1–16 qubits per shared state-vector register. The QPC compiler can lower logical registers up to 64 qubits before device-specific constraints are considered; the selected QPU driver is responsible for checking actual target limits, qubit topology and gate availability.

## 8. Tests and boundaries

- `make check` runs PC and QPC integration tests, including target selection, local driver ABI mock, DRNC constructors, gates, measurement feed-forward, custom reversible operations, and bytecode validation.
- `make sanitize` runs the regression suite under AddressSanitizer and UndefinedBehaviorSanitizer where supported.
- The included mock driver proves only that the driver process contract works; it does **not** prove physical QPU execution.
- The simulator's PRNG is not a cryptographic random generator. The educational BB84 example is not a production QKD system.
- The modular parameter `modular_s` is dimensionless and not claimed as SI time. The QPC compiler maps the defined sector-phase rule to phase gates; it does not establish a physical derivation of time from DRNC.
- No physical QPU or calibrated hardware controller is bundled. An actual hardware-specific local driver/control stack is needed for physical execution; no provider API is bundled or silently contacted.

See [`LANGUAGE_SPEC.md`](LANGUAGE_SPEC.md), [`docs/API_REFERENCE.md`](docs/API_REFERENCE.md), [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md), [`docs/SECURITY.md`](docs/SECURITY.md), [`docs/FEATURE_MATRIX.md`](docs/FEATURE_MATRIX.md), and [`hardware/LOCAL_QPU_DRIVER_ABI.md`](hardware/LOCAL_QPU_DRIVER_ABI.md). Commercial terms and source availability are described in [`LICENSE`](LICENSE).
