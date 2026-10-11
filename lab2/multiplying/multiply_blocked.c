#include "multiply.h"

#include <assert.h>

#include "simd.h"

/*
 * C2. A sub-matrix of B (depth x width blocks, sized for the last level cache) stays
 * put while every block row of A passes it. For one block row, its depth A blocks
 * stay in L1 across all columns of the sub-matrix, and five rows of each result
 * block stay in registers for the whole depth.
 */

_Static_assert(VECTORS_PER_BLOCK_ROW == 5, "a result row is five vectors");

/* REGISTER_ROWS rows of a result block, kept in registers across depth_blocks
   block products: C is loaded and stored once per sub-matrix, not once per block. */
static inline void accumulate_register_rows(const matrix_element *const restrict left_rows,
                                            const matrix_element *const restrict right_first,
                                            size_t right_block_stride, int depth_blocks,
                                            matrix_element *const restrict result_rows)
{
    __m128d accumulator[REGISTER_ROWS][VECTORS_PER_BLOCK_ROW];
    for (int row = 0; row < REGISTER_ROWS; ++row) {
        for (int vector = 0; vector < VECTORS_PER_BLOCK_ROW; ++vector) {
            accumulator[row][vector] =
                _mm_loadu_pd(result_rows + row * BLOCK_COLS + vector * VECTOR_LANES);
        }
    }

    for (int block = 0; block < depth_blocks; ++block) {
        /* A blocks of a block row are adjacent; B blocks of a block column are a
           whole block row of B apart -- inside the sub-matrix, so they hit L2. */
        const matrix_element *const left_block = left_rows + (size_t)block * BLOCK_ELEMENT_COUNT;
        const matrix_element *const right_block = right_first + (size_t)block * right_block_stride;

        for (int inner_k = 0; inner_k < BLOCK_COLS; ++inner_k) {
            __m128d right[VECTORS_PER_BLOCK_ROW];
            for (int vector = 0; vector < VECTORS_PER_BLOCK_ROW; ++vector) {
                right[vector] = _mm_loadu_pd(right_block + inner_k * BLOCK_COLS + vector * VECTOR_LANES);
            }
            for (int row = 0; row < REGISTER_ROWS; ++row) {
                const __m128d left = _mm_set1_pd(left_block[row * BLOCK_COLS + inner_k]);
                for (int vector = 0; vector < VECTORS_PER_BLOCK_ROW; ++vector) {
                    accumulator[row][vector] =
                        accumulate_product(accumulator[row][vector], left, right[vector]);
                }
            }
        }
    }

    for (int row = 0; row < REGISTER_ROWS; ++row) {
        for (int vector = 0; vector < VECTORS_PER_BLOCK_ROW; ++vector) {
            _mm_storeu_pd(result_rows + row * BLOCK_COLS + vector * VECTOR_LANES,
                          accumulator[row][vector]);
        }
    }
}

static void multiply_row_by_submatrix(const block_matrix *left, const block_matrix *right,
                                      block_matrix *result, int block_row,
                                      int first_k, int depth_blocks, int first_col, int width_blocks)
{
    const matrix_element *const left_first = block_matrix_block_const(left, block_row, first_k);
    const size_t right_block_stride = (size_t)right->block_cols * BLOCK_ELEMENT_COUNT;

    for (int block_col = first_col; block_col < first_col + width_blocks; ++block_col) {
        const matrix_element *const right_first = block_matrix_block_const(right, first_k, block_col);
        matrix_element *const result_block = block_matrix_block(result, block_row, block_col);

        /* The second pass over this column of B finds it in L1. */
        for (int first_row = 0; first_row < BLOCK_ROWS; first_row += REGISTER_ROWS) {
            accumulate_register_rows(left_first + first_row * BLOCK_COLS, right_first,
                                     right_block_stride, depth_blocks,
                                     result_block + first_row * BLOCK_COLS);
        }
    }
}

void multiply_blocked(const block_matrix *left, const block_matrix *right, block_matrix *result,
                      const cache_geometry *geometry)
{
    assert(left != NULL && right != NULL && result != NULL && geometry != NULL);
    assert(left->block_cols == right->block_rows);
    assert(result->block_rows == left->block_rows);
    assert(result->block_cols == right->block_cols);
    assert(left->block_cols % geometry->submatrix_depth_blocks == 0);
    assert(right->block_cols % geometry->submatrix_width_blocks == 0);

    const int depth = geometry->submatrix_depth_blocks;
    const int width = geometry->submatrix_width_blocks;

    /* Sub-matrices go in increasing depth, so every element of C still sums k
       upward, as in C1. */
    for (int first_k = 0; first_k < left->block_cols; first_k += depth) {
        for (int first_col = 0; first_col < right->block_cols; first_col += width) {
            for (int block_row = 0; block_row < left->block_rows; ++block_row) {
                multiply_row_by_submatrix(left, right, result, block_row, first_k, depth,
                                          first_col, width);
            }
        }
    }
}
