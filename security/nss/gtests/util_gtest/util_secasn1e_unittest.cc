/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "scoped_ptrs_util.h"
#include "secasn1.h"

#include "gtest/gtest.h"

namespace nss_test {

static const SEC_ASN1Template NullTemplate[] = {
    {SEC_ASN1_NULL, 0, NULL, sizeof(SECItem)}, {0}};

// Bug 2030979: sec_asn1e_contents_length did not hard-code the fact
// that SEC_ASN1_NULL has zero content length. It would treat the source
// pointer as a SECItem* and then try to read the content length from the
// ->len field, 16 bytes past the source, which might be out-of-bounds.
TEST(SECASN1ETest, EncodeNullNearArenaAllocationBoundary) {
  ScopedPLArenaPool arena(PORT_NewArena(4096));
  ASSERT_NE(nullptr, arena);

  void* src = PORT_ArenaAlloc(arena.get(), 16);
  ASSERT_NE(nullptr, src);
  memset(src, 0, 16);

  SECItem result = {siBuffer, nullptr, 0};
  SECItem* encoded =
      SEC_ASN1EncodeItem(arena.get(), &result, src, NullTemplate);
  ASSERT_NE(nullptr, encoded);

  // DER encoding of NULL: tag 0x05, length 0x00
  ASSERT_EQ(2U, result.len);
  EXPECT_EQ(0x05, result.data[0]);
  EXPECT_EQ(0x00, result.data[1]);
}

}  // namespace nss_test
