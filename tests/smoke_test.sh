#!/usr/bin/env sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
binary="$repo_dir/build/pwlforge"
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/pwlforge-smoke.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

(
  cd "$work_dir"
  "$binary" 'tanh(x)' 0 1 1e-4 --interval-mode optimized --grouping-mode full >/dev/null
)

summary=$(find "$work_dir/results" -name quantization_summary.csv -type f -print -quit)
[ -n "$summary" ]

awk -F, '
  $1 == "Fixed2_14_Fixed2_14" {
    found = 1
    if (($3 + 0) >= 1e-4) {
      exit 1
    }
  }
  END {
    if (!found) {
      exit 1
    }
  }
' "$summary"

echo "Smoke test passed: Fixed2_14_Fixed2_14 sampled average MAE is below 1e-4."
