#include "multiply.h"

#include "multiply_plain_kernel.h"

void multiply_blocks_auto(const block_matrix *left, const block_matrix *right, block_matrix *result)
{
    multiply_blocks_plain(left, right, result);
}
