#!/bin/bash

# Test harness for testing the RLB processes from the outside.
#
# Some behavior can only be observed when properly exiting the process running Glean,
# e.g. when an uploader runs in another thread.
# On exit the threads will be killed, regardless of their state.

# Remove the temporary data path on all exit conditions
cleanup() {
  if [ -n "$datapath" ]; then
    rm -r "$datapath"
  fi
}
trap cleanup INT ABRT TERM EXIT

tmp="${TMPDIR:-/tmp}"
datapath=$(mktemp -d "${tmp}/crashing_threads.XXXX")

RUSTFLAGS="-C panic=abort" \
RUST_LOG=debug \
cargo run -p glean --example crashing-threads -- "$datapath"
ret=$?
count=$(ls -1q "$datapath/pending_pings" | wc -l)

# We expect 1 `prototype` ping:
if [[ $ret -eq 0 ]] && [[ "$count" -eq 1 ]]; then
  echo "test result: ok."
  exit 0
else
  echo "Assertions:"
  echo "  ret - expected: 0, was: $ret"
  echo "  count - expected: 1, was: $count"
  echo "test result: FAILED."
  exit 101
fi
