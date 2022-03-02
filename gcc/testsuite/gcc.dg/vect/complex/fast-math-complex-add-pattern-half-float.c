/* { dg-do compile } */
/* { dg-require-effective-target vect_float } */
/* { dg-require-effective-target float16 } */
/* { dg-add-options arm_v8_3a_fp16_complex_neon } */

#define TYPE _Float16
#define N 200
#include "complex-add-pattern-template.c"

/* { dg-final { scan-tree-dump-times "stmt.*COMPLEX_ADD_ROT90" 4 "vect" { target { vect_complex_add_half } } } } */
/* { dg-final { scan-tree-dump-times "stmt.*COMPLEX_ADD_ROT270" 1 "vect" { target { vect_complex_add_half } } } } */

/* { dg-final { scan-tree-dump "Found COMPLEX_ADD_ROT270" "slp1" } } */
/* { dg-final { scan-tree-dump "Found COMPLEX_ADD_ROT270" "vect" } } */
/* { dg-final { scan-tree-dump "Found COMPLEX_ADD_ROT90" "slp1" } } */
/* { dg-final { scan-tree-dump "Found COMPLEX_ADD_ROT90" "vect" } } */
