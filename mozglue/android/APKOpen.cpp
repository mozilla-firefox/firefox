/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * This custom library loading code is only meant to be called
 * during initialization. As a result, it takes no special
 * precautions to be threadsafe. Any of the library loading functions
 * like mozload should not be available to other code.
 */

#include <jni.h>
#include <android/log.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/limits.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include "dlfcn.h"
#include "APKOpen.h"
#include <sys/time.h>
#include <sys/syscall.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include "sqlite3.h"
#include "Linker.h"
#include "BaseProfiler.h"
#include "application.ini.h"

#include "mozilla/arm.h"
#include "mozilla/Bootstrap.h"
#include "mozilla/Printf.h"
#include "mozilla/ProcessType.h"
#include "mozilla/Sprintf.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/Try.h"
#include "mozilla/UniquePtr.h"
#include "XREChildData.h"
#include "XREShellData.h"

/* Android headers don't define RUSAGE_THREAD */
#ifndef RUSAGE_THREAD
#  define RUSAGE_THREAD 1
#endif

#ifndef RELEASE_OR_BETA
/* Official builds have the debuggable flag set to false, which disables
 * the backtrace dumper from bionic. However, as it is useful for native
 * crashes happening before the crash reporter is registered, re-enable
 * it on non release builds (i.e. nightly and aurora).
 * Using a constructor so that it is re-enabled as soon as libmozglue.so
 * is loaded.
 */
__attribute__((constructor)) void make_dumpable() { prctl(PR_SET_DUMPABLE, 1); }
#endif

typedef int mozglueresult;

using LoadGeckoLibsResult =
    mozilla::Result<mozilla::Ok, mozilla::BootstrapError>;

enum StartupEvent {
#define mozilla_StartupTimeline_Event(ev, z) ev,
#include "StartupTimeline.h"
#undef mozilla_StartupTimeline_Event
  MAX_STARTUP_EVENT_ID
};

using namespace mozilla;

void JNI_Throw(JNIEnv* jenv, const char* classname, const char* msg) {
  __android_log_print(ANDROID_LOG_ERROR, "GeckoLibLoad", "Throw\n");
  jclass cls = jenv->FindClass(classname);
  if (cls == nullptr) {
    __android_log_print(
        ANDROID_LOG_ERROR, "GeckoLibLoad",
        "Couldn't find exception class (or exception pending) %s\n", classname);
    exit(FAILURE);
  }
  int rc = jenv->ThrowNew(cls, msg);
  if (rc < 0) {
    __android_log_print(ANDROID_LOG_ERROR, "GeckoLibLoad",
                        "Error throwing exception %s\n", msg);
    exit(FAILURE);
  }
  jenv->DeleteLocalRef(cls);
}

namespace {
JavaVM* sJavaVM;
}

void abortThroughJava(const char* msg) {
  struct sigaction sigact = {};
  if (__wrap_sigaction(SIGSEGV, nullptr, &sigact)) {
    return;  // sigaction call failed.
  }

  Dl_info info = {};
  if ((sigact.sa_flags & SA_SIGINFO) &&
      __wrap_dladdr(reinterpret_cast<void*>(sigact.sa_sigaction), &info) &&
      info.dli_fname && strstr(info.dli_fname, "libxul.so")) {
    return;  // Existing signal handler is in libxul (i.e. we have crash
             // reporter).
  }

  JNIEnv* env = nullptr;
  if (!sJavaVM ||
      sJavaVM->AttachCurrentThreadAsDaemon(&env, nullptr) != JNI_OK) {
    return;
  }

  if (!env || env->PushLocalFrame(2) != JNI_OK) {
    return;
  }

  jclass loader = env->FindClass("org/mozilla/gecko/mozglue/GeckoLoader");
  if (!loader) {
    return;
  }

  jmethodID method =
      env->GetStaticMethodID(loader, "abort", "(Ljava/lang/String;)V");
  jstring str = env->NewStringUTF(msg);

  if (method && str) {
    env->CallStaticVoidMethod(loader, method, str);
  }

  env->PopLocalFrame(nullptr);
}

MOZ_RUNINIT Bootstrap::UniquePtr gBootstrap;
#ifndef MOZ_FOLD_LIBS
static void* sqlite_handle = nullptr;
static void* nspr_handle = nullptr;
static void* plc_handle = nullptr;
#else
#  define sqlite_handle nss_handle
#  define nspr_handle nss_handle
#  define plc_handle nss_handle
#endif
static void* nss_handle = nullptr;

static UniquePtr<char[]> getUnpackedLibraryName(const char* libraryName) {
  static const char* libdir = getenv("MOZ_ANDROID_LIBDIR");

  size_t len = strlen(libdir) + 1 /* path separator */ + strlen(libraryName) +
               1; /* null terminator */
  auto file = MakeUnique<char[]>(len);
  snprintf(file.get(), len, "%s/%s", libdir, libraryName);
  return file;
}

static void* dlopenLibrary(const char* libraryName) {
  return __wrap_dlopen(getUnpackedLibraryName(libraryName).get(),
                       RTLD_GLOBAL | RTLD_LAZY);
}

static void EnsureBaseProfilerInitialized() {
  // There is no single entry-point into C++ code on Android.
  // Instead, GeckoThread and GeckoLibLoader call various functions to load
  // libraries one-by-one.
  // We want to capture all that library loading in the profiler, so we need to
  // kick off the base profiler at the beginning of whichever function is called
  // first.
  // We currently assume that all these functions are called on the same thread.
  static bool sInitialized = false;
  if (sInitialized) {
    return;
  }

  // The stack depth we observe here will be determined by the stack of
  // whichever caller enters this code first. In practice this means that we may
  // miss some root-most frames, which hopefully shouldn't ruin profiling.
  int stackBase = 5;
  mozilla::baseprofiler::profiler_init(&stackBase);
  sInitialized = true;
}

static LoadGeckoLibsResult loadGeckoLibs() {
  TimeStamp t0 = TimeStamp::Now();
  struct rusage usage1_thread, usage1;
  getrusage(RUSAGE_THREAD, &usage1_thread);
  getrusage(RUSAGE_SELF, &usage1);

  static const char* libxul = getenv("MOZ_ANDROID_LIBDIR_OVERRIDE");
  MOZ_TRY_VAR(
      gBootstrap,
      GetBootstrap(libxul ? libxul : getUnpackedLibraryName("libxul.so").get(),
                   LibLoadingStrategy::ReadAhead));

  TimeStamp t1 = TimeStamp::Now();
  struct rusage usage2_thread, usage2;
  getrusage(RUSAGE_THREAD, &usage2_thread);
  getrusage(RUSAGE_SELF, &usage2);

#define RUSAGE_TIMEDIFF(u1, u2, field)                    \
  ((u2.ru_##field.tv_sec - u1.ru_##field.tv_sec) * 1000 + \
   (u2.ru_##field.tv_usec - u1.ru_##field.tv_usec) / 1000)

  __android_log_print(ANDROID_LOG_ERROR, "GeckoLibLoad",
                      "Loaded libs in %fms total, %ldms(%ldms) user, "
                      "%ldms(%ldms) system, %ld(%ld) faults",
                      (t1 - t0).ToMilliseconds(),
                      RUSAGE_TIMEDIFF(usage1_thread, usage2_thread, utime),
                      RUSAGE_TIMEDIFF(usage1, usage2, utime),
                      RUSAGE_TIMEDIFF(usage1_thread, usage2_thread, stime),
                      RUSAGE_TIMEDIFF(usage1, usage2, stime),
                      usage2_thread.ru_majflt - usage1_thread.ru_majflt,
                      usage2.ru_majflt - usage1.ru_majflt);

  gBootstrap->XRE_StartupTimelineRecord(LINKER_INITIALIZED, t0);
  gBootstrap->XRE_StartupTimelineRecord(LIBRARIES_LOADED, t1);
  return Ok();
}

static mozglueresult loadNSSLibs();

static mozglueresult loadSQLiteLibs() {
  if (sqlite_handle) return SUCCESS;

#ifdef MOZ_FOLD_LIBS
  if (loadNSSLibs() != SUCCESS) return FAILURE;
#else

  sqlite_handle = dlopenLibrary("libmozsqlite3.so");
  if (!sqlite_handle) {
    __android_log_print(ANDROID_LOG_ERROR, "GeckoLibLoad",
                        "Couldn't get a handle to libmozsqlite3!");
    return FAILURE;
  }
#endif

  return SUCCESS;
}

static mozglueresult loadNSSLibs() {
  if (nss_handle && nspr_handle && plc_handle) return SUCCESS;

  nss_handle = dlopenLibrary("libnss3.so");

#ifndef MOZ_FOLD_LIBS
  nspr_handle = dlopenLibrary("libnspr4.so");

  plc_handle = dlopenLibrary("libplc4.so");
#endif

  if (!nss_handle) {
    __android_log_print(ANDROID_LOG_ERROR, "GeckoLibLoad",
                        "Couldn't get a handle to libnss3!");
    return FAILURE;
  }

#ifndef MOZ_FOLD_LIBS
  if (!nspr_handle) {
    __android_log_print(ANDROID_LOG_ERROR, "GeckoLibLoad",
                        "Couldn't get a handle to libnspr4!");
    return FAILURE;
  }

  if (!plc_handle) {
    __android_log_print(ANDROID_LOG_ERROR, "GeckoLibLoad",
                        "Couldn't get a handle to libplc4!");
    return FAILURE;
  }
#endif

  return SUCCESS;
}

extern "C" APKOPEN_EXPORT void MOZ_JNICALL
Java_org_mozilla_gecko_mozglue_GeckoLoader_loadGeckoLibsNative(
    JNIEnv* jenv, jclass jGeckoAppShellClass) {
  EnsureBaseProfilerInitialized();

  jenv->GetJavaVM(&sJavaVM);

  LoadGeckoLibsResult res = loadGeckoLibs();
  if (res.isOk()) {
    return;
  }

  const BootstrapError& errorInfo = res.inspectErr();

  auto msg = errorInfo.match(
      [](const nsresult& aRv) {
        return Smprintf("Error loading Gecko libraries: nsresult 0x%08X",
                        uint32_t(aRv));
      },
      [](const DLErrorType& aErr) {
        return Smprintf("Error loading Gecko libraries: %s", aErr.get());
      });

  JNI_Throw(jenv, "java/lang/Exception", msg.get());
}

extern "C" APKOPEN_EXPORT void MOZ_JNICALL
Java_org_mozilla_gecko_mozglue_GeckoLoader_loadSQLiteLibsNative(
    JNIEnv* jenv, jclass jGeckoAppShellClass) {
  EnsureBaseProfilerInitialized();

  __android_log_print(ANDROID_LOG_ERROR, "GeckoLibLoad", "Load sqlite start\n");
  mozglueresult rv = loadSQLiteLibs();
  if (rv != SUCCESS) {
    JNI_Throw(jenv, "java/lang/Exception", "Error loading sqlite libraries");
  }
  __android_log_print(ANDROID_LOG_ERROR, "GeckoLibLoad", "Load sqlite done\n");
}

extern "C" APKOPEN_EXPORT void MOZ_JNICALL
Java_org_mozilla_gecko_mozglue_GeckoLoader_loadNSSLibsNative(
    JNIEnv* jenv, jclass jGeckoAppShellClass) {
  EnsureBaseProfilerInitialized();

  __android_log_print(ANDROID_LOG_ERROR, "GeckoLibLoad", "Load nss start\n");
  mozglueresult rv = loadNSSLibs();
  if (rv != SUCCESS) {
    JNI_Throw(jenv, "java/lang/Exception", "Error loading nss libraries");
  }
  __android_log_print(ANDROID_LOG_ERROR, "GeckoLibLoad", "Load nss done\n");
}

static char** CreateArgvFromObjectArray(JNIEnv* jenv, jobjectArray jargs,
                                        int* length) {
  size_t stringCount = jenv->GetArrayLength(jargs);

  if (length) {
    *length = stringCount;
  }

  if (!stringCount) {
    return nullptr;
  }

  char** argv = new char*[stringCount + 1];

  argv[stringCount] = nullptr;

  for (size_t ix = 0; ix < stringCount; ix++) {
    jstring string = (jstring)(jenv->GetObjectArrayElement(jargs, ix));
    const char* rawString = jenv->GetStringUTFChars(string, nullptr);
    const int strLength = jenv->GetStringUTFLength(string);
    argv[ix] = strndup(rawString, strLength);
    jenv->ReleaseStringUTFChars(string, rawString);
    jenv->DeleteLocalRef(string);
  }

  return argv;
}

static void FreeArgv(char** argv, int argc) {
  for (int ix = 0; ix < argc; ix++) {
    // String was allocated with strndup, so need to use free to deallocate.
    free(argv[ix]);
  }
  delete[] (argv);
}

// These are mirrored in GeckoLoader.java
#define PROCESS_TYPE_MAIN 0
#define PROCESS_TYPE_CHILD 1
#define PROCESS_TYPE_XPCSHELL 2

extern "C" APKOPEN_EXPORT void MOZ_JNICALL
Java_org_mozilla_gecko_mozglue_GeckoLoader_nativeRun(JNIEnv* jenv, jclass jc,
                                                     jobjectArray jargs,
                                                     jintArray jfds,
                                                     jint processType,
                                                     jstring outFilePath) {
  EnsureBaseProfilerInitialized();

  int argc = 0;
  char** argv = CreateArgvFromObjectArray(jenv, jargs, &argc);

  if (processType != PROCESS_TYPE_CHILD) {
    if (gBootstrap == nullptr) {
      FreeArgv(argv, argc);
      return;
    }

#ifdef MOZ_LINKER
    ElfLoader::Singleton.ExpectShutdown(false);
#endif
    gBootstrap->XRE_SetGeckoThreadEnv(jenv);
    if (!argv) {
      __android_log_print(
          ANDROID_LOG_FATAL, "mozglue", "Failed to get arguments for %s",
          processType == PROCESS_TYPE_XPCSHELL ? "XRE_XPCShellMain"
                                               : "XRE_main");
      return;
    }

    jsize size = jenv->GetArrayLength(jfds);
    if (size != 2) {
      __android_log_print(ANDROID_LOG_FATAL, "mozglue",
                          "Wrong number of file descriptors passed to a "
                          "browser/xpcshell process");
      return;
    }
    jint* fds = jenv->GetIntArrayElements(jfds, nullptr);
    int crashChildNotificationSocket = fds[0];
    int crashHelperSocket = fds[1];
    jenv->ReleaseIntArrayElements(jfds, fds, JNI_ABORT);

    if (processType == PROCESS_TYPE_XPCSHELL) {
      MOZ_ASSERT(outFilePath);
      const char* outFilePathRaw =
          jenv->GetStringUTFChars(outFilePath, nullptr);
      FILE* outFile = outFilePathRaw ? fopen(outFilePathRaw, "w") : nullptr;
      if (outFile) {
        XREShellData shellData;
        // We redirect both stdout and stderr to the same file, to conform with
        // what runxpcshell.py does on Desktop.
        shellData.outFile = outFile;
        shellData.errFile = outFile;
        shellData.crashChildNotificationSocket = crashChildNotificationSocket;
        shellData.crashHelperSocket = crashHelperSocket;
        int result =
            gBootstrap->XRE_XPCShellMain(argc, argv, nullptr, &shellData);
        fclose(shellData.outFile);
        if (result) {
          __android_log_print(ANDROID_LOG_INFO, "mozglue",
                              "XRE_XPCShellMain returned %d", result);
        }
      } else {
        __android_log_print(ANDROID_LOG_FATAL, "mozglue",
                            "XRE_XPCShellMain cannot open %s", outFilePathRaw);
      }
      if (outFilePathRaw) {
        jenv->ReleaseStringUTFChars(outFilePath, outFilePathRaw);
      }
    } else {
      BootstrapConfig config;
      config.appData = &sAppData;
      config.appDataPath = nullptr;
      config.crashChildNotificationSocket = crashChildNotificationSocket;
      config.crashHelperSocket = crashHelperSocket;

      int result = gBootstrap->XRE_main(argc, argv, config);
      if (result) {
        __android_log_print(ANDROID_LOG_INFO, "mozglue", "XRE_main returned %d",
                            result);
      }
    }
#ifdef MOZ_LINKER
    ElfLoader::Singleton.ExpectShutdown(true);
#endif
  } else {
    if (argc < 2) {
      FreeArgv(argv, argc);
      return;
    }

    SetGeckoProcessType(argv[--argc]);
    SetGeckoChildID(argv[--argc]);
#if defined(MOZ_MEMORY)
    // XRE_IsContentProcess is not accessible here
    jemalloc_reset_small_alloc_randomization(
        /* aRandomizeSmall */ GetGeckoProcessType() !=
        GeckoProcessType_Content);
#endif

    gBootstrap->XRE_SetAndroidChildFds(jenv, jfds);

    XREChildData childData;
    gBootstrap->XRE_InitChildProcess(argc, argv, &childData);
  }

#ifdef MOZ_WIDGET_ANDROID
#  ifdef MOZ_PROFILE_GENERATE
  gBootstrap->XRE_WriteLLVMProfData();
#  endif
#endif
  gBootstrap.reset();
  FreeArgv(argv, argc);
}

extern "C" APKOPEN_EXPORT mozglueresult ChildProcessInit(int argc,
                                                         char* argv[]) {
  EnsureBaseProfilerInitialized();

  if (argc < 2) {
    return FAILURE;
  }

  SetGeckoProcessType(argv[--argc]);
  SetGeckoChildID(argv[--argc]);
#if defined(MOZ_MEMORY)
  // XRE_IsContentProcess is not accessible here
  jemalloc_reset_small_alloc_randomization(
      /* aRandomizeSmall */ GetGeckoProcessType() != GeckoProcessType_Content);
#endif

  if (loadNSSLibs() != SUCCESS) {
    return FAILURE;
  }
  if (loadSQLiteLibs() != SUCCESS) {
    return FAILURE;
  }
  if (loadGeckoLibs().isErr()) {
    return FAILURE;
  }

  XREChildData childData;
  return NS_FAILED(gBootstrap->XRE_InitChildProcess(argc, argv, &childData));
}

// Does current process name end with ':media'?
static bool IsMediaProcess() {
  pid_t pid = getpid();
  char str[256];
  SprintfLiteral(str, "/proc/%d/cmdline", pid);
  FILE* f = fopen(str, "r");
  if (f) {
    fgets(str, sizeof(str), f);
    fclose(f);
    const size_t strLen = strlen(str);
    const char suffix[] = ":media";
    const size_t suffixLen = sizeof(suffix) - 1;
    if (strLen >= suffixLen &&
        !strncmp(str + strLen - suffixLen, suffix, suffixLen)) {
      return true;
    }
  }
  return false;
}

#ifndef SYS_rt_tgsigqueueinfo
#  define SYS_rt_tgsigqueueinfo __NR_rt_tgsigqueueinfo
#endif
/* Copy of http://androidxref.com/7.1.1_r6/xref/bionic/linker/debugger.cpp#262,
 * with debuggerd related code stripped.
 *
 * Copyright (C) 2008 The Android Open Source Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
static void CatchFatalSignals(int num, siginfo_t* info, void* context) {
  // It's possible somebody cleared the SA_SIGINFO flag, which would mean
  // our "info" arg holds an undefined value.
  struct sigaction action = {};
  if ((sigaction(num, nullptr, &action) < 0) ||
      !(action.sa_flags & SA_SIGINFO)) {
    info = nullptr;
  }

  // We need to return from the signal handler so that debuggerd can dump the
  // thread that crashed, but returning here does not guarantee that the signal
  // will be thrown again, even for SIGSEGV and friends, since the signal could
  // have been sent manually. Resend the signal with rt_tgsigqueueinfo(2) to
  // preserve the SA_SIGINFO contents.
  signal(num, SIG_DFL);

  struct siginfo si;
  if (!info) {
    memset(&si, 0, sizeof(si));
    si.si_code = SI_USER;
    si.si_pid = getpid();
    si.si_uid = getuid();
    info = &si;
  } else if (info->si_code >= 0 || info->si_code == SI_TKILL) {
    // rt_tgsigqueueinfo(2)'s documentation appears to be incorrect on kernels
    // that contain commit 66dd34a (3.9+). The manpage claims to only allow
    // negative si_code values that are not SI_TKILL, but 66dd34a changed the
    // check to allow all si_code values in calls coming from inside the house.
  }

  int rc = syscall(SYS_rt_tgsigqueueinfo, getpid(), gettid(), num, info);
  if (rc != 0) {
    __android_log_print(ANDROID_LOG_FATAL, "mozglue",
                        "failed to resend signal during crash: %s",
                        strerror(errno));
    _exit(0);
  }
}

extern "C" APKOPEN_EXPORT void MOZ_JNICALL
Java_org_mozilla_gecko_mozglue_GeckoLoader_suppressCrashDialog(JNIEnv* jenv,
                                                               jclass jc) {
  MOZ_RELEASE_ASSERT(IsMediaProcess(),
                     "Suppress crash dialog only for media process");
  // Restoring to SIG_DFL will crash on x86/Android M devices (see bug 1374556)
  // so copy Android code
  // (http://androidxref.com/7.1.1_r6/xref/bionic/linker/debugger.cpp#302). See
  // comments above CatchFatalSignals() for copyright notice.
  struct sigaction action;
  memset(&action, 0, sizeof(action));
  sigemptyset(&action.sa_mask);
  action.sa_sigaction = &CatchFatalSignals;
  action.sa_flags = SA_RESTART | SA_SIGINFO;

  // Use the alternate signal stack if available so we can catch stack
  // overflows.
  action.sa_flags |= SA_ONSTACK;

  sigaction(SIGABRT, &action, nullptr);
  sigaction(SIGBUS, &action, nullptr);
  sigaction(SIGFPE, &action, nullptr);
  sigaction(SIGILL, &action, nullptr);
  sigaction(SIGSEGV, &action, nullptr);
#if defined(SIGSTKFLT)
  sigaction(SIGSTKFLT, &action, nullptr);
#endif
  sigaction(SIGTRAP, &action, nullptr);
}
