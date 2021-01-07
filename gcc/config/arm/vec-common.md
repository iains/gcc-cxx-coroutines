;; Machine Description for shared bits common to IWMMXT and Neon.
;; Copyright (C) 2006-2021 Free Software Foundation, Inc.
;; Written by CodeSourcery.
;;
;; This file is part of GCC.
;;
;; GCC is free software; you can redistribute it and/or modify it
;; under the terms of the GNU General Public License as published by
;; the Free Software Foundation; either version 3, or (at your option)
;; any later version.
;;
;; GCC is distributed in the hope that it will be useful, but
;; WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;; General Public License for more details.
;;
;; You should have received a copy of the GNU General Public License
;; along with GCC; see the file COPYING3.  If not see
;; <http://www.gnu.org/licenses/>.

;; Vector Moves

(define_expand "mov<mode>"
  [(set (match_operand:VNIM1 0 "nonimmediate_operand")
	(match_operand:VNIM1 1 "general_operand"))]
  "TARGET_NEON
   || (TARGET_REALLY_IWMMXT && VALID_IWMMXT_REG_MODE (<MODE>mode))
   || (TARGET_HAVE_MVE && VALID_MVE_SI_MODE (<MODE>mode))
   || (TARGET_HAVE_MVE_FLOAT && VALID_MVE_SF_MODE (<MODE>mode))"
   {
  gcc_checking_assert (aligned_operand (operands[0], <MODE>mode));
  gcc_checking_assert (aligned_operand (operands[1], <MODE>mode));
  if (can_create_pseudo_p ())
    {
      if (!REG_P (operands[0]))
	operands[1] = force_reg (<MODE>mode, operands[1]);
      else if ((TARGET_NEON || TARGET_HAVE_MVE || TARGET_HAVE_MVE_FLOAT)
	       && (CONSTANT_P (operands[1])))
	{
	  operands[1] = neon_make_constant (operands[1]);
	  gcc_assert (operands[1] != NULL_RTX);
	}
    }
})

(define_expand "mov<mode>"
  [(set (match_operand:VNINOTM1 0 "nonimmediate_operand")
	(match_operand:VNINOTM1 1 "general_operand"))]
  "TARGET_NEON
   || (TARGET_REALLY_IWMMXT && VALID_IWMMXT_REG_MODE (<MODE>mode))"
{
  gcc_checking_assert (aligned_operand (operands[0], <MODE>mode));
  gcc_checking_assert (aligned_operand (operands[1], <MODE>mode));
  if (can_create_pseudo_p ())
    {
      if (!REG_P (operands[0]))
	operands[1] = force_reg (<MODE>mode, operands[1]);
      else if (TARGET_NEON && CONSTANT_P (operands[1]))
	{
	  operands[1] = neon_make_constant (operands[1]);
	  gcc_assert (operands[1] != NULL_RTX);
	}
    }
})

(define_expand "movv8hf"
  [(set (match_operand:V8HF 0 "s_register_operand")
       (match_operand:V8HF 1 "s_register_operand"))]
   "TARGET_NEON || TARGET_HAVE_MVE_FLOAT"
{
  gcc_checking_assert (aligned_operand (operands[0], E_V8HFmode));
  gcc_checking_assert (aligned_operand (operands[1], E_V8HFmode));
   if (can_create_pseudo_p ())
     {
       if (!REG_P (operands[0]))
	 operands[1] = force_reg (E_V8HFmode, operands[1]);
     }
})

;; Vector arithmetic.  Expanders are blank, then unnamed insns implement
;; patterns separately for Neon, IWMMXT and MVE.

(define_expand "add<mode>3"
  [(set (match_operand:VDQ 0 "s_register_operand")
	(plus:VDQ (match_operand:VDQ 1 "s_register_operand")
		  (match_operand:VDQ 2 "s_register_operand")))]
  "ARM_HAVE_<MODE>_ARITH"
)

(define_expand "sub<mode>3"
  [(set (match_operand:VDQ 0 "s_register_operand")
	(minus:VDQ (match_operand:VDQ 1 "s_register_operand")
		   (match_operand:VDQ 2 "s_register_operand")))]
  "ARM_HAVE_<MODE>_ARITH"
)

(define_expand "mul<mode>3"
  [(set (match_operand:VDQWH 0 "s_register_operand")
	(mult:VDQWH (match_operand:VDQWH 1 "s_register_operand")
		    (match_operand:VDQWH 2 "s_register_operand")))]
  "ARM_HAVE_<MODE>_ARITH"
)

(define_expand "smin<mode>3"
  [(set (match_operand:VALLW 0 "s_register_operand")
	(smin:VALLW (match_operand:VALLW 1 "s_register_operand")
		    (match_operand:VALLW 2 "s_register_operand")))]
   "ARM_HAVE_<MODE>_ARITH"
)

(define_expand "umin<mode>3"
  [(set (match_operand:VINTW 0 "s_register_operand")
	(umin:VINTW (match_operand:VINTW 1 "s_register_operand")
		    (match_operand:VINTW 2 "s_register_operand")))]
   "ARM_HAVE_<MODE>_ARITH"
)

(define_expand "smax<mode>3"
  [(set (match_operand:VALLW 0 "s_register_operand")
	(smax:VALLW (match_operand:VALLW 1 "s_register_operand")
		    (match_operand:VALLW 2 "s_register_operand")))]
   "ARM_HAVE_<MODE>_ARITH"
)

(define_expand "umax<mode>3"
  [(set (match_operand:VINTW 0 "s_register_operand")
	(umax:VINTW (match_operand:VINTW 1 "s_register_operand")
		    (match_operand:VINTW 2 "s_register_operand")))]
   "ARM_HAVE_<MODE>_ARITH"
)

(define_expand "vec_perm<mode>"
  [(match_operand:VE 0 "s_register_operand")
   (match_operand:VE 1 "s_register_operand")
   (match_operand:VE 2 "s_register_operand")
   (match_operand:VE 3 "s_register_operand")]
  "TARGET_NEON && !BYTES_BIG_ENDIAN"
{
  arm_expand_vec_perm (operands[0], operands[1], operands[2], operands[3]);
  DONE;
})

(define_expand "vec_extract<mode><V_elem_l>"
 [(match_operand:<V_elem> 0 "nonimmediate_operand")
  (match_operand:VQX_NOBF 1 "s_register_operand")
  (match_operand:SI 2 "immediate_operand")]
 "TARGET_NEON || TARGET_HAVE_MVE"
{
  if (TARGET_NEON)
    emit_insn (gen_neon_vec_extract<mode><V_elem_l> (operands[0], operands[1],
						     operands[2]));
  else if (TARGET_HAVE_MVE)
    emit_insn (gen_mve_vec_extract<mode><V_elem_l> (operands[0], operands[1],
						     operands[2]));
  else
    gcc_unreachable ();
  DONE;
})

(define_expand "vec_set<mode>"
  [(match_operand:VQX_NOBF 0 "s_register_operand" "")
   (match_operand:<V_elem> 1 "s_register_operand" "")
   (match_operand:SI 2 "immediate_operand" "")]
  "TARGET_NEON || TARGET_HAVE_MVE"
{
  HOST_WIDE_INT elem = HOST_WIDE_INT_1 << INTVAL (operands[2]);
  if (TARGET_NEON)
    emit_insn (gen_vec_set<mode>_internal (operands[0], operands[1],
					   GEN_INT (elem), operands[0]));
  else
    emit_insn (gen_mve_vec_set<mode>_internal (operands[0], operands[1],
					       GEN_INT (elem), operands[0]));
  DONE;
})

(define_expand "and<mode>3"
  [(set (match_operand:VDQ 0 "s_register_operand" "")
	(and:VDQ (match_operand:VDQ 1 "s_register_operand" "")
		 (match_operand:VDQ 2 "neon_inv_logic_op2" "")))]
  "ARM_HAVE_<MODE>_ARITH"
)

(define_expand "ior<mode>3"
  [(set (match_operand:VDQ 0 "s_register_operand" "")
	(ior:VDQ (match_operand:VDQ 1 "s_register_operand" "")
		 (match_operand:VDQ 2 "neon_logic_op2" "")))]
  "ARM_HAVE_<MODE>_ARITH"
)

(define_expand "xor<mode>3"
  [(set (match_operand:VDQ 0 "s_register_operand" "")
	(xor:VDQ (match_operand:VDQ 1 "s_register_operand" "")
		 (match_operand:VDQ 2 "s_register_operand" "")))]
  "ARM_HAVE_<MODE>_ARITH"
)

(define_expand "one_cmpl<mode>2"
  [(set (match_operand:VDQ 0 "s_register_operand")
	(not:VDQ (match_operand:VDQ 1 "s_register_operand")))]
  "ARM_HAVE_<MODE>_ARITH"
)

(define_expand "neg<mode>2"
  [(set (match_operand:VDQWH 0 "s_register_operand" "")
	(neg:VDQWH (match_operand:VDQWH 1 "s_register_operand" "")))]
  "ARM_HAVE_<MODE>_ARITH"
)

(define_expand "cadd<rot><mode>3"
  [(set (match_operand:VF 0 "register_operand")
	(unspec:VF [(match_operand:VF 1 "register_operand")
		    (match_operand:VF 2 "register_operand")]
		   VCADD))]
  "(TARGET_COMPLEX || (TARGET_HAVE_MVE && TARGET_HAVE_MVE_FLOAT
		      && ARM_HAVE_<MODE>_ARITH)) && !BYTES_BIG_ENDIAN"
)

