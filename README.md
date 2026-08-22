# PWLForge

PWLForge is a C++ generator for piecewise-polynomial approximations of
non-linear functions. It searches an interval partition, fits each interval,
evaluates numeric-format candidates, groups compatible data, and can emit
hardware-mapping artifacts.

This repository accompanies the ICCAD 2026 paper *Flexible Non-linear Function
Hardware Generator with Error-constrained Optimization*.

## What it does

- accepts built-in functions or ExprTk-compatible scalar expressions;
- builds an optimized or uniform interval partition;
- fits a per-interval polynomial approximation;
- evaluates floating-point and fixed-point candidates;
- groups and delta-encodes quantized interval data; and
- writes CSV reports, plus hardware-mapping files with `hw` enabled.

The generator performs a candidate sweep. It does **not** automatically select
a single minimum-cost implementation that satisfies a constraint: use the
generated reports to select the implementation point appropriate to the target
backend.

## Build

Requirements: a C++17 compiler and `make`. No package installation is needed.

```sh
make
```

On macOS the Makefile selects Apple Clang and adds the Command Line Tools SDK
headers required by the bundled ExprTk version. On Linux, it uses `g++` unless
you set `CXX` explicitly.

## Quick start

```sh
./build/pwlforge 'tanh(x)' 0 1 1e-4 \
  --interval-mode optimized --grouping-mode full
```

Results are written below `results/`. `quantization_summary.csv` records the
sampled `max_mae`, `avg_mae`, and `rmse` for each numeric-format candidate;
`stage3_all_configs_summary.csv` records the corresponding compression output.
The supplied target is evaluated as a sampled metric, not a formal worst-case
guarantee.

To request hardware-mapping artifacts for every evaluated candidate:

```sh
./build/pwlforge 'tanh(x)' 0 1 1e-4 hw
```

This writes memory-initialization files, quantized-data CSV files, and a
per-candidate configuration header. The generated mapping artifacts are not a
substitute for RTL simulation or FPGA implementation verification.

## Options

```text
./build/pwlforge <expression> <start> <end> <error> [options]

hw                          write hardware-mapping files
-p/-a/-b/-c BITS            position and polynomial-coefficient bit widths
--interval-mode MODE        optimized (default) | uniform
--uniform-intervals N       interval count for uniform mode
--grouping-mode MODE        full (default) | nogroup | nosym
```

## Reproducibility check

```sh
make test
```

This creates an isolated temporary run of `tanh(x)` over `[0, 1]` and checks
that the `Fixed2_14_Fixed2_14` candidate reports sampled average MAE below
`1e-4`. It does not replace full RTL simulation or FPGA implementation.

## Repository layout

```text
src/pwlforge/
  common/            shared types and expression helpers
  partitioning/      interval search
  fitting/           polynomial fitting and numeric-format evaluation
  compression/       grouping, symmetry detection, and delta encoding
  hardware_export/   hardware-mapping artifact writers
third_party/exprtk/  bundled expression parser
tests/               self-contained smoke test
docs/                implementation notes
```

## Citation

If you use PWLForge in academic work, please cite the accompanying ICCAD 2026
paper. Machine-readable metadata is provided in `CITATION.cff`.

## License

The project-level license has not yet been selected. Do not redistribute this
repository until a `LICENSE` file is added by the copyright holders. ExprTk is
covered separately; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
