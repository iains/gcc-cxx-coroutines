/* { dg-final { check-function-bodies "**" "" "-DCHECK_ASM" } } */

#include "test_sve_acle.h"

/*
** qdmullt_lane_0_s64_tied1:
**	sqdmullt	z0\.d, z0\.s, z1\.s\[0\]
**	ret
*/
TEST_TYPE_CHANGE_Z (qdmullt_lane_0_s64_tied1, svint64_t, svint32_t,
		    z0_res = svqdmullt_lane_s64 (z0, z1, 0),
		    z0_res = svqdmullt_lane (z0, z1, 0))

/*
** qdmullt_lane_0_s64_tied2:
**	sqdmullt	z0\.d, z1\.s, z0\.s\[0\]
**	ret
*/
TEST_TYPE_CHANGE_Z (qdmullt_lane_0_s64_tied2, svint64_t, svint32_t,
		    z0_res = svqdmullt_lane_s64 (z1, z0, 0),
		    z0_res = svqdmullt_lane (z1, z0, 0))

/*
** qdmullt_lane_0_s64_untied:
**	sqdmullt	z0\.d, z1\.s, z2\.s\[0\]
**	ret
*/
TEST_TYPE_CHANGE_Z (qdmullt_lane_0_s64_untied, svint64_t, svint32_t,
		    z0_res = svqdmullt_lane_s64 (z1, z2, 0),
		    z0_res = svqdmullt_lane (z1, z2, 0))

/*
** qdmullt_lane_1_s64:
**	sqdmullt	z0\.d, z1\.s, z2\.s\[1\]
**	ret
*/
TEST_TYPE_CHANGE_Z (qdmullt_lane_1_s64, svint64_t, svint32_t,
		    z0_res = svqdmullt_lane_s64 (z1, z2, 1),
		    z0_res = svqdmullt_lane (z1, z2, 1))

/*
** qdmullt_lane_2_s64:
**	sqdmullt	z0\.d, z1\.s, z2\.s\[2\]
**	ret
*/
TEST_TYPE_CHANGE_Z (qdmullt_lane_2_s64, svint64_t, svint32_t,
		    z0_res = svqdmullt_lane_s64 (z1, z2, 2),
		    z0_res = svqdmullt_lane (z1, z2, 2))

/*
** qdmullt_lane_3_s64:
**	sqdmullt	z0\.d, z1\.s, z2\.s\[3\]
**	ret
*/
TEST_TYPE_CHANGE_Z (qdmullt_lane_3_s64, svint64_t, svint32_t,
		    z0_res = svqdmullt_lane_s64 (z1, z2, 3),
		    z0_res = svqdmullt_lane (z1, z2, 3))

/*
** qdmullt_lane_z15_s64:
**	str	d15, \[sp, -16\]!
**	sqdmullt	z0\.d, z1\.s, z15\.s\[1\]
**	ldr	d15, \[sp\], 16
**	ret
*/
TEST_DUAL_LANE_REG (qdmullt_lane_z15_s64, svint64_t, svint32_t, z15,
		    z0 = svqdmullt_lane_s64 (z1, z15, 1),
		    z0 = svqdmullt_lane (z1, z15, 1))

/*
** qdmullt_lane_z16_s64:
**	mov	(z[0-9]|z1[0-5])\.d, z16\.d
**	sqdmullt	z0\.d, z1\.s, \1\.s\[1\]
**	ret
*/
TEST_DUAL_LANE_REG (qdmullt_lane_z16_s64, svint64_t, svint32_t, z16,
		    z0 = svqdmullt_lane_s64 (z1, z16, 1),
		    z0 = svqdmullt_lane (z1, z16, 1))
