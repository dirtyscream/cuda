# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

University lab work for a parallel-programming course (lecturer Перцев Д.Ю.). Each lab lives in
its own top-level directory: `lab1/` (block matrices and vectorization) and `lab2/` (cache
optimisation on top of lab 1's block matrix and vectorized kernel). The work is graded by live
demonstration plus oral defence, so the build targets that *prove* something (disassembly counts,
optimizer remarks) matter as much as the program itself.

Course materials live in `useful_docs/` locally but are **gitignored** — they are third-party PDFs
(Intel AP-930, Drepper's *What Every Programmer Should Know About Memory*, a TUM GotoBLAS paper)
plus the lecturer's own slides, deliberately not redistributed in this public repo.

## Lab 2

**The matrix of matrices must be kept.** In `Лаб.2.pdf` the line «матрицу матриц можно
удалить» is struck through — text extraction loses the strike-through, so always render the
slide. A first version wrongly switched to flat matrices; it was redone.

`lab2/` keeps lab 1's block matrix (10 x 10 `double` blocks, block-major, 384 x 384 blocks so
3840 % 64 == 0). **Single-threaded by the user's decision**; the output shows only configuration,
cache geometry, verification and the results table. **Terminology:** "block" = the 10 x 10
matrix of lab 1; "sub-matrix" (подматрица) = a rectangle of blocks held in cache. The user
dislikes "tile" — never use it in code, output or docs. Code comments are kept short, only where
something non-obvious needs explaining (user's request).

- **C0** = lab 1's plain kernel built with `-fno-vectorize -fno-slp-vectorize` (that object
  only), blocks i-j-k — the scalar baseline, so C1/C0 is vectorisation alone.
- **C1** = lab 1's SSE2 kernel unchanged, blocks in the textbook order i-j-k. The statement asks
  for the vectorised version *only*; lab 1's own i-k-j order is already a cache optimisation (B
  becomes a forward stream). Never claim C1 is "lab 1's order" — it is lab 1's kernel.
- **C2** = the same i-j-k order, but inside a B sub-matrix held in cache. `floor(L/3 * 0.9)` is
  applied twice: L1d (39 321 B) sets the depth to 48 blocks (48 A blocks of a block row and a B
  column of 48 blocks, 38.4 KB each, in L1), the P-cluster L2 (5 033 164 B) sets the width to
  128 (B sub-matrix 48 x 128 = 4.9 MB, unchanged while all 384 block rows of A pass — the lab's
  "one block holds still" rule). 5 rows of a result block = 25 accumulators stay in registers for
  the whole depth (`REGISTER_ROWS`; 31 of 32 NEON registers, no spills). Nothing copied or
  transposed. C2 does **not** interchange loops — its gain is registers + sub-matrices.
Widths are multiples of 8 blocks (640 B as a matrix, 6400 B in memory). Shape barely matters
(48x48 .. 96x96 within 3%). `LAB2_DEPTH` / `LAB2_WIDTH` override the sizes; `=384` both gives
C2 without cache blocking (registers only).

Results (1 thread, 5 runs): C2/C1 = 1.56–1.71x, C2/C0 = 2.32–2.61x, C1/C0 = 1.35–1.59x; with
`make FMA=1` 2.14x / 2.83x. Parts measured separately: registers ~1.24–1.34x (FMA 1.41x),
sub-matrices ~1.19–1.32x (FMA 1.56x). Without FMA C2 hits the one-core arithmetic ceiling
(~33 GF/s), the same place lab 1's own program (SSE2, i-k-j, 31.2 GF/s on 4000^2) reaches by
sequential access — so against lab 1's program C2 is 0.98–1.07x, and 1.34–1.38x with FMA. C1 is
slow because i-j-k walks B down a block column: 307 KB jumps across pages = 384 interleaved
streams the prefetcher cannot follow (Drepper §6.3.1), and B is reread from memory per block row
(~45.7 GB vs ~2.4 GB for C2). Vectorisation is 1.35–1.59x here (1.82x in lab 1) because i-j-k
waits on B. The theory PDF (§8) says all of this openly — keep it that way.

**10x is not reachable** with the block matrix on one core: flat-matrix solutions that show
~11x (e.g. a classmate's, measured on this M5: C1 2.7 GF/s) get it from a C1 that reads B by
columns (16 of 128 bytes per line used); the block layout removed that in lab 1. With 10
threads C2/C0 was ~13x, but the user chose to drop threads. The program output must not mention
a 10x target (user's request).

**Input data are random reals in [-1, 1)** (`block_matrix_fill_random`). Integers in [-8, 8]
would make the memcmp meaningless for lab 2: their sums are exact in any order — summing k in
reverse gives 0 differing elements on integers, 95.7% on random data. Bit-exactness is structural
(same `accumulate_product` / scalar twin, same per-element k order) in both arithmetic modes.

`make probe` builds `cache_probe` (pointer chase, one pointer per 128 B line, random vs
sequential): L1 0.68 ns, L2 2.7–8 ns, memory ~86 ns random vs 6.6 ns sequential. It needs a
250 ms timed warm-up or the first rows read high while the core ramps its clock.

Read cache sizes from `hw.perflevel0.*`; the unsuffixed `hw.l1dcachesize` / `hw.l2cachesize`
report the efficiency cluster (64 KiB / 6 MiB). Apple silicon has no L3. Measure on AC power,
Low Power Mode off, not in the background (the program warns when clamped to E-cores).

## Commands

All of these run from `lab1/`. Lab 2 has `make`, `make run` (~50 s), `make probe`,
`make disasm`, `make FMA=1` and `make clean`; its exit code gates the two bit-exact checks.

```bash
make            # Release build, -O3. Must stay warning-free under -Wall -Wextra -Wpedantic -Wshadow -Wconversion
make run        # THE test: runs all three kernels, verifies, prints timings. ~35 s. Exit code gates every check
make clean
```

There is no separate test suite. `make run` is the test: it exits non-zero unless C1 == C2
bit-exactly, scalar == C1, and C2 is not slower than C1. Check it with `echo $?`.

Evidence targets, used during the defence and worth re-running after touching any kernel:

```bash
make report      # -Rpass remarks: which loops vectorized and by which vectorizer
make disasm      # packed-double vs scalar instruction counts per object
make prove       # compiles multiply_auto.c under 4 flag combinations; shows -fno-vectorize alone does nothing
make x86         # cross-compile to x86_64 (SSE2 path via <emmintrin.h> instead of sse2neon)
make x86-run     # run the x86 build under Rosetta
make x86-disasm  # x86 mulpd/addpd next to aarch64 fmul.2d/fadd.2d, same source
```

For a fast edit-test cycle, lower `OUTER_*` in `config/config.h` (200 gives ~4 s instead of ~35 s),
but restore 400 before any demonstration — the lab requires C1 to take "several seconds".

## Lab 1 architecture

### The three kernels

`multiply.h` declares three functions that all compute the same block product:

- `multiply_blocks_scalar` — the baseline, vectorization disabled
- `multiply_blocks_auto` — C1, compiler auto-vectorizes
- `multiply_blocks_sse2` — C2, hand-written SSE2 intrinsics

The scalar and auto versions share **one** kernel: both `.c` files are three lines that include
`multiply_plain_kernel.h` and call `multiply_blocks_plain`. They differ only in compiler flags —
the Makefile adds `NOVEC_CFLAGS` to `multiply_scalar.o` alone. Keep it that way: the whole point is
that the measured speedup is the effect of vectorization and nothing else.

### Block-major layout

`block_matrix` (in `matrix.h`) stores whole blocks back to back, elements row-major inside a block.
Block `(r, c)` starts at `(r * block_cols + c) * BLOCK_ELEMENT_COUNT`. This makes each 800-byte
block contiguous, so it fits in L1 and every cache line is fully used. Do not "simplify" this into a
flat row-major matrix — the layout is the performance story of the whole lab.

### The bit-exactness contract — the single most important invariant

C1 and C2 are compared with `memcmp`, not an epsilon, and the lab requires exact equality. Three
things hold it up, spread across three files:

1. **Identical accumulation order.** All kernels iterate blocks i-k-j and, inside a block,
   `inner_row` → `inner_k` → `inner_col`. Floating-point addition is not associative, so reordering
   any loop in one kernel breaks the comparison. This is documented in `multiply.h` and is a
   correctness constraint, not a style preference — never reorder loops to chase speed.
2. **No FMA (lab 1 only).** Fused multiply-add rounds once instead of twice and changes the low
   bits. Both lab 1 kernels carry `#pragma STDC FP_CONTRACT OFF`, and the Makefile passes
   `-ffp-contract=off` and omits `-ffast-math`. In lab 1 `make disasm` must show 0 FMA. Lab 2
   instead uses FMA consistently in every kernel — see the Lab 2 section.
3. **Exact input data (lab 1 only — lab 2 deliberately uses random reals).** `block_matrix_fill_deterministic` generates integers in [-8, 8], so every
   product and partial sum stays well inside the 53-bit exact-integer range of a double and no
   rounding happens at all. This is what makes the memcmp meaningful rather than lucky.

### SSE2 on ARM64

The target machine is Apple silicon, where SSE2 does not exist. `multiply_sse2.c` is written in
literal SSE2 intrinsics and picks its header by architecture: `<emmintrin.h>` on x86, the vendored
`vendor/sse2neon.h` on aarch64, `#error` otherwise. sse2neon is a compile-time macro layer, not a
runtime shim, and it implements `__m128d` **only on aarch64** — the guard is load-bearing.

The kernel uses the broadcast-and-accumulate scheme from Intel AP-930: one element of A is splatted
across the vector and multiplied by a forward, stride-1 row of B. Transposing is forbidden by the
lab statement, and this scheme makes it unnecessary rather than merely avoided.

### Timing

`clock()` and `time()` are forbidden by the lab. `timer.c` provides `clock_gettime(CLOCK_MONOTONIC_RAW)`
as the primary measurement plus a raw cycle counter (`__rdtsc` on x86, `cntvct_el0` on aarch64).
`timer_cycle_frequency_hz` returns 0 on x86 because the TSC rate is not architecturally readable —
do not invent a value. On Apple silicon the counter is a fixed 1 GHz timebase, not core cycles, so
`main.c` labels it "counter ticks" and never calls it cycles.

`main.c` times each kernel `MEASUREMENT_REPEAT_COUNT` times and keeps the fastest run. With a single
run the timing verdict flipped on roughly one run in four.

### Sizes

`config/config.h` is the only place sizes live: element type, block size, outer dimensions, lane
count, alignment. No magic numbers elsewhere. `MATRIX_ALIGNMENT` is 128 because that is the cache
line size of the target machine (not 64 as on x86).

`NDEBUG` is never defined, so the asserts in the block accessors are live inside the timed loops.
This is intentional — the cost is identical across all three kernels and negligible next to 100
flops per block product.

## Landmines

These cost real debugging time already. Do not re-discover them.

- **`-fno-vectorize` alone does nothing here.** The 10-iteration inner loop is fully unrolled before
  the loop vectorizer runs, so the vector code comes from the *SLP* vectorizer. Both
  `-fno-vectorize` and `-fno-slp-vectorize` are required. `make prove` demonstrates this.
- **`restrict` must sit on function parameters.** The inner row update lives in its own
  `accumulate_scaled_row` because that is where the compiler can use the aliasing promise. With
  `restrict` only on the outer block pointers the loop stays scalar and the kernel runs ~7x slower.
- **The AArch64 assembler treats `;` as a comment introducer.** `__asm__("isb; mrs %0, cntvct_el0")`
  silently assembles to `isb` alone and the counter read vanishes with no diagnostic. Separate the
  instructions with `\n\t`.
- **Apple's objdump prints `fmul.2d v9, v9, v31[0]`,** not `fmul v9.2d, ...`. Grepping for the
  latter yields a false zero and makes a vectorized object look scalar.
- **sse2neon has no FMA.** `_mm_fmadd_pd` is FMA3, not SSE2, and is absent from the vendored
  header; use `vfmaq_f64` on aarch64.
- **A peak-FLOPS microbenchmark with loop-invariant operands lies.** Clang hoists `a * b` out of
  the loop and only the adds remain. Make the multiplicand depend on the accumulator, or measure
  a real kernel instead.
- **zsh does not word-split `$var`.** `for p in "1 2 3"; do ./prog $p; done` passes one argument;
  use `${=p}`.

## Documentation

`lab1/docs/` holds `lab1-basics.pdf` (short, from-scratch introduction) and `lab1-theory.pdf`
(full write-up); `lab2/docs/` holds `lab2-theory.pdf`. Each is built from the `.html` source
beside it.

This machine has no poppler, pandoc, wkhtmltopdf or LaTeX. Regenerate a PDF with headless Chrome
(the `file://` source must be absolute, so keep both paths absolute):

```bash
D=/Users/danil/cuda/lab1/docs
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless --disable-gpu \
  --no-pdf-header-footer --run-all-compositor-stages-before-draw --virtual-time-budget=6000 \
  --print-to-pdf="$D/lab1-theory.pdf" "file://$D/theory.html"
```

Print CSS gotchas already solved in those files: `page-break-inside: avoid` on `table` leaves
half-empty pages (use `thead { display: table-header-group }` plus `tr { page-break-inside: avoid }`),
and `p { orphans: 3; widows: 3 }` prevents single lines stranded on their own page. Always render
the result to PNG and look at it — layout bugs do not show up any other way.

To read a PDF or inspect a rendered page, use PyMuPDF in a scratch venv (`pip install pymupdf`);
the built-in PDF reader needs `pdftoppm`, which is not installed.

If measurements or file paths change, update both HTML sources and re-render — the PDFs quote
concrete numbers from real runs.
