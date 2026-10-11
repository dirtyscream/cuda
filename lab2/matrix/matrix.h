#ifndef LAB2_MATRIX_H
#define LAB2_MATRIX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"

/* Block-major, as in lab 1: block (r, c) starts at (r * block_cols + c) *
   BLOCK_ELEMENT_COUNT, and a block is 800 contiguous bytes, row by row. */
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

void block_matrix_fill_random(block_matrix *matrix, uint64_t seed);

void block_matrix_clear(block_matrix *matrix);

bool block_matrix_equals(const block_matrix *left, const block_matrix *right);

#endif
