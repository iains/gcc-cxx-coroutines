/* { dg-final { check-function-bodies "**" "" "-DCHECK_ASM" } } */
/* { dg-additional-options "-march=armv8.6-a+sve+f64mm" } */
/* { dg-require-effective-target aarch64_asm_f64mm_ok }  */

#include "test_sve_acle.h"

/*
** ld1ro_f64_base:
**	ld1rod	z0\.d, p0/z, \[x0\]
**	ret
*/
TEST_LOAD (ld1ro_f64_base, svfloat64_t, float64_t,
	   z0 = svld1ro_f64 (p0, x0),
	   z0 = svld1ro (p0, x0))

/*
** ld1ro_f64_index:
**	ld1rod	z0\.d, p0/z, \[x0, x1, lsl 3\]
**	ret
*/
TEST_LOAD (ld1ro_f64_index, svfloat64_t, float64_t,
	   z0 = svld1ro_f64 (p0, x0 + x1),
	   z0 = svld1ro (p0, x0 + x1))

/*
** ld1ro_f64_1:
**	add	(x[0-9]+), x0, #?8
**	ld1rod	z0\.d, p0/z, \[\1\]
**	ret
*/
TEST_LOAD (ld1ro_f64_1, svfloat64_t, float64_t,
	   z0 = svld1ro_f64 (p0, x0 + 1),
	   z0 = svld1ro (p0, x0 + 1))

/*
** ld1ro_f64_2:
**	add	(x[0-9]+), x0, #?16
**	ld1rod	z0\.d, p0/z, \[\1\]
**	ret
*/
TEST_LOAD (ld1ro_f64_2, svfloat64_t, float64_t,
	   z0 = svld1ro_f64 (p0, x0 + 2),
	   z0 = svld1ro (p0, x0 + 2))

/*
** ld1ro_f64_32:
**	add	(x[0-9]+), x0, #?256
**	ld1rod	z0\.d, p0/z, \[\1\]
**	ret
*/
TEST_LOAD (ld1ro_f64_32, svfloat64_t, float64_t,
	   z0 = svld1ro_f64 (p0, x0 + 32),
	   z0 = svld1ro (p0, x0 + 32))

/*
** ld1ro_f64_m1:
**	sub	(x[0-9]+), x0, #?8
**	ld1rod	z0\.d, p0/z, \[\1\]
**	ret
*/
TEST_LOAD (ld1ro_f64_m1, svfloat64_t, float64_t,
	   z0 = svld1ro_f64 (p0, x0 - 1),
	   z0 = svld1ro (p0, x0 - 1))

/*
** ld1ro_f64_m2:
**	sub	(x[0-9]+), x0, #?16
**	ld1rod	z0\.d, p0/z, \[\1\]
**	ret
*/
TEST_LOAD (ld1ro_f64_m2, svfloat64_t, float64_t,
	   z0 = svld1ro_f64 (p0, x0 - 2),
	   z0 = svld1ro (p0, x0 - 2))

/*
** ld1ro_f64_m36:
**	sub	(x[0-9]+), x0, #?288
**	ld1rod	z0\.d, p0/z, \[\1\]
**	ret
*/
TEST_LOAD (ld1ro_f64_m36, svfloat64_t, float64_t,
	   z0 = svld1ro_f64 (p0, x0 - 36),
	   z0 = svld1ro (p0, x0 - 36))

/*
** ld1ro_f64_4:
**	ld1rod	z0\.d, p0/z, \[x0, #?32\]
**	ret
*/
TEST_LOAD (ld1ro_f64_4, svfloat64_t, float64_t,
	   z0 = svld1ro_f64 (p0, x0 + 4),
	   z0 = svld1ro (p0, x0 + 4))

/*
** ld1ro_f64_28:
**	ld1rod	z0\.d, p0/z, \[x0, #?224\]
**	ret
*/
TEST_LOAD (ld1ro_f64_28, svfloat64_t, float64_t,
	   z0 = svld1ro_f64 (p0, x0 + 28),
	   z0 = svld1ro (p0, x0 + 28))

/*
** ld1ro_f64_m4:
**	ld1rod	z0\.d, p0/z, \[x0, #?-32\]
**	ret
*/
TEST_LOAD (ld1ro_f64_m4, svfloat64_t, float64_t,
	   z0 = svld1ro_f64 (p0, x0 - 4),
	   z0 = svld1ro (p0, x0 - 4))

/*
** ld1ro_f64_m32:
**	ld1rod	z0\.d, p0/z, \[x0, #?-256\]
**	ret
*/
TEST_LOAD (ld1ro_f64_m32, svfloat64_t, float64_t,
	   z0 = svld1ro_f64 (p0, x0 - 32),
	   z0 = svld1ro (p0, x0 - 32))

