// { dg-additional-options "-std=c++23" }
#include <generator>

auto another_fnc() -> std::generator<int>;

template <bool V>
auto foo() -> std::generator<int> {
    if constexpr (V) {
        co_yield 1;
    } else {
        // gcc + clang => nope
        // msvc => it's a c++23 extension
        // edg => fine by me
        return another_fnc(); 
    }
}

//template auto foo<true>() -> std::generator<int>;

template auto foo<false>() -> std::generator<int>;
