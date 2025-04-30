/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/JSWindowActorBinding.h"
#include "mozilla/dom/JSWindowActorParent.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/WindowGlobalChild.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "mozilla/dom/MessageManagerBinding.h"

namespace mozilla::dom {

JSWindowActorParent::~JSWindowActorParent() { MOZ_ASSERT(!mManager); }

JSObject* JSWindowActorParent::WrapObject(JSContext* aCx,
                                          JS::Handle<JSObject*> aGivenProto) {
  return JSWindowActorParent_Binding::Wrap(aCx, this, aGivenProto);
}

WindowGlobalParent* JSWindowActorParent::GetManager() const { return mManager; }

WindowContext* JSWindowActorParent::GetWindowContext() const {
  return mManager;
}

void JSWindowActorParent::Init(const nsACString& aName,
                               WindowGlobalParent* aManager) {
  MOZ_ASSERT(!mManager, "Cannot Init() a JSWindowActorParent twice!");
  mManager = aManager;
  JSActor::Init(aName);
}

void JSWindowActorParent::SendRawMessage(
    const JSActorMessageMeta& aMeta, Maybe<ipc::StructuredCloneData>&& aData,
    Maybe<ipc::StructuredCloneData>&& aStack, ErrorResult& aRv) {
  if (NS_WARN_IF(!CanSend() || !mManager || !mManager->CanSend())) {
    aRv.ThrowInvalidStateError("JSWindowActorParent cannot send at the moment");
    return;
  }

  if (mManager->IsInProcess()) {
    SendRawMessageInProcess(
        aMeta, std::move(aData), std::move(aStack),
        [manager{mManager}]() { return manager->GetChildActor(); });
    return;
  }

  Maybe<ClonedMessageData> msgData;
  if (aData) {
    msgData.emplace();
    if (NS_WARN_IF(!aData->BuildClonedMessageData(*msgData))) {
      aRv.ThrowDataCloneError(
          nsPrintfCString("JSWindowActorParent serialization error: cannot "
                          "clone, in actor '%s'",
                          PromiseFlatCString(aMeta.actorName()).get()));
      return;
    }
  }

  Maybe<ClonedMessageData> stackData;
  if (aStack) {
    stackData.emplace();
    if (!aStack->BuildClonedMessageData(*stackData)) {
      stackData.reset();
    }
  }

  if (NS_WARN_IF(!mManager->SendRawMessage(aMeta, msgData, stackData))) {
    aRv.ThrowOperationError(
        nsPrintfCString("JSWindowActorParent send error in actor '%s'",
                        PromiseFlatCString(aMeta.actorName()).get()));
    return;
  }
}

CanonicalBrowsingContext* JSWindowActorParent::GetBrowsingContext(
    ErrorResult& aRv) {
  if (!mManager) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return nullptr;
  }

  return mManager->BrowsingContext();
}

void JSWindowActorParent::ClearManager() { mManager = nullptr; }

NS_IMPL_CYCLE_COLLECTION_INHERITED(JSWindowActorParent, JSActor, mManager)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(JSWindowActorParent)
NS_INTERFACE_MAP_END_INHERITING(JSActor)

NS_IMPL_ADDREF_INHERITED(JSWindowActorParent, JSActor)
NS_IMPL_RELEASE_INHERITED(JSWindowActorParent, JSActor)

}  // namespace mozilla::dom
