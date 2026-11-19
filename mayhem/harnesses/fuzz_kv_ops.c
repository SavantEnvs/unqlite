/* A byte-coded sequence of KV API operations on a fresh on-disk database:
 * store/append/delete/fetch, cursor seeks, begin/commit/rollback; then the
 * database is reopened and walked. Each op: 1 opcode byte, 1 key-length
 * byte, 2 value-length bytes (big endian), then key and value bytes. */
#include <stdint.h>
#include "fuzz_common.h"

static int sink(const void *p, unsigned int n, void *ud) { (void)p; (void)n; (void)ud; return UNQLITE_OK; }

static void walk(unqlite *db) {
  unqlite_kv_cursor *cur;
  int steps = 0;
  if (unqlite_kv_cursor_init(db, &cur) != UNQLITE_OK) return;
  for (unqlite_kv_cursor_first_entry(cur);
       unqlite_kv_cursor_valid_entry(cur) && steps < 2000;
       unqlite_kv_cursor_next_entry(cur), steps++) {
    unqlite_kv_cursor_key_callback(cur, sink, NULL);
    unqlite_kv_cursor_data_callback(cur, sink, NULL);
  }
  unqlite_kv_cursor_release(db, cur);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  char path[4096];
  unqlite *db;
  size_t off = 0;
  int ops = 0;
  fuzz_db_path(path, sizeof path, "kvops");
  fuzz_db_unlink(path);
  if (unqlite_open(&db, path, UNQLITE_OPEN_CREATE) != UNQLITE_OK) return 0;
  unqlite_config(db, UNQLITE_CONFIG_MAX_PAGE_CACHE, 16);
  while (off + 4 <= size && ops < 512) {
    uint8_t op = data[off];
    size_t klen = data[off + 1];
    size_t vlen = ((size_t)data[off + 2] << 8) | data[off + 3];
    const uint8_t *key, *val;
    off += 4;
    if (klen > size - off) klen = size - off;
    key = data + off; off += klen;
    if (vlen > size - off) vlen = size - off;
    val = data + off; off += vlen;
    ops++;
    switch (op % 11) {
      case 0: unqlite_kv_store(db, key, (int)klen, val, (unqlite_int64)vlen); break;
      case 1: unqlite_kv_append(db, key, (int)klen, val, (unqlite_int64)vlen); break;
      case 2: unqlite_kv_delete(db, key, (int)klen); break;
      case 3: unqlite_kv_fetch_callback(db, key, (int)klen, sink, NULL); break;
      case 4: { unqlite_int64 n = 0; unqlite_kv_fetch(db, key, (int)klen, NULL, &n); break; }
      case 5: unqlite_begin(db); break;
      case 6: unqlite_commit(db); break;
      case 7: unqlite_rollback(db); break;
      case 8: {
        unqlite_kv_cursor *cur;
        if (unqlite_kv_cursor_init(db, &cur) == UNQLITE_OK) {
          int pos = (vlen % 3) + 1; /* UNQLITE_CURSOR_MATCH_EXACT/LE/GE */
          if (unqlite_kv_cursor_seek(cur, key, (int)klen, pos) == UNQLITE_OK) {
            unqlite_kv_cursor_data_callback(cur, sink, NULL);
            if (op & 0x80) unqlite_kv_cursor_delete_entry(cur);
          }
          unqlite_kv_cursor_release(db, cur);
        }
        break;
      }
      case 9: walk(db); break;
      default: {
        /* Store a large, input-derived value to force overflow pages. */
        size_t big = (vlen + 1) * 8;
        uint8_t *b = malloc(big);
        size_t i;
        if (b) {
          for (i = 0; i < big; i++) b[i] = vlen ? val[i % vlen] : (uint8_t)i;
          unqlite_kv_store(db, key, (int)klen, b, (unqlite_int64)big);
          free(b);
        }
        break;
      }
    }
  }
  unqlite_close(db);
  if (unqlite_open(&db, path, UNQLITE_OPEN_READWRITE) == UNQLITE_OK) {
    walk(db);
    unqlite_close(db);
  }
  fuzz_db_unlink(path);
  return 0;
}
