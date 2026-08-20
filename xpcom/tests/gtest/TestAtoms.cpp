/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "UTFStrings.h"
#include "gtest/gtest.h"
#include "mozilla/gtest/MozAssertions.h"
#include "nsAtom.h"
#include "nsIThread.h"
#include "nsString.h"
#include "nsThreadUtils.h"

using namespace mozilla;

int32_t NS_GetUnusedAtomCount(void);
size_t TestGetShortAtomCacheSize(void);

namespace TestAtoms {

TEST(Atoms, Basic)
{
  for (unsigned int i = 0; i < std::size(ValidStrings); ++i) {
    nsDependentString str16(ValidStrings[i].m16);
    nsDependentCString str8(ValidStrings[i].m8);

    RefPtr<nsAtom> atom = NS_Atomize(str16);

    EXPECT_TRUE(atom->Equals(str16));

    nsString tmp16;
    nsCString tmp8;
    atom->ToString(tmp16);
    atom->ToUTF8String(tmp8);
    EXPECT_TRUE(str16.Equals(tmp16));
    EXPECT_TRUE(str8.Equals(tmp8));

    EXPECT_TRUE(nsDependentString(atom->GetUTF16String()).Equals(str16));

    EXPECT_TRUE(nsAtomString(atom).Equals(str16));
    EXPECT_TRUE(nsDependentAtomString(atom).Equals(str16));
    EXPECT_TRUE(nsAtomCString(atom).Equals(str8));
  }
}

TEST(Atoms, 16vs8)
{
  for (unsigned int i = 0; i < std::size(ValidStrings); ++i) {
    RefPtr<nsAtom> atom16 = NS_Atomize(ValidStrings[i].m16);
    RefPtr<nsAtom> atom8 = NS_Atomize(ValidStrings[i].m8);
    EXPECT_EQ(atom16, atom8);
  }
}

TEST(Atoms, Null)
{
  nsAutoString str(u"string with a \0 char"_ns);
  nsDependentString strCut(str.get());

  EXPECT_FALSE(str.Equals(strCut));

  RefPtr<nsAtom> atomCut = NS_Atomize(strCut);
  RefPtr<nsAtom> atom = NS_Atomize(str);

  EXPECT_EQ(atom->GetLength(), str.Length());
  EXPECT_TRUE(atom->Equals(str));
  EXPECT_NE(atom, atomCut);
  EXPECT_TRUE(atomCut->Equals(strCut));
}

static constexpr nsLiteralString kShortCacheTestStrings[] = {
    u""_ns,       u"abcdefgh"_ns, u"a"_ns,        u"ab"_ns,
    u"\0"_ns,     u"abcdefg"_ns,  u"a\0"_ns,      u"\0a"_ns,
    u"\u00ff"_ns, u"\u0100"_ns,   u"abcdef\0"_ns, u"abcdef\u0100"_ns,
};

TEST(Atoms, ShortStringCacheCorrectness)
{
  for (const auto& str : kShortCacheTestStrings) {
    RefPtr<nsAtom> uncached = NS_Atomize(str);
    RefPtr<nsAtom> miss = NS_AtomizeMainThread(str);
    RefPtr<nsAtom> hit = NS_AtomizeMainThread(str);

    EXPECT_EQ(uncached, miss);
    EXPECT_EQ(miss, hit);
    EXPECT_TRUE(hit->Equals(str));
  }
}

TEST(Atoms, ShortStringCacheEviction)
{
  const uint64_t kCount = TestGetShortAtomCacheSize() + 10;

  constexpr size_t len = 2;
  ASSERT_LE(kCount, uint64_t(1) << (len * 8));

  for (uint64_t pass = 0; pass < 2; ++pass) {
    for (uint64_t i = 0; i < kCount; ++i) {
      char16_t buf[len] = {char16_t(i & 0xff), char16_t((i >> 8) & 0xff)};
      nsDependentSubstring str(buf, len);
      RefPtr<nsAtom> atom = NS_AtomizeMainThread(str);
      RefPtr<nsAtom> atom2 = NS_Atomize(str);
      EXPECT_TRUE(atom->Equals(str));
      EXPECT_EQ(atom, atom2);
    }
  }
}

TEST(Atoms, Invalid)
{
  for (unsigned int i = 0; i < std::size(Invalid16Strings); ++i) {
    nsrefcnt count = NS_GetNumberOfAtoms();

    {
      RefPtr<nsAtom> atom16 = NS_Atomize(Invalid16Strings[i].m16);
      EXPECT_TRUE(atom16->Equals(nsDependentString(Invalid16Strings[i].m16)));
    }

    EXPECT_EQ(count, NS_GetNumberOfAtoms());
  }
#ifndef DEBUG
  // Don't run this test in debug builds as that intentionally asserts.
  for (unsigned int i = 0; i < std::size(Invalid8Strings); ++i) {
    nsrefcnt count = NS_GetNumberOfAtoms();

    {
      RefPtr<nsAtom> atom8 = NS_Atomize(Invalid8Strings[i].m8);
      RefPtr<nsAtom> atom16 = NS_Atomize(Invalid8Strings[i].m16);
      EXPECT_EQ(atom16, atom8);
      EXPECT_TRUE(atom16->Equals(nsDependentString(Invalid8Strings[i].m16)));
    }

    EXPECT_EQ(count, NS_GetNumberOfAtoms());
  }

  for (unsigned int i = 0; i < std::size(Malformed8Strings); ++i) {
    nsrefcnt count = NS_GetNumberOfAtoms();

    {
      RefPtr<nsAtom> atom8 = NS_Atomize(Malformed8Strings[i].m8);
      RefPtr<nsAtom> atom16 = NS_Atomize(Malformed8Strings[i].m16);
      EXPECT_EQ(atom8, atom16);
    }

    EXPECT_EQ(count, NS_GetNumberOfAtoms());
  }
#endif
}

#define FIRST_ATOM_STR "first static atom. Hello!"
#define SECOND_ATOM_STR "second static atom. @World!"
#define THIRD_ATOM_STR "third static atom?!"

static bool isStaticAtom(nsAtom* atom) {
  // Don't use logic && in order to ensure that all addrefs/releases are always
  // run, even if one of the tests fail. This allows us to run this code on a
  // non-static atom without affecting its refcount.
  bool rv = (atom->AddRef() == 2);
  rv &= (atom->AddRef() == 2);
  rv &= (atom->AddRef() == 2);

  rv &= (atom->Release() == 1);
  rv &= (atom->Release() == 1);
  rv &= (atom->Release() == 1);
  return rv;
}

TEST(Atoms, Table)
{
  nsrefcnt count = NS_GetNumberOfAtoms();

  RefPtr<nsAtom> thirdDynamic = NS_Atomize(THIRD_ATOM_STR);

  EXPECT_FALSE(isStaticAtom(thirdDynamic));

  EXPECT_TRUE(thirdDynamic);
  EXPECT_EQ(NS_GetNumberOfAtoms(), count + 1);
}

static void AccessAtoms(void*) {
  for (int i = 0; i < 10000; i++) {
    RefPtr<nsAtom> atom = NS_Atomize(u"A Testing Atom");
  }
}

TEST(Atoms, ConcurrentAccessing)
{
  static const size_t kThreadCount = 4;
  // Force a GC before so that we don't have any unused atom.
  NS_GetNumberOfAtoms();
  EXPECT_EQ(NS_GetUnusedAtomCount(), int32_t(0));

  // Spawn PRThreads to do the concurrent atom access, to make sure we don't
  // spin the main thread event loop. Spinning the event loop may run a task
  // that uses an atom, leading to a false positive test failure.
  PRThread* threads[kThreadCount];
  for (size_t i = 0; i < kThreadCount; i++) {
    threads[i] = PR_CreateThread(PR_USER_THREAD, AccessAtoms, nullptr,
                                 PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                                 PR_JOINABLE_THREAD, 0);
    EXPECT_TRUE(threads[i]);
  }

  for (size_t i = 0; i < kThreadCount; i++) {
    EXPECT_EQ(PR_SUCCESS, PR_JoinThread(threads[i]));
  }

  // We should have one unused atom from this test.
  EXPECT_EQ(NS_GetUnusedAtomCount(), int32_t(1));
}

}  // namespace TestAtoms
