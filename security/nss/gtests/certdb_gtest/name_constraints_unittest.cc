/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <initializer_list>
#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "cert.h"
#include "nss_scoped_ptrs.h"
#include "secasn1.h"

namespace nss_test {

typedef std::vector<uint8_t> Bytes;

// Encodes a DER TLV. Every test value is short, so a one-byte length is
// enough.
static Bytes Tlv(uint8_t tag, const Bytes& body) {
  EXPECT_LT(body.size(), 0x80U);
  Bytes out = {tag, static_cast<uint8_t>(body.size())};
  out.insert(out.end(), body.begin(), body.end());
  return out;
}

static Bytes Concat(std::initializer_list<Bytes> parts) {
  Bytes out;
  for (const Bytes& p : parts) {
    out.insert(out.end(), p.begin(), p.end());
  }
  return out;
}

// AttributeTypeAndValue with a PrintableString value. |oidLast| is the last
// arc of id-at (2.5.4.x).
static Bytes Ava(uint8_t oidLast, const std::string& value) {
  Bytes oid = Tlv(0x06, {0x55, 0x04, oidLast});
  Bytes str = Tlv(0x13, Bytes(value.begin(), value.end()));
  return Tlv(0x30, Concat({oid, str}));
}

static const uint8_t kCountry = 0x06;
static const uint8_t kOrg = 0x0A;
static const uint8_t kOrgUnit = 0x0B;
static const uint8_t kCommonName = 0x03;

static Bytes Rdn(std::initializer_list<Bytes> avas) {
  return Tlv(0x31, Concat(avas));
}
static Bytes Name(std::initializer_list<Bytes> rdns) {
  return Tlv(0x30, Concat(rdns));
}

// NameConstraints with a single directoryName subtree, either permitted
// ([0]) or excluded ([1]).
static Bytes DirectoryNameConstraints(const Bytes& name, bool excluded) {
  Bytes base = Tlv(0xA4, name);
  Bytes subtree = Tlv(0x30, base);
  Bytes subtrees = Tlv(excluded ? 0xA1 : 0xA0, subtree);
  return Tlv(0x30, subtrees);
}

class NameConstraintsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    arena_.reset(PORT_NewArena(DER_DEFAULT_CHUNKSIZE));
    ASSERT_TRUE(arena_);
  }

  // Checks the directory name |name| against a single directoryName
  // constraint |constraint|.
  SECStatus Check(const Bytes& constraint, bool excluded, const Bytes& name) {
    Bytes ncDer = DirectoryNameConstraints(constraint, excluded);
    SECItem ncItem = {siBuffer, const_cast<uint8_t*>(ncDer.data()),
                      static_cast<unsigned int>(ncDer.size())};
    CERTNameConstraints* constraints =
        CERT_DecodeNameConstraintsExtension(arena_.get(), &ncItem);
    EXPECT_NE(nullptr, constraints);
    if (!constraints) {
      return SECFailure;
    }

    CERTGeneralName* gn = PORT_ArenaZNew(arena_.get(), CERTGeneralName);
    EXPECT_NE(nullptr, gn);
    if (!gn) {
      return SECFailure;
    }
    gn->type = certDirectoryName;
    PR_INIT_CLIST(&gn->l);
    // Copy the DER into the arena: the decoded name points into it.
    SECItem nameItem = {siBuffer, nullptr, 0};
    SECItem src = {siBuffer, const_cast<uint8_t*>(name.data()),
                   static_cast<unsigned int>(name.size())};
    if (SECITEM_CopyItem(arena_.get(), &nameItem, &src) != SECSuccess ||
        SEC_QuickDERDecodeItem(arena_.get(), &gn->name.directoryName,
                               SEC_ASN1_GET(CERT_NameTemplate),
                               &nameItem) != SECSuccess) {
      ADD_FAILURE() << "failed to decode the test name";
      return SECFailure;
    }
    return CERT_CheckNameSpace(arena_.get(), constraints, gn);
  }

  ScopedPLArenaPool arena_;
};

static const Bytes kUS = Ava(kCountry, "US");
static const Bytes kGood = Ava(kOrg, "Good");
static const Bytes kEvil = Ava(kOrg, "Evil");
static const Bytes kCN = Ava(kCommonName, "host");
static const Bytes kEmptyRdn = Rdn({});

TEST_F(NameConstraintsTest, PermittedPrefixMatches) {
  Bytes constraint = Name({Rdn({kUS}), Rdn({kGood})});
  EXPECT_EQ(SECSuccess,
            Check(constraint, false, Name({Rdn({kUS}), Rdn({kGood})})));
  EXPECT_EQ(SECSuccess, Check(constraint, false,
                              Name({Rdn({kUS}), Rdn({kGood}), Rdn({kCN})})));
}

TEST_F(NameConstraintsTest, PermittedOtherNameFails) {
  Bytes constraint = Name({Rdn({kUS}), Rdn({kGood})});
  EXPECT_EQ(SECFailure, Check(constraint, false,
                              Name({Rdn({kUS}), Rdn({kEvil}), Rdn({kCN})})));
}

// A name RDN without attributes must not stand in for a constraint RDN.
TEST_F(NameConstraintsTest, PermittedEmptyRdnFails) {
  Bytes constraint = Name({Rdn({kUS}), Rdn({kGood})});
  EXPECT_EQ(SECFailure,
            Check(constraint, false,
                  Name({Rdn({kUS}), kEmptyRdn, Rdn({kEvil}), Rdn({kCN})})));
  EXPECT_EQ(SECFailure, Check(constraint, false,
                              Name({kEmptyRdn, kEmptyRdn, Rdn({kCN})})));
}

// A name shorter than the constraint is not inside the subtree.
TEST_F(NameConstraintsTest, PermittedShorterNameFails) {
  Bytes constraint = Name({Rdn({kUS}), Rdn({kGood})});
  EXPECT_EQ(SECFailure, Check(constraint, false, Name({Rdn({kUS})})));
}

// RDNs are compared as a whole: an extra attribute in the name's RDN is a
// different RDN.
TEST_F(NameConstraintsTest, PermittedMultiValuedRdnFails) {
  Bytes constraint = Name({Rdn({kUS}), Rdn({kGood})});
  Bytes ou = Ava(kOrgUnit, "Unit");
  EXPECT_EQ(SECFailure, Check(constraint, false,
                              Name({Rdn({kUS}), Rdn({kGood, ou}), Rdn({kCN})})));
}

// RFC 5280 4.2.1.10: an empty subject is not subject to directoryName
// constraints.
TEST_F(NameConstraintsTest, PermittedEmptySubjectPasses) {
  Bytes constraint = Name({Rdn({kUS}), Rdn({kGood})});
  EXPECT_EQ(SECSuccess, Check(constraint, false, Name({})));
}

TEST_F(NameConstraintsTest, EmptyConstraintMatchesEverything) {
  EXPECT_EQ(SECSuccess,
            Check(Name({}), false, Name({Rdn({kUS}), Rdn({kEvil})})));
  EXPECT_EQ(SECFailure,
            Check(Name({}), true, Name({Rdn({kUS}), Rdn({kEvil})})));
}

TEST_F(NameConstraintsTest, Excluded) {
  Bytes constraint = Name({Rdn({kUS}), Rdn({kEvil})});
  EXPECT_EQ(SECFailure, Check(constraint, true,
                              Name({Rdn({kUS}), Rdn({kEvil}), Rdn({kCN})})));
  EXPECT_EQ(SECSuccess, Check(constraint, true,
                              Name({Rdn({kUS}), Rdn({kGood}), Rdn({kCN})})));
}

// Excluded subtrees keep their broader match: a name RDN holding every
// attribute of the constraint RDN, or a name that runs out before the
// constraint does, is still excluded.
TEST_F(NameConstraintsTest, ExcludedKeepsBroaderMatch) {
  Bytes constraint = Name({Rdn({kUS}), Rdn({kEvil})});
  Bytes ou = Ava(kOrgUnit, "Unit");
  EXPECT_EQ(SECFailure, Check(constraint, true,
                              Name({Rdn({kUS}), Rdn({kEvil, ou}), Rdn({kCN})})));
  EXPECT_EQ(SECFailure, Check(constraint, true, Name({Rdn({kUS})})));
}

}  // namespace nss_test
