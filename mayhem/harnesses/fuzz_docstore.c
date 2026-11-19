/* A crafted database file read through the Jx9 document store: a fixed
 * script loads collection 'c' (collection header, schema, FastJSON-encoded
 * records) and fetches, counts and appends to it. */
#include <stdint.h>
#include "fuzz_common.h"

static const char kScript[] =
  "if( db_exists('c') ){\n"
  "  $n = db_total_records('c');\n"
  "  $s = db_get_schema('c');\n"
  "  $all = db_fetch_all('c');\n"
  "  print json_encode($all);\n"
  "  $r = db_fetch_by_id('c', 0); print $r;\n"
  "  $r = db_fetch_by_id('c', 1); print $r;\n"
  "  db_reset_record_cursor('c');\n"
  "  while( ($rec = db_fetch('c')) != NULL ){ print $rec; }\n"
  "  db_store('c', {fuzz: 1, list: [1, 2, 'x']});\n"
  "  db_drop_record('c', 0);\n"
  "  print db_last_record_id('c');\n"
  "}\n";

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  char path[4096];
  unqlite *db;
  unqlite_vm *vm;
  fuzz_db_path(path, sizeof path, "docstore");
  fuzz_db_unlink(path);
  if (fuzz_write_file(path, data, size) != 0) return 0;
  if (unqlite_open(&db, path, UNQLITE_OPEN_READWRITE) == UNQLITE_OK) {
    if (unqlite_compile(db, kScript, (int)sizeof(kScript) - 1, &vm) == UNQLITE_OK) {
      unqlite_vm_config(vm, UNQLITE_VM_CONFIG_OUTPUT, fuzz_discard, NULL);
      unqlite_vm_exec(vm);
      unqlite_vm_release(vm);
    }
    unqlite_close(db);
  }
  fuzz_db_unlink(path);
  return 0;
}
