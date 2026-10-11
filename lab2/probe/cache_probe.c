/*
 * Load latency by working-set size: one pointer per cache line, each load waiting
 * for the previous one. "random" chains the lines in a random cycle that no
 * prefetcher can follow; "sequential" chains them in address order.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "cache.h"
#include "config.h"
#include "timer.h"

#define PROBE_MIN_BYTES ((size_t)16 * 1024)
#define PROBE_MAX_BYTES ((size_t)512 * 1024 * 1024)

/* Enough loads for a stable average, and at least two trips round a large set. */
#define PROBE_MIN_LOADS ((size_t)8 * 1000 * 1000)
#define PROBE_TRIPS 2u

#define PROBE_SEED 0x9E3779B97F4A7C15ULL
#define PROBE_XORSHIFT_MULTIPLIER 0x2545F4914F6CDD1DULL

/* The core takes tens of milliseconds to reach full clock. */
#define PROBE_WARM_UP_NANOSECONDS ((uint64_t)250 * 1000 * 1000)

#define BAR_WIDTH 40
#define BYTES_PER_KIBIBYTE ((size_t)1024)
#define BYTES_PER_MEBIBYTE ((size_t)1024 * 1024)

/* Keeps the chase from being optimised away. */
static volatile uintptr_t chase_sink;

static uint64_t next_random(uint64_t *state)
{
    *state ^= *state >> 12;
    *state ^= *state << 25;
    *state ^= *state >> 27;
    return *state * PROBE_XORSHIFT_MULTIPLIER;
}

static void **slot_at(char *buffer, size_t line_bytes, size_t index)
{
    return (void **)(void *)(buffer + index * line_bytes);
}

/* Sattolo's shuffle: a single cycle, so the chase visits every line. */
static void chain_random(char *buffer, size_t line_bytes, size_t slots, size_t *order, uint64_t *state)
{
    for (size_t index = 0; index < slots; ++index) {
        order[index] = index;
    }
    for (size_t index = slots - 1; index > 0; --index) {
        const size_t other = (size_t)(next_random(state) % index);
        const size_t kept = order[index];
        order[index] = order[other];
        order[other] = kept;
    }
    for (size_t index = 0; index < slots; ++index) {
        *slot_at(buffer, line_bytes, order[index]) =
            slot_at(buffer, line_bytes, order[(index + 1) % slots]);
    }
}

static void chain_sequential(char *buffer, size_t line_bytes, size_t slots)
{
    for (size_t index = 0; index < slots; ++index) {
        *slot_at(buffer, line_bytes, index) = slot_at(buffer, line_bytes, (index + 1) % slots);
    }
}

/* One untimed trip to warm the caches, then the timed loads. Nanoseconds per load. */
static double chase(char *buffer, size_t slots)
{
    void *pointer = buffer;
    for (size_t step = 0; step < slots; ++step) {
        pointer = *(void **)pointer;
    }

    size_t loads = slots * PROBE_TRIPS;
    if (loads < PROBE_MIN_LOADS) {
        loads = PROBE_MIN_LOADS;
    }

    const uint64_t before = timer_now_ns();
    for (size_t step = 0; step < loads; ++step) {
        pointer = *(void **)pointer;
    }
    const uint64_t after = timer_now_ns();

    chase_sink = (uintptr_t)pointer;
    return (double)(after - before) / (double)loads;
}

static void print_size(size_t bytes)
{
    if (bytes >= BYTES_PER_MEBIBYTE) {
        printf("%7zu MiB", bytes / BYTES_PER_MEBIBYTE);
    } else {
        printf("%7zu KiB", bytes / BYTES_PER_KIBIBYTE);
    }
}

int main(void)
{
    cache_geometry geometry;
    cache_geometry_detect(&geometry, OUTER_BLOCKS);
    const size_t line_bytes = geometry.line_bytes;

    char *const buffer = aligned_alloc(line_bytes, PROBE_MAX_BYTES);
    size_t *const order = malloc(PROBE_MAX_BYTES / line_bytes * sizeof(size_t));
    if (buffer == NULL || order == NULL) {
        fprintf(stderr, "error: failed to allocate %zu MiB for the probe\n",
                PROBE_MAX_BYTES / BYTES_PER_MEBIBYTE);
        free(order);
        free(buffer);
        return EXIT_FAILURE;
    }

    printf("=== load latency by working-set size ===\n");
    printf("one pointer per %zu-byte line, every load depends on the previous one\n", line_bytes);
    printf("L1d %zu KiB, %s %zu KiB (from sysctl)\n\n", geometry.l1_data_bytes / BYTES_PER_KIBIBYTE,
           geometry.l3_bytes > 0 ? "L3" : "L2", geometry.last_level_bytes / BYTES_PER_KIBIBYTE);

    double random_ns[32];
    double sequential_ns[32];
    size_t sizes[32];
    size_t count = 0;
    uint64_t state = PROBE_SEED;

    chain_sequential(buffer, line_bytes, PROBE_MIN_BYTES / line_bytes);
    const uint64_t warm_up_start = timer_now_ns();
    while (timer_now_ns() - warm_up_start < PROBE_WARM_UP_NANOSECONDS) {
        (void)chase(buffer, PROBE_MIN_BYTES / line_bytes);
    }

    for (size_t bytes = PROBE_MIN_BYTES; bytes <= PROBE_MAX_BYTES && count < 32; bytes *= 2) {
        const size_t slots = bytes / line_bytes;
        chain_random(buffer, line_bytes, slots, order, &state);
        random_ns[count] = chase(buffer, slots);
        chain_sequential(buffer, line_bytes, slots);
        sequential_ns[count] = chase(buffer, slots);
        sizes[count] = bytes;
        ++count;
    }

    const double slowest = random_ns[count - 1];
    double fastest = random_ns[0];
    for (size_t index = 1; index < count; ++index) {
        if (random_ns[index] < fastest) {
            fastest = random_ns[index];
        }
    }
    printf("%11s %12s %16s\n", "working set", "random, ns", "sequential, ns");
    for (size_t index = 0; index < count; ++index) {
        print_size(sizes[index]);
        printf(" %12.2f %16.2f  ", random_ns[index], sequential_ns[index]);

        const int bar = (int)(random_ns[index] / slowest * BAR_WIDTH + 0.5);
        for (int column = 0; column < BAR_WIDTH; ++column) {
            putchar(column < bar ? '#' : ' ');
        }
        if (sizes[index] == geometry.l1_data_bytes) {
            printf("  = L1d");
        } else if (sizes[index] == geometry.last_level_bytes) {
            printf("  = %s", geometry.l3_bytes > 0 ? "L3" : "L2");
        }
        putchar('\n');
    }

    printf("\na random load from memory (%zu MiB) costs %.0fx the fastest one (an L1d hit)\n",
           sizes[count - 1] / BYTES_PER_MEBIBYTE, slowest / fastest);
    printf("from a few MiB up the random figures include TLB misses as well\n");

    free(order);
    free(buffer);
    return EXIT_SUCCESS;
}
