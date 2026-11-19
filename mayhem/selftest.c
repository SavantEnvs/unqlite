/* Known-answer functional test for unqlite (mayhem integration oracle).
 * Prints one "PASS <name>" / "FAIL <name>" line per check; mayhem/test.sh
 * counts them and requires every expected check to pass. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "unqlite.h"

static int npass, nfail;
static void check(const char *name, int ok) {
  printf("%s %s\n", ok ? "PASS" : "FAIL", name);
  if (ok) npass++; else nfail++;
}

typedef struct { char buf[8192]; size_t n; } outbuf;
static int collect(const void *p, unsigned int n, void *ud) {
  outbuf *o = ud;
  if (o->n + n < sizeof o->buf) { memcpy(o->buf + o->n, p, n); o->n += n; o->buf[o->n] = 0; }
  return UNQLITE_OK;
}

static int run_jx9(unqlite *db, const char *script, outbuf *o) {
  unqlite_vm *vm;
  memset(o, 0, sizeof *o);
  if (unqlite_compile(db, script, -1, &vm) != UNQLITE_OK) return -1;
  unqlite_vm_config(vm, UNQLITE_VM_CONFIG_OUTPUT, collect, o);
  int rc = unqlite_vm_exec(vm);
  unqlite_vm_release(vm);
  return rc;
}

int main(void) {
  char path[512];
  unqlite *db;
  char buf[256];
  unqlite_int64 n;
  outbuf o;
  const char *t = getenv("TMPDIR");
  snprintf(path, sizeof path, "%s/unq_selftest_%ld.db", (t && *t) ? t : "/tmp", (long)getpid());
  unlink(path);

  /* 1. On-disk KV store: store, append, fetch, delete, persist across reopen. */
  check("kv_open_create", unqlite_open(&db, path, UNQLITE_OPEN_CREATE) == UNQLITE_OK);
  check("kv_store", unqlite_kv_store(db, "hello", -1, "world", 5) == UNQLITE_OK);
  check("kv_append", unqlite_kv_append(db, "hello", -1, "!!", 2) == UNQLITE_OK);
  for (int i = 0; i < 500; i++) {
    char k[32], v[64];
    snprintf(k, sizeof k, "key%04d", i);
    snprintf(v, sizeof v, "value-%d-%d", i, i * i);
    unqlite_kv_store(db, k, -1, v, (unqlite_int64)strlen(v));
  }
  unqlite_kv_delete(db, "key0007", -1);
  check("kv_commit", unqlite_commit(db) == UNQLITE_OK);
  unqlite_close(db);

  check("kv_reopen", unqlite_open(&db, path, UNQLITE_OPEN_READWRITE) == UNQLITE_OK);
  n = sizeof buf; memset(buf, 0, sizeof buf);
  check("kv_fetch_value", unqlite_kv_fetch(db, "hello", -1, buf, &n) == UNQLITE_OK &&
        n == 7 && memcmp(buf, "world!!", 7) == 0);
  n = sizeof buf; memset(buf, 0, sizeof buf);
  check("kv_fetch_many", unqlite_kv_fetch(db, "key0420", -1, buf, &n) == UNQLITE_OK &&
        strcmp(buf, "value-420-176400") == 0);
  n = sizeof buf;
  check("kv_deleted_absent", unqlite_kv_fetch(db, "key0007", -1, buf, &n) == UNQLITE_NOTFOUND);
  {
    unqlite_kv_cursor *cur;
    int count = 0;
    unqlite_kv_cursor_init(db, &cur);
    for (unqlite_kv_cursor_first_entry(cur); unqlite_kv_cursor_valid_entry(cur);
         unqlite_kv_cursor_next_entry(cur)) count++;
    unqlite_kv_cursor_release(db, cur);
    check("kv_cursor_count", count == 500); /* 499 keys + "hello" */
  }
  unqlite_close(db);
  unlink(path);

  /* 2. Jx9 scripting engine on an in-memory DB: known-answer outputs. */
  check("mem_open", unqlite_open(&db, ":mem:", UNQLITE_OPEN_IN_MEMORY) == UNQLITE_OK);
  run_jx9(db, "$a = 6 * 7; print $a;", &o);
  check("jx9_arith", strcmp(o.buf, "42") == 0);
  run_jx9(db, "print sprintf('%05d|%s|%x', 3, 'ab', 255);", &o);
  check("jx9_sprintf", strcmp(o.buf, "00003|ab|ff") == 0);
  run_jx9(db, "$x = json_decode('{\"a\":[1,2,3],\"b\":\"z\"}'); print count($x.a) .. $x.b;", &o);
  check("jx9_json_decode", strcmp(o.buf, "3z") == 0);
  run_jx9(db, "print json_encode([1,'two',{k:true}]);", &o);
  check("jx9_json_encode", strstr(o.buf, "two") != NULL && strstr(o.buf, "true") != NULL);
  run_jx9(db, "function f($n){ if($n < 2) return $n; return f($n-1) + f($n-2); } print f(15);", &o);
  check("jx9_recursion", strcmp(o.buf, "610") == 0);
  run_jx9(db, "print strtoupper(str_replace('l', 'L', 'hello'));", &o);
  check("jx9_strings", strcmp(o.buf, "HELLO") == 0);
  run_jx9(db,
    "db_create('users');"
    "db_store('users', {name:'ann', age:30});"
    "db_store('users', [{name:'bob', age:41}, {name:'cy', age:7}]);"
    "print db_total_records('users'), ':';"
    "$r = db_fetch_by_id('users', 1); print $r.name, ':';"
    "$all = db_fetch_all('users', function($rec){ return $rec.age > 10; });"
    "print count($all);", &o);
  check("jx9_docstore", strcmp(o.buf, "3:bob:2") == 0);
  unqlite_close(db);

  printf("selftest: %d passed, %d failed\n", npass, nfail);
  return nfail ? 1 : 0;
}
