/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsCCUncollectableMarker_h_
#define nsCCUncollectableMarker_h_

#include "js/TracingAPI.h"
#include "mozilla/Attributes.h"
#include "nsIObserver.h"

class nsCCUncollectableMarker final : public nsIObserver {
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  /**
   * Inits a global nsCCUncollectableMarker. Should only be called once.
   */
  static nsresult Init();

  /**
   * Checks if we're collecting during a given generation
   */
  static bool InGeneration(uint32_t aGeneration) {
    return aGeneration && aGeneration == sGeneration;
  }

  template <class CCCallback>
  static bool InGeneration(CCCallback& aCb, uint32_t aGeneration) {
    return InGeneration(aGeneration) && !aCb.WantAllTraces();
  }

  static void CleanupForForgetSkippable();

  static uint32_t sGeneration;

 private:
  nsCCUncollectableMarker() = default;
  ~nsCCUncollectableMarker() = default;

  nsresult Cleanup(bool aPrepareForCC);
};

namespace mozilla::dom {
void TraceBlackJS(JSTracer* aTrc);
}  // namespace mozilla::dom

#endif
