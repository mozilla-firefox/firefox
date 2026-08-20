/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef TOOLKIT_COMPONENTS_ML_IPC_HWINFERENCEMANAGERPARENT_H_
#define TOOLKIT_COMPONENTS_ML_IPC_HWINFERENCEMANAGERPARENT_H_

#include "mozilla/hwinference/PHWInferenceManagerParent.h"
#include "mozilla/dom/ipc/IdType.h"
#include "nsRefPtrHashtable.h"

namespace mozilla::hwinference {

class HWInferenceManagerParent final : public PHWInferenceManagerParent {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(HWInferenceManagerParent, override);

  static bool CreateForContent(Endpoint<PHWInferenceManagerParent>&& aEndpoint,
                               dom::ContentParentId aContentId);

  void ActorDestroy(ActorDestroyReason aReason) override;

  // GeckoChildID of the content process accessing this manager, for permission
  // checks. 0, the GeckoChildID the parent process has, when the parent
  // process created it.
  dom::ContentParentId ContentId() const { return mContentId; }

 private:
  explicit HWInferenceManagerParent(dom::ContentParentId aContentId);
  ~HWInferenceManagerParent() = default;

  const dom::ContentParentId mContentId;
};

}  // namespace mozilla::hwinference

#endif  // TOOLKIT_COMPONENTS_ML_IPC_HWINFERENCEMANAGERPARENT_H_
