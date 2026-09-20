#include "matrix.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define FILL_LCG_MULTIPLIER 1664525u
#define FILL_LCG_INCREMENT  1013904223u
#define FILL_LCG_SHIFT      16u

/*
 * Values stay in [-8, 8] so that every product and partial sum of the whole
 * multiplication is exact: one element of C sums 4000 products, so it never leaves
 * the 53-bit range where a double holds each integer exactly. That is what makes
 * the bit-exact comparison of the two results meaningful rather than lucky.
 */
#define FILL_VALUE_BOUND 8
#define FILL_VALUE_RANGE (2 * FILL_VALUE_BOUND + 1)

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

    /* A wrapped size would allocate a small buffer and turn every later write into
       a heap overflow, so each multiplication below is checked. */
    const size_t block_count = (size_t)block_rows * (size_t)block_cols;
    if (block_count / (size_t)block_rows != (size_t)block_cols) {
        return false;
    }
    if (block_count > SIZE_MAX / ((size_t)BLOCK_ELEMENT_COUNT * sizeof(matrix_element))) {
        return false;
    }

    const size_t element_count = block_count * (size_t)BLOCK_ELEMENT_COUNT;
    const size_t requested_bytes = element_count * sizeof(matrix_element);
    if (requested_bytes > SIZE_MAX - (size_t)MATRIX_ALIGNMENT + 1u) {
        return false;
    }

    /* aligned_alloc requires a size that is a multiple of the alignment. */
    const size_t allocated_bytes = round_up_to_multiple(requested_bytes, (size_t)MATRIX_ALIGNMENT);

    matrix_element *const elements = aligned_alloc((size_t)MATRIX_ALIGNMENT, allocated_bytes);
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

void block_matrix_fill_deterministic(block_matrix *matrix, unsigned seed)
{
    assert(matrix != NULL);
    assert(matrix->elements != NULL);

    const size_t element_count = block_matrix_element_count(matrix);
    unsigned state = seed;

    for (size_t index = 0; index < element_count; ++index) {
        state = state * FILL_LCG_MULTIPLIER + FILL_LCG_INCREMENT;

        /* The high bits of an LCG are the well-behaved ones. */
        const unsigned bucket = (state >> FILL_LCG_SHIFT) % (unsigned)FILL_VALUE_RANGE;
        matrix->elements[index] = (matrix_element)((int)bucket - FILL_VALUE_BOUND);
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
        /* memcmp wants valid pointers even for length zero; destroyed matrices are NULL. */
        return true;
    }

    /* Bit-exact on purpose: no epsilon is used or wanted here. */
    return memcmp(left->elements, right->elements,
                  element_count * sizeof(matrix_element)) == 0;
}
