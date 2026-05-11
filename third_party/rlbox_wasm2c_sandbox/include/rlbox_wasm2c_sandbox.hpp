#pragma once

#include "rlbox_wasm2c_tls.hpp"
#include "wasm-rt.h"
#include "wasm2c_rt_mem.h"
#include "wasm2c_rt_minwasi.h"

// Pull the helper header from the main repo for dynamic_check and scope_exit
#include "rlbox_helpers.hpp"

#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
// RLBox allows applications to provide a custom shared lock implementation
#ifndef RLBOX_USE_CUSTOM_SHARED_LOCK
#  include <shared_mutex>
#endif
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(_WIN32)
// Ensure the min/max macro in the header doesn't collide with functions in
// std::
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

#define RLBOX_WASM2C_UNUSED(...) (void)__VA_ARGS__

// Use the same convention as rlbox to allow applications to customize the
// shared lock
#ifndef RLBOX_USE_CUSTOM_SHARED_LOCK
#  define RLBOX_SHARED_LOCK(name) std::shared_timed_mutex name
#  define RLBOX_ACQUIRE_SHARED_GUARD(name, ...)                                \
    std::shared_lock<std::shared_timed_mutex> name(__VA_ARGS__)
#  define RLBOX_ACQUIRE_UNIQUE_GUARD(name, ...)                                \
    std::unique_lock<std::shared_timed_mutex> name(__VA_ARGS__)
#else
#  if !defined(RLBOX_SHARED_LOCK) || !defined(RLBOX_ACQUIRE_SHARED_GUARD) ||   \
    !defined(RLBOX_ACQUIRE_UNIQUE_GUARD)
#    error                                                                     \
      "RLBOX_USE_CUSTOM_SHARED_LOCK defined but missing definitions for RLBOX_SHARED_LOCK, RLBOX_ACQUIRE_SHARED_GUARD, RLBOX_ACQUIRE_UNIQUE_GUARD"
#  endif
#endif

#define DEFINE_RLBOX_WASM2C_MODULE_TYPE(modname)                                   \
  struct rlbox_wasm2c_module_type_##modname                                        \
  {                                                                                \
    using instance_t = w2c_##modname;                                              \
                                                                                   \
    using create_instance_imported_t = void (*)(instance_t*,                       \
                                       struct w2c_env*,                            \
                                       struct w2c_wasi__snapshot__preview1*);      \
    using create_instance_exported_t = void (*)(instance_t*,                       \
                                       struct w2c_wasi__snapshot__preview1*);      \
    static constexpr auto create_instance =                                        \
      &wasm2c_##modname##_instantiate;                                             \
                                                                                   \
    using free_instance_t = void (*)(instance_t*);                                 \
    static constexpr free_instance_t free_instance = &wasm2c_##modname##_free;     \
                                                                                   \
    using get_func_type_t = wasm_rt_func_type_t (*)(uint32_t, uint32_t, ...);      \
    static constexpr get_func_type_t get_func_type =                               \
      &wasm2c_##modname##_get_func_type;                                           \
                                                                                   \
    static constexpr const char* prefix = #modname;                                \
                                                                                   \
    /* A function that returns the address of the func specified as a              \
     * constexpr string */                                                         \
    /* Unfortunately, there is no way to implement the below in C++. */            \
    /* Implement this to fully support multiple static modules. */                 \
    /* static constexpr void* dlsym_in_w2c_module(const char* func_name) { */      \
    /*    return &w2c_##modname##_%func%; */                                       \
    /* } */                                                                        \
                                                                                   \
    static constexpr auto malloc_address = &w2c_##modname##_malloc;                \
    static constexpr auto free_address = &w2c_##modname##_free;                    \
                                                                                   \
  };                                                                               \
  extern "C" {                                                                     \
    /* Declarations for imported memory and tables */                              \
    extern const uint64_t wasm2c_##modname##_min_env_memory;                       \
    extern const uint8_t wasm2c_##modname##_is64_env_memory;                       \
    extern const uint32_t wasm2c_##modname##_min_env_0x5F_indirect_function_table; \
    /* Declarations for exported memory and tables */                              \
    extern wasm_rt_memory_t* w2c_##modname##_memory(w2c_##modname*);               \
    extern wasm_rt_funcref_table_t* w2c_##modname##_0x5F_indirect_function_table(  \
      w2c_##modname*);                                                             \
  }

#define DEFINE_RLBOX_WASM2C_IMPORTED_MODULE_ATTRIBS(modname)                           \
  constexpr const uint64_t* initial_memory_pages = &wasm2c_##modname##_min_env_memory; \
  constexpr const uint8_t* is_memory_64 = &wasm2c_##modname##_is64_env_memory;         \
  constexpr const uint32_t* initial_func_elements = &wasm2c_##modname##_min_env_0x5F_indirect_function_table;

#define DEFINE_RLBOX_WASM2C_EXPORTED_MODULE_ATTRIBS(modname)                               \
  constexpr const auto get_exported_memory = &w2c_##modname##_memory;                      \
  constexpr const auto get_exported_table = &w2c_##modname##_0x5F_indirect_function_table; \

// wasm_module_name module name used when compiling with wasm2c
#ifndef RLBOX_WASM2C_MODULE_NAME
#  error "Expected definition for RLBOX_WASM2C_MODULE_NAME"
#endif

// Need an extra macro to expand RLBOX_WASM2C_MODULE_NAME
#define INVOKE_DEFINE_RLBOX_WASM2C_MODULE_TYPE(modname)                        \
  DEFINE_RLBOX_WASM2C_MODULE_TYPE(modname)

#define INVOKE_DEFINE_RLBOX_WASM2C_IMPORTED_MODULE_ATTRIBS(modname)           \
  DEFINE_RLBOX_WASM2C_IMPORTED_MODULE_ATTRIBS(modname)

#define INVOKE_DEFINE_RLBOX_WASM2C_EXPORTED_MODULE_ATTRIBS(modname)           \
  DEFINE_RLBOX_WASM2C_EXPORTED_MODULE_ATTRIBS(modname)

// Define the base wasm2c module type
INVOKE_DEFINE_RLBOX_WASM2C_MODULE_TYPE(RLBOX_WASM2C_MODULE_NAME);

// Concat after macro expansion
#define RLBOX_WASM2C_CONCAT2(x, y) x##y
#define RLBOX_WASM2C_CONCAT(x, y) RLBOX_WASM2C_CONCAT2(x, y)

#define RLBOX_WASM_MODULE_TYPE_CURR                                            \
  RLBOX_WASM2C_CONCAT(rlbox_wasm2c_module_type_, RLBOX_WASM2C_MODULE_NAME)

#define RLBOX_WASM2C_STRINGIFY(x) RLBOX_WASM2C_STRINGIFY2(x)
#define RLBOX_WASM2C_STRINGIFY2(x) #x

#define RLBOX_WASM2C_MODULE_NAME_STR                                           \
  RLBOX_WASM2C_STRINGIFY(RLBOX_WASM2C_MODULE_NAME)

#define RLBOX_WASM2C_MODULE_FUNC_HELPER2(part1, part2, part3)                  \
  part1##part2##part3
#define RLBOX_WASM2C_MODULE_FUNC_HELPER(part1, part2, part3)                   \
  RLBOX_WASM2C_MODULE_FUNC_HELPER2(part1, part2, part3)
#define RLBOX_WASM2C_MODULE_FUNC(name)                                         \
  RLBOX_WASM2C_MODULE_FUNC_HELPER(w2c_, RLBOX_WASM2C_MODULE_NAME, name)

namespace rlbox {

namespace wasm2c_detail {

  template<typename T>
  constexpr bool false_v = false;

  // https://stackoverflow.com/questions/6512019/can-we-get-the-type-of-a-lambda-argument
  namespace return_argument_detail {
    template<typename Ret, typename... Rest>
    Ret helper(Ret (*)(Rest...));

    template<typename Ret, typename F, typename... Rest>
    Ret helper(Ret (F::*)(Rest...));

    template<typename Ret, typename F, typename... Rest>
    Ret helper(Ret (F::*)(Rest...) const);

    template<typename F>
    decltype(helper(&F::operator())) helper(F);
  } // namespace return_argument_detail

  template<typename T>
  using return_argument =
    decltype(return_argument_detail::helper(std::declval<T>()));

  ///////////////////////////////////////////////////////////////

  // https://stackoverflow.com/questions/37602057/why-isnt-a-for-loop-a-compile-time-expression
  namespace compile_time_for_detail {
    template<std::size_t N>
    struct num
    {
      static const constexpr auto value = N;
    };

    template<class F, std::size_t... Is>
    inline void compile_time_for_helper(F func, std::index_sequence<Is...>)
    {
      (func(num<Is>{}), ...);
    }
  } // namespace compile_time_for_detail

  template<std::size_t N, typename F>
  inline void compile_time_for(F func)
  {
    compile_time_for_detail::compile_time_for_helper(
      func, std::make_index_sequence<N>());
  }

  ///////////////////////////////////////////////////////////////

  template<typename T, typename = void>
  struct convert_type_to_wasm_type
  {
    static_assert(std::is_void_v<T>, "Missing specialization");
    using type = void;
    // wasm2c has no void type so use i32 for now
    static constexpr wasm_rt_type_t wasm2c_type = WASM_RT_I32;
  };

  template<typename T>
  struct convert_type_to_wasm_type<
    T,
    std::enable_if_t<(std::is_integral_v<T> || std::is_enum_v<T>)&&sizeof(T) <=
                     sizeof(uint32_t)>>
  {
    using type = uint32_t;
    static constexpr wasm_rt_type_t wasm2c_type = WASM_RT_I32;
  };

  template<typename T>
  struct convert_type_to_wasm_type<
    T,
    std::enable_if_t<(std::is_integral_v<T> ||
                      std::is_enum_v<T>)&&sizeof(uint32_t) < sizeof(T) &&
                     sizeof(T) <= sizeof(uint64_t)>>
  {
    using type = uint64_t;
    static constexpr wasm_rt_type_t wasm2c_type = WASM_RT_I64;
  };

  template<typename T>
  struct convert_type_to_wasm_type<T,
                                   std::enable_if_t<std::is_same_v<T, float>>>
  {
    using type = T;
    static constexpr wasm_rt_type_t wasm2c_type = WASM_RT_F32;
  };

  template<typename T>
  struct convert_type_to_wasm_type<T,
                                   std::enable_if_t<std::is_same_v<T, double>>>
  {
    using type = T;
    static constexpr wasm_rt_type_t wasm2c_type = WASM_RT_F64;
  };

  template<typename T>
  struct convert_type_to_wasm_type<
    T,
    std::enable_if_t<std::is_pointer_v<T> || std::is_class_v<T>>>
  {
    // pointers are 32 bit indexes in wasm
    // class paramters are passed as a pointer to an object in the stack or heap
    using type = uint32_t;
    static constexpr wasm_rt_type_t wasm2c_type = WASM_RT_I32;
  };

  ///////////////////////////////////////////////////////////////

  namespace prepend_arg_type_detail {
    template<typename T, typename T_ArgNew>
    struct helper;

    template<typename T_ArgNew, typename T_Ret, typename... T_Args>
    struct helper<T_Ret(T_Args...), T_ArgNew>
    {
      using type = T_Ret(T_ArgNew, T_Args...);
    };
  }

  template<typename T_Func, typename T_ArgNew>
  using prepend_arg_type =
    typename prepend_arg_type_detail::helper<T_Func, T_ArgNew>::type;

  ///////////////////////////////////////////////////////////////

  namespace change_return_type_detail {
    template<typename T, typename T_RetNew>
    struct helper;

    template<typename T_RetNew, typename T_Ret, typename... T_Args>
    struct helper<T_Ret(T_Args...), T_RetNew>
    {
      using type = T_RetNew(T_Args...);
    };
  }

  template<typename T_Func, typename T_RetNew>
  using change_return_type =
    typename change_return_type_detail::helper<T_Func, T_RetNew>::type;

  ///////////////////////////////////////////////////////////////

  namespace change_class_arg_types_detail {
    template<typename T, typename T_ArgNew>
    struct helper;

    template<typename T_ArgNew, typename T_Ret, typename... T_Args>
    struct helper<T_Ret(T_Args...), T_ArgNew>
    {
      using type =
        T_Ret(std::conditional_t<std::is_class_v<T_Args>, T_ArgNew, T_Args>...);
    };
  }

  template<typename T_Func, typename T_ArgNew>
  using change_class_arg_types =
    typename change_class_arg_types_detail::helper<T_Func, T_ArgNew>::type;

} // namespace wasm2c_detail

// declare the static symbol with weak linkage to keep this header only
#if defined(_WIN32)
__declspec(selectany)
#else
__attribute__((weak))
#endif
  std::once_flag rlbox_wasm2c_initialized;

class rlbox_wasm2c_sandbox
{
public:
  using T_LongLongType = int64_t;
  using T_LongType = int32_t;
  using T_IntType = int32_t;
  using T_PointerType = uint32_t;
  using T_ShortType = int16_t;

private:
  mutable typename RLBOX_WASM_MODULE_TYPE_CURR::instance_t wasm2c_instance{ 0 };
  struct w2c_env sandbox_memory_env{ 0 };
  struct w2c_wasi__snapshot__preview1 wasi_env{ 0 };
  bool instance_initialized = false;
  bool minwasi_init_inst_succeeded = false;
  // Only used if memory and tables are imported
  wasm_rt_memory_t local_sandbox_memory_info{ 0 };
  mutable wasm_rt_funcref_table_t local_sandbox_callback_table{ 0 };
  // Pointers to actual memory and table. This points to
  // local_sandbox_memory_info and local_sandbox_callback_table if the Wasm
  // module imports the memory and table.
  wasm_rt_memory_t* sandbox_memory_info{ 0 };
  mutable wasm_rt_funcref_table_t* sandbox_callback_table{ 0 };
  uintptr_t heap_base = 0;
  size_t return_slot_size = 0;
  T_PointerType return_slot = 0;
  mutable std::vector<T_PointerType> callback_free_list;

  static const size_t MAX_CALLBACKS = 128;
  mutable RLBOX_SHARED_LOCK(callback_mutex);
  void* callback_unique_keys[MAX_CALLBACKS]{ 0 };
  void* callbacks[MAX_CALLBACKS]{ 0 };
  uint32_t callback_slot_assignment[MAX_CALLBACKS]{ 0 };
  mutable std::map<const void*, uint32_t> internal_callbacks;
  mutable std::map<uint32_t, const void*> slot_assignments;

#ifndef RLBOX_EMBEDDER_PROVIDES_TLS_STATIC_VARIABLES
  thread_local static inline rlbox_wasm2c_sandbox_thread_data thread_data{ 0,
                                                                           0 };
#endif

  template<typename T_FormalRet, typename T_ActualRet>
  inline auto serialize_to_sandbox(T_ActualRet arg)
  {
    if constexpr (std::is_class_v<T_FormalRet>) {
      // structs returned as pointers into wasm memory/wasm stack
      auto ptr = reinterpret_cast<T_FormalRet*>(
        impl_get_unsandboxed_pointer<T_FormalRet*>(arg));
      T_FormalRet ret = *ptr;
      return ret;
    } else {
      return arg;
    }
  }

  template<uint32_t N, typename T_Ret, typename... T_Args>
  static typename wasm2c_detail::convert_type_to_wasm_type<T_Ret>::type
  callback_interceptor(
    void* /* vmContext */,
    typename wasm2c_detail::convert_type_to_wasm_type<T_Args>::type... params)
  {
#ifdef RLBOX_EMBEDDER_PROVIDES_TLS_STATIC_VARIABLES
    auto& thread_data = *get_rlbox_wasm2c_sandbox_thread_data();
#endif
    thread_data.last_callback_invoked = N;
    using T_Func = T_Ret (*)(T_Args...);
    T_Func func;
    {
#ifndef RLBOX_SINGLE_THREADED_INVOCATIONS
      RLBOX_ACQUIRE_SHARED_GUARD(lock, thread_data.sandbox->callback_mutex);
#endif
      func = reinterpret_cast<T_Func>(thread_data.sandbox->callbacks[N]);
    }
    // Callbacks are invoked through function pointers, cannot use std::forward
    // as we don't have caller context for T_Args, which means they are all
    // effectively passed by value
    return func(
      thread_data.sandbox->template serialize_to_sandbox<T_Args>(params)...);
  }

  template<uint32_t N, typename T_Ret, typename... T_Args>
  static void callback_interceptor_promoted(
    void* /* vmContext */,
    typename wasm2c_detail::convert_type_to_wasm_type<T_Ret>::type ret,
    typename wasm2c_detail::convert_type_to_wasm_type<T_Args>::type... params)
  {
#ifdef RLBOX_EMBEDDER_PROVIDES_TLS_STATIC_VARIABLES
    auto& thread_data = *get_rlbox_wasm2c_sandbox_thread_data();
#endif
    thread_data.last_callback_invoked = N;
    using T_Func = T_Ret (*)(T_Args...);
    T_Func func;
    {
#ifndef RLBOX_SINGLE_THREADED_INVOCATIONS
      RLBOX_ACQUIRE_SHARED_GUARD(lock, thread_data.sandbox->callback_mutex);
#endif
      func = reinterpret_cast<T_Func>(thread_data.sandbox->callbacks[N]);
    }
    // Callbacks are invoked through function pointers, cannot use std::forward
    // as we don't have caller context for T_Args, which means they are all
    // effectively passed by value
    auto ret_val = func(
      thread_data.sandbox->template serialize_to_sandbox<T_Args>(params)...);
    // Copy the return value back
    auto ret_ptr = reinterpret_cast<T_Ret*>(
      thread_data.sandbox->template impl_get_unsandboxed_pointer<T_Ret*>(ret));
    *ret_ptr = ret_val;
  }

  template<typename T_Ret, typename... T_Args>
  inline wasm_rt_func_type_t get_wasm2c_func_index(
    // dummy for template inference
    T_Ret (*)(T_Args...) = nullptr) const
  {
    // Class return types as promoted to args
    constexpr bool promoted = std::is_class_v<T_Ret>;
    constexpr uint32_t param_count =
      promoted ? (sizeof...(T_Args) + 1) : (sizeof...(T_Args));
    constexpr uint32_t ret_count =
      promoted ? 0 : (std::is_void_v<T_Ret> ? 0 : 1);

    wasm_rt_func_type_t ret = nullptr;
    if constexpr (ret_count == 0) {
      ret = RLBOX_WASM_MODULE_TYPE_CURR::get_func_type(
        param_count,
        ret_count,
        wasm2c_detail::convert_type_to_wasm_type<T_Args>::wasm2c_type...);
    } else {
      ret = RLBOX_WASM_MODULE_TYPE_CURR::get_func_type(
        param_count,
        ret_count,
        wasm2c_detail::convert_type_to_wasm_type<T_Args>::wasm2c_type...,
        wasm2c_detail::convert_type_to_wasm_type<T_Ret>::wasm2c_type);
    }

    return ret;
  }

  void ensure_return_slot_size(size_t size)
  {
    if (size > return_slot_size) {
      if (return_slot_size) {
        impl_free_in_sandbox(return_slot);
      }
      return_slot = impl_malloc_in_sandbox(size);
      detail::dynamic_check(
        return_slot != 0,
        "Error initializing return slot. Sandbox may be out of memory!");
      return_slot_size = size;
    }
  }

  // function takes a 32-bit value and returns the next power of 2
  // return is a 64-bit value as large 32-bit values will return 2^32
  static inline uint64_t next_power_of_two(uint32_t value)
  {
    uint64_t power = 1;
    while (power < value) {
      power *= 2;
    }
    return power;
  }

protected:
#define rlbox_wasm2c_sandbox_lookup_symbol(func_name)                          \
  reinterpret_cast<void*>(&RLBOX_WASM2C_MODULE_FUNC(_##func_name)) /* NOLINT   \
                                                                    */

  // adding a template so that we can use static_assert to fire only if this
  // function is invoked
  template<typename T = void>
  void* impl_lookup_symbol(const char* func_name)
  {
    constexpr bool fail = std::is_same_v<T, void>;
    static_assert(
      !fail,
      "The wasm2c_sandbox uses static calls and thus developers should add\n\n"
      "#define RLBOX_USE_STATIC_CALLS() rlbox_wasm2c_sandbox_lookup_symbol\n\n"
      "to their code, to ensure that static calls are handled correctly.");
    return nullptr;
  }

  using create_instance_t = std::remove_cv_t<decltype(RLBOX_WASM_MODULE_TYPE_CURR::create_instance)>;
  static constexpr bool is_imported_memory_and_table = std::is_same_v<create_instance_t, RLBOX_WASM_MODULE_TYPE_CURR::create_instance_imported_t>;
  static constexpr bool is_exported_memory_and_table = std::is_same_v<create_instance_t, RLBOX_WASM_MODULE_TYPE_CURR::create_instance_exported_t>;

public:
#define FALLIBLE_DYNAMIC_CHECK(infallible, cond, msg)                          \
  if (infallible) {                                                            \
    detail::dynamic_check(cond, msg);                                          \
  } else if (!(cond)) {                                                        \
    impl_destroy_sandbox();                                                    \
    return false;                                                              \
  }

  /**
   * @brief creates the Wasm sandbox from the given shared library
   *
   * @param infallible if set to true, the sandbox aborts on failure. If false,
   * the sandbox returns creation status as a return value
   * @param custom_capacity allows optionally overriding the platform-specified
   * maximum size of the wasm heap allowed for this sandbox instance.
   * @return true when sandbox is successfully created. false when infallible is
   * set to false and sandbox was not successfully created. If infallible is set
   * to true, this function will never return false.
   */
  inline bool impl_create_sandbox(
    bool infallible = true,
    const w2c_mem_capacity* custom_capacity = nullptr,
    const char* instance_name = nullptr)
  {
    FALLIBLE_DYNAMIC_CHECK(
      infallible, instance_initialized == false, "Sandbox already initialized");

    bool minwasi_init_succeeded = true;

    std::call_once(rlbox_wasm2c_initialized, [&]() {
      wasm_rt_init();
      minwasi_init_succeeded = minwasi_init();
    });

    FALLIBLE_DYNAMIC_CHECK(
      infallible, minwasi_init_succeeded, "Could not initialize min wasi");

    minwasi_init_inst_succeeded = minwasi_init_instance(&wasi_env);
    FALLIBLE_DYNAMIC_CHECK(
      infallible, minwasi_init_inst_succeeded, "Could not initialize min wasi instance");

    if (custom_capacity) {
      FALLIBLE_DYNAMIC_CHECK(
        infallible, custom_capacity->is_valid, "Invalid capacity");
    }

    static_assert(is_imported_memory_and_table || is_exported_memory_and_table,
      "Wasm modules memories and tables must either be imported or exported");

    if constexpr (is_imported_memory_and_table) {
      sandbox_memory_info = &local_sandbox_memory_info;
      sandbox_callback_table = &local_sandbox_callback_table;

      INVOKE_DEFINE_RLBOX_WASM2C_IMPORTED_MODULE_ATTRIBS(RLBOX_WASM2C_MODULE_NAME);

      *sandbox_memory_info = create_wasm2c_memory(
        *initial_memory_pages, custom_capacity, instance_name? instance_name : "rlbox_wasm2c");
      FALLIBLE_DYNAMIC_CHECK(infallible,
                            sandbox_memory_info->data != nullptr,
                            "Could not allocate a heap for the wasm2c sandbox");

      FALLIBLE_DYNAMIC_CHECK(infallible,
                            *is_memory_64 == 0,
                            "Does not support Wasm with memory64");

      const uint32_t max_table_size = 0xffffffffu; /* this means unlimited */
      wasm_rt_allocate_funcref_table(
        sandbox_callback_table,
        *initial_func_elements,
        max_table_size);

      sandbox_memory_env.sandbox_memory_info = sandbox_memory_info;
      sandbox_memory_env.sandbox_callback_table = sandbox_callback_table;
      wasi_env.instance_memory = sandbox_memory_info;

      auto create_instance_func = (RLBOX_WASM_MODULE_TYPE_CURR::create_instance_imported_t) RLBOX_WASM_MODULE_TYPE_CURR::create_instance;
      create_instance_func(&wasm2c_instance, &sandbox_memory_env, &wasi_env);
    } else {
      INVOKE_DEFINE_RLBOX_WASM2C_EXPORTED_MODULE_ATTRIBS(RLBOX_WASM2C_MODULE_NAME);
      sandbox_memory_info = get_exported_memory(&wasm2c_instance);
      sandbox_callback_table = get_exported_table(&wasm2c_instance);

      wasi_env.instance_memory = sandbox_memory_info;

      auto create_instance_func = (RLBOX_WASM_MODULE_TYPE_CURR::create_instance_exported_t) RLBOX_WASM_MODULE_TYPE_CURR::create_instance;
      create_instance_func(&wasm2c_instance, &wasi_env);
    }

    heap_base = reinterpret_cast<uintptr_t>(impl_get_memory_location());

    if constexpr (is_imported_memory_and_table && sizeof(uintptr_t) != sizeof(uint32_t)) {
      // On larger platforms, with imported memory check that the heap is
      // aligned to the pointer size i.e. 32-bit pointer => aligned to 4GB. The
      // implementations of impl_get_unsandboxed_pointer_no_ctx and
      // impl_get_sandboxed_pointer_no_ctx below rely on this.
      uintptr_t heap_offset_mask = std::numeric_limits<T_PointerType>::max();
      FALLIBLE_DYNAMIC_CHECK(infallible,
                             (heap_base & heap_offset_mask) == 0,
                             "Sandbox heap not aligned to 4GB");
    }

    instance_initialized = true;

    return true;
  }

#undef FALLIBLE_DYNAMIC_CHECK

  inline void impl_destroy_sandbox()
  {
    if (return_slot_size) {
      impl_free_in_sandbox(return_slot);
    }

    if (instance_initialized) {
      instance_initialized = false;
      RLBOX_WASM_MODULE_TYPE_CURR::free_instance(&wasm2c_instance);
    }

    if constexpr (is_imported_memory_and_table) {
      if (sandbox_memory_info && sandbox_memory_info->data) {
        destroy_wasm2c_memory(sandbox_memory_info);
        sandbox_memory_info = nullptr;
      }
      if (sandbox_callback_table && sandbox_callback_table->data) {
        wasm_rt_free_funcref_table(sandbox_callback_table);
        sandbox_callback_table = nullptr;
      }
    }

    if (minwasi_init_inst_succeeded) {
      minwasi_init_inst_succeeded = false;
      minwasi_cleanup_instance(&wasi_env);
    }
  }

  template<typename T>
  inline void* impl_get_unsandboxed_pointer(T_PointerType p) const
  {
    if constexpr (std::is_function_v<std::remove_pointer_t<T>>) {
      RLBOX_ACQUIRE_UNIQUE_GUARD(lock, callback_mutex);
      auto found = slot_assignments.find(p);
      if (found != slot_assignments.end()) {
        auto ret = found->second;
        return const_cast<void*>(ret);
      } else {
        return nullptr;
      }
    } else {
      auto ret = reinterpret_cast<void*>(heap_base + p);
      if constexpr (sizeof(uintptr_t) == sizeof(uint32_t)) {
        detail::dynamic_check(impl_is_pointer_in_sandbox_memory(ret),
                              "Received an invalid pointer");
      }
      return ret;
    }
  }

  template<typename T>
  inline T_PointerType impl_get_sandboxed_pointer(const void* p) const
  {
    if constexpr (std::is_function_v<std::remove_pointer_t<T>>) {
      RLBOX_ACQUIRE_UNIQUE_GUARD(lock, callback_mutex);

      uint32_t slot_number = 0;
      auto found = internal_callbacks.find(p);
      if (found != internal_callbacks.end()) {
        slot_number = found->second;
      } else {

        slot_number = new_callback_slot();
        wasm_rt_funcref_t func_val;
        func_val.func_type = get_wasm2c_func_index(static_cast<T>(nullptr));
        func_val.func =
          reinterpret_cast<wasm_rt_function_ptr_t>(const_cast<void*>(p));
        func_val.module_instance = &wasm2c_instance;

        sandbox_callback_table->data[slot_number] = func_val;
        internal_callbacks[p] = slot_number;
        slot_assignments[slot_number] = p;
      }
      return static_cast<T_PointerType>(slot_number);
    } else {
      // if imported memory on a 64-bit platform, we can assume that the heap is
      // aligned and let integer truncation handle the conversion
      if constexpr (is_imported_memory_and_table && sizeof(uintptr_t) != sizeof(uint32_t)) {
        return static_cast<T_PointerType>(reinterpret_cast<uintptr_t>(p));
      } else {
        return static_cast<T_PointerType>(reinterpret_cast<uintptr_t>(p) -
                                          heap_base);
      }
    }
  }

  template<typename T>
  static inline void* impl_get_unsandboxed_pointer_no_ctx(
    T_PointerType p,
    const void* example_unsandboxed_ptr,
    rlbox_wasm2c_sandbox* (*expensive_sandbox_finder)(
      const void* example_unsandboxed_ptr))
  {
    // if imported memory on a 64-bit platform, we can assume that the heap is
    // aligned
    if constexpr (is_imported_memory_and_table && sizeof(uintptr_t) != sizeof(uint32_t)) {
      if constexpr (std::is_function_v<std::remove_pointer_t<T>>) {
        // swizzling function pointers needs access to the function pointer
        // tables and thus cannot be done without context
        auto sandbox = expensive_sandbox_finder(example_unsandboxed_ptr);
        return sandbox->template impl_get_unsandboxed_pointer<T>(p);
      } else {
        // grab the memory base from the example_unsandboxed_ptr
        uintptr_t heap_base_mask =
          std::numeric_limits<uintptr_t>::max() &
          ~(static_cast<uintptr_t>(std::numeric_limits<T_PointerType>::max()));
        uintptr_t computed_heap_base =
          reinterpret_cast<uintptr_t>(example_unsandboxed_ptr) & heap_base_mask;
        uintptr_t ret = computed_heap_base | p;
        return reinterpret_cast<void*>(ret);
      }
    } else {
      auto sandbox = expensive_sandbox_finder(example_unsandboxed_ptr);
      return sandbox->template impl_get_unsandboxed_pointer<T>(p);
    }
  }

  template<typename T>
  static inline T_PointerType impl_get_sandboxed_pointer_no_ctx(
    const void* p,
    const void* example_unsandboxed_ptr,
    rlbox_wasm2c_sandbox* (*expensive_sandbox_finder)(
      const void* example_unsandboxed_ptr))
  {
    // if imported memory on a 64-bit platform, we can assume that the heap is
    // aligned
    if constexpr (is_imported_memory_and_table && sizeof(uintptr_t) != sizeof(uint32_t)) {
      if constexpr (std::is_function_v<std::remove_pointer_t<T>>) {
        // swizzling function pointers needs access to the function pointer
        // tables and thus cannot be done without context
        auto sandbox = expensive_sandbox_finder(example_unsandboxed_ptr);
        return sandbox->template impl_get_sandboxed_pointer<T>(p);
      } else {
        // Just clear the memory base to leave the offset
        RLBOX_WASM2C_UNUSED(example_unsandboxed_ptr);
        uintptr_t ret = reinterpret_cast<uintptr_t>(p) &
                        std::numeric_limits<T_PointerType>::max();
        return static_cast<T_PointerType>(ret);
      }
    } else {
      auto sandbox = expensive_sandbox_finder(example_unsandboxed_ptr);
      return sandbox->template impl_get_sandboxed_pointer<T>(p);
    }
  }

  static inline bool impl_is_in_same_sandbox(const void* p1, const void* p2,
    rlbox_wasm2c_sandbox* (*expensive_sandbox_finder)(const void* hostptr_or_unsandboxedptr)
  )
  {
    if constexpr (sizeof(uintptr_t) == sizeof(uint32_t)) {
      if (p1 == nullptr || p2 == nullptr) {
        return true;
      }

      // This call returns the sandbox the pointer belongs to.
      // It returns null if this belongs to the app.
      void* p1_sbx = expensive_sandbox_finder(p1);
      void* p2_sbx = expensive_sandbox_finder(p2);

      return p1_sbx == p2_sbx;
    } else {
      uintptr_t heap_base_mask = std::numeric_limits<uintptr_t>::max() &
                               ~(static_cast<uintptr_t>(std::numeric_limits<T_PointerType>::max()));
      return (reinterpret_cast<uintptr_t>(p1) & heap_base_mask) ==
            (reinterpret_cast<uintptr_t>(p2) & heap_base_mask);
    }
  }

  inline bool impl_is_pointer_in_sandbox_memory(const void* p) const
  {
    size_t length = impl_get_total_memory();
    uintptr_t p_val = reinterpret_cast<uintptr_t>(p);
    return p_val >= heap_base && p_val < (heap_base + length);
  }

  inline bool impl_is_pointer_in_app_memory(const void* p) const
  {
    return !(impl_is_pointer_in_sandbox_memory(p));
  }

  inline size_t impl_get_total_memory() const { return sandbox_memory_info->size; }

  inline void* impl_get_memory_location() const
  {
    return sandbox_memory_info->data;
  }

  template<typename T, typename T_Converted, typename... T_Args>
  auto impl_invoke_with_func_ptr(T_Converted* func_ptr, T_Args&&... params)
  {
#ifdef RLBOX_EMBEDDER_PROVIDES_TLS_STATIC_VARIABLES
    auto& thread_data = *get_rlbox_wasm2c_sandbox_thread_data();
#endif
    auto old_sandbox = thread_data.sandbox;
    thread_data.sandbox = this;
    auto on_exit =
      detail::make_scope_exit([&] { thread_data.sandbox = old_sandbox; });

    // WASM functions are mangled in the following manner
    // 1. All primitive types are left as is and follow an LP32 machine model
    // (as opposed to the possibly 64-bit application)
    // 2. All pointers are changed to u32 types
    // 3. Returned class are returned as an out parameter before the actual
    // function parameters
    // 4. All class parameters are passed as pointers (u32 types)
    // 5. The heap address is passed in as the first argument to the function
    //
    // RLBox accounts for the first 2 differences in T_Converted type, but we
    // need to handle the rest

    // Handle point 3
    using T_Ret = wasm2c_detail::return_argument<T_Converted>;
    if constexpr (std::is_class_v<T_Ret>) {
      using T_Conv1 = wasm2c_detail::change_return_type<T_Converted, void>;
      using T_Conv2 = wasm2c_detail::prepend_arg_type<T_Conv1, T_PointerType>;
      auto func_ptr_conv =
        reinterpret_cast<T_Conv2*>(reinterpret_cast<uintptr_t>(func_ptr));
      ensure_return_slot_size(sizeof(T_Ret));
      impl_invoke_with_func_ptr<T>(func_ptr_conv, return_slot, params...);

      auto ptr = reinterpret_cast<T_Ret*>(
        impl_get_unsandboxed_pointer<T_Ret*>(return_slot));
      T_Ret ret = *ptr;
      return ret;
    }

    // Handle point 4
    constexpr size_t alloc_length = [&] {
      if constexpr (sizeof...(params) > 0) {
        return ((std::is_class_v<T_Args> ? 1 : 0) + ...);
      } else {
        return 0;
      }
    }();

    // 0 arg functions create 0 length arrays which is not allowed
    T_PointerType allocations_buff[alloc_length == 0 ? 1 : alloc_length];
    T_PointerType* allocations = allocations_buff;

    auto serialize_class_arg =
      [&](auto arg) -> std::conditional_t<std::is_class_v<decltype(arg)>,
                                          T_PointerType,
                                          decltype(arg)> {
      using T_Arg = decltype(arg);
      if constexpr (std::is_class_v<T_Arg>) {
        auto slot = impl_malloc_in_sandbox(sizeof(T_Arg));
        auto ptr =
          reinterpret_cast<T_Arg*>(impl_get_unsandboxed_pointer<T_Arg*>(slot));
        *ptr = arg;
        allocations[0] = slot;
        allocations++;
        return slot;
      } else {
        return arg;
      }
    };

    // 0 arg functions don't use serialize
    RLBOX_WASM2C_UNUSED(serialize_class_arg);

    using T_ConvNoClass =
      wasm2c_detail::change_class_arg_types<T_Converted, T_PointerType>;

    // Handle Point 5
    using T_ConvHeap = wasm2c_detail::prepend_arg_type<
      T_ConvNoClass,
      typename RLBOX_WASM_MODULE_TYPE_CURR::instance_t*>;

    // Function invocation
    auto func_ptr_conv =
      reinterpret_cast<T_ConvHeap*>(reinterpret_cast<uintptr_t>(func_ptr));

    using T_NoVoidRet =
      std::conditional_t<std::is_void_v<T_Ret>, uint32_t, T_Ret>;
    T_NoVoidRet ret;

    if constexpr (std::is_void_v<T_Ret>) {
      RLBOX_WASM2C_UNUSED(ret);
      func_ptr_conv(&wasm2c_instance, serialize_class_arg(params)...);
    } else {
      ret = func_ptr_conv(&wasm2c_instance, serialize_class_arg(params)...);
    }

    for (size_t i = 0; i < alloc_length; i++) {
      impl_free_in_sandbox(allocations_buff[i]);
    }

    if constexpr (!std::is_void_v<T_Ret>) {
      return ret;
    }
  }

  inline T_PointerType impl_malloc_in_sandbox(size_t size)
  {
    if constexpr (sizeof(size) > sizeof(uint32_t)) {
      detail::dynamic_check(size <= std::numeric_limits<uint32_t>::max(),
                            "Attempting to malloc more than the heap size");
    }
    using T_Func = void*(size_t);
    using T_Converted = T_PointerType(uint32_t);
    T_PointerType ret = impl_invoke_with_func_ptr<T_Func, T_Converted>(
      reinterpret_cast<T_Converted*>(
        RLBOX_WASM_MODULE_TYPE_CURR::malloc_address),
      static_cast<uint32_t>(size));
    return ret;
  }

  inline void impl_free_in_sandbox(T_PointerType p)
  {
    using T_Func = void(void*);
    using T_Converted = void(T_PointerType);
    impl_invoke_with_func_ptr<T_Func, T_Converted>(
      reinterpret_cast<T_Converted*>(RLBOX_WASM_MODULE_TYPE_CURR::free_address),
      p);
  }

private:
  // Should be called with callback_mutex held
  uint32_t new_callback_slot() const
  {
    if (callback_free_list.size() > 0) {
      uint32_t ret = callback_free_list.back();
      callback_free_list.pop_back();
      return ret;
    }

    const uint32_t curr_size = sandbox_callback_table->size;

    detail::dynamic_check(
      curr_size < sandbox_callback_table->max_size,
      "Could not find an empty row in Wasm instance table. This would "
      "happen if you have registered too many callbacks, or unsandboxed "
      "too many function pointers.");

    wasm_rt_funcref_t func_val{ 0 };
    // on success, this returns the previous number of elements in the table
    const uint32_t ret =
      wasm_rt_grow_funcref_table(sandbox_callback_table, 1, func_val);

    detail::dynamic_check(
      ret != 0 && ret != (uint32_t)-1,
      "Adding a new callback slot to the wasm instance failed.");

    // We have expanded the number of slots
    // Previous slots size: ret
    // New slot is at index: ret
    const uint32_t slot_number = ret;
    return slot_number;
  }

  void free_callback_slot(uint32_t slot) const
  {
    callback_free_list.push_back(slot);
  }

public:
  template<typename T_Ret, typename... T_Args>
  inline T_PointerType impl_register_callback(void* key, void* callback)
  {
    bool found = false;
    uint32_t found_loc = 0;
    wasm_rt_function_ptr_t chosen_interceptor = nullptr;

    RLBOX_ACQUIRE_UNIQUE_GUARD(lock, callback_mutex);

    // need a compile time for loop as we we need I to be a compile time value
    // this is because we are setting the I'th callback ineterceptor
    wasm2c_detail::compile_time_for<MAX_CALLBACKS>([&](auto I) {
      constexpr auto i = I.value;
      if (!found && callbacks[i] == nullptr) {
        found = true;
        found_loc = i;

        if constexpr (std::is_class_v<T_Ret>) {
          chosen_interceptor = (wasm_rt_function_ptr_t)(
            callback_interceptor_promoted<i, T_Ret, T_Args...>);
        } else {
          chosen_interceptor =
            (wasm_rt_function_ptr_t)(callback_interceptor<i, T_Ret, T_Args...>);
        }
      }
    });

    detail::dynamic_check(
      found,
      "Could not find an empty slot in sandbox function table. This would "
      "happen if you have registered too many callbacks, or unsandboxed "
      "too many function pointers. You can file a bug if you want to "
      "increase the maximum allowed callbacks or unsadnboxed functions "
      "pointers");

    wasm_rt_funcref_t func_val;
    func_val.func_type = get_wasm2c_func_index<T_Ret, T_Args...>();
    func_val.func = chosen_interceptor;
    func_val.module_instance = &wasm2c_instance;

    const uint32_t slot_number = new_callback_slot();
    sandbox_callback_table->data[slot_number] = func_val;

    callback_unique_keys[found_loc] = key;
    callbacks[found_loc] = callback;
    callback_slot_assignment[found_loc] = slot_number;
    slot_assignments[slot_number] = callback;

    return static_cast<T_PointerType>(slot_number);
  }

  static inline std::pair<rlbox_wasm2c_sandbox*, void*>
  impl_get_executed_callback_sandbox_and_key()
  {
#ifdef RLBOX_EMBEDDER_PROVIDES_TLS_STATIC_VARIABLES
    auto& thread_data = *get_rlbox_wasm2c_sandbox_thread_data();
#endif
    auto sandbox = thread_data.sandbox;
    auto callback_num = thread_data.last_callback_invoked;
    void* key = sandbox->callback_unique_keys[callback_num];
    return std::make_pair(sandbox, key);
  }

  template<typename T_Ret, typename... T_Args>
  inline void impl_unregister_callback(void* key)
  {
    bool found = false;
    uint32_t i = 0;
    {
      RLBOX_ACQUIRE_UNIQUE_GUARD(lock, callback_mutex);
      for (; i < MAX_CALLBACKS; i++) {
        if (callback_unique_keys[i] == key) {
          const uint32_t slot_number = callback_slot_assignment[i];
          wasm_rt_funcref_t func_val{ 0 };
          sandbox_callback_table->data[slot_number] = func_val;

          callback_unique_keys[i] = nullptr;
          callbacks[i] = nullptr;
          callback_slot_assignment[i] = 0;
          found = true;
          break;
        }
      }
    }

    detail::dynamic_check(
      found, "Internal error: Could not find callback to unregister");

    return;
  }
};

} // namespace rlbox
