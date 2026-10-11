#include "cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

#if defined(__APPLE__)
#include <sys/sysctl.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

#define FORMULA_DIVISOR 3          /* A, B and C share a cache level */
#define FORMULA_PERCENT 90         /* the 0.9 */

#define BLOCK_BYTES ((size_t)BLOCK_ELEMENT_COUNT * sizeof(matrix_element))

#if defined(__APPLE__)
static size_t read_sysctl_size(const char *name)
{
    size_t value = 0;
    size_t length = sizeof(value);

    if (sysctlbyname(name, &value, &length, NULL, 0) != 0) {
        return 0;
    }
    return value;
}
#endif

/* floor(L / 3 * 0.9) as L * 90 / 300: one division, so no double rounding. */
static size_t formula_limit(size_t cache_bytes)
{
    return cache_bytes * FORMULA_PERCENT / (FORMULA_DIVISOR * 100u);
}

/* Largest divisor of whole that is <= limit and a multiple of step. */
static int largest_divisor(int whole, int limit, int step)
{
    for (int candidate = (limit / step) * step; candidate >= step; candidate -= step) {
        if (whole % candidate == 0) {
            return candidate;
        }
    }
    return step;
}

static void detect_raw_sizes(cache_geometry *geometry)
{
#if defined(__APPLE__)
    geometry->line_bytes = read_sysctl_size("hw.cachelinesize");

    /* perflevel0 is the performance cluster. The unsuffixed names report the
       efficiency cluster (64 KiB / 6 MiB on M5). */
    geometry->l1_data_bytes = read_sysctl_size("hw.perflevel0.l1dcachesize");
    if (geometry->l1_data_bytes == 0) {
        geometry->l1_data_bytes = read_sysctl_size("hw.l1dcachesize");
    }
    geometry->l2_bytes = read_sysctl_size("hw.perflevel0.l2cachesize");
    if (geometry->l2_bytes == 0) {
        geometry->l2_bytes = read_sysctl_size("hw.l2cachesize");
    }
    geometry->l3_bytes = read_sysctl_size("hw.l3cachesize");
#elif defined(__linux__)
    const long line = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
    const long l1 = sysconf(_SC_LEVEL1_DCACHE_SIZE);
    const long l2 = sysconf(_SC_LEVEL2_CACHE_SIZE);
    const long l3 = sysconf(_SC_LEVEL3_CACHE_SIZE);
    geometry->line_bytes = line > 0 ? (size_t)line : 0;
    geometry->l1_data_bytes = l1 > 0 ? (size_t)l1 : 0;
    geometry->l2_bytes = l2 > 0 ? (size_t)l2 : 0;
    geometry->l3_bytes = l3 > 0 ? (size_t)l3 : 0;
#endif

    if (geometry->line_bytes == 0) {
        geometry->line_bytes = CACHE_LINE_BYTES;
    }
    if (geometry->l1_data_bytes == 0) {
        geometry->l1_data_bytes = 32u * 1024u;
    }
    if (geometry->l2_bytes == 0) {
        geometry->l2_bytes = 1024u * 1024u;
    }
}

/* LAB2_DEPTH and LAB2_WIDTH override the sizes for experiments; a value that breaks
   a sub-matrix rule is ignored. */
static void override_from_environment(const char *name, int *target, int outer_blocks)
{
    const char *const text = getenv(name);
    if (text == NULL) {
        return;
    }

    const long value = strtol(text, NULL, 10);
    if (value < WIDTH_GRANULE_BLOCKS || value > outer_blocks
        || outer_blocks % (int)value != 0 || (int)value % WIDTH_GRANULE_BLOCKS != 0) {
        return;
    }
    *target = (int)value;
}

void cache_geometry_detect(cache_geometry *geometry, int outer_blocks)
{
    memset(geometry, 0, sizeof(*geometry));
    detect_raw_sizes(geometry);

    /* Apple silicon has no L3; the cluster L2, shared like an x86 L3, stands in. */
    if (geometry->l3_bytes > 0) {
        geometry->last_level_bytes = geometry->l3_bytes;
        geometry->last_level_label = "L3";
    } else {
        geometry->last_level_bytes = geometry->l2_bytes;
        geometry->last_level_label = "L2";
    }

    geometry->last_level_limit_bytes = formula_limit(geometry->last_level_bytes);
    geometry->l1_limit_bytes = formula_limit(geometry->l1_data_bytes);

    /* Depth from L1d: the A blocks of one block row (1 x depth) and one column of
       the B sub-matrix (depth x 1) are each held to the L1d limit. */
    const int depth_limit = (int)(geometry->l1_limit_bytes / BLOCK_BYTES);
    geometry->submatrix_depth_blocks = largest_divisor(outer_blocks, depth_limit, WIDTH_GRANULE_BLOCKS);

    /* Width from the last level: the whole B sub-matrix is held to its limit. */
    const int width_limit = (int)(geometry->last_level_limit_bytes
                                  / ((size_t)geometry->submatrix_depth_blocks * BLOCK_BYTES));
    geometry->submatrix_width_blocks = largest_divisor(outer_blocks, width_limit, WIDTH_GRANULE_BLOCKS);

    override_from_environment("LAB2_DEPTH", &geometry->submatrix_depth_blocks, outer_blocks);
    override_from_environment("LAB2_WIDTH", &geometry->submatrix_width_blocks, outer_blocks);
}

void cache_geometry_print(const cache_geometry *geometry)
{
    const size_t depth = (size_t)geometry->submatrix_depth_blocks;
    const size_t width = (size_t)geometry->submatrix_width_blocks;
    const size_t a_width_bytes = depth * BLOCK_COLS * sizeof(matrix_element);
    const size_t b_width_bytes = width * BLOCK_COLS * sizeof(matrix_element);
    const char *const last = geometry->last_level_label;

    printf("=== cache geometry ===\n");
    printf("cache line              : %zu bytes\n", geometry->line_bytes);
    printf("L1 data                 : %zu KiB\n", geometry->l1_data_bytes / 1024u);
    printf("L2                      : %zu KiB\n", geometry->l2_bytes / 1024u);
    if (geometry->l3_bytes > 0) {
        printf("L3                      : %zu KiB\n", geometry->l3_bytes / 1024u);
    } else {
        printf("L3                      : absent, L2 takes its place\n");
    }
    printf("max sub-matrix, %-3s     : %zu bytes (%s / 3 * 0.9)\n",
           last, geometry->last_level_limit_bytes, last);
    printf("max sub-matrix, L1d     : %zu bytes (L1d / 3 * 0.9)\n", geometry->l1_limit_bytes);
    printf("B sub-matrix, in %-3s    : %zu x %zu blocks, %zu bytes\n",
           last, depth, width, depth * width * BLOCK_BYTES);
    printf("  width of A sub-matrix : %zu blocks = %zu bytes (mod 64 = %zu)\n",
           depth, a_width_bytes, a_width_bytes % REQUIRED_WIDTH_BYTES);
    printf("  width of B sub-matrix : %zu blocks = %zu bytes (mod 64 = %zu)\n",
           width, b_width_bytes, b_width_bytes % REQUIRED_WIDTH_BYTES);
    printf("in L1d                  : A 1 x %zu blocks + B column %zu x 1 blocks, %zu bytes each\n",
           depth, depth, depth * BLOCK_BYTES);
    printf("in registers            : %d rows x %d columns of a result block (%d accumulators)\n",
           REGISTER_ROWS, BLOCK_COLS, REGISTER_ROWS * VECTORS_PER_BLOCK_ROW);
    printf("\n");
}
