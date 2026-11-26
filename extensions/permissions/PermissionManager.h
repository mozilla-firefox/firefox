/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_PermissionManager_h
#define mozilla_PermissionManager_h

#include "nsIPermissionManager.h"
#include "nsIAsyncShutdown.h"
#include "nsIObserver.h"
#include "nsIRemotePermissionService.h"
#include "nsWeakReference.h"
#include "nsCOMPtr.h"
#include "nsIURI.h"
#include "nsITimer.h"
#include "nsTHashMap.h"
#include "nsTHashSet.h"
#include "nsTHashtable.h"
#include "nsTArray.h"
#include "nsString.h"
#include "nsHashKeys.h"
#include "nsRefPtrHashtable.h"
#include "mozilla/Atomics.h"
#include "mozilla/Monitor.h"
#include "mozilla/MozPromise.h"
#include "mozilla/OriginAttributes.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/ThreadBound.h"
#include "mozilla/Variant.h"
#include "mozilla/Vector.h"

#include <utility>

class mozIStorageConnection;
class mozIStorageStatement;
class nsIInputStream;
class nsIPermission;
class nsIPrefBranch;

namespace IPC {
struct Permission;
}

namespace mozilla {
class OriginAttributesPattern;

namespace dom {
class ContentChild;
class ContentParent;
class WindowContext;
}  // namespace dom

////////////////////////////////////////////////////////////////////////////////

class PermissionManager final : public nsIPermissionManager,
                                public nsIObserver,
                                public nsSupportsWeakReference,
                                public nsIAsyncShutdownBlocker {
 public:
  class PermissionEntry {
   public:
    PermissionEntry(int64_t aID, uint32_t aType, uint32_t aPermission,
                    uint32_t aExpireType, int64_t aExpireTime,
                    int64_t aModificationTime)
        : mID(aID),
          mExpireTime(aExpireTime),
          mModificationTime(aModificationTime),
          mType(aType),
          mPermission(aPermission),
          mExpireType(aExpireType),
          mNonSessionPermission(aPermission),
          mNonSessionExpireType(aExpireType),
          mNonSessionExpireTime(aExpireTime) {}

    int64_t mID;
    int64_t mExpireTime;
    int64_t mModificationTime;
    uint32_t mType;
    uint32_t mPermission;
    uint32_t mExpireType;
    uint32_t mNonSessionPermission;
    uint32_t mNonSessionExpireType;
    uint32_t mNonSessionExpireTime;
  };

  /**
   * PermissionKey is the key used by PermissionHashKey hash table.
   */
  class PermissionKey {
   public:
    static already_AddRefed<PermissionKey> CreateFromPrincipal(
        nsIPrincipal* aPrincipal, bool aForceStripOA, bool aScopeToSite,
        nsresult& aResult);
    static already_AddRefed<PermissionKey> CreateFromURI(nsIURI* aURI,
                                                         nsresult& aResult);
    static already_AddRefed<PermissionKey> CreateFromURIAndOriginAttributes(
        nsIURI* aURI, const OriginAttributes* aOriginAttributes,
        bool aForceStripOA, nsresult& aResult);

    explicit PermissionKey(const nsACString& aOrigin)
        : mOrigin(aOrigin), mHashCode(HashString(aOrigin)) {}

    PermissionKey() = delete;

    bool operator==(const PermissionKey& aKey) const {
      return mOrigin.Equals(aKey.mOrigin);
    }

    PLDHashNumber GetHashCode() const { return mHashCode; }

    NS_INLINE_DECL_THREADSAFE_REFCOUNTING(PermissionKey)

    const nsCString mOrigin;
    const PLDHashNumber mHashCode;

   private:
    // Dtor shouldn't be used outside of the class.
    ~PermissionKey() = default;
  };

  class PermissionHashKey : public nsRefPtrHashKey<PermissionKey> {
   public:
    explicit PermissionHashKey(const PermissionKey* aPermissionKey)
        : nsRefPtrHashKey<PermissionKey>(aPermissionKey) {}

    PermissionHashKey(PermissionHashKey&& toCopy)
        : nsRefPtrHashKey<PermissionKey>(std::move(toCopy)),
          mPermissions(std::move(toCopy.mPermissions)) {}

    bool KeyEquals(const PermissionKey* aKey) const {
      return *aKey == *GetKey();
    }

    static PLDHashNumber HashKey(const PermissionKey* aKey) {
      return aKey->GetHashCode();
    }

    // Force the hashtable to use the copy constructor when shuffling entries
    // around, otherwise the Auto part of our AutoTArray won't be happy!
    enum { ALLOW_MEMMOVE = false };

    inline nsTArray<PermissionEntry>& GetPermissions() { return mPermissions; }
    inline const nsTArray<PermissionEntry>& GetPermissions() const {
      return mPermissions;
    }

    inline int32_t GetPermissionIndex(uint32_t aType) const {
      for (uint32_t i = 0; i < mPermissions.Length(); ++i) {
        if (mPermissions[i].mType == aType) {
          return i;
        }
      }

      return -1;
    }

    inline PermissionEntry GetPermission(uint32_t aType) const {
      for (uint32_t i = 0; i < mPermissions.Length(); ++i) {
        if (mPermissions[i].mType == aType) {
          return mPermissions[i];
        }
      }

      // unknown permission... return relevant data
      return PermissionEntry(-1, aType, nsIPermissionManager::UNKNOWN_ACTION,
                             nsIPermissionManager::EXPIRE_NEVER, 0, 0);
    }

   private:
    AutoTArray<PermissionEntry, 1> mPermissions;
  };

  // nsISupports
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIPERMISSIONMANAGER
  NS_DECL_NSIOBSERVER
  NS_DECL_NSIASYNCSHUTDOWNBLOCKER

  PermissionManager();
  static already_AddRefed<nsIPermissionManager> GetXPCOMSingleton();
  static already_AddRefed<PermissionManager> GetInstance();

  // Record a user interaction for permission expiry tracking.
  // Handles content/parent process IPC branching internally.
  static nsresult RecordSiteInteraction(dom::WindowContext* aWindowContext);

  // enums for AddInternal()
  enum OperationType {
    eOperationNone,
    eOperationAdding,
    eOperationRemoving,
    eOperationChanging,
    eOperationReplacingDefault
  };

  enum DBOperationType { eNoDBOperation, eWriteToDB };

  enum NotifyOperationType { eDontNotify, eNotify };

  // Similar to TestPermissionFromPrincipal, except that it is used only for
  // permissions which can never have default values.
  nsresult TestPermissionWithoutDefaultsFromPrincipal(nsIPrincipal* aPrincipal,
                                                      const nsACString& aType,
                                                      uint32_t* aPermission);

  nsresult RemovePermissionsWithAttributes(
      OriginAttributesPattern& aPattern,
      const nsTArray<nsCString>& aTypeInclusions = {},
      const nsTArray<nsCString>& aTypeExceptions = {}) MOZ_REQUIRES(mMonitor);

  /**
   * See `nsIPermissionManager::GetPermissionsWithKey` for more info on
   * permission keys.
   *
   * Get the permission key corresponding to the given Principal. This method is
   * intentionally infallible, as we want to provide an permission key to every
   * principal. Principals which don't have meaningful URIs with http://,
   * https://, or ftp:// schemes are given the default "" Permission Key.
   *
   * @param aPrincipal  The Principal which the key is to be extracted from.
   * @param aForceStripOA Whether to force stripping the principals origin
   *        attributes prior to generating the key.
   * @param aSiteScopePermissions  Whether to prepare the key for permissions
   *        scoped to the Principal's site, rather than origin. These are looked
   *        up independently. Scoping of a permission is fully determined by its
   *        type and determined by calls to the function IsSiteScopedPermission.
   * @param aKey  A string which will be filled with the permission
   * key.
   */
  static nsresult GetKeyForPrincipal(nsIPrincipal* aPrincipal,
                                     bool aForceStripOA,
                                     bool aSiteScopePermissions,
                                     nsACString& aKey);

  /**
   * See `nsIPermissionManager::GetPermissionsWithKey` for more info on
   * permission keys.
   *
   * Get the permission key corresponding to the given Origin. This method is
   * like GetKeyForPrincipal, except that it avoids creating a nsIPrincipal
   * object when you already have access to an origin string.
   *
   * If this method is passed a nonsensical origin string it may produce a
   * nonsensical permission key result.
   *
   * @param aOrigin  The origin which the key is to be extracted from.
   * @param aForceStripOA Whether to force stripping the origins attributes
   *        prior to generating the key.
   * @param aSiteScopePermissions  Whether to prepare the key for permissions
   *        scoped to the Principal's site, rather than origin. These are looked
   *        up independently. Scoping of a permission is fully determined by its
   *        type and determined by calls to the function IsSiteScopedPermission.
   * @param aKey  A string which will be filled with the permission
   * key.
   */
  static nsresult GetKeyForOrigin(const nsACString& aOrigin, bool aForceStripOA,
                                  bool aSiteScopePermissions, nsACString& aKey);

  /**
   * See `nsIPermissionManager::GetPermissionsWithKey` for more info on
   * permission keys.
   *
   * Get the permission key corresponding to the given Principal and type. This
   * method is intentionally infallible, as we want to provide an permission key
   * to every principal. Principals which don't have meaningful URIs with
   * http://, https://, or ftp:// schemes are given the default "" Permission
   * Key.
   *
   * This method is different from GetKeyForPrincipal in that it also takes
   * permissions which must be sent down before loading a document into account.
   *
   * @param aPrincipal  The Principal which the key is to be extracted from.
   * @param aType  The type of the permission to get the key for.
   * @param aPermissionKey  A string which will be filled with the permission
   * key.
   */
  static nsresult GetKeyForPermission(nsIPrincipal* aPrincipal,
                                      const nsACString& aType,
                                      nsACString& aKey);

  /**
   * See `nsIPermissionManager::GetPermissionsWithKey` for more info on
   * permission keys.
   *
   * Get all permissions keys which could correspond to the given principal.
   * This method, like GetKeyForPrincipal, is infallible and should always
   * produce at least one (key, origin) pair.
   *
   * Unlike GetKeyForPrincipal, this method also gets the keys for base domains
   * of the given principal. All keys returned by this method must be available
   * in the content process for a given URL to successfully have its permissions
   * checked in the `aExactHostMatch = false` situation.
   *
   * @param aPrincipal  The Principal which the key is to be extracted from.
   * @return returns an array of (key, origin) pairs.
   */
  static nsTArray<std::pair<nsCString, nsCString>> GetAllKeysForPrincipal(
      nsIPrincipal* aPrincipal);

  // From ContentChild.
  nsresult RemoveAllFromIPC();

  /**
   * Returns false if this permission manager wouldn't have the permission
   * requested available.
   *
   * If aType is empty, checks that the permission manager would have all
   * permissions available for the given principal.
   */
  bool PermissionAvailable(nsIPrincipal* aPrincipal, const nsACString& aType);

  /**
   * The content process doesn't have access to every permission. Instead, when
   * LOAD_DOCUMENT_URI channels for http://, https://, and ftp:// URIs are
   * opened, the permissions for those channels are sent down to the content
   * process before the OnStartRequest message. Permissions for principals with
   * other schemes are sent down at process startup.
   *
   * Permissions are keyed and grouped by "Permission Key"s.
   * `PermissionManager::GetKeyForPrincipal` provides the mechanism for
   * determining the permission key for a given principal.
   *
   * This method may only be called in the parent process. It fills the nsTArray
   * argument with the IPC::Permission objects which have a matching origin.
   *
   * @param origin The origin to use to find the permissions of interest.
   * @param key The key to use to find the permissions of interest. Only used
   *            when the origin argument is empty.
   * @param perms  An array which will be filled with the permissions which
   *               match the given origin.
   */
  bool GetPermissionsFromOriginOrKey(const nsACString& aOrigin,
                                     const nsACString& aKey,
                                     nsTArray<IPC::Permission>& aPerms);

  /**
   * See `PermissionManager::GetPermissionsWithKey` for more info on
   * Permission keys.
   *
   * `SetPermissionsWithKey` may only be called in the Child process, and
   * initializes the permission manager with the permissions for a given
   * Permission key. marking permissions with that key as available.
   *
   * @param permissionKey  The key for the permissions which have been sent
   * over.
   * @param perms  An array with the permissions which match the given key.
   */
  void SetPermissionsWithKey(const nsACString& aPermissionKey,
                             nsTArray<IPC::Permission>& aPerms);

  /**
   * Strip origin attributes for permissions, depending on permission isolation
   * pref state.
   * @param aForceStrip If true, strips user context and private browsing id,
   * ignoring permission isolation prefs.
   * @param aOriginAttributes object to strip.
   */
  static void MaybeStripOriginAttributes(bool aForceStrip,
                                         OriginAttributes& aOriginAttributes);

  /**
   * Whether permissions of type aType are never isolated by origin attributes.
   */
  static bool IsOAForceStripPermission(const nsACString& aType);

  nsresult Add(nsIPrincipal* aPrincipal, const nsACString& aType,
               uint32_t aPermission, int64_t aID, uint32_t aExpireType,
               int64_t aExpireTime, int64_t aModificationTime,
               NotifyOperationType aNotifyOperation,
               DBOperationType aDBOperation,
               const nsACString* aOriginString = nullptr,
               const bool aAllowPersistInPrivateBrowsing = false);

  struct BrowserPermissionEntry {
    uint32_t mPermission;
    int64_t mExpireTime;  // absolute timestamp (ms since epoch), 0 = no expiry
    nsCOMPtr<nsITimer> mTimer;
    uint32_t mTypeIndex;
    bool mSiteScoped;
  };

  using BrowserPermissionMap =
      nsTHashMap<nsCStringHashKey, BrowserPermissionEntry>;

 private:
  ~PermissionManager();
  nsresult Init();

  static StaticMutex sCreationMutex;
  // Holding our singleton instance until shutdown.
  static StaticRefPtr<PermissionManager> sInstanceHolder
      MOZ_GUARDED_BY(sCreationMutex);
  // Flag that signals shutdown has started.
  static bool sInstanceDead MOZ_GUARDED_BY(sCreationMutex);

  /**
   * Get all permissions for a given principal, which should not be isolated
   * by user context or private browsing. The principal has its origin
   * attributes stripped before perm db lookup. This is currently only affects
   * the "cookie" permission.
   * @param aPrincipal Used for creating the permission key.
   * @param aSiteScopePermissions Used to specify whether to get strip perms for
   * site scoped permissions (defined in IsSiteScopedPermission) or all other
   * permissions. Also used to create the permission key.
   */
  nsresult GetStripPermsForPrincipal(nsIPrincipal* aPrincipal,
                                     bool aSiteScopePermissions,
                                     nsTArray<PermissionEntry>& aResult)
      MOZ_REQUIRES(mMonitor);

  // Returns -1 on failure
  int32_t GetTypeIndex(const nsACString& aType, bool aAdd = false)
      MOZ_REQUIRES(mMonitor);

  // Returns whether the given combination of expire type and expire time are
  // expired. Note that EXPIRE_SESSION only honors expireTime if it is nonzero.
  bool HasExpired(uint32_t aExpireType, int64_t aExpireTime);

  // Appends the permissions associated with this principal to aResult.
  // If the onlySiteScopePermissions argument is true, the permissions searched
  // are those for the site of the principal and only the permissions that are
  // site-scoped are used.
  nsresult GetAllForPrincipalHelper(nsIPrincipal* aPrincipal,
                                    bool aSiteScopePermissions,
                                    nsTArray<RefPtr<nsIPermission>>& aResult)
      MOZ_REQUIRES(mMonitor);

  // Returns true if the principal can be used for getting / setting
  // permissions. If the principal can not be used an error code may be
  // returned.
  nsresult ShouldHandlePrincipalForPermission(
      nsIPrincipal* aPrincipal, bool& aIsPermissionPrincipalValid);

  // Returns PermissionHashKey for a given { host, isInBrowserElement } tuple.
  // This is not simply using PermissionKey because we will walk-up domains in
  // case of |host| contains sub-domains. Returns null if nothing found. Also
  // accepts host on the format "<foo>". This will perform an exact match lookup
  // as the string doesn't contain any dots.
  PermissionHashKey* GetPermissionHashKey(nsIPrincipal* aPrincipal,
                                          uint32_t aType, bool aExactHostMatch)
      MOZ_REQUIRES(mMonitor);

  nsresult RemoveFromPrincipalInternal(nsIPrincipal* aPrincipal,
                                       const nsACString& aType)
      MOZ_REQUIRES(mMonitor);

  // Returns PermissionHashKey for a given { host, isInBrowserElement } tuple.
  // This is not simply using PermissionKey because we will walk-up domains in
  // case of |host| contains sub-domains. Returns null if nothing found. Also
  // accepts host on the format "<foo>". This will perform an exact match lookup
  // as the string doesn't contain any dots.
  PermissionHashKey* GetPermissionHashKey(
      nsIURI* aURI, const OriginAttributes* aOriginAttributes, uint32_t aType,
      bool aExactHostMatch) MOZ_REQUIRES(mMonitor);

  // to be used by internal caller as a helper method; monitor lock must be
  // held.
  bool PermissionAvailableInternal(nsIPrincipal* aPrincipal,
                                   const nsACString& aType)
      MOZ_REQUIRES(mMonitor);

  // The int32_t is the type index, the nsresult is an early bail-out return
  // code.
  typedef Variant<int32_t, nsresult> TestPreparationResult;
  TestPreparationResult CommonPrepareToTestPermission(
      nsIPrincipal* aPrincipal, int32_t aTypeIndex, const nsACString& aType,
      uint32_t* aPermission, uint32_t aDefaultPermission,
      bool aDefaultPermissionIsValid, bool aExactHostMatch,
      bool aIncludingSession) MOZ_REQUIRES(mMonitor);

  // If aTypeIndex is passed -1, we try to inder the type index from aType.
  nsresult CommonTestPermission(nsIPrincipal* aPrincipal, int32_t aTypeIndex,
                                const nsACString& aType, uint32_t* aPermission,
                                uint32_t aDefaultPermission,
                                bool aDefaultPermissionIsValid,
                                bool aExactHostMatch, bool aIncludingSession)
      MOZ_REQUIRES(mMonitor);

  // If aTypeIndex is passed -1, we try to inder the type index from aType.
  nsresult CommonTestPermission(nsIURI* aURI, int32_t aTypeIndex,
                                const nsACString& aType, uint32_t* aPermission,
                                uint32_t aDefaultPermission,
                                bool aDefaultPermissionIsValid,
                                bool aExactHostMatch, bool aIncludingSession)
      MOZ_REQUIRES(mMonitor);

  nsresult CommonTestPermission(
      nsIURI* aURI, const OriginAttributes* aOriginAttributes,
      int32_t aTypeIndex, const nsACString& aType, uint32_t* aPermission,
      uint32_t aDefaultPermission, bool aDefaultPermissionIsValid,
      bool aExactHostMatch, bool aIncludingSession) MOZ_REQUIRES(mMonitor);

  // Only one of aPrincipal or aURI is allowed to be passed in.
  nsresult CommonTestPermissionInternal(
      nsIPrincipal* aPrincipal, nsIURI* aURI,
      const OriginAttributes* aOriginAttributes, int32_t aTypeIndex,
      const nsACString& aType, uint32_t* aPermission, bool aExactHostMatch,
      bool aIncludingSession) MOZ_REQUIRES(mMonitor);

  nsresult OpenDatabase(nsIFile* permissionsFile);

  void InitDB(bool aRemoveFile) MOZ_REQUIRES(mMonitor);
  nsresult TryInitDB(bool aRemoveFile, nsIInputStream* aDefaultsInputStream,
                     const MonitorAutoLock& aProofOfLock)
      MOZ_REQUIRES(mMonitor);

  void AddIdleDailyMaintenanceJob();
  void RemoveIdleDailyMaintenanceJob();
  void PerformIdleDailyMaintenance() MOZ_REQUIRES(mMonitor);

  nsresult ImportLatestDefaults(const nsTArray<uint32_t>& aUserContextIds)
      MOZ_REQUIRES(mMonitor);
  already_AddRefed<nsIInputStream> GetDefaultsInputStream();
  void ConsumeDefaultsInputStream(nsIInputStream* aInputStream,
                                  const MonitorAutoLock& aProofOfLock)
      MOZ_REQUIRES(mMonitor);

  nsresult CreateTable();
  void NotifyObserversWithPermission(
      nsIPrincipal* aPrincipal, const nsACString& aType, uint32_t aPermission,
      uint32_t aExpireType, int64_t aExpireTime, int64_t aModificationTime,
      const nsString& aData) MOZ_REQUIRES(mMonitor);

  void NotifyObservers(const nsCOMPtr<nsIPermission>& aPermission,
                       const nsString& aData) MOZ_REQUIRES(mMonitor);

  // Finalize all statements, close the DB and null it.
  enum CloseDBNextOp {
    eNone,
    eRebuldOnSuccess,
    eShutdown,
  };
  void CloseDB(CloseDBNextOp aNextOp) MOZ_REQUIRES(mMonitor);

  nsresult RemoveAllInternal(bool aNotifyObservers) MOZ_REQUIRES(mMonitor);
  nsresult RemoveAllFromMemory() MOZ_REQUIRES(mMonitor);

  void UpdateDB(OperationType aOp, int64_t aID, const nsACString& aOrigin,
                const nsACString& aType, uint32_t aPermission,
                uint32_t aExpireType, int64_t aExpireTime,
                int64_t aModificationTime) MOZ_REQUIRES(mMonitor);

  /**
   * This method removes all permissions modified after the specified time.
   */
  nsresult RemoveAllModifiedSince(int64_t aModificationTime)
      MOZ_REQUIRES(mMonitor);

  // Removes all permissions with a private browsing principal (i.e.
  // privateBrowsingId OA != 0).
  nsresult RemoveAllForPrivateBrowsing() MOZ_REQUIRES(mMonitor);

  // Helper function which removes all permissions for which aCondition
  // evaluates to true.
  nsresult RemovePermissionEntries(
      const std::function<bool(const PermissionEntry& aPermEntry,
                               const nsCOMPtr<nsIPrincipal>& aPrincipal)>&
          aCondition,
      bool aComputePrincipalForCondition = true) MOZ_REQUIRES(mMonitor);

  // Overload of RemovePermissionEntries allowing aCondition not to take
  // aPrincipal as an argument.
  nsresult RemovePermissionEntries(
      const std::function<bool(const PermissionEntry& aPermEntry)>& aCondition)
      MOZ_REQUIRES(mMonitor);

  // Helper function which returns all permissions for which aCondition
  // evaluates to true.
  nsresult GetPermissionEntries(
      const std::function<bool(const PermissionEntry& aPermEntry)>& aCondition,
      nsTArray<RefPtr<nsIPermission>>& aResult) MOZ_REQUIRES(mMonitor);

  // This method must be called before doing any operation to be sure that the
  // DB reading has been completed. This method is also in charge to complete
  // the migrations if needed.
  void EnsureReadCompleted() MOZ_REQUIRES(mMonitor);

  // If aStoreUnknownAction is set, an UNKNOWN_ACTION permission is stored even
  // if there is no permission to remove, so that it overrides a default
  // imported later.
  nsresult AddInternal(nsIPrincipal* aPrincipal, const nsACString& aType,
                       uint32_t aPermission, int64_t aID, uint32_t aExpireType,
                       int64_t aExpireTime, int64_t aModificationTime,
                       NotifyOperationType aNotifyOperation,
                       DBOperationType aDBOperation,
                       const nsACString* aOriginString = nullptr,
                       const bool aAllowPersistInPrivateBrowsing = false,
                       const bool aStoreUnknownAction = false)
      MOZ_REQUIRES(mMonitor);

  void MaybeAddReadEntryFromMigration(const nsACString& aOrigin,
                                      const nsCString& aType,
                                      uint32_t aPermission,
                                      uint32_t aExpireType, int64_t aExpireTime,
                                      int64_t aModificationTime, int64_t aId)
      MOZ_REQUIRES(mMonitor);

  nsCOMPtr<nsIAsyncShutdownClient> GetAsyncShutdownBarrier() const;

  void FinishAsyncShutdown();

  // Set of permission keys loaded within this content process.
  nsTHashSet<nsCString> mPermissionKeys MOZ_GUARDED_BY(mMonitor);

  nsCOMPtr<nsIFile> mPermissionsFile MOZ_GUARDED_BY(mMonitor);

  // This monitor is used to ensure the database reading before any other
  // operation. The reading of the database happens OMT. See |State| to know the
  // steps of the database reading.
  Monitor mMonitor MOZ_UNANNOTATED;

  enum State {
    // Initial state. The database has not been read yet.
    // |TryInitDB| is called at startup time to read the database OMT.
    // During the reading, |mReadEntries| will be populated with all the
    // existing permissions.
    eInitializing,

    // At the end of the database reading, we are in this state. A runnable is
    // executed to call |EnsureReadCompleted| on the main thread.
    // |EnsureReadCompleted| processes |mReadEntries| and goes to the next
    // state.
    eDBInitialized,

    // The permissions are fully read and any pending operation can proceed.
    eReady,

    // The permission manager has been terminated. No extra database operations
    // will be allowed.
    eClosed,
  };
  Atomic<State> mState;

  // Whether ContextualIdentityService has loaded its containers
  bool mUserContextsLoaded = false;

  // A single entry, from the database.
  struct ReadEntry {
    ReadEntry()
        : mId(0),
          mPermission(0),
          mExpireType(0),
          mExpireTime(0),
          mModificationTime(0),
          mFromMigration(false) {}

    nsCString mOrigin;
    nsCString mType;
    int64_t mId;
    uint32_t mPermission;
    uint32_t mExpireType;
    int64_t mExpireTime;
    int64_t mModificationTime;

    // true if this entry is the result of a migration.
    bool mFromMigration;
  };

  // List of entries read from the database. It will be populated OMT and
  // consumed on the main-thread.
  // This array is protected by the monitor.
  nsTArray<ReadEntry> mReadEntries MOZ_GUARDED_BY(mMonitor);

  // A single entry, from the database.
  struct MigrationEntry {
    MigrationEntry()
        : mId(0),
          mPermission(0),
          mExpireType(0),
          mExpireTime(0),
          mModificationTime(0) {}

    nsCString mHost;
    nsCString mType;
    int64_t mId;
    uint32_t mPermission;
    uint32_t mExpireType;
    int64_t mExpireTime;
    int64_t mModificationTime;
  };

  // List of entries read from the database. It will be populated OMT and
  // consumed on the main-thread. The migration entries will be converted to
  // ReadEntry in |CompleteMigrations|.
  // This array is protected by the monitor.
  nsTArray<MigrationEntry> mMigrationEntries MOZ_GUARDED_BY(mMonitor);

  // A single entry from the defaults URL.
  struct DefaultEntry {
    nsCString mOrigin;
    nsCString mType;
    uint32_t mPermission = 0;
  };

  // List of entries read from the default settings.
  // This array is protected by the monitor.
  nsTArray<DefaultEntry> mDefaultEntriesForImport MOZ_GUARDED_BY(mMonitor);
  // Adds a default permission entry to AddDefaultEntryForImport for given
  // origin, type and value
  void AddDefaultEntryForImport(const nsACString& aOrigin,
                                const nsCString& aType, uint32_t aPermission,
                                const MonitorAutoLock& aProofOfLock)
      MOZ_REQUIRES(mMonitor);
  // Given a default entry, import it as a default permission (id = -1) into the
  // permission manager without storing it to disk. If permission isolation for
  // private browsing is enabled (which is the default), and the permission type
  // is not exempt from it, this will also create a separate default permission
  // for private browsing. The same is done for every container in
  // aUserContextIds.
  nsresult ImportDefaultEntry(const DefaultEntry& aDefaultEntry,
                              const nsTArray<uint32_t>& aUserContextIds)
      MOZ_REQUIRES(mMonitor);
  // Imports a default entry for the given principal.
  nsresult ImportDefaultEntryForPrincipal(nsIPrincipal* aPrincipal,
                                          const DefaultEntry& aDefaultEntry)
      MOZ_REQUIRES(mMonitor);
  // Imports a default entry for aPrincipal with its origin attributes replaced
  // by aOriginAttributes.
  nsresult ImportDefaultEntryForOriginAttributes(
      nsIPrincipal* aPrincipal, const DefaultEntry& aDefaultEntry,
      const OriginAttributes& aOriginAttributes) MOZ_REQUIRES(mMonitor);
  // Imports a default entry into each of the given containers. Does nothing
  // if the permission type is exempt from it.
  nsresult ImportDefaultEntryForUserContexts(
      const DefaultEntry& aDefaultEntry,
      const nsTArray<uint32_t>& aUserContextIds) MOZ_REQUIRES(mMonitor);
  // Imports all default entries into each of the given containers.
  nsresult ImportDefaultsForUserContexts(
      const nsTArray<uint32_t>& aUserContextIds) MOZ_REQUIRES(mMonitor);
  // Removes the copies of the default entries and the removals of defaults
  // stored for a container which was deleted. Its other permissions are
  // removed by the clear data service.
  void RemoveDefaultsForUserContext(uint32_t aUserContextId)
      MOZ_REQUIRES(mMonitor);
  // The containers which exist at this point, as reported by
  // ContextualIdentityService. Empty as long as it hasn't loaded its data.
  // After observing "contextual-identity-service-load-finished" the contexts
  // are guaranteed to be loaded and this array will be filled. This must not
  // be called while holding mMonitor, as nsISiteContainerService may call back
  // into the permission manager.
  nsTArray<uint32_t> GetAllUserContextIds();

  // Calls GetAllUserContextIds with mMonitor temporarily released.
  nsTArray<uint32_t> GetUserContextIdsUnlocked() MOZ_REQUIRES(mMonitor);

  nsresult Read(const MonitorAutoLock& aProofOfLock) MOZ_REQUIRES(mMonitor);
  void CompleteRead() MOZ_REQUIRES(mMonitor);

  void CompleteMigrations() MOZ_REQUIRES(mMonitor);

  Atomic<bool> mMemoryOnlyDB;

  nsTHashtable<PermissionHashKey> mPermissionTable MOZ_GUARDED_BY(mMonitor);
  // a unique, monotonically increasing id used to identify each database entry
  Atomic<int64_t> mLargestID;

  nsCOMPtr<nsIPrefBranch> mDefaultPrefBranch MOZ_GUARDED_BY(mMonitor);

  // NOTE: Ensure this is the last member since it has a large inline buffer.
  // An array to store the strings identifying the different types.
  Vector<nsCString, 512> mTypeArray MOZ_GUARDED_BY(mMonitor);

  nsCOMPtr<nsIThread> mThread MOZ_GUARDED_BY(mMonitor);

  struct ThreadBoundData {
    nsCOMPtr<mozIStorageConnection> mDBConn;

    nsCOMPtr<mozIStorageStatement> mStmtInsert;
    nsCOMPtr<mozIStorageStatement> mStmtDelete;
    nsCOMPtr<mozIStorageStatement> mStmtUpdate;
    nsCOMPtr<mozIStorageStatement> mStmtInsertInteraction;
  };
  ThreadBound<ThreadBoundData> mThreadBoundData;

  void UpdateLastInteractionInternal(const nsACString& aOrigin)
      MOZ_REQUIRES(mMonitor);
  void ExpireUnusedPermissions();
  bool ShouldExpirePermission(const PermissionEntry& aEntry,
                              const nsTArray<nsCString>& aExpirableTypes) const
      MOZ_REQUIRES(mMonitor);
  RefPtr<GenericPromise> CleanupOrphanedInteractionRecords();

  // BrowserId -> permission map for browser-scoped (per-tab) permissions.
  nsTHashMap<nsUint64HashKey, UniquePtr<BrowserPermissionMap>>
      mBrowserPermissionTable;

  void NotifyBrowserObservers(const nsCOMPtr<nsIPermission>& aPermission,
                              const nsString& aData);

  void ForwardBrowserPermissionToChild(nsIPrincipal* aPrincipal,
                                       const nsACString& aType,
                                       uint32_t aAction, uint64_t aBrowserId,
                                       bool aIsRemoval);

  void ForwardClearBrowserPermissionsToChild(uint64_t aBrowserId,
                                             uint32_t aActionFilter);

 public:
  void TransmitBrowserPermissionsForPrincipal(
      dom::ContentParent* aContentParent, nsIPrincipal* aPrincipal,
      uint64_t aBrowserId);
  // Called from ContentChild IPC handlers. These are thin wrappers around
  // the internal methods, asserting we are in the content process.
  void SetBrowserPermissionFromIPC(nsIPrincipal* aPrincipal,
                                   const nsACString& aType, uint32_t aAction,
                                   uint64_t aBrowserId, bool aIsRemoval);
  void ClearBrowserPermissionsFromIPC(uint64_t aBrowserId,
                                      uint32_t aActionFilter);

 private:
  // Core browser permission operations. These work in any process and are
  // shared between the XPCOM (parent-only) and IPC (child-only) entry points.
  nsresult AddBrowserPermissionInternal(nsIPrincipal* aPrincipal,
                                        const nsACString& aType,
                                        uint32_t aPermission,
                                        uint64_t aBrowserId,
                                        int64_t aExpireTimeMS);
  void RemoveBrowserPermissionInternal(nsIPrincipal* aPrincipal,
                                       const nsACString& aType,
                                       uint64_t aBrowserId);
  // Returns true if any entries were removed.
  bool ClearBrowserPermissionsInternal(uint64_t aBrowserId,
                                       uint32_t aActionFilter);

  nsCString BrowserCompositeKey(nsIPrincipal* aPrincipal,
                                const nsACString& aType, bool aSiteScoped);

  nsCOMPtr<nsITimer> ScheduleBrowserPermissionExpiry(
      uint64_t aBrowserId, const nsACString& aCompositeKey,
      nsIPrincipal* aPrincipal, const nsACString& aType, uint32_t aPermission,
      int64_t aExpireMS);
};

// {4F6B5E00-0C36-11d5-A535-0010A401EB10}
#define NS_PERMISSIONMANAGER_CID \
  {0x4f6b5e00, 0xc36, 0x11d5, {0xa5, 0x35, 0x0, 0x10, 0xa4, 0x1, 0xeb, 0x10}}

}  // namespace mozilla

#endif  // mozilla_PermissionManager_h
