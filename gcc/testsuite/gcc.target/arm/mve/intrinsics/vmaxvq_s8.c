/* { dg-require-effective-target arm_v8_1m_mve_ok } */
/* { dg-add-options arm_v8_1m_mve } */
/* { dg-additional-options "-O2" } */

#include "arm_mve.h"

int8_t
foo (int8_t a, int8x16_t b)
{
  return vmaxvq_s8 (a, b);
}

/* { dg-final { scan-assembler "vmaxv.s8"  }  } */

int8_t
foo1 (int8_t a, int8x16_t b)
{
  return vmaxvq (a, b);
}

/* { dg-final { scan-assembler "vmaxv.s8"  }  } */
