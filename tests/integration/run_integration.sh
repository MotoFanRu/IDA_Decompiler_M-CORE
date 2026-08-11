#!/usr/bin/env bash
# Headless integration tests: for each fixture, load the flat MCORE blob, run the
# decompiler plugin, and diff the pseudocode against expected.c.
#
# Requires a built plugin (build/mcore_decompiler.so) and IDA Pro 9.4 with its
# built-in MCORE processor module (procs/mcore.so).
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
: "${IDA_DIR:?set IDA_DIR to your IDA Pro 9.4 installation}"
IDAT="${IDA_DIR}/idat"
PROC="${IDA_DIR}/procs/mcore.so"
PLUGIN="${ROOT}/build/mcore_decompiler.so"
SCRIPT="${ROOT}/tests/integration/decompile_dump.py"

[ -x "${IDAT}" ] || { echo "idat not found at ${IDAT} (set IDA_DIR)"; exit 2; }
[ -f "${PROC}" ] || { echo "built-in MCORE processor not found at ${PROC}"; exit 2; }
[ -f "${PLUGIN}" ] || { echo "plugin not built: ${PLUGIN} (run ./run_tests.sh)"; exit 2; }

# Install into the selected IDA user profile. For an isolated run, point IDAUSR
# at a temporary profile containing a valid ida.reg before invoking this script.
PLUGDIR="${IDAUSR:-${HOME}/.idapro}/plugins"
mkdir -p "${PLUGDIR}"
cp "${PLUGIN}" "${PLUGDIR}/"

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

norm() { sed -E 's/sub_[0-9A-Fa-f]+/sub/g'; }

fail=0
for fx in "${ROOT}"/tests/integration/fixtures/*/; do
  name="$(basename "${fx}")"
  bin="${fx}prog.bin"; exp="${fx}expected.c"
  [ -f "${bin}" ] && [ -f "${exp}" ] || continue

  cp "${bin}" "${WORK}/${name}.bin"; rm -f "${WORK}/${name}.bin".* 2>/dev/null
  TVHEADLESS=1 "${IDAT}" -A -pMCORE -L"${WORK}/${name}.log" \
    -S"${SCRIPT}" "${WORK}/${name}.bin" >/dev/null 2>&1

  got="$(awk '/^>>>MCORE_FUNC/{f=1;next} /^<<<MCORE_END/{f=0} f' "${WORK}/${name}.log" | norm)"
  want="$(norm < "${exp}")"

  if [ "${got}" == "${want}" ]; then
    echo "PASS ${name}"
  else
    echo "FAIL ${name}"
    diff <(echo "${want}") <(echo "${got}") | sed 's/^/    /'
    fail=1
  fi
done

exit "${fail}"
