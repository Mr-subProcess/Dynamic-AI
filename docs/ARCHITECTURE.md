# Dynamic AI architecture v1.2

## Component diagram

```text
training.csv + training_config.csv
               |
               v
      Dynamic_AI_PC.drq -----> DRNC-Q PC VM
               |                    |
               |                    +--> modular DRNC relation states
               |                    +--> recurrent matrix learning
               v
       build/checkpoints/*.csv
               |                     
        +------+-----------------------+
        |                              |
        v                              v
Dynamic_AI_Infer_PC.drq      Dynamic_AI_Online_PC.drq
        |                              |
        | predictions                  | labelled incremental updates
        v                              v
 results/inference_latest.csv  updated checkpoints
                                       |
                                       v
                         templates/Dynamic_AI_QPC.drq.in
                                       |
                                       v
                     parameterized QPC source + OpenQASM 3
                                       |
                                       v
                  optional local hardware-specific driver
```

## PC target

The PC path is a three-input, three-hidden-channel, three-branch-channel, three-output recurrent matrix network. It keeps four explicit 3×3 matrices (`W_sensory`, `W_memory`, `W_branch`, `W_output`) as mutable nested lists. A current sample is combined with the prior hidden state; the hidden activation is passed through the branch matrix; a DRNC superposition's EVET/HAYIR probabilities provide a bounded scalar branch gain; the output projection produces a 3-vector.

The optimizer is an inspectable online local-delta update with a recurrent context term and clipping. It is intentionally modest and makes no claim to be BPTT, Adam, transformer training, a large language model or a production AI system. Dataset shape checks and training bounds are explicit.

The DRNC relation layer consists of a sensory-memory pair, a superposition plus Zerya branch, and an additional global pair/branch. Normalization and selected entanglement properties are checked by DRNC-Q's own runtime. The classical numerical matrices remain classical weights, not quantum density matrices.

## Checkpoint lifecycle

The training entry point starts from source-declared initial matrices and writes flattened row-major CSV checkpoints plus metadata. The inference entry point uses `load_checkpoint`, checks matrix shapes, and evaluates `data/inference.csv` without reinitializing the trained weights. Online-feedback mode loads the same checkpoint, uses labelled input/target records for one-pass adaptation and saves the modified weights. All paths are relative to the project root; launcher scripts `cd` there before running.

## QPC target

The QPC template contains eight base logical nodes, automatic DRNC constructors, parameterized `matrixEdge` and `zeryaBroadcast` unitary routines, controlled/adjoint/power compositions, a global dimensionless modular step, mid-circuit measurement and source-level classical feed-forward. The build script injects twelve diagonal scalars from the current PC matrices into rotation/phase parameters.

This is a parameter bridge, not an exact quantum simulation of the floating-point neural network. The circuit is lowered to OpenQASM 3. Device-specific qubit layout, routing, decomposition to native gates, pulse scheduling, calibration and real-time feed-forward belong to a local device driver/control stack.

## Security and trust boundaries

DRNC-Q PC file I/O executes with the user's operating-system permissions. Only run source and bytecode that you trust. The checked-in compiler executable targets Linux x86-64; rebuilding from `vendor/DRNC-Q-v1.8/src` requires C++17 and `make`. The local QPU driver interface is an external executable contract, not a sandbox and not a cloud service.
