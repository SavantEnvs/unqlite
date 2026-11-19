/* Shared helpers for the unqlite fuzz harnesses (mayhem integration). */
#ifndef UNQ_FUZZ_COMMON_H
#define UNQ_FUZZ_COMMON_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "unqlite.h"

/* Output consumer that discards everything the VM prints. */
static int fuzz_discard(const void *p, unsigned int n, void *ud) {
  (void)p; (void)n; (void)ud;
  return UNQLITE_OK;
}

/* Per-process scratch DB path under $TMPDIR (default /tmp). */
static void fuzz_db_path(char *out, size_t n, const char *tag) {
  const char *t = getenv("TMPDIR");
  if (!t || !*t) t = "/tmp";
  snprintf(out, n, "%s/unqfuzz_%s_%ld.db", t, tag, (long)getpid());
}

/* Remove the database and its journal (UNQLITE_JOURNAL_FILE_SUFFIX). */
static void fuzz_db_unlink(const char *db) {
  char j[4200];
  snprintf(j, sizeof j, "%s_unqlite_journal", db);
  unlink(db);
  unlink(j);
}

static int fuzz_write_file(const char *path, const uint8_t *d, size_t n) {
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) return -1;
  size_t off = 0;
  while (off < n) {
    ssize_t w = write(fd, d + off, n - off);
    if (w <= 0) { close(fd); return -1; }
    off += (size_t)w;
  }
  close(fd);
  return 0;
}
#endif
