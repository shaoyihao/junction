#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <csignal>
#include <cstdlib>

#include "snapshot_sys.h"

const char* elf_filename = "/tmp/entrypoint_snapshot.elf";
const char* metadata_filename = "/tmp/entrypoint.metadata";

int main() {
  auto r = snapshot(elf_filename, metadata_filename);
  if (r == 0) {
    printf("snapshotted!\n");
  } else if (r == 1) {
    printf("restored!\n");
  } else {
    printf("snapshot/restore failed :-(\n");
    return -1;
  }

  return 0;
}