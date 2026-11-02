#!/bin/sh
# Host unit tests for the layout parser/validator and the upload state machine (no board needed).
# Builds with AddressSanitizer + UBSan so any out-of-bounds read on malformed input fails the run.
set -e
cd "$(dirname "$0")/.."
OUT="${TMPDIR:-/tmp}/layout_core_test"
${CXX:-c++} -std=c++14 -g -O1 -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined \
  sketch/test/host/layout_core_test.cpp sketch/src/layout_core.cpp -o "$OUT"
"$OUT"
