/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* JS shell command line option parsing and processing. */

#include "shell/ShellOptions.h"

#include "mozilla/Maybe.h"

#ifdef XP_LINUX
#  include <sys/prctl.h>
#endif

#ifndef JS_WITHOUT_NSPR
#  include "prerror.h"
#  include "prlink.h"
#endif

#include "jsapi.h"

#include "gc/GC.h"
#include "jit/Ion.h"  // js::jit::CPUFlagsHaveBeenComputed
#include "jit/JitOptions.h"
#include "jit/Simulator.h"
#include "js/experimental/CodeCoverage.h"  // js::EnableCodeCoverage
#include "shell/jsshell.h"
#include "vm/JSContext.h"

using namespace js;
using namespace js::cli;
using namespace js::shell;

enum class BenchmarkMode { Off, Enable, Strict };
static BenchmarkMode sBenchmarkMode = BenchmarkMode::Off;

MOZ_RUNINIT OptionParser
    shell::gOptions("Usage: {progname} [options] [[script] scriptArgs*]");

const char* shell::selfHostedXDRPath = nullptr;
bool shell::reportWarnings = true;
bool shell::compileOnly = false;
bool shell::printTiming = false;
bool shell::enableDisassemblyDumps = false;
bool shell::encodeSelfHostedCode = false;
JS::DelazificationOption shell::defaultDelazificationMode =
    JS::DelazificationOption::OnDemandOnly;
bool shell::enableSharedMemory = SHARED_MEMORY_DEFAULT;
bool shell::enableToSource = false;
#ifdef DEBUG
bool shell::dumpEntrainedVariables = false;
bool shell::OOM_printAllocationCount = false;
#endif

#ifndef __wasi__
MOZ_RUNINIT Vector<const char*, 5, SystemAllocPolicy>
    shell::sCompilerProcessFlags;
#endif

bool shell::InitOptionParser() {
  gOptions.setDescription(
      "The SpiderMonkey shell provides a command line interface to the "
      "JavaScript engine. Code and file options provided via the command line "
      "are "
      "run left to right. If provided, the optional script argument is run "
      "after "
      "all options have been processed. Just-In-Time compilation modes may be "
      "enabled via "
      "command line options.");
  gOptions.setDescriptionWidth(72);
  gOptions.setHelpWidth(80);
  gOptions.setVersion(JS_GetImplementationVersion());

  if (!gOptions.addMultiStringOption(
          'f', "file", "PATH",
          "File path to run, parsing file contents as UTF-8") ||
      !gOptions.addMultiStringOption(
          'u', "utf16-file", "PATH",
          "File path to run, inflating the file's UTF-8 contents to UTF-16 and "
          "then parsing that") ||
      !gOptions.addMultiStringOption('m', "module", "PATH",
                                     "Module path to run") ||
      !gOptions.addMultiStringOption('p', "prelude", "PATH",
                                     "Prelude path to run") ||
      !gOptions.addMultiStringOption('e', "execute", "CODE",
                                     "Inline code to run") ||
      !gOptions.addStringOption(
          '\0', "selfhosted-xdr-path", "[filename]",
          "Read/Write selfhosted script data from/to the given "
          "XDR file") ||
      !gOptions.addStringOption(
          '\0', "selfhosted-xdr-mode", "(encode,decode,off)",
          "Whether to encode/decode data of the file provided"
          "with --selfhosted-xdr-path.") ||
      !gOptions.addBoolOption('i', "shell",
                              "Enter prompt after running code") ||
      !gOptions.addBoolOption(
          'c', "compileonly",
          "Only compile, don't run (syntax checking mode)") ||
      !gOptions.addBoolOption('w', "warnings", "Emit warnings") ||
      !gOptions.addBoolOption('W', "nowarnings", "Don't emit warnings") ||
      !gOptions.addBoolOption(
          'D', "dump-bytecode",
          "Dump bytecode with exec count for all scripts") ||
      !gOptions.addBoolOption(
          'b', "print-timing",
          "Print sub-ms runtime for each file that's run") ||
      !gOptions.addBoolOption('\0', "code-coverage",
                              "Enable code coverage instrumentation.") ||
      !gOptions.addBoolOption(
          '\0', "disable-parser-deferred-alloc",
          "Disable deferred allocation of GC objects until after parser") ||
#ifdef DEBUG
      !gOptions.addBoolOption('O', "print-alloc",
                              "Print the number of allocations at exit") ||
#endif
      !gOptions.addOptionalStringArg(
          "script", "A script to execute (after all options)") ||
      !gOptions.addOptionalMultiStringArg(
          "scriptArgs",
          "String arguments to bind as |scriptArgs| in the "
          "shell's global") ||
      !gOptions.addIntOption(
          '\0', "cpu-count", "COUNT",
          "Set the number of CPUs (hardware threads) to COUNT, the "
          "default is the actual number of CPUs. The total number of "
          "background helper threads is the CPU count plus some constant.",
          -1) ||
      !gOptions.addIntOption('\0', "thread-count", "COUNT",
                             "Alias for --cpu-count.", -1) ||
      !gOptions.addBoolOption('\0', "ion", "Enable IonMonkey (default)") ||
      !gOptions.addBoolOption('\0', "no-ion", "Disable IonMonkey") ||
      !gOptions.addBoolOption('\0', "no-ion-for-main-context",
                              "Disable IonMonkey for the main context only") ||
      !gOptions.addIntOption(
          '\0', "inlining-entry-threshold", "COUNT",
          "The minimum stub entry count before trial-inlining a"
          " call",
          -1) ||
      !gOptions.addIntOption(
          '\0', "small-function-length", "COUNT",
          "The maximum bytecode length of a 'small function' for "
          "the purpose of inlining.",
          -1) ||
      !gOptions.addBoolOption('\0', "only-inline-selfhosted",
                              "Only inline selfhosted functions") ||
      !gOptions.addStringOption(
          '\0', "wasm-compiler", "[option]",
          "Choose to enable a subset of the wasm compilers, valid options are "
          "'none', 'baseline', 'ion', 'optimizing', "
          "'baseline+ion', 'baseline+optimizing'.") ||
      !gOptions.addBoolOption('\0', "wasm-verbose",
                              "Enable WebAssembly verbose logging") ||
      !gOptions.addBoolOption('\0', "disable-wasm-huge-memory",
                              "Disable WebAssembly huge memory") ||
      !gOptions.addBoolOption('\0', "test-wasm-await-tier2",
                              "Forcibly activate tiering and block "
                              "instantiation on completion of tier2") ||
      !gOptions.addBoolOption(
          '\0', "disable-main-thread-wasm-denormals",
          "Disable denormals (FTZ+DAZ) for wasm execution on "
          "the main thread, to emulate WebAudio worklets") ||
      !gOptions.addBoolOption('\0', "no-native-regexp",
                              "Disable native regexp compilation") ||
      !gOptions.addIntOption(
          '\0', "regexp-warmup-threshold", "COUNT",
          "Wait for COUNT invocations before compiling regexps to native code "
          "(default 10)",
          -1) ||
      !gOptions.addBoolOption('\0', "trace-regexp-parser",
                              "Trace regexp parsing") ||
      !gOptions.addBoolOption('\0', "trace-regexp-assembler",
                              "Trace regexp assembler") ||
      !gOptions.addBoolOption('\0', "trace-regexp-compiler",
                              "Trace regexp compiler") ||
      !gOptions.addBoolOption('\0', "trace-regexp-graph-building",
                              "Trace regexp graph building") ||
      !gOptions.addBoolOption('\0', "trace-regexp-interpreter",
                              "Trace regexp interpreter") ||
      !gOptions.addBoolOption('\0', "trace-regexp-peephole",
                              "Trace regexp peephole optimization") ||
      !gOptions.addBoolOption('\0', "less-debug-code",
                              "Emit less machine code for "
                              "checking assertions under DEBUG.") ||
      !gOptions.addBoolOption('\0', "disable-tosource",
                              "Disable toSource/uneval") ||
      !gOptions.addBoolOption(
          '\0', "disable-property-error-message-fix",
          "Disable fix for the error message when accessing "
          "property of null or undefined") ||
      !gOptions.addBoolOption('\0', "enable-iterator-helpers",
                              "Enable iterator helpers") ||
      !gOptions.addBoolOption('\0', "enable-async-iterator-helpers",
                              "Enable async iterator helpers") ||
      !gOptions.addBoolOption('\0', "disable-array-grouping",
                              "Disable Object.groupBy and Map.groupBy") ||
      !gOptions.addBoolOption('\0', "enable-symbols-as-weakmap-keys",
                              "Enable Symbols As WeakMap keys") ||
      !gOptions.addBoolOption('\0', "no-symbols-as-weakmap-keys",
                              "Disable Symbols As WeakMap keys") ||
      !gOptions.addBoolOption('\0', "enable-top-level-await",
                              "Enable top-level await") ||
      !gOptions.addStringOption('\0', "shared-memory", "on/off",
                                "SharedArrayBuffer and Atomics "
#if SHARED_MEMORY_DEFAULT
                                "(default: on, off to disable)"
#else
                                "(default: off, on to enable)"
#endif
                                ) ||
      !gOptions.addStringOption(
          '\0', "spectre-mitigations", "on/off",
          "Whether Spectre mitigations are enabled (default: "
          "off, on to enable)") ||
      !gOptions.addStringOption(
          '\0', "write-protect-code", "on/off",
          "Whether the W^X policy is enforced to mark JIT code "
          "pages as either writable or executable but never "
          "both at the same time (default: on, off to "
          "disable)") ||
      !gOptions.addStringOption(
          '\0', "cache-ir-stubs", "on/off/call",
          "Use CacheIR stubs (default: on, off to disable, "
          "call to enable work-in-progress call ICs)") ||
      !gOptions.addStringOption(
          '\0', "ion-shared-stubs", "on/off",
          "Use shared stubs (default: on, off to disable)") ||
      !gOptions.addStringOption(
          '\0', "stub-folding", "on/off",
          "Enable stub folding (default: on, off to disable)") ||
      !gOptions.addStringOption(
          '\0', "stub-folding-loads-and-stores", "on/off",
          "Enable stub folding for load and stores (default: "
          "on, off to disable)") ||
      !gOptions.addStringOption(
          '\0', "ion-scalar-replacement", "on/off",
          "Scalar Replacement (default: on, off to disable)") ||
      !gOptions.addStringOption('\0', "ion-gvn", "[mode]",
                                "Specify Ion global value numbering:\n"
                                "  off: disable GVN\n"
                                "  on:  enable GVN (default)\n") ||
      !gOptions.addStringOption(
          '\0', "ion-licm", "on/off",
          "Loop invariant code motion (default: on, off to disable)") ||
      !gOptions.addStringOption('\0', "ion-edgecase-analysis", "on/off",
                                "Find edge cases where Ion can avoid bailouts "
                                "(default: on, off to disable)") ||
      !gOptions.addStringOption(
          '\0', "ion-pruning", "on/off",
          "Branch pruning (default: on, off to disable)") ||
      !gOptions.addStringOption(
          '\0', "ion-range-analysis", "on/off",
          "Range analysis (default: on, off to disable)") ||
      !gOptions.addStringOption(
          '\0', "ion-instruction-reordering", "on/off",
          "Instruction reordering (default: off, on to enable)") ||
      !gOptions.addStringOption(
          '\0', "ion-optimize-shapeguards", "on/off",
          "Eliminate redundant shape guards (default: on, off to disable)") ||
      !gOptions.addStringOption(
          '\0', "ion-optimize-gcbarriers", "on/off",
          "Eliminate redundant GC barriers (default: on, off to disable)") ||
      !gOptions.addStringOption('\0', "ion-iterator-indices", "on/off",
                                "Optimize property access in for-in loops "
                                "(default: on, off to disable)") ||
      !gOptions.addStringOption('\0', "ion-load-keys", "on/off",
                                "Atomize property loads used as keys "
                                "(default: on, off to disable)") ||
      !gOptions.addBoolOption('\0', "ion-check-range-analysis",
                              "Range analysis checking") ||
      !gOptions.addBoolOption('\0', "ion-extra-checks",
                              "Perform extra dynamic validation checks") ||
      !gOptions.addStringOption(
          '\0', "ion-inlining", "on/off",
          "Inline methods where possible (default: on, off to disable)") ||
      !gOptions.addStringOption(
          '\0', "ion-osr", "on/off",
          "On-Stack Replacement (default: on, off to disable)") ||
      !gOptions.addBoolOption('\0', "disable-bailout-loop-check",
                              "Turn off bailout loop check") ||
      !gOptions.addStringOption(
          '\0', "canonicalize-nan-at-uses", "on/off",
          "Canonicalize NaN values at uses (default: on, off to disable)") ||
      !gOptions.addBoolOption(
          '\0', "enable-stub-frame-pointers",
          "Use frame pointers in baseline IC stubs and other "
          "trampolines. No-op on x86.") ||
      !gOptions.addBoolOption(
          '\0', "scalar-replace-arguments",
          "Use scalar replacement to optimize ArgumentsObject") ||
      !gOptions.addStringOption(
          '\0', "ion-limit-script-size", "on/off",
          "Don't compile very large scripts (default: on, off to disable)") ||
      !gOptions.addIntOption(
          '\0', "ion-warmup-threshold", "COUNT",
          "Wait for COUNT calls or iterations before compiling "
          "at the normal optimization level (default: 1500)",
          -1) ||
      !gOptions.addStringOption(
          '\0', "ion-regalloc", "[mode]",
          "Specify Ion register allocation:\n"
          "  backtracking: Priority based backtracking register allocation "
          "(default)\n"
          "  simple: Simple register allocator optimized for compile time") ||
      !gOptions.addBoolOption(
          '\0', "ion-eager",
          "Always ion-compile methods (implies --baseline-eager)") ||
      !gOptions.addBoolOption('\0', "fast-warmup",
                              "Reduce warmup thresholds for each tier.") ||
      !gOptions.addStringOption(
          '\0', "baseline-offthread-compile", "on/off",
          "Compile baseline scripts offthread (default: off)") ||
      !gOptions.addStringOption(
          '\0', "baseline-batching", "on/off",
          "Batch baseline scripts before dispatching (default: off)") ||
      !gOptions.addStringOption(
          '\0', "ion-offthread-compile", "on/off",
          "Compile Ion scripts offthread (default: on)") ||
      !gOptions.addStringOption('\0', "ion-parallel-compile", "on/off",
                                "--ion-parallel compile is deprecated. Use "
                                "--ion-offthread-compile.") ||
      !gOptions.addStringOption('\0', "object-keys-scalar-replacement",
                                "on/off",
                                "Replace Object.keys with a NativeIterators "
                                "(default: on)") ||
      !gOptions.addBoolOption('\0', "baseline",
                              "Enable baseline compiler (default)") ||
      !gOptions.addBoolOption('\0', "no-baseline",
                              "Disable baseline compiler") ||
      !gOptions.addBoolOption('\0', "baseline-eager",
                              "Always baseline-compile methods") ||
#ifdef ENABLE_PORTABLE_BASELINE_INTERP
      !gOptions.addBoolOption('\0', "portable-baseline-eager",
                              "Always use the portable baseline interpreter") ||
      !gOptions.addBoolOption(
          '\0', "portable-baseline",
          "Enable Portable Baseline Interpreter (default)") ||
      !gOptions.addBoolOption('\0', "no-portable-baseline",
                              "Disable Portable Baseline Interpreter") ||
#endif
#ifdef ENABLE_JS_AOT_ICS
      !gOptions.addBoolOption('\0', "aot-ics",
                              "Enable ahead-of-time-known ICs") ||
      !gOptions.addBoolOption(
          '\0', "enforce-aot-ics",
          "Enable enforcing only use of ahead-of-time-known ICs") ||
#endif
      !gOptions.addIntOption(
          '\0', "baseline-warmup-threshold", "COUNT",
          "Wait for COUNT calls or iterations before baseline-compiling "
          "(default: 10)",
          -1) ||
      !gOptions.addIntOption(
          '\0', "baseline-queue-capacity", "CAPACITY",
          "Wait for at most CAPACITY scripts to be added to baseline compile"
          "queue before dispatching (default: 8)",
          -1) ||
      !gOptions.addBoolOption('\0', "blinterp",
                              "Enable Baseline Interpreter (default)") ||
      !gOptions.addBoolOption('\0', "no-blinterp",
                              "Disable Baseline Interpreter") ||
      !gOptions.addBoolOption(
          '\0', "disable-jithints",
          "Disable caching eager baseline compilation hints.") ||
      !gOptions.addBoolOption(
          '\0', "emit-interpreter-entry",
          "Emit Interpreter entry trampolines (default under --enable-perf)") ||
      !gOptions.addBoolOption(
          '\0', "no-emit-interpreter-entry",
          "Do not emit Interpreter entry trampolines (default).") ||
      !gOptions.addBoolOption('\0', "blinterp-eager",
                              "Always Baseline-interpret scripts") ||
      !gOptions.addIntOption(
          '\0', "blinterp-warmup-threshold", "COUNT",
          "Wait for COUNT calls or iterations before Baseline-interpreting "
          "(default: 10)",
          -1) ||
      !gOptions.addIntOption(
          '\0', "trial-inlining-warmup-threshold", "COUNT",
          "Wait for COUNT calls or iterations before trial-inlining "
          "(default: 500)",
          -1) ||
      !gOptions.addStringOption(
          '\0', "monomorphic-inlining", "default/always/never",
          "Whether monomorphic inlining is used instead of trial inlining "
          "always, never, or based on heuristics (default)") ||
      !gOptions.addBoolOption(
          '\0', "no-sse3",
          "Pretend CPU does not support SSE3 instructions and above "
          "to test JIT codegen (no-op on platforms other than x86 and x64).") ||
      !gOptions.addBoolOption(
          '\0', "no-ssse3",
          "Pretend CPU does not support SSSE3 [sic] instructions and above "
          "to test JIT codegen (no-op on platforms other than x86 and x64).") ||
      !gOptions.addBoolOption(
          '\0', "no-sse41",
          "Pretend CPU does not support SSE4.1 instructions "
          "to test JIT codegen (no-op on platforms other than x86 and x64).") ||
      !gOptions.addBoolOption('\0', "no-sse4", "Alias for --no-sse41") ||
      !gOptions.addBoolOption(
          '\0', "no-sse42",
          "Pretend CPU does not support SSE4.2 instructions "
          "to test JIT codegen (no-op on platforms other than x86 and x64).") ||
#ifdef ENABLE_JIT_AVX
      !gOptions.addBoolOption(
          '\0', "enable-avx",
          "No-op. AVX is enabled by default, if available.") ||
      !gOptions.addBoolOption(
          '\0', "no-avx",
          "Pretend CPU does not support AVX or AVX2 instructions "
          "to test JIT codegen (no-op on platforms other than x86 and x64).") ||
#else
      !gOptions.addBoolOption('\0', "enable-avx",
                              "AVX is disabled by default. Enable AVX. "
                              "(no-op on platforms other than x86 and x64).") ||
      !gOptions.addBoolOption('\0', "no-avx",
                              "No-op. AVX is currently disabled by default.") ||
#endif
      !gOptions.addBoolOption(
          '\0', "no-fjcvtzs",
          "Pretend CPU does not support FJCVTZS instruction.") ||
      !gOptions.addBoolOption('\0', "no-cssc",
                              "Pretend CPU does not support CSSC extension.") ||
      !gOptions.addBoolOption('\0', "more-compartments",
                              "Make newGlobal default to creating a new "
                              "compartment.") ||
      !gOptions.addBoolOption('\0', "benchmark-mode",
                              "Configure the shell for realistic benchmarking. "
                              "Prints warnings for detected issues.") ||
      !gOptions.addBoolOption('\0', "strict-benchmark-mode",
                              "Like --benchmark-mode but refuses to run if the "
                              "build config is wrong or conflicting settings "
                              "exist.") ||
      !gOptions.addBoolOption('\0', "fuzzing-safe",
                              "Don't expose functions that aren't safe for "
                              "fuzzers to call") ||
#ifdef DEBUG
      !gOptions.addBoolOption('\0', "differential-testing",
                              "Avoid random/undefined behavior that disturbs "
                              "differential testing (correctness fuzzing)") ||
#endif
      !gOptions.addBoolOption('\0', "disable-oom-functions",
                              "Disable functions that cause "
                              "artificial OOMs") ||
      !gOptions.addBoolOption('\0', "no-threads", "Disable helper threads") ||
      !gOptions.addBoolOption(
          '\0', "no-jit-backend",
          "Disable the JIT backend completely for this process") ||
#ifdef DEBUG
      !gOptions.addBoolOption('\0', "dump-entrained-variables",
                              "Print variables which are "
                              "unnecessarily entrained by inner functions") ||
#endif
      !gOptions.addBoolOption('\0', "no-ggc", "Disable Generational GC") ||
      !gOptions.addBoolOption('\0', "no-cgc", "Disable Compacting GC") ||
      !gOptions.addBoolOption('\0', "no-incremental-gc",
                              "Disable Incremental GC") ||
      !gOptions.addBoolOption('\0', "no-parallel-marking",
                              "Disable GC parallel marking") ||
      !gOptions.addBoolOption('\0', "enable-parallel-marking",
                              "Enable GC parallel marking") ||
#ifdef JS_GC_CONCURRENT_MARKING
      !gOptions.addBoolOption('\0', "no-concurrent-marking",
                              "Disable GC concurrent marking") ||
      !gOptions.addBoolOption('\0', "enable-concurrent-marking",
                              "Enable GC concurrent marking") ||
#endif
      !gOptions.addStringOption('\0', "nursery-strings", "on/off",
                                "Allocate strings in the nursery") ||
      !gOptions.addStringOption('\0', "nursery-bigints", "on/off",
                                "Allocate BigInts in the nursery") ||
      !gOptions.addIntOption(
          '\0', "available-memory", "SIZE",
          "Select GC settings based on available memory (MB)", 0) ||
      !gOptions.addBoolOption('\0', "disable-decommit",
                              "Disable decommitting unsued GC memory") ||
      !gOptions.addStringOption(
          '\0', "arm-hwcap", "[features]",
          "Specify ARM code generation features, or 'help' to "
          "list all features.") ||
      !gOptions.addIntOption(
          '\0', "arm-asm-nop-fill", "SIZE",
          "Insert the given number of NOP instructions at all "
          "possible pool locations.",
          0) ||
      !gOptions.addIntOption('\0', "asm-pool-max-offset", "OFFSET",
                             "The maximum pc relative OFFSET permitted in pool "
                             "reference instructions.",
                             1024) ||
      !gOptions.addBoolOption('\0', "arm-sim-icache-checks",
                              "Enable icache flush checks in the ARM "
                              "simulator.") ||
      !gOptions.addIntOption('\0', "arm-sim-stop-at", "NUMBER",
                             "Stop the ARM simulator after the given "
                             "NUMBER of instructions.",
                             -1) ||
      !gOptions.addBoolOption('\0', "mips-sim-icache-checks",
                              "Enable icache flush checks in the MIPS "
                              "simulator.") ||
      !gOptions.addIntOption('\0', "mips-sim-stop-at", "NUMBER",
                             "Stop the MIPS simulator after the given "
                             "NUMBER of instructions.",
                             -1) ||
#ifdef JS_CODEGEN_LOONG64
      !gOptions.addStringOption(
          '\0', "loong64-isa", "[isa]",
          "Specify the LoongArch code generation ISA (\"la64v1.0\" or "
          "\"la64v1.1\").") ||
#endif
#if defined(JS_SIMULATOR_LOONG64)
      !gOptions.addBoolOption('\0', "loong64-sim-icache-checks",
                              "Enable icache flush checks in the LoongArch64 "
                              "simulator.") ||
      !gOptions.addIntOption('\0', "loong64-sim-stop-at", "NUMBER",
                             "Stop the LoongArch64 simulator after the given "
                             "NUMBER of instructions.",
                             -1) ||
#endif
      // Like arm-hwcap, accepted by every shell so that jit-test variants can
      // pass it; only riscv64 builds read it.
      !gOptions.addStringOption(
          '\0', "riscv-ext", "[features]",
          "Specify RISCV code generation features. Starts with ISA (\"rv64g\") "
          "or profile (\"rva20u64\", \"rva22u64\", \"rva23u64\"), followed by "
          "extensions separated with '_'.") ||
#ifdef JS_CODEGEN_RISCV64
      !gOptions.addBoolOption('\0', "riscv-debug",
                              "Print riscv debugging messages.") ||
#endif
#ifdef JS_SIMULATOR_RISCV64
      !gOptions.addBoolOption('\0', "riscv-sim-trace",
                              "Print the RISC-V simulator info.") ||
      !gOptions.addBoolOption('\0', "riscv-sim-debug",
                              "Debug the RISC-V simulator.") ||
      !gOptions.addBoolOption('\0', "riscv-trap-to-simulator-debugger",
                              "Trap into the RISC-V simulator debuggger.") ||
      !gOptions.addBoolOption('\0', "riscv-sim-icache-checks",
                              "Enable icache flush checks in the RISC-V "
                              "simulator.") ||
      !gOptions.addIntOption('\0', "riscv-sim-stop-at", "NUMBER",
                             "Stop the RISC-V simulator after the given "
                             "NUMBER of instructions.",
                             -1) ||
#endif
      !gOptions.addIntOption('\0', "nursery-size", "SIZE-MB",
                             "Set the maximum nursery size in MB",
                             JS::DefaultNurseryMaxBytes / 1024 / 1024) ||
#ifdef JS_GC_ZEAL
      !gOptions.addStringOption('z', "gc-zeal", "LEVEL(;LEVEL)*[,N]",
                                gc::ZealModeHelpText) ||
#else
      !gOptions.addStringOption('z', "gc-zeal", "LEVEL(;LEVEL)*[,N]",
                                "option ignored in non-gc-zeal builds") ||
#endif
      !gOptions.addMultiStringOption('\0', "gc-param", "NAME=VALUE",
                                     "Set a named GC parameter") ||
      !gOptions.addStringOption('\0', "module-load-path", "DIR",
                                "Set directory to load modules from") ||
      !gOptions.addBoolOption('\0', "no-source-pragmas",
                              "Disable source(Mapping)URL pragma parsing") ||
      !gOptions.addBoolOption('\0', "no-async-stacks",
                              "Disable async stacks") ||
      !gOptions.addBoolOption('\0', "async-stacks-capture-debuggee-only",
                              "Limit async stack capture to only debuggees") ||
      !gOptions.addMultiStringOption('\0', "dll", "LIBRARY",
                                     "Dynamically load LIBRARY") ||
      !gOptions.addBoolOption('\0', "suppress-minidump",
                              "Suppress crash minidumps") ||
      !gOptions.addStringOption(
          '\0', "delazification-mode", "[option]",
          "Select one of the delazification mode for scripts given on the "
          "command line, valid options are: "
          "'on-demand', 'concurrent-df', 'eager', 'concurrent-df+on-demand'. "
          "Choosing 'concurrent-df+on-demand' will run both concurrent-df and "
          "on-demand delazification mode, and compare compilation outcome. ") ||
      !gOptions.addBoolOption(
          '\0', "wasm-compile-and-serialize",
          "Compile the wasm bytecode from stdin and serialize "
          "the results to stdout") ||
#ifdef FUZZING_JS_FUZZILLI
      !gOptions.addBoolOption('\0', "reprl", "Enable REPRL mode for fuzzing") ||
#endif
      !gOptions.addMultiStringOption(
          'P', "setpref", "name[=val]",
          "Set the value of a JS pref. The value may "
          "be omitted for boolean prefs, in which case "
          "they default to true. Use --list-prefs "
          "to print all pref names.") ||
      !gOptions.addBoolOption(
          '\0', "list-prefs",
          "Print list of prefs that can be set with --setpref.") ||
      !gOptions.addBoolOption(
          '\0', "use-fdlibm-for-sin-cos-tan",
          "Use fdlibm for Math.sin, Math.cos, and Math.tan") ||
      !gOptions.addBoolOption('\0', "wasm-gc",
                              "Enable WebAssembly gc proposal.") ||
      !gOptions.addBoolOption('\0', "wasm-relaxed-simd",
                              "Enable WebAssembly relaxed-simd proposal.") ||
      !gOptions.addBoolOption('\0', "wasm-multi-memory",
                              "Enable WebAssembly multi-memory proposal.") ||
      !gOptions.addBoolOption('\0', "wasm-memory-control",
                              "Enable WebAssembly memory-control proposal.") ||
      !gOptions.addBoolOption('\0', "wasm-memory64",
                              "Enable WebAssembly memory64 proposal.") ||
      !gOptions.addBoolOption('\0', "wasm-tail-calls",
                              "Enable WebAssembly tail-calls proposal.") ||
      !gOptions.addBoolOption(
          '\0', "wasm-js-string-builtins",
          "Enable WebAssembly js-string-builtins proposal.") ||
      !gOptions.addBoolOption('\0', "enable-iterator-sequencing",
                              "Enable Iterator Sequencing") ||
      !gOptions.addBoolOption('\0', "enable-error-iserror",
                              "Enable Error.isError") ||
      !gOptions.addBoolOption('\0', "enable-error-stack-trace-limit",
                              "Enable Error.stackTraceLimit") ||
      !gOptions.addBoolOption('\0', "enable-iterator-range",
                              "Enable Iterator.range") ||
      !gOptions.addBoolOption('\0', "enable-joint-iteration",
                              "Enable Joint Iteration") ||
      !gOptions.addBoolOption('\0', "enable-atomics-pause",
                              "Enable Atomics pause") ||
      !gOptions.addBoolOption('\0', "enable-import-bytes",
                              "Enable import bytes") ||
      !gOptions.addBoolOption('\0', "enable-export-star-default",
                              "Include default in export * declarations") ||
      !gOptions.addBoolOption('\0', "enable-import-text",
                              "Enable import text") ||
      !gOptions.addBoolOption('\0', "enable-promise-allkeyed",
                              "Enable Promise.allKeyed") ||
      !gOptions.addBoolOption('\0', "enable-arraybuffer-immutable",
                              "Enable immutable ArrayBuffers") ||
      !gOptions.addBoolOption('\0', "enable-iterator-chunking",
                              "Enable Iterator Chunking") ||
      !gOptions.addBoolOption('\0', "enable-iterator-join",
                              "Enable Iterator.join") ||
      !gOptions.addBoolOption('\0', "enable-iterator-includes",
                              "Enable Iterator.prototype.includes") ||
      !gOptions.addBoolOption('\0', "enable-intl-locale-info",
                              "Enable Intl Locale Info") ||
      !gOptions.addBoolOption('\0', "enable-source-phase-imports",
                              "Enable source phase imports") ||
      !gOptions.addBoolOption(
          '\0', "enable-source-phase-imports-test262-module-source",
          "Support <module source> specifier for test262 tests") ||
      !gOptions.addBoolOption('\0', "enable-legacy-regexp",
                              "Enable Legacy RegExp features") ||
      !gOptions.addBoolOption('\0', "enable-regexp-buffer-boundaries",
                              "Enable RegExp Buffer Boundaries") ||
      !gOptions.addBoolOption('\0', "enable-wasm-esm-integration",
                              "Enable wasm/esm integration") ||
      !gOptions.addBoolOption('\0', "enable-defer-import-eval",
                              "Enable Deferred Import Evaluation")) {
    return false;
  }

  gOptions.setArgTerminatesOptions("script", true);
  gOptions.setArgCapturesRest("scriptArgs");

  // If --fuzzing-safe is used, print a warning for unknown shell flags instead
  // of aborting execution.
  gOptions.setIgnoresUnknownOptions("fuzzing-safe", true);

  return true;
}

bool shell::ParseOptions(int argc, char** argv, int* failureCodeOut) {
  MOZ_ASSERT(failureCodeOut);

  switch (gOptions.parseArgs(argc, argv)) {
    case OptionParser::Okay:
      return true;
    case OptionParser::EarlyExit:
      *failureCodeOut = EXIT_SUCCESS;
      return false;
    case OptionParser::ParseError:
      gOptions.printHelp(argv[0]);
      [[fallthrough]];
    default:
      *failureCodeOut = EXIT_FAILURE;
      return false;
  }
}

static bool SetGCParameterFromArg(JSContext* cx, const char* arg) {
  const char* c = strchr(arg, '=');
  if (!c) {
    fprintf(stderr,
            "Error: --gc-param argument '%s' must be of the form "
            "name=decimalValue\n",
            arg);
    return false;
  }

  UniqueChars nameCopy = DuplicateString(arg, c - arg);
  if (!nameCopy) {
    return false;
  }
  const char* name = nameCopy.get();
  const char* valueStr = c + 1;

  JSGCParamKey key;
  bool writable;
  if (!GetGCParameterInfo(name, &key, &writable)) {
    fprintf(stderr, "Error: Unknown GC parameter name '%s'\n", name);
    fprintf(stderr, "Writable GC parameter names are:\n");
#define PRINT_WRITABLE_PARAM_NAME(name, _, writable) \
  if (writable) {                                    \
    fprintf(stderr, "  %s\n", name);                 \
  }
    FOR_EACH_GC_PARAM(PRINT_WRITABLE_PARAM_NAME)
#undef PRINT_WRITABLE_PARAM_NAME
    return false;
  }

  if (!writable) {
    fprintf(stderr, "Error: GC parameter '%s' is not writable\n", name);
    return false;
  }

  char* end = nullptr;
  unsigned long int value = strtoul(valueStr, &end, 10);
  if (end == valueStr || *end) {
    fprintf(stderr,
            "Error: Could not parse '%s' as decimal for GC parameter '%s'\n",
            valueStr, name);
    return false;
  }

  // Some parameters are not fuzzing safe and so we silently skip changing them.
  if (fuzzingSafe && !IsGCParameterFuzzingSafe(key)) {
    return true;
  }

  // Some parameters can only be set on the main runtime so skip changing them
  // on workers.
  if (IsGCThreadParameter(key) && !cx->runtime()->isMainRuntime()) {
    return true;
  }

  uint32_t paramValue = uint32_t(value);
  if (value == ULONG_MAX || value != paramValue ||
      !cx->runtime()->gc.setParameter(cx, key, paramValue)) {
    fprintf(stderr, "Error: Value %s is out of range for GC parameter '%s'\n",
            valueStr, name);
    return false;
  }

  return true;
}

static bool SetGlobalJITOptions();
static bool SetContextWasmOptions(JSContext* cx);
static bool SetContextJITOptions(JSContext* cx);
static bool SetContextGCOptions(JSContext* cx);

template <typename T>
static bool ParsePrefValue(const char* name, const char* val, T* result) {
  if constexpr (std::is_same_v<T, bool>) {
    if (strcmp(val, "true") == 0) {
      *result = true;
      return true;
    }
    if (strcmp(val, "false") == 0) {
      *result = false;
      return true;
    }
    fprintf(stderr, "Invalid value for boolean pref %s: %s\n", name, val);
    return false;
  } else {
    static_assert(std::is_same_v<T, int32_t> || std::is_same_v<T, uint32_t>);
    char* end;
    long v = strtol(val, &end, 10);
    if (end != val + strlen(val) || static_cast<long>(static_cast<T>(v)) != v) {
      fprintf(stderr, "Invalid value for integer pref %s: %s\n", name, val);
      return false;
    }
    *result = static_cast<T>(v);
    return true;
  }
}

static bool SetPrefToTrueForBool(const char* name) {
  // Search for a matching pref and try to set it to a default value for the
  // type.
#define CHECK_PREF(NAME, CPP_NAME, TYPE, SETTER, IS_STARTUP_PREF,      \
                   FUZZING_SAFE)                                       \
  if (strcmp(name, NAME) == 0) {                                       \
    if constexpr (std::is_same_v<TYPE, bool>) {                        \
      JS::Prefs::SETTER(true);                                         \
      return true;                                                     \
    } else {                                                           \
      fprintf(stderr, "Pref %s must have a value specified.\n", name); \
      return false;                                                    \
    }                                                                  \
  }
  FOR_EACH_JS_PREF(CHECK_PREF)
#undef CHECK_PREF

  // Nothing matched. If --fuzzing-safe is used, return true after printing a
  // message, to continue execution without breaking fuzzing when a pref is
  // removed.
  if (fuzzingSafe) {
    fprintf(stderr, "Warning: Ignoring unknown pref name: %s\n", name);
    return true;
  }
  fprintf(stderr, "Invalid pref name: %s\n", name);
  return false;
}

static bool SetPrefToValue(const char* name, size_t nameLen,
                           const char* value) {
  // Search for a matching pref and try to set it to the provided value.
#define CHECK_PREF(NAME, CPP_NAME, TYPE, SETTER, IS_STARTUP_PREF,         \
                   FUZZING_SAFE)                                          \
  if (nameLen == strlen(NAME) && memcmp(name, NAME, strlen(NAME)) == 0) { \
    TYPE v;                                                               \
    if (!ParsePrefValue<TYPE>(NAME, value, &v)) {                         \
      return false;                                                       \
    }                                                                     \
    JS::Prefs::SETTER(v);                                                 \
    return true;                                                          \
  }
  FOR_EACH_JS_PREF(CHECK_PREF)
#undef CHECK_PREF

  // Nothing matched. If --fuzzing-safe is used, return true after printing a
  // message, to continue execution without breaking fuzzing when a pref is
  // removed.
  if (fuzzingSafe) {
    fprintf(stderr, "Warning: Ignoring unknown pref name: %s\n", name);
    return true;
  }
  fprintf(stderr, "Invalid pref name: %s\n", name);
  return false;
}

static bool SetPref(const char* pref) {
  const char* assign = strchr(pref, '=');
  if (!assign) {
    if (!SetPrefToTrueForBool(pref)) {
      return false;
    }
    return true;
  }

  size_t nameLen = assign - pref;
  const char* valStart = assign + 1;  // Skip '='.

  if (!SetPrefToValue(pref, nameLen, valStart)) {
    return false;
  }
  return true;
}

static void ListPrefs() {
  auto printPref = [](const char* name, auto defaultVal, bool fuzzingSafe) {
    const char* suffix = fuzzingSafe ? "" : "  [not fuzzing-safe]";
    using T = decltype(defaultVal);
    if constexpr (std::is_same_v<T, bool>) {
      fprintf(stderr, "%s=%s%s\n", name, defaultVal ? "true" : "false", suffix);
    } else if constexpr (std::is_same_v<T, int32_t>) {
      fprintf(stderr, "%s=%d%s\n", name, defaultVal, suffix);
    } else {
      static_assert(std::is_same_v<T, uint32_t>);
      fprintf(stderr, "%s=%u%s\n", name, defaultVal, suffix);
    }
  };

#define PRINT_PREF(NAME, CPP_NAME, TYPE, SETTER, IS_STARTUP_PREF, \
                   FUZZING_SAFE)                                  \
  printPref(NAME, JS::Prefs::CPP_NAME(), FUZZING_SAFE);
  FOR_EACH_JS_PREF(PRINT_PREF)
#undef PRINT_PREF
}

static bool OptionFailure(const char* option, const char* str) {
  fprintf(stderr, "Unrecognized option for %s: %s\n", option, str);
  return false;
}

bool shell::SetGlobalOptionsPreJSInit() {
  if (gOptions.getBoolOption("fuzzing-safe")) {
    fuzzingSafe = true;
  } else {
    fuzzingSafe =
        (getenv("MOZ_FUZZING_SAFE") && getenv("MOZ_FUZZING_SAFE")[0] != '0');
  }
  JS::Prefs::setFuzzingSafe(fuzzingSafe);
  JS::Prefs::setReportIgnoredFuzzingUnsafePrefs(true);

  if (gOptions.getBoolOption("strict-benchmark-mode")) {
    sBenchmarkMode = BenchmarkMode::Strict;
  } else if (gOptions.getBoolOption("benchmark-mode")) {
    sBenchmarkMode = BenchmarkMode::Enable;
  }

  for (MultiStringRange args = gOptions.getMultiStringOption("setpref");
       !args.empty(); args.popFront()) {
    if (!SetPref(args.front())) {
      return false;
    }
  }

#if defined(DEBUG) || defined(NIGHTLY_BUILD) || defined(JS_GC_ZEAL)
  if (sBenchmarkMode != BenchmarkMode::Off) {
    JS::Prefs::setAtStartup_extra_gc_poisoning(false);
  }
#endif

  // Override pref values for prefs that have a custom shell flag.
  // If you're adding a new feature, consider using --setpref instead.
  if (gOptions.getBoolOption("enable-atomics-pause")) {
    JS::Prefs::setAtStartup_experimental_atomics_pause(true);
  }
  if (gOptions.getBoolOption("enable-error-iserror")) {
    JS::Prefs::set_experimental_error_iserror(true);
  }

  bool symbolsAsWeakMapKeys = true;
  if (gOptions.getBoolOption("enable-symbols-as-weakmap-keys")) {
    symbolsAsWeakMapKeys = true;
  }
  if (gOptions.getBoolOption("no-symbols-as-weakmap-keys")) {
    symbolsAsWeakMapKeys = false;
  }
  JS::Prefs::setAtStartup_experimental_symbols_as_weakmap_keys(
      symbolsAsWeakMapKeys);
  if (gOptions.getBoolOption("enable-joint-iteration")) {
    JS::Prefs::setAtStartup_experimental_joint_iteration(true);
  }
  if (gOptions.getBoolOption("enable-legacy-regexp")) {
    JS::Prefs::setAtStartup_experimental_legacy_regexp(true);
  }
  if (gOptions.getBoolOption("enable-import-text")) {
    JS::Prefs::set_experimental_import_text(true);
  }
  if (gOptions.getBoolOption("enable-intl-locale-info")) {
    JS::Prefs::setAtStartup_experimental_intl_locale_info(true);
  }
  if (gOptions.getBoolOption("enable-iterator-includes")) {
    JS::Prefs::setAtStartup_experimental_iterator_includes(true);
  }
  if (gOptions.getBoolOption("enable-iterator-join")) {
    JS::Prefs::setAtStartup_experimental_iterator_join(true);
  }
  if (gOptions.getBoolOption("enable-iterator-chunking")) {
    JS::Prefs::setAtStartup_experimental_iterator_chunking(true);
  }
  if (gOptions.getBoolOption("enable-regexp-buffer-boundaries")) {
    JS::Prefs::setAtStartup_experimental_regexp_buffer_boundaries(true);
  }
#ifdef NIGHTLY_BUILD
  if (gOptions.getBoolOption("enable-async-iterator-helpers")) {
    JS::Prefs::setAtStartup_experimental_async_iterator_helpers(true);
  }
  if (gOptions.getBoolOption("enable-iterator-sequencing")) {
    JS::Prefs::setAtStartup_experimental_iterator_sequencing(true);
  }
  if (gOptions.getBoolOption("enable-iterator-range")) {
    JS::Prefs::setAtStartup_experimental_iterator_range(true);
  }
  if (gOptions.getBoolOption("enable-arraybuffer-immutable")) {
    JS::Prefs::setAtStartup_experimental_arraybuffer_immutable(true);
  }
  if (gOptions.getBoolOption("enable-import-bytes")) {
    JS::Prefs::setAtStartup_experimental_import_bytes(true);
  }
  if (gOptions.getBoolOption("enable-export-star-default")) {
    JS::Prefs::setAtStartup_experimental_export_star_default(true);
  }
  if (gOptions.getBoolOption("enable-promise-allkeyed")) {
    JS::Prefs::setAtStartup_experimental_promise_allkeyed(true);
  }
  if (gOptions.getBoolOption("enable-error-stack-trace-limit")) {
    JS::Prefs::setAtStartup_experimental_error_stack_trace_limit(true);
  }
  if (gOptions.getBoolOption("enable-wasm-esm-integration")) {
    JS::Prefs::set_experimental_wasm_esm_integration(true);
  }
  if (gOptions.getBoolOption("enable-defer-import-eval")) {
    JS::Prefs::setAtStartup_experimental_defer_import_eval(true);
  }
#endif
  if (gOptions.getBoolOption("enable-source-phase-imports")) {
    JS::Prefs::set_experimental_source_phase_imports(true);
  }
  if (gOptions.getBoolOption(
          "enable-source-phase-imports-test262-module-source")) {
    JS::Prefs::
        setAtStartup_experimental_source_phase_imports_test262_module_source(
            true);
  }
  JS::Prefs::setAtStartup_experimental_weakrefs_expose_cleanupSome(true);

  if (gOptions.getBoolOption("disable-property-error-message-fix")) {
    JS::Prefs::setAtStartup_property_error_message_fix(false);
  }

  JS::Prefs::set_use_fdlibm_for_sin_cos_tan(
      gOptions.getBoolOption("use-fdlibm-for-sin-cos-tan"));

  if (const char* str = gOptions.getStringOption("ion-regalloc")) {
    auto allocator = jit::LookupRegisterAllocator(str);
    if (allocator.isNothing()) {
      return OptionFailure("ion-regalloc", str);
    }
    switch (*allocator) {
      case jit::RegisterAllocator_Backtracking:
        JS::Prefs::setAtStartup_ion_regalloc(1);
        break;
      case jit::RegisterAllocator_Simple:
        JS::Prefs::setAtStartup_ion_regalloc(2);
        break;
    }
  }

  if (gOptions.getBoolOption("wasm-gc") ||
      gOptions.getBoolOption("wasm-relaxed-simd") ||
      gOptions.getBoolOption("wasm-multi-memory") ||
      gOptions.getBoolOption("wasm-memory-control") ||
      gOptions.getBoolOption("wasm-memory64") ||
      gOptions.getBoolOption("wasm-tail-calls") ||
      gOptions.getBoolOption("wasm-js-string-builtins")) {
    fprintf(
        stderr,
        "Wasm shell flags are now using prefs, use -P wasm_feature instead.\n");
    return false;
  }

  if (gOptions.getBoolOption("list-prefs")) {
    ListPrefs();
    return false;
  }

  // Note: DisableJitBackend must be called before JS_InitWithFailureDiagnostic.
  if (gOptions.getBoolOption("no-jit-backend")) {
    JS::DisableJitBackend();
  }

#if defined(JS_CODEGEN_ARM)
  if (const char* str = gOptions.getStringOption("arm-hwcap")) {
    jit::SetARMHwCapFlagsString(str);
  }

  int32_t fill = gOptions.getIntOption("arm-asm-nop-fill");
  if (fill >= 0) {
    jit::Assembler::NopFill = fill;
  }

  int32_t poolMaxOffset = gOptions.getIntOption("asm-pool-max-offset");

  if (poolMaxOffset >= 5 && poolMaxOffset <= 1024) {
    jit::Assembler::AsmPoolMaxOffset = poolMaxOffset;
  }
#endif

  // Fish around in the option parser for various important
  // compiler-configuration flags and make sure they get handed on to any child
  // processes we might create. See bug 1700900.  Semantically speaking, this is
  // all rather dubious:
  //
  // * What set of flags need to be propagated in order to guarantee that the
  //   child produces code that is "compatible" (in whatever sense) with that
  //   produced by the parent? This isn't always easy to determine.
  //
  // * There's nothing that ensures that flags given to the child are
  //   presented in the same order that they exist in the parent's `argv[]`.
  //   That could be a problem in the case where two flags with contradictory
  //   meanings are given, and they are presented to the child in the opposite
  //   order.  For example: --wasm-compiler=optimizing --wasm-compiler=baseline.

#if defined(JS_CODEGEN_X86) || defined(JS_CODEGEN_X64)
  MOZ_ASSERT(!jit::CPUFlagsHaveBeenComputed());

  if (gOptions.getBoolOption("no-avx")) {
    jit::CPUInfo::SetAVXDisabled();
    if (!sCompilerProcessFlags.append("--no-avx")) {
      return false;
    }
  }
  if (gOptions.getBoolOption("enable-avx")) {
    jit::CPUInfo::SetAVXEnabled();
    if (!sCompilerProcessFlags.append("--enable-avx")) {
      return false;
    }
  }
  if (gOptions.getBoolOption("no-sse3")) {
    jit::CPUInfo::SetSSE3Disabled();
    if (!sCompilerProcessFlags.append("--no-sse3")) {
      return false;
    }
  }
  if (gOptions.getBoolOption("no-ssse3")) {
    jit::CPUInfo::SetSSSE3Disabled();
    if (!sCompilerProcessFlags.append("--no-ssse3")) {
      return false;
    }
  }
  if (gOptions.getBoolOption("no-sse4") || gOptions.getBoolOption("no-sse41")) {
    jit::CPUInfo::SetSSE41Disabled();
    if (!sCompilerProcessFlags.append("--no-sse41")) {
      return false;
    }
  }
  if (gOptions.getBoolOption("no-sse42")) {
    jit::CPUInfo::SetSSE42Disabled();
    if (!sCompilerProcessFlags.append("--no-sse42")) {
      return false;
    }
  }
#endif
#if defined(JS_CODEGEN_ARM64)
  if (gOptions.getBoolOption("no-fjcvtzs")) {
    vixl::CPUFeatures fjcvtzs(vixl::CPUFeatures::kJSCVT);
    jit::ARM64Flags::DisableCPUFeatures(fjcvtzs);
  }
  if (gOptions.getBoolOption("no-cssc")) {
    vixl::CPUFeatures cssc(vixl::CPUFeatures::kCSSC);
    jit::ARM64Flags::DisableCPUFeatures(cssc);
  }
#endif
#if defined(JS_CODEGEN_LOONG64)
  if (const char* str = gOptions.getStringOption("loong64-isa")) {
    jit::SetLOONG64ISAString(str);
  }
#endif
#if defined(JS_CODEGEN_RISCV64)
  if (const char* str = gOptions.getStringOption("riscv-ext")) {
    jit::SetRISCV64ExtensionsString(str);
  }
#endif
#ifndef __wasi__
  if (gOptions.getBoolOption("disable-wasm-huge-memory")) {
    JS::Prefs::setAtStartup_wasm_disable_huge_memory(true);
    if (!sCompilerProcessFlags.append("--disable-wasm-huge-memory")) {
      return false;
    }
  }
#endif

  if (gOptions.getBoolOption("disable-decommit")) {
    gc::DisableDecommit();
  }

  return true;
}

#ifndef JS_WITHOUT_NSPR
class AutoLibraryLoader {
  Vector<PRLibrary*, 4, SystemAllocPolicy> libraries;

 public:
  ~AutoLibraryLoader() {
    for (auto dll : libraries) {
      PR_UnloadLibrary(dll);
    }
  }

  PRLibrary* load(const char* path) {
    PRLibSpec libSpec;
    libSpec.type = PR_LibSpec_Pathname;
    libSpec.value.pathname = path;
    PRLibrary* dll = PR_LoadLibraryWithFlags(libSpec, PR_LD_NOW | PR_LD_GLOBAL);
    if (!dll) {
      fprintf(stderr, "LoadLibrary '%s' failed with code %d\n", path,
              PR_GetError());
      MOZ_CRASH("Failed to load library");
    }

    MOZ_ALWAYS_TRUE(libraries.append(dll));
    return dll;
  }
};
#endif

bool shell::SetGlobalOptionsPostJSInit() {
  // Allow dumping on Linux with the fuzzing flag set, even when running with
  // the suid/sgid flag set on the shell.
#ifdef XP_LINUX
  if (gOptions.getBoolOption("fuzzing-safe")) {
    prctl(PR_SET_DUMPABLE, 1);
  }
#endif

#ifdef DEBUG
  /*
   * Process OOM options as early as possible so that we can observe as many
   * allocations as possible.
   */
  OOM_printAllocationCount = gOptions.getBoolOption('O');
#endif

  if (gOptions.getBoolOption("no-threads")) {
    js::DisableExtraThreads();
  }

  if (gOptions.getBoolOption("code-coverage")) {
    js::EnableCodeCoverage();
  }

  // If LCov is enabled, then the default delazification mode should be changed
  // to parse everything eagerly, such that we know the location of every
  // instruction, to report them in the LCov summary, even if there is no uses
  // of these instructions.
  //
  // Note: code coverage can be enabled either using the --code-coverage command
  // line, or the JS_CODE_COVERAGE_OUTPUT_DIR environment variable, which is
  // processed by JS_InitWithFailureDiagnostic.
  if (coverage::IsLCovEnabled()) {
    defaultDelazificationMode =
        JS::DelazificationOption::ParseEverythingEagerly;
  }

  if (const char* xdr = gOptions.getStringOption("selfhosted-xdr-path")) {
    if (fuzzingSafe) {
      fprintf(stderr,
              "Warning: ignoring --selfhosted-xdr-path with --fuzzing-safe\n");
    } else {
      shell::selfHostedXDRPath = xdr;
    }
  }
  if (const char* opt = gOptions.getStringOption("selfhosted-xdr-mode")) {
    if (fuzzingSafe) {
      fprintf(stderr,
              "Warning: ignoring --selfhosted-xdr-mode with --fuzzing-safe\n");
    } else {
      if (strcmp(opt, "encode") == 0) {
        shell::encodeSelfHostedCode = true;
      } else if (strcmp(opt, "decode") == 0) {
        shell::encodeSelfHostedCode = false;
      } else if (strcmp(opt, "off") == 0) {
        shell::selfHostedXDRPath = nullptr;
      } else {
        MOZ_CRASH(
            "invalid option value for --selfhosted-xdr-mode, must be "
            "encode/decode");
      }
    }
  }

#ifdef JS_WITHOUT_NSPR
  if (!gOptions.getMultiStringOption("dll").empty()) {
    fprintf(stderr, "Error: --dll requires NSPR support!\n");
    return false;
  }
#else
  AutoLibraryLoader loader;
  MultiStringRange dllPaths = gOptions.getMultiStringOption("dll");
  while (!dllPaths.empty()) {
    char* path = dllPaths.front();
    loader.load(path);
    dllPaths.popFront();
  }
#endif

  if (gOptions.getBoolOption("suppress-minidump")) {
    js::NoteIntentionalCrash();
  }

  // The fake CPU count must be set before initializing the Runtime,
  // which spins up the thread pool.
  int32_t cpuCount =
      gOptions.getIntOption("cpu-count");  // What we're really setting
  if (cpuCount < 0) {
    cpuCount = gOptions.getIntOption("thread-count");  // Legacy name
  }
  if (cpuCount >= 0 && !SetFakeCPUCount(cpuCount)) {
    return false;
  }

  return true;
}

static bool SetCommonContextOptions(JSContext* cx);

bool shell::SetMainThreadContextOptions(JSContext* cx) {
  if (gOptions.getBoolOption("no-ion-for-main-context")) {
    JS::ContextOptionsRef(cx).setDisableIon();
  }

  if (gOptions.getBoolOption("disable-main-thread-wasm-denormals")) {
    JS::ContextOptionsRef(cx).setWasmDisablesDenormals();
  }

#ifndef __wasi__
  // Also the following are to be propagated.
  const char* to_propagate[] = {
      // Compiler selection options
      "--test-wasm-await-tier2",
  };
  for (const char* p : to_propagate) {
    if (gOptions.getBoolOption(p + 2 /* 2 => skip the leading '--' */)) {
      if (!sCompilerProcessFlags.append(p)) {
        return false;
      }
    }
  }

  // Also --wasm-compiler= is to be propagated.  This is tricky because it is
  // necessary to reconstitute the --wasm-compiler=<whatever> string from its
  // pieces, without causing a leak.  Hence it is copied into a static buffer.
  // This is thread-unsafe, but we're in `main()` and on the process' root
  // thread.  Also, we do this only once -- it wouldn't work properly if we
  // handled multiple --wasm-compiler= flags in a loop.
  const char* wasm_compiler = gOptions.getStringOption("wasm-compiler");
  if (wasm_compiler) {
    size_t n_needed =
        2 + strlen("wasm-compiler") + 1 + strlen(wasm_compiler) + 1;
    const size_t n_avail = 128;
    static char buf[n_avail];
    // `n_needed` depends on the compiler name specified.  However, it can't
    // be arbitrarily long, since previous flag-checking should have limited
    // it to a set of known possibilities: "baseline", "ion",
    // "baseline+ion",  Still, assert this for safety.
    MOZ_RELEASE_ASSERT(n_needed < n_avail);
    memset(buf, 0, sizeof(buf));
    SprintfBuf(buf, n_avail, "--%s=%s", "wasm-compiler", wasm_compiler);
    if (!sCompilerProcessFlags.append(buf)) {
      return false;
    }
  }
#endif  // __wasi__

  enableToSource = !gOptions.getBoolOption("disable-tosource");

  if (const char* str = gOptions.getStringOption("shared-memory")) {
    if (strcmp(str, "off") == 0) {
      enableSharedMemory = false;
    } else if (strcmp(str, "on") == 0) {
      enableSharedMemory = true;
    } else {
      return OptionFailure("shared-memory", str);
    }
  }

  reportWarnings = gOptions.getBoolOption('w');
  compileOnly = gOptions.getBoolOption('c');
  printTiming = gOptions.getBoolOption('b');
  enableDisassemblyDumps = gOptions.getBoolOption('D');

#ifdef DEBUG
  dumpEntrainedVariables = gOptions.getBoolOption("dump-entrained-variables");
#endif

  if (const char* mode = gOptions.getStringOption("delazification-mode")) {
    if (strcmp(mode, "on-demand") == 0) {
      defaultDelazificationMode = JS::DelazificationOption::OnDemandOnly;
    } else if (strcmp(mode, "concurrent-df") == 0) {
      defaultDelazificationMode =
          JS::DelazificationOption::ConcurrentDepthFirst;
    } else if (strcmp(mode, "eager") == 0) {
      defaultDelazificationMode =
          JS::DelazificationOption::ParseEverythingEagerly;
    } else if (strcmp(mode, "concurrent-df+on-demand") == 0 ||
               strcmp(mode, "on-demand+concurrent-df") == 0) {
      defaultDelazificationMode =
          JS::DelazificationOption::CheckConcurrentWithOnDemand;
    } else {
      return OptionFailure("delazification-mode", mode);
    }
  }

  return SetGlobalJITOptions() && SetCommonContextOptions(cx);
}

bool shell::SetWorkerContextOptions(JSContext* cx) {
  // TODO: Worker-specific options will go here.
  return SetCommonContextOptions(cx);
}

static bool SetCommonContextOptions(JSContext* cx) {
  if (!SetContextJITOptions(cx) || !SetContextWasmOptions(cx) ||
      !SetContextGCOptions(cx)) {
    return false;
  }

  JS::ContextOptionsRef(cx)
      .setSourcePragmas(!gOptions.getBoolOption("no-source-pragmas"))
      .setAsyncStack(!gOptions.getBoolOption("no-async-stacks"))
      .setAsyncStackCaptureDebuggeeOnly(
          gOptions.getBoolOption("async-stacks-capture-debuggee-only"));

  cx->runtime()->profilingScripts =
      gOptions.getBoolOption("code-coverage") || enableDisassemblyDumps;

  return true;
}

static bool SetContextWasmOptions(JSContext* cx) {
  bool wasm = true;
  bool baseline = true;
  bool optimizing = true;

  if (const char* str = gOptions.getStringOption("wasm-compiler")) {
    if (strcmp(str, "none") == 0) {
      wasm = false;
    } else if (strcmp(str, "baseline") == 0) {
      MOZ_ASSERT(baseline);
      optimizing = false;
    } else if (strcmp(str, "optimizing") == 0 ||
               strcmp(str, "optimized") == 0 || strcmp(str, "ion") == 0) {
      baseline = false;
      MOZ_ASSERT(optimizing);
    } else if (strcmp(str, "baseline+optimizing") == 0 ||
               strcmp(str, "baseline+optimized") == 0 ||
               strcmp(str, "baseline+ion") == 0) {
      MOZ_ASSERT(baseline);
      MOZ_ASSERT(optimizing);
    } else {
      return OptionFailure("wasm-compiler", str);
    }
  }

  JS::ContextOptionsRef(cx)
      .setWasm(wasm)
      .setWasmForTrustedPrinciples(wasm)
      .setWasmBaseline(baseline)
      .setWasmIon(optimizing)
      .setTestWasmAwaitTier2(gOptions.getBoolOption("test-wasm-await-tier2"));
  return true;
}

static bool SetGlobalJITOptions() {
  // Check --fast-warmup first because it sets default warm-up thresholds. These
  // thresholds can then be overridden below by --ion-eager and other flags.
  if (gOptions.getBoolOption("fast-warmup")) {
    jit::JitOptions.setFastWarmUp();
  }

  if (const char* str = gOptions.getStringOption("cache-ir-stubs")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.disableCacheIR = false;
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.disableCacheIR = true;
    } else {
      return OptionFailure("cache-ir-stubs", str);
    }
  }

  if (const char* str = gOptions.getStringOption("stub-folding")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.disableStubFolding = false;
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.disableStubFolding = true;
    } else {
      return OptionFailure("stub-folding", str);
    }
  }

  if (const char* str =
          gOptions.getStringOption("stub-folding-loads-and-stores")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.disableStubFoldingLoadsAndStores = false;
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.disableStubFoldingLoadsAndStores = true;
    } else {
      return OptionFailure("stub-folding-loads-and-stores", str);
    }
  }

  if (const char* str = gOptions.getStringOption("spectre-mitigations")) {
    if (strcmp(str, "on") == 0) {
#if defined(JS_CODEGEN_RISCV64) || defined(JS_CODEGEN_LOONG64) || \
    defined(JS_CODEGEN_MIPS64) || defined(JS_CODEGEN_WASM32)
      // MacroAssembler::spectreZeroRegister and MacroAssembler::spectreMovePtr
      // are not implemented for these targets.
      fprintf(
          stderr,
          "Warning: Spectre mitigations are not implemented for this target."
          " --spectre-mitigations=on is ignored.\n");
#else
      jit::JitOptions.spectreIndexMasking = true;
      jit::JitOptions.spectreObjectMitigations = true;
      jit::JitOptions.spectreStringMitigations = true;
      jit::JitOptions.spectreValueMasking = true;
      jit::JitOptions.spectreJitToCxxCalls = true;
#endif
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.spectreIndexMasking = false;
      jit::JitOptions.spectreObjectMitigations = false;
      jit::JitOptions.spectreStringMitigations = false;
      jit::JitOptions.spectreValueMasking = false;
      jit::JitOptions.spectreJitToCxxCalls = false;
    } else {
      return OptionFailure("spectre-mitigations", str);
    }
  }

  if (const char* str = gOptions.getStringOption("write-protect-code")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.maybeSetWriteProtectCode(true);
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.maybeSetWriteProtectCode(false);
    } else {
      return OptionFailure("write-protect-code", str);
    }
  }

  if (const char* str = gOptions.getStringOption("monomorphic-inlining")) {
    if (strcmp(str, "default") == 0) {
      jit::JitOptions.monomorphicInlining =
          jit::UseMonomorphicInlining::Default;
    } else if (strcmp(str, "always") == 0) {
      jit::JitOptions.monomorphicInlining = jit::UseMonomorphicInlining::Always;
    } else if (strcmp(str, "never") == 0) {
      jit::JitOptions.monomorphicInlining = jit::UseMonomorphicInlining::Never;
    } else {
      return OptionFailure("monomorphic-inlining", str);
    }
  }

  if (const char* str = gOptions.getStringOption("ion-scalar-replacement")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.disableScalarReplacement = false;
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.disableScalarReplacement = true;
    } else {
      return OptionFailure("ion-scalar-replacement", str);
    }
  }

  if (gOptions.getStringOption("ion-shared-stubs")) {
    // Dead option, preserved for now for potential fuzzer interaction.
  }

  if (const char* str = gOptions.getStringOption("ion-gvn")) {
    if (strcmp(str, "off") == 0) {
      jit::JitOptions.disableGvn = true;
    } else if (strcmp(str, "on") != 0 && strcmp(str, "optimistic") != 0 &&
               strcmp(str, "pessimistic") != 0) {
      // We accept "pessimistic" and "optimistic" as synonyms for "on"
      // for backwards compatibility.
      return OptionFailure("ion-gvn", str);
    }
  }

  if (const char* str = gOptions.getStringOption("ion-licm")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.disableLicm = false;
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.disableLicm = true;
    } else {
      return OptionFailure("ion-licm", str);
    }
  }

  if (const char* str = gOptions.getStringOption("ion-edgecase-analysis")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.disableEdgeCaseAnalysis = false;
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.disableEdgeCaseAnalysis = true;
    } else {
      return OptionFailure("ion-edgecase-analysis", str);
    }
  }

  if (const char* str = gOptions.getStringOption("ion-pruning")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.disablePruning = false;
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.disablePruning = true;
    } else {
      return OptionFailure("ion-pruning", str);
    }
  }

  if (const char* str = gOptions.getStringOption("ion-range-analysis")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.disableRangeAnalysis = false;
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.disableRangeAnalysis = true;
    } else {
      return OptionFailure("ion-range-analysis", str);
    }
  }

  if (const char* str = gOptions.getStringOption("ion-optimize-shapeguards")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.disableRedundantShapeGuards = false;
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.disableRedundantShapeGuards = true;
    } else {
      return OptionFailure("ion-optimize-shapeguards", str);
    }
  }

  if (const char* str = gOptions.getStringOption("ion-optimize-gcbarriers")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.disableRedundantGCBarriers = false;
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.disableRedundantGCBarriers = true;
    } else {
      return OptionFailure("ion-optimize-gcbarriers", str);
    }
  }

  if (const char* str =
          gOptions.getStringOption("ion-instruction-reordering")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.disableInstructionReordering = false;
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.disableInstructionReordering = true;
    } else {
      return OptionFailure("ion-instruction-reordering", str);
    }
  }

  if (gOptions.getBoolOption("ion-check-range-analysis")) {
    jit::JitOptions.checkRangeAnalysis = true;
  }

  if (gOptions.getBoolOption("ion-extra-checks")) {
    jit::JitOptions.runExtraChecks = true;
  }

  if (const char* str = gOptions.getStringOption("ion-inlining")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.disableInlining = false;
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.disableInlining = true;
    } else {
      return OptionFailure("ion-inlining", str);
    }
  }

  if (const char* str = gOptions.getStringOption("ion-osr")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.osr = true;
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.osr = false;
    } else {
      return OptionFailure("ion-osr", str);
    }
  }

  if (const char* str = gOptions.getStringOption("ion-limit-script-size")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.limitScriptSize = true;
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.limitScriptSize = false;
    } else {
      return OptionFailure("ion-limit-script-size", str);
    }
  }

  if (const char* str =
          gOptions.getStringOption("object-keys-scalar-replacement")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.disableObjectKeysScalarReplacement = false;
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.disableObjectKeysScalarReplacement = true;
    } else {
      return OptionFailure("object-keys-scalar-replacement", str);
    }
  }

  int32_t warmUpThreshold = gOptions.getIntOption("ion-warmup-threshold");
  if (warmUpThreshold >= 0) {
    jit::JitOptions.setNormalIonWarmUpThreshold(warmUpThreshold);
  }

  warmUpThreshold = gOptions.getIntOption("baseline-warmup-threshold");
  if (warmUpThreshold >= 0) {
    jit::JitOptions.baselineJitWarmUpThreshold = warmUpThreshold;
  }

  warmUpThreshold = gOptions.getIntOption("trial-inlining-warmup-threshold");
  if (warmUpThreshold >= 0) {
    jit::JitOptions.trialInliningWarmUpThreshold = warmUpThreshold;
  }

  warmUpThreshold = gOptions.getIntOption("regexp-warmup-threshold");
  if (warmUpThreshold >= 0) {
    jit::JitOptions.regexpWarmUpThreshold = warmUpThreshold;
  }

  if (gOptions.getBoolOption("baseline-eager")) {
    jit::JitOptions.setEagerBaselineCompilation();
  }

#ifdef ENABLE_PORTABLE_BASELINE_INTERP
  if (gOptions.getBoolOption("portable-baseline-eager")) {
    jit::JitOptions.setEagerPortableBaselineInterpreter();
  }
  if (gOptions.getBoolOption("portable-baseline")) {
    jit::JitOptions.portableBaselineInterpreter = true;
  }
  if (gOptions.getBoolOption("no-portable-baseline")) {
    jit::JitOptions.portableBaselineInterpreter = false;
  }
#endif

#ifdef ENABLE_JS_AOT_ICS
  if (gOptions.getBoolOption("aot-ics")) {
    jit::JitOptions.enableAOTICs = true;
  }
  if (gOptions.getBoolOption("enforce-aot-ics")) {
    jit::JitOptions.enableAOTICEnforce = true;
  }
#endif

  if (gOptions.getBoolOption("blinterp")) {
    jit::JitOptions.baselineInterpreter = true;
  }

  if (gOptions.getBoolOption("no-blinterp")) {
    jit::JitOptions.baselineInterpreter = false;
  }

  if (gOptions.getBoolOption("disable-jithints")) {
    jit::JitOptions.disableJitHints = true;
  }

  if (gOptions.getBoolOption("emit-interpreter-entry")) {
    jit::JitOptions.emitInterpreterEntryTrampoline = true;
  }

  if (gOptions.getBoolOption("no-emit-interpreter-entry")) {
    jit::JitOptions.emitInterpreterEntryTrampoline = false;
  }

  warmUpThreshold = gOptions.getIntOption("blinterp-warmup-threshold");
  if (warmUpThreshold >= 0) {
    jit::JitOptions.baselineInterpreterWarmUpThreshold = warmUpThreshold;
  }

  if (gOptions.getBoolOption("blinterp-eager")) {
    jit::JitOptions.baselineInterpreterWarmUpThreshold = 0;
  }

  if (gOptions.getBoolOption("no-baseline")) {
    jit::JitOptions.baselineJit = false;
  }

  if (gOptions.getBoolOption("no-ion")) {
    jit::JitOptions.ion = false;
  }

  if (gOptions.getBoolOption("no-native-regexp")) {
    jit::JitOptions.nativeRegExp = false;
  }

  if (gOptions.getBoolOption("trace-regexp-parser")) {
    jit::JitOptions.trace_regexp_parser = true;
  }
  if (gOptions.getBoolOption("trace-regexp-assembler")) {
    jit::JitOptions.trace_regexp_assembler = true;
  }
  if (gOptions.getBoolOption("trace-regexp-compiler")) {
    jit::JitOptions.trace_regexp_compiler = true;
  }
  if (gOptions.getBoolOption("trace-regexp-graph-building")) {
    jit::JitOptions.trace_regexp_graph_building = true;
  }
  if (gOptions.getBoolOption("trace-regexp-interpreter")) {
    jit::JitOptions.trace_regexp_bytecodes = true;
  }
  if (gOptions.getBoolOption("trace-regexp-peephole")) {
    jit::JitOptions.trace_regexp_peephole_optimization = true;
  }

  if (gOptions.getBoolOption("less-debug-code")) {
    jit::JitOptions.lessDebugCode = true;
  }

  int32_t inliningEntryThreshold =
      gOptions.getIntOption("inlining-entry-threshold");
  if (inliningEntryThreshold > 0) {
    jit::JitOptions.inliningEntryThreshold = inliningEntryThreshold;
  }

  int32_t smallFunctionLength = gOptions.getIntOption("small-function-length");
  if (smallFunctionLength > 0) {
    jit::JitOptions.smallFunctionMaxBytecodeLength = smallFunctionLength;
  }

  if (gOptions.getBoolOption("ion-eager")) {
    jit::JitOptions.setEagerIonCompilation();
  }

  if (const char* str = gOptions.getStringOption("baseline-batching")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.baselineBatching = true;
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.baselineBatching = false;
    } else {
      return OptionFailure("baseline-batching", str);
    }
  }

  int32_t queueCapacity = gOptions.getIntOption("baseline-queue-capacity");
  if (queueCapacity == 0) {
    fprintf(stderr, "baseline-queue-capacity must be positive\n");
    return false;
  }
  if (queueCapacity > 0) {
    jit::JitOptions.baselineQueueCapacity = std::min(
        uint32_t(queueCapacity), jit::BaselineCompileQueue::MaxCapacity);
  }

  if (gOptions.getStringOption("ion-parallel-compile")) {
    fprintf(stderr,
            "--ion-parallel-compile is deprecated. Please use "
            "--ion-offthread-compile instead.\n");
    return false;
  }

  if (gOptions.getBoolOption("disable-bailout-loop-check")) {
    jit::JitOptions.disableBailoutLoopCheck = true;
  }

  if (const char* str = gOptions.getStringOption("canonicalize-nan-at-uses")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.disableCanonicalizeNaNAtUses = false;
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.disableCanonicalizeNaNAtUses = true;
    } else {
      return OptionFailure("canonicalize-nan-at-uses", str);
    }
  }

  if (gOptions.getBoolOption("only-inline-selfhosted")) {
    jit::JitOptions.onlyInlineSelfHosted = true;
  }

  if (gOptions.getBoolOption("enable-stub-frame-pointers")) {
#ifndef JS_CODEGEN_X86
    jit::JitOptions.enableStubFramePointers = true;
#endif
  }

  if (const char* str = gOptions.getStringOption("ion-iterator-indices")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.disableIteratorIndices = false;
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.disableIteratorIndices = true;
    } else {
      return OptionFailure("ion-iterator-indices", str);
    }
  }

  if (const char* str = gOptions.getStringOption("ion-load-keys")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.disableMarkLoadsUsedAsPropertyKeys = false;
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.disableMarkLoadsUsedAsPropertyKeys = true;
    } else {
      return OptionFailure("ion-load-keys", str);
    }
  }

#if defined(JS_SIMULATOR_ARM)
  if (gOptions.getBoolOption("arm-sim-icache-checks")) {
    jit::SimulatorProcess::ICacheCheckingDisableCount = 0;
  }

  int32_t stopAt = gOptions.getIntOption("arm-sim-stop-at");
  if (stopAt >= 0) {
    jit::Simulator::StopSimAt = stopAt;
  }
#elif defined(JS_SIMULATOR_MIPS64)
  if (gOptions.getBoolOption("mips-sim-icache-checks")) {
    jit::SimulatorProcess::ICacheCheckingDisableCount = 0;
  }

  int32_t stopAt = gOptions.getIntOption("mips-sim-stop-at");
  if (stopAt >= 0) {
    jit::Simulator::StopSimAt = stopAt;
  }
#elif defined(JS_SIMULATOR_LOONG64)
  if (gOptions.getBoolOption("loong64-sim-icache-checks")) {
    jit::SimulatorProcess::ICacheCheckingDisableCount = 0;
  }

  int32_t stopAt = gOptions.getIntOption("loong64-sim-stop-at");
  if (stopAt >= 0) {
    jit::Simulator::StopSimAt = stopAt;
  }
#endif

#ifdef DEBUG
#  ifdef JS_CODEGEN_RISCV64
  if (gOptions.getBoolOption("riscv-debug")) {
    jit::Assembler::FLAG_riscv_debug = true;
  }
#  endif
#  ifdef JS_SIMULATOR_RISCV64
  if (gOptions.getBoolOption("riscv-sim-trace")) {
    jit::Simulator::FLAG_trace_sim = true;
  }
  if (gOptions.getBoolOption("riscv-sim-debug")) {
    jit::Simulator::FLAG_debug_sim = true;
  }
  if (gOptions.getBoolOption("riscv-sim-icache-checks")) {
    jit::SimulatorProcess::ICacheCheckingDisableCount = 0;
  }
  if (gOptions.getBoolOption("riscv-trap-to-simulator-debugger")) {
    jit::Simulator::FLAG_riscv_trap_to_simulator_debugger = true;
  }
  int32_t stopAt = gOptions.getIntOption("riscv-sim-stop-at");
  if (stopAt >= 0) {
    jit::Simulator::StopSimAt = stopAt;
  }
#  endif
#endif

  return true;
}

static bool SetContextJITOptions(JSContext* cx) {
  bool offthreadBaselineCompilation = false;
  if (const char* str =
          gOptions.getStringOption("baseline-offthread-compile")) {
    if (strcmp(str, "on") == 0) {
      offthreadBaselineCompilation = true;
    } else if (strcmp(str, "off") != 0) {
      return OptionFailure("baseline-offthread-compile", str);
    }
  }
  JSRuntime* runtime = cx->runtime();
  runtime->setOffthreadBaselineCompilationEnabled(offthreadBaselineCompilation);

  bool offthreadIonCompilation = true;
  if (const char* str = gOptions.getStringOption("ion-offthread-compile")) {
    if (strcmp(str, "off") == 0) {
      offthreadIonCompilation = false;
    } else if (strcmp(str, "on") != 0) {
      return OptionFailure("ion-offthread-compile", str);
    }
  }
  runtime->setOffthreadIonCompilationEnabled(offthreadIonCompilation);

  return true;
}

static bool SetContextGCOptions(JSContext* cx) {
  JS_SetGCParameter(cx, JSGC_MAX_BYTES, 0xffffffff);

  size_t nurseryBytes = gOptions.getIntOption("nursery-size") * 1024L * 1024L;
  if (nurseryBytes == 0) {
    fprintf(stderr, "Error: --nursery-size parameter must be non-zero.\n");
    fprintf(stderr,
            "The nursery can be disabled by passing the --no-ggc option.\n");
    return false;
  }
  JS_SetGCParameter(cx, JSGC_MAX_NURSERY_BYTES, nurseryBytes);

  size_t availMemMB = gOptions.getIntOption("available-memory");
  if (availMemMB > 0) {
    JS_SetGCParametersBasedOnAvailableMemory(cx, availMemMB);
  }

  if (const char* opt = gOptions.getStringOption("nursery-strings")) {
    if (strcmp(opt, "on") == 0) {
      cx->runtime()->gc.nursery().enableStrings();
    } else if (strcmp(opt, "off") == 0) {
      cx->runtime()->gc.nursery().disableStrings();
    } else {
      MOZ_CRASH("invalid option value for --nursery-strings, must be on/off");
    }
  }

  if (const char* opt = gOptions.getStringOption("nursery-bigints")) {
    if (strcmp(opt, "on") == 0) {
      cx->runtime()->gc.nursery().enableBigInts();
    } else if (strcmp(opt, "off") == 0) {
      cx->runtime()->gc.nursery().disableBigInts();
    } else {
      MOZ_CRASH("invalid option value for --nursery-bigints, must be on/off");
    }
  }

  bool incrementalGC = !gOptions.getBoolOption("no-incremental-gc");
  JS_SetGCParameter(cx, JSGC_INCREMENTAL_GC_ENABLED, incrementalGC);

#ifndef ANDROID
  bool parallelMarking = true;
#else
  bool parallelMarking = false;
#endif
  if (gOptions.getBoolOption("enable-parallel-marking")) {
    parallelMarking = true;
  }
  if (gOptions.getBoolOption("no-parallel-marking")) {
    parallelMarking = false;
  }
  JS_SetGCParameter(cx, JSGC_PARALLEL_MARKING_ENABLED, parallelMarking);

#ifdef JS_GC_CONCURRENT_MARKING
  bool concurrentMarking = false;
  if (gOptions.getBoolOption("enable-concurrent-marking")) {
    concurrentMarking = true;
  }
  if (gOptions.getBoolOption("no-concurrent-marking")) {
    concurrentMarking = false;
  }
  JS_SetGCParameter(cx, JSGC_CONCURRENT_MARKING_ENABLED, concurrentMarking);
#endif

  JS_SetGCParameter(cx, JSGC_SLICE_TIME_BUDGET_MS, 5);

  JS_SetGCParameter(cx, JSGC_PER_ZONE_GC_ENABLED, true);

  for (MultiStringRange args = gOptions.getMultiStringOption("gc-param");
       !args.empty(); args.popFront()) {
    if (!SetGCParameterFromArg(cx, args.front())) {
      return false;
    }
  }

#ifdef JS_GC_ZEAL
  const char* zealStr = gOptions.getStringOption("gc-zeal");
  if (zealStr) {
    if (!cx->runtime()->gc.parseAndSetZeal(zealStr)) {
      return false;
    }
  }
#endif

  return true;
}

bool shell::ApplyBenchmarkMode(JSContext* cx) {
  if (sBenchmarkMode == BenchmarkMode::Off) {
    return true;
  }

  bool isStrict = (sBenchmarkMode == BenchmarkMode::Strict);
  bool hasErrors = false;

  auto warn = [](const char* msg) {
    fprintf(stderr, "--benchmark-mode: WARNING: %s\n", msg);
  };

  auto issue = [&](const char* msg) {
    hasErrors = true;
    if (isStrict) {
      fprintf(stderr, "--benchmark-mode: ERROR: %s\n", msg);
    } else {
      warn(msg);
    }
  };

  // Build configuration checks.
#ifdef JS_DEBUG
  issue("This is a DEBUG build. Use an optimized, non-debug build.");
#endif
#ifdef MOZ_ASAN
  issue("This is an AddressSanitizer (ASan) build.");
#endif
#ifdef MOZ_TSAN
  issue("This is a ThreadSanitizer (TSan) build.");
#endif
#ifdef MOZ_MSAN
  issue("This is a MemorySanitizer (MSan) build.");
#endif
#if defined(JS_SIMULATOR)
  issue("Running on a CPU simulator.");
#endif
#ifdef FUZZING
  issue("This is a fuzzing build.");
#endif
#ifdef JS_GC_ZEAL
  warn(
      "Build has GC zeal support (JS_GC_ZEAL). "
      "This may indicate a non-optimized build.");
#endif

  // Runtime option checks.
#ifdef JS_GC_ZEAL
  if (gOptions.getStringOption("gc-zeal") != nullptr) {
    issue("--gc-zeal is active.");
  }
#endif

  if (gOptions.getBoolOption("code-coverage")) {
    issue("--code-coverage adds instrumentation overhead.");
  }

  if (gOptions.getBoolOption("ion-check-range-analysis")) {
    issue("--ion-check-range-analysis adds runtime checks.");
  }

  if (gOptions.getBoolOption("ion-extra-checks")) {
    issue("--ion-extra-checks adds runtime checks.");
  }

  if (gOptions.getBoolOption("emit-interpreter-entry")) {
    issue("--emit-interpreter-entry adds overhead.");
  }

  if (gOptions.getBoolOption("enable-stub-frame-pointers")) {
    issue(
        "--enable-stub-frame-pointers adds overhead to IC stubs and various "
        "trampolines.");
  }

  if (gOptions.getBoolOption("no-ion")) {
    issue("IonMonkey JIT is disabled (--no-ion).");
  }

  if (gOptions.getBoolOption("no-baseline")) {
    issue("Baseline compiler is disabled (--no-baseline).");
  }

  if (gOptions.getBoolOption("no-threads")) {
    issue("Helper threads are disabled (--no-threads).");
  }

  if (gOptions.getBoolOption("fuzzing-safe")) {
    issue("--fuzzing-safe is set.");
  }

#ifdef WASM_SUPPORTS_HUGE_MEMORY
  // Wasm checks
  if (JS::Prefs::wasm_disable_huge_memory()) {
    issue("huge memory disabled");
  }
#endif

  if (!JS::ContextOptionsRef(cx).wasmIon() ||
      !JS::ContextOptionsRef(cx).wasmBaseline()) {
    issue("missing wasm compiler");
  }
  if (JS::Prefs::wasm_test_serialization()) {
    issue("testing serialization");
  }
  if (JS::Prefs::wasm_lazy_tiering_synchronous()) {
    issue("tiering test configuration");
  }

  if (jit::JitOptions.spectreIndexMasking ||
      jit::JitOptions.spectreObjectMitigations ||
      jit::JitOptions.spectreStringMitigations ||
      jit::JitOptions.spectreValueMasking ||
      jit::JitOptions.spectreJitToCxxCalls) {
    issue("Spectre mitigations are enabled.");
  }

  // Apply benchmark-mode overrides.
  jit::JitOptions.spectreIndexMasking = false;
  jit::JitOptions.spectreObjectMitigations = false;
  jit::JitOptions.spectreStringMitigations = false;
  jit::JitOptions.spectreValueMasking = false;
  jit::JitOptions.spectreJitToCxxCalls = false;

  // See the default set for
  // javascript.options.content_process_write_protect_code
#if !defined(XP_OPENBSD)
  jit::JitOptions.maybeSetWriteProtectCode(false);
#endif

  JS::ContextOptionsRef(cx).setAsyncStack(false);

  jit::JitOptions.fullDebugChecks = false;

  if (isStrict && hasErrors) {
    fprintf(stderr,
            "--benchmark-mode: Refusing to run due to errors listed above.\n");
    return false;
  }

  return true;
}
