// { dg-do compile { target c++11 } }
//
// Copyright (C) 2011-2020 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 3, or (at your option)
// any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING3.  If not see
// <http://www.gnu.org/licenses/>.

#include <type_traits>
#include <testsuite_tr1.h>

void test01()
{
  using std::is_function;
  using namespace __gnu_test;

  // Positive tests.
  static_assert(test_category<is_function, int (int)>(true), "");
  static_assert(test_category<is_function, ClassType (ClassType)>(true), "");
  static_assert(test_category<is_function,
		float (int, float, int[], int&)>(true), "");
  static_assert(test_category<is_function, int (int, ...)>(true), "");
  static_assert(test_category<is_function, bool (ClassType) const>(true), "");
  static_assert(test_category<is_function, ClassType () &>(true), "");
  static_assert(test_category<is_function,
		char (int, ClassType) const volatile &&>(true), "");

  // Negative tests.
  static_assert(test_category<is_function, int*>(false), "");
  static_assert(test_category<is_function, int&>(false), "");
  static_assert(test_category<is_function, void>(false), "");
  static_assert(test_category<is_function, const void>(false), "");
  static_assert(test_category<is_function, void*>(false), "");
  static_assert(test_category<is_function, const void*>(false), "");
  static_assert(test_category<is_function, void**>(false), "");
  static_assert(test_category<is_function, std::nullptr_t>(false), "");

  static_assert(test_category<is_function, AbstractClass>(false), "");
  static_assert(test_category<is_function, int(&)(int)>(false), "");
  static_assert(test_category<is_function, int(*)(int)>(false), "");
  
  // Sanity check.
  static_assert(test_category<is_function, ClassType>(false), "");
  static_assert(test_category<is_function, IncompleteClass>(false), "");
  static_assert(test_category<is_function, IncompleteUnion>(false), "");
}
