/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_ReportingUtils_h
#define mozilla_dom_ReportingUtils_h

#include "nsString.h"

class nsAtom;
class nsIGlobalObject;

namespace mozilla::dom {

struct ReportBody;

class ReportingUtils final {
 public:
  static void Report(nsIGlobalObject* aGlobal, nsAtom* aType,
                     const nsAString& aGroupName, const nsAString& aURL,
                     ReportBody* aBody);
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_ReportingUtils_h
