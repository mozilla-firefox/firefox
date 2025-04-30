/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsXULAppAPI.h"
#include "jsapi.h"
#include "jsfriendapi.h"
#include "js/Array.h"             // JS::NewArrayObject
#include "js/CallAndConstruct.h"  // JS_CallFunctionValue
#include "js/CharacterEncoding.h"
#include "js/CompilationAndEvaluation.h"  // JS::Evaluate
#include "js/ContextOptions.h"
#include "js/Printf.h"
#include "js/PropertyAndElement.h"  // JS_DefineElement, JS_DefineFunctions, JS_DefineProperty
#include "js/PropertySpec.h"
#include "js/SourceText.h"  // JS::SourceText
#include "mozilla/ChaosMode.h"
#include "mozilla/dom/AutoEntryScript.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/IOInterposer.h"
#include "mozilla/Preferences.h"
#include "mozilla/Unused.h"
#include "mozilla/Utf8.h"  // mozilla::Utf8Unit
#include "nsServiceManagerUtils.h"
#include "nsComponentManagerUtils.h"
#include "nsExceptionHandler.h"
#include "nsIServiceManager.h"
#include "nsIFile.h"
#include "nsString.h"
#include "nsIDirectoryService.h"
#include "nsDirectoryServiceDefs.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nscore.h"
#include "nsArrayEnumerator.h"
#include "nsCOMArray.h"
#include "nsDirectoryServiceUtils.h"
#include "nsCOMPtr.h"
#include "nsJSPrincipals.h"
#include "nsJSUtils.h"
#include "xpcpublic.h"
#include "xpcprivate.h"
#include "SystemGlobal.h"
#include "nsIScriptSecurityManager.h"
#include "nsIPrincipal.h"
#include "nsJSUtils.h"

#include "nsIXULRuntime.h"
#include "nsIAppStartup.h"
#include "Components.h"
#include "ProfilerControl.h"

#ifdef ANDROID
#  include <android/log.h>
#  include "XREShellData.h"
#endif

#ifdef XP_WIN
#  include "mozilla/mscom/ProcessRuntime.h"
#  include "mozilla/ScopeExit.h"
#  include "mozilla/widget/AudioSession.h"
#  include "mozilla/WinDllServices.h"
#  include "mozilla/WindowsBCryptInitialization.h"
#  include <windows.h>
#  if defined(MOZ_SANDBOX)
#    include "XREShellData.h"
#    include "sandboxBroker.h"
#  endif
#endif

#ifdef MOZ_CODE_COVERAGE
#  include "mozilla/CodeCoverageHandler.h"
#endif

// all this crap is needed to do the interactive shell stuff
#include <stdlib.h>
#include <errno.h>
#ifdef HAVE_IO_H
#  include <io.h> /* for isatty() */
#endif
#ifdef HAVE_UNISTD_H
#  include <unistd.h> /* for isatty() */
#endif

#ifdef ENABLE_TESTS
#  include "xpctest_private.h"
#endif

// Fuzzing support for XPC runtime fuzzing
#ifdef FUZZING_INTERFACES
#  include "xpcrtfuzzing/xpcrtfuzzing.h"
#  include "XREShellData.h"
MOZ_RUNINIT static bool fuzzDoDebug = !!getenv("MOZ_FUZZ_DEBUG");
MOZ_RUNINIT static bool fuzzHaveModule = !!getenv("FUZZER");
#endif  // FUZZING_INTERFACES

using namespace mozilla;
using namespace JS;
using mozilla::dom::AutoEntryScript;
using mozilla::dom::AutoJSAPI;

class XPCShellDirProvider : public nsIDirectoryServiceProvider2 {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIDIRECTORYSERVICEPROVIDER
  NS_DECL_NSIDIRECTORYSERVICEPROVIDER2

  XPCShellDirProvider() = default;
  ~XPCShellDirProvider() = default;

  // The platform resource folder
  void SetGREDirs(nsIFile* greDir);
  void ClearGREDirs() {
    mGREDir = nullptr;
    mGREBinDir = nullptr;
  }
  // The application resource folder
  void SetAppDir(nsIFile* appFile);
  void ClearAppDir() { mAppDir = nullptr; }
  // The app executable
  void SetAppFile(nsIFile* appFile);
  void ClearAppFile() { mAppFile = nullptr; }

 private:
  nsCOMPtr<nsIFile> mGREDir;
  nsCOMPtr<nsIFile> mGREBinDir;
  nsCOMPtr<nsIFile> mAppDir;
  nsCOMPtr<nsIFile> mAppFile;
};

#ifdef XP_WIN
class MOZ_STACK_CLASS AutoAudioSession {
 public:
  AutoAudioSession() { widget::StartAudioSession(); }

  ~AutoAudioSession() { widget::StopAudioSession(); }
};
#endif

#define EXITCODE_RUNTIME_ERROR 3
#define EXITCODE_FILE_NOT_FOUND 4

static FILE* gOutFile = nullptr;
static FILE* gErrFile = nullptr;
static FILE* gInFile = nullptr;

static int gExitCode = 0;
static bool gQuitting = false;
static bool reportWarnings = true;
static bool compileOnly = false;

static JSPrincipals* gJSPrincipals = nullptr;
static nsAutoString* gWorkingDirectory = nullptr;

static bool GetLocationProperty(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  if (!args.thisv().isObject()) {
    JS_ReportErrorASCII(cx, "Unexpected this value for GetLocationProperty");
    return false;
  }
#if !defined(XP_WIN) && !defined(XP_UNIX)
  // XXX: your platform should really implement this
  return false;
#else
  JS::AutoFilename filename;
  if (JS::DescribeScriptedCaller(&filename, cx) && filename.get()) {
    NS_ConvertUTF8toUTF16 filenameString(filename.get());

#  if defined(XP_WIN)
    // replace forward slashes with backslashes,
    // since nsLocalFileWin chokes on them
    char16_t* start = filenameString.BeginWriting();
    char16_t* end = filenameString.EndWriting();

    while (start != end) {
      if (*start == L'/') {
        *start = L'\\';
      }
      start++;
    }
#  endif

    nsCOMPtr<nsIFile> location;
    Unused << NS_NewLocalFile(filenameString, getter_AddRefs(location));

    if (!location && gWorkingDirectory) {
      // could be a relative path, try appending it to the cwd
      // and then normalize
      nsAutoString absolutePath(*gWorkingDirectory);
      absolutePath.Append(filenameString);

      Unused << NS_NewLocalFile(absolutePath, getter_AddRefs(location));
    }

    if (location) {
      bool symlink;
      // don't normalize symlinks, because that's kind of confusing
      if (NS_SUCCEEDED(location->IsSymlink(&symlink)) && !symlink)
        location->Normalize();
      RootedObject locationObj(cx);
      RootedObject scope(cx, JS::CurrentGlobalOrNull(cx));
      nsresult rv = nsXPConnect::XPConnect()->WrapNative(
          cx, scope, location, NS_GET_IID(nsIFile), locationObj.address());
      if (NS_SUCCEEDED(rv) && locationObj) {
        args.rval().setObject(*locationObj);
      }
    }
  }

  return true;
#endif
}

static bool GetLine(JSContext* cx, char* bufp, FILE* file, const char* prompt) {
  fputs(prompt, gOutFile);
  fflush(gOutFile);

  char line[4096] = {'\0'};
  while (true) {
    if (fgets(line, sizeof line, file)) {
      strcpy(bufp, line);
      return true;
    }
    if (errno != EINTR) {
      return false;
    }
  }
}

static bool ReadLine(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // While 4096 might be quite arbitrary, this is something to be fixed in
  // bug 105707. It is also the same limit as in ProcessFile.
  char buf[4096];
  RootedString str(cx);

  /* If a prompt was specified, construct the string */
  if (args.length() > 0) {
    str = JS::ToString(cx, args[0]);
    if (!str) {
      return false;
    }
  } else {
    str = JS_GetEmptyString(cx);
  }

  /* Get a line from the infile */
  JS::UniqueChars strBytes = JS_EncodeStringToLatin1(cx, str);
  if (!strBytes || !GetLine(cx, buf, gInFile, strBytes.get())) {
    return false;
  }

  /* Strip newline character added by GetLine() */
  unsigned int buflen = strlen(buf);
  if (buflen == 0) {
    if (feof(gInFile)) {
      args.rval().setNull();
      return true;
    }
  } else if (buf[buflen - 1] == '\n') {
    --buflen;
  }

  /* Turn buf into a JSString */
  str = JS_NewStringCopyN(cx, buf, buflen);
  if (!str) {
    return false;
  }

  args.rval().setString(str);
  return true;
}

static bool Print(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  args.rval().setUndefined();

#ifdef FUZZING_INTERFACES
  if (fuzzHaveModule && !fuzzDoDebug) {
    // When fuzzing and not debugging, suppress any print() output,
    // as it slows down fuzzing and makes libFuzzer's output hard
    // to read.
    return true;
  }
#endif  // FUZZING_INTERFACES

  RootedString str(cx);
  nsAutoCString utf8output;

  for (unsigned i = 0; i < args.length(); i++) {
    str = ToString(cx, args[i]);
    if (!str) {
      return false;
    }

    JS::UniqueChars utf8str = JS_EncodeStringToUTF8(cx, str);
    if (!utf8str) {
      return false;
    }

    if (i) {
      utf8output.Append(' ');
    }
    utf8output.Append(utf8str.get(), strlen(utf8str.get()));
  }
  utf8output.Append('\n');
  fputs(utf8output.get(), gOutFile);
  fflush(gOutFile);
  return true;
}

static bool Dump(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  args.rval().setUndefined();

  if (!args.length()) {
    return true;
  }

  RootedString str(cx, ToString(cx, args[0]));
  if (!str) {
    return false;
  }

  JS::UniqueChars utf8str = JS_EncodeStringToUTF8(cx, str);
  if (!utf8str) {
    return false;
  }

#ifdef ANDROID
  __android_log_print(ANDROID_LOG_INFO, "Gecko", "%s", utf8str.get());
#endif
#ifdef XP_WIN
  if (IsDebuggerPresent()) {
    nsAutoJSString wstr;
    if (!wstr.init(cx, str)) {
      return false;
    }
    OutputDebugStringW(wstr.get());
  }
#endif
  fputs(utf8str.get(), gOutFile);
  fflush(gOutFile);
  return true;
}

static bool Load(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  JS::RootedObject thisObject(cx);
  if (!args.computeThis(cx, &thisObject)) {
    return false;
  }
  if (!JS_IsGlobalObject(thisObject)) {
    JS_ReportErrorASCII(cx, "Trying to load() into a non-global object");
    return false;
  }

  RootedString str(cx);
  for (unsigned i = 0; i < args.length(); i++) {
    str = ToString(cx, args[i]);
    if (!str) {
      return false;
    }
    JS::UniqueChars filename = JS_EncodeStringToUTF8(cx, str);
    if (!filename) {
      return false;
    }
    JS::CompileOptions options(cx);
    options.setIsRunOnce(true).setSkipFilenameValidation(true);
    JS::Rooted<JSScript*> script(
        cx, JS::CompileUtf8Path(cx, options, filename.get()));
    if (!script) {
      return false;
    }

    if (!compileOnly) {
      if (!JS_ExecuteScript(cx, script)) {
        return false;
      }
    }
  }
  args.rval().setUndefined();
  return true;
}

static bool Quit(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  gExitCode = 0;
  if (!ToInt32(cx, args.get(0), &gExitCode)) {
    return false;
  }

  gQuitting = true;
  JS::ReportUncatchableException(cx);
  return false;
}

static bool DumpXPC(JSContext* cx, unsigned argc, Value* vp) {
  JS::CallArgs args = CallArgsFromVp(argc, vp);

  uint16_t depth = 2;
  if (args.length() > 0) {
    if (!JS::ToUint16(cx, args[0], &depth)) {
      return false;
    }
  }

  nsXPConnect::XPConnect()->DebugDump(int16_t(depth));
  args.rval().setUndefined();
  return true;
}

static bool GC(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  JS_GC(cx);

  args.rval().setUndefined();
  return true;
}

#ifdef JS_GC_ZEAL
static bool GCZeal(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  uint32_t zeal;
  if (!ToUint32(cx, args.get(0), &zeal)) {
    return false;
  }

  JS::SetGCZeal(cx, uint8_t(zeal), JS::ShellDefaultGCZealFrequency);
  args.rval().setUndefined();
  return true;
}
#endif

static bool SendCommand(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  if (args.length() == 0) {
    JS_ReportErrorASCII(cx, "Function takes at least one argument!");
    return false;
  }

  RootedString str(cx, ToString(cx, args[0]));
  if (!str) {
    JS_ReportErrorASCII(cx, "Could not convert argument 1 to string!");
    return false;
  }

  if (args.get(1).isObject() && !JS_ObjectIsFunction(&args[1].toObject())) {
    JS_ReportErrorASCII(cx, "Could not convert argument 2 to function!");
    return false;
  }

  if (!XRE_SendTestShellCommand(
          cx, str, args.length() > 1 ? args[1].address() : nullptr)) {
    JS_ReportErrorASCII(cx, "Couldn't send command!");
    return false;
  }

  args.rval().setUndefined();
  return true;
}

static bool Options(JSContext* cx, unsigned argc, Value* vp) {
  JS::CallArgs args = CallArgsFromVp(argc, vp);

  RootedString str(cx);
  JS::UniqueChars opt;
  if (args.length() > 0) {
    str = ToString(cx, args[0]);
    if (!str) {
      return false;
    }

    opt = JS_EncodeStringToUTF8(cx, str);
    if (!opt) {
      return false;
    }

    JS_ReportErrorUTF8(cx, "unknown option name '%s'.", opt.get());
    return false;
  }

  args.rval().setString(JS_GetEmptyString(cx));
  return true;
}

static PersistentRootedValue* sScriptedInterruptCallback = nullptr;

static bool XPCShellInterruptCallback(JSContext* cx) {
  MOZ_ASSERT(sScriptedInterruptCallback->initialized());
  RootedValue callback(cx, *sScriptedInterruptCallback);

  // If no interrupt callback was set by script, no-op.
  if (callback.isUndefined()) {
    return true;
  }

  MOZ_ASSERT(js::IsFunctionObject(&callback.toObject()));

  JSAutoRealm ar(cx, &callback.toObject());
  RootedValue rv(cx);
  if (!JS_CallFunctionValue(cx, nullptr, callback,
                            JS::HandleValueArray::empty(), &rv) ||
      !rv.isBoolean()) {
    NS_WARNING("Scripted interrupt callback failed! Terminating script.");
    JS_ClearPendingException(cx);
    return false;
  }

  return rv.toBoolean();
}

static bool SetInterruptCallback(JSContext* cx, unsigned argc, Value* vp) {
  MOZ_ASSERT(sScriptedInterruptCallback->initialized());

  // Sanity-check args.
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
  if (args.length() != 1) {
    JS_ReportErrorASCII(cx, "Wrong number of arguments");
    return false;
  }

  // Allow callers to remove the interrupt callback by passing undefined.
  if (args[0].isUndefined()) {
    *sScriptedInterruptCallback = UndefinedValue();
    return true;
  }

  // Otherwise, we should have a function object.
  if (!args[0].isObject() || !js::IsFunctionObject(&args[0].toObject())) {
    JS_ReportErrorASCII(cx, "Argument must be a function");
    return false;
  }

  *sScriptedInterruptCallback = args[0];

  return true;
}

static bool SimulateNoScriptActivity(JSContext* cx, unsigned argc, Value* vp) {
  // Sanity-check args.
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
  if (args.length() != 1 || !args[0].isInt32() || args[0].toInt32() < 0) {
    JS_ReportErrorASCII(cx, "Expected a positive integer argument");
    return false;
  }

  // This mimics mozilla::SpinEventLoopUntil but instead of spinning the
  // event loop we sleep, to make sure we don't run script.
  xpc::AutoScriptActivity asa(false);
  PR_Sleep(PR_SecondsToInterval(args[0].toInt32()));

  args.rval().setUndefined();
  return true;
}

static bool RegisterAppManifest(JSContext* cx, unsigned argc, Value* vp) {
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
  if (args.length() != 1) {
    JS_ReportErrorASCII(cx, "Wrong number of arguments");
    return false;
  }
  if (!args[0].isObject()) {
    JS_ReportErrorASCII(cx,
                        "Expected object as argument 1 to registerAppManifest");
    return false;
  }

  Rooted<JSObject*> arg1(cx, &args[0].toObject());
  nsCOMPtr<nsIFile> file;
  nsresult rv = nsXPConnect::XPConnect()->WrapJS(cx, arg1, NS_GET_IID(nsIFile),
                                                 getter_AddRefs(file));
  if (NS_FAILED(rv)) {
    XPCThrower::Throw(rv, cx);
    return false;
  }
  rv = XRE_AddManifestLocation(NS_APP_LOCATION, file);
  if (NS_FAILED(rv)) {
    XPCThrower::Throw(rv, cx);
    return false;
  }
  return true;
}

#ifdef ANDROID
static bool ChangeTestShellDir(JSContext* cx, unsigned argc, Value* vp) {
  // This method should only be used by testing/xpcshell/head.js to change to
  // the correct directory on Android Remote XPCShell tests.
  //
  // TODO: Bug 1801725 - Find a more ergonomic way to do this than exposing
  // identical methods in XPCShellEnvironment and XPCShellImpl to chdir on
  // android for Remote XPCShell tests on Android.
  CallArgs args = CallArgsFromVp(argc, vp);

  if (args.length() != 1) {
    JS_ReportErrorASCII(cx, "changeTestShellDir() takes one argument");
    return false;
  }

  nsAutoJSCString path;
  if (!path.init(cx, args[0])) {
    JS_ReportErrorASCII(
        cx, "changeTestShellDir(): could not convert argument 1 to string");
    return false;
  }

  if (chdir(path.get())) {
    JS_ReportErrorASCII(cx, "changeTestShellDir(): could not change directory");
    return false;
  }

  return true;
}
#endif

#ifdef ENABLE_TESTS
static bool RegisterXPCTestComponents(JSContext* cx, unsigned argc, Value* vp) {
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
  if (args.length() != 0) {
    JS_ReportErrorASCII(cx, "Wrong number of arguments");
    return false;
  }
  nsresult rv = xpcTestRegisterComponents();
  if (NS_FAILED(rv)) {
    XPCThrower::Throw(rv, cx);
    return false;
  }
  return true;
}
#endif

static const JSFunctionSpec glob_functions[] = {
    // clang-format off
    JS_FN("print",           Print,          0,0),
    JS_FN("readline",        ReadLine,       1,0),
    JS_FN("load",            Load,           1,0),
    JS_FN("quit",            Quit,           0,0),
    JS_FN("dumpXPC",         DumpXPC,        1,0),
    JS_FN("dump",            Dump,           1,0),
    JS_FN("gc",              GC,             0,0),
#ifdef JS_GC_ZEAL
    JS_FN("gczeal",          GCZeal,         1,0),
#endif
    JS_FN("options",         Options,        0,0),
    JS_FN("sendCommand",     SendCommand,    1,0),
    JS_FN("atob",            xpc::Atob,      1,0),
    JS_FN("btoa",            xpc::Btoa,      1,0),
    JS_FN("setInterruptCallback", SetInterruptCallback, 1,0),
    JS_FN("simulateNoScriptActivity", SimulateNoScriptActivity, 1,0),
    JS_FN("registerAppManifest", RegisterAppManifest, 1, 0),
#ifdef ANDROID
    JS_FN("changeTestShellDir", ChangeTestShellDir, 1,0),
#endif
#ifdef ENABLE_TESTS
    JS_FN("registerXPCTestComponents", RegisterXPCTestComponents, 0, 0),
#endif
    JS_FS_END
    // clang-format on
};

/***************************************************************************/

typedef enum JSShellErrNum {
#define MSG_DEF(name, number, count, exception, format) name = number,
#include "jsshell.msg"
#undef MSG_DEF
  JSShellErr_Limit
} JSShellErrNum;

static const JSErrorFormatString jsShell_ErrorFormatString[JSShellErr_Limit] = {
#define MSG_DEF(name, number, count, exception, format) {#name, format, count},
#include "jsshell.msg"
#undef MSG_DEF
};

static const JSErrorFormatString* my_GetErrorMessage(
    void* userRef, const unsigned errorNumber) {
  if (errorNumber == 0 || errorNumber >= JSShellErr_Limit) {
    return nullptr;
  }

  return &jsShell_ErrorFormatString[errorNumber];
}

static bool ProcessUtf8Line(AutoJSAPI& jsapi, const char* buffer,
                            int startline) {
  JSContext* cx = jsapi.cx();
  JS::CompileOptions options(cx);
  options.setFileAndLine("typein", startline)
      .setIsRunOnce(true)
      .setSkipFilenameValidation(true);

  JS::SourceText<mozilla::Utf8Unit> srcBuf;
  if (!srcBuf.init(cx, buffer, strlen(buffer), JS::SourceOwnership::Borrowed)) {
    return false;
  }

  JS::RootedScript script(cx, JS::Compile(cx, options, srcBuf));
  if (!script) {
    return false;
  }
  if (compileOnly) {
    return true;
  }

  JS::RootedValue result(cx);
  if (!JS_ExecuteScript(cx, script, &result)) {
    return false;
  }

  if (result.isUndefined()) {
    return true;
  }

  RootedString str(cx, JS::ToString(cx, result));
  if (!str) {
    return false;
  }

  JS::UniqueChars bytes = JS_EncodeStringToLatin1(cx, str);
  if (!bytes) {
    return false;
  }

  fprintf(gOutFile, "%s\n", bytes.get());
  return true;
}

static bool ProcessFile(AutoJSAPI& jsapi, const char* filename, FILE* file,
                        bool forceTTY) {
  JSContext* cx = jsapi.cx();
  JS::Rooted<JSObject*> global(cx, JS::CurrentGlobalOrNull(cx));
  MOZ_ASSERT(global);

  if (forceTTY) {
    file = stdin;
  } else if (!isatty(fileno(file))) {
    /*
     * It's not interactive - just execute it.
     *
     * Support the UNIX #! shell hack; gobble the first line if it starts
     * with '#'.
     */
    int ch = fgetc(file);
    if (ch == '#') {
      while ((ch = fgetc(file)) != EOF) {
        if (ch == '\n' || ch == '\r') {
          break;
        }
      }
    }
    ungetc(ch, file);

    JS::UniqueChars filenameUtf8 = JS::EncodeNarrowToUtf8(jsapi.cx(), filename);
    if (!filenameUtf8) {
      return false;
    }

    JS::RootedScript script(cx);
    JS::RootedValue unused(cx);
    JS::CompileOptions options(cx);
    options.setFileAndLine(filenameUtf8.get(), 1)
        .setIsRunOnce(true)
        .setNoScriptRval(true)
        .setSkipFilenameValidation(true);
    script = JS::CompileUtf8File(cx, options, file);
    if (!script) {
      return false;
    }
    return compileOnly || JS_ExecuteScript(cx, script, &unused);
  }

  /* It's an interactive filehandle; drop into read-eval-print loop. */
  int lineno = 1;
  bool hitEOF = false;
  do {
    char buffer[4096];
    char* bufp = buffer;
    *bufp = '\0';

    /*
     * Accumulate lines until we get a 'compilable unit' - one that either
     * generates an error (before running out of source) or that compiles
     * cleanly.  This should be whenever we get a complete statement that
     * coincides with the end of a line.
     */
    int startline = lineno;
    do {
      if (!GetLine(cx, bufp, file, startline == lineno ? "js> " : "")) {
        hitEOF = true;
        break;
      }
      bufp += strlen(bufp);
      lineno++;
    } while (
        !JS_Utf8BufferIsCompilableUnit(cx, global, buffer, strlen(buffer)));

    if (!ProcessUtf8Line(jsapi, buffer, startline)) {
      jsapi.ReportException();
    }
  } while (!hitEOF && !gQuitting);

  fprintf(gOutFile, "\n");
  return true;
}

static bool Process(AutoJSAPI& jsapi, const char* filename, bool forceTTY) {
  FILE* file;

  if (forceTTY || !filename || strcmp(filename, "-") == 0) {
    file = stdin;
  } else {
    file = fopen(filename, "r");
    if (!file) {
      /*
       * Use Latin1 variant here because the encoding of the return value
       * of strerror function can be non-UTF-8.
       */
      JS_ReportErrorNumberLatin1(jsapi.cx(), my_GetErrorMessage, nullptr,
                                 JSSMSG_CANT_OPEN, filename, strerror(errno));
      gExitCode = EXITCODE_FILE_NOT_FOUND;
      return false;
    }
  }

  bool ok = ProcessFile(jsapi, filename, file, forceTTY);
  if (file != stdin) {
    fclose(file);
  }
  return ok;
}

static int usage() {
  fprintf(gErrFile, "%s\n", JS_GetImplementationVersion());
  fprintf(
      gErrFile,
      "usage: xpcshell [-g gredir] [-a appdir] [-r manifest]... [-WwxiCmIp] "
      "[-f scriptfile] [-e script] [scriptfile] [scriptarg...]\n");
  return 2;
}

static bool printUsageAndSetExitCode() {
  gExitCode = usage();
  return false;
}

static bool ProcessArgs(AutoJSAPI& jsapi, char** argv, int argc,
                        XPCShellDirProvider* aDirProvider) {
  JSContext* cx = jsapi.cx();
  const char rcfilename[] = "xpcshell.js";
  FILE* rcfile;
  int rootPosition;
  JS::Rooted<JSObject*> argsObj(cx);
  char* filename = nullptr;
  bool isInteractive = true;
  bool forceTTY = false;

  rcfile = fopen(rcfilename, "r");
  if (rcfile) {
    printf("[loading '%s'...]\n", rcfilename);
    bool ok = ProcessFile(jsapi, rcfilename, rcfile, false);
    fclose(rcfile);
    if (!ok) {
      return false;
    }
  }

  JS::Rooted<JSObject*> global(cx, JS::CurrentGlobalOrNull(cx));

  /*
   * Scan past all optional arguments so we can create the arguments object
   * before processing any -f options, which must interleave properly with
   * -v and -w options.  This requires two passes, and without getopt, we'll
   * have to keep the option logic here and in the second for loop in sync.
   * First of all, find out the first argument position which will be passed
   * as a script file to be executed.
   */
  for (rootPosition = 0; rootPosition < argc; rootPosition++) {
    if (argv[rootPosition][0] != '-' || argv[rootPosition][1] == '\0') {
      ++rootPosition;
      break;
    }

    bool isPairedFlag =
        argv[rootPosition][0] != '\0' &&
        (argv[rootPosition][1] == 'v' || argv[rootPosition][1] == 'f' ||
         argv[rootPosition][1] == 'e');
    if (isPairedFlag && rootPosition < argc - 1) {
      ++rootPosition;  // Skip over the 'foo' portion of |-v foo|, |-f foo|, or
                       // |-e foo|.
    }
  }

  /*
   * Create arguments early and define it to root it, so it's safe from any
   * GC calls nested below, and so it is available to -f <file> arguments.
   */
  argsObj = JS::NewArrayObject(cx, 0);
  if (!argsObj) {
    return 1;
  }
  if (!JS_DefineProperty(cx, global, "arguments", argsObj, 0)) {
    return 1;
  }

  for (int j = 0, length = argc - rootPosition; j < length; j++) {
    RootedString str(cx, JS_NewStringCopyZ(cx, argv[rootPosition++]));
    if (!str || !JS_DefineElement(cx, argsObj, j, str, JSPROP_ENUMERATE)) {
      return 1;
    }
  }

  for (int i = 0; i < argc; i++) {
    if (argv[i][0] != '-' || argv[i][1] == '\0') {
      filename = argv[i++];
      isInteractive = false;
      break;
    }
    switch (argv[i][1]) {
      case 'W':
        reportWarnings = false;
        break;
      case 'w':
        reportWarnings = true;
        break;
      case 'x':
        break;
      case 'd':
        /* This used to try to turn on the debugger. */
        break;
      case 'm':
        break;
      case 'f':
        if (++i == argc) {
          return printUsageAndSetExitCode();
        }
        if (!Process(jsapi, argv[i], false)) {
          return false;
        }
        /*
         * XXX: js -f foo.js should interpret foo.js and then
         * drop into interactive mode, but that breaks test
         * harness. Just execute foo.js for now.
         */
        isInteractive = false;
        break;
      case 'i':
        isInteractive = forceTTY = true;
        break;
      case 'e': {
        RootedValue rval(cx);

        if (++i == argc) {
          return printUsageAndSetExitCode();
        }

        JS::CompileOptions opts(cx);
        opts.setSkipFilenameValidation(true);
        opts.setFileAndLine("-e", 1);

        JS::SourceText<mozilla::Utf8Unit> srcBuf;
        if (srcBuf.init(cx, argv[i], strlen(argv[i]),
                        JS::SourceOwnership::Borrowed)) {
          JS::Evaluate(cx, opts, srcBuf, &rval);
        }

        isInteractive = false;
        break;
      }
      case 'C':
        compileOnly = true;
        isInteractive = false;
        break;
      default:
        return printUsageAndSetExitCode();
    }
  }

  if (filename || isInteractive) {
    return Process(jsapi, filename, forceTTY);
  }
  return true;
}

/***************************************************************************/

static bool GetCurrentWorkingDirectory(nsAString& workingDirectory) {
#if !defined(XP_WIN) && !defined(XP_UNIX)
  // XXX: your platform should really implement this
  return false;
#elif XP_WIN
  DWORD requiredLength = GetCurrentDirectoryW(0, nullptr);
  workingDirectory.SetLength(requiredLength);
  GetCurrentDirectoryW(workingDirectory.Length(),
                       (LPWSTR)workingDirectory.BeginWriting());
  // we got a trailing null there
  workingDirectory.SetLength(requiredLength);
  workingDirectory.Replace(workingDirectory.Length() - 1, 1, L'\\');
#elif defined(XP_UNIX)
  nsAutoCString cwd;
  // 1024 is just a guess at a sane starting value
  size_t bufsize = 1024;
  char* result = nullptr;
  while (result == nullptr) {
    cwd.SetLength(bufsize);
    result = getcwd(cwd.BeginWriting(), cwd.Length());
    if (!result) {
      if (errno != ERANGE) {
        return false;
      }
      // need to make the buffer bigger
      bufsize *= 2;
    }
  }
  // size back down to the actual string length
  cwd.SetLength(strlen(result) + 1);
  cwd.Replace(cwd.Length() - 1, 1, '/');
  CopyUTF8toUTF16(cwd, workingDirectory);
#endif
  return true;
}

static JSSecurityCallbacks shellSecurityCallbacks;

int XRE_XPCShellMain(int argc, char** argv, char** envp,
                     const XREShellData* aShellData) {
  MOZ_ASSERT(aShellData);

  JSContext* cx;
  int result = 0;
  nsresult rv;

#ifdef ANDROID
  gOutFile = aShellData->outFile;
  gErrFile = aShellData->errFile;
#else
  gOutFile = stdout;
  gErrFile = stderr;
#endif
  gInFile = stdin;

  NS_LogInit();

  mozilla::LogModule::Init(argc, argv);

  // This guard ensures that all threads that attempt to register themselves
  // with the IOInterposer will be properly tracked.
  mozilla::AutoIOInterposer ioInterposerGuard;
  ioInterposerGuard.Init();

  XRE_InitCommandLine(argc, argv);

  char aLocal;
  profiler_init(&aLocal);

#ifdef MOZ_ASAN_REPORTER
  PR_SetEnv("MOZ_DISABLE_ASAN_REPORTER=1");
#endif

  if (auto* featureStr = PR_GetEnv("MOZ_CHAOSMODE")) {
    ChaosFeature feature = ChaosFeature::Any;
    long featureInt = strtol(featureStr, nullptr, 16);
    if (featureInt) {
      // NOTE: MOZ_CHAOSMODE=0 or a non-hex value maps to Any feature.
      feature = static_cast<ChaosFeature>(featureInt);
    }
    ChaosMode::SetChaosFeature(feature);
    ChaosMode::enterChaosMode();
    MOZ_ASSERT(ChaosMode::isActive(ChaosFeature::Any));
  }

  if (ChaosMode::isActive(ChaosFeature::Any)) {
    printf_stderr(
        "*** You are running in chaos test mode. See ChaosMode.h. ***\n");
  }

#ifdef XP_WIN
  // Some COM settings are global to the process and must be set before any non-
  // trivial COM is run in the application. Since these settings may affect
  // stability, we should instantiate COM ASAP so that we can ensure that these
  // global settings are configured before anything can interfere.
  mscom::ProcessRuntime mscom;

#  ifdef MOZ_SANDBOX
  nsAutoString binDirPath;
#  endif
#endif

  // The provider needs to outlive the call to shutting down XPCOM.
  XPCShellDirProvider dirprovider;

  {  // Start scoping nsCOMPtrs
    nsCOMPtr<nsIFile> appFile;
    rv = XRE_GetBinaryPath(getter_AddRefs(appFile));
    if (NS_FAILED(rv)) {
      printf("Couldn't find application file.\n");
      return 1;
    }
    nsCOMPtr<nsIFile> appDir;
    rv = appFile->GetParent(getter_AddRefs(appDir));
    if (NS_FAILED(rv)) {
      printf("Couldn't get application directory.\n");
      return 1;
    }

#if defined(XP_WIN) && defined(MOZ_SANDBOX)
    // We need the binary directory to initialize the windows sandbox.
    MOZ_ALWAYS_SUCCEEDS(appDir->GetPath(binDirPath));
#endif

    dirprovider.SetAppFile(appFile);

    nsCOMPtr<nsIFile> greDir;
    if (argc > 1 && !strcmp(argv[1], "-g")) {
      if (argc < 3) {
        return usage();
      }

      rv = XRE_GetFileFromPath(argv[2], getter_AddRefs(greDir));
      if (NS_FAILED(rv)) {
        printf("Couldn't use given GRE dir.\n");
        return 1;
      }

      dirprovider.SetGREDirs(greDir);

      argc -= 2;
      argv += 2;
    } else {
#ifdef XP_MACOSX
      // On OSX, the GreD needs to point to Contents/Resources in the .app
      // bundle. Libraries will be loaded at a relative path to GreD, i.e.
      // ../MacOS.
      nsCOMPtr<nsIFile> tmpDir;
      XRE_GetFileFromPath(argv[0], getter_AddRefs(greDir));
      greDir->GetParent(getter_AddRefs(tmpDir));
      tmpDir->Clone(getter_AddRefs(greDir));
      tmpDir->SetNativeLeafName("Resources"_ns);
      bool dirExists = false;
      tmpDir->Exists(&dirExists);
      if (dirExists) {
        greDir = tmpDir.forget();
      }
      dirprovider.SetGREDirs(greDir);
#else
      nsAutoString workingDir;
      if (!GetCurrentWorkingDirectory(workingDir)) {
        printf("GetCurrentWorkingDirectory failed.\n");
        return 1;
      }
      rv = NS_NewLocalFile(workingDir, getter_AddRefs(greDir));
      if (NS_FAILED(rv)) {
        printf("NS_NewLocalFile failed.\n");
        return 1;
      }
#endif
    }

    if (argc > 1 && !strcmp(argv[1], "-a")) {
      if (argc < 3) {
        return usage();
      }

      nsCOMPtr<nsIFile> dir;
      rv = XRE_GetFileFromPath(argv[2], getter_AddRefs(dir));
      if (NS_SUCCEEDED(rv)) {
        appDir = dir;
        dirprovider.SetAppDir(appDir);
      }
      if (NS_FAILED(rv)) {
        printf("Couldn't use given appdir.\n");
        return 1;
      }
      argc -= 2;
      argv += 2;
    }

    while (argc > 1 && !strcmp(argv[1], "-r")) {
      if (argc < 3) {
        return usage();
      }

      nsCOMPtr<nsIFile> lf;
      rv = XRE_GetFileFromPath(argv[2], getter_AddRefs(lf));
      if (NS_FAILED(rv)) {
        printf("Couldn't get manifest file.\n");
        return 1;
      }
      XRE_AddManifestLocation(NS_APP_LOCATION, lf);

      argc -= 2;
      argv += 2;
    }

    const char* val = getenv("MOZ_CRASHREPORTER");
    if (val && *val && !CrashReporter::IsDummy()) {
      nsCOMPtr<nsIFile> greBinDir;
      bool persistent = false;
      rv = dirprovider.GetFile(NS_GRE_BIN_DIR, &persistent,
                               getter_AddRefs(greBinDir));
      if (NS_FAILED(rv)) {
        printf("Could not get the GreBinD directory\n");
        return 1;
      }

#if defined(MOZ_WIDGET_ANDROID)
      CrashReporter::SetCrashHelperPipes(
          aShellData->crashChildNotificationSocket,
          aShellData->crashHelperSocket);
#endif  // defined(MOZ_WIDGET_ANDROID)

      rv = CrashReporter::SetExceptionHandler(greBinDir, true);
      if (NS_FAILED(rv)) {
        printf("CrashReporter::SetExceptionHandler failed!\n");
        return 1;
      }
      MOZ_ASSERT(CrashReporter::GetEnabled());
    }

    if (argc > 1 && !strcmp(argv[1], "--greomni")) {
      nsCOMPtr<nsIFile> greOmni;
      XRE_GetFileFromPath(argv[2], getter_AddRefs(greOmni));
      XRE_InitOmnijar(greOmni, greOmni);
      argc -= 2;
      argv += 2;
    }

    rv = NS_InitXPCOM(nullptr, appDir, &dirprovider);
    if (NS_FAILED(rv)) {
      printf("NS_InitXPCOM failed!\n");
      return 1;
    }

    // xpc::ErrorReport::LogToConsoleWithStack needs this to print errors
    // to stderr.
    Preferences::SetBool("browser.dom.window.dump.enabled", true);
    Preferences::SetBool("devtools.console.stdout.chrome", true);

    AutoJSAPI jsapi;
    jsapi.Init();
    cx = jsapi.cx();

    // Override the default XPConnect interrupt callback. We could store the
    // old one and restore it before shutting down, but there's not really a
    // reason to bother.
    sScriptedInterruptCallback = new PersistentRootedValue;
    sScriptedInterruptCallback->init(cx, UndefinedValue());

    JS_AddInterruptCallback(cx, XPCShellInterruptCallback);

    argc--;
    argv++;

    nsCOMPtr<nsIPrincipal> systemprincipal;
    // Fetch the system principal and store it away in a global, to use for
    // script compilation in Load() and ProcessFile() (including interactive
    // eval loop)
    {
      nsCOMPtr<nsIScriptSecurityManager> securityManager =
          do_GetService(NS_SCRIPTSECURITYMANAGER_CONTRACTID, &rv);
      if (NS_SUCCEEDED(rv) && securityManager) {
        rv = securityManager->GetSystemPrincipal(
            getter_AddRefs(systemprincipal));
        if (NS_FAILED(rv)) {
          fprintf(gErrFile,
                  "+++ Failed to obtain SystemPrincipal from "
                  "ScriptSecurityManager service.\n");
        } else {
          // fetch the JS principals and stick in a global
          gJSPrincipals = nsJSPrincipals::get(systemprincipal);
          JS_HoldPrincipals(gJSPrincipals);
        }
      } else {
        fprintf(gErrFile,
                "+++ Failed to get ScriptSecurityManager service, running "
                "without principals");
      }
    }

    const JSSecurityCallbacks* scb = JS_GetSecurityCallbacks(cx);
    MOZ_ASSERT(
        scb,
        "We are assuming that nsScriptSecurityManager::Init() has been run");
    shellSecurityCallbacks = *scb;
    JS_SetSecurityCallbacks(cx, &shellSecurityCallbacks);

    auto systemGlobal = MakeRefPtr<SystemGlobal>();

    // Make the default XPCShell global use a fresh zone (rather than the
    // System Zone) to improve cross-zone test coverage.
    JS::RealmOptions options;
    options.creationOptions().setNewCompartmentAndZone();
    xpc::SetPrefableRealmOptions(options);

    // Even if we're building in a configuration where source is
    // discarded, there's no reason to do that on XPCShell, and doing so
    // might break various automation scripts.
    options.behaviors().setDiscardSource(false);

    JS::Rooted<JSObject*> glob(cx);
    rv = xpc::InitClassesWithNewWrappedGlobal(
        cx, static_cast<nsIGlobalObject*>(systemGlobal), systemprincipal, 0,
        options, &glob);
    if (NS_FAILED(rv)) {
      return 1;
    }

    // Initialize e10s check on the main thread, if not already done
    BrowserTabsRemoteAutostart();
#if defined(XP_WIN)
    // Plugin may require audio session if installed plugin can initialize
    // asynchronized.
    AutoAudioSession audioSession;

    // Ensure that DLL Services are running
    RefPtr<DllServices> dllSvc(DllServices::Get());
    dllSvc->StartUntrustedModulesProcessor(true);
    auto dllServicesDisable =
        MakeScopeExit([&dllSvc]() { dllSvc->DisableFull(); });

#  if defined(MOZ_SANDBOX)
    // Required for sandboxed child processes.
    if (aShellData->sandboxBrokerServices) {
      SandboxBroker::Initialize(aShellData->sandboxBrokerServices, binDirPath);
      SandboxBroker::GeckoDependentInitialize();
    } else {
      NS_WARNING(
          "Failed to initialize broker services, sandboxed "
          "processes will fail to start.");
    }
#  endif  // defined(MOZ_SANDBOX)

    {
      DebugOnly<bool> result = WindowsBCryptInitialization();
      MOZ_ASSERT(result);
    }
#endif  // defined(XP_WIN)

#ifdef MOZ_CODE_COVERAGE
    CodeCoverageHandler::Init();
#endif

    {
      if (!glob) {
        return 1;
      }

      nsCOMPtr<nsIAppStartup> appStartup(components::AppStartup::Service());
      if (!appStartup) {
        return 1;
      }
      appStartup->DoneStartingUp();

      systemGlobal->SetGlobalObject(glob);

      JSAutoRealm ar(cx, glob);

      if (!JS_InitReflectParse(cx, glob)) {
        return 1;
      }

      if (!JS_DefineFunctions(cx, glob, glob_functions)) {
        return 1;
      }

      nsAutoString workingDirectory;
      if (GetCurrentWorkingDirectory(workingDirectory)) {
        gWorkingDirectory = &workingDirectory;
      }

      JS_DefineProperty(cx, glob, "__LOCATION__", GetLocationProperty, nullptr,
                        0);

      {
#ifdef FUZZING_INTERFACES
        if (fuzzHaveModule) {
          // argv[0] was removed previously, but libFuzzer expects it
          argc++;
          argv--;

          result = FuzzXPCRuntimeStart(&jsapi, &argc, &argv, aShellData);
        } else {
#endif
          // We are almost certainly going to run script here, so we need an
          // AutoEntryScript. This is Gecko-specific and not in any spec.
          AutoEntryScript aes(systemGlobal, "xpcshell argument processing");

          // If an exception is thrown, we'll set our return code
          // appropriately, and then let the AutoEntryScript destructor report
          // the error to the console.
          if (!ProcessArgs(aes, argv, argc, &dirprovider)) {
            if (gExitCode) {
              result = gExitCode;
            } else if (gQuitting) {
              result = 0;
            } else {
              result = EXITCODE_RUNTIME_ERROR;
            }
          }
#ifdef FUZZING_INTERFACES
        }
#endif
      }

      // Signal that we're now shutting down.
      nsCOMPtr<nsIObserver> obs = do_QueryInterface(appStartup);
      if (obs) {
        obs->Observe(nullptr, "quit-application-forced", nullptr);
      }

      JS_DropPrincipals(cx, gJSPrincipals);
      JS_SetAllNonReservedSlotsToUndefined(glob);
      JS::RootedObject lexicalEnv(cx, JS_GlobalLexicalEnvironment(glob));
      JS_SetAllNonReservedSlotsToUndefined(lexicalEnv);
      JS_GC(cx);
    }
    JS_GC(cx);

    dirprovider.ClearGREDirs();
    dirprovider.ClearAppDir();
    dirprovider.ClearAppFile();
  }  // this scopes the nsCOMPtrs

  if (!XRE_ShutdownTestShell()) {
    NS_ERROR("problem shutting down testshell");
  }

  // no nsCOMPtrs are allowed to be alive when you call NS_ShutdownXPCOM
  rv = NS_ShutdownXPCOM(nullptr);
  MOZ_ASSERT(NS_SUCCEEDED(rv), "NS_ShutdownXPCOM failed");

  // Shut down the crashreporter service to prevent leaking some strings it
  // holds.
  if (CrashReporter::GetEnabled()) {
    CrashReporter::UnsetExceptionHandler();
  }

  // This must precede NS_LogTerm(), otherwise xpcshell return non-zero
  // during some tests, which causes failures.
  profiler_shutdown();

  NS_LogTerm();

  XRE_DeinitCommandLine();

  return result;
}

void XPCShellDirProvider::SetGREDirs(nsIFile* greDir) {
  mGREDir = greDir;
  mGREDir->Clone(getter_AddRefs(mGREBinDir));

#ifdef XP_MACOSX
  nsAutoCString leafName;
  mGREDir->GetNativeLeafName(leafName);
  if (leafName.EqualsLiteral("Resources")) {
    mGREBinDir->SetNativeLeafName("MacOS"_ns);
  }
#endif
}

void XPCShellDirProvider::SetAppFile(nsIFile* appFile) { mAppFile = appFile; }

void XPCShellDirProvider::SetAppDir(nsIFile* appDir) { mAppDir = appDir; }

NS_IMETHODIMP_(MozExternalRefCountType)
XPCShellDirProvider::AddRef() { return 2; }

NS_IMETHODIMP_(MozExternalRefCountType)
XPCShellDirProvider::Release() { return 1; }

NS_IMPL_QUERY_INTERFACE(XPCShellDirProvider, nsIDirectoryServiceProvider,
                        nsIDirectoryServiceProvider2)

NS_IMETHODIMP
XPCShellDirProvider::GetFile(const char* prop, bool* persistent,
                             nsIFile** result) {
  if (mGREDir && !strcmp(prop, NS_GRE_DIR)) {
    *persistent = true;
    return mGREDir->Clone(result);
  } else if (mGREBinDir && !strcmp(prop, NS_GRE_BIN_DIR)) {
    *persistent = true;
    return mGREBinDir->Clone(result);
  } else if (mAppFile && !strcmp(prop, XRE_EXECUTABLE_FILE)) {
    *persistent = true;
    return mAppFile->Clone(result);
  } else if (mGREDir && !strcmp(prop, NS_APP_PREF_DEFAULTS_50_DIR)) {
    nsCOMPtr<nsIFile> file;
    *persistent = true;
    if (NS_FAILED(mGREDir->Clone(getter_AddRefs(file))) ||
        NS_FAILED(file->AppendNative("defaults"_ns)) ||
        NS_FAILED(file->AppendNative("pref"_ns)))
      return NS_ERROR_FAILURE;
    file.forget(result);
    return NS_OK;
  }

  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
XPCShellDirProvider::GetFiles(const char* prop, nsISimpleEnumerator** result) {
  if (mGREDir && !strcmp(prop, "ChromeML")) {
    nsCOMArray<nsIFile> dirs;

    nsCOMPtr<nsIFile> file;
    mGREDir->Clone(getter_AddRefs(file));
    file->AppendNative("chrome"_ns);
    dirs.AppendObject(file);

    nsresult rv =
        NS_GetSpecialDirectory(NS_APP_CHROME_DIR, getter_AddRefs(file));
    if (NS_SUCCEEDED(rv)) {
      dirs.AppendObject(file);
    }

    return NS_NewArrayEnumerator(result, dirs, NS_GET_IID(nsIFile));
  } else if (!strcmp(prop, NS_APP_PREFS_DEFAULTS_DIR_LIST)) {
    nsCOMArray<nsIFile> dirs;
    nsCOMPtr<nsIFile> appDir;
    bool exists;
    if (mAppDir && NS_SUCCEEDED(mAppDir->Clone(getter_AddRefs(appDir))) &&
        NS_SUCCEEDED(appDir->AppendNative("defaults"_ns)) &&
        NS_SUCCEEDED(appDir->AppendNative("preferences"_ns)) &&
        NS_SUCCEEDED(appDir->Exists(&exists)) && exists) {
      dirs.AppendObject(appDir);
      return NS_NewArrayEnumerator(result, dirs, NS_GET_IID(nsIFile));
    }
    return NS_ERROR_FAILURE;
  }
  return NS_ERROR_FAILURE;
}
