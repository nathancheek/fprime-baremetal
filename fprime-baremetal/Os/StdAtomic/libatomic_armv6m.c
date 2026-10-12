// Atomic library calls for single-core ARMv6-M (Cortex-M0/M0+) toolchains that don't provide them.
//
// ARMv6-M has no exclusive load/store instructions, so gcc turns std::atomic read-modify-write operations (used by
// F´'s Os::Stub::Mutex and Fw::DataStructures) into __atomic_* calls that libgcc doesn't implement. These do each
// operation with interrupts masked, which makes them atomic on a single core.
//
// This file isn't built by any module. F´ framework libraries call these functions, and a static library only
// supplies them to calls the linker has already seen. As a module the deployment depends on, it would link only if a
// library ahead of it on the link line happens to call one. Instead, a platform compiles it into a library that's
// linked at the end of every target (fprime-arduino's patch library does, with FPRIME_ARDUINO_SYNTHETIC_ATOMICS).
// Don't use it with a toolchain or core that already provides these calls.
#if !defined(__ARM_ARCH_6M__)
#error "These atomic library calls are for single-core ARMv6-M targets"
#endif

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static inline uint32_t atomics_lock(void) {
    uint32_t primask;
    __asm__ volatile("mrs %0, primask\n cpsid i" : "=r"(primask)::"memory");
    return primask;
}

static inline void atomics_unlock(uint32_t primask) {
    __asm__ volatile("msr primask, %0" ::"r"(primask) : "memory");
}

#define ATOMIC_EXCHANGE(N, T)                                         \
    T __atomic_exchange_##N(volatile void* ptr, T value, int order) { \
        (void)order;                                                  \
        const uint32_t primask = atomics_lock();                      \
        const T old = *(volatile T*)ptr;                              \
        *(volatile T*)ptr = value;                                    \
        atomics_unlock(primask);                                      \
        return old;                                                   \
    }

#define ATOMIC_COMPARE_EXCHANGE(N, T)                                                                         \
    bool __atomic_compare_exchange_##N(volatile void* ptr, void* expected, T desired, bool weak, int success, \
                                       int failure) {                                                         \
        (void)weak;                                                                                           \
        (void)success;                                                                                        \
        (void)failure;                                                                                        \
        const uint32_t primask = atomics_lock();                                                              \
        const T current = *(volatile T*)ptr;                                                                  \
        T expectedValue;                                                                                      \
        memcpy(&expectedValue, expected, sizeof(T));                                                          \
        const bool equal = (current == expectedValue);                                                        \
        if (equal) {                                                                                          \
            *(volatile T*)ptr = desired;                                                                      \
        }                                                                                                     \
        atomics_unlock(primask);                                                                              \
        if (!equal) {                                                                                         \
            memcpy(expected, &current, sizeof(T));                                                            \
        }                                                                                                     \
        return equal;                                                                                         \
    }

#define ATOMIC_FETCH_OP(N, T, NAME, OP)                                     \
    T __atomic_fetch_##NAME##_##N(volatile void* ptr, T value, int order) { \
        (void)order;                                                        \
        const uint32_t primask = atomics_lock();                            \
        const T old = *(volatile T*)ptr;                                    \
        *(volatile T*)ptr = (T)(old OP value);                              \
        atomics_unlock(primask);                                            \
        return old;                                                         \
    }                                                                       \
    T __atomic_##NAME##_fetch_##N(volatile void* ptr, T value, int order) { \
        (void)order;                                                        \
        const uint32_t primask = atomics_lock();                            \
        const T result = (T)(*(volatile T*)ptr OP value);                   \
        *(volatile T*)ptr = result;                                         \
        atomics_unlock(primask);                                            \
        return result;                                                      \
    }

#define ATOMIC_ALL(N, T)          \
    ATOMIC_EXCHANGE(N, T)         \
    ATOMIC_COMPARE_EXCHANGE(N, T) \
    ATOMIC_FETCH_OP(N, T, add, +) \
    ATOMIC_FETCH_OP(N, T, sub, -) \
    ATOMIC_FETCH_OP(N, T, and, &) \
    ATOMIC_FETCH_OP(N, T, or, |)  \
    ATOMIC_FETCH_OP(N, T, xor, ^)

ATOMIC_ALL(1, uint8_t)
ATOMIC_ALL(2, uint16_t)
ATOMIC_ALL(4, uint32_t)
