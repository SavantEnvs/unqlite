#!/usr/bin/env bash
#
# mayhem/test.sh — run the known-answer selftest built by mayhem/build.sh (upstream ships no test
# suite). It checks concrete values: KV store/append/fetch/delete/cursor results persisted across a
# reopen of an on-disk database, and the exact output of Jx9 scripts (arithmetic, sprintf,
# json_decode/encode, recursion, string builtins, document store). Exit 0 iff all checks pass.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"
cd "$SRC"

emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

RUNNER="$SRC/out/unqlite_selftest"
EXPECTED=17
if [ ! -x "$RUNNER" ]; then
  echo "test.sh: $RUNNER missing — mayhem/build.sh must build it" >&2
  emit_ctrf "unqlite-selftest" 0 "$EXPECTED"
  exit 1
fi
log=$("$RUNNER" 2>&1); rc=$?
echo "$log"
passed=$(grep -c '^PASS ' <<<"$log" || true)
failed=$(grep -c '^FAIL ' <<<"$log" || true)
# Checks that never reported (program died or was neutered) count as failures.
missing=$(( EXPECTED - passed - failed ))
[ "$missing" -gt 0 ] && failed=$(( failed + missing ))
[ "$rc" -ne 0 ] && [ "$failed" -eq 0 ] && failed=1
emit_ctrf "unqlite-selftest" "$passed" "$failed"
