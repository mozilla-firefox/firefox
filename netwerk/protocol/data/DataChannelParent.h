/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=4 sw=2 sts=2 et tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef NS_DATACHANNELPARENT_H
#define NS_DATACHANNELPARENT_H

#include "nsIParentChannel.h"
#include "nsISupportsImpl.h"

#include "mozilla/net/PDataChannelParent.h"

namespace mozilla {
namespace net {

// In order to support HTTP redirects to data:, we need to implement the HTTP
// redirection API, which requires a class that implements nsIParentChannel
// and which calls NS_LinkRedirectChannels.
class DataChannelParent : public nsIParentChannel, public PDataChannelParent {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIPARENTCHANNEL
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_NSISTREAMLISTENER

  [[nodiscard]] bool Init(const uint64_t& aChannelId);

 private:
  ~DataChannelParent() = default;

  virtual void ActorDestroy(ActorDestroyReason why) override;
};

}  // namespace net
}  // namespace mozilla

#endif /* NS_DATACHANNELPARENT_H */
