#ifndef LAB2_CACHE_H
#define LAB2_CACHE_H

#include <stddef.h>

/* Cache sizes read at run time, and the sub-matrix sizes the statement's formula
   floor(L / 3 * 0.9) gives for L1d (depth) and the last level cache (width). */
typedef struct {
    size_t line_bytes;
    size_t l1_data_bytes;
    size_t l2_bytes;
    size_t l3_bytes;                /* 0 when there is no L3 */
    size_t last_level_bytes;
    const char *last_level_label;   /* "L3", or "L2" standing in for it */

    size_t last_level_limit_bytes;  /* floor(last level / 3 * 0.9) */
    size_t l1_limit_bytes;          /* floor(L1d / 3 * 0.9) */

    int submatrix_depth_blocks;     /* block rows of the B sub-matrix = width of the A one */
    int submatrix_width_blocks;     /* block columns of the B sub-matrix */
} cache_geometry;

void cache_geometry_detect(cache_geometry *geometry, int outer_blocks);

void cache_geometry_print(const cache_geometry *geometry);

#endif
