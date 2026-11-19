/* Seed generator for the database-file targets (run by hand; output committed under
 * mayhem/<target>/testsuite). Usage: make_seeds <outdir> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "unqlite.h"

static char outdir[1024];

static void fill(unqlite *db, int n, int big) {
  for (int i = 0; i < n; i++) {
    char k[32], v[256];
    snprintf(k, sizeof k, "key%d", i);
    snprintf(v, sizeof v, "value-%d", i * 31);
    unqlite_kv_store(db, k, -1, v, (unqlite_int64)strlen(v));
  }
  unqlite_kv_store(db, "key", 3, "hello", 5);
  unqlite_kv_store(db, "a", 1, "b", 1);
  if (big) {
    char *b = malloc(20000);
    memset(b, 'Z', 20000);
    unqlite_kv_store(db, "big", 3, b, 20000);
    free(b);
  }
}

static unsigned char *slurp(const char *p, size_t *n) {
  FILE *f = fopen(p, "rb");
  if (!f) { *n = 0; return NULL; }
  fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET);
  unsigned char *b = malloc(l ? l : 1);
  *n = fread(b, 1, l, f);
  fclose(f);
  return b;
}

static void put(const char *name, const unsigned char *b, size_t n) {
  char p[2048];
  snprintf(p, sizeof p, "%s/%s", outdir, name);
  FILE *f = fopen(p, "wb"); fwrite(b, 1, n, f); fclose(f);
}

static void copy_db(const char *db, const char *name) {
  size_t n; unsigned char *b = slurp(db, &n);
  put(name, b, n); free(b);
}

static void script(unqlite *db, const char *s) {
  unqlite_vm *vm;
  if (unqlite_compile(db, s, -1, &vm) == UNQLITE_OK) { unqlite_vm_exec(vm); unqlite_vm_release(vm); }
}

int main(int argc, char **argv) {
  unqlite *db;
  const char *p = "/tmp/seed.db", *j = "/tmp/seed.db_unqlite_journal";
  snprintf(outdir, sizeof outdir, "%s", argv[1]);

  unlink(p); unqlite_open(&db, p, UNQLITE_OPEN_CREATE); fill(db, 3, 0); unqlite_close(db);
  copy_db(p, "db_small");
  unlink(p); unqlite_open(&db, p, UNQLITE_OPEN_CREATE); fill(db, 300, 1); unqlite_close(db);
  copy_db(p, "db_large");

  unlink(p); unqlite_open(&db, p, UNQLITE_OPEN_CREATE);
  script(db, "db_create('c'); db_store('c', {a:1, b:[1,2], s:'str'});"
             "db_store('c', [{a:2, n:null}, {a:3.5, t:true, o:{x:'y'}}]);");
  unqlite_close(db);
  copy_db(p, "docstore_c");

  /* Hot journal: a child writes past its page cache and dies before commit. */
  unlink(p); unlink(j);
  unqlite_open(&db, p, UNQLITE_OPEN_CREATE); fill(db, 50, 0); unqlite_close(db);
  if (fork() == 0) {
    unqlite_open(&db, p, UNQLITE_OPEN_READWRITE);
    unqlite_config(db, UNQLITE_CONFIG_MAX_PAGE_CACHE, 2);
    unqlite_begin(db);
    fill(db, 400, 1);
    _exit(0);
  }
  wait(NULL);
  {
    size_t dn, jn; unsigned char *d = slurp(p, &dn), *jb = slurp(j, &jn);
    if (dn > 65535) dn = 65535;
    unsigned char *o = malloc(2 + dn + jn);
    o[0] = dn >> 8; o[1] = dn & 0xff;
    memcpy(o + 2, d, dn); memcpy(o + 2 + dn, jb, jn);
    put("journal_hot", o, 2 + dn + jn);
    fprintf(stderr, "db %zu journal %zu\n", dn, jn);
  }
  return 0;
}
