/* Raw HTTP request: UNQLITE_VM_CONFIG_HTTP_REQUEST -> VmHttpProcessRequest.
 * This is the entry point for embedding Jx9 behind a web server: it parses the
 * request line, headers, query string, cookies and POST body into the $_SERVER,
 * $_GET, $_POST, $_COOKIE and $_HEADER superglobals. A fixed script then reads
 * every superglobal back (json_encode walks nested arrays, foreach touches keys). */
#include <stdint.h>
#include "fuzz_common.h"

static const char kScript[] =
  "print json_encode($_SERVER), json_encode($_GET), json_encode($_POST),"
  "      json_encode($_COOKIE), json_encode($_HEADER), json_encode($_REQUEST);"
  "foreach($_HEADER as $k, $v){ print $k, ':', $v; }"
  "foreach($_GET as $k, $v){ print $k, '=', $v; }";

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  unqlite *db;
  unqlite_vm *vm;
  /* The config call takes an int length and requires a non-empty request: it
   * reads zRequest[0] (SX_EMPTY_STR) before looking at the length, so a 0-byte
   * buffer is outside its contract (unqlite.c, case JX9_VM_CONFIG_HTTP_REQUEST). */
  if (size == 0 || size > 65536) return -1;
  if (unqlite_open(&db, ":mem:", UNQLITE_OPEN_IN_MEMORY) != UNQLITE_OK) return 0;
  if (unqlite_compile(db, kScript, (int)(sizeof kScript - 1), &vm) == UNQLITE_OK) {
    unqlite_vm_config(vm, UNQLITE_VM_CONFIG_OUTPUT, fuzz_discard, NULL);
    unqlite_vm_config(vm, UNQLITE_VM_CONFIG_HTTP_REQUEST, (const char *)data, (int)size);
    unqlite_vm_exec(vm);
    unqlite_vm_release(vm);
  }
  unqlite_close(db);
  return 0;
}
