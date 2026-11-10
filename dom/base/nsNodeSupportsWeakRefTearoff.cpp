/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsNodeSupportsWeakRefTearoff.h"

#include "nsINode.h"

nsNodeSupportsWeakRefTearoff::nsNodeSupportsWeakRefTearoff(nsINode* aNode)
    : mNode(aNode) {}
nsNodeSupportsWeakRefTearoff::~nsNodeSupportsWeakRefTearoff() = default;

NS_IMPL_CYCLE_COLLECTION(nsNodeSupportsWeakRefTearoff, mNode)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsNodeSupportsWeakRefTearoff)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
NS_INTERFACE_MAP_END_AGGREGATED(mNode)

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsNodeSupportsWeakRefTearoff)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsNodeSupportsWeakRefTearoff)

NS_IMETHODIMP
nsNodeSupportsWeakRefTearoff::GetWeakReference(
    nsIWeakReference** aInstancePtr) {
  nsINode::nsSlots* slots = mNode->Slots();
  if (!slots->mWeakReference) {
    slots->mWeakReference = new nsNodeWeakReference(mNode);
  }

  NS_ADDREF(*aInstancePtr = slots->mWeakReference);

  return NS_OK;
}
