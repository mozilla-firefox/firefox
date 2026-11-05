/* A minimum wasi implementation supporting only stdin, stdout, stderr, argv
 * (upto 1000 args), env (upto 1000 env), and clock functions. */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#  include <windows.h>
#endif

#if defined(__APPLE__) && defined(__MACH__)
// Macs priors to OSX 10.12 don't have the clock functions. So we will use mac
// specific options
#  include <mach/mach_time.h>
#  include <sys/time.h>
#endif

#include "wasm-rt.h"
#include "wasm2c_rt_minwasi.h"

#ifndef WASM_RT_CORE_TYPES_DEFINED
#  define WASM_RT_CORE_TYPES_DEFINED
typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;
typedef int64_t s64;
typedef float f32;
typedef double f64;
#endif

#ifndef UNLIKELY
#  if defined(__GNUC__)
#    define UNLIKELY(x) __builtin_expect(!!(x), 0)
#    define LIKELY(x) __builtin_expect(!!(x), 1)
#  else
#    define UNLIKELY(x) (x)
#    define LIKELY(x) (x)
#  endif
#endif

#define TRAP(x) wasm_rt_trap(WASM_RT_TRAP_##x)

#if WABT_BIG_ENDIAN
#  error "WABT_BIG_ENDIAN is currently not supported"
#endif

#define WASI_MEMACCESS(mem, a) ((void*)&(mem->data[a]))

#define WASI_MEMCHECK_SIZE(mem, a, sz)                                         \
  if (UNLIKELY(((u64)(a)) + sz > mem->size))                                   \
  TRAP(OOB)

#define WASI_CHECK_COPY(mem, a, sz, src)                                       \
  do {                                                                         \
    WASI_MEMCHECK_SIZE(mem, a, sz);                                            \
    memcpy(WASI_MEMACCESS(mem, a), src, sz);                                   \
  } while (0)

#define WASI_MEMCHECK(mem, a, t) WASI_MEMCHECK_SIZE(mem, a, sizeof(t))

#define DEFINE_WASI_LOAD(name, t1, t2, t3)                                     \
  static inline t3 name(wasm_rt_memory_t* mem, u64 addr)                       \
  {                                                                            \
    WASI_MEMCHECK(mem, addr, t1);                                              \
    t1 result;                                                                 \
    memcpy(&result, WASI_MEMACCESS(mem, addr), sizeof(t1));                    \
    return (t3)(t2)result;                                                     \
  }

#define DEFINE_WASI_STORE(name, t1, t2)                                        \
  static inline void name(wasm_rt_memory_t* mem, u64 addr, t2 value)           \
  {                                                                            \
    WASI_MEMCHECK(mem, addr, t1);                                              \
    t1 wrapped = (t1)value;                                                    \
    memcpy(WASI_MEMACCESS(mem, addr), &wrapped, sizeof(t1));                   \
  }

DEFINE_WASI_LOAD(wasm_i32_load, u32, u32, u32);
DEFINE_WASI_STORE(wasm_i32_store, u32, u32);
DEFINE_WASI_STORE(wasm_i64_store, u64, u64);

static bool safe_add_u32(u32* ret, u32 a, u32 b)
{
  if (UINT32_MAX - a < b) {
    *ret = 0;
    return false;
  }
  *ret = a + b;
  return true;
}

// clang-format off

////////////// Supported WASI APIs
//
// Clock operations
// ----------------
// errno_t clock_res_get(void* ctx, clockid_t clock_id, timestamp_t* resolution);
// errno_t clock_time_get(void* ctx, clockid_t clock_id, timestamp_t precision, timestamp_t* time);
//
// File operations
// ----------------
// Only the default descriptors of STDIN, STDOUT, STDERR are allowed by the
// runtime.
//
// errno_t fd_prestat_get(void* ctx, fd_t fd, prestat_t* buf);
// errno_t fd_read(void* ctx, fd_t fd, const iovec_t* iovs, size_t iovs_len, size_t* nread);
// errno_t fd_write(void* ctx, fd_t fd, const ciovec_t* iovs, size_t iovs_len, size_t* nwritten);

////////////// Partially supported WASI APIs
//
// App environment operations
// --------------------------
// These APIs work but return an empty buffer
//
// errno_t args_get(void* ctx, char** argv, char* argv_buf);
// errno_t args_sizes_get(void* ctx, size_t* argc, size_t* argv_buf_size);
// errno_t environ_get(void* ctx, char** environment, char* environ_buf);
// errno_t environ_sizes_get(void* ctx, size_t* environ_count, size_t* environ_buf_size);
//
// Proc exit operation
// -------------------
// This is a no-op here in this runtime as the focus is on library
// sandboxing
//
// errno_t proc_exit(void* ctx, exitcode_t rval);

////////////// Unsupported WASI APIs
// errno_t fd_advise(void* ctx, fd_t fd, filesize_t offset, filesize_t len, advice_t advice);
// errno_t fd_allocate(void* ctx, fd_t fd, filesize_t offset, filesize_t len);
// errno_t fd_close(void* ctx, fd_t fd);
// errno_t fd_datasync(void* ctx, fd_t fd);
// errno_t fd_fdstat_get(void* ctx, fd_t fd, fdstat_t* buf);
// errno_t fd_fdstat_set_flags(void* ctx, fd_t fd, fdflags_t flags);
// errno_t fd_fdstat_set_rights(void* ctx, fd_t fd, rights_t fs_rights_base, rights_t fs_rights_inheriting);
// errno_t fd_filestat_get(void* ctx, fd_t fd, filestat_t* buf);
// errno_t fd_filestat_set_size(void* ctx, fd_t fd, filesize_t st_size);
// errno_t fd_filestat_set_times(void* ctx, fd_t fd, timestamp_t st_atim, timestamp_t st_mtim, fstflags_t fst_flags);
// errno_t fd_pread(void* ctx, fd_t fd, const iovec_t* iovs, size_t iovs_len, filesize_t offset, size_t* nread);
// errno_t fd_prestat_dir_name(void* ctx, fd_t fd, char* path, size_t path_len);
// errno_t fd_pwrite(void* ctx, fd_t fd, const ciovec_t* iovs, size_t iovs_len, filesize_t offset, size_t* nwritten);
// errno_t fd_readdir(void* ctx, fd_t fd, void* buf, size_t buf_len, dircookie_t cookie, size_t* bufused);
// errno_t fd_renumber(void* ctx, fd_t from, fd_t to);
// errno_t fd_seek(void* ctx, fd_t fd, filedelta_t offset, whence_t whence, filesize_t* newoffset);
// errno_t fd_sync(void* ctx, fd_t fd);
// errno_t fd_tell(void* ctx, fd_t fd, filesize_t* offset);
// errno_t path_create_directory(void* ctx, fd_t fd, const char* path, size_t path_len);
// errno_t path_filestat_get(void* ctx, fd_t fd, lookupflags_t flags, const char* path, size_t path_len, filestat_t* buf);
// errno_t path_filestat_set_times(void* ctx, fd_t fd, lookupflags_t flags, const char* path, size_t path_len, timestamp_t st_atim, timestamp_t st_mtim, fstflags_t fst_flags);
// errno_t path_link(void* ctx, fd_t old_fd, lookupflags_t old_flags, const char* old_path, size_t old_path_len, fd_t new_fd, const char* new_path, size_t new_path_len);
// errno_t path_open(void* ctx, fd_t dirfd, lookupflags_t dirflags, const char* path, size_t path_len, oflags_t o_flags, rights_t fs_rights_base, rights_t fs_rights_inheriting, fdflags_t fs_flags, fd_t* fd);
// errno_t path_readlink(void* ctx, fd_t fd, const char* path, size_t path_len, char* buf, size_t buf_len, size_t* bufused);
// errno_t path_remove_directory(void* ctx, fd_t fd, const char* path, size_t path_len);
// errno_t path_rename(void* ctx, fd_t old_fd, const char* old_path, size_t old_path_len, fd_t new_fd, const char* new_path, size_t new_path_len);
// errno_t path_symlink(void* ctx, const char* old_path, size_t old_path_len, fd_t fd, const char* new_path, size_t new_path_len);
// errno_t path_unlink_file(void* ctx, fd_t fd, const char* path, size_t path_len);
// errno_t poll_oneoff(void* ctx, const subscription_t* in, event_t* out, size_t nsubscriptions, size_t* nevents);
// errno_t proc_raise(void* ctx, signal_t sig);
// errno_t random_get(void* ctx, void* buf, size_t buf_len);
// errno_t sched_yield(t* uvwasi);
// errno_t sock_accept(void* ctx, fd_t sock, flags_t fdflags, fd* fd);
// errno_t sock_recv(void* ctx, fd_t sock, const iovec_t* ri_data, size_t ri_data_len, riflags_t ri_flags, size_t* ro_datalen, roflags_t* ro_flags);
// errno_t sock_send(void* ctx, fd_t sock, const ciovec_t* si_data, size_t si_data_len, siflags_t si_flags, size_t* so_datalen);
// errno_t sock_shutdown(void* ctx, fd_t sock, sdflags_t how);

// clang-format on

// Success
#define WASI_SUCCESS 0
// Bad file descriptor.
#define WASI_BADF_ERROR 8
// Invalid argument
#define WASI_INVAL_ERROR 28
// Operation not permitted.
#define WASI_PERM_ERROR 63
// Syscall not implemented
#define WASI_NOSYS_ERROR 53

#define WASI_RET_ERR_ON_FAIL(exp)                                              \
  if (!(exp)) {                                                                \
    return WASI_INVAL_ERROR;                                                   \
  }

/////////////////////////////////////////////////////////////
// Clock operations
/////////////////////////////////////////////////////////////

#define WASM_CLOCK_REALTIME 0
#define WASM_CLOCK_MONOTONIC 1
#define WASM_CLOCK_PROCESS_CPUTIME 2
#define WASM_CLOCK_THREAD_CPUTIME_ID 3

#if defined(_WIN32)

typedef struct
{
  LARGE_INTEGER counts_per_sec; /* conversion factor */
} wasi_win_clock_info_t;

static wasi_win_clock_info_t g_wasi_win_clock_info;
static int g_os_data_initialized = 0;

static bool os_clock_init()
{
  // From here:
  // https://stackoverflow.com/questions/5404277/porting-clock-gettime-to-windows/38212960#38212960
  if (QueryPerformanceFrequency(&g_wasi_win_clock_info.counts_per_sec) == 0) {
    return false;
  }
  g_os_data_initialized = 1;
  return true;
}

static bool os_clock_init_instance(void** clock_data_pointer)
{
  if (!g_os_data_initialized) {
    os_clock_init();
  }

  wasi_win_clock_info_t* alloc =
    (wasi_win_clock_info_t*)malloc(sizeof(wasi_win_clock_info_t));
  if (!alloc) {
    return false;
  }
  memcpy(alloc, &g_wasi_win_clock_info, sizeof(wasi_win_clock_info_t));
  *clock_data_pointer = alloc;
  return true;
}

static void os_clock_cleanup_instance(void** clock_data_pointer)
{
  if (*clock_data_pointer == 0) {
    free(*clock_data_pointer);
    *clock_data_pointer = 0;
  }
}

static int os_clock_gettime(void* clock_data,
                            int clock_id,
                            struct timespec* out_struct)
{
  wasi_win_clock_info_t* alloc = (wasi_win_clock_info_t*)clock_data;

  LARGE_INTEGER count;
  (void)clock_id;

  if (alloc->counts_per_sec.QuadPart <= 0 ||
      QueryPerformanceCounter(&count) == 0) {
    return -1;
  }

#  define BILLION 1000000000LL
  out_struct->tv_sec = count.QuadPart / alloc->counts_per_sec.QuadPart;
  out_struct->tv_nsec =
    ((count.QuadPart % alloc->counts_per_sec.QuadPart) * BILLION) /
    alloc->counts_per_sec.QuadPart;
#  undef BILLION

  return 0;
}

static int os_clock_getres(void* clock_data,
                           int clock_id,
                           struct timespec* out_struct)
{
  (void)clock_id;
  out_struct->tv_sec = 0;
  out_struct->tv_nsec = 1000;
  return 0;
}

#elif defined(__APPLE__) && defined(__MACH__)

typedef struct
{
  mach_timebase_info_data_t timebase; /* numer = 0, denom = 0 */
  struct timespec inittime; /* nanoseconds since 1-Jan-1970 to init() */
  uint64_t initclock;       /* ticks since boot to init() */
} wasi_mac_clock_info_t;

static wasi_mac_clock_info_t g_wasi_mac_clock_info;
static int g_os_data_initialized = 0;

static bool os_clock_init()
{
  // From here:
  // https://stackoverflow.com/questions/5167269/clock-gettime-alternative-in-mac-os-x/21352348#21352348
  if (mach_timebase_info(&g_wasi_mac_clock_info.timebase) != 0) {
    return false;
  }

  // microseconds since 1 Jan 1970
  struct timeval micro;
  if (gettimeofday(&micro, NULL) != 0) {
    return false;
  }

  g_wasi_mac_clock_info.initclock = mach_absolute_time();

  g_wasi_mac_clock_info.inittime.tv_sec = micro.tv_sec;
  g_wasi_mac_clock_info.inittime.tv_nsec = micro.tv_usec * 1000;

  g_os_data_initialized = 1;
  return true;
}

static bool os_clock_init_instance(void** clock_data_pointer)
{
  if (!g_os_data_initialized) {
    os_clock_init();
  }

  wasi_mac_clock_info_t* alloc =
    (wasi_mac_clock_info_t*)malloc(sizeof(wasi_mac_clock_info_t));
  if (!alloc) {
    return false;
  }
  memcpy(alloc, &g_wasi_mac_clock_info, sizeof(wasi_mac_clock_info_t));
  *clock_data_pointer = alloc;
  return true;
}

static void os_clock_cleanup_instance(void** clock_data_pointer)
{
  if (*clock_data_pointer == 0) {
    free(*clock_data_pointer);
    *clock_data_pointer = 0;
  }
}

static int os_clock_gettime(void* clock_data,
                            int clock_id,
                            struct timespec* out_struct)
{
  int ret = 0;
  wasi_mac_clock_info_t* alloc = (wasi_mac_clock_info_t*)clock_data;

  // From here:
  // https://stackoverflow.com/questions/5167269/clock-gettime-alternative-in-mac-os-x/21352348#21352348

  (void)clock_id;
  // ticks since init
  uint64_t clock = mach_absolute_time() - alloc->initclock;
  // nanoseconds since init
  uint64_t nano = clock * (uint64_t)(alloc->timebase.numer) /
                  (uint64_t)(alloc->timebase.denom);
  *out_struct = alloc->inittime;

#  define BILLION 1000000000L
  out_struct->tv_sec += nano / BILLION;
  out_struct->tv_nsec += nano % BILLION;
  // normalize
  out_struct->tv_sec += out_struct->tv_nsec / BILLION;
  out_struct->tv_nsec = out_struct->tv_nsec % BILLION;
#  undef BILLION
  return ret;
}

static int os_clock_getres(void* clock_data,
                           int clock_id,
                           struct timespec* out_struct)
{
  int ret = 0;
  (void)clock_id;
  out_struct->tv_sec = 0;
  out_struct->tv_nsec = 1;
  return ret;
}

#else

static bool os_clock_init()
{
  return true;
}

static bool os_clock_init_instance(void** clock_data_pointer)
{
  (void)clock_data_pointer;
  return true;
}

static void os_clock_cleanup_instance(void** clock_data_pointer)
{
  (void)clock_data_pointer;
}

static int os_translate_clock_id(int wasm_clock_id)
{
  // Some OSes like OpenBSD use different clock ids, so wasm's clock id should
  // be explicitly translated
  switch (wasm_clock_id) {
    case WASM_CLOCK_REALTIME:
      return CLOCK_REALTIME;
    case WASM_CLOCK_MONOTONIC:
      return CLOCK_MONOTONIC;
    case WASM_CLOCK_PROCESS_CPUTIME:
      return CLOCK_PROCESS_CPUTIME_ID;
    case WASM_CLOCK_THREAD_CPUTIME_ID:
      return CLOCK_THREAD_CPUTIME_ID;
  }
  return CLOCK_REALTIME;
}

static int os_clock_gettime(void* clock_data,
                            int clock_id,
                            struct timespec* out_struct)
{
  (void)clock_data;
  int ret = clock_gettime(os_translate_clock_id(clock_id), out_struct);
  return ret;
}

static int os_clock_getres(void* clock_data,
                           int clock_id,
                           struct timespec* out_struct)
{
  (void)clock_data;
  int ret = clock_getres(os_translate_clock_id(clock_id), out_struct);
  return ret;
}

#endif

static int check_clock(u32 clock_id)
{
  return clock_id == WASM_CLOCK_REALTIME || clock_id == WASM_CLOCK_MONOTONIC ||
         clock_id == WASM_CLOCK_PROCESS_CPUTIME ||
         clock_id == WASM_CLOCK_THREAD_CPUTIME_ID;
}

// out is a pointer to a u64 timestamp in nanoseconds
// https://github.com/WebAssembly/WASI/blob/main/phases/snapshot/docs.md#-timestamp-u64
u32 w2c_wasi__snapshot__preview1_clock_time_get(
  w2c_wasi__snapshot__preview1* wasi_data,
  u32 clock_id,
  u64 precision,
  u32 out)
{
  if (!check_clock(clock_id)) {
    return WASI_INVAL_ERROR;
  }

  struct timespec out_struct;
  int ret = os_clock_gettime(wasi_data->clock_data, clock_id, &out_struct);
  u64 result =
    ((u64)out_struct.tv_sec) * 1000 * 1000 * 1000 + ((u64)out_struct.tv_nsec);
  wasm_i64_store(wasi_data->instance_memory, out, result);
  return ret;
}

u32 w2c_wasi__snapshot__preview1_clock_res_get(
  w2c_wasi__snapshot__preview1* wasi_data,
  u32 clock_id,
  u32 out)
{
  if (!check_clock(clock_id)) {
    return WASI_INVAL_ERROR;
  }

  struct timespec out_struct;
  int ret = os_clock_getres(wasi_data->clock_data, clock_id, &out_struct);
  u64 result =
    ((u64)out_struct.tv_sec) * 1000 * 1000 * 1000 + ((u64)out_struct.tv_nsec);
  wasm_i64_store(wasi_data->instance_memory, out, result);
  return ret;
}

/////////////////////////////////////////////////////////////
////////// File operations
/////////////////////////////////////////////////////////////

// Only allow stdin (0), stdout (1), stderr(2)

#define WASM_STDIN 0
#define WASM_STDOUT 1
#define WASM_STDERR 2

u32 w2c_wasi__snapshot__preview1_fd_prestat_get(
  w2c_wasi__snapshot__preview1* wasi_data,
  u32 fd,
  u32 prestat)
{
  if (fd == WASM_STDIN || fd == WASM_STDOUT || fd == WASM_STDERR) {
    return WASI_PERM_ERROR;
  }
  return WASI_BADF_ERROR;
}

u32 w2c_wasi__snapshot__preview1_fd_write(
  w2c_wasi__snapshot__preview1* wasi_data,
  u32 fd,
  u32 iov,
  u32 iovcnt,
  u32 pnum)
{
  if (fd != WASM_STDOUT && fd != WASM_STDERR) {
    return WASI_BADF_ERROR;
  }

  u32 num = 0;
  for (u32 i = 0; i < iovcnt; i++) {
    u32 ptr = wasm_i32_load(wasi_data->instance_memory, iov + i * 8);
    u32 len = wasm_i32_load(wasi_data->instance_memory, iov + i * 8 + 4);

    WASI_MEMCHECK_SIZE(wasi_data->instance_memory, ptr, len);

    size_t result = fwrite(WASI_MEMACCESS(wasi_data->instance_memory, ptr),
                           1 /* size */,
                           len /* n */,
                           fd == WASM_STDOUT ? stdout : stderr);

    // Guaranteed by fwrite
    assert(result <= len);

    WASI_RET_ERR_ON_FAIL(safe_add_u32(&num, num, (u32)result));

    if (((u32)result) != len) {
      wasm_i32_store(wasi_data->instance_memory, pnum, num);
      return WASI_PERM_ERROR;
    }
  }

  wasm_i32_store(wasi_data->instance_memory, pnum, num);
  return WASI_SUCCESS;
}

u32 w2c_wasi__snapshot__preview1_fd_read(
  w2c_wasi__snapshot__preview1* wasi_data,
  u32 fd,
  u32 iov,
  u32 iovcnt,
  u32 pnum)
{
  if (fd != WASM_STDIN) {
    return WASI_BADF_ERROR;
  }

  u32 num = 0;
  for (u32 i = 0; i < iovcnt; i++) {
    u32 ptr = wasm_i32_load(wasi_data->instance_memory, iov + i * 8);
    u32 len = wasm_i32_load(wasi_data->instance_memory, iov + i * 8 + 4);

    WASI_MEMCHECK_SIZE(wasi_data->instance_memory, ptr, len);
    size_t result = fread(WASI_MEMACCESS(wasi_data->instance_memory, ptr),
                          1 /* size */,
                          len /* n */,
                          stdin);

    // Guaranteed by fwrite
    assert(result <= len);

    WASI_RET_ERR_ON_FAIL(safe_add_u32(&num, num, (u32)result));

    if (((u32)result) != len) {
      break; // nothing more to read
    }
  }
  wasm_i32_store(wasi_data->instance_memory, pnum, num);
  return WASI_SUCCESS;
}

/////////////////////////////////////////////////////////////
// App environment operations
/////////////////////////////////////////////////////////////

#define ARGV_AND_ENV_LIMIT 1000

static u32 strings_sizes_get(wasm_rt_memory_t* instance_memory,
                             const char* name,
                             u32 p_str_count,
                             u32 p_str_buff_size,
                             u32 string_count,
                             const char** strings)
{
  u32 chosen_count = string_count;
  if (chosen_count > ARGV_AND_ENV_LIMIT) {
    chosen_count = ARGV_AND_ENV_LIMIT;
    printf("Truncated %s args to %d\n", name, ARGV_AND_ENV_LIMIT);
  }

  u32 curr_buf_size = 0;
  for (u32 i = 0; i < chosen_count; i++) {
    size_t original_len = strlen(strings[i]);
    // len has to be at most u32 - 1
    WASI_RET_ERR_ON_FAIL(original_len < (size_t)UINT32_MAX);

    u32 len = (u32)original_len;
    u32 len_plus_nullchar = len + 1;

    WASI_RET_ERR_ON_FAIL(
      safe_add_u32(&curr_buf_size, curr_buf_size, len_plus_nullchar));
  }

  wasm_i32_store(instance_memory, p_str_count, chosen_count);
  wasm_i32_store(instance_memory, p_str_buff_size, curr_buf_size);
  return WASI_SUCCESS;
}

static u32 strings_get(wasm_rt_memory_t* instance_memory,
                       const char* name,
                       u32 p_str_arr,
                       u32 p_str_buf,
                       u32 string_count,
                       const char** strings)
{
  u32 chosen_count = string_count;
  if (chosen_count > ARGV_AND_ENV_LIMIT) {
    chosen_count = ARGV_AND_ENV_LIMIT;
    // Warning is already printed in get_size
  }

  u32 curr_buf_loc = 0;

  for (u32 i = 0; i < chosen_count; i++) {
    // Implement: p_str_arr[i] = p_str_buf[curr_buf_loc]
    u32 target_argv_i_ref;
    WASI_RET_ERR_ON_FAIL(safe_add_u32(&target_argv_i_ref, p_str_arr, i * 4));

    u32 target_buf_curr_ref;
    WASI_RET_ERR_ON_FAIL(
      safe_add_u32(&target_buf_curr_ref, p_str_buf, curr_buf_loc));

    wasm_i32_store(instance_memory, target_argv_i_ref, target_buf_curr_ref);

    // Implement: strcpy(p_str_buf[curr_buf_loc], strings[i]);
    size_t original_len = strlen(strings[i]);
    // len has to be at most u32 - 1
    WASI_RET_ERR_ON_FAIL(original_len < (size_t)UINT32_MAX);

    u32 len = (u32)original_len;
    u32 len_plus_nullchar = len + 1;

    WASI_CHECK_COPY(
      instance_memory, target_buf_curr_ref, len_plus_nullchar, strings[i]);
    // Implement: curr_buf_loc += strlen(p_str_buf[curr_buf_loc])
    WASI_RET_ERR_ON_FAIL(
      safe_add_u32(&curr_buf_loc, curr_buf_loc, len_plus_nullchar));
  }
  return WASI_SUCCESS;
}

u32 w2c_wasi__snapshot__preview1_args_sizes_get(
  w2c_wasi__snapshot__preview1* wasi_data,
  u32 p_argc,
  u32 p_argv_buf_size)
{
  return strings_sizes_get(wasi_data->instance_memory,
                           "main",
                           p_argc,
                           p_argv_buf_size,
                           wasi_data->main_argc,
                           wasi_data->main_argv);
}

u32 w2c_wasi__snapshot__preview1_args_get(
  w2c_wasi__snapshot__preview1* wasi_data,
  u32 p_argv,
  u32 p_argv_buf)
{
  return strings_get(wasi_data->instance_memory,
                     "main",
                     p_argv,
                     p_argv_buf,
                     wasi_data->main_argc,
                     wasi_data->main_argv);
}

u32 w2c_wasi__snapshot__preview1_environ_sizes_get(
  w2c_wasi__snapshot__preview1* wasi_data,
  u32 p_env_count,
  u32 p_env_buf_size)
{
  return strings_sizes_get(wasi_data->instance_memory,
                           "env",
                           p_env_count,
                           p_env_buf_size,
                           wasi_data->env_count,
                           wasi_data->env);
}

u32 w2c_wasi__snapshot__preview1_environ_get(
  w2c_wasi__snapshot__preview1* wasi_data,
  u32 p_env,
  u32 p_env_buf)
{
  return strings_get(wasi_data->instance_memory,
                     "env",
                     p_env,
                     p_env_buf,
                     wasi_data->env_count,
                     wasi_data->env);
}

/////////////////////////////////////////////////////////////
// Proc exit operation
/////////////////////////////////////////////////////////////

void w2c_wasi__snapshot__preview1_proc_exit(
  w2c_wasi__snapshot__preview1* wasi_data,
  u32 x)
{
#ifdef WASM2C_WASI_TRAP_ON_EXIT
  TRAP(WASI);
#else
  exit(x);
#endif
}

/////////////////////////////////////////////////////////////
////////////// Unsupported WASI APIs
/////////////////////////////////////////////////////////////

#define STUB_IMPORT_IMPL(ret, name, params)                                    \
  ret name params                                                              \
  {                                                                            \
    return WASI_NOSYS_ERROR;                                                   \
  }

// clang-format off

STUB_IMPORT_IMPL(u32, w2c_wasi__snapshot__preview1_fd_advise,
                 (w2c_wasi__snapshot__preview1* wasi_data, u32 a, u64 b, u64 c, u32 d));
STUB_IMPORT_IMPL(u32, w2c_wasi__snapshot__preview1_fd_allocate,
                 (w2c_wasi__snapshot__preview1* wasi_data, u32 a, u64 b, u64 c));
STUB_IMPORT_IMPL(u32, w2c_wasi__snapshot__preview1_fd_close,
                 (w2c_wasi__snapshot__preview1* wasi_data, u32 fd));
STUB_IMPORT_IMPL(u32, w2c_wasi__snapshot__preview1_fd_datasync,
                 (w2c_wasi__snapshot__preview1* wasi_data, u32 a));
STUB_IMPORT_IMPL(u32, w2c_wasi__snapshot__preview1_fd_fdstat_get,
                 (w2c_wasi__snapshot__preview1* wasi_data, u32 a, u32 b));
STUB_IMPORT_IMPL(u32, w2c_wasi__snapshot__preview1_fd_fdstat_set_flags,
                 (w2c_wasi__snapshot__preview1* wasi_data, u32 a, u32 b));
STUB_IMPORT_IMPL(u32, w2c_wasi__snapshot__preview1_fd_fdstat_set_rights,
                 (w2c_wasi__snapshot__preview1* wasi_data, u32 a, u64 b, u64 c));
STUB_IMPORT_IMPL(u32, w2c_wasi__snapshot__preview1_fd_filestat_get,
                 (w2c_wasi__snapshot__preview1* wasi_data, u32 a, u32 b));
STUB_IMPORT_IMPL(u32, w2c_wasi__snapshot__preview1_fd_filestat_set_size,
                 (w2c_wasi__snapshot__preview1* wasi_data, u32 a, u64 b));
STUB_IMPORT_IMPL(u32, w2c_wasi__snapshot__preview1_fd_filestat_set_times,
                 (w2c_wasi__snapshot__preview1* wasi_data, u32 a, u64 b, u64 c, u32 d));
STUB_IMPORT_IMPL(u32, w2c_wasi__snapshot__preview1_fd_pread,
                 (w2c_wasi__snapshot__preview1* wasi_data, u32 a, u32 b, u32 c, u64 d, u32 e));
STUB_IMPORT_IMPL(u32, w2c_wasi__snapshot__preview1_fd_prestat_dir_name,
                 (w2c_wasi__snapshot__preview1* wasi_data, u32 a, u32 b, u32 c));
STUB_IMPORT_IMPL(u32, w2c_wasi__snapshot__preview1_fd_pwrite,
                 (w2c_wasi__snapshot__preview1* wasi_data, u32 a, u32 b, u32 c, u64 d, u32 e));
STUB_IMPORT_IMPL(u32, w2c_wasi__snapshot__preview1_fd_readdir,
                 (w2c_wasi__snapshot__preview1* wasi_data, u32 a, u32 b, u32 c, u64 d, u32 e));
STUB_IMPORT_IMPL(u32, w2c_wasi__snapshot__preview1_fd_renumber,
                 (w2c_wasi__snapshot__preview1* wasi_data, u32 a, u32 b));
STUB_IMPORT_IMPL(u32, w2c_wasi__snapshot__preview1_fd_seek,
                 (w2c_wasi__snapshot__preview1* wasi_data, u32 fd, u64 offset, u32 whence, u32 new_offset));
STUB_IMPORT_IMPL(u32, w2c_wasi__snapshot__preview1_fd_sync,
                 (w2c_wasi__snapshot__preview1* wasi_data, u32 a));
STUB_IMPORT_IMPL(u32, w2c_wasi__snapshot__preview1_fd_tell,
                 (w2c_wasi__snapshot__preview1* wasi_data, u32 a, u32 b));
STUB_IMPORT_IMPL(u32, w2c_wasi__snapshot__preview1_path_create_directory,
                 (w2c_wasi__snapshot__preview1* wasi_data, u32 a, u32 b, u32 c));
STUB_IMPORT_IMPL(u32, w2c_wasi__snapshot__preview1_path_filestat_get,
                 (w2c_wasi__snapshot__preview1* wasi_data, u32 a, u32 b, u32 c, u32 d, u32 e));
STUB_IMPORT_IMPL(u32, w2c_wasi__snapshot__preview1_path_filestat_set_times,
                 (w2c_wasi__snapshot__preview1* wasi_data, u32 a, u32 b, u32 c, u32 d, u64 e, u64 f, u32 g));
STUB_IMPORT_IMPL(u32, w2c_wasi__snapshot__preview1_path_link,
                 (w2c_wasi__snapshot__preview1* wasi_data, u32 a, u32 b, u32 c, u32 d, u32 e, u32 f, u32 g));
STUB_IMPORT_IMPL(u32, w2c_wasi__snapshot__preview1_path_open,
                 (w2c_wasi__snapshot__preview1* wasi_data, u32 a, u32 b, u32 c, u32 d, u32 e, u64 f, u64 g, u32 h, u32 i));
STUB_IMPORT_IMPL(u32, w2c_wasi__snapshot__preview1_path_readlink,
                 (w2c_wasi__snapshot__preview1* wasi_data, u32 a, u32 b, u32 c, u32 d, u32 e, u32 f));
STUB_IMPORT_IMPL(u32, w2c_wasi__snapshot__preview1_path_remove_directory,
                 (w2c_wasi__snapshot__preview1* wasi_data, u32 a, u32 b, u32 c));
STUB_IMPORT_IMPL(u32, w2c_wasi__snapshot__preview1_path_rename,
                 (w2c_wasi__snapshot__preview1* wasi_data, u32 a, u32 b, u32 c, u32 d, u32 e, u32 f));
STUB_IMPORT_IMPL(u32, w2c_wasi__snapshot__preview1_path_symlink,
                 (w2c_wasi__snapshot__preview1* wasi_data, u32 a, u32 b, u32 c, u32 d, u32 e));
STUB_IMPORT_IMPL(u32, w2c_wasi__snapshot__preview1_path_unlink_file,
                 (w2c_wasi__snapshot__preview1* wasi_data, u32 a, u32 b, u32 c));
STUB_IMPORT_IMPL(u32, w2c_wasi__snapshot__preview1_poll_oneoff,
                 (w2c_wasi__snapshot__preview1* wasi_data, u32 a, u32 b, u32 c, u32 d));
STUB_IMPORT_IMPL(u32, w2c_wasi__snapshot__preview1_proc_raise,
                 (w2c_wasi__snapshot__preview1* wasi_data, u32 a));
STUB_IMPORT_IMPL(u32, w2c_wasi__snapshot__preview1_random_get,
                 (w2c_wasi__snapshot__preview1* wasi_data, u32 a, u32 b));
STUB_IMPORT_IMPL(u32, w2c_wasi__snapshot__preview1_sched_yield,
                 (w2c_wasi__snapshot__preview1* wasi_data));
STUB_IMPORT_IMPL(u32, w2c_wasi__snapshot__preview1_sock_accept,
                 (w2c_wasi__snapshot__preview1* wasi_data, u32 a, u32 b, u32 c));
STUB_IMPORT_IMPL(u32, w2c_wasi__snapshot__preview1_sock_recv,
                 (w2c_wasi__snapshot__preview1* wasi_data, u32 a, u32 b, u32 c, u32 d, u32 e, u32 f));
STUB_IMPORT_IMPL(u32, w2c_wasi__snapshot__preview1_sock_send,
                 (w2c_wasi__snapshot__preview1* wasi_data, u32 a, u32 b, u32 c, u32 d, u32 e));
STUB_IMPORT_IMPL(u32, w2c_wasi__snapshot__preview1_sock_shutdown,
                 (w2c_wasi__snapshot__preview1* wasi_data, u32 a, u32 b));

// clang-format on

/////////////////////////////////////////////////////////////
////////// Misc
/////////////////////////////////////////////////////////////

bool minwasi_init()
{
  return os_clock_init();
}

bool minwasi_init_instance(w2c_wasi__snapshot__preview1* wasi_data)
{
  return os_clock_init_instance(&(wasi_data->clock_data));
}

void minwasi_cleanup_instance(w2c_wasi__snapshot__preview1* wasi_data)
{
  os_clock_cleanup_instance(&(wasi_data->clock_data));
}
