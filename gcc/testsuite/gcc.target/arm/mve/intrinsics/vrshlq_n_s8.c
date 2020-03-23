/* { dg-require-effective-target arm_v8_1m_mve_ok } */
/* { dg-add-options arm_v8_1m_mve } */
/* { dg-additional-options "-O2" } */

#include "arm_mve.h"

int8x16_t
foo (int8x16_t a, int32_t b)
{
  return vrshlq_n_s8 (a, b);
}

/* { dg-final { scan-assembler "vrshl.s8"  }  } */

int8x16_t
foo1 (int8x16_t a, int32_t b)
{
  return vrshlq (a, b);
}

/* { dg-final { scan-assembler "vrshl.s8"  }  } */
