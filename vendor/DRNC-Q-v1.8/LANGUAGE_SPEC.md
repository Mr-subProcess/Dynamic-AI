# DRNC-Q v1.8 Full Edition — Language and Target Specification

This document describes the implemented v1.8 source language. It distinguishes implemented behavior from device-dependent hardware responsibilities. The compiler source and regression suite are authoritative if a concise example below omits a parser detail.

## 1. Mandatory target directive

The first non-comment, non-blank line of every entry-point `.drq` program must be exactly one of:

```text
%set[PC]
%set[QPC]
```

- `PC`: compile to DRNQB7 bytecode and execute with the native VM and local state-vector simulator.
- `QPC`: compile the DRNC-Q quantum subset to OpenQASM 3 IR. `qpc-run` calls a hardware-specific local driver. Without a driver, no physical job runs.

Unknown `%set[...]` selectors and missing selectors on entry-point files are errors. Imported modules may omit the selector; the entry point chooses the target.

## 2. Lexical/source model

Source files are UTF-8-compatible byte streams subject to the implementation's source-size bound. Comments begin with `#` or `//`. Identifiers are ASCII letters/underscore followed by letters, digits, or underscores. Strings use double quotes and the escapes `\\`, `\"`, `\n`, `\r`, and `\t`. Statements are separated by semicolons except block braces and statement forms where the parser permits omission.

## 3. High-level grammar sketch

```ebnf
entry          = target, { declaration | statement } ;
target         = "%set[PC]" | "%set[QPC]" ;
declaration    = program_decl | import_decl | function_decl | operation_decl | class_decl ;
program_decl   = "program", identifier, [";"] ;
import_decl    = "import", string, ";" ;
function_decl  = ("fonk" | "fn" | "function"), identifier, "(", [parameters], ")", block ;
operation_decl = ("operation" | "kuantumfonk"), identifier, "(", [parameters], ")", block ;
parameters     = parameter, {",", parameter} ;
parameter      = ["girdi" | "cikti" | "girdicikti" | "in" | "out" | "inout"], identifier ;
class_decl     = "class", identifier, block ;
block          = "{", { declaration | statement }, "}" ;
declaration-statement = "DRNCQ", "=", string, ",", expression, ";" ;
statement      = declaration-statement | assignment | call, ";"
               | if-statement | while-statement | for-statement
               | return-statement | break-statement | continue-statement
               | try-catch | throw-statement | block ;
if-statement   = "if", "(", expression, ")", statement,
                 ["else", statement] ;
while-statement= "while", "(", expression, ")", statement ;
for-statement  = "for", "(", assignment-or-expression, ";", expression,
                 ";", expression, ")", statement ;
return-statement = "return", [expression], ";" ;
```

This is a sketch, not a replacement parser grammar; see `drnq.cpp` for precedence, class syntax, constructor syntax, and the exact accepted statement forms.

## 4. Parameter and function rules

`fonk` defines general language functions for the PC target. Parameter mode defaults to input. `girdi`/`in` binds a value; `cikti`/`out` creates an output-only local copied back on normal return; `girdicikti`/`inout` initializes from the caller and copies the updated value back on normal return. A `return expression;` returns one value; a list packages multiple return values. Output destinations must exist, be writable, and be distinct within one call.

`operation` and `kuantumfonk` declarations are intended for reusable unitary quantum gate routines in QPC source. The QPC backend supports operation bodies composed of supported gate calls. Measurement and classic control flow inside an operation declaration are rejected. This syntactic restriction does not constitute a formal proof of unitarity for all possible definitions.

## 5. Target capability model

| Facility | PC | QPC |
|---|---|---|
| General variables, lists, classes, file I/O, exceptions | Yes | No; rejected unless supported as compile-time circuit values |
| General `fonk` runtime calls | Yes | Host-language functions are not in the circuit subset |
| `operation`/`kuantumfonk` unitary gate definitions | Usable as normal language routines | Lowered to OpenQASM gate definitions for supported bodies |
| Qubit registers/constructors | State-vector QRef handles | Compile-time logical qubit allocation |
| DRNC auto-sector constructors | State-vector semantics | Lowered to physical computational qubits with logical labels |
| Quantum gates | Local simulator | Circuit IR, later mapped by a local device driver |
| Measurement | Simulator samples and collapses | Emits hardware-circuit measurement into classical bits |
| Measurement-controlled `if` | Host control | Emits circuit-time classical feed-forward where supported by the target |
| `try/catch/throw`, console/file I/O | Yes | Rejected in circuit path |
| `density_matrix`, `sample_counts`, simulator noise diagnostics | Yes, simulation-specific | Rejected as host diagnostic helpers |
| Physical pulses and calibrated readout | No | Device-driver/control-stack responsibility |

## 6. DRNC quantum constructors — PC semantics

The runtime creates sector labels internally; user programs do not declare bit-string literals as quantum-state constructors.

- `qubitfonk()` returns one qubit initialized to EVET.
- `qregfonk(n)` creates `1 <= n <= 16` qubits in one state-vector register, all initialized to EVET, returning qubit handles in a list.
- `superposfonk([phase])` prepares `(EVET + exp(i*phase) HAYIR)/sqrt(2)`, with phase in radians.
- `donfonk()` creates a two-qubit Bell-type state by applying H to the first qubit and CX from first to second. It returns the first handle; `partner(pair)` yields the other handle.
- `donfonk(q0, q1)` applies the Bell-preparation H+CX sequence to two distinct compatible qubits.
- `zeryafonk(q)` appends a DAL+/DAL− branch qubit and applies CX from the source to that branch. It entangles a superposed source with the branch; it does not guarantee entanglement for every possible source state.
- `teleportfonk(source,destination)` simulates the standard three-qubit teleportation protocol with measurement and feed-forward in PC mode.
- `moduleradim([ds])` advances the global dimensionless internal modular parameter; `evrimfonk(q[,ds])` uses `q` as a relational anchor and advances the same global flow. Both apply the configured sector-phase rule to all sectors. This is operational software semantics, not a proof that the parameter is physical SI time.

## 7. DRNC constructors — QPC lowering

- `qregfonk(n)` allocates `n` logical QPC qubits at compile time; the portable compiler bound is 64, but physical hardware may support fewer.
- `qubitfonk()` allocates one logical qubit, initialized by the QPC execution model to its computational ground state.
- `superposfonk([phase])` allocates one qubit and emits H; if `phase` is present, emits P(phase).
- `donfonk()` allocates two qubits, emits H on the first and CX to the second. Logical sectors EVET/HAYIR are mapped internally to computational |0>/|1>.
- `donfonk(q0,q1)` as a statement emits H(q0) and CX(q0,q1).
- `zeryafonk(q)` allocates a branch qubit and emits CX(source, branch), assigning logical DAL sector metadata for modular phase lowering.
- `partner(pair)` resolves the other member of a named two-qubit constructor result.
- `measure(q)`, `measure_basis(q,"X"|"Y"|"Z")`, and `measure_all(reg)` must be bound to a `DRNCQ` variable so the classical result can be referenced later.
- `if (measurement == "HAYIR") { ... }` is lowered to classical bit comparison (`1`). EVET/DAL+ map to `0`; HAYIR/DAL− map to `1`.
- `teleportfonk(source,destination)` emits a standard logical teleportation circuit with two mid-circuit measurements and conditional Pauli corrections; actual feed-forward must be implemented by the selected device/controller.
- `evrimfonk(q[,ds])` validates a relational anchor, advances the global dimensionless modular parameter and emits phase gates on every allocated QPC qubit; `moduleradim([ds])` does the same without an anchor. The selected target's native phase implementation and calibration are driver responsibilities.

## 8. Gate API

Supported single-qubit gate calls in both semantics, subject to target restrictions: `hgate(q)`, `xgate(q)`, `ygate(q)`, `zgate(q)`, `sgate(q)`, `tgate(q)`, `phasegate(q,theta)`, `rxgate(q,theta)`, `rygate(q,theta)`, `rzgate(q,theta)`.

Two-/multi-qubit calls include `cxgate(a,b)`, `czgate(a,b)`, `swapgate(a,b)`, `cphasegate(a,b,theta)`, `ccxgate(a,b,t)`, `toffoligate(a,b,t)`, `mcxgate(controls,t)`, `mcphasegate(controls,t,theta)`, `mczgate(controls,t)`. In QPC mode, operands must be known named qubit handles or constant-index qubits; unsupported/dynamic host constructs are rejected instead of silently approximated.

QPC supports gate modifiers:

- `controlledgate("xgate", control, target)`; for a custom unitary operation, its qubit operands are a target list and angle parameters are supplied as an additional scalar/list argument, for example `controlledgate("drncRZ", control, [target], theta)`.
- `adjointgate("drncRZ", theta, target)` takes custom operation arguments in original source-signature order and emits an inverse modifier.
- `powergate("drncRZ", [target], n, theta)` separates qubit targets from angle parameter(s); powers are integer `n` in `[-32,32]`, and negative powers request inverse operations for supported forms.

QPC validates operand counts, distinct controls/targets, compile-time allocation sizes, and supported expression forms. It does not perform target-specific topology routing or physical pulse scheduling.

## 9. Measurement, classical control and target result mapping

In PC, measurement uses the simulator's Born-rule sampling and collapses the shared state vector. In QPC, measurement is emitted as a circuit measurement into an internally assigned classical bit/register. A following `if/else`, `while`, or `for` is lowered to OpenQASM 3 control-flow syntax if its expressions fit the supported circuit-time subset. Whether a particular QPU runs mid-circuit measurement and feed-forward in real time is target-specific and must be verified by the local driver/control stack.

## 10. QPC compiler and physical driver boundary

`qpc-compile source.drq -o circuit.qasm` emits OpenQASM 3 IR. OpenQASM is not a universal physical-device driver and its specification does not define a single execution environment. The compiler deliberately makes no provider API calls.

`qpc-run source.drq --driver /absolute/path/to/driver --shots N` invokes the locally installed driver with:

```text
DRIVER --drncq-qpc-driver-v1 --qasm /tmp/drncq-qpc-XXXXXX --shots N
```

The QASM temporary file remains available until the driver exits; the compiler then removes it. The driver must read the circuit synchronously, validate it against its physical target, perform qubit placement/routing/native-gate decomposition and calibrated hardware execution, and report results/diagnostics on stdout/stderr. Exit code 0 means the driver reports success; nonzero is propagated as a failure. The supplied mock driver only tests this process ABI and does not execute a physical QPU.

## 11. Bytecode and safety limits

PC bytecode uses `DRNQB7` magic/version and CRC-32 for accidental-corruption detection. CRC-32 is not a signature. Untrusted source/bytecode is not sandboxed; file I/O runs with host process permissions. The PC simulator is state-vector based and limited to 16 qubits per joint register. QPC pre-lowering allocation is limited to 64 qubits per declared register and 256 overall qubits before device-specific constraints; the physical target can impose lower limits.


## Parameterized custom operations

The QPC compiler infers each quantum-operation parameter as either a qubit or an angle based on its use in supported gate calls. It rejects parameters that are unused or used in both categories. Custom operations may call previously declared operations; forward references are rejected so the emitted gate definitions are ordered. Example: `operation drncRZ(girdi theta, girdi target) { rzgate(target, theta); }`. At a normal call site, arguments follow source declaration order. In `controlledgate` and `powergate`, qubit operands are provided separately from angle operands; one angle is a scalar, multiple angles a list. Supported operations are restricted to unitary gate-call bodies; no formal proof of unitarity is performed.


## 12. QPC analysis and optimization commands (v1.8)

These are compiler commands, not source-level quantum operations:

- `drnq qpc-report PROGRAM.drq`: compile QPC source and report static resource counts. The serialized-depth estimate assumes source-order execution over each emitted operation line and is not a physical execution-depth prediction.
- `drnq qpc-optimize PROGRAM.drq -o FILE.qasm`: write optimized OpenQASM 3 IR. The optimizer removes literal numeric zero-angle `p`, `phase`, `rx`, `ry`, `rz` statements and cancels adjacent identical built-in self-inverse `h`, `x`, `y`, `z`, `cx`, `cz`, `swap`, `ccx` instructions in straight-line blocks only. No rewrite is made inside user-defined gate bodies or classical-control blocks.
- `drnq qpc-run PROGRAM.drq --driver PATH --shots N --optimize`: compile and optimize before handing the QASM artifact to the chosen local driver.
- `drnq qpc-run-qasm FILE.qasm --driver PATH --shots N`: hand an existing OpenQASM 3 artifact to the local driver; the file must start with `OPENQASM 3.0;`.

The optimizer is a conservative peephole pass, not a proof-producing quantum compiler optimizer. Drivers must validate the OpenQASM subset they support. No physical QPU or hardware-specific driver is included by this release.
