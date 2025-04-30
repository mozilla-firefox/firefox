/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/JSActor.h"
#include "mozilla/dom/JSActorBinding.h"

#include "chrome/common/ipc_channel.h"
#include "mozilla/Attributes.h"
#include "mozilla/FunctionRef.h"
#include "mozilla/dom/AutoEntryScript.h"
#include "mozilla/dom/ClonedErrorHolder.h"
#include "mozilla/dom/DOMException.h"
#include "mozilla/dom/DOMExceptionBinding.h"
#include "mozilla/dom/JSActorManager.h"
#include "mozilla/dom/MessageManagerBinding.h"
#include "mozilla/dom/PWindowGlobal.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/RootedDictionary.h"
#include "mozilla/dom/ipc/StructuredCloneData.h"
#include "mozilla/ProfilerMarkers.h"
#include "js/Promise.h"
#include "xpcprivate.h"
#include "nsFrameMessageManager.h"
#include "nsICrashReporter.h"

namespace mozilla::dom {

struct JSActorMessageMarker {
  static constexpr Span<const char> MarkerTypeName() {
    return MakeStringSpan("JSActorMessage");
  }
  static void StreamJSONMarkerData(baseprofiler::SpliceableJSONWriter& aWriter,
                                   const ProfilerString8View& aActorName,
                                   const ProfilerString16View& aMessageName) {
    aWriter.StringProperty("actor", aActorName);
    aWriter.StringProperty("name", NS_ConvertUTF16toUTF8(aMessageName));
  }
  static MarkerSchema MarkerTypeDisplay() {
    using MS = MarkerSchema;
    MS schema{MS::Location::MarkerChart, MS::Location::MarkerTable};
    schema.AddKeyLabelFormatSearchable(
        "actor", "Actor Name", MS::Format::String, MS::Searchable::Searchable);
    schema.AddKeyLabelFormatSearchable(
        "name", "Message Name", MS::Format::String, MS::Searchable::Searchable);
    schema.SetTooltipLabel("JSActor - {marker.name}");
    schema.SetTableLabel(
        "{marker.name} - [{marker.data.actor}] {marker.data.name}");
    return schema;
  }
};

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(JSActor)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(JSActor)
NS_IMPL_CYCLE_COLLECTING_RELEASE(JSActor)

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(JSActor)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(JSActor)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mGlobal)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mWrappedJS)
  tmp->mPendingQueries.Clear();
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(JSActor)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mGlobal)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mWrappedJS)
  for (const auto& query : tmp->mPendingQueries.Values()) {
    CycleCollectionNoteChild(cb, query.mPromise.get(), "Pending Query Promise");
  }
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

JSActor::JSActor(nsISupports* aGlobal) {
  mGlobal = do_QueryInterface(aGlobal);
  if (!mGlobal) {
    mGlobal = xpc::NativeGlobal(xpc::PrivilegedJunkScope());
  }
}

void JSActor::StartDestroy() { mCanSend = false; }

void JSActor::AfterDestroy() {
  mCanSend = false;

  // Take our queries out, in case somehow rejecting promises can trigger
  // additions or removals.
  const nsTHashMap<nsUint64HashKey, PendingQuery> pendingQueries =
      std::move(mPendingQueries);
  for (const auto& entry : pendingQueries.Values()) {
    nsPrintfCString message(
        "Actor '%s' destroyed before query '%s' was resolved", mName.get(),
        NS_LossyConvertUTF16toASCII(entry.mMessageName).get());
    entry.mPromise->MaybeRejectWithAbortError(message);
  }

  InvokeCallback(CallbackFunction::DidDestroy);
  ClearManager();
}

void JSActor::InvokeCallback(CallbackFunction callback) {
  MOZ_ASSERT(nsContentUtils::IsSafeToRunScript());

  AutoEntryScript aes(GetParentObject(), "JSActor destroy callback");
  JSContext* cx = aes.cx();
  MozJSActorCallbacks callbacksHolder;
  JS::Rooted<JS::Value> val(cx, JS::ObjectOrNullValue(GetWrapper()));
  if (NS_WARN_IF(!callbacksHolder.Init(cx, val))) {
    return;
  }

  // Destroy callback is optional.
  if (callback == CallbackFunction::DidDestroy) {
    if (callbacksHolder.mDidDestroy.WasPassed()) {
      callbacksHolder.mDidDestroy.Value()->Call(this);
    }
  } else {
    if (callbacksHolder.mActorCreated.WasPassed()) {
      callbacksHolder.mActorCreated.Value()->Call(this);
    }
  }
}

nsresult JSActor::QueryInterfaceActor(const nsIID& aIID, void** aPtr) {
  MOZ_ASSERT(nsContentUtils::IsSafeToRunScript());
  if (!GetWrapperPreserveColor()) {
    // If we have no preserved wrapper, we won't implement any interfaces.
    return NS_NOINTERFACE;
  }

  if (!mWrappedJS) {
    AutoEntryScript aes(GetParentObject(), "JSActor query interface");
    JSContext* cx = aes.cx();

    JS::Rooted<JSObject*> self(cx, GetWrapper());
    JSAutoRealm ar(cx, self);

    RefPtr<nsXPCWrappedJS> wrappedJS;
    nsresult rv = nsXPCWrappedJS::GetNewOrUsed(
        cx, self, NS_GET_IID(nsISupports), getter_AddRefs(wrappedJS));
    NS_ENSURE_SUCCESS(rv, rv);

    mWrappedJS = do_QueryInterface(wrappedJS);
    MOZ_ASSERT(mWrappedJS);
  }

  return mWrappedJS->QueryInterface(aIID, aPtr);
}

void JSActor::Init(const nsACString& aName) {
  MOZ_ASSERT(mName.IsEmpty(), "Cannot set name twice!");
  mName = aName;
  InvokeCallback(CallbackFunction::ActorCreated);
}

void JSActor::ThrowStateErrorForGetter(const char* aName,
                                       ErrorResult& aRv) const {
  if (mName.IsEmpty()) {
    aRv.ThrowInvalidStateError(nsPrintfCString(
        "Cannot access property '%s' before actor is initialized", aName));
  } else {
    aRv.ThrowInvalidStateError(nsPrintfCString(
        "Cannot access property '%s' after actor '%s' has been destroyed",
        aName, mName.get()));
  }
}

static Maybe<ipc::StructuredCloneData> TryClone(JSContext* aCx,
                                                JS::Handle<JS::Value> aValue) {
  Maybe<ipc::StructuredCloneData> data{std::in_place};

  // Try to directly serialize the passed-in data, and return it to our caller.
  IgnoredErrorResult rv;
  data->Write(aCx, aValue, rv);
  if (rv.Failed()) {
    // Serialization failed, return `Nothing()` instead.
    JS_ClearPendingException(aCx);
    data.reset();
  }
  return data;
}

static Maybe<ipc::StructuredCloneData> CloneJSStack(
    JSContext* aCx, JS::Handle<JSObject*> aStack) {
  JS::Rooted<JS::Value> stackVal(aCx, JS::ObjectOrNullValue(aStack));
  return TryClone(aCx, stackVal);
}

static Maybe<ipc::StructuredCloneData> CaptureJSStack(JSContext* aCx) {
  JS::Rooted<JSObject*> stack(aCx, nullptr);
  if (JS::IsAsyncStackCaptureEnabledForRealm(aCx) &&
      !JS::CaptureCurrentStack(aCx, &stack)) {
    JS_ClearPendingException(aCx);
  }

  return CloneJSStack(aCx, stack);
}

void JSActor::SendAsyncMessage(JSContext* aCx, const nsAString& aMessageName,
                               JS::Handle<JS::Value> aObj,
                               JS::Handle<JS::Value> aTransfers,
                               ErrorResult& aRv) {
  profiler_add_marker("SendAsyncMessage", geckoprofiler::category::IPC, {},
                      JSActorMessageMarker{}, mName, aMessageName);
  Maybe<ipc::StructuredCloneData> data{std::in_place};
  if (!nsFrameMessageManager::GetParamsForMessage(aCx, aObj, aTransfers,
                                                  *data)) {
    aRv.ThrowDataCloneError(nsPrintfCString(
        "Failed to serialize message '%s::%s'",
        NS_LossyConvertUTF16toASCII(aMessageName).get(), mName.get()));
    return;
  }

  JSActorMessageMeta meta;
  meta.actorName() = mName;
  meta.messageName() = aMessageName;
  meta.kind() = JSActorMessageKind::Message;

  SendRawMessage(meta, std::move(data), CaptureJSStack(aCx), aRv);
}

already_AddRefed<Promise> JSActor::SendQuery(JSContext* aCx,
                                             const nsAString& aMessageName,
                                             JS::Handle<JS::Value> aObj,
                                             ErrorResult& aRv) {
  profiler_add_marker("SendQuery", geckoprofiler::category::IPC, {},
                      JSActorMessageMarker{}, mName, aMessageName);
  Maybe<ipc::StructuredCloneData> data{std::in_place};
  if (!nsFrameMessageManager::GetParamsForMessage(
          aCx, aObj, JS::UndefinedHandleValue, *data)) {
    aRv.ThrowDataCloneError(nsPrintfCString(
        "Failed to serialize message '%s::%s'",
        NS_LossyConvertUTF16toASCII(aMessageName).get(), mName.get()));
    return nullptr;
  }

  nsIGlobalObject* global = xpc::CurrentNativeGlobal(aCx);
  if (NS_WARN_IF(!global)) {
    aRv.ThrowUnknownError("Unable to get current native global");
    return nullptr;
  }

  RefPtr<Promise> promise = Promise::Create(global, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  JSActorMessageMeta meta;
  meta.actorName() = mName;
  meta.messageName() = aMessageName;
  meta.queryId() = mNextQueryId++;
  meta.kind() = JSActorMessageKind::Query;

  SendRawMessage(meta, std::move(data), CaptureJSStack(aCx), aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  mPendingQueries.InsertOrUpdate(meta.queryId(),
                                 PendingQuery{promise, meta.messageName()});

  return promise.forget();
}

void JSActor::CallReceiveMessage(JSContext* aCx,
                                 const JSActorMessageMeta& aMetadata,
                                 JS::Handle<JS::Value> aData,
                                 JS::MutableHandle<JS::Value> aRetVal,
                                 ErrorResult& aRv) {
  // The argument which we want to pass to IPC.
  RootedDictionary<ReceiveMessageArgument> argument(aCx);
  argument.mTarget = this;
  argument.mName = aMetadata.messageName();
  argument.mData = aData;
  argument.mJson = aData;
  argument.mSync = false;

  if (GetWrapperPreserveColor()) {
    // Invoke the actual callback.
    JS::Rooted<JSObject*> global(aCx, JS::GetNonCCWObjectGlobal(GetWrapper()));
    RefPtr<MessageListener> messageListener =
        new MessageListener(GetWrapper(), global, nullptr, nullptr);
    messageListener->ReceiveMessage(argument, aRetVal, aRv,
                                    "JSActor receive message",
                                    MessageListener::eRethrowExceptions);
  } else {
    aRv.ThrowTypeError<MSG_NOT_CALLABLE>("Property 'receiveMessage'");
  }
}

void JSActor::ReceiveMessage(JSContext* aCx,
                             const JSActorMessageMeta& aMetadata,
                             JS::Handle<JS::Value> aData, ErrorResult& aRv) {
  MOZ_ASSERT(aMetadata.kind() == JSActorMessageKind::Message);
  profiler_add_marker("ReceiveMessage", geckoprofiler::category::IPC, {},
                      JSActorMessageMarker{}, mName, aMetadata.messageName());

  JS::Rooted<JS::Value> retval(aCx);
  CallReceiveMessage(aCx, aMetadata, aData, &retval, aRv);
}

void JSActor::ReceiveQuery(JSContext* aCx, const JSActorMessageMeta& aMetadata,
                           JS::Handle<JS::Value> aData, ErrorResult& aRv) {
  MOZ_ASSERT(aMetadata.kind() == JSActorMessageKind::Query);
  profiler_add_marker("ReceiveQuery", geckoprofiler::category::IPC, {},
                      JSActorMessageMarker{}, mName, aMetadata.messageName());

  // This promise will be resolved or rejected once the listener has been
  // called. Our listener on this promise will then send the reply.
  RefPtr<Promise> promise = Promise::Create(GetParentObject(), aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return;
  }

  RefPtr<QueryHandler> handler = new QueryHandler(this, aMetadata, promise);
  promise->AppendNativeHandler(handler);

  ErrorResult error;
  JS::Rooted<JS::Value> retval(aCx);
  CallReceiveMessage(aCx, aMetadata, aData, &retval, error);

  // If we have a promise, resolve or reject it respectively.
  if (error.Failed()) {
    if (error.IsUncatchableException()) {
      promise->MaybeRejectWithTimeoutError(
          "Message handler threw uncatchable exception");
    } else {
      promise->MaybeReject(std::move(error));
    }
  } else {
    promise->MaybeResolve(retval);
  }
  error.SuppressException();
}

void JSActor::ReceiveQueryReply(JSContext* aCx,
                                const JSActorMessageMeta& aMetadata,
                                JS::Handle<JS::Value> aData, ErrorResult& aRv) {
  if (NS_WARN_IF(aMetadata.actorName() != mName)) {
    aRv.ThrowUnknownError("Mismatched actor name for query reply");
    return;
  }

  Maybe<PendingQuery> query = mPendingQueries.Extract(aMetadata.queryId());
  if (NS_WARN_IF(!query)) {
    aRv.ThrowUnknownError("Received reply for non-pending query");
    return;
  }

  profiler_add_marker("ReceiveQueryReply", geckoprofiler::category::IPC, {},
                      JSActorMessageMarker{}, mName, aMetadata.messageName());

  Promise* promise = query->mPromise;
  JSAutoRealm ar(aCx, promise->PromiseObj());
  JS::RootedValue data(aCx, aData);
  if (NS_WARN_IF(!JS_WrapValue(aCx, &data))) {
    aRv.NoteJSContextException(aCx);
    return;
  }

  if (aMetadata.kind() == JSActorMessageKind::QueryResolve) {
    promise->MaybeResolve(data);
  } else {
    promise->MaybeReject(data);
  }
}

void JSActor::SendRawMessageInProcess(const JSActorMessageMeta& aMeta,
                                      Maybe<ipc::StructuredCloneData>&& aData,
                                      Maybe<ipc::StructuredCloneData>&& aStack,
                                      OtherSideCallback&& aGetOtherSide) {
  MOZ_DIAGNOSTIC_ASSERT(XRE_IsParentProcess());
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      "JSActor Async Message",
      [aMeta, data{std::move(aData)}, stack{std::move(aStack)},
       getOtherSide{std::move(aGetOtherSide)}]() mutable {
        if (RefPtr<JSActorManager> otherSide = getOtherSide()) {
          otherSide->ReceiveRawMessage(aMeta, std::move(data),
                                       std::move(stack));
        }
      }));
}

// Native handler for our generated promise which is used to handle Queries and
// send the reply when their promises have been resolved.
JSActor::QueryHandler::QueryHandler(JSActor* aActor,
                                    const JSActorMessageMeta& aMetadata,
                                    Promise* aPromise)
    : mActor(aActor),
      mPromise(aPromise),
      mMessageName(aMetadata.messageName()),
      mQueryId(aMetadata.queryId()) {}

void JSActor::QueryHandler::RejectedCallback(JSContext* aCx,
                                             JS::Handle<JS::Value> aValue,
                                             ErrorResult& aRv) {
  if (!mActor) {
    // Make sure that this rejection is reported. See comment below.
    if (!JS::CallOriginalPromiseReject(aCx, aValue)) {
      JS_ClearPendingException(aCx);
    }
    return;
  }

  JS::Rooted<JS::Value> value(aCx, aValue);
  if (value.isObject()) {
    JS::Rooted<JSObject*> error(aCx, &value.toObject());
    if (UniquePtr<ClonedErrorHolder> ceh =
            ClonedErrorHolder::Create(aCx, error, IgnoreErrors())) {
      if (!ToJSValue(aCx, std::move(ceh), &value)) {
        JS_ClearPendingException(aCx);
      }
    } else {
      JS_ClearPendingException(aCx);
    }
  }

  Maybe<ipc::StructuredCloneData> data = TryClone(aCx, value);
  if (!data) {
    // Failed to clone the rejection value. Make sure that this
    // rejection is reported, despite being "handled". This is done by
    // creating a new promise in the rejected state, and throwing it
    // away. This will be reported as an unhandled rejected promise.
    if (!JS::CallOriginalPromiseReject(aCx, aValue)) {
      JS_ClearPendingException(aCx);
    }
  }

  SendReply(aCx, JSActorMessageKind::QueryReject, std::move(data));
}

void JSActor::QueryHandler::ResolvedCallback(JSContext* aCx,
                                             JS::Handle<JS::Value> aValue,
                                             ErrorResult& aRv) {
  if (!mActor) {
    return;
  }

  Maybe<ipc::StructuredCloneData> data = TryClone(aCx, aValue);
  if (!data) {
    nsAutoCString msg;
    msg.Append(mActor->Name());
    msg.Append(':');
    msg.Append(NS_LossyConvertUTF16toASCII(mMessageName));
    msg.AppendLiteral(": message reply cannot be cloned.");

    auto exc = MakeRefPtr<Exception>(msg, NS_ERROR_FAILURE, "DataCloneError"_ns,
                                     nullptr, nullptr);

    JS::Rooted<JS::Value> val(aCx);
    if (ToJSValue(aCx, exc, &val)) {
      RejectedCallback(aCx, val, aRv);
    } else {
      JS_ClearPendingException(aCx);
    }
    return;
  }

  SendReply(aCx, JSActorMessageKind::QueryResolve, std::move(data));
}

void JSActor::QueryHandler::SendReply(JSContext* aCx, JSActorMessageKind aKind,
                                      Maybe<ipc::StructuredCloneData>&& aData) {
  MOZ_ASSERT(mActor);
  profiler_add_marker("SendQueryReply", geckoprofiler::category::IPC, {},
                      JSActorMessageMarker{}, mActor->Name(), mMessageName);

  JSActorMessageMeta meta;
  meta.actorName() = mActor->Name();
  meta.messageName() = mMessageName;
  meta.queryId() = mQueryId;
  meta.kind() = aKind;

  JS::Rooted<JSObject*> promise(aCx, mPromise->PromiseObj());
  JS::Rooted<JSObject*> stack(aCx, JS::GetPromiseResolutionSite(promise));

  mActor->SendRawMessage(meta, std::move(aData), CloneJSStack(aCx, stack),
                         IgnoreErrors());
  mActor = nullptr;
  mPromise = nullptr;
}

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(JSActor::QueryHandler)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(JSActor::QueryHandler)
NS_IMPL_CYCLE_COLLECTING_RELEASE(JSActor::QueryHandler)

NS_IMPL_CYCLE_COLLECTION(JSActor::QueryHandler, mActor, mPromise)

}  // namespace mozilla::dom
