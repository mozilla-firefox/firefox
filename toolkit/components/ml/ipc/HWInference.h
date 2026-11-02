/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_HWInference_h
#define mozilla_dom_HWInference_h

#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/ErrorResult.h"

namespace mozilla::dom {

class GlobalObject;
class Promise;

// The HWInference WebIDL namespace. Main thread only.
class HWInference final {
 public:
  static already_AddRefed<Promise> ListDevices(const GlobalObject& aGlobal,
                                               ErrorResult& aRv);
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_HWInference_h
