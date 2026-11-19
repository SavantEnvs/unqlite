/* A crafted database file: open it, read, walk with cursors, then write and
 * commit. Reaches the pager (header, page cache, commit) and the linear-hash
 * KV engine (page/cell/overflow parsing, bucket map, split). */
#include <stdint.h>
#include "fuzz_common.h"

static int sink(const void *p, unsigned int n, void *ud) { (void)p; (void)n; (void)ud; return UNQLITE_OK; }

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  char path[4096];
  unqlite *db;
  unqlite_kv_cursor *cur;
  static const char *keys[] = {"a", "key", "test", "k0", "k1", "hello"};
  int i;
  fuzz_db_path(path, sizeof path, "dbfile");
  fuzz_db_unlink(path);
  if (fuzz_write_file(path, data, size) != 0) return 0;
  if (unqlite_open(&db, path, UNQLITE_OPEN_READWRITE) == UNQLITE_OK) {
    unqlite_config(db, UNQLITE_CONFIG_MAX_PAGE_CACHE, 64);
    for (i = 0; i < 6; i++) {
      unqlite_int64 n = 0;
      unqlite_kv_fetch(db, keys[i], -1, NULL, &n);
      unqlite_kv_fetch_callback(db, keys[i], -1, sink, NULL);
    }
    if (unqlite_kv_cursor_init(db, &cur) == UNQLITE_OK) {
      int steps = 0;
      for (unqlite_kv_cursor_first_entry(cur);
           unqlite_kv_cursor_valid_entry(cur) && steps < 1000;
           unqlite_kv_cursor_next_entry(cur), steps++) {
        unqlite_kv_cursor_key_callback(cur, sink, NULL);
        unqlite_kv_cursor_data_callback(cur, sink, NULL);
      }
      steps = 0;
      for (unqlite_kv_cursor_last_entry(cur);
           unqlite_kv_cursor_valid_entry(cur) && steps < 1000;
           unqlite_kv_cursor_prev_entry(cur), steps++) {
        unqlite_kv_cursor_key_callback(cur, sink, NULL);
      }
      unqlite_kv_cursor_release(db, cur);
    }
    unqlite_kv_store(db, "fuzz", 4, "value", 5);
    unqlite_kv_append(db, "key", 3, "more", 4);
    unqlite_kv_delete(db, "a", 1);
    unqlite_commit(db);
    unqlite_close(db);
  }
  fuzz_db_unlink(path);
  return 0;
}
