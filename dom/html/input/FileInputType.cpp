/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/FileInputType.h"

#include "mozilla/dom/HTMLInputElement.h"

using namespace mozilla;
using namespace mozilla::dom;

bool FileInputType::IsValueMissing() const {
  if (!mInputElement->IsRequired()) {
    return false;
  }

  return mInputElement->GetFilesOrDirectoriesInternal().IsEmpty();
}

nsresult FileInputType::GetValueMissingMessage(nsAString& aMessage) {
  return nsContentUtils::GetMaybeLocalizedString(
      PropertiesFile::DOM_PROPERTIES, "FormValidationFileMissing",
      mInputElement->OwnerDoc(), aMessage);
}
