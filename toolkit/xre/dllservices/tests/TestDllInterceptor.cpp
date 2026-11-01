/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <shlobj.h>
#include <stdio.h>
#include <commdlg.h>
#define SECURITY_WIN32
#include <security.h>
#include <wininet.h>
#include <schnlsp.h>
#include <winternl.h>
#include <processthreadsapi.h>

#include <oleauto.h>
#pragma comment(lib, "oleaut32.lib")

#include "AssemblyPayloads.h"
#include "mozilla/Attributes.h"
#include "mozilla/DynamicallyLinkedFunctionPtr.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/WindowsProcessMitigations.h"
#include "nsWindowsDllInterceptor.h"
#include "nsWindowsHelpers.h"

NTSTATUS NTAPI NtFlushBuffersFile(HANDLE, PIO_STATUS_BLOCK);
NTSTATUS NTAPI NtReadFile(HANDLE, HANDLE, PIO_APC_ROUTINE, PVOID,
                          PIO_STATUS_BLOCK, PVOID, ULONG, PLARGE_INTEGER,
                          PULONG);
NTSTATUS NTAPI NtReadFileScatter(HANDLE, HANDLE, PIO_APC_ROUTINE, PVOID,
                                 PIO_STATUS_BLOCK, PFILE_SEGMENT_ELEMENT, ULONG,
                                 PLARGE_INTEGER, PULONG);
NTSTATUS NTAPI NtWriteFile(HANDLE, HANDLE, PIO_APC_ROUTINE, PVOID,
                           PIO_STATUS_BLOCK, PVOID, ULONG, PLARGE_INTEGER,
                           PULONG);
NTSTATUS NTAPI NtWriteFileGather(HANDLE, HANDLE, PIO_APC_ROUTINE, PVOID,
                                 PIO_STATUS_BLOCK, PFILE_SEGMENT_ELEMENT, ULONG,
                                 PLARGE_INTEGER, PULONG);
NTSTATUS NTAPI NtQueryFullAttributesFile(POBJECT_ATTRIBUTES, PVOID);
NTSTATUS NTAPI LdrLoadDll(PWCHAR filePath, PULONG flags,
                          PUNICODE_STRING moduleFileName, PHANDLE handle);
NTSTATUS NTAPI LdrUnloadDll(HMODULE);

NTSTATUS NTAPI NtMapViewOfSection(
    HANDLE aSection, HANDLE aProcess, PVOID* aBaseAddress, ULONG_PTR aZeroBits,
    SIZE_T aCommitSize, PLARGE_INTEGER aSectionOffset, PSIZE_T aViewSize,
    SECTION_INHERIT aInheritDisposition, ULONG aAllocationType,
    ULONG aProtectionFlags);

// These pointers are disguised as PVOID to avoid pulling in obscure headers
PVOID NTAPI LdrResolveDelayLoadedAPI(PVOID, PVOID, PVOID, PVOID, PVOID, ULONG);
void CALLBACK ProcessCaretEvents(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD,
                                 DWORD);
void __fastcall BaseThreadInitThunk(BOOL aIsInitialThread, void* aStartAddress,
                                    void* aThreadParam);

BOOL WINAPI ApiSetQueryApiSetPresence(PCUNICODE_STRING, PBOOLEAN);

#define RtlGenRandom SystemFunction036
extern "C" BOOLEAN NTAPI RtlGenRandom(PVOID aRandomBuffer,
                                      ULONG aRandomBufferLength);

extern "C" uintptr_t WINAPI TF_Notify(UINT uMsg, WPARAM wParam, LPARAM lParam);

using namespace mozilla;

struct payload {
  UINT64 a;
  UINT64 b;
  UINT64 c;

  bool operator==(const payload& other) const {
    return (a == other.a && b == other.b && c == other.c);
  }
};

extern "C" MOZ_NEVER_INLINE MOZ_NOPROFILE MOZ_NOINSTRUMENT
    __declspec(dllexport) payload
    rotatePayload(payload p) {
  UINT64 tmp = p.a;
  p.a = p.b;
  p.b = p.c;
  p.c = tmp;
  return p;
}

// payloadNotHooked is a target function for a test to expect a negative result.
// We cannot use rotatePayload for that purpose because our detour cannot hook
// a function detoured already.  Please keep this function always unhooked.
extern "C" MOZ_NEVER_INLINE MOZ_NOPROFILE MOZ_NOINSTRUMENT
    __declspec(dllexport) payload
    payloadNotHooked(payload p) {
  // Do something different from rotatePayload to avoid ICF.
  p.a ^= p.b;
  p.b ^= p.c;
  p.c ^= p.a;
  return p;
}

// Declared as volatile to prevent optimizers from incorrectly eliding accesses
// to it. (See bug 1769001 for a motivating example.)
static volatile bool patched_func_called = false;

static WindowsDllInterceptor::FuncHookType<decltype(&rotatePayload)>
    orig_rotatePayload;

static WindowsDllInterceptor::FuncHookType<decltype(&payloadNotHooked)>
    orig_payloadNotHooked;

static payload patched_rotatePayload(payload p) {
  patched_func_called = true;
  return orig_rotatePayload(p);
}

// Invoke aFunc by taking aArg's contents and using them as aFunc's arguments
template <typename OrigFuncT, typename... Args,
          typename ArgTuple = std::tuple<Args...>, size_t... Indices>
decltype(auto) Apply(OrigFuncT& aFunc, ArgTuple&& aArgs,
                     std::index_sequence<Indices...>) {
  return std::apply(aFunc, aArgs);
}

#define DEFINE_TEST_FUNCTION(calling_convention)                               \
  template <typename R, typename... Args, typename... TestArgs>                \
  bool TestFunction(R(calling_convention* aFunc)(Args...), bool (*aPred)(R),   \
                    TestArgs&&... aArgs) {                                     \
    using ArgTuple = std::tuple<Args...>;                                      \
    using Indices = std::index_sequence_for<Args...>;                          \
    ArgTuple fakeArgs{std::forward<TestArgs>(aArgs)...};                       \
    patched_func_called = false;                                               \
    return aPred(Apply(aFunc, std::forward<ArgTuple>(fakeArgs), Indices())) && \
           patched_func_called;                                                \
  }                                                                            \
                                                                               \
  /* Specialization for functions returning void */                            \
  template <typename PredT, typename... Args, typename... TestArgs>            \
  bool TestFunction(void(calling_convention * aFunc)(Args...), PredT,          \
                    TestArgs&&... aArgs) {                                     \
    using ArgTuple = std::tuple<Args...>;                                      \
    using Indices = std::index_sequence_for<Args...>;                          \
    ArgTuple fakeArgs{std::forward<TestArgs>(aArgs)...};                       \
    patched_func_called = false;                                               \
    Apply(aFunc, std::forward<ArgTuple>(fakeArgs), Indices());                 \
    return patched_func_called;                                                \
  }

// C++11 allows empty arguments to macros. clang works just fine. MSVC does the
// right thing, but it also throws up warning C4003.
#if defined(_MSC_VER) && !defined(__clang__)
DEFINE_TEST_FUNCTION(__cdecl)
#else
DEFINE_TEST_FUNCTION()
#endif

#ifdef _M_IX86
DEFINE_TEST_FUNCTION(__stdcall)
DEFINE_TEST_FUNCTION(__fastcall)
#endif  // _M_IX86

// Test the hooked function against the supplied predicate
template <typename OrigFuncT, typename PredicateT, typename... Args>
bool CheckHook(OrigFuncT& aOrigFunc, const char* aDllName,
               const char* aFuncName, PredicateT&& aPred, Args&&... aArgs) {
  if (TestFunction(aOrigFunc, std::forward<PredicateT>(aPred),
                   std::forward<Args>(aArgs)...)) {
    printf(
        "TEST-PASS | WindowsDllInterceptor | "
        "Executed hooked function %s from %s\n",
        aFuncName, aDllName);
    fflush(stdout);
    return true;
  }
  printf(
      "TEST-FAILED | WindowsDllInterceptor | "
      "Failed to execute hooked function %s from %s\n",
      aFuncName, aDllName);
  return false;
}

struct InterceptorFunction {
  static const size_t EXEC_MEMBLOCK_SIZE = 64 * 1024;  // 64K

  static InterceptorFunction& Create() {
    // Make sure the executable memory is allocated
    if (!sBlock) {
      Init();
    }
    MOZ_ASSERT(sBlock);

    // Make sure we aren't making more functions than we allocated room for
    MOZ_RELEASE_ASSERT((sNumInstances + 1) * sizeof(InterceptorFunction) <=
                       EXEC_MEMBLOCK_SIZE);

    // Grab the next InterceptorFunction from executable memory
    InterceptorFunction& ret = *reinterpret_cast<InterceptorFunction*>(
        sBlock + (sNumInstances++ * sizeof(InterceptorFunction)));

    // Set the InterceptorFunction to the code template.
    auto funcCode = &ret[0];
    memcpy(funcCode, sInterceptorTemplate, TemplateLength);

    // Fill in the patched_func_called pointer in the template.
    auto pfPtr =
        reinterpret_cast<volatile bool**>(&ret[PatchedFuncCalledIndex]);
    *pfPtr = &patched_func_called;
    return ret;
  }

  uint8_t& operator[](size_t i) { return mFuncCode[i]; }

  uint8_t* GetFunction() { return mFuncCode; }

  void SetStub(uintptr_t aStub) {
    auto pfPtr = reinterpret_cast<uintptr_t*>(&mFuncCode[StubFuncIndex]);
    *pfPtr = aStub;
  }

 private:
  // We intercept functions with short machine-code functions that set a boolean
  // and run the stub that launches the original function.  Each entry in the
  // array is the code for one of those interceptor functions.  We cannot
  // free this memory until the test shuts down.
  // The templates have spots for the address of patched_func_called
  // and for the address of the stub function.  Their indices in the byte
  // array are given as constants below and they appear as blocks of
  // 0xff bytes in the templates.
#if defined(_M_X64)
  //  0: 48 b8 ff ff ff ff ff ff ff ff    movabs rax, &patched_func_called
  //  a: c6 00 01                         mov    BYTE PTR [rax],0x1
  //  d: 48 b8 ff ff ff ff ff ff ff ff    movabs rax, &stub_func_ptr
  // 17: ff e0                            jmp    rax
  static constexpr uint8_t sInterceptorTemplate[] = {
      0x48, 0xB8, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
      0xFF, 0xC6, 0x00, 0x01, 0x48, 0xB8, 0xFF, 0xFF, 0xFF,
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xE0};
  static const size_t PatchedFuncCalledIndex = 0x2;
  static const size_t StubFuncIndex = 0xf;
#elif defined(_M_IX86)
  // 0: c6 05 ff ff ff ff 01     mov    BYTE PTR &patched_func_called, 0x1
  // 7: 68 ff ff ff ff           push   &stub_func_ptr
  // c: c3                       ret
  static constexpr uint8_t sInterceptorTemplate[] = {
      0xC6, 0x05, 0xFF, 0xFF, 0xFF, 0xFF, 0x01,
      0x68, 0xFF, 0xFF, 0xFF, 0xFF, 0xC3};
  static const size_t PatchedFuncCalledIndex = 0x2;
  static const size_t StubFuncIndex = 0x8;
#elif defined(_M_ARM64)
  //  0: 31 00 80 52    movz w17, #0x1
  //  4: 90 00 00 58    ldr  x16, #16
  //  8: 11 02 00 39    strb w17, [x16]
  //  c: 90 00 00 58    ldr  x16, #16
  // 10: 00 02 1F D6    br   x16
  // 14: &patched_func_called
  // 1c: &stub_func_ptr
  static constexpr uint8_t sInterceptorTemplate[] = {
      0x31, 0x00, 0x80, 0x52, 0x90, 0x00, 0x00, 0x58, 0x11, 0x02, 0x00, 0x39,
      0x90, 0x00, 0x00, 0x58, 0x00, 0x02, 0x1F, 0xD6, 0xFF, 0xFF, 0xFF, 0xFF,
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  static const size_t PatchedFuncCalledIndex = 0x14;
  static const size_t StubFuncIndex = 0x1c;
#else
#  error "Missing template for architecture"
#endif

  static const size_t TemplateLength = sizeof(sInterceptorTemplate);
  uint8_t mFuncCode[TemplateLength];

  InterceptorFunction() = delete;
  InterceptorFunction(const InterceptorFunction&) = delete;
  InterceptorFunction& operator=(const InterceptorFunction&) = delete;

  static void Init() {
    MOZ_ASSERT(!sBlock);
    sBlock = reinterpret_cast<uint8_t*>(
        ::VirtualAlloc(nullptr, EXEC_MEMBLOCK_SIZE, MEM_RESERVE | MEM_COMMIT,
                       PAGE_EXECUTE_READWRITE));
  }

  static uint8_t* sBlock;
  static size_t sNumInstances;
};

uint8_t* InterceptorFunction::sBlock = nullptr;
size_t InterceptorFunction::sNumInstances = 0;

constexpr uint8_t InterceptorFunction::sInterceptorTemplate[];

#ifdef _M_X64

// To check that unwind information propagates from hooked functions to their
// stubs, we need to find the real address where the detoured code lives.
class RedirectionResolver : public interceptor::WindowsDllPatcherBase<
                                interceptor::VMSharingPolicyShared> {
 public:
  uintptr_t ResolveRedirectedAddressForTest(FARPROC aFunc) {
    return ResolveRedirectedAddress(aFunc).GetAddress();
  }
};

#endif  // _M_X64

void PrintFunctionBytes(FARPROC aFuncAddr, uint32_t aNumBytesToDump) {
  printf("\tFirst %u bytes of function:\n\t", aNumBytesToDump);
  auto code = reinterpret_cast<const uint8_t*>(aFuncAddr);
  for (uint32_t i = 0; i < aNumBytesToDump; ++i) {
    char suffix = (i < (aNumBytesToDump - 1)) ? ' ' : '\n';
    printf("%02hhX%c", code[i], suffix);
  }

  fflush(stdout);
}

// Hook the function and optionally attempt calling it
template <typename OrigFuncT, size_t N, typename PredicateT, typename... Args>
bool TestHook(const char (&dll)[N], const char* func, PredicateT&& aPred,
              Args&&... aArgs) {
  auto orig_func(
      mozilla::MakeUnique<WindowsDllInterceptor::FuncHookType<OrigFuncT>>());
  wchar_t dllW[N];
  std::copy(std::begin(dll), std::end(dll), std::begin(dllW));

  HMODULE module = ::LoadLibraryW(dllW);
  FARPROC funcAddr = ::GetProcAddress(module, func);
  if (!funcAddr) {
    printf(
        "TEST-UNEXPECTED-FAIL | WindowsDllInterceptor | Failed to find %s from "
        "%s\n",
        func, dll);
    fflush(stdout);
    return false;
  }

#ifdef _M_X64

  // Resolve what is the actual address of the code that will be detoured, as
  // that's the code we want to compare with when we check for unwind
  // information. Do that *before* detouring, although the address will only be
  // used after detouring.
  RedirectionResolver resolver;
  auto detouredCodeAddr = resolver.ResolveRedirectedAddressForTest(funcAddr);

#endif  // _M_X64

  bool successful = false;
  WindowsDllInterceptor TestIntercept;
  TestIntercept.Init(dll);

  InterceptorFunction& interceptorFunc = InterceptorFunction::Create();
  successful = orig_func->Set(
      TestIntercept, func,
      reinterpret_cast<OrigFuncT>(interceptorFunc.GetFunction()));

  if (successful) {
    auto stub = reinterpret_cast<uintptr_t>(orig_func->GetStub());
    interceptorFunc.SetStub(stub);
    printf("TEST-PASS | WindowsDllInterceptor | Could hook %s from %s\n", func,
           dll);
    fflush(stdout);

#ifdef _M_X64

    // Check that unwind information has been added if and only if it was
    // present for the original detoured code.
    uintptr_t funcImageBase = 0;
    auto funcEntry =
        RtlLookupFunctionEntry(detouredCodeAddr, &funcImageBase, nullptr);
    bool funcHasUnwindInfo = bool(funcEntry);

    uintptr_t stubImageBase = 0;
    auto stubEntry = RtlLookupFunctionEntry(stub, &stubImageBase, nullptr);
    bool stubHasUnwindInfo = bool(stubEntry);

    if (funcHasUnwindInfo == stubHasUnwindInfo) {
      printf(
          "TEST-PASS | WindowsDllInterceptor | The hook for %s from %s and "
          "the original function are coherent with respect to unwind info: "
          "funcHasUnwindInfo (%d) == stubHasUnwindInfo (%d).\n",
          func, dll, funcHasUnwindInfo, stubHasUnwindInfo);
      fflush(stdout);
    } else {
      printf(
          "TEST-UNEXPECTED-FAIL | WindowsDllInterceptor | Hook for %s from %s "
          "and the original function are not coherent with respect to unwind "
          "info: "
          "funcHasUnwindInfo (%d) != stubHasUnwindInfo (%d).\n",
          func, dll, funcHasUnwindInfo, stubHasUnwindInfo);
      fflush(stdout);
      return false;
    }

    if (stubHasUnwindInfo) {
      if (stub == (stubImageBase + stubEntry->BeginAddress)) {
        printf(
            "TEST-PASS | WindowsDllInterceptor | The hook for %s from %s has "
            "coherent unwind info.\n",
            func, dll);
        fflush(stdout);
      } else {
        printf(
            "TEST-UNEXPECTED-FAIL | WindowsDllInterceptor | The hook for %s "
            " from %s has incoherent unwind info.\n",
            func, dll);
        fflush(stdout);
        return false;
      }
    }

#endif  // _M_X64

    if (!aPred) {
      printf(
          "TEST-SKIPPED | WindowsDllInterceptor | "
          "Will not attempt to execute patched %s.\n",
          func);
      fflush(stdout);
      return true;
    }

    // Test the DLL function we just hooked.
    return CheckHook(reinterpret_cast<OrigFuncT&>(funcAddr), dll, func,
                     std::forward<PredicateT>(aPred),
                     std::forward<Args>(aArgs)...);
  } else {
    printf(
        "TEST-UNEXPECTED-FAIL | WindowsDllInterceptor | Failed to hook %s from "
        "%s\n",
        func, dll);
    fflush(stdout);

    // Print out the function's bytes so that we can easily analyze the error.
    nsModuleHandle mod(::LoadLibraryW(dllW));
    FARPROC funcAddr = ::GetProcAddress(mod, func);
    if (funcAddr) {
      const uint32_t kNumBytesToDump =
          WindowsDllInterceptor::GetWorstCaseRequiredBytesToPatch();
      PrintFunctionBytes(funcAddr, kNumBytesToDump);
    }
    return false;
  }
}

// Detour the function and optionally attempt calling it
template <typename OrigFuncT, size_t N, typename PredicateT>
bool TestDetour(const char (&dll)[N], const char* func, PredicateT&& aPred) {
  auto orig_func(
      mozilla::MakeUnique<WindowsDllInterceptor::FuncHookType<OrigFuncT>>());
  wchar_t dllW[N];
  std::copy(std::begin(dll), std::end(dll), std::begin(dllW));

  bool successful = false;
  WindowsDllInterceptor TestIntercept;
  TestIntercept.Init(dll);

  InterceptorFunction& interceptorFunc = InterceptorFunction::Create();
  successful = orig_func->Set(
      TestIntercept, func,
      reinterpret_cast<OrigFuncT>(interceptorFunc.GetFunction()));

  if (successful) {
    interceptorFunc.SetStub(reinterpret_cast<uintptr_t>(orig_func->GetStub()));
    printf("TEST-PASS | WindowsDllInterceptor | Could detour %s from %s\n",
           func, dll);
    fflush(stdout);
    if (!aPred) {
      printf(
          "TEST-SKIPPED | WindowsDllInterceptor | "
          "Will not attempt to execute patched %s.\n",
          func);
      fflush(stdout);
      return true;
    }

    // Test the DLL function we just hooked.
    HMODULE module = ::LoadLibraryW(dllW);
    FARPROC funcAddr = ::GetProcAddress(module, func);
    if (!funcAddr) {
      return false;
    }

    return CheckHook(reinterpret_cast<OrigFuncT&>(funcAddr), dll, func,
                     std::forward<PredicateT>(aPred));
  } else {
    printf(
        "TEST-UNEXPECTED-FAIL | WindowsDllInterceptor | Failed to detour %s "
        "from %s\n",
        func, dll);
    fflush(stdout);
    return false;
  }
}

// If a function pointer's type returns void*, this template converts that type
// to return uintptr_t instead, for the purposes of predicates.
template <typename FuncT>
struct SubstituteForVoidPtr {
  using Type = FuncT;
};

template <typename... Args>
struct SubstituteForVoidPtr<void* (*)(Args...)> {
  using Type = uintptr_t (*)(Args...);
};

#ifdef _M_IX86
template <typename... Args>
struct SubstituteForVoidPtr<void*(__stdcall*)(Args...)> {
  using Type = uintptr_t(__stdcall*)(Args...);
};

template <typename... Args>
struct SubstituteForVoidPtr<void*(__fastcall*)(Args...)> {
  using Type = uintptr_t(__fastcall*)(Args...);
};
#endif  // _M_IX86

// Determines the function's return type
template <typename FuncT>
struct ReturnType;

template <typename R, typename... Args>
struct ReturnType<R (*)(Args...)> {
  using Type = R;
};

#ifdef _M_IX86
template <typename R, typename... Args>
struct ReturnType<R(__stdcall*)(Args...)> {
  using Type = R;
};

template <typename R, typename... Args>
struct ReturnType<R(__fastcall*)(Args...)> {
  using Type = R;
};
#endif  // _M_IX86

// Predicates that may be supplied during tests
template <typename FuncT>
struct Predicates {
  using ArgType = typename ReturnType<FuncT>::Type;

  template <ArgType CompVal>
  static bool Equals(ArgType aValue) {
    return CompVal == aValue;
  }

  template <ArgType CompVal>
  static bool NotEquals(ArgType aValue) {
    return CompVal != aValue;
  }

  template <ArgType CompVal>
  static bool Ignore(ArgType aValue) {
    return true;
  }
};

// Functions that return void should be ignored, so we specialize the
// Ignore predicate for that case. Use nullptr as the value to compare against.
template <typename... Args>
struct Predicates<void (*)(Args...)> {
  template <nullptr_t DummyVal>
  static bool Ignore() {
    return true;
  }
};

#ifdef _M_IX86
template <typename... Args>
struct Predicates<void(__stdcall*)(Args...)> {
  template <nullptr_t DummyVal>
  static bool Ignore() {
    return true;
  }
};

template <typename... Args>
struct Predicates<void(__fastcall*)(Args...)> {
  template <nullptr_t DummyVal>
  static bool Ignore() {
    return true;
  }
};
#endif  // _M_IX86

// The standard test. Hook |func|, and then try executing it with all zero
// arguments, using |pred| and |comp| to determine whether the call successfully
// executed. In general, you want set pred and comp such that they return true
// when the function is returning whatever value is expected with all-zero
// arguments.
//
// Note: When |func| returns void, you must supply |Ignore| and |nullptr| as the
// |pred| and |comp| arguments, respectively.
#define TEST_HOOK_HELPER(dll, func, pred, comp) \
  TestHook<decltype(&func)>(dll, #func,         \
                            &Predicates<decltype(&func)>::pred<comp>)

#define TEST_HOOK(dll, func, pred, comp) TEST_HOOK_HELPER(dll, func, pred, comp)

// We need to special-case functions that return INVALID_HANDLE_VALUE
// (ie, CreateFile). Our template machinery for comparing values doesn't work
// with integer constants passed as pointers (well, it works on MSVC, but not
// clang, because that is not standard-compliant).
#define TEST_HOOK_FOR_INVALID_HANDLE_VALUE(dll, func)                   \
  TestHook<SubstituteForVoidPtr<decltype(&func)>::Type>(                \
      dll, #func,                                                       \
      &Predicates<SubstituteForVoidPtr<decltype(&func)>::Type>::Equals< \
          uintptr_t(-1)>)

// This variant allows you to explicitly supply arguments to the hooked function
// during testing. You want to provide arguments that produce the conditions
// that induce the function to return a value that is accepted by your
// predicate.
#define TEST_HOOK_PARAMS(dll, func, pred, comp, ...) \
  TestHook<decltype(&func)>(                         \
      dll, #func, &Predicates<decltype(&func)>::pred<comp>, __VA_ARGS__)

// This is for cases when we want to hook |func|, but it is unsafe to attempt
// to execute the function in the context of a test.
#define TEST_HOOK_SKIP_EXEC(dll, func)                                        \
  TestHook<decltype(&func)>(                                                  \
      dll, #func,                                                             \
      reinterpret_cast<bool (*)(typename ReturnType<decltype(&func)>::Type)>( \
          NULL))

// The following three variants are identical to the previous macros,
// however the forcibly use a Detour on 32-bit Windows. On 64-bit Windows,
// these macros are identical to their TEST_HOOK variants.
#define TEST_DETOUR(dll, func, pred, comp) \
  TestDetour<decltype(&func)>(dll, #func,  \
                              &Predicates<decltype(&func)>::pred<comp>)

#define TEST_DETOUR_PARAMS(dll, func, pred, comp, ...) \
  TestDetour<decltype(&func)>(                         \
      dll, #func, &Predicates<decltype(&func)>::pred<comp>, __VA_ARGS__)

#define TEST_DETOUR_SKIP_EXEC(dll, func)                                      \
  TestDetour<decltype(&func)>(                                                \
      dll, #func,                                                             \
      reinterpret_cast<bool (*)(typename ReturnType<decltype(&func)>::Type)>( \
          NULL))

template <typename OrigFuncT, size_t N, typename PredicateT, typename... Args>
bool MaybeTestHook(const bool cond, const char (&dll)[N], const char* func,
                   PredicateT&& aPred, Args&&... aArgs) {
  if (!cond) {
    printf(
        "TEST-SKIPPED | WindowsDllInterceptor | Skipped hook test for %s from "
        "%s\n",
        func, dll);
    fflush(stdout);
    return true;
  }

  return TestHook<OrigFuncT>(dll, func, std::forward<PredicateT>(aPred),
                             std::forward<Args>(aArgs)...);
}

// Like TEST_HOOK, but the test is only executed when cond is true.
#define MAYBE_TEST_HOOK(cond, dll, func, pred, comp) \
  MaybeTestHook<decltype(&func)>(cond, dll, #func,   \
                                 &Predicates<decltype(&func)>::pred<comp>)

#define MAYBE_TEST_HOOK_PARAMS(cond, dll, func, pred, comp, ...) \
  MaybeTestHook<decltype(&func)>(                                \
      cond, dll, #func, &Predicates<decltype(&func)>::pred<comp>, __VA_ARGS__)

#define MAYBE_TEST_HOOK_SKIP_EXEC(cond, dll, func)                            \
  MaybeTestHook<decltype(&func)>(                                             \
      cond, dll, #func,                                                       \
      reinterpret_cast<bool (*)(typename ReturnType<decltype(&func)>::Type)>( \
          NULL))

bool ShouldTestTipTsf() {
  mozilla::DynamicallyLinkedFunctionPtr<decltype(&SHGetKnownFolderPath)>
      pSHGetKnownFolderPath(L"shell32.dll", "SHGetKnownFolderPath");
  if (!pSHGetKnownFolderPath) {
    return false;
  }

  PWSTR commonFilesPath = nullptr;
  if (FAILED(pSHGetKnownFolderPath(FOLDERID_ProgramFilesCommon, 0, nullptr,
                                   &commonFilesPath))) {
    return false;
  }

  wchar_t fullPath[MAX_PATH + 1] = {};
  wcscpy(fullPath, commonFilesPath);
  wcscat(fullPath, L"\\Microsoft Shared\\Ink\\tiptsf.dll");
  CoTaskMemFree(commonFilesPath);

  if (!LoadLibraryW(fullPath)) {
    return false;
  }

  // Leak the module so that it's loaded for the interceptor test
  return true;
}

static const wchar_t gEmptyUnicodeStringLiteral[] = L"";
static UNICODE_STRING gEmptyUnicodeString;
static BOOLEAN gIsPresent;

bool HasApiSetQueryApiSetPresence() {
  mozilla::DynamicallyLinkedFunctionPtr<decltype(&ApiSetQueryApiSetPresence)>
      func(L"Api-ms-win-core-apiquery-l1-1-0.dll", "ApiSetQueryApiSetPresence");
  if (!func) {
    return false;
  }

  // Prepare gEmptyUnicodeString for the test
  ::RtlInitUnicodeString(&gEmptyUnicodeString, gEmptyUnicodeStringLiteral);

  return true;
}

// Set this to true to test function unhooking (currently broken).
const bool ShouldTestUnhookFunction = false;

#if defined(_M_X64) || defined(_M_ARM64)

// Use VMSharingPolicyUnique for the ShortInterceptor, as it needs to
// reserve its trampoline memory in a special location.
using ShortInterceptor = mozilla::interceptor::WindowsDllInterceptor<
    mozilla::interceptor::VMSharingPolicyUnique<
        mozilla::interceptor::MMPolicyInProcess>>;

static ShortInterceptor::FuncHookType<decltype(&::NtMapViewOfSection)>
    orig_NtMapViewOfSection;

#endif  // defined(_M_X64) || defined(_M_ARM64)

bool TestShortDetour() {
#if defined(_M_X64) || defined(_M_ARM64)
  auto pNtMapViewOfSection = reinterpret_cast<decltype(&::NtMapViewOfSection)>(
      ::GetProcAddress(::GetModuleHandleW(L"ntdll.dll"), "NtMapViewOfSection"));
  if (!pNtMapViewOfSection) {
    printf(
        "TEST-FAILED | WindowsDllInterceptor | "
        "Failed to resolve ntdll!NtMapViewOfSection\n");
    fflush(stdout);
    return false;
  }

  {  // Scope for shortInterceptor
    ShortInterceptor shortInterceptor;
    shortInterceptor.TestOnlyDetourInit(
        L"ntdll.dll",
        mozilla::interceptor::DetourFlags::eTestOnlyForceShortPatch);

    InterceptorFunction& interceptorFunc = InterceptorFunction::Create();
    if (!orig_NtMapViewOfSection.SetDetour(
            shortInterceptor, "NtMapViewOfSection",
            reinterpret_cast<decltype(&::NtMapViewOfSection)>(
                interceptorFunc.GetFunction()))) {
      printf(
          "TEST-FAILED | WindowsDllInterceptor | "
          "Failed to hook ntdll!NtMapViewOfSection via 10-byte patch\n");
      fflush(stdout);
      return false;
    }

    interceptorFunc.SetStub(
        reinterpret_cast<uintptr_t>(orig_NtMapViewOfSection.GetStub()));

    auto pred =
        &Predicates<decltype(&::NtMapViewOfSection)>::Ignore<((NTSTATUS)0)>;

    if (!CheckHook(pNtMapViewOfSection, "ntdll.dll", "NtMapViewOfSection",
                   pred)) {
      // CheckHook has already printed the error message for us
      return false;
    }
  }

  // Now ensure that our hook cleanup worked
  if (ShouldTestUnhookFunction) {
    NTSTATUS status =
        pNtMapViewOfSection(nullptr, nullptr, nullptr, 0, 0, nullptr, nullptr,
                            ((SECTION_INHERIT)0), 0, 0);
    if (NT_SUCCESS(status)) {
      printf(
          "TEST-FAILED | WindowsDllInterceptor | "
          "Unexpected successful call to ntdll!NtMapViewOfSection after "
          "removing short-patched hook\n");
      fflush(stdout);
      return false;
    }

    printf(
        "TEST-PASS | WindowsDllInterceptor | "
        "Successfully unhooked ntdll!NtMapViewOfSection via short patch\n");
    fflush(stdout);
  }

  return true;
#else
  return true;
#endif  // defined(_M_X64) || defined(_M_ARM64)
}

constexpr uintptr_t NoStubAddressCheck = 0;
constexpr uintptr_t ExpectedFail = 1;
MOZ_GLOBINIT struct TestCase {
  const char* mFunctionName;
  uintptr_t mExpectedStub;
  bool mPatchedOnce;
  bool mSkipExec;
  explicit TestCase(const char* aFunctionName, uintptr_t aExpectedStub,
                    bool aSkipExec = false)
      : mFunctionName(aFunctionName),
        mExpectedStub(aExpectedStub),
        mPatchedOnce(false),
        mSkipExec(aSkipExec) {}
} g_AssemblyTestCases[] = {
#if defined(__clang__)
#  if defined(_M_X64)
    // Since we have PatchIfTargetIsRecognizedTrampoline for x64, we expect the
    // original jump destination is returned as a stub.
    TestCase("MovPushRet", JumpDestination,
             mozilla::IsUserShadowStackEnabled()),
    TestCase("MovRaxJump", JumpDestination),
    TestCase("DoubleJump", JumpDestination),

    // Passing NoStubAddressCheck as the following testcases return
    // a trampoline address instead of the original destination.
    TestCase("NearJump", NoStubAddressCheck),
    TestCase("NearJump2", NoStubAddressCheck),
    TestCase("JumpWith8BitOffset", NoStubAddressCheck),
    TestCase("OpcodeFF", NoStubAddressCheck),
    TestCase("IndirectCall", NoStubAddressCheck),
    TestCase("MovImm64", NoStubAddressCheck),
    TestCase("RexCmpRipRelativeBytePtr", NoStubAddressCheck),
    TestCase("AndWithSib", NoStubAddressCheck),
    TestCase("AndWithoutSib", NoStubAddressCheck),
    TestCase("RexAndWithSib", NoStubAddressCheck),
    TestCase("RexAndWithoutSib", NoStubAddressCheck),
    TestCase("JmpInsideEarlyBytes", ExpectedFail),
    TestCase("CallInsideEarlyBytes", ExpectedFail),
    TestCase("StoreR10RipRelative", ExpectedFail),
    TestCase("LoadRspRipRelative", ExpectedFail),
    TestCase("RexCmpRipRelativeSpl", ExpectedFail),
#  elif defined(_M_IX86)
    // Skip the stub address check as we always generate a trampoline for x86.
    TestCase("PushRet", NoStubAddressCheck,
             mozilla::IsUserShadowStackEnabled()),
    TestCase("MovEaxJump", NoStubAddressCheck),
    TestCase("DoubleJump", NoStubAddressCheck),
    TestCase("Opcode83", NoStubAddressCheck),
    TestCase("LockPrefix", NoStubAddressCheck),
    TestCase("LooksLikeLockPrefix", NoStubAddressCheck),
#  endif
#  if !defined(_M_ARM64) && !defined(DEBUG)
    // Skip on Debug build because it hits MOZ_ASSERT_UNREACHABLE.
    TestCase("UnsupportedOp", ExpectedFail),
#  endif  // !defined(_M_ARM64) && !defined(DEBUG)
#endif    // defined(__clang__)
};

template <typename InterceptorType>
bool TestAssemblyFunctions() {
  static const auto patchedFunction = []() { patched_func_called = true; };

  InterceptorType interceptor;
  interceptor.Init("TestDllInterceptor.exe");

  for (auto& testCase : g_AssemblyTestCases) {
    if (testCase.mExpectedStub == NoStubAddressCheck && testCase.mPatchedOnce) {
      // For the testcases with NoStubAddressCheck, we revert a hook by
      // jumping into the original stub, which is not detourable again.
      continue;
    }

    typename InterceptorType::template FuncHookType<void (*)()> hook;
    bool result =
        hook.Set(interceptor, testCase.mFunctionName, patchedFunction);
    if (testCase.mExpectedStub == ExpectedFail) {
      if (result) {
        printf(
            "TEST-FAILED | WindowsDllInterceptor | "
            "Unexpectedly succeeded to detour %s.\n",
            testCase.mFunctionName);
        return false;
      }
#if defined(NIGHTLY_BUILD)
      const Maybe<DetourError>& maybeError = interceptor.GetLastDetourError();
      if (maybeError.isNothing()) {
        printf(
            "TEST-FAILED | WindowsDllInterceptor | "
            "DetourError was not set on detour error.\n");
        return false;
      }
      if (maybeError.ref().mErrorCode !=
          DetourResultCode::DETOUR_PATCHER_CREATE_TRAMPOLINE_ERROR) {
        printf(
            "TEST-FAILED | WindowsDllInterceptor | "
            "A wrong detour errorcode was set on detour error for %s. (got "
            "%d)\n",
            testCase.mFunctionName, maybeError.ref().mErrorCode);
        return false;
      }
#endif  // defined(NIGHTLY_BUILD)
      printf("TEST-PASS | WindowsDllInterceptor | %s\n",
             testCase.mFunctionName);
      continue;
    }

    if (!result) {
      printf(
          "TEST-FAILED | WindowsDllInterceptor | "
          "Failed to detour %s.\n",
          testCase.mFunctionName);
      return false;
    }

    testCase.mPatchedOnce = true;

    const auto actualStub = reinterpret_cast<uintptr_t>(hook.GetStub());
    if (testCase.mExpectedStub != NoStubAddressCheck &&
        actualStub != testCase.mExpectedStub) {
      printf(
          "TEST-FAILED | WindowsDllInterceptor | "
          "Wrong stub was backed up for %s: %zx\n",
          testCase.mFunctionName, actualStub);
      return false;
    }

    if (testCase.mSkipExec) {
      printf(
          "TEST-SKIPPED | WindowsDllInterceptor | "
          "Will not attempt to execute patched %s.\n",
          testCase.mFunctionName);
    } else {
      patched_func_called = false;

      auto originalFunction = reinterpret_cast<void (*)()>(
          GetProcAddress(GetModuleHandleW(nullptr), testCase.mFunctionName));
      originalFunction();

      if (!patched_func_called) {
        printf(
            "TEST-FAILED | WindowsDllInterceptor | "
            "Hook from %s was not called\n",
            testCase.mFunctionName);
        return false;
      }
    }

    printf("TEST-PASS | WindowsDllInterceptor | %s\n", testCase.mFunctionName);
  }

  return true;
}

#if defined(_M_X64) && !defined(MOZ_CODE_COVERAGE)
// We want to test hooking and unhooking with unwind information, so we need:
//  - a VMSharingPolicy such that ShouldUnhookUponDestruction() is true and
//    Items() is implemented;
//  - a MMPolicy such that ShouldUnhookUponDestruction() is true and
//    kSupportsUnwindInfo is true.
using DetouredCallInterceptor = mozilla::interceptor::WindowsDllInterceptor<
    mozilla::interceptor::VMSharingPolicyUnique<
        mozilla::interceptor::MMPolicyInProcess>>;

struct DetouredCallChunk {
  alignas(uint32_t) RUNTIME_FUNCTION functionTable[1];
  alignas(uint32_t) uint8_t unwindInfo[sizeof(gDetouredCallUnwindInfo)];
  uint8_t code[gDetouredCallCodeSize];
};

// Unfortunately using RtlAddFunctionTable for static code that lives within
// a module doesn't seem to work. Presumably it conflicts with the static
// function tables. So we recreate gDetouredCall as dynamic code to be able to
// associate it with unwind information.
MOZ_RUNINIT decltype(&DetouredCallCode) gDetouredCall =
    []() -> decltype(&DetouredCallCode) {
  // We first adjust the detoured call jumper from:
  //   ff 25 00 00 00 00    jmp qword ptr [rip + 0]
  // to:
  //   ff 25 XX XX XX XX    jmp qword ptr [rip + offset gDetouredCall]
  uint8_t bytes[6]{0xff, 0x25, 0, 0, 0, 0};
  if (0 != memcmp(bytes, reinterpret_cast<void*>(DetouredCallJumper),
                  sizeof bytes)) {
    return nullptr;
  }

  DWORD oldProtect{};
  // Because the current function could be located on the same memory page as
  // DetouredCallJumper, we must preserve the permission to execute the page
  // while we adjust the detoured call jumper (hence PAGE_EXECUTE_READWRITE).
  if (!VirtualProtect(reinterpret_cast<void*>(DetouredCallJumper), sizeof bytes,
                      PAGE_EXECUTE_READWRITE, &oldProtect)) {
    return nullptr;
  }

  *reinterpret_cast<uint32_t*>(&bytes[2]) = static_cast<uint32_t>(
      reinterpret_cast<uintptr_t>(&gDetouredCall) -
      (reinterpret_cast<uintptr_t>(DetouredCallJumper) + sizeof bytes));
  memcpy(reinterpret_cast<void*>(DetouredCallJumper), bytes, sizeof bytes);

  if (!VirtualProtect(reinterpret_cast<void*>(DetouredCallJumper), sizeof bytes,
                      oldProtect, &oldProtect)) {
    return nullptr;
  }

  auto detouredCallChunk = reinterpret_cast<DetouredCallChunk*>(
      VirtualAlloc(nullptr, sizeof(DetouredCallChunk), MEM_RESERVE | MEM_COMMIT,
                   PAGE_READWRITE));
  if (!detouredCallChunk) {
    return nullptr;
  }

  detouredCallChunk->functionTable[0].BeginAddress =
      offsetof(DetouredCallChunk, code);
  detouredCallChunk->functionTable[0].EndAddress =
      offsetof(DetouredCallChunk, code) + gDetouredCallCodeSize;
  detouredCallChunk->functionTable[0].UnwindData =
      offsetof(DetouredCallChunk, unwindInfo);
  memcpy(reinterpret_cast<void*>(&detouredCallChunk->unwindInfo),
         reinterpret_cast<void*>(gDetouredCallUnwindInfo),
         sizeof(detouredCallChunk->unwindInfo));
  memcpy(reinterpret_cast<void*>(&detouredCallChunk->code[0]),
         reinterpret_cast<void*>(DetouredCallCode),
         sizeof(detouredCallChunk->code));

  if (!VirtualProtect(reinterpret_cast<void*>(detouredCallChunk),
                      sizeof(DetouredCallChunk), PAGE_EXECUTE_READ,
                      &oldProtect)) {
    VirtualFree(detouredCallChunk, 0, MEM_RELEASE);
    return nullptr;
  }

  if (!RtlAddFunctionTable(detouredCallChunk->functionTable, 1,
                           reinterpret_cast<uintptr_t>(detouredCallChunk))) {
    VirtualFree(detouredCallChunk, 0, MEM_RELEASE);
    return nullptr;
  }

  return reinterpret_cast<decltype(&DetouredCallCode)>(detouredCallChunk->code);
}();

// We use our own variable instead of patched_func_called because the callee of
// gDetouredCall could end up calling other, already hooked functions could
// change patched_func_called.
static volatile bool sCalledPatchedDetouredCall = false;
static volatile bool sCalledDetouredCallCallee = false;
static volatile bool sCouldUnwindFromDetouredCallCallee = false;
void DetouredCallCallee() {
  sCalledDetouredCallCallee = true;

  // Check that we can fully unwind the stack
  CONTEXT contextRecord{};
  RtlCaptureContext(&contextRecord);
  if (!contextRecord.Rip) {
    printf(
        "TEST-UNEXPECTED-FAIL | WindowsDllInterceptor | "
        "DetouredCallCallee was unable to get an initial context to work "
        "with\n");
    fflush(stdout);
    return;
  }
  while (contextRecord.Rip) {
    DWORD64 imageBase = 0;
    auto FunctionEntry =
        RtlLookupFunctionEntry(contextRecord.Rip, &imageBase, nullptr);
    if (!FunctionEntry) {
      printf(
          "TEST-UNEXPECTED-FAIL | WindowsDllInterceptor | "
          "DetouredCallCallee was unable to get unwind info for ControlPc=%p\n",
          reinterpret_cast<void*>(contextRecord.Rip));
      fflush(stdout);
      return;
    }
    printf(
        "TEST-PASS | WindowsDllInterceptor | "
        "DetouredCallCallee was able to get unwind info for ControlPc=%p\n",
        reinterpret_cast<void*>(contextRecord.Rip));
    fflush(stdout);
    void* handlerData = nullptr;
    DWORD64 establisherFrame = 0;
    RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, contextRecord.Rip,
                     FunctionEntry, &contextRecord, &handlerData,
                     &establisherFrame, nullptr);
  }
  sCouldUnwindFromDetouredCallCallee = true;
}

static DetouredCallInterceptor::FuncHookType<decltype(&DetouredCallCode)>
    orig_DetouredCall;

static void patched_DetouredCall(uintptr_t aCallee) {
  sCalledPatchedDetouredCall = true;
  return orig_DetouredCall(aCallee);
}

bool TestCallingDetouredCall(const char* aTestDescription,
                             bool aExpectCalledPatchedDetouredCall) {
  sCalledPatchedDetouredCall = false;
  sCalledDetouredCallCallee = false;
  sCouldUnwindFromDetouredCallCallee = false;
  DetouredCallJumper(reinterpret_cast<uintptr_t>(DetouredCallCallee));

  if (aExpectCalledPatchedDetouredCall != sCalledPatchedDetouredCall) {
    printf(
        "TEST-UNEXPECTED-FAIL | WindowsDllInterceptor | "
        "%s: expectCalledPatchedDetouredCall (%d) differs from "
        "sCalledPatchedDetouredCall (%d)\n",
        aTestDescription, aExpectCalledPatchedDetouredCall,
        sCalledPatchedDetouredCall);
    fflush(stdout);
    return false;
  }

  printf(
      "TEST-PASS | WindowsDllInterceptor | "
      "%s: expectCalledPatchedDetouredCall (%d) matches with "
      "sCalledPatchedDetouredCall (%d)\n",
      aTestDescription, aExpectCalledPatchedDetouredCall,
      sCalledPatchedDetouredCall);
  fflush(stdout);

  if (!sCalledDetouredCallCallee) {
    printf(
        "TEST-UNEXPECTED-FAIL | WindowsDllInterceptor | "
        "%s: gDetouredCall failed to call its callee\n",
        aTestDescription);
    fflush(stdout);
    return false;
  }

  printf(
      "TEST-PASS | WindowsDllInterceptor | "
      "%s: gDetouredCall successfully called its callee\n",
      aTestDescription);
  fflush(stdout);

  if (!sCouldUnwindFromDetouredCallCallee) {
    printf(
        "TEST-UNEXPECTED-FAIL | WindowsDllInterceptor | "
        "%s: the callee of gDetouredCall failed to unwind\n",
        aTestDescription);
    fflush(stdout);
    return false;
  }

  printf(
      "TEST-PASS | WindowsDllInterceptor | "
      "%s: the callee of gDetouredCall successfully unwinded\n",
      aTestDescription);
  fflush(stdout);
  return true;
}

// Test that detouring a call preserves unwind information (bug 1798787).
bool TestDetouredCallUnwindInfo() {
  if (!gDetouredCall) {
    printf(
        "TEST-UNEXPECTED-FAIL | WindowsDllInterceptor | "
        "Failed to generate dynamic gDetouredCall code\n");
    fflush(stdout);
    return false;
  }

  uintptr_t imageBase = 0;
  if (!RtlLookupFunctionEntry(reinterpret_cast<uintptr_t>(gDetouredCall),
                              &imageBase, nullptr)) {
    printf(
        "TEST-UNEXPECTED-FAIL | WindowsDllInterceptor | "
        "Failed to find unwind information for dynamic gDetouredCall code\n");
    fflush(stdout);
    return false;
  }

  // We first double check that we manage to unwind when we *do not* detour
  if (!TestCallingDetouredCall("Before hooking", false)) {
    return false;
  }

  uintptr_t StubAddress = 0;

  // The real test starts here: let's detour and check if we can still unwind
  {
    DetouredCallInterceptor ExeIntercept;
    ExeIntercept.Init("TestDllInterceptor.exe");
    if (!orig_DetouredCall.Set(ExeIntercept, "DetouredCallJumper",
                               &patched_DetouredCall)) {
      printf(
          "TEST-UNEXPECTED-FAIL | WindowsDllInterceptor | "
          "Failed to hook the detoured call jumper.\n");
      fflush(stdout);
      return false;
    }

    printf(
        "TEST-PASS | WindowsDllInterceptor | "
        "Successfully hooked the detoured call jumper.\n");
    fflush(stdout);

    StubAddress = reinterpret_cast<uintptr_t>(orig_DetouredCall.GetStub());
    if (!RtlLookupFunctionEntry(StubAddress, &imageBase, nullptr)) {
      printf(
          "TEST-UNEXPECTED-FAIL | WindowsDllInterceptor | "
          "Failed to find unwind information for detoured code of "
          "gDetouredCall\n");
      fflush(stdout);
      return false;
    }

    TestCallingDetouredCall("After hooking", true);
  }

  // Check that we can still unwind after clearing the hook.
  return TestCallingDetouredCall("After unhooking", false);
}
#endif  // defined(_M_X64) && !defined(MOZ_CODE_COVERAGE)

#if defined(_M_X64) || defined(_M_IX86)
bool TestSpareBytesAfterDetour() {
  WindowsDllInterceptor interceptor;
  interceptor.Init("TestDllInterceptor.exe");
  InterceptorFunction& interceptorFunc = InterceptorFunction::Create();
  auto orig_func(
      mozilla::MakeUnique<WindowsDllInterceptor::FuncHookType<void (*)()>>());

  bool successful = orig_func->Set(
      interceptor, "SpareBytesAfterDetour",
      reinterpret_cast<void (*)()>(interceptorFunc.GetFunction()));
  if (!successful) {
    printf(
        "TEST-FAILED | WindowsDllInterceptor | "
        "Failed to detour SpareBytesAfterDetour.\n");
    return false;
  }
  FARPROC funcAddr =
      ::GetProcAddress(GetModuleHandleW(nullptr), "SpareBytesAfterDetour");
  if (!funcAddr) {
    printf(
        "TEST-FAILED | WindowsDllInterceptor | "
        "Failed to GetProcAddress() for SpareBytesAfterDetour.\n");
    return false;
  }
  uint8_t* funcBytes = reinterpret_cast<uint8_t*>(funcAddr);
#  if defined(_M_X64)
  // patch is 13 bytes
  // the next instruction ends after 17 bytes
  if (*(funcBytes + 13) != 0x90 || *(funcBytes + 14) != 0x90 ||
      *(funcBytes + 15) != 0x90 || *(funcBytes + 16) != 0x90) {
    printf(
        "TEST-FAILED | WindowsDllInterceptor | "
        "SpareBytesAfterDetour doesn't have nop's after the patch.\n");
    PrintFunctionBytes(funcAddr, 17);
    return false;
  }
  printf(
      "TEST-PASS | WindowsDllInterceptor | "
      "SpareBytesAfterDetour has correct nop bytes after the patch.\n");
#  elif defined(_M_IX86)
  // patch is 5 bytes
  // the next instruction ends after 6 bytes
  if (*(funcBytes + 5) != 0x90) {
    printf(
        "TEST-FAILED | WindowsDllInterceptor | "
        "SpareBytesAfterDetour doesn't have nop's after the patch.\n");
    PrintFunctionBytes(funcAddr, 6);
    return false;
  }
  printf(
      "TEST-PASS | WindowsDllInterceptor | "
      "SpareBytesAfterDetour has correct nop bytes after the patch.\n");
#  endif

  return true;
}
#endif  // defined(_M_X64) || defined(_M_IX86)

#if defined(_M_X64)
bool TestSpareBytesAfterDetourFor10BytePatch() {
  ShortInterceptor interceptor;
  interceptor.TestOnlyDetourInit(
      L"TestDllInterceptor.exe",
      mozilla::interceptor::DetourFlags::eTestOnlyForceShortPatch);
  InterceptorFunction& interceptorFunc = InterceptorFunction::Create();

  auto orig_func(
      mozilla::MakeUnique<ShortInterceptor::FuncHookType<void (*)()>>());
  bool successful = orig_func->SetDetour(
      interceptor, "SpareBytesAfterDetourFor10BytePatch",
      reinterpret_cast<void (*)()>(interceptorFunc.GetFunction()));
  if (!successful) {
    printf(
        "TEST-FAILED | WindowsDllInterceptor | "
        "Failed to detour SpareBytesAfterDetourFor10BytePatch.\n");
    return false;
  }
  FARPROC funcAddr = ::GetProcAddress(GetModuleHandleW(nullptr),
                                      "SpareBytesAfterDetourFor10BytePatch");
  if (!funcAddr) {
    printf(
        "TEST-FAILED | WindowsDllInterceptor | "
        "Failed to GetProcAddress() for "
        "SpareBytesAfterDetourFor10BytePatch.\n");
    return false;
  }
  uint8_t* funcBytes = reinterpret_cast<uint8_t*>(funcAddr);
  // patch is 10 bytes
  // the next instruction ends after 12 bytes
  if (*(funcBytes + 10) != 0x90 || *(funcBytes + 11) != 0x90) {
    printf(
        "TEST-FAILED | WindowsDllInterceptor | "
        "SpareBytesAfterDetourFor10BytePatch doesn't have nop's after the "
        "patch.\n");
    PrintFunctionBytes(funcAddr, 12);
    return false;
  }
  printf(
      "TEST-PASS | WindowsDllInterceptor | "
      "SpareBytesAfterDetourFor10BytePatch has correct nop bytes after the "
      "patch.\n");
  return true;
}
#endif  // defined(_M_X64)

bool TestDynamicCodePolicy() {
  PROCESS_MITIGATION_DYNAMIC_CODE_POLICY policy = {};
  policy.ProhibitDynamicCode = true;

  mozilla::DynamicallyLinkedFunctionPtr<decltype(&SetProcessMitigationPolicy)>
      pSetProcessMitigationPolicy(L"kernel32.dll",
                                  "SetProcessMitigationPolicy");
  if (!pSetProcessMitigationPolicy) {
    printf(
        "TEST-UNEXPECTED-FAIL | WindowsDllInterceptor | "
        "SetProcessMitigationPolicy does not exist.\n");
    fflush(stdout);
    return false;
  }

  if (!pSetProcessMitigationPolicy(ProcessDynamicCodePolicy, &policy,
                                   sizeof(policy))) {
    printf(
        "TEST-UNEXPECTED-FAIL | WindowsDllInterceptor | "
        "Fail to enable ProcessDynamicCodePolicy.\n");
    fflush(stdout);
    return false;
  }

  WindowsDllInterceptor ExeIntercept;
  ExeIntercept.Init("TestDllInterceptor.exe");

  // Make sure we fail to hook a function if ProcessDynamicCodePolicy is on
  // because we cannot create an executable trampoline region.
  if (orig_payloadNotHooked.Set(ExeIntercept, "payloadNotHooked",
                                &patched_rotatePayload)) {
    printf(
        "TEST-UNEXPECTED-FAIL | WindowsDllInterceptor | "
        "ProcessDynamicCodePolicy is not working.\n");
    fflush(stdout);
    return false;
  }

  printf(
      "TEST-PASS | WindowsDllInterceptor | "
      "Successfully passed TestDynamicCodePolicy.\n");
  fflush(stdout);
  return true;
}

#if defined(_M_X64)
static uint64_t gXorOperands[] = {0, kRipRelativeValue};

constexpr unsigned char kTestValue = 0xcc;

bool TestIsEqualToGlobalValue() {
  gGlobalValue = kTestValue;
  if (!IsEqualToGlobalValue(kTestValue)) {
    printf(
        "TEST-UNEXPECTED-FAIL | WindowsDllInterceptor | "
        "Obtained wrong result from IsEqualToGlobalValue (value=0x%02x, "
        "expected: true).\n",
        kTestValue);
    fflush(stdout);
    return false;
  }
  for (int value = 0; value <= 255; ++value) {
    if (value != kTestValue && IsEqualToGlobalValue(value)) {
      printf(
          "TEST-UNEXPECTED-FAIL | WindowsDllInterceptor | "
          "Obtained wrong result from IsEqualToGlobalValue (value=0x%02x, "
          "expected: false).\n",
          value);
      fflush(stdout);
      return false;
    }
  }
  printf(
      "TEST-PASS | WindowsDllInterceptor | "
      "Successfully passed TestIsEqualToGlobalValue.\n");
  fflush(stdout);
  return true;
}
#endif  // defined(_M_X64)

extern "C" int wmain(int argc, wchar_t* argv[]) {
  LARGE_INTEGER start;
  QueryPerformanceCounter(&start);

  payload initial = {0x12345678, 0xfc4e9d31, 0x87654321};
  payload p0, p1;
  ZeroMemory(&p0, sizeof(p0));
  ZeroMemory(&p1, sizeof(p1));

  p0 = rotatePayload(initial);

  {
    WindowsDllInterceptor ExeIntercept;
    ExeIntercept.Init("TestDllInterceptor.exe");
    if (orig_rotatePayload.Set(ExeIntercept, "rotatePayload",
                               &patched_rotatePayload)) {
      printf("TEST-PASS | WindowsDllInterceptor | Hook added\n");
      fflush(stdout);
    } else {
      printf(
          "TEST-UNEXPECTED-FAIL | WindowsDllInterceptor | Failed to add "
          "hook\n");
      fflush(stdout);
      return 1;
    }

    p1 = rotatePayload(initial);

    if (patched_func_called) {
      printf("TEST-PASS | WindowsDllInterceptor | Hook called\n");
      fflush(stdout);
    } else {
      printf(
          "TEST-UNEXPECTED-FAIL | WindowsDllInterceptor | Hook was not "
          "called\n");
      fflush(stdout);
      return 1;
    }

    if (p0 == p1) {
      printf("TEST-PASS | WindowsDllInterceptor | Hook works properly\n");
      fflush(stdout);
    } else {
      printf(
          "TEST-UNEXPECTED-FAIL | WindowsDllInterceptor | Hook didn't return "
          "the right information\n");
      fflush(stdout);
      return 1;
    }
  }

  patched_func_called = false;
  ZeroMemory(&p1, sizeof(p1));

  p1 = rotatePayload(initial);

  if (ShouldTestUnhookFunction != patched_func_called) {
    printf(
        "TEST-PASS | WindowsDllInterceptor | Hook was %scalled after "
        "unregistration\n",
        ShouldTestUnhookFunction ? "not " : "");
    fflush(stdout);
  } else {
    printf(
        "TEST-UNEXPECTED-FAIL | WindowsDllInterceptor | Hook was %scalled "
        "after unregistration\n",
        ShouldTestUnhookFunction ? "" : "not ");
    fflush(stdout);
    return 1;
  }

  if (p0 == p1) {
    printf(
        "TEST-PASS | WindowsDllInterceptor | Original function worked "
        "properly\n");
    fflush(stdout);
  } else {
    printf(
        "TEST-UNEXPECTED-FAIL | WindowsDllInterceptor | Original function "
        "didn't return the right information\n");
    fflush(stdout);
    return 1;
  }

  CredHandle credHandle;
  memset(&credHandle, 0, sizeof(CredHandle));
  OBJECT_ATTRIBUTES attributes = {};
  VARIANTARG var;
  VariantInit(&var);

  // NB: These tests should be ordered such that lower-level APIs are tested
  // before higher-level APIs.
  if (TestShortDetour() &&
  // Run <ShortInterceptor> first because <WindowsDllInterceptor>
  // does not clean up hooks.
#if defined(_M_X64)
      TestAssemblyFunctions<ShortInterceptor>() &&
#endif
      TestAssemblyFunctions<WindowsDllInterceptor>() &&
#if defined(_M_X64)
      // Check that the test passes before hooking ...
      TestIsEqualToGlobalValue() &&
      TEST_HOOK_PARAMS("TestDllInterceptor.exe", IsEqualToGlobalValue, Equals,
                       true, kTestValue) &&
      // ... and also after hooking.
      TestIsEqualToGlobalValue() &&
      TEST_HOOK("TestDllInterceptor.exe", LoadRaxRipRelative, Equals,
                kRipRelativeValue) &&
      TEST_HOOK("TestDllInterceptor.exe", LoadR10RipRelative, Equals,
                kRipRelativeValue) &&
      TEST_HOOK("TestDllInterceptor.exe", LoadR8RipRelative, Equals,
                kRipRelativeValue) &&
      TEST_HOOK("TestDllInterceptor.exe", LoadR11RexWRBRipRelative, Equals,
                kRipRelativeValue) &&
      TEST_HOOK("TestDllInterceptor.exe", LoadRdxRexWBRipRelative, Equals,
                kRipRelativeValue) &&
      TEST_HOOK("TestDllInterceptor.exe", LoadR12RipRelative, Equals,
                kRipRelativeValue) &&
      TEST_HOOK("TestDllInterceptor.exe", LoadR13RipRelative, Equals,
                kRipRelativeValue) &&
      TEST_HOOK("TestDllInterceptor.exe", LoadRbpRipRelative, Equals,
                kRipRelativeValue) &&
      TEST_HOOK("TestDllInterceptor.exe", LeaR8RipRelative, Equals,
                kRipRelativeValue) &&
      TEST_HOOK("TestDllInterceptor.exe", LeaRdxRexWBRipRelative, Equals,
                kRipRelativeValue) &&
      TEST_HOOK("TestDllInterceptor.exe", RexWIndirectJump, Equals,
                kRipRelativeValue) &&
      TEST_HOOK("TestDllInterceptor.exe", RexWIndirectCall, Equals,
                ~kRipRelativeValue) &&
      TEST_HOOK_PARAMS("TestDllInterceptor.exe", XorWithMemoryOperand, Equals,
                       kRipRelativeValue & 0xffffffffULL, gXorOperands) &&
      TEST_HOOK_PARAMS("TestDllInterceptor.exe", XorRexRBWithMemoryOperand,
                       Equals, kRipRelativeValue & 0xffffffffULL, 0, 0, 0,
                       gXorOperands) &&
      TEST_HOOK("TestDllInterceptor.exe", CmpImm8RipRelativeBytePtr, Equals,
                true) &&
#endif
#ifdef _M_IX86
      // We keep this test to hook complex code on x86. (Bug 850957)
      TEST_HOOK("ntdll.dll", NtFlushBuffersFile, NotEquals, 0) &&
#endif
      TEST_HOOK("ntdll.dll", NtCreateFile, NotEquals, 0) &&
      TEST_HOOK("ntdll.dll", NtReadFile, NotEquals, 0) &&
      TEST_HOOK("ntdll.dll", NtReadFileScatter, NotEquals, 0) &&
      TEST_HOOK("ntdll.dll", NtWriteFile, NotEquals, 0) &&
      TEST_HOOK("ntdll.dll", NtWriteFileGather, NotEquals, 0) &&
      TEST_HOOK_PARAMS("ntdll.dll", NtQueryFullAttributesFile, NotEquals, 0,
                       &attributes, nullptr) &&
      TEST_DETOUR_SKIP_EXEC("ntdll.dll", LdrLoadDll) &&
      TEST_HOOK("ntdll.dll", LdrUnloadDll, NotEquals, 0) &&
      TEST_HOOK_SKIP_EXEC("ntdll.dll", LdrResolveDelayLoadedAPI) &&
      MAYBE_TEST_HOOK_PARAMS(HasApiSetQueryApiSetPresence(),
                             "Api-ms-win-core-apiquery-l1-1-0.dll",
                             ApiSetQueryApiSetPresence, Equals, FALSE,
                             &gEmptyUnicodeString, &gIsPresent) &&
      TEST_HOOK("kernelbase.dll", QueryDosDeviceW, Equals, 0) &&
#if !defined(_M_ARM64)
      TEST_HOOK("kernel32.dll", GetFileAttributesW, Equals,
                INVALID_FILE_ATTRIBUTES) &&
#  ifndef MOZ_ASAN
      // Bug 733892: toolkit/crashreporter/nsExceptionHandler.cpp
      // This fails on ASan because the ASan runtime already hooked this
      // function
      TEST_HOOK("kernel32.dll", SetUnhandledExceptionFilter, Ignore, nullptr) &&
#  endif
#endif  // !defined(_M_ARM64)
#ifdef _M_IX86
      TEST_HOOK_FOR_INVALID_HANDLE_VALUE("kernel32.dll", CreateFileW) &&
#endif
#if !defined(_M_ARM64)
      TEST_HOOK_FOR_INVALID_HANDLE_VALUE("kernel32.dll", CreateFileA) &&
#endif  // !defined(_M_ARM64)
#if !defined(_M_ARM64)
      TEST_HOOK("kernel32.dll", CloseHandle, Equals, FALSE) &&
      TEST_HOOK("kernel32.dll", DuplicateHandle, Equals, FALSE) &&
#endif  // !defined(_M_ARM64)
      TEST_DETOUR_SKIP_EXEC("kernel32.dll", BaseThreadInitThunk) &&
#if defined(_M_X64) || defined(_M_ARM64)
      TEST_HOOK("user32.dll", GetKeyState, Ignore, 0) &&  // see Bug 1316415
#endif
      TEST_HOOK("user32.dll", GetWindowInfo, Equals, FALSE) &&
#if defined(_M_IX86) || defined(_M_X64)
      // Bug 1997530: TrackPopupMenu uses a PC-relative branch on ARM64.
      TEST_HOOK("user32.dll", TrackPopupMenu, Equals, FALSE) &&
#endif
      TEST_DETOUR("user32.dll", CreateWindowExW, Equals, nullptr) &&
      TEST_HOOK("user32.dll", InSendMessageEx, Equals, ISMEX_NOSEND) &&
      TEST_HOOK("user32.dll", SendMessageTimeoutW, Equals, 0) &&
#if !defined(_M_ARM64)
      TEST_HOOK("user32.dll", SetCursorPos, NotEquals, FALSE) &&
#endif
      TEST_HOOK("advapi32.dll", RtlGenRandom, Equals, TRUE) &&
      TEST_HOOK_PARAMS("oleaut32.dll", VariantClear, Equals, S_OK, &var) &&
#if !defined(_M_ARM64)
      TEST_HOOK("imm32.dll", ImmGetContext, Equals, nullptr) &&
#endif  // !defined(_M_ARM64)
      TEST_HOOK("imm32.dll", ImmGetCompositionStringW, Ignore, 0) &&
      TEST_HOOK_SKIP_EXEC("imm32.dll", ImmSetCandidateWindow) &&
      TEST_HOOK("imm32.dll", ImmNotifyIME, Equals, 0) &&
      TEST_HOOK("msctf.dll", TF_Notify, Equals, 0) &&
      TEST_HOOK("comdlg32.dll", GetSaveFileNameW, Ignore, FALSE) &&
      TEST_HOOK("comdlg32.dll", GetOpenFileNameW, Ignore, FALSE) &&
#if defined(_M_X64)
      TEST_HOOK("comdlg32.dll", PrintDlgW, Ignore, 0) &&
#endif
      MAYBE_TEST_HOOK(ShouldTestTipTsf(), "tiptsf.dll", ProcessCaretEvents,
                      Ignore, nullptr) &&
      TEST_HOOK("wininet.dll", InternetOpenA, NotEquals, nullptr) &&
      TEST_HOOK("wininet.dll", InternetCloseHandle, Equals, FALSE) &&
      TEST_HOOK("wininet.dll", InternetConnectA, Equals, nullptr) &&
      TEST_HOOK("wininet.dll", InternetQueryDataAvailable, Equals, FALSE) &&
      TEST_HOOK("wininet.dll", InternetReadFile, Equals, FALSE) &&
      TEST_HOOK("wininet.dll", InternetWriteFile, Equals, FALSE) &&
      TEST_HOOK("wininet.dll", InternetSetOptionA, Equals, FALSE) &&
      TEST_HOOK("wininet.dll", HttpAddRequestHeadersA, Equals, FALSE) &&
      TEST_HOOK("wininet.dll", HttpOpenRequestA, Equals, nullptr) &&
      TEST_HOOK("wininet.dll", HttpQueryInfoA, Equals, FALSE) &&
      TEST_HOOK("wininet.dll", HttpSendRequestA, Equals, FALSE) &&
      TEST_HOOK("wininet.dll", HttpSendRequestExA, Equals, FALSE) &&
      TEST_HOOK("wininet.dll", HttpEndRequestA, Equals, FALSE) &&
      TEST_HOOK("wininet.dll", InternetQueryOptionA, Equals, FALSE) &&
      TEST_HOOK("sspicli.dll", AcquireCredentialsHandleA, NotEquals,
                SEC_E_OK) &&
      TEST_HOOK_PARAMS("sspicli.dll", QueryCredentialsAttributesA, Equals,
                       SEC_E_INVALID_HANDLE, &credHandle, 0, nullptr) &&
      TEST_HOOK_PARAMS("sspicli.dll", FreeCredentialsHandle, Equals,
                       SEC_E_INVALID_HANDLE, &credHandle) &&
#if defined(_M_X64) && !defined(MOZ_CODE_COVERAGE)
      TestDetouredCallUnwindInfo() &&
#endif  // defined(_M_X64) && !defined(MOZ_CODE_COVERAGE)
// We disable these testcases because the code coverage instrumentation injects
// code in a way that WindowsDllInterceptor doesn't understand.
#ifndef MOZ_CODE_COVERAGE
#  if defined(_M_X64) || defined(_M_IX86)
      TestSpareBytesAfterDetour() &&
#    if defined(_M_X64)
      TestSpareBytesAfterDetourFor10BytePatch() &&
#    endif  // defined(_M_X64)
#  endif    // MOZ_CODE_COVERAGE
#endif      // defined(_M_X64) || defined(_M_IX86)
      // Run TestDynamicCodePolicy() at the end because the policy is
      // irreversible.
      TestDynamicCodePolicy()) {
    printf("TEST-PASS | WindowsDllInterceptor | all checks passed\n");

    LARGE_INTEGER end, freq;
    QueryPerformanceCounter(&end);

    QueryPerformanceFrequency(&freq);

    LARGE_INTEGER result;
    result.QuadPart = end.QuadPart - start.QuadPart;
    result.QuadPart *= 1000000;
    result.QuadPart /= freq.QuadPart;

    printf("Elapsed time: %lld microseconds\n", result.QuadPart);

    return 0;
  }

  return 1;
}
