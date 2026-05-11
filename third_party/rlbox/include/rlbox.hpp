#pragma once

#include <array>
#include <cstring>
#include <memory>
#include <type_traits>
#include <utility>

#include "rlbox_app_pointer.hpp"
#include "rlbox_conversion.hpp"
#include "rlbox_helpers.hpp"
#include "rlbox_policy_types.hpp"
#include "rlbox_range.hpp"
#include "rlbox_sandbox.hpp"
#include "rlbox_stdlib.hpp"
#include "rlbox_struct_support.hpp"
#include "rlbox_type_traits.hpp"
#include "rlbox_types.hpp"
#include "rlbox_unwrap.hpp"
#include "rlbox_wrapper_traits.hpp"

namespace rlbox {

template<template<typename, typename> typename T_Wrap,
         typename T,
         typename T_Sbx>
class tainted_base_impl
{
  KEEP_CLASSES_FRIENDLY
  KEEP_CAST_FRIENDLY

public:
  inline auto& impl() { return *static_cast<T_Wrap<T, T_Sbx>*>(this); }
  inline auto& impl() const
  {
    return *static_cast<const T_Wrap<T, T_Sbx>*>(this);
  }

  /**
   * @brief Unwrap a tainted value without verification. This is an unsafe
   * operation and should be used with care.
   */
  inline auto UNSAFE_unverified() const { return impl().get_raw_value(); }
  /**
   * @brief Like UNSAFE_unverified, but get the underlying sandbox
   * representation.
   *
   * @param sandbox Reference to sandbox.
   *
   * For the Wasm-based sandbox, this function additionally validates the
   * unwrapped value against the machine model of the sandbox (LP32).
   */
  inline auto UNSAFE_sandboxed(rlbox_sandbox<T_Sbx>& sandbox) const
  {
    return impl().get_raw_sandbox_value(sandbox);
  }

  /**
   * @brief Unwrap a tainted value without verification. This function should
   * be used when unwrapping is safe.
   *
   * @param reason An explanation why the unverified unwrapping is safe.
   */
  template<size_t N>
  inline auto unverified_safe_because(const char (&reason)[N]) const
  {
    RLBOX_UNUSED(reason);
    static_assert(!std::is_pointer_v<T>,
                  "unverified_safe_because does not support pointers. Use "
                  "unverified_safe_pointer_because.");
    return UNSAFE_unverified();
  }

  template<size_t N>
  inline auto unverified_safe_pointer_because(size_t count,
                                              const char (&reason)[N]) const
  {
    RLBOX_UNUSED(reason);

    static_assert(std::is_pointer_v<T>, "Expected pointer type");
    using T_Pointed = std::remove_pointer_t<T>;
    if_constexpr_named(cond1, std::is_pointer_v<T_Pointed>)
    {
      rlbox_detail_static_fail_because(
        cond1,
        "There is no way to use unverified_safe_pointer_because for "
        "'pointers to pointers' safely. Use copy_and_verify instead.");
      return nullptr;
    }

    auto ret = UNSAFE_unverified();
    if (ret != nullptr) {
      size_t bytes = sizeof(T_Pointed) * count;
      detail::check_range_doesnt_cross_app_sbx_boundary<T_Sbx>(ret, bytes);
    }
    return ret;
  }

  inline auto INTERNAL_unverified_safe() const { return UNSAFE_unverified(); }

#define BinaryOpValAndPtr(opSymbol)                                            \
  template<typename T_Rhs>                                                     \
  inline constexpr auto operator opSymbol(const T_Rhs& rhs)                    \
    const->tainted<decltype(std::declval<T>() opSymbol std::declval<           \
                            detail::rlbox_remove_wrapper_t<T_Rhs>>()),         \
                   T_Sbx>                                                      \
  {                                                                            \
    static_assert(detail::is_basic_type_v<T>,                                  \
                  "Operator " #opSymbol                                        \
                  " only supported for primitive and pointer types");          \
                                                                               \
    auto raw_rhs = detail::unwrap_value(rhs);                                  \
                                                                               \
    if constexpr (std::is_pointer_v<T>) {                                      \
      static_assert(std::is_integral_v<decltype(raw_rhs)>,                     \
                    "Can only operate on numeric types");                      \
      auto ptr = impl().get_raw_value();                                       \
      detail::dynamic_check(ptr != nullptr,                                    \
                            "Pointer arithmetic on a null pointer");           \
      /* increment the target by size of the data structure */                 \
      auto target =                                                            \
        reinterpret_cast<uintptr_t>(ptr) opSymbol raw_rhs * sizeof(*impl());   \
      auto no_overflow = rlbox_sandbox<T_Sbx>::is_in_same_sandbox(             \
        reinterpret_cast<const void*>(ptr),                                    \
        reinterpret_cast<const void*>(target));                                \
      detail::dynamic_check(                                                   \
        no_overflow,                                                           \
        "Pointer arithmetic overflowed a pointer beyond sandbox memory");      \
                                                                               \
      return tainted<T, T_Sbx>::internal_factory(reinterpret_cast<T>(target)); \
    } else {                                                                   \
      auto raw = impl().get_raw_value();                                       \
      auto ret = raw opSymbol raw_rhs;                                         \
      using T_Ret = decltype(ret);                                             \
      return tainted<T_Ret, T_Sbx>::internal_factory(ret);                     \
    }                                                                          \
  }                                                                            \
  RLBOX_REQUIRE_SEMI_COLON

  BinaryOpValAndPtr(+);
  BinaryOpValAndPtr(-);

#undef BinaryOpValAndPtr

#define BinaryOp(opSymbol)                                                     \
  template<typename T_Rhs>                                                     \
  inline constexpr auto operator opSymbol(const T_Rhs& rhs)                    \
    const->tainted<decltype(std::declval<T>() opSymbol std::declval<           \
                            detail::rlbox_remove_wrapper_t<T_Rhs>>()),         \
                   T_Sbx>                                                      \
  {                                                                            \
    static_assert(detail::is_fundamental_or_enum_v<T>,                         \
                  "Operator " #opSymbol                                        \
                  " only supported for primitive  types");                     \
                                                                               \
    auto raw = impl().get_raw_value();                                         \
    auto raw_rhs = detail::unwrap_value(rhs);                                  \
    static_assert(std::is_integral_v<decltype(raw_rhs)> ||                     \
                    std::is_floating_point_v<decltype(raw_rhs)>,               \
                  "Can only operate on numeric types");                        \
                                                                               \
    auto ret = raw opSymbol raw_rhs;                                           \
    using T_Ret = decltype(ret);                                               \
    return tainted<T_Ret, T_Sbx>::internal_factory(ret);                       \
  }                                                                            \
  RLBOX_REQUIRE_SEMI_COLON

  BinaryOp(*);
  BinaryOp(/);
  BinaryOp(%);
  BinaryOp(^);
  BinaryOp(&);
  BinaryOp(|);
  BinaryOp(<<);
  BinaryOp(>>);

#undef BinaryOp

#define CompoundAssignmentOp(opSymbol)                                         \
  template<typename T_Rhs>                                                     \
  inline constexpr T_Wrap<T, T_Sbx>& operator opSymbol##=(const T_Rhs& rhs)    \
  {                                                                            \
    auto& this_ref = impl();                                                   \
    this_ref = this_ref opSymbol rhs;                                          \
    return this_ref;                                                           \
  }                                                                            \
  RLBOX_REQUIRE_SEMI_COLON

  CompoundAssignmentOp(+);
  CompoundAssignmentOp(-);
  CompoundAssignmentOp(*);
  CompoundAssignmentOp(/);
  CompoundAssignmentOp(%);
  CompoundAssignmentOp(^);
  CompoundAssignmentOp(&);
  CompoundAssignmentOp(|);
  CompoundAssignmentOp(<<);
  CompoundAssignmentOp(>>);

#undef CompoundAssignmentOp

#define PreIncDecOps(opSymbol)                                                 \
  inline constexpr T_Wrap<T, T_Sbx>& operator opSymbol##opSymbol()             \
  {                                                                            \
    auto& this_ref = impl();                                                   \
    this_ref = this_ref opSymbol 1;                                            \
    return this_ref;                                                           \
  }                                                                            \
  RLBOX_REQUIRE_SEMI_COLON

  PreIncDecOps(+);
  PreIncDecOps(-);

#undef PreIncDecOps

#define PostIncDecOps(opSymbol)                                                \
  inline constexpr T_Wrap<T, T_Sbx> operator opSymbol##opSymbol(int)           \
  {                                                                            \
    tainted<T, T_Sbx> ret = impl();                                            \
    operator opSymbol##opSymbol();                                                              \
    return ret;                                                                \
  }                                                                            \
  RLBOX_REQUIRE_SEMI_COLON

  PostIncDecOps(+);
  PostIncDecOps(-);

#undef PostIncDecOps

#define BooleanBinaryOp(opSymbol)                                              \
  template<typename T_Rhs>                                                     \
  inline constexpr auto operator opSymbol(const T_Rhs& rhs)                    \
    const->tainted<decltype(std::declval<T>() opSymbol std::declval<           \
                            detail::rlbox_remove_wrapper_t<T_Rhs>>()),         \
                   T_Sbx>                                                      \
  {                                                                            \
    static_assert(detail::is_fundamental_or_enum_v<T>,                         \
                  "Operator " #opSymbol                                        \
                  " only supported for primitive  types");                     \
                                                                               \
    auto raw = impl().get_raw_value();                                         \
    auto raw_rhs = detail::unwrap_value(rhs);                                  \
    static_assert(std::is_integral_v<decltype(raw_rhs)>,                       \
                  "Can only operate on numeric types");                        \
                                                                               \
    auto ret = raw opSymbol raw_rhs;                                           \
    using T_Ret = decltype(ret);                                               \
    return tainted<T_Ret, T_Sbx>::internal_factory(ret);                       \
  }                                                                            \
                                                                               \
  template<typename T_Rhs>                                                     \
  inline constexpr auto operator opSymbol(const T_Rhs&&)                       \
    const->tainted<decltype(std::declval<T>() opSymbol std::declval<           \
                            detail::rlbox_remove_wrapper_t<T_Rhs>>()),         \
                   T_Sbx>                                                      \
  {                                                                            \
    rlbox_detail_static_fail_because(                                          \
      detail::true_v<T_Rhs>,                                                   \
      "C++ does not permit safe overloading of && and || operations as this "  \
      "affects the short circuiting behaviour of these operations. RLBox "     \
      "does let you use && and || with tainted in limited situations - when "  \
      "all arguments starting from the second are local variables. It does "   \
      "not allow it if arguments starting from the second  are expressions.\n" \
      "For example the following is not allowed\n"                             \
      "\n"                                                                     \
      "tainted<bool, T_Sbx> a = true;\n"                                       \
      "auto r = a && true && sandbox.invoke_sandbox_function(getBool);\n"      \
      "\n"                                                                     \
      "However the following would be allowed\n"                               \
      "tainted<bool, T_Sbx> a = true;\n"                                       \
      "auto b = true\n"                                                        \
      "auto c = sandbox.invoke_sandbox_function(getBool);\n"                   \
      "auto r = a && b && c;\n"                                                \
      "\n"                                                                     \
      "Note that these 2 programs are not identical. The first program may "   \
      "or may not call getBool, while second program always calls getBool");   \
    return tainted<bool, T_Sbx>(false);                                        \
  }                                                                            \
  RLBOX_REQUIRE_SEMI_COLON

  BooleanBinaryOp(&&);
  BooleanBinaryOp(||);

#undef BooleanBinaryOp

#define UnaryOp(opSymbol)                                                      \
  inline auto operator opSymbol()                                              \
  {                                                                            \
    static_assert(detail::is_fundamental_or_enum_v<T>,                         \
                  "Operator " #opSymbol " only supported for primitive");      \
                                                                               \
    auto raw = impl().get_raw_value();                                         \
    auto ret = opSymbol raw;                                                   \
    using T_Ret = decltype(ret);                                               \
    return tainted<T_Ret, T_Sbx>::internal_factory(ret);                       \
  }                                                                            \
  RLBOX_REQUIRE_SEMI_COLON

  UnaryOp(-);
  UnaryOp(~);

#undef UnaryOp

/**
 * @brief Comparison operators. Comparisons to values in sandbox memory can
 * only return a "tainted_boolean_hint" as the values in memory can be
 * incorrect or malicously change in the future.
 *
 * @tparam T_Rhs
 * @param rhs
 * @return One of either a bool, tainted<bool>, or a tainted_boolean_hint
 * depending on the arguments to the binary expression.
 */
#define CompareOp(opSymbol, permit_pointers)                                   \
  template<typename T_Rhs>                                                     \
  inline constexpr auto operator opSymbol(const T_Rhs& rhs) const              \
  {                                                                            \
    using T_RhsNoQ = detail::remove_cv_ref_t<T_Rhs>;                           \
    constexpr bool check_rhs_hint =                                            \
      detail::rlbox_is_tainted_volatile_v<T_RhsNoQ> ||                         \
      detail::rlbox_is_tainted_boolean_hint_v<T_RhsNoQ>;                       \
    constexpr bool check_lhs_hint =                                            \
      detail::rlbox_is_tainted_volatile_v<T_Wrap<T, T_Sbx>>;                   \
    constexpr bool is_hint = check_lhs_hint || check_rhs_hint;                 \
                                                                               \
    constexpr bool is_unwrapped =                                              \
      detail::rlbox_is_tainted_v<T_Wrap<T, T_Sbx>> &&                          \
      std::is_null_pointer_v<T_RhsNoQ>;                                        \
                                                                               \
    /* Sanity check - can't be a hint and unwrapped */                         \
    static_assert(is_hint ? !is_unwrapped : true,                              \
                  "Internal error: Could not deduce type for comparison. "     \
                  "Please file a bug.");                                       \
                                                                               \
    if constexpr (!permit_pointers && std::is_pointer_v<T>) {                  \
      rlbox_detail_static_fail_because(                                        \
        std::is_pointer_v<T>,                                                  \
        "Only == and != comparisons are allowed for pointers");                \
    }                                                                          \
                                                                               \
    bool ret = (impl().get_raw_value() opSymbol detail::unwrap_value(rhs));    \
                                                                               \
    if constexpr (is_hint) {                                                   \
      return tainted_boolean_hint(ret);                                        \
    } else if constexpr (is_unwrapped) {                                       \
      return ret;                                                              \
    } else {                                                                   \
      return tainted<bool, T_Sbx>(ret);                                        \
    }                                                                          \
  }                                                                            \
  RLBOX_REQUIRE_SEMI_COLON

  CompareOp(==, true /* permit_pointers */);
  CompareOp(!=, true /* permit_pointers */);
  CompareOp(<, false /* permit_pointers */);
  CompareOp(<=, false /* permit_pointers */);
  CompareOp(>, false /* permit_pointers */);
  CompareOp(>=, false /* permit_pointers */);

#undef CompareOp

private:
  using T_OpSubscriptArrRet = std::conditional_t<
    std::is_pointer_v<T>,
    tainted_volatile<detail::dereference_result_t<T>, T_Sbx>, // is_pointer
    T_Wrap<detail::dereference_result_t<T>, T_Sbx>            // is_array
    >;

public:
  template<typename T_Rhs>
  inline const T_OpSubscriptArrRet& operator[](T_Rhs&& rhs) const
  {
    static_assert(std::is_pointer_v<T> || detail::is_c_or_std_array_v<T>,
                  "Operator [] supports pointers and arrays only");

    auto raw_rhs = detail::unwrap_value(rhs);
    static_assert(std::is_integral_v<decltype(raw_rhs)>,
                  "Can only index with numeric types");

    if constexpr (std::is_pointer_v<T>) {
      auto ptr = this->impl().get_raw_value();

      // increment the target by size of the data structure
      auto target =
        reinterpret_cast<uintptr_t>(ptr) + raw_rhs * sizeof(*this->impl());
      auto no_overflow = rlbox_sandbox<T_Sbx>::is_in_same_sandbox(
        ptr, reinterpret_cast<const void*>(target));
      detail::dynamic_check(
        no_overflow,
        "Pointer arithmetic overflowed a pointer beyond sandbox memory");

      auto target_wrap = tainted<const T, T_Sbx>::internal_factory(
        reinterpret_cast<const T>(target));
      return *target_wrap;
    } else {
      using T_Rhs_Unsigned = std::make_unsigned_t<decltype(raw_rhs)>;
      detail::dynamic_check(
        raw_rhs >= 0 && static_cast<T_Rhs_Unsigned>(raw_rhs) <
                          std::extent_v<detail::std_array_to_c_arr_t<T>, 0>,
        "Static array indexing overflow");

      const void* target_ptr;
      if constexpr (detail::rlbox_is_tainted_v<T_Wrap<T, T_Sbx>>) {
        auto& data_ref = impl().get_raw_value_ref();
        target_ptr = &(data_ref[raw_rhs]);
      } else {
        auto& data_ref = impl().get_sandbox_value_ref();
        auto target_ptr_vol = &(data_ref[raw_rhs]);
        // target_ptr is a volatile... remove this.
        // Safe as we will return a tainted_volatile if this is the case
        target_ptr = detail::remove_volatile_from_ptr_cast(target_ptr_vol);
      }

      using T_Target = const T_Wrap<detail::dereference_result_t<T>, T_Sbx>;
      auto wrapped_target_ptr = reinterpret_cast<T_Target*>(target_ptr);
      return *wrapped_target_ptr;
    }
  }

  template<typename T_Rhs>
  inline T_OpSubscriptArrRet& operator[](T_Rhs&& rhs)
  {
    return const_cast<T_OpSubscriptArrRet&>(std::as_const(*this)[rhs]);
  }

private:
  using T_OpDerefRet = tainted_volatile<std::remove_pointer_t<T>, T_Sbx>;

public:
  inline T_OpDerefRet& operator*() const
  {
    static_assert(std::is_pointer_v<T>, "Operator * only allowed on pointers");
    auto ret_ptr_const =
      reinterpret_cast<const T_OpDerefRet*>(impl().get_raw_value());
    // Safe - If T_OpDerefRet is not a const ptr, this is trivially safe
    //        If T_OpDerefRet is a const ptr, then the const is captured
    //        inside the wrapper
    auto ret_ptr = const_cast<T_OpDerefRet*>(ret_ptr_const);
    return *ret_ptr;
  }

  // We need to implement the -> operator even if T is not a struct
  // So that we can support code patterns such as the below
  // tainted<T*> a;
  // a->UNSAFE_unverified();
  inline const T_OpDerefRet* operator->() const
  {
    static_assert(std::is_pointer_v<T>,
                  "Operator -> only supported for pointer types");
    return reinterpret_cast<const T_OpDerefRet*>(impl().get_raw_value());
  }

  inline T_OpDerefRet* operator->()
  {
    return const_cast<T_OpDerefRet*>(std::as_const(*this).operator->());
  }

  inline auto operator!()
  {
    if_constexpr_named(cond1, std::is_pointer_v<T>)
    {
      return impl() == nullptr;
    }
    else if_constexpr_named(cond2, std::is_same_v<std::remove_cv_t<T>, bool>)
    {
      return impl() == false;
    }
    else
    {
      auto unknownCase = !(cond1 || cond2);
      rlbox_detail_static_fail_because(
        unknownCase,
        "Operator ! only permitted for pointer or boolean types. For other"
        "types, unwrap the tainted value with the copy_and_verify API and then"
        "use operator !");
    }
  }

  /**
   * @brief Copy tainted value from sandbox and verify it.
   *
   * @param verifier Function used to verify the copied value.
   * @tparam T_Func the type of the verifier.
   * @return Whatever the verifier function returns.
   */
  template<typename T_Func>
  inline auto copy_and_verify(T_Func verifier) const
  {
    using T_Deref = std::remove_cv_t<std::remove_pointer_t<T>>;

    if_constexpr_named(cond1, detail::is_fundamental_or_enum_v<T>)
    {
      auto val = impl().get_raw_value();
      return verifier(val);
    }
    else if_constexpr_named(
      cond2, detail::is_one_level_ptr_v<T> && !std::is_class_v<T_Deref>)
    {
      // Some paths don't use the verifier
      RLBOX_UNUSED(verifier);

      if_constexpr_named(subcond1, std::is_void_v<T_Deref>)
      {
        rlbox_detail_static_fail_because(
          subcond1,
          "copy_and_verify not recommended for void* as it could lead to some "
          "anti-patterns in verifiers. Cast it to a different tainted pointer "
          "with sandbox_reinterpret_cast and then call copy_and_verify. "
          "Alternately, you can use the UNSAFE_unverified API to do this "
          "without casting.");
        return nullptr;
      }
      // Test with detail::is_func_ptr_v to check for member funcs also
      else if_constexpr_named(subcond2, detail::is_func_ptr_v<T>)
      {
        rlbox_detail_static_fail_because(
          subcond2,
          "copy_and_verify cannot be applied to function pointers as this "
          "makes a deep copy. This is not possible for function pointers. "
          "Consider copy_and_verify_address instead.");
        return nullptr;
      }
      else
      {
        auto val = impl().get_raw_value();
        if (val == nullptr) {
          return verifier(nullptr);
        } else {
          // Important to assign to a local variable (i.e. make a copy)
          // Else, for tainted_volatile, this will allow a
          // time-of-check-time-of-use attack
          auto val_copy = std::make_unique<T_Deref>();
          *val_copy = *val;
          return verifier(std::move(val_copy));
        }
      }
    }
    else if_constexpr_named(
      cond3, detail::is_one_level_ptr_v<T> && std::is_class_v<T_Deref>)
    {
      auto val_copy = std::make_unique<tainted<T_Deref, T_Sbx>>(*impl());
      return verifier(std::move(val_copy));
    }
    else if_constexpr_named(cond4, std::is_array_v<T>)
    {
      static_assert(
        detail::is_fundamental_or_enum_v<std::remove_all_extents_t<T>>,
        "copy_and_verify on arrays is only safe for fundamental or enum types. "
        "For arrays of other types, apply copy_and_verify on each element "
        "individually --- a[i].copy_and_verify(...)");

      auto copy = impl().get_raw_value();
      return verifier(copy);
    }
    else
    {
      auto unknownCase = !(cond1 || cond2 || cond3 || cond4);
      rlbox_detail_static_fail_because(
        unknownCase,
        "copy_and_verify not supported for this type as it may be unsafe");
    }
  }

private:
  using T_CopyAndVerifyRangeEl =
    detail::valid_array_el_t<std::remove_cv_t<std::remove_pointer_t<T>>>;

  // Template needed to ensure that function isn't instantiated for unsupported
  // types like function pointers which causes compile errors...
  template<typename T2 = T>
  inline const void* verify_range_helper(std::size_t count) const
  {
    static_assert(std::is_pointer_v<T>);
    static_assert(detail::is_fundamental_or_enum_v<T_CopyAndVerifyRangeEl>);

    detail::dynamic_check(
      count != 0,
      "Called copy_and_verify_range/copy_and_verify_string with count 0");

    auto start = reinterpret_cast<const void*>(impl().get_raw_value());
    if (start == nullptr) {
      return nullptr;
    }

    detail::check_range_doesnt_cross_app_sbx_boundary<T_Sbx>(
      start, count * sizeof(T_CopyAndVerifyRangeEl));

    return start;
  }

  template<typename T2 = T>
  inline std::unique_ptr<T_CopyAndVerifyRangeEl[]> copy_and_verify_range_helper(
    std::size_t count) const
  {
    const void* start = verify_range_helper(count);
    if (start == nullptr) {
      return nullptr;
    }

    auto target = std::make_unique<T_CopyAndVerifyRangeEl[]>(count);

    for (size_t i = 0; i < count; i++) {
      auto p_src_i_tainted = &(impl()[i]);
      auto p_src_i = p_src_i_tainted.get_raw_value();
      detail::convert_type_fundamental_or_array(target[i], *p_src_i);
    }

    return target;
  }

public:
  /**
   * @brief Copy a range of tainted values from sandbox and verify them.
   *
   * @param verifier Function used to verify the copied value.
   * @param count Number of elements to copy.
   * @tparam T_Func the type of the verifier. If the tainted type is ``int*``
   * then ``T_Func = T_Ret(*)(unique_ptr<int[]>)``.
   * @return Whatever the verifier function returns.
   */
  template<typename T_Func>
  inline auto copy_and_verify_range(T_Func verifier, std::size_t count) const
  {
    static_assert(std::is_pointer_v<T>,
                  "Can only call copy_and_verify_range on pointers");

    static_assert(
      detail::is_fundamental_or_enum_v<T_CopyAndVerifyRangeEl>,
      "copy_and_verify_range is only safe for ranges of "
      "fundamental or enum types. For other types, call "
      "copy_and_verify on each element --- a[i].copy_and_verify(...)");

    std::unique_ptr<T_CopyAndVerifyRangeEl[]> target =
      copy_and_verify_range_helper(count);
    return verifier(std::move(target));
  }

  /**
   * @brief Copy a tainted string from sandbox and verify it.
   *
   * @param verifier Function used to verify the copied value.
   * @tparam T_Func the type of the verifier either
   * ``T_Ret(*)(unique_ptr<char[]>)`` or ``T_Ret(*)(std::string)``
   * @return Whatever the verifier function returns.
   */
  template<typename T_Func>
  inline auto copy_and_verify_string(T_Func verifier) const
  {
    static_assert(std::is_pointer_v<T>,
                  "Can only call copy_and_verify_string on pointers");

    static_assert(std::is_same_v<char, T_CopyAndVerifyRangeEl>,
                  "copy_and_verify_string only allows char*");

    using T_VerifParam = detail::func_first_arg_t<T_Func>;

    auto start = impl().get_raw_value();
    if_constexpr_named(
      cond1,
      std::is_same_v<T_VerifParam, std::unique_ptr<char[]>> ||
        std::is_same_v<T_VerifParam, std::unique_ptr<const char[]>>)
    {
      if (start == nullptr) {
        return verifier(nullptr);
      }

      // it is safe to run strlen on a tainted<string> as worst case, the string
      // does not have a null and we try to copy all the memory out of the
      // sandbox however, copy_and_verify_range ensures that we never copy
      // memory outsider the range
      auto str_len = std::strlen(start) + 1;
      std::unique_ptr<T_CopyAndVerifyRangeEl[]> target =
        copy_and_verify_range_helper(str_len);

      // ensure the string has a trailing null
      target[str_len - 1] = '\0';

      return verifier(std::move(target));
    }
    else if_constexpr_named(cond2, std::is_same_v<T_VerifParam, std::string>)
    {
      if (start == nullptr) {
        std::string param = "";
        return verifier(param);
      }

      // it is safe to run strlen on a tainted<string> as worst case, the string
      // does not have a null and we try to copy all the memory out of the
      // sandbox however, copy_and_verify_range ensures that we never copy
      // memory outsider the range
      auto str_len = std::strlen(start) + 1;

      const char* checked_start = (const char*)verify_range_helper(str_len);
      if (checked_start == nullptr) {
        std::string param = "";
        return verifier(param);
      }

      std::string copy(checked_start, str_len - 1);
      return verifier(std::move(copy));
    }
    else
    {
      constexpr bool unknownCase = !(cond1 || cond2);
      rlbox_detail_static_fail_because(
        unknownCase,
        "copy_and_verify_string verifier parameter should either be "
        "unique_ptr<char[]>, unique_ptr<const char[]> or std::string");
    }
  }

  /**
   * @brief Copy a tainted pointer from sandbox and verify the address.
   *
   * This function is useful if you need to verify physical bits representing
   * the address of a pointer. Other APIs such as copy_and_verify performs a
   * deep copy and changes the address bits.
   *
   * @param verifier Function used to verify the copied value.
   * @tparam T_Func the type of the verifier ``T_Ret(*)(uintptr_t)``
   * @return Whatever the verifier function returns.
   */
  template<typename T_Func>
  inline auto copy_and_verify_address(T_Func verifier) const
  {
    static_assert(std::is_pointer_v<T>,
                  "copy_and_verify_address must be used on pointers");
    auto val = reinterpret_cast<uintptr_t>(impl().get_raw_value());
    return verifier(val);
  }

  /**
   * @brief Copy a tainted pointer to a buffer from sandbox and verify the
   * address.
   *
   * This function is useful if you need to verify physical bits representing
   * the address of a buffer. Other APIs such as copy_and_verify performs a
   * deep copy and changes the address bits.
   *
   * @param verifier Function used to verify the copied value.
   * @param size Size of the buffer. Buffer with length size is expected to fit
   * inside sandbox memory.
   * @tparam T_Func the type of the verifier ``T_Ret(*)(uintptr_t)``
   * @return Whatever the verifier function returns.
   */
  template<typename T_Func>
  inline auto copy_and_verify_buffer_address(T_Func verifier,
                                             std::size_t size) const
  {
    static_assert(std::is_pointer_v<T>,
                  "copy_and_verify_address must be used on pointers");
    auto val = reinterpret_cast<uintptr_t>(verify_range_helper(size));
    return verifier(val);
  }
};

#define BinaryOpWrappedRhs(opSymbol)                                           \
  template<template<typename, typename> typename T_Wrap,                       \
           typename T,                                                         \
           typename T_Sbx,                                                     \
           typename T_Lhs,                                                     \
           RLBOX_ENABLE_IF(!detail::rlbox_is_wrapper_v<T_Lhs> &&               \
                           !detail::rlbox_is_tainted_boolean_hint_v<T_Lhs>)>   \
  inline constexpr auto operator opSymbol(                                     \
    const T_Lhs& lhs, const tainted_base_impl<T_Wrap, T, T_Sbx>& rhs)          \
  {                                                                            \
    /* Handles the case for "3 + tainted", where + is a binary op */           \
    /* Technically pointer arithmetic can be performed as 3 + tainted_ptr */   \
    /* as well. However, this is unusual and to keep the code simple we do */  \
    /* not support this. */                                                    \
    static_assert(                                                             \
      std::is_arithmetic_v<T_Lhs>,                                             \
      "Binary expressions between an non tainted type and tainted"             \
      "type is only permitted if the first value is the tainted type. Try "    \
      "changing the order of the binary expression accordingly");              \
    auto ret = tainted<T_Lhs, T_Sbx>(lhs) opSymbol rhs.impl();                 \
    return ret;                                                                \
  }                                                                            \
  RLBOX_REQUIRE_SEMI_COLON

BinaryOpWrappedRhs(+);
BinaryOpWrappedRhs(-);
BinaryOpWrappedRhs(*);
BinaryOpWrappedRhs(/);
BinaryOpWrappedRhs(%);
BinaryOpWrappedRhs(^);
BinaryOpWrappedRhs(&);
BinaryOpWrappedRhs(|);
BinaryOpWrappedRhs(<<);
BinaryOpWrappedRhs(>>);

// GCC10.1 to GCC13 has a bug where it selects the wrong overload of operator==
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=114549 . However C++ 20 also
// provides reverse comparisons by default, and so we no longer need to define
// them. Thus, from C++ 20, we do not define the reverse operators.
#if __cplusplus < 202002L
BinaryOpWrappedRhs(==);
BinaryOpWrappedRhs(!=);
BinaryOpWrappedRhs(<);
BinaryOpWrappedRhs(<=);
BinaryOpWrappedRhs(>);
BinaryOpWrappedRhs(>=);
#endif

#undef BinaryOpWrappedRhs

#define BooleanBinaryOpWrappedRhs(opSymbol)                                    \
  template<template<typename, typename> typename T_Wrap,                       \
           typename T,                                                         \
           typename T_Sbx,                                                     \
           typename T_Lhs,                                                     \
           RLBOX_ENABLE_IF(!detail::rlbox_is_wrapper_v<T_Lhs> &&               \
                           !detail::rlbox_is_tainted_boolean_hint_v<T_Lhs>)>   \
  inline constexpr auto operator opSymbol(                                     \
    const T_Lhs& lhs, const tainted_base_impl<T_Wrap, T, T_Sbx>& rhs)          \
  {                                                                            \
    static_assert(                                                             \
      std::is_arithmetic_v<T_Lhs>,                                             \
      "Binary expressions between an non tainted type and tainted"             \
      "type is only permitted if the first value is the tainted type. Try "    \
      "changing the order of the binary expression accordingly");              \
    auto ret = tainted<T_Lhs, T_Sbx>(lhs) opSymbol rhs.impl();                 \
    return ret;                                                                \
  }                                                                            \
                                                                               \
  template<template<typename, typename> typename T_Wrap,                       \
           typename T,                                                         \
           typename T_Sbx,                                                     \
           typename T_Lhs,                                                     \
           RLBOX_ENABLE_IF(!detail::rlbox_is_wrapper_v<T_Lhs> &&               \
                           !detail::rlbox_is_tainted_boolean_hint_v<T_Lhs>)>   \
  inline constexpr auto operator opSymbol(                                     \
    const T_Lhs&, const tainted_base_impl<T_Wrap, T, T_Sbx>&&)                 \
  {                                                                            \
    rlbox_detail_static_fail_because(                                          \
      detail::true_v<T_Lhs>,                                                   \
      "C++ does not permit safe overloading of && and || operations as this "  \
      "affects the short circuiting behaviour of these operations. RLBox "     \
      "does let you use && and || with tainted in limited situations - when "  \
      "all arguments starting from the second are local variables. It does "   \
      "not allow it if arguments starting from the second  are expressions.\n" \
      "For example the following is not allowed\n"                             \
      "\n"                                                                     \
      "tainted<bool, T_Sbx> a = true;\n"                                       \
      "auto r = a && true && sandbox.invoke_sandbox_function(getBool);\n"      \
      "\n"                                                                     \
      "However the following would be allowed\n"                               \
      "tainted<bool, T_Sbx> a = true;\n"                                       \
      "auto b = true\n"                                                        \
      "auto c = sandbox.invoke_sandbox_function(getBool);\n"                   \
      "auto r = a && b && c;\n"                                                \
      "\n"                                                                     \
      "Note that these 2 programs are not identical. The first program may "   \
      "or may not call getBool, while second program always calls getBool");   \
    return tainted<bool, T_Sbx>(false);                                        \
  }                                                                            \
  RLBOX_REQUIRE_SEMI_COLON

BooleanBinaryOpWrappedRhs(&&);
BooleanBinaryOpWrappedRhs(||);
#undef BooleanBinaryOpWrappedRhs

namespace tainted_detail {
  template<typename T, typename T_Sbx>
  using tainted_repr_t = detail::c_to_std_array_t<T>;

  template<typename T, typename T_Sbx>
  using tainted_vol_repr_t =
    detail::c_to_std_array_t<std::add_volatile_t<typename rlbox_sandbox<
      T_Sbx>::template convert_to_sandbox_equivalent_nonclass_t<T>>>;
}

/**
 * @brief Tainted values represent untrusted values that originate from the
 * sandbox.
 */
template<typename T, typename T_Sbx>
class tainted : public tainted_base_impl<tainted, T, T_Sbx>
{
  KEEP_CLASSES_FRIENDLY
  KEEP_CAST_FRIENDLY

  // Classes recieve their own specialization
  static_assert(
    !std::is_class_v<T>,
    "Missing definition for class T. This error occurs for one "
    "of 2 reasons.\n"
    "  1) Make sure you have include a call rlbox_load_structs_from_library "
    "for this library with this class included.\n"
    "  2) Make sure you run (re-run) the struct-dump tool to list "
    "all structs in use by your program.\n");

  static_assert(
    detail::is_basic_type_v<T> || std::is_array_v<T>,
    "Tainted types only support fundamental, enum, pointer, array and struct "
    "types. Please file a bug if more support is needed.");

private:
  using T_ClassBase = tainted_base_impl<tainted, T, T_Sbx>;
  using T_AppType = tainted_detail::tainted_repr_t<T, T_Sbx>;
  using T_SandboxedType = tainted_detail::tainted_vol_repr_t<T, T_Sbx>;
  T_AppType data;

  inline auto& get_raw_value_ref() noexcept { return data; }
  inline auto& get_raw_value_ref() const noexcept { return data; }

  inline std::remove_cv_t<T_AppType> get_raw_value() const noexcept
  {
    return data;
  }

  inline std::remove_cv_t<T_SandboxedType> get_raw_sandbox_value(
    rlbox_sandbox<T_Sbx>& sandbox) const
  {
    std::remove_cv_t<T_SandboxedType> ret;

    using namespace detail;
    convert_type_non_class<T_Sbx,
                           adjust_type_direction::TO_SANDBOX,
                           adjust_type_context::SANDBOX>(
      ret, data, nullptr /* example_unsandboxed_ptr */, &sandbox);
    return ret;
  };

  inline const void* find_example_pointer_or_null() const noexcept
  {
    if constexpr (std::is_array_v<T>) {
      auto& data_ref = get_raw_value_ref();

      for (size_t i = 0; i < std::extent_v<T>; i++) {
        const void* ret = data[i].find_example_pointer_or_null();
        if (ret != nullptr) {
          return ret;
        }
      }
    } else if constexpr (std::is_pointer_v<T> && !detail::is_func_ptr_v<T>) {
      auto data = get_raw_value();
      return data;
    }
    return nullptr;
  }

  // Initializing with a pointer is dangerous and permitted only internally
  template<typename T2 = T, RLBOX_ENABLE_IF(std::is_pointer_v<T2>)>
  tainted(T2 val, const void* /* internal_tag */)
    : data(val)
  {
    // Sanity check
    static_assert(std::is_pointer_v<T>);
  }

  template<typename T_Rhs>
  static inline tainted<T, T_Sbx> internal_factory(T_Rhs&& rhs)
  {
    if constexpr (std::is_pointer_v<std::remove_reference_t<T_Rhs>>) {
      const void* internal_tag = nullptr;
      return tainted(std::forward<T_Rhs>(rhs), internal_tag);
    } else {
      return tainted(std::forward<T_Rhs>(rhs));
    }
  }

public:
  tainted() = default;
  tainted(const tainted<T, T_Sbx>& p) = default;

  tainted(const tainted_volatile<T, T_Sbx>& p)
  {
    // Need to construct an example_unsandboxed_ptr for pointers or arrays of
    // pointers. Since tainted_volatile is the type of data in sandbox memory,
    // the address of data (&data) refers to a location in sandbox memory and
    // can thus be the example_unsandboxed_ptr
    const volatile void* p_data_ref = &p.get_sandbox_value_ref();
    const void* example_unsandboxed_ptr = const_cast<const void*>(p_data_ref);
    using namespace detail;
    convert_type_non_class<T_Sbx,
                           adjust_type_direction::TO_APPLICATION,
                           adjust_type_context::EXAMPLE>(
      get_raw_value_ref(),
      p.get_sandbox_value_ref(),
      example_unsandboxed_ptr,
      nullptr /* sandbox_ptr */);
  }

  // Initializing with a pointer is dangerous and permitted only internally
  template<typename T2 = T, RLBOX_ENABLE_IF(std::is_pointer_v<T2>)>
  tainted(T2 val)
    : data(val)
  {
    rlbox_detail_static_fail_because(
      std::is_pointer_v<T2>,
      "Assignment of pointers is not safe as it could\n "
      "1) Leak pointers from the appliction to the sandbox which may break "
      "ASLR\n "
      "2) Pass inaccessible pointers to the sandbox leading to crash\n "
      "3) Break sandboxes that require pointers to be swizzled first\n "
      "\n "
      "Instead, if you want to pass in a pointer, do one of the following\n "
      "1) Allocate with malloc_in_sandbox, and pass in a tainted pointer\n "
      "2) For pointers that point to functions in the application, register "
      "with sandbox.register_callback(\"foo\"), and pass in the registered "
      "value\n "
      "3) For pointers that point to functions in the sandbox, get the "
      "address with get_sandbox_function_address(sandbox, foo), and pass in "
      "the "
      "address\n "
      "4) For raw pointers, use assign_raw_pointer which performs required "
      "safety checks\n ");
  }

  tainted(
    const sandbox_callback<
      detail::function_ptr_t<T> // Need to ensure we never generate code that
                                // creates a sandbox_callback of a non function
      ,
      T_Sbx>&)
  {
    rlbox_detail_static_fail_because(
      detail::true_v<T>,
      "RLBox does not support assigning sandbox_callback values to tainted "
      "types (i.e. types that live in application memory).\n"
      "If you still want to do this, consider changing your code to store the "
      "value in sandbox memory as follows. Convert\n\n"
      "sandbox_callback<T_Func, Sbx> cb = ...;\n"
      "tainted<T_Func, Sbx> foo = cb;\n\n"
      "to\n\n"
      "tainted<T_Func*, Sbx> foo_ptr = sandbox.malloc_in_sandbox<T_Func*>();\n"
      "*foo_ptr = cb;\n\n"
      "This would keep the assignment in sandbox memory");
  }

  tainted(const std::nullptr_t& arg)
    : data(arg)
  {
    static_assert(std::is_pointer_v<T>);
  }

  // We explicitly disable this constructor if it has one of the signatures
  // above, so that we give the above constructors a higher priority. We only
  // allow this for fundamental types as this is potentially unsafe for pointers
  // and structs
  template<typename T_Arg,
           RLBOX_ENABLE_IF(
             !detail::rlbox_is_wrapper_v<std::remove_reference_t<T_Arg>> &&
             detail::is_fundamental_or_enum_v<T> &&
             detail::is_fundamental_or_enum_v<std::remove_reference_t<T_Arg>>)>
  tainted(T_Arg&& arg)
    : data(std::forward<T_Arg>(arg))
  {}

  template<typename T_Rhs>
  void assign_raw_pointer(rlbox_sandbox<T_Sbx>& sandbox, T_Rhs val)
  {
    static_assert(std::is_pointer_v<T_Rhs>, "Must be a pointer");
    static_assert(std::is_assignable_v<T&, T_Rhs>,
                  "Should assign pointers of compatible types.");
    // Maybe a function pointer, so we need to cast
    const void* cast_val = reinterpret_cast<const void*>(val);
    bool safe = sandbox.is_pointer_in_sandbox_memory(cast_val);
    detail::dynamic_check(
      safe,
      "Tried to assign a pointer that is not in the sandbox.\n "
      "This is not safe as it could\n "
      "1) Leak pointers from the appliction to the sandbox which may break "
      "ASLR\n "
      "2) Pass inaccessible pointers to the sandbox leading to crash\n "
      "3) Break sandboxes that require pointers to be swizzled first\n "
      "\n "
      "Instead, if you want to pass in a pointer, do one of the following\n "
      "1) Allocate with malloc_in_sandbox, and pass in a tainted pointer\n "
      "2) For pointers that point to functions in the application, register "
      "with sandbox.register_callback(\"foo\"), and pass in the registered "
      "value\n "
      "3) For pointers that point to functions in the sandbox, get the "
      "address with get_sandbox_function_address(sandbox, foo), and pass in "
      "the "
      "address\n ");
    data = val;
  }

  inline tainted_opaque<T, T_Sbx> to_opaque()
  {
    return *reinterpret_cast<tainted_opaque<T, T_Sbx>*>(this);
  }

  template<typename T_Dummy = void>
  operator bool() const
  {
    if_constexpr_named(cond1, std::is_pointer_v<T>)
    {
      // We return this without the tainted wrapper as the checking for null
      // doesn't really "induce" tainting in the application If the
      // application is checking this pointer for null, then it is robust to
      // this pointer being null or not null
      return get_raw_value() != nullptr;
    }
    else
    {
      auto unknownCase = !(cond1);
      rlbox_detail_static_fail_because(
        unknownCase,
        "Implicit conversion to bool is only permitted for pointer types. For "
        "other types, unwrap the tainted value with the copy_and_verify API "
        "and then perform the required checks");
    }
  }
};

template<typename T, typename T_Sbx>
inline tainted<T, T_Sbx> from_opaque(tainted_opaque<T, T_Sbx> val)
{
  return *reinterpret_cast<tainted<T, T_Sbx>*>(&val);
}

/**
 * @brief Tainted volatile values are like tainted values but still point to
 * sandbox memory. Dereferencing a tainted pointer produces a tainted_volatile.
 */
template<typename T, typename T_Sbx>
class tainted_volatile : public tainted_base_impl<tainted_volatile, T, T_Sbx>
{
  KEEP_CLASSES_FRIENDLY
  KEEP_CAST_FRIENDLY

  // Classes recieve their own specialization
  static_assert(
    !std::is_class_v<T>,
    "Missing definition for class T. This error occurs for one "
    "of 2 reasons.\n"
    "  1) Make sure you have include a call rlbox_load_structs_from_library "
    "for this library with this class included.\n"
    "  2) Make sure you run (re-run) the struct-dump tool to list "
    "all structs in use by your program.\n");

  static_assert(
    detail::is_basic_type_v<T> || std::is_array_v<T>,
    "Tainted types only support fundamental, enum, pointer, array and struct "
    "types. Please file a bug if more support is needed.");

private:
  using T_ClassBase = tainted_base_impl<tainted_volatile, T, T_Sbx>;
  using T_AppType = tainted_detail::tainted_repr_t<T, T_Sbx>;
  using T_SandboxedType = tainted_detail::tainted_vol_repr_t<T, T_Sbx>;
  T_SandboxedType data;

  inline auto& get_sandbox_value_ref() noexcept { return data; }
  inline auto& get_sandbox_value_ref() const noexcept { return data; }

  inline std::remove_cv_t<T_AppType> get_raw_value() const
  {
    std::remove_cv_t<T_AppType> ret;
    // Need to construct an example_unsandboxed_ptr for pointers or arrays of
    // pointers. Since tainted_volatile is the type of data in sandbox memory,
    // the address of data (&data) refers to a location in sandbox memory and
    // can thus be the example_unsandboxed_ptr
    const volatile void* data_ref = &data;
    const void* example_unsandboxed_ptr = const_cast<const void*>(data_ref);
    using namespace detail;
    convert_type_non_class<T_Sbx,
                           adjust_type_direction::TO_APPLICATION,
                           adjust_type_context::EXAMPLE>(
      ret, data, example_unsandboxed_ptr, nullptr /* sandbox_ptr */);
    return ret;
  }

  inline std::remove_cv_t<T_SandboxedType> get_raw_sandbox_value()
    const noexcept
  {
    return data;
  };

  inline std::remove_cv_t<T_SandboxedType> get_raw_sandbox_value(
    rlbox_sandbox<T_Sbx>& sandbox) const noexcept
  {
    RLBOX_UNUSED(sandbox);
    return data;
  };

  tainted_volatile() = default;
  tainted_volatile(const tainted_volatile<T, T_Sbx>& p) = default;

public:
  inline tainted<const T*, T_Sbx> operator&() const noexcept
  {
    auto ref =
      detail::remove_volatile_from_ptr_cast(&this->get_sandbox_value_ref());
    auto ref_cast = reinterpret_cast<const T*>(ref);
    return tainted<const T*, T_Sbx>::internal_factory(ref_cast);
  }

  inline tainted<T*, T_Sbx> operator&() noexcept
  {
    return sandbox_const_cast<T*>(&std::as_const(*this));
  }

  // Needed as the definition of unary & above shadows the base's binary &
  rlbox_detail_forward_binop_to_base(&, T_ClassBase);

  template<typename T_RhsRef>
  inline tainted_volatile<T, T_Sbx>& operator=(T_RhsRef&& val)
  {
    using T_Rhs = std::remove_reference_t<T_RhsRef>;
    using T_Rhs_El = std::remove_all_extents_t<T_Rhs>;

    // Need to construct an example_unsandboxed_ptr for pointers or arrays of
    // pointers. Since tainted_volatile is the type of data in sandbox memory,
    // the address of data (&data) refers to a location in sandbox memory and
    // can thus be the example_unsandboxed_ptr
    const volatile void* data_ref = &get_sandbox_value_ref();
    const void* example_unsandboxed_ptr = const_cast<const void*>(data_ref);
    // Some branches don't use this
    RLBOX_UNUSED(example_unsandboxed_ptr);

    if_constexpr_named(
      cond1, std::is_same_v<std::remove_const_t<T_Rhs>, std::nullptr_t>)
    {
      static_assert(std::is_pointer_v<T>,
                    "Null pointer can only be assigned to pointers");
      // assign using an integer instead of nullptr, as the pointer field may be
      // represented as integer
      data = 0;
    }
    else if_constexpr_named(cond2, detail::rlbox_is_tainted_v<T_Rhs>)
    {
      using namespace detail;
      convert_type_non_class<T_Sbx,
                             adjust_type_direction::TO_SANDBOX,
                             adjust_type_context::EXAMPLE>(
        get_sandbox_value_ref(),
        val.get_raw_value_ref(),
        example_unsandboxed_ptr,
        nullptr /* sandbox_ptr */);
    }
    else if_constexpr_named(cond3, detail::rlbox_is_tainted_volatile_v<T_Rhs>)
    {
      using namespace detail;
      convert_type_non_class<T_Sbx,
                             adjust_type_direction::NO_CHANGE,
                             adjust_type_context::EXAMPLE>(
        get_sandbox_value_ref(),
        val.get_sandbox_value_ref(),
        example_unsandboxed_ptr,
        nullptr /* sandbox_ptr */);
    }
    else if_constexpr_named(cond4, detail::rlbox_is_sandbox_callback_v<T_Rhs>)
    {
      using T_RhsFunc = detail::rlbox_remove_wrapper_t<T_Rhs>;

      // need to perform some typechecking to ensure we are assigning compatible
      // function pointer types only
      if_constexpr_named(subcond1, !std::is_assignable_v<T&, T_RhsFunc>)
      {
        rlbox_detail_static_fail_because(
          subcond1,
          "Trying to assign function pointer to field of incompatible types");
      }
      else
      {
        // Need to reinterpret_cast as the representation of the signature of a
        // callback uses the machine model of the sandbox, while the field uses
        // that of the application. But we have already checked above that this
        // is safe.
        auto func = val.get_raw_sandbox_value();
        using T_Cast = std::remove_volatile_t<T_SandboxedType>;
        get_sandbox_value_ref() = (T_Cast)func;
      }
    }
    else if_constexpr_named(
      cond5,
      detail::is_fundamental_or_enum_v<T> ||
        (std::is_array_v<T> && !std::is_pointer_v<T_Rhs_El>))
    {
      detail::convert_type_fundamental_or_array(get_sandbox_value_ref(), val);
    }
    else if_constexpr_named(
      cond6, std::is_pointer_v<T_Rhs> || std::is_pointer_v<T_Rhs_El>)
    {
      rlbox_detail_static_fail_because(
        cond6,
        "Assignment of pointers is not safe as it could\n "
        "1) Leak pointers from the appliction to the sandbox which may break "
        "ASLR\n "
        "2) Pass inaccessible pointers to the sandbox leading to crash\n "
        "3) Break sandboxes that require pointers to be swizzled first\n "
        "\n "
        "Instead, if you want to pass in a pointer, do one of the following\n "
        "1) Allocate with malloc_in_sandbox, and pass in a tainted pointer\n "
        "2) For pointers that point to functions in the application, register "
        "with sandbox.register_callback(\"foo\"), and pass in the registered "
        "value\n "
        "3) For pointers that point to functions in the sandbox, get the "
        "address with get_sandbox_function_address(sandbox, foo), and pass in "
        "the "
        "address\n "
        "4) For raw pointers, use assign_raw_pointer which performs required "
        "safety checks\n ");
    }
    else
    {
      auto unknownCase =
        !(cond1 || cond2 || cond3 || cond4 || cond5 /* || cond6 */);
      rlbox_detail_static_fail_because(
        unknownCase, "Assignment of the given type of value is not supported");
    }

    return *this;
  }

  template<typename T_Rhs>
  void assign_raw_pointer(rlbox_sandbox<T_Sbx>& sandbox, T_Rhs val)
  {
    static_assert(std::is_pointer_v<T_Rhs>, "Must be a pointer");
    static_assert(std::is_assignable_v<T&, T_Rhs>,
                  "Should assign pointers of compatible types.");
    // Maybe a function pointer, so we need to cast
    const void* cast_val = reinterpret_cast<const void*>(val);
    bool safe = sandbox.is_pointer_in_sandbox_memory(cast_val);
    detail::dynamic_check(
      safe,
      "Tried to assign a pointer that is not in the sandbox.\n "
      "This is not safe as it could\n "
      "1) Leak pointers from the appliction to the sandbox which may break "
      "ASLR\n "
      "2) Pass inaccessible pointers to the sandbox leading to crash\n "
      "3) Break sandboxes that require pointers to be swizzled first\n "
      "\n "
      "Instead, if you want to pass in a pointer, do one of the following\n "
      "1) Allocate with malloc_in_sandbox, and pass in a tainted pointer\n "
      "2) For pointers that point to functions in the application, register "
      "with sandbox.register_callback(\"foo\"), and pass in the registered "
      "value\n "
      "3) For pointers that point to functions in the sandbox, get the "
      "address with get_sandbox_function_address(sandbox, foo), and pass in "
      "the "
      "address\n ");
    get_sandbox_value_ref() =
      sandbox.template get_sandboxed_pointer<T_Rhs>(cast_val);
  }

  template<typename T_Dummy = void>
  operator bool() const
  {
    rlbox_detail_static_fail_because(
      detail::true_v<T_Dummy>,
      "Cannot apply implicit conversion to bool on values that are located in "
      "sandbox memory. This error occurs if you compare a dereferenced value "
      "such as the code shown below\n\n"
      "tainted<int**> a = ...;\n"
      "assert(*a);\n\n"
      "Instead you can write this code as \n"
      "tainted<int*> temp = *a;\n"
      "assert(temp);\n");
    return false;
  }
};

}
