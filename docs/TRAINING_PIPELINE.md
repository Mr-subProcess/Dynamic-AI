# Training, checkpoint and inference pipeline

## Command sequence

```bash
./scripts/train_pc.sh
./scripts/run_inference.sh
./scripts/run_online.sh
./scripts/compile_qpc.sh
```

## Supervised training

`Dynamic_AI_PC.drq` loads `data/training.csv`, then runs a bounded number of epochs configured by `data/training_config.csv`. Each non-empty training row provides three features followed by three target values. The learner computes a three-component prediction, records per-sample squared error, calculates local deltas, updates each matrix with a clipped outer-product rule, and advances the DRNC modulator and relations.

Configuration columns: `epochs,learning_rate,decay_interval,decay_factor,modular_rate`. The supported bundled VM budget currently limits epochs to 35. Values are validated in source before training begins.

Outputs:

- `build/checkpoints/W_sensory.csv`
- `build/checkpoints/W_memory.csv`
- `build/checkpoints/W_branch.csv`
- `build/checkpoints/W_output.csv`
- `build/checkpoints/metadata.txt`
- `results/training_history.csv`
- `results/final_metrics.txt`
- `results/training_console.log`

## Checkpoint format

Each matrix file contains nine row-major numeric values separated by commas. The source functions validate the expected 3×3 shape before loading. `metadata.txt` records the checkpoint version, update count/epoch, loss and dimensionless modular parameter. This is a lightweight prototype format, not a secure signed model container. Do not load untrusted checkpoints from uncontrolled sources.

## Inference

`Dynamic_AI_Infer_PC.drq` initializes matrix shape placeholders, loads all saved matrices, reads one three-feature vector per line from `data/inference.csv`, and writes predictions with the argmax component index to `results/inference_latest.csv`. It does not train or mutate the weights.

## Labelled online feedback

`Dynamic_AI_Online_PC.drq` loads the current checkpoint and applies one local weight update per row in `data/online_feedback.csv`. This mode modifies the checkpoint and changes future inference. Save copies of the checkpoint if you need reproducibility. The included feedback data is synthetic.

## Current sample result

With the supplied files/config, the training loss decreases from about 0.1715933 to 0.1440643. The held-out MSE is about 0.2077853 and the tiny three-row fixture matches 3/3 argmax class labels. These metrics are only smoke-test expectations on a tiny synthetic example and are not representative of generalization to external data.
