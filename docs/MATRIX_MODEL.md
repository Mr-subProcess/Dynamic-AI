# Dynamic AI matrix model

## Shapes and pathways

All activation vectors and trainable matrices are currently fixed to three channels. The trainable matrices are nested lists, each 3×3:

- `W_sensory`: input-to-hidden projection.
- `W_memory`: recurrent hidden-state transform.
- `W_branch`: hidden-to-relational-branch projection.
- `W_output`: branch-to-output projection.

Forward propagation for a sample is expressed in the source as:

1. `sensory_drive = squash(W_sensory · x)`
2. `memory_drive = squash(W_memory · memory_previous)`
3. `hidden = squash(sensory_drive + memory_drive)`
4. `branch_raw = squash(W_branch · hidden)`
5. Evolve the `superposfonk` modulator and `zeryafonk` branch, calculate `p(EVET)`, `p(HAYIR)`, `p(DAL+)` and `p(DAL-)`, then `branch_gain = 0.75 + 0.10 * (p(EVET) - p(HAYIR)) + 0.05 * (p(DAL+) - p(DAL-))`.
6. `branch = branch_gain * branch_raw`
7. `prediction = squash(W_output · branch)`

`squash(x) = x/(1+|x|)` bounds activation magnitudes. The branch gain remains bounded in `[0.60, 0.90]` under the stated probability semantics.

## Loss and online updates

The supervised loss for one example is the mean squared error across output channels:

`L(y, t) = (1/3) Σ_i (y_i - t_i)^2`.

The source computes local output/branch/hidden deltas using the derivative associated with `squash`; it applies outer-product-style updates. Each candidate weight is clipped to `[-0.95, 0.95]`:

`W_ij <- clamp(W_ij + η δ_i a_j, -0.95, 0.95)`.

The recurrence is used as context in forward propagation. The implemented trainer does not differentiate through the entire unrolled recurrent history; it is not exact backpropagation through time. Treat the algorithm as an experimental local-delta learner rather than a standard deep-learning optimizer.

## Dynamic data lifecycle

`data/training.csv` is loaded at startup and must have six numeric fields per non-empty row. `data/validation.csv` uses the same schema and is used only for held-out metrics. `data/inference.csv` has three numeric fields per row. `data/online_feedback.csv` has six fields and is applied as a feedback stream to a previously trained checkpoint.

`train_pc.sh` starts from fixed initial weights, so results can be compared across runs. `run_online.sh` modifies the saved state and therefore changes subsequent inference and QPC-generated gate angles.

## Interpretation boundaries

- These matrices are ordinary classical numeric arrays, not quantum matrices over amplitudes.
- PC synchronous propagation is ordinary software execution and does not imply physical zero-latency transport.
- The DRNC-generated relational state registers are separate quantum-simulation objects used to modulate/check the classical process.
- The current three-channel synthetic dataset is only an integration fixture. It does not support claims of general AI performance.
