/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_timeout_handler_h
#define mozilla_dom_timeout_handler_h

#include "js/Promise.h"  // JS::Dispatchable
#include "mozilla/Attributes.h"
#include "mozilla/HoldDropJSObjects.h"
#include "mozilla/SourceLocation.h"
#include "mozilla/dom/FunctionBinding.h"
#include "nsCOMPtr.h"
#include "nsCycleCollectionParticipant.h"
#include "nsIGlobalObject.h"
#include "nsISupports.h"
#include "nsString.h"

namespace mozilla::dom {

/**
 * Utility class for implementing nsITimeoutHandlers, designed to be subclassed.
 */
class TimeoutHandler : public nsISupports, public JSHolderBase {
 public:
  TimeoutHandler(const TimeoutHandler&) = delete;
  TimeoutHandler& operator=(const TimeoutHandler&) = delete;
  TimeoutHandler& operator=(const TimeoutHandler&&) = delete;

  MOZ_CAN_RUN_SCRIPT virtual bool Call(const char* /* unused */);
  // Append a UTF-8 string to aOutString that describes the callback function,
  // for use in logging or profiler markers.
  // The string contains the function name and its source location, if
  // available, in the following format:
  // "<functionName> (<sourceURL>:<lineNumber>:<columnNumber>)"
  virtual void GetDescription(nsACString& aOutString);
  virtual void MarkForCC() {}

 protected:
  TimeoutHandler() = default;
  explicit TimeoutHandler(JSContext* aCx)
      : mCaller(JSCallingLocation::Get(aCx)) {}

  virtual ~TimeoutHandler() = default;

  // filename, line number and JS language version string of the
  // caller of setTimeout()
  const JSCallingLocation mCaller = {};
};

class ScriptTimeoutHandler : public TimeoutHandler {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS(ScriptTimeoutHandler)

  ScriptTimeoutHandler(JSContext* aCx, nsIGlobalObject* aGlobal,
                       const nsAString& aExpression);

  MOZ_CAN_RUN_SCRIPT virtual bool Call(const char* /* unused */) override {
    return false;
  };
  virtual void GetDescription(nsACString& aOutString) override;

 protected:
  virtual ~ScriptTimeoutHandler() = default;

  nsCOMPtr<nsIGlobalObject> mGlobal;
  // The expression to evaluate or function to call. If mFunction is non-null
  // it should be used, else use mExpr.
  nsString mExpr;
};

class CallbackTimeoutHandler final : public TimeoutHandler {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(CallbackTimeoutHandler)

  CallbackTimeoutHandler(JSContext* aCx, nsIGlobalObject* aGlobal,
                         Function* aFunction,
                         nsTArray<JS::Heap<JS::Value>>&& aArguments);

  MOZ_CAN_RUN_SCRIPT virtual bool Call(const char* aExecutionReason) override;
  virtual void MarkForCC() override;
  virtual void GetDescription(nsACString& aOutString) override;

  void ReleaseJSObjects();

 private:
  virtual ~CallbackTimeoutHandler() { ReleaseJSObjects(); }

  nsCOMPtr<nsIGlobalObject> mGlobal;
  RefPtr<Function> mFunction;
  nsTArray<JS::Heap<JS::Value>> mArgs;
};

class DelayedJSDispatchableHandler final : public TimeoutHandler {
 public:
  DelayedJSDispatchableHandler(JSContext* aCx,
                               js::UniquePtr<JS::Dispatchable>&& aDispatchable)
      : TimeoutHandler(aCx), mDispatchable(std::move(aDispatchable)) {}

  NS_DECL_ISUPPORTS

  MOZ_CAN_RUN_SCRIPT bool Call(const char* /* unused */) override;

 private:
  ~DelayedJSDispatchableHandler() override;
  js::UniquePtr<JS::Dispatchable> mDispatchable;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_timeout_handler_h
