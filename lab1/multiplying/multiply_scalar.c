#include "multiply.h"

#include "multiply_plain_kernel.h"

/*
 * Same kernel source as multiply_auto.c on purpose. The Makefile compiles only this
 * translation unit with -fno-vectorize -fno-slp-vectorize, which is what makes it the
 * scalar baseline. Both flags are needed: the vector code here comes from the SLP
 * vectorizer, so -fno-vectorize alone leaves the object fully vectorized.
 */
void multiply_blocks_scalar(const block_matrix *left, const block_matrix *right, block_matrix *result)
{
    multiply_blocks_plain(left, right, result);
}
