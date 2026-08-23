# Release scope

This release contains the self-contained C++ generator, an expression-parser
dependency, documentation, a smoke test, shared fixed-/floating-point RTL
templates, small example mapping datasets, and timing-constraint references.
It intentionally excludes local experiment sweeps, notebooks, vendor build
directories, generated floating-point IP/netlists, synthesis reports, and
historical stage tests that are not self-contained against this implementation.

The `hw` option generates mapping data for the automatically selected
numeric-format candidate. The repository does not claim independent RTL
simulation or FPGA implementation verification for newly generated files.
See [hardware/README.md](../hardware/README.md) for the template boundary and
the local AMD/Xilinx Floating-Point IP requirement.
