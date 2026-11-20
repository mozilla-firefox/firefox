/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TimeoutHandler.h"

#include "mozilla/Assertions.h"
#include "mozilla/HoldDropJSObjects.h"
#include "nsJSUtils.h"

namespace mozilla::dom {

//-----------------------------------------------------------------------------
// TimeoutHandler
//-----------------------------------------------------------------------------

TimeoutHandler::TimeoutHandler(JSContext* aCx) : TimeoutHandler() {
  nsJSUtils::GetCallingLocation(aCx, mFileName, &mLineNo, &mColumn);
}

bool TimeoutHandler::Call(const char* /* unused */) { return false; }

void TimeoutHandler::GetLocation(const char** aFileName, uint32_t* aLineNo,
                                 uint32_t* aColumn) {
  *aFileName = mFileName.get();
  *aLineNo = mLineNo;
  *aColumn = mColumn;
}

void TimeoutHandler::GetDescription(nsACString& aOutString) {
  aOutString.AppendPrintf("<generic handler> (%s:%d:%d)", mFileName.get(),
                          mLineNo, mColumn);
}

//-----------------------------------------------------------------------------
// ScriptTimeoutHandler
//-----------------------------------------------------------------------------

ScriptTimeoutHandler::ScriptTimeoutHandler(JSContext* aCx,
                                           nsIGlobalObject* aGlobal,
                                           const nsAString& aExpression)
    : TimeoutHandler(aCx), mGlobal(aGlobal), mExpr(aExpression) {}

NS_IMPL_CYCLE_COLLECTION_CLASS(ScriptTimeoutHandler)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(ScriptTimeoutHandler)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mGlobal)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INTERNAL(ScriptTimeoutHandler)
  if (MOZ_UNLIKELY(cb.WantDebugInfo())) {
    nsAutoCString name("ScriptTimeoutHandler");
    name.AppendLiteral(" [");
    name.Append(tmp->mFileName);
    name.Append(':');
    name.AppendInt(tmp->mLineNo);
    name.Append(':');
    name.AppendInt(tmp->mColumn);
    name.Append(']');
    cb.DescribeRefCountedNode(tmp->mRefCnt.get(), name.get());
  } else {
    NS_IMPL_CYCLE_COLLECTION_DESCRIBE(ScriptTimeoutHandler, tmp->mRefCnt.get())
  }

  // If we need to make TimeoutHandler CCed, don't call its Traverse method
  // here, otherwise we ends up report same object twice if logging is on. See
  // https://bugzilla.mozilla.org/show_bug.cgi?id=1588208.

  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mGlobal)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ScriptTimeoutHandler)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(ScriptTimeoutHandler)
NS_IMPL_CYCLE_COLLECTING_RELEASE(ScriptTimeoutHandler)

void ScriptTimeoutHandler::GetDescription(nsACString& aOutString) {
  if (mExpr.Length() > 15) {
    aOutString.AppendPrintf(
        "<string handler (truncated): \"%s...\"> (%s:%d:%d)",
        NS_ConvertUTF16toUTF8(Substring(mExpr, 0, 13)).get(), mFileName.get(),
        mLineNo, mColumn);
  } else {
    aOutString.AppendPrintf("<string handler: \"%s\"> (%s:%d:%d)",
                            NS_ConvertUTF16toUTF8(mExpr).get(), mFileName.get(),
                            mLineNo, mColumn);
  }
}

//-----------------------------------------------------------------------------
// CallbackTimeoutHandler
//-----------------------------------------------------------------------------

CallbackTimeoutHandler::CallbackTimeoutHandler(
    JSContext* aCx, nsIGlobalObject* aGlobal, Function* aFunction,
    nsTArray<JS::Heap<JS::Value>>&& aArguments)
    : TimeoutHandler(aCx), mGlobal(aGlobal), mFunction(aFunction) {
  mozilla::HoldJSObjects(this);
  mArgs = std::move(aArguments);
}

NS_IMPL_CYCLE_COLLECTION_CLASS(CallbackTimeoutHandler)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(CallbackTimeoutHandler)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mGlobal)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mFunction)
  tmp->mArgs.Clear();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INTERNAL(CallbackTimeoutHandler)
  if (MOZ_UNLIKELY(cb.WantDebugInfo())) {
    nsAutoCString name("CallbackTimeoutHandler");
    JSObject* obj = tmp->mFunction->CallablePreserveColor();
    JSFunction* fun =
        JS_GetObjectFunction(js::UncheckedUnwrapWithoutExpose(obj));
    if (fun && JS_GetFunctionId(fun)) {
      JSLinearString* funId = JS_ASSERT_STRING_IS_LINEAR(JS_GetFunctionId(fun));
      size_t size = 1 + JS_PutEscapedLinearString(nullptr, 0, funId, 0);
      char* funIdName = new char[size];
      if (funIdName) {
        JS_PutEscapedLinearString(funIdName, size, funId, 0);
        name.AppendLiteral(" [");
        name.Append(funIdName);
        delete[] funIdName;
        name.Append(']');
      }
    }
    cb.DescribeRefCountedNode(tmp->mRefCnt.get(), name.get());
  } else {
    NS_IMPL_CYCLE_COLLECTION_DESCRIBE(CallbackTimeoutHandler,
                                      tmp->mRefCnt.get())
  }

  // If we need to make TimeoutHandler CCed, don't call its Traverse method
  // here, otherwise we ends up report same object twice if logging is on. See
  // https://bugzilla.mozilla.org/show_bug.cgi?id=1588208.

  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mGlobal)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mFunction)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(CallbackTimeoutHandler)
  for (size_t i = 0; i < tmp->mArgs.Length(); ++i) {
    NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(mArgs[i])
  }
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(CallbackTimeoutHandler)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(CallbackTimeoutHandler)
NS_IMPL_CYCLE_COLLECTING_RELEASE(CallbackTimeoutHandler)

bool CallbackTimeoutHandler::Call(const char* aExecutionReason) {
  IgnoredErrorResult rv;
  JS::Rooted<JS::Value> ignoredVal(RootingCx());
  MOZ_KnownLive(mFunction)->Call(MOZ_KnownLive(mGlobal), mArgs, &ignoredVal, rv,
                                 aExecutionReason);
  return !rv.IsUncatchableException();
}

void CallbackTimeoutHandler::MarkForCC() { mFunction->MarkForCC(); }

void CallbackTimeoutHandler::GetDescription(nsACString& aOutString) {
  mFunction->GetDescription(aOutString);
}

}  // namespace mozilla::dom
