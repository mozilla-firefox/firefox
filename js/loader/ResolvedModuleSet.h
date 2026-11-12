/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef js_loader_ResolvedModuleSet_h
#define js_loader_ResolvedModuleSet_h

#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/HashTable.h"

#include "nsCOMPtr.h"
#include "nsIURI.h"

namespace JS::loader {

// https://html.spec.whatwg.org/#specifier-resolution-record
struct SpecifierResolutionRecord {
  SpecifierResolutionRecord(const nsACString& aSerializedBaseURL,
                            const nsAString& aNormalizedSpecifier,
                            nsIURI* aResult, bool aIsURLLike,
                            bool aIsSpecialScheme)
      : mSerializedBaseURL(aSerializedBaseURL),
        mNormalizedSpecifier(aNormalizedSpecifier),
        mResult(aResult),
        mIsURLLike(aIsURLLike),
        mIsSpecialScheme(aIsSpecialScheme) {}

  const nsACString& SerializedBaseURL() const { return mSerializedBaseURL; }

  const nsAString& NormalizedSpecifier() const { return mNormalizedSpecifier; }

  nsIURI* Result() const { return mResult; }
  already_AddRefed<nsIURI> TakeResult() { return mResult.forget(); }

  bool IsURLLike() const { return mIsURLLike; }
  bool IsAsURLNull() const { return !mIsURLLike; }
  bool IsSpecialScheme() const { return mIsSpecialScheme; }

  mozilla::HashNumber Hash() const {
    return mozilla::AddToHash(
        mozilla::HashString(mSerializedBaseURL.get(),
                            mSerializedBaseURL.Length()),
        mozilla::HashString(mNormalizedSpecifier.get(),
                            mNormalizedSpecifier.Length()));
  }

  bool Match(const mozilla::UniquePtr<SpecifierResolutionRecord>& aLookup) {
    bool eq = mSerializedBaseURL.Equals(aLookup->SerializedBaseURL()) &&
              mNormalizedSpecifier.Equals(aLookup->NormalizedSpecifier());
    MOZ_ASSERT_IF(eq, mIsURLLike == aLookup->IsURLLike());
    MOZ_ASSERT_IF(eq, mIsSpecialScheme == aLookup->IsSpecialScheme());
    return eq;
  }

  const nsCString mSerializedBaseURL;

  const nsString mNormalizedSpecifier;

  // The resolved URL. Released before the record is added to a resolved module
  // set.
  nsCOMPtr<nsIURI> mResult;

  const bool mIsURLLike;

  const bool mIsSpecialScheme;
};

struct SpecifierResolutionHasher {
  using Key = mozilla::UniquePtr<SpecifierResolutionRecord>;
  using Lookup = const Key&;

  static mozilla::HashNumber hash(Lookup aLookup) { return aLookup->Hash(); }

  static bool match(const Key& aKey, Lookup aLookup) {
    return aKey->Match(aLookup);
  }
};

// https://html.spec.whatwg.org/#resolved-module-set
using ResolvedModuleSet =
    mozilla::HashSet<mozilla::UniquePtr<SpecifierResolutionRecord>,
                     SpecifierResolutionHasher, InfallibleAllocPolicy>;

}  // namespace JS::loader

#endif  // js_loader_ResolvedModuleSet_h
