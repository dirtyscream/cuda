#ifndef LAB1_CONFIG_H
#define LAB1_CONFIG_H

typedef double matrix_element;

#define BLOCK_ROWS 10
#define BLOCK_COLS 10
#define BLOCK_ELEMENT_COUNT (BLOCK_ROWS * BLOCK_COLS)

/* Sized so the auto-vectorized run takes a few seconds, as the lab requires. */
#define OUTER_ROWS_A 400
#define OUTER_COLS_A 400
#define OUTER_COLS_B 400

#define VECTOR_LANES (16 / (int)sizeof(matrix_element))

/* Cache line size of the target machine. */
#define MATRIX_ALIGNMENT 128

#endif
