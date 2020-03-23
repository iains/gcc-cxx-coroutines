/* { dg-require-effective-target arm_v8_1m_mve_ok } */
/* { dg-add-options arm_v8_1m_mve } */
/* { dg-additional-options "-O2" } */

#include "arm_mve.h"

int32x4_t
foo (int16x8_t a, int16x8_t b, mve_pred16_t p)
{
  return vmullbq_int_x_s16 (a, b, p);
}

/* { dg-final { scan-assembler "vpst" } } */
/* { dg-final { scan-assembler "vmullbt.s16"  }  } */

int32x4_t
foo1 (int16x8_t a, int16x8_t b, mve_pred16_t p)
{
  return vmullbq_int_x (a, b, p);
}

/* { dg-final { scan-assembler "vpst" } } */
