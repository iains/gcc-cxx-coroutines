/* Test compatibility of unprototyped and prototyped function types (C2x makes
   the case of types affected by default argument promotions compatible).  Test
   invalid-in-C2x usages.  */
/* { dg-do compile } */
/* { dg-options "-std=c2x -pedantic-errors" } */

void f1 (); /* { dg-message "previous declaration" } */
void f1 (int, ...); /* { dg-error "conflicting types" } */
/* { dg-message "ellipsis" "" { target *-*-* } .-1 } */

void f2 (int, ...); /* { dg-message "previous declaration" } */
void f2 (); /* { dg-error "conflicting types" } */
/* { dg-message "ellipsis" "" { target *-*-* } .-1 } */

void f3 (); /* { dg-message "previous declaration" } */
void f3 (char, ...); /* { dg-error "conflicting types" } */
/* { dg-message "ellipsis" "" { target *-*-* } .-1 } */

void f4 (char, ...); /* { dg-message "previous declaration" } */
void f4 (); /* { dg-error "conflicting types" } */
/* { dg-message "ellipsis" "" { target *-*-* } .-1 } */
