/* { dg-do compile } */
/* { dg-options "-mavx512f -mno-avx512er -O2" } */

#include "pr82941-1.c"

/* { dg-final { scan-assembler-times "vzeroupper" 1 { target ia32 } } } */
/* { dg-final { scan-assembler-not "vzeroupper" { target { ! ia32 } } } } */
