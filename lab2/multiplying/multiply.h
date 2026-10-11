#ifndef LAB2_MULTIPLY_H
#define LAB2_MULTIPLY_H

#include "cache.h"
#include "matrix.h"

/* All three add A * B into result without clearing it. Each sums every element of C
   in the same order, k = 0, 1, ..., N-1, with the same operation, so the results
   are bit-identical. */

/* C0: lab 1's plain kernel without vectorisation, blocks in order i, j, k. */
void multiply_scalar(const block_matrix *left, const block_matrix *right, block_matrix *result);

/* C1: lab 1's SSE2 kernel unchanged, blocks in order i, j, k. */
void multiply_vectorized(const block_matrix *left, const block_matrix *right, block_matrix *result);

/* C2: the same arithmetic, optimised for the cache hierarchy. */
void multiply_blocked(const block_matrix *left, const block_matrix *right, block_matrix *result,
                      const cache_geometry *geometry);

#endif
