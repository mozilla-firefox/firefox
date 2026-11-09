/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef DummyImeiProvider_h
#define DummyImeiProvider_h

#include "nsIImeiProvider.h"

namespace mozilla::dom {

class DummyImeiProvider final : public nsIImeiProvider {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIIMEIPROVIDER

  DummyImeiProvider() = default;
  static already_AddRefed<DummyImeiProvider> GetInstance();

 private:
  ~DummyImeiProvider() = default;
};

}  // namespace mozilla::dom

#endif /* DummyImeiProvider_h */
