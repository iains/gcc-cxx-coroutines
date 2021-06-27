/* Code for GIMPLE range related routines.
   Copyright (C) 2019-2021 Free Software Foundation, Inc.
   Contributed by Andrew MacLeod <amacleod@redhat.com>
   and Aldy Hernandez <aldyh@redhat.com>.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "insn-codes.h"
#include "rtl.h"
#include "tree.h"
#include "gimple.h"
#include "ssa.h"
#include "gimple-pretty-print.h"
#include "gimple-iterator.h"
#include "optabs-tree.h"
#include "gimple-fold.h"
#include "tree-cfg.h"
#include "fold-const.h"
#include "tree-cfg.h"
#include "wide-int.h"
#include "fold-const.h"
#include "case-cfn-macros.h"
#include "omp-general.h"
#include "cfgloop.h"
#include "tree-ssa-loop.h"
#include "tree-scalar-evolution.h"
#include "dbgcnt.h"
#include "alloc-pool.h"
#include "vr-values.h"
#include "gimple-range.h"

// Evaluate expression EXPR using the source information the class was
// instantiated with.  Place the result in R, and return TRUE.  If a range
// cannot be calculated, return FALSE.

bool
fur_source::get_operand (irange &r, tree expr)
{
  return get_range_query (cfun)->range_of_expr (r, expr);
}

// Evaluate EXPR for this stmt as a PHI argument on edge E.  Use the current
// range_query to get the range on the edge.

bool
fur_source::get_phi_operand (irange &r, tree expr, edge e)
{
  return get_range_query (cfun)->range_on_edge (r, e, expr);
}

// Default is to not register any dependencies from fold_using_range.

void
fur_source::register_dependency (tree lhs ATTRIBUTE_UNUSED,
				 tree rhs ATTRIBUTE_UNUSED)
{
}

// Default object is the current range query.

range_query *
fur_source::query ()
{
  return get_range_query (cfun);
}

// This version of fur_source will pick a range up off an edge.

class fur_edge : public fur_source
{
public:
  fur_edge (edge e, range_query *q = NULL);
  virtual bool get_operand (irange &r, tree expr) OVERRIDE;
  virtual bool get_phi_operand (irange &r, tree expr, edge e) OVERRIDE;
  virtual range_query *query () OVERRIDE;
private:
  range_query *m_query;
  edge m_edge;
};

// Instantiate an edge based fur_source.

inline
fur_edge::fur_edge (edge e, range_query *q)
{
  m_edge = e;
  if (q)
    m_query = q;
  else
    m_query = get_range_query (cfun);
}

// Get the value of EXPR on edge m_edge.

bool
fur_edge::get_operand (irange &r, tree expr)
{
  return m_query->range_on_edge (r, m_edge, expr);
}

// Evaluate EXPR for this stmt as a PHI argument on edge E.  Use the current
// range_query to get the range on the edge.

bool
fur_edge::get_phi_operand (irange &r, tree expr, edge e)
{
  // edge to edge recalculations not supoprted yet, until we sort it out.
  gcc_checking_assert (e == m_edge);
  return m_query->range_on_edge (r, e, expr);
}

// Return the current range_query object.

range_query *
fur_edge::query ()
{
  return m_query;
}

// Instantiate a stmt based fur_source.

fur_stmt::fur_stmt (gimple *s, range_query *q)
{
  m_stmt = s;
  if (q)
    m_query = q;
  else
    m_query = get_global_range_query ();
}

// Retrieve range of EXPR as it occurs as a use on stmt M_STMT.

bool
fur_stmt::get_operand (irange &r, tree expr)
{
  return m_query->range_of_expr (r, expr, m_stmt);
}

// Evaluate EXPR for this stmt as a PHI argument on edge E.  Use the current
// range_query to get the range on the edge.

bool
fur_stmt::get_phi_operand (irange &r, tree expr, edge e)
{
  // Pick up the range of expr from edge E.
  fur_edge e_src (e, m_query);
  return e_src.get_operand (r, expr);
}

// Return the current range_query object.

range_query *
fur_stmt::query ()
{
  return m_query;
}

// This version of fur_source will pick a range from a stmt, and also register
// dependencies via a gori_compute object.  This is mostly an internal API.

class fur_depend : public fur_stmt
{
public:
  fur_depend (gimple *s, gori_compute *gori, range_query *q = NULL);
  virtual void register_dependency (tree lhs, tree rhs) OVERRIDE;
private:
  gori_compute *m_gori;
};

// Instantiate a stmt based fur_source with a GORI object

inline
fur_depend::fur_depend (gimple *s, gori_compute *gori, range_query *q)
  : fur_stmt (s, q)
{
  gcc_checking_assert (gori);
  m_gori = gori;
}

// find and add any dependnecy between LHS and RHS

void
fur_depend::register_dependency (tree lhs, tree rhs)
{
  m_gori->register_dependency (lhs, rhs);
}

// This version of fur_source will pick a range up from a list of ranges
// supplied by the caller.

class fur_list : public fur_source
{
public:
  fur_list (irange &r1);
  fur_list (irange &r1, irange &r2);
  fur_list (unsigned num, irange *list);
  virtual bool get_operand (irange &r, tree expr) OVERRIDE;
  virtual bool get_phi_operand (irange &r, tree expr, edge e) OVERRIDE;
private:
  int_range_max m_local[2];
  irange *m_list;
  unsigned m_index;
  unsigned m_limit;
};

// One range supplied for unary operations.

fur_list::fur_list (irange &r1)
{
  m_list = m_local;
  m_index = 0;
  m_limit = 1;
  m_local[0] = r1;
}

// Two ranges supplied for binary operations.

fur_list::fur_list (irange &r1, irange &r2)
{
  m_list = m_local;
  m_index = 0;
  m_limit = 2;
  m_local[0] = r1;
  m_local[0] = r2;
}

// Arbitrary number of ranges in a vector.

fur_list::fur_list (unsigned num, irange *list)
{
  m_list = list;
  m_index = 0;
  m_limit = num;
}

// Get the next operand from the vector, ensure types are compatible.

bool
fur_list::get_operand (irange &r, tree expr)
{
  if (m_index >= m_limit)
    return get_range_query (cfun)->range_of_expr (r, expr);
  r = m_list[m_index++];
  gcc_checking_assert (range_compatible_p (TREE_TYPE (expr), r.type ()));
  return true;
}

// This will simply pick the next operand from the vector.
bool
fur_list::get_phi_operand (irange &r, tree expr, edge e ATTRIBUTE_UNUSED)
{
  return get_operand (r, expr);
}

// Fold stmt S into range R using R1 as the first operand.

bool
fold_range (irange &r, gimple *s, irange &r1)
{
  fold_using_range f;
  fur_list src (r1);
  return f.fold_stmt (r, s, src);
}

// Fold stmt S into range R using R1  and R2 as the first two operands.

bool
fold_range (irange &r, gimple *s, irange &r1, irange &r2)
{
  fold_using_range f;
  fur_list src (r1, r2);
  return f.fold_stmt (r, s, src);
}


// Fold stmt S into range R using NUM_ELEMENTS from VECTOR as the initial
// operands encountered.

bool
fold_range (irange &r, gimple *s, unsigned num_elements, irange *vector)
{
  fold_using_range f;
  fur_list src (num_elements, vector);
  return f.fold_stmt (r, s, src);
}

// Fold stmt S into range R using range query Q.

bool
fold_range (irange &r, gimple *s, range_query *q)
{
  fold_using_range f;
  fur_stmt src (s, q);
  return f.fold_stmt (r, s, src);
}

// Recalculate stmt S into R using range query Q as if it were on edge ON_EDGE.

bool
fold_range (irange &r, gimple *s, edge on_edge, range_query *q)
{
  fold_using_range f;
  fur_edge src (on_edge, q);
  return f.fold_stmt (r, s, src);
}

// -------------------------------------------------------------------------

// Adjust the range for a pointer difference where the operands came
// from a memchr.
//
// This notices the following sequence:
//
//	def = __builtin_memchr (arg, 0, sz)
//	n = def - arg
//
// The range for N can be narrowed to [0, PTRDIFF_MAX - 1].

static void
adjust_pointer_diff_expr (irange &res, const gimple *diff_stmt)
{
  tree op0 = gimple_assign_rhs1 (diff_stmt);
  tree op1 = gimple_assign_rhs2 (diff_stmt);
  tree op0_ptype = TREE_TYPE (TREE_TYPE (op0));
  tree op1_ptype = TREE_TYPE (TREE_TYPE (op1));
  gimple *call;

  if (TREE_CODE (op0) == SSA_NAME
      && TREE_CODE (op1) == SSA_NAME
      && (call = SSA_NAME_DEF_STMT (op0))
      && is_gimple_call (call)
      && gimple_call_builtin_p (call, BUILT_IN_MEMCHR)
      && TYPE_MODE (op0_ptype) == TYPE_MODE (char_type_node)
      && TYPE_PRECISION (op0_ptype) == TYPE_PRECISION (char_type_node)
      && TYPE_MODE (op1_ptype) == TYPE_MODE (char_type_node)
      && TYPE_PRECISION (op1_ptype) == TYPE_PRECISION (char_type_node)
      && gimple_call_builtin_p (call, BUILT_IN_MEMCHR)
      && vrp_operand_equal_p (op1, gimple_call_arg (call, 0))
      && integer_zerop (gimple_call_arg (call, 1)))
    {
      tree max = vrp_val_max (ptrdiff_type_node);
      wide_int wmax = wi::to_wide (max, TYPE_PRECISION (TREE_TYPE (max)));
      tree expr_type = gimple_expr_type (diff_stmt);
      tree range_min = build_zero_cst (expr_type);
      tree range_max = wide_int_to_tree (expr_type, wmax - 1);
      int_range<2> r (range_min, range_max);
      res.intersect (r);
    }
}

// This function looks for situations when walking the use/def chains
// may provide additonal contextual range information not exposed on
// this statement.  Like knowing the IMAGPART return value from a
// builtin function is a boolean result.

// We should rework how we're called, as we have an op_unknown entry
// for IMAGPART_EXPR and POINTER_DIFF_EXPR in range-ops just so this
// function gets called.

static void
gimple_range_adjustment (irange &res, const gimple *stmt)
{
  switch (gimple_expr_code (stmt))
    {
    case POINTER_DIFF_EXPR:
      adjust_pointer_diff_expr (res, stmt);
      return;

    case IMAGPART_EXPR:
      {
	tree name = TREE_OPERAND (gimple_assign_rhs1 (stmt), 0);
	if (TREE_CODE (name) == SSA_NAME)
	  {
	    gimple *def_stmt = SSA_NAME_DEF_STMT (name);
	    if (def_stmt && is_gimple_call (def_stmt)
		&& gimple_call_internal_p (def_stmt))
	      {
		switch (gimple_call_internal_fn (def_stmt))
		  {
		  case IFN_ADD_OVERFLOW:
		  case IFN_SUB_OVERFLOW:
		  case IFN_MUL_OVERFLOW:
		  case IFN_ATOMIC_COMPARE_EXCHANGE:
		    {
		      int_range<2> r;
		      r.set_varying (boolean_type_node);
		      tree type = TREE_TYPE (gimple_assign_lhs (stmt));
		      range_cast (r, type);
		      res.intersect (r);
		    }
		  default:
		    break;
		  }
	      }
	  }
	break;
      }

    default:
      break;
    }
}

// Return the base of the RHS of an assignment.

static tree
gimple_range_base_of_assignment (const gimple *stmt)
{
  gcc_checking_assert (gimple_code (stmt) == GIMPLE_ASSIGN);
  tree op1 = gimple_assign_rhs1 (stmt);
  if (gimple_assign_rhs_code (stmt) == ADDR_EXPR)
    return get_base_address (TREE_OPERAND (op1, 0));
  return op1;
}

// Return the first operand of this statement if it is a valid operand
// supported by ranges, otherwise return NULL_TREE.  Special case is
// &(SSA_NAME expr), return the SSA_NAME instead of the ADDR expr.

tree
gimple_range_operand1 (const gimple *stmt)
{
  gcc_checking_assert (gimple_range_handler (stmt));

  switch (gimple_code (stmt))
    {
      case GIMPLE_COND:
	return gimple_cond_lhs (stmt);
      case GIMPLE_ASSIGN:
	{
	  tree base = gimple_range_base_of_assignment (stmt);
	  if (base && TREE_CODE (base) == MEM_REF)
	    {
	      // If the base address is an SSA_NAME, we return it
	      // here.  This allows processing of the range of that
	      // name, while the rest of the expression is simply
	      // ignored.  The code in range_ops will see the
	      // ADDR_EXPR and do the right thing.
	      tree ssa = TREE_OPERAND (base, 0);
	      if (TREE_CODE (ssa) == SSA_NAME)
		return ssa;
	    }
	  return base;
	}
      default:
	break;
    }
  return NULL;
}

// Return the second operand of statement STMT, otherwise return NULL_TREE.

tree
gimple_range_operand2 (const gimple *stmt)
{
  gcc_checking_assert (gimple_range_handler (stmt));

  switch (gimple_code (stmt))
    {
    case GIMPLE_COND:
      return gimple_cond_rhs (stmt);
    case GIMPLE_ASSIGN:
      if (gimple_num_ops (stmt) >= 3)
	return gimple_assign_rhs2 (stmt);
    default:
      break;
    }
  return NULL_TREE;
}

// Calculate what we can determine of the range of this unary
// statement's operand if the lhs of the expression has the range
// LHS_RANGE.  Return false if nothing can be determined.

bool
gimple_range_calc_op1 (irange &r, const gimple *stmt, const irange &lhs_range)
{
  gcc_checking_assert (gimple_num_ops (stmt) < 3);

  // An empty range is viral.
  tree type = TREE_TYPE (gimple_range_operand1 (stmt));
  if (lhs_range.undefined_p ())
    {
      r.set_undefined ();
      return true;
    }
  // Unary operations require the type of the first operand in the
  // second range position.
  int_range<2> type_range (type);
  return gimple_range_handler (stmt)->op1_range (r, type, lhs_range,
						 type_range);
}

// Calculate what we can determine of the range of this statement's
// first operand if the lhs of the expression has the range LHS_RANGE
// and the second operand has the range OP2_RANGE.  Return false if
// nothing can be determined.

bool
gimple_range_calc_op1 (irange &r, const gimple *stmt,
		       const irange &lhs_range, const irange &op2_range)
{
  // Unary operation are allowed to pass a range in for second operand
  // as there are often additional restrictions beyond the type which
  // can be imposed.  See operator_cast::op1_range().
  tree type = TREE_TYPE (gimple_range_operand1 (stmt));
  // An empty range is viral.
  if (op2_range.undefined_p () || lhs_range.undefined_p ())
    {
      r.set_undefined ();
      return true;
    }
  return gimple_range_handler (stmt)->op1_range (r, type, lhs_range,
						 op2_range);
}

// Calculate what we can determine of the range of this statement's
// second operand if the lhs of the expression has the range LHS_RANGE
// and the first operand has the range OP1_RANGE.  Return false if
// nothing can be determined.

bool
gimple_range_calc_op2 (irange &r, const gimple *stmt,
		       const irange &lhs_range, const irange &op1_range)
{
  tree type = TREE_TYPE (gimple_range_operand2 (stmt));
  // An empty range is viral.
  if (op1_range.undefined_p () || lhs_range.undefined_p ())
    {
      r.set_undefined ();
      return true;
    }
  return gimple_range_handler (stmt)->op2_range (r, type, lhs_range,
						 op1_range);
}

// Calculate a range for statement S and return it in R. If NAME is provided it
// represents the SSA_NAME on the LHS of the statement. It is only required
// if there is more than one lhs/output.  If a range cannot
// be calculated, return false.

bool
fold_using_range::fold_stmt (irange &r, gimple *s, fur_source &src, tree name)
{
  bool res = false;
  // If name and S are specified, make sure it is an LHS of S.
  gcc_checking_assert (!name || !gimple_get_lhs (s) ||
		       name == gimple_get_lhs (s));

  if (!name)
    name = gimple_get_lhs (s);

  // Process addresses.
  if (gimple_code (s) == GIMPLE_ASSIGN
      && gimple_assign_rhs_code (s) == ADDR_EXPR)
    return range_of_address (r, s, src);

  if (gimple_range_handler (s))
    res = range_of_range_op (r, s, src);
  else if (is_a<gphi *>(s))
    res = range_of_phi (r, as_a<gphi *> (s), src);
  else if (is_a<gcall *>(s))
    res = range_of_call (r, as_a<gcall *> (s), src);
  else if (is_a<gassign *> (s) && gimple_assign_rhs_code (s) == COND_EXPR)
    res = range_of_cond_expr (r, as_a<gassign *> (s), src);

  if (!res)
    {
      // If no name is specified, try the expression kind.
      if (!name)
	{
	  tree t = gimple_expr_type (s);
	  if (!irange::supports_type_p (t))
	    return false;
	  r.set_varying (t);
	  return true;
	}
      if (!gimple_range_ssa_p (name))
	return false;
      // We don't understand the stmt, so return the global range.
      r = gimple_range_global (name);
      return true;
    }

  if (r.undefined_p ())
    return true;

  // We sometimes get compatible types copied from operands, make sure
  // the correct type is being returned.
  if (name && TREE_TYPE (name) != r.type ())
    {
      gcc_checking_assert (range_compatible_p (r.type (), TREE_TYPE (name)));
      range_cast (r, TREE_TYPE (name));
    }
  return true;
}

// Calculate a range for range_op statement S and return it in R.  If any
// If a range cannot be calculated, return false.

bool
fold_using_range::range_of_range_op (irange &r, gimple *s, fur_source &src)
{
  int_range_max range1, range2;
  tree type = gimple_expr_type (s);
  range_operator *handler = gimple_range_handler (s);
  gcc_checking_assert (handler);
  gcc_checking_assert (irange::supports_type_p (type));

  tree lhs = gimple_get_lhs (s);
  tree op1 = gimple_range_operand1 (s);
  tree op2 = gimple_range_operand2 (s);

  if (src.get_operand (range1, op1))
    {
      if (!op2)
	{
	  // Fold range, and register any dependency if available.
	  int_range<2> r2 (type);
	  handler->fold_range (r, type, range1, r2);
	  if (lhs)
	    src.register_dependency (lhs, op1);
	}
      else if (src.get_operand (range2, op2))
	{
	  // Fold range, and register any dependency if available.
	  handler->fold_range (r, type, range1, range2);
	  if (lhs)
	    {
	      src.register_dependency (lhs, op1);
	      src.register_dependency (lhs, op2);
	    }
	}
      else
	r.set_varying (type);
    }
  else
    r.set_varying (type);
  // Make certain range-op adjustments that aren't handled any other way.
  gimple_range_adjustment (r, s);
  return true;
}

// Calculate the range of an assignment containing an ADDR_EXPR.
// Return the range in R.
// If a range cannot be calculated, set it to VARYING and return true.

bool
fold_using_range::range_of_address (irange &r, gimple *stmt, fur_source &src)
{
  gcc_checking_assert (gimple_code (stmt) == GIMPLE_ASSIGN);
  gcc_checking_assert (gimple_assign_rhs_code (stmt) == ADDR_EXPR);

  bool strict_overflow_p;
  tree expr = gimple_assign_rhs1 (stmt);
  poly_int64 bitsize, bitpos;
  tree offset;
  machine_mode mode;
  int unsignedp, reversep, volatilep;
  tree base = get_inner_reference (TREE_OPERAND (expr, 0), &bitsize,
				   &bitpos, &offset, &mode, &unsignedp,
				   &reversep, &volatilep);


  if (base != NULL_TREE
      && TREE_CODE (base) == MEM_REF
      && TREE_CODE (TREE_OPERAND (base, 0)) == SSA_NAME)
    {
      tree ssa = TREE_OPERAND (base, 0);
      tree lhs = gimple_get_lhs (stmt);
      if (lhs && gimple_range_ssa_p (ssa))
	src.register_dependency (lhs, ssa);
      gcc_checking_assert (irange::supports_type_p (TREE_TYPE (ssa)));
      src.get_operand (r, ssa);
      range_cast (r, TREE_TYPE (gimple_assign_rhs1 (stmt)));

      poly_offset_int off = 0;
      bool off_cst = false;
      if (offset == NULL_TREE || TREE_CODE (offset) == INTEGER_CST)
	{
	  off = mem_ref_offset (base);
	  if (offset)
	    off += poly_offset_int::from (wi::to_poly_wide (offset),
					  SIGNED);
	  off <<= LOG2_BITS_PER_UNIT;
	  off += bitpos;
	  off_cst = true;
	}
      /* If &X->a is equal to X, the range of X is the result.  */
      if (off_cst && known_eq (off, 0))
	  return true;
      else if (flag_delete_null_pointer_checks
	       && !TYPE_OVERFLOW_WRAPS (TREE_TYPE (expr)))
	{
	 /* For -fdelete-null-pointer-checks -fno-wrapv-pointer we don't
	 allow going from non-NULL pointer to NULL.  */
	   if(!range_includes_zero_p (&r))
	    return true;
	}
      /* If MEM_REF has a "positive" offset, consider it non-NULL
	 always, for -fdelete-null-pointer-checks also "negative"
	 ones.  Punt for unknown offsets (e.g. variable ones).  */
      if (!TYPE_OVERFLOW_WRAPS (TREE_TYPE (expr))
	  && off_cst
	  && known_ne (off, 0)
	  && (flag_delete_null_pointer_checks || known_gt (off, 0)))
	{
	  r = range_nonzero (TREE_TYPE (gimple_assign_rhs1 (stmt)));
	  return true;
	}
      r = int_range<2> (TREE_TYPE (gimple_assign_rhs1 (stmt)));
      return true;
    }

  // Handle "= &a".
  if (tree_single_nonzero_warnv_p (expr, &strict_overflow_p))
    {
      r = range_nonzero (TREE_TYPE (gimple_assign_rhs1 (stmt)));
      return true;
    }

  // Otherwise return varying.
  r = int_range<2> (TREE_TYPE (gimple_assign_rhs1 (stmt)));
  return true;
}

// Calculate a range for phi statement S and return it in R.
// If a range cannot be calculated, return false.

bool
fold_using_range::range_of_phi (irange &r, gphi *phi, fur_source &src)
{
  tree phi_def = gimple_phi_result (phi);
  tree type = TREE_TYPE (phi_def);
  int_range_max arg_range;
  unsigned x;

  if (!irange::supports_type_p (type))
    return false;

  // Start with an empty range, unioning in each argument's range.
  r.set_undefined ();
  for (x = 0; x < gimple_phi_num_args (phi); x++)
    {
      tree arg = gimple_phi_arg_def (phi, x);
      edge e = gimple_phi_arg_edge (phi, x);

      // Register potential dependencies for stale value tracking.
      if (gimple_range_ssa_p (arg))
	src.register_dependency (phi_def, arg);

      // Get the range of the argument on its edge.
      src.get_phi_operand (arg_range, arg, e);
      // If we're recomputing the argument elsewhere, try to refine it.
      r.union_ (arg_range);
      // Once the value reaches varying, stop looking.
      if (r.varying_p ())
	break;
    }

  // If SCEV is available, query if this PHI has any knonwn values.
  if (scev_initialized_p () && !POINTER_TYPE_P (TREE_TYPE (phi_def)))
    {
      value_range loop_range;
      class loop *l = loop_containing_stmt (phi);
      if (l && loop_outer (l))
        {
	  range_of_ssa_name_with_loop_info (loop_range, phi_def, l, phi, src);
	  if (!loop_range.varying_p ())
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		{
		  fprintf (dump_file, "   Loops range found for ");
		  print_generic_expr (dump_file, phi_def, TDF_SLIM);
		  fprintf (dump_file, ": ");
		  loop_range.dump (dump_file);
		  fprintf (dump_file, " and calculated range :");
		  r.dump (dump_file);
		  fprintf (dump_file, "\n");
		}
	      r.intersect (loop_range);
	    }
	}
    }

  return true;
}

// Calculate a range for call statement S and return it in R.
// If a range cannot be calculated, return false.

bool
fold_using_range::range_of_call (irange &r, gcall *call, fur_source &src)
{
  tree type = gimple_call_return_type (call);
  tree lhs = gimple_call_lhs (call);
  bool strict_overflow_p;

  if (!irange::supports_type_p (type))
    return false;

  if (range_of_builtin_call (r, call, src))
    ;
  else if (gimple_stmt_nonnegative_warnv_p (call, &strict_overflow_p))
    r.set (build_int_cst (type, 0), TYPE_MAX_VALUE (type));
  else if (gimple_call_nonnull_result_p (call)
	   || gimple_call_nonnull_arg (call))
    r = range_nonzero (type);
  else
    r.set_varying (type);

  // If there is an LHS, intersect that with what is known.
  if (lhs)
    {
      value_range def;
      def = gimple_range_global (lhs);
      r.intersect (def);
    }
  return true;
}

// Return the range of a __builtin_ubsan* in CALL and set it in R.
// CODE is the type of ubsan call (PLUS_EXPR, MINUS_EXPR or
// MULT_EXPR).

void
fold_using_range::range_of_builtin_ubsan_call (irange &r, gcall *call,
					       tree_code code, fur_source &src)
{
  gcc_checking_assert (code == PLUS_EXPR || code == MINUS_EXPR
		       || code == MULT_EXPR);
  tree type = gimple_call_return_type (call);
  range_operator *op = range_op_handler (code, type);
  gcc_checking_assert (op);
  int_range_max ir0, ir1;
  tree arg0 = gimple_call_arg (call, 0);
  tree arg1 = gimple_call_arg (call, 1);
  src.get_operand (ir0, arg0);
  src.get_operand (ir1, arg1);

  bool saved_flag_wrapv = flag_wrapv;
  // Pretend the arithmetic is wrapping.  If there is any overflow,
  // we'll complain, but will actually do wrapping operation.
  flag_wrapv = 1;
  op->fold_range (r, type, ir0, ir1);
  flag_wrapv = saved_flag_wrapv;

  // If for both arguments vrp_valueize returned non-NULL, this should
  // have been already folded and if not, it wasn't folded because of
  // overflow.  Avoid removing the UBSAN_CHECK_* calls in that case.
  if (r.singleton_p ())
    r.set_varying (type);
}

// For a builtin in CALL, return a range in R if known and return
// TRUE.  Otherwise return FALSE.

bool
fold_using_range::range_of_builtin_call (irange &r, gcall *call,
					 fur_source &src)
{
  combined_fn func = gimple_call_combined_fn (call);
  if (func == CFN_LAST)
    return false;

  tree type = gimple_call_return_type (call);
  tree arg;
  int mini, maxi, zerov = 0, prec;
  scalar_int_mode mode;

  switch (func)
    {
    case CFN_BUILT_IN_CONSTANT_P:
      if (cfun->after_inlining)
	{
	  r.set_zero (type);
	  // r.equiv_clear ();
	  return true;
	}
      arg = gimple_call_arg (call, 0);
      if (src.get_operand (r, arg) && r.singleton_p ())
	{
	  r.set (build_one_cst (type), build_one_cst (type));
	  return true;
	}
      break;

    CASE_CFN_FFS:
    CASE_CFN_POPCOUNT:
      // __builtin_ffs* and __builtin_popcount* return [0, prec].
      arg = gimple_call_arg (call, 0);
      prec = TYPE_PRECISION (TREE_TYPE (arg));
      mini = 0;
      maxi = prec;
      src.get_operand (r, arg);
      // If arg is non-zero, then ffs or popcount are non-zero.
      if (!range_includes_zero_p (&r))
	mini = 1;
      // If some high bits are known to be zero, decrease the maximum.
      if (!r.undefined_p ())
	{
	  if (TYPE_SIGN (r.type ()) == SIGNED)
	    range_cast (r, unsigned_type_for (r.type ()));
	  wide_int max = r.upper_bound ();
	  maxi = wi::floor_log2 (max) + 1;
	}
      r.set (build_int_cst (type, mini), build_int_cst (type, maxi));
      return true;

    CASE_CFN_PARITY:
      r.set (build_zero_cst (type), build_one_cst (type));
      return true;

    CASE_CFN_CLZ:
      // __builtin_c[lt]z* return [0, prec-1], except when the
      // argument is 0, but that is undefined behavior.
      //
      // For __builtin_c[lt]z* consider argument of 0 always undefined
      // behavior, for internal fns depending on C?Z_DEFINED_VALUE_AT_ZERO.
      arg = gimple_call_arg (call, 0);
      prec = TYPE_PRECISION (TREE_TYPE (arg));
      mini = 0;
      maxi = prec - 1;
      mode = SCALAR_INT_TYPE_MODE (TREE_TYPE (arg));
      if (gimple_call_internal_p (call))
	{
	  if (optab_handler (clz_optab, mode) != CODE_FOR_nothing
	      && CLZ_DEFINED_VALUE_AT_ZERO (mode, zerov) == 2)
	    {
	      // Only handle the single common value.
	      if (zerov == prec)
		maxi = prec;
	      else
		// Magic value to give up, unless we can prove arg is non-zero.
		mini = -2;
	    }
	}

      src.get_operand (r, arg);
      // From clz of minimum we can compute result maximum.
      if (!r.undefined_p ())
	{
	  // From clz of minimum we can compute result maximum.
	  if (wi::gt_p (r.lower_bound (), 0, TYPE_SIGN (r.type ())))
	    {
	      maxi = prec - 1 - wi::floor_log2 (r.lower_bound ());
	      if (mini == -2)
		mini = 0;
	    }
	  else if (!range_includes_zero_p (&r))
	    {
	      mini = 0;
	      maxi = prec - 1;
	    }
	  if (mini == -2)
	    break;
	  // From clz of maximum we can compute result minimum.
	  wide_int max = r.upper_bound ();
	  int newmini = prec - 1 - wi::floor_log2 (max);
	  if (max == 0)
	    {
	      // If CLZ_DEFINED_VALUE_AT_ZERO is 2 with VALUE of prec,
	      // return [prec, prec], otherwise ignore the range.
	      if (maxi == prec)
		mini = prec;
	    }
	  else
	    mini = newmini;
	}
      if (mini == -2)
	break;
      r.set (build_int_cst (type, mini), build_int_cst (type, maxi));
      return true;

    CASE_CFN_CTZ:
      // __builtin_ctz* return [0, prec-1], except for when the
      // argument is 0, but that is undefined behavior.
      //
      // For __builtin_ctz* consider argument of 0 always undefined
      // behavior, for internal fns depending on CTZ_DEFINED_VALUE_AT_ZERO.
      arg = gimple_call_arg (call, 0);
      prec = TYPE_PRECISION (TREE_TYPE (arg));
      mini = 0;
      maxi = prec - 1;
      mode = SCALAR_INT_TYPE_MODE (TREE_TYPE (arg));
      if (gimple_call_internal_p (call))
	{
	  if (optab_handler (ctz_optab, mode) != CODE_FOR_nothing
	      && CTZ_DEFINED_VALUE_AT_ZERO (mode, zerov) == 2)
	    {
	      // Handle only the two common values.
	      if (zerov == -1)
		mini = -1;
	      else if (zerov == prec)
		maxi = prec;
	      else
		// Magic value to give up, unless we can prove arg is non-zero.
		mini = -2;
	    }
	}
      src.get_operand (r, arg);
      if (!r.undefined_p ())
	{
	  // If arg is non-zero, then use [0, prec - 1].
	  if (!range_includes_zero_p (&r))
	    {
	      mini = 0;
	      maxi = prec - 1;
	    }
	  // If some high bits are known to be zero, we can decrease
	  // the maximum.
	  wide_int max = r.upper_bound ();
	  if (max == 0)
	    {
	      // Argument is [0, 0].  If CTZ_DEFINED_VALUE_AT_ZERO
	      // is 2 with value -1 or prec, return [-1, -1] or [prec, prec].
	      // Otherwise ignore the range.
	      if (mini == -1)
		maxi = -1;
	      else if (maxi == prec)
		mini = prec;
	    }
	  // If value at zero is prec and 0 is in the range, we can't lower
	  // the upper bound.  We could create two separate ranges though,
	  // [0,floor_log2(max)][prec,prec] though.
	  else if (maxi != prec)
	    maxi = wi::floor_log2 (max);
	}
      if (mini == -2)
	break;
      r.set (build_int_cst (type, mini), build_int_cst (type, maxi));
      return true;

    CASE_CFN_CLRSB:
      arg = gimple_call_arg (call, 0);
      prec = TYPE_PRECISION (TREE_TYPE (arg));
      r.set (build_int_cst (type, 0), build_int_cst (type, prec - 1));
      return true;
    case CFN_UBSAN_CHECK_ADD:
      range_of_builtin_ubsan_call (r, call, PLUS_EXPR, src);
      return true;
    case CFN_UBSAN_CHECK_SUB:
      range_of_builtin_ubsan_call (r, call, MINUS_EXPR, src);
      return true;
    case CFN_UBSAN_CHECK_MUL:
      range_of_builtin_ubsan_call (r, call, MULT_EXPR, src);
      return true;

    case CFN_GOACC_DIM_SIZE:
    case CFN_GOACC_DIM_POS:
      // Optimizing these two internal functions helps the loop
      // optimizer eliminate outer comparisons.  Size is [1,N]
      // and pos is [0,N-1].
      {
	bool is_pos = func == CFN_GOACC_DIM_POS;
	int axis = oacc_get_ifn_dim_arg (call);
	int size = oacc_get_fn_dim_size (current_function_decl, axis);
	if (!size)
	  // If it's dynamic, the backend might know a hardware limitation.
	  size = targetm.goacc.dim_limit (axis);

	r.set (build_int_cst (type, is_pos ? 0 : 1),
	       size
	       ? build_int_cst (type, size - is_pos) : vrp_val_max (type));
	return true;
      }

    case CFN_BUILT_IN_STRLEN:
      if (tree lhs = gimple_call_lhs (call))
	if (ptrdiff_type_node
	    && (TYPE_PRECISION (ptrdiff_type_node)
		== TYPE_PRECISION (TREE_TYPE (lhs))))
	  {
	    tree type = TREE_TYPE (lhs);
	    tree max = vrp_val_max (ptrdiff_type_node);
	    wide_int wmax
	      = wi::to_wide (max, TYPE_PRECISION (TREE_TYPE (max)));
	    tree range_min = build_zero_cst (type);
	    // To account for the terminating NULL, the maximum length
	    // is one less than the maximum array size, which in turn
	    // is one less than PTRDIFF_MAX (or SIZE_MAX where it's
	    // smaller than the former type).
	    // FIXME: Use max_object_size() - 1 here.
	    tree range_max = wide_int_to_tree (type, wmax - 2);
	    r.set (range_min, range_max);
	    return true;
	  }
      break;
    default:
      break;
    }
  return false;
}


// Calculate a range for COND_EXPR statement S and return it in R.
// If a range cannot be calculated, return false.

bool
fold_using_range::range_of_cond_expr  (irange &r, gassign *s, fur_source &src)
{
  int_range_max cond_range, range1, range2;
  tree cond = gimple_assign_rhs1 (s);
  tree op1 = gimple_assign_rhs2 (s);
  tree op2 = gimple_assign_rhs3 (s);

  gcc_checking_assert (gimple_assign_rhs_code (s) == COND_EXPR);
  gcc_checking_assert (useless_type_conversion_p  (TREE_TYPE (op1),
						   TREE_TYPE (op2)));
  if (!irange::supports_type_p (TREE_TYPE (op1)))
    return false;

  src.get_operand (cond_range, cond);
  src.get_operand (range1, op1);
  src.get_operand (range2, op2);

  // If the condition is known, choose the appropriate expression.
  if (cond_range.singleton_p ())
    {
      // False, pick second operand.
      if (cond_range.zero_p ())
	r = range2;
      else
	r = range1;
    }
  else
    {
      r = range1;
      r.union_ (range2);
    }
  return true;
}

bool
gimple_ranger::range_of_expr (irange &r, tree expr, gimple *stmt)
{
  if (!gimple_range_ssa_p (expr))
    return get_tree_range (r, expr, stmt);

  // If there is no statement, just get the global value.
  if (!stmt)
    {
      if (!m_cache.get_global_range (r, expr))
        r = gimple_range_global (expr);
      return true;
    }

  // For a debug stmt, pick the best value currently available, do not
  // trigger new value calculations.  PR 100781.
  if (is_gimple_debug (stmt))
    {
      m_cache.range_of_expr (r, expr, stmt);
      return true;
    }
  basic_block bb = gimple_bb (stmt);
  gimple *def_stmt = SSA_NAME_DEF_STMT (expr);

  // If name is defined in this block, try to get an range from S.
  if (def_stmt && gimple_bb (def_stmt) == bb)
    {
      range_of_stmt (r, def_stmt, expr);
      if (!cfun->can_throw_non_call_exceptions && r.varying_p () &&
	  m_cache.m_non_null.non_null_deref_p (expr, bb))
	r = range_nonzero (TREE_TYPE (expr));
    }
  else
    // Otherwise OP comes from outside this block, use range on entry.
    range_on_entry (r, bb, expr);

  return true;
}

// Return the range of NAME on entry to block BB in R.

void
gimple_ranger::range_on_entry (irange &r, basic_block bb, tree name)
{
  int_range_max entry_range;
  gcc_checking_assert (gimple_range_ssa_p (name));

  // Start with any known range
  range_of_stmt (r, SSA_NAME_DEF_STMT (name), name);

  // Now see if there is any on_entry value which may refine it.
  if (m_cache.block_range (entry_range, bb, name))
    r.intersect (entry_range);

  if (!cfun->can_throw_non_call_exceptions && r.varying_p () &&
      m_cache.m_non_null.non_null_deref_p (name, bb))
    r = range_nonzero (TREE_TYPE (name));
}

// Calculate the range for NAME at the end of block BB and return it in R.
// Return false if no range can be calculated.

void
gimple_ranger::range_on_exit (irange &r, basic_block bb, tree name)
{
  // on-exit from the exit block?
  gcc_checking_assert (bb != EXIT_BLOCK_PTR_FOR_FN (cfun));
  gcc_checking_assert (gimple_range_ssa_p (name));

  gimple *s = SSA_NAME_DEF_STMT (name);
  basic_block def_bb = gimple_bb (s);
  // If this is not the definition block, get the range on the last stmt in
  // the block... if there is one.
  if (def_bb != bb)
    s = last_stmt (bb);
  // If there is no statement provided, get the range_on_entry for this block.
  if (s)
    range_of_expr (r, name, s);
  else
    range_on_entry (r, bb, name);
  gcc_checking_assert (r.undefined_p ()
		       || range_compatible_p (r.type (), TREE_TYPE (name)));
}

// Calculate a range for NAME on edge E and return it in R.

bool
gimple_ranger::range_on_edge (irange &r, edge e, tree name)
{
  int_range_max edge_range;
  gcc_checking_assert (irange::supports_type_p (TREE_TYPE (name)));

  // PHI arguments can be constants, catch these here.
  if (!gimple_range_ssa_p (name))
    return range_of_expr (r, name);

  range_on_exit (r, e->src, name);
  gcc_checking_assert  (r.undefined_p ()
			|| range_compatible_p (r.type(), TREE_TYPE (name)));

  // Check to see if NAME is defined on edge e.
  if (m_cache.range_on_edge (edge_range, e, name))
    r.intersect (edge_range);

  return true;
}

// fold_range wrapper for range_of_stmt to use as an internal client.

bool
gimple_ranger::fold_range_internal (irange &r, gimple *s, tree name)
{
  fold_using_range f;
  fur_depend src (s, &(gori ()), this);
  return f.fold_stmt (r, s, src, name);
}

// Calculate a range for statement S and return it in R.  If NAME is
// provided it represents the SSA_NAME on the LHS of the statement.
// It is only required if there is more than one lhs/output.  Check
// the global cache for NAME first to see if the evaluation can be
// avoided.  If a range cannot be calculated, return false and UNDEFINED.

bool
gimple_ranger::range_of_stmt (irange &r, gimple *s, tree name)
{
  r.set_undefined ();

  if (!name)
    name = gimple_get_lhs (s);

  // If no name, simply call the base routine.
  if (!name)
    return fold_range_internal (r, s, NULL_TREE);

  if (!gimple_range_ssa_p (name))
    return false;

  // Check if the stmt has already been processed, and is not stale.
  if (m_cache.get_non_stale_global_range (r, name))
    return true;

  // Otherwise calculate a new value.
  int_range_max tmp;
  fold_range_internal (tmp, s, name);

  // Combine the new value with the old value.  This is required because
  // the way value propagation works, when the IL changes on the fly we
  // can sometimes get different results.  See PR 97741.
  r.intersect (tmp);
  m_cache.set_global_range (name, r);

  return true;
}

// This routine will export whatever global ranges are known to GCC
// SSA_RANGE_NAME_INFO and SSA_NAME_PTR_INFO fields.

void
gimple_ranger::export_global_ranges ()
{
  unsigned x;
  int_range_max r;
  if (dump_file)
    {
      fprintf (dump_file, "Exported global range table\n");
      fprintf (dump_file, "===========================\n");
    }

  for ( x = 1; x < num_ssa_names; x++)
    {
      tree name = ssa_name (x);
      if (name && !SSA_NAME_IN_FREE_LIST (name)
	  && gimple_range_ssa_p (name)
	  && m_cache.get_global_range (r, name)
	  && !r.varying_p())
	{
	  bool updated = update_global_range (r, name);

	  if (updated && dump_file)
	    {
	      value_range vr = r;
	      print_generic_expr (dump_file, name , TDF_SLIM);
	      fprintf (dump_file, " --> ");
	      vr.dump (dump_file);
	      fprintf (dump_file, "\n");
	      int_range_max same = vr;
	      if (same != r)
		{
		  fprintf (dump_file, "         irange : ");
		  r.dump (dump_file);
		  fprintf (dump_file, "\n");
		}
	    }
	}
    }
}

// Print the known table values to file F.

void
gimple_ranger::dump_bb (FILE *f, basic_block bb)
{
  unsigned x;
  edge_iterator ei;
  edge e;
  int_range_max range;
  fprintf (f, "\n=========== BB %d ============\n", bb->index);
  m_cache.dump_bb (f, bb);

  ::dump_bb (f, bb, 4, TDF_NONE);

  // Now find any globals defined in this block.
  for (x = 1; x < num_ssa_names; x++)
    {
      tree name = ssa_name (x);
      if (gimple_range_ssa_p (name) && SSA_NAME_DEF_STMT (name) &&
	  gimple_bb (SSA_NAME_DEF_STMT (name)) == bb &&
	  m_cache.get_global_range (range, name))
	{
	  if (!range.varying_p ())
	    {
	      print_generic_expr (f, name, TDF_SLIM);
	      fprintf (f, " : ");
	      range.dump (f);
	      fprintf (f, "\n");
	    }

	}
    }

  // And now outgoing edges, if they define anything.
  FOR_EACH_EDGE (e, ei, bb->succs)
    {
      for (x = 1; x < num_ssa_names; x++)
	{
	  tree name = gimple_range_ssa_p (ssa_name (x));
	  if (name && gori ().has_edge_range_p (name, e)
	      && m_cache.range_on_edge (range, e, name))
	    {
	      gimple *s = SSA_NAME_DEF_STMT (name);
	      // Only print the range if this is the def block, or
	      // the on entry cache for either end of the edge is
	      // set.
	      if ((s && bb == gimple_bb (s)) ||
		  m_cache.block_range (range, bb, name, false) ||
		  m_cache.block_range (range, e->dest, name, false))
		{
		  range_on_edge (range, e, name);
		  if (!range.varying_p ())
		    {
		      fprintf (f, "%d->%d ", e->src->index,
			       e->dest->index);
		      char c = ' ';
		      if (e->flags & EDGE_TRUE_VALUE)
			fprintf (f, " (T)%c", c);
		      else if (e->flags & EDGE_FALSE_VALUE)
			fprintf (f, " (F)%c", c);
		      else
			fprintf (f, "     ");
		      print_generic_expr (f, name, TDF_SLIM);
		      fprintf(f, " : \t");
		      range.dump(f);
		      fprintf (f, "\n");
		    }
		}
	    }
	}
    }
}

// Print the known table values to file F.

void
gimple_ranger::dump (FILE *f)
{
  basic_block bb;

  FOR_EACH_BB_FN (bb, cfun)
    dump_bb (f, bb);

  m_cache.dump (f);
}

// If SCEV has any information about phi node NAME, return it as a range in R.

void
fold_using_range::range_of_ssa_name_with_loop_info (irange &r, tree name,
						    class loop *l, gphi *phi,
						    fur_source &src)
{
  gcc_checking_assert (TREE_CODE (name) == SSA_NAME);
  tree min, max, type = TREE_TYPE (name);
  if (bounds_of_var_in_loop (&min, &max, src.query (), l, phi, name))
    {
      if (TREE_CODE (min) != INTEGER_CST)
	{
	  if (src.query ()->range_of_expr (r, min, phi) && !r.undefined_p ())
	    min = wide_int_to_tree (type, r.lower_bound ());
	  else
	    min = vrp_val_min (type);
	}
      if (TREE_CODE (max) != INTEGER_CST)
	{
	  if (src.query ()->range_of_expr (r, max, phi) && !r.undefined_p ())
	    max = wide_int_to_tree (type, r.upper_bound ());
	  else
	    max = vrp_val_max (type);
	}
      r.set (min, max);
    }
  else
    r.set_varying (type);
}

// --------------------------------------------------------------------------
// trace_ranger implementation.


trace_ranger::trace_ranger ()
{
  indent = 0;
  trace_count = 0;
}

// If dumping, return true and print the prefix for the next output line.

bool
trace_ranger::dumping (unsigned counter, bool trailing)
{
  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      // Print counter index as well as INDENT spaces.
      if (!trailing)
	fprintf (dump_file, " %-7u ", counter);
      else
	fprintf (dump_file, "         ");
      unsigned x;
      for (x = 0; x< indent; x++)
	fputc (' ', dump_file);
      return true;
    }
  return false;
}

// After calling a routine, if dumping, print the CALLER, NAME, and RESULT,
// returning RESULT.

bool
trace_ranger::trailer (unsigned counter, const char *caller, bool result,
		       tree name, const irange &r)
{
  if (dumping (counter, true))
    {
      indent -= bump;
      fputs(result ? "TRUE : " : "FALSE : ", dump_file);
      fprintf (dump_file, "(%u) ", counter);
      fputs (caller, dump_file);
      fputs (" (",dump_file);
      if (name)
	print_generic_expr (dump_file, name, TDF_SLIM);
      fputs (") ",dump_file);
      if (result)
	{
	  r.dump (dump_file);
	  fputc('\n', dump_file);
	}
      else
	fputc('\n', dump_file);
      // Marks the end of a request.
      if (indent == 0)
	fputc('\n', dump_file);
    }
  return result;
}

// Tracing version of range_on_edge.  Call it with printing wrappers.

bool
trace_ranger::range_on_edge (irange &r, edge e, tree name)
{
  unsigned idx = ++trace_count;
  if (dumping (idx))
    {
      fprintf (dump_file, "range_on_edge (");
      print_generic_expr (dump_file, name, TDF_SLIM);
      fprintf (dump_file, ") on edge %d->%d\n", e->src->index, e->dest->index);
      indent += bump;
    }

  bool res = gimple_ranger::range_on_edge (r, e, name);
  trailer (idx, "range_on_edge", true, name, r);
  return res;
}

// Tracing version of range_on_entry.  Call it with printing wrappers.

void
trace_ranger::range_on_entry (irange &r, basic_block bb, tree name)
{
  unsigned idx = ++trace_count;
  if (dumping (idx))
    {
      fprintf (dump_file, "range_on_entry (");
      print_generic_expr (dump_file, name, TDF_SLIM);
      fprintf (dump_file, ") to BB %d\n", bb->index);
      indent += bump;
    }

  gimple_ranger::range_on_entry (r, bb, name);

  trailer (idx, "range_on_entry", true, name, r);
}

// Tracing version of range_on_exit.  Call it with printing wrappers.

void
trace_ranger::range_on_exit (irange &r, basic_block bb, tree name)
{
  unsigned idx = ++trace_count;
  if (dumping (idx))
    {
      fprintf (dump_file, "range_on_exit (");
      print_generic_expr (dump_file, name, TDF_SLIM);
      fprintf (dump_file, ") from BB %d\n", bb->index);
      indent += bump;
    }

  gimple_ranger::range_on_exit (r, bb, name);

  trailer (idx, "range_on_exit", true, name, r);
}

// Tracing version of range_of_stmt.  Call it with printing wrappers.

bool
trace_ranger::range_of_stmt (irange &r, gimple *s, tree name)
{
  bool res;
  unsigned idx = ++trace_count;
  if (dumping (idx))
    {
      fprintf (dump_file, "range_of_stmt (");
      if (name)
	print_generic_expr (dump_file, name, TDF_SLIM);
      fputs (") at stmt ", dump_file);
      print_gimple_stmt (dump_file, s, 0, TDF_SLIM);
      indent += bump;
    }

  res = gimple_ranger::range_of_stmt (r, s, name);

  return trailer (idx, "range_of_stmt", res, name, r);
}

// Tracing version of range_of_expr.  Call it with printing wrappers.

bool
trace_ranger::range_of_expr (irange &r, tree name, gimple *s)
{
  bool res;
  unsigned idx = ++trace_count;
  if (dumping (idx))
    {
      fprintf (dump_file, "range_of_expr(");
      print_generic_expr (dump_file, name, TDF_SLIM);
      fputs (")", dump_file);
      if (s)
	{
	  fputs (" at stmt ", dump_file);
	  print_gimple_stmt (dump_file, s, 0, TDF_SLIM);
	}
      else
	fputs ("\n", dump_file);
      indent += bump;
    }

  res = gimple_ranger::range_of_expr (r, name, s);

  return trailer (idx, "range_of_expr", res, name, r);
}

gimple_ranger *
enable_ranger (struct function *fun)
{
  gimple_ranger *r;

  if (param_evrp_mode & EVRP_MODE_TRACE)
    r = new trace_ranger;
  else
    r = new gimple_ranger;

  fun->x_range_query = r;

  return r;
}

void
disable_ranger (struct function *fun)
{
  delete fun->x_range_query;

  fun->x_range_query = &global_ranges;
}

// =========================================
// Debugging helpers.
// =========================================

// Query all statements in the IL to precalculate computable ranges in RANGER.

static DEBUG_FUNCTION void
debug_seed_ranger (gimple_ranger &ranger)
{
  // Recalculate SCEV to make sure the dump lists everything.
  if (scev_initialized_p ())
    {
      scev_finalize ();
      scev_initialize ();
    }

  basic_block bb;
  int_range_max r;
  gimple_stmt_iterator gsi;
  FOR_EACH_BB_FN (bb, cfun)
    for (gsi = gsi_start_bb (bb); !gsi_end_p (gsi); gsi_next (&gsi))
      {
	gimple *stmt = gsi_stmt (gsi);

	if (is_gimple_debug (stmt))
	  continue;

	ranger.range_of_stmt (r, stmt);
      }
}

// Dump all that ranger knows for the current function.

DEBUG_FUNCTION void
dump_ranger (FILE *out)
{
  gimple_ranger ranger;
  debug_seed_ranger (ranger);
  ranger.dump (out);
}

DEBUG_FUNCTION void
debug_ranger ()
{
  dump_ranger (stderr);
}

// Dump all that ranger knows on a path of BBs.
//
// Note that the blocks are in reverse order, thus the exit block is
// path[0].

DEBUG_FUNCTION void
dump_ranger (FILE *dump_file, const vec<basic_block> &path)
{
  if (path.length () == 0)
    {
      fprintf (dump_file, "empty\n");
      return;
    }

  gimple_ranger ranger;
  debug_seed_ranger (ranger);

  unsigned i = path.length ();
  do
    {
      i--;
      ranger.dump_bb (dump_file, path[i]);
    }
  while (i > 0);
}

DEBUG_FUNCTION void
debug_ranger (const vec<basic_block> &path)
{
  dump_ranger (stderr, path);
}

#include "gimple-range-tests.cc"
