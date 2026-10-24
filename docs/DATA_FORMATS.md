# Dynamic AI input and output formats

All CSV files use UTF-8, comma separators, LF line endings, no header row, and numeric cells only. Blank lines are ignored. The current network uses three input features and three output channels.

- `training.csv`: `x0,x1,x2,target0,target1,target2`; labelled supervised examples.
- `validation.csv`: same six-field schema; held-out evaluation examples.
- `inference.csv`: `x0,x1,x2`; inputs whose predicted values are written to an output file.
- `online_feedback.csv`: same six-field schema as training; labelled records applied incrementally to a trained checkpoint.
- `training_config.csv`: `epochs,learning_rate,decay_interval,decay_factor,modular_rate` on the first line.

Results are written under `results/`. The trainer saves each 3×3 matrix as a nine-value flattened CSV in `build/checkpoints/`. Model checkpoints are plain text and are not cryptographically signed. Keep a backup before running online-feedback updates.
