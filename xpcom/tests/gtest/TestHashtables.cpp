/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <functional>
#include <numeric>

#include "gtest/gtest.h"
#include "mozilla/Attributes.h"
#include "mozilla/ThreadSafeWeakPtr.h"
#include "nsBaseHashtable.h"
#include "nsCOMArray.h"
#include "nsCOMPtr.h"
#include "nsClassHashtable.h"
#include "nsIMemoryReporter.h"
#include "nsISupports.h"
#include "nsInterfaceHashtable.h"
#include "nsRefCountedHashtable.h"
#include "nsTHashMap.h"
#include "nsTHashtable.h"

using mozilla::MakeRefPtr;
using mozilla::SupportsThreadSafeWeakPtr;
using mozilla::ThreadSafeWeakPtr;
using mozilla::UniquePtr;

namespace TestHashtables {

class TestUniChar  // for nsClassHashtable
{
 public:
  explicit TestUniChar(uint32_t aWord) { mWord = aWord; }

  ~TestUniChar() = default;

  uint32_t GetChar() const { return mWord; }

 private:
  uint32_t mWord;
};

class TestUniCharDerived : public TestUniChar {
  using TestUniChar::TestUniChar;
};

class TestUniCharRefCounted  // for nsRefPtrHashtable
{
 public:
  explicit TestUniCharRefCounted(uint32_t aWord,
                                 uint32_t aExpectedAddRefCnt = 0)
      : mExpectedAddRefCnt(aExpectedAddRefCnt),
        mAddRefCnt(0),
        mRefCnt(0),
        mWord(aWord) {}

  uint32_t AddRef() {
    mRefCnt++;
    mAddRefCnt++;
    return mRefCnt;
  }

  uint32_t Release() {
    EXPECT_TRUE(mRefCnt > 0);
    mRefCnt--;
    if (mRefCnt == 0) {
      delete this;
      return 0;
    }
    return mRefCnt;
  }

  uint32_t GetChar() const { return mWord; }

 private:
  ~TestUniCharRefCounted() {
    if (mExpectedAddRefCnt > 0) {
      EXPECT_EQ(mAddRefCnt, mExpectedAddRefCnt);
    }
  }

  uint32_t mExpectedAddRefCnt;
  uint32_t mAddRefCnt;
  uint32_t mRefCnt;
  uint32_t mWord;
};

struct EntityNode {
  const char* mStr;  // never owns buffer
  uint32_t mUnicode;

  bool operator<(const EntityNode& aOther) const {
    return mUnicode < aOther.mUnicode ||
           (mUnicode == aOther.mUnicode && strcmp(mStr, aOther.mStr) < 0);
  }
};

static const EntityNode gEntities[] = {
    {"nbsp", 160},   {"iexcl", 161}, {"cent", 162},   {"pound", 163},
    {"curren", 164}, {"yen", 165},   {"brvbar", 166}, {"sect", 167},
    {"uml", 168},    {"copy", 169},  {"ordf", 170},   {"laquo", 171},
    {"not", 172},    {"shy", 173},   {"reg", 174},    {"macr", 175}};

#define ENTITY_COUNT (unsigned(sizeof(gEntities) / sizeof(EntityNode)))

class EntityToUnicodeEntry : public PLDHashEntryHdr {
 public:
  typedef const char* KeyType;
  typedef const char* KeyTypePointer;

  explicit EntityToUnicodeEntry(const char* /*aKey*/) { mNode = nullptr; }
  EntityToUnicodeEntry(const EntityToUnicodeEntry& aEntry) {
    mNode = aEntry.mNode;
  }
  ~EntityToUnicodeEntry() = default;

  bool KeyEquals(const char* aEntity) const {
    return !strcmp(mNode->mStr, aEntity);
  }
  static const char* KeyToPointer(const char* aEntity) { return aEntity; }
  static PLDHashNumber HashKey(const char* aEntity) {
    return mozilla::HashString(aEntity, strlen(aEntity));
  }
  enum { ALLOW_MEMMOVE = true };

  const EntityNode* mNode;
};

static uint32_t nsTIterPrint(nsTHashtable<EntityToUnicodeEntry>& hash) {
  uint32_t n = 0;
  for (auto iter = hash.Iter(); !iter.Done(); iter.Next()) {
    n++;
  }
  return n;
}

static uint32_t nsTIterPrintRemove(nsTHashtable<EntityToUnicodeEntry>& hash) {
  uint32_t n = 0;
  for (auto iter = hash.Iter(); !iter.Done(); iter.Next()) {
    iter.Remove();
    n++;
  }
  return n;
}

static void testTHashtable(nsTHashtable<EntityToUnicodeEntry>& hash,
                           uint32_t numEntries) {
  uint32_t i;
  for (i = 0; i < numEntries; ++i) {
    EntityToUnicodeEntry* entry = hash.PutEntry(gEntities[i].mStr);

    EXPECT_TRUE(entry);

    EXPECT_FALSE(entry->mNode);
    entry->mNode = &gEntities[i];
  }

  for (i = 0; i < numEntries; ++i) {
    EntityToUnicodeEntry* entry = hash.GetEntry(gEntities[i].mStr);

    EXPECT_TRUE(entry);
  }

  EntityToUnicodeEntry* entry = hash.GetEntry("xxxy");

  EXPECT_FALSE(entry);

  uint32_t count = nsTIterPrint(hash);
  EXPECT_EQ(count, numEntries);

  for (const auto& entry :
       const_cast<const nsTHashtable<EntityToUnicodeEntry>&>(hash)) {
    static_assert(std::is_same_v<decltype(entry), const EntityToUnicodeEntry&>);
  }
  for (auto& entry : hash) {
    static_assert(std::is_same_v<decltype(entry), EntityToUnicodeEntry&>);
  }

  EXPECT_EQ(numEntries == ENTITY_COUNT ? 6 : 0,
            std::count_if(hash.cbegin(), hash.cend(), [](const auto& entry) {
              return entry.mNode->mUnicode >= 170;
            }));
}

//
// all this nsIFoo stuff was copied wholesale from TestCOMPtr.cpp
//

#define NS_IFOO_IID \
  {0x6f7652e0, 0xee43, 0x11d1, {0x9c, 0xc3, 0x00, 0x60, 0x08, 0x8c, 0xa6, 0xb3}}

class IFoo final : public nsISupports {
 public:
  NS_INLINE_DECL_STATIC_IID(NS_IFOO_IID)

  IFoo();

  NS_IMETHOD_(MozExternalRefCountType) AddRef() override;
  NS_IMETHOD_(MozExternalRefCountType) Release() override;
  NS_IMETHOD QueryInterface(const nsIID&, void**) override;

  NS_IMETHOD SetString(const nsACString& /*in*/ aString);
  NS_IMETHOD GetString(nsACString& /*out*/ aString);

  static void print_totals();

 private:
  ~IFoo();

  unsigned int refcount_;

  static unsigned int total_constructions_;
  static unsigned int total_destructions_;
  nsCString mString;
};

unsigned int IFoo::total_constructions_;
unsigned int IFoo::total_destructions_;

void IFoo::print_totals() {}

IFoo::IFoo() : refcount_(0) { ++total_constructions_; }

IFoo::~IFoo() { ++total_destructions_; }

MozExternalRefCountType IFoo::AddRef() {
  ++refcount_;
  return refcount_;
}

MozExternalRefCountType IFoo::Release() {
  int newcount = --refcount_;
  if (newcount == 0) {
    delete this;
  }

  return newcount;
}

nsresult IFoo::QueryInterface(const nsIID& aIID, void** aResult) {
  nsISupports* rawPtr = nullptr;
  nsresult status = NS_OK;

  if (aIID.Equals(NS_GET_IID(IFoo)))
    rawPtr = this;
  else {
    nsID iid_of_ISupports = NS_ISUPPORTS_IID;
    if (aIID.Equals(iid_of_ISupports))
      rawPtr = static_cast<nsISupports*>(this);
    else
      status = NS_ERROR_NO_INTERFACE;
  }

  NS_IF_ADDREF(rawPtr);
  *aResult = rawPtr;

  return status;
}

nsresult IFoo::SetString(const nsACString& aString) {
  mString = aString;
  return NS_OK;
}

nsresult IFoo::GetString(nsACString& aString) {
  aString = mString;
  return NS_OK;
}

static nsresult CreateIFoo(IFoo** result)
// a typical factory function (that calls AddRef)
{
  auto* foop = new IFoo();

  foop->AddRef();
  *result = foop;

  return NS_OK;
}

class DefaultConstructible {
 public:
  // Allow default construction.
  DefaultConstructible() = default;

  // Construct/assign from a ref counted char.
  explicit DefaultConstructible(RefPtr<TestUniCharRefCounted> aChar)
      : mChar(std::move(aChar)) {}

  const RefPtr<TestUniCharRefCounted>& CharRef() const { return mChar; }

  // DefaultConstructible can be copied and moved.
  DefaultConstructible(const DefaultConstructible&) = default;
  DefaultConstructible& operator=(const DefaultConstructible&) = default;
  DefaultConstructible(DefaultConstructible&&) = default;
  DefaultConstructible& operator=(DefaultConstructible&&) = default;

 private:
  RefPtr<TestUniCharRefCounted> mChar;
};

class MovingNonDefaultConstructible;

class NonDefaultConstructible {
 public:
  // Construct/assign from a ref counted char.
  explicit NonDefaultConstructible(RefPtr<TestUniCharRefCounted> aChar)
      : mChar(std::move(aChar)) {}

  // Disallow default construction.
  NonDefaultConstructible() = delete;

  MOZ_IMPLICIT NonDefaultConstructible(MovingNonDefaultConstructible&& aOther);

  const RefPtr<TestUniCharRefCounted>& CharRef() const { return mChar; }

  // NonDefaultConstructible can be copied, but not trivially (efficiently)
  // moved.
  NonDefaultConstructible(const NonDefaultConstructible&) = default;
  NonDefaultConstructible& operator=(const NonDefaultConstructible&) = default;

 private:
  RefPtr<TestUniCharRefCounted> mChar;
};

class MovingNonDefaultConstructible {
 public:
  // Construct/assign from a ref counted char.
  explicit MovingNonDefaultConstructible(RefPtr<TestUniCharRefCounted> aChar)
      : mChar(std::move(aChar)) {}

  MovingNonDefaultConstructible() = delete;

  MOZ_IMPLICIT MovingNonDefaultConstructible(
      const NonDefaultConstructible& aSrc)
      : mChar(aSrc.CharRef()) {}

  RefPtr<TestUniCharRefCounted> unwrapChar() && { return std::move(mChar); }

  // MovingNonDefaultConstructible can be moved, but not copied.
  MovingNonDefaultConstructible(const MovingNonDefaultConstructible&) = delete;
  MovingNonDefaultConstructible& operator=(
      const MovingNonDefaultConstructible&) = delete;
  MovingNonDefaultConstructible(MovingNonDefaultConstructible&&) = default;
  MovingNonDefaultConstructible& operator=(MovingNonDefaultConstructible&&) =
      default;

 private:
  RefPtr<TestUniCharRefCounted> mChar;
};

NonDefaultConstructible::NonDefaultConstructible(
    MovingNonDefaultConstructible&& aOther)
    : mChar(std::move(aOther).unwrapChar()) {}

struct DefaultConstructible_DefaultConstructible {
  using DataType = DefaultConstructible;
  using UserDataType = DefaultConstructible;

  static constexpr uint32_t kExpectedAddRefCnt_Contains = 1;
  static constexpr uint32_t kExpectedAddRefCnt_GetGeneration = 1;
  static constexpr uint32_t kExpectedAddRefCnt_SizeOfExcludingThis = 1;
  static constexpr uint32_t kExpectedAddRefCnt_SizeOfIncludingThis = 1;
  static constexpr uint32_t kExpectedAddRefCnt_Count = 1;
  static constexpr uint32_t kExpectedAddRefCnt_IsEmpty = 1;
  static constexpr uint32_t kExpectedAddRefCnt_Get_OutputParam = 3;
  static constexpr uint32_t kExpectedAddRefCnt_Get = 3;
  static constexpr uint32_t kExpectedAddRefCnt_MaybeGet = 3;
  static constexpr uint32_t kExpectedAddRefCnt_LookupOrInsert = 1;
  static constexpr uint32_t kExpectedAddRefCnt_InsertOrUpdate = 1;
  static constexpr uint32_t kExpectedAddRefCnt_InsertOrUpdate_Fallible = 1;
  static constexpr uint32_t kExpectedAddRefCnt_InsertOrUpdate_Rvalue = 1;
  static constexpr uint32_t kExpectedAddRefCnt_InsertOrUpdate_Rvalue_Fallible =
      1;
  static constexpr uint32_t kExpectedAddRefCnt_Remove_OutputParam = 1;
  static constexpr uint32_t kExpectedAddRefCnt_Remove = 1;
  static constexpr uint32_t kExpectedAddRefCnt_Extract = 1;
  static constexpr uint32_t kExpectedAddRefCnt_RemoveIf = 1;
  static constexpr uint32_t kExpectedAddRefCnt_Lookup = 1;
  static constexpr uint32_t kExpectedAddRefCnt_Lookup_Remove = 1;
  static constexpr uint32_t kExpectedAddRefCnt_LookupForAdd = 1;
  static constexpr uint32_t kExpectedAddRefCnt_LookupForAdd_OrInsert = 1;
  static constexpr uint32_t kExpectedAddRefCnt_LookupForAdd_OrRemove = 1;
  static constexpr uint32_t kExpectedAddRefCnt_Iter = 1;
  static constexpr uint32_t kExpectedAddRefCnt_ConstIter = 1;
  static constexpr uint32_t kExpectedAddRefCnt_begin_end = 1;
  static constexpr uint32_t kExpectedAddRefCnt_cbegin_cend = 1;
  static constexpr uint32_t kExpectedAddRefCnt_Clear = 1;
  static constexpr uint32_t kExpectedAddRefCnt_ShallowSizeOfExcludingThis = 1;
  static constexpr uint32_t kExpectedAddRefCnt_ShallowSizeOfIncludingThis = 1;
  static constexpr uint32_t kExpectedAddRefCnt_SwapElements = 1;
  static constexpr uint32_t kExpectedAddRefCnt_MarkImmutable = 1;
};

struct NonDefaultConstructible_NonDefaultConstructible {
  using DataType = NonDefaultConstructible;
  using UserDataType = NonDefaultConstructible;

  static constexpr uint32_t kExpectedAddRefCnt_Contains = 2;
  static constexpr uint32_t kExpectedAddRefCnt_GetGeneration = 2;
  static constexpr uint32_t kExpectedAddRefCnt_SizeOfExcludingThis = 3;
  static constexpr uint32_t kExpectedAddRefCnt_SizeOfIncludingThis = 3;
  static constexpr uint32_t kExpectedAddRefCnt_Count = 2;
  static constexpr uint32_t kExpectedAddRefCnt_IsEmpty = 2;
  static constexpr uint32_t kExpectedAddRefCnt_Get_OutputParam = 5;
  static constexpr uint32_t kExpectedAddRefCnt_MaybeGet = 5;
  static constexpr uint32_t kExpectedAddRefCnt_InsertOrUpdate = 2;
  static constexpr uint32_t kExpectedAddRefCnt_InsertOrUpdate_Fallible = 2;
  static constexpr uint32_t kExpectedAddRefCnt_InsertOrUpdate_Rvalue = 2;
  static constexpr uint32_t kExpectedAddRefCnt_InsertOrUpdate_Rvalue_Fallible =
      2;
  static constexpr uint32_t kExpectedAddRefCnt_Remove = 2;
  static constexpr uint32_t kExpectedAddRefCnt_Extract = 3;
  static constexpr uint32_t kExpectedAddRefCnt_RemoveIf = 2;
  static constexpr uint32_t kExpectedAddRefCnt_Lookup = 2;
  static constexpr uint32_t kExpectedAddRefCnt_Lookup_Remove = 2;
  static constexpr uint32_t kExpectedAddRefCnt_Iter = 2;
  static constexpr uint32_t kExpectedAddRefCnt_ConstIter = 2;
  static constexpr uint32_t kExpectedAddRefCnt_begin_end = 2;
  static constexpr uint32_t kExpectedAddRefCnt_cbegin_cend = 2;
  static constexpr uint32_t kExpectedAddRefCnt_Clear = 2;
  static constexpr uint32_t kExpectedAddRefCnt_ShallowSizeOfExcludingThis = 2;
  static constexpr uint32_t kExpectedAddRefCnt_ShallowSizeOfIncludingThis = 2;
  static constexpr uint32_t kExpectedAddRefCnt_SwapElements = 2;
  static constexpr uint32_t kExpectedAddRefCnt_MarkImmutable = 2;
};

struct NonDefaultConstructible_MovingNonDefaultConstructible {
  using DataType = NonDefaultConstructible;
  using UserDataType = MovingNonDefaultConstructible;

  static constexpr uint32_t kExpectedAddRefCnt_Contains = 1;
  static constexpr uint32_t kExpectedAddRefCnt_GetGeneration = 1;
  static constexpr uint32_t kExpectedAddRefCnt_SizeOfExcludingThis = 1;
  static constexpr uint32_t kExpectedAddRefCnt_SizeOfIncludingThis = 1;
  static constexpr uint32_t kExpectedAddRefCnt_Count = 1;
  static constexpr uint32_t kExpectedAddRefCnt_IsEmpty = 1;
  static constexpr uint32_t kExpectedAddRefCnt_Get_OutputParam = 2;
  static constexpr uint32_t kExpectedAddRefCnt_MaybeGet = 2;
  static constexpr uint32_t kExpectedAddRefCnt_InsertOrUpdate = 1;
  static constexpr uint32_t kExpectedAddRefCnt_InsertOrUpdate_Fallible = 1;
  static constexpr uint32_t kExpectedAddRefCnt_InsertOrUpdate_Rvalue = 1;
  static constexpr uint32_t kExpectedAddRefCnt_InsertOrUpdate_Rvalue_Fallible =
      1;
  static constexpr uint32_t kExpectedAddRefCnt_Remove = 1;
  static constexpr uint32_t kExpectedAddRefCnt_Extract = 2;
  static constexpr uint32_t kExpectedAddRefCnt_RemoveIf = 1;
  static constexpr uint32_t kExpectedAddRefCnt_Lookup = 1;
  static constexpr uint32_t kExpectedAddRefCnt_Lookup_Remove = 1;
  static constexpr uint32_t kExpectedAddRefCnt_Iter = 1;
  static constexpr uint32_t kExpectedAddRefCnt_ConstIter = 1;
  static constexpr uint32_t kExpectedAddRefCnt_begin_end = 1;
  static constexpr uint32_t kExpectedAddRefCnt_cbegin_cend = 1;
  static constexpr uint32_t kExpectedAddRefCnt_Clear = 1;
  static constexpr uint32_t kExpectedAddRefCnt_ShallowSizeOfExcludingThis = 1;
  static constexpr uint32_t kExpectedAddRefCnt_ShallowSizeOfIncludingThis = 1;
  static constexpr uint32_t kExpectedAddRefCnt_SwapElements = 1;
  static constexpr uint32_t kExpectedAddRefCnt_MarkImmutable = 1;
};

template <bool flag = false>
void UnsupportedType() {
  static_assert(flag, "Unsupported type!");
}

class TypeNames {
 public:
  template <typename T>
  static std::string GetName(int) {
    if constexpr (std::is_same<T,
                               DefaultConstructible_DefaultConstructible>()) {
      return "DefaultConstructible_DefaultConstructible";
    } else if constexpr (
        std::is_same<T, NonDefaultConstructible_NonDefaultConstructible>()) {
      return "NonDefaultConstructible_NonDefaultConstructible";
    } else if constexpr (
        std::is_same<T,
                     NonDefaultConstructible_MovingNonDefaultConstructible>()) {
      return "NonDefaultConstructible_MovingNonDefaultConstructible";
    } else {
      UnsupportedType();
    }
  }
};

template <typename TypeParam>
auto MakeEmptyBaseHashtable() {
  nsBaseHashtable<nsUint64HashKey, typename TypeParam::DataType,
                  typename TypeParam::UserDataType>
      table;

  return table;
}

template <typename TypeParam>
auto MakeUniqueEmptyBaseHashtable() {
  return std::make_unique<
      nsBaseHashtable<nsUint64HashKey, typename TypeParam::DataType,
                      typename TypeParam::UserDataType>>();
}

template <typename TypeParam>
auto MakeBaseHashtable(const uint32_t aExpectedAddRefCnt) {
  auto table = MakeEmptyBaseHashtable<TypeParam>();

  auto myChar = MakeRefPtr<TestUniCharRefCounted>(42, aExpectedAddRefCnt);

  table.InsertOrUpdate(1, typename TypeParam::UserDataType(std::move(myChar)));

  return table;
}

template <typename TypeParam>
auto MakeUniqueBaseHashtable(const uint32_t aExpectedAddRefCnt) {
  auto table = MakeUniqueEmptyBaseHashtable<TypeParam>();

  auto myChar = MakeRefPtr<TestUniCharRefCounted>(42, aExpectedAddRefCnt);

  table->InsertOrUpdate(1, typename TypeParam::UserDataType(std::move(myChar)));

  return table;
}

template <typename TypeParam>
typename TypeParam::DataType GetDataFrom(
    typename TypeParam::UserDataType& aUserData) {
  if constexpr (std::is_same_v<TypeParam,
                               DefaultConstructible_DefaultConstructible> ||
                std::is_same_v<
                    TypeParam,
                    NonDefaultConstructible_NonDefaultConstructible>) {
    return aUserData;
  } else if constexpr (
      std::is_same_v<TypeParam,
                     NonDefaultConstructible_MovingNonDefaultConstructible>) {
    return std::move(aUserData);
  } else {
    UnsupportedType();
  }
}

template <typename TypeParam>
typename TypeParam::DataType GetDataFrom(
    mozilla::Maybe<typename TypeParam::UserDataType>& aMaybeUserData) {
  return GetDataFrom<TypeParam>(*aMaybeUserData);
}

}  // namespace TestHashtables

using namespace TestHashtables;

TEST(Hashtable, THashtable)
{
  // check an nsTHashtable
  nsTHashtable<EntityToUnicodeEntry> EntityToUnicode(ENTITY_COUNT);

  testTHashtable(EntityToUnicode, 5);

  uint32_t count = nsTIterPrintRemove(EntityToUnicode);
  ASSERT_EQ(count, uint32_t(5));

  count = nsTIterPrint(EntityToUnicode);
  ASSERT_EQ(count, uint32_t(0));

  testTHashtable(EntityToUnicode, ENTITY_COUNT);

  EntityToUnicode.Clear();

  count = nsTIterPrint(EntityToUnicode);
  ASSERT_EQ(count, uint32_t(0));
}

TEST(Hashtable, PtrHashtable)
{
  nsTHashtable<nsPtrHashKey<int>> hash;

  for (const auto& entry :
       const_cast<const nsTHashtable<nsPtrHashKey<int>>&>(hash)) {
    static_assert(std::is_same_v<decltype(entry), const nsPtrHashKey<int>&>);
  }
  for (auto& entry : hash) {
    static_assert(std::is_same_v<decltype(entry), nsPtrHashKey<int>&>);
  }
}

TEST(Hashtable, Move)
{
  const void* kPtr = reinterpret_cast<void*>(static_cast<uintptr_t>(0xbadc0de));

  nsTHashtable<nsPtrHashKey<const void>> table;
  table.PutEntry(kPtr);

  nsTHashtable<nsPtrHashKey<const void>> moved = std::move(table);
  ASSERT_EQ(table.Count(), 0u);
  ASSERT_EQ(moved.Count(), 1u);

  EXPECT_TRUE(moved.Contains(kPtr));
  EXPECT_FALSE(table.Contains(kPtr));
}

TEST(Hashtable, Keys)
{
  static constexpr uint64_t count = 10;

  nsTHashtable<nsUint64HashKey> table;
  for (uint64_t i = 0; i < count; i++) {
    table.PutEntry(i);
  }

  nsTArray<uint64_t> keys;
  for (const uint64_t& key : table.Keys()) {
    keys.AppendElement(key);
  }
  keys.Sort();

  EXPECT_EQ((nsTArray<uint64_t>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}), keys);
}

template <typename TypeParam>
class BaseHashtableTest : public ::testing::Test {};

TYPED_TEST_SUITE_P(BaseHashtableTest);

TYPED_TEST_P(BaseHashtableTest, Contains) {
  auto table =
      MakeBaseHashtable<TypeParam>(TypeParam::kExpectedAddRefCnt_Contains);

  auto res = table.Contains(1);
  EXPECT_TRUE(res);
}

TYPED_TEST_P(BaseHashtableTest, GetGeneration) {
  auto table =
      MakeBaseHashtable<TypeParam>(TypeParam::kExpectedAddRefCnt_GetGeneration);

  auto res = table.GetGeneration();
  EXPECT_GT(res, 0u);
}

MOZ_DEFINE_MALLOC_SIZE_OF(MallocSizeOf);

TYPED_TEST_P(BaseHashtableTest, SizeOfExcludingThis) {
  // This doesn't compile at the moment, since nsBaseHashtableET lacks
  // SizeOfExcludingThis implementation. Bug 1689214.
#if 0
  auto table = MakeBaseHashtable<TypeParam>(
      TypeParam::kExpectedAddRefCnt_SizeOfExcludingThis);

  auto res = table.SizeOfExcludingThis(MallocSizeOf);
  EXPECT_GT(res, 0u);
#endif
}

TYPED_TEST_P(BaseHashtableTest, SizeOfIncludingThis) {
  // This doesn't compile at the moment, since nsBaseHashtableET lacks
  // SizeOfIncludingThis implementation. Bug 1689214.
#if 0
  auto table = MakeUniqueBaseHashtable<TypeParam>(
      TypeParam::kExpectedAddRefCnt_SizeOfIncludingThis);

  auto res = table->SizeOfIncludingThis(MallocSizeOf);
  EXPECT_GT(res, 0u);
#endif
}

TYPED_TEST_P(BaseHashtableTest, Count) {
  auto table =
      MakeBaseHashtable<TypeParam>(TypeParam::kExpectedAddRefCnt_Count);

  auto res = table.Count();
  EXPECT_EQ(res, 1u);
}

TYPED_TEST_P(BaseHashtableTest, IsEmpty) {
  auto table =
      MakeBaseHashtable<TypeParam>(TypeParam::kExpectedAddRefCnt_IsEmpty);

  auto res = table.IsEmpty();
  EXPECT_EQ(res, false);
}

TYPED_TEST_P(BaseHashtableTest, Get_OutputParam) {
  auto table = MakeBaseHashtable<TypeParam>(
      TypeParam::kExpectedAddRefCnt_Get_OutputParam);

  typename TypeParam::UserDataType userData(nullptr);
  auto res = table.Get(1, &userData);
  EXPECT_TRUE(res);

  auto data = GetDataFrom<TypeParam>(userData);
  EXPECT_EQ(data.CharRef()->GetChar(), 42u);
}

TYPED_TEST_P(BaseHashtableTest, Get) {
  // The Get overload can't support non-default-constructible UserDataType.
  if constexpr (std::is_default_constructible_v<
                    typename TypeParam::UserDataType>) {
    auto table =
        MakeBaseHashtable<TypeParam>(TypeParam::kExpectedAddRefCnt_Get);

    auto userData = table.Get(1);

    auto data = GetDataFrom<TypeParam>(userData);
    EXPECT_EQ(data.CharRef()->GetChar(), 42u);
  }
}

TYPED_TEST_P(BaseHashtableTest, MaybeGet) {
  auto table =
      MakeBaseHashtable<TypeParam>(TypeParam::kExpectedAddRefCnt_MaybeGet);

  auto maybeUserData = table.MaybeGet(1);
  EXPECT_TRUE(maybeUserData);

  auto data = GetDataFrom<TypeParam>(maybeUserData);
  EXPECT_EQ(data.CharRef()->GetChar(), 42u);
}

TYPED_TEST_P(BaseHashtableTest, LookupOrInsert_Default) {
  if constexpr (std::is_default_constructible_v<typename TypeParam::DataType>) {
    auto table = MakeEmptyBaseHashtable<TypeParam>();

    typename TypeParam::DataType& data = table.LookupOrInsert(1);
    EXPECT_EQ(data.CharRef(), nullptr);

    data = typename TypeParam::DataType(MakeRefPtr<TestUniCharRefCounted>(
        42, TypeParam::kExpectedAddRefCnt_LookupOrInsert));
  }
}

TYPED_TEST_P(BaseHashtableTest, LookupOrInsert_NonDefault) {
  auto table = MakeEmptyBaseHashtable<TypeParam>();

  typename TypeParam::DataType& data = table.LookupOrInsert(
      1, typename TypeParam::DataType{MakeRefPtr<TestUniCharRefCounted>(42)});
  EXPECT_NE(data.CharRef(), nullptr);
}

TYPED_TEST_P(BaseHashtableTest, LookupOrInsert_NonDefault_AlreadyPresent) {
  auto table = MakeEmptyBaseHashtable<TypeParam>();

  typename TypeParam::DataType& data1 = table.LookupOrInsert(
      1, typename TypeParam::DataType{MakeRefPtr<TestUniCharRefCounted>(42)});
  TestUniCharRefCounted* const address = data1.CharRef();
  typename TypeParam::DataType& data2 = table.LookupOrInsert(
      1,
      typename TypeParam::DataType{MakeRefPtr<TestUniCharRefCounted>(42, 1)});
  EXPECT_EQ(&data1, &data2);
  EXPECT_EQ(address, data2.CharRef());
}

TYPED_TEST_P(BaseHashtableTest, LookupOrInsertWith) {
  auto table = MakeEmptyBaseHashtable<TypeParam>();

  typename TypeParam::DataType& data = table.LookupOrInsertWith(1, [] {
    return typename TypeParam::DataType{MakeRefPtr<TestUniCharRefCounted>(42)};
  });
  EXPECT_NE(data.CharRef(), nullptr);
}

TYPED_TEST_P(BaseHashtableTest, LookupOrInsertWith_AlreadyPresent) {
  auto table = MakeEmptyBaseHashtable<TypeParam>();

  table.LookupOrInsertWith(1, [] {
    return typename TypeParam::DataType{MakeRefPtr<TestUniCharRefCounted>(42)};
  });
  table.LookupOrInsertWith(1, [] {
    ADD_FAILURE();
    return typename TypeParam::DataType{MakeRefPtr<TestUniCharRefCounted>(42)};
  });
}

TYPED_TEST_P(BaseHashtableTest, InsertOrUpdate) {
  auto table = MakeEmptyBaseHashtable<TypeParam>();

  auto myChar = MakeRefPtr<TestUniCharRefCounted>(
      42, TypeParam::kExpectedAddRefCnt_InsertOrUpdate);

  table.InsertOrUpdate(1, typename TypeParam::UserDataType(std::move(myChar)));
}

TYPED_TEST_P(BaseHashtableTest, InsertOrUpdate_Fallible) {
  auto table = MakeEmptyBaseHashtable<TypeParam>();

  auto myChar = MakeRefPtr<TestUniCharRefCounted>(
      42, TypeParam::kExpectedAddRefCnt_InsertOrUpdate_Fallible);

  auto res = table.InsertOrUpdate(
      1, typename TypeParam::UserDataType(std::move(myChar)),
      mozilla::fallible);
  EXPECT_TRUE(res);
}

TYPED_TEST_P(BaseHashtableTest, InsertOrUpdate_Rvalue) {
  auto table = MakeEmptyBaseHashtable<TypeParam>();

  auto myChar = MakeRefPtr<TestUniCharRefCounted>(
      42, TypeParam::kExpectedAddRefCnt_InsertOrUpdate_Rvalue);

  table.InsertOrUpdate(
      1, std::move(typename TypeParam::UserDataType(std::move(myChar))));
}

TYPED_TEST_P(BaseHashtableTest, InsertOrUpdate_Rvalue_Fallible) {
  auto table = MakeEmptyBaseHashtable<TypeParam>();

  auto myChar = MakeRefPtr<TestUniCharRefCounted>(
      42, TypeParam::kExpectedAddRefCnt_InsertOrUpdate_Rvalue_Fallible);

  auto res = table.InsertOrUpdate(
      1, std::move(typename TypeParam::UserDataType(std::move(myChar))),
      mozilla::fallible);
  EXPECT_TRUE(res);
}

TYPED_TEST_P(BaseHashtableTest, Remove_OutputParam) {
  // The Remove overload can't support non-default-constructible DataType.
  if constexpr (std::is_default_constructible_v<typename TypeParam::DataType>) {
    auto table = MakeBaseHashtable<TypeParam>(
        TypeParam::kExpectedAddRefCnt_Remove_OutputParam);

    typename TypeParam::DataType data;
    auto res = table.Remove(1, &data);
    EXPECT_TRUE(res);
    EXPECT_EQ(data.CharRef()->GetChar(), 42u);
  }
}

TYPED_TEST_P(BaseHashtableTest, Remove) {
  auto table =
      MakeBaseHashtable<TypeParam>(TypeParam::kExpectedAddRefCnt_Remove);

  auto res = table.Remove(1);
  EXPECT_TRUE(res);
}

TYPED_TEST_P(BaseHashtableTest, Extract) {
  auto table =
      MakeBaseHashtable<TypeParam>(TypeParam::kExpectedAddRefCnt_Extract);

  auto maybeData = table.Extract(1);
  EXPECT_TRUE(maybeData);
  EXPECT_EQ(maybeData->CharRef()->GetChar(), 42u);
}

TYPED_TEST_P(BaseHashtableTest, RemoveIf) {
  auto table =
      MakeBaseHashtable<TypeParam>(TypeParam::kExpectedAddRefCnt_RemoveIf);

  table.RemoveIf([](const auto&) { return true; });
}

TYPED_TEST_P(BaseHashtableTest, Lookup) {
  auto table =
      MakeBaseHashtable<TypeParam>(TypeParam::kExpectedAddRefCnt_Lookup);

  auto res = table.Lookup(1);
  EXPECT_TRUE(res);
  EXPECT_EQ(res.Data().CharRef()->GetChar(), 42u);
}

TYPED_TEST_P(BaseHashtableTest, Lookup_Remove) {
  auto table =
      MakeBaseHashtable<TypeParam>(TypeParam::kExpectedAddRefCnt_Lookup_Remove);

  auto res = table.Lookup(1);
  EXPECT_TRUE(res);
  EXPECT_EQ(res.Data().CharRef()->GetChar(), 42u);

  res.Remove();
}

TYPED_TEST_P(BaseHashtableTest, WithEntryHandle_NoOp) {
  auto table = MakeEmptyBaseHashtable<TypeParam>();

  table.WithEntryHandle(1, [](auto&&) {});

  EXPECT_FALSE(table.Contains(1));
}

TYPED_TEST_P(BaseHashtableTest, WithEntryHandle_NotFound_OrInsert) {
  auto table = MakeEmptyBaseHashtable<TypeParam>();

  table.WithEntryHandle(1, [](auto&& entry) {
    entry.OrInsert(typename TypeParam::UserDataType(
        MakeRefPtr<TestUniCharRefCounted>(42)));
  });

  EXPECT_TRUE(table.Contains(1));
}

TYPED_TEST_P(BaseHashtableTest, WithEntryHandle_NotFound_OrInsertFrom) {
  auto table = MakeEmptyBaseHashtable<TypeParam>();

  table.WithEntryHandle(1, [](auto&& entry) {
    entry.OrInsertWith([] {
      return typename TypeParam::UserDataType(
          MakeRefPtr<TestUniCharRefCounted>(42));
    });
  });

  EXPECT_TRUE(table.Contains(1));
}

TYPED_TEST_P(BaseHashtableTest, WithEntryHandle_NotFound_OrInsertFrom_Exists) {
  auto table = MakeEmptyBaseHashtable<TypeParam>();

  table.WithEntryHandle(1, [](auto&& entry) {
    entry.OrInsertWith([] {
      return typename TypeParam::UserDataType(
          MakeRefPtr<TestUniCharRefCounted>(42));
    });
  });
  table.WithEntryHandle(1, [](auto&& entry) {
    entry.OrInsertWith([]() -> typename TypeParam::UserDataType {
      ADD_FAILURE();
      return typename TypeParam::UserDataType(
          MakeRefPtr<TestUniCharRefCounted>(42));
    });
  });

  EXPECT_TRUE(table.Contains(1));
}

TYPED_TEST_P(BaseHashtableTest, WithEntryHandle_NotFound_OrRemove) {
  auto table = MakeEmptyBaseHashtable<TypeParam>();

  table.WithEntryHandle(1, [](auto&& entry) { entry.OrRemove(); });

  EXPECT_FALSE(table.Contains(1));
}

TYPED_TEST_P(BaseHashtableTest, WithEntryHandle_NotFound_OrRemove_Exists) {
  auto table = MakeEmptyBaseHashtable<TypeParam>();

  table.WithEntryHandle(1, [](auto&& entry) {
    entry.OrInsertWith([] {
      return typename TypeParam::UserDataType(
          MakeRefPtr<TestUniCharRefCounted>(42));
    });
  });
  table.WithEntryHandle(1, [](auto&& entry) { entry.OrRemove(); });

  EXPECT_FALSE(table.Contains(1));
}

TYPED_TEST_P(BaseHashtableTest, Iter) {
  auto table = MakeBaseHashtable<TypeParam>(TypeParam::kExpectedAddRefCnt_Iter);

  for (auto iter = table.Iter(); !iter.Done(); iter.Next()) {
    EXPECT_EQ(iter.Data().CharRef()->GetChar(), 42u);
  }
}

TYPED_TEST_P(BaseHashtableTest, ConstIter) {
  auto table =
      MakeBaseHashtable<TypeParam>(TypeParam::kExpectedAddRefCnt_ConstIter);

  for (auto iter = table.ConstIter(); !iter.Done(); iter.Next()) {
    EXPECT_EQ(iter.Data().CharRef()->GetChar(), 42u);
  }
}

TYPED_TEST_P(BaseHashtableTest, begin_end) {
  auto table =
      MakeBaseHashtable<TypeParam>(TypeParam::kExpectedAddRefCnt_begin_end);

  auto res = std::count_if(table.begin(), table.end(), [](const auto& entry) {
    return entry.GetData().CharRef()->GetChar() == 42;
  });
  EXPECT_EQ(res, 1);
}

TYPED_TEST_P(BaseHashtableTest, cbegin_cend) {
  auto table =
      MakeBaseHashtable<TypeParam>(TypeParam::kExpectedAddRefCnt_cbegin_cend);

  auto res = std::count_if(table.cbegin(), table.cend(), [](const auto& entry) {
    return entry.GetData().CharRef()->GetChar() == 42;
  });
  EXPECT_EQ(res, 1);
}

TYPED_TEST_P(BaseHashtableTest, Clear) {
  auto table =
      MakeBaseHashtable<TypeParam>(TypeParam::kExpectedAddRefCnt_Clear);

  table.Clear();
}

TYPED_TEST_P(BaseHashtableTest, ShallowSizeOfExcludingThis) {
  auto table = MakeBaseHashtable<TypeParam>(
      TypeParam::kExpectedAddRefCnt_ShallowSizeOfExcludingThis);

  auto res = table.ShallowSizeOfExcludingThis(MallocSizeOf);
  EXPECT_GT(res, 0u);
}

TYPED_TEST_P(BaseHashtableTest, ShallowSizeOfIncludingThis) {
  auto table = MakeUniqueBaseHashtable<TypeParam>(
      TypeParam::kExpectedAddRefCnt_ShallowSizeOfIncludingThis);

  auto res = table->ShallowSizeOfIncludingThis(MallocSizeOf);
  EXPECT_GT(res, 0u);
}

TYPED_TEST_P(BaseHashtableTest, SwapElements) {
  auto table =
      MakeBaseHashtable<TypeParam>(TypeParam::kExpectedAddRefCnt_SwapElements);

  auto table2 = MakeEmptyBaseHashtable<TypeParam>();

  table.SwapElements(table2);
}

TYPED_TEST_P(BaseHashtableTest, MarkImmutable) {
  auto table =
      MakeBaseHashtable<TypeParam>(TypeParam::kExpectedAddRefCnt_MarkImmutable);

  table.MarkImmutable();
}

REGISTER_TYPED_TEST_SUITE_P(
    BaseHashtableTest, Contains, GetGeneration, SizeOfExcludingThis,
    SizeOfIncludingThis, Count, IsEmpty, Get_OutputParam, Get, MaybeGet,
    LookupOrInsert_Default, LookupOrInsert_NonDefault,
    LookupOrInsert_NonDefault_AlreadyPresent, LookupOrInsertWith,
    LookupOrInsertWith_AlreadyPresent, InsertOrUpdate, InsertOrUpdate_Fallible,
    InsertOrUpdate_Rvalue, InsertOrUpdate_Rvalue_Fallible, Remove_OutputParam,
    Remove, Extract, RemoveIf, Lookup, Lookup_Remove, WithEntryHandle_NoOp,
    WithEntryHandle_NotFound_OrInsert, WithEntryHandle_NotFound_OrInsertFrom,
    WithEntryHandle_NotFound_OrInsertFrom_Exists,
    WithEntryHandle_NotFound_OrRemove, WithEntryHandle_NotFound_OrRemove_Exists,
    Iter, ConstIter, begin_end, cbegin_cend, Clear, ShallowSizeOfExcludingThis,
    ShallowSizeOfIncludingThis, SwapElements, MarkImmutable);

using BaseHashtableTestTypes =
    ::testing::Types<DefaultConstructible_DefaultConstructible,
                     NonDefaultConstructible_NonDefaultConstructible,
                     NonDefaultConstructible_MovingNonDefaultConstructible>;

INSTANTIATE_TYPED_TEST_SUITE_P(Hashtables, BaseHashtableTest,
                               BaseHashtableTestTypes, TypeNames);

TEST(Hashtables, DataHashtable)
{
  // check a data-hashtable
  nsTHashMap<nsUint32HashKey, const char*> UniToEntity(ENTITY_COUNT);

  for (auto& entity : gEntities) {
    UniToEntity.InsertOrUpdate(entity.mUnicode, entity.mStr);
  }

  const char* str;

  for (auto& entity : gEntities) {
    ASSERT_TRUE(UniToEntity.Get(entity.mUnicode, &str));
  }

  ASSERT_FALSE(UniToEntity.Get(99446, &str));

  uint32_t count = 0;
  for (auto iter = UniToEntity.Iter(); !iter.Done(); iter.Next()) {
    count++;
  }
  ASSERT_EQ(count, ENTITY_COUNT);

  UniToEntity.Clear();

  count = 0;
  for (auto iter = UniToEntity.Iter(); !iter.Done(); iter.Next()) {
    printf("  enumerated %u = \"%s\"\n", iter.Key(), iter.Data());
    count++;
  }
  ASSERT_EQ(count, uint32_t(0));
}

TEST(Hashtables, DataHashtable_STLIterators)
{
  nsTHashMap<nsUint32HashKey, const char*> UniToEntity(ENTITY_COUNT);

  for (auto& entity : gEntities) {
    UniToEntity.InsertOrUpdate(entity.mUnicode, entity.mStr);
  }

  // operators, including conversion from iterator to const_iterator
  nsTHashMap<nsUint32HashKey, const char*>::const_iterator ci =
      UniToEntity.begin();
  ++ci;
  ASSERT_EQ(1, std::distance(UniToEntity.cbegin(), ci++));
  ASSERT_EQ(2, std::distance(UniToEntity.cbegin(), ci));
  ASSERT_TRUE(ci == ci);
  auto otherCi = ci;
  ++otherCi;
  ++ci;
  ASSERT_TRUE(&*ci == &*otherCi);

  // STL algorithms (just to check that the iterator sufficiently conforms
  // with the actual syntactical requirements of those algorithms).
  std::for_each(UniToEntity.cbegin(), UniToEntity.cend(),
                [](const auto& entry) {});
  (void)std::find_if(UniToEntity.cbegin(), UniToEntity.cend(),
                     [](const auto& entry) { return entry.GetKey() == 42; });
  (void)std::accumulate(
      UniToEntity.cbegin(), UniToEntity.cend(), 0u,
      [](size_t sum, const auto& entry) { return sum + entry.GetKey(); });
  (void)std::any_of(UniToEntity.cbegin(), UniToEntity.cend(),
                    [](const auto& entry) { return entry.GetKey() == 42; });
  (void)std::max_element(UniToEntity.cbegin(), UniToEntity.cend(),
                         [](const auto& lhs, const auto& rhs) {
                           return lhs.GetKey() > rhs.GetKey();
                         });

  // const range-based for
  {
    std::set<EntityNode> entities(gEntities, gEntities + ENTITY_COUNT);
    for (const auto& entity :
         const_cast<const nsTHashMap<nsUint32HashKey, const char*>&>(
             UniToEntity)) {
      ASSERT_EQ(1u,
                entities.erase(EntityNode{entity.GetData(), entity.GetKey()}));
    }
    ASSERT_TRUE(entities.empty());
  }

  // non-const range-based for
  {
    std::set<EntityNode> entities(gEntities, gEntities + ENTITY_COUNT);
    for (auto& entity : UniToEntity) {
      ASSERT_EQ(1u,
                entities.erase(EntityNode{entity.GetData(), entity.GetKey()}));

      entity.SetData(nullptr);
      ASSERT_EQ(nullptr, entity.GetData());
    }
    ASSERT_TRUE(entities.empty());
  }
}

TEST(Hashtables, DataHashtable_RemoveIf)
{
  // check a data-hashtable
  nsTHashMap<nsUint32HashKey, const char*> UniToEntity(ENTITY_COUNT);

  for (auto& entity : gEntities) {
    UniToEntity.InsertOrUpdate(entity.mUnicode, entity.mStr);
  }

  UniToEntity.RemoveIf([](const auto& iter) { return iter.Key() >= 170; });

  ASSERT_EQ(10u, UniToEntity.Count());
}

TEST(Hashtables, ClassHashtable)
{
  // check a class-hashtable
  nsClassHashtable<nsCStringHashKey, TestUniChar> EntToUniClass(ENTITY_COUNT);

  for (auto& entity : gEntities) {
    // Insert a sub-class of TestUniChar to test if this is accepted by
    // InsertOrUpdate.
    EntToUniClass.InsertOrUpdate(
        nsDependentCString(entity.mStr),
        std::make_unique<TestUniCharDerived>(entity.mUnicode));
  }

  TestUniChar* myChar;

  for (auto& entity : gEntities) {
    ASSERT_TRUE(EntToUniClass.Get(nsDependentCString(entity.mStr), &myChar));
  }

  ASSERT_FALSE(EntToUniClass.Get("xxxx"_ns, &myChar));

  uint32_t count = 0;
  for (auto iter = EntToUniClass.Iter(); !iter.Done(); iter.Next()) {
    count++;
  }
  ASSERT_EQ(count, ENTITY_COUNT);

  EntToUniClass.Clear();

  count = 0;
  for (auto iter = EntToUniClass.Iter(); !iter.Done(); iter.Next()) {
    count++;
  }
  ASSERT_EQ(count, uint32_t(0));
}

TEST(Hashtables, ClassHashtable_RangeBasedFor)
{
  // check a class-hashtable
  nsClassHashtable<nsCStringHashKey, TestUniChar> EntToUniClass(ENTITY_COUNT);

  for (auto& entity : gEntities) {
    EntToUniClass.InsertOrUpdate(
        nsDependentCString(entity.mStr),
        std::make_unique<TestUniChar>(entity.mUnicode));
  }

  // const range-based for
  {
    std::set<EntityNode> entities(gEntities, gEntities + ENTITY_COUNT);
    for (const auto& entity :
         const_cast<const nsClassHashtable<nsCStringHashKey, TestUniChar>&>(
             EntToUniClass)) {
      const char* str;
      entity.GetKey().GetData(&str);
      ASSERT_EQ(1u,
                entities.erase(EntityNode{str, entity.GetData()->GetChar()}));
    }
    ASSERT_TRUE(entities.empty());
  }

  // non-const range-based for
  {
    std::set<EntityNode> entities(gEntities, gEntities + ENTITY_COUNT);
    for (auto& entity : EntToUniClass) {
      const char* str;
      entity.GetKey().GetData(&str);
      ASSERT_EQ(1u,
                entities.erase(EntityNode{str, entity.GetData()->GetChar()}));

      entity.SetData(UniquePtr<TestUniChar>{});
      ASSERT_EQ(nullptr, entity.GetData());
    }
    ASSERT_TRUE(entities.empty());
  }
}

TEST(Hashtables, DataHashtableWithInterfaceKey)
{
  // check a data-hashtable with an interface key
  nsTHashMap<nsISupportsHashKey, uint32_t> EntToUniClass2(ENTITY_COUNT);

  nsCOMArray<IFoo> fooArray;

  for (uint32_t i = 0; i < ENTITY_COUNT; ++i) {
    nsCOMPtr<IFoo> foo;
    CreateIFoo(getter_AddRefs(foo));
    foo->SetString(nsDependentCString(gEntities[i].mStr));

    fooArray.InsertObjectAt(foo, i);

    EntToUniClass2.InsertOrUpdate(foo, gEntities[i].mUnicode);
  }

  uint32_t myChar2;

  for (uint32_t i = 0; i < ENTITY_COUNT; ++i) {
    ASSERT_TRUE(EntToUniClass2.Get(fooArray[i], &myChar2));
  }

  ASSERT_FALSE(EntToUniClass2.Get((nsISupports*)0x55443316, &myChar2));

  uint32_t count = 0;
  for (auto iter = EntToUniClass2.Iter(); !iter.Done(); iter.Next()) {
    nsAutoCString s;
    nsCOMPtr<IFoo> foo = do_QueryInterface(iter.Key());
    foo->GetString(s);
    count++;
  }
  ASSERT_EQ(count, ENTITY_COUNT);

  EntToUniClass2.Clear();

  count = 0;
  for (auto iter = EntToUniClass2.Iter(); !iter.Done(); iter.Next()) {
    nsAutoCString s;
    nsCOMPtr<IFoo> foo = do_QueryInterface(iter.Key());
    foo->GetString(s);
    count++;
  }
  ASSERT_EQ(count, uint32_t(0));
}

TEST(Hashtables, InterfaceHashtable)
{
  // check an interface-hashtable with an uint32_t key
  nsInterfaceHashtable<nsUint32HashKey, IFoo> UniToEntClass2(ENTITY_COUNT);

  for (auto& entity : gEntities) {
    nsCOMPtr<IFoo> foo;
    CreateIFoo(getter_AddRefs(foo));
    foo->SetString(nsDependentCString(entity.mStr));

    UniToEntClass2.InsertOrUpdate(entity.mUnicode, foo);
  }

  for (auto& entity : gEntities) {
    nsCOMPtr<IFoo> myEnt;
    ASSERT_TRUE(UniToEntClass2.Get(entity.mUnicode, getter_AddRefs(myEnt)));

    nsAutoCString myEntStr;
    myEnt->GetString(myEntStr);
  }

  nsCOMPtr<IFoo> myEnt;
  ASSERT_FALSE(UniToEntClass2.Get(9462, getter_AddRefs(myEnt)));

  uint32_t count = 0;
  for (auto iter = UniToEntClass2.Iter(); !iter.Done(); iter.Next()) {
    nsAutoCString s;
    iter.UserData()->GetString(s);
    count++;
  }
  ASSERT_EQ(count, ENTITY_COUNT);

  UniToEntClass2.Clear();

  count = 0;
  for (auto iter = UniToEntClass2.Iter(); !iter.Done(); iter.Next()) {
    nsAutoCString s;
    iter.Data()->GetString(s);
    count++;
  }
  ASSERT_EQ(count, uint32_t(0));
}

TEST(Hashtables, DataHashtable_WithEntryHandle)
{
  // check WithEntryHandle/OrInsertWith
  nsTHashMap<nsUint32HashKey, const char*> UniToEntity(ENTITY_COUNT);

  for (auto& entity : gEntities) {
    UniToEntity.WithEntryHandle(entity.mUnicode, [&entity](auto&& entry) {
      EXPECT_FALSE(entry);
      const char* const val =
          entry.OrInsertWith([&entity]() { return entity.mStr; });
      EXPECT_TRUE(entry);
      EXPECT_TRUE(val == entity.mStr);
      EXPECT_TRUE(entry.Data() == entity.mStr);
    });
  }

  for (auto& entity : gEntities) {
    UniToEntity.WithEntryHandle(entity.mUnicode,
                                [](auto&& entry) { EXPECT_TRUE(entry); });
  }

  // 0 should not be found
  size_t count = UniToEntity.Count();
  UniToEntity.Lookup(0U).Remove();
  ASSERT_TRUE(count == UniToEntity.Count());

  // Lookup should find all entries
  count = 0;
  for (auto& entity : gEntities) {
    if (UniToEntity.Lookup(entity.mUnicode)) {
      count++;
    }
  }
  ASSERT_TRUE(count == UniToEntity.Count());

  for (auto& entity : gEntities) {
    UniToEntity.WithEntryHandle(entity.mUnicode,
                                [](auto&& entry) { EXPECT_TRUE(entry); });
  }

  // Lookup().Remove() should remove all entries.
  for (auto& entity : gEntities) {
    if (auto entry = UniToEntity.Lookup(entity.mUnicode)) {
      entry.Remove();
    }
  }
  ASSERT_TRUE(0 == UniToEntity.Count());

  // Remove newly added entries via OrRemove.
  for (auto& entity : gEntities) {
    UniToEntity.WithEntryHandle(entity.mUnicode, [](auto&& entry) {
      EXPECT_FALSE(entry);
      entry.OrRemove();
    });
  }
  ASSERT_TRUE(0 == UniToEntity.Count());

  // Remove existing entries via OrRemove.
  for (auto& entity : gEntities) {
    UniToEntity.WithEntryHandle(entity.mUnicode, [&entity](auto&& entry) {
      EXPECT_FALSE(entry);
      const char* const val = entry.OrInsert(entity.mStr);
      EXPECT_TRUE(entry);
      EXPECT_TRUE(val == entity.mStr);
      EXPECT_TRUE(entry.Data() == entity.mStr);
    });

    UniToEntity.WithEntryHandle(entity.mUnicode, [](auto&& entry) {
      EXPECT_TRUE(entry);
      entry.OrRemove();
    });
  }
  ASSERT_TRUE(0 == UniToEntity.Count());
}

TEST(Hashtables, ClassHashtable_WithEntryHandle)
{
  // check a class-hashtable WithEntryHandle with null values
  nsClassHashtable<nsCStringHashKey, TestUniChar> EntToUniClass(ENTITY_COUNT);

  for (auto& entity : gEntities) {
    EntToUniClass.WithEntryHandle(
        nsDependentCString(entity.mStr), [](auto&& entry) {
          EXPECT_FALSE(entry);
          const TestUniChar* val = entry.OrInsert(nullptr).get();
          EXPECT_TRUE(entry);
          EXPECT_TRUE(val == nullptr);
          EXPECT_TRUE(entry.Data() == nullptr);
        });
  }

  for (auto& entity : gEntities) {
    EntToUniClass.WithEntryHandle(nsDependentCString(entity.mStr),
                                  [](auto&& entry) { EXPECT_TRUE(entry); });
    EntToUniClass.WithEntryHandle(
        nsDependentCString(entity.mStr),
        [](auto&& entry) { EXPECT_TRUE(entry.Data() == nullptr); });
  }

  // "" should not be found
  size_t count = EntToUniClass.Count();
  EntToUniClass.Lookup(nsDependentCString("")).Remove();
  ASSERT_TRUE(count == EntToUniClass.Count());

  // Lookup should find all entries.
  count = 0;
  for (auto& entity : gEntities) {
    if (EntToUniClass.Lookup(nsDependentCString(entity.mStr))) {
      count++;
    }
  }
  ASSERT_TRUE(count == EntToUniClass.Count());

  for (auto& entity : gEntities) {
    EntToUniClass.WithEntryHandle(nsDependentCString(entity.mStr),
                                  [](auto&& entry) { EXPECT_TRUE(entry); });
  }

  // Lookup().Remove() should remove all entries.
  for (auto& entity : gEntities) {
    if (auto entry = EntToUniClass.Lookup(nsDependentCString(entity.mStr))) {
      entry.Remove();
    }
  }
  ASSERT_TRUE(0 == EntToUniClass.Count());

  // Remove newly added entries via OrRemove.
  for (auto& entity : gEntities) {
    EntToUniClass.WithEntryHandle(nsDependentCString(entity.mStr),
                                  [](auto&& entry) {
                                    EXPECT_FALSE(entry);
                                    entry.OrRemove();
                                  });
  }
  ASSERT_TRUE(0 == EntToUniClass.Count());

  // Remove existing entries via OrRemove.
  for (auto& entity : gEntities) {
    EntToUniClass.WithEntryHandle(
        nsDependentCString(entity.mStr), [](auto&& entry) {
          EXPECT_FALSE(entry);
          const TestUniChar* val = entry.OrInsert(nullptr).get();
          EXPECT_TRUE(entry);
          EXPECT_TRUE(val == nullptr);
          EXPECT_TRUE(entry.Data() == nullptr);
        });

    EntToUniClass.WithEntryHandle(nsDependentCString(entity.mStr),
                                  [](auto&& entry) {
                                    EXPECT_TRUE(entry);
                                    entry.OrRemove();
                                  });
  }
  ASSERT_TRUE(0 == EntToUniClass.Count());
}

TEST(Hashtables, ClassHashtable_GetOrInsertNew_Present)
{
  nsClassHashtable<nsCStringHashKey, TestUniChar> EntToUniClass(ENTITY_COUNT);

  for (const auto& entity : gEntities) {
    EntToUniClass.InsertOrUpdate(
        nsDependentCString(entity.mStr),
        std::make_unique<TestUniCharDerived>(entity.mUnicode));
  }

  auto* entry = EntToUniClass.GetOrInsertNew("uml"_ns, 42);
  EXPECT_EQ(168u, entry->GetChar());
}

TEST(Hashtables, ClassHashtable_GetOrInsertNew_NotPresent)
{
  nsClassHashtable<nsCStringHashKey, TestUniChar> EntToUniClass(ENTITY_COUNT);

  // This is going to insert a TestUniChar.
  auto* entry = EntToUniClass.GetOrInsertNew("uml"_ns, 42);
  EXPECT_EQ(42u, entry->GetChar());
}

TEST(Hashtables, ClassHashtable_LookupOrInsertWith_Present)
{
  nsClassHashtable<nsCStringHashKey, TestUniChar> EntToUniClass(ENTITY_COUNT);

  for (const auto& entity : gEntities) {
    EntToUniClass.InsertOrUpdate(
        nsDependentCString(entity.mStr),
        std::make_unique<TestUniCharDerived>(entity.mUnicode));
  }

  const auto& entry = EntToUniClass.LookupOrInsertWith(
      "uml"_ns, [] { return std::make_unique<TestUniCharDerived>(42); });
  EXPECT_EQ(168u, entry->GetChar());
}

TEST(Hashtables, ClassHashtable_LookupOrInsertWith_NotPresent)
{
  nsClassHashtable<nsCStringHashKey, TestUniChar> EntToUniClass(ENTITY_COUNT);

  // This is going to insert a TestUniCharDerived.
  const auto& entry = EntToUniClass.LookupOrInsertWith(
      "uml"_ns, [] { return std::make_unique<TestUniCharDerived>(42); });
  EXPECT_EQ(42u, entry->GetChar());
}

TEST(Hashtables, RefPtrHashtable)
{
  // check a RefPtr-hashtable
  nsRefPtrHashtable<nsCStringHashKey, TestUniCharRefCounted> EntToUniClass(
      ENTITY_COUNT);

  for (auto& entity : gEntities) {
    EntToUniClass.InsertOrUpdate(
        nsDependentCString(entity.mStr),
        MakeRefPtr<TestUniCharRefCounted>(entity.mUnicode));
  }

  TestUniCharRefCounted* myChar;

  for (auto& entity : gEntities) {
    ASSERT_TRUE(EntToUniClass.Get(nsDependentCString(entity.mStr), &myChar));
  }

  ASSERT_FALSE(EntToUniClass.Get("xxxx"_ns, &myChar));

  uint32_t count = 0;
  for (auto iter = EntToUniClass.Iter(); !iter.Done(); iter.Next()) {
    count++;
  }
  ASSERT_EQ(count, ENTITY_COUNT);

  EntToUniClass.Clear();

  count = 0;
  for (auto iter = EntToUniClass.Iter(); !iter.Done(); iter.Next()) {
    count++;
  }
  ASSERT_EQ(count, uint32_t(0));
}

TEST(Hashtables, RefPtrHashtable_Clone)
{
  // check a RefPtr-hashtable
  nsRefPtrHashtable<nsCStringHashKey, TestUniCharRefCounted> EntToUniClass(
      ENTITY_COUNT);

  for (auto& entity : gEntities) {
    EntToUniClass.InsertOrUpdate(
        nsDependentCString(entity.mStr),
        MakeRefPtr<TestUniCharRefCounted>(entity.mUnicode));
  }

  auto clone = EntToUniClass.Clone();
  static_assert(std::is_same_v<decltype(clone), decltype(EntToUniClass)>);

  EXPECT_EQ(clone.Count(), EntToUniClass.Count());

  for (const auto& entry : EntToUniClass) {
    auto cloneEntry = clone.Lookup(entry.GetKey());

    EXPECT_TRUE(cloneEntry);
    EXPECT_EQ(cloneEntry.Data(), entry.GetRawPtr());
  }
}

TEST(Hashtables, Clone)
{
  static constexpr uint64_t count = 10;

  nsTHashMap<nsUint64HashKey, uint64_t> table;
  for (uint64_t i = 0; i < count; i++) {
    table.InsertOrUpdate(42 + i, i);
  }

  auto clone = table.Clone();

  static_assert(std::is_same_v<decltype(clone), decltype(table)>);

  EXPECT_EQ(clone.Count(), table.Count());

  for (const auto& entry : table) {
    auto cloneEntry = clone.Lookup(entry.GetKey());

    EXPECT_TRUE(cloneEntry);
    EXPECT_EQ(cloneEntry.Data(), entry.GetData());
  }
}

TEST(Hashtables, Values)
{
  static constexpr uint64_t count = 10;

  nsTHashMap<nsUint64HashKey, uint64_t> table;
  for (uint64_t i = 0; i < count; i++) {
    table.InsertOrUpdate(42 + i, i);
  }

  nsTArray<uint64_t> values;
  for (const uint64_t& value : table.Values()) {
    values.AppendElement(value);
  }
  values.Sort();

  EXPECT_EQ((nsTArray<uint64_t>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}), values);
}

struct HasThreadSafeWeak : public SupportsThreadSafeWeakPtr<HasThreadSafeWeak> {
  MOZ_DECLARE_REFCOUNTED_TYPENAME(HasThreadSafeWeak)
  ~HasThreadSafeWeak() { mOnDestroy(this); }
  std::function<void(HasThreadSafeWeak*)> mOnDestroy;
};

TEST(Hashtables, ThreadSafeWeakPtrAsKey)
{
  nsTHashMap<ThreadSafeWeakPtr<HasThreadSafeWeak>, int32_t> table;
  RefPtr<HasThreadSafeWeak> alive = new HasThreadSafeWeak;

  ThreadSafeWeakPtr<HasThreadSafeWeak> weak(alive);
  EXPECT_FALSE(weak.IsDead());
  EXPECT_FALSE(weak.IsNull());

  table.InsertOrUpdate(weak, 1);
  table.InsertOrUpdate(nullptr, 0);

  auto origHash = weak.Hash();

  bool destructorRan = false;
  alive->mOnDestroy = [&](HasThreadSafeWeak* aDyingObject) {
    destructorRan = true;
    EXPECT_TRUE(weak.IsDead()) << "no longer upgradable";
    EXPECT_FALSE(weak.IsNull());
    EXPECT_EQ(weak, aDyingObject) << "still compares equal";

    ThreadSafeWeakPtr<HasThreadSafeWeak> weak2(aDyingObject);
    EXPECT_TRUE(weak2.IsDead());
    EXPECT_FALSE(weak2.IsNull());
    EXPECT_EQ(weak, weak2) << "still compares equal";
    EXPECT_NE(weak, nullptr) << "is not == nullptr";

    EXPECT_TRUE(table.Contains(aDyingObject));
    EXPECT_EQ(table.Get(aDyingObject), 1);

    EXPECT_EQ(origHash, weak2.Hash());
    EXPECT_EQ(origHash, weak.Hash());
  };

  EXPECT_FALSE(destructorRan);
  alive = nullptr;
  EXPECT_TRUE(destructorRan);

  EXPECT_TRUE(table.Contains(weak));
  EXPECT_EQ(table.Get(weak), 1);

  table.Remove(weak);

  EXPECT_FALSE(table.Contains(weak));
  EXPECT_TRUE(table.Contains(nullptr));
}
