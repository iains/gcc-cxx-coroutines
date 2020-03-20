/* { dg-do compile  } */
/* { dg-require-effective-target arm_v8_1m_mve_ok } */
/* { dg-add-options arm_v8_1m_mve } */
/* { dg-additional-options "-O2" } */

#include "arm_mve.h"

uint32x4_t
foo (uint32_t * a, uint32_t b, mve_pred16_t p)
{
  return viwdupq_x_wb_u32 (a, b, 2, p);
}

/* { dg-final { scan-assembler "vpst" } } */
/* { dg-final { scan-assembler "viwdupt.u32"  }  } */

uint32x4_t
foo1 (uint32_t * a, uint32_t b, mve_pred16_t p)
{
  return viwdupq_x_u32 (a, b, 2, p);
}

/* { dg-final { scan-assembler "vpst" } } */
/* { dg-final { scan-assembler "viwdupt.u32"  }  } */
