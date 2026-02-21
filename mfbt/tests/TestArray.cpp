/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Array.h"
#include <cstdint>

void TestInitialValueByConstructor() {
  using namespace mozilla;
  // Style 1
  Array<int32_t, 3> arr1(1, 2, 3);
  MOZ_RELEASE_ASSERT(arr1[0] == 1);
  MOZ_RELEASE_ASSERT(arr1[1] == 2);
  MOZ_RELEASE_ASSERT(arr1[2] == 3);
  // Style 2
  Array<int32_t, 3> arr2{5, 6, 7};
  MOZ_RELEASE_ASSERT(arr2[0] == 5);
  MOZ_RELEASE_ASSERT(arr2[1] == 6);
  MOZ_RELEASE_ASSERT(arr2[2] == 7);
  // Style 3
  Array<int32_t, 3> arr3({8, 9, 10});
  MOZ_RELEASE_ASSERT(arr3[0] == 8);
  MOZ_RELEASE_ASSERT(arr3[1] == 9);
  MOZ_RELEASE_ASSERT(arr3[2] == 10);
}

int main() {
  TestInitialValueByConstructor();
  return 0;
}
