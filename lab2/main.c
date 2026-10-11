#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(__APPLE__)
#include <pthread.h>
#include <sys/resource.h>
#endif

#include "cache.h"
#include "config.h"
#include "matrix.h"
#include "multiply.h"
#include "timer.h"

#define FILL_SEED_LEFT 12345u
#define FILL_SEED_RIGHT 67890u

#define NANOSECONDS_PER_MILLISECOND 1000000.0
#define NANOSECONDS_PER_SECOND_AS_DOUBLE 1000000000.0
#define FLOPS_PER_GIGAFLOP 1000000000.0
#define BYTES_PER_MEBIBYTE (1024.0 * 1024.0)

/* Every disturbance can only add time, so the fastest run is the robust estimate. */
#define MEASUREMENT_REPEAT_COUNT 3

#define TOTAL_FLOATING_POINT_OPERATIONS \
    (2.0 * (double)MATRIX_SIZE * (double)MATRIX_SIZE * (double)MATRIX_SIZE)

/* C1 is the reference of every bit-exact check. */
typedef enum {
    version_scalar = 0,
    version_vectorized,
    version_blocked,
    version_count
} version_slot;

typedef struct {
    const char *version_name;
    uint64_t elapsed_nanoseconds;
    uint64_t cycle_ticks;
} run_measurement;

typedef void (*multiply_function)(const block_matrix *left, const block_matrix *right,
                                  block_matrix *result, const cache_geometry *geometry);

/* A terminal launch runs at user-interactive QoS on a performance core. Background
   QoS or policy pins the process to the efficiency cores, whose caches the
   sub-matrices are not sized for, and the process cannot lift that itself -- so it
   only warns. */
typedef struct {
    const char *qos_name;
    bool clamped_to_efficiency_cores;
} scheduling_state;

static scheduling_state read_scheduling_state(void)
{
    scheduling_state state = {"not applicable on this platform", false};

#if defined(__APPLE__)
    const qos_class_t qos = qos_class_self();
    switch (qos) {
    case QOS_CLASS_USER_INTERACTIVE: state.qos_name = "user-interactive"; break;
    case QOS_CLASS_USER_INITIATED:   state.qos_name = "user-initiated"; break;
    case QOS_CLASS_DEFAULT:          state.qos_name = "default"; break;
    case QOS_CLASS_UTILITY:          state.qos_name = "utility"; break;
    case QOS_CLASS_BACKGROUND:       state.qos_name = "background"; break;
    default:                         state.qos_name = "unspecified"; break;
    }

    const bool background_policy = getpriority(PRIO_DARWIN_PROCESS, 0) != 0;
    state.clamped_to_efficiency_cores = qos == QOS_CLASS_BACKGROUND || background_policy;
#endif

    return state;
}

static void print_configuration(const scheduling_state *scheduling)
{
    const size_t matrix_bytes =
        (size_t)OUTER_BLOCKS * (size_t)OUTER_BLOCKS * BLOCK_ELEMENT_COUNT * sizeof(matrix_element);

    printf("=== configuration ===\n");
    printf("element type            : double (%zu bytes)\n", sizeof(matrix_element));
    printf("block size              : %d x %d elements\n", BLOCK_ROWS, BLOCK_COLS);
    printf("outer size (blocks)     : %d x %d\n", OUTER_BLOCKS, OUTER_BLOCKS);
    printf("flat size (elements)    : %d x %d (mod 64 = %d)\n",
           MATRIX_SIZE, MATRIX_SIZE, MATRIX_SIZE % 64);
    printf("bytes per matrix        : %zu (%.2f MiB)\n",
           matrix_bytes, (double)matrix_bytes / BYTES_PER_MEBIBYTE);
    printf("allocation alignment    : %d bytes\n", CACHE_LINE_BYTES);
    printf("arithmetic              : %s\n",
           FUSED_MULTIPLY_ADD ? "fused multiply-add" : "separate multiply and add (as lab 1)");
    printf("input data              : random reals in [-1, 1), full 53-bit significands\n");
    printf("total floating point ops: %.0f\n", TOTAL_FLOATING_POINT_OPERATIONS);
    printf("thread QoS              : %s\n", scheduling->qos_name);
    if (scheduling->clamped_to_efficiency_cores) {
        printf("WARNING                 : background QoS or policy -- the process is held on the\n"
               "                          efficiency cores, whose caches the sub-matrices are not sized\n"
               "                          for; results stay correct, timings are not representative\n");
    }
    printf("cycle counter source    : %s\n", timer_cycle_source_name());

    const uint64_t frequency_hz = timer_cycle_frequency_hz();
    if (frequency_hz == 0u) {
        printf("cycle counter frequency : unknown (ticks reported raw, not as core cycles)\n");
    } else {
        printf("cycle counter frequency : %" PRIu64 " Hz (timebase, not the core clock)\n",
               frequency_hz);
    }
    printf("\n");
}

static run_measurement run_once(const char *version_name, multiply_function multiply,
                                const block_matrix *left, const block_matrix *right,
                                block_matrix *result, const cache_geometry *geometry)
{
    run_measurement measurement;
    measurement.version_name = version_name;
    measurement.elapsed_nanoseconds = UINT64_MAX;
    measurement.cycle_ticks = 0u;

    for (int repetition = 0; repetition < MEASUREMENT_REPEAT_COUNT; ++repetition) {
        /* The kernels accumulate; zeroing stays outside the timed region. */
        block_matrix_clear(result);

        const uint64_t cycles_before = timer_read_cycles();
        const uint64_t nanoseconds_before = timer_now_ns();

        multiply(left, right, result, geometry);

        const uint64_t nanoseconds_after = timer_now_ns();
        const uint64_t cycles_after = timer_read_cycles();

        const uint64_t elapsed = nanoseconds_after - nanoseconds_before;
        if (elapsed < measurement.elapsed_nanoseconds) {
            measurement.elapsed_nanoseconds = elapsed;
            measurement.cycle_ticks = cycles_after - cycles_before;
        }
    }

    return measurement;
}

/* Only C2 needs the cache geometry. */
static void call_scalar(const block_matrix *l, const block_matrix *r, block_matrix *o,
                        const cache_geometry *g)
{
    (void)g;
    multiply_scalar(l, r, o);
}

static void call_vectorized(const block_matrix *l, const block_matrix *r, block_matrix *o,
                            const cache_geometry *g)
{
    (void)g;
    multiply_vectorized(l, r, o);
}

static double gigaflops_of(const run_measurement *measurement)
{
    if (measurement->elapsed_nanoseconds == 0u) {
        return 0.0;
    }

    const double seconds =
        (double)measurement->elapsed_nanoseconds / NANOSECONDS_PER_SECOND_AS_DOUBLE;
    return TOTAL_FLOATING_POINT_OPERATIONS / seconds / FLOPS_PER_GIGAFLOP;
}

static double speedup_over(const run_measurement *measurement, const run_measurement *baseline)
{
    if (measurement->elapsed_nanoseconds == 0u) {
        return 0.0;
    }
    return (double)baseline->elapsed_nanoseconds / (double)measurement->elapsed_nanoseconds;
}

static void print_results(const run_measurement *measurements)
{
    printf("=== results ===\n");
    printf("%-26s %12s %16s %10s %8s %8s\n", "version", "time, ms", "counter ticks", "GFLOP/s",
           "vs C0", "vs C1");

    for (int slot = 0; slot < version_count; ++slot) {
        const run_measurement *measurement = &measurements[slot];
        printf("%-26s %12.3f %16" PRIu64 " %10.3f %7.2fx %7.2fx\n",
               measurement->version_name,
               (double)measurement->elapsed_nanoseconds / NANOSECONDS_PER_MILLISECOND,
               measurement->cycle_ticks,
               gigaflops_of(measurement),
               speedup_over(measurement, &measurements[version_scalar]),
               speedup_over(measurement, &measurements[version_vectorized]));
    }
    printf("ticks come from %s and are raw counter units; time comes from CLOCK_MONOTONIC_RAW.\n\n",
           timer_cycle_source_name());
}

static bool report_verification(const char *description, bool passed)
{
    printf("%-36s : %s\n", description, passed ? "PASS" : "FAIL");
    return passed;
}

int main(void)
{
    block_matrix left = {0};
    block_matrix right = {0};
    block_matrix results[version_count] = {{0}};
    int exit_status = EXIT_FAILURE;

    const scheduling_state scheduling = read_scheduling_state();

    cache_geometry geometry;
    cache_geometry_detect(&geometry, OUTER_BLOCKS);

    bool allocated = block_matrix_create(&left, OUTER_BLOCKS, OUTER_BLOCKS)
                     && block_matrix_create(&right, OUTER_BLOCKS, OUTER_BLOCKS);
    for (int slot = 0; slot < version_count && allocated; ++slot) {
        allocated = block_matrix_create(&results[slot], OUTER_BLOCKS, OUTER_BLOCKS);
    }
    if (!allocated) {
        fprintf(stderr, "error: failed to allocate the matrices\n");
        goto cleanup;
    }

    print_configuration(&scheduling);
    cache_geometry_print(&geometry);

    block_matrix_fill_random(&left, FILL_SEED_LEFT);
    block_matrix_fill_random(&right, FILL_SEED_RIGHT);

    run_measurement measurements[version_count];
    measurements[version_scalar] = run_once("C0 (scalar)", call_scalar, &left, &right,
                                            &results[version_scalar], &geometry);
    measurements[version_vectorized] = run_once("C1 (vectorized, lab 1)", call_vectorized, &left,
                                                &right, &results[version_vectorized], &geometry);
    measurements[version_blocked] = run_once("C2 (cache-optimised)", multiply_blocked, &left,
                                             &right, &results[version_blocked], &geometry);

    /* The check is the left operand of && so it runs even after a failure. */
    printf("=== verification (memcmp, no epsilon) ===\n");
    bool correctness_passed = report_verification(
        "C2 equals C1, bit-exact",
        block_matrix_equals(&results[version_vectorized], &results[version_blocked]));
    correctness_passed = report_verification(
        "C0 equals C1, bit-exact",
        block_matrix_equals(&results[version_vectorized], &results[version_scalar]))
        && correctness_passed;
    printf("\n");

    print_results(measurements);

    printf("verdict: %s\n", correctness_passed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    exit_status = correctness_passed ? EXIT_SUCCESS : EXIT_FAILURE;

cleanup:
    for (int slot = version_count - 1; slot >= 0; --slot) {
        block_matrix_destroy(&results[slot]);
    }
    block_matrix_destroy(&right);
    block_matrix_destroy(&left);
    return exit_status;
}
