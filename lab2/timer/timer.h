#ifndef LAB2_TIMER_H
#define LAB2_TIMER_H

#include <stdint.h>

/* clock() and time() are forbidden by the lab. On Apple silicon cntvct_el0 ticks at
   a fixed timebase (cntfrq_el0), not at the core clock: its ticks are not cycles. */

uint64_t timer_now_ns(void);

uint64_t timer_read_cycles(void);

/* Hz, or 0 when the rate is not readable on this architecture. */
uint64_t timer_cycle_frequency_hz(void);

const char *timer_cycle_source_name(void);

#endif
