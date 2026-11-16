/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_Feature_h
#define mozilla_dom_Feature_h

#include "mozilla/RefPtr.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/nsCSPUtils.h"
#include "nsCOMPtr.h"
#include "nsISupportsImpl.h"
#include "nsString.h"
#include "nsTArray.h"

class nsIPrincipal;

namespace mozilla::dom {

// Refcounted wrapper that lets Feature copies share a parsed source expression.
class ParsedSourceExpression final {
 public:
  NS_INLINE_DECL_REFCOUNTING(ParsedSourceExpression)

  explicit ParsedSourceExpression(UniquePtr<nsCSPBaseSrc> aSource)
      : mSource(std::move(aSource)) {}

  const nsCSPBaseSrc& Source() const { return *mSource; }

 private:
  ~ParsedSourceExpression() = default;

  UniquePtr<nsCSPBaseSrc> mSource;
};

class Feature final {
 public:
  explicit Feature(const nsAString& aFeatureName);

  ~Feature();

  const nsAString& Name() const;

  void SetAllowsNone();

  bool AllowsNone() const;

  void SetAllowsAll();

  bool AllowsAll() const;

  void AppendToAllowList(nsIPrincipal* aPrincipal);

  void AppendSourceExpression(UniquePtr<nsCSPBaseSrc> aSourceExpression);

  void AppendSourceExpressionsFromFeature(const Feature& aOther);

  void GetAllowList(nsTArray<nsCOMPtr<nsIPrincipal>>& aList) const;

  void GetSourceExpressions(nsTArray<nsString>& aList) const;

  bool AllowListContains(nsIPrincipal* aPrincipal) const;

  bool HasAllowList() const;

  bool Allows(nsIPrincipal* aPrincipal) const;

 private:
  nsString mFeatureName;

  enum Policy {
    // denotes a policy of "feature 'none'"
    eNone,

    // denotes a policy of "feature *"
    eAll,

    // denotes a policy of "feature bar.com foo.com"
    eAllowList,
  };

  Policy mPolicy;

  CopyableTArray<nsCOMPtr<nsIPrincipal>> mAllowList;
  CopyableTArray<RefPtr<ParsedSourceExpression>> mSourceExpressions;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_Feature_h
