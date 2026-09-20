/* _GNU_SOURCE must be set before any system header; glibc hides
   CLOCK_MONOTONIC_RAW behind it, macOS exposes it unconditionally. */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "timer.h"

#include <time.h>

#if defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#elif !defined(__aarch64__)
#error "no supported cycle counter for this architecture"
#endif

#define NANOSECONDS_PER_SECOND 1000000000ULL

uint64_t timer_now_ns(void)
{
    struct timespec moment = {0};

    /* RAW is never slewed by NTP, so a measured interval cannot be stretched by a
       clock adjustment landing mid-run. */
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &moment) != 0) {
        return 0;
    }

    return (uint64_t)moment.tv_sec * NANOSECONDS_PER_SECOND + (uint64_t)moment.tv_nsec;
}

uint64_t timer_read_cycles(void)
{
#if defined(__aarch64__)
    uint64_t ticks;

    /* isb keeps the core from sampling the counter speculatively, out of order with
       the measured code; the memory clobber stops the compiler doing the same.

       The two instructions must be separated by a newline and never by ";": the
       AArch64 assembler treats ";" as a comment introducer, so "isb; mrs ..."
       assembles to isb alone and the read vanishes without any diagnostic. */
    __asm__ __volatile__("isb\n\tmrs %0, cntvct_el0" : "=r"(ticks) : : "memory");

    return ticks;
#else
    /* Same reason as the isb above: rdtsc is not serializing on its own. */
    _mm_lfence();

    return (uint64_t)__rdtsc();
#endif
}

uint64_t timer_cycle_frequency_hz(void)
{
#if defined(__aarch64__)
    uint64_t frequency;

    __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(frequency));

    return frequency;
#else
    /* The TSC rate is not architecturally readable; 0 means unknown. */
    return 0;
#endif
}

const char *timer_cycle_source_name(void)
{
#if defined(__aarch64__)
    return "cntvct_el0";
#else
    return "rdtsc";
#endif
}
