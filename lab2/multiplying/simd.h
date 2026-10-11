#ifndef LAB2_SIMD_H
#define LAB2_SIMD_H

#include "config.h"

/* The compiler may not fuse a multiply and an add on its own: that would round
   differently and break the bit-exact match between kernels. */
#pragma STDC FP_CONTRACT OFF

/* SSE2 intrinsics: native on x86, translated to NEON by sse2neon on aarch64, which
   provides __m128d only there. */
#if defined(__x86_64__) || defined(__i386__)
#include <emmintrin.h>
#if FUSED_MULTIPLY_ADD
#include <immintrin.h>
#endif
#elif defined(__aarch64__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#include "sse2neon.h"
#pragma clang diagnostic pop
#else
#error "lab 2 requires SSE2 (x86) or sse2neon on aarch64"
#endif

/* accumulator + left * right: the one arithmetic step of both vector kernels.
   sse2neon has no FMA, so the fused variant uses each target's own instruction. */
static inline __m128d accumulate_product(__m128d accumulator, __m128d left, __m128d right)
{
#if FUSED_MULTIPLY_ADD && defined(__aarch64__)
    return vfmaq_f64(accumulator, left, right);
#elif FUSED_MULTIPLY_ADD
    return _mm_fmadd_pd(left, right, accumulator);
#else
    return _mm_add_pd(accumulator, _mm_mul_pd(left, right));
#endif
}

#endif
