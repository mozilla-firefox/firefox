/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef CacheStorage_h_
#define CacheStorage_h_

#include "CacheEntry.h"
#include "LoadContextInfo.h"
#include "nsICacheStorage.h"
#include "nsILoadContextInfo.h"
#include "nsTArray.h"
#include "nsTHashMap.h"

class nsIURI;

namespace mozilla {
namespace net {

// This dance is needed to make CacheEntryTable declarable-only in headers
// w/o exporting CacheEntry.h file to make nsNetModule.cpp compilable.
using TCacheEntryTable = nsRefPtrHashtable<nsCStringHashKey, CacheEntry>;
class CacheEntryTable : public TCacheEntryTable {
 public:
  enum EType { MEMORY_ONLY, ALL_ENTRIES };

  explicit CacheEntryTable(EType aType) : mType(aType) {}
  CacheEntryTable() = delete;

  EType Type() const { return mType; }

  // Secondary index for No-Vary-Search cache lookup.
  //
  // Without NVS, a cache lookup is a single exact-key hash lookup on this
  // table. With NVS, a response can declare that certain query parameters do
  // not affect the response (e.g. "No-Vary-Search: params=(\"utm_source\")"),
  // meaning /page?q=hello&utm_source=email should hit the same cache entry as
  // /page?q=hello. An exact-key lookup misses in this case, so we need a way
  // to find candidate entries that share the same base path.
  //
  // This index maps scheme://host:port/path (no query string) to the list of
  // full entry keys — the cache storage keys used to look up CacheEntry
  // objects in this table — for all entries that carry NVS metadata. On an
  // exact-key miss, AddStorageEntry() consults this index to find candidates
  // and checks each one for URL equivalence under its stored NVS header.
  //
  // A "full entry key" is the string passed to CacheEntry::HashingKey(), of
  // the form "https://example.com/page?q=hello", uniquely identifying the
  // cached response within this CacheEntryTable.
  //
  // Protected by CacheStorageService::sLock.
  nsTHashMap<nsCStringHashKey, nsTArray<nsCString>> mNoVarySearchIndex;

  void NoteNoVarySearchEntry(const nsACString& aBasePath,
                             const nsACString& aFullKey) {
    auto& keys = mNoVarySearchIndex.LookupOrInsert(aBasePath);
    // Only append if the key is not already present
    if (!keys.Contains(aFullKey)) {
      keys.AppendElement(aFullKey);
    }
  }

  void RemoveNoVarySearchEntry(const nsACString& aBasePath,
                               const nsACString& aFullKey) {
    auto entry = mNoVarySearchIndex.Lookup(aBasePath);
    if (!entry) {
      return;
    }
    entry->RemoveElement(aFullKey);
    if (entry->IsEmpty()) {
      mNoVarySearchIndex.Remove(aBasePath);
    }
  }

  void RemoveNoVarySearchEntryByKey(const nsACString& aFullKey) {
    for (auto iter = mNoVarySearchIndex.Iter(); !iter.Done(); iter.Next()) {
      if (iter.Data().RemoveElement(aFullKey)) {
        if (iter.Data().IsEmpty()) {
          iter.Remove();
        }
        break;
      }
    }
  }

 private:
  EType const mType;
};

class CacheStorage : public nsICacheStorage {
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSICACHESTORAGE

 public:
  CacheStorage(nsILoadContextInfo* aInfo, bool aAllowDisk, bool aSkipSizeCheck,
               bool aPinning);

 protected:
  virtual ~CacheStorage() = default;

  // Shared by asyncOpenURI and asyncOpenURIString once the latter has parsed
  // its spec. aURI must already have had its ref stripped.
  nsresult AsyncOpenInternal(nsIURI* aURI, const nsACString& aIdExtension,
                             uint32_t aFlags,
                             nsICacheEntryOpenCallback* aCallback);

  RefPtr<LoadContextInfo> mLoadContextInfo;
  bool mWriteToDisk : 1;
  bool mSkipSizeCheck : 1;
  bool mPinning : 1;

 public:
  nsILoadContextInfo* LoadInfo() const { return mLoadContextInfo; }
  bool WriteToDisk() const {
    return mWriteToDisk &&
           (!mLoadContextInfo || !mLoadContextInfo->IsPrivate());
  }
  bool SkipSizeCheck() const { return mSkipSizeCheck; }
  bool Pinning() const { return mPinning; }
};

}  // namespace net
}  // namespace mozilla

#endif
