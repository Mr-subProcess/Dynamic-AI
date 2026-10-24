# Security notes

- This package is commercially licensed. See `LICENSE` before copying, building, executing, modifying, or redistributing it.
- `read_file`, `write_file`, `input` and other source facilities execute with the host process's filesystem permissions. The VM is **not** an operating-system sandbox. Run untrusted programs in a separately restricted OS account or container.
- The `DRNQB7` container uses CRC-32 to detect accidental corruption. CRC-32 is not a cryptographic signature, code provenance mechanism, or trust boundary. Do not run bytecode from untrusted sources.
- VM instruction, call-depth, source-size, bytecode-size, list-size and qubit-count limits reduce accidental runaway resource consumption but do not provide a formal denial-of-service guarantee.
- `seed`, `random`, `randint`, and state-vector measurements use a standard pseudorandom generator. They are unsuitable for secrets, session keys, production QKD, or cryptographic key generation.
- The BB84 example is illustrative only. It omits a real quantum channel, an authenticated classical channel, eavesdropper/noise models, error correction, privacy amplification, side-channel protections, and security proof.
- A future physical device driver would require its own threat model, authentication, firmware integrity, transport framing, timing and failsafe analysis. No physical QPU is controlled by this package.


## Simulator and noise-channel security boundary (v1.2+)

- `sample_counts`, state measurement, and stochastic noise channels use the runtime's standard PRNG. This is not a cryptographic random generator and must not generate production secrets, keys, or nonces.
- Noise channels are idealized Monte Carlo trajectories, not device-calibrated physical noise or a security proof. The BB84 example remains educational and does not implement authenticated classical communications, a full adversary model, error correction, or privacy amplification.
- Local file operations continue to run with the OS permissions of the DRNC-Q process. The VM is not an operating-system sandbox.


## Function output parameters

The compiler accepts output targets only when they are a named variable or constant-index list slot, requires that the target exists, and rejects duplicate output targets in one call. Copy-back occurs only on normal return. This feature is not a memory-safety or OS sandbox boundary; programs retain the local file-I/O capabilities of the host process. Bytecode CRC-32 detects accidental corruption but is not a signature.


## v1.4 modifier and diagnostic boundaries

- `operation` and `kuantumfonk` declarations are not unitary-verified. Treat source from untrusted authors as arbitrary programs, not as mathematically verified quantum circuits.
- Gate names passed to modifiers are constrained and checked, but this is not a formal verification proof.
- `density_matrix` returns the outer product of the current pure simulated state; it does not mean the runtime protects against mixed-state modelling errors.
- The simulator is limited to 16 qubits per state vector and dense `density_matrix` output to 8 qubits; input sizes can still consume substantial CPU/RAM within those bounds.


## v1.8 QPC driver boundary

- The QPC compiler emits OpenQASM 3 circuit IR; it does not create a physical QPU, calibrated controller, or device firmware. The local driver contract is not an execution guarantee.
- The mock driver is test-only and explicitly identifies itself as non-physical. Do not interpret its success as evidence that a quantum circuit executed on hardware.
- User-defined quantum operations are checked against the supported syntax and parameter-kind rules, but the compiler does not prove unitarity or physical realizability. The target-specific driver must reject unsupported gates and unavailable real-time classical feed-forward.


## v1.8 QPC optimizer scope

The peephole optimizer is deliberately limited to literal zero-angle rotations and adjacent matching self-inverse gate statements in straight-line code. It does not parse or rewrite inside custom gate definitions or classical-control blocks. The QPC report is advisory metadata; drivers must independently validate OpenQASM and reject unsupported operations before device execution. Passing input to a local executable driver is a trust boundary: do not run an untrusted driver, and inspect/validate the QASM artifact before sending it to any device.
