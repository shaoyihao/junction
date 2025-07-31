#pragma once

extern "C" {
#include "base/lock.h"
#include "base/log.h"
}

#ifndef BYTENUM
#define BYTENUM(n)  (((n) + 7) / 8)
#endif

#ifndef CEIL
#define CEIL(x, b)  (((x) + (b) - 1) / (b))
#endif

#ifndef MIN
#define MIN(m,n)    ((m) < (n) ? (m) : (n))
#endif

#ifndef ALIGN
#define ALIGN(x, k) (((x) + (k) - 1) / (k) * (k))
#endif