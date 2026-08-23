# Architecture

PWLForge implements a four-step generation flow:

1. `partitioning/` searches for an interval partition under a user-supplied
   sampled error target.
2. `fitting/` fits a linear model for each interval and evaluates a sweep of
   numeric formats.
3. `compression/` groups compatible intervals and delta-encodes the stored
   quantized data.
4. `hardware_export/` writes per-format mapping artifacts when the `hw` option
   is enabled.

The executable coordinates the flow and writes CSV reports into `results/`.
The input error target and the reported `max_mae`, `avg_mae`, and `rmse` are
sample-based metrics; they are not formal pointwise error guarantees.

The format sweep evaluates all candidates, then selects the smallest
quantized-representation candidate whose sampled average MAE satisfies the
input target (with sampled average MAE as the tie-breaker). The selected result
is written to `selected_config.txt`, while `quantization_summary.csv` retains
the complete sweep. Stage 3 and optional hardware exports use the selected
configuration only.
