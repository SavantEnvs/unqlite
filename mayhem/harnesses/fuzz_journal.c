/* A crafted hot journal next to a crafted database: opening and reading the
 * database makes the pager roll the journal back (journal header parse,
 * record playback, checksums). Input layout: 2-byte big-endian length L,
 * then L bytes of database file, then the journal file. */
#include <stdint.h>
#include "fuzz_common.h"

static int sink(const void *p, unsigned int n, void *ud) { (void)p; (void)n; (void)ud; return UNQLITE_OK; }

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  char path[4096], jpath[4200];
  unqlite *db;
  unqlite_kv_cursor *cur;
  size_t dblen;
  if (size < 2) return -1;
  dblen = ((size_t)data[0] << 8) | data[1];
  if (dblen > size - 2) dblen = size - 2;
  fuzz_db_path(path, sizeof path, "journal");
  snprintf(jpath, sizeof jpath, "%s_unqlite_journal", path);
  fuzz_db_unlink(path);
  if (fuzz_write_file(path, data + 2, dblen) != 0 ||
      fuzz_write_file(jpath, data + 2 + dblen, size - 2 - dblen) != 0) {
    fuzz_db_unlink(path);
    return 0;
  }
  if (unqlite_open(&db, path, UNQLITE_OPEN_READWRITE) == UNQLITE_OK) {
    unqlite_int64 n = 0;
    unqlite_kv_fetch(db, "key", 3, NULL, &n);
    if (unqlite_kv_cursor_init(db, &cur) == UNQLITE_OK) {
      int steps = 0;
      for (unqlite_kv_cursor_first_entry(cur);
           unqlite_kv_cursor_valid_entry(cur) && steps < 100;
           unqlite_kv_cursor_next_entry(cur), steps++) {
        unqlite_kv_cursor_key_callback(cur, sink, NULL);
        unqlite_kv_cursor_data_callback(cur, sink, NULL);
      }
      unqlite_kv_cursor_release(db, cur);
    }
    unqlite_close(db);
  }
  fuzz_db_unlink(path);
  return 0;
}
