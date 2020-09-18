/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * Various low-level, bit-manipulation routines.
 *
 * findFirstSet(x)  [constexpr]
 *    find first (least significant) bit set in a value of an integral type,
 *    1-based (like ffs()).  0 = no bits are set (x == 0)
 *
 * findLastSet(x)  [constexpr]
 *    find last (most significant) bit set in a value of an integral type,
 *    1-based.  0 = no bits are set (x == 0)
 *    for x != 0, findLastSet(x) == 1 + floor(log2(x))
 *
 * extractFirstSet(x)  [constexpr]
 *    extract first (least significant) bit set in a value of an integral
 *    type, 0 = no bits are set (x == 0)
 *
 * nextPowTwo(x)  [constexpr]
 *    Finds the next power of two >= x.
 *
 * strictNextPowTwo(x)  [constexpr]
 *    Finds the next power of two > x.
 *
 * isPowTwo(x)  [constexpr]
 *    return true iff x is a power of two
 *
 * popcount(x)
 *    return the number of 1 bits in x
 *
 * Endian
 *    convert between native, big, and little endian representation
 *    Endian::big(x)      big <-> native
 *    Endian::little(x)   little <-> native
 *    Endian::swap(x)     big <-> little
 *
 * @author Tudor Bosman (tudorb@fb.com)
 */

#pragma once

#include <cassert>
#include <cinttypes>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

template <std::size_t I>
using index_constant = std::integral_constant<std::size_t, I>;

#if __has_include(<bit>)
#include <bit>
#endif

namespace folly {

#if __cpp_lib_bit_cast

using std::bit_cast;

#else

//  mimic: std::bit_cast, C++20
/*
template <
    typename To,
    typename From,
    std::enable_if_t<
        sizeof(From) == sizeof(To) && is_trivially_copyable<To>::value &&
            is_trivially_copyable<From>::value,
        int> = 0>
To bit_cast(const From& src) noexcept {
  aligned_storage_for_t<To> storage;
  std::memcpy(&storage, &src, sizeof(From));
  return reinterpret_cast<To&>(storage);
}
*/

#endif
template <typename T>
constexpr T constexpr_max(T a) {
  return a;
}
template <typename T, typename... Ts>
constexpr T constexpr_max(T a, T b, Ts... ts) {
  return b < a ? constexpr_max(a, ts...) : constexpr_max(b, ts...);
}

template <typename Int>
constexpr typename std::make_unsigned<Int>::type to_unsigned(Int value) {
  assert(value >= 0 && "negative value");
  return static_cast<typename std::make_unsigned<Int>::type>(value);
}

namespace detail {
template <typename Dst, typename Src>
constexpr std::make_signed_t<Dst> bits_to_signed(Src const s) {
  static_assert(std::is_signed<Dst>::value, "unsigned type");
  return to_signed(static_cast<std::make_unsigned_t<Dst>>(to_unsigned(s)));
}
template <typename Dst, typename Src>
constexpr std::make_unsigned_t<Dst> bits_to_unsigned(Src const s) {
  static_assert(std::is_unsigned<Dst>::value, "signed type");
  return static_cast<Dst>(to_unsigned(s));
}
}  // namespace detail

/// findFirstSet
///
/// Return the 1-based index of the least significant bit which is set.
/// For x > 0, the exponent in the largest power of two which does not divide x.
template <typename T>
inline constexpr unsigned int findFirstSet(T const v) {
  using S0 = int;
  using S1 = long int;
  using S2 = long long int;
  using detail::bits_to_signed;
  static_assert(sizeof(T) <= sizeof(S2), "over-sized type");
  static_assert(std::is_integral<T>::value, "non-integral type");
  static_assert(!std::is_same<T, bool>::value, "bool type");

  // clang-format off
  return static_cast<unsigned int>(
      sizeof(T) <= sizeof(S0) ? __builtin_ffs(bits_to_signed<S0>(v)) :
      sizeof(T) <= sizeof(S1) ? __builtin_ffsl(bits_to_signed<S1>(v)) :
      sizeof(T) <= sizeof(S2) ? __builtin_ffsll(bits_to_signed<S2>(v)) :
      0);
  // clang-format on
}
// When a and b are equivalent objects, we return a to
// make sorting stable.
template <typename T>
constexpr T constexpr_min(T a) {
  return a;
}
template <typename T, typename... Ts>
constexpr T constexpr_min(T a, T b, Ts... ts) {
  return b < a ? constexpr_min(b, ts...) : constexpr_min(a, ts...);
}
template <typename Dst, typename Src>
constexpr std::make_signed_t<Dst> bits_to_signed(Src const s) {
  static_assert(std::is_signed<Dst>::value, "unsigned type");
  return to_signed(static_cast<std::make_unsigned_t<Dst>>(to_unsigned(s)));
}
template <typename Dst, typename Src>
constexpr std::make_unsigned_t<Dst> bits_to_unsigned(Src const s) {
  static_assert(std::is_unsigned<Dst>::value, "signed type");
  return static_cast<Dst>(to_unsigned(s));
}

/// findLastSet
///
/// Return the 1-based index of the most significant bit which is set.
/// For x > 0, findLastSet(x) == 1 + floor(log2(x)).
template <typename T>
inline constexpr unsigned int findLastSet(T const v) {
  using U0 = unsigned int;
  using U1 = unsigned long int;
  using U2 = unsigned long long int;
  using detail::bits_to_unsigned;
  static_assert(sizeof(T) <= sizeof(U2), "over-sized type");
  static_assert(std::is_integral<T>::value, "non-integral type");
  static_assert(!std::is_same<T, bool>::value, "bool type");

  // If X is a power of two X - Y = 1 + ((X - 1) ^ Y). Doing this transformation
  // allows GCC to remove its own xor that it adds to implement clz using bsr.
  // clang-format off
  using size = index_constant<constexpr_max(sizeof(T), sizeof(U0))>;
  return v ? 1u + static_cast<unsigned int>((8u * size{} - 1u) ^ (
      sizeof(T) <= sizeof(U0) ? __builtin_clz(bits_to_unsigned<U0>(v)) :
      sizeof(T) <= sizeof(U1) ? __builtin_clzl(bits_to_unsigned<U1>(v)) :
      sizeof(T) <= sizeof(U2) ? __builtin_clzll(bits_to_unsigned<U2>(v)) :
      0)) : 0u;
  // clang-format on
}

/// extractFirstSet
///
/// Return a value where all the bits but the least significant are cleared.
template <typename T>
inline constexpr T extractFirstSet(T const v) {
  static_assert(std::is_integral<T>::value, "non-integral type");
  static_assert(std::is_unsigned<T>::value, "signed type");
  static_assert(!std::is_same<T, bool>::value, "bool type");

  return v & -v;
}

/// popcount
///
/// Returns the number of bits which are set.
template <typename T>
inline constexpr unsigned int popcount(T const v) {
  using U0 = unsigned int;
  using U1 = unsigned long int;
  using U2 = unsigned long long int;
  using detail::bits_to_unsigned;
  static_assert(sizeof(T) <= sizeof(U2), "over-sized type");
  static_assert(std::is_integral<T>::value, "non-integral type");
  static_assert(!std::is_same<T, bool>::value, "bool type");

  // clang-format off
  return static_cast<unsigned int>(
      sizeof(T) <= sizeof(U0) ? __builtin_popcount(bits_to_unsigned<U0>(v)) :
      sizeof(T) <= sizeof(U1) ? __builtin_popcountl(bits_to_unsigned<U1>(v)) :
      sizeof(T) <= sizeof(U2) ? __builtin_popcountll(bits_to_unsigned<U2>(v)) :
      0);
  // clang-format on
}

template <class T>
inline constexpr T nextPowTwo(T const v) {
  static_assert(std::is_unsigned<T>::value, "signed type");
  return v ? (T(1) << findLastSet(v - 1)) : T(1);
}

template <class T>
inline constexpr T prevPowTwo(T const v) {
  static_assert(std::is_unsigned<T>::value, "signed type");
  return v ? (T(1) << (findLastSet(v) - 1)) : T(0);
}

template <class T>
inline constexpr bool isPowTwo(T const v) {
  static_assert(std::is_integral<T>::value, "non-integral type");
  static_assert(std::is_unsigned<T>::value, "signed type");
  static_assert(!std::is_same<T, bool>::value, "bool type");
  return (v != 0) && !(v & (v - 1));
}

template <class T>
inline constexpr T strictNextPowTwo(T const v) {
  static_assert(std::is_unsigned<T>::value, "signed type");
  return nextPowTwo(T(v + 1));
}

template <class T>
inline constexpr T strictPrevPowTwo(T const v) {
  static_assert(std::is_unsigned<T>::value, "signed type");
  return v > 1 ? prevPowTwo(T(v - 1)) : T(0);
}

}  // namespace folly
