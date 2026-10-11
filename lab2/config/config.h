#ifndef LAB2_CONFIG_H
#define LAB2_CONFIG_H

typedef double matrix_element;

#define BLOCK_ROWS 10
#define BLOCK_COLS 10
#define BLOCK_ELEMENT_COUNT (BLOCK_ROWS * BLOCK_COLS)

/* 384 blocks = 3840 elements per side: mod 64 = 0, as the statement requires. */
#define OUTER_BLOCKS 384
#define MATRIX_SIZE (OUTER_BLOCKS * BLOCK_COLS)

/* Apple M5 cache line; also the alignment of every allocation. */
#define CACHE_LINE_BYTES 128

/* Sub-matrix widths are multiples of 8 blocks: 640 bytes wide as a matrix and 6400
   contiguous bytes per block row -- multiples of the required 64 and of the line. */
#define REQUIRED_WIDTH_BYTES 64
#define WIDTH_GRANULE_BLOCKS 8

#define VECTOR_LANES (16 / (int)sizeof(matrix_element))
#define VECTORS_PER_BLOCK_ROW (BLOCK_COLS / VECTOR_LANES)

/* Rows of a result block that C2 keeps in registers: 5 x 5 = 25 accumulators, plus
   5 vectors of a B row and a broadcast -- 31 of NEON's 32 registers. */
#define REGISTER_ROWS 5

/* make FMA=1 switches every kernel to fused multiply-add at once. */
#if defined(LAB2_FUSED_MULTIPLY_ADD) && (defined(__aarch64__) || defined(__FMA__))
#define FUSED_MULTIPLY_ADD 1
#else
#define FUSED_MULTIPLY_ADD 0
#endif

_Static_assert(MATRIX_SIZE % 64 == 0, "the statement requires mod(size, 64) == 0");
_Static_assert((WIDTH_GRANULE_BLOCKS * BLOCK_COLS * sizeof(matrix_element)) % REQUIRED_WIDTH_BYTES == 0,
               "a width granule must be a multiple of 64 bytes wide");
_Static_assert((WIDTH_GRANULE_BLOCKS * BLOCK_ELEMENT_COUNT * sizeof(matrix_element)) % CACHE_LINE_BYTES == 0,
               "a width granule must cover whole cache lines in memory");
_Static_assert(OUTER_BLOCKS % WIDTH_GRANULE_BLOCKS == 0, "sub-matrix widths must divide the matrix");
_Static_assert(BLOCK_COLS % VECTOR_LANES == 0, "a block row must be a whole number of vectors");
_Static_assert(BLOCK_ROWS % REGISTER_ROWS == 0, "a block must split into whole register groups");

#endif
