#ifndef WASM_RT_OS_H_
#define WASM_RT_OS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "wasm-rt.h"

#ifdef __cplusplus
extern "C"
{
#endif

  typedef struct w2c_env
  {
    wasm_rt_memory_t* sandbox_memory_info;
    wasm_rt_funcref_table_t* sandbox_callback_table;
  } w2c_env;

  wasm_rt_memory_t* w2c_env_memory(struct w2c_env* instance);
  wasm_rt_funcref_table_t* w2c_env_0x5F_indirect_function_table(
    struct w2c_env*);

  typedef struct w2c_mem_capacity
  {
    bool is_valid;
    bool is_mem_32;
    uint64_t max_pages;
    uint64_t max_size;
  } w2c_mem_capacity;

  w2c_mem_capacity get_valid_wasm2c_memory_capacity(uint64_t min_capacity,
                                                    bool is_mem_32);

  wasm_rt_memory_t create_wasm2c_memory(uint32_t initial_pages,
                                        const w2c_mem_capacity* custom_capacity,
                                        const char* name);
  void destroy_wasm2c_memory(wasm_rt_memory_t* memory);

#ifdef __cplusplus
}
#endif

#endif
