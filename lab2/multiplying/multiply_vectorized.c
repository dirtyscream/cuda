#include "multiply.h"

#include <assert.h>

#include "simd.h"

_Static_assert(VECTORS_PER_BLOCK_ROW == 5, "accumulators below are unrolled for five vectors");

/* Lab 1's SSE2 kernel, unchanged: an element of A is broadcast and multiplied by a
   row of B (Intel AP-930), so nothing is transposed. */
static inline void accumulate_block_product_lab1(const matrix_element *const restrict left_block,
                                                 const matrix_element *const restrict right_block,
                                                 matrix_element *const restrict result_block)
{
    for (int inner_row = 0; inner_row < BLOCK_ROWS; ++inner_row) {
        matrix_element *const restrict result_row = result_block + inner_row * BLOCK_COLS;

        __m128d accumulator_0 = _mm_loadu_pd(result_row + 0 * VECTOR_LANES);
        __m128d accumulator_1 = _mm_loadu_pd(result_row + 1 * VECTOR_LANES);
        __m128d accumulator_2 = _mm_loadu_pd(result_row + 2 * VECTOR_LANES);
        __m128d accumulator_3 = _mm_loadu_pd(result_row + 3 * VECTOR_LANES);
        __m128d accumulator_4 = _mm_loadu_pd(result_row + 4 * VECTOR_LANES);

        for (int inner_k = 0; inner_k < BLOCK_COLS; ++inner_k) {
            const __m128d left_broadcast = _mm_set1_pd(left_block[inner_row * BLOCK_COLS + inner_k]);
            const matrix_element *const restrict right_row = right_block + inner_k * BLOCK_COLS;

            accumulator_0 = accumulate_product(accumulator_0, left_broadcast,
                                               _mm_loadu_pd(right_row + 0 * VECTOR_LANES));
            accumulator_1 = accumulate_product(accumulator_1, left_broadcast,
                                               _mm_loadu_pd(right_row + 1 * VECTOR_LANES));
            accumulator_2 = accumulate_product(accumulator_2, left_broadcast,
                                               _mm_loadu_pd(right_row + 2 * VECTOR_LANES));
            accumulator_3 = accumulate_product(accumulator_3, left_broadcast,
                                               _mm_loadu_pd(right_row + 3 * VECTOR_LANES));
            accumulator_4 = accumulate_product(accumulator_4, left_broadcast,
                                               _mm_loadu_pd(right_row + 4 * VECTOR_LANES));
        }

        _mm_storeu_pd(result_row + 0 * VECTOR_LANES, accumulator_0);
        _mm_storeu_pd(result_row + 1 * VECTOR_LANES, accumulator_1);
        _mm_storeu_pd(result_row + 2 * VECTOR_LANES, accumulator_2);
        _mm_storeu_pd(result_row + 3 * VECTOR_LANES, accumulator_3);
        _mm_storeu_pd(result_row + 4 * VECTOR_LANES, accumulator_4);
    }
}

void multiply_vectorized(const block_matrix *left, const block_matrix *right, block_matrix *result)
{
    assert(left != NULL && right != NULL && result != NULL);
    assert(left->block_cols == right->block_rows);
    assert(result->block_rows == left->block_rows);
    assert(result->block_cols == right->block_cols);

    /* Order i, j, k: B is walked down a block column, a whole block row (307 KB)
       per step, and all of B is read again for every block row of C. */
    for (int block_row = 0; block_row < left->block_rows; ++block_row) {
        for (int block_col = 0; block_col < right->block_cols; ++block_col) {
            matrix_element *const result_block = block_matrix_block(result, block_row, block_col);

            for (int block_k = 0; block_k < left->block_cols; ++block_k) {
                accumulate_block_product_lab1(block_matrix_block_const(left, block_row, block_k),
                                              block_matrix_block_const(right, block_k, block_col),
                                              result_block);
            }
        }
    }
}
