#!/usr/bin/env bash
#
# mayhem/build.sh — build the unqlite fuzz harnesses, their standalone reproducers and the
# known-answer selftest. unqlite is a single-file amalgamation (unqlite.c + unqlite.h at the repo
# root; CMakeLists.txt only archives unqlite.c), so it is compiled directly here, once per flavour.
# Everything lands in out/ (gitignored upstream), so `git clean -ffdX` resets the tree.
set -euo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
OUT="$SRC/out"
OBJ="$OUT/obj"
mkdir -p "$OBJ"

HARNESSES="fuzz_jx9_exec fuzz_jx9_compile fuzz_db_file fuzz_journal fuzz_kv_ops fuzz_docstore fuzz_http_request"
# JX9_DISABLE_DISK_IO swaps the Jx9 VFS for a null one, so fuzzed scripts cannot fopen/unlink/
# chdir/sleep on the host. The database pager uses unqlite's own VFS and is unaffected.
DEFS="-DJX9_DISABLE_DISK_IO"
# UBSan's alignment check is relaxed (only that check): unqlite's memory backend (SyMemBackendAlloc)
# prefixes every allocation with a 4-byte-aligned SyMemBlock header, so every allocation trips
# -fsanitize=alignment before any input is parsed. ASan and the rest of UBSan stay on and halting.
RELAX=""
case "$SANITIZER_FLAGS" in *undefined*|*alignment*) RELAX="-fno-sanitize=alignment" ;; esac
FUZZ_CFLAGS="$SANITIZER_FLAGS $RELAX $DEBUG_FLAGS -O1 $DEFS -I$SRC"

# 1) The library, instrumented: once with fuzzer coverage (for the libFuzzer binaries), once
#    without (for the standalone reproducers, which have no libFuzzer runtime).
$CC $FUZZ_CFLAGS -fsanitize=fuzzer-no-link -w -c unqlite.c -o "$OBJ/unqlite_fuzz.o" &
$CC $FUZZ_CFLAGS -w -c unqlite.c -o "$OBJ/unqlite_plain.o" &
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.cc -o "$OBJ/lsan_off.o" &
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$OBJ/standalone_main.o" &
wait

# 2) Each harness twice: libFuzzer binary + standalone run-once reproducer.
for h in $HARNESSES; do
  $CC $FUZZ_CFLAGS $LIB_FUZZING_ENGINE "mayhem/harnesses/$h.c" "$OBJ/unqlite_fuzz.o" \
      "$OBJ/lsan_off.o" -lm -lpthread -o "$OUT/$h" &
  $CC $FUZZ_CFLAGS "mayhem/harnesses/$h.c" "$OBJ/standalone_main.o" "$OBJ/unqlite_plain.o" \
      "$OBJ/lsan_off.o" -lm -lpthread -o "$OUT/$h-standalone" &
done
wait
for h in $HARNESSES; do [ -x "$OUT/$h" ] && [ -x "$OUT/$h-standalone" ]; done

# 3) The functional oracle with normal flags (no sanitizers), run by mayhem/test.sh.
$CC -O2 $COVERAGE_FLAGS -w -I"$SRC" mayhem/selftest.c unqlite.c -lm -lpthread -o "$OUT/unqlite_selftest"
echo "build.sh: built $(echo $HARNESSES | wc -w) harnesses + selftest in $OUT"
