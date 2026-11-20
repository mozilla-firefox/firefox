/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebTransportDatagramsWritable.h"

#include "mozilla/dom/WebTransport.h"
#include "mozilla/dom/WebTransportDatagramDuplexStream.h"
#include "mozilla/dom/WebTransportDatagramsWritableBinding.h"
#include "mozilla/dom/WebTransportLog.h"
#include "mozilla/dom/WebTransportSendGroup.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_INHERITED(WebTransportDatagramsWritable,
                                   WritableStream, mTransport, mSendGroup,
                                   mAlgorithms)
NS_IMPL_ADDREF_INHERITED(WebTransportDatagramsWritable, WritableStream)
NS_IMPL_RELEASE_INHERITED(WebTransportDatagramsWritable, WritableStream)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(WebTransportDatagramsWritable)
NS_INTERFACE_MAP_END_INHERITING(WritableStream)

WebTransportDatagramsWritable::WebTransportDatagramsWritable(
    nsIGlobalObject* aGlobal, WebTransport* aTransport,
    WebTransportSendGroup* aSendGroup, int64_t aSendOrder)
    : WritableStream(aGlobal,
                     WritableStream::HoldDropJSObjectsCaller::Explicit),
      mTransport(aTransport),
      mSendGroup(aSendGroup),
      mSendOrder(aSendOrder) {
  LOG(("WebTransportDatagramsWritable %p created", this));
  mozilla::HoldJSObjects(this);
}

WebTransportDatagramsWritable::~WebTransportDatagramsWritable() {
  LOG(("WebTransportDatagramsWritable %p destroyed", this));
  mozilla::DropJSObjects(this);
}

/* static */
already_AddRefed<WebTransportDatagramsWritable>
WebTransportDatagramsWritable::Create(
    JSContext* aCx, nsIGlobalObject* aGlobal, WebTransport* aTransport,
    WebTransportDatagramDuplexStream* aDatagrams,
    WebTransportSendGroup* aSendGroup, int64_t aSendOrder, ErrorResult& aRv) {
  // https://w3c.github.io/webtransport/#webtransportdatagramswritable-create
  // Step 1: Let stream be a new WebTransportDatagramsWritable with
  // [[OutgoingDatagramsQueue]] as an empty queue, [[Transport]] as transport,
  // [[SendGroup]] as sendGroup, and [[SendOrder]] as sendOrder.
  auto datagramsWritable = MakeRefPtr<WebTransportDatagramsWritable>(
      aGlobal, aTransport, aSendGroup, aSendOrder);

  // Step 2: Let writeDatagramsAlgorithm be an action that runs writeDatagrams
  // with transport and stream.
  // Step 3: Set up stream with writeAlgorithm set to writeDatagramsAlgorithm.
  // Create a new OutgoingDatagramStreamAlgorithms instance with the specified
  // sendGroup and sendOrder. This allows this writable to have its own
  // prioritization parameters.
  RefPtr<OutgoingDatagramStreamAlgorithms> algorithms =
      new OutgoingDatagramStreamAlgorithms(aDatagrams, aSendGroup, aSendOrder);

  // Copy the child from the default algorithms so this stream can send
  // datagrams
  if (aDatagrams->mOutgoingAlgorithms) {
    algorithms->SetChild(aDatagrams->mOutgoingAlgorithms->GetChild());
  }

  datagramsWritable->mAlgorithms = algorithms;
  algorithms->SetOwnerWritable(datagramsWritable.get());

  // Set up the WritableStream with our custom algorithms
  datagramsWritable->SetUpNative(aCx, *algorithms, Nothing(), nullptr, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  // Step 4: Return stream.
  return datagramsWritable.forget();
}

JSObject* WebTransportDatagramsWritable::WrapObject(
    JSContext* aCx, JS::Handle<JSObject*> aGivenProto) {
  return WebTransportDatagramsWritable_Binding::Wrap(aCx, this, aGivenProto);
}

void WebTransportDatagramsWritable::SetSendGroup(
    WebTransportSendGroup* aSendGroup, ErrorResult& aRv) {
  // https://w3c.github.io/webtransport/#send-stream-attributes (sendGroup
  // setter) Step 1: If value is non-null, and value.[[Transport]] is not
  //         this.[[Transport]], throw an "InvalidStateError" DOMException.
  if (aSendGroup && aSendGroup->GetTransport() != mTransport) {
    aRv.ThrowInvalidStateError("SendGroup belongs to different transport");
    return;
  }
  LOG(("WebTransportDatagramsWritable::SetSendGroup %p -> %p", mSendGroup.get(),
       aSendGroup));
  // Step 2: Set this.[[SendGroup]] to value.
  mSendGroup = aSendGroup;
  if (mAlgorithms) {
    mAlgorithms->SetSendGroup(aSendGroup);
  }
}

void WebTransportDatagramsWritable::SetSendOrder(int64_t aSendOrder) {
  LOG(("WebTransportDatagramsWritable::SetSendOrder %" PRId64, aSendOrder));
  mSendOrder = aSendOrder;
  if (mAlgorithms) {
    mAlgorithms->SetSendOrder(aSendOrder);
  }
}

void WebTransportDatagramsWritable::OnDatagramReady(uint64_t aDatagramId) {
  if (mAlgorithms) {
    mAlgorithms->OnDatagramReady(aDatagramId);
  }
}

bool WebTransportDatagramsWritable::IsConnected() const {
  return mAlgorithms && mAlgorithms->GetChild();
}

void WebTransportDatagramsWritable::SetChild(WebTransportChild* aChild) {
  if (mAlgorithms) {
    mAlgorithms->SetChild(aChild);
  }
}

void WebTransportDatagramsWritable::Cleanup() {
  if (mAlgorithms) {
    mAlgorithms->Cleanup();
  }
}

}  // namespace mozilla::dom
