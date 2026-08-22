# Architecture

PWLForge implements a four-step generation flow:

1. `partitioning/` searches for an interval partition under a user-supplied
   sampled error target.
2. `fitting/` fits each interval and evaluates a sweep of numeric formats.
3. `compression/` groups compatible intervals and delta-encodes the stored
   quantized data.
4. `hardware_export/` writes per-format mapping artifacts when the `hw` option
   is enabled.

The executable coordinates the flow and writes CSV reports into `results/`.
The input error target and the reported `max_mae`, `avg_mae`, and `rmse` are
sample-based metrics; they are not formal pointwise error guarantees.

The format sweep deliberately emits all evaluated candidates. Choosing a final
implementation point remains a design decision based on the generated reports
and downstream implementation constraints.
