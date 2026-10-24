# Dynamic AI — DRNC Quantum Systems v1.2 Full Bundle

**A separate AI research prototype built using the DRNC-Q v1.8 language and runtime. This is not a modification or a new release of DRNC-Q.** The bundle contains the DRNC-Q compiler/runtime executable plus its source and notices, so the workflow is self-contained for Linux x86-64.

**Organisation:** DRNC Quantum Systems  
**Author:** Oğuz Kağan Sayın  
**Location:** Ankara, Türkiye  
**Contact:** sayink07hotmail.com@gmail.com

## What changed in v1.2

- Supervised data-driven PC matrix training from an editable CSV dataset.
- Four explicit trainable 3×3 matrices: sensory, recurrent memory, Zerya branch and output projection.
- DRNC relational state coupling: `superposfonk`, `donfonk`, `zeryafonk`, and modular evolution participate in the PC training loop.
- Matrix-weight checkpoint save/load in plain CSV files, with metadata and per-epoch loss logs.
- A separate inference entry point that loads the trained checkpoint and processes editable input rows.
- A separate online-feedback entry point that loads the existing checkpoint and updates it incrementally from labelled feedback.
- A QPC source template whose rotation angles are generated from diagonal values in the latest PC checkpoints.
- OpenQASM 3 compilation, resource report, conservative optimization and local QPU-driver ABI smoke test.
- Stronger end-to-end checks for loss reduction, checkpoint shape, inference classes, generated QPC tokens, and mock-driver ABI.

## Quick start

From the extracted project directory:

```bash
./bin/drnq --version
./scripts/run_all.sh
```

The full workflow rebuilds the training checkpoint from the model's fixed initial weights, performs local labelled online adaptation, runs inference, creates a QPC circuit from the latest matrices, emits optimized OpenQASM 3, and runs verification tests. The QPU driver test uses a mock and is explicitly **not** a physical quantum execution.

Individual commands:

```bash
./scripts/train_pc.sh          # Train with data/training.csv
./scripts/run_inference.sh     # Load the latest checkpoint; read data/inference.csv
./scripts/run_online.sh       # Continue with labelled data/online_feedback.csv
./scripts/compile_qpc.sh      # Generate a parameterized QPC source and OpenQASM 3
./scripts/verify.sh            # Verify current artifacts and integration invariants
./scripts/test_compiler.sh    # Run the bundled DRNC-Q compiler regression suite
```

`bin/drnq` is the bundled DRNC-Q v1.8 compiler/runtime. Ordinary execution needs no Python, Qiskit, cloud provider account, or external quantum API. Rebuilding `bin/drnq` from source does require a C++17 compiler and `make`:

```bash
./scripts/build_compiler.sh
```

The included executable is for Linux x86-64 and uses the host glibc. The compiler source, component license and third-party notices are preserved under `vendor/DRNC-Q-v1.8/`.

## Project layout

- `Dynamic_AI_PC.drq` — `%set[PC]` supervised recurrent matrix trainer.
- `Dynamic_AI_Infer_PC.drq` — `%set[PC]` checkpoint-backed inference.
- `Dynamic_AI_Online_PC.drq` — `%set[PC]` one-pass labelled feedback adaptation.
- `Dynamic_AI_QPC.drq` — checked-in example snapshot of the QPC target.
- `templates/Dynamic_AI_QPC.drq.in` — canonical QPC template with learned-weight placeholders.
- `modules/matrix_core.drq` — shared matrix arithmetic, bounded activation, online update, serialization and checkpoint functions.
- `data/training.csv` — editable supervised data; 3 inputs followed by 3 desired outputs per row.
- `data/validation.csv` — held-out validation rows with the same six-column schema.
- `data/inference.csv` — 3-feature rows to classify using the checkpoint.
- `data/online_feedback.csv` — labelled rows used for incremental adaptation.
- `data/training_config.csv` — epochs, learning rate, decay interval, decay factor and modular angle increment.
- `build/checkpoints/` — latest matrices and metadata, created during training.
- `results/` — console logs, loss traces, metrics, QASM circuit, optimization report and test results.
- `bin/drnq` and `vendor/DRNC-Q-v1.8/` — bundled compiler/runtime and build materials.
- `scripts/` — run, build, compiler tests, end-to-end verification and local device-driver invocation.

## PC model and learning procedure

The PC network has four trainable 3×3 matrices:

- `W_sensory`: maps input features to sensory activation.
- `W_memory`: transforms the recurrent hidden state.
- `W_branch`: maps hidden activations into the dynamic branch representation.
- `W_output`: projects branch activations to a three-component output.

At each training sample the forward path is:

`input -> W_sensory + W_memory(memory) -> bounded hidden state -> W_branch -> modular branch gain -> W_output -> prediction`

The bounded activation is `squash(x) = x / (1 + abs(x))`. The loss is mean squared error across the three output components. Weight updates use a manually implemented local delta/backpropagation-like rule through the branch/output layers, combined with recurrent context and a clipped outer-product update:

`W[i][j] := clamp(W[i][j] + eta * delta[i] * pre[j], -0.95, +0.95)`

This is a small custom online learner; it is **not** a claim of full backpropagation through time, a general-purpose neural framework, or a production-grade optimizer. It is designed to be inspectable in DRNC-Q source and easy to modify.

During training, a DRNC-generated `superposfonk` modulator evolves through the runtime's dimensionless modular parameter. The EVET/HAYIR probabilities and the DAL+/DAL− probabilities of a `zeryafonk` branch jointly determine `branch_gain = 0.75 + 0.10*(p(EVET)-p(HAYIR)) + 0.05*(p(DAL+)-p(DAL−))`, bounded to `[0.60, 0.90]`. Separate `donfonk` and `zeryafonk` relations are initialized and checked by the actual DRNC-Q runtime. These operations supply the program's relational control layer; they do not imply that the classical matrices themselves are quantum states.

## Data and configuration formats

### `data/training.csv` and `data/validation.csv`

One CSV row is `x0,x1,x2,target0,target1,target2`. Rows must contain six numeric fields. The current architecture requires exactly three input and three output values. Keep the file UTF-8 text with LF line endings and no header row.

### `data/inference.csv`

One row is `x0,x1,x2`. Results are written to `results/inference_latest.csv` with prediction components and selected argmax class index.

### `data/online_feedback.csv`

One row is `x0,x1,x2,target0,target1,target2`. Online mode loads the latest checkpoint, applies one update per non-empty record, and overwrites the checkpoint files. Keep a backup if you need the previous model.

### `data/training_config.csv`

One line with five numeric entries: `epochs,learning_rate,decay_interval,decay_factor,modular_rate`. Current safe bounds are epochs `1..35` (the bundled VM has a step budget), learning rate `(0, 0.2]`, decay interval `1..35`, decay factor `(0, 1]`, modular rate `[0, 0.01]`.

`train_pc.sh` begins from the fixed matrix initialization in the source, so training runs are reproducible under the included files. `run_online.sh` then starts from the saved training checkpoint and applies additional labelled updates.

## QPC circuit generation and PC–QPC bridge

`templates/Dynamic_AI_QPC.drq.in` is rendered by `scripts/generate_qpc_source.sh`. Twelve diagonal matrix entries (three each from `W_sensory`, `W_memory`, `W_branch` and `W_output`) are inserted as parameterized gate angles. Then `drnq qpc-compile`, `qpc-report`, and `qpc-optimize` produce an OpenQASM 3 circuit and reports.

This is a **hybrid parameter-transfer prototype**, not an exact mathematical equivalence between classical matrix multiplication and quantum evolution. The QPC circuit encodes selected learned scalars as rotation/phase angles; the circuit has its own gate dynamics and measurement outcomes. Training remains on the PC target; the current program does not perform a fully closed-loop hardware-in-the-training-loop optimization procedure.

To ask the installed local driver to execute the generated circuit:

```bash
DRNC_QPC_DRIVER=/absolute/path/to/device-specific-driver SHOTS=1024 ./scripts/run_qpc_hardware.sh
```

The driver must implement `vendor/DRNC-Q-v1.8/hardware/LOCAL_QPU_DRIVER_ABI.md`. The bundled mock driver only checks the local process ABI. No physical QPU, calibrated pulse controller, device-specific mapping/routing stack, or real hardware driver is included. A successful OpenQASM build is not a physical quantum result.

## Interpreting the included sample metrics

With the bundled data/config, the training run should decrease the reported training MSE from approximately `0.1715933` to approximately `0.1440643`, and the tiny three-row validation set currently matches all three target argmax labels (3/3), with held-out MSE around `0.2077853`. These are smoke-test results on a very small synthetic dataset; they are not a benchmark, scientific validation, or evidence of general intelligence. Changing the CSV inputs, configuration, compiler, or runtime can change them.

## DRNC semantic cautions

- `superposfonk`, `donfonk`, and `zeryafonk` are the constructors defined by the bundled DRNC-Q runtime; their state behavior is not redefined by Dynamic AI.
- Modular parameter `s` is dimensionless and is not SI seconds.
- PC weight propagation is synchronous within one process tick; it is not instantaneous physical communication.
- Quantum entanglement cannot be used for controllable faster-than-light classical signalling.
- The PC and QPC executors share an abstract topology but do not produce identical mathematical dynamics.

## Licensing

Dynamic AI and the bundled DRNC-Q component are proprietary commercial software. Read `LICENSE`, `BUNDLE_AUTHORIZATION.md`, `vendor/DRNC-Q-v1.8/LICENSE`, and `THIRD_PARTY_NOTICES.md`. This package is source-available, not open source; downloading or inspecting it does not grant free execution, modification, redistribution, or commercial deployment rights. An applicable written license from DRNC Quantum Systems is required.
