# Release scope

This release contains the self-contained C++ generator, an expression-parser
dependency, documentation, and a smoke test. It intentionally excludes local
experiment sweeps, notebooks, vendor build directories, synthesis reports, and
historical stage tests that are not self-contained against this implementation.

The `hw` option generates mapping data for the evaluated numeric-format
candidates. The repository does not claim independent RTL simulation or FPGA
implementation verification for those generated files.
