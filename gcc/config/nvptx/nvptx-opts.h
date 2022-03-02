/* Definitions for the NVPTX port needed for option handling.
   Copyright (C) 2015-2022 Free Software Foundation, Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 3, or (at your
   option) any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING3.  If not see
   <http://www.gnu.org/licenses/>.  */

#ifndef NVPTX_OPTS_H
#define NVPTX_OPTS_H

enum ptx_isa
{
  PTX_ISA_SM30,
  PTX_ISA_SM35,
  PTX_ISA_SM53,
  PTX_ISA_SM70,
  PTX_ISA_SM75,
  PTX_ISA_SM80
};

enum ptx_version
{
  PTX_VERSION_3_0,
  PTX_VERSION_3_1,
  PTX_VERSION_4_2,
  PTX_VERSION_6_0,
  PTX_VERSION_6_3,
  PTX_VERSION_7_0
};

#endif

