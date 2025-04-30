/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RemoteWorkerDebuggerChild.h"
#include "mozilla/dom/MessageEvent.h"
#include "mozilla/dom/MessageEventBinding.h"
#include "mozilla/dom/WorkerCommon.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/dom/WorkerRunnable.h"
#include "mozilla/dom/WorkerScope.h"
#include "mozilla/dom/workerinternals/ScriptLoader.h"
#include "nsThreadUtils.h"

namespace mozilla::dom {

namespace {

class RemoteDebuggerMessageEventRunnable final : public WorkerDebuggerRunnable {
  nsString mMessage;

 public:
  explicit RemoteDebuggerMessageEventRunnable(const nsAString& aMessage)
      : WorkerDebuggerRunnable("RemoteDebuggerMessageEventRunnable"),
        mMessage(aMessage) {}

 private:
  virtual bool PreDispatch(WorkerPrivate* aWorkerPrivate) override {
    return true;
  }

  virtual void PostDispatch(WorkerPrivate* aWorkerPrivate,
                            bool aDispatchResult) override {}

  virtual bool WorkerRun(JSContext* aCx,
                         WorkerPrivate* aWorkerPrivate) override {
    WorkerDebuggerGlobalScope* globalScope =
        aWorkerPrivate->DebuggerGlobalScope();
    MOZ_ASSERT(globalScope);

    JS::Rooted<JSString*> message(
        aCx, JS_NewUCStringCopyN(aCx, mMessage.get(), mMessage.Length()));
    if (!message) {
      return false;
    }
    JS::Rooted<JS::Value> data(aCx, JS::StringValue(message));

    RefPtr<MessageEvent> event =
        new MessageEvent(globalScope, nullptr, nullptr);
    event->InitMessageEvent(nullptr, u"message"_ns, CanBubble::eNo,
                            Cancelable::eYes, data, u""_ns, u""_ns, nullptr,
                            Sequence<OwningNonNull<MessagePort>>());
    event->SetTrusted(true);

    globalScope->DispatchEvent(*event);
    return true;
  }
};

class CompileRemoteDebuggerScriptRunnable final
    : public WorkerDebuggerRunnable {
  nsString mScriptURL;
  const mozilla::Encoding* mDocumentEncoding;

 public:
  CompileRemoteDebuggerScriptRunnable(
      WorkerPrivate* aWorkerPrivate, const nsAString& aScriptURL,
      const mozilla::Encoding* aDocumentEncoding)
      : WorkerDebuggerRunnable("CompileDebuggerScriptRunnable"),
        mScriptURL(aScriptURL),
        mDocumentEncoding(aDocumentEncoding) {}

 private:
  virtual bool PreDispatch(WorkerPrivate* aWorkerPrivate) override {
    return true;
  }

  virtual void PostDispatch(WorkerPrivate* aWorkerPrivate,
                            bool aDispatchResult) override {}

  virtual bool WorkerRun(JSContext* aCx,
                         WorkerPrivate* aWorkerPrivate) override {
    aWorkerPrivate->AssertIsOnWorkerThread();

    WorkerDebuggerGlobalScope* globalScope =
        aWorkerPrivate->CreateDebuggerGlobalScope(aCx);
    if (!globalScope) {
      NS_WARNING("Failed to make global!");
      return false;
    }

    if (NS_WARN_IF(!aWorkerPrivate->EnsureCSPEventListener())) {
      return false;
    }

    JS::Rooted<JSObject*> global(aCx, globalScope->GetWrapper());

    ErrorResult rv;
    JSAutoRealm ar(aCx, global);
    workerinternals::LoadMainScript(aWorkerPrivate, nullptr, mScriptURL,
                                    DebuggerScript, rv, mDocumentEncoding);
    rv.WouldReportJSException();
    // Explicitly ignore NS_BINDING_ABORTED on rv.  Or more precisely, still
    // return false and don't SetWorkerScriptExecutedSuccessfully() in that
    // case, but don't throw anything on aCx.  The idea is to not dispatch error
    // events if our load is canceled with that error code.
    if (rv.ErrorCodeIs(NS_BINDING_ABORTED)) {
      rv.SuppressException();
      return false;
    }
    // Make sure to propagate exceptions from rv onto aCx, so that they will get
    // reported after we return.  We do this for all failures on rv, because now
    // we're using rv to track all the state we care about.
    if (rv.MaybeSetPendingException(aCx)) {
      return false;
    }

    return true;
  }
};

}  // namespace

RemoteWorkerDebuggerChild::RemoteWorkerDebuggerChild(
    WorkerPrivate* aWorkerPrivate) {
  MOZ_ASSERT_DEBUG_OR_FUZZING(aWorkerPrivate);
  aWorkerPrivate->AssertIsOnWorkerThread();
}

RemoteWorkerDebuggerChild::~RemoteWorkerDebuggerChild() {}

mozilla::ipc::IPCResult RemoteWorkerDebuggerChild::RecvRegisterDone() {
  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
  MOZ_ASSERT_DEBUG_OR_FUZZING(workerPrivate);

  workerPrivate->SetIsRemoteDebuggerRegistered(true);
  return IPC_OK();
}

mozilla::ipc::IPCResult RemoteWorkerDebuggerChild::RecvUnregisterDone() {
  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
  MOZ_ASSERT_DEBUG_OR_FUZZING(workerPrivate);

  workerPrivate->SetIsRemoteDebuggerRegistered(false);
  return IPC_OK();
}

mozilla::ipc::IPCResult RemoteWorkerDebuggerChild::RecvInitialize(
    const nsString& aURL) {
  if (!mIsInitialized) {
    WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
    MOZ_ASSERT_DEBUG_OR_FUZZING(workerPrivate);
    RefPtr<CompileRemoteDebuggerScriptRunnable> runnable =
        new CompileRemoteDebuggerScriptRunnable(workerPrivate, aURL, nullptr);
    Unused << NS_WARN_IF(!runnable->Dispatch(workerPrivate));
    Unused << SendSetAsInitialized();
  }
  mIsInitialized = true;
  return IPC_OK();
}

mozilla::ipc::IPCResult RemoteWorkerDebuggerChild::RecvPostMessage(
    const nsString& aMessage) {
  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
  MOZ_ASSERT_DEBUG_OR_FUZZING(workerPrivate);
  RefPtr<RemoteDebuggerMessageEventRunnable> runnable =
      new RemoteDebuggerMessageEventRunnable(aMessage);
  Unused << NS_WARN_IF(!runnable->Dispatch(workerPrivate));
  return IPC_OK();
}

mozilla::ipc::IPCResult RemoteWorkerDebuggerChild::RecvSetDebuggerReady(
    const bool& aReady) {
  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
  MOZ_ASSERT_DEBUG_OR_FUZZING(workerPrivate);
  workerPrivate->SetIsRemoteDebuggerReady(aReady);
  return IPC_OK();
}

}  // namespace mozilla::dom
