/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

export var CrashTestUtils = {
  // These will be defined using ctypes APIs below.
  crash: null,
  dumpHasStream: null,
  dumpHasInstructionPointerMemory: null,
  dumpWin64CFITestSymbols: null,
  enablePHC: null,

  // Constants for crash()
  // Keep these in sync with nsTestCrasher.cpp!
  CRASH_INVALID_POINTER_DEREF: 0,
  CRASH_PURE_VIRTUAL_CALL: 1,
  CRASH_RUNTIMEABORT: 2,
  CRASH_OOM: 3,
  CRASH_MOZ_CRASH: 4,
  CRASH_ABORT: 5,
  CRASH_UNCAUGHT_EXCEPTION: 6,
  CRASH_X64CFI_NO_MANS_LAND: 7,
  CRASH_X64CFI_LAUNCHER: 8,
  CRASH_X64CFI_UNKNOWN_OPCODE: 9,
  CRASH_X64CFI_PUSH_NONVOL: 10,
  CRASH_X64CFI_ALLOC_SMALL: 11,
  CRASH_X64CFI_ALLOC_LARGE: 12,
  CRASH_X64CFI_SAVE_NONVOL: 15,
  CRASH_X64CFI_SAVE_NONVOL_FAR: 16,
  CRASH_X64CFI_SAVE_XMM128: 17,
  CRASH_X64CFI_SAVE_XMM128_FAR: 18,
  CRASH_X64CFI_EPILOG: 19,
  CRASH_X64CFI_EOF: 20,
  CRASH_PHC_USE_AFTER_FREE: 21,
  CRASH_PHC_DOUBLE_FREE: 22,
  CRASH_PHC_BOUNDS_VIOLATION: 23,
  CRASH_HEAP_CORRUPTION: 24,
  CRASH_EXC_GUARD: 25,
  CRASH_STACK_OVERFLOW: 26,
  CRASH_STL_VECTOR_OOB: 27,
  CRASH_SIGSYS: 28,

  // Constants for dumpHasStream()
  // From google_breakpad/common/minidump_format.h
  MD_THREAD_LIST_STREAM: 3,
  MD_MEMORY_INFO_LIST_STREAM: 16,
};

// Grab APIs from the testcrasher shared library
import { ctypes } from "moz-src:///toolkit/components/ctypes/ctypes.sys.mjs";

var dir = Services.dirsvc.get("CurWorkD", Ci.nsIFile);
var file = dir.clone();
file = file.parent;
file.append(ctypes.libraryName("testcrasher"));
var lib = ctypes.open(file.path);
CrashTestUtils.crash = lib.declare(
  "Crash",
  ctypes.default_abi,
  ctypes.void_t,
  ctypes.int16_t
);
CrashTestUtils.enablePHC = lib.declare(
  "EnablePHC",
  ctypes.default_abi,
  ctypes.void_t
);

try {
  CrashTestUtils.TryOverrideExceptionHandler = lib.declare(
    "TryOverrideExceptionHandler",
    ctypes.default_abi,
    ctypes.void_t
  );
} catch (ex) {}

CrashTestUtils.dumpHasStream = lib.declare(
  "DumpHasStream",
  ctypes.default_abi,
  ctypes.bool,
  ctypes.char.ptr,
  ctypes.uint32_t
);

CrashTestUtils.dumpHasInstructionPointerMemory = lib.declare(
  "DumpHasInstructionPointerMemory",
  ctypes.default_abi,
  ctypes.bool,
  ctypes.char.ptr
);

CrashTestUtils.getWin64CFITestFnAddrOffset = lib.declare(
  "GetWin64CFITestFnAddrOffset",
  ctypes.default_abi,
  ctypes.int32_t,
  ctypes.int16_t
);
