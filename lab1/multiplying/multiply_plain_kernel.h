#ifndef LAB1_MULTIPLY_PLAIN_KERNEL_H
#define LAB1_MULTIPLY_PLAIN_KERNEL_H

#include <assert.h>

#include "config.h"
#include "matrix.h"

/*
 * Shared verbatim by multiply_auto.c and multiply_scalar.c, so the vectorized and
 * the non-vectorized build are provably made from identical source and differ only
 * in compiler flags.
 */

/* Contraction into fused multiply-add would round differently from the SSE2 kernel,
   which issues a separate multiply and add, and the results would stop matching. */
#pragma STDC FP_CONTRACT OFF

_Static_assert(BLOCK_ROWS == BLOCK_COLS, "square blocks: one block is both a left and a right operand");

/*
 * restrict sits on the parameters, not on the caller's block pointers: that is where
 * the compiler actually uses it to prove the iterations independent. Without this
 * split the loop stays scalar and the kernel runs about seven times slower.
 */
static inline void accumulate_scaled_row(matrix_element *const restrict result_row,
                                         const matrix_element *const restrict right_row,
                                         const matrix_element left_value)
{
    for (int inner_col = 0; inner_col < BLOCK_COLS; ++inner_col) {
        result_row[inner_col] += left_value * right_row[inner_col];
    }
}

static inline void accumulate_block_product(const matrix_element *const restrict left_block,
                                            const matrix_element *const restrict right_block,
                                            matrix_element *const restrict result_block)
{
    for (int inner_row = 0; inner_row < BLOCK_ROWS; ++inner_row) {
        for (int inner_k = 0; inner_k < BLOCK_COLS; ++inner_k) {
            const matrix_element left_value = left_block[inner_row * BLOCK_COLS + inner_k];

            accumulate_scaled_row(result_block + inner_row * BLOCK_COLS,
                                  right_block + inner_k * BLOCK_COLS,
                                  left_value);
        }
    }
}

static inline void multiply_blocks_plain(const block_matrix *const left,
                                         const block_matrix *const right, 
                                         block_matrix *const result)
{
    assert(left != NULL && right != NULL && result != NULL);
    assert(left->block_cols == right->block_rows);
    assert(result->block_rows == left->block_rows);
    assert(result->block_cols == right->block_cols);

    for (int block_row = 0; block_row < left->block_rows; ++block_row) {
        for (int block_k = 0; block_k < left->block_cols; ++block_k) {
            const matrix_element *const left_block =
                block_matrix_block_const(left, block_row, block_k);

            for (int block_col = 0; block_col < right->block_cols; ++block_col) {
                accumulate_block_product(left_block,
                                         block_matrix_block_const(right, block_k, block_col),
                                         block_matrix_block(result, block_row, block_col));
            }
        }
    }
}

#endif
