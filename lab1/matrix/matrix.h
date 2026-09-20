#ifndef LAB1_MATRIX_H
#define LAB1_MATRIX_H

#include <stdbool.h>
#include <stddef.h>

#include "config.h"

/*
 * Block-major storage: whole blocks lie back to back, elements inside a block lie
 * row by row. Block (r, c) starts at (r * block_cols + c) * BLOCK_ELEMENT_COUNT.
 * A block is therefore contiguous, which is what keeps it inside L1 and lets the
 * kernels walk memory forward without transposing anything.
 */
typedef struct {
    matrix_element *elements;
    int block_rows;
    int block_cols;
} block_matrix;

bool block_matrix_create(block_matrix *matrix, int block_rows, int block_cols);

void block_matrix_destroy(block_matrix *matrix);

size_t block_matrix_element_count(const block_matrix *matrix);

matrix_element *block_matrix_block(block_matrix *matrix, int block_row, int block_col);

const matrix_element *block_matrix_block_const(const block_matrix *matrix, int block_row, int block_col);

void block_matrix_fill_deterministic(block_matrix *matrix, unsigned seed);

void block_matrix_clear(block_matrix *matrix);

bool block_matrix_equals(const block_matrix *left, const block_matrix *right);

#endif
