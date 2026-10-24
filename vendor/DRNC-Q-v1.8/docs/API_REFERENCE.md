# DRNC-Q v1.8 Full Edition — Built-in API Reference

All functions below are built into the local runtime. They do not call an external quantum provider API. Unless a function is explicitly documented as changing state, its call does not advance modular flow.

## User-defined functions

Top-level function syntax: `fonk name(parameters) { ... }` (`fn` and `function` are accepted aliases).

| Parameter form | Meaning | Call-site behavior |
|---|---|---|
| `girdi x` / `in x` / `x` | Input | Evaluates any expression and binds its value to `x`.
| `cikti x` / `out x` | Output only | The local `x` starts as `null`; on normal return it is copied back into the caller's variable or constant-index list slot.
| `girdicikti x` / `inout x` | Input and output | Starts from the caller's value and copies the updated value back on normal return.

`return expression;` yields the function result, which can be nested in other calls. Use a list return to bundle multiple values, or output parameters for copy-back. Output destinations must already exist and be distinct; accepted forms are `name` and `items[0]`. If the call throws and the exception is not handled within the function, copy-back does not occur. Class methods currently support input parameters only.

Example: `fonk sum(girdi a, girdi b) { return a+b; }`; `fonk split(girdi x, cikti a, cikti b) { a=x*2; b=x*x; return [a,b]; }`.

## Core language and utility functions

| Function | Signature | Result / behavior |
|---|---|---|
| `print` / `yaz` | `print(value)` | Prints text, scalar, list, object marker or quantum state. |
| `type` | `type(value)` | Returns a runtime type name. |
| `assert` | `assert(condition[, message])` | Throws if condition is false. |
| `to_string` | `to_string(value)` | Returns the language's display representation. |
| `to_number` | `to_number(value)` | Converts finite number, bool or parseable numeric string. |
| `len` | `len(list_or_string)` | List length or string byte length. |
| `get` / `set` | `get(list,index)`, `set(list,index,value)` | Bounds-checked list access/mutation. |
| `push` / `pop` | `push(list,value)`, `pop(list)` | Append or remove the last item. |
| `contains` | `contains(string_or_list,value)` | Substring or list membership. |
| `split` / `join` | `split(text,separator)`, `join(string_list,separator)` | String/list helpers. |
| `substr` | `substr(text,start[,length])` | Byte-indexed substring. |
| `upper` / `lower` | `upper(text)`, `lower(text)` | ASCII-oriented case conversion. |
| `input` | `input([prompt])` | Reads one line from standard input. |
| `read_file` / `write_file` | `read_file(path)`, `write_file(path,content)` | Local filesystem I/O under host permissions. |
| `file_exists` | `file_exists(path)` | Checks local path existence. |

Math: `pi()`, `abs(x)`, `sqrt(x)`, `sin(x)`, `cos(x)`, `tan(x)`, `asin(x)`, `acos(x)`, `atan(x)`, `exp(x)`, `log(x)`, `log10(x)`, `floor(x)`, `ceil(x)`, `round(x)`, `pow(x,y)`, `min(x,...)`, `max(x,...)`. Trigonometric functions use radians.

## Pseudorandomness

| Function | Signature | Behavior |
|---|---|---|
| `seed` | `seed(nonnegative_integer)` | Sets the runtime PRNG seed for reproducible simulation/tests. |
| `random` | `random()` | Returns a pseudorandom real in `[0,1)`. |
| `randint` | `randint(n)` | Returns a pseudorandom integer in `[0,n)`, where `1 <= n <= 1e9`. |

These functions are **not cryptographically secure**. Do not use them to generate production keys or security-critical entropy.

## Callable quantum routines

`operation name(parameters) { ... }` and `kuantumfonk name(parameters) { ... }` are aliases for `fonk` declarations, intended for reusable quantum routines. They take the same `girdi`/`cikti`/`girdicikti` parameters and return values as functions. These declarations do not prove that the body is unitary or reversible; the routine can execute any built-in permitted by the language.

## Quantum constructors and references

| Function | Signature | Behavior |
|---|---|---|
| `qubitfonk` | `qubitfonk()` | Creates a single qubit in the internally generated EVET sector. |
| `qregfonk` | `qregfonk(n)` | Creates a joint register of `1 <= n <= 16` qubits and returns a list of `QRef` handles. Access qubit `i` as `q[i]`; indices are zero-based. |
| `superposfonk` | `superposfonk([phase])` | Creates `(EVET + exp(i*phase)*HAYIR)/sqrt(2)`. Phase is in radians. |
| `donfonk` | `donfonk()` or `donfonk(q0,q1)` | No args: creates a Bell-type pair and returns the first handle; `partner(q)` returns the other handle. Two args: applies H and CX to two distinct qubit references. |
| `partner` | `partner(q)` | Returns the other qubit reference in a two-qubit register. |
| `zeryafonk` | `zeryafonk(q)` | Appends a DAL+/DAL− relational sector, couples it to the source qubit and returns the new branch handle. |
| `register_size` | `register_size(q_or_qreg_list)` | Returns qubit count in the shared state vector. |

## Gate API

| Gate | Signature | Notes |
|---|---|---|
| Hadamard | `hgate(q)` | H gate. |
| Pauli gates | `xgate(q)`, `ygate(q)`, `zgate(q)` | Standard X/Y/Z matrices. |
| Phase gates | `sgate(q)`, `tgate(q)`, `phasegate(q,theta)` | S phase π/2, T phase π/4, or user phase. |
| Rotations | `rxgate(q,theta)`, `rygate(q,theta)`, `rzgate(q,theta)` | Rotation angle in radians. |
| Controlled gates | `cxgate(control,target)`, `czgate(a,b)`, `swapgate(a,b)` | May tensor-merge separate rebindable registers; indexed references into a shared `qregfonk` list are supported. |
| Controlled phase | `cphasegate(a,b,theta)` | Multiplies the joint HAYIR/HAYIR sector by `exp(i*theta)`. May merge separately referenced registers if references can be rebound. |
| Toffoli | `toffoligate(c1,c2,target)` / `ccxgate(c1,c2,target)` | Requires all qubits to reside in a shared register (normally created using `qregfonk(n)`). |
| Multi-controlled X | `mcxgate(controls,target)` | `controls` is a non-empty list of distinct qubit handles, or one handle; flips the target iff every control is HAYIR/|1>. All handles must already share one joint register. |
| Multi-controlled phase | `mcphasegate(controls,target,theta)` | Applies `exp(i*theta)` only to basis components where target and every control are HAYIR/|1>. All handles must already share one joint register. |
| Multi-controlled Z | `mczgate(controls,target)` | Equivalent to `mcphasegate(controls,target,pi())`. |

Example:

```text
DRNCQ = "q", qregfonk(3);
hgate(q[0]);
cxgate(q[0], q[1]);
toffoligate(q[0], q[1], q[2]);
```

When using indexed operands from different registers, automatic tensor merging is supported for constant-index references such as `left[0]` and `right[0]`. Dynamic-index references such as `left[i]` are supported for gates when the referenced qubits already share a register; merging independent registers requires rebindable direct or constant-index references.

## Gate modifiers and basis-aware measurement

| Function | Signature | Behavior |
|---|---|---|
| `controlledgate` | `controlledgate("hgate", control, target)`; rotation/phase variants take a fourth angle argument | Applies a supported single-qubit gate to `target` only when `control` is in computational HAYIR/|1>. Supports `hgate`, `xgate`, `ygate`, `zgate`, `sgate`, `tgate`, `phasegate`, `rxgate`, `rygate`, and `rzgate`. The gate name must be a string literal. |
| `adjointgate` | `adjointgate("rxgate", q, theta)`; fixed gates omit `theta` | Applies the conjugate-transpose of a supported 2x2 gate. Also supports self-adjoint `cxgate`, `czgate`, `swapgate`, `ccxgate`/`toffoligate`, and phase-inverted `cphasegate`. |
| `powergate` | `powergate("rxgate", q, integerPower, theta)`; fixed gates omit `theta` | Applies an integer power of the unitary matrix, with integer power in `[-32,32]`. Negative powers use the adjoint; zero is identity. Single-qubit gates only. |
| `measure_basis` | `measure_basis(q, "X"|"Y"|"Z")` | Performs a projective measurement in the selected Pauli basis by basis rotations, then returns the generated sector label. The register collapses and modular flow advances once. |
| `barrier` | `barrier(q_or_register)` | State-neutral, flow-neutral scheduling marker for source-level circuit boundaries. |

The compiler checks gate-name literals, supported gate names, modifier-specific arity, quantum-reference operands, and incompatible angle signatures before execution. Multi-control helpers additionally check at runtime that the control list is non-empty, controls are distinct, target is not a control, indices are valid, and all handles share one register.

## Measurement, diagnostics, and reset

| Function | Signature | Behavior |
|---|---|---|
| `measure` | `measure(q)` | Samples one qubit using Born probabilities, collapses the shared state vector and returns its sector label. |
| `measure_all` | `measure_all(q_or_qreg_list)` | Samples one joint basis outcome, collapses the register and returns labels ordered from highest qubit index to zero. |
| `resetqubit` | `resetqubit(q)` | Measures the qubit and conditionally flips it to EVET; other qubits can remain conditionally affected through entanglement. |
| `probability` | `probability(q,"EVET")` or `probability(q,"HAYIR")` | Marginal probability of a sector label for that qubit. DAL labels are valid for branch qubits. |
| `probabilities` | `probabilities(q)` | Returns a list of basis-state probabilities for the entire joint register. Index 0 is all EVET; bit `i` corresponds to qubit `q[i]`. |
| `expectation_z` | `expectation_z(q)` | Returns `P(EVET)-P(HAYIR)` for that qubit. |
| `norm` | `norm(q)` | Norm squared of the whole shared state vector. |
| `assert_normalized` | `assert_normalized(q)` | Checks the shared register has norm squared approximately 1. |
| `verify_entanglement` | `verify_entanglement(q[,other])` | Tests reduced-state purity. One arg tests that qubit against the remainder of its register; two args also checks a distinct partner in the same register. |
| `dump_state` | `dump_state(q)` | Prints non-negligible complex amplitudes and generated sector labels (up to 64 displayed components). |
| `same_register` | `same_register(a,b)` | Returns whether two qubit handles refer to the same joint state-vector register. |

## State-vector and density diagnostics

| Function | Signature | Behavior |
|---|---|---|
| `expectation_x`, `expectation_y` | `expectation_x(q)`, `expectation_y(q)` | Return reduced single-qubit Pauli expectations. |
| `expectation_pauli` | `expectation_pauli(q, "X"|"Y"|"Z")` | Selects one Pauli expectation. |
| `statevector` | `statevector(q)` | Full shared pure-state amplitude list, each complex amplitude represented as `[real,imag]`. |
| `density_matrix` | `density_matrix(q)` | Produces the full pure-state outer product `|psi><psi|`, matrix entries represented as `[real,imag]`; limited to 8 qubits. This is not a mixed-state/noisy ensemble solver. |
| `reduced_density` | `reduced_density(q)` | 2x2 reduced density matrix of one qubit as nested lists of complex pairs. |
| `purity` | `purity(q)` | `Tr(rho_q^2)` for the one-qubit reduced state. |
| `fidelity` | `fidelity(a,b)` | Squared overlap fidelity for two pure joint-register state vectors of equal dimension. If both refs share one joint register, result is 1 because they point into the same global state. Not a pairwise-qubit fidelity measure. |

## Shot sampling, observables, and noise channels

| Function | Signature | Behavior |
|---|---|---|
| `sample_counts` | `sample_counts(q_or_register, shots)` | Returns `2^n` outcome counts for repeated Born sampling without state collapse. `shots` is `1..1,000,000`; list index is the joint basis integer with qubit 0 as LSB. |
| `bloch_vector` | `bloch_vector(q)` | Returns `[<X>, <Y>, <Z>]` for the selected qubit's reduced state. |
| `entanglement_entropy` | `entanglement_entropy(q)` | Returns reduced single-qubit von Neumann entropy in bits from the global pure state. |
| `bit_flip_noise` | `bit_flip_noise(q,p)` | Applies X with probability `p`; `p` must be in `[0,1]`. |
| `phase_flip_noise` | `phase_flip_noise(q,p)` | Applies Z with probability `p`; `p` must be in `[0,1]`. |
| `depolarizing_noise` | `depolarizing_noise(q,p)` | With probability `p`, applies a uniformly selected X, Y or Z Pauli. |
| `phase_damping_noise` | `phase_damping_noise(q,gamma)` | Applies Z with probability `gamma/2`, producing ensemble off-diagonal attenuation factor `1-gamma`. |
| `amplitude_damping` | `amplitude_damping(q,gamma)` | Samples the jump/no-jump Kraus trajectory for amplitude damping and renormalizes that pure-state trajectory. |

All channel parameters must be finite and in `[0,1]`. These are stochastic pure-state trajectories, not exact density-matrix propagation or a calibrated hardware-noise model. Sampling uses the local non-cryptographic PRNG. A single noise invocation chooses one outcome; ensemble estimates require independent repeated runs. State-changing noise calls advance the modular step counter, whereas `sample_counts`, `bloch_vector`, and `entanglement_entropy` are read-only.

## Modular flow

| Function / value | Signature | Behavior |
|---|---|---|
| `modular_s` | Read-only value | Current dimensionless internal modular parameter. |
| `modular_status` | `modular_status()` | Reports `s`, step count, and configured sector values without advancing flow. |
| `moduleradim` | `moduleradim([ds])` | Advances the modular parameter by `ds` (default 1) and applies the sector-phase law to all live registers. |
| `evrimfonk` | `evrimfonk(q[,ds])` | Requires a valid qubit handle as a relational anchor, then advances the global modular parameter and applies spectral phase evolution to all live registers. |

All state-changing quantum operations advance the modular step counter. Gate functions apply the ideal gate only; spectral free evolution is explicit, through `moduleradim` or `evrimfonk`, so textbook algorithms are reproducible unless the programmer requests modular phase evolution. Read-only functions do not advance the counter. The modular parameter is dimensionless and is not calibrated to SI time.

## Error and resource model

Errors include source line and opcode context where available. Common failures include invalid arity/type, division by zero, non-finite math results, out-of-range list/qubit indices, invalid quantum operand combinations, non-normalizable states, unsupported bytecode versions and checksum mismatches. Limits: 8 MiB source, 64 MiB bytecode, 1,000,000 instructions, 2,000,000 VM steps, 256 call frames, 1,000,000 list elements and 16 qubits per state-vector register.


## Explicit target selector

Every executable entry-point source starts with `%set[PC]` or `%set[QPC]` on the first non-comment line. Missing/unknown selectors are compile-time errors.

| Selector | Meaning | Execution behavior |
|---|---|---|
| `%set[PC]` | Host-CPU target | Executes the DRNC-Q VM and local state-vector simulator. |
| `%set[QPC]` | Physical quantum-circuit target | Compiles the supported circuit subset to OpenQASM 3. Physical execution requires an explicitly configured local device driver. The compiler never silently falls back to the PC simulator. |

## QPC target constructors

The same DRNC constructor names lower to hardware-oriented circuit operations in QPC mode:

| Function | Signature | QPC lowering |
|---|---|---|
| `qregfonk` | `qregfonk(n)` | Allocate a compile-time logical register of n qubits; target can impose a lower limit. |
| `qubitfonk` | `qubitfonk()` | Allocate one logical qubit in the target's computational ground state. |
| `superposfonk` | `superposfonk([phase])` | Allocate one qubit; emit H and optionally P(phase). |
| `donfonk` | `donfonk()` | Allocate two qubits; emit H on the first and CX to the second. Returns the first handle; use `partner(pair)` for the other. |
| `donfonk` statement | `donfonk(q0,q1)` | Emit H(q0) then CX(q0,q1). |
| `zeryafonk` | `zeryafonk(q)` | Allocate a branch qubit and emit CX(q, branch). In a superposed input, this relation can produce entanglement. |
| `partner` | `partner(pair)` | Resolve the companion qubit in a two-qubit constructor register. |
| `teleportfonk` | `teleportfonk(source,destination)` | Emit Bell-pair preparation, source coupling, mid-circuit measurement and conditional X/Z corrections; rebind the destination handle to the output qubit. Hardware must support mid-circuit feed-forward. |
| `evrimfonk` | `evrimfonk(q[,ds])` | Validate the relational anchor, advance global modular flow, and lower the software's defined sector-phase law to phase gates on every allocated QPC qubit. `ds` must be compile-time constant in QPC mode. |
| `moduleradim` | `moduleradim([ds])` | Emit phase gates from the defined per-sector gaps. `ds` is dimensionless. |

DRNC-Q internally maps EVET/DAL+ to computational zero and HAYIR/DAL− to computational one. Source authors do not manually write bit-string basis states; the output backend necessarily uses physical qubit and classical-bit references.

## QPC measurement and feed-forward

| Function | Signature | Lowering |
|---|---|---|
| `measure` | `DRNCQ = "m", measure(q);` | Allocate a classical bit and emit `c[i] = measure q[k]`. |
| `measure_basis` | `DRNCQ = "m", measure_basis(q,"X"|"Y"|"Z");` | Emit the required basis rotations and measurement. |
| `measure_all` | `DRNCQ = "m", measure_all(qreg);` | Emit one measurement per qubit into a classical-bit array. |
| classical condition | `if (m == "HAYIR") { xgate(q); }` | Lower HAYIR/DAL− to classical bit 1 and EVET/DAL+ to bit 0, then emit conditional circuit control. |

## QPC custom unitary operations and modifiers

An `operation` or `kuantumfonk` definition can be lowered to an OpenQASM gate definition when its body only uses supported unitary gates and its qubit operands are operation parameters:

```text
operation drncCNOT(girdi control, girdi target) {
    cxgate(control, target);
}
```

The target accepts these forms for supported gates/operations:

- `controlledgate("xgate", control, target)`.
- `controlledgate("drncCNOT", control, [target0,target1])` for a custom gate with the listed target operands.
- `adjointgate("drncCNOT", q0, q1)`.
- `powergate("drncCNOT", [q0,q1], integerPower)` with power in `[-32,32]`; negative power requests an inverse for supported forms.
- `mcxgate([controls...], target)`, `mcphasegate([controls...], target, theta)`, and `mczgate([controls...], target)`.

These are circuit transforms, not a formal unitarity proof. For device execution the local driver still has to route/decompose the operations to native gates.

## Local QPU driver invocation (no provider API)

```sh
./drnq qpc-run program.drq --driver /absolute/path/to/local-driver --shots 1024
```

The child process receives `--drncq-qpc-driver-v1 --qasm PATH --shots N`. The driver must read the temporary QASM synchronously and implement hardware-specific topology mapping, gate decomposition, calibration, readout and execution. The mock driver in `tests/` proves only process hand-off. No physical QPU is bundled; see `hardware/LOCAL_QPU_DRIVER_ABI.md` and `hardware/PHYSICAL_EXECUTION_CHECKLIST.md`.


## Parameterized QPC operations (introduced in v1.7)

An `operation`/`kuantumfonk` declaration's parameters are written in source-signature order. The compiler infers which parameters are angles and which are qubit operands from their use in supported gate statements; a parameter used as both, or neither, is rejected. Operations can call earlier declared operations, including parameterized operations. Forward references are rejected so generated gate declarations remain ordered.

```text
operation drncRZ(girdi theta, girdi target) {
    rzgate(target, theta);
}

DRNCQ = "q", qregfonk(2);
drncRZ(pi()/3, q[0]);
controlledgate("drncRZ", q[1], [q[0]], pi()/5);
adjointgate("drncRZ", pi()/5, q[0]);
powergate("drncRZ", [q[0]], -1, pi()/5);
```

For plain calls and `adjointgate`, arguments follow the original source signature. For `controlledgate` and `powergate`, qubit targets and angle arguments are passed separately: one angle may be a scalar, while multiple angles must be a list. These modifiers lower to OpenQASM gate modifiers; they do not prove arbitrary user operations are unitary.


## QPC tooling commands (v1.8)

### `drnq qpc-report PROGRAM.drq`
Compiles a `%set[QPC]` program and prints declared logical qubits and classical bits, emitted quantum-operation statement count, measurement statement count, statements touching at least two distinct qubits, classical `if` count, custom-gate definition count, a statement histogram, QASM byte size and a source-ordered serialized-depth estimate. This report is static and is not hardware mapping or physical timing.

### `drnq qpc-optimize PROGRAM.drq -o FILE.qasm`
Compiles then applies a conservative peephole pass. It drops explicit numeric zero-angle `p`, `phase`, `rx`, `ry`, `rz` instructions and cancels adjacent identical self-inverse primitive instructions `h`, `x`, `y`, `z`, `cx`, `cz`, `swap`, `ccx` in straight-line blocks. It avoids custom gate bodies and classical-control blocks. Output is OpenQASM 3 IR; no hardware execution occurs.

### `drnq qpc-run PROGRAM.drq --driver PATH --shots N [--optimize]`
Compiles the QPC source, optionally applies the conservative optimizer, and invokes the named local driver. This is not a built-in device driver.

### `drnq qpc-run-qasm FILE.qasm --driver PATH --shots N`
Reads an existing file beginning with `OPENQASM 3.0;`, then submits its contents to the specified local driver. The driver remains responsible for full grammar validation, target compatibility, qubit routing, native-gate decomposition, calibration, and result decoding.
