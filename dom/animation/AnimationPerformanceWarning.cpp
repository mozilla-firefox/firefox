/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AnimationPerformanceWarning.h"

#include "nsContentUtils.h"

namespace mozilla {

template <uint32_t N>
nsresult AnimationPerformanceWarning::ToLocalizedStringWithIntParams(
    const char* aKey, nsAString& aLocalizedString) const {
  AutoTArray<nsString, N> strings;

  MOZ_DIAGNOSTIC_ASSERT(mParams->Length() == N);
  for (size_t i = 0, n = mParams->Length(); i < n; i++) {
    strings.AppendElement()->AppendInt((*mParams)[i]);
  }

  return nsContentUtils::FormatLocalizedString(
      PropertiesFile::LAYOUT_PROPERTIES, aKey, strings, aLocalizedString);
}

bool AnimationPerformanceWarning::ToLocalizedString(
    nsAString& aLocalizedString) const {
  const char* key = nullptr;

  switch (mType) {
    case Type::ContentTooLarge:
      MOZ_ASSERT(mParams && mParams->Length() == 6,
                 "Parameter's length should be 6 for ContentTooLarge2");

      return NS_SUCCEEDED(ToLocalizedStringWithIntParams<6>(
          "CompositorAnimationWarningContentTooLarge2", aLocalizedString));
    case Type::ContentTooLargeArea:
      MOZ_ASSERT(mParams && mParams->Length() == 2,
                 "Parameter's length should be 2 for ContentTooLargeArea");

      return NS_SUCCEEDED(ToLocalizedStringWithIntParams<2>(
          "CompositorAnimationWarningContentTooLargeArea", aLocalizedString));
    case Type::NonScalingStroke:
      key = "CompositorAnimationWarningNonScalingStroke";
      break;
    case Type::TransformSVG:
      key = "CompositorAnimationWarningTransformSVG";
      break;
    case Type::TransformFrameInactive:
      key = "CompositorAnimationWarningTransformFrameInactive";
      break;
    case Type::TransformIsBlockedByImportantRules:
      key = "CompositorAnimationWarningTransformIsBlockedByImportantRules";
      break;
    case Type::OpacityFrameInactive:
      key = "CompositorAnimationWarningOpacityFrameInactive";
      break;
    case Type::HasRenderingObserver:
      key = "CompositorAnimationWarningHasRenderingObserver";
      break;
    case Type::HasCurrentColor:
      key = "CompositorAnimationWarningHasCurrentColor";
      break;
    case Type::None:
      MOZ_ASSERT_UNREACHABLE("Uninitialized type shouldn't be used");
      return false;
  }

  nsresult rv = nsContentUtils::GetLocalizedString(
      PropertiesFile::LAYOUT_PROPERTIES, key, aLocalizedString);
  return NS_SUCCEEDED(rv);
}

}  // namespace mozilla
