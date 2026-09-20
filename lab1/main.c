#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

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

/* Every disturbance can only add time, so the fastest of several runs is the robust
   estimate. With a single run the "sse2 not slower" verdict flipped about one run
   in four on an idle machine. */
#define MEASUREMENT_REPEAT_COUNT 3

#define TOTAL_FLOATING_POINT_OPERATIONS \
    (2.0 * (double)OUTER_ROWS_A * (double)OUTER_COLS_A * (double)OUTER_COLS_B \
     * (double)BLOCK_ROWS * (double)BLOCK_COLS * (double)BLOCK_COLS)

typedef void (*multiply_function)(const block_matrix *left, const block_matrix *right, block_matrix *result);

/* Slot 0 is the baseline the speedup column divides by. */
typedef enum {
    measurement_scalar = 0,
    measurement_auto,
    measurement_sse2,
    measurement_count
} measurement_slot;

typedef struct {
    const char *version_name;
    uint64_t elapsed_nanoseconds;
    uint64_t cycle_ticks;
} run_measurement;

static void print_configuration(void)
{
    const size_t left_bytes = (size_t)OUTER_ROWS_A * (size_t)OUTER_COLS_A
                              * (size_t)BLOCK_ELEMENT_COUNT * sizeof(matrix_element);

    printf("=== configuration ===\n");
    printf("element type            : double (%zu bytes)\n", sizeof(matrix_element));
    printf("block size              : %d x %d elements\n", BLOCK_ROWS, BLOCK_COLS);
    printf("outer size (blocks)     : A %d x %d, B %d x %d, C %d x %d\n",
           OUTER_ROWS_A, OUTER_COLS_A, OUTER_COLS_A, OUTER_COLS_B, OUTER_ROWS_A, OUTER_COLS_B);
    printf("flat size (elements)    : A %d x %d, B %d x %d, C %d x %d\n",
           OUTER_ROWS_A * BLOCK_ROWS, OUTER_COLS_A * BLOCK_COLS,
           OUTER_COLS_A * BLOCK_ROWS, OUTER_COLS_B * BLOCK_COLS,
           OUTER_ROWS_A * BLOCK_ROWS, OUTER_COLS_B * BLOCK_COLS);
    printf("bytes per matrix        : %zu (%.2f MiB)\n",
           left_bytes, (double)left_bytes / BYTES_PER_MEBIBYTE);
    printf("allocation alignment    : %d bytes\n", MATRIX_ALIGNMENT);
    printf("vector lanes per 128 bit: %d\n", VECTOR_LANES);
    printf("total floating point ops: %.0f\n", TOTAL_FLOATING_POINT_OPERATIONS);
    printf("timed runs per version  : %d (the fastest one is reported)\n", MEASUREMENT_REPEAT_COUNT);
    printf("cycle counter source    : %s\n", timer_cycle_source_name());

    const uint64_t frequency_hz = timer_cycle_frequency_hz();
    if (frequency_hz == 0u) {
        printf("cycle counter frequency : unknown (ticks reported raw, not as core cycles)\n");
    } else {
        printf("cycle counter frequency : %" PRIu64 " Hz (timebase, not necessarily the core clock)\n",
               frequency_hz);
    }
    printf("\n");
}

static run_measurement run_multiplication(const char *version_name,
                                          multiply_function multiply,
                                          const block_matrix *left,
                                          const block_matrix *right,
                                          block_matrix *result)
{
    run_measurement measurement;
    measurement.version_name = version_name;
    measurement.elapsed_nanoseconds = UINT64_MAX;
    measurement.cycle_ticks = 0u;

    for (int repetition = 0; repetition < MEASUREMENT_REPEAT_COUNT; ++repetition) {
        /* The kernels accumulate and never clear, so zeroing happens here, outside
           the timed region. */
        block_matrix_clear(result);

        const uint64_t cycles_before = timer_read_cycles();
        const uint64_t nanoseconds_before = timer_now_ns();

        multiply(left, right, result);

        const uint64_t nanoseconds_after = timer_now_ns();
        const uint64_t cycles_after = timer_read_cycles();

        const uint64_t elapsed_nanoseconds = nanoseconds_after - nanoseconds_before;
        if (elapsed_nanoseconds < measurement.elapsed_nanoseconds) {
            measurement.elapsed_nanoseconds = elapsed_nanoseconds;
            measurement.cycle_ticks = cycles_after - cycles_before;
        }
    }

    return measurement;
}

static double gigaflops_of(const run_measurement *measurement)
{
    if (measurement->elapsed_nanoseconds == 0u) {
        return 0.0;
    }

    const double elapsed_seconds =
        (double)measurement->elapsed_nanoseconds / NANOSECONDS_PER_SECOND_AS_DOUBLE;
    return TOTAL_FLOATING_POINT_OPERATIONS / elapsed_seconds / FLOPS_PER_GIGAFLOP;
}

static double speedup_over_baseline(const run_measurement *measurement, const run_measurement *baseline)
{
    if (measurement->elapsed_nanoseconds == 0u) {
        return 0.0;
    }

    return (double)baseline->elapsed_nanoseconds / (double)measurement->elapsed_nanoseconds;
}

static void print_measurement_table(const run_measurement *measurements, size_t measurement_count)
{
    printf("=== results ===\n");
    printf("%-22s %14s %20s %12s %10s\n", "version", "time, ms", "counter ticks", "GFLOP/s", "speedup");

    for (size_t index = 0; index < measurement_count; ++index) {
        const run_measurement *measurement = &measurements[index];
        printf("%-22s %14.3f %20" PRIu64 " %12.3f %9.2fx\n",
               measurement->version_name,
               (double)measurement->elapsed_nanoseconds / NANOSECONDS_PER_MILLISECOND,
               measurement->cycle_ticks,
               gigaflops_of(measurement),
               speedup_over_baseline(measurement, &measurements[measurement_scalar]));
    }

    printf("speedup is relative to %s; ticks come from %s and are raw counter units.\n\n",
           measurements[measurement_scalar].version_name, timer_cycle_source_name());
}

static bool report_verification(const char *description, bool passed)
{
    printf("%-42s : %s\n", description, passed ? "PASS" : "FAIL");
    return passed;
}

int main(void)
{
    block_matrix left = {0};
    block_matrix right = {0};
    block_matrix result_scalar = {0};
    block_matrix result_auto = {0};
    block_matrix result_sse2 = {0};
    int exit_status = EXIT_FAILURE;

    const bool allocated = block_matrix_create(&left, OUTER_ROWS_A, OUTER_COLS_A)
                           && block_matrix_create(&right, OUTER_COLS_A, OUTER_COLS_B)
                           && block_matrix_create(&result_scalar, OUTER_ROWS_A, OUTER_COLS_B)
                           && block_matrix_create(&result_auto, OUTER_ROWS_A, OUTER_COLS_B)
                           && block_matrix_create(&result_sse2, OUTER_ROWS_A, OUTER_COLS_B);
    if (!allocated) {
        fprintf(stderr, "error: failed to allocate the block matrices\n");
        goto cleanup;
    }

    print_configuration();

    block_matrix_fill_deterministic(&left, FILL_SEED_LEFT);
    block_matrix_fill_deterministic(&right, FILL_SEED_RIGHT);

    run_measurement measurements[measurement_count];
    measurements[measurement_scalar] =
        run_multiplication("scalar (no vector)", multiply_blocks_scalar, &left, &right, &result_scalar);
    measurements[measurement_auto] =
        run_multiplication("auto-vectorized C1", multiply_blocks_auto, &left, &right, &result_auto);
    measurements[measurement_sse2] =
        run_multiplication("sse2 intrinsics C2", multiply_blocks_sse2, &left, &right, &result_sse2);

    printf("=== verification ===\n");
    bool correctness_passed = report_verification("C1 (auto) equals C2 (sse2), bit-exact",
                                                  block_matrix_equals(&result_auto, &result_sse2));
    /* The accumulated flag is deliberately the right operand: && must not
       short-circuit away the call, which has to print its line either way. */
    correctness_passed = report_verification("baseline (scalar) equals C1 (auto)",
                                             block_matrix_equals(&result_scalar, &result_auto))
                         && correctness_passed;
    printf("\n");

    print_measurement_table(measurements, (size_t)measurement_count);

    /* The lab lists this as a hard requirement, so it gates the exit status too. */
    printf("=== performance requirement ===\n");
    const bool performance_passed =
        report_verification("sse2 (C2) is not slower than auto (C1)",
                            measurements[measurement_sse2].elapsed_nanoseconds
                                <= measurements[measurement_auto].elapsed_nanoseconds);
    printf("\n");

    const bool all_requirements_passed = correctness_passed && performance_passed;
    printf("verdict: %s\n", all_requirements_passed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    exit_status = all_requirements_passed ? EXIT_SUCCESS : EXIT_FAILURE;

cleanup:
    block_matrix_destroy(&result_sse2);
    block_matrix_destroy(&result_auto);
    block_matrix_destroy(&result_scalar);
    block_matrix_destroy(&right);
    block_matrix_destroy(&left);
    return exit_status;
}
