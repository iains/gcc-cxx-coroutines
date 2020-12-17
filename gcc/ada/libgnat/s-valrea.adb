------------------------------------------------------------------------------
--                                                                          --
--                         GNAT COMPILER COMPONENTS                         --
--                                                                          --
--                      S Y S T E M . V A L _ R E A L                       --
--                                                                          --
--                                 B o d y                                  --
--                                                                          --
--          Copyright (C) 1992-2020, Free Software Foundation, Inc.         --
--                                                                          --
-- GNAT is free software;  you can  redistribute it  and/or modify it under --
-- terms of the  GNU General Public License as published  by the Free Soft- --
-- ware  Foundation;  either version 3,  or (at your option) any later ver- --
-- sion.  GNAT is distributed in the hope that it will be useful, but WITH- --
-- OUT ANY WARRANTY;  without even the  implied warranty of MERCHANTABILITY --
-- or FITNESS FOR A PARTICULAR PURPOSE.                                     --
--                                                                          --
-- As a special exception under Section 7 of GPL version 3, you are granted --
-- additional permissions described in the GCC Runtime Library Exception,   --
-- version 3.1, as published by the Free Software Foundation.               --
--                                                                          --
-- You should have received a copy of the GNU General Public License and    --
-- a copy of the GCC Runtime Library Exception along with this program;     --
-- see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see    --
-- <http://www.gnu.org/licenses/>.                                          --
--                                                                          --
-- GNAT was originally developed  by the GNAT team at  New York University. --
-- Extensive contributions were provided by Ada Core Technologies Inc.      --
--                                                                          --
------------------------------------------------------------------------------

with System.Float_Control;
with System.Unsigned_Types; use System.Unsigned_Types;
with System.Val_Util;       use System.Val_Util;
with System.Value_R;

package body System.Val_Real is

   pragma Assert (Num'Machine_Mantissa <= Uns'Size);
   --  We need an unsigned type large enough to represent the mantissa

   Precision_Limit : constant Uns := 2**Num'Machine_Mantissa - 1;
   --  We use the precision of the floating-point type

   package Impl is new Value_R (Uns, Precision_Limit, Floating => True);

   function Integer_to_Real
     (Str   : String;
      Val   : Uns;
      Base  : Unsigned;
      Scale : Integer;
      Extra : Unsigned;
      Minus : Boolean) return Num;
   --  Convert the real value from integer to real representation

   ---------------------
   -- Integer_to_Real --
   ---------------------

   function Integer_to_Real
     (Str   : String;
      Val   : Uns;
      Base  : Unsigned;
      Scale : Integer;
      Extra : Unsigned;
      Minus : Boolean) return Num
   is
      pragma Assert (Base in 2 .. 16);

      pragma Unsuppress (Range_Check);

      R_Val : Num;
      S     : Integer := Scale;

   begin
      --  We call the floating-point processor reset routine so we can be sure
      --  that the x87 FPU is properly set for conversions. This is especially
      --  needed on Windows, where calls to the operating system randomly reset
      --  the processor into 64-bit mode.

      if Num'Machine_Mantissa = 64 then
         System.Float_Control.Reset;
      end if;

      --  Take into account the extra digit

      R_Val := Num (Val);
      if Extra > 0 then
         R_Val := R_Val * Num (Base) + Num (Extra);
         S := S - 1;
      end if;

      --  Compute the final value

      if S < 0 then
         R_Val := R_Val / Num (Base) ** (-S);
      else
         R_Val := R_Val * Num (Base) ** S;
      end if;

      --  Finally deal with initial minus sign, note that this processing is
      --  done even if Uval is zero, so that -0.0 is correctly interpreted.

      return (if Minus then -R_Val else R_Val);

   exception
      when Constraint_Error => Bad_Value (Str);
   end Integer_to_Real;

   ---------------
   -- Scan_Real --
   ---------------

   function Scan_Real
     (Str : String;
      Ptr : not null access Integer;
      Max : Integer) return Num
   is
      Base  : Unsigned;
      Scale : Integer;
      Extra : Unsigned;
      Minus : Boolean;
      Val   : Uns;

   begin
      Val := Impl.Scan_Raw_Real (Str, Ptr, Max, Base, Scale, Extra, Minus);

      return Integer_to_Real (Str, Val, Base, Scale, Extra, Minus);
   end Scan_Real;

   ----------------
   -- Value_Real --
   ----------------

   function Value_Real (Str : String) return Num is
      Base  : Unsigned;
      Scale : Integer;
      Extra : Unsigned;
      Minus : Boolean;
      Val   : Uns;

   begin
      Val := Impl.Value_Raw_Real (Str, Base, Scale, Extra, Minus);

      return Integer_to_Real (Str, Val, Base, Scale, Extra, Minus);
   end Value_Real;

end System.Val_Real;
