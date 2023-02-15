// misc.cc - miscellaneous system calls

#include <cstring>

#include "junction/base/bits.h"
#include "junction/bindings/runtime.h"
#include "junction/kernel/usys.h"

namespace junction {

int usys_sched_getaffinity(pid_t pid, size_t cpusetsize, cpu_set_t *mask) {
  // Fake response that can be used by programs to detect the number of cores
  constexpr size_t kBitsPerSet = sizeof(cpu_set_t) * kBitsPerByte;
  size_t cores = rt::RuntimeMaxCores();
  if (cores / kBitsPerByte > cpusetsize) return -EPERM;
  std::memset(mask, 0, cpusetsize);
  for (size_t i = 0; i < cores; ++i) CPU_SET(i, mask);
  return 0;
}

}  // namespace junction