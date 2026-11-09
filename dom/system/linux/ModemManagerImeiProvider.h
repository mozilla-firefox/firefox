/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef ModemManagerImeiProvider_h
#define ModemManagerImeiProvider_h

#include <gio/gio.h>
#include <glib.h>

#include "mozilla/GRefPtr.h"
#include "nsCOMPtr.h"
#include "nsIImeiProvider.h"
#include "nsString.h"

namespace mozilla::dom {

class ModemManagerImeiProvider final : public nsIImeiProvider {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIIMEIPROVIDER

  ModemManagerImeiProvider() : mCancellable(dont_AddRef(g_cancellable_new())) {}

  static already_AddRefed<ModemManagerImeiProvider> GetInstance();

 private:
  ~ModemManagerImeiProvider() = default;

  RefPtr<GCancellable> mCancellable;

  void CollectImei(RefPtr<Promise>);
  void OnIntrospectFinish(RefPtr<Promise>, RefPtr<GVariant>);
  void QueryModemImei(RefPtr<Promise>, const nsCString&);
};

}  // namespace mozilla::dom

#endif /* ModemManagerImeiProvider_h */
