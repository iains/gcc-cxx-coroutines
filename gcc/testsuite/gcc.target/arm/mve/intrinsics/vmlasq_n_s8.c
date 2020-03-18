/* { dg-do compile  } */
/* { dg-require-effective-target arm_v8_1m_mve_ok } */
/* { dg-add-options arm_v8_1m_mve } */
/* { dg-additional-options "-O2" } */

#include "arm_mve.h"

int8x16_t
foo (int8x16_t a, int8x16_t b, int8_t c)
{
  return vmlasq_n_s8 (a, b, c);
}

/* { dg-final { scan-assembler "vmlas.s8"  }  } */

int8x16_t
foo1 (int8x16_t a, int8x16_t b, int8_t c)
{
  return vmlasq (a, b, c);
}

/* { dg-final { scan-assembler "vmlas.s8"  }  } */
