/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_extensions_WebExtensionPolicy_h
#define mozilla_extensions_WebExtensionPolicy_h

#include "MainThreadUtils.h"
#include "mozilla/RWLock.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/BrowsingContextGroup.h"
#include "mozilla/dom/Nullable.h"
#include "mozilla/dom/WebExtensionPolicyBinding.h"
#include "mozilla/dom/WindowProxyHolder.h"
#include "mozilla/extensions/MatchPattern.h"

#include "jspubtd.h"

#include "mozilla/Result.h"
#include "nsCOMPtr.h"
#include "nsCycleCollectionParticipant.h"
#include "nsISupports.h"
#include "nsWrapperCache.h"

namespace mozilla {
namespace dom {
class Promise;
}  // namespace dom

namespace extensions {

using dom::WebAccessibleResourceInit;
using dom::WebExtensionInit;
using dom::WebExtensionLocalizeCallback;

class DocInfo;
class WebExtensionContentScript;

class WebAccessibleResource final {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(WebAccessibleResource)

  WebAccessibleResource(dom::GlobalObject& aGlobal,
                        const WebAccessibleResourceInit& aInit,
                        ErrorResult& aRv);

  bool IsWebAccessiblePath(const nsACString& aPath) const {
    return mWebAccessiblePaths.Matches(aPath);
  }

  bool SourceMayAccessPath(const URLInfo& aURI, const nsACString& aPath) {
    return mWebAccessiblePaths.Matches(aPath) &&
           (IsHostMatch(aURI) || IsExtensionMatch(aURI));
  }

  bool IsHostMatch(const URLInfo& aURI);

  bool IsExtensionMatch(const URLInfo& aURI);

 private:
  ~WebAccessibleResource() = default;

  MatchGlobSet mWebAccessiblePaths;
  RefPtr<MatchPatternSetCore> mMatches;
  RefPtr<AtomSet> mExtensionIDs;
};

/// The thread-safe component of the WebExtensionPolicy.
///
/// Acts as a weak reference to the base WebExtensionPolicy.
class WebExtensionPolicyCore final {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(WebExtensionPolicyCore)

  nsAtom* Id() const { return mId; }

  const nsCString& MozExtensionHostname() const { return mHostname; }

  nsIURI* BaseURI() const { return mBaseURI; }

  bool IsPrivileged() const { return mIsPrivileged; }

  bool TemporarilyInstalled() { return mTemporarilyInstalled; }

  const nsString& Name() const { return mName; }

  const nsString& Version() const { return mVersion; }

  nsAtom* Type() const { return mType; }

  uint32_t ManifestVersion() const { return mManifestVersion; }

  const nsString& ExtensionPageCSP() const { return mExtensionPageCSP; }

  const nsString& BaseCSP() const { return mBaseCSP; }

  const nsString& SandboxPageCSP() const { return mSandboxPageCSP; }

  const nsString& BackgroundWorkerScript() const {
    return mBackgroundWorkerScript;
  }

  bool IsWebAccessiblePath(const nsACString& aPath) const {
    for (const auto& resource : mWebAccessibleResources) {
      if (resource->IsWebAccessiblePath(aPath)) {
        return true;
      }
    }
    return false;
  }

  bool IsSandboxPage(nsIURI* aURL) const;

  bool SourceMayAccessPath(const URLInfo& aURI, const nsACString& aPath) const;

  bool HasPermission(const nsAtom* aPermission) const {
    AutoReadLock lock(mLock);
    return mPermissions->Contains(aPermission);
  }

  void GetPermissions(nsTArray<nsString>& aResult) const MOZ_EXCLUDES(mLock) {
    AutoReadLock lock(mLock);
    return mPermissions->Get(aResult);
  }

  void SetPermissions(const nsTArray<nsString>& aPermissions)
      MOZ_EXCLUDES(mLock) {
    RefPtr<AtomSet> newPermissions = new AtomSet(aPermissions);
    AutoWriteLock lock(mLock);
    mPermissions = std::move(newPermissions);
  }

  bool FileSchemeAllowed() const;

  bool CanAccessURI(const URLInfo& aURI, bool aExplicit = false,
                    bool aCheckRestricted = true,
                    bool aAllowFilePermission = false) const;

  bool IgnoreQuarantine() const MOZ_EXCLUDES(mLock) {
    AutoReadLock lock(mLock);
    return mIgnoreQuarantine;
  }
  void SetIgnoreQuarantine(bool aIgnore) MOZ_EXCLUDES(mLock) {
    AutoWriteLock lock(mLock);
    mIgnoreQuarantine = aIgnore;
  }

  bool QuarantinedFromDoc(const DocInfo& aDoc) const;
  bool QuarantinedFromURI(const URLInfo& aURI) const MOZ_EXCLUDES(mLock);

  Maybe<dom::ExtensionGuardSource> CheckGuarded(const URLInfo& aURI) const
      MOZ_EXCLUDES(mLock);

  bool HasRecommendedState() const MOZ_EXCLUDES(mLock) {
    AutoReadLock lock(mLock);
    return mHasRecommendedState;
  }

  void SetHasRecommendedState(bool aHasRecommendedState) MOZ_EXCLUDES(mLock) {
    AutoWriteLock lock(mLock);
    mHasRecommendedState = aHasRecommendedState;
  }

  bool PrivateBrowsingAllowed() const;

  // Try to get a reference to the cycle-collected main-thread-only
  // WebExtensionPolicy instance.
  //
  // Will return nullptr if the policy has already been unlinked or destroyed.
  WebExtensionPolicy* GetMainThreadPolicy() const
      MOZ_REQUIRES(sMainThreadCapability) {
    return mPolicy;
  }

 private:
  friend class WebExtensionPolicy;

  WebExtensionPolicyCore(dom::GlobalObject& aGlobal,
                         WebExtensionPolicy* aPolicy,
                         const WebExtensionInit& aInit, ErrorResult& aRv);

  ~WebExtensionPolicyCore() = default;

  void ClearPolicyWeakRef() MOZ_REQUIRES(sMainThreadCapability) {
    mPolicy = nullptr;
  }

  // Unless otherwise guarded by a capability, all members on
  // WebExtensionPolicyCore should be immutable and threadsafe.

  WebExtensionPolicy* MOZ_NON_OWNING_REF mPolicy
      MOZ_GUARDED_BY(sMainThreadCapability);

  const RefPtr<nsAtom> mId;
  /* const */ nsCString mHostname;
  /* const */ nsCOMPtr<nsIURI> mBaseURI;

  const nsString mName;
  const nsString mVersion;
  const RefPtr<nsAtom> mType;
  const uint32_t mManifestVersion;
  /* const */ nsString mExtensionPageCSP;
  /* const */ nsString mBaseCSP;

  /* const */ nsString mSandboxPageCSP;
  /* const */ dom::Nullable<MatchGlobSet> mSandboxPages;

  const bool mIsPrivileged;
  // Whether about:newtab and about:home may load this extension's resources,
  // computed on the main thread because ExtensionPolicyService is main-thread
  // only.
  const bool mAboutNewTabMayAccess;
  const bool mTemporarilyInstalled;

  const nsString mBackgroundWorkerScript;

  /* const */ nsTArray<RefPtr<WebAccessibleResource>> mWebAccessibleResources;

  mutable RWLock mLock{"WebExtensionPolicyCore"};

  bool mIgnoreQuarantine MOZ_GUARDED_BY(mLock);
  bool mHasRecommendedState MOZ_GUARDED_BY(mLock);
  RefPtr<AtomSet> mPermissions MOZ_GUARDED_BY(mLock);
  RefPtr<MatchPatternSetCore> mHostPermissions MOZ_GUARDED_BY(mLock);
  nsTArray<RefPtr<ExtensionGuardSetCore>> mGuardSets MOZ_GUARDED_BY(mLock);
};

class WebExtensionPolicy final : public nsISupports, public nsWrapperCache {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(WebExtensionPolicy)

  using ScriptArray = nsTArray<RefPtr<WebExtensionContentScript>>;

  static already_AddRefed<WebExtensionPolicy> Constructor(
      dom::GlobalObject& aGlobal, const WebExtensionInit& aInit,
      ErrorResult& aRv);

  WebExtensionPolicyCore* Core() const { return mCore; }

  nsAtom* Id() const { return mCore->Id(); }
  void GetId(nsAString& aId) const { aId = nsDependentAtomString(Id()); };

  const nsCString& MozExtensionHostname() const {
    return mCore->MozExtensionHostname();
  }
  void GetMozExtensionHostname(nsACString& aHostname) const {
    aHostname = MozExtensionHostname();
  }

  nsIURI* BaseURI() const { return mCore->BaseURI(); }
  void GetBaseURL(nsACString& aBaseURL) const {
    MOZ_ALWAYS_SUCCEEDS(mCore->BaseURI()->GetSpec(aBaseURL));
  }

  bool IsPrivileged() { return mCore->IsPrivileged(); }

  bool TemporarilyInstalled() { return mCore->TemporarilyInstalled(); }

  void GetURL(const nsAString& aPath, nsAString& aURL, ErrorResult& aRv) const;

  Result<nsString, nsresult> GetURL(const nsAString& aPath) const;

  void RegisterContentScript(WebExtensionContentScript& script,
                             ErrorResult& aRv);

  void UnregisterContentScript(const WebExtensionContentScript& script,
                               ErrorResult& aRv);

  void InjectContentScripts(ErrorResult& aRv);

  bool CanAccessURI(const URLInfo& aURI, bool aExplicit = false,
                    bool aCheckRestricted = true,
                    bool aAllowFilePermission = false) const {
    return mCore->CanAccessURI(aURI, aExplicit, aCheckRestricted,
                               aAllowFilePermission);
  }

  bool IsWebAccessiblePath(const nsACString& aPath) const {
    return mCore->IsWebAccessiblePath(aPath);
  }

  bool SourceMayAccessPath(const URLInfo& aURI, const nsACString& aPath) const {
    return mCore->SourceMayAccessPath(aURI, aPath);
  }

  bool HasPermission(const nsAtom* aPermission) const {
    return mCore->HasPermission(aPermission);
  }
  bool HasPermission(const nsAString& aPermission) const {
    RefPtr<nsAtom> atom = NS_AtomizeMainThread(aPermission);
    return HasPermission(atom);
  }

  static bool IsRestrictedDoc(const DocInfo& aDoc);
  static bool IsRestrictedURI(const URLInfo& aURI);

  static bool IsQuarantinedDoc(const DocInfo& aDoc);
  static bool IsQuarantinedURI(const URLInfo& aURI);

  bool QuarantinedFromDoc(const DocInfo& aDoc) const {
    return mCore->QuarantinedFromDoc(aDoc);
  }

  bool QuarantinedFromURI(const URLInfo& aURI) const {
    return mCore->QuarantinedFromURI(aURI);
  }

  nsCString BackgroundPageHTML() const;

  MOZ_CAN_RUN_SCRIPT
  void Localize(const nsAString& aInput, nsString& aResult) const;

  const nsString& Name() const { return mCore->Name(); }
  void GetName(nsAString& aName) const { aName = Name(); }

  const nsString& Version() const { return mCore->Version(); }
  void GetVersion(nsAString& aVersion) const { aVersion = Version(); }

  nsAtom* Type() const { return mCore->Type(); }
  void GetType(nsAString& aType) const {
    aType = nsDependentAtomString(Type());
  };

  uint32_t ManifestVersion() const { return mCore->ManifestVersion(); }

  const nsString& ExtensionPageCSP() const { return mCore->ExtensionPageCSP(); }
  void GetExtensionPageCSP(nsAString& aCSP) const { aCSP = ExtensionPageCSP(); }

  const nsString& BaseCSP() const { return mCore->BaseCSP(); }
  void GetBaseCSP(nsAString& aCSP) const { aCSP = BaseCSP(); }

  const nsString& SandboxPageCSP() const { return mCore->SandboxPageCSP(); }
  void GetSandboxPageCSP(nsAString& aCSP) const { aCSP = SandboxPageCSP(); }

  already_AddRefed<MatchPatternSet> AllowedOrigins() {
    return do_AddRef(mHostPermissions);
  }
  void SetAllowedOrigins(MatchPatternSet& aAllowedOrigins);

  void GetGuardSets(nsTArray<RefPtr<ExtensionGuardSet>>& aResult) const;
  void SetGuardSets(const nsTArray<OwningNonNull<ExtensionGuardSet>>& aLists);

  dom::Nullable<dom::ExtensionGuardSource> CheckGuarded(
      const URLInfo& aURI) const {
    const auto result = mCore->CheckGuarded(aURI);
    if (result.isSome()) {
      return dom::Nullable<dom::ExtensionGuardSource>(result.ref());
    }
    return {};
  }

  void GetPermissions(nsTArray<nsString>& aResult) const {
    mCore->GetPermissions(aResult);
  }
  void SetPermissions(const nsTArray<nsString>& aPermissions) {
    mCore->SetPermissions(aPermissions);
  }

  bool FileSchemeAllowed() const { return mCore->FileSchemeAllowed(); }

  bool IgnoreQuarantine() const { return mCore->IgnoreQuarantine(); }
  void SetIgnoreQuarantine(bool aIgnore);

  bool HasRecommendedState() const { return mCore->HasRecommendedState(); }
  void SetHasRecommendedState(bool aHasRecommendedState);

  void GetContentScripts(ScriptArray& aScripts) const;
  const ScriptArray& ContentScripts() const { return mContentScripts; }

  bool Active() const { return mActive; }
  void SetActive(bool aActive, ErrorResult& aRv);

  bool PrivateBrowsingAllowed() const {
    return mCore->PrivateBrowsingAllowed();
  }

  bool CanAccessContext(nsILoadContext* aContext) const;

  bool CanAccessWindow(const dom::WindowProxyHolder& aWindow) const;

  void GetReadyPromise(JSContext* aCx,
                       JS::MutableHandle<JSObject*> aResult) const;
  dom::Promise* ReadyPromise() const { return mReadyPromise; }

  const nsString& BackgroundWorkerScript() const {
    return mCore->BackgroundWorkerScript();
  }
  void GetBackgroundWorker(nsString& aScriptURL) const {
    aScriptURL.Assign(BackgroundWorkerScript());
  }

  bool IsManifestBackgroundWorker(const nsAString& aWorkerScriptURL) const {
    return BackgroundWorkerScript().Equals(aWorkerScriptURL);
  }

  uint64_t GetBrowsingContextGroupId() const;
  uint64_t GetBrowsingContextGroupId(ErrorResult& aRv);

  static void GetActiveExtensions(
      dom::GlobalObject& aGlobal,
      nsTArray<RefPtr<WebExtensionPolicy>>& aResults);

  static already_AddRefed<WebExtensionPolicy> GetByID(
      dom::GlobalObject& aGlobal, const nsAString& aID);

  static already_AddRefed<WebExtensionPolicy> GetByHostname(
      dom::GlobalObject& aGlobal, const nsACString& aHostname);

  static already_AddRefed<WebExtensionPolicy> GetByURI(
      dom::GlobalObject& aGlobal, nsIURI* aURI);

  static bool IsRestrictedURI(dom::GlobalObject& aGlobal, const URLInfo& aURI) {
    return IsRestrictedURI(aURI);
  }

  static bool IsQuarantinedURI(dom::GlobalObject& aGlobal,
                               const URLInfo& aURI) {
    return IsQuarantinedURI(aURI);
  }

  bool QuarantinedFromURI(dom::GlobalObject& aGlobal,
                          const URLInfo& aURI) const {
    return QuarantinedFromURI(aURI);
  }

  static bool UseRemoteWebExtensions(dom::GlobalObject& aGlobal);
  static bool IsExtensionProcess(dom::GlobalObject& aGlobal);
  static bool BackgroundServiceWorkerEnabled(dom::GlobalObject& aGlobal);
  static bool QuarantinedDomainsEnabled(dom::GlobalObject& aGlobal);

  nsISupports* GetParentObject() const { return mParent; }

  virtual JSObject* WrapObject(JSContext* aCx,
                               JS::Handle<JSObject*> aGivenProto) override;

 protected:
  ~WebExtensionPolicy();

 private:
  WebExtensionPolicy(dom::GlobalObject& aGlobal, const WebExtensionInit& aInit,
                     ErrorResult& aRv);

  bool Enable();
  bool Disable();

  nsCOMPtr<nsISupports> mParent;

  RefPtr<WebExtensionPolicyCore> mCore;

  dom::BrowsingContextGroup::KeepAlivePtr mBrowsingContextGroup;

  bool mActive = false;

  RefPtr<WebExtensionLocalizeCallback> mLocalizeCallback;

  // NOTE: This is a mirror of the object in `mCore`, except with the
  // non-threadsafe wrapper.
  RefPtr<MatchPatternSet> mHostPermissions;

  // NOTE: mirrors mCore->mGuardSets with non-threadsafe wrappers.
  nsTArray<RefPtr<ExtensionGuardSet>> mGuardSets;

  dom::Nullable<nsTArray<nsString>> mBackgroundScripts;

  bool mBackgroundTypeModule = false;

  nsTArray<RefPtr<WebExtensionContentScript>> mContentScripts;

  RefPtr<dom::Promise> mReadyPromise;
};

}  // namespace extensions
}  // namespace mozilla

#endif  // mozilla_extensions_WebExtensionPolicy_h
