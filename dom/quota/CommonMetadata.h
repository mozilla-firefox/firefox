/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_QUOTA_COMMONMETADATA_H_
#define DOM_QUOTA_COMMONMETADATA_H_

#include <utility>

#include "DirectoryMetadata.h"
#include "mozilla/dom/quota/Client.h"
#include "mozilla/dom/quota/ClientUsageArray.h"
#include "mozilla/dom/quota/Constants.h"
#include "mozilla/dom/quota/PersistenceType.h"
#include "nsString.h"

class mozIStorageStatement;

namespace mozilla::dom::quota {

bool CheckClientUsagesConsistency(const ClientUsageArray& aClientUsages,
                                  int64_t aUsage, const nsACString& aContext);

struct PrincipalMetadata {
  nsCString mSuffix;
  nsCString mGroup;
  nsCString mOrigin;
  nsCString mStorageOrigin;
  bool mIsPrivate = false;
  bool mIsEncrypted = false;

  // These explicit constructors exist to prevent accidental aggregate
  // initialization which could for example initialize mSuffix as group and
  // mGroup as origin (if only two string arguments are used).
  PrincipalMetadata() = default;

  PrincipalMetadata(nsCString aSuffix, nsCString aGroup, nsCString aOrigin,
                    nsCString aStorageOrigin, bool aIsPrivate,
                    bool aIsEncrypted = false)
      : mSuffix{std::move(aSuffix)},
        mGroup{std::move(aGroup)},
        mOrigin{std::move(aOrigin)},
        mStorageOrigin{std::move(aStorageOrigin)},
        mIsPrivate{aIsPrivate},
        mIsEncrypted{aIsEncrypted} {
    AssertInvariants();
  }

  bool ShouldEncrypt() const { return mIsPrivate || mIsEncrypted; }

  void AssertInvariants() const {
    MOZ_ASSERT(!StringBeginsWith(mOrigin, kUUIDOriginScheme));
    MOZ_ASSERT_IF(!mIsPrivate, mOrigin == mStorageOrigin);
    MOZ_ASSERT_IF(mIsPrivate, mOrigin != mStorageOrigin);
  }

  // Templated to restrict Equals() to exactly PrincipalMetadata. Prevents
  // derived types from accidentally inheriting Equals() and comparing only
  // base fields without their own fields.
  template <typename T,
            typename = std::enable_if_t<std::is_same_v<T, PrincipalMetadata>>>
  bool Equals(const T& aOther) const {
    return mSuffix == aOther.mSuffix && mGroup == aOther.mGroup &&
           mOrigin == aOther.mOrigin &&
           mStorageOrigin == aOther.mStorageOrigin &&
           mIsPrivate == aOther.mIsPrivate &&
           mIsEncrypted == aOther.mIsEncrypted;
  }
};

struct OriginMetadata : public PrincipalMetadata {
  PersistenceType mPersistenceType;

  OriginMetadata() = default;

  OriginMetadata(nsCString aSuffix, nsCString aGroup, nsCString aOrigin,
                 nsCString aStorageOrigin, bool aIsPrivate,
                 PersistenceType aPersistenceType, bool aIsEncrypted = false)
      : PrincipalMetadata(std::move(aSuffix), std::move(aGroup),
                          std::move(aOrigin), std::move(aStorageOrigin),
                          aIsPrivate, aIsEncrypted),
        mPersistenceType(aPersistenceType) {}

  OriginMetadata(PrincipalMetadata aPrincipalMetadata,
                 PersistenceType aPersistenceType)
      : PrincipalMetadata(std::move(aPrincipalMetadata)),
        mPersistenceType(aPersistenceType) {}

  // Templated to restrict Equals() to exactly OriginMetadata. Prevents
  // derived types from accidentally inheriting Equals() and comparing only
  // base fields without their own fields.
  template <typename T,
            typename = std::enable_if_t<std::is_same_v<T, OriginMetadata>>>
  bool Equals(const T& aOther) const {
    return static_cast<const PrincipalMetadata&>(*this).Equals(
               static_cast<const PrincipalMetadata&>(aOther)) &&
           mPersistenceType == aOther.mPersistenceType;
  }

  // Returns a composite string key in the form "<persistence>*<origin>".
  // Useful for flat hash maps keyed by both persistence type and origin,
  // as an alternative to using structured keys or nested maps.
  // Suitable when tree-based representation is unnecessary.
  nsCString GetCompositeKey() const {
    nsCString result;

    result.AppendInt(mPersistenceType);
    result.Append("*");
    result.Append(mOrigin);

    return result;
  }
};

struct OriginStateMetadata {
  int64_t mLastAccessTime;
  int32_t mLastMaintenanceDate;
  bool mAccessed;
  bool mPersisted;
  bool mDirty;

  OriginStateMetadata() = default;

  OriginStateMetadata(int64_t aLastAccessTime, int32_t aLastMaintenanceDate,
                      bool aAccessed, bool aPersisted, bool aDirty)
      : mLastAccessTime(aLastAccessTime),
        mLastMaintenanceDate(aLastMaintenanceDate),
        mAccessed(aAccessed),
        mPersisted(aPersisted),
        mDirty(aDirty) {}

  uint32_t ToMetadataFlags(bool aIsEncrypted) const {
    return DirectoryMetadataFlags::Initialized |
           (mAccessed ? DirectoryMetadataFlags::Accessed : 0u) |
           (mDirty ? DirectoryMetadataFlags::Dirty : 0u) |
           (mPersisted ? DirectoryMetadataFlags::Persisted : 0u) |
           (aIsEncrypted ? DirectoryMetadataFlags::Encrypted : 0u);
  }

  void FromMetadataFlags(uint32_t aFlags) {
    mAccessed = (aFlags & DirectoryMetadataFlags::Accessed) != 0;
    mPersisted = (aFlags & DirectoryMetadataFlags::Persisted) != 0;
    mDirty = (aFlags & DirectoryMetadataFlags::Dirty) != 0;
  }

  // Templated to restrict Equals() to exactly OriginStateMetadata. Prevents
  // derived types from accidentally inheriting Equals() and comparing only
  // base fields without their own fields.
  template <typename T,
            typename = std::enable_if_t<std::is_same_v<T, OriginStateMetadata>>>
  bool Equals(const T& aOther) const {
    return mLastAccessTime == aOther.mLastAccessTime &&
           mLastMaintenanceDate == aOther.mLastMaintenanceDate &&
           mAccessed == aOther.mAccessed && mPersisted == aOther.mPersisted &&
           mDirty == aOther.mDirty;
  }
};

struct FullOriginMetadata : OriginMetadata, OriginStateMetadata {
  ClientUsageArray mClientUsages;

  // Signed for the same reason as OriginInfo::mUsage, which this is normally
  // populated from: it can transiently (or due to a bug elsewhere) go
  // negative, and an unsigned type would turn that into a huge value that
  // corrupts every quota check that reads it (see bug 2066923, bug 1585978).
  int64_t mOriginUsage;
  uint32_t mQuotaVersion;

  FullOriginMetadata() = default;

  FullOriginMetadata(OriginMetadata aOriginMetadata,
                     OriginStateMetadata aOriginStateMetadata,
                     const ClientUsageArray& aClientUsages, int64_t aUsage,
                     uint32_t aQuotaVersion)
      : OriginMetadata(std::move(aOriginMetadata)),
        OriginStateMetadata(aOriginStateMetadata),
        mClientUsages(aClientUsages),
        mOriginUsage(aUsage),
        mQuotaVersion(aQuotaVersion) {}

  // Templated to restrict Equals() to exactly FullOriginMetadata. Prevents
  // derived types from accidentally inheriting Equals() and comparing only
  // base fields without their own fields.
  template <typename T,
            typename = std::enable_if_t<std::is_same_v<T, FullOriginMetadata>>>
  bool Equals(const T& aOther) const {
    return static_cast<const OriginMetadata&>(*this).Equals(
               static_cast<const OriginMetadata&>(aOther)) &&
           static_cast<const OriginStateMetadata&>(*this).Equals(
               static_cast<const OriginStateMetadata&>(aOther)) &&
           mClientUsages == aOther.mClientUsages &&
           mOriginUsage == aOther.mOriginUsage &&
           mQuotaVersion == aOther.mQuotaVersion;
  }

  // Compares all fields of this FullOriginMetadata instance with another,
  // except for the fields inherited from OriginStateMetadata.
  template <typename T,
            typename = std::enable_if_t<std::is_same_v<T, FullOriginMetadata>>>
  bool EqualsIgnoringOriginState(const T& aOther) const {
    return static_cast<const OriginMetadata&>(*this).Equals(
               static_cast<const OriginMetadata&>(aOther)) &&
           mClientUsages == aOther.mClientUsages &&
           mOriginUsage == aOther.mOriginUsage &&
           mQuotaVersion == aOther.mQuotaVersion;
  }

  // Compares all fields except mStorageOrigin, mIsPrivate, and mQuotaVersion.
  // Used when comparing scanned metadata against a DB row that omits these.
  template <typename T,
            typename = std::enable_if_t<std::is_same_v<T, FullOriginMetadata>>>
  bool EqualsIgnoringIntrinsicFields(const T& aOther) const {
    return mSuffix == aOther.mSuffix && mGroup == aOther.mGroup &&
           mOrigin == aOther.mOrigin &&
           mPersistenceType == aOther.mPersistenceType &&
           static_cast<const OriginStateMetadata&>(*this).Equals(
               static_cast<const OriginStateMetadata&>(aOther)) &&
           mClientUsages == aOther.mClientUsages &&
           mOriginUsage == aOther.mOriginUsage;
  }

  // The Encrypted bit lives next to the origin state flags on disk, but the
  // value belongs to PrincipalMetadata, so these hide the OriginStateMetadata
  // versions to keep the two in sync.
  uint32_t ToMetadataFlags() const {
    return OriginStateMetadata::ToMetadataFlags(mIsEncrypted);
  }

  void FromMetadataFlags(uint32_t aFlags) {
    OriginStateMetadata::FromMetadataFlags(aFlags);
    mIsEncrypted = (aFlags & DirectoryMetadataFlags::Encrypted) != 0;
  }

  // Convenient method for duplicating a FullOriginMetadata instance. Creates
  // a new object by copying both the OriginMetadata and OriginStateMetadata
  // parts of this instance.
  FullOriginMetadata Clone() const {
    return {static_cast<const OriginMetadata&>(*this),
            static_cast<const OriginStateMetadata&>(*this), mClientUsages,
            mOriginUsage, mQuotaVersion};
  }

  nsresult BindToStatement(mozIStorageStatement* aStatement) const;

  bool CheckIfUsageIsConsistent(const nsACString& aContext) const;
};

struct OriginUsageMetadata : FullOriginMetadata {
  uint64_t mUsage;

  OriginUsageMetadata() = default;

  OriginUsageMetadata(FullOriginMetadata aFullOriginMetadata, uint64_t aUsage)
      : FullOriginMetadata(std::move(aFullOriginMetadata)), mUsage(aUsage) {}
};

struct ClientMetadata : OriginMetadata {
  Client::Type mClientType;

  ClientMetadata() = default;

  ClientMetadata(OriginMetadata aOriginMetadata, Client::Type aClientType)
      : OriginMetadata(std::move(aOriginMetadata)), mClientType(aClientType) {}
};

}  // namespace mozilla::dom::quota

#endif  // DOM_QUOTA_COMMONMETADATA_H_
