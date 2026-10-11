#include "matrix.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* xorshift64* generator. */
#define FILL_XORSHIFT_MULTIPLIER 0x2545F4914F6CDD1DULL
#define FILL_ZERO_STATE_REPLACEMENT 0x9E3779B97F4A7C15ULL

/* The top 53 random bits times 2^-53: every double step of [0, 1). */
#define FILL_SIGNIFICAND_BITS 53
#define FILL_UNIT_SCALE (1.0 / 9007199254740992.0)

static size_t block_matrix_block_offset(const block_matrix *matrix, int block_row, int block_col)
{
    assert(matrix != NULL);
    assert(matrix->elements != NULL);
    assert(block_row >= 0 && block_row < matrix->block_rows);
    assert(block_col >= 0 && block_col < matrix->block_cols);

    const size_t block_index = (size_t)block_row * (size_t)matrix->block_cols + (size_t)block_col;
    return block_index * (size_t)BLOCK_ELEMENT_COUNT;
}

static size_t round_up_to_multiple(size_t value, size_t multiple)
{
    return ((value + multiple - 1u) / multiple) * multiple;
}

bool block_matrix_create(block_matrix *matrix, int block_rows, int block_cols)
{
    assert(matrix != NULL);

    matrix->elements = NULL;
    matrix->block_rows = 0;
    matrix->block_cols = 0;

    if (block_rows <= 0 || block_cols <= 0) {
        return false;
    }

    /* A wrapped size would allocate a short buffer, so every product is checked. */
    const size_t block_count = (size_t)block_rows * (size_t)block_cols;
    if (block_count / (size_t)block_rows != (size_t)block_cols) {
        return false;
    }
    if (block_count > SIZE_MAX / ((size_t)BLOCK_ELEMENT_COUNT * sizeof(matrix_element))) {
        return false;
    }

    const size_t element_count = block_count * (size_t)BLOCK_ELEMENT_COUNT;
    const size_t requested_bytes = element_count * sizeof(matrix_element);
    if (requested_bytes > SIZE_MAX - (size_t)CACHE_LINE_BYTES + 1u) {
        return false;
    }

    /* aligned_alloc requires a size that is a multiple of the alignment. */
    const size_t allocated_bytes = round_up_to_multiple(requested_bytes, (size_t)CACHE_LINE_BYTES);

    matrix_element *const elements = aligned_alloc((size_t)CACHE_LINE_BYTES, allocated_bytes);
    if (elements == NULL) {
        return false;
    }

    memset(elements, 0, allocated_bytes);

    matrix->elements = elements;
    matrix->block_rows = block_rows;
    matrix->block_cols = block_cols;
    return true;
}

void block_matrix_destroy(block_matrix *matrix)
{
    assert(matrix != NULL);

    free(matrix->elements);
    matrix->elements = NULL;
    matrix->block_rows = 0;
    matrix->block_cols = 0;
}

size_t block_matrix_element_count(const block_matrix *matrix)
{
    assert(matrix != NULL);

    return (size_t)matrix->block_rows * (size_t)matrix->block_cols * (size_t)BLOCK_ELEMENT_COUNT;
}

matrix_element *block_matrix_block(block_matrix *matrix, int block_row, int block_col)
{
    return matrix->elements + block_matrix_block_offset(matrix, block_row, block_col);
}

const matrix_element *block_matrix_block_const(const block_matrix *matrix, int block_row, int block_col)
{
    return matrix->elements + block_matrix_block_offset(matrix, block_row, block_col);
}

/* Random reals in [-1, 1): their sums round, so two kernels agree bit for bit only
   if they sum every element in the same order. Small integers would prove nothing --
   their sums are exact in any order. */
void block_matrix_fill_random(block_matrix *matrix, uint64_t seed)
{
    assert(matrix != NULL);
    assert(matrix->elements != NULL);

    const size_t element_count = block_matrix_element_count(matrix);
    uint64_t state = seed != 0u ? seed : FILL_ZERO_STATE_REPLACEMENT;

    for (size_t index = 0; index < element_count; ++index) {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        const uint64_t bits = (state * FILL_XORSHIFT_MULTIPLIER) >> (64 - FILL_SIGNIFICAND_BITS);

        matrix->elements[index] = 2.0 * ((double)bits * FILL_UNIT_SCALE) - 1.0;
    }
}

void block_matrix_clear(block_matrix *matrix)
{
    assert(matrix != NULL);
    assert(matrix->elements != NULL);

    memset(matrix->elements, 0, block_matrix_element_count(matrix) * sizeof(matrix_element));
}

bool block_matrix_equals(const block_matrix *left, const block_matrix *right)
{
    assert(left != NULL);
    assert(right != NULL);

    if (left->block_rows != right->block_rows || left->block_cols != right->block_cols) {
        return false;
    }

    const size_t element_count = block_matrix_element_count(left);
    if (element_count == 0u) {
        /* memcmp needs valid pointers even for length 0. */
        return true;
    }

    return memcmp(left->elements, right->elements,
                  element_count * sizeof(matrix_element)) == 0;
}
