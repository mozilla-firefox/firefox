/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef AndroidImeiProvider_h
#define AndroidImeiProvider_h

#include "nsIImeiProvider.h"

namespace mozilla::dom {

class AndroidImeiProvider final : public nsIImeiProvider {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIIMEIPROVIDER

  AndroidImeiProvider() = default;
  static already_AddRefed<AndroidImeiProvider> GetInstance();

 private:
  ~AndroidImeiProvider() = default;
};

}  // namespace mozilla::dom

#endif /* AndroidImeiProvider_h */
