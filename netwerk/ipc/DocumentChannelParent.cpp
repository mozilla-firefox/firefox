/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DocumentChannelParent.h"

#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/ClientInfo.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/net/NeckoChannelParams.h"
#include "nsDocShellLoadState.h"

extern mozilla::LazyLogModule gDocumentChannelLog;
#define LOG(fmt) MOZ_LOG(gDocumentChannelLog, mozilla::LogLevel::Verbose, fmt)

using namespace mozilla::dom;

namespace mozilla {
namespace net {

DocumentChannelParent::DocumentChannelParent() {
  LOG(("DocumentChannelParent ctor [this=%p]", this));
}

DocumentChannelParent::~DocumentChannelParent() {
  LOG(("DocumentChannelParent dtor [this=%p]", this));
}

bool DocumentChannelParent::Init(dom::CanonicalBrowsingContext* aContext,
                                 const DocumentChannelCreationArgs& aArgs) {
  RefPtr<nsDocShellLoadState> loadState = aArgs.loadState();
  LOG(("DocumentChannelParent Init [this=%p, uri=%s]", this,
       loadState->URI()->GetSpecOrDefault().get()));
  if (aArgs.parentInitiatedNavigationEpoch() <
      aContext->GetParentInitiatedNavigationEpoch()) {
    nsresult rv = NS_BINDING_ABORTED;
    return SendFailedAsyncOpen(rv);
  }

  ContentParent* contentParent =
      mozilla::ipc::ActorCast<ContentParent>(Manager()->Manager());

  RefPtr<DocumentLoadListener::OpenPromise> promise;
  mDocumentLoadListener = loadState->TakeSpeculativeListener();
  if (mDocumentLoadListener) {
    promise = mDocumentLoadListener->ClaimParentLoad(Some(aArgs.channelId()));
  } else {
    bool isDocumentLoad =
        aArgs.elementCreationArgs().type() ==
        DocumentChannelElementCreationArgs::TDocumentCreationArgs;
    mDocumentLoadListener = new DocumentLoadListener(aContext, isDocumentLoad);

    Maybe<ClientInfo> clientInfo;
    if (aArgs.initialClientInfo().isSome()) {
      clientInfo.emplace(ClientInfo(aArgs.initialClientInfo().ref()));
    }

    nsresult rv = NS_ERROR_UNEXPECTED;

    if (isDocumentLoad) {
      const DocumentCreationArgs& docArgs = aArgs.elementCreationArgs();

      promise = mDocumentLoadListener->OpenDocument(
          loadState, docArgs.loadFlags(), aArgs.cacheKey(),
          Some(aArgs.channelId()), aArgs.asyncOpenTime(), aArgs.timing(),
          std::move(clientInfo), docArgs.uriModified(),
          Some(docArgs.isEmbeddingBlockedError()), contentParent, &rv);
    } else {
      const ObjectCreationArgs& objectArgs = aArgs.elementCreationArgs();

      promise = mDocumentLoadListener->OpenObject(
          loadState, aArgs.cacheKey(), Some(aArgs.channelId()),
          aArgs.asyncOpenTime(), aArgs.timing(), std::move(clientInfo),
          objectArgs.embedderInnerWindowId(), objectArgs.loadFlags(),
          objectArgs.contentPolicyType(), objectArgs.isUrgentStart(),
          contentParent, this /* ObjectUpgradeHandler */, &rv);
    }

    if (NS_FAILED(rv)) {
      MOZ_ASSERT(!promise);
      return SendFailedAsyncOpen(rv);
    }
  }

  RefPtr<DocumentChannelParent> self = this;
  promise->Then(
      GetCurrentSerialEventTarget(), __func__,
      [self](DocumentLoadListener::OpenPromiseSucceededType&& aResolveValue) {
        // The DLL is waiting for us to resolve the
        // PDocumentChannel::RedirectToRealChannelPromise given as parameter.
        auto promise = self->RedirectToRealChannel(
            std::move(aResolveValue.mStreamFilterEndpoints),
            aResolveValue.mRedirectFlags, aResolveValue.mLoadFlags,
            aResolveValue.mEarlyHintLinkType);
        // We chain the promise the DLL is waiting on to the one returned by
        // RedirectToRealChannel. As soon as the promise returned is resolved
        // or rejected, so will the DLL's promise.
        promise->ChainTo(aResolveValue.mPromise.forget(), __func__);
        self->mDocumentLoadListener = nullptr;
      },
      [self](DocumentLoadListener::OpenPromiseFailedType&& aRejectValue) {
        if (self->CanSend()) {
          (void)self->SendDisconnectChildListeners(
              aRejectValue.mStatus, aRejectValue.mLoadGroupStatus,
              aRejectValue.mContinueNavigating);
        }
        self->mDocumentLoadListener = nullptr;
      });

  return true;
}

auto DocumentChannelParent::UpgradeObjectLoad()
    -> RefPtr<ObjectUpgradePromise> {
  return SendUpgradeObjectLoad()->Then(
      GetCurrentSerialEventTarget(), __func__,
      [](const UpgradeObjectLoadPromise::ResolveOrRejectValue& aValue) {
        if (!aValue.IsResolve() || aValue.ResolveValue().IsNullOrDiscarded()) {
          LOG(("DocumentChannelParent object load upgrade failed"));
          return ObjectUpgradePromise::CreateAndReject(NS_ERROR_FAILURE,
                                                       __func__);
        }

        return ObjectUpgradePromise::CreateAndResolve(
            aValue.ResolveValue().get_canonical(), __func__);
      });
}

RefPtr<PDocumentChannelParent::RedirectToRealChannelPromise>
DocumentChannelParent::RedirectToRealChannel(
    nsTArray<ipc::Endpoint<extensions::PStreamFilterParent>>&&
        aStreamFilterEndpoints,
    uint32_t aRedirectFlags, uint32_t aLoadFlags, uint32_t aEarlyHintLinkType) {
  if (!CanSend()) {
    return PDocumentChannelParent::RedirectToRealChannelPromise::
        CreateAndReject(ResponseRejectReason::ChannelClosed, __func__);
  }

  ContentParent* cp =
      mozilla::ipc::ActorCast<ContentParent>(Manager()->Manager());
  nsTArray<EarlyHintConnectArgs> earlyHints;
  mDocumentLoadListener->RegisterEarlyHintLinksAndGetConnectArgs(cp->ChildID(),
                                                                 earlyHints);

  RedirectToRealChannelArgs args;
  mDocumentLoadListener->SerializeRedirectData(
      args, false, aRedirectFlags, aLoadFlags, std::move(earlyHints),
      aEarlyHintLinkType);
  return SendRedirectToRealChannel(args, std::move(aStreamFilterEndpoints));
}

}  // namespace net
}  // namespace mozilla

#undef LOG
