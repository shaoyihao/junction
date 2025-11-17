#pragma once

extern "C" {
#include "base/lock.h"
#include "base/log.h"
#include "base/bitmap.h"
#include "asm/ops.h"
#include "runtime/smalloc.h"
#include "runtime/thread.h"
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

struct SpinGuard 
{
    spinlock_t* m;
    explicit SpinGuard(spinlock_t* mm) : m(mm) { spin_lock(m); }
    ~SpinGuard() { spin_unlock(m); }
    SpinGuard(const SpinGuard&) = delete;
    SpinGuard& operator=(const SpinGuard&) = delete;
};


// template <typename T>
// struct MyAllocator {
//     using value_type = T;
//     MyAllocator() = default;
//     template <typename U> constexpr MyAllocator(const MyAllocator<U>&) noexcept {}

//     [[nodiscard]] T* allocate(std::size_t n) 
//     {   
//         const std::size_t total_size = n * sizeof(T);
//         void* p = smalloc(total_size);
//         return static_cast<T*>(p);
//     }
//     void deallocate(T* p, std::size_t n) noexcept 
//     {
//         sfree(p);
//     }

//     template <class U> bool operator==(const MyAllocator<U>&) const { return true;  }
//     template <class U> bool operator!=(const MyAllocator<U>&) const { return false; }
// };