# DRNC-Q v1.8 Full Edition — Architecture

## Dual-target pipeline

Entry-point source files require `%set[PC]` or `%set[QPC]` as the first non-comment line. The directive is extracted before lexing while preserving line offsets.

### PC target

1. Lexer tokenizes `.drq` source with line/column locations.
2. Parser builds the program/function/class/statement/expression representation.
3. Semantic validation checks declarations/scopes, duplicate names, arity, return/loop context, class/methods and quantum references.
4. Bytecode compiler emits the DRNQB7 stack instruction stream and function/class manifests.
5. Container encoding stores magic/version, manifests, instructions and CRC-32.
6. Decoder validates container sizes, checksum, opcodes, jump targets, finite numeric values and function entries.
7. VM executes normal language operations and the state-vector simulator's quantum instructions.

### QPC target

1. The same lexer/parser builds the DRNC-Q AST after reading `%set[QPC]`.
2. `QPCEmitter` performs compile-time qubit allocation, operation signature inference and supported-subset checks.
3. DRNC constructors, gates, modular phase steps, measurements, measurement-conditioned `if` statements and unitary user operations lower to OpenQASM 3 circuit IR.
4. `qpc-compile` writes the IR. `qpc-check` validates/lower-checks without execution.
5. `qpc-run` invokes a local device-specific executable through the `DRNC-Q local physical-QPU driver ABI v1`. It never calls a cloud/provider API and does not fall back to the simulator. It does not guarantee actual device execution unless the local driver is a working hardware implementation.

OpenQASM 3 is used as an IR because it represents gates, measurements and supported classical feed-forward. Its specification does not define a universal execution environment; hardware backends may restrict which operations they accept. This project does not bundle a physical QPU or calibrated driver.

## DRNC constructors and logical sectors

The source does not require bit-string literals for quantum-state construction. The compiler maps DRNC logical labels internally:

- EVET ↔ computational `|0>` in QPC output.
- HAYIR ↔ computational `|1>` in QPC output.
- DAL+/DAL− ↔ branch logical labels mapped to `|0>`/`|1>` for the branch qubit.

`superposfonk` allocates a qubit and prepares H (and optional relative phase); `donfonk()` allocates two qubits and prepares H+CX; `zeryafonk(q)` allocates a branch qubit and applies CX from source to branch. Entanglement is an output of the prepared relation and input state, not a guaranteed property of every branch invocation.

## Reversible/unitary operation definitions

The parser distinguishes regular host-language `fonk` routines from circuit-level `operation`/`kuantumfonk` routines. In QPC mode the latter are lowered to gate definitions when their bodies are composed of supported unitary gate calls. The compiler supports `ctrl @`, `inv @`, integer-power expansion, and multi-controlled gates for the implemented subset. A gate definition body is syntax-checked, but the compiler does not perform theorem-prover-level unitarity proof or automatic ancilla cleanup.

## State-vector simulator and modular flow

The PC target uses `2^n` complex amplitudes per shared register, with `n <= 16`, and sector labels carried alongside qubits. Measurements use Born-rule sampling and collapse state vectors. Simulator noise helpers model stochastic pure-state trajectories, not calibrated physical-device noise or exact mixed-state propagation.

`modular_s` is dimensionless. State-mutating operations advance the internal counter according to their runtime semantics. `evrimfonk(q,ds)` and `moduleradim(ds)` explicitly apply the configured sector-phase law. In QPC the same law is lowered to relative phase gates when `ds` is a compile-time constant. The software's energy gaps are declared operational conventions; the compiler does not prove that the modular parameter is physical time.

## Local QPU driver process contract

Invocation:

```text
DRIVER --drncq-qpc-driver-v1 --qasm /tmp/drncq-qpc-XXXXXX --shots N
```

The compiler waits synchronously for the driver, removes the temporary circuit file on completion, and propagates nonzero exit codes. The driver handles target identification, topology, native-gate decomposition, calibrated pulse scheduling, readout and result mapping. It is a local process ABI, not a remote/provider API. See `hardware/LOCAL_QPU_DRIVER_ABI.md`.

## Limits and security

- Source: 8 MiB; bytecode: 64 MiB; instructions: 1,000,000.
- VM budget: 2,000,000 instructions; call depth: 256; argument count: 64.
- PC state-vector limit: 16 qubits per joint register.
- QPC logical pre-lowering limits: 64 qubits per declared register, 256 total compiler-allocated qubits and 1,024 classical bits; physical hardware may impose lower limits.
- File I/O uses host OS permissions; the VM is not an OS sandbox.
- CRC-32 detects accidental corruption, not malicious modification or provenance.
- Simulator PRNG is not a cryptographically secure RNG.


## QPC analysis and conservative optimizer (v1.8)

`qpc-report` compiles QPC source to OpenQASM 3 and counts declared registers, measurement and operation statements, multi-qubit statements, conditional statements and custom gate definitions. It computes a source-ordered serialized-depth estimate by tracking last-used layers per logical qubit. The depth is deliberately an IR-level estimate and does not model branch-dependent timing, hardware topology, native gate decomposition, pulse duration, crosstalk or calibration.

`qpc-optimize` performs only exact safe local rewrites in straight-line operation blocks: it removes literal numeric zero-angle `p`, `phase`, `rx`, `ry` and `rz` statements and cancels adjacent identical self-inverse built-in `h`, `x`, `y`, `z`, `cx`, `cz`, `swap` and `ccx` statements. It does not rewrite inside custom gate definitions or classical-control blocks and does not attempt general commutation, global-phase reasoning, synthesis, routing or physical scheduling. `qpc-run --optimize` invokes the same pass before handing source-generated QASM to the local driver. `qpc-run-qasm` accepts an existing QASM 3 artifact, checks the version header and passes the file contents through the existing local ABI.
