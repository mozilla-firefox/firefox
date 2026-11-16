/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PrincipalHandle.h"
#include "gtest/gtest.h"
#include "mozilla/NullPrincipal.h"

using namespace mozilla;

TEST(PrincipalHandle, EqualityIsPerConstruction)
{
  RefPtr<nsIPrincipal> principal =
      NullPrincipal::CreateWithoutOriginAttributes();
  PrincipalHandle a(principal);
  PrincipalHandle copy = a;
  PrincipalHandle b(principal);

  EXPECT_EQ(a, copy);
  EXPECT_NE(a, b);
  EXPECT_EQ(GetPrincipalFromHandle(a), GetPrincipalFromHandle(b));
}

TEST(PrincipalHandle, NoneIsDistinctFromAnyMadeHandle)
{
  RefPtr<nsIPrincipal> principal =
      NullPrincipal::CreateWithoutOriginAttributes();
  PrincipalHandle none = PRINCIPAL_HANDLE_NONE;

  EXPECT_EQ(none, PRINCIPAL_HANDLE_NONE);
  EXPECT_NE(none, PrincipalHandle(principal));
  EXPECT_EQ(GetPrincipalFromHandle(none), nullptr);
}
