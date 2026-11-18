/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Classifier.h"
#include "HashStore.h"
#include "mozilla/Components.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nsICryptoHash.h"
#include "nsIFile.h"
#include "nsThreadUtils.h"
#include "string.h"
#include "LookupCacheV4.h"
#include "nsUrlClassifierUtils.h"

#include "Common.h"

#define GTEST_SAFEBROWSING_DIR "safebrowsing"_ns
#define GTEST_TABLE "gtest-malware-proto"_ns
#define GTEST_PREFIXFILE "gtest-malware-proto.vlpset"_ns

// This function removes common elements of inArray and outArray from
// outArray. This is used by partial update testcase to ensure partial update
// data won't contain prefixes we already have.
static void RemoveIntersection(const _PrefixArray& inArray,
                               _PrefixArray& outArray) {
  for (uint32_t i = 0; i < inArray.Length(); i++) {
    int32_t idx = outArray.BinaryIndexOf(inArray[i]);
    if (idx >= 0) {
      outArray.RemoveElementAt(idx);
    }
  }
}

// This fucntion removes elements from outArray by index specified in
// removal array.
static void RemoveElements(const nsTArray<uint32_t>& removal,
                           _PrefixArray& outArray) {
  for (int32_t i = removal.Length() - 1; i >= 0; i--) {
    outArray.RemoveElementAt(removal[i]);
  }
}

static void MergeAndSortArray(const _PrefixArray& array1,
                              const _PrefixArray& array2,
                              _PrefixArray& output) {
  output.Clear();
  output.AppendElements(array1);
  output.AppendElements(array2);
  output.Sort();
}

static void CalculateSHA256(_PrefixArray& prefixArray, nsCString& sha256) {
  prefixArray.Sort();

  nsresult rv;
  nsCOMPtr<nsICryptoHash> cryptoHash =
      do_CreateInstance(NS_CRYPTO_HASH_CONTRACTID, &rv);

  cryptoHash->Init(nsICryptoHash::SHA256);
  for (uint32_t i = 0; i < prefixArray.Length(); i++) {
    const _Prefix& prefix = prefixArray[i];
    cryptoHash->Update(
        reinterpret_cast<uint8_t*>(const_cast<char*>(prefix.get())),
        prefix.Length());
  }
  cryptoHash->Finish(false, sha256);
}

// N: Number of prefixes, MIN/MAX: minimum/maximum prefix size
// This function will append generated prefixes to outArray.
static void CreateRandomSortedPrefixArray(uint32_t N, uint32_t MIN,
                                          uint32_t MAX,
                                          _PrefixArray& outArray) {
  outArray.SetCapacity(outArray.Length() + N);

  const uint32_t range = (MAX - MIN + 1);

  for (uint32_t i = 0; i < N; i++) {
    uint32_t prefixSize = (rand() % range) + MIN;
    _Prefix prefix;
    prefix.SetLength(prefixSize);

    while (true) {
      char* dst = prefix.BeginWriting();
      for (uint32_t j = 0; j < prefixSize; j++) {
        dst[j] = rand() % 256;
      }

      if (!outArray.Contains(prefix)) {
        outArray.AppendElement(prefix);
        break;
      }
    }
  }

  outArray.Sort();
}

// N: Number of removal indices, MAX: maximum index
static void CreateRandomRemovalIndices(uint32_t N, uint32_t MAX,
                                       nsTArray<uint32_t>& outArray) {
  for (uint32_t i = 0; i < N; i++) {
    uint32_t idx = rand() % MAX;
    if (!outArray.Contains(idx)) {
      outArray.InsertElementSorted(idx);
    }
  }
}

// Function to generate TableUpdateV4.
static void GenerateUpdateData(bool fullUpdate, PrefixStringMap& add,
                               nsTArray<uint32_t>* removal, nsCString* sha256,
                               TableUpdateArray& tableUpdates) {
  RefPtr<TableUpdateV4> tableUpdate = new TableUpdateV4(GTEST_TABLE);
  tableUpdate->SetFullUpdate(fullUpdate);

  for (const auto& entry : add) {
    nsCString* pstring = entry.GetRawPtr();
    tableUpdate->NewPrefixes(entry.GetKey(), *pstring);
  }

  if (removal) {
    tableUpdate->NewRemovalIndices(removal->Elements(), removal->Length());
  }

  if (sha256) {
    std::string stdSHA256;
    stdSHA256.assign(const_cast<char*>(sha256->BeginReading()),
                     sha256->Length());

    tableUpdate->SetSHA256(stdSHA256);
  }

  tableUpdates.AppendElement(tableUpdate);
}

static void VerifyPrefixSet(PrefixStringMap& expected) {
  // Verify the prefix set is written to disk.
  nsCOMPtr<nsIFile> file;
  NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR, getter_AddRefs(file));
  file->AppendNative(GTEST_SAFEBROWSING_DIR);

  RefPtr<LookupCacheV4> lookup =
      new LookupCacheV4(GTEST_TABLE, "test"_ns, file);
  lookup->Init();

  file->AppendNative(GTEST_PREFIXFILE);
  lookup->LoadFromFile(file);

  PrefixStringMap prefixesInFile;
  lookup->GetPrefixes(prefixesInFile);

  for (const auto& entry : expected) {
    nsCString* expectedPrefix = entry.GetRawPtr();
    nsCString* resultPrefix = prefixesInFile.Get(entry.GetKey());

    ASSERT_TRUE(*resultPrefix == *expectedPrefix);
  }
}

static void Clear() {
  nsCOMPtr<nsIFile> file;
  NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR, getter_AddRefs(file));

  RefPtr<Classifier> classifier = new Classifier();
  classifier->Open(*file);
  classifier->Reset();
}

static void testUpdateFail(TableUpdateArray& tableUpdates) {
  nsresult rv = SyncApplyUpdates(tableUpdates);
  ASSERT_NS_FAILED(rv);
}

static void testUpdate(TableUpdateArray& tableUpdates,
                       PrefixStringMap& expected) {
  // Force nsUrlClassifierUtils loading on main thread
  // because nsIUrlClassifierDBService will not run in advance
  // in gtest.
  nsUrlClassifierUtils::GetInstance();

  nsresult rv = SyncApplyUpdates(tableUpdates);
  ASSERT_TRUE(rv == NS_OK);
  VerifyPrefixSet(expected);
}

static void testFullUpdate(PrefixStringMap& add, nsCString* sha256) {
  TableUpdateArray tableUpdates;

  GenerateUpdateData(true, add, nullptr, sha256, tableUpdates);

  testUpdate(tableUpdates, add);
}

static void testPartialUpdate(PrefixStringMap& add, nsTArray<uint32_t>* removal,
                              nsCString* sha256, PrefixStringMap& expected) {
  TableUpdateArray tableUpdates;
  GenerateUpdateData(false, add, removal, sha256, tableUpdates);

  testUpdate(tableUpdates, expected);
}

static void testOpenLookupCache() {
  nsCOMPtr<nsIFile> file;
  NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR, getter_AddRefs(file));
  file->AppendNative(GTEST_SAFEBROWSING_DIR);

  RunTestInNewThread([&]() -> void {
    RefPtr<LookupCacheV4> cache =
        new LookupCacheV4(nsCString(GTEST_TABLE), ""_ns, file);
    nsresult rv = cache->Init();
    ASSERT_EQ(rv, NS_OK);

    rv = cache->Open();
    ASSERT_EQ(rv, NS_OK);
  });
}

// Tests start from here.
TEST(UrlClassifierTableUpdateV4, FixLengthPSetFullUpdate)
{
  srand(time(NULL));

  _PrefixArray array;
  PrefixStringMap map;
  nsCString sha256;

  CreateRandomSortedPrefixArray(5000, 4, 4, array);
  PrefixArrayToPrefixStringMap(array, map);
  CalculateSHA256(array, sha256);

  testFullUpdate(map, &sha256);

  Clear();
}

TEST(UrlClassifierTableUpdateV4, VariableLengthPSetFullUpdate)
{
  _PrefixArray array;
  PrefixStringMap map;
  nsCString sha256;

  CreateRandomSortedPrefixArray(5000, 5, 32, array);
  PrefixArrayToPrefixStringMap(array, map);
  CalculateSHA256(array, sha256);

  testFullUpdate(map, &sha256);

  Clear();
}

// This test contain both variable length prefix set and fixed-length prefix set
TEST(UrlClassifierTableUpdateV4, MixedPSetFullUpdate)
{
  _PrefixArray array;
  PrefixStringMap map;
  nsCString sha256;

  CreateRandomSortedPrefixArray(5000, 4, 4, array);
  CreateRandomSortedPrefixArray(1000, 5, 32, array);
  PrefixArrayToPrefixStringMap(array, map);
  CalculateSHA256(array, sha256);

  testFullUpdate(map, &sha256);

  Clear();
}

TEST(UrlClassifierTableUpdateV4, PartialUpdateWithRemoval)
{
  _PrefixArray fArray;

  // Apply a full update first.
  {
    PrefixStringMap fMap;
    nsCString sha256;

    CreateRandomSortedPrefixArray(10000, 4, 4, fArray);
    CreateRandomSortedPrefixArray(2000, 5, 32, fArray);
    PrefixArrayToPrefixStringMap(fArray, fMap);
    CalculateSHA256(fArray, sha256);

    testFullUpdate(fMap, &sha256);
  }

  // Apply a partial update with removal.
  {
    _PrefixArray pArray, mergedArray;
    PrefixStringMap pMap, mergedMap;
    nsCString sha256;

    CreateRandomSortedPrefixArray(5000, 4, 4, pArray);
    CreateRandomSortedPrefixArray(1000, 5, 32, pArray);
    RemoveIntersection(fArray, pArray);
    PrefixArrayToPrefixStringMap(pArray, pMap);

    // Remove 1/5 of elements of original prefix set.
    nsTArray<uint32_t> removal;
    CreateRandomRemovalIndices(fArray.Length() / 5, fArray.Length(), removal);
    RemoveElements(removal, fArray);

    // Calculate the expected prefix map.
    MergeAndSortArray(fArray, pArray, mergedArray);
    PrefixArrayToPrefixStringMap(mergedArray, mergedMap);
    CalculateSHA256(mergedArray, sha256);

    testPartialUpdate(pMap, &removal, &sha256, mergedMap);
  }

  Clear();
}

TEST(UrlClassifierTableUpdateV4, PartialUpdateWithoutRemoval)
{
  _PrefixArray fArray;

  // Apply a full update first.
  {
    PrefixStringMap fMap;
    nsCString sha256;

    CreateRandomSortedPrefixArray(10000, 4, 4, fArray);
    CreateRandomSortedPrefixArray(2000, 5, 32, fArray);
    PrefixArrayToPrefixStringMap(fArray, fMap);
    CalculateSHA256(fArray, sha256);

    testFullUpdate(fMap, &sha256);
  }

  // Apply a partial update without removal
  {
    _PrefixArray pArray, mergedArray;
    PrefixStringMap pMap, mergedMap;
    nsCString sha256;

    CreateRandomSortedPrefixArray(5000, 4, 4, pArray);
    CreateRandomSortedPrefixArray(1000, 5, 32, pArray);
    RemoveIntersection(fArray, pArray);
    PrefixArrayToPrefixStringMap(pArray, pMap);

    // Calculate the expected prefix map.
    MergeAndSortArray(fArray, pArray, mergedArray);
    PrefixArrayToPrefixStringMap(mergedArray, mergedMap);
    CalculateSHA256(mergedArray, sha256);

    testPartialUpdate(pMap, nullptr, &sha256, mergedMap);
  }

  Clear();
}

// Expect failure because partial update contains prefix already
// in old prefix set.
TEST(UrlClassifierTableUpdateV4, PartialUpdatePrefixAlreadyExist)
{
  _PrefixArray fArray;

  // Apply a full update fist.
  {
    PrefixStringMap fMap;
    nsCString sha256;

    CreateRandomSortedPrefixArray(1000, 4, 32, fArray);
    PrefixArrayToPrefixStringMap(fArray, fMap);
    CalculateSHA256(fArray, sha256);

    testFullUpdate(fMap, &sha256);
  }

  // Apply a partial update which contains a prefix in previous full update.
  // This should cause an update error.
  {
    _PrefixArray pArray;
    PrefixStringMap pMap;
    TableUpdateArray tableUpdates;

    // Pick one prefix from full update prefix and add it to partial update.
    // This should result a failure when call ApplyUpdates.
    pArray.AppendElement(fArray[rand() % fArray.Length()]);
    CreateRandomSortedPrefixArray(200, 4, 32, pArray);
    PrefixArrayToPrefixStringMap(pArray, pMap);

    GenerateUpdateData(false, pMap, nullptr, nullptr, tableUpdates);
    testUpdateFail(tableUpdates);
  }

  Clear();
}

// Test apply partial update directly without applying an full update first.
TEST(UrlClassifierTableUpdateV4, OnlyPartialUpdate)
{
  _PrefixArray pArray;
  PrefixStringMap pMap;
  nsCString sha256;

  CreateRandomSortedPrefixArray(5000, 4, 4, pArray);
  CreateRandomSortedPrefixArray(1000, 5, 32, pArray);
  PrefixArrayToPrefixStringMap(pArray, pMap);
  CalculateSHA256(pArray, sha256);

  testPartialUpdate(pMap, nullptr, &sha256, pMap);

  Clear();
}

// Test partial update without any ADD prefixes, only removalIndices.
TEST(UrlClassifierTableUpdateV4, PartialUpdateOnlyRemoval)
{
  _PrefixArray fArray;

  // Apply a full update first.
  {
    PrefixStringMap fMap;
    nsCString sha256;

    CreateRandomSortedPrefixArray(5000, 4, 4, fArray);
    CreateRandomSortedPrefixArray(1000, 5, 32, fArray);
    PrefixArrayToPrefixStringMap(fArray, fMap);
    CalculateSHA256(fArray, sha256);

    testFullUpdate(fMap, &sha256);
  }

  // Apply a partial update without add prefix, only contain removal indices.
  {
    _PrefixArray pArray;
    PrefixStringMap pMap, mergedMap;
    nsCString sha256;

    // Remove 1/5 of elements of original prefix set.
    nsTArray<uint32_t> removal;
    CreateRandomRemovalIndices(fArray.Length() / 5, fArray.Length(), removal);
    RemoveElements(removal, fArray);

    PrefixArrayToPrefixStringMap(fArray, mergedMap);
    CalculateSHA256(fArray, sha256);

    testPartialUpdate(pMap, &removal, &sha256, mergedMap);
  }

  Clear();
}

// Test one tableupdate array contains full update and multiple partial updates.
TEST(UrlClassifierTableUpdateV4, MultipleTableUpdates)
{
  _PrefixArray fArray, pArray, mergedArray;
  PrefixStringMap fMap, pMap, mergedMap;
  nsCString sha256;

  TableUpdateArray tableUpdates;

  // Generate first full udpate
  CreateRandomSortedPrefixArray(10000, 4, 4, fArray);
  CreateRandomSortedPrefixArray(2000, 5, 32, fArray);
  PrefixArrayToPrefixStringMap(fArray, fMap);
  CalculateSHA256(fArray, sha256);

  GenerateUpdateData(true, fMap, nullptr, &sha256, tableUpdates);

  // Generate second partial update
  CreateRandomSortedPrefixArray(3000, 4, 4, pArray);
  CreateRandomSortedPrefixArray(1000, 5, 32, pArray);
  RemoveIntersection(fArray, pArray);
  PrefixArrayToPrefixStringMap(pArray, pMap);

  MergeAndSortArray(fArray, pArray, mergedArray);
  CalculateSHA256(mergedArray, sha256);

  GenerateUpdateData(false, pMap, nullptr, &sha256, tableUpdates);

  // Generate thrid partial update
  fArray.AppendElements(pArray);
  fArray.Sort();
  pArray.Clear();
  CreateRandomSortedPrefixArray(3000, 4, 4, pArray);
  CreateRandomSortedPrefixArray(1000, 5, 32, pArray);
  RemoveIntersection(fArray, pArray);
  PrefixArrayToPrefixStringMap(pArray, pMap);

  // Remove 1/5 of elements of original prefix set.
  nsTArray<uint32_t> removal;
  CreateRandomRemovalIndices(fArray.Length() / 5, fArray.Length(), removal);
  RemoveElements(removal, fArray);

  MergeAndSortArray(fArray, pArray, mergedArray);
  PrefixArrayToPrefixStringMap(mergedArray, mergedMap);
  CalculateSHA256(mergedArray, sha256);

  GenerateUpdateData(false, pMap, &removal, &sha256, tableUpdates);

  testUpdate(tableUpdates, mergedMap);

  Clear();
}

// Test apply full update first, and then apply multiple partial updates
// in one tableupdate array.
TEST(UrlClassifierTableUpdateV4, MultiplePartialUpdateTableUpdates)
{
  _PrefixArray fArray;

  // Apply a full update first
  {
    PrefixStringMap fMap;
    nsCString sha256;

    // Generate first full udpate
    CreateRandomSortedPrefixArray(10000, 4, 4, fArray);
    CreateRandomSortedPrefixArray(3000, 5, 32, fArray);
    PrefixArrayToPrefixStringMap(fArray, fMap);
    CalculateSHA256(fArray, sha256);

    testFullUpdate(fMap, &sha256);
  }

  // Apply multiple partial updates in one table update
  {
    _PrefixArray pArray, mergedArray;
    PrefixStringMap pMap, mergedMap;
    nsCString sha256;
    nsTArray<uint32_t> removal;
    TableUpdateArray tableUpdates;

    // Generate first partial update
    CreateRandomSortedPrefixArray(3000, 4, 4, pArray);
    CreateRandomSortedPrefixArray(1000, 5, 32, pArray);
    RemoveIntersection(fArray, pArray);
    PrefixArrayToPrefixStringMap(pArray, pMap);

    // Remove 1/5 of elements of original prefix set.
    CreateRandomRemovalIndices(fArray.Length() / 5, fArray.Length(), removal);
    RemoveElements(removal, fArray);

    MergeAndSortArray(fArray, pArray, mergedArray);
    CalculateSHA256(mergedArray, sha256);

    GenerateUpdateData(false, pMap, &removal, &sha256, tableUpdates);

    fArray.AppendElements(pArray);
    fArray.Sort();
    pArray.Clear();
    removal.Clear();

    // Generate second partial update.
    CreateRandomSortedPrefixArray(2000, 4, 4, pArray);
    CreateRandomSortedPrefixArray(1000, 5, 32, pArray);
    RemoveIntersection(fArray, pArray);
    PrefixArrayToPrefixStringMap(pArray, pMap);

    // Remove 1/5 of elements of original prefix set.
    CreateRandomRemovalIndices(fArray.Length() / 5, fArray.Length(), removal);
    RemoveElements(removal, fArray);

    MergeAndSortArray(fArray, pArray, mergedArray);
    PrefixArrayToPrefixStringMap(mergedArray, mergedMap);
    CalculateSHA256(mergedArray, sha256);

    GenerateUpdateData(false, pMap, &removal, &sha256, tableUpdates);

    testUpdate(tableUpdates, mergedMap);
  }

  Clear();
}

// Test removal indices are larger than the original prefix set.
TEST(UrlClassifierTableUpdateV4, RemovalIndexTooLarge)
{
  _PrefixArray fArray;

  // Apply a full update first
  {
    PrefixStringMap fMap;
    nsCString sha256;

    CreateRandomSortedPrefixArray(1000, 4, 32, fArray);
    PrefixArrayToPrefixStringMap(fArray, fMap);
    CalculateSHA256(fArray, sha256);

    testFullUpdate(fMap, &sha256);
  }

  // Apply a partial update with removal indice array larger than
  // old prefix set(fArray). This should cause an error.
  {
    _PrefixArray pArray;
    PrefixStringMap pMap;
    nsTArray<uint32_t> removal;
    TableUpdateArray tableUpdates;

    CreateRandomSortedPrefixArray(200, 4, 32, pArray);
    RemoveIntersection(fArray, pArray);
    PrefixArrayToPrefixStringMap(pArray, pMap);

    for (uint32_t i = 0; i < fArray.Length() + 1; i++) {
      removal.AppendElement(i);
    }

    GenerateUpdateData(false, pMap, &removal, nullptr, tableUpdates);
    testUpdateFail(tableUpdates);
  }

  Clear();
}

TEST(UrlClassifierTableUpdateV4, ChecksumMismatch)
{
  // Apply a full update first
  {
    _PrefixArray fArray;
    PrefixStringMap fMap;
    nsCString sha256;

    CreateRandomSortedPrefixArray(1000, 4, 32, fArray);
    PrefixArrayToPrefixStringMap(fArray, fMap);
    CalculateSHA256(fArray, sha256);

    testFullUpdate(fMap, &sha256);
  }

  // Apply a partial update with incorrect sha256
  {
    _PrefixArray pArray;
    PrefixStringMap pMap;
    nsCString sha256;
    TableUpdateArray tableUpdates;

    CreateRandomSortedPrefixArray(200, 4, 32, pArray);
    PrefixArrayToPrefixStringMap(pArray, pMap);

    // sha256 should be calculated with both old prefix set and add prefix
    // set, here we only calculate sha256 with add prefix set to check if
    // applyUpdate will return failure.
    CalculateSHA256(pArray, sha256);

    GenerateUpdateData(false, pMap, nullptr, &sha256, tableUpdates);
    testUpdateFail(tableUpdates);
  }

  Clear();
}

TEST(UrlClassifierTableUpdateV4, ApplyUpdateThenLoad)
{
  // Apply update with sha256
  {
    _PrefixArray fArray;
    PrefixStringMap fMap;
    nsCString sha256;

    CreateRandomSortedPrefixArray(1000, 4, 32, fArray);
    PrefixArrayToPrefixStringMap(fArray, fMap);
    CalculateSHA256(fArray, sha256);

    testFullUpdate(fMap, &sha256);

    // Open lookup cache will load prefix set and verify the sha256
    testOpenLookupCache();
  }

  Clear();

  // Apply update without sha256
  {
    _PrefixArray fArray;
    PrefixStringMap fMap;

    CreateRandomSortedPrefixArray(1000, 4, 32, fArray);
    PrefixArrayToPrefixStringMap(fArray, fMap);

    testFullUpdate(fMap, nullptr);

    testOpenLookupCache();
  }

  Clear();
}

// This test is used to avoid an eror from nsICryptoHash
TEST(UrlClassifierTableUpdateV4, ApplyUpdateWithFixedChecksum)
{
  _PrefixArray fArray = {_Prefix("enus"),
                         _Prefix("apollo"),
                         _Prefix("mars"),
                         _Prefix("Hecatonchires cyclopes"),
                         _Prefix("vesta"),
                         _Prefix("neptunus"),
                         _Prefix("jupiter"),
                         _Prefix("diana"),
                         _Prefix("minerva"),
                         _Prefix("ceres"),
                         _Prefix("Aidos,Adephagia,Adikia,Aletheia"),
                         _Prefix("hecatonchires"),
                         _Prefix("alcyoneus"),
                         _Prefix("hades"),
                         _Prefix("vulcanus"),
                         _Prefix("juno"),
                         _Prefix("mercury"),
                         _Prefix("Stheno, Euryale and Medusa")};
  fArray.Sort();

  PrefixStringMap fMap;
  PrefixArrayToPrefixStringMap(fArray, fMap);

  nsCString sha256(
      "\xae\x18\x94\xd7\xd0\x83\x5f\xc1"
      "\x58\x59\x5c\x2c\x72\xb9\x6e\x5e"
      "\xf4\xe8\x0a\x6b\xff\x5e\x6b\x81"
      "\x65\x34\x06\x16\x06\x59\xa0\x67");

  testFullUpdate(fMap, &sha256);

  // Open lookup cache will load prefix set and verify the sha256
  testOpenLookupCache();

  Clear();
}

// This test ensure that an empty update works correctly. Empty update
// should be skipped by CheckValidUpdate in Classifier::UpdateTableV4.
TEST(UrlClassifierTableUpdateV4, EmptyUpdate)
{
  PrefixStringMap emptyAddition;
  nsTArray<uint32_t> emptyRemoval;

  _PrefixArray array;
  PrefixStringMap map;
  nsCString sha256;

  CalculateSHA256(array, sha256);

  // Test apply empty full/partial update before we already
  // have data in DB.
  testFullUpdate(emptyAddition, &sha256);
  testPartialUpdate(emptyAddition, &emptyRemoval, &sha256, map);

  // Apply an full update.
  CreateRandomSortedPrefixArray(100, 4, 4, array);
  CreateRandomSortedPrefixArray(10, 5, 32, array);
  PrefixArrayToPrefixStringMap(array, map);
  CalculateSHA256(array, sha256);

  testFullUpdate(map, &sha256);

  // Test apply empty full/partial update when we already
  // have data in DB
  testPartialUpdate(emptyAddition, &emptyRemoval, &sha256, map);
  testFullUpdate(emptyAddition, &sha256);

  Clear();
}

// This test ensure applying an empty update directly through update algorithm
// should be correct.
TEST(UrlClassifierTableUpdateV4, EmptyUpdate2)
{
  // Setup LookupCache with initial data
  _PrefixArray array;
  CreateRandomSortedPrefixArray(100, 4, 4, array);
  CreateRandomSortedPrefixArray(10, 5, 32, array);
  RefPtr<LookupCacheV4> cache = SetupLookupCache<LookupCacheV4>(array);

  // Setup TableUpdate object with only sha256 from previous update(initial
  // data).
  nsCString sha256;
  CalculateSHA256(array, sha256);
  std::string stdSHA256;
  stdSHA256.assign(const_cast<char*>(sha256.BeginReading()), sha256.Length());

  RefPtr<TableUpdateV4> tableUpdate = new TableUpdateV4(GTEST_TABLE);
  tableUpdate->SetSHA256(stdSHA256);

  // Apply update directly through LookupCache interface
  PrefixStringMap input, output;
  PrefixArrayToPrefixStringMap(array, input);
  nsresult rv = cache->ApplyUpdate(tableUpdate.get(), input, output);

  ASSERT_TRUE(rv == NS_OK);

  Clear();
}
