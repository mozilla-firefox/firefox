#!/usr/bin/env bash
# Builds test/write-baseline.cpp against zmij.cc and zmij.h in several
# configurations, and compares the output of each with test/baseline.txt.
#
# Usage: mfbt/zmij/test/verify-baseline.sh [--update]
#
# With --update, regenerates test/baseline.txt if all configurations agree.
# Outputs and diffs go to <topsrcdir>/mfbt/zmij/test/output/. Set CXX to choose
# the compiler.

cd "$(dirname "$0")"/.. || exit
out_dir=test/output
baseline=test/baseline.txt

configs=(
  "default:"
  "optimize-size:-DZMIJ_OPTIMIZE_SIZE=1"
  "no-simd:-DZMIJ_USE_SIMD=0"
  "no-int128:-DZMIJ_USE_INT128=0"
  "no-builtins:-DZMIJ_NO_BUILTINS=1"
)
case $(uname -m) in
  x86_64 | i?86)
    configs+=("sse4:-msse4.1 -DZMIJ_USE_SSE4_1=1")
    echo "== neon: skipped (not ARM64)"
    ;;
  aarch64 | arm64)
    configs+=("neon:-DZMIJ_USE_NEON=1")
    echo "== sse4: skipped (not x86)"
    ;;
  *) echo "== sse4, neon: skipped (neither x86 nor ARM64)" ;;
esac

rm -rf "$out_dir"
mkdir -p "$out_dir"
status=0
for config in "${configs[@]}"; do
  name=${config%%:*}
  output=$out_dir/$name.txt
  echo "== $name"
  # shellcheck disable=SC2086 #intentionally splitting ${config#*:}
  "${CXX:-c++}" -std=c++20 -O2 ${config#*:} -I.. -Itest/stub-include \
    zmij.cc test/write-baseline.cpp -o "$out_dir/$name" ||
    { status=1; continue; }
  "$out_dir/$name" > "$output" || status=1
  if [ "${1:-}" = --update ]; then
    diff -u "$out_dir/default.txt" "$output" || status=1
  else
    diff -u "$baseline" "$output" > "$out_dir/$name.diff" ||
      { echo "differs from baseline, see $out_dir/$name.diff"; status=1; }
  fi
done

if [ "${1:-}" = --update ] && [ $status -eq 0 ]; then
  cp "$out_dir/default.txt" "$baseline"
fi
exit $status
