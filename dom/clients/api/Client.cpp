/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Client.h"

#include "ClientDOMUtil.h"
#include "mozilla/dom/ClientHandle.h"
#include "mozilla/dom/ClientIPCTypes.h"
#include "mozilla/dom/ClientManager.h"
#include "mozilla/dom/ClientState.h"
#include "mozilla/dom/DOMMozPromiseRequestHolder.h"
#include "mozilla/dom/MessagePortBinding.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/WorkerScope.h"
#include "nsIDUtils.h"
#include "nsIGlobalObject.h"
#include "nsNetUtil.h"
#include "nsURLHelper.h"

namespace mozilla::dom {

using mozilla::dom::ipc::StructuredCloneData;

NS_IMPL_CYCLE_COLLECTING_ADDREF(mozilla::dom::Client);
NS_IMPL_CYCLE_COLLECTING_RELEASE(mozilla::dom::Client);
NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(mozilla::dom::Client, mGlobal);

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(mozilla::dom::Client)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

void Client::EnsureHandle() {
  NS_ASSERT_OWNINGTHREAD(mozilla::dom::Client);
  if (!mHandle) {
    mHandle = ClientManager::CreateHandle(ClientInfo(mData->info()),
                                          mGlobal->SerialEventTarget());
  }
}

Client::Client(nsIGlobalObject* aGlobal, const ClientInfoAndState& aData)
    : mGlobal(aGlobal), mData(std::make_unique<ClientInfoAndState>(aData)) {
  MOZ_DIAGNOSTIC_ASSERT(mGlobal);
}

TimeStamp Client::CreationTime() const { return mData->info().creationTime(); }

TimeStamp Client::LastFocusTime() const {
  if (mData->info().type() != ClientType::Window) {
    return TimeStamp();
  }
  return mData->state().get_IPCClientWindowState().lastFocusTime();
}

StorageAccess Client::GetStorageAccess() const {
  ClientState state(ClientState::FromIPC(mData->state()));
  return state.GetStorageAccess();
}

JSObject* Client::WrapObject(JSContext* aCx,
                             JS::Handle<JSObject*> aGivenProto) {
  if (mData->info().type() == ClientType::Window) {
    return WindowClient_Binding::Wrap(aCx, this, aGivenProto);
  }
  return Client_Binding::Wrap(aCx, this, aGivenProto);
}

nsIGlobalObject* Client::GetParentObject() const { return mGlobal; }

void Client::GetUrl(nsAString& aUrlOut) const {
  CopyUTF8toUTF16(mData->info().url(), aUrlOut);
}

void Client::GetId(nsAString& aIdOut) const {
  aIdOut = NSID_TrimBracketsUTF16(mData->info().id());
}

ClientType Client::Type() const { return mData->info().type(); }

FrameType Client::GetFrameType() const { return mData->info().frameType(); }

void Client::PostMessage(JSContext* aCx, JS::Handle<JS::Value> aMessage,
                         const Sequence<JSObject*>& aTransferable,
                         ErrorResult& aRv) {
  MOZ_ASSERT(!NS_IsMainThread());
  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
  MOZ_DIAGNOSTIC_ASSERT(workerPrivate);
  MOZ_DIAGNOSTIC_ASSERT(workerPrivate->IsServiceWorker());
  workerPrivate->AssertIsOnWorkerThread();

  JS::Rooted<JS::Value> transferable(aCx, JS::UndefinedValue());
  aRv = nsContentUtils::CreateJSValueFromSequenceOfObject(aCx, aTransferable,
                                                          &transferable);
  if (aRv.Failed()) {
    return;
  }

  auto data = MakeNotNull<RefPtr<StructuredCloneData>>();
  data->Write(aCx, aMessage, transferable, JS::CloneDataPolicy(), aRv);
  if (aRv.Failed()) {
    return;
  }

  EnsureHandle();
  mHandle->PostMessage(data, workerPrivate->GetServiceWorkerDescriptor());
}

void Client::PostMessage(JSContext* aCx, JS::Handle<JS::Value> aMessage,
                         const StructuredSerializeOptions& aOptions,
                         ErrorResult& aRv) {
  PostMessage(aCx, aMessage, aOptions.mTransfer, aRv);
}

VisibilityState Client::GetVisibilityState() const {
  return mData->state().get_IPCClientWindowState().visibilityState();
}

bool Client::Focused() const {
  return mData->state().get_IPCClientWindowState().focused();
}

already_AddRefed<Promise> Client::Focus(CallerType aCallerType,
                                        ErrorResult& aRv) {
  MOZ_ASSERT(!NS_IsMainThread());
  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
  MOZ_DIAGNOSTIC_ASSERT(workerPrivate);
  MOZ_DIAGNOSTIC_ASSERT(workerPrivate->IsServiceWorker());
  workerPrivate->AssertIsOnWorkerThread();

  RefPtr<Promise> outerPromise = Promise::Create(mGlobal, aRv);
  if (aRv.Failed()) {
    return outerPromise.forget();
  }

  if (!workerPrivate->GlobalScope()->WindowInteractionAllowed()) {
    outerPromise->MaybeReject(NS_ERROR_DOM_INVALID_ACCESS_ERR);
    return outerPromise.forget();
  }

  EnsureHandle();

  IPCClientInfo ipcClientInfo(mData->info());
  auto holder =
      MakeRefPtr<DOMMozPromiseRequestHolder<ClientStatePromise>>(mGlobal);

  mHandle->Focus(aCallerType)
      ->Then(
          mGlobal->SerialEventTarget(), __func__,
          [ipcClientInfo = std::move(ipcClientInfo), holder,
           outerPromise](const ClientState& aResult) {
            holder->Complete();
            NS_ENSURE_TRUE_VOID(holder->GetParentObject());
            RefPtr<Client> newClient =
                new Client(holder->GetParentObject(),
                           ClientInfoAndState(ipcClientInfo, aResult.ToIPC()));
            outerPromise->MaybeResolve(newClient);
          },
          [holder, outerPromise](const CopyableErrorResult& aResult) {
            holder->Complete();
            // MaybeReject needs a non-const-ref result, so make a copy.
            outerPromise->MaybeReject(CopyableErrorResult(aResult));
          })
      ->Track(*holder);

  return outerPromise.forget();
}

already_AddRefed<Promise> Client::Navigate(const nsAString& aURL,
                                           ErrorResult& aRv) {
  MOZ_ASSERT(!NS_IsMainThread());
  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
  MOZ_DIAGNOSTIC_ASSERT(workerPrivate);
  MOZ_DIAGNOSTIC_ASSERT(workerPrivate->IsServiceWorker());
  workerPrivate->AssertIsOnWorkerThread();

  NS_ConvertUTF16toUTF8 spec(aURL);
  nsIURI* baseURI = workerPrivate->GetBaseURI();

  // There is an edge case for view-source url here. According to the wpt test
  // windowclient-navigate.https.html, a view-source URL with a relative inner
  // URL should be treated as an invalid URL. However, we will still resolve it
  // into a valid view-source URL since the baseURL is involved while creating
  // the URI. So, an invalid view-source URL will be treated as a valid URL
  // in this case. To address this, we should not take the baseURL into account
  // for the view-source URL.
  bool shouldUseBaseURL = true;
  nsAutoCString scheme;
  if (NS_SUCCEEDED(net_ExtractURLScheme(spec, scheme)) &&
      scheme.LowerCaseEqualsLiteral("view-source")) {
    shouldUseBaseURL = false;
  }

  nsCOMPtr<nsIURI> uri;
  nsresult rv = NS_NewURI(getter_AddRefs(uri), spec, nullptr,
                          shouldUseBaseURL ? baseURI : nullptr);
  if (NS_FAILED(rv)) {
    // Per https://w3c.github.io/ServiceWorker/#dom-windowclient-navigate step
    // 2, if the URL fails to parse, we reject with a TypeError.
    aRv.ThrowTypeError<MSG_INVALID_URL>(spec);
    return nullptr;
  }

  if (NS_IsAboutBlankAllowQueryAndFragment(uri)) {
    aRv.ThrowTypeError("Navigation to \"about:blank\" is not allowed");
    return nullptr;
  }

  RefPtr<Promise> outerPromise = Promise::Create(mGlobal, aRv);
  if (aRv.Failed()) {
    return outerPromise.forget();
  }

  ClientNavigateArgs args(mData->info(), WrapNotNull(uri.get()),
                          WrapNotNull(baseURI),
                          workerPrivate->GetServiceWorkerDescriptor().ToIPC());
  RefPtr<Client> self = this;

  StartClientManagerOp(
      &ClientManager::Navigate, args, mGlobal,
      [self, outerPromise](const ClientOpResult& aResult) {
        if (aResult.type() != ClientOpResult::TClientInfoAndState) {
          outerPromise->MaybeResolve(JS::NullHandleValue);
          return;
        }
        RefPtr<Client> newClient =
            new Client(self->mGlobal, aResult.get_ClientInfoAndState());
        outerPromise->MaybeResolve(newClient);
      },
      [self, outerPromise](const CopyableErrorResult& aResult) {
        // MaybeReject needs a non-const-ref result, so make a copy.
        outerPromise->MaybeReject(CopyableErrorResult(aResult));
      });

  return outerPromise.forget();
}

}  // namespace mozilla::dom
