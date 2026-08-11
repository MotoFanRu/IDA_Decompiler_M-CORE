#!/usr/bin/env bash
# Build the plugin with the official IDA 9.4 SDK, run the offline unit suite,
# then run the built-in-MCORE integration fixtures when IDA_DIR is available.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${ROOT}/build"

: "${IDASDK:?set IDASDK to the HexRaysSA/ida-sdk v9.4.0-release checkout}"

cmake -S "${ROOT}" -B "${BUILD}" -DCMAKE_BUILD_TYPE=Debug "$@"
cmake --build "${BUILD}" -j"$(nproc)"

echo "=== unit tests ==="
ctest --test-dir "${BUILD}" --output-on-failure

echo "=== integration tests (headless idat) ==="
if [ -n "${IDA_DIR:-}" ] && [ -x "${IDA_DIR}/idat" ]; then
  "${ROOT}/tests/integration/run_integration.sh"
else
  echo "skipped: idat not found (set IDA_DIR to enable)"
fi
