#!/usr/bin/env bash
# Build the plugin + unit tests and run the offline unit suite.
#
# Integration fixtures (headless idat) are added from M1 onward and will be
# invoked from here too.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${ROOT}/build"

cmake -S "${ROOT}" -B "${BUILD}" -DCMAKE_BUILD_TYPE=Debug "$@"
cmake --build "${BUILD}" -j"$(nproc)"

echo "=== unit tests ==="
ctest --test-dir "${BUILD}" --output-on-failure
