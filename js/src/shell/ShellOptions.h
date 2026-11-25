/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef shell_ShellOptions_h
#define shell_ShellOptions_h

#include "js/AllocPolicy.h"
#include "js/CompileOptions.h"
#include "js/TypeDecls.h"
#include "js/Vector.h"
#include "shell/jsoptparse.h"

// Not necessarily in sync with the browser
#ifdef ENABLE_SHARED_MEMORY
#  define SHARED_MEMORY_DEFAULT 1
#else
#  define SHARED_MEMORY_DEFAULT 0
#endif

namespace js {
namespace shell {

// The shell's command line option parser. Populated by InitOptionParser and
// queried throughout the shell.
extern cli::OptionParser gOptions;

// Shell state set once at startup.
extern const char* selfHostedXDRPath;
extern bool reportWarnings;
extern bool compileOnly;
extern bool printTiming;
extern bool enableDisassemblyDumps;
extern bool encodeSelfHostedCode;
extern JS::DelazificationOption defaultDelazificationMode;
extern bool enableSharedMemory;
extern bool enableToSource;
#ifdef DEBUG
extern bool dumpEntrainedVariables;
extern bool OOM_printAllocationCount;
#endif

#ifndef __wasi__
// Flags to pass on to the separate process used for wasm compilation.
extern Vector<const char*, 5, js::SystemAllocPolicy> sCompilerProcessFlags;
#endif

// Define all the shell's command line options.
bool InitOptionParser();

bool ParseOptions(int argc, char** argv, int* failureCodeOut);

// Set up global state from options. These must be called after the command
// line has been parsed, before and after JS_Init respectively.
bool SetGlobalOptionsPreJSInit();
bool SetGlobalOptionsPostJSInit();

bool SetMainThreadContextOptions(JSContext* cx);
bool SetWorkerContextOptions(JSContext* cx);
bool ApplyBenchmarkMode(JSContext* cx);

}  // namespace shell
}  // namespace js

#endif  // shell_ShellOptions_h
