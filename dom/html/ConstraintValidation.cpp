/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ConstraintValidation.h"

#include "mozilla/ErrorResult.h"
#include "nsAString.h"
#include "nsContentUtils.h"
#include "nsIContent.h"

namespace mozilla::dom {

void ConstraintValidation::GetValidationMessage(nsAString& aValidationMessage,
                                                ErrorResult& aError) {
  aValidationMessage.Truncate();

  if (IsCandidateForConstraintValidation() && !IsValid()) {
    if (GetValidityState(VALIDITY_STATE_CUSTOM_ERROR)) {
      aValidationMessage.Assign(mCustomValidity);
      if (aValidationMessage.Length() > sContentSpecifiedMaxLengthMessage) {
        aValidationMessage.Truncate(sContentSpecifiedMaxLengthMessage);
      }
    } else if (GetValidityState(VALIDITY_STATE_TOO_LONG)) {
      GetValidationMessage(aValidationMessage, VALIDITY_STATE_TOO_LONG);
    } else if (GetValidityState(VALIDITY_STATE_TOO_SHORT)) {
      GetValidationMessage(aValidationMessage, VALIDITY_STATE_TOO_SHORT);
    } else if (GetValidityState(VALIDITY_STATE_VALUE_MISSING)) {
      GetValidationMessage(aValidationMessage, VALIDITY_STATE_VALUE_MISSING);
    } else if (GetValidityState(VALIDITY_STATE_TYPE_MISMATCH)) {
      GetValidationMessage(aValidationMessage, VALIDITY_STATE_TYPE_MISMATCH);
    } else if (GetValidityState(VALIDITY_STATE_PATTERN_MISMATCH)) {
      GetValidationMessage(aValidationMessage, VALIDITY_STATE_PATTERN_MISMATCH);
    } else if (GetValidityState(VALIDITY_STATE_RANGE_OVERFLOW)) {
      GetValidationMessage(aValidationMessage, VALIDITY_STATE_RANGE_OVERFLOW);
    } else if (GetValidityState(VALIDITY_STATE_RANGE_UNDERFLOW)) {
      GetValidationMessage(aValidationMessage, VALIDITY_STATE_RANGE_UNDERFLOW);
    } else if (GetValidityState(VALIDITY_STATE_STEP_MISMATCH)) {
      GetValidationMessage(aValidationMessage, VALIDITY_STATE_STEP_MISMATCH);
    } else if (GetValidityState(VALIDITY_STATE_BAD_INPUT)) {
      GetValidationMessage(aValidationMessage, VALIDITY_STATE_BAD_INPUT);
    } else {
      // There should not be other validity states.
      aError.Throw(NS_ERROR_UNEXPECTED);
      return;
    }
  } else {
    aValidationMessage.Truncate();
  }
}

bool ConstraintValidation::CheckValidity() {
  nsCOMPtr<nsIContent> content = do_QueryInterface(this);
  MOZ_ASSERT(content, "This class should be inherited by HTML elements only!");
  return nsIConstraintValidation::CheckValidity(*content);
}

ConstraintValidation::ConstraintValidation() = default;

void ConstraintValidation::SetCustomValidity(const nsAString& aError) {
  nsAutoString error(aError);
  nsContentUtils::PlatformToDOMLineBreaks(error);
  mCustomValidity.Assign(error);
  SetValidityState(VALIDITY_STATE_CUSTOM_ERROR, !mCustomValidity.IsEmpty());
}

}  // namespace mozilla::dom
