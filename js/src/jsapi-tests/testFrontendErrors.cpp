/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/RefPtr.h"  // RefPtr
#include "mozilla/Utf8.h"    // mozilla::Utf8Unit

#include <string.h>  // strcmp

#include "frontend/FrontendContext.h"  // js::FrontendContext
#include "js/AllocPolicy.h"            // js::ReportOutOfMemory
#include "js/CompileOptions.h"  // JS::PrefableCompileOptions, JS::CompileOptions
#include "js/Exception.h"  // JS_IsExceptionPending, JS_IsThrowingOutOfMemory, JS_GetPendingException, JS_ClearPendingException, JS_ErrorFromException
#include "js/experimental/CompileScript.h"  // JS::NewFrontendContext, JS::DestroyFrontendContext, JS::SetNativeStackQuota, JS::CompileGlobalScriptToStencil, JS::ConvertFrontendErrorsToRuntimeErrors, JS::HadFrontendErrors, JS::HadFrontendOverRecursed, JS::HadFrontendOutOfMemory, JS::HadFrontendAllocationOverflow, JS::AllowCancellingCompilation, JS::RequestFrontendCompilationCancellation, JS::HadFrontendCancelled, JS::GetFrontendWarningCount, JS::GetFrontendWarningAt
#include "js/friend/ErrorMessages.h"        // JSMSG_*
#include "js/friend/StackLimits.h"          // js::ReportOverRecursed
#include "js/RootingAPI.h"                  // JS::Rooted
#include "js/SourceText.h"                  // JS::SourceText
#include "js/Stack.h"                       // JS::NativeStackSize
#include "js/Warnings.h"                    // JS::SetWarningReporter
#include "jsapi-tests/tests.h"
#include "util/NativeStack.h"  // js::GetNativeStackBase
#include "vm/ErrorObject.h"    // js::ErrorObject
#include "vm/JSContext.h"      // JSContext
#include "vm/JSObject.h"       // JSObject
#include "vm/NativeObject.h"   // js::NativeObject
#include "vm/Runtime.h"        // js::ReportAllocationOverflow

using namespace JS;

BEGIN_TEST(testFrontendErrors_error) {
  JS::FrontendContext* fc = JS::NewFrontendContext();
  CHECK(fc);

  static constexpr JS::NativeStackSize stackSize = 128 * sizeof(size_t) * 1024;

  JS::SetNativeStackQuota(fc, stackSize);

  JS::PrefableCompileOptions prefableOptions;
  JS::CompileOptions options(prefableOptions);
  const char* filename = "testFrontendErrors_error.js";
  options.setFile(filename);

  CHECK(!JS::HadFrontendErrors(fc));

  {
    const char source[] = "syntax error";

    JS::SourceText<mozilla::Utf8Unit> srcBuf;
    CHECK(
        srcBuf.init(fc, source, strlen(source), JS::SourceOwnership::Borrowed));
    RefPtr<JS::Stencil> stencil =
        JS::CompileGlobalScriptToStencil(fc, options, srcBuf);
    CHECK(!stencil);
  }

  CHECK(JS::HadFrontendErrors(fc));
  CHECK(!JS::HadFrontendOverRecursed(fc));
  CHECK(!JS::HadFrontendOutOfMemory(fc));
  CHECK(!JS::HadFrontendAllocationOverflow(fc));
  CHECK(JS::GetFrontendWarningCount(fc) == 0);

  {
    const JSErrorReport* report = JS::GetFrontendErrorReport(fc, options);
    CHECK(report);

    CHECK(report->errorNumber == JSMSG_UNEXPECTED_TOKEN_NO_EXPECT);
    // FrontendContext's error report borrows the filename.
    CHECK(report->filename.c_str() == filename);
  }

  CHECK(!JS_IsExceptionPending(cx));

  JS::SetWarningReporter(cx, warningReporter);

  bool result = JS::ConvertFrontendErrorsToRuntimeErrors(cx, fc, options);
  CHECK(result);

  CHECK(JS_IsExceptionPending(cx));
  CHECK(!warningReporterCalled);
  CHECK(!JS_IsThrowingOutOfMemory(cx));

  {
    JS::Rooted<JS::Value> exception(cx);
    CHECK(JS_GetPendingException(cx, &exception));

    CHECK(exception.isObject());
    JS::Rooted<JSObject*> exceptionObj(cx, &exception.toObject());

    JS::BorrowedErrorReport report(cx);
    CHECK(JS_ErrorFromException(cx, exceptionObj, report));

    CHECK(report->errorNumber == JSMSG_UNEXPECTED_TOKEN_NO_EXPECT);
    // Runtime's error report doesn't borrow the filename.
    CHECK(report->filename.c_str() != filename);
    CHECK(strcmp(report->filename.c_str(), filename) == 0);
  }

  JS_ClearPendingException(cx);

  JS::DestroyFrontendContext(fc);

  return true;
}

static bool warningReporterCalled;

static void warningReporter(JSContext* cx, JSErrorReport* report) {
  warningReporterCalled = true;
}
END_TEST(testFrontendErrors_error)

/* static */ bool cls_testFrontendErrors_error::warningReporterCalled = false;

BEGIN_TEST(testFrontendErrors_warning) {
  JS::FrontendContext* fc = JS::NewFrontendContext();
  CHECK(fc);

  static constexpr JS::NativeStackSize stackSize = 128 * sizeof(size_t) * 1024;

  JS::SetNativeStackQuota(fc, stackSize);

  JS::PrefableCompileOptions prefableOptions;
  JS::CompileOptions options(prefableOptions);
  options.setFile(filename);

  {
    const char source[] = "function f() { return; f(); }";

    JS::SourceText<mozilla::Utf8Unit> srcBuf;
    CHECK(
        srcBuf.init(fc, source, strlen(source), JS::SourceOwnership::Borrowed));
    RefPtr<JS::Stencil> stencil =
        JS::CompileGlobalScriptToStencil(fc, options, srcBuf);
    CHECK(stencil);
  }

  CHECK(!JS::HadFrontendErrors(fc));
  CHECK(!JS::HadFrontendOverRecursed(fc));
  CHECK(!JS::HadFrontendOutOfMemory(fc));
  CHECK(!JS::HadFrontendAllocationOverflow(fc));
  CHECK(JS::GetFrontendWarningCount(fc) == 1);

  {
    const JSErrorReport* report = JS::GetFrontendWarningAt(fc, 0, options);
    CHECK(report);

    CHECK(report->errorNumber == JSMSG_STMT_AFTER_RETURN);
    // FrontendContext's error report borrows the filename.
    CHECK(report->filename.c_str() == filename);
  }

  CHECK(!JS_IsExceptionPending(cx));

  JS::SetWarningReporter(cx, warningReporter);

  bool result = JS::ConvertFrontendErrorsToRuntimeErrors(cx, fc, options);
  CHECK(result);

  CHECK(!JS_IsExceptionPending(cx));
  CHECK(warningReporterCalled);
  CHECK(!JS_IsThrowingOutOfMemory(cx));

  CHECK(errorNumberMatches);
  CHECK(filenameMatches);

  JS::DestroyFrontendContext(fc);

  return true;
}

static const char* filename;
static bool warningReporterCalled;
static bool errorNumberMatches;
static bool filenameMatches;

static void warningReporter(JSContext* cx, JSErrorReport* report) {
  warningReporterCalled = true;

  errorNumberMatches = report->errorNumber == JSMSG_STMT_AFTER_RETURN;
  filenameMatches = report->filename.c_str() == filename;
}
END_TEST(testFrontendErrors_warning)

/* static */ const char* cls_testFrontendErrors_warning::filename =
    "testFrontendErrors_warning.js";
/* static */ bool cls_testFrontendErrors_warning::warningReporterCalled = false;
/* static */ bool cls_testFrontendErrors_warning::errorNumberMatches = false;
/* static */ bool cls_testFrontendErrors_warning::filenameMatches = false;

BEGIN_TEST(testFrontendErrors_oom) {
  JS::FrontendContext* fc = JS::NewFrontendContext();
  CHECK(fc);

  JS::PrefableCompileOptions prefableOptions;
  JS::CompileOptions options(prefableOptions);

  CHECK(!JS::HadFrontendErrors(fc));

  js::ReportOutOfMemory(fc);

  CHECK(JS::HadFrontendErrors(fc));
  CHECK(!JS::HadFrontendOverRecursed(fc));
  CHECK(JS::HadFrontendOutOfMemory(fc));
  CHECK(!JS::HadFrontendAllocationOverflow(fc));
  CHECK(JS::GetFrontendWarningCount(fc) == 0);

  CHECK(!JS_IsExceptionPending(cx));

  JS::SetWarningReporter(cx, warningReporter);

  bool result = JS::ConvertFrontendErrorsToRuntimeErrors(cx, fc, options);
  CHECK(!result);

  CHECK(JS_IsExceptionPending(cx));
  CHECK(!warningReporterCalled);
  CHECK(JS_IsThrowingOutOfMemory(cx));

  JS_ClearPendingException(cx);

  JS::DestroyFrontendContext(fc);

  return true;
}

static bool warningReporterCalled;

static void warningReporter(JSContext* cx, JSErrorReport* report) {
  warningReporterCalled = true;
}
END_TEST(testFrontendErrors_oom)

/* static */ bool cls_testFrontendErrors_oom::warningReporterCalled = false;

BEGIN_TEST(testFrontendErrors_overRecursed) {
  JS::FrontendContext* fc = JS::NewFrontendContext();
  CHECK(fc);

  JS::PrefableCompileOptions prefableOptions;
  JS::CompileOptions options(prefableOptions);

  CHECK(!JS::HadFrontendErrors(fc));

  js::ReportOverRecursed(fc);

  CHECK(JS::HadFrontendErrors(fc));
  CHECK(JS::HadFrontendOverRecursed(fc));
  CHECK(!JS::HadFrontendOutOfMemory(fc));
  CHECK(!JS::HadFrontendAllocationOverflow(fc));
  CHECK(JS::GetFrontendWarningCount(fc) == 0);

  CHECK(!JS_IsExceptionPending(cx));

  JS::SetWarningReporter(cx, warningReporter);

  bool result = JS::ConvertFrontendErrorsToRuntimeErrors(cx, fc, options);
  CHECK(result);

  CHECK(JS_IsExceptionPending(cx));
  CHECK(!warningReporterCalled);
  CHECK(!JS_IsThrowingOutOfMemory(cx));

  {
    JS::Rooted<JS::Value> exception(cx);
    CHECK(JS_GetPendingException(cx, &exception));

    CHECK(exception.isObject());
    JS::Rooted<JSObject*> exceptionObj(cx, &exception.toObject());

    JS::BorrowedErrorReport report(cx);
    CHECK(JS_ErrorFromException(cx, exceptionObj, report));

    CHECK(report->errorNumber == JSMSG_OVER_RECURSED);
  }

  JS_ClearPendingException(cx);

  JS::DestroyFrontendContext(fc);

  return true;
}

static bool warningReporterCalled;

static void warningReporter(JSContext* cx, JSErrorReport* report) {
  warningReporterCalled = true;
}
END_TEST(testFrontendErrors_overRecursed)

/* static */ bool cls_testFrontendErrors_overRecursed::warningReporterCalled =
    false;

BEGIN_TEST(testFrontendErrors_allocationOverflow) {
  JS::FrontendContext* fc = JS::NewFrontendContext();
  CHECK(fc);

  JS::PrefableCompileOptions prefableOptions;
  JS::CompileOptions options(prefableOptions);

  CHECK(!JS::HadFrontendErrors(fc));

  js::ReportAllocationOverflow(fc);

  CHECK(JS::HadFrontendErrors(fc));
  CHECK(!JS::HadFrontendOverRecursed(fc));
  CHECK(!JS::HadFrontendOutOfMemory(fc));
  CHECK(JS::HadFrontendAllocationOverflow(fc));
  CHECK(JS::GetFrontendWarningCount(fc) == 0);

  CHECK(!JS_IsExceptionPending(cx));

  JS::SetWarningReporter(cx, warningReporter);

  bool result = JS::ConvertFrontendErrorsToRuntimeErrors(cx, fc, options);
  CHECK(result);

  CHECK(JS_IsExceptionPending(cx));
  CHECK(!warningReporterCalled);
  CHECK(!JS_IsThrowingOutOfMemory(cx));

  {
    JS::Rooted<JS::Value> exception(cx);
    CHECK(JS_GetPendingException(cx, &exception));

    CHECK(exception.isObject());
    JS::Rooted<JSObject*> exceptionObj(cx, &exception.toObject());

    JS::BorrowedErrorReport report(cx);
    CHECK(JS_ErrorFromException(cx, exceptionObj, report));

    CHECK(report->errorNumber == JSMSG_ALLOC_OVERFLOW);
  }

  JS_ClearPendingException(cx);

  JS::DestroyFrontendContext(fc);

  return true;
}

static bool warningReporterCalled;

static void warningReporter(JSContext* cx, JSErrorReport* report) {
  warningReporterCalled = true;
}
END_TEST(testFrontendErrors_allocationOverflow)

/* static */ bool
    cls_testFrontendErrors_allocationOverflow::warningReporterCalled = false;

BEGIN_TEST(testFrontendErrors_cancelled) {
  JS::FrontendContext* fc =
      JS::NewFrontendContext(JS::AllowCancellingCompilation::Yes);
  CHECK(fc);

  static constexpr JS::NativeStackSize stackSize = 128 * sizeof(size_t) * 1024;

  JS::SetNativeStackQuota(fc, stackSize);

  JS::PrefableCompileOptions prefableOptions;
  JS::CompileOptions options(prefableOptions);
  options.setFile("testFrontendErrors_cancelled.js");

  CHECK(!JS::HadFrontendErrors(fc));

  JS::RequestFrontendCompilationCancellation(fc);

  {
    const char source[] = "function f(a) { return a + 1; } f(1);";

    JS::SourceText<mozilla::Utf8Unit> srcBuf;
    CHECK(
        srcBuf.init(fc, source, strlen(source), JS::SourceOwnership::Borrowed));
    RefPtr<JS::Stencil> stencil =
        JS::CompileGlobalScriptToStencil(fc, options, srcBuf);
    CHECK(!stencil);
  }

  CHECK(JS::HadFrontendErrors(fc));
  CHECK(JS::HadFrontendCancelled(fc));
  CHECK(!JS::HadFrontendOverRecursed(fc));
  CHECK(!JS::HadFrontendOutOfMemory(fc));
  CHECK(!JS::HadFrontendAllocationOverflow(fc));
  CHECK(JS::GetFrontendWarningCount(fc) == 0);
  CHECK(!JS::GetFrontendErrorReport(fc, options));

  JS::DestroyFrontendContext(fc);

  return true;
}
END_TEST(testFrontendErrors_cancelled)
