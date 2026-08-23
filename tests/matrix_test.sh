#!/usr/bin/env sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
binary="$repo_dir/build/pwlforge"
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/pwlforge-matrix.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

run_case() {
  name=$1
  expression=$2
  start=$3
  end=$4
  expected=$5

  case_dir="$work_dir/$name"
  mkdir -p "$case_dir"
  (
    cd "$case_dir"
    "$binary" "$expression" "$start" "$end" 1e-4 hw \
      --interval-mode optimized --grouping-mode full >/dev/null
  )

  selected=$(find "$case_dir/results" -name selected_config.txt -type f -print -quit)
  [ -n "$selected" ]
  grep -qx "selected_config=$expected" "$selected"
}

run_case tanh 'tanh(x)' 0 1 Fixed2_14_Fixed2_14
run_case gelu '0.5*x*(1+tanh(0.7978845608028654*(x+0.044715*x^3)))' -3 3 Fixed4_12_Fixed3_13
run_case silu 'x/(1+exp(-x))' -5 5 Fixed5_11_Fixed3_13
run_case hswish 'x*min(max(x+3,0),6)/6' -3 3 Fixed4_12_Fixed3_13

echo "Matrix test passed: tanh, GELU, SiLU, and HSwish selected expected configurations."
