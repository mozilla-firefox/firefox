/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SimpleChannel.h"

#include "mozilla/Try.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/net/NeckoChild.h"
#include "nsBaseChannel.h"
#include "nsICancelable.h"
#include "nsIChannel.h"
#include "nsIChildChannel.h"
#include "nsIInputStream.h"
#include "nsIRequest.h"
#include "nsISupportsImpl.h"
#include "nsNetUtil.h"

namespace mozilla {
namespace net {

NS_IMPL_ISUPPORTS_INHERITED(SimpleChannel, nsBaseChannel, nsIChildChannel)

SimpleChannel::SimpleChannel(UniquePtr<SimpleChannelCallbacks>&& aCallbacks)
    : mCallbacks(std::move(aCallbacks)) {
  EnableSynthesizedProgressEvents(true);
}

nsresult SimpleChannel::OpenContentStream(bool async,
                                          nsIInputStream** streamOut,
                                          nsIChannel** channel) {
  NS_ENSURE_TRUE(mCallbacks, NS_ERROR_UNEXPECTED);

  nsCOMPtr<nsIInputStream> stream =
      MOZ_TRY(mCallbacks->OpenContentStream(async, this));
  MOZ_ASSERT(stream);

  mCallbacks = nullptr;

  *channel = nullptr;
  stream.forget(streamOut);
  return NS_OK;
}

nsresult SimpleChannel::BeginAsyncRead(nsIStreamListener* listener,
                                       nsIRequest** request,
                                       nsICancelable** cancelableRequest) {
  NS_ENSURE_TRUE(mCallbacks, NS_ERROR_UNEXPECTED);

  RequestOrReason res = mCallbacks->StartAsyncRead(listener, this);

  if (res.isErr()) {
    return res.propagateErr();
  }

  mCallbacks = nullptr;

  RequestOrCancelable value = res.unwrap();

  if (value.is<NotNullRequest>()) {
    nsCOMPtr<nsIRequest> req = value.as<NotNullRequest>().get();
    req.forget(request);
  } else if (value.is<NotNullCancelable>()) {
    nsCOMPtr<nsICancelable> cancelable = value.as<NotNullCancelable>().get();
    cancelable.forget(cancelableRequest);
  } else {
    MOZ_ASSERT_UNREACHABLE(
        "StartAsyncRead didn't return a NotNull nsIRequest or nsICancelable.");
    return NS_ERROR_UNEXPECTED;
  }

  return NS_OK;
}

NS_IMETHODIMP
SimpleChannel::ConnectParent(uint32_t aId) {
  if (!IsNeckoChild()) {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  mozilla::dom::ContentChild* cc =
      mozilla::ipc::ActorCast<mozilla::dom::ContentChild>(
          gNeckoChild->Manager());
  if (cc->IsShuttingDown()) {
    return NS_ERROR_FAILURE;
  }

  gNeckoChild->SendConnectBaseChannel(aId);
  return NS_OK;
}

NS_IMETHODIMP
SimpleChannel::CompleteRedirectSetup(nsIStreamListener* aListener) {
  return AsyncOpen(aListener);
}

already_AddRefed<nsIChannel> NS_NewSimpleChannelInternal(
    nsIURI* aURI, nsILoadInfo* aLoadInfo,
    UniquePtr<SimpleChannelCallbacks>&& aCallbacks) {
  RefPtr<SimpleChannel> chan = new SimpleChannel(std::move(aCallbacks));

  chan->SetURI(aURI);

  MOZ_ALWAYS_SUCCEEDS(chan->SetLoadInfo(aLoadInfo));

  return chan.forget();
}

}  // namespace net
}  // namespace mozilla
