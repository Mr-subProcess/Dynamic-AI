#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${BIN:-$ROOT/drnq}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cd "$ROOT"

expect_fail() {
  local label="$1"; shift
  if "$@" >"$TMP/stdout" 2>"$TMP/stderr"; then
    echo "FAIL: $label unexpectedly succeeded" >&2
    cat "$TMP/stdout" "$TMP/stderr" >&2
    exit 1
  fi
}

"$BIN" --version | grep -q 'v1.8 Full Edition'
"$BIN" --about | grep -q 'standalone native compiler'

# Control flow, math, short circuit, recursion, scopes, list mutation, exceptions.
"$BIN" check examples/control_flow_math.drq | grep -q 'PASS:'
OUT="$("$BIN" run examples/control_flow_math.drq)"
grep -q 'arithmetic: 8' <<<"$OUT"
grep -q 'if branch selected' <<<"$OUT"
grep -q 'after for: 18' <<<"$OUT"
grep -q 'after while: 3' <<<"$OUT"
grep -q 'sqrt(9)=3' <<<"$OUT"
grep -q '2\^3=8' <<<"$OUT"
grep -Fq -- '-2^2=-4' <<<"$OUT"

OUT="$("$BIN" run examples/control_flow_short_circuit.drq)"
grep -q 'short-circuit and else-if PASS' <<<"$OUT"
! grep -q 'must not print' <<<"$OUT"

OUT="$("$BIN" run examples/language_core.drq)"
grep -q 'factorial=720' <<<"$OUT"
grep -q 'items=\[1, 10, 3, 4\]' <<<"$OUT"
grep -q 'sum=18' <<<"$OUT"
grep -q 'loop_sum=25' <<<"$OUT"
grep -q 'caught=division by zero' <<<"$OUT"
grep -q 'PASS normalized' <<<"$OUT"

# User-defined classes/method dispatch and constructor path.
OUT="$("$BIN" run examples/classes.drq)"
grep -q 'type=Vector2' <<<"$OUT"
grep -q 'length=5' <<<"$OUT"
grep -q 'scaled_length=10' <<<"$OUT"
grep -q 'new_length=13' <<<"$OUT"

# Standard library: strings/lists/null/cycle-safe formatting.
OUT="$("$BIN" run examples/standard_library.drq)"
grep -q 'cycle guard PASS' <<<"$OUT"
grep -q 'standard library PASS' <<<"$OUT"

# Quantum model: auto-generated basis, gates, normalization, entanglement, modular flow.
OUT="$("$BIN" run examples/quantum_gates.drq)"
grep -q 'DRNC-generated sectors' <<<"$OUT"
grep -q 'ENTANGLED' <<<"$OUT"
grep -q 'gate sequence complete' <<<"$OUT"
OUT="$("$BIN" run examples/quantum_dynamics.drq)"
grep -q 'DRNC modular flow ACTIVE' <<<"$OUT"
grep -q 'dimensionless internal relational parameter' <<<"$OUT"
OUT="$("$BIN" run examples/entangle_variables.drq)"
grep -q 'ENTANGLED' <<<"$OUT"
grep -q 'PASS normalized' <<<"$OUT"
OUT="$("$BIN" run examples/entangle_teleport.drq)"
grep -q 'logical teleportation complete' <<<"$OUT"
grep -q 'PASS normalized' <<<"$OUT"
OUT="$("$BIN" run examples/full_drncq_demo.drq)"
grep -q 'DRNC relational branch is coherent' <<<"$OUT"
grep -q 'logical teleportation complete' <<<"$OUT"

# Expanded quantum register, indexed operands, gates, exact probabilities, modular spectral evolution.
OUT="$($BIN run examples/quantum_registers.drq)"
grep -q 'indexed multi-qubit register PASS' <<<"$OUT"
grep -q 'DRNC modular flow ACTIVE' <<<"$OUT"
printf '%s\n' '%set[PC]' > "$TMP/reset_register.drq"; cat >> "$TMP/reset_register.drq" <<'EOF2'
DRNCQ = "q", qregfonk(2);
xgate(q[0]);
resetqubit(q[0]);
assert(abs(probability(q[0], "EVET") - 1.0) < 0.00000001, "reset must return selected qubit to EVET");
assert(abs(norm(q[0]) - 1.0) < 0.00000001, "reset must preserve normalization");
print("qubit reset PASS");
EOF2
OUT="$($BIN run "$TMP/reset_register.drq")"
grep -q 'qubit reset PASS' <<<"$OUT"
OUT="$($BIN run examples/quantum_algorithms/grover_2q.drq)"
grep -q 'Grover 2-qubit algorithm PASS' <<<"$OUT"
grep -q 'measurement_all(HAYIR | HAYIR)' <<<"$OUT"
OUT="$($BIN run examples/quantum_algorithms/ghz_3q.drq)"
grep -q 'GHZ state PASS' <<<"$OUT"
grep -q 'ENTANGLED' <<<"$OUT"
OUT="$($BIN run examples/quantum_algorithms/qft_3q.drq)"
grep -q 'QFT 3-qubit unitary block PASS' <<<"$OUT"
OUT="$($BIN run examples/quantum_algorithms/deutsch.drq)"
grep -q 'Deutsch algorithm constant/balanced cases PASS' <<<"$OUT"
OUT="$($BIN run examples/quantum_algorithms/teleport_phase_test.drq)"
grep -q 'teleport relative-phase correctness PASS' <<<"$OUT"
OUT="$($BIN run examples/quantum_cryptography/bb84_simulator.drq)"
grep -q 'BB84 educational simulation complete' <<<"$OUT"
grep -q 'observed sifted bit mismatches=0' <<<"$OUT"

# Indexed qubit array operands can be wired together across separate registers.
printf '%s\n' '%set[PC]' > "$TMP/indexed_merge.drq"; cat >> "$TMP/indexed_merge.drq" <<'EOF2'
DRNCQ = "a", qregfonk(1);
DRNCQ = "b", qregfonk(1);
hgate(a[0]);
cxgate(a[0], b[0]);
assert(same_register(a[0], b[0]));
assert(verify_entanglement(a[0], b[0]));
print("indexed register merge PASS");
EOF2
OUT="$($BIN run "$TMP/indexed_merge.drq")"
grep -q 'indexed register merge PASS' <<<"$OUT"
printf '%s\n' '%set[PC]' > "$TMP/controlled_phase_merge.drq"; cat >> "$TMP/controlled_phase_merge.drq" <<'EOF2'
DRNCQ = "a", qregfonk(1);
DRNCQ = "b", qregfonk(1);
hgate(a[0]);
hgate(b[0]);
cphasegate(a[0], b[0], pi());
assert(same_register(a[0], b[0]));
assert(abs(norm(a[0]) - 1.0) < 0.00000001);
print("controlled phase merge PASS");
EOF2
OUT="$($BIN run "$TMP/controlled_phase_merge.drq")"
grep -q 'controlled phase merge PASS' <<<"$OUT"
printf '%s\n' '%set[PC]' > "$TMP/max_qubits.drq"; cat >> "$TMP/max_qubits.drq" <<'EOF2'
DRNCQ = "q", qregfonk(16);
assert(register_size(q) == 16);
assert(abs(norm(q[0]) - 1.0) < 0.00000001);
assert(abs(probabilities(q[0])[0] - 1.0) < 0.00000001);
print("16-qubit state-vector capacity PASS");
EOF2
OUT="$($BIN run "$TMP/max_qubits.drq")"
grep -q '16-qubit state-vector capacity PASS' <<<"$OUT"

# PRNG seed reproduces simulator-side sampling; this is deterministic testing, not a cryptographic RNG.
printf '%s\n' '%set[PC]' > "$TMP/seeded_measurement.drq"; cat >> "$TMP/seeded_measurement.drq" <<'EOF2'
seed(9001);
DRNCQ = "a", superposfonk();
DRNCQ = "x", measure(a);
seed(9001);
DRNCQ = "b", superposfonk();
DRNCQ = "y", measure(b);
assert(x == y, "seeded measurement should be reproducible");
print("seeded PRNG PASS");
EOF2
OUT="$($BIN run "$TMP/seeded_measurement.drq")"
grep -q 'seeded PRNG PASS' <<<"$OUT"

# Relative module import.
OUT="$("$BIN" run examples/modules/module_main.drq)"
grep -q 'square=25' <<<"$OUT"
grep -q 'cube=125' <<<"$OUT"

# Bytecode round-trip, including class and function manifests.
"$BIN" compile examples/classes.drq -o "$TMP/classes.drnb" > /dev/null
OUT="$("$BIN" exec "$TMP/classes.drnb")"
grep -q 'scaled_length=10' <<<"$OUT"
"$BIN" compile examples/modules/module_main.drq -o "$TMP/module.drnb" > /dev/null
OUT="$("$BIN" exec "$TMP/module.drnb")"
grep -q 'cube=125' <<<"$OUT"
"$BIN" ir examples/quantum_gates.drq | grep -q 'DRNC-Q v1.8 DRNQ-IR'
cp "$TMP/classes.drnb" "$TMP/corrupt.drnb"
printf 'X' | dd of="$TMP/corrupt.drnb" bs=1 seek=10 conv=notrunc status=none
expect_fail 'bytecode checksum' "$BIN" exec "$TMP/corrupt.drnb"
grep -q 'checksum mismatch' "$TMP/stderr"

# Standard file API; paths are local to this process and no service is contacted.
printf '%s\n' '%set[PC]' > "$TMP/file_io.drq"; cat >> "$TMP/file_io.drq" <<EOF2
program file_io;
DRNCQ = "path", "$TMP/data.txt";
DRNCQ = "written", write_file(path, "hello\\nworld");
assert(written == 11, "write_file byte count wrong");
assert(file_exists(path), "file_exists false after write");
DRNCQ = "content", read_file(path);
assert(content == "hello\\nworld", "read_file data mismatch");
print("file I/O PASS");
EOF2
OUT="$("$BIN" run "$TMP/file_io.drq")"
grep -q 'file I/O PASS' <<<"$OUT"
printf '%s\n' '%set[PC]' > "$TMP/input.drq"; cat >> "$TMP/input.drq" <<'EOF2'
program input_test;
DRNCQ = "raw", input("number: ");
DRNCQ = "n", to_number(raw);
assert(n == 42, "input conversion failed");
print("input PASS");
EOF2
OUT="$(printf '42\n' | "$BIN" run "$TMP/input.drq")"
grep -q 'input PASS' <<<"$OUT"

# Exceptions thrown in called functions unwind cleanly into caller's handler.
printf '%s\n' '%set[PC]' > "$TMP/throw.drq"; cat >> "$TMP/throw.drq" <<'EOF2'
program throw_test;
fn explode() { throw "intentional"; }
try { explode(); } catch (err) {
    assert(contains(err, "intentional"));
    print("throw/catch PASS");
}
EOF2
OUT="$("$BIN" run "$TMP/throw.drq")"
grep -q 'throw/catch PASS' <<<"$OUT"

# Compile-time rejection: names, arity, scope, malformed control flow, invalid qubit refs.
printf '%s\n' '%set[PC]' > "$TMP/unknown_var.drq"; cat >> "$TMP/unknown_var.drq" <<'EOF2'
program invalid;
DRNCQ = "x", 1;
print(y);
EOF2
expect_fail 'undeclared variable' "$BIN" check "$TMP/unknown_var.drq"
grep -q "undeclared variable 'y'" "$TMP/stderr"
printf '%s\n' '%set[PC]' > "$TMP/function_arity.drq"; cat >> "$TMP/function_arity.drq" <<'EOF2'
fn add(a, b) { return a + b; }
print(add(1));
EOF2
expect_fail 'function arity' "$BIN" check "$TMP/function_arity.drq"
grep -q 'expects 2 argument' "$TMP/stderr"
printf '%s\n' '%set[PC]' > "$TMP/constructor_arity.drq"; cat >> "$TMP/constructor_arity.drq" <<'EOF2'
class Pair { fn init(self, x, y) { self.x = x; self.y = y; } }
DRNCQ = "p", Pair(1);
EOF2
expect_fail 'constructor arity' "$BIN" check "$TMP/constructor_arity.drq"
printf '%s\n' '%set[PC]' > "$TMP/scope_escape.drq"; cat >> "$TMP/scope_escape.drq" <<'EOF2'
if (true) { DRNCQ = "hidden", 4; }
print(hidden);
EOF2
expect_fail 'lexical scope escape' "$BIN" check "$TMP/scope_escape.drq"
printf '%s\n' '%set[PC]' > "$TMP/break_outside.drq"; cat >> "$TMP/break_outside.drq" <<'EOF2'
break;
EOF2
expect_fail 'break outside loop' "$BIN" check "$TMP/break_outside.drq"
printf '%s\n' '%set[PC]' > "$TMP/non_qref_gate.drq"; cat >> "$TMP/non_qref_gate.drq" <<'EOF2'
DRNCQ = "q", qubitfonk();
cxgate(q, 1);
EOF2
expect_fail 'invalid controlled-gate operand' "$BIN" check "$TMP/non_qref_gate.drq"
printf '%s\n' '%set[PC]' > "$TMP/qubit_limit.drq"; cat >> "$TMP/qubit_limit.drq" <<'EOF2'
DRNCQ = "q", qregfonk(17);
EOF2
expect_fail 'qubit register limit' "$BIN" run "$TMP/qubit_limit.drq"
grep -q 'integer n in 1..16' "$TMP/stderr"
printf '%s\n' '%set[PC]' > "$TMP/duplicate.drq"; cat >> "$TMP/duplicate.drq" <<'EOF2'
DRNCQ = "x", 1;
DRNCQ = "x", 2;
EOF2
expect_fail 'duplicate global' "$BIN" check "$TMP/duplicate.drq"
printf '%s\n' '%set[PC]' > "$TMP/reserved.drq"; cat >> "$TMP/reserved.drq" <<'EOF2'
DRNCQ = "sqrt", 1;
EOF2
expect_fail 'reserved variable' "$BIN" check "$TMP/reserved.drq"
printf '%s\n' '%set[PC]' > "$TMP/divzero.drq"; cat >> "$TMP/divzero.drq" <<'EOF2'
DRNCQ = "x", 1;
print(x / 0);
EOF2
expect_fail 'division by zero' "$BIN" run "$TMP/divzero.drq"
grep -q 'division by zero' "$TMP/stderr"
printf '%s\n' '%set[PC]' > "$TMP/index_oob.drq"; cat >> "$TMP/index_oob.drq" <<'EOF2'
DRNCQ = "xs", [1];
print(xs[3]);
EOF2
expect_fail 'index bounds' "$BIN" run "$TMP/index_oob.drq"
printf '%s\n' '%set[PC]' > "$TMP/cycle_a.drq"; cat >> "$TMP/cycle_a.drq" <<EOF2
import "$TMP/cycle_b.drq";
fn a() { return 1; }
EOF2
printf '%s\n' '%set[PC]' > "$TMP/cycle_b.drq"; cat >> "$TMP/cycle_b.drq" <<EOF2
import "$TMP/cycle_a.drq";
fn b() { return 2; }
EOF2
expect_fail 'cyclic import' "$BIN" check "$TMP/cycle_a.drq"
grep -q 'cyclic module import' "$TMP/stderr"

# A real program returning no value yields null, not a dangling stack value.
printf '%s\n' '%set[PC]' > "$TMP/null_return.drq"; cat >> "$TMP/null_return.drq" <<'EOF2'
fn noop() { return; }
DRNCQ = "n", noop();
assert(type(n) == "null");
print("null return PASS");
EOF2
OUT="$("$BIN" run "$TMP/null_return.drq")"
grep -q 'null return PASS' <<<"$OUT"

# v1.2-origin shot sampling, Bloch-vector diagnostics, entropy and stochastic noise channels.
OUT="$($BIN run examples/quantum_noise.drq)"
grep -q 'shot sampling, Bloch diagnostics, entropy and stochastic noise channels PASS' <<<"$OUT"
printf '%s\n' '%set[PC]' > "$TMP/noise_probability_invalid.drq"; cat >> "$TMP/noise_probability_invalid.drq" <<'EOF2'
DRNCQ = "q", qubitfonk();
bit_flip_noise(q, 1.1);
EOF2
expect_fail 'noise probability range' "$BIN" run "$TMP/noise_probability_invalid.drq"
grep -q 'probability must be finite and in \[0,1\]' "$TMP/stderr"
printf '%s\n' '%set[PC]' > "$TMP/sample_shots_invalid.drq"; cat >> "$TMP/sample_shots_invalid.drq" <<'EOF2'
DRNCQ = "q", qubitfonk();
DRNCQ = "c", sample_counts(q, 0);
EOF2
expect_fail 'sample counts shot range' "$BIN" run "$TMP/sample_shots_invalid.drq"
grep -q 'shots must be an integer in 1..1000000' "$TMP/stderr"
printf '%s\n' '%set[PC]' > "$TMP/sample_counts_bad_operand.drq"; cat >> "$TMP/sample_counts_bad_operand.drq" <<'EOF2'
DRNCQ = "c", sample_counts(3, 10);
EOF2
expect_fail 'sample counts reference validation' "$BIN" check "$TMP/sample_counts_bad_operand.drq"
grep -q 'sample_counts requires a qubit variable or indexed register reference' "$TMP/stderr"
printf '%s\n' '%set[PC]' > "$TMP/entropy_product.drq"; cat >> "$TMP/entropy_product.drq" <<'EOF2'
DRNCQ = "q", qregfonk(2);
assert(abs(entanglement_entropy(q[0])) < 1e-8, "product state entropy must be zero");
print("product entropy PASS");
EOF2
OUT="$($BIN run "$TMP/entropy_product.drq")"
grep -q 'product entropy PASS' <<<"$OUT"
"$BIN" compile examples/quantum_noise.drq -o "$TMP/noise.drnb" >/dev/null
OUT="$($BIN exec "$TMP/noise.drnb")"
grep -q 'shot sampling, Bloch diagnostics, entropy and stochastic noise channels PASS' <<<"$OUT"


printf '%s\n' '%set[PC]' > "$TMP/fonk_duplicate_outputs.drq"; cat >> "$TMP/fonk_duplicate_outputs.drq" <<'EOF2'
fonk fill(cikti first, cikti second) { first = 1; second = 2; }
DRNCQ = "x", 0;
fill(x, x);
EOF2
expect_fail 'duplicate output destinations' "$BIN" check "$TMP/fonk_duplicate_outputs.drq"
grep -q 'must target distinct variables/list slots' "$TMP/stderr"

# v1.4: operation/quantum-function aliases, gate modifiers, basis measurement and state analyses.
OUT="$($BIN run examples/quantum_language/advanced_semantics.drq)"
grep -q 'operation/modifier/state-analysis tests PASS' <<<"$OUT"
OUT="$($BIN run examples/quantum_language/multi_controlled_gates.drq)"
grep -q 'multi-controlled X/phase gates PASS' <<<"$OUT"
OUT="$($BIN run examples/quantum_algorithms/bit_flip_qec_3q.drq)"
grep -q 'three-qubit bit-flip QEC and classical feed-forward PASS' <<<"$OUT"
printf '%s\n' '%set[PC]' > "$TMP/mcx_duplicate_control.drq"; cat >> "$TMP/mcx_duplicate_control.drq" <<'EOF2'
DRNCQ = "q", qregfonk(3);
mcxgate([q[0], q[0]], q[2]);
EOF2
expect_fail 'duplicate multi-control operand' "$BIN" run "$TMP/mcx_duplicate_control.drq"
grep -q 'controls must be distinct' "$TMP/stderr"
printf '%s\n' '%set[PC]' > "$TMP/mcx_target_control_alias.drq"; cat >> "$TMP/mcx_target_control_alias.drq" <<'EOF2'
DRNCQ = "q", qregfonk(3);
mcxgate([q[0], q[1]], q[0]);
EOF2
expect_fail 'multi-control target alias' "$BIN" run "$TMP/mcx_target_control_alias.drq"
grep -q 'target must not also be a control' "$TMP/stderr"
printf '%s\n' '%set[PC]' > "$TMP/gate_modifier_wrong_signature.drq"; cat >> "$TMP/gate_modifier_wrong_signature.drq" <<'EOF2'
DRNCQ = "q", qubitfonk();
adjointgate("hgate", q, pi());
EOF2
expect_fail 'adjoint gate arity' "$BIN" check "$TMP/gate_modifier_wrong_signature.drq"
grep -q 'expects 2 argument(s)' "$TMP/stderr"
printf '%s\n' '%set[PC]' > "$TMP/control_same_qubit.drq"; cat >> "$TMP/control_same_qubit.drq" <<'EOF2'
DRNCQ = "q", qubitfonk();
controlledgate("xgate", q, q);
EOF2
expect_fail 'controlled gate same operand' "$BIN" run "$TMP/control_same_qubit.drq"
grep -q 'control and target must differ' "$TMP/stderr"
printf '%s\n' '%set[PC]' > "$TMP/powergate_range.drq"; cat >> "$TMP/powergate_range.drq" <<'EOF2'
DRNCQ = "q", qubitfonk();
powergate("xgate", q, 33);
EOF2
expect_fail 'powergate exponent bound' "$BIN" run "$TMP/powergate_range.drq"
grep -q 'integer in \[-32,32\]' "$TMP/stderr"
printf '%s\n' '%set[PC]' > "$TMP/invalid_measure_basis.drq"; cat >> "$TMP/invalid_measure_basis.drq" <<'EOF2'
DRNCQ = "q", qubitfonk();
measure_basis(q, "W");
EOF2
expect_fail 'invalid measurement basis' "$BIN" run "$TMP/invalid_measure_basis.drq"
grep -q 'basis must be X, Y, or Z' "$TMP/stderr"
"$BIN" compile examples/quantum_language/advanced_semantics.drq -o "$TMP/advanced.drnb" >/dev/null
OUT="$($BIN exec "$TMP/advanced.drnb")"
grep -q 'operation/modifier/state-analysis tests PASS' <<<"$OUT"

# PC/QPC targets, DRNC-native QPC lowering, parameterized reversible operations, modular targeting, and local-driver ABI.
printf 'program no_target;\nprint("missing selector");\n' > "$TMP/no_target.drq"
expect_fail 'mandatory target selector' "$BIN" check "$TMP/no_target.drq"
grep -q 'missing target selector' "$TMP/stderr"
printf '%%set[GPU]\nprogram invalid_target;\n' > "$TMP/invalid_target.drq"
expect_fail 'invalid target selector' "$BIN" check "$TMP/invalid_target.drq"
grep -q 'invalid target selector' "$TMP/stderr"

"$BIN" qpc-check examples/qpc/bell_feedforward.drq | grep -q 'PASS: QPC target lowered to OpenQASM 3.0'
"$BIN" qpc-compile examples/qpc/bell_feedforward.drq -o "$TMP/bell.qasm" >/dev/null
grep -q '^OPENQASM 3.0;' "$TMP/bell.qasm"
grep -q 'cx q\[0\], q\[1\];' "$TMP/bell.qasm"
grep -q 'if ((c\[0\] == 1))' "$TMP/bell.qasm"

"$BIN" qpc-check examples/qpc/modular_zerya.drq | grep -q 'PASS: QPC target lowered'
"$BIN" qpc-compile examples/qpc/modular_zerya.drq -o "$TMP/modular.qasm" >/dev/null
grep -q 'DRNC modular evolution' "$TMP/modular.qasm"
grep -q 'c\[0\] = measure q\[1\];' "$TMP/modular.qasm"

"$BIN" qpc-check examples/qpc/reversible_ops.drq | grep -q 'PASS: QPC target lowered'
"$BIN" qpc-compile examples/qpc/reversible_ops.drq -o "$TMP/reversible.qasm" >/dev/null
grep -q 'gate drncCNOT control target' "$TMP/reversible.qasm"
grep -q 'ctrl @ drncCNOT q\[2\], q\[0\], q\[1\];' "$TMP/reversible.qasm"
grep -q 'inv @ drncCNOT q\[0\], q\[1\];' "$TMP/reversible.qasm"

# Source-signature-aware parameterized operations, nested calls, controls, adjoints and powers.
"$BIN" qpc-check examples/qpc/reversible_parameterized.drq | grep -q 'PASS: QPC target lowered'
"$BIN" qpc-compile examples/qpc/reversible_parameterized.drq -o "$TMP/reversible_parameterized.qasm" >/dev/null
grep -q 'gate drncRZ(theta) target' "$TMP/reversible_parameterized.qasm"
grep -q 'gate drncPhaseEntangler(phi) control target' "$TMP/reversible_parameterized.qasm"
grep -q 'drncRZ(phi) control;' "$TMP/reversible_parameterized.qasm"
grep -q 'ctrl @ drncRZ((pi / 5)) q\[5\], q\[4\];' "$TMP/reversible_parameterized.qasm"
grep -q 'inv @ drncRZ((pi / 5)) q\[4\];' "$TMP/reversible_parameterized.qasm"
[[ $(grep -c '^drncRZ((pi / 10)) q\[4\];$' "$TMP/reversible_parameterized.qasm") -eq 2 ]]
grep -q 'inv @ drncRZ((pi / 10)) q\[4\];' "$TMP/reversible_parameterized.qasm"
grep -q 'gate drncTwoAngle(theta, phi) target' "$TMP/reversible_parameterized.qasm"
grep -q 'drncTwoAngle((pi / 8), (pi / 11)) q\[5\], q\[4\];' "$TMP/reversible_parameterized.qasm"
grep -q 'inv @ drncTwoAngle((pi / 8), (pi / 11)) q\[4\];' "$TMP/reversible_parameterized.qasm"
[[ $(grep -c '^drncTwoAngle((pi / 6), (pi / 7)) q\[4\];$' "$TMP/reversible_parameterized.qasm") -eq 2 ]]
grep -q 'inv @ drncTwoAngle((pi / 6), (pi / 7)) q\[4\];' "$TMP/reversible_parameterized.qasm"

"$BIN" qpc-compile examples/qpc/modular_global_flow.drq -o "$TMP/modular_single.qasm" >/dev/null
[[ $(grep -c '^p(.*) q\[0\];$' "$TMP/modular_single.qasm") -eq 2 ]]
[[ $(grep -c '^p(.*) q\[1\];$' "$TMP/modular_single.qasm") -eq 2 ]]

"$BIN" qpc-check examples/qpc/teleportation_feedforward.drq | grep -q 'PASS: QPC target lowered'
"$BIN" qpc-compile examples/qpc/teleportation_feedforward.drq -o "$TMP/teleport.qasm" >/dev/null
grep -q 'if (c\[1\] == 1) { x q\[2\]; }' "$TMP/teleport.qasm"
grep -q 'if (c\[0\] == 1) { z q\[2\]; }' "$TMP/teleport.qasm"

expect_fail 'QPC run must not pretend without driver' "$BIN" run examples/qpc/bell_feedforward.drq
grep -q 'No local hardware driver is configured' "$TMP/stderr"
OUT="$("$BIN" qpc-run examples/qpc/bell_feedforward.drq --driver "$ROOT/tests/mock_qpc_driver.sh" --shots 37)"
grep -q 'MOCK DRIVER ABI PASS (not a physical QPU): shots=37' <<<"$OUT"



# v1.8 QPC static resource analysis and conservative optimizer.
REPORT="$($BIN qpc-report examples/qpc/reversible_parameterized.drq)"
grep -q 'Logical qubits declared: 6' <<<"$REPORT"
grep -q 'Classical bits declared: 5' <<<"$REPORT"
grep -q 'Measurement statements: 5' <<<"$REPORT"
grep -q 'static IR estimate' <<<"$REPORT"
"$BIN" qpc-optimize examples/qpc/optimizer_demo.drq -o "$TMP/optimized.qasm" >"$TMP/optimizer.out"
grep -q 'removed 1 identity rotation(s), canceled 2 adjacent involution pair(s)' "$TMP/optimizer.out"
! grep -q '^h q\[0\];$' "$TMP/optimized.qasm"
! grep -q '^rz(0) q\[0\];$' "$TMP/optimized.qasm"
grep -q '^x q\[0\];$' "$TMP/optimized.qasm"
"$BIN" qpc-run examples/qpc/optimizer_demo.drq --driver "$ROOT/tests/mock_qpc_driver.sh" --shots 19 --optimize >"$TMP/optimized_driver.out" 2>"$TMP/optimized_driver.err"
grep -q 'MOCK DRIVER ABI PASS (not a physical QPU): shots=19' "$TMP/optimized_driver.out"
grep -q 'DRNC-Q optimizer:' "$TMP/optimized_driver.err"
"$BIN" qpc-compile examples/qpc/optimizer_demo.drq -o "$TMP/unoptimized.qasm" >/dev/null
"$BIN" qpc-run-qasm "$TMP/unoptimized.qasm" --driver "$ROOT/tests/mock_qpc_driver.sh" --shots 23 --optimize >"$TMP/qasm_driver.out" 2>"$TMP/qasm_driver.err"
grep -q 'MOCK DRIVER ABI PASS (not a physical QPU): shots=23' "$TMP/qasm_driver.out"
grep -q 'DRNC-Q optimizer:' "$TMP/qasm_driver.err"
printf 'OPENQASM 2.0;\n' > "$TMP/not_qasm3.qasm"
expect_fail 'QPC runner rejects wrong QASM version' "$BIN" qpc-run-qasm "$TMP/not_qasm3.qasm" --driver "$ROOT/tests/mock_qpc_driver.sh" --shots 1
grep -q "must begin with 'OPENQASM 3.0;'" "$TMP/stderr"

echo 'PASS: DRNC-Q v1.8 Full Edition regression suite' 
