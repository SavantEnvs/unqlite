/* Jx9 script: compile + execute against an in-memory database.
 * Reaches the Jx9 VM and its builtins (json_decode, string/array/date
 * functions, db_* document store on :mem:).
 *
 * Input handling (compile-only coverage of every input lives in jx9-compile,
 * which passes the raw bytes unchanged):
 *
 * - NUL-terminated copy. The script is handed over as a NUL-terminated copy
 *   (length still passed), as callers that compile a C string do. Without the
 *   terminator, every script ending in an identifier trips the lexer's
 *   one-byte over-read at unqlite.c:25921 (jx9TokenizeInput reads zIn[0] at
 *   zEnd). One mutation of any seed reaches it, so this target crashed in the
 *   lexer instead of reaching the VM. jx9-compile still reports that bug.
 *
 * - No unbounded loops. Jx9 has no instruction or time limit (no VM hook to
 *   bound execution), and `while`/`for` run forever by design when their
 *   condition never turns false (`while($i < 10)` with the `$i++` mutated
 *   away). That is correct interpreter behaviour, not a bug, so inputs that
 *   contain `while` or `for` as a whole word (Jx9 keywords are
 *   case-sensitive, see jx9_lex.c keyword table) are rejected with -1.
 *   `foreach` stays: it walks a finite array. Recursion is bounded by the
 *   library's own UNQLITE_VM_CONFIG_RECURSION_DEPTH, kept low (16) so that a
 *   function calling itself several times cannot fan out exponentially. */
#include <stdint.h>
#include "fuzz_common.h"

static int is_ident(uint8_t c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_' || c >= 0x80;
}

/* 1 if the script contains `while` or `for` as a whole identifier. */
static int has_loop_keyword(const uint8_t *d, size_t n) {
  size_t i = 0;
  while (i < n) {
    size_t s;
    if (!is_ident(d[i])) { i++; continue; }
    s = i;
    while (i < n && is_ident(d[i])) i++;
    if ((i - s == 5 && memcmp(d + s, "while", 5) == 0) ||
        (i - s == 3 && memcmp(d + s, "for", 3) == 0))
      return 1;
  }
  return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  unqlite *db;
  unqlite_vm *vm;
  char *script;
  if (size > 65536) return -1; /* unqlite_compile takes an int length */
  if (has_loop_keyword(data, size)) return -1;
  script = (char *)malloc(size + 1);
  if (!script) return 0;
  memcpy(script, data, size);
  script[size] = '\0';
  if (unqlite_open(&db, ":mem:", UNQLITE_OPEN_IN_MEMORY) != UNQLITE_OK) {
    free(script);
    return 0;
  }
  if (unqlite_compile(db, script, (int)size, &vm) == UNQLITE_OK) {
    unqlite_vm_config(vm, UNQLITE_VM_CONFIG_OUTPUT, fuzz_discard, NULL);
    unqlite_vm_config(vm, UNQLITE_VM_CONFIG_RECURSION_DEPTH, 16);
    unqlite_vm_exec(vm);
    unqlite_vm_release(vm);
  }
  unqlite_close(db);
  free(script);
  return 0;
}
