/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_extensions_MatchPattern_h
#define mozilla_extensions_MatchPattern_h

#include <utility>

#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/MatchPatternBinding.h"
#include "mozilla/extensions/MatchGlob.h"

#include "jspubtd.h"

#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Maybe.h"
#include "mozilla/Span.h"
#include "nsCOMPtr.h"
#include "nsCycleCollectionParticipant.h"
#include "nsTArray.h"
#include "nsAtom.h"
#include "nsICookie.h"
#include "nsISupports.h"
#include "nsIURI.h"
#include "nsWrapperCache.h"

namespace mozilla {
namespace extensions {

using dom::MatchPatternOptions;

// A sorted, immutable, binary-search-backed set of atoms, optimized for
// frequent lookups.
class AtomSet final {
 public:
  using ArrayType = AutoTArray<RefPtr<nsAtom>, 1>;

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(AtomSet)

  explicit AtomSet(const nsTArray<nsString>& aElems);

  MOZ_IMPLICIT AtomSet(std::initializer_list<nsAtom*> aIL);

  explicit AtomSet(Span<nsStaticAtom* const> aElems);

  bool Contains(const nsAString& elem) const {
    RefPtr<nsAtom> atom = NS_Atomize(elem);
    return Contains(atom);
  }

  bool Contains(const nsACString& aElem) const {
    RefPtr<nsAtom> atom = NS_Atomize(aElem);
    return Contains(atom);
  }

  bool Contains(const nsAtom* aAtom) const {
    return mElems.ContainsSorted(aAtom);
  }

  bool Intersects(const AtomSet& aOther) const;

  void Get(nsTArray<nsString>& aResult) const {
    aResult.SetCapacity(mElems.Length());

    for (const auto& atom : mElems) {
      aResult.AppendElement(nsDependentAtomString(atom));
    }
  }

  auto begin() const -> decltype(std::declval<const ArrayType>().begin()) {
    return mElems.begin();
  }

  auto end() const -> decltype(std::declval<const ArrayType>().end()) {
    return mElems.end();
  }

 private:
  ~AtomSet() = default;

  const ArrayType mElems;
};

// A helper class to lazily retrieve, transcode, and atomize certain URI
// properties the first time they're used, and cache the results, so that they
// can be used across multiple match operations.
class URLInfo final {
 public:
  MOZ_IMPLICIT URLInfo(nsIURI* aURI) : mURI(aURI) { mHost.SetIsVoid(true); }

  URLInfo(nsIURI* aURI, bool aNoRef) : URLInfo(aURI) {
    if (aNoRef) {
      mURINoRef = mURI;
    }
  }

  URLInfo(const URLInfo& aOther) : URLInfo(aOther.mURI.get()) {}

  nsIURI* URI() const { return mURI; }

  nsAtom* Scheme() const;
  const nsCString& Host() const;
  const nsAtom* HostAtom() const;
  const nsCString& Path() const;
  const nsCString& FilePath() const;
  const nsString& Spec() const;
  const nsCString& CSpec() const;

  bool InheritsPrincipal() const;
  // Whether the URL is a meaningful representation of a document URL, even if
  // that document were to have an opaque origin (null principal).
  // These URLs are more verbose than the precursor (origin) of a null
  // principal, and therefore preferred for matching purposes.
  bool IsNonOpaqueURL() const;

 private:
  nsIURI* URINoRef() const;

  nsCOMPtr<nsIURI> mURI;
  mutable nsCOMPtr<nsIURI> mURINoRef;

  mutable RefPtr<nsAtom> mScheme;
  mutable nsCString mHost;
  mutable RefPtr<nsAtom> mHostAtom;

  mutable nsCString mPath;
  mutable nsCString mFilePath;
  mutable nsString mSpec;
  mutable nsCString mCSpec;

  mutable Maybe<bool> mInheritsPrincipal;
  mutable Maybe<bool> mIsNonOpaqueURL;
};

// Similar to URLInfo, but for cookies.
class MOZ_STACK_CLASS CookieInfo final {
 public:
  MOZ_IMPLICIT CookieInfo(nsICookie* aCookie) : mCookie(aCookie) {}

  bool IsSecure() const;
  bool IsDomain() const;

  const nsCString& Host() const;
  const nsCString& RawHost() const;

 private:
  nsCOMPtr<nsICookie> mCookie;

  mutable Maybe<bool> mIsSecure;
  mutable Maybe<bool> mIsDomain;

  mutable nsCString mHost;
  mutable nsCString mRawHost;
};

class MatchPatternCore final {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MatchPatternCore)

  // NOTE: Must be constructed on the main thread!
  MatchPatternCore(const nsAString& aPattern, bool aIgnorePath,
                   bool aRestrictSchemes, ErrorResult& aRv);

  bool Matches(const nsAString& aURL, bool aExplicit, ErrorResult& aRv) const;

  bool Matches(const URLInfo& aURL, bool aExplicit = false) const;

  bool MatchesAllWebUrls() const;
  // Helper for MatchPatternSetCore::MatchesAllWebUrls:
  bool MatchesAllUrlsWithScheme(const nsAtom* aScheme) const;

  // A quick way to check if a particular URL matches <all_urls> without
  // actually instantiating a MatchPattern. Callable from any thread.
  static bool MatchesAllURLs(const URLInfo& aURL);

  bool ContainsScheme(const nsAtom* aScheme) const;

  bool MatchesCookie(const CookieInfo& aCookie) const;

  bool MatchesDomain(const nsACString& aDomain) const;

  bool Subsumes(const MatchPatternCore& aPattern) const;

  bool SubsumesDomain(const MatchPatternCore& aPattern) const;

  bool Overlaps(const MatchPatternCore& aPattern) const;

  bool DomainIsWildcard() const { return mMatchSubdomain && mDomain.IsEmpty(); }

  void GetPattern(nsAString& aPattern) const { aPattern = mPattern; }

  void GetDomain(nsACString& aDomain) const { aDomain = mDomain; }

 private:
  ~MatchPatternCore() = default;

  // The normalized match pattern string that this object represents.
  nsString mPattern;

  // The set of atomized URI schemes that this pattern matches.
  RefPtr<AtomSet> mSchemes;

  // The domain that this matcher matches. If mMatchSubdomain is false, only
  // matches the exact domain. If it's true, matches the domain or any
  // subdomain.
  //
  // For instance, "*.foo.com" gives mDomain = "foo.com" and mMatchSubdomain =
  // true, and matches "foo.com" or "bar.foo.com" but not "barfoo.com".
  //
  // While "foo.com" gives mDomain = "foo.com" and mMatchSubdomain = false,
  // and matches "foo.com" but not "bar.foo.com".
  nsCString mDomain;
  bool mMatchSubdomain = false;

  // The glob against which the URL path must match. If null, the path is
  // ignored entirely. If non-null, the path must match this glob.
  RefPtr<MatchGlobCore> mPath;
};

class MatchPattern final : public nsISupports, public nsWrapperCache {
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(MatchPattern)

  static already_AddRefed<MatchPattern> Constructor(
      dom::GlobalObject& aGlobal, const nsAString& aPattern,
      const MatchPatternOptions& aOptions, ErrorResult& aRv);

  bool Matches(const nsAString& aURL, bool aExplicit, ErrorResult& aRv) const {
    return Core()->Matches(aURL, aExplicit, aRv);
  }

  bool MatchesAllWebUrls() const { return Core()->MatchesAllWebUrls(); }

  bool Matches(const URLInfo& aURL, bool aExplicit = false) const {
    return Core()->Matches(aURL, aExplicit);
  }

  bool Matches(const URLInfo& aURL, bool aExplicit, ErrorResult& aRv) const {
    return Matches(aURL, aExplicit);
  }

  bool MatchesCookie(const CookieInfo& aCookie) const {
    return Core()->MatchesCookie(aCookie);
  }

  bool MatchesDomain(const nsACString& aDomain) const {
    return Core()->MatchesDomain(aDomain);
  }

  bool Subsumes(const MatchPattern& aPattern) const {
    return Core()->Subsumes(*aPattern.Core());
  }

  bool SubsumesDomain(const MatchPattern& aPattern) const {
    return Core()->SubsumesDomain(*aPattern.Core());
  }

  bool Overlaps(const MatchPattern& aPattern) const {
    return Core()->Overlaps(*aPattern.Core());
  }

  bool DomainIsWildcard() const { return Core()->DomainIsWildcard(); }

  void GetPattern(nsAString& aPattern) const { Core()->GetPattern(aPattern); }

  MatchPatternCore* Core() const { return mCore; }

  nsISupports* GetParentObject() const { return mParent; }

  virtual JSObject* WrapObject(JSContext* aCx,
                               JS::Handle<JSObject*> aGivenProto) override;

 protected:
  virtual ~MatchPattern() = default;

 private:
  friend class MatchPatternSet;

  explicit MatchPattern(nsISupports* aParent,
                        already_AddRefed<MatchPatternCore> aCore)
      : mParent(aParent), mCore(std::move(aCore)) {}

  void Init(JSContext* aCx, const nsAString& aPattern, bool aIgnorePath,
            bool aRestrictSchemes, ErrorResult& aRv);

  nsCOMPtr<nsISupports> mParent;

  RefPtr<MatchPatternCore> mCore;

 public:
  // A quick way to check if a particular URL matches <all_urls> without
  // actually instantiating a MatchPattern.
  static bool MatchesAllURLs(const URLInfo& aURL) {
    return MatchPatternCore::MatchesAllURLs(aURL);
  }
};

class MatchPatternSetCore final {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MatchPatternSetCore)

  using ArrayType = nsTArray<RefPtr<MatchPatternCore>>;

  explicit MatchPatternSetCore(ArrayType&& aPatterns)
      : mPatterns(std::move(aPatterns)) {}

  static already_AddRefed<MatchPatternSet> Constructor(
      dom::GlobalObject& aGlobal,
      const nsTArray<dom::OwningStringOrMatchPattern>& aPatterns,
      const MatchPatternOptions& aOptions, ErrorResult& aRv);

  bool Matches(const nsAString& aURL, bool aExplicit, ErrorResult& aRv) const;

  bool Matches(const URLInfo& aURL, bool aExplicit = false) const;

  bool MatchesAllWebUrls() const;

  bool MatchesCookie(const CookieInfo& aCookie) const;

  bool Subsumes(const MatchPatternCore& aPattern) const;

  bool SubsumesDomain(const MatchPatternCore& aPattern) const;

  bool Overlaps(const MatchPatternCore& aPattern) const;

  bool Overlaps(const MatchPatternSetCore& aPatternSet) const;

  bool OverlapsAll(const MatchPatternSetCore& aPatternSet) const;

  void GetPatterns(ArrayType& aPatterns) {
    aPatterns.AppendElements(mPatterns);
  }

 private:
  friend class MatchPatternSet;

  ~MatchPatternSetCore() = default;

  ArrayType mPatterns;
};

class MatchPatternSet final : public nsISupports, public nsWrapperCache {
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(MatchPatternSet)

  using ArrayType = nsTArray<RefPtr<MatchPattern>>;

  static already_AddRefed<MatchPatternSet> Constructor(
      dom::GlobalObject& aGlobal,
      const nsTArray<dom::OwningStringOrMatchPattern>& aPatterns,
      const MatchPatternOptions& aOptions, ErrorResult& aRv);

  bool Matches(const nsAString& aURL, bool aExplicit, ErrorResult& aRv) const {
    return Core()->Matches(aURL, aExplicit, aRv);
  }

  bool Matches(const URLInfo& aURL, bool aExplicit = false) const {
    return Core()->Matches(aURL, aExplicit);
  }

  bool Matches(const URLInfo& aURL, bool aExplicit, ErrorResult& aRv) const {
    return Matches(aURL, aExplicit);
  }

  bool MatchesAllWebUrls() const { return Core()->MatchesAllWebUrls(); }

  bool MatchesCookie(const CookieInfo& aCookie) const {
    return Core()->MatchesCookie(aCookie);
  }

  bool Subsumes(const MatchPattern& aPattern) const {
    return Core()->Subsumes(*aPattern.Core());
  }

  bool SubsumesDomain(const MatchPattern& aPattern) const {
    return Core()->SubsumesDomain(*aPattern.Core());
  }

  bool Overlaps(const MatchPattern& aPattern) const {
    return Core()->Overlaps(*aPattern.Core());
  }

  bool Overlaps(const MatchPatternSet& aPatternSet) const {
    return Core()->Overlaps(*aPatternSet.Core());
  }

  bool OverlapsAll(const MatchPatternSet& aPatternSet) const {
    return Core()->OverlapsAll(*aPatternSet.Core());
  }

  void GetPatterns(ArrayType& aPatterns);

  MatchPatternSetCore* Core() const { return mCore; }

  nsISupports* GetParentObject() const { return mParent; }

  virtual JSObject* WrapObject(JSContext* aCx,
                               JS::Handle<JSObject*> aGivenProto) override;

 protected:
  virtual ~MatchPatternSet() = default;

 private:
  friend class ExtensionGuardSet;

  explicit MatchPatternSet(nsISupports* aParent,
                           already_AddRefed<MatchPatternSetCore> aCore)
      : mParent(aParent), mCore(std::move(aCore)) {}

  nsCOMPtr<nsISupports> mParent;

  RefPtr<MatchPatternSetCore> mCore;

  mozilla::Maybe<ArrayType> mPatternsCache;
};

class ExtensionGuardSetCore final {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(ExtensionGuardSetCore)

  ExtensionGuardSetCore(RefPtr<MatchPatternSetCore> aDeny,
                        RefPtr<MatchPatternSetCore> aExcept,
                        dom::ExtensionGuardSource aSource)
      : mDeny(std::move(aDeny)),
        mExcept(std::move(aExcept)),
        mSource(aSource) {}

  bool Denies(const URLInfo& aURI) const {
    if (!mDeny->Matches(aURI)) {
      return false;
    }
    return !mExcept || !mExcept->Matches(aURI);
  }

  dom::ExtensionGuardSource Source() const { return mSource; }
  MatchPatternSetCore* DenyCore() const { return mDeny; }
  MatchPatternSetCore* GetExceptCore() const { return mExcept; }

 private:
  ~ExtensionGuardSetCore() = default;

  RefPtr<MatchPatternSetCore> mDeny;
  RefPtr<MatchPatternSetCore> mExcept;  // may be null
  dom::ExtensionGuardSource mSource;
};

class ExtensionGuardSet final : public nsISupports, public nsWrapperCache {
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(ExtensionGuardSet)

  static already_AddRefed<ExtensionGuardSet> Constructor(
      dom::GlobalObject& aGlobal, const dom::ExtensionGuardSetInit& aInit,
      ErrorResult& aRv);

  MatchPatternSet* Deny() const { return mDeny; }
  MatchPatternSet* GetExcept() const { return mExcept; }
  dom::ExtensionGuardSource Source() const { return mCore->Source(); }

  bool Denies(const URLInfo& aURI) const { return mCore->Denies(aURI); }

  ExtensionGuardSetCore* Core() const { return mCore; }

  nsISupports* GetParentObject() const { return mParent; }

  virtual JSObject* WrapObject(JSContext* aCx,
                               JS::Handle<JSObject*> aGivenProto) override;

 protected:
  virtual ~ExtensionGuardSet() = default;

 private:
  friend class WebExtensionPolicy;

  explicit ExtensionGuardSet(nsISupports* aParent,
                             already_AddRefed<ExtensionGuardSetCore> aCore);

  nsCOMPtr<nsISupports> mParent;
  RefPtr<ExtensionGuardSetCore> mCore;
  RefPtr<MatchPatternSet> mDeny;
  RefPtr<MatchPatternSet> mExcept;  // may be null
};

}  // namespace extensions
}  // namespace mozilla

#endif  // mozilla_extensions_MatchPattern_h
