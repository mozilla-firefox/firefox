#include "wasm2c_rt_mem.h"
#include "wasm-rt.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
  MMAP_PROT_NONE = 0,
  MMAP_PROT_READ = 1,
  MMAP_PROT_WRITE = 2,
  MMAP_PROT_EXEC = 4
};

/* Memory map flags */
enum
{
  MMAP_MAP_NONE = 0,
  /* Put the mapping into 0 to 2 G, supported only on x86_64 */
  MMAP_MAP_32BIT = 1,
  /* Don't interpret addr as a hint: place the mapping at exactly
     that address. */
  MMAP_MAP_FIXED = 2
};

// Try reserving an aligned memory space.
// Returns pointer to allocated space on success, 0 on failure.
static void* os_mmap_aligned(void* addr,
                             size_t requested_length,
                             int prot,
                             int flags,
                             size_t alignment,
                             size_t alignment_offset,
                             const char* name);
// Unreserve the memory space
static void os_munmap(void* addr, size_t size);
// Allocates and sets the permissions on the previously reserved memory space
// Returns 0 on success, non zero on failure.
static int os_mmap_commit(void* curr_heap_end_pointer,
                          size_t expanded_size,
                          int prot);

wasm_rt_memory_t* w2c_env_memory(struct w2c_env* instance)
{
  return instance->sandbox_memory_info;
}

wasm_rt_funcref_table_t* w2c_env_0x5F_indirect_function_table(
  struct w2c_env* instance)
{
  return instance->sandbox_callback_table;
}

#define WASM_PAGE_SIZE 65536
#define RLBOX_FOUR_GIG 0x100000000ull

#if UINTPTR_MAX == 0xffffffffffffffff
// Guard page of 4GiB
#  define WASM_HEAP_GUARD_PAGE_SIZE 0x100000000ull
// Heap aligned to 8GB. Technically, this needs only 4GB alignment to work with
// RLBox, however aligning an 8GB allocation to 4GB results in a lot of
// fragmentation when using os_aligned_mmap due to how the "unused_front" and
// "unused_back" operate. So use 8GB instead which works better.
#  define WASM_HEAP_ALIGNMENT 0x200000000ull
// By default max heap is 4GB
#  define WASM_HEAP_DEFAULT_MAX_PAGES 65536
#elif UINTPTR_MAX == 0xffffffff
// Single guard page as an extra safety check as a second defense for partial
// OOB read/writes. Platforms that have non-4k pages will simply round up this
// value to the their default page size, which will work fine.
#  define WASM_HEAP_GUARD_PAGE_SIZE 4096
// Unaligned heap
#  define WASM_HEAP_ALIGNMENT 0
// Default max heap is 16MB
#  define WASM_HEAP_DEFAULT_MAX_PAGES 256
#else
#  error "Unknown pointer size"
#endif

static uint64_t compute_heap_reserve_space(uint32_t chosen_max_pages)
{
  const uint64_t heap_reserve_size =
    ((uint64_t)chosen_max_pages) * WASM_PAGE_SIZE + WASM_HEAP_GUARD_PAGE_SIZE;
  return heap_reserve_size;
}

w2c_mem_capacity get_valid_wasm2c_memory_capacity(uint64_t min_capacity,
                                                  bool is_mem_32)
{
  const w2c_mem_capacity err_val = { false /* is_valid */,
                                     false /* is_mem_32 */,
                                     0 /* max_pages */,
                                     0 /* max_size */ };

  // We do not handle memory 64
  if (!is_mem_32) {
    return err_val;
  }

  const uint64_t default_capacity =
    ((uint64_t)WASM_HEAP_DEFAULT_MAX_PAGES) * WASM_PAGE_SIZE;

  if (min_capacity <= default_capacity) {
    // Handle 0 case and small values
    const w2c_mem_capacity ret = { true /* is_valid */,
                                   true /* is_mem_32 */,
                                   WASM_HEAP_DEFAULT_MAX_PAGES /* max_pages */,
                                   default_capacity /* max_size */ };
    return ret;
  } else if (min_capacity > UINT32_MAX) {
    // Handle out of range values
    return err_val;
  }

  const uint64_t page_size_minus_1 = WASM_PAGE_SIZE - 1;
  // Get number of pages greater than min_capacity
  const uint64_t capacity_pages = ((min_capacity - 1) / page_size_minus_1) + 1;

  const w2c_mem_capacity ret = { true /* is_valid */,
                                 true /* is_mem_32 */,
                                 capacity_pages /* max_pages */,
                                 capacity_pages *
                                   WASM_PAGE_SIZE /* max_size */ };
  return ret;
}

wasm_rt_memory_t create_wasm2c_memory(uint32_t initial_pages,
                                      const w2c_mem_capacity* custom_capacity,
                                      const char* name)
{

  if (custom_capacity && !custom_capacity->is_valid) {
    wasm_rt_memory_t ret = { 0 };
    return ret;
  }

  const uint32_t byte_length = initial_pages * WASM_PAGE_SIZE;
  const uint64_t chosen_max_pages =
    custom_capacity ? custom_capacity->max_pages : WASM_HEAP_DEFAULT_MAX_PAGES;
  const uint64_t heap_reserve_size =
    compute_heap_reserve_space(chosen_max_pages);

  uint8_t* data = 0;
  const uint64_t retries = 10;
  for (uint64_t i = 0; i < retries; i++) {
    data = (uint8_t*)os_mmap_aligned(0,
                                     heap_reserve_size,
                                     MMAP_PROT_NONE,
                                     MMAP_MAP_NONE,
                                     WASM_HEAP_ALIGNMENT,
                                     0 /* alignment_offset */,
                                     name);
    if (data) {
      int ret =
        os_mmap_commit(data, byte_length, MMAP_PROT_READ | MMAP_PROT_WRITE);
      if (ret != 0) {
        // failed to set permissions
        os_munmap(data, heap_reserve_size);
        data = 0;
      }
      break;
    }
  }

  wasm_rt_memory_t ret;
  ret.data = data;
  ret.page_size = WASM_PAGE_SIZE;
  ret.pages = initial_pages;
  ret.max_pages = chosen_max_pages;
  ret.size = byte_length;
  ret.is64 = 0;
  return ret;
}

void destroy_wasm2c_memory(wasm_rt_memory_t* memory)
{
  if (memory->data != 0) {
    const uint64_t heap_reserve_size =
      compute_heap_reserve_space(memory->max_pages);
    os_munmap(memory->data, heap_reserve_size);
    memory->data = 0;
  }
}

#undef WASM_HEAP_DEFAULT_MAX_PAGES
#undef WASM_HEAP_ALIGNMENT
#undef WASM_HEAP_GUARD_PAGE_SIZE
#undef RLBOX_FOUR_GIG
#undef WASM_PAGE_SIZE

// Based on
// https://web.archive.org/web/20191012035921/http://nadeausoftware.com/articles/2012/01/c_c_tip_how_use_compiler_predefined_macros_detect_operating_system#BSD
// Check for windows (non cygwin) environment
#if defined(_WIN32)

#  include <windows.h>

static size_t os_getpagesize()
{
  SYSTEM_INFO S;
  GetNativeSystemInfo(&S);
  return S.dwPageSize;
}

static void* win_mmap(void* hint,
                      size_t size,
                      int prot,
                      int flags,
                      DWORD alloc_flag)
{
  DWORD flProtect = PAGE_NOACCESS;
  size_t request_size, page_size;
  void* addr;

  page_size = os_getpagesize();
  request_size = (size + page_size - 1) & ~(page_size - 1);

  if (request_size < size)
    /* integer overflow */
    return NULL;

  if (request_size == 0)
    request_size = page_size;

  if (prot & MMAP_PROT_EXEC) {
    if (prot & MMAP_PROT_WRITE)
      flProtect = PAGE_EXECUTE_READWRITE;
    else
      flProtect = PAGE_EXECUTE_READ;
  } else if (prot & MMAP_PROT_WRITE)
    flProtect = PAGE_READWRITE;
  else if (prot & MMAP_PROT_READ)
    flProtect = PAGE_READONLY;

  addr = VirtualAlloc((LPVOID)hint, request_size, alloc_flag, flProtect);
  return addr;
}

static void* os_mmap_aligned(void* addr,
                             size_t requested_length,
                             int prot,
                             int flags,
                             size_t alignment,
                             size_t alignment_offset,
                             const char* name)
{
  size_t padded_length = requested_length + alignment + alignment_offset;
  uintptr_t unaligned =
    (uintptr_t)win_mmap(addr, padded_length, prot, flags, MEM_RESERVE);

  if (!unaligned) {
    return (void*)unaligned;
  }

  // Round up the next address that has addr % alignment = 0
  const size_t alignment_corrected = alignment == 0 ? 1 : alignment;
  uintptr_t aligned_nonoffset =
    (unaligned + (alignment_corrected - 1)) & ~(alignment_corrected - 1);

  // Currently offset 0 is aligned according to alignment
  // Alignment needs to be enforced at the given offset
  uintptr_t aligned = 0;
  if ((aligned_nonoffset - alignment_offset) >= unaligned) {
    aligned = aligned_nonoffset - alignment_offset;
  } else {
    aligned = aligned_nonoffset - alignment_offset + alignment;
  }

  if (aligned == unaligned && padded_length == requested_length) {
    return (void*)aligned;
  }

  // Sanity check
  if (aligned < unaligned ||
      (aligned + (requested_length - 1)) > (unaligned + (padded_length - 1)) ||
      (aligned + alignment_offset) % alignment_corrected != 0) {
    os_munmap((void*)unaligned, padded_length);
    return NULL;
  }

  // windows does not support partial unmapping, so unmap and remap
  os_munmap((void*)unaligned, padded_length);
  aligned = (uintptr_t)win_mmap(
    (void*)aligned, requested_length, prot, flags, MEM_RESERVE);
  return (void*)aligned;
}

static void os_munmap(void* addr, size_t size)
{
  DWORD alloc_flag = MEM_RELEASE;
  if (addr) {
    if (VirtualFree(addr, 0, alloc_flag) == 0) {
      size_t page_size = os_getpagesize();
      size_t request_size = (size + page_size - 1) & ~(page_size - 1);
      int64_t curr_err = errno;
      printf("os_munmap error addr:%p, size:0x%zx, errno:%" PRId64 "\n",
             addr,
             request_size,
             curr_err);
    }
  }
}

static int os_mmap_commit(void* curr_heap_end_pointer,
                          size_t expanded_size,
                          int prot)
{
  uintptr_t addr = (uintptr_t)win_mmap(
    curr_heap_end_pointer, expanded_size, prot, MMAP_MAP_NONE, MEM_COMMIT);
  int ret = addr ? 0 : -1;
  return ret;
}

#elif !defined(_WIN32) && (defined(__unix__) || defined(__unix) ||             \
                           (defined(__APPLE__) && defined(__MACH__)))

#  include <sys/mman.h>
#  include <unistd.h>
#  if defined(linux)
#    include <sys/prctl.h>
#  endif

// These constants are copied from <sys/prctl.h>, because the headers
// used for building may not have them even though the running kernel
// supports them.
#  ifndef PR_SET_VMA
#    define PR_SET_VMA 0x53564d41
#  endif
#  ifndef PR_SET_VMA_ANON_NAME
#    define PR_SET_VMA_ANON_NAME 0
#  endif

static size_t os_getpagesize()
{
  return getpagesize();
}

static void* os_mmap(void* hint, size_t size, int prot, int flags)
{
  int map_prot = PROT_NONE;
  int map_flags = MAP_ANONYMOUS | MAP_PRIVATE;
  uint64_t request_size, page_size;
  void* addr;

  page_size = (uint64_t)os_getpagesize();
  request_size = (size + page_size - 1) & ~(page_size - 1);

  if ((size_t)request_size < size)
    /* integer overflow */
    return NULL;

  if (request_size > (uint64_t)0x400000000)
    /* Sanity check: At most 16 G is allowed */
    return NULL;

  if (prot & MMAP_PROT_READ)
    map_prot |= PROT_READ;

  if (prot & MMAP_PROT_WRITE)
    map_prot |= PROT_WRITE;

  if (prot & MMAP_PROT_EXEC)
    map_prot |= PROT_EXEC;

#  if defined(BUILD_TARGET_X86_64) || defined(BUILD_TARGET_AMD_64)
#    ifndef __APPLE__
  if (flags & MMAP_MAP_32BIT)
    map_flags |= MAP_32BIT;
#    endif
#  endif

  if (flags & MMAP_MAP_FIXED)
    map_flags |= MAP_FIXED;

  addr = mmap(hint, request_size, map_prot, map_flags, -1, 0);

  if (addr == MAP_FAILED)
    return NULL;

  return addr;
}

static void* os_mmap_aligned(void* addr,
                             size_t requested_length,
                             int prot,
                             int flags,
                             size_t alignment,
                             size_t alignment_offset,
                             const char* name)
{
  size_t padded_length = requested_length + alignment + alignment_offset;
  uintptr_t unaligned = (uintptr_t)os_mmap(addr, padded_length, prot, flags);

  if (!unaligned) {
    return (void*)unaligned;
  }

  // Round up the next address that has addr % alignment = 0
  const size_t alignment_corrected = alignment == 0 ? 1 : alignment;
  uintptr_t aligned_nonoffset =
    (unaligned + (alignment_corrected - 1)) & ~(alignment_corrected - 1);

  // Currently offset 0 is aligned according to alignment
  // Alignment needs to be enforced at the given offset
  uintptr_t aligned = 0;
  if ((aligned_nonoffset - alignment_offset) >= unaligned) {
    aligned = aligned_nonoffset - alignment_offset;
  } else {
    aligned = aligned_nonoffset - alignment_offset + alignment;
  }

  // Sanity check
  if (aligned < unaligned ||
      (aligned + (requested_length - 1)) > (unaligned + (padded_length - 1)) ||
      (aligned + alignment_offset) % alignment_corrected != 0) {
    os_munmap((void*)unaligned, padded_length);
    return NULL;
  }

  {
    size_t unused_front = aligned - unaligned;
    if (unused_front != 0) {
      os_munmap((void*)unaligned, unused_front);
    }
  }

  {
    size_t unused_back =
      (unaligned + (padded_length - 1)) - (aligned + (requested_length - 1));
    if (unused_back != 0) {
      os_munmap((void*)(aligned + requested_length), unused_back);
    }
  }

#  if defined(linux)
  if (name) {
    // Ignore errors here as this is best effort
    prctl(PR_SET_VMA,
          PR_SET_VMA_ANON_NAME,
          (unsigned long)aligned,
          requested_length,
          (unsigned long)name);
  }
#  endif

  return (void*)aligned;
}

static void os_munmap(void* addr, size_t size)
{
  uint64_t page_size = (uint64_t)os_getpagesize();
  uint64_t request_size = (size + page_size - 1) & ~(page_size - 1);

  if (addr) {
    if (munmap(addr, request_size)) {
      printf("os_munmap error addr:%p, size:0x%" PRIx64 ", errno:%d\n",
             addr,
             request_size,
             errno);
    }
  }
}

static int os_mmap_commit(void* addr, size_t size, int prot)
{
  int map_prot = PROT_NONE;
  uint64_t page_size = (uint64_t)os_getpagesize();
  uint64_t request_size = (size + page_size - 1) & ~(page_size - 1);

  if (!addr)
    return 0;

  if (prot & MMAP_PROT_READ)
    map_prot |= PROT_READ;

  if (prot & MMAP_PROT_WRITE)
    map_prot |= PROT_WRITE;

  if (prot & MMAP_PROT_EXEC)
    map_prot |= PROT_EXEC;

  return mprotect(addr, request_size, map_prot);
}

#else
#  error "Unknown OS"
#endif
