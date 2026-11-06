/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "fuzz-tests/tests.h"

#include <stdio.h>
#include <stdlib.h>

#include "js/AllocPolicy.h"
#include "js/GlobalObject.h"
#include "js/Initialization.h"
#include "js/Prefs.h"
#include "js/RealmOptions.h"
#include "js/RootingAPI.h"
#include "js/Stack.h"
#include "vm/JSContext.h"

#ifdef LIBFUZZER
#  include "FuzzerDefs.h"
#endif

using namespace mozilla;

constinit JS::PersistentRootedObject gGlobal;
JSContext* gCx = nullptr;

static const JSClass* getGlobalClass() {
  static const JSClass c = {
      "global",
      JSCLASS_GLOBAL_FLAGS,
      &JS::DefaultGlobalClassOps,
  };
  return &c;
}

static JSObject* jsfuzz_createGlobal(JSContext* cx, JSPrincipals* principals) {
  /* Create the global object. */
  JS::RealmOptions options;
  options.creationOptions().setSharedMemoryAndAtomicsEnabled(true);
  return JS_NewGlobalObject(cx, getGlobalClass(), principals,
                            JS::FireOnNewGlobalHook, options);
}

static bool jsfuzz_init(JSContext** cx, JS::PersistentRootedObject* global) {
  *cx = JS_NewContext(8L * 1024 * 1024);
  if (!*cx) {
    return false;
  }

  const size_t MAX_STACK_SIZE = 500000;

  JS_SetNativeStackQuota(*cx, MAX_STACK_SIZE);

  js::UseInternalJobQueues(*cx);
  if (!JS::InitSelfHostedCode(*cx)) {
    return false;
  }
  global->init(*cx);
  *global = jsfuzz_createGlobal(*cx, nullptr);
  if (!*global) {
    return false;
  }
  JS::EnterRealm(*cx, *global);
  return true;
}

static void jsfuzz_uninit(JSContext* cx) {
  if (cx) {
    JS::LeaveRealm(cx, nullptr);
    JS_DestroyContext(cx);
    cx = nullptr;
  }
}

static void jsfuzz_shutdown() {
  jsfuzz_uninit(gCx);
  gCx = nullptr;
  JS_ShutDown();
}

int main(int argc, char* argv[]) {
  // Override prefs for fuzz-tests.
  JS::Prefs::setAtStartup_experimental_weakrefs_expose_cleanupSome(true);

  if (!JS_Init()) {
    fprintf(stderr, "Error: Call to jsfuzz_init() failed\n");
    return 1;
  }

  // Shut down before static destructors run, while helper threads can still
  // use their synchronization primitives.
  if (atexit(jsfuzz_shutdown) != 0) {
    fprintf(stderr, "Error: Could not register shutdown handler\n");
    JS_ShutDown();
    return 1;
  }

  if (!jsfuzz_init(&gCx, &gGlobal)) {
    fprintf(stderr, "Error: Call to jsfuzz_init() failed\n");
    return 1;
  }

  const char* fuzzerEnv = getenv("FUZZER");
  if (!fuzzerEnv) {
    fprintf(stderr,
            "Must specify fuzzing target in FUZZER environment variable\n");
    return 1;
  }

  std::string moduleNameStr(getenv("FUZZER"));

  FuzzerFunctions funcs =
      FuzzerRegistry::getInstance().getModuleFunctions(moduleNameStr);
  FuzzerInitFunc initFunc = funcs.first;
  FuzzerTestingFunc testingFunc = funcs.second;
  if (initFunc) {
    int ret = initFunc(&argc, &argv);
    if (ret) {
      fprintf(stderr, "Fuzzing Interface: Error: Initialize callback failed\n");
      return ret;
    }
  }

  if (!testingFunc) {
    fprintf(stderr, "Fuzzing Interface: Error: No testing callback found\n");
    return 1;
  }

#ifdef LIBFUZZER
  fuzzer::FuzzerDriver(&argc, &argv, testingFunc);
#elif AFLFUZZ
  testingFunc(nullptr, 0);
#endif

  return 0;
}
