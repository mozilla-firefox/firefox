/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_ConstraintValidition_h_
#define mozilla_dom_ConstraintValidition_h_

#include "nsIConstraintValidation.h"
#include "nsString.h"

namespace mozilla {
class ErrorResult;

namespace dom {

class Document;

class ConstraintValidation : public nsIConstraintValidation {
 public:
  // Web IDL binding methods
  void GetValidationMessage(nsAString& aValidationMessage,
                            mozilla::ErrorResult& aError);
  MOZ_CAN_RUN_SCRIPT bool CheckValidity();

  enum class TextLengthLimit { Max, Min };

  /**
   * Formats the validation message for a text that is too long (if aLimitType
   * is Max) or too short (if Min), given the length limit and the current
   * length of the text.
   */
  static nsresult GetTextLengthValidationMessage(const Document* aDocument,
                                                 TextLengthLimit aLimitType,
                                                 int32_t aLimit,
                                                 int32_t aTextLength,
                                                 nsAString& aResult);

 protected:
  // You can't instantiate an object from that class.
  ConstraintValidation();

  virtual ~ConstraintValidation() = default;

  void SetCustomValidity(const nsAString& aError);

  virtual nsresult GetValidationMessage(nsAString& aValidationMessage,
                                        ValidityStateType aType) {
    return NS_OK;
  }

 private:
  /**
   * The string representing the custom error.
   */
  nsString mCustomValidity;
};

}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_dom_ConstraintValidition_h_
