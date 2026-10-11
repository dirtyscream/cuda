#include "multiply.h"

#include <assert.h>
#include <math.h>

/* Built with -fno-vectorize -fno-slp-vectorize (Makefile), so C1 / C0 measures
   vectorisation alone. */

#pragma STDC FP_CONTRACT OFF

_Static_assert(BLOCK_ROWS == BLOCK_COLS, "square blocks: one block is both a left and a right operand");

/* The one-lane twin of accumulate_product in simd.h. */
static inline matrix_element accumulate_scalar_product(matrix_element accumulator,
                                                       matrix_element left, matrix_element right)
{
#if FUSED_MULTIPLY_ADD
    return fma(left, right, accumulator);
#else
    return accumulator + left * right;
#endif
}

static inline void accumulate_scaled_row(matrix_element *const restrict result_row,
                                         const matrix_element *const restrict right_row,
                                         const matrix_element left_value)
{
    for (int inner_col = 0; inner_col < BLOCK_COLS; ++inner_col) {
        result_row[inner_col] = accumulate_scalar_product(result_row[inner_col], left_value,
                                                          right_row[inner_col]);
    }
}

static inline void accumulate_block_product(const matrix_element *const restrict left_block,
                                            const matrix_element *const restrict right_block,
                                            matrix_element *const restrict result_block)
{
    for (int inner_row = 0; inner_row < BLOCK_ROWS; ++inner_row) {
        for (int inner_k = 0; inner_k < BLOCK_COLS; ++inner_k) {
            accumulate_scaled_row(result_block + inner_row * BLOCK_COLS,
                                  right_block + inner_k * BLOCK_COLS,
                                  left_block[inner_row * BLOCK_COLS + inner_k]);
        }
    }
}

void multiply_scalar(const block_matrix *left, const block_matrix *right, block_matrix *result)
{
    assert(left != NULL && right != NULL && result != NULL);
    assert(left->block_cols == right->block_rows);
    assert(result->block_rows == left->block_rows);
    assert(result->block_cols == right->block_cols);

    for (int block_row = 0; block_row < left->block_rows; ++block_row) {
        for (int block_col = 0; block_col < right->block_cols; ++block_col) {
            matrix_element *const result_block = block_matrix_block(result, block_row, block_col);

            for (int block_k = 0; block_k < left->block_cols; ++block_k) {
                accumulate_block_product(block_matrix_block_const(left, block_row, block_k),
                                         block_matrix_block_const(right, block_k, block_col),
                                         result_block);
            }
        }
    }
}
