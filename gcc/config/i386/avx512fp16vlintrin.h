/* Copyright (C) 2019 Free Software Foundation, Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   GCC is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   Under Section 7 of GPL version 3, you are granted additional
   permissions described in the GCC Runtime Library Exception, version
   3.1, as published by the Free Software Foundation.

   You should have received a copy of the GNU General Public License and
   a copy of the GCC Runtime Library Exception along with this program;
   see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
   <http://www.gnu.org/licenses/>.  */

#ifndef _IMMINTRIN_H_INCLUDED
#error "Never use <avx512fp16vlintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __AVX512FP16VLINTRIN_H_INCLUDED
#define __AVX512FP16VLINTRIN_H_INCLUDED

#if !defined(__AVX512VL__) || !defined(__AVX512FP16__)
#pragma GCC push_options
#pragma GCC target("avx512fp16,avx512vl")
#define __DISABLE_AVX512FP16VL__
#endif /* __AVX512FP16VL__ */

/* Intrinsics v[add,sub,mul,div]ph.  */
extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_add_ph (__m128h __A, __m128h __B)
{
  return (__m128h) ((__v8hf) __A + (__v8hf) __B);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_add_ph (__m256h __A, __m256h __B)
{
  return (__m256h) ((__v16hf) __A + (__v16hf) __B);
}

extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_mask_add_ph (__m128h __A, __mmask8 __B, __m128h __C, __m128h __D)
{
  return __builtin_ia32_addph128_mask (__C, __D, __A, __B);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_mask_add_ph (__m256h __A, __mmask16 __B, __m256h __C, __m256h __D)
{
  return __builtin_ia32_addph256_mask (__C, __D, __A, __B);
}

extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_maskz_add_ph (__mmask8 __A, __m128h __B, __m128h __C)
{
  return __builtin_ia32_addph128_mask (__B, __C, _mm_setzero_ph (),
				       __A);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_maskz_add_ph (__mmask16 __A, __m256h __B, __m256h __C)
{
  return __builtin_ia32_addph256_mask (__B, __C,
				       _mm256_setzero_ph (), __A);
}

extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_sub_ph (__m128h __A, __m128h __B)
{
  return (__m128h) ((__v8hf) __A - (__v8hf) __B);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_sub_ph (__m256h __A, __m256h __B)
{
  return (__m256h) ((__v16hf) __A - (__v16hf) __B);
}

extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_mask_sub_ph (__m128h __A, __mmask8 __B, __m128h __C, __m128h __D)
{
  return __builtin_ia32_subph128_mask (__C, __D, __A, __B);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_mask_sub_ph (__m256h __A, __mmask16 __B, __m256h __C, __m256h __D)
{
  return __builtin_ia32_subph256_mask (__C, __D, __A, __B);
}

extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_maskz_sub_ph (__mmask8 __A, __m128h __B, __m128h __C)
{
  return __builtin_ia32_subph128_mask (__B, __C, _mm_setzero_ph (),
				       __A);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_maskz_sub_ph (__mmask16 __A, __m256h __B, __m256h __C)
{
  return __builtin_ia32_subph256_mask (__B, __C,
				       _mm256_setzero_ph (), __A);
}

extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_mul_ph (__m128h __A, __m128h __B)
{
  return (__m128h) ((__v8hf) __A * (__v8hf) __B);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_mul_ph (__m256h __A, __m256h __B)
{
  return (__m256h) ((__v16hf) __A * (__v16hf) __B);
}

extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_mask_mul_ph (__m128h __A, __mmask8 __B, __m128h __C, __m128h __D)
{
  return __builtin_ia32_mulph128_mask (__C, __D, __A, __B);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_mask_mul_ph (__m256h __A, __mmask16 __B, __m256h __C, __m256h __D)
{
  return __builtin_ia32_mulph256_mask (__C, __D, __A, __B);
}

extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_maskz_mul_ph (__mmask8 __A, __m128h __B, __m128h __C)
{
  return __builtin_ia32_mulph128_mask (__B, __C, _mm_setzero_ph (),
				       __A);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_maskz_mul_ph (__mmask16 __A, __m256h __B, __m256h __C)
{
  return __builtin_ia32_mulph256_mask (__B, __C,
				       _mm256_setzero_ph (), __A);
}

extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_div_ph (__m128h __A, __m128h __B)
{
  return (__m128h) ((__v8hf) __A / (__v8hf) __B);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_div_ph (__m256h __A, __m256h __B)
{
  return (__m256h) ((__v16hf) __A / (__v16hf) __B);
}

extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_mask_div_ph (__m128h __A, __mmask8 __B, __m128h __C, __m128h __D)
{
  return __builtin_ia32_divph128_mask (__C, __D, __A, __B);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_mask_div_ph (__m256h __A, __mmask16 __B, __m256h __C, __m256h __D)
{
  return __builtin_ia32_divph256_mask (__C, __D, __A, __B);
}

extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_maskz_div_ph (__mmask8 __A, __m128h __B, __m128h __C)
{
  return __builtin_ia32_divph128_mask (__B, __C, _mm_setzero_ph (),
				       __A);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_maskz_div_ph (__mmask16 __A, __m256h __B, __m256h __C)
{
  return __builtin_ia32_divph256_mask (__B, __C,
				       _mm256_setzero_ph (), __A);
}

/* Intrinsics v[max,min]ph.  */
extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_max_ph (__m128h __A, __m128h __B)
{
  return __builtin_ia32_maxph128_mask (__A, __B,
				       _mm_setzero_ph (),
				       (__mmask8) -1);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_max_ph (__m256h __A, __m256h __B)
{
  return __builtin_ia32_maxph256_mask (__A, __B,
				       _mm256_setzero_ph (),
				       (__mmask16) -1);
}

extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_mask_max_ph (__m128h __A, __mmask8 __B, __m128h __C, __m128h __D)
{
  return __builtin_ia32_maxph128_mask (__C, __D, __A, __B);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_mask_max_ph (__m256h __A, __mmask16 __B, __m256h __C, __m256h __D)
{
  return __builtin_ia32_maxph256_mask (__C, __D, __A, __B);
}

extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_maskz_max_ph (__mmask8 __A, __m128h __B, __m128h __C)
{
  return __builtin_ia32_maxph128_mask (__B, __C, _mm_setzero_ph (),
				       __A);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_maskz_max_ph (__mmask16 __A, __m256h __B, __m256h __C)
{
  return __builtin_ia32_maxph256_mask (__B, __C,
				       _mm256_setzero_ph (), __A);
}

extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_min_ph (__m128h __A, __m128h __B)
{
  return __builtin_ia32_minph128_mask (__A, __B,
				       _mm_setzero_ph (),
				       (__mmask8) -1);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_min_ph (__m256h __A, __m256h __B)
{
  return __builtin_ia32_minph256_mask (__A, __B,
				       _mm256_setzero_ph (),
				       (__mmask16) -1);
}

extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_mask_min_ph (__m128h __A, __mmask8 __B, __m128h __C, __m128h __D)
{
  return __builtin_ia32_minph128_mask (__C, __D, __A, __B);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_mask_min_ph (__m256h __A, __mmask16 __B, __m256h __C, __m256h __D)
{
  return __builtin_ia32_minph256_mask (__C, __D, __A, __B);
}

extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_maskz_min_ph (__mmask8 __A, __m128h __B, __m128h __C)
{
  return __builtin_ia32_minph128_mask (__B, __C, _mm_setzero_ph (),
				       __A);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_maskz_min_ph (__mmask16 __A, __m256h __B, __m256h __C)
{
  return __builtin_ia32_minph256_mask (__B, __C,
				       _mm256_setzero_ph (), __A);
}

/* vcmpph */
#ifdef __OPTIMIZE
extern __inline __mmask8
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_cmp_ph_mask (__m128h __A, __m128h __B, const int __C)
{
  return (__mmask8) __builtin_ia32_cmpph128_mask (__A, __B, __C,
						  (__mmask8) -1);
}

extern __inline __mmask8
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_mask_cmp_ph_mask (__mmask8 __A, __m128h __B, __m128h __C,
		      const int __D)
{
  return (__mmask8) __builtin_ia32_cmpph128_mask (__B, __C, __D, __A);
}

extern __inline __mmask16
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_cmp_ph_mask (__m256h __A, __m256h __B, const int __C)
{
  return (__mmask16) __builtin_ia32_cmpph256_mask (__A, __B, __C,
						   (__mmask16) -1);
}

extern __inline __mmask16
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_mask_cmp_ph_mask (__mmask16 __A, __m256h __B, __m256h __C,
		      const int __D)
{
  return (__mmask16) __builtin_ia32_cmpph256_mask (__B, __C, __D,
						   __A);
}

#else
#define _mm_cmp_ph_mask(A, B, C)			\
  (__builtin_ia32_cmpph128_mask ((A), (B), (C), (-1)))

#define _mm_mask_cmp_ph_mask(A, B, C, D)		\
  (__builtin_ia32_cmpph128_mask ((B), (C), (D), (A)))

#define _mm256_cmp_ph_mask(A, B, C)			\
  (__builtin_ia32_cmpph256_mask ((A), (B), (C), (-1)))

#define _mm256_mask_cmp_ph_mask(A, B, C, D)		\
  (__builtin_ia32_cmpph256_mask ((B), (C), (D), (A)))

#endif /* __OPTIMIZE__ */

/* Intrinsics vsqrtph.  */
extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_sqrt_ph (__m128h __A)
{
  return __builtin_ia32_sqrtph128_mask (__A, _mm_setzero_ph (),
					(__mmask8) -1);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_sqrt_ph (__m256h __A)
{
  return __builtin_ia32_sqrtph256_mask (__A, _mm256_setzero_ph (),
					(__mmask16) -1);
}

extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_mask_sqrt_ph (__m128h __A, __mmask8 __B, __m128h __C)
{
  return __builtin_ia32_sqrtph128_mask (__C, __A, __B);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_mask_sqrt_ph (__m256h __A, __mmask16 __B, __m256h __C)
{
  return __builtin_ia32_sqrtph256_mask (__C, __A, __B);
}

extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_maskz_sqrt_ph (__mmask8 __A, __m128h __B)
{
  return __builtin_ia32_sqrtph128_mask (__B, _mm_setzero_ph (),
					__A);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_maskz_sqrt_ph (__mmask16 __A, __m256h __B)
{
  return __builtin_ia32_sqrtph256_mask (__B, _mm256_setzero_ph (),
					__A);
}

/* Intrinsics vrsqrtph.  */
extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_rsqrt_ph (__m128h __A)
{
  return __builtin_ia32_rsqrtph128_mask (__A, _mm_setzero_ph (),
					 (__mmask8) -1);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_rsqrt_ph (__m256h __A)
{
  return __builtin_ia32_rsqrtph256_mask (__A, _mm256_setzero_ph (),
					 (__mmask16) -1);
}

extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_mask_rsqrt_ph (__m128h __A, __mmask8 __B, __m128h __C)
{
  return __builtin_ia32_rsqrtph128_mask (__C, __A, __B);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_mask_rsqrt_ph (__m256h __A, __mmask16 __B, __m256h __C)
{
  return __builtin_ia32_rsqrtph256_mask (__C, __A, __B);
}

extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_maskz_rsqrt_ph (__mmask8 __A, __m128h __B)
{
  return __builtin_ia32_rsqrtph128_mask (__B, _mm_setzero_ph (), __A);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_maskz_rsqrt_ph (__mmask16 __A, __m256h __B)
{
  return __builtin_ia32_rsqrtph256_mask (__B, _mm256_setzero_ph (),
					 __A);
}

/* Intrinsics vrcpph.  */
extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_rcp_ph (__m128h __A)
{
  return __builtin_ia32_rcpph128_mask (__A, _mm_setzero_ph (),
				       (__mmask8) -1);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_rcp_ph (__m256h __A)
{
  return __builtin_ia32_rcpph256_mask (__A, _mm256_setzero_ph (),
				       (__mmask16) -1);
}

extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_mask_rcp_ph (__m128h __A, __mmask8 __B, __m128h __C)
{
  return __builtin_ia32_rcpph128_mask (__C, __A, __B);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_mask_rcp_ph (__m256h __A, __mmask16 __B, __m256h __C)
{
  return __builtin_ia32_rcpph256_mask (__C, __A, __B);
}

extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_maskz_rcp_ph (__mmask8 __A, __m128h __B)
{
  return __builtin_ia32_rcpph128_mask (__B, _mm_setzero_ph (), __A);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_maskz_rcp_ph (__mmask16 __A, __m256h __B)
{
  return __builtin_ia32_rcpph256_mask (__B, _mm256_setzero_ph (),
				       __A);
}

/* Intrinsics vscalefph.  */
extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_scalef_ph (__m128h __A, __m128h __B)
{
  return __builtin_ia32_scalefph128_mask (__A, __B,
					  _mm_setzero_ph (),
					  (__mmask8) -1);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_scalef_ph (__m256h __A, __m256h __B)
{
  return __builtin_ia32_scalefph256_mask (__A, __B,
					  _mm256_setzero_ph (),
					  (__mmask16) -1);
}

extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_mask_scalef_ph (__m128h __A, __mmask8 __B, __m128h __C, __m128h __D)
{
  return __builtin_ia32_scalefph128_mask (__C, __D, __A, __B);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_mask_scalef_ph (__m256h __A, __mmask16 __B, __m256h __C,
		       __m256h __D)
{
  return __builtin_ia32_scalefph256_mask (__C, __D, __A, __B);
}

extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_maskz_scalef_ph (__mmask8 __A, __m128h __B, __m128h __C)
{
  return __builtin_ia32_scalefph128_mask (__B, __C,
					  _mm_setzero_ph (), __A);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_maskz_scalef_ph (__mmask16 __A, __m256h __B, __m256h __C)
{
  return __builtin_ia32_scalefph256_mask (__B, __C,
					  _mm256_setzero_ph (),
					  __A);
}

/* Intrinsics vreduceph.  */
#ifdef __OPTIMIZE__
extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_reduce_ph (__m128h __A, int __B)
{
  return __builtin_ia32_reduceph128_mask (__A, __B,
					  _mm_setzero_ph (),
					  (__mmask8) -1);
}

extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_mask_reduce_ph (__m128h __A, __mmask8 __B, __m128h __C, int __D)
{
  return __builtin_ia32_reduceph128_mask (__C, __D, __A, __B);
}

extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_maskz_reduce_ph (__mmask8 __A, __m128h __B, int __C)
{
  return __builtin_ia32_reduceph128_mask (__B, __C,
					  _mm_setzero_ph (), __A);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_reduce_ph (__m256h __A, int __B)
{
  return __builtin_ia32_reduceph256_mask (__A, __B,
					  _mm256_setzero_ph (),
					  (__mmask16) -1);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_mask_reduce_ph (__m256h __A, __mmask16 __B, __m256h __C, int __D)
{
  return __builtin_ia32_reduceph256_mask (__C, __D, __A, __B);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_maskz_reduce_ph (__mmask16 __A, __m256h __B, int __C)
{
  return __builtin_ia32_reduceph256_mask (__B, __C,
					  _mm256_setzero_ph (),
					  __A);
}

#else
#define _mm_reduce_ph(A, B)				\
  (__builtin_ia32_reduceph128_mask ((A), (B),		\
				    _mm_setzero_ph (),	\
				    ((__mmask8)-1)))

#define _mm_mask_reduce_ph(A,  B,  C, D)			\
  (__builtin_ia32_reduceph128_mask ((C), (D), (A), (B)))

#define _mm_maskz_reduce_ph(A,  B, C)					\
  (__builtin_ia32_reduceph128_mask ((B), (C), _mm_setzero_ph (), (A)))

#define _mm256_reduce_ph(A, B)					\
  (__builtin_ia32_reduceph256_mask ((A), (B),			\
				    _mm256_setzero_ph (),	\
				    ((__mmask16)-1)))

#define _mm256_mask_reduce_ph(A, B, C, D)			\
  (__builtin_ia32_reduceph256_mask ((C), (D), (A), (B)))

#define _mm256_maskz_reduce_ph(A, B, C)					\
  (__builtin_ia32_reduceph256_mask ((B), (C), _mm256_setzero_ph (), (A)))

#endif /* __OPTIMIZE__ */

/* Intrinsics vrndscaleph.  */
#ifdef __OPTIMIZE__
  extern __inline __m128h
  __attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
  _mm_roundscale_ph (__m128h __A, int __B)
  {
    return __builtin_ia32_rndscaleph128_mask (__A, __B,
					      _mm_setzero_ph (),
					      (__mmask8) -1);
  }

extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_mask_roundscale_ph (__m128h __A, __mmask8 __B, __m128h __C, int __D)
{
  return __builtin_ia32_rndscaleph128_mask (__C, __D, __A, __B);
}

extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_maskz_roundscale_ph (__mmask8 __A, __m128h __B, int __C)
{
  return __builtin_ia32_rndscaleph128_mask (__B, __C,
					    _mm_setzero_ph (), __A);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_roundscale_ph (__m256h __A, int __B)
{
  return __builtin_ia32_rndscaleph256_mask (__A, __B,
					    _mm256_setzero_ph (),
					    (__mmask16) -1);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_mask_roundscale_ph (__m256h __A, __mmask16 __B, __m256h __C,
			   int __D)
{
  return __builtin_ia32_rndscaleph256_mask (__C, __D, __A, __B);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_maskz_roundscale_ph (__mmask16 __A, __m256h __B, int __C)
{
  return __builtin_ia32_rndscaleph256_mask (__B, __C,
					    _mm256_setzero_ph (),
					    __A);
}

#else
#define _mm_roundscale_ph(A, B)						\
  (__builtin_ia32_rndscaleph128_mask ((A), (B), _mm_setzero_ph (),	\
				      ((__mmask8)-1)))

#define _mm_mask_roundscale_ph(A, B, C, D)			\
  (__builtin_ia32_rndscaleph128_mask ((C), (D), (A), (B)))

#define _mm_maskz_roundscale_ph(A, B, C)				\
  (__builtin_ia32_rndscaleph128_mask ((B), (C), _mm_setzero_ph (), (A)))

#define _mm256_roundscale_ph(A, B)				\
  (__builtin_ia32_rndscaleph256_mask ((A), (B),			\
				      _mm256_setzero_ph(),	\
				      ((__mmask16)-1)))

#define _mm256_mask_roundscale_ph(A, B, C, D)			\
  (__builtin_ia32_rndscaleph256_mask ((C), (D), (A), (B)))

#define _mm256_maskz_roundscale_ph(A, B, C)				\
  (__builtin_ia32_rndscaleph256_mask ((B), (C),				\
				      _mm256_setzero_ph (), (A)))

#endif /* __OPTIMIZE__ */

/* Intrinsics vfpclassph.  */
#ifdef __OPTIMIZE__
extern __inline __mmask8
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
  _mm_mask_fpclass_ph_mask (__mmask8 __U, __m128h __A, const int __imm)
{
  return (__mmask8) __builtin_ia32_fpclassph128_mask ((__v8hf) __A,
						      __imm, __U);
}

extern __inline __mmask8
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_fpclass_ph_mask (__m128h __A, const int __imm)
{
  return (__mmask8) __builtin_ia32_fpclassph128_mask ((__v8hf) __A,
						      __imm,
						      (__mmask8) -1);
}

extern __inline __mmask16
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_mask_fpclass_ph_mask (__mmask16 __U, __m256h __A, const int __imm)
{
  return (__mmask16) __builtin_ia32_fpclassph256_mask ((__v16hf) __A,
						       __imm, __U);
}

extern __inline __mmask16
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_fpclass_ph_mask (__m256h __A, const int __imm)
{
  return (__mmask16) __builtin_ia32_fpclassph256_mask ((__v16hf) __A,
						       __imm,
						       (__mmask16) -1);
}

#else
#define _mm_fpclass_ph_mask(X, C)                                       \
  ((__mmask8) __builtin_ia32_fpclassph128_mask ((__v8hf) (__m128h) (X),	\
						(int) (C),(__mmask8)-1))

#define _mm_mask_fpclass_ph_mask(u, X, C)                               \
  ((__mmask8) __builtin_ia32_fpclassph128_mask ((__v8hf) (__m128h) (X),	\
						(int) (C),(__mmask8)(u)))

#define _mm256_fpclass_ph_mask(X, C)                                    \
  ((__mmask16) __builtin_ia32_fpclassph256_mask ((__v16hf) (__m256h) (X), \
						 (int) (C),(__mmask16)-1))

#define _mm256_mask_fpclass_ph_mask(u, X, C)				\
  ((__mmask16) __builtin_ia32_fpclassph256_mask ((__v16hf) (__m256h) (X), \
						 (int) (C),(__mmask16)(u)))
#endif /* __OPTIMIZE__ */

/* Intrinsics vgetexpph, vgetexpsh.  */
extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_getexp_ph (__m256h __A)
{
  return (__m256h) __builtin_ia32_getexpph256_mask ((__v16hf) __A,
						    (__v16hf)
						    _mm256_setzero_ph (),
						    (__mmask16) -1);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_mask_getexp_ph (__m256h __W, __mmask16 __U, __m256h __A)
{
  return (__m256h) __builtin_ia32_getexpph256_mask ((__v16hf) __A,
						    (__v16hf) __W,
						    (__mmask16) __U);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_maskz_getexp_ph (__mmask16 __U, __m256h __A)
{
  return (__m256h) __builtin_ia32_getexpph256_mask ((__v16hf) __A,
						    (__v16hf)
						    _mm256_setzero_ph (),
						    (__mmask16) __U);
}

extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_getexp_ph (__m128h __A)
{
  return (__m128h) __builtin_ia32_getexpph128_mask ((__v8hf) __A,
						    (__v8hf)
						    _mm_setzero_ph (),
						    (__mmask8) -1);
}

extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_mask_getexp_ph (__m128h __W, __mmask8 __U, __m128h __A)
{
  return (__m128h) __builtin_ia32_getexpph128_mask ((__v8hf) __A,
						    (__v8hf) __W,
						    (__mmask8) __U);
}

extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_maskz_getexp_ph (__mmask8 __U, __m128h __A)
{
  return (__m128h) __builtin_ia32_getexpph128_mask ((__v8hf) __A,
						    (__v8hf)
						    _mm_setzero_ph (),
						    (__mmask8) __U);
}


/* Intrinsics vgetmantph, vgetmantsh.  */
#ifdef __OPTIMIZE__
extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_getmant_ph (__m256h __A, _MM_MANTISSA_NORM_ENUM __B,
		   _MM_MANTISSA_SIGN_ENUM __C)
{
  return (__m256h) __builtin_ia32_getmantph256_mask ((__v16hf) __A,
						     (__C << 2) | __B,
						     (__v16hf)
						     _mm256_setzero_ph (),
						     (__mmask16) -1);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_mask_getmant_ph (__m256h __W, __mmask16 __U, __m256h __A,
			_MM_MANTISSA_NORM_ENUM __B,
			_MM_MANTISSA_SIGN_ENUM __C)
{
  return (__m256h) __builtin_ia32_getmantph256_mask ((__v16hf) __A,
						     (__C << 2) | __B,
						     (__v16hf) __W,
						     (__mmask16) __U);
}

extern __inline __m256h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm256_maskz_getmant_ph (__mmask16 __U, __m256h __A,
			 _MM_MANTISSA_NORM_ENUM __B,
			 _MM_MANTISSA_SIGN_ENUM __C)
{
  return (__m256h) __builtin_ia32_getmantph256_mask ((__v16hf) __A,
						     (__C << 2) | __B,
						     (__v16hf)
						     _mm256_setzero_ph (),
						     (__mmask16) __U);
}

extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_getmant_ph (__m128h __A, _MM_MANTISSA_NORM_ENUM __B,
		_MM_MANTISSA_SIGN_ENUM __C)
{
  return (__m128h) __builtin_ia32_getmantph128_mask ((__v8hf) __A,
						     (__C << 2) | __B,
						     (__v8hf)
						     _mm_setzero_ph (),
						     (__mmask8) -1);
}

extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_mask_getmant_ph (__m128h __W, __mmask8 __U, __m128h __A,
		     _MM_MANTISSA_NORM_ENUM __B,
		     _MM_MANTISSA_SIGN_ENUM __C)
{
  return (__m128h) __builtin_ia32_getmantph128_mask ((__v8hf) __A,
						     (__C << 2) | __B,
						     (__v8hf) __W,
						     (__mmask8) __U);
}

extern __inline __m128h
__attribute__ ((__gnu_inline__, __always_inline__, __artificial__))
_mm_maskz_getmant_ph (__mmask8 __U, __m128h __A,
		      _MM_MANTISSA_NORM_ENUM __B,
		      _MM_MANTISSA_SIGN_ENUM __C)
{
  return (__m128h) __builtin_ia32_getmantph128_mask ((__v8hf) __A,
						     (__C << 2) | __B,
						     (__v8hf)
						     _mm_setzero_ph (),
						     (__mmask8) __U);
}

#else
#define _mm256_getmant_ph(X, B, C)					\
  ((__m256h) __builtin_ia32_getmantph256_mask ((__v16hf)(__m256h) (X),	\
					       (int)(((C)<<2) | (B)),	\
					       (__v16hf)(__m256h)_mm256_setzero_ph (), \
					       (__mmask16)-1))

#define _mm256_mask_getmant_ph(W, U, X, B, C)				\
  ((__m256h) __builtin_ia32_getmantph256_mask ((__v16hf)(__m256h) (X),	\
					       (int)(((C)<<2) | (B)),	\
					       (__v16hf)(__m256h)(W),	\
					       (__mmask16)(U)))

#define _mm256_maskz_getmant_ph(U, X, B, C)				\
  ((__m256h) __builtin_ia32_getmantph256_mask ((__v16hf)(__m256h) (X),	\
					       (int)(((C)<<2) | (B)),	\
					       (__v16hf)(__m256h)_mm256_setzero_ph (), \
					       (__mmask16)(U)))

#define _mm_getmant_ph(X, B, C)						\
  ((__m128h) __builtin_ia32_getmantph128_mask ((__v8hf)(__m128h) (X),	\
					       (int)(((C)<<2) | (B)),	\
					       (__v8hf)(__m128h)_mm_setzero_ph (), \
					       (__mmask8)-1))

#define _mm_mask_getmant_ph(W, U, X, B, C)				\
  ((__m128h) __builtin_ia32_getmantph128_mask ((__v8hf)(__m128h) (X),	\
					       (int)(((C)<<2) | (B)),	\
					       (__v8hf)(__m128h)(W),	\
					       (__mmask8)(U)))

#define _mm_maskz_getmant_ph(U, X, B, C)				\
  ((__m128h) __builtin_ia32_getmantph128_mask ((__v8hf)(__m128h) (X),	\
					       (int)(((C)<<2) | (B)),	\
					       (__v8hf)(__m128h)_mm_setzero_ph (), \
					       (__mmask8)(U)))

#endif /* __OPTIMIZE__ */

#ifdef __DISABLE_AVX512FP16VL__
#undef __DISABLE_AVX512FP16VL__
#pragma GCC pop_options
#endif /* __DISABLE_AVX512FP16VL__ */

#endif /* __AVX512FP16VLINTRIN_H_INCLUDED */
