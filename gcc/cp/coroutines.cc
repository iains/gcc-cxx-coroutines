/* coroutine-specific state, expansions and tests.

   Copyright (C) 2018-2020 Free Software Foundation, Inc.

 Contributed by Iain Sandoe <iain@sandoe.co.uk> under contract to Facebook.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "target.h"
#include "cp-tree.h"
#include "stringpool.h"
#include "stmt.h"
#include "stor-layout.h"
#include "tree-iterator.h"
#include "tree.h"
#include "gcc-rich-location.h"
#include "hash-map.h"

static tree find_promise_type (tree);
static bool coro_promise_type_found_p (tree, location_t);

/* GCC C++ coroutines implementation.

  The user authors a function that becomes a coroutine (lazily) by
  making use of any of the co_await, co_yield or co_return keywords.

  Unlike a regular function, where the activation record is placed on the
  stack, and is destroyed on function exit, a coroutine has some state that
  persists between calls - the coroutine frame (analogous to a stack frame).

  We transform the user's function into three pieces:
  1. A so-called ramp function, that establishes the coroutine frame and
     begins execution of the coroutine.
  2. An actor function that contains the state machine corresponding to the
     user's suspend/resume structure.
  3. A stub function that calls the actor function in 'destroy' mode.

  The actor function is executed:
   * from "resume point 0" by the ramp.
   * from resume point N ( > 0 ) for handle.resume() calls.
   * from the destroy stub for destroy point N for handle.destroy() calls.

  The functions in this file carry out the necessary analysis of, and
  transforms to, the AST to perform this.

  The C++ coroutine design makes use of some helper functions that are
  authored in a so-called "promise" class provided by the user.

  At parse time (or post substitution) the type of the coroutine promise
  will be determined.  At that point, we can look up the required promise
  class methods and issue diagnostics if they are missing or incorrect.  To
  avoid repeating these actions at code-gen time, we make use of temporary
  'proxy' variables for the coroutine handle and the promise - which will
  eventually be instantiated in the coroutine frame.

  Each of the keywords will expand to a code sequence (although co_yield is
  just syntactic sugar for a co_await).

  We defer the analysis and transformation until template expansion is
  complete so that we have complete types at that time.  */


/* The state that we collect during parsing (and template expansion) for
   a coroutine.  */

struct GTY((for_user)) coroutine_info
{
  tree function_decl; /* The original function decl.  */
  tree promise_type; /* The cached promise type for this function.  */
  tree handle_type;  /* The cached coroutine handle for this function.  */
  tree self_h_proxy; /* A handle instance that is used as the proxy for the
			one that will eventually be allocated in the coroutine
			frame.  */
  tree promise_proxy; /* Likewise, a proxy promise instance.  */
  location_t first_coro_keyword; /* The location of the keyword that made this
				    function into a coroutine.  */
};

struct coroutine_info_hasher : ggc_ptr_hash<coroutine_info>
{
  typedef tree compare_type; /* We only compare the function decl.  */
  static inline hashval_t hash (coroutine_info *);
  static inline hashval_t hash (const compare_type &);
  static inline bool equal (coroutine_info *, coroutine_info *);
  static inline bool equal (coroutine_info *, const compare_type &);
};

/* This table holds all the collected coroutine state for coroutines in
   the current translation unit.  */

static GTY (()) hash_table<coroutine_info_hasher> *coroutine_info_table;

/* We will initialise state lazily.  */
static bool coro_initialized = false;

/* Return a hash value for the entry pointed to by INFO.
   The compare type is a tree, but the only trees we are going use are
   function decls.  We use the DECL_UID as the hash value since that is
   stable across PCH.  */

hashval_t
coroutine_info_hasher::hash (coroutine_info *info)
{
  return DECL_UID (info->function_decl);
}

/* Return a hash value for the compare value COMP.  */

hashval_t
coroutine_info_hasher::hash (const compare_type& comp)
{
  return DECL_UID (comp);
}

/* Return true if the entries pointed to by LHS and RHS are for the
   same coroutine.  */

bool
coroutine_info_hasher::equal (coroutine_info *lhs, coroutine_info *rhs)
{
  return lhs->function_decl == rhs->function_decl;
}

bool
coroutine_info_hasher::equal (coroutine_info *lhs, const compare_type& rhs)
{
  return lhs->function_decl == rhs;
}

/* Get the existing coroutine_info for FN_DECL, or insert a new one if the
   entry does not yet exist.  */

coroutine_info *
get_or_insert_coroutine_info (tree fn_decl)
{
  gcc_checking_assert (coroutine_info_table != NULL);

  coroutine_info **slot = coroutine_info_table->find_slot_with_hash
    (fn_decl, coroutine_info_hasher::hash (fn_decl), INSERT);

  if (*slot == NULL)
    {
      *slot = new (ggc_cleared_alloc<coroutine_info> ()) coroutine_info ();
      (*slot)->function_decl = fn_decl;
    }

  return *slot;
}

/* Get the existing coroutine_info for FN_DECL, fail if it doesn't exist.  */

coroutine_info *
get_coroutine_info (tree fn_decl)
{
  gcc_checking_assert (coroutine_info_table != NULL);

  coroutine_info **slot = coroutine_info_table->find_slot_with_hash
    (fn_decl, coroutine_info_hasher::hash (fn_decl), NO_INSERT);
  if (slot)
    return *slot;
  return NULL;
}

/* We will lazily create all the identifiers that are used by coroutines
   on the first attempt to lookup the traits.  */

/* Identifiers that are used by all coroutines.  */

static GTY(()) tree coro_traits_identifier;
static GTY(()) tree coro_handle_identifier;
static GTY(()) tree coro_promise_type_identifier;

/* Required promise method name identifiers.  */

static GTY(()) tree coro_await_transform_identifier;
static GTY(()) tree coro_initial_suspend_identifier;
static GTY(()) tree coro_final_suspend_identifier;
static GTY(()) tree coro_return_void_identifier;
static GTY(()) tree coro_return_value_identifier;
static GTY(()) tree coro_yield_value_identifier;
static GTY(()) tree coro_resume_identifier;
static GTY(()) tree coro_from_address_identifier;
static GTY(()) tree coro_get_return_object_identifier;
static GTY(()) tree coro_gro_on_allocation_fail_identifier;
static GTY(()) tree coro_unhandled_exception_identifier;

/* Awaitable methods.  */

static GTY(()) tree coro_await_ready_identifier;
static GTY(()) tree coro_await_suspend_identifier;
static GTY(()) tree coro_await_resume_identifier;

/* Create the identifiers used by the coroutines library interfaces.  */

static void
coro_init_identifiers ()
{
  coro_traits_identifier = get_identifier ("coroutine_traits");
  coro_handle_identifier = get_identifier ("coroutine_handle");
  coro_promise_type_identifier = get_identifier ("promise_type");

  coro_await_transform_identifier = get_identifier ("await_transform");
  coro_initial_suspend_identifier = get_identifier ("initial_suspend");
  coro_final_suspend_identifier = get_identifier ("final_suspend");
  coro_return_void_identifier = get_identifier ("return_void");
  coro_return_value_identifier = get_identifier ("return_value");
  coro_yield_value_identifier = get_identifier ("yield_value");
  coro_resume_identifier = get_identifier ("resume");
  coro_from_address_identifier = get_identifier ("from_address");
  coro_get_return_object_identifier = get_identifier ("get_return_object");
  coro_gro_on_allocation_fail_identifier =
    get_identifier ("get_return_object_on_allocation_failure");
  coro_unhandled_exception_identifier = get_identifier ("unhandled_exception");

  coro_await_ready_identifier = get_identifier ("await_ready");
  coro_await_suspend_identifier = get_identifier ("await_suspend");
  coro_await_resume_identifier = get_identifier ("await_resume");
}

/* Trees we only need to set up once.  */

static GTY(()) tree coro_traits_templ;
static GTY(()) tree coro_handle_templ;
static GTY(()) tree void_coro_handle_type;

/* ================= Parse, Semantics and Type checking ================= */

/* This initial set of routines are helper for the parsing and template
   expansion phases.

   At the completion of this, we will have completed trees for each of the
   keywords, but making use of proxy variables for the self-handle and the
   promise class instance.  */

/* [coroutine.traits]
   Lookup the coroutine_traits template decl.  */

static tree
find_coro_traits_template_decl (location_t kw)
{
  tree traits_decl = lookup_qualified_name (std_node, coro_traits_identifier,
					    0, true);
  if (traits_decl == NULL_TREE || traits_decl == error_mark_node)
    {
      error_at (kw, "cannot find %<coroutine traits%> template");
      return NULL_TREE;
    }
  else
    return traits_decl;
}

/*  Instantiate Coroutine traits for the function signature.  */

static tree
instantiate_coro_traits (tree fndecl, location_t kw)
{
  /* [coroutine.traits.primary]
     So now build up a type list for the template <typename _R, typename...>.
     The types are the function's arg types and _R is the function return
     type.  */

  tree functyp = TREE_TYPE (fndecl);
  tree arg_node = TYPE_ARG_TYPES (functyp);
  tree argtypes = make_tree_vec (list_length (arg_node)-1);
  unsigned p = 0;

  while (arg_node != NULL_TREE && !VOID_TYPE_P (TREE_VALUE (arg_node)))
    {
      TREE_VEC_ELT (argtypes, p++) = TREE_VALUE (arg_node);
      arg_node = TREE_CHAIN (arg_node);
    }

  tree argtypepack = cxx_make_type (TYPE_ARGUMENT_PACK);
  SET_ARGUMENT_PACK_ARGS (argtypepack, argtypes);

  tree targ = make_tree_vec (2);
  TREE_VEC_ELT (targ, 0) = TREE_TYPE (functyp);
  TREE_VEC_ELT (targ, 1) = argtypepack;

  tree traits_class
    = lookup_template_class (coro_traits_templ, targ,
			     /*in_decl=*/ NULL_TREE,
			     /*context=*/ NULL_TREE /*std_node*/,
			     /*entering scope=*/ false, tf_warning_or_error);

  if (traits_class == error_mark_node || traits_class == NULL_TREE)
    {
      error_at (kw, "cannot instantiate %<coroutine traits%>");
      return NULL_TREE;
    }

  return traits_class;
}

/* [coroutine.handle] */

static tree
find_coro_handle_template_decl (location_t kw)
{
  tree handle_decl = lookup_qualified_name (std_node, coro_handle_identifier,
					    0, true);
  if (handle_decl == NULL_TREE || handle_decl == error_mark_node)
    {
      error_at (kw, "cannot find %<coroutine handle%> template");
      return NULL_TREE;
    }
  else
    return handle_decl;
}

/* Instantiate the handle template for a given promise type.  */

static tree
instantiate_coro_handle_for_promise_type (location_t kw, tree promise_type)
{
  /* So now build up a type list for the template, one entry, the promise.  */
  tree targ = make_tree_vec (1);
  TREE_VEC_ELT (targ, 0) = promise_type;
  tree handle_type
    = lookup_template_class (coro_handle_identifier, targ,
			     /* in_decl */ NULL_TREE,
			     /* context */ std_node,
			     /* entering scope */ false, tf_warning_or_error);

  if (handle_type == error_mark_node)
    {
      error_at (kw, "cannot instantiate a %<coroutine handle%> for"
		" promise type %qT", promise_type);
      return NULL_TREE;
    }

  return handle_type;
}

/* Look for the promise_type in the instantiated traits.  */

static tree
find_promise_type (tree traits_class)
{
  tree promise_type
    = lookup_member (traits_class, coro_promise_type_identifier,
		     /* protect */ 1, /*want_type=*/true, tf_warning_or_error);

  if (promise_type)
    promise_type
      = complete_type_or_else (TREE_TYPE (promise_type), promise_type);

  /* NULL_TREE on fail.  */
  return promise_type;
}

static bool
coro_promise_type_found_p (tree fndecl, location_t loc)
{
  gcc_assert (fndecl != NULL_TREE);

  /* Save the coroutine data on the side to avoid the overhead on every
     function decl.  */

  /* We only need one entry per coroutine in a TU, the assumption here is that
     there are typically not 1000s.  */
  if (!coro_initialized)
    {
      gcc_checking_assert (coroutine_info_table == NULL);
      /* A table to hold the state, per coroutine decl.  */
      coroutine_info_table =
	hash_table<coroutine_info_hasher>::create_ggc (11);
      /* Set up the identifiers we will use.  */
      gcc_checking_assert (coro_traits_identifier == NULL);
      coro_init_identifiers ();
      /* Trees we only need to create once.  */
      /* Coroutine traits template.  */
      coro_traits_templ = find_coro_traits_template_decl (loc);
      gcc_checking_assert (coro_traits_templ != NULL);
      /*  coroutine_handle<> template.  */
      coro_handle_templ = find_coro_handle_template_decl (loc);
      gcc_checking_assert (coro_handle_templ != NULL);
      /*  We can also instantiate the void coroutine_handle<>  */
      void_coro_handle_type =
	instantiate_coro_handle_for_promise_type (loc, NULL_TREE);
      gcc_checking_assert (void_coro_handle_type != NULL);
      coro_initialized = true;
    }

  coroutine_info *coro_info = get_or_insert_coroutine_info (fndecl);
  /* Without this, we cannot really proceed.  */
  gcc_checking_assert (coro_info);

  /* If we don't already have a current promise type, try to look it up.  */
  if (coro_info->promise_type == NULL_TREE)
    {
      /* Get the coroutine traits template class instance for the function
	 signature we have - coroutine_traits <R, ...>  */
      tree templ_class = instantiate_coro_traits (fndecl, loc);

      /* Find the promise type for that.  */
      coro_info->promise_type = find_promise_type (templ_class);

      /* If we don't find it, punt on the rest.  */
      if (coro_info->promise_type == NULL_TREE)
	{
	  error_at (loc, "unable to find the promise type for this coroutine");
	  return false;
	}

      /* Try to find the handle type for the promise.  */
      tree handle_type =
	instantiate_coro_handle_for_promise_type (loc, coro_info->promise_type);
      if (handle_type == NULL_TREE)
	return false;

      /* Complete this, we're going to use it.  */
      coro_info->handle_type = complete_type_or_else (handle_type, fndecl);
      /* Diagnostic would be emitted by complete_type_or_else.  */
      if (coro_info->handle_type == error_mark_node)
	return false;

      /* Build a proxy for a handle to "self" as the param to
	 await_suspend() calls.  */
      coro_info->self_h_proxy
	= build_lang_decl (VAR_DECL, get_identifier ("self_h.proxy"),
			   coro_info->handle_type);

      /* Build a proxy for the promise so that we can perform lookups.  */
      coro_info->promise_proxy
	= build_lang_decl (VAR_DECL, get_identifier ("promise.proxy"),
			   coro_info->promise_type);

      /* Note where we first saw a coroutine keyword.  */
      coro_info->first_coro_keyword = loc;
    }

  return true;
}

/* These functions assumes that the caller has verified that the state for
   the decl has been initialized, we try to minimize work here.  */

static tree
get_coroutine_promise_type (tree decl)
{
  if (coroutine_info *info = get_coroutine_info (decl))
    return info->promise_type;

  return NULL_TREE;
}

static tree
get_coroutine_handle_type (tree decl)
{
  if (coroutine_info *info = get_coroutine_info (decl))
    return info->handle_type;

  return NULL_TREE;
}

static tree
get_coroutine_self_handle_proxy (tree decl)
{
  if (coroutine_info *info = get_coroutine_info (decl))
    return info->self_h_proxy;

  return NULL_TREE;
}

static tree
get_coroutine_promise_proxy (tree decl)
{
  if (coroutine_info *info = get_coroutine_info (decl))
    return info->promise_proxy;

  return NULL_TREE;
}

static tree
lookup_promise_method (tree fndecl, tree member_id, location_t loc,
		       bool musthave)
{
  tree promise = get_coroutine_promise_type (fndecl);
  tree pm_memb
    = lookup_member (promise, member_id,
		     /*protect*/ 1, /*want_type*/ 0, tf_warning_or_error);
  if (musthave && (pm_memb == NULL_TREE || pm_memb == error_mark_node))
    {
      error_at (loc, "no member named %qE in %qT", member_id, promise);
      return error_mark_node;
    }
  return pm_memb;
}

/* Here we check the constraints that are common to all keywords (since the
   presence of a coroutine keyword makes the function into a coroutine).  */

static bool
coro_common_keyword_context_valid_p (tree fndecl, location_t kw_loc,
				     const char *kw_name)
{
  if (fndecl == NULL_TREE)
    {
      error_at (kw_loc, "%qs cannot be used outside a function", kw_name);
      return false;
    }

  /* This is arranged in order of prohibitions in the std.  */
  if (DECL_MAIN_P (fndecl))
    {
      // [basic.start.main] 3. The function main shall not be a coroutine.
      error_at (kw_loc, "%qs cannot be used in the %<main%> function",
		kw_name);
      return false;
    }

  if (DECL_DECLARED_CONSTEXPR_P (fndecl))
    {
      // [dcl.constexpr] 3.3 it shall not be a coroutine.
      error_at (kw_loc, "%qs cannot be used in a %<constexpr%> function",
		kw_name);
      cp_function_chain->invalid_constexpr = true;
      return false;
    }

  if (FNDECL_USED_AUTO (fndecl))
    {
      // [dcl.spec.auto] 15. A function declared with a return type that uses
      // a placeholder type shall not be a coroutine .
      error_at (kw_loc,
		"%qs cannot be used in a function with a deduced return type",
		kw_name);
      return false;
    }

  if (varargs_function_p (fndecl))
    {
      // [dcl.fct.def.coroutine] The parameter-declaration-clause of the
      // coroutine shall not terminate with an ellipsis that is not part
      // of a parameter-declaration.
      error_at (kw_loc,
		"%qs cannot be used in a varargs function", kw_name);
      return false;
    }

  if (DECL_CONSTRUCTOR_P (fndecl))
    {
      // [class.ctor] 7. a constructor shall not be a coroutine.
      error_at (kw_loc, "%qs cannot be used in a constructor", kw_name);
      return false;
    }

  if (DECL_DESTRUCTOR_P (fndecl))
    {
      // [class.dtor] 21. a destructor shall not be a coroutine.
      error_at (kw_loc, "%qs cannot be used in a destructor", kw_name);
      return false;
    }

  return true;
}

/* Here we check the constraints that are not per keyword.  */

static bool
coro_function_valid_p (tree fndecl)
{
  location_t f_loc = DECL_SOURCE_LOCATION (fndecl);

  /* Since we think the function is a coroutine, that implies we parsed
     a keyword that triggered this.  Keywords check promise validity for
     their context and thus the promise type should be known at this point.  */
  gcc_checking_assert (get_coroutine_handle_type (fndecl) != NULL_TREE
		       && get_coroutine_promise_type (fndecl) != NULL_TREE);

  if (current_function_returns_value || current_function_returns_null)
    {
       /* TODO: record or extract positions of returns (and the first coro
	  keyword) so that we can add notes to the diagnostic about where
	  the bad keyword is and what made the function into a coro.  */
      error_at (f_loc, "a %<return%> statement is not allowed in coroutine;"
			" did you mean %<co_return%>?");
      return false;
    }

  return true;
}

enum suspend_point_kind {
  CO_AWAIT_SUSPEND_POINT = 0,
  CO_YIELD_SUSPEND_POINT,
  INITIAL_SUSPEND_POINT,
  FINAL_SUSPEND_POINT
};

/*  This performs [expr.await] bullet 3.3 and validates the interface obtained.
    It is also used to build the initial and final suspend points.

    'a', 'o' and 'e' are used as per the description in the section noted.

    A, the original yield/await expr, is found at source location LOC.

    We will be constructing a CO_AWAIT_EXPR for a suspend point of one of
    the four suspend_point_kind kinds.  This is indicated by SUSPEND_KIND.  */

static tree
build_co_await (location_t loc, tree a, suspend_point_kind suspend_kind)
{
  /* Try and overload of operator co_await, .... */
  tree o;
  if (MAYBE_CLASS_TYPE_P (TREE_TYPE (a)))
    {
      tree overload = NULL_TREE;
      o = build_new_op (loc, CO_AWAIT_EXPR, LOOKUP_NORMAL, a, NULL_TREE,
			NULL_TREE, &overload, tf_warning_or_error);
      /* If no viable functions are found, o is a.  */
      if (!o || o == error_mark_node)
	o = a;
    }
  else
    o = a; /* This is most likely about to fail anyway.  */

  tree o_type = complete_type_or_else (TREE_TYPE (o), o);
  if (TREE_CODE (o_type) != RECORD_TYPE)
    {
      error_at (loc, "awaitable type %qT is not a structure",
		o_type);
      return error_mark_node;
    }

  /* Check for required awaitable members and their types.  */
  tree awrd_meth
    = lookup_member (o_type, coro_await_ready_identifier,
		     /* protect */ 1, /*want_type=*/0, tf_warning_or_error);

  if (!awrd_meth || awrd_meth == error_mark_node)
    return error_mark_node;

  tree awsp_meth
    = lookup_member (o_type, coro_await_suspend_identifier,
		     /* protect */ 1, /*want_type=*/0, tf_warning_or_error);

  if (!awsp_meth || awsp_meth == error_mark_node)
    return error_mark_node;

  /* The type of the co_await is the return type of the awaitable's
     co_resume(), so we need to look that up.  */
  tree awrs_meth
    = lookup_member (o_type, coro_await_resume_identifier,
		     /* protect */ 1, /*want_type=*/0, tf_warning_or_error);

  if (!awrs_meth || awrs_meth == error_mark_node)
    return error_mark_node;

  /* To complete the lookups, we need an instance of 'e' which is built from
     'o' according to [expr.await] 3.4.  However, we don't want to materialize
     'e' here (it might need to be placed in the coroutine frame) so we will
     make a temp placeholder instead. */
  tree e_proxy = build_lang_decl (VAR_DECL, NULL_TREE, o_type);

  /* I suppose we could check that this is contextually convertible to bool.  */
  tree awrd_func = NULL_TREE;
  tree awrd_call
    = build_new_method_call (e_proxy, awrd_meth, NULL, NULL_TREE, LOOKUP_NORMAL,
			     &awrd_func, tf_warning_or_error);

  if (!awrd_func || !awrd_call || awrd_call == error_mark_node)
    return error_mark_node;

  /* The suspend method may return one of three types:
      1. void (no special action needed).
      2. bool (if true, we don't need to suspend).
      3. a coroutine handle, we execute the handle.resume() call.  */
  tree awsp_func = NULL_TREE;
  tree h_proxy = get_coroutine_self_handle_proxy (current_function_decl);
  vec<tree, va_gc> *args = make_tree_vector_single (h_proxy);
  tree awsp_call
    = build_new_method_call (e_proxy, awsp_meth, &args, NULL_TREE,
			     LOOKUP_NORMAL, &awsp_func, tf_warning_or_error);

  release_tree_vector (args);
  if (!awsp_func || !awsp_call || awsp_call == error_mark_node)
    return error_mark_node;

  bool ok = false;
  tree susp_return_type = TREE_TYPE (TREE_TYPE (awsp_func));
  if (same_type_p (susp_return_type, void_type_node))
    ok = true;
  else if (same_type_p (susp_return_type, boolean_type_node))
    ok = true;
  else if (TREE_CODE (susp_return_type) == RECORD_TYPE)
    /* ???: perhaps we should have some way to check that this is actually
	    a coroutine handle type.  */
    ok = true;

  if (!ok)
    {
      error_at (loc, "%<await_suspend%> must return %<void%>, %<bool%> or"
		     " a coroutine handle");
      return error_mark_node;
    }

  /* Finally, the type of e.await_resume() is the co_await's type.  */
  tree awrs_func = NULL_TREE;
  tree awrs_call
    = build_new_method_call (e_proxy, awrs_meth, NULL, NULL_TREE, LOOKUP_NORMAL,
			     &awrs_func, tf_warning_or_error);

  if (!awrs_func || !awrs_call || awrs_call == error_mark_node)
    return error_mark_node;

  /* We now have three call expressions, in terms of the promise, handle and
     'e' proxies.  Save them in the await expression for later expansion.  */

  tree awaiter_calls = make_tree_vec (3);
  TREE_VEC_ELT (awaiter_calls, 0) = awrd_call; /* await_ready().  */
  TREE_VEC_ELT (awaiter_calls, 1) = awsp_call; /* await_suspend().  */
  TREE_VEC_ELT (awaiter_calls, 2) = awrs_call; /* await_resume().  */

  return build5_loc (loc, CO_AWAIT_EXPR, TREE_TYPE (awrs_call), a,
		     e_proxy, o, awaiter_calls,
		     build_int_cst (integer_type_node, (int) suspend_kind));
}

tree
finish_co_await_expr (location_t kw, tree expr)
{
  if (!expr || error_operand_p (expr))
    return error_mark_node;

  if (!coro_common_keyword_context_valid_p (current_function_decl, kw,
					    "co_await"))
    return error_mark_node;

  /* The current function has now become a coroutine, if it wasn't already.  */
  DECL_COROUTINE_P (current_function_decl) = 1;

  if (processing_template_decl)
    {
      if (check_for_bare_parameter_packs (expr))
	return error_mark_node;

      /* If we don't know the promise type, we can't proceed.  */
      tree functype = TREE_TYPE (current_function_decl);
      if (dependent_type_p (functype) || type_dependent_expression_p (expr))
	return build5_loc (kw, CO_AWAIT_EXPR, TREE_TYPE (expr), expr, NULL_TREE,
			   NULL_TREE, NULL_TREE, integer_zero_node);
    }

  /* We must be able to look up the "await_transform" method in the scope of
     the promise type, and obtain its return type.  */
  if (!coro_promise_type_found_p (current_function_decl, kw))
    return error_mark_node;

  /* [expr.await] 3.2
     The incoming cast expression might be transformed by a promise
     'await_transform()'.  */
  tree at_meth
    = lookup_promise_method (current_function_decl,
			     coro_await_transform_identifier, kw,
			     /*musthave=*/ false);
  if (at_meth == error_mark_node)
    return error_mark_node;

  tree a = expr;
  if (at_meth)
    {
      /* try to build a = p.await_transform (e). */
      tree at_fn = NULL_TREE;
      vec<tree, va_gc> *args = make_tree_vector_single (expr);
      a = build_new_method_call (get_coroutine_promise_proxy (
				   current_function_decl),
				 at_meth, &args, NULL_TREE, LOOKUP_NORMAL,
				 &at_fn, tf_warning_or_error);

      /* As I read the section.
	 We saw an await_transform method, so it's mandatory that we replace
	 expr with p.await_transform (expr), therefore if the method call fails
	 (presumably, we don't have suitable arguments) then this part of the
	 process fails.  */
      if (!at_fn || a == error_mark_node)
	return error_mark_node;
    }

  /* Now we want to build co_await a.  */
  tree op = build_co_await (kw, a, CO_AWAIT_SUSPEND_POINT);
  TREE_SIDE_EFFECTS (op) = 1;
  SET_EXPR_LOCATION (op, kw);

  return op;
}

/* Take the EXPR given and attempt to build:
     co_await p.yield_value (expr);
   per [expr.yield] para 1. */

tree
finish_co_yield_expr (location_t kw, tree expr)
{
  if (!expr || error_operand_p (expr))
    return error_mark_node;

  /* Check the general requirements and simple syntax errors.  */
  if (!coro_common_keyword_context_valid_p (current_function_decl, kw,
					    "co_yield"))
    return error_mark_node;

  /* The current function has now become a coroutine, if it wasn't already.  */
  DECL_COROUTINE_P (current_function_decl) = 1;

  if (processing_template_decl)
    {
      if (check_for_bare_parameter_packs (expr))
	return error_mark_node;

      tree functype = TREE_TYPE (current_function_decl);
      /* If we don't know the promise type, we can't proceed.  */
      if (dependent_type_p (functype) || type_dependent_expression_p (expr))
	return build2_loc (kw, CO_YIELD_EXPR, TREE_TYPE (expr), expr,
			   NULL_TREE);
    }

  if (!coro_promise_type_found_p (current_function_decl, kw))
    /* We must be able to look up the "yield_value" method in the scope of
       the promise type, and obtain its return type.  */
    return error_mark_node;

  /* The incoming expr is "e" per [expr.yield] para 1, lookup and build a
     call for p.yield_value(e).  */
  tree y_meth = lookup_promise_method (current_function_decl,
				       coro_yield_value_identifier, kw,
				        /*musthave=*/ true);
  if (!y_meth || y_meth == error_mark_node)
    return error_mark_node;

  tree yield_fn = NULL_TREE;
  vec<tree, va_gc> *args = make_tree_vector_single (expr);
  tree yield_call = build_new_method_call (
    get_coroutine_promise_proxy (current_function_decl), y_meth, &args,
    NULL_TREE, LOOKUP_NORMAL, &yield_fn, tf_warning_or_error);

  if (!yield_fn || yield_call == error_mark_node)
    return error_mark_node;

  /* So now we have the type of p.yield_value (e).
     Now we want to build co_await p.yield_value (e).
     Noting that for co_yield, there is no evaluation of any potential
     promise transform_await().  */

  tree op = build_co_await (kw, yield_call, CO_YIELD_SUSPEND_POINT);

  op = build2_loc (kw, CO_YIELD_EXPR, TREE_TYPE (op), expr, op);
  TREE_SIDE_EFFECTS (op) = 1;

  return op;
}

/* Check that it's valid to have a co_return keyword here.
   If it is, then check and build the p.return_{void(),value(expr)}.
   These are built against the promise proxy, but saved for expand time.  */

tree
finish_co_return_stmt (location_t kw, tree expr)
{
  if (expr == error_mark_node)
    return error_mark_node;

  if (!coro_common_keyword_context_valid_p (current_function_decl, kw,
					    "co_return"))
    return error_mark_node;

  /* The current function has now become a coroutine, if it wasn't
     already.  */
  DECL_COROUTINE_P (current_function_decl) = 1;

  if (processing_template_decl)
    {
      current_function_returns_value = 1;

      if (check_for_bare_parameter_packs (expr))
	return error_mark_node;

      tree functype = TREE_TYPE (current_function_decl);
      /* If we don't know the promise type, we can't proceed, return the
	 expression as it is.  */
      if (dependent_type_p (functype) || type_dependent_expression_p (expr))
	{
	  expr
	    = build2_loc (kw, CO_RETURN_EXPR, void_type_node, expr, NULL_TREE);
	  expr = maybe_cleanup_point_expr_void (expr);
	  expr = add_stmt (expr);
	  return expr;
	}
    }

  if (!coro_promise_type_found_p (current_function_decl, kw))
    return error_mark_node;

  if (error_operand_p (expr))
    return error_mark_node;

  /* Suppress -Wreturn-type for co_return, we need to check indirectly
     whether the promise type has a suitable return_void/return_value.  */
  TREE_NO_WARNING (current_function_decl) = true;

  if (!processing_template_decl && warn_sequence_point)
    verify_sequence_points (expr);

  /* If the promise object doesn't have the correct return call then
     there's a mis-match between the co_return <expr> and this.  */
  tree co_ret_call = NULL_TREE;
  if (expr == NULL_TREE || VOID_TYPE_P (TREE_TYPE (expr)))
    {
      tree crv_meth
	= lookup_promise_method (current_function_decl,
				 coro_return_void_identifier, kw,
				 /*musthave=*/ true);
      if (!crv_meth || crv_meth == error_mark_node)
	return error_mark_node;

      co_ret_call = build_new_method_call (
	get_coroutine_promise_proxy (current_function_decl), crv_meth, NULL,
	NULL_TREE, LOOKUP_NORMAL, NULL, tf_warning_or_error);
    }
  else
    {
      tree crv_meth
	= lookup_promise_method (current_function_decl,
				 coro_return_value_identifier, kw,
				 /*musthave=*/ true);
      if (!crv_meth || crv_meth == error_mark_node)
	return error_mark_node;

      vec<tree, va_gc> *args = make_tree_vector_single (expr);
      co_ret_call = build_new_method_call (
	get_coroutine_promise_proxy (current_function_decl), crv_meth, &args,
	NULL_TREE, LOOKUP_NORMAL, NULL, tf_warning_or_error);
    }

  /* Makes no sense for a co-routine really. */
  if (TREE_THIS_VOLATILE (current_function_decl))
    warning_at (kw, 0,
		"function declared %<noreturn%> has a"
		" %<co_return%> statement");

  if (!co_ret_call || co_ret_call == error_mark_node)
    return error_mark_node;

  expr = build2_loc (kw, CO_RETURN_EXPR, void_type_node, expr, co_ret_call);
  expr = maybe_cleanup_point_expr_void (expr);
  expr = add_stmt (expr);
  return expr;
}

/* We need to validate the arguments to __builtin_coro_promise, since the
   second two must be constant, and the builtins machinery doesn't seem to
   deal with that properly.  */

tree
coro_validate_builtin_call (tree call, tsubst_flags_t)
{
  tree fn = TREE_OPERAND (CALL_EXPR_FN (call), 0);

  gcc_checking_assert (DECL_BUILT_IN_CLASS (fn) == BUILT_IN_NORMAL);
  switch (DECL_FUNCTION_CODE (fn))
    {
    default:
      return call;

    case BUILT_IN_CORO_PROMISE:
      {
	/* Argument 0 is already checked by the normal built-in machinery
	   Argument 1 must be a constant of size type.  It probably makes
	   little sense if it's not a power of 2, but that isn't specified
	   formally.  */
	tree arg = CALL_EXPR_ARG (call, 1);
	location_t loc = EXPR_LOCATION (arg);

	/* We expect alignof expressions in templates.  */
	if (TREE_CODE (arg) == NON_DEPENDENT_EXPR
	    && TREE_CODE (TREE_OPERAND (arg, 0)) == ALIGNOF_EXPR)
	  ;
	else if (!TREE_CONSTANT (arg))
	  {
	    error_at (loc, "the align argument to %<__builtin_coro_promise%>"
			   " must be a constant");
	    return error_mark_node;
	  }
	/* Argument 2 is the direction - to / from handle address to promise
	   address.  */
	arg = CALL_EXPR_ARG (call, 2);
	loc = EXPR_LOCATION (arg);
	if (!TREE_CONSTANT (arg))
	  {
	    error_at (loc, "the direction argument to"
			   " %<__builtin_coro_promise%> must be a constant");
	    return error_mark_node;
	  }
	return call;
	break;
      }
    }
}

/* ================= Morph and Expand. =================

   The entry point here is morph_fn_to_coro () which is called from
   finish_function () when we have completed any template expansion.

   This is preceded by helper functions that implement the phases below.

   The process proceeds in four phases.

   A Initial framing.
     The user's function body is wrapped in the initial and final suspend
     points and we begin building the coroutine frame.
     We build empty decls for the actor and destroyer functions at this
     time too.
     When exceptions are enabled, the user's function body will also be
     wrapped in a try-catch block with the catch invoking the promise
     class 'unhandled_exception' method.

   B Analysis.
     The user's function body is analyzed to determine the suspend points,
     if any, and to capture local variables that might persist across such
     suspensions.  In most cases, it is not necessary to capture compiler
     temporaries, since the tree-lowering nests the suspensions correctly.
     However, in the case of a captured reference, there is a lifetime
     extension to the end of the full expression - which can mean across a
     suspend point in which case it must be promoted to a frame variable.

     At the conclusion of analysis, we have a conservative frame layout and
     maps of the local variables to their frame entry points.

   C Build the ramp function.
     Carry out the allocation for the coroutine frame (NOTE; the actual size
     computation is deferred until late in the middle end to allow for future
     optimizations that will be allowed to elide unused frame entries).
     We build the return object.

   D Build and expand the actor and destroyer function bodies.
     The destroyer is a trivial shim that sets a bit to indicate that the
     destroy dispatcher should be used and then calls into the actor.

     The actor function is the implementation of the user's state machine.
     The current suspend point is noted in an index.
     Each suspend point is encoded as a pair of internal functions, one in
     the relevant dispatcher, and one representing the suspend point.

     During this process, the user's local variables and the proxies for the
     self-handle and the promise class instance are re-written to their
     coroutine frame equivalents.

     The complete bodies for the ramp, actor and destroy function are passed
     back to finish_function for folding and gimplification.  */

/* Helpers to build EXPR_STMT and void-cast EXPR_STMT, common ops.  */

static tree
coro_build_expr_stmt (tree expr, location_t loc)
{
  return maybe_cleanup_point_expr_void (build_stmt (loc, EXPR_STMT, expr));
}

static tree
coro_build_cvt_void_expr_stmt (tree expr, location_t loc)
{
  tree t = build1 (CONVERT_EXPR, void_type_node, expr);
  return coro_build_expr_stmt (t, loc);
}

/* Helpers for label creation:
   1. Create a named label in the specified context.  */

static tree
create_anon_label_with_ctx (location_t loc, tree ctx)
{
  tree lab = build_decl (loc, LABEL_DECL, NULL_TREE, void_type_node);

  DECL_CONTEXT (lab) = ctx;
  DECL_ARTIFICIAL (lab) = true;
  DECL_IGNORED_P (lab) = true;
  TREE_USED (lab) = true;
  return lab;
}

/*  2. Create a named label in the specified context.  */

static tree
create_named_label_with_ctx (location_t loc, const char *name, tree ctx)
{
  tree lab_id = get_identifier (name);
  tree lab = define_label (loc, lab_id);
  DECL_CONTEXT (lab) = ctx;
  DECL_ARTIFICIAL (lab) = true;
  TREE_USED (lab) = true;
  return lab;
}

struct proxy_replace
{
  tree from, to;
};

static tree
replace_proxy (tree *here, int *do_subtree, void *d)
{
  proxy_replace *data = (proxy_replace *) d;

  if (*here == data->from)
    {
      *here = data->to;
      *do_subtree = 0;
    }
  else
    *do_subtree = 1;
  return NULL_TREE;
}

/* Support for expansion of co_return statements.  */

struct coro_ret_data
{
  tree promise_proxy;
  tree real_promise;
  tree fs_label;
};

/* If this is a coreturn statement (or one wrapped in a cleanup) then
   return the list of statements to replace it.  */

static tree
coro_maybe_expand_co_return (tree co_ret_expr, coro_ret_data *data)
{
  /* Look inside <(void) (expr)> cleanup */
  if (TREE_CODE (co_ret_expr) == CLEANUP_POINT_EXPR)
    co_ret_expr = TREE_OPERAND (co_ret_expr, 0);

  if (TREE_CODE (co_ret_expr) != CO_RETURN_EXPR)
    return NULL_TREE;

  location_t loc = EXPR_LOCATION (co_ret_expr);
  tree expr = TREE_OPERAND (co_ret_expr, 0);
  tree call = TREE_OPERAND (co_ret_expr, 1);
  tree stmt_list = NULL;
  if (expr && VOID_TYPE_P (TREE_TYPE (expr)))
    {
       /* [stmt.return.coroutine], 2.2 
	  If expr is present and void, it is placed immediately before
	  the call for return_void;  */
      expr = maybe_cleanup_point_expr_void (expr);
      append_to_statement_list (expr, &stmt_list);
    }

  /* Now replace the promise proxy with its real value.  */
  proxy_replace p_data;
  p_data.from = data->promise_proxy;
  p_data.to = data->real_promise;
  cp_walk_tree (&call, replace_proxy, &p_data, NULL);

  /* The types of p.return_void and p.return_value are not explicitly stated
     at least in n4835, it is expected that they will return void.  */
  call = maybe_cleanup_point_expr_void (call);
  append_to_statement_list (call, &stmt_list);
  tree r = build1_loc (loc, GOTO_EXPR, void_type_node, data->fs_label);
  append_to_statement_list (r, &stmt_list);
  return stmt_list;
}

/* Callback that rewrites co_return as per [stmt.return.coroutine]
   - for co_return;
   { p.return_void (); goto final_suspend; }
   - for co_return [void expr];
   { expr; p.return_void(); goto final_suspend;}
   - for co_return [non void expr];
   { p.return_value(expr); goto final_suspend; }  */

static tree
co_return_expander (tree *stmt, int *do_subtree, void *d)
{
  coro_ret_data *data = (coro_ret_data *) d;

  /* To avoid nesting statement lists, walk them and insert as needed.  */
  if (TREE_CODE (*stmt) == STATEMENT_LIST)
    {
      tree_stmt_iterator i;
      for (i = tsi_start (*stmt); !tsi_end_p (i); tsi_next (&i))
	{
	  tree *new_stmt = tsi_stmt_ptr (i);
	  tree replace = coro_maybe_expand_co_return (*new_stmt, data);
	  /* If we got something, it will be list and we want to splice
	     it in.  */
	  if (replace != NULL_TREE)
	    {
	      /* Splice it in ... */
	      tsi_link_before (&i, replace, TSI_SAME_STMT);
	      /* ... and delete what we expanded.  */
	      tsi_delink (&i);
	      /* Maybe, even likely, we replaced the last in the list.  */
	      if (tsi_end_p (i))
		break;
	    }
	  else /* Continue the walk.  */
	    cp_walk_tree (new_stmt, co_return_expander, d, NULL);
	}
      *do_subtree = 0; /* Done subtrees.  */
    }
  else
    {
      /* We might have a single co_return statement, in which case, we do
	 have to replace it with a list.  */
      tree replace = coro_maybe_expand_co_return (*stmt, data);
      if (replace != NULL_TREE)
	{
	  *stmt = replace;
	  *do_subtree = 0; /* Done here.  */
	}
    }
  return NULL_TREE;
}

/* Walk the original function body, rewriting co_returns.  */

static tree
expand_co_returns (tree *fnbody, tree promise_proxy, tree promise,
		   tree fs_label)
{
  coro_ret_data data = {promise_proxy, promise, fs_label};
  cp_walk_tree (fnbody, co_return_expander, &data, NULL);
  return *fnbody;
}

/* Support for expansion of co_await statements.  */

struct coro_aw_data
{
  tree actor_fn;   /* Decl for context.  */
  tree coro_fp;    /* Frame pointer var.  */
  tree resume_idx; /* This is the index var in the frame.  */
  tree self_h;     /* This is a handle to the current coro (frame var).  */
  tree cleanup;    /* This is where to go once we complete local destroy.  */
  tree cororet;    /* This is where to go if we suspend.  */
  unsigned index;  /* This is our current resume index.  */
};

static tree
co_await_find_in_subtree (tree *stmt, int *do_subtree ATTRIBUTE_UNUSED, void *d)
{
  tree **p = (tree **) d;
  if (TREE_CODE (*stmt) == CO_AWAIT_EXPR)
    {
      *p = stmt;
      return *stmt;
    }
  return NULL_TREE;
}

/* When we come here:
    the first operand is the [currently unused] handle for suspend.
    the second operand is the var to be copy-initialized
    the third operand is 'o' (the initializer for the second)
			      as defined in [await.expr] (3.3)
    the fourth operand is the mode as per the comment on build_co_await ().

   When we leave:
   the IFN_CO_YIELD carries the labels of the resume and destroy
   branch targets for this await.  */

static tree
co_await_expander (tree *stmt, int * /*do_subtree*/, void *d)
{
  if (STATEMENT_CLASS_P (*stmt) || !EXPR_P (*stmt))
    return NULL_TREE;

  coro_aw_data *data = (coro_aw_data *) d;

  enum tree_code stmt_code = TREE_CODE (*stmt);
  tree stripped_stmt = *stmt;

  /* Look inside <(void) (expr)> cleanup */
  if (stmt_code == CLEANUP_POINT_EXPR)
    {
      stripped_stmt = TREE_OPERAND (*stmt, 0);
      stmt_code = TREE_CODE (stripped_stmt);
      if (stmt_code == EXPR_STMT
	  && (TREE_CODE (EXPR_STMT_EXPR (stripped_stmt)) == CONVERT_EXPR
	      || TREE_CODE (EXPR_STMT_EXPR (stripped_stmt)) == CAST_EXPR)
	  && VOID_TYPE_P (TREE_TYPE (EXPR_STMT_EXPR (stripped_stmt))))
	{
	  stripped_stmt = TREE_OPERAND (EXPR_STMT_EXPR (stripped_stmt), 0);
	  stmt_code = TREE_CODE (stripped_stmt);
	}
    }

  tree *buried_stmt = NULL;
  tree saved_co_await = NULL_TREE;
  enum tree_code sub_code = NOP_EXPR;

  if (stmt_code == EXPR_STMT
      && TREE_CODE (EXPR_STMT_EXPR (stripped_stmt)) == CO_AWAIT_EXPR)
    saved_co_await
      = EXPR_STMT_EXPR (stripped_stmt); /* hopefully, a void exp.  */
  else if (stmt_code == MODIFY_EXPR || stmt_code == INIT_EXPR)
    {
      sub_code = TREE_CODE (TREE_OPERAND (stripped_stmt, 1));
      if (sub_code == CO_AWAIT_EXPR)
	saved_co_await = TREE_OPERAND (stripped_stmt, 1); /* Get the RHS.  */
      else if (tree r
	       = cp_walk_tree (&TREE_OPERAND (stripped_stmt, 1),
			       co_await_find_in_subtree, &buried_stmt, NULL))
	saved_co_await = r;
    }
  else if (stmt_code == CALL_EXPR)
    {
      if (tree r = cp_walk_tree (&stripped_stmt, co_await_find_in_subtree,
				 &buried_stmt, NULL))
	saved_co_await = r;
    }

  if (!saved_co_await)
    return NULL_TREE;

  /* We want to splice in the await_resume() value in some cases.  */
  tree saved_statement = *stmt;

  tree actor = data->actor_fn;
  location_t loc = EXPR_LOCATION (*stmt);
  tree sv_handle = TREE_OPERAND (saved_co_await, 0);
  tree var = TREE_OPERAND (saved_co_await, 1);  /* frame slot. */
  tree expr = TREE_OPERAND (saved_co_await, 2); /* initializer.  */
  tree awaiter_calls = TREE_OPERAND (saved_co_await, 3);

  tree source = TREE_OPERAND (saved_co_await, 4);
  bool is_final = (source
		   && TREE_INT_CST_LOW (source) == (int) FINAL_SUSPEND_POINT);
  bool needs_dtor = TYPE_HAS_NONTRIVIAL_DESTRUCTOR (TREE_TYPE (var));
  int resume_point = data->index;
  size_t bufsize = sizeof ("destroy.") + 10;
  char *buf = (char *) alloca (bufsize);
  snprintf (buf, bufsize, "destroy.%d", resume_point);
  tree destroy_label = create_named_label_with_ctx (loc, buf, actor);
  snprintf (buf, bufsize, "resume.%d", resume_point);
  tree resume_label = create_named_label_with_ctx (loc, buf, actor);
  tree empty_list = build_empty_stmt (loc);

  tree dtor = NULL_TREE;
  tree await_type = TREE_TYPE (var);
  if (needs_dtor)
    dtor = build_special_member_call (var, complete_dtor_identifier, NULL,
				      await_type, LOOKUP_NORMAL,
				      tf_warning_or_error);

  tree stmt_list = NULL;
  /* Initialize the var from the provided 'o' expression.  */
  tree r = build2 (INIT_EXPR, await_type, var, expr);
  r = coro_build_cvt_void_expr_stmt (r, loc);
  append_to_statement_list (r, &stmt_list);

  /* Use the await_ready() call to test if we need to suspend.  */
  tree ready_cond = TREE_VEC_ELT (awaiter_calls, 0); /* await_ready().  */
  ready_cond = build1_loc (loc, TRUTH_NOT_EXPR, boolean_type_node, ready_cond);
  ready_cond
    = build1_loc (loc, CLEANUP_POINT_EXPR, boolean_type_node, ready_cond);

  tree body_list = NULL;
  tree susp_idx = build_int_cst (short_unsigned_type_node, data->index);
  r = build2_loc (loc, MODIFY_EXPR, short_unsigned_type_node, data->resume_idx,
		  susp_idx);
  r = coro_build_cvt_void_expr_stmt (r, loc);
  append_to_statement_list (r, &body_list);

  tree suspend = TREE_VEC_ELT (awaiter_calls, 1); /* await_suspend().  */

  if (sv_handle == NULL_TREE)
    {
      /* void return, we just call it and hit the yield.  */
      suspend = coro_build_cvt_void_expr_stmt (suspend, loc);
      append_to_statement_list (suspend, &body_list);
    }
  else if (sv_handle == boolean_type_node)
    {
      /* Boolean return, continue if the call returns false.  */
      suspend = build1_loc (loc, TRUTH_NOT_EXPR, boolean_type_node, suspend);
      suspend
	= build1_loc (loc, CLEANUP_POINT_EXPR, boolean_type_node, suspend);
      tree go_on = build1_loc (loc, GOTO_EXPR, void_type_node, resume_label);
      r = build3_loc (loc, COND_EXPR, void_type_node, suspend, go_on,
		      empty_list);
      append_to_statement_list (r, &body_list);
    }
  else
    {
      r = build2_loc (loc, INIT_EXPR, TREE_TYPE (sv_handle), sv_handle,
		      suspend);
      append_to_statement_list (r, &body_list);
      tree resume
	= lookup_member (TREE_TYPE (sv_handle), coro_resume_identifier, 1, 0,
			 tf_warning_or_error);
      resume = build_new_method_call (sv_handle, resume, NULL, NULL_TREE,
				      LOOKUP_NORMAL, NULL, tf_warning_or_error);
      resume = coro_build_cvt_void_expr_stmt (resume, loc);
      append_to_statement_list (resume, &body_list);
    }

  tree d_l
    = build1 (ADDR_EXPR, build_reference_type (void_type_node), destroy_label);
  tree r_l
    = build1 (ADDR_EXPR, build_reference_type (void_type_node), resume_label);
  tree susp
    = build1 (ADDR_EXPR, build_reference_type (void_type_node), data->cororet);
  tree final_susp = build_int_cst (integer_type_node, is_final ? 1 : 0);

  susp_idx = build_int_cst (integer_type_node, data->index);

  tree sw = begin_switch_stmt ();
  tree cond = build_decl (loc, VAR_DECL, NULL_TREE, integer_type_node);
  DECL_ARTIFICIAL (cond) = 1;
  DECL_IGNORED_P (cond) = 1;
  layout_decl (cond, 0);

  r = build_call_expr_internal_loc (loc, IFN_CO_YIELD, integer_type_node, 5,
				    susp_idx, final_susp, r_l, d_l,
				    data->coro_fp);
  r = build2 (INIT_EXPR, integer_type_node, cond, r);
  finish_switch_cond (r, sw);
  r = build_case_label (build_int_cst (integer_type_node, 0), NULL_TREE,
			create_anon_label_with_ctx (loc, actor));
  add_stmt (r); /* case 0: */
  /* Implement the suspend, a scope exit without clean ups.  */
  r = build_call_expr_internal_loc (loc, IFN_CO_SUSPN, void_type_node, 1, susp);
  r = coro_build_cvt_void_expr_stmt (r, loc);
  add_stmt (r); //   goto ret;
  r = build_case_label (build_int_cst (integer_type_node, 1), NULL_TREE,
			create_anon_label_with_ctx (loc, actor));
  add_stmt (r); // case 1:
  r = build1_loc (loc, GOTO_EXPR, void_type_node, resume_label);
  add_stmt (r); //  goto resume;
  r = build_case_label (NULL_TREE, NULL_TREE,
			create_anon_label_with_ctx (loc, actor));
  add_stmt (r); // default:;
  r = build1_loc (loc, GOTO_EXPR, void_type_node, destroy_label);
  add_stmt (r); // goto destroy;

  /* part of finish switch.  */
  SWITCH_STMT_BODY (sw) = pop_stmt_list (SWITCH_STMT_BODY (sw));
  pop_switch ();
  tree scope = SWITCH_STMT_SCOPE (sw);
  SWITCH_STMT_SCOPE (sw) = NULL;
  r = do_poplevel (scope);
  append_to_statement_list (r, &body_list);

  destroy_label = build_stmt (loc, LABEL_EXPR, destroy_label);
  append_to_statement_list (destroy_label, &body_list);
  if (needs_dtor)
    append_to_statement_list (dtor, &body_list);
  r = build1_loc (loc, GOTO_EXPR, void_type_node, data->cleanup);
  append_to_statement_list (r, &body_list);

  r = build3_loc (loc, COND_EXPR, void_type_node, ready_cond, body_list,
		  empty_list);

  append_to_statement_list (r, &stmt_list);

  /* Resume point.  */
  resume_label = build_stmt (loc, LABEL_EXPR, resume_label);
  append_to_statement_list (resume_label, &stmt_list);

  /* This will produce the value (if one is provided) from the co_await
     expression.  */
  tree resume_call = TREE_VEC_ELT (awaiter_calls, 2); /* await_resume().  */
  switch (stmt_code)
    {
    default: /* not likely to work .. but... */
      append_to_statement_list (resume_call, &stmt_list);
      break;
    case INIT_EXPR:
    case MODIFY_EXPR:
    case CALL_EXPR:
      /* Replace the use of co_await by the resume expr.  */
      if (sub_code == CO_AWAIT_EXPR)
	{
	  /* We're updating the interior of a possibly <(void) expr>cleanup.  */
	  TREE_OPERAND (stripped_stmt, 1) = resume_call;
	  append_to_statement_list (saved_statement, &stmt_list);
	}
      else if (buried_stmt != NULL)
	{
	  *buried_stmt = resume_call;
	  append_to_statement_list (saved_statement, &stmt_list);
	}
      else
	{
	  error_at (loc, "failed to substitute the resume method in %qE",
		    saved_statement);
	  append_to_statement_list (saved_statement, &stmt_list);
	}
      break;
    }
  if (needs_dtor)
    append_to_statement_list (dtor, &stmt_list);
  data->index += 2;
  *stmt = stmt_list;
  return NULL_TREE;
}

static tree
expand_co_awaits (tree fn, tree *fnbody, tree coro_fp, tree resume_idx,
		  tree cleanup, tree cororet, tree self_h)
{
  coro_aw_data data
    = {fn, coro_fp, resume_idx, self_h, cleanup, cororet, 2};
  cp_walk_tree (fnbody, co_await_expander, &data, NULL);
  return *fnbody;
}

/* Suspend point hash_map.  */

struct suspend_point_info
{
  /* coro frame field type.  */
  tree awaitable_type;
  /* coro frame field name.  */
  tree await_field_id;
  /* suspend method return type.  */
  tree suspend_type;
  /* suspend handle field name, NULL_TREE if not needed.  */
  tree susp_handle_id;
};

static hash_map<tree, suspend_point_info> *suspend_points;

struct await_xform_data
{
  tree actor_frame;
  tree promise_proxy;
  tree real_promise;
  tree self_h_proxy;
  tree real_self_h;
};

/* When we built the await expressions, we didn't know the coro frame
   layout, therefore no idea where to find the promise or where to put
   the awaitables.  Now we know these things, fill them in.  */

static tree
transform_await_expr (tree await_expr, await_xform_data *xform)
{
  suspend_point_info *si = suspend_points->get (await_expr);
  location_t loc = EXPR_LOCATION (await_expr);
  if (!si)
    {
      error_at (loc, "no suspend point info for %qD", await_expr);
      return error_mark_node;
    }

  /* So, on entry, we have:
     in : CO_AWAIT_EXPR (a, e_proxy, o, awr_call_vector, mode)
	  We no longer need a [it had diagnostic value, maybe?]
	  We need to replace the promise proxy in all elements
	  We need to replace the e_proxy in the awr_call.
  */

  tree coro_frame_type = TREE_TYPE (xform->actor_frame);
  tree ah = NULL_TREE;
  if (si->susp_handle_id)
    {
      tree ah_m
	= lookup_member (coro_frame_type, si->susp_handle_id,
			 /*protect*/ 1, /*want_type*/ 0, tf_warning_or_error);
      ah = build_class_member_access_expr (xform->actor_frame, ah_m, NULL_TREE,
					   true, tf_warning_or_error);
    }
  else if (TREE_CODE (si->suspend_type) == BOOLEAN_TYPE)
    ah = boolean_type_node;

  /* Replace Op 0 with the frame slot for the temporary handle, if it's needed.
     If there's no frame type to be stored we flag boolean_type for that case
     and an empty pointer for void return.  */
  TREE_OPERAND (await_expr, 0) = ah;

  /* Get a reference to the initial suspend var in the frame.  */
  tree as_m
    = lookup_member (coro_frame_type, si->await_field_id,
		     /*protect*/ 1, /*want_type*/ 0, tf_warning_or_error);
  tree as = build_class_member_access_expr (xform->actor_frame, as_m, NULL_TREE,
					    true, tf_warning_or_error);

  /* Replace references to the instance proxy with the frame entry now
     computed.  */
  proxy_replace data = {TREE_OPERAND (await_expr, 1), as};
  cp_walk_tree (&await_expr, replace_proxy, &data, NULL);

  /* .. and replace.  */
  TREE_OPERAND (await_expr, 1) = as;

  /* Now do the self_handle.  */
  data.from = xform->self_h_proxy;
  data.to = xform->real_self_h;
  cp_walk_tree (&await_expr, replace_proxy, &data, NULL);

  /* Now do the promise.  */
  data.from = xform->promise_proxy;
  data.to = xform->real_promise;
  cp_walk_tree (&await_expr, replace_proxy, &data, NULL);

  return await_expr;
}

/* A wrapper for the transform_await_expr function so that it can be a
   callback from cp_walk_tree.  */

static tree
transform_await_wrapper (tree *stmt, int *do_subtree, void *d)
{
  if (TREE_CODE (*stmt) != CO_AWAIT_EXPR && TREE_CODE (*stmt) != CO_YIELD_EXPR)
    return NULL_TREE;

  tree await_expr = *stmt;
  await_xform_data *xform = (await_xform_data *) d;

  *stmt = transform_await_expr (await_expr, xform);
  if (*stmt == error_mark_node)
    *do_subtree = 0;
  return NULL_TREE;
}

struct param_info
{
  tree field_id;
  vec<tree *> *body_uses;
  tree frame_type;
};

struct local_var_info
{
  tree field_id;
  tree field_idx;
  tree frame_type;
  tree captured;
  location_t def_loc;
};

/* For figuring out what local variable usage we have.  */
struct local_vars_transform
{
  tree context;
  tree actor_frame;
  tree coro_frame_type;
  location_t loc;
  hash_map<tree, local_var_info> *local_var_uses;
};

static tree
transform_local_var_uses (tree *stmt, int *do_subtree, void *d)
{
  local_vars_transform *lvd = (local_vars_transform *) d;

  /* For each var in this bind expr (that has a frame id, which means it was
     accessed), build a frame reference for each and then walk the bind expr
     statements, substituting the frame ref for the original var.  */

  if (TREE_CODE (*stmt) == BIND_EXPR)
    {
      tree lvar;
      for (lvar = BIND_EXPR_VARS (*stmt); lvar != NULL;
	   lvar = DECL_CHAIN (lvar))
	{
	  bool existed;
	  local_var_info &local_var
	    = lvd->local_var_uses->get_or_insert (lvar, &existed);
	  gcc_checking_assert (existed);

	  /* Re-write the variable's context to be in the actor func.  */
	  DECL_CONTEXT (lvar) = lvd->context;

	  /* we need to walk some of the decl trees, which might contain
	     references to vars replaced at a higher level.  */
	  cp_walk_tree (&DECL_INITIAL (lvar), transform_local_var_uses, d,
			NULL);
	  cp_walk_tree (&DECL_SIZE (lvar), transform_local_var_uses, d, NULL);
	  cp_walk_tree (&DECL_SIZE_UNIT (lvar), transform_local_var_uses, d,
			NULL);

	  /* TODO: implement selective generation of fields when vars are
	     known not-used.  */
	  if (local_var.field_id == NULL_TREE)
	    continue; /* Wasn't used.  */

	  tree fld_ref
	    = lookup_member (lvd->coro_frame_type, local_var.field_id,
			     /*protect*/ 1, /*want_type*/ 0,
			     tf_warning_or_error);
	  tree fld_idx = build3_loc (lvd->loc, COMPONENT_REF, TREE_TYPE (lvar),
				     lvd->actor_frame, fld_ref, NULL_TREE);
	  local_var.field_idx = fld_idx;
	}
      cp_walk_tree (&BIND_EXPR_BODY (*stmt), transform_local_var_uses, d, NULL);
      /* Now we have processed and removed references to the original vars,
	 we can drop those from the bind.  */
      for (tree *pvar = &BIND_EXPR_VARS (*stmt); *pvar != NULL;)
	{
	  bool existed;
	  local_var_info &local_var
	    = lvd->local_var_uses->get_or_insert (*pvar, &existed);
	  gcc_checking_assert (existed);

	  if (local_var.field_id == NULL_TREE)
	    pvar = &DECL_CHAIN (*pvar); /* Wasn't used.  */

	  *pvar = DECL_CHAIN (*pvar); // discard this one, we replaced it.
	}

      *do_subtree = 0; /* We've done the body already.  */
      return NULL_TREE;
    }

  tree var_decl = *stmt;
  /* Look inside cleanups, we don't want to wrap a statement list in a
     cleanup.  */
  bool needs_cleanup = true;
  if (TREE_CODE (var_decl) == CLEANUP_POINT_EXPR)
    var_decl = TREE_OPERAND (var_decl, 0);
  else
    needs_cleanup = false;

  /* Look inside the decl_expr for the actual var.  */
  bool decl_expr_p = TREE_CODE (var_decl) == DECL_EXPR;
  if (decl_expr_p && TREE_CODE (DECL_EXPR_DECL (var_decl)) == VAR_DECL)
    var_decl = DECL_EXPR_DECL (var_decl);
  else if (TREE_CODE (var_decl) != VAR_DECL)
    return NULL_TREE;

  /* VAR_DECLs that are not recorded can belong to the proxies we've placed
     for the promise and coroutine handle(s), to global vars or to compiler
     temporaries.  Skip past these, we will handle them later.  */
  local_var_info *local_var_i = lvd->local_var_uses->get (var_decl);
  if (local_var_i == NULL)
    return NULL_TREE;

  /* This is our revised 'local' i.e. a frame slot.  */
  tree revised = local_var_i->field_idx;
  gcc_checking_assert (DECL_CONTEXT (var_decl) == lvd->context);

  if (decl_expr_p && DECL_INITIAL (var_decl))
    {
      location_t loc = DECL_SOURCE_LOCATION (var_decl);
      tree r
	= cp_build_modify_expr (loc, revised, INIT_EXPR,
				DECL_INITIAL (var_decl), tf_warning_or_error);
      if (needs_cleanup)
	r = coro_build_cvt_void_expr_stmt (r, EXPR_LOCATION (*stmt));
      *stmt = r;
    }
  else
    *stmt = revised;

  if (decl_expr_p)
    *do_subtree = 0; /* We've accounted for the nested use.  */
  return NULL_TREE;
}

/* The actor transform.  */

static void
build_actor_fn (location_t loc, tree coro_frame_type, tree actor, tree fnbody,
		tree orig, hash_map<tree, param_info> *param_uses,
		hash_map<tree, local_var_info> *local_var_uses,
		vec<tree, va_gc> *param_dtor_list, tree initial_await,
		tree final_await, unsigned body_count)
{
  verify_stmt_tree (fnbody);
  /* Some things we inherit from the original function.  */
  tree coro_frame_ptr = build_pointer_type (coro_frame_type);
  tree handle_type = get_coroutine_handle_type (orig);
  tree self_h_proxy = get_coroutine_self_handle_proxy (orig);
  tree promise_type = get_coroutine_promise_type (orig);
  tree promise_proxy = get_coroutine_promise_proxy (orig);
  tree act_des_fn_type
    = build_function_type_list (void_type_node, coro_frame_ptr, NULL_TREE);
  tree act_des_fn_ptr = build_pointer_type (act_des_fn_type);

  /* One param, the coro frame pointer.  */
  tree actor_fp
    = build_lang_decl (PARM_DECL, get_identifier ("frame_ptr"), coro_frame_ptr);
  DECL_CONTEXT (actor_fp) = actor;
  DECL_ARG_TYPE (actor_fp) = type_passed_as (coro_frame_ptr);
  DECL_ARGUMENTS (actor) = actor_fp;

  /* A void return.  */
  tree resdecl = build_decl (loc, RESULT_DECL, 0, void_type_node);
  DECL_ARTIFICIAL (resdecl) = 1;
  DECL_IGNORED_P (resdecl) = 1;
  DECL_RESULT (actor) = resdecl;
  DECL_COROUTINE_P (actor) = 1;

  /* We have a definition here.  */
  TREE_STATIC (actor) = 1;

  tree actor_outer = push_stmt_list ();
  current_stmt_tree ()->stmts_are_full_exprs_p = 1;
  tree stmt = begin_compound_stmt (BCS_FN_BODY);

  /* ??? Can we dispense with the enclosing bind if the function body does
     not start with a bind_expr? (i.e. there's no contained scopes).  */
  tree actor_bind = build3 (BIND_EXPR, void_type_node, NULL, NULL, NULL);
  tree top_block = make_node (BLOCK);
  BIND_EXPR_BLOCK (actor_bind) = top_block;

  /* Update the block associated with the outer scope of the orig fn.  */
  tree first = expr_first (fnbody);
  if (first && TREE_CODE (first) == BIND_EXPR)
    {
      /* We will discard this, since it's connected to the original scope
	 nest.  */
      tree block = BIND_EXPR_BLOCK (first);
      if (block) // For this to be missing is probably a bug.
	{
	  gcc_assert (BLOCK_SUPERCONTEXT (block) == NULL_TREE);
	  gcc_assert (BLOCK_CHAIN (block) == NULL_TREE);
	  BLOCK_SUPERCONTEXT (block) = top_block;
	  BLOCK_SUBBLOCKS (top_block) = block;
	}
    }

  add_stmt (actor_bind);
  tree actor_body = push_stmt_list ();

  /* The entry point for the actor code from the ramp.  */
  tree actor_begin_label
    = create_named_label_with_ctx (loc, "actor.begin", actor);
  tree actor_frame = build1_loc (loc, INDIRECT_REF, coro_frame_type, actor_fp);

  /* Re-write param references in the body, no code should be generated
     here.  */
  if (DECL_ARGUMENTS (orig) && param_uses != NULL)
    {
      tree arg;
      for (arg = DECL_ARGUMENTS (orig); arg != NULL; arg = DECL_CHAIN (arg))
	{
	  bool existed;
	  param_info &parm = param_uses->get_or_insert (arg, &existed);
	  if (parm.field_id == NULL_TREE)
	    continue; /* Wasn't used.  */
	  tree fld_ref = lookup_member (coro_frame_type, parm.field_id,
					/*protect*/ 1, /*want_type*/ 0,
					tf_warning_or_error);
	  tree fld_idx = build3_loc (loc, COMPONENT_REF, TREE_TYPE (arg),
				     actor_frame, fld_ref, NULL_TREE);
	  int i;
	  tree *puse;
	  FOR_EACH_VEC_ELT (*parm.body_uses, i, puse)
	    {
	      *puse = fld_idx;
	    }
	}
    }

  /* Re-write local vars, similarly.  */
  local_vars_transform xform_vars_data
    = {actor, actor_frame, coro_frame_type, loc, local_var_uses};
  cp_walk_tree (&fnbody, transform_local_var_uses, &xform_vars_data, NULL);

  tree resume_idx_name = get_identifier ("__resume_at");
  tree rat_field = lookup_member (coro_frame_type, resume_idx_name, 1, 0,
				  tf_warning_or_error);
  tree rat = build3 (COMPONENT_REF, short_unsigned_type_node, actor_frame,
		     rat_field, NULL_TREE);

  tree ret_label
    = create_named_label_with_ctx (loc, "actor.suspend.ret", actor);

  tree lsb_if = begin_if_stmt ();
  tree chkb0 = build2 (BIT_AND_EXPR, short_unsigned_type_node, rat,
		       build_int_cst (short_unsigned_type_node, 1));
  chkb0 = build2 (NE_EXPR, short_unsigned_type_node, chkb0,
		  build_int_cst (short_unsigned_type_node, 0));
  finish_if_stmt_cond (chkb0, lsb_if);

  tree destroy_dispatcher = begin_switch_stmt ();
  finish_switch_cond (rat, destroy_dispatcher);
  tree ddeflab = build_case_label (NULL_TREE, NULL_TREE,
				   create_anon_label_with_ctx (loc, actor));
  add_stmt (ddeflab);
  tree b = build_call_expr_loc (loc, builtin_decl_explicit (BUILT_IN_TRAP), 0);
  b = coro_build_cvt_void_expr_stmt (b, loc);
  add_stmt (b);

  short unsigned lab_num = 3;
  for (unsigned destr_pt = 0; destr_pt < body_count + 2; destr_pt++)
    {
      tree l_num = build_int_cst (short_unsigned_type_node, lab_num);
      b = build_case_label (l_num, NULL_TREE,
			    create_anon_label_with_ctx (loc, actor));
      add_stmt (b);
      b = build_call_expr_internal_loc (loc, IFN_CO_ACTOR, void_type_node, 1,
					l_num);
      b = coro_build_cvt_void_expr_stmt (b, loc);
      add_stmt (b);
      b = build1 (GOTO_EXPR, void_type_node, CASE_LABEL (ddeflab));
      add_stmt (b);
      lab_num += 2;
    }

  /* Insert the prototype dispatcher.  */
  finish_switch_stmt (destroy_dispatcher);

  finish_then_clause (lsb_if);

  tree dispatcher = begin_switch_stmt ();
  finish_switch_cond (rat, dispatcher);
  b = build_case_label (build_int_cst (short_unsigned_type_node, 0), NULL_TREE,
			create_anon_label_with_ctx (loc, actor));
  add_stmt (b);
  b = build1 (GOTO_EXPR, void_type_node, actor_begin_label);
  add_stmt (b);

  tree rdeflab = build_case_label (NULL_TREE, NULL_TREE,
				   create_anon_label_with_ctx (loc, actor));
  add_stmt (rdeflab);
  b = build_call_expr_loc (loc, builtin_decl_explicit (BUILT_IN_TRAP), 0);
  b = coro_build_cvt_void_expr_stmt (b, loc);
  add_stmt (b);

  lab_num = 2;
  /* The final resume should be made to hit the default (trap, UB) entry.  */
  for (unsigned resu_pt = 0; resu_pt < body_count + 1; resu_pt++)
    {
      tree l_num = build_int_cst (short_unsigned_type_node, lab_num);
      b = build_case_label (l_num, NULL_TREE,
			    create_anon_label_with_ctx (loc, actor));
      add_stmt (b);
      b = build_call_expr_internal_loc (loc, IFN_CO_ACTOR, void_type_node, 1,
					l_num);
      b = coro_build_cvt_void_expr_stmt (b, loc);
      add_stmt (b);
      b = build1 (GOTO_EXPR, void_type_node, CASE_LABEL (rdeflab));
      add_stmt (b);
      lab_num += 2;
    }

  /* Insert the prototype dispatcher.  */
  finish_switch_stmt (dispatcher);

  finish_if_stmt (lsb_if);

  tree r = build_stmt (loc, LABEL_EXPR, actor_begin_label);
  add_stmt (r);

  /* actor's version of the promise.  */
  tree ap_m = lookup_member (coro_frame_type, get_identifier ("__p"), 1, 0,
			     tf_warning_or_error);
  tree ap = build_class_member_access_expr (actor_frame, ap_m, NULL_TREE, false,
					    tf_warning_or_error);

  /* actor's coroutine 'self handle'.  */
  tree ash_m = lookup_member (coro_frame_type, get_identifier ("__self_h"), 1,
			      0, tf_warning_or_error);
  tree ash = build_class_member_access_expr (actor_frame, ash_m, NULL_TREE,
					     false, tf_warning_or_error);
  /* So construct the self-handle from the frame address.  */
  tree hfa_m = lookup_member (handle_type, coro_from_address_identifier, 1,
			      0, tf_warning_or_error);

  r = build1 (CONVERT_EXPR, build_pointer_type (void_type_node), actor_fp);
  vec<tree, va_gc> *args = make_tree_vector_single (r);
  tree hfa = build_new_method_call (ash, hfa_m, &args, NULL_TREE, LOOKUP_NORMAL,
				    NULL, tf_warning_or_error);
  r = build2 (INIT_EXPR, handle_type, ash, hfa);
  r = coro_build_cvt_void_expr_stmt (r, loc);
  add_stmt (r);
  release_tree_vector (args);

  /* Now we know the real promise, and enough about the frame layout to
     decide where to put things.  */

  await_xform_data xform
    = {actor_frame, promise_proxy, ap, self_h_proxy, ash};

  /* Get a reference to the initial suspend var in the frame.  */
  transform_await_expr (initial_await, &xform);
  r = coro_build_expr_stmt (initial_await, loc);
  add_stmt (r);

  /* Now we've built the promise etc, process fnbody for co_returns.
     We want the call to return_void () below and it has no params so
     we can create it once here.
     Calls to return_value () will have to be checked and created as
     required.  */

  tree return_void = NULL_TREE;
  tree rvm
    = lookup_promise_method (orig, coro_return_void_identifier, loc,
			     /*musthave=*/ false);
  if (rvm && rvm != error_mark_node)
    return_void
      = build_new_method_call (ap, rvm, NULL, NULL_TREE, LOOKUP_NORMAL, NULL,
			       tf_warning_or_error);

  /* co_return branches to the final_suspend label, so declare that now.  */
  tree fs_label = create_named_label_with_ctx (loc, "final.suspend", actor);

  /* Expand co_returns in the saved function body  */
  fnbody = expand_co_returns (&fnbody, promise_proxy, ap, fs_label);

  /* Transform the await expressions in the function body.  Only do each
     await tree once!  */
  hash_set<tree> pset;
  cp_walk_tree (&fnbody, transform_await_wrapper, &xform, &pset);

  /* Add in our function body with the co_returns rewritten to final form.  */
  add_stmt (fnbody);

  /* [stmt.return.coroutine] (2.2 : 3) if p.return_void() is a valid
     expression, flowing off the end of a coroutine is equivalent to
     co_return; otherwise UB.
     We just inject the call to p.return_void() here, and fall through to
     the final_suspend: label (eliding the goto).  If the function body has
     a co_return, then this statement will be unreachable and DCEd.  */
  if (return_void != NULL_TREE)
    add_stmt (return_void);

  /* Final suspend starts here.  */
  r = build_stmt (loc, LABEL_EXPR, fs_label);
  add_stmt (r);

  /* Set the actor pointer to null, so that 'done' will work.
     Resume from here is UB anyway - although a 'ready' await will
     branch to the final resume, and fall through to the destroy.  */
  tree resume_m
    = lookup_member (coro_frame_type, get_identifier ("__resume"),
		     /*protect*/ 1, /*want_type*/ 0, tf_warning_or_error);
  tree res_x = build_class_member_access_expr (actor_frame, resume_m, NULL_TREE,
					       false, tf_warning_or_error);
  r = build1 (CONVERT_EXPR, act_des_fn_ptr, integer_zero_node);
  r = build2 (INIT_EXPR, act_des_fn_ptr, res_x, r);
  r = coro_build_cvt_void_expr_stmt (r, loc);
  add_stmt (r);

  /* Get a reference to the final suspend var in the frame.  */
  transform_await_expr (final_await, &xform);
  r = coro_build_expr_stmt (final_await, loc);
  add_stmt (r);

  /* now do the tail of the function.  */
  tree del_promise_label
    = create_named_label_with_ctx (loc, "coro.delete.promise", actor);
  r = build_stmt (loc, LABEL_EXPR, del_promise_label);
  add_stmt (r);

  /* Destructors for the things we built explicitly.  */
  r = build_special_member_call (ap, complete_dtor_identifier, NULL,
				 promise_type, LOOKUP_NORMAL,
				 tf_warning_or_error);
  add_stmt (r);

  tree del_frame_label
    = create_named_label_with_ctx (loc, "coro.delete.frame", actor);
  r = build_stmt (loc, LABEL_EXPR, del_frame_label);
  add_stmt (r);

  /* Here deallocate the frame (if we allocated it), which we will have at
     present.  */
  tree fnf_m
    = lookup_member (coro_frame_type, get_identifier ("__frame_needs_free"), 1,
		     0, tf_warning_or_error);
  tree fnf2_x = build_class_member_access_expr (actor_frame, fnf_m, NULL_TREE,
						false, tf_warning_or_error);

  tree need_free_if = begin_if_stmt ();
  fnf2_x = build1 (CONVERT_EXPR, integer_type_node, fnf2_x);
  tree cmp = build2 (NE_EXPR, integer_type_node, fnf2_x, integer_zero_node);
  finish_if_stmt_cond (cmp, need_free_if);
  if (param_dtor_list != NULL)
    {
      int i;
      tree pid;
      FOR_EACH_VEC_ELT (*param_dtor_list, i, pid)
	{
	  tree m
	    = lookup_member (coro_frame_type, pid, 1, 0, tf_warning_or_error);
	  tree a = build_class_member_access_expr (actor_frame, m, NULL_TREE,
						   false, tf_warning_or_error);
	  tree t = TREE_TYPE (a);
	  tree dtor;
	  dtor
	    = build_special_member_call (a, complete_dtor_identifier, NULL, t,
					 LOOKUP_NORMAL, tf_warning_or_error);
	  add_stmt (dtor);
	}
    }

  tree delname = ovl_op_identifier (false, DELETE_EXPR);
  tree arg = build1 (CONVERT_EXPR, ptr_type_node, actor_fp);
  vec<tree, va_gc> *arglist = make_tree_vector_single (arg);

  /* The user can (optionally) provide a delete function in the promise
      type, it's not a failure for it to be absent.  */
  tree fns = lookup_promise_method (orig, delname, loc, false);
  tree del_coro_fr = NULL_TREE;
  if (fns && fns != error_mark_node)
    {
      del_coro_fr = lookup_arg_dependent (delname, fns, arglist);
      if (OVL_P (del_coro_fr))
	del_coro_fr = OVL_FIRST (del_coro_fr);
      else
	del_coro_fr = BASELINK_FUNCTIONS (del_coro_fr);

      gcc_checking_assert (DECL_STATIC_FUNCTION_P (del_coro_fr));
      TREE_USED (del_coro_fr) = 1;
      del_coro_fr = build_call_expr_loc_vec (loc, del_coro_fr, arglist);
    }

  /* If that fails, then fall back to the global delete operator.  */
  if (del_coro_fr == NULL_TREE || del_coro_fr == error_mark_node)
    {
      fns =lookup_name_real (delname, 0, 1, /*block_p=*/true, 0, 0);
      del_coro_fr = lookup_arg_dependent (del_coro_fr, fns, arglist);
      del_coro_fr = build_new_function_call (del_coro_fr, &arglist, true);
    }

  del_coro_fr = coro_build_cvt_void_expr_stmt (del_coro_fr, loc);
  add_stmt (del_coro_fr);
  finish_then_clause (need_free_if);
  tree scope = IF_SCOPE (need_free_if);
  IF_SCOPE (need_free_if) = NULL;
  r = do_poplevel (scope);
  add_stmt (r);

  /* done.  */
  r = build_stmt (loc, RETURN_EXPR, NULL);
  TREE_NO_WARNING (r) |= 1; /* We don't want a warning about this.  */
  r = maybe_cleanup_point_expr_void (r);
  add_stmt (r);

  /* This is the suspend return point.  */
  r = build_stmt (loc, LABEL_EXPR, ret_label);
  add_stmt (r);

  r = build_stmt (loc, RETURN_EXPR, NULL);
  TREE_NO_WARNING (r) |= 1; /* We don't want a warning about this.  */
  r = maybe_cleanup_point_expr_void (r);
  add_stmt (r);

  /* We need the resume index to work with.  */
  tree res_idx_m
    = lookup_member (coro_frame_type, resume_idx_name,
		     /*protect*/ 1, /*want_type*/ 0, tf_warning_or_error);
  tree res_idx
    = build_class_member_access_expr (actor_frame, res_idx_m, NULL_TREE, false,
				      tf_warning_or_error);

  /* We've now rewritten the tree and added the initial and final
     co_awaits.  Now pass over the tree and expand the co_awaits.  */
  actor_body = expand_co_awaits (actor, &actor_body, actor_fp, res_idx,
				 del_promise_label, ret_label, ash);

  actor_body = pop_stmt_list (actor_body);
  BIND_EXPR_BODY (actor_bind) = actor_body;

  finish_compound_stmt (stmt);
  DECL_SAVED_TREE (actor) = pop_stmt_list (actor_outer);
  verify_stmt_tree (DECL_SAVED_TREE (actor));
}

/* The prototype 'destroy' function :
   frame->__resume_at |= 1;
   actor (frame);  */

static void
build_destroy_fn (location_t loc, tree coro_frame_type, tree destroy,
		  tree actor)
{
  /* One param, the coro frame pointer.  */
  tree coro_frame_ptr = build_pointer_type (coro_frame_type);
  tree destr_fp
    = build_lang_decl (PARM_DECL, get_identifier ("frame_ptr"), coro_frame_ptr);
  DECL_CONTEXT (destr_fp) = destroy;
  DECL_ARG_TYPE (destr_fp) = type_passed_as (coro_frame_ptr);
  DECL_ARGUMENTS (destroy) = destr_fp;

  /* A void return.  */
  tree resdecl = build_decl (loc, RESULT_DECL, 0, void_type_node);
  DECL_ARTIFICIAL (resdecl) = 1;
  DECL_IGNORED_P (resdecl) = 1;
  DECL_RESULT (destroy) = resdecl;

  /* We have a definition here.  */
  TREE_STATIC (destroy) = 1;
  DECL_COROUTINE_P (destroy) = 1;

  tree destr_outer = push_stmt_list ();
  current_stmt_tree ()->stmts_are_full_exprs_p = 1;
  tree dstr_stmt = begin_compound_stmt (BCS_FN_BODY);

  tree destr_frame = build1 (INDIRECT_REF, coro_frame_type, destr_fp);

  tree resume_idx_name = get_identifier ("__resume_at");
  tree rat_field = lookup_member (coro_frame_type, resume_idx_name, 1, 0,
				  tf_warning_or_error);
  tree rat = build3 (COMPONENT_REF, short_unsigned_type_node, destr_frame,
		     rat_field, NULL_TREE);

  /* _resume_at |= 1 */
  tree dstr_idx = build2 (BIT_IOR_EXPR, short_unsigned_type_node, rat,
			  build_int_cst (short_unsigned_type_node, 1));
  tree r = build2 (MODIFY_EXPR, short_unsigned_type_node, rat, dstr_idx);
  r = coro_build_cvt_void_expr_stmt (r, loc);
  add_stmt (r);

  /* So .. call the actor ..  */
  r = build_call_expr_loc (loc, actor, 1, destr_fp);
  r = coro_build_cvt_void_expr_stmt (r, loc);
  add_stmt (r);

  /* done. */
  r = build_stmt (loc, RETURN_EXPR, NULL);
  r = maybe_cleanup_point_expr_void (r);
  add_stmt (r);

  finish_compound_stmt (dstr_stmt);
  DECL_SAVED_TREE (destroy) = pop_stmt_list (destr_outer);
}

/* Helper that returns an identifier for an appended extension to the
   current un-mangled function name.  */

static tree
get_fn_local_identifier (tree orig, const char *append)
{
  /* Figure out the bits we need to generate names for the outlined things
     For consistency, this needs to behave the same way as
     ASM_FORMAT_PRIVATE_NAME does. */
  tree nm = DECL_NAME (orig);
  const char *sep, *pfx = "";
#ifndef NO_DOT_IN_LABEL
  sep = ".";
#else
#ifndef NO_DOLLAR_IN_LABEL
  sep = "$"
#else
  sep = "_";
  pfx = "__";
#endif
#endif

  char *an;
  if (DECL_ASSEMBLER_NAME (orig))
    an = ACONCAT ((IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (orig)), sep, append,
		   (char *) 0));
  else if (DECL_USE_TEMPLATE (orig) && DECL_TEMPLATE_INFO (orig)
	   && DECL_TI_ARGS (orig))
    {
      tree tpl_args = DECL_TI_ARGS (orig);
      an = ACONCAT ((pfx, IDENTIFIER_POINTER (nm), (char *) 0));
      for (int i = 0; i < TREE_VEC_LENGTH (tpl_args); ++i)
	{
	  tree typ = DECL_NAME (TYPE_NAME (TREE_VEC_ELT (tpl_args, i)));
	  an = ACONCAT ((an, sep, IDENTIFIER_POINTER (typ), (char *) 0));
	}
      an = ACONCAT ((an, sep, append, (char *) 0));
    }
  else
    an = ACONCAT ((pfx, IDENTIFIER_POINTER (nm), sep, append, (char *) 0));

  return get_identifier (an);
}

static tree
build_init_or_final_await (location_t loc, bool is_final)
{
  tree suspend_alt = is_final ? coro_final_suspend_identifier
			      : coro_initial_suspend_identifier;
  tree setup_meth = lookup_promise_method (current_function_decl, suspend_alt,
					   loc, /*musthave=*/ true);
  if (!setup_meth || setup_meth == error_mark_node)
    return error_mark_node;

  tree s_fn = NULL_TREE;
  tree setup_call = build_new_method_call (
    get_coroutine_promise_proxy (current_function_decl), setup_meth, NULL,
    NULL_TREE, LOOKUP_NORMAL, &s_fn, tf_warning_or_error);

  if (!s_fn || setup_call == error_mark_node)
    return error_mark_node;

  /* So build the co_await for this */
  /* For initial/final suspends the call is is "a" per [expr.await] 3.2.  */
  return build_co_await (loc, setup_call, (is_final ? FINAL_SUSPEND_POINT
						    : INITIAL_SUSPEND_POINT));
}

/* Callback to record the essential data for each await point found in the
   function.  */

static bool
register_await_info (tree await_expr, tree aw_type, tree aw_nam, tree susp_type,
		     tree susp_handle_nam)
{
  bool seen;
  suspend_point_info &s
    = suspend_points->get_or_insert (await_expr, &seen);
  if (seen)
    {
      error_at (EXPR_LOCATION (await_expr), "duplicate info for %qE",
		await_expr);
      return false;
    }
  s.awaitable_type = aw_type;
  s.await_field_id = aw_nam;
  s.suspend_type = susp_type;
  s.susp_handle_id = susp_handle_nam;
  return true;
}

/* Small helper for the repetitive task of adding a new field to the coro
   frame type.  */

static tree
coro_make_frame_entry (tree *field_list, const char *name, tree fld_type,
		       location_t loc)
{
  tree id = get_identifier (name);
  tree decl = build_decl (loc, FIELD_DECL, id, fld_type);
  DECL_CHAIN (decl) = *field_list;
  *field_list = decl;
  return id;
}

struct susp_frame_data
{
  tree *field_list;
  tree handle_type;
  hash_set<tree> captured_temps;
  vec<tree, va_gc> *to_replace;
  vec<tree, va_gc> *block_stack;
  unsigned count;
  unsigned saw_awaits;
  bool captures_temporary;
};

/* Helper to return the type of an awaiter's await_suspend() method.
   We start with the result of the build method call, which will be either
   a call expression (void, bool) or a target expressions (handle).  */

static tree
get_await_suspend_return_type (tree aw_expr)
{
  tree susp_fn = TREE_VEC_ELT (TREE_OPERAND (aw_expr, 3), 1);
  if (TREE_CODE (susp_fn) == CALL_EXPR)
    {
      susp_fn = CALL_EXPR_FN (susp_fn);
      if (TREE_CODE (susp_fn) == ADDR_EXPR)
	susp_fn = TREE_OPERAND (susp_fn, 0);
      return TREE_TYPE (TREE_TYPE (susp_fn));
    }
  else if (TREE_CODE (susp_fn) == TARGET_EXPR)
    return TREE_TYPE (susp_fn);
  return TREE_TYPE (susp_fn);
}

/* Walk the sub-tree looking for call expressions that both capture
   references and have compiler-temporaries as parms.  */

static tree
captures_temporary (tree *stmt, int *do_subtree, void *d)
{
  /* Stop recursing if we see an await expression, the subtrees
     of that will be handled when it it processed.  */
  if (TREE_CODE (*stmt) == CO_AWAIT_EXPR || TREE_CODE (*stmt) == CO_YIELD_EXPR)
    {
      *do_subtree = 0;
      return NULL_TREE;
    }

  /* We're only interested in calls.  */
  if (TREE_CODE (*stmt) != CALL_EXPR)
    return NULL_TREE;

  /* Does this call capture references?
     Strip the ADDRESS_EXPR to get the fn decl and inspect it.  */
  tree fn = TREE_OPERAND (CALL_EXPR_FN (*stmt), 0);
  bool is_meth = TREE_CODE (TREE_TYPE (fn)) == METHOD_TYPE;
  tree arg = TYPE_ARG_TYPES (TREE_TYPE (fn));
  unsigned offset = 3;
  for (unsigned anum = 0; arg != NULL; arg = TREE_CHAIN (arg), anum++)
    {
      tree parm_type = TREE_VALUE (arg);
      if (anum == 0 && is_meth && INDIRECT_TYPE_P (parm_type))
	{
	  /* Account for 'this' when the fn is a method.  Unless it
	     belongs to a CTOR or DTOR.  */
	  if (DECL_CONSTRUCTOR_P (fn) || DECL_DESTRUCTOR_P (fn))
	    continue;
	}
      else if (!TYPE_REF_P (parm_type))
	/* If it's not a reference, we don't care.  */
	continue;

      /* Fetch the value presented to the fn.  */
      tree parm = TREE_OPERAND (*stmt, anum + offset);

      while (TREE_CODE (parm) == NOP_EXPR)
	parm = TREE_OPERAND (parm, 0);

      /* We only care if we're taking the addr of a temporary.  */
      if (TREE_CODE (parm) != ADDR_EXPR)
	continue;

      parm = TREE_OPERAND (parm, 0);
      if (TREE_CODE (parm) == VAR_DECL && !DECL_ARTIFICIAL (parm))
	/* This isn't a temporary... */
	continue;

      if (TREE_CODE (parm) == PARM_DECL)
	/* .. nor is this... */
	continue;

      if (TREE_CODE (parm) == TARGET_EXPR)
	{
	  /* We're taking the address of a temporary and using it as a ref.  */
	  tree tvar = TREE_OPERAND (parm, 0);
	  gcc_checking_assert (DECL_ARTIFICIAL (tvar));

	  susp_frame_data *data = (susp_frame_data *) d;
	  data->captures_temporary = true;
	  /* Record this one so we don't duplicate, and on the first
	     occurrence note the target expr to be replaced.  */
	  if (!data->captured_temps.add (tvar))
	    vec_safe_push (data->to_replace, parm);
	  /* Now see if the initializer contains any more cases.  */
	  hash_set<tree> visited;
	  tree res = cp_walk_tree (&TREE_OPERAND (parm, 1),
				   captures_temporary, d, &visited);
	  if (res)
	    return res;
	  /* Otherwise, we're done with sub-trees for this.  */
	}
      else if (TREE_CODE (parm) == CO_AWAIT_EXPR)
	{
	  /* CO_AWAIT expressions behave in a similar manner to target
	     expressions when the await_resume call is contained in one.  */
	  tree awr = TREE_OPERAND (parm, 3); /* call vector.  */
	  awr = TREE_VEC_ELT (awr, 2); /* resume call.  */
	  if (TREE_CODE (awr) == TARGET_EXPR)
	    {
	      tree tvar = TREE_OPERAND (awr, 0);
	      gcc_checking_assert (DECL_ARTIFICIAL (tvar));

	      susp_frame_data *data = (susp_frame_data *) d;
	      data->captures_temporary = true;
	      /* Use this as a place-holder.  */
	      if (!data->captured_temps.add (tvar))
		vec_safe_push (data->to_replace, parm);
	    }
	/* We will walk the sub-trees of this co_await separately.  */
	}
      else
	gcc_unreachable ();
    }
  /* As far as it's necessary, we've walked the subtrees of the call
     expr.  */
  do_subtree = 0;
  return NULL_TREE;
}

/* If this is an await, then register it and decide on what coro
   frame storage is needed.
   If this is a co_yield (which embeds an await), drop the yield
   and record the await (the yield was kept for diagnostics only).  */

static tree
register_awaits (tree *stmt, int *do_subtree ATTRIBUTE_UNUSED, void *d)
{
  susp_frame_data *data = (susp_frame_data *) d;

  if (TREE_CODE (*stmt) != CO_AWAIT_EXPR && TREE_CODE (*stmt) != CO_YIELD_EXPR)
    return NULL_TREE;

  /* co_yield is syntactic sugar, re-write it to co_await.  */
  tree aw_expr = *stmt;
  location_t aw_loc = EXPR_LOCATION (aw_expr); /* location of the co_xxxx.  */
  if (TREE_CODE (aw_expr) == CO_YIELD_EXPR)
    {
      aw_expr = TREE_OPERAND (aw_expr, 1);
      *stmt = aw_expr;
    }

  /* Count how many awaits full expression contains.  This is not the same
     as the counter used for the function-wide await point number.  */
  data->saw_awaits++;

  /* The required field has the same type as the proxy stored in the
      await expr.  */
  tree aw_field_type = TREE_TYPE (TREE_OPERAND (aw_expr, 1));

  size_t bufsize = sizeof ("__aw_s.") + 10;
  char *buf = (char *) alloca (bufsize);
  snprintf (buf, bufsize, "__aw_s.%d", data->count);
  tree aw_field_nam
    = coro_make_frame_entry (data->field_list, buf, aw_field_type, aw_loc);

  /* Find out what we have to do with the awaiter's suspend method (this
     determines if we need somewhere to stash the suspend method's handle).
     Cache the result of this in the suspend point info.
     [expr.await]
     (5.1) If the result of await-ready is false, the coroutine is considered
	   suspended. Then:
     (5.1.1) If the type of await-suspend is std::coroutine_handle<Z>,
	     await-suspend.resume() is evaluated.
     (5.1.2) if the type of await-suspend is bool, await-suspend is evaluated,
	     and the coroutine is resumed if the result is false.
     (5.1.3) Otherwise, await-suspend is evaluated.
  */
  tree susp_typ = get_await_suspend_return_type (aw_expr);
  tree handle_field_nam;
  if (VOID_TYPE_P (susp_typ) || TREE_CODE (susp_typ) == BOOLEAN_TYPE)
    handle_field_nam = NULL_TREE; /* no handle is needed.  */
  else
    {
      snprintf (buf, bufsize, "__aw_h.%u", data->count);
      handle_field_nam
	= coro_make_frame_entry (data->field_list, buf, susp_typ, aw_loc);
    }
  register_await_info (aw_expr, aw_field_type, aw_field_nam, susp_typ,
		       handle_field_nam);

  data->count++; /* Each await suspend context is unique.  */

  /* We now need to know if to take special action on lifetime extension
     of temporaries captured by reference.  This can only happen if such
     a case appears in the initializer for the awaitable.  The callback
     records captured temporaries including subtrees of initializers.  */
  hash_set<tree> visited;
  tree res = cp_walk_tree (&TREE_OPERAND (aw_expr, 2), captures_temporary, d,
			   &visited);
  return res;
}

/* The gimplifier correctly extends the lifetime of temporaries captured
   by reference (per. [class.temporary] (6.9) "A temporary object bound
   to a reference parameter in a function call persists until the completion
   of the full-expression containing the call").  However, that is not
   sufficient to work across a suspension - and we need to promote such
   temporaries to be regular vars that will then get a coro frame slot.
   We don't want to incur the effort of checking for this unless we have
   an await expression in the current full expression.  */

static tree
maybe_promote_captured_temps (tree *stmt, void *d)
{
  susp_frame_data *awpts = (susp_frame_data *) d;
  hash_set<tree> visited;
  awpts->saw_awaits = 0;

  /* When register_awaits sees an await, it walks the initializer for
     that await looking for temporaries captured by reference and notes
     them in awpts->captured_temps.  We only need to take any action
     here if the statement contained any awaits, and any of those had
     temporaries captured by reference in the initializers for their class.
  */

  tree res = cp_walk_tree (stmt, register_awaits, d, &visited);
  if (!res && awpts->saw_awaits > 0 && !awpts->captured_temps.is_empty ())
    {
      location_t sloc = EXPR_LOCATION (*stmt);
      tree aw_bind
	= build3_loc (sloc, BIND_EXPR, void_type_node, NULL, NULL, NULL);
      tree aw_statement_current;
      if (TREE_CODE (*stmt) == CLEANUP_POINT_EXPR)
	aw_statement_current = TREE_OPERAND (*stmt, 0);
      else
	aw_statement_current = *stmt;
      /* Collected the scope vars we need move the temps to regular. */
      tree aw_bind_body = push_stmt_list ();
      tree varlist = NULL_TREE;
      unsigned vnum = 0;
      while (!awpts->to_replace->is_empty ())
	{
	  size_t bufsize = sizeof ("__aw_.tmp.") + 20;
	  char *buf = (char *) alloca (bufsize);
	  snprintf (buf, bufsize, "__aw_%d.tmp.%d", awpts->count, vnum);
	  tree to_replace = awpts->to_replace->pop ();
	  tree orig_temp;
	  if (TREE_CODE (to_replace) == CO_AWAIT_EXPR)
	    {
	      orig_temp = TREE_OPERAND (to_replace, 3);
	      orig_temp = TREE_VEC_ELT (orig_temp, 2);
	      orig_temp = TREE_OPERAND (orig_temp, 0);
	    }
	  else
	    orig_temp = TREE_OPERAND (to_replace, 0);

	  tree var_type = TREE_TYPE (orig_temp);
	  gcc_assert (same_type_p (TREE_TYPE (to_replace), var_type));
	  tree newvar
	    = build_lang_decl (VAR_DECL, get_identifier (buf), var_type);
	  DECL_CONTEXT (newvar) = DECL_CONTEXT (orig_temp);
	  if (DECL_SOURCE_LOCATION (orig_temp))
	    sloc = DECL_SOURCE_LOCATION (orig_temp);
	  DECL_SOURCE_LOCATION (newvar) = sloc;
	  DECL_CHAIN (newvar) = varlist;
	  varlist = newvar;
	  tree stmt
	    = build2_loc (sloc, INIT_EXPR, var_type, newvar, to_replace);
	  stmt = coro_build_cvt_void_expr_stmt (stmt, sloc);
	  add_stmt (stmt);
	  proxy_replace pr = {to_replace, newvar};
	  /* Replace all instances of that temp in the original expr.  */
	  cp_walk_tree (&aw_statement_current, replace_proxy, &pr, NULL);
	}
      /* What's left should be the original statement with any temporaries
	 broken out.  */
      add_stmt (aw_statement_current);
      BIND_EXPR_BODY (aw_bind) = pop_stmt_list (aw_bind_body);
      awpts->captured_temps.empty ();

      BIND_EXPR_VARS (aw_bind) = nreverse (varlist);
      tree b_block = make_node (BLOCK);
      if (!awpts->block_stack->is_empty ())
	{
	  tree s_block = awpts->block_stack->last ();
	  if (s_block)
	    {
	      BLOCK_SUPERCONTEXT (b_block) = s_block;
	      BLOCK_CHAIN (b_block) = BLOCK_SUBBLOCKS (s_block);
	      BLOCK_SUBBLOCKS (s_block) = b_block;
	    }
	}
      BIND_EXPR_BLOCK (aw_bind) = b_block;

      *stmt = aw_bind;
    }
  return res;
}

static tree
await_statement_walker (tree *stmt, int *do_subtree, void *d)
{
  tree res = NULL_TREE;
  susp_frame_data *awpts = (susp_frame_data *) d;

  /* We might need to insert a new bind expression, and want to link it
     into the correct scope, so keep a note of the current block scope.  */
  if (TREE_CODE (*stmt) == BIND_EXPR)
    {
      tree *body = &BIND_EXPR_BODY (*stmt);
      tree blk = BIND_EXPR_BLOCK (*stmt);
      vec_safe_push (awpts->block_stack, blk);

      if (TREE_CODE (*body) == STATEMENT_LIST)
	{
	  tree_stmt_iterator i;
	  for (i = tsi_start (*body); !tsi_end_p (i); tsi_next (&i))
	    {
	      tree *new_stmt = tsi_stmt_ptr (i);
	      if (STATEMENT_CLASS_P (*new_stmt) || !EXPR_P (*new_stmt)
		  || TREE_CODE (*new_stmt) == BIND_EXPR)
		res = cp_walk_tree (new_stmt, await_statement_walker, d, NULL);
	      else
		res = maybe_promote_captured_temps (new_stmt, d);
	      if (res)
		return res;
	    }
	  *do_subtree = 0; /* Done subtrees.  */
	}
      else if (!STATEMENT_CLASS_P (*body) && EXPR_P (*body)
	       && TREE_CODE (*body) != BIND_EXPR)
	{
	  res = maybe_promote_captured_temps (body, d);
	  *do_subtree = 0; /* Done subtrees.  */
	}
      awpts->block_stack->pop ();
    }
  else if (!STATEMENT_CLASS_P (*stmt) && EXPR_P (*stmt)
	   && TREE_CODE (*stmt) != BIND_EXPR)
    {
      res = maybe_promote_captured_temps (stmt, d);
      *do_subtree = 0; /* Done subtrees.  */
    }
  /* If it wasn't a statement list, or a single statement, continue.  */
  return res;
}

/* For figuring out what param usage we have.  */

struct param_frame_data
{
  tree *field_list;
  hash_map<tree, param_info> *param_uses;
  location_t loc;
  bool param_seen;
};

static tree
register_param_uses (tree *stmt, int *do_subtree ATTRIBUTE_UNUSED, void *d)
{
  param_frame_data *data = (param_frame_data *) d;

  if (TREE_CODE (*stmt) != PARM_DECL)
    return NULL_TREE;

  bool existed;
  param_info &parm = data->param_uses->get_or_insert (*stmt, &existed);
  gcc_checking_assert (existed);

  if (parm.field_id == NULL_TREE)
    {
      tree actual_type = TREE_TYPE (*stmt);

      if (!COMPLETE_TYPE_P (actual_type))
	actual_type = complete_type_or_else (actual_type, *stmt);

      if (TREE_CODE (actual_type) == REFERENCE_TYPE)
	actual_type = build_pointer_type (TREE_TYPE (actual_type));

      parm.frame_type = actual_type;
      tree pname = DECL_NAME (*stmt);
      size_t namsize = sizeof ("__parm.") + IDENTIFIER_LENGTH (pname) + 1;
      char *buf = (char *) alloca (namsize);
      snprintf (buf, namsize, "__parm.%s", IDENTIFIER_POINTER (pname));
      parm.field_id
	= coro_make_frame_entry (data->field_list, buf, actual_type, data->loc);
      vec_alloc (parm.body_uses, 4);
      parm.body_uses->quick_push (stmt);
      data->param_seen = true;
    }
  else
    parm.body_uses->safe_push (stmt);

  return NULL_TREE;
}

/* For figuring out what local variable usage we have.  */

struct local_vars_frame_data
{
  tree *field_list;
  hash_map<tree, local_var_info> *local_var_uses;
  vec<local_var_info> *captures;
  unsigned int nest_depth, bind_indx;
  location_t loc;
  bool saw_capture;
  bool local_var_seen;
};

static tree
register_local_var_uses (tree *stmt, int *do_subtree, void *d)
{
  local_vars_frame_data *lvd = (local_vars_frame_data *) d;

  /* As we enter a bind expression - record the vars there and then recurse.
     As we exit drop the nest depth.
     The bind index is a growing count of how many bind indices we've seen.
     We build a space in the frame for each local var.
  */
  if (TREE_CODE (*stmt) == BIND_EXPR)
    {
      lvd->bind_indx++;
      lvd->nest_depth++;
      tree lvar;
      for (lvar = BIND_EXPR_VARS (*stmt); lvar != NULL;
	   lvar = DECL_CHAIN (lvar))
	{
	  bool existed;
	  local_var_info &local_var
	    = lvd->local_var_uses->get_or_insert (lvar, &existed);
	  gcc_checking_assert (!existed);
	  tree lvtype = TREE_TYPE (lvar);
	  tree lvname = DECL_NAME (lvar);
	  bool captured = is_normal_capture_proxy (lvar);
	  /* Make names depth+index unique, so that we can support nested
	     scopes with identically named locals.  */
	  char *buf;
	  size_t namsize = sizeof ("__lv...") + 18;
	  const char *nm = (captured ? "cp" : "lv");
	  if (lvname != NULL_TREE)
	    {
	      namsize += IDENTIFIER_LENGTH (lvname);
	      buf = (char *) alloca (namsize);
	      snprintf (buf, namsize, "__%s.%u.%u.%s", nm, lvd->bind_indx,
			lvd->nest_depth, IDENTIFIER_POINTER (lvname));
	    }
	  else
	    {
	      namsize += 10; // 'D' followed by an unsigned.
	      buf = (char *) alloca (namsize);
	      snprintf (buf, namsize, "__%s.%u.%u.D%u", nm, lvd->bind_indx,
			lvd->nest_depth, DECL_UID (lvar));
	    }
	  /* TODO: Figure out if we should build a local type that has any
	     excess alignment or size from the original decl.  */
	  local_var.field_id
	    = coro_make_frame_entry (lvd->field_list, buf, lvtype, lvd->loc);
	  local_var.def_loc = DECL_SOURCE_LOCATION (lvar);
	  local_var.frame_type = lvtype;
	  local_var.field_idx = NULL_TREE;
	  if (captured)
	    {
	      gcc_checking_assert (DECL_INITIAL (lvar) == NULL_TREE);
	      local_var.captured = lvar;
	      lvd->captures->safe_push (local_var);
	      lvd->saw_capture = true;
	    }
	  else
	    local_var.captured = NULL;
	  lvd->local_var_seen = true;
	  /* We don't walk any of the local var sub-trees, they won't contain
	     any bind exprs.  */
	}
      cp_walk_tree (&BIND_EXPR_BODY (*stmt), register_local_var_uses, d, NULL);
      *do_subtree = 0; /* We've done this.  */
      lvd->nest_depth--;
    }
  return NULL_TREE;
}

/* Here we:
   a) Check that the function and promise type are valid for a
      coroutine.
   b) Carry out the initial morph to create the skeleton of the
      coroutine ramp function and the rewritten body.

  Assumptions.

  1. We only hit this code once all dependencies are resolved.
  2. The function body will be either a bind expr or a statement list
  3. That cfun and current_function_decl are valid for the case we're
     expanding.
  4. 'input_location' will be of the final brace for the function.

 We do something like this:
 declare a dummy coro frame.
 struct _R_frame {
  using handle_type = coro::coroutine_handle<coro1::promise_type>;
  void (*__resume)(_R_frame *);
  void (*__destroy)(_R_frame *);
  coro1::promise_type __p;
  bool frame_needs_free; // free the coro frame mem if set.
  short __resume_at; // this is where clang puts it - but it's a smaller entity.
  coro1::suspend_never_prt __is;
  (maybe) handle_type i_hand;
  coro1::suspend_always_prt __fs;
  (maybe) handle_type f_hand;
  (maybe) parameters used in the body.
  (maybe) local variables saved
  (maybe) trailing space.
 };  */

bool
morph_fn_to_coro (tree orig, tree *resumer, tree *destroyer)
{
  gcc_checking_assert (orig && TREE_CODE (orig) == FUNCTION_DECL);

  if (!coro_function_valid_p (orig))
    return false;

  /* We can't validly get here with an empty statement list, since there's no
     way for the FE to decide it's a coroutine in the absence of any code.  */
  tree fnbody = pop_stmt_list (DECL_SAVED_TREE (orig));
  if (fnbody == NULL_TREE)
    return false;

  /* We don't have the locus of the opening brace - it's filled in later (and
     there doesn't really seem to be any easy way to get at it).
     The closing brace is assumed to be input_location.  */
  location_t fn_start = DECL_SOURCE_LOCATION (orig);
  gcc_rich_location fn_start_loc (fn_start);

  /* Initial processing of the captured body.
     If we have no expressions or just an error then punt.  */
  tree body_start = expr_first (fnbody);
  if (body_start == NULL_TREE || body_start == error_mark_node)
    {
      DECL_SAVED_TREE (orig) = push_stmt_list ();
      append_to_statement_list (DECL_SAVED_TREE (orig), &fnbody);
      return false;
    }

  /* So, we've tied off the original body.  Now start the replacement.
     If we encounter a fatal error we might return a now-empty body.
     TODO: determine if it would help to restore the original.
	   determine if looking for more errors in coro_function_valid_p()
	   and stashing types is a better solution.
  */

  tree newbody = push_stmt_list ();
  DECL_SAVED_TREE (orig) = newbody;

  /* If our original body is noexcept, then that's what we apply to our
     generated functions.  Remember that we're NOEXCEPT and fish out the
     contained list (we tied off to the top level already).  */
  bool is_noexcept = TREE_CODE (body_start) == MUST_NOT_THROW_EXPR;
  if (is_noexcept)
    {
      /* Simplified abstract from begin_eh_spec_block, since we already
	 know the outcome.  */
      fnbody = TREE_OPERAND (body_start, 0); /* Stash the original...  */
      add_stmt (body_start);		     /* ... and start the new.  */
      TREE_OPERAND (body_start, 0) = push_stmt_list ();
    }

  /* Create the coro frame type, as far as it can be known at this stage.
     1. Types we already know.  */

  tree fn_return_type = TREE_TYPE (TREE_TYPE (orig));
  gcc_assert (!VOID_TYPE_P (fn_return_type));
  tree handle_type = get_coroutine_handle_type (orig);
  tree promise_type = get_coroutine_promise_type (orig);

  /* 2. Types we need to define or look up.  */

  /* We need to know, and inspect, each suspend point in the function
     in several places.  It's convenient to place this map out of line
     since it's used from tree walk callbacks.  */
  suspend_points = new hash_map<tree, suspend_point_info>;

  /* Initial and final suspend types are special in that the co_awaits for
     them are synthetic.  We need to find the type for each awaiter from
     the coroutine promise.  */
  tree initial_await = build_init_or_final_await (fn_start, false);
  if (initial_await == error_mark_node)
    return false;
  /* The type of the frame var for this is the type of its temp proxy.  */
  tree initial_suspend_type = TREE_TYPE (TREE_OPERAND (initial_await, 1));

  tree final_await = build_init_or_final_await (fn_start, true);
  if (final_await == error_mark_node)
    return false;

  /* The type of the frame var for this is the type of its temp proxy.  */
  tree final_suspend_type = TREE_TYPE (TREE_OPERAND (final_await, 1));

  tree fr_name = get_fn_local_identifier (orig, "frame");
  tree coro_frame_type = xref_tag (record_type, fr_name, ts_current, false);
  DECL_CONTEXT (TYPE_NAME (coro_frame_type)) = current_scope ();
  tree coro_frame_ptr = build_pointer_type (coro_frame_type);
  tree act_des_fn_type
    = build_function_type_list (void_type_node, coro_frame_ptr, NULL_TREE);
  tree act_des_fn_ptr = build_pointer_type (act_des_fn_type);

  /* Declare the actor function.  */
  tree actor_name = get_fn_local_identifier (orig, "actor");
  tree actor = build_lang_decl (FUNCTION_DECL, actor_name, act_des_fn_type);
  DECL_CONTEXT (actor) = DECL_CONTEXT (orig);
  DECL_INITIAL (actor) = error_mark_node;

  /* Declare the destroyer function.  */
  tree destr_name = get_fn_local_identifier (orig, "destroy");
  tree destroy = build_lang_decl (FUNCTION_DECL, destr_name, act_des_fn_type);
  DECL_CONTEXT (destroy) = DECL_CONTEXT (orig);
  DECL_INITIAL (destroy) = error_mark_node;

  /* Build our dummy coro frame layout.  */
  coro_frame_type = begin_class_definition (coro_frame_type);

  tree field_list = NULL_TREE;
  tree resume_name
    = coro_make_frame_entry (&field_list, "__resume", act_des_fn_ptr, fn_start);
  tree destroy_name = coro_make_frame_entry (&field_list, "__destroy",
					     act_des_fn_ptr, fn_start);
  tree promise_name
    = coro_make_frame_entry (&field_list, "__p", promise_type, fn_start);
  tree fnf_name = coro_make_frame_entry (&field_list, "__frame_needs_free",
					 boolean_type_node, fn_start);
  tree resume_idx_name
    = coro_make_frame_entry (&field_list, "__resume_at",
			     short_unsigned_type_node, fn_start);

  /* We need a handle to this coroutine, which is passed to every
     await_suspend().  There's no point in creating it over and over.  */
  (void) coro_make_frame_entry (&field_list, "__self_h", handle_type, fn_start);

  /* Initial suspend is mandated.  */
  tree init_susp_name = coro_make_frame_entry (&field_list, "__aw_s.is",
					       initial_suspend_type, fn_start);

  /* Figure out if we need a saved handle from the awaiter type.  */
  tree ret_typ = get_await_suspend_return_type (initial_await);
  tree init_hand_name;
  if (VOID_TYPE_P (ret_typ) || TREE_CODE (ret_typ) == BOOLEAN_TYPE)
    init_hand_name = NULL_TREE; /* no handle is needed.  */
  else
    {
      init_hand_name
	= coro_make_frame_entry (&field_list, "__ih", ret_typ, fn_start);
    }

  register_await_info (initial_await, initial_suspend_type, init_susp_name,
		       ret_typ, init_hand_name);

  /* Now insert the data for any body await points, at this time we also need
     to promote any temporaries that are captured by reference (to regular
     vars) they will get added to the coro frame along with other locals.  */
  susp_frame_data body_aw_points
    = {&field_list, handle_type, hash_set<tree> (), NULL, NULL, 0, 0, false};
  body_aw_points.to_replace = make_tree_vector ();
  body_aw_points.block_stack = make_tree_vector ();
  cp_walk_tree (&fnbody, await_statement_walker, &body_aw_points, NULL);

  /* Final suspend is mandated.  */
  tree fin_susp_name = coro_make_frame_entry (&field_list, "__aw_s.fs",
					      final_suspend_type, fn_start);

  ret_typ = get_await_suspend_return_type (final_await);
  tree fin_hand_name;
  if (VOID_TYPE_P (ret_typ) || TREE_CODE (ret_typ) == BOOLEAN_TYPE)
    fin_hand_name = NULL_TREE; /* no handle is needed.  */
  else
    {
      fin_hand_name
	= coro_make_frame_entry (&field_list, "__fh", ret_typ, fn_start);
    }

  register_await_info (final_await, final_suspend_type, fin_susp_name,
		       void_type_node, fin_hand_name);

  /* 3. Now add in fields for function params (if there are any) that are used
     within the function body.  This is conservative; we can't tell at this
     stage if such uses might be optimized away, or if they might turn out not
     to persist across any suspend points.  Of course, even if they don't
     persist across suspend points, when the actor is out of line the saved
     frame version is still needed.  */
  hash_map<tree, param_info> *param_uses = NULL;
  if (DECL_ARGUMENTS (orig))
    {
      /* Build a hash map with an entry for each param.
	  The key is the param tree.
	  Then we have an entry for the frame field name.
	  Then a cache for the field ref when we come to use it.
	  Then a tree list of the uses.
	  The second two entries start out empty - and only get populated
	  when we see uses.  */
      param_uses = new hash_map<tree, param_info>;

      for (tree arg = DECL_ARGUMENTS (orig); arg != NULL;
	   arg = DECL_CHAIN (arg))
	{
	  bool existed;
	  param_info &parm = param_uses->get_or_insert (arg, &existed);
	  gcc_checking_assert (!existed);
	  parm.field_id = NULL_TREE;
	  parm.body_uses = NULL;
	}

      param_frame_data param_data
	= {&field_list, param_uses, fn_start, false};
      /* We want to record every instance of param's use, so don't include
	 a 'visited' hash_set.  */
      cp_walk_tree (&fnbody, register_param_uses, &param_data, NULL);

      /* If no uses for any param were seen, act as if there were no
	 params (it could be that they are only used to construct the
	 promise).  */
      if (!param_data.param_seen)
	{
	  delete param_uses;
	  param_uses = NULL;
	}
    }

  /* 4. Now make space for local vars, this is conservative again, and we
     would expect to delete unused entries later.  */
  hash_map<tree, local_var_info> local_var_uses;
  auto_vec<local_var_info> captures;

  local_vars_frame_data local_vars_data
    = {&field_list, &local_var_uses, &captures, 0, 0, fn_start, false, false};
  cp_walk_tree (&fnbody, register_local_var_uses, &local_vars_data, NULL);

  /* Tie off the struct for now, so that we can build offsets to the
     known entries.  */
  TYPE_FIELDS (coro_frame_type) = field_list;
  TYPE_BINFO (coro_frame_type) = make_tree_binfo (0);
  BINFO_OFFSET (TYPE_BINFO (coro_frame_type)) = size_zero_node;
  BINFO_TYPE (TYPE_BINFO (coro_frame_type)) = coro_frame_type;

  coro_frame_type = finish_struct (coro_frame_type, NULL_TREE);

  /* Ramp: */
  /* Now build the ramp function pieces.  */
  tree ramp_bind = build3 (BIND_EXPR, void_type_node, NULL, NULL, NULL);
  add_stmt (ramp_bind);
  tree ramp_body = push_stmt_list ();
  tree empty_list = build_empty_stmt (fn_start);

  tree coro_fp = build_lang_decl (VAR_DECL, get_identifier ("coro.frameptr"),
				  coro_frame_ptr);
  tree varlist = coro_fp;
  local_var_info *cap;
  if (!captures.is_empty())
    for (int ix = 0; captures.iterate (ix, &cap); ix++)
      {
	if (cap->field_id == NULL_TREE)
	  continue;
	tree t = cap->captured;
	DECL_CHAIN (t) = varlist;
	varlist = t;
      }

  /* Collected the scope vars we need ... only one for now. */
  BIND_EXPR_VARS (ramp_bind) = nreverse (varlist);

  /* We're now going to create a new top level scope block for the ramp
     function.  */
  tree top_block = make_node (BLOCK);

  BIND_EXPR_BLOCK (ramp_bind) = top_block;
  BLOCK_VARS (top_block) = BIND_EXPR_VARS (ramp_bind);
  BLOCK_SUBBLOCKS (top_block) = NULL_TREE;

  /* The decl_expr for the coro frame pointer, initialize to zero so that we
     can pass it to the IFN_CO_FRAME (since there's no way to pass a type,
     directly apparently).  This avoids a "used uninitialized" warning.  */
  tree r = build_stmt (fn_start, DECL_EXPR, coro_fp);
  tree zeroinit = build1 (CONVERT_EXPR, coro_frame_ptr, integer_zero_node);
  r = build2 (INIT_EXPR, TREE_TYPE (coro_fp), coro_fp, zeroinit);
  r = coro_build_cvt_void_expr_stmt (r, fn_start);
  add_stmt (r);

  /* We are going to copy the behavior of clang w.r.t to failed allocation
     of the coroutine frame.
     1. If the promise has a 'get_return_object_on_allocation_failure()'
	method, then we use a nothrow new and check the return value, calling
	the method on failure to initialize an early return.
     2. Otherwise, we call new and the ramp is expected to terminate with an
	unhandled exception in the case of failure to allocate.

     The get_return_object_on_allocation_failure() must be a static method.
  */
  tree grooaf_meth
    = lookup_promise_method (orig, coro_gro_on_allocation_fail_identifier,
			     fn_start, /*musthave=*/ false);

  /* The CO_FRAME internal function is a mechanism to allow the middle end
     to adjust the allocation in response to optimisations.  We provide the
     current conservative estimate of the frame size (as per the current)
     computed layout.  */
  tree resizeable
    = build_call_expr_internal_loc (fn_start, IFN_CO_FRAME, size_type_node, 2,
				    TYPE_SIZE_UNIT (coro_frame_type), coro_fp);

  /* We need to adjust the operator new call as per the description above when
     there is a return on allocation fail function provided in the promise.  */
  tree grooaf = NULL_TREE;
  vec<tree, va_gc> *arglist;
  vec_alloc (arglist, 2);
  arglist->quick_push (resizeable);
  if (grooaf_meth && BASELINK_P (grooaf_meth))
    {
      tree fn = BASELINK_FUNCTIONS (grooaf_meth);
      if (TREE_CODE (fn) == FUNCTION_DECL && DECL_STATIC_FUNCTION_P (fn))
	{
	  grooaf = build_call_expr_loc (fn_start, fn, 0);
	  TREE_USED (fn) = 1;
	}
      tree nth_ns = lookup_qualified_name (std_node, get_identifier ("nothrow"),
					   0, /*complain=*/ true, false);
      arglist->quick_push (nth_ns);
    }

  /* Allocate the frame.  */

  tree nwname = ovl_op_identifier (false, NEW_EXPR);
  /* The user can (optionally) provide an allocation function in the promise
      type, it's not a failure for it to be absent.  */
  tree fns = lookup_promise_method (orig, nwname, fn_start,
				    /*musthave=*/ false);
  tree new_fn = NULL_TREE;
  if (fns && fns != error_mark_node)
    {
      new_fn = lookup_arg_dependent (nwname, fns, arglist);
      if (OVL_P (new_fn))
	new_fn = OVL_FIRST (new_fn);
      else
	new_fn = BASELINK_FUNCTIONS (new_fn);

	gcc_checking_assert (DECL_STATIC_FUNCTION_P (new_fn));
	TREE_USED (new_fn) = 1;
	new_fn = build_call_expr_loc_vec (fn_start, new_fn, arglist);
    }

  /* If that fails, then fall back to the global operator new.  */
  if (new_fn == NULL_TREE || new_fn == error_mark_node)
    {
      fns =lookup_name_real (nwname, 0, 1, /*block_p=*/true, 0, 0);
      new_fn = lookup_arg_dependent (nwname, fns, arglist);
      new_fn = build_new_function_call (new_fn, &arglist, /*complain=*/ true);
    }

  tree allocated = build1 (CONVERT_EXPR, coro_frame_ptr, new_fn);
  r = build2 (INIT_EXPR, TREE_TYPE (coro_fp), coro_fp, allocated);
  r = coro_build_cvt_void_expr_stmt (r, fn_start);
  add_stmt (r);

  /* If the user provided a method to return an object on alloc fail, then
     check the returned pointer and call the func if it's null.
     Otherwise, no check, and we fail for noexcept/fno-exceptions cases.  */

  if (grooaf)
    {
      tree cfra_label
	= create_named_label_with_ctx (fn_start, "coro.frame.active",
				       current_scope ());
      tree early_ret_list = NULL;
      /* init the retval using the user's func.  */
      r = build2 (INIT_EXPR, TREE_TYPE (DECL_RESULT (orig)), DECL_RESULT (orig),
		  grooaf);
      r = coro_build_cvt_void_expr_stmt (r, fn_start);
      append_to_statement_list (r, &early_ret_list);
      // We know it's the correct type.
      r = DECL_RESULT (orig);
      r = build_stmt (fn_start, RETURN_EXPR, r);
      TREE_NO_WARNING (r) |= 1;
      r = maybe_cleanup_point_expr_void (r);
      append_to_statement_list (r, &early_ret_list);

      tree goto_st = NULL;
      r = build1 (GOTO_EXPR, void_type_node, cfra_label);
      append_to_statement_list (r, &goto_st);

      tree ckk = build1 (CONVERT_EXPR, coro_frame_ptr, integer_zero_node);
      tree ckz = build2 (EQ_EXPR, boolean_type_node, coro_fp, ckk);
      r = build3 (COND_EXPR, void_type_node, ckz, early_ret_list, empty_list);
      add_stmt (r);

      cfra_label = build_stmt (fn_start, LABEL_EXPR, cfra_label);
      add_stmt (cfra_label);
    }

  /* deref the frame pointer, to use in member access code.  */
  tree deref_fp = build_x_arrow (fn_start, coro_fp, tf_warning_or_error);

  /* For now, we always assume that this needs destruction, there's no impl.
     for frame allocation elision.  */
  tree fnf_m
    = lookup_member (coro_frame_type, fnf_name, 1, 0, tf_warning_or_error);
  tree fnf_x = build_class_member_access_expr (deref_fp, fnf_m, NULL_TREE,
					       false, tf_warning_or_error);
  r = build2 (INIT_EXPR, boolean_type_node, fnf_x, boolean_true_node);
  r = coro_build_cvt_void_expr_stmt (r, fn_start);
  add_stmt (r);

  /* Put the resumer and destroyer functions in.  */

  tree actor_addr = build1 (ADDR_EXPR, act_des_fn_ptr, actor);
  tree resume_m
    = lookup_member (coro_frame_type, resume_name,
		     /*protect*/ 1, /*want_type*/ 0, tf_warning_or_error);
  tree resume_x = build_class_member_access_expr (deref_fp, resume_m, NULL_TREE,
						  false, tf_warning_or_error);
  r = build2_loc (fn_start, INIT_EXPR, act_des_fn_ptr, resume_x, actor_addr);
  r = coro_build_cvt_void_expr_stmt (r, fn_start);
  add_stmt (r);

  tree destroy_addr = build1 (ADDR_EXPR, act_des_fn_ptr, destroy);
  tree destroy_m
    = lookup_member (coro_frame_type, destroy_name,
		     /*protect*/ 1, /*want_type*/ 0, tf_warning_or_error);
  tree destroy_x
    = build_class_member_access_expr (deref_fp, destroy_m, NULL_TREE, false,
				      tf_warning_or_error);
  r = build2_loc (fn_start, INIT_EXPR, act_des_fn_ptr, destroy_x, destroy_addr);
  r = coro_build_cvt_void_expr_stmt (r, fn_start);
  add_stmt (r);

  /* Set up the promise.  */
  tree promise_m
    = lookup_member (coro_frame_type, promise_name,
		     /*protect*/ 1, /*want_type*/ 0, tf_warning_or_error);

  tree p = build_class_member_access_expr (deref_fp, promise_m, NULL_TREE,
					   false, tf_warning_or_error);

  if (TYPE_NEEDS_CONSTRUCTING (promise_type))
    {
      /* Do a placement new constructor for the promise type (we never call
	 the new operator, just the constructor on the object in place in the
	 frame).

	 First try to find a constructor with the same parameter list as the
	 original function (if it has params), failing that find a constructor
	 with no parameter list.
      */

      if (DECL_ARGUMENTS (orig))
	{
	  vec<tree, va_gc> *args = make_tree_vector ();
	  tree arg;
	  for (arg = DECL_ARGUMENTS (orig); arg != NULL; arg = DECL_CHAIN (arg))
	    vec_safe_push (args, arg);
	  r = build_special_member_call (p, complete_ctor_identifier, &args,
					 promise_type, LOOKUP_NORMAL, tf_none);
	  release_tree_vector (args);
	}
      else
	r = NULL_TREE;

      if (r == NULL_TREE || r == error_mark_node)
	r = build_special_member_call (p, complete_ctor_identifier, NULL,
				       promise_type, LOOKUP_NORMAL,
				       tf_warning_or_error);

      r = coro_build_cvt_void_expr_stmt (r, fn_start);
      add_stmt (r);
    }

  /* Copy in any of the function params we found to be used.
     Param types with non-trivial dtors will have to be moved into position
     and the dtor run before the frame is freed.  */
  vec<tree, va_gc> *param_dtor_list = NULL;
  if (DECL_ARGUMENTS (orig) && param_uses != NULL)
    {
      tree arg;
      for (arg = DECL_ARGUMENTS (orig); arg != NULL; arg = DECL_CHAIN (arg))
	{
	  bool existed;
	  param_info &parm = param_uses->get_or_insert (arg, &existed);
	  if (parm.field_id == NULL_TREE)
	    continue; /* Wasn't used.  */

	  tree fld_ref = lookup_member (coro_frame_type, parm.field_id,
					/*protect*/ 1, /*want_type*/ 0,
					tf_warning_or_error);
	  tree fld_idx
	    = build_class_member_access_expr (deref_fp, fld_ref, NULL_TREE,
					      false, tf_warning_or_error);

	  if (TYPE_NEEDS_CONSTRUCTING (parm.frame_type))
	    {
	      vec<tree, va_gc> *p_in;
	      if (TYPE_REF_P (DECL_ARG_TYPE (arg))
		  && (CLASSTYPE_LAZY_MOVE_CTOR (parm.frame_type)
		      || CLASSTYPE_LAZY_MOVE_ASSIGN (parm.frame_type)
		      || classtype_has_move_assign_or_move_ctor_p
			    (parm.frame_type, /* user-declared */ true)))
		p_in = make_tree_vector_single (rvalue (arg));
	      else
		p_in = make_tree_vector_single (arg);
	      /* Construct in place or move as relevant.  */
	      r = build_special_member_call (fld_idx, complete_ctor_identifier,
					     &p_in, parm.frame_type,
					     LOOKUP_NORMAL,
					     tf_warning_or_error);
	      release_tree_vector (p_in);
	      if (param_dtor_list == NULL)
		param_dtor_list = make_tree_vector ();
	      vec_safe_push (param_dtor_list, parm.field_id);
	    }
	  else
	    {
	      if (!same_type_p (parm.frame_type, DECL_ARG_TYPE (arg)))
		r = build1_loc (DECL_SOURCE_LOCATION (arg), CONVERT_EXPR,
				parm.frame_type, arg);
	      else
		r = arg;
	      r = build_modify_expr (fn_start, fld_idx, parm.frame_type,
				     INIT_EXPR, DECL_SOURCE_LOCATION (arg), r,
				     TREE_TYPE (r));
	    }
	  r = coro_build_cvt_void_expr_stmt (r, fn_start);
	  add_stmt (r);
	}
    }

  vec<tree, va_gc> *captures_dtor_list = NULL;
  while (!captures.is_empty())
    {
      local_var_info cap = captures.pop();
      if (cap.field_id == NULL_TREE)
	continue;

      tree fld_ref = lookup_member (coro_frame_type, cap.field_id,
				    /*protect*/ 1, /*want_type*/ 0,
				    tf_warning_or_error);
      tree fld_idx
	= build_class_member_access_expr (deref_fp, fld_ref, NULL_TREE,
					  false, tf_warning_or_error);

      tree cap_type = cap.frame_type;

      /* When we have a reference, we do not want to change the referenced
	 item, but actually to set the reference to the proxy var.  */
      if (REFERENCE_REF_P (fld_idx))
	fld_idx = TREE_OPERAND (fld_idx, 0);

      if (TYPE_NEEDS_CONSTRUCTING (cap_type))
	{
	  vec<tree, va_gc> *p_in;
	      if (TYPE_REF_P (cap_type)
		  && (CLASSTYPE_LAZY_MOVE_CTOR (cap_type)
		      || CLASSTYPE_LAZY_MOVE_ASSIGN (cap_type)
		      || classtype_has_move_assign_or_move_ctor_p
			    (cap_type, /* user-declared */ true)))
	    p_in = make_tree_vector_single (rvalue (cap.captured));
	  else
	    p_in = make_tree_vector_single (cap.captured);
	  /* Construct in place or move as relevant.  */
	  r = build_special_member_call (fld_idx, complete_ctor_identifier,
					 &p_in, cap_type, LOOKUP_NORMAL,
					 tf_warning_or_error);
	  release_tree_vector (p_in);
	  if (captures_dtor_list == NULL)
	    captures_dtor_list = make_tree_vector ();
	  vec_safe_push (captures_dtor_list, cap.field_id);
	}
      else
	{
	  if (!same_type_p (cap_type, TREE_TYPE (cap.captured)))
	    r = build1_loc (DECL_SOURCE_LOCATION (cap.captured), CONVERT_EXPR,
			    cap_type, cap.captured);
	  else
	    r = cap.captured;
	  r = build_modify_expr (fn_start, fld_idx, cap_type,
				 INIT_EXPR, DECL_SOURCE_LOCATION (cap.captured),
				 r, TREE_TYPE (r));
	}
      r = coro_build_cvt_void_expr_stmt (r, fn_start);
      add_stmt (r);
    }

  /* Set up a new bind context for the GRO.  */
  tree gro_context_bind = build3 (BIND_EXPR, void_type_node, NULL, NULL, NULL);
  /* Make and connect the scope blocks.  */
  tree gro_block = make_node (BLOCK);
  BLOCK_SUPERCONTEXT (gro_block) = top_block;
  BLOCK_SUBBLOCKS (top_block) = gro_block;
  BIND_EXPR_BLOCK (gro_context_bind) = gro_block;
  add_stmt (gro_context_bind);

  tree gro_meth = lookup_promise_method (orig,
					 coro_get_return_object_identifier,
					 fn_start, /*musthave=*/ true );
  tree get_ro
    = build_new_method_call (p, gro_meth, NULL, NULL_TREE, LOOKUP_NORMAL, NULL,
			     tf_warning_or_error);
  /* Without a return object we haven't got much clue what's going on.  */
  if (get_ro == error_mark_node)
    {
      BIND_EXPR_BODY (ramp_bind) = pop_stmt_list (ramp_body);
      DECL_SAVED_TREE (orig) = newbody;
      return false;
    }

  tree gro_context_body = push_stmt_list ();
  tree gro, gro_bind_vars;
  if (same_type_p (TREE_TYPE (get_ro), fn_return_type))
    {
      gro = DECL_RESULT (orig);
      gro_bind_vars = NULL_TREE; // We don't need a separate var.
    }
  else
    {
      gro = build_lang_decl (VAR_DECL, get_identifier ("coro.gro"),
			     TREE_TYPE (TREE_OPERAND (get_ro, 0)));
      DECL_CONTEXT (gro) = current_scope ();
      r = build_stmt (fn_start, DECL_EXPR, gro);
      add_stmt (r);
      gro_bind_vars = gro; // We need a temporary var.
    }

  // init our actual var.
  r = build2_loc (fn_start, INIT_EXPR, TREE_TYPE (gro), gro, get_ro);
  r = coro_build_cvt_void_expr_stmt (r, fn_start);
  add_stmt (r);

  /* Initialize the resume_idx_name to 0, meaning "not started".  */
  tree resume_idx_m
    = lookup_member (coro_frame_type, resume_idx_name,
		     /*protect*/ 1, /*want_type*/ 0, tf_warning_or_error);
  tree resume_idx
    = build_class_member_access_expr (deref_fp, resume_idx_m, NULL_TREE, false,
				      tf_warning_or_error);
  r = build_int_cst (short_unsigned_type_node, 0);
  r = build2_loc (fn_start, INIT_EXPR, short_unsigned_type_node, resume_idx, r);
  r = coro_build_cvt_void_expr_stmt (r, fn_start);
  add_stmt (r);

  /* So .. call the actor ..  */
  r = build_call_expr_loc (fn_start, actor, 1, coro_fp);
  r = maybe_cleanup_point_expr_void (r);
  add_stmt (r);

  /* Switch to using 'input_location' as the loc, since we're now more
     logically doing things related to the end of the function.  */
  /* done, we just need the return value.  */
  bool no_warning;
  if (same_type_p (TREE_TYPE (gro), fn_return_type))
    {
      /* Already got the result.  */
      r = check_return_expr (DECL_RESULT (orig), &no_warning);
    }
  else
    {
      // construct the return value with a single GRO param.
      vec<tree, va_gc> *args = make_tree_vector_single (gro);
      r = build_special_member_call (DECL_RESULT (orig),
				     complete_ctor_identifier, &args,
				     fn_return_type, LOOKUP_NORMAL,
				     tf_warning_or_error);
      r = coro_build_cvt_void_expr_stmt (r, input_location);
      add_stmt (r);
      release_tree_vector (args);
    }

  r = build_stmt (input_location, RETURN_EXPR, DECL_RESULT (orig));
  TREE_NO_WARNING (r) |= no_warning;
  r = maybe_cleanup_point_expr_void (r);
  add_stmt (r);
  BIND_EXPR_VARS (gro_context_bind) = gro_bind_vars;
  BIND_EXPR_BODY (gro_context_bind) = pop_stmt_list (gro_context_body);
  BIND_EXPR_BODY (ramp_bind) = pop_stmt_list (ramp_body);

  /* We know the "real" promise and have a frame layout with a slot for each
     suspend point, so we can build an actor function (which contains the
     functionality for both 'resume' and 'destroy').

     wrap the function body in a try {} catch (...) {} block, if exceptions
     are enabled.  */

  /* First make a new block for the body - that will be embedded in the
     re-written function.  */
  tree first = expr_first (fnbody);
  bool orig_fn_has_outer_bind = false;
  tree replace_blk = NULL_TREE;
  if (first && TREE_CODE (first) == BIND_EXPR)
    {
      orig_fn_has_outer_bind = true;
      tree block = BIND_EXPR_BLOCK (first);
      replace_blk = make_node (BLOCK);
      if (block) // missing block is probably an error.
	{
	  gcc_assert (BLOCK_SUPERCONTEXT (block) == NULL_TREE);
	  gcc_assert (BLOCK_CHAIN (block) == NULL_TREE);
	  BLOCK_VARS (replace_blk) = BLOCK_VARS (block);
	  BLOCK_SUBBLOCKS (replace_blk) = BLOCK_SUBBLOCKS (block);
	  for (tree b = BLOCK_SUBBLOCKS (replace_blk); b; b = BLOCK_CHAIN (b))
	    BLOCK_SUPERCONTEXT (b) = replace_blk;
	}
      BIND_EXPR_BLOCK (first) = replace_blk;
    }

  if (flag_exceptions)
    {
      tree ueh_meth
	= lookup_promise_method (orig, coro_unhandled_exception_identifier,
				 fn_start, /*musthave=*/ true);
      /* Build promise.unhandled_exception();  */
      tree ueh
	= build_new_method_call (p, ueh_meth, NULL, NULL_TREE, LOOKUP_NORMAL,
				 NULL, tf_warning_or_error);

      /* The try block is just the original function, there's no real
	 need to call any function to do this.  */
      tree tcb = build_stmt (fn_start, TRY_BLOCK, NULL_TREE, NULL_TREE);
      TRY_STMTS (tcb) = fnbody;
      TRY_HANDLERS (tcb) = push_stmt_list ();
      /* Mimic what the parser does for the catch.  */
      tree handler = begin_handler ();
      finish_handler_parms (NULL_TREE, handler); /* catch (...) */
      ueh = maybe_cleanup_point_expr_void (ueh);
      add_stmt (ueh);
      finish_handler (handler);
      TRY_HANDLERS (tcb) = pop_stmt_list (TRY_HANDLERS (tcb));
      /* If the function starts with a BIND_EXPR, then we need to create
	 one here to contain the try-catch and to link up the scopes.  */
      if (orig_fn_has_outer_bind)
	{
	  tree tcb_bind = build3 (BIND_EXPR, void_type_node, NULL, NULL, NULL);
	  /* Make and connect the scope blocks.  */
	  tree tcb_block = make_node (BLOCK);
	  /* .. and connect it here.  */
	  BLOCK_SUPERCONTEXT (replace_blk) = tcb_block;
	  BLOCK_SUBBLOCKS (tcb_block) = replace_blk;
	  BIND_EXPR_BLOCK (tcb_bind) = tcb_block;
	  BIND_EXPR_BODY (tcb_bind) = tcb;
	  fnbody = tcb_bind;
	}
      else
	fnbody = tcb;
    }
  else if (pedantic)
    {
      /* We still try to look for the promise method and warn if it's not
	 present.  */
      tree ueh_meth
	= lookup_promise_method (orig, coro_unhandled_exception_identifier,
				 fn_start, /*musthave=*/ false);
      if (!ueh_meth || ueh_meth == error_mark_node)
	warning_at (fn_start, 0, "no member named %qE in %qT",
		    coro_unhandled_exception_identifier,
		    get_coroutine_promise_type (orig));
    }
  /* Else we don't check and don't care if the method is missing.  */

  /* Start to build the final functions.

     We push_deferring_access_checks to avoid these routines being seen as
     nested by the middle end; we are doing the outlining here.  */

  push_deferring_access_checks (dk_no_check);

  /* Actor ...  */
  build_actor_fn (fn_start, coro_frame_type, actor, fnbody, orig, param_uses,
		  &local_var_uses, param_dtor_list, initial_await, final_await,
		  body_aw_points.count);

  /* Destroyer ... */
  build_destroy_fn (fn_start, coro_frame_type, destroy, actor);

  pop_deferring_access_checks ();

  DECL_SAVED_TREE (orig) = newbody;
  /* Link our new functions into the list.  */
  TREE_CHAIN (destroy) = TREE_CHAIN (orig);
  TREE_CHAIN (actor) = destroy;
  TREE_CHAIN (orig) = actor;

  *resumer = actor;
  *destroyer = destroy;

  delete suspend_points;
  suspend_points = NULL;
  return true;
}

#include "gt-cp-coroutines.h"

