/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_JSActor_h
#define mozilla_dom_JSActor_h

#include "js/TypeDecls.h"
#include "ipc/EnumSerializer.h"
#include "mozilla/Attributes.h"
#include "mozilla/dom/PromiseNativeHandler.h"
#include "nsCycleCollectionParticipant.h"
#include "nsTHashMap.h"
#include "nsWrapperCache.h"

class nsIGlobalObject;
class nsQueryJSActor;

namespace mozilla {
class ErrorResult;

namespace dom {

namespace ipc {
class StructuredCloneData;
}

class JSActorManager;
class JSActorMessageMeta;
class QueryPromiseHandler;

enum class JSActorMessageKind {
  Message,
  Query,
  QueryResolve,
  QueryReject,
  EndGuard_,
};

// Common base class for JSWindowActor{Parent,Child}.
class JSActor : public nsISupports, public nsWrapperCache {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(JSActor)

  explicit JSActor(nsISupports* aGlobal = nullptr);

  const nsCString& Name() const { return mName; }
  void GetName(nsCString& aName) { aName = Name(); }

  void SendAsyncMessage(JSContext* aCx, const nsAString& aMessageName,
                        JS::Handle<JS::Value> aObj,
                        JS::Handle<JS::Value> aTransfers, ErrorResult& aRv);

  already_AddRefed<Promise> SendQuery(JSContext* aCx,
                                      const nsAString& aMessageName,
                                      JS::Handle<JS::Value> aObj,
                                      ErrorResult& aRv);

  nsIGlobalObject* GetParentObject() const { return mGlobal; };

 protected:
  // Send the message described by the structured clone data |aData|, and the
  // message metadata |aMetadata|. The underlying transport should call the
  // |ReceiveMessage| method on the other side asynchronously.
  virtual void SendRawMessage(const JSActorMessageMeta& aMetadata,
                              Maybe<ipc::StructuredCloneData>&& aData,
                              Maybe<ipc::StructuredCloneData>&& aStack,
                              ErrorResult& aRv) = 0;

  // Helper method to send an in-process raw message.
  using OtherSideCallback = std::function<already_AddRefed<JSActorManager>()>;
  static void SendRawMessageInProcess(const JSActorMessageMeta& aMeta,
                                      Maybe<ipc::StructuredCloneData>&& aData,
                                      Maybe<ipc::StructuredCloneData>&& aStack,
                                      OtherSideCallback&& aGetOtherSide);

  virtual ~JSActor() = default;

  void Init(const nsACString& aName);

  bool CanSend() const { return mCanSend; }

  void ThrowStateErrorForGetter(const char* aName, ErrorResult& aRv) const;

  void StartDestroy();
  void AfterDestroy();

  enum class CallbackFunction { DidDestroy, ActorCreated };
  void InvokeCallback(CallbackFunction callback);

  virtual void ClearManager() = 0;

 private:
  friend class JSActorManager;
  friend class ::nsQueryJSActor;  // for QueryInterfaceActor

  nsresult QueryInterfaceActor(const nsIID& aIID, void** aPtr);

  // Called by JSActorManager when they receive raw message data destined for
  // this actor.
  void ReceiveMessage(JSContext* aCx, const JSActorMessageMeta& aMetadata,
                      JS::Handle<JS::Value> aData, ErrorResult& aRv);
  void ReceiveQuery(JSContext* aCx, const JSActorMessageMeta& aMetadata,
                    JS::Handle<JS::Value> aData, ErrorResult& aRv);
  void ReceiveQueryReply(JSContext* aCx, const JSActorMessageMeta& aMetadata,
                         JS::Handle<JS::Value> aData, ErrorResult& aRv);

  // Call the actual `ReceiveMessage` method, and get the return value.
  void CallReceiveMessage(JSContext* aCx, const JSActorMessageMeta& aMetadata,
                          JS::Handle<JS::Value> aData,
                          JS::MutableHandle<JS::Value> aRetVal,
                          ErrorResult& aRv);

  // Helper object used while processing query messages to send the final reply
  // message.
  class QueryHandler final : public PromiseNativeHandler {
   public:
    NS_DECL_CYCLE_COLLECTING_ISUPPORTS
    NS_DECL_CYCLE_COLLECTION_CLASS(QueryHandler)

    QueryHandler(JSActor* aActor, const JSActorMessageMeta& aMetadata,
                 Promise* aPromise);

    void RejectedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                          ErrorResult& aRv) override;

    void ResolvedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                          ErrorResult& aRv) override;

   private:
    ~QueryHandler() = default;

    void SendReply(JSContext* aCx, JSActorMessageKind aKind,
                   Maybe<ipc::StructuredCloneData>&& aData);

    RefPtr<JSActor> mActor;
    RefPtr<Promise> mPromise;
    nsString mMessageName;
    uint64_t mQueryId;
  };

  // A query which hasn't been resolved yet, along with metadata about what
  // query the promise is for.
  struct PendingQuery {
    RefPtr<Promise> mPromise;
    nsString mMessageName;
  };

  nsCOMPtr<nsIGlobalObject> mGlobal;
  nsCOMPtr<nsISupports> mWrappedJS;
  nsCString mName;
  nsTHashMap<nsUint64HashKey, PendingQuery> mPendingQueries;
  uint64_t mNextQueryId = 0;
  bool mCanSend = true;
};

}  // namespace dom
}  // namespace mozilla

namespace IPC {

template <>
struct ParamTraits<mozilla::dom::JSActorMessageKind>
    : public ContiguousEnumSerializer<
          mozilla::dom::JSActorMessageKind,
          mozilla::dom::JSActorMessageKind::Message,
          mozilla::dom::JSActorMessageKind::EndGuard_> {};

}  // namespace IPC

#endif  // !defined(mozilla_dom_JSActor_h)
