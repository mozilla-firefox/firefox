/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_CSSPropertyId_h
#define mozilla_CSSPropertyId_h

#include "NonCustomCSSPropertyId.h"
#include "mozilla/Assertions.h"
#include "mozilla/HashFunctions.h"
#include "mozilla/RefPtr.h"
#include "nsAtom.h"
#include "nsCSSProps.h"
#include "nsString.h"

namespace mozilla {

struct CSSPropertyId {
  explicit CSSPropertyId(NonCustomCSSPropertyId aProperty) : mId(aProperty) {
    MOZ_ASSERT(aProperty != eCSSPropertyExtra_variable,
               "Cannot create an CSSPropertyId from only a "
               "eCSSPropertyExtra_variable.");
  }

 private:
  explicit CSSPropertyId(RefPtr<nsAtom> aCustomName)
      : mId(eCSSPropertyExtra_variable), mCustomName(std::move(aCustomName)) {
    MOZ_ASSERT(mCustomName, "Null custom property name");
  }

 public:
  // Returns a CSSPropertyId for the given string. Note that the property
  // might be invalid, so callers need to check it with IsValid().
  static CSSPropertyId Parse(const nsACString& aName) {
    NonCustomCSSPropertyId prop = nsCSSProps::LookupProperty(aName);
    if (prop == eCSSPropertyExtra_variable) {
      return FromCustomProperty(aName);
    }
    return CSSPropertyId(prop);
  }

  // Callers are expected to pass a custom name atom with the leading "--"
  // already stripped. The remaining ident may legally start with "-"
  // (or even "--"), so we cannot assert anything about its prefix here.
  static CSSPropertyId FromCustomName(RefPtr<nsAtom> aCustomName) {
    return CSSPropertyId(std::move(aCustomName));
  }

  static CSSPropertyId FromCustomProperty(const nsACString& aCustomProperty) {
    MOZ_ASSERT(StringBeginsWith(aCustomProperty, "--"_ns));

    // TODO(zrhoffman, bug 1811897) Add test coverage for removing the `--`
    // prefix here.
    RefPtr<nsAtom> atom =
        NS_Atomize(Substring(aCustomProperty, 2, aCustomProperty.Length() - 2));

    return FromCustomName(atom);
  }

  static CSSPropertyId FromIdOrCustomProperty(
      NonCustomCSSPropertyId aId, const nsACString& aCustomProperty) {
    if (aId != eCSSPropertyExtra_variable) {
      return CSSPropertyId(aId);
    }

    return FromCustomProperty(aCustomProperty);
  }

  NonCustomCSSPropertyId mId = eCSSProperty_UNKNOWN;
  RefPtr<nsAtom> mCustomName;

  bool IsCustom() const { return mId == eCSSPropertyExtra_variable; }
  bool IsShorthand() const { return nsCSSProps::IsShorthand(mId); }

  bool operator==(const CSSPropertyId&) const = default;

  bool IsValid() const {
    if (mId == eCSSProperty_UNKNOWN) {
      return false;
    }
    return IsCustom() == !!mCustomName;
  }

  void ToString(nsACString& aString) const {
    if (IsCustom()) {
      MOZ_ASSERT(mCustomName, "Custom property should have a name");
      // mCustomName does not include the "--" prefix
      aString.AssignLiteral("--");
      AppendUTF16toUTF8(nsDependentAtomString(mCustomName), aString);
    } else {
      aString.Assign(nsCSSProps::GetStringValue(mId));
    }
  }

  void ToString(nsAString& aString) const {
    if (IsCustom()) {
      MOZ_ASSERT(mCustomName, "Custom property should have a name");
      // mCustomName does not include the "--" prefix
      aString.AssignLiteral("--");
      aString.Append(nsDependentAtomString(mCustomName));
    } else {
      aString.Assign(NS_ConvertUTF8toUTF16(nsCSSProps::GetStringValue(mId)));
    }
  }

  HashNumber Hash() const {
    HashNumber hash = mCustomName ? mCustomName->hash() : 0;
    return AddToHash(hash, mId);
  }

  CSSPropertyId ToPhysical(const ComputedStyle& aStyle) const {
    if (IsCustom()) {
      return *this;
    }
    return CSSPropertyId{nsCSSProps::Physicalize(mId, aStyle)};
  }
};

// MOZ_DBG support for AnimatedPropertyId
inline std::ostream& operator<<(std::ostream& aOut,
                                const CSSPropertyId& aProperty) {
  if (aProperty.IsCustom()) {
    return aOut << nsAtomCString(aProperty.mCustomName);
  }
  return aOut << nsCSSProps::GetStringValue(aProperty.mId);
}

}  // namespace mozilla

#endif  // mozilla_CSSPropertyId_h
