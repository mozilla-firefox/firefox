/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Common.h"

#include "Classifier.h"
#include "HashStore.h"
#include "mozilla/Components.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nsIThread.h"
#include "nsTArray.h"
#include "nsThreadUtils.h"
#include "nsUrlClassifierUtils.h"

using namespace mozilla;
using namespace mozilla::safebrowsing;

nsresult SyncApplyUpdates(TableUpdateArray& aUpdates) {
  // We need to spin a new thread specifically because the callback
  // will be on the caller thread. If we call Classifier::AsyncApplyUpdates
  // and wait on the same thread, this function will never return.

  nsresult ret = NS_ERROR_FAILURE;
  bool done = false;
  auto onUpdateComplete = [&done, &ret](nsresult rv) {
    // We are on the "ApplyUpdate" thread. Post an event to main thread
    // so that we can avoid busy waiting on the main thread.
    nsCOMPtr<nsIRunnable> r =
        NS_NewRunnableFunction("SyncApplyUpdates", [&done, &ret, rv] {
          ret = rv;
          done = true;
        });
    NS_DispatchToMainThread(r);
  };

  nsCOMPtr<nsIFile> file;
  NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR, getter_AddRefs(file));

  nsCOMPtr<nsIRunnable> r = NS_NewRunnableFunction("SyncApplyUpdates", [&]() {
    RefPtr<Classifier> classifier = new Classifier();
    classifier->Open(*file);

    nsresult rv = classifier->AsyncApplyUpdates(aUpdates, onUpdateComplete);
    if (NS_FAILED(rv)) {
      onUpdateComplete(rv);
    }
  });

  nsCOMPtr<nsIThread> testingThread;
  NS_NewNamedThread("ApplyUpdates", getter_AddRefs(testingThread));
  if (!testingThread) {
    return NS_ERROR_FAILURE;
  }

  testingThread->Dispatch(r, NS_DISPATCH_NORMAL);

  // NS_NewCheckSummedOutputStream in HashStore::WriteFile
  // will synchronously init NS_CRYPTO_HASH_CONTRACTID on
  // the main thread. As a result we have to keep processing
  // pending event until |done| becomes true. If there's no
  // more pending event, what we only can do is wait.
  MOZ_ALWAYS_TRUE(SpinEventLoopUntil("url-classifier:SyncApplyUpdates"_ns,
                                     [&]() { return done; }));

  return ret;
}

already_AddRefed<nsIFile> GetFile(const nsTArray<nsString>& path) {
  nsCOMPtr<nsIFile> file;
  nsresult rv =
      NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR, getter_AddRefs(file));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nullptr;
  }

  for (uint32_t i = 0; i < path.Length(); i++) {
    file->Append(path[i]);
  }
  return file.forget();
}

void ApplyUpdate(TableUpdateArray& updates) {
  // Force nsUrlClassifierUtils loading on main thread
  // because nsIUrlClassifierDBService will not run in advance
  // in gtest.
  nsUrlClassifierUtils::GetInstance();

  SyncApplyUpdates(updates);
}

void ApplyUpdate(TableUpdate* update) {
  TableUpdateArray updates = {update};
  ApplyUpdate(updates);
}

nsresult PrefixArrayToPrefixStringMap(const _PrefixArray& aPrefixArray,
                                      PrefixStringMap& aOut) {
  aOut.Clear();

  // Buckets are keyed by prefix length and contain an array of
  // all prefixes of that length.
  nsClassHashtable<nsUint32HashKey, _PrefixArray> table;
  for (const auto& prefix : aPrefixArray) {
    _PrefixArray* array = table.GetOrInsertNew(prefix.Length());
    array->AppendElement(prefix);
  }

  // The resulting map entries will be a concatenation of all
  // prefix data for the prefixes of a given size.
  for (const auto& entry : table) {
    uint32_t size = entry.GetKey();
    uint32_t count = entry.GetData()->Length();

    auto str = std::make_unique<_Prefix>();
    str->SetLength(size * count);

    char* dst = str->BeginWriting();

    entry.GetData()->Sort();
    for (uint32_t i = 0; i < count; i++) {
      memcpy(dst, entry.GetData()->ElementAt(i).get(), size);
      dst += size;
    }

    aOut.InsertOrUpdate(size, std::move(str));
  }

  return NS_OK;
}

nsresult PrefixArrayToAddPrefixArray(const _PrefixArray& aPrefixArray,
                                     AddPrefixArray& aOut) {
  aOut.Clear();

  for (const auto& prefix : aPrefixArray) {
    // Create prefix hash from string
    AddPrefix* add = aOut.AppendElement(fallible);
    if (!add) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    add->addChunk = 1;
    add->prefix.Assign(prefix);
  }

  EntrySort(aOut);

  return NS_OK;
}

_Prefix CreatePrefixFromURL(const char* aURL, uint8_t aPrefixSize) {
  return CreatePrefixFromURL(nsCString(aURL), aPrefixSize);
}

_Prefix CreatePrefixFromURL(const nsCString& aURL, uint8_t aPrefixSize) {
  Completion complete;
  complete.FromPlaintext(aURL);

  _Prefix prefix;
  prefix.Assign((const char*)complete.buf, aPrefixSize);
  return prefix;
}

void CheckContent(LookupCacheV4* aCache, const _PrefixArray& aPrefixArray) {
  PrefixStringMap vlPSetMap;
  aCache->GetPrefixes(vlPSetMap);

  PrefixStringMap expected;
  PrefixArrayToPrefixStringMap(aPrefixArray, expected);

  for (const auto& entry : vlPSetMap) {
    nsCString* expectedPrefix = expected.Get(entry.GetKey());
    nsCString* resultPrefix = entry.GetRawPtr();

    ASSERT_TRUE(resultPrefix->Equals(*expectedPrefix));
  }
}

nsresult BuildLookupCache(const RefPtr<Classifier>& classifier,
                          const nsACString& aTable,
                          _PrefixArray& aPrefixArray) {
  RefPtr<LookupCache> cache = classifier->GetLookupCache(aTable, false);
  if (!cache) {
    return NS_ERROR_FAILURE;
  }

  if (LookupCache::Cast<LookupCacheV4>(cache)) {
    // V4
    RefPtr<LookupCacheV4> cacheV4 = LookupCache::Cast<LookupCacheV4>(cache);

    PrefixStringMap map;
    PrefixArrayToPrefixStringMap(aPrefixArray, map);
    return cacheV4->Build(map);
  } else {
    // V2
    RefPtr<LookupCacheV2> cacheV2 = LookupCache::Cast<LookupCacheV2>(cache);

    AddPrefixArray addPrefixes;
    AddCompleteArray addComples;

    PrefixArrayToAddPrefixArray(aPrefixArray, addPrefixes);
    return cacheV2->Build(addPrefixes, addComples);
  }
}

RefPtr<Classifier> GetClassifier() {
  nsCOMPtr<nsIFile> file;
  NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR, getter_AddRefs(file));

  RefPtr<Classifier> classifier = new Classifier();
  nsresult rv = classifier->Open(*file);
  EXPECT_TRUE(rv == NS_OK);

  return classifier;
}
