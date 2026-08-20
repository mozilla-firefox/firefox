/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsObserverService_h_
#define nsObserverService_h_

#include "nsIMemoryReporter.h"
#include "nsIObserverService.h"
#include "nsObserverList.h"
#include "nsTHashtable.h"

// {D07F5195-E3D1-11d2-8ACD-00105A1B8860}
#define NS_OBSERVERSERVICE_CID \
  {0xd07f5195, 0xe3d1, 0x11d2, {0x8a, 0xcd, 0x0, 0x10, 0x5a, 0x1b, 0x88, 0x60}}

class nsObserverService final : public nsIObserverService,
                                public nsIMemoryReporter {
 public:
  NS_INLINE_DECL_STATIC_IID(NS_OBSERVERSERVICE_CID)

  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVERSERVICE
  NS_DECL_NSIMEMORYREPORTER

  void Shutdown();

  [[nodiscard]] static nsresult Create(const nsIID& aIID, void** aInstancePtr);

  // Unmark any strongly held observers implemented in JS so the cycle
  // collector will not traverse them.
  NS_IMETHOD UnmarkGrayStrongObservers();

 private:
  nsObserverService() = default;
  ~nsObserverService();
  void RegisterReporter();
  nsresult EnsureValidCall() const;
  nsresult FilterHttpOnTopics(const char* aTopic);

  static const size_t kSuspectReferentCount = 100;
  nsTHashtable<nsObserverList> mObserverTopicTable;
  bool mShuttingDown = false;
};

#endif /* nsObserverService_h_ */
