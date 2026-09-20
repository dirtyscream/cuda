#include "multiply.h"

#include <assert.h>

#include "config.h"
#include "matrix.h"

/* No fused multiply-add: FMA leaves the product unrounded and would change the low
   bits, breaking the bit-exact match with the plain kernel. */
#pragma STDC FP_CONTRACT OFF

/* sse2neon implements __m128d only on aarch64, so this guard is load-bearing. */
#if defined(__x86_64__) || defined(__i386__)
#include <emmintrin.h>
#elif defined(__aarch64__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#include "sse2neon.h"
#pragma clang diagnostic pop
#else
#error "multiply_sse2.c requires SSE2 (x86) or sse2neon on aarch64"
#endif

_Static_assert(VECTOR_LANES == 2, "SSE2 packed doubles hold exactly two lanes");
_Static_assert(BLOCK_COLS % VECTOR_LANES == 0, "block row must be a whole number of vectors");
_Static_assert(BLOCK_ROWS == BLOCK_COLS, "square blocks: one block is both a left and a right operand");

#define VECTORS_PER_BLOCK_ROW (BLOCK_COLS / VECTOR_LANES)

_Static_assert(VECTORS_PER_BLOCK_ROW == 5, "accumulators below are unrolled for five vectors");

/*
 * Broadcast-and-accumulate (Intel AP-930): the left element is splatted across the
 * vector and multiplied by a forward, stride-1 row of the right block, so nothing is
 * ever transposed. The five accumulators are named variables rather than an array so
 * they stay in registers for the whole inner_k loop.
 */
static inline void accumulate_block_product_sse2(const matrix_element *const restrict left_block,
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

            accumulator_0 = _mm_add_pd(accumulator_0,
                                       _mm_mul_pd(left_broadcast, _mm_loadu_pd(right_row + 0 * VECTOR_LANES)));
            accumulator_1 = _mm_add_pd(accumulator_1,
                                       _mm_mul_pd(left_broadcast, _mm_loadu_pd(right_row + 1 * VECTOR_LANES)));
            accumulator_2 = _mm_add_pd(accumulator_2,
                                       _mm_mul_pd(left_broadcast, _mm_loadu_pd(right_row + 2 * VECTOR_LANES)));
            accumulator_3 = _mm_add_pd(accumulator_3,
                                       _mm_mul_pd(left_broadcast, _mm_loadu_pd(right_row + 3 * VECTOR_LANES)));
            accumulator_4 = _mm_add_pd(accumulator_4,
                                       _mm_mul_pd(left_broadcast, _mm_loadu_pd(right_row + 4 * VECTOR_LANES)));
        }

        _mm_storeu_pd(result_row + 0 * VECTOR_LANES, accumulator_0);
        _mm_storeu_pd(result_row + 1 * VECTOR_LANES, accumulator_1);
        _mm_storeu_pd(result_row + 2 * VECTOR_LANES, accumulator_2);
        _mm_storeu_pd(result_row + 3 * VECTOR_LANES, accumulator_3);
        _mm_storeu_pd(result_row + 4 * VECTOR_LANES, accumulator_4);
    }
}

void multiply_blocks_sse2(const block_matrix *left, const block_matrix *right, block_matrix *result)
{
    assert(left != NULL && right != NULL && result != NULL);
    assert(left->block_cols == right->block_rows);
    assert(result->block_rows == left->block_rows);
    assert(result->block_cols == right->block_cols);

    /* Same i-k-j order as the plain kernel; see multiply.h on why it may not change. */
    for (int block_row = 0; block_row < left->block_rows; ++block_row) {
        for (int block_k = 0; block_k < left->block_cols; ++block_k) {
            const matrix_element *const left_block = block_matrix_block_const(left, block_row, block_k);

            for (int block_col = 0; block_col < right->block_cols; ++block_col) {
                accumulate_block_product_sse2(left_block,
                                              block_matrix_block_const(right, block_k, block_col),
                                              block_matrix_block(result, block_row, block_col));
            }
        }
    }
}
