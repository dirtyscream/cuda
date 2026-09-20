#ifndef LAB1_MULTIPLY_H
#define LAB1_MULTIPLY_H

#include "matrix.h"

/*
 * All three accumulate into result without clearing it first; clearing is the
 * caller's job. They accumulate in the same order on purpose, so their results are
 * bit-identical and can be compared with memcmp. That order is a contract: changing
 * it in one kernel breaks the comparison the lab is graded on.
 */

void multiply_blocks_auto(const block_matrix *left, const block_matrix *right, block_matrix *result);

void multiply_blocks_scalar(const block_matrix *left, const block_matrix *right, block_matrix *result);

void multiply_blocks_sse2(const block_matrix *left, const block_matrix *right, block_matrix *result);

#endif
