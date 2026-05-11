#pragma once
// IWYU pragma: private, include "rlbox.hpp"
// IWYU pragma: friend "rlbox_.*\.hpp"

#include <algorithm>
#include <atomic>
#ifdef RLBOX_MEASURE_TRANSITION_TIMES
#  include <chrono>
#endif
#include <cstdlib>
#include <limits>
#include <map>
#include <mutex>
#include <string>
#ifndef RLBOX_USE_CUSTOM_SHARED_LOCK
#  include <shared_mutex>
#endif
#ifdef RLBOX_MEASURE_TRANSITION_TIMES
#  include <sstream>
#endif
#include <stdint.h>
#include <type_traits>
#include <utility>
#include <vector>

#include "rlbox_conversion.hpp"
#include "rlbox_helpers.hpp"
#include "rlbox_stdlib_polyfill.hpp"
#include "rlbox_struct_support.hpp"
#include "rlbox_type_traits.hpp"
#include "rlbox_wrapper_traits.hpp"

#ifdef RLBOX_MEASURE_TRANSITION_TIMES
using namespace std::chrono;
#endif

namespace rlbox {

namespace convert_fn_ptr_to_sandbox_equivalent_detail {
  template<typename T, typename T_Sbx>
  using conv = ::rlbox::detail::convert_to_sandbox_equivalent_t<T, T_Sbx>;

  template<typename T_Ret, typename... T_Args>
  using T_Func = T_Ret (*)(T_Args...);

  template<typename T_Sbx, typename T_Ret, typename... T_Args>
  T_Func<conv<T_Ret, T_Sbx>, conv<T_Args, T_Sbx>...> helper(
    T_Ret (*)(T_Args...));
}

#if defined(RLBOX_MEASURE_TRANSITION_TIMES) ||                                 \
  defined(RLBOX_TRANSITION_ACTION_OUT) || defined(RLBOX_TRANSITION_ACTION_IN)
enum class rlbox_transition
{
  INVOKE,
  CALLBACK
};
#endif
#ifdef RLBOX_MEASURE_TRANSITION_TIMES
struct rlbox_transition_timing
{
  rlbox_transition invoke;
  const char* name;
  void* ptr;
  int64_t time;

  std::string to_string()
  {
    std::ostringstream ret;
    if (invoke == rlbox_transition::INVOKE) {
      ret << name;
    } else {
      ret << "Callback " << ptr;
    }
    ret << " : " << time << "\n";

    return ret.str();
  }
};
#endif

#ifndef RLBOX_SINGLE_THREADED_INVOCATIONS
#  error                                                                       \
    "RLBox does not yet support threading. Please define RLBOX_SINGLE_THREADED_INVOCATIONS prior to including RLBox and ensure you are only using it from a single thread. If threading is required, please file a bug."
#endif

/**
 * @brief Encapsulation for sandboxes.
 *
 * @tparam T_Sbx Type of sandbox. For the null sandbox this is
 * `rlbox_noop_sandbox`
 */
template<typename T_Sbx>
class rlbox_sandbox : protected T_Sbx
{
  KEEP_CLASSES_FRIENDLY

private:
#ifdef RLBOX_MEASURE_TRANSITION_TIMES
  std::vector<rlbox_transition_timing> transition_times;
#endif

  static inline RLBOX_SHARED_LOCK(sandbox_list_lock);
  // The actual type of the vector is std::vector<rlbox_sandbox<T_Sbx>*>
  // However clang 5, 6 have bugs where compilation seg-faults on this type
  // So we just use this std::vector<void*>
  static inline std::vector<void*> sandbox_list;

  RLBOX_SHARED_LOCK(func_ptr_cache_lock);
  std::map<std::string, void*> func_ptr_map;

  app_pointer_map<typename T_Sbx::T_PointerType> app_ptr_map;

  // This variable tracks of the sandbox has already been created/destroyed.
  // APIs in this class should be called only when the sandbox is created.
  // However, it is expensive to check in APIs such as invoke or in the callback
  // interceptor. What's more, there could be time of check time of use issues
  // in the checks as well.
  // In general, we leave it up to the user to ensure these APIs are never
  // called prior to sandbox construction or after destruction. We perform some
  // conservative sanity checks, where they would not add too much overhead.
  enum class Sandbox_Status
  {
    NOT_CREATED,
    INITIALIZING,
    CREATED,
    CLEANING_UP
  };
  std::atomic<Sandbox_Status> sandbox_created = Sandbox_Status::NOT_CREATED;

  std::mutex callback_lock;
  std::vector<void*> callback_keys;

  void* transition_state = nullptr;

  template<typename T>
  using convert_fn_ptr_to_sandbox_equivalent_t = decltype(
    ::rlbox::convert_fn_ptr_to_sandbox_equivalent_detail::helper<T_Sbx>(
      std::declval<T>()));

  template<typename T>
  inline constexpr void check_invoke_param_type_is_ok()
  {
    using T_NoRef = std::remove_reference_t<T>;

    if_constexpr_named(cond1, detail::rlbox_is_wrapper_v<T_NoRef>)
    {
      if_constexpr_named(
        subcond1,
        !std::is_same_v<T_Sbx, detail::rlbox_get_wrapper_sandbox_t<T_NoRef>>)
      {
        rlbox_detail_static_fail_because(
          cond1 && subcond1,
          "Mixing tainted data from a different sandbox types. This could "
          "happen due to couple of different reasons.\n"
          "1. You are using 2 sandbox types for example'rlbox_noop_sandbox' "
          "and 'rlbox_lucet_sandbox', and are passing tainted data from one "
          "sandbox as parameters into a function call to the other sandbox. "
          "This is not allowed, unwrap the tainted data with copy_and_verify "
          "or other unwrapping APIs first.\n"
          "2. You have inadvertantly forgotten to set/remove "
          "RLBOX_USE_STATIC_CALLS depending on the sandbox type. Some sandbox "
          "types like rlbox_noop_sandbox require this to be set to a given "
          "value, while other types like rlbox_lucet_sandbox, require this not "
          "to be set.");
      }
    }
    else if_constexpr_named(cond2,
                            std::is_null_pointer_v<T_NoRef> ||
                              detail::is_fundamental_or_enum_v<T_NoRef>)
    {}
    else
    {
      constexpr auto unknownCase = !(cond1 || cond2);
      rlbox_detail_static_fail_because(
        unknownCase,
        "Arguments to a sandbox function call should be primitives or wrapped "
        "types like tainted, callbacks etc.");
    }
  }

  template<typename T>
  inline auto invoke_process_param(T&& param)
  {
    check_invoke_param_type_is_ok<T>();

    using T_NoRef = std::remove_reference_t<T>;

    if constexpr (detail::rlbox_is_tainted_opaque_v<T_NoRef>) {
      auto ret = from_opaque(param);
      return ret.UNSAFE_sandboxed(*this);
    } else if constexpr (detail::rlbox_is_wrapper_v<T_NoRef>) {
      return param.UNSAFE_sandboxed(*this);
    } else if constexpr (std::is_null_pointer_v<T_NoRef>) {
      tainted<void*, T_Sbx> ret = nullptr;
      return ret.UNSAFE_sandboxed(*this);
    } else if constexpr (detail::is_fundamental_or_enum_v<T_NoRef>) {
      // For unwrapped primitives, assign to a tainted var and then unwrap so
      // that we adjust for machine model
      tainted<T_NoRef, T_Sbx> ret = param;
      return ret.UNSAFE_sandboxed(*this);
    } else {
      rlbox_detail_static_fail_because(
        detail::true_v<T_NoRef>,
        "Only tainted types, callbacks or primitive values such as ints can be "
        "passed as parameters.\n"
        "To make a parameter tainted, try moving the allocation into the "
        "sandbox.\n"
        "If the parameter is a callback, try registering the callback via the "
        "register_callback API.");
    }
  }

  template<typename T, typename T_Arg>
  inline tainted<T, T_Sbx> sandbox_callback_intercept_convert_param(
    rlbox_sandbox<T_Sbx>& sandbox,
    const T_Arg& arg)
  {
    tainted<T, T_Sbx> ret;
    using namespace detail;
    convert_type<T_Sbx,
                 adjust_type_direction::TO_APPLICATION,
                 adjust_type_context::SANDBOX>(
      ret.get_raw_value_ref(),
      arg,
      nullptr /* example_unsandboxed_ptr */,
      &sandbox);
    return ret;
  }

  template<typename T_Ret, typename... T_Args>
  static detail::convert_to_sandbox_equivalent_t<T_Ret, T_Sbx>
  sandbox_callback_interceptor(
    detail::convert_to_sandbox_equivalent_t<T_Args, T_Sbx>... args)
  {
    std::pair<T_Sbx*, void*> context =
      T_Sbx::impl_get_executed_callback_sandbox_and_key();
    auto& sandbox = *(reinterpret_cast<rlbox_sandbox<T_Sbx>*>(context.first));
    auto key = context.second;

    using T_Func_Ret =
      std::conditional_t<std::is_void_v<T_Ret>, void, tainted<T_Ret, T_Sbx>>;
    using T_Func =
      T_Func_Ret (*)(rlbox_sandbox<T_Sbx>&, tainted<T_Args, T_Sbx>...);
    auto target_fn_ptr = reinterpret_cast<T_Func>(key);

#ifdef RLBOX_MEASURE_TRANSITION_TIMES
    high_resolution_clock::time_point enter_time = high_resolution_clock::now();
    auto on_exit = rlbox::detail::make_scope_exit([&] {
      auto exit_time = high_resolution_clock::now();
      int64_t ns = duration_cast<nanoseconds>(exit_time - enter_time).count();
      sandbox.transition_times.push_back(
        rlbox_transition_timing{ rlbox_transition::CALLBACK,
                                 nullptr /* func_name */,
                                 key /* func_ptr */,
                                 ns });
    });
#endif
#ifdef RLBOX_TRANSITION_ACTION_OUT
    RLBOX_TRANSITION_ACTION_OUT(rlbox_transition::CALLBACK,
                                nullptr /* func_name */,
                                key /* func_ptr */,
                                sandbox.transition_state);
#endif
#ifdef RLBOX_TRANSITION_ACTION_IN
    auto on_exit_transition = rlbox::detail::make_scope_exit([&] {
      RLBOX_TRANSITION_ACTION_IN(rlbox_transition::CALLBACK,
                                 nullptr /* func_name */,
                                 key /* func_ptr */,
                                 sandbox.transition_state);
    });
#endif
    if constexpr (std::is_void_v<T_Func_Ret>) {
      (*target_fn_ptr)(
        sandbox,
        sandbox.template sandbox_callback_intercept_convert_param<T_Args>(
          sandbox, args)...);
      return;
    } else {
      auto tainted_ret = (*target_fn_ptr)(
        sandbox,
        sandbox.template sandbox_callback_intercept_convert_param<T_Args>(
          sandbox, args)...);

      using namespace detail;
      convert_to_sandbox_equivalent_t<T_Ret, T_Sbx> ret;
      convert_type<T_Sbx,
                   adjust_type_direction::TO_SANDBOX,
                   adjust_type_context::SANDBOX>(
        ret,
        tainted_ret.get_raw_value_ref(),
        nullptr /* example_unsandboxed_ptr */,
        &sandbox);
      return ret;
    }
  }

  /**
   * @brief Unregister a callback function and disallow the sandbox from
   * calling this function henceforth.
   */
  template<typename T_Ret, typename... T_Args>
  inline void unregister_callback(void* key)
  {
    // Silently swallowing the failure is better here as RAII types may try to
    // cleanup callbacks after sandbox destruction
    if (sandbox_created.load() != Sandbox_Status::CREATED) {
      return;
    }

    this->template impl_unregister_callback<
      detail::convert_to_sandbox_equivalent_t<T_Ret, T_Sbx>,
      detail::convert_to_sandbox_equivalent_t<T_Args, T_Sbx>...>(key);

    std::lock_guard<std::mutex> lock(callback_lock);
    auto el_ref = std::find(callback_keys.begin(), callback_keys.end(), key);
    detail::dynamic_check(
      el_ref != callback_keys.end(),
      "Unexpected state. Unregistering a callback that was never registered.");
    callback_keys.erase(el_ref);
  }

  static T_Sbx* find_sandbox_from_example(const void* example_sandbox_ptr)
  {
    detail::dynamic_check(
      example_sandbox_ptr != nullptr,
      "Internal error: received a null example pointer. Please file a bug.");

    RLBOX_ACQUIRE_SHARED_GUARD(lock, sandbox_list_lock);
    for (auto sandbox_v : sandbox_list) {
      auto sandbox = reinterpret_cast<rlbox_sandbox<T_Sbx>*>(sandbox_v);
      if (sandbox->is_pointer_in_sandbox_memory(example_sandbox_ptr)) {
        return sandbox;
      }
    }

    return nullptr;
  }

  template<typename... T_Args>
  static auto impl_create_sandbox_helper(rlbox_sandbox<T_Sbx>* this_ptr,
                                         T_Args... args)
  {
    return this_ptr->impl_create_sandbox(std::forward<T_Args>(args)...);
  }


template <typename T>
T checked_add(T aLhs, T aRhs, const char* aErrorMsg) {
  static_assert(std::is_unsigned_v<T>, "Expected unsigned type");

  T ret = aLhs + aRhs;
  bool has_overflow = ret < aLhs;
  detail::dynamic_check(!has_overflow, aErrorMsg);

  return ret;
}

template <typename T>
T checked_multiply(T aLhs, T aRhs, const char* aErrorMsg) {
  static_assert(std::is_unsigned_v<T>, "Expected unsigned type");

  T ret = aLhs * aRhs;
  bool has_overflow = (aLhs != 0) && ((ret / aLhs) != aRhs);
  detail::dynamic_check(!has_overflow, aErrorMsg);

  return ret;
}

public:
  /**
   * @brief Unused member that allows the calling code to save data in a
   * "per-sandbox" storage. This can be useful to save context which is used
   * in callbacks.
   */
  void* sandbox_storage;

  /***** Function to adjust for custom machine models *****/

  template<typename T>
  using convert_to_sandbox_equivalent_nonclass_t =
    detail::convert_base_types_t<T,
                                 typename T_Sbx::T_ShortType,
                                 typename T_Sbx::T_IntType,
                                 typename T_Sbx::T_LongType,
                                 typename T_Sbx::T_LongLongType,
                                 typename T_Sbx::T_PointerType>;

  T_Sbx* get_sandbox_impl() { return this; }

  /**
   * @brief Create a new sandbox.
   *
   * @tparam T_Args Arguments passed to the underlying sandbox
   * implementation. For the null sandbox, no arguments are necessary.
   */
  template<typename... T_Args>
  inline bool create_sandbox(T_Args... args)
  {
#ifdef RLBOX_MEASURE_TRANSITION_TIMES
    // Warm up the timer. The first call is always slow (at least on the test
    // platform)
    for (int i = 0; i < 10; i++) {
      auto val = high_resolution_clock::now();
      RLBOX_UNUSED(val);
    }
#endif
    auto expected = Sandbox_Status::NOT_CREATED;
    bool success = sandbox_created.compare_exchange_strong(
      expected, Sandbox_Status::INITIALIZING /* desired */);
    detail::dynamic_check(
      success,
      "create_sandbox called when sandbox already created/is being "
      "created concurrently");

    using T_Result = rlbox::detail::polyfill::invoke_result_t<
      decltype(impl_create_sandbox_helper<T_Args...>),
      decltype(this),
      T_Args...>;

    bool created = true;
    if constexpr (std::is_same_v<T_Result, void>) {
      this->impl_create_sandbox(std::forward<T_Args>(args)...);
    } else if constexpr (std::is_same_v<T_Result, bool>) {
      created = this->impl_create_sandbox(std::forward<T_Args>(args)...);
    } else {
      rlbox_detail_static_fail_because(
        (!std::is_same_v<T_Result, void> && !std::is_same_v<T_Result, bool>),
        "Expected impl_create_sandbox to return void or a boolean");
    }

    if (created) {
      sandbox_created.store(Sandbox_Status::CREATED);
      RLBOX_ACQUIRE_UNIQUE_GUARD(lock, sandbox_list_lock);
      sandbox_list.push_back(this);
    }

    return created;
  }

  /**
   * @brief Destroy sandbox and reclaim any memory.
   */
  inline auto destroy_sandbox()
  {
    auto expected = Sandbox_Status::CREATED;
    bool success = sandbox_created.compare_exchange_strong(
      expected, Sandbox_Status::CLEANING_UP /* desired */);

    detail::dynamic_check(
      success,
      "destroy_sandbox called without sandbox creation/is being "
      "destroyed concurrently");

    {
      RLBOX_ACQUIRE_UNIQUE_GUARD(lock, sandbox_list_lock);
      auto el_ref = std::find(sandbox_list.begin(), sandbox_list.end(), this);
      detail::dynamic_check(
        el_ref != sandbox_list.end(),
        "Unexpected state. Destroying a sandbox that was never initialized.");
      sandbox_list.erase(el_ref);
    }

    sandbox_created.store(Sandbox_Status::NOT_CREATED);
    return this->impl_destroy_sandbox();
  }

  template<typename T>
  inline T get_unsandboxed_pointer(
    convert_to_sandbox_equivalent_nonclass_t<T> p) const
  {
    static_assert(std::is_pointer_v<T>);
    if (p == 0) {
      return nullptr;
    }
    auto ret = this->template impl_get_unsandboxed_pointer<T>(p);
    return reinterpret_cast<T>(ret);
  }

  template<typename T>
  inline convert_to_sandbox_equivalent_nonclass_t<T> get_sandboxed_pointer(
    const void* p) const
  {
    static_assert(std::is_pointer_v<T>);
    if (p == nullptr) {
      return 0;
    }
    return this->template impl_get_sandboxed_pointer<T>(p);
  }

  template<typename T>
  static inline T get_unsandboxed_pointer_no_ctx(
    convert_to_sandbox_equivalent_nonclass_t<T> p,
    const void* example_unsandboxed_ptr)
  {
    static_assert(std::is_pointer_v<T>);
    if (p == 0) {
      return nullptr;
    }
    auto ret = T_Sbx::template impl_get_unsandboxed_pointer_no_ctx<T>(
      p, example_unsandboxed_ptr, find_sandbox_from_example);
    return reinterpret_cast<T>(ret);
  }

  template<typename T>
  static inline convert_to_sandbox_equivalent_nonclass_t<T>
  get_sandboxed_pointer_no_ctx(const void* p,
                               const void* example_unsandboxed_ptr)
  {
    static_assert(std::is_pointer_v<T>);
    if (p == nullptr) {
      return 0;
    }
    return T_Sbx::template impl_get_sandboxed_pointer_no_ctx<T>(
      p, example_unsandboxed_ptr, find_sandbox_from_example);
  }

  /**
   * @brief Allocate a new pointer that is accessible to both the application
   * and sandbox. The pointer is allocated in sandbox memory.
   *
   * @tparam T The type of the pointer you want to create. If T=int, this
   * would return a pointer to an int.
   *
   * @return tainted<T*, T_Sbx> Tainted pointer accessible to the application
   * and sandbox.
   */
  template<typename T>
  inline tainted<T*, T_Sbx> malloc_in_sandbox()
  {
    const uint32_t defaultCount = 1;
    return malloc_in_sandbox<T>(defaultCount);
  }

  /**
   * @brief Allocate an array that is accessible to both the application
   * and sandbox. The pointer is allocated in sandbox memory.
   *
   * @tparam T The type of the array elements you want to create. If T=int, this
   * would return a pointer to an array of ints.
   *
   * @param count The number of array elements to allocate.
   *
   * @return tainted<T*, T_Sbx> Tainted pointer accessible to the application
   * and sandbox.
   */
  template<typename T>
  inline tainted<T*, T_Sbx> malloc_in_sandbox(uint32_t count)
  {
    // Silently swallowing the failure is better here as RAII types may try to
    // malloc after sandbox destruction
    if (sandbox_created.load() != Sandbox_Status::CREATED) {
      return tainted<T*, T_Sbx>::internal_factory(nullptr);
    }

    detail::dynamic_check(count != 0, "Malloc tried to allocate 0 bytes");
    if constexpr (sizeof(T) >= std::numeric_limits<uint32_t>::max()) {
      rlbox_detail_static_fail_because(sizeof(T) >=
                                         std::numeric_limits<uint32_t>::max(),
                                       "Tried to allocate an object over 4GB.");
    }
    auto total_size = static_cast<uint64_t>(sizeof(T)) * count;
    if constexpr (sizeof(size_t) == 4) {
      // On a 32-bit platform, we need to make sure that total_size is not >=4GB
      detail::dynamic_check(total_size < std::numeric_limits<uint32_t>::max(),
                            "Tried to allocate memory over 4GB");
    } else if constexpr (sizeof(size_t) != 8) {
      // Double check we are on a 64-bit platform
      // Note for static checks we need to have some dependence on T, so adding
      // a dummy
      constexpr bool dummy = sizeof(T) >= 0;
      rlbox_detail_static_fail_because(dummy && sizeof(size_t) != 8,
                                       "Expected 32 or 64 bit platform.");
    }
    auto ptr_in_sandbox = this->impl_malloc_in_sandbox(total_size);
    auto ptr = get_unsandboxed_pointer<T*>(ptr_in_sandbox);
    if (!ptr) {
      return tainted<T*, T_Sbx>(nullptr);
    }
    detail::dynamic_check(is_pointer_in_sandbox_memory(ptr),
                          "Malloc returned pointer outside the sandbox memory");

    const size_t obj_size = checked_multiply(static_cast<size_t>(count), sizeof(T), "Malloc object size too large");
    auto ptr_end = checked_add(reinterpret_cast<uintptr_t>(ptr), reinterpret_cast<uintptr_t>(obj_size - 1), "Malloc object end too large");
    detail::dynamic_check(is_pointer_in_sandbox_memory(reinterpret_cast<const char *>(ptr_end)),
      "Malloc returned a pointer whose range goes beyond sandbox memory");
    auto cast_ptr = reinterpret_cast<T*>(ptr);
    return tainted<T*, T_Sbx>::internal_factory(cast_ptr);
  }

  /**
   * @brief Free the memory referenced by the tainted pointer.
   *
   * @param ptr Pointer to sandbox memory to free.
   */
  template<typename T>
  inline void free_in_sandbox(tainted<T*, T_Sbx> ptr)
  {
    // Silently swallowing the failure is better here as RAII types may try to
    // free after sandbox destruction
    if (sandbox_created.load() != Sandbox_Status::CREATED) {
      return;
    }

    this->impl_free_in_sandbox(ptr.get_raw_sandbox_value(*this));
  }

  /**
   * @brief Free the memory referenced by a tainted_volatile pointer ref.
   *
   * @param ptr_ref Pointer reference to sandbox memory to free.
   */
  template<typename T>
  inline void free_in_sandbox(tainted_volatile<T, T_Sbx>& ptr_ref)
  {
    tainted<T, T_Sbx> ptr = ptr_ref;
    free_in_sandbox(ptr);
  }

  /**
   * @brief Free the memory referenced by a tainted_opaque pointer.
   *
   * @param ptr_opaque Opaque pointer to sandbox memory to free.
   */
  template<typename T>
  inline void free_in_sandbox(tainted_opaque<T, T_Sbx> ptr_opaque)
  {
    tainted<T, T_Sbx> ptr = from_opaque(ptr_opaque);
    free_in_sandbox(ptr);
  }

  /**
   * @brief Check if two pointers are in the same sandbox.
   * For the null-sandbox, this always returns true.
   */
  static inline bool is_in_same_sandbox(const void* p1, const void* p2)
  {
    const size_t num_args =
      detail::func_arg_nums_v<decltype(T_Sbx::impl_is_in_same_sandbox)>;
    if constexpr (num_args == 2) {
      return T_Sbx::impl_is_in_same_sandbox(p1, p2);
    } else {
      return T_Sbx::impl_is_in_same_sandbox(p1, p2, find_sandbox_from_example);
    }
  }

  /**
   * @brief Check if the pointer points to this sandbox's memory.
   * For the null-sandbox, this always returns true.
   */
  inline bool is_pointer_in_sandbox_memory(const void* p)
  {
    return this->impl_is_pointer_in_sandbox_memory(p);
  }

  /**
   * @brief Check if the pointer points to application memory.
   * For the null-sandbox, this always returns true.
   */
  inline bool is_pointer_in_app_memory(const void* p)
  {
    return this->impl_is_pointer_in_app_memory(p);
  }

  inline size_t get_total_memory() { return this->impl_get_total_memory(); }

  inline void* get_memory_location()
  {
    return this->impl_get_memory_location();
  }

  void* get_transition_state() { return transition_state; }

  void set_transition_state(void* new_state) { transition_state = new_state; }

  /**
   * @brief For internal use only.
   * Grant access of the passed in buffer in to the sandbox instance. Called by
   * internal APIs only if the underlying sandbox supports
   * can_grant_deny_access by including the line
   * ```
   * using can_grant_deny_access = void;
   * ```
   */
  template<typename T>
  inline tainted<T*, T_Sbx> INTERNAL_grant_access(T* src,
                                                  size_t num,
                                                  bool& success)
  {
    auto ret = this->impl_grant_access(src, num, success);
    return tainted<T*, T_Sbx>::internal_factory(ret);
  }

  /**
   * @brief For internal use only.
   * Grant access of the passed in buffer in to the sandbox instance. Called by
   * internal APIs only if the underlying sandbox supports
   * can_grant_deny_access by including the line
   * ```
   * using can_grant_deny_access = void;
   * ```
   */
  template<typename T>
  inline T* INTERNAL_deny_access(tainted<T*, T_Sbx> src,
                                 size_t num,
                                 bool& success)
  {
    auto ret =
      this->impl_deny_access(src.INTERNAL_unverified_safe(), num, success);
    return ret;
  }

  void* lookup_symbol(const char* func_name)
  {
    {
      RLBOX_ACQUIRE_SHARED_GUARD(lock, func_ptr_cache_lock);

      auto func_ptr_ref = func_ptr_map.find(func_name);
      if (func_ptr_ref != func_ptr_map.end()) {
        return func_ptr_ref->second;
      }
    }

    void* func_ptr = this->impl_lookup_symbol(func_name);
    RLBOX_ACQUIRE_UNIQUE_GUARD(lock, func_ptr_cache_lock);
    func_ptr_map[func_name] = func_ptr;
    return func_ptr;
  }

  void* internal_lookup_symbol(const char* func_name)
  {
    {
      RLBOX_ACQUIRE_SHARED_GUARD(lock, func_ptr_cache_lock);

      auto func_ptr_ref = func_ptr_map.find(func_name);
      if (func_ptr_ref != func_ptr_map.end()) {
        return func_ptr_ref->second;
      }
    }

    void* func_ptr = 0;
    if constexpr (rlbox::detail::
                    has_member_using_needs_internal_lookup_symbol_v<T_Sbx>) {
      func_ptr = this->impl_internal_lookup_symbol(func_name);
    } else {
      func_ptr = this->impl_lookup_symbol(func_name);
    }
    RLBOX_ACQUIRE_UNIQUE_GUARD(lock, func_ptr_cache_lock);
    func_ptr_map[func_name] = func_ptr;
    return func_ptr;
  }

  // this is an internal function invoked from macros, so it has be public
  template<typename T, typename... T_Args>
  inline auto INTERNAL_invoke_with_func_name(const char* func_name,
                                             T_Args&&... params)
  {
    return INTERNAL_invoke_with_func_ptr<T, T_Args...>(
      func_name, lookup_symbol(func_name), std::forward<T_Args>(params)...);
  }

  // this is an internal function invoked from macros, so it has be public
  // Explicitly don't use inline on this, as this adds a lot of instructions
  // prior to function call. What's more, by not inlining, different function
  // calls with the same signature can share the same code segments for
  // sandboxed function execution in the binary
  template<typename T, typename... T_Args>
  auto INTERNAL_invoke_with_func_ptr(const char* func_name,
                                     void* func_ptr,
                                     T_Args&&... params)
  {
    // unused in some paths
    RLBOX_UNUSED(func_name);
#ifdef RLBOX_MEASURE_TRANSITION_TIMES
    auto enter_time = high_resolution_clock::now();
    auto on_exit = rlbox::detail::make_scope_exit([&] {
      auto exit_time = high_resolution_clock::now();
      int64_t ns = duration_cast<nanoseconds>(exit_time - enter_time).count();
      transition_times.push_back(rlbox_transition_timing{
        rlbox_transition::INVOKE, func_name, func_ptr, ns });
    });
#endif
#ifdef RLBOX_TRANSITION_ACTION_IN
    RLBOX_TRANSITION_ACTION_IN(
      rlbox_transition::INVOKE, func_name, func_ptr, transition_state);
#endif
#ifdef RLBOX_TRANSITION_ACTION_OUT
    auto on_exit_transition = rlbox::detail::make_scope_exit([&] {
      RLBOX_TRANSITION_ACTION_OUT(
        rlbox_transition::INVOKE, func_name, func_ptr, transition_state);
    });
#endif
    (check_invoke_param_type_is_ok<T_Args>(), ...);

    static_assert(
      rlbox::detail::polyfill::is_invocable_v<
        T,
        detail::rlbox_remove_wrapper_t<std::remove_reference_t<T_Args>>...>,
      "Mismatched arguments types for function");

    using T_Result = rlbox::detail::polyfill::invoke_result_t<
      T,
      detail::rlbox_remove_wrapper_t<std::remove_reference_t<T_Args>>...>;

    using T_Converted =
      std::remove_pointer_t<convert_fn_ptr_to_sandbox_equivalent_t<T*>>;

    if constexpr (std::is_void_v<T_Result>) {
      this->template impl_invoke_with_func_ptr<T>(
        reinterpret_cast<T_Converted*>(func_ptr),
        invoke_process_param(params)...);
      return;
    } else {
      auto raw_result = this->template impl_invoke_with_func_ptr<T>(
        reinterpret_cast<T_Converted*>(func_ptr),
        invoke_process_param(params)...);
      tainted<T_Result, T_Sbx> wrapped_result;
      using namespace detail;
      convert_type<T_Sbx,
                   adjust_type_direction::TO_APPLICATION,
                   adjust_type_context::SANDBOX>(
        wrapped_result.get_raw_value_ref(),
        raw_result,
        nullptr /* example_unsandboxed_ptr */,
        this /* sandbox_ptr */);
      return wrapped_result;
    }
  }

  // Useful in the porting stage to temporarily allow non tainted pointers to go
  // through. This will only ever work in the rlbox_noop_sandbox. Any sandbox
  // that actually enforces isolation will crash here.
  template<typename T2>
  tainted<T2, T_Sbx> UNSAFE_accept_pointer(T2 ptr)
  {
    static_assert(std::is_pointer_v<T2>,
                  "UNSAFE_accept_pointer expects a pointer param");
    tainted<T2, T_Sbx> ret;
    ret.assign_raw_pointer(*this, ptr);
    return ret;
  }

  template<typename T_Ret, typename... T_Args>
  using T_Cb_no_wrap = detail::rlbox_remove_wrapper_t<T_Ret>(
    detail::rlbox_remove_wrapper_t<T_Args>...);

  template<typename T_Ret>
  sandbox_callback<T_Cb_no_wrap<T_Ret>*, T_Sbx> register_callback(T_Ret (*)())
  {
    rlbox_detail_static_fail_because(
      detail::true_v<T_Ret>,
      "Modify the callback to change the first parameter to a sandbox. "
      "For instance if a callback has type\n\n"
      "int foo() {...}\n\n"
      "Change this to \n\n"
      "tainted<int, T_Sbx> foo(rlbox_sandbox<T_Sbx>& sandbox) {...}\n");

    // this is never executed, but we need it for the function to type-check
    std::abort();
  }

  /**
   * @brief Expose a callback function to the sandboxed code.
   *
   * @param func_ptr The callback to expose.
   *
   * @tparam T_RL   Sandbox reference type (first argument).
   * @tparam T_Ret  Return type of callback. Must be tainted or void.
   * @tparam T_Args Types of remaining callback arguments. Must be tainted.
   *
   * @return Wrapped callback function pointer that can be passed to the
   * sandbox.
   */
  template<typename T_RL, typename T_Ret, typename... T_Args>
  sandbox_callback<T_Cb_no_wrap<T_Ret, T_Args...>*, T_Sbx> register_callback(
    T_Ret (*func_ptr)(T_RL, T_Args...))
  {
    // Some branches don't use the param
    RLBOX_UNUSED(func_ptr);

    if_constexpr_named(cond1, !std::is_same_v<T_RL, rlbox_sandbox<T_Sbx>&>)
    {
      rlbox_detail_static_fail_because(
        cond1,
        "Modify the callback to change the first parameter to a sandbox. "
        "For instance if a callback has type\n\n"
        "int foo(int a, int b) {...}\n\n"
        "Change this to \n\n"
        "tainted<int, T_Sbx> foo(rlbox_sandbox<T_Sbx>& sandbox, "
        "tainted<int, T_Sbx> a, tainted<int, T_Sbx> b) {...}\n");
    }
    else if_constexpr_named(
      cond2, !(detail::rlbox_is_tainted_or_opaque_v<T_Args> && ...))
    {
      rlbox_detail_static_fail_because(
        cond2,
        "Change all arguments to the callback have to be tainted or "
        "tainted_opaque. "
        "For instance if a callback has type\n\n"
        "int foo(int a, int b) {...}\n\n"
        "Change this to \n\n"
        "tainted<int, T_Sbx> foo(rlbox_sandbox<T_Sbx>& sandbox, "
        "tainted<int, T_Sbx> a, tainted<int, T_Sbx> b) {...}\n");
    }
    else if_constexpr_named(
      cond3, (std::is_array_v<detail::rlbox_remove_wrapper_t<T_Args>> || ...))
    {
      rlbox_detail_static_fail_because(
        cond3,
        "Change all static array arguments to the callback to be pointers. "
        "For instance if a callback has type\n\n"
        "int foo(int a[4]) {...}\n\n"
        "Change this to \n\n"
        "tainted<int, T_Sbx> foo(rlbox_sandbox<T_Sbx>& sandbox, "
        "tainted<int*, T_Sbx> a) {...}\n");
    }
    else if_constexpr_named(
      cond4,
      !(std::is_void_v<T_Ret> || detail::rlbox_is_tainted_or_opaque_v<T_Ret>))
    {
      rlbox_detail_static_fail_because(
        cond4,
        "Change the callback return type to be tainted or tainted_opaque if it "
        "is not void. "
        "For instance if a callback has type\n\n"
        "int foo(int a, int b) {...}\n\n"
        "Change this to \n\n"
        "tainted<int, T_Sbx> foo(rlbox_sandbox<T_Sbx>& sandbox, "
        "tainted<int, T_Sbx> a, tainted<int, T_Sbx> b) {...}\n");
    }
    else
    {
      detail::dynamic_check(
        sandbox_created.load() == Sandbox_Status::CREATED,
        "register_callback called without sandbox creation");

      // Need unique key for each callback we register - just use the func addr
      void* unique_key = reinterpret_cast<void*>(func_ptr);

      // Make sure that the user hasn't previously registered this function...
      // If they have, we would returning 2 owning types (sandbox_callback) to
      // the same callback which would be bad
      {
        std::lock_guard<std::mutex> lock(callback_lock);
        bool exists =
          std::find(callback_keys.begin(), callback_keys.end(), unique_key) !=
          callback_keys.end();
        detail::dynamic_check(
          !exists, "You have previously already registered this callback.");
        callback_keys.push_back(unique_key);
      }

      auto callback_interceptor =
        sandbox_callback_interceptor<detail::rlbox_remove_wrapper_t<T_Ret>,
                                     detail::rlbox_remove_wrapper_t<T_Args>...>;

      auto callback_trampoline = this->template impl_register_callback<
        detail::convert_to_sandbox_equivalent_t<
          detail::rlbox_remove_wrapper_t<T_Ret>,
          T_Sbx>,
        detail::convert_to_sandbox_equivalent_t<
          detail::rlbox_remove_wrapper_t<T_Args>,
          T_Sbx>...>(unique_key, reinterpret_cast<void*>(callback_interceptor));

      auto tainted_func_ptr = reinterpret_cast<
        detail::rlbox_tainted_opaque_to_tainted_t<T_Ret, T_Sbx> (*)(
          T_RL, detail::rlbox_tainted_opaque_to_tainted_t<T_Args, T_Sbx>...)>(
        reinterpret_cast<void*>(func_ptr));

      auto ret = sandbox_callback<T_Cb_no_wrap<T_Ret, T_Args...>*, T_Sbx>(
        this,
        tainted_func_ptr,
        callback_interceptor,
        callback_trampoline,
        unique_key);
      return ret;
    }
  }

  // this is an internal function invoked from macros, so it has be public
  template<typename T>
  inline tainted<T*, T_Sbx> INTERNAL_get_sandbox_function_name(
    const char* func_name)
  {
    return INTERNAL_get_sandbox_function_ptr<T>(
      internal_lookup_symbol(func_name));
  }

  // this is an internal function invoked from macros, so it has be public
  template<typename T>
  inline tainted<T*, T_Sbx> INTERNAL_get_sandbox_function_ptr(void* func_ptr)
  {
    return tainted<T*, T_Sbx>::internal_factory(reinterpret_cast<T*>(func_ptr));
  }

  /**
   * @brief Create a "fake" pointer referring to a location in the application
   * memory
   *
   * @param ptr The pointer to refer to
   *
   * @return The app_pointer object that refers to this location.
   */
  template<typename T>
  app_pointer<T*, T_Sbx> get_app_pointer(T* ptr)
  {
    auto max_ptr = (typename T_Sbx::T_PointerType)(get_total_memory() - 1);
    auto idx = app_ptr_map.get_app_pointer_idx((void*)ptr, max_ptr);
    auto idx_as_ptr = this->template impl_get_unsandboxed_pointer<T>(idx);
    // Right now we simply assume that any integer can be converted to a valid
    // pointer in the sandbox This may not be true for some sandboxing mechanism
    // plugins in the future In this case, we will have to come up with
    // something more clever to construct indexes that look like valid pointers
    // Add a check for now to make sure things work fine
    detail::dynamic_check(is_pointer_in_sandbox_memory(idx_as_ptr),
                          "App pointers are not currently supported for this "
                          "rlbox sandbox plugin. Please file a bug.");
    auto ret = app_pointer<T*, T_Sbx>(
      &app_ptr_map, idx, reinterpret_cast<T*>(idx_as_ptr));
    return ret;
  }

  /**
   * @brief The mirror of get_app_pointer. Take a tainted pointer which is
   * actually an app_pointer, and get the application location being pointed to
   *
   * @param tainted_ptr The tainted pointer that is actually an app_pointer
   *
   * @return The original location being referred to by the app_ptr
   */
  template<typename T>
  T* lookup_app_ptr(tainted<T*, T_Sbx> tainted_ptr)
  {
    auto idx = tainted_ptr.get_raw_sandbox_value(*this);
    void* ret = app_ptr_map.lookup_index(idx);
    return reinterpret_cast<T*>(ret);
  }

#ifdef RLBOX_MEASURE_TRANSITION_TIMES
  inline std::vector<rlbox_transition_timing>&
  process_and_get_transition_times()
  {
    return transition_times;
  }
  inline int64_t get_total_ns_time_in_sandbox_and_transitions()
  {
    int64_t ret = 0;
    for (auto& transition_time : transition_times) {
      if (transition_time.invoke == rlbox_transition::INVOKE) {
        ret += transition_time.time;
      } else {
        ret -= transition_time.time;
      }
    }
    return ret;
  }
  inline void clear_transition_times() { transition_times.clear(); }
#endif
};

#if defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(__GNUC__) || defined(__GNUG__)
// Can't turn off the variadic macro warning emitted from -pedantic so use a
// hack to stop GCC emitting warnings for the reminder of this file
#  pragma GCC system_header
#elif defined(_MSC_VER)
// Doesn't seem to emit the warning
#else
// Don't know the compiler... just let it go through
#endif

/**
 * @def  invoke_sandbox_function
 * @brief Call sandbox function.
 *
 * @param func_name The sandboxed library function to call.
 * @param ... Arguments to function should be simple or tainted values.
 * @return Tainted value or void.
 */
#ifdef RLBOX_USE_STATIC_CALLS

#  define sandbox_lookup_symbol_helper(prefix, func_name) prefix(func_name)

#  define invoke_sandbox_function(func_name, ...)                              \
    template INTERNAL_invoke_with_func_ptr<decltype(func_name)>(               \
      #func_name,                                                              \
      sandbox_lookup_symbol_helper(RLBOX_USE_STATIC_CALLS(), func_name),       \
      ##__VA_ARGS__)

#  define get_sandbox_function_address(func_name)                              \
    template INTERNAL_get_sandbox_function_ptr<decltype(func_name)>(           \
      sandbox_lookup_symbol_helper(RLBOX_USE_STATIC_CALLS(), func_name))

#else

#  define invoke_sandbox_function(func_name, ...)                              \
    template INTERNAL_invoke_with_func_name<decltype(func_name)>(              \
      #func_name, ##__VA_ARGS__)

#  define get_sandbox_function_address(func_name)                              \
    template INTERNAL_get_sandbox_function_name<decltype(func_name)>(#func_name)

#endif

#define sandbox_invoke(sandbox, func_name, ...)                                \
  (sandbox).invoke_sandbox_function(func_name, ##__VA_ARGS__)

#define sandbox_function_address(sandbox, func_name)                           \
  (sandbox).get_sandbox_function_address(func_name)

#if defined(__clang__)
#  pragma clang diagnostic pop
#else
#endif

}
