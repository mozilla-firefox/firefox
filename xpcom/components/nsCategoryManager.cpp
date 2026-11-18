/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsCategoryManager.h"

#include "ManifestParser.h"
#include "mozilla/ArenaAllocatorExtensions.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/Services.h"
#include "mozilla/SimpleEnumerator.h"
#include "nsArrayEnumerator.h"
#include "nsCOMArray.h"
#include "nsCOMPtr.h"
#include "nsCRT.h"
#include "nsCategoryManagerUtils.h"
#include "nsClassHashtable.h"
#include "nsComponentManagerUtils.h"
#include "nsEnumeratorUtils.h"
#include "nsIObserver.h"
#include "nsIObserverService.h"
#include "nsPrintfCString.h"
#include "nsReadableUtils.h"
#include "nsServiceManagerUtils.h"
#include "nsSimpleEnumerator.h"
#include "nsStringEnumerator.h"
#include "nsSupportsPrimitives.h"
#include "nsTHashtable.h"
#include "nsThreadUtils.h"
#include "prio.h"
#include "prlock.h"

using namespace mozilla;
class nsIComponentLoaderManager;

/*
  CategoryDatabase
  contains 0 or more 1-1 mappings of string to Category
  each Category contains 0 or more 1-1 mappings of string keys to string values

  In other words, the CategoryDatabase is a tree, whose root is a hashtable.
  Internal nodes (or Categories) are hashtables. Leaf nodes are strings.

  The leaf strings are allocated in an arena, because we assume they're not
  going to change much ;)
*/

//
// CategoryEnumerator class
//

class CategoryEnumerator : public nsSimpleEnumerator,
                           private nsStringEnumeratorBase {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSISIMPLEENUMERATOR
  NS_DECL_NSIUTF8STRINGENUMERATOR

  using nsStringEnumeratorBase::GetNext;

  const nsID& DefaultInterface() override {
    return NS_GET_IID(nsISupportsCString);
  }

  static CategoryEnumerator* Create(
      nsClassHashtable<nsDepCharHashKey, CategoryNode>& aTable);

 protected:
  CategoryEnumerator()
      : mArray(nullptr), mCount(0), mSimpleCurItem(0), mStringCurItem(0) {}

  ~CategoryEnumerator() override { delete[] mArray; }

  const char** mArray;
  uint32_t mCount;
  uint32_t mSimpleCurItem;
  uint32_t mStringCurItem;
};

NS_IMPL_ISUPPORTS_INHERITED(CategoryEnumerator, nsSimpleEnumerator,
                            nsIUTF8StringEnumerator, nsIStringEnumerator)

NS_IMETHODIMP
CategoryEnumerator::HasMoreElements(bool* aResult) {
  *aResult = (mSimpleCurItem < mCount);

  return NS_OK;
}

NS_IMETHODIMP
CategoryEnumerator::GetNext(nsISupports** aResult) {
  if (mSimpleCurItem >= mCount) {
    return NS_ERROR_FAILURE;
  }

  auto* str = new nsSupportsDependentCString(mArray[mSimpleCurItem++]);

  *aResult = str;
  NS_ADDREF(*aResult);
  return NS_OK;
}

NS_IMETHODIMP
CategoryEnumerator::HasMore(bool* aResult) {
  *aResult = (mStringCurItem < mCount);

  return NS_OK;
}

NS_IMETHODIMP
CategoryEnumerator::GetNext(nsACString& aResult) {
  if (mStringCurItem >= mCount) {
    return NS_ERROR_FAILURE;
  }

  aResult = nsDependentCString(mArray[mStringCurItem++]);
  return NS_OK;
}

CategoryEnumerator* CategoryEnumerator::Create(
    nsClassHashtable<nsDepCharHashKey, CategoryNode>& aTable) {
  auto* enumObj = new CategoryEnumerator();
  if (!enumObj) {
    return nullptr;
  }

  enumObj->mArray = new const char*[aTable.Count()];
  if (!enumObj->mArray) {
    delete enumObj;
    return nullptr;
  }

  for (const auto& entry : aTable) {
    // if a category has no entries, we pretend it doesn't exist
    CategoryNode* aNode = entry.GetRawPtr();
    if (aNode->Count()) {
      enumObj->mArray[enumObj->mCount++] = entry.GetKey();
    }
  }

  return enumObj;
}

class CategoryEntry final : public nsICategoryEntry {
  NS_DECL_ISUPPORTS
  NS_DECL_NSICATEGORYENTRY
  NS_DECL_NSISUPPORTSCSTRING
  NS_DECL_NSISUPPORTSPRIMITIVE

  CategoryEntry(const char* aKey, const char* aValue)
      : mKey(aKey), mValue(aValue) {}

  const char* Key() const { return mKey; }

  static CategoryEntry* Cast(nsICategoryEntry* aEntry) {
    return static_cast<CategoryEntry*>(aEntry);
  }

 private:
  ~CategoryEntry() = default;

  const char* mKey;
  const char* mValue;
};

NS_IMPL_ISUPPORTS(CategoryEntry, nsICategoryEntry, nsISupportsCString)

nsresult CategoryEntry::ToString(char** aResult) {
  *aResult = moz_xstrdup(mKey);
  return NS_OK;
}

nsresult CategoryEntry::GetType(uint16_t* aType) {
  *aType = TYPE_CSTRING;
  return NS_OK;
}

nsresult CategoryEntry::GetData(nsACString& aData) {
  aData = mKey;
  return NS_OK;
}

nsresult CategoryEntry::SetData(const nsACString& aData) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult CategoryEntry::GetEntry(nsACString& aEntry) {
  aEntry = mKey;
  return NS_OK;
}

nsresult CategoryEntry::GetValue(nsACString& aValue) {
  aValue = mValue;
  return NS_OK;
}

static nsresult CreateEntryEnumerator(nsTHashtable<CategoryLeaf>& aTable,
                                      nsISimpleEnumerator** aResult) {
  nsCOMArray<nsICategoryEntry> entries(aTable.Count());

  for (auto iter = aTable.Iter(); !iter.Done(); iter.Next()) {
    CategoryLeaf* leaf = iter.Get();
    if (leaf->value) {
      entries.AppendElement(new CategoryEntry(leaf->GetKey(), leaf->value));
    }
  }

  entries.Sort([](nsICategoryEntry* aA, nsICategoryEntry* aB) {
    return strcmp(CategoryEntry::Cast(aA)->Key(),
                  CategoryEntry::Cast(aB)->Key());
  });

  return NS_NewArrayEnumerator(aResult, entries, NS_GET_IID(nsICategoryEntry));
}

//
// CategoryNode implementations
//

CategoryNode* CategoryNode::Create(CategoryAllocator* aArena) {
  return new (aArena) CategoryNode();
}

CategoryNode::~CategoryNode() = default;

void* CategoryNode::operator new(size_t aSize, CategoryAllocator* aArena) {
  return aArena->Allocate(aSize, mozilla::fallible);
}

static inline const char* MaybeStrdup(const nsACString& aStr,
                                      CategoryAllocator* aArena) {
  if (aStr.IsLiteral()) {
    return aStr.BeginReading();
  }
  return ArenaStrdup(PromiseFlatCString(aStr).get(), *aArena);
}

nsresult CategoryNode::GetLeaf(const nsACString& aEntryName,
                               nsACString& aResult) {
  MutexAutoLock lock(mLock);
  nsresult rv = NS_ERROR_NOT_AVAILABLE;
  CategoryLeaf* ent = mTable.GetEntry(PromiseFlatCString(aEntryName).get());

  if (ent && ent->value) {
    aResult.Assign(ent->value);
    return NS_OK;
  }

  return rv;
}

nsresult CategoryNode::AddLeaf(const nsACString& aEntryName,
                               const nsACString& aValue, bool aReplace,
                               nsACString& aResult, CategoryAllocator* aArena) {
  aResult.SetIsVoid(true);

  auto entryName = PromiseFlatCString(aEntryName);

  MutexAutoLock lock(mLock);
  CategoryLeaf* leaf = mTable.GetEntry(entryName.get());

  if (!leaf) {
    leaf = mTable.PutEntry(MaybeStrdup(aEntryName, aArena));
    if (!leaf) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
  }

  if (leaf->value && !aReplace) {
    return NS_ERROR_INVALID_ARG;
  }

  if (leaf->value) {
    aResult.AssignLiteral(leaf->value, strlen(leaf->value));
  } else {
    aResult.SetIsVoid(true);
  }
  leaf->value = MaybeStrdup(aValue, aArena);
  return NS_OK;
}

void CategoryNode::DeleteLeaf(const nsACString& aEntryName) {
  // we don't throw any errors, because it normally doesn't matter
  // and it makes JS a lot cleaner
  MutexAutoLock lock(mLock);

  // we can just remove the entire hash entry without introspection
  mTable.RemoveEntry(PromiseFlatCString(aEntryName).get());
}

nsresult CategoryNode::Enumerate(nsISimpleEnumerator** aResult) {
  MutexAutoLock lock(mLock);
  return CreateEntryEnumerator(mTable, aResult);
}

size_t CategoryNode::SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) {
  // We don't measure the strings pointed to by the entries because the
  // pointers are non-owning.
  MutexAutoLock lock(mLock);
  return mTable.ShallowSizeOfExcludingThis(aMallocSizeOf);
}

//
// nsCategoryManager implementations
//

NS_IMPL_QUERY_INTERFACE(nsCategoryManager, nsICategoryManager,
                        nsIMemoryReporter)

NS_IMETHODIMP_(MozExternalRefCountType)
nsCategoryManager::AddRef() { return 2; }

NS_IMETHODIMP_(MozExternalRefCountType)
nsCategoryManager::Release() { return 1; }

nsCategoryManager* nsCategoryManager::gCategoryManager;

/* static */
nsCategoryManager* nsCategoryManager::GetSingleton() {
  if (!gCategoryManager) {
    gCategoryManager = new nsCategoryManager();
  }
  return gCategoryManager;
}

/* static */
void nsCategoryManager::Destroy() {
  // The nsMemoryReporterManager gets destroyed before the nsCategoryManager,
  // so we don't need to unregister the nsCategoryManager as a memory reporter.
  // In debug builds we assert that unregistering fails, as a way (imperfect
  // but better than nothing) of testing the "destroyed before" part.
  MOZ_ASSERT(NS_FAILED(UnregisterWeakMemoryReporter(gCategoryManager)));

  delete gCategoryManager;
  gCategoryManager = nullptr;
}

nsresult nsCategoryManager::Create(REFNSIID aIID, void** aResult) {
  return GetSingleton()->QueryInterface(aIID, aResult);
}

nsCategoryManager::nsCategoryManager()
    : mLock("nsCategoryManager"), mSuppressNotifications(false) {}

void nsCategoryManager::InitMemoryReporter() {
  RegisterWeakMemoryReporter(this);
}

nsCategoryManager::~nsCategoryManager() {
  // the hashtable contains entries that must be deleted before the arena is
  // destroyed, or else you will have PRLocks undestroyed and other Really
  // Bad Stuff (TM)
  mTable.Clear();
}

inline CategoryNode* nsCategoryManager::get_category(const nsACString& aName) {
  CategoryNode* node;
  if (!mTable.Get(PromiseFlatCString(aName).get(), &node)) {
    return nullptr;
  }
  return node;
}

MOZ_DEFINE_MALLOC_SIZE_OF(CategoryManagerMallocSizeOf)

NS_IMETHODIMP
nsCategoryManager::CollectReports(nsIHandleReportCallback* aHandleReport,
                                  nsISupports* aData, bool aAnonymize) {
  MOZ_COLLECT_REPORT("explicit/xpcom/category-manager", KIND_HEAP, UNITS_BYTES,
                     SizeOfIncludingThis(CategoryManagerMallocSizeOf),
                     "Memory used for the XPCOM category manager.");

  return NS_OK;
}

size_t nsCategoryManager::SizeOfIncludingThis(
    mozilla::MallocSizeOf aMallocSizeOf) {
  MOZ_ASSERT(NS_IsMainThread());
  MutexAutoLock lock(mLock);
  size_t n = aMallocSizeOf(this);

  n += mArena.SizeOfExcludingThis(aMallocSizeOf);

  n += mTable.ShallowSizeOfExcludingThis(aMallocSizeOf);
  for (const auto& data : mTable.Values()) {
    // We don't measure the key string because it's a non-owning pointer.
    n += data->SizeOfExcludingThis(aMallocSizeOf);
  }

  return n;
}

namespace {

class CategoryNotificationRunnable : public Runnable {
 public:
  CategoryNotificationRunnable(nsISupports* aSubject, const char* aTopic,
                               const nsACString& aData)
      : Runnable("CategoryNotificationRunnable"),
        mSubject(aSubject),
        mTopic(aTopic),
        mData(aData) {}

  NS_DECL_NSIRUNNABLE

 private:
  nsCOMPtr<nsISupports> mSubject;
  const char* mTopic;
  NS_ConvertUTF8toUTF16 mData;
};

NS_IMETHODIMP
CategoryNotificationRunnable::Run() {
  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();
  if (observerService) {
    observerService->NotifyObservers(mSubject, mTopic, mData.get());
  }

  return NS_OK;
}

}  // namespace

void nsCategoryManager::NotifyObservers(const char* aTopic,
                                        const nsACString& aCategoryName,
                                        const nsACString& aEntryName) {
  if (mSuppressNotifications) {
    return;
  }

  RefPtr<CategoryNotificationRunnable> r;

  if (aEntryName.Length()) {
    nsCOMPtr<nsISupportsCString> entry =
        do_CreateInstance(NS_SUPPORTS_CSTRING_CONTRACTID);
    if (!entry) {
      return;
    }

    nsresult rv = entry->SetData(aEntryName);
    if (NS_FAILED(rv)) {
      return;
    }

    r = new CategoryNotificationRunnable(entry, aTopic, aCategoryName);
  } else {
    r = new CategoryNotificationRunnable(
        NS_ISUPPORTS_CAST(nsICategoryManager*, this), aTopic, aCategoryName);
  }

  NS_DispatchToMainThread(r);
}

NS_IMETHODIMP
nsCategoryManager::GetCategoryEntry(const nsACString& aCategoryName,
                                    const nsACString& aEntryName,
                                    nsACString& aResult) {
  nsresult status = NS_ERROR_NOT_AVAILABLE;

  CategoryNode* category;
  {
    MutexAutoLock lock(mLock);
    category = get_category(aCategoryName);
  }

  if (category) {
    status = category->GetLeaf(aEntryName, aResult);
  }

  return status;
}

NS_IMETHODIMP
nsCategoryManager::AddCategoryEntry(const nsACString& aCategoryName,
                                    const nsACString& aEntryName,
                                    const nsACString& aValue, bool aPersist,
                                    bool aReplace, nsACString& aResult) {
  if (aPersist) {
    NS_ERROR("Category manager doesn't support persistence.");
    return NS_ERROR_INVALID_ARG;
  }

  AddCategoryEntry(aCategoryName, aEntryName, aValue, aReplace, aResult);
  return NS_OK;
}

void nsCategoryManager::AddCategoryEntry(const nsACString& aCategoryName,
                                         const nsACString& aEntryName,
                                         const nsACString& aValue,
                                         bool aReplace, nsACString& aOldValue) {
  MOZ_ASSERT(NS_IsMainThread());
  aOldValue.SetIsVoid(true);

  // Before we can insert a new entry, we'll need to
  //  find the |CategoryNode| to put it in...
  CategoryNode* category;
  {
    MutexAutoLock lock(mLock);
    category = get_category(aCategoryName);

    if (!category) {
      // That category doesn't exist yet; let's make it.
      category = mTable
                     .InsertOrUpdate(
                         MaybeStrdup(aCategoryName, &mArena),
                         UniquePtr<CategoryNode>{CategoryNode::Create(&mArena)})
                     .get();
    }
  }

  if (!category) {
    return;
  }

  nsresult rv =
      category->AddLeaf(aEntryName, aValue, aReplace, aOldValue, &mArena);

  if (NS_SUCCEEDED(rv)) {
    if (!aOldValue.IsEmpty()) {
      NotifyObservers(NS_XPCOM_CATEGORY_ENTRY_REMOVED_OBSERVER_ID,
                      aCategoryName, aEntryName);
    }
    NotifyObservers(NS_XPCOM_CATEGORY_ENTRY_ADDED_OBSERVER_ID, aCategoryName,
                    aEntryName);
  }
}

NS_IMETHODIMP
nsCategoryManager::DeleteCategoryEntry(const nsACString& aCategoryName,
                                       const nsACString& aEntryName,
                                       bool aDontPersist) {
  /*
    Note: no errors are reported since failure to delete
    probably won't hurt you, and returning errors seriously
    inconveniences JS clients
  */

  CategoryNode* category;
  {
    MutexAutoLock lock(mLock);
    category = get_category(aCategoryName);
  }

  if (category) {
    category->DeleteLeaf(aEntryName);

    NotifyObservers(NS_XPCOM_CATEGORY_ENTRY_REMOVED_OBSERVER_ID, aCategoryName,
                    aEntryName);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsCategoryManager::DeleteCategory(const nsACString& aCategoryName) {
  // the categories are arena-allocated, so we don't
  // actually delete them. We just remove all of the
  // leaf nodes.

  CategoryNode* category;
  {
    MutexAutoLock lock(mLock);
    category = get_category(aCategoryName);
  }

  if (category) {
    category->Clear();
    NotifyObservers(NS_XPCOM_CATEGORY_CLEARED_OBSERVER_ID, aCategoryName,
                    VoidCString());
  }

  return NS_OK;
}

NS_IMETHODIMP
nsCategoryManager::EnumerateCategory(const nsACString& aCategoryName,
                                     nsISimpleEnumerator** aResult) {
  CategoryNode* category;
  {
    MutexAutoLock lock(mLock);
    category = get_category(aCategoryName);
  }

  if (!category) {
    return NS_NewEmptyEnumerator(aResult);
  }

  return category->Enumerate(aResult);
}

NS_IMETHODIMP
nsCategoryManager::EnumerateCategories(nsISimpleEnumerator** aResult) {
  if (NS_WARN_IF(!aResult)) {
    return NS_ERROR_INVALID_ARG;
  }

  MutexAutoLock lock(mLock);
  CategoryEnumerator* enumObj = CategoryEnumerator::Create(mTable);

  if (!enumObj) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  *aResult = enumObj;
  NS_ADDREF(*aResult);
  return NS_OK;
}

struct writecat_struct {
  PRFileDesc* fd;
  bool success;
};

nsresult nsCategoryManager::SuppressNotifications(bool aSuppress) {
  mSuppressNotifications = aSuppress;
  return NS_OK;
}

/*
 * CreateServicesFromCategory()
 *
 * Given a category, this convenience functions enumerates the category and
 * creates a service of every CID or ContractID registered under the category.
 * If observerTopic is non null and the service implements nsIObserver,
 * this will attempt to notify the observer with the origin, observerTopic
 * string as parameter.
 */
void NS_CreateServicesFromCategory(const char* aCategory, nsISupports* aOrigin,
                                   const char* aObserverTopic,
                                   const char16_t* aObserverData) {
  nsresult rv;

  nsCOMPtr<nsICategoryManager> categoryManager =
      do_GetService("@mozilla.org/categorymanager;1");
  if (!categoryManager) {
    return;
  }

  nsDependentCString category(aCategory);

  nsCOMPtr<nsISimpleEnumerator> enumerator;
  rv = categoryManager->EnumerateCategory(category, getter_AddRefs(enumerator));
  if (NS_FAILED(rv)) {
    return;
  }

  for (auto& categoryEntry : SimpleEnumerator<nsICategoryEntry>(enumerator)) {
    // From here on just skip any error we get.
    nsAutoCString entryString;
    categoryEntry->GetEntry(entryString);

    nsAutoCString contractID;
    categoryEntry->GetValue(contractID);

    nsCOMPtr<nsISupports> instance = do_GetService(contractID.get());
    if (!instance) {
      LogMessage(
          "While creating services from category '%s', could not create "
          "service for entry '%s', contract ID '%s'",
          aCategory, entryString.get(), contractID.get());
      continue;
    }

    if (aObserverTopic) {
      // try an observer, if it implements it.
      nsCOMPtr<nsIObserver> observer = do_QueryInterface(instance);
      if (observer) {
        nsPrintfCString profilerStr("%s (%s)", aObserverTopic,
                                    entryString.get());
        AUTO_PROFILER_MARKER_TEXT("Category observer notification", OTHER,
                                  MarkerStack::Capture(), profilerStr);
        AUTO_PROFILER_LABEL_DYNAMIC_NSCSTRING_NONSENSITIVE(
            "Category observer notification -", OTHER, profilerStr);

        observer->Observe(aOrigin, aObserverTopic,
                          aObserverData ? aObserverData : u"");
      } else {
        LogMessage(
            "While creating services from category '%s', service for entry "
            "'%s', contract ID '%s' does not implement nsIObserver.",
            aCategory, entryString.get(), contractID.get());
      }
    }
  }
}
