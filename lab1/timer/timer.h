#ifndef LAB1_TIMER_H
#define LAB1_TIMER_H

#include <stdint.h>

/*
 * The lab forbids clock() and time(). timer_now_ns is the primary measurement; the
 * cycle counter is secondary, because on Apple silicon cntvct_el0 advances at a
 * fixed system timebase (cntfrq_el0) rather than at the core clock, so its ticks
 * must never be reported as core cycles.
 */

uint64_t timer_now_ns(void);

uint64_t timer_read_cycles(void);

/* Hz, or 0 when the rate is not readable on this architecture. */
uint64_t timer_cycle_frequency_hz(void);

const char *timer_cycle_source_name(void);

#endif
