/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsNodeSupportsWeakRefTearoff_h_
#define nsNodeSupportsWeakRefTearoff_h_

#include "nsCOMPtr.h"
#include "nsCycleCollectionParticipant.h"
#include "nsIWeakReference.h"

class nsINode;

/**
 * Tearoff to use for nodes to implement nsISupportsWeakReference
 */
class nsNodeSupportsWeakRefTearoff final : public nsISupportsWeakReference {
 public:
  explicit nsNodeSupportsWeakRefTearoff(nsINode* aNode);

  // nsISupports
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL

  // nsISupportsWeakReference
  NS_DECL_NSISUPPORTSWEAKREFERENCE

  NS_DECL_CYCLE_COLLECTION_CLASS(nsNodeSupportsWeakRefTearoff)

 private:
  ~nsNodeSupportsWeakRefTearoff();

  nsCOMPtr<nsINode> mNode;
};

#endif  // nsNodeSupportsWeakRefTearoff_h_
