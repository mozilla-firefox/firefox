/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_Permission_h
#define mozilla_Permission_h

#include "nsIPermission.h"
#include "nsCOMPtr.h"
#include "nsString.h"

namespace mozilla {

////////////////////////////////////////////////////////////////////////////////

class Permission : public nsIPermission {
 public:
  // nsISupports
  NS_DECL_ISUPPORTS
  NS_DECL_NSIPERMISSION

  static already_AddRefed<Permission> Create(
      nsIPrincipal* aPrincipal, const nsACString& aType, uint32_t aCapability,
      uint32_t aExpireType, int64_t aExpireTime, int64_t aModificationTime,
      uint64_t aBrowserId = 0);

  // This method creates a new nsIPrincipal with a stripped OriginAttributes (no
  // userContextId) and a content principal equal to the origin of 'aPrincipal'.
  // If aForceStripOA is true, the origin attributes are stripped regardless of
  // the permission isolation prefs.
  static already_AddRefed<nsIPrincipal> ClonePrincipalForPermission(
      nsIPrincipal* aPrincipal, bool aForceStripOA = false);

 protected:
  Permission(nsIPrincipal* aPrincipal, const nsACString& aType,
             uint32_t aCapability, uint32_t aExpireType, int64_t aExpireTime,
             int64_t aModificationTime, uint64_t aBrowserId = 0);

  virtual ~Permission() = default;

  nsCOMPtr<nsIPrincipal> mPrincipal;
  nsCString mType;
  uint32_t mCapability;
  uint32_t mExpireType;
  int64_t mExpireTime;
  int64_t mModificationTime;
  uint64_t mBrowserId;
};

}  // namespace mozilla

#endif  // mozilla_Permission_h
