/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <string.h>
#include "nsJARInputStream.h"
#include "nsJAR.h"
#include "nsIFile.h"
#include "nsIObserverService.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/Logging.h"
#include "mozilla/Omnijar.h"

#ifdef XP_UNIX
#  include <sys/stat.h>
#elif defined(XP_WIN)
#  include <io.h>
#endif

using namespace mozilla;

static LazyLogModule gJarLog("nsJAR");

#ifdef LOG
#  undef LOG
#endif
#ifdef LOG_ENABLED
#  undef LOG_ENABLED
#endif

#define LOG(args) MOZ_LOG(gJarLog, mozilla::LogLevel::Debug, args)
#define LOG_ENABLED() MOZ_LOG_TEST(gJarLog, mozilla::LogLevel::Debug)

//----------------------------------------------
// nsJAR constructor/destructor
//----------------------------------------------

// The following initialization makes a guess of 10 entries per jarfile.
nsJAR::nsJAR()
    : mReleaseTime(PR_INTERVAL_NO_TIMEOUT),
      mLock("nsJAR::mLock"),
      mCache(nullptr) {}

nsJAR::~nsJAR() { Close(); }

NS_IMPL_QUERY_INTERFACE(nsJAR, nsIZipReader)
NS_IMPL_ADDREF(nsJAR)

// Custom Release method works with nsZipReaderCache...
// Release might be called from multi-thread, we have to
// take this function carefully to avoid delete-after-use.
MozExternalRefCountType nsJAR::Release(void) {
  nsrefcnt count;
  MOZ_ASSERT(0 != mRefCnt, "dup release");

  RefPtr<nsZipReaderCache> cache;
  if (mRefCnt == 2) {  // don't use a lock too frequently
    // Use a mutex here to guarantee mCache is not racing and the target
    // instance is still valid to increase ref-count.
    RecursiveMutexAutoLock lock(mLock);
    cache = mCache;
    mCache = nullptr;
  }
  if (cache) {
    DebugOnly<nsresult> rv = cache->ReleaseZip(this);
    MOZ_ASSERT(NS_SUCCEEDED(rv), "failed to release zip file");
  }

  count = --mRefCnt;  // don't access any member variable after this line
  NS_LOG_RELEASE(this, count, "nsJAR");
  if (0 == count) {
    mRefCnt = 1; /* stabilize */
    /* enable this to find non-threadsafe destructors: */
    /* NS_ASSERT_OWNINGTHREAD(nsJAR); */
    delete this;
    return 0;
  }

  return count;
}

//----------------------------------------------
// nsIZipReader implementation
//----------------------------------------------

NS_IMETHODIMP
nsJAR::Open(nsIFile* zipFile) {
  NS_ENSURE_ARG_POINTER(zipFile);
  RecursiveMutexAutoLock lock(mLock);
  LOG(("Open[%p] %s", this, zipFile->HumanReadablePath().get()));
  if (mZip) return NS_ERROR_FAILURE;  // Already open!

  mZipFile = zipFile;
  mOuterZipEntry.Truncate();

  // The omnijar is special, it is opened early on and closed late
  RefPtr<nsZipArchive> zip = mozilla::Omnijar::GetReader(zipFile);
  if (!zip) {
    zip = nsZipArchive::OpenArchive(zipFile);
  }
  mZip = zip;
  return mZip ? NS_OK : NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsJAR::OpenInner(nsIZipReader* aZipReader, const nsACString& aZipEntry) {
  nsresult rv;

  LOG(("OpenInner[%p] %s", this, PromiseFlatCString(aZipEntry).get()));
  NS_ENSURE_ARG_POINTER(aZipReader);

  nsCOMPtr<nsIFile> zipFile;
  rv = aZipReader->GetFile(getter_AddRefs(zipFile));
  NS_ENSURE_SUCCESS(rv, rv);

  RefPtr<nsZipArchive> innerZip =
      mozilla::Omnijar::GetInnerReader(zipFile, aZipEntry);
  if (innerZip) {
    RecursiveMutexAutoLock lock(mLock);
    if (mZip) {
      return NS_ERROR_FAILURE;
    }
    mZip = innerZip;
    return NS_OK;
  }

  bool exist;
  rv = aZipReader->HasEntry(aZipEntry, &exist);
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_TRUE(exist, NS_ERROR_FILE_NOT_FOUND);

  RefPtr<nsZipHandle> handle;
  {
    nsJAR* outerJAR = static_cast<nsJAR*>(aZipReader);
    RecursiveMutexAutoLock outerLock(outerJAR->mLock);
    rv = nsZipHandle::Init(outerJAR->mZip.get(), aZipEntry,
                           getter_AddRefs(handle));
    NS_ENSURE_SUCCESS(rv, rv);
  }

  RecursiveMutexAutoLock lock(mLock);
  MOZ_ASSERT(!mZip, "Another thread tried to open this nsJAR racily!");
  mZipFile = zipFile.forget();
  mOuterZipEntry.Assign(aZipEntry);
  mZip = nsZipArchive::OpenArchive(handle);
  return mZip ? NS_OK : NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsJAR::OpenMemory(void* aData, uint32_t aLength) {
  NS_ENSURE_ARG_POINTER(aData);
  RecursiveMutexAutoLock lock(mLock);
  if (mZip) return NS_ERROR_FAILURE;  // Already open!

  RefPtr<nsZipHandle> handle;
  nsresult rv = nsZipHandle::Init(static_cast<uint8_t*>(aData), aLength,
                                  getter_AddRefs(handle));
  if (NS_FAILED(rv)) return rv;

  mZip = nsZipArchive::OpenArchive(handle);
  return mZip ? NS_OK : NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsJAR::GetFile(nsIFile** result) {
  RecursiveMutexAutoLock lock(mLock);
  LOG(("GetFile[%p]", this));
  *result = mZipFile;
  NS_IF_ADDREF(*result);
  return NS_OK;
}

NS_IMETHODIMP
nsJAR::Close() {
  RecursiveMutexAutoLock lock(mLock);
  LOG(("Close[%p]", this));
  if (!mZip) {
    return NS_ERROR_FAILURE;  // Never opened or already closed.
  }

  mZip = nullptr;
  return NS_OK;
}

NS_IMETHODIMP
nsJAR::Test(const nsACString& aEntryName) {
  RecursiveMutexAutoLock lock(mLock);
  if (!mZip) {
    return NS_ERROR_FAILURE;
  }
  return mZip->Test(aEntryName);
}

NS_IMETHODIMP
nsJAR::Extract(const nsACString& aEntryName, nsIFile* outFile) {
  // nsZipArchive and zlib are not thread safe
  // we need to use a lock to prevent bug #51267
  RecursiveMutexAutoLock lock(mLock);
  if (!mZip) {
    return NS_ERROR_FAILURE;
  }

  LOG(("Extract[%p] %s", this, PromiseFlatCString(aEntryName).get()));
  nsZipItem* item = mZip->GetItem(aEntryName);
  NS_ENSURE_TRUE(item, NS_ERROR_FILE_NOT_FOUND);

  // Remove existing file or directory so we set permissions correctly.
  // If it's a directory that already exists and contains files, throw
  // an exception and return.

  nsresult rv = outFile->Remove(false);
  if (rv == NS_ERROR_FILE_DIR_NOT_EMPTY || rv == NS_ERROR_FAILURE) return rv;

  if (item->IsDirectory()) {
    rv = outFile->Create(nsIFile::DIRECTORY_TYPE, item->Mode());
    // XXX Do this in nsZipArchive?  It would be nice to keep extraction
    // XXX code completely there, but that would require a way to get a
    // XXX PRDir from outFile.
  } else {
    PRFileDesc* fd;
    rv = outFile->OpenNSPRFileDesc(PR_WRONLY | PR_CREATE_FILE, item->Mode(),
                                   &fd);
    if (NS_FAILED(rv)) return rv;

    // ExtractFile also closes the fd handle and resolves the symlink if needed
    rv = mZip->ExtractFile(item, outFile, fd);
  }
  if (NS_FAILED(rv)) return rv;

  // nsIFile needs milliseconds, while prtime is in microseconds.
  // non-fatal if this fails, ignore errors
  outFile->SetLastModifiedTime(item->LastModTime() / PR_USEC_PER_MSEC);

  return NS_OK;
}

NS_IMETHODIMP
nsJAR::GetEntry(const nsACString& aEntryName, nsIZipEntry** result) {
  RecursiveMutexAutoLock lock(mLock);
  LOG(("GetEntry[%p] %s", this, PromiseFlatCString(aEntryName).get()));
  if (!mZip) {
    return NS_ERROR_FAILURE;
  }
  nsZipItem* zipItem = mZip->GetItem(aEntryName);
  NS_ENSURE_TRUE(zipItem, NS_ERROR_FILE_NOT_FOUND);

  RefPtr<nsJARItem> jarItem = new nsJARItem(zipItem);

  *result = jarItem.forget().take();
  return NS_OK;
}

NS_IMETHODIMP
nsJAR::HasEntry(const nsACString& aEntryName, bool* result) {
  RecursiveMutexAutoLock lock(mLock);
  LOG(("HasEntry[%p] %s", this, PromiseFlatCString(aEntryName).get()));
  if (!mZip) {
    return NS_ERROR_FAILURE;
  }
  *result = mZip->GetItem(aEntryName) != nullptr;
  return NS_OK;
}

NS_IMETHODIMP
nsJAR::FindEntries(const nsACString& aPattern,
                   nsIUTF8StringEnumerator** result) {
  NS_ENSURE_ARG_POINTER(result);
  RecursiveMutexAutoLock lock(mLock);
  LOG(("FindEntries[%p] %s", this, PromiseFlatCString(aPattern).get()));
  if (!mZip) {
    return NS_ERROR_FAILURE;
  }

  nsZipFind* find;
  nsresult rv = mZip->FindInit(
      aPattern.IsEmpty() ? nullptr : PromiseFlatCString(aPattern).get(), &find);
  NS_ENSURE_SUCCESS(rv, rv);

  RefPtr<nsIUTF8StringEnumerator> zipEnum = new nsJAREnumerator(find);

  // Callers use getter_addrefs
  *result = zipEnum.forget().take();
  return NS_OK;
}

NS_IMETHODIMP
nsJAR::GetInputStream(const nsACString& aEntryName, nsIInputStream** result) {
  NS_ENSURE_ARG_POINTER(result);
  RecursiveMutexAutoLock lock(mLock);
  if (!mZip) {
    return NS_ERROR_FAILURE;
  }

  LOG(("GetInputStream[%p] %s", this, PromiseFlatCString(aEntryName).get()));
  // Watch out for the jar:foo.zip!/ (aDir is empty) top-level special case!
  nsZipItem* item = nullptr;
  const nsCString& entry = PromiseFlatCString(aEntryName);
  if (*entry.get()) {
    // First check if item exists in jar
    item = mZip->GetItem(entry);
    if (!item) return NS_ERROR_FILE_NOT_FOUND;
  }
  RefPtr<nsJARInputStream> jis = new nsJARInputStream();

  nsresult rv = NS_OK;
  if (!item || item->IsDirectory()) {
    rv = jis->InitDirectory(this, entry.get());
  } else {
    RefPtr<nsZipHandle> fd = mZip->GetFD();
    rv = jis->InitFile(fd, mZip->GetData(item), item);
  }
  if (NS_SUCCEEDED(rv)) {
    // Callers use getter_addrefs
    *result = jis.forget().take();
  }
  return rv;
}

nsresult nsJAR::GetFullJarPath(nsACString& aResult) {
  RecursiveMutexAutoLock lock(mLock);
  NS_ENSURE_ARG_POINTER(mZipFile);

  nsresult rv = mZipFile->GetPersistentDescriptor(aResult);
  if (NS_FAILED(rv)) {
    return rv;
  }

  if (mOuterZipEntry.IsEmpty()) {
    aResult.InsertLiteral("file:", 0);
  } else {
    aResult.InsertLiteral("jar:", 0);
    aResult.AppendLiteral("!/");
    aResult.Append(mOuterZipEntry);
  }
  return NS_OK;
}

nsresult nsJAR::GetNSPRFileDesc(PRFileDesc** aNSPRFileDesc) {
  RecursiveMutexAutoLock lock(mLock);
  if (!aNSPRFileDesc) {
    return NS_ERROR_ILLEGAL_VALUE;
  }
  *aNSPRFileDesc = nullptr;

  if (!mZip) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<nsZipHandle> handle = mZip->GetFD();
  if (!handle) {
    return NS_ERROR_FAILURE;
  }

  return handle->GetNSPRFileDesc(aNSPRFileDesc);
}

//----------------------------------------------
// nsJAR private implementation
//----------------------------------------------
nsresult nsJAR::LoadEntry(const nsACString& aFilename, nsCString& aBuf) {
  //-- Get a stream for reading the file
  nsresult rv;
  nsCOMPtr<nsIInputStream> manifestStream;
  rv = GetInputStream(aFilename, getter_AddRefs(manifestStream));
  if (NS_FAILED(rv)) return NS_ERROR_FILE_NOT_FOUND;

  //-- Read the manifest file into memory
  char* buf;
  uint64_t len64;
  rv = manifestStream->Available(&len64);
  if (NS_FAILED(rv)) return rv;
  NS_ENSURE_TRUE(len64 < UINT32_MAX, NS_ERROR_FILE_CORRUPTED);  // bug 164695
  uint32_t len = (uint32_t)len64;
  buf = (char*)malloc(len + 1);
  if (!buf) return NS_ERROR_OUT_OF_MEMORY;
  uint32_t bytesRead;
  rv = manifestStream->Read(buf, len, &bytesRead);
  if (bytesRead != len) {
    rv = NS_ERROR_FILE_CORRUPTED;
  }
  if (NS_FAILED(rv)) {
    free(buf);
    return rv;
  }
  buf[len] = '\0';  // Null-terminate the buffer
  aBuf.Adopt(buf, len);
  return NS_OK;
}

int32_t nsJAR::ReadLine(const char** src) {
  if (!*src) {
    return 0;
  }

  //--Moves pointer to beginning of next line and returns line length
  //  not including CR/LF.
  int32_t length;
  const char* eol = strpbrk(*src, "\r\n");

  if (eol == nullptr)  // Probably reached end of file before newline
  {
    length = strlen(*src);
    if (length == 0)  // immediate end-of-file
      *src = nullptr;
    else  // some data left on this line
      *src += length;
  } else {
    length = eol - *src;
    if (eol[0] == '\r' && eol[1] == '\n')  // CR LF, so skip 2
      *src = eol + 2;
    else  // Either CR or LF, so skip 1
      *src = eol + 1;
  }
  return length;
}

NS_IMPL_ISUPPORTS(nsJAREnumerator, nsIUTF8StringEnumerator, nsIStringEnumerator)

//----------------------------------------------
// nsJAREnumerator::HasMore
//----------------------------------------------
NS_IMETHODIMP
nsJAREnumerator::HasMore(bool* aResult) {
  // try to get the next element
  if (!mName) {
    NS_ASSERTION(mFind, "nsJAREnumerator: Missing zipFind.");
    nsresult rv = mFind->FindNext(&mName, &mNameLen);
    if (rv == NS_ERROR_FILE_NOT_FOUND) {
      *aResult = false;  // No more matches available
      return NS_OK;
    }
    NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);  // no error translation
  }

  *aResult = true;
  return NS_OK;
}

//----------------------------------------------
// nsJAREnumerator::GetNext
//----------------------------------------------
NS_IMETHODIMP
nsJAREnumerator::GetNext(nsACString& aResult) {
  // check if the current item is "stale"
  if (!mName) {
    bool bMore;
    nsresult rv = HasMore(&bMore);
    if (NS_FAILED(rv) || !bMore)
      return NS_ERROR_FAILURE;  // no error translation
  }
  aResult.Assign(mName, mNameLen);
  mName = nullptr;  // we just gave this one away
  return NS_OK;
}

NS_IMPL_ISUPPORTS(nsJARItem, nsIZipEntry)

nsJARItem::nsJARItem(nsZipItem* aZipItem)
    : mSize(aZipItem->Size()),
      mRealsize(aZipItem->RealSize()),
      mCrc32(aZipItem->CRC32()),
      mLastModTime(aZipItem->LastModTime()),
      mCompression(aZipItem->Compression()),
      mPermissions(aZipItem->Mode()),
      mIsDirectory(aZipItem->IsDirectory()),
      mIsSynthetic(aZipItem->isSynthetic) {}

//------------------------------------------
// nsJARItem::GetCompression
//------------------------------------------
NS_IMETHODIMP
nsJARItem::GetCompression(uint16_t* aCompression) {
  NS_ENSURE_ARG_POINTER(aCompression);

  *aCompression = mCompression;
  return NS_OK;
}

//------------------------------------------
// nsJARItem::GetSize
//------------------------------------------
NS_IMETHODIMP
nsJARItem::GetSize(uint32_t* aSize) {
  NS_ENSURE_ARG_POINTER(aSize);

  *aSize = mSize;
  return NS_OK;
}

//------------------------------------------
// nsJARItem::GetRealSize
//------------------------------------------
NS_IMETHODIMP
nsJARItem::GetRealSize(uint32_t* aRealsize) {
  NS_ENSURE_ARG_POINTER(aRealsize);

  *aRealsize = mRealsize;
  return NS_OK;
}

//------------------------------------------
// nsJARItem::GetCrc32
//------------------------------------------
NS_IMETHODIMP
nsJARItem::GetCRC32(uint32_t* aCrc32) {
  NS_ENSURE_ARG_POINTER(aCrc32);

  *aCrc32 = mCrc32;
  return NS_OK;
}

//------------------------------------------
// nsJARItem::GetIsDirectory
//------------------------------------------
NS_IMETHODIMP
nsJARItem::GetIsDirectory(bool* aIsDirectory) {
  NS_ENSURE_ARG_POINTER(aIsDirectory);

  *aIsDirectory = mIsDirectory;
  return NS_OK;
}

//------------------------------------------
// nsJARItem::GetIsSynthetic
//------------------------------------------
NS_IMETHODIMP
nsJARItem::GetIsSynthetic(bool* aIsSynthetic) {
  NS_ENSURE_ARG_POINTER(aIsSynthetic);

  *aIsSynthetic = mIsSynthetic;
  return NS_OK;
}

//------------------------------------------
// nsJARItem::GetLastModifiedTime
//------------------------------------------
NS_IMETHODIMP
nsJARItem::GetLastModifiedTime(PRTime* aLastModTime) {
  NS_ENSURE_ARG_POINTER(aLastModTime);

  *aLastModTime = mLastModTime;
  return NS_OK;
}

//------------------------------------------
// nsJARItem::GetPermissions
//------------------------------------------
NS_IMETHODIMP
nsJARItem::GetPermissions(uint32_t* aPermissions) {
  NS_ENSURE_ARG_POINTER(aPermissions);

  *aPermissions = mPermissions;
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// nsIZipReaderCache

NS_IMPL_ISUPPORTS(nsZipReaderCache, nsIZipReaderCache, nsIObserver,
                  nsISupportsWeakReference)

nsZipReaderCache::nsZipReaderCache()
    : mLock("nsZipReaderCache.mLock"),
      mCacheSize(0),
      mZips()
#ifdef ZIP_CACHE_HIT_RATE
      ,
      mZipCacheLookups(0),
      mZipCacheHits(0),
      mZipCacheFlushes(0),
      mZipSyncMisses(0)
#endif
{
}

NS_IMETHODIMP
nsZipReaderCache::Init(uint32_t cacheSize) {
  MutexAutoLock lock(mLock);
  mCacheSize = cacheSize;

  // Register as a memory pressure observer
  nsCOMPtr<nsIObserverService> os =
      do_GetService("@mozilla.org/observer-service;1");
  if (os) {
    os->AddObserver(this, "memory-pressure", true);
    os->AddObserver(this, "chrome-flush-caches", true);
    os->AddObserver(this, "flush-cache-entry", true);
  }
  // ignore failure of the observer registration.

  return NS_OK;
}

// Runs at the very end of XPCOM shutdown, where no service is available any
// more, not even the observer service we registered with in Init().
nsZipReaderCache::~nsZipReaderCache() {
  for (const auto& zip : mZips.Values()) {
    zip->SetZipReaderCache(nullptr);
  }

#ifdef ZIP_CACHE_HIT_RATE
  printf(
      "nsZipReaderCache size=%d hits=%d lookups=%d rate=%f%% flushes=%d missed "
      "%d\n",
      mCacheSize, mZipCacheHits, mZipCacheLookups,
      (float)mZipCacheHits / mZipCacheLookups, mZipCacheFlushes,
      mZipSyncMisses);
#endif
}

NS_IMETHODIMP
nsZipReaderCache::IsCached(nsIFile* zipFile, bool* aResult) {
  NS_ENSURE_ARG_POINTER(zipFile);
  nsresult rv;
  MutexAutoLock lock(mLock);

  nsAutoCString uri;
  rv = zipFile->GetPersistentDescriptor(uri);
  if (NS_FAILED(rv)) return rv;

  uri.InsertLiteral("file:", 0);

  *aResult = mZips.Contains(uri);
  return NS_OK;
}

nsresult nsZipReaderCache::GetZip(nsIFile* zipFile, nsIZipReader** result,
                                  bool failOnMiss) {
  NS_ENSURE_ARG_POINTER(zipFile);
  nsresult rv;
  MutexAutoLock lock(mLock);

#ifdef ZIP_CACHE_HIT_RATE
  mZipCacheLookups++;
#endif

  nsAutoCString uri;
  rv = zipFile->GetPersistentDescriptor(uri);
  if (NS_FAILED(rv)) return rv;

  uri.InsertLiteral("file:", 0);

  RefPtr<nsJAR> zip;
  mZips.Get(uri, getter_AddRefs(zip));
  if (zip) {
#ifdef ZIP_CACHE_HIT_RATE
    mZipCacheHits++;
#endif
    zip->ClearReleaseTime();
  } else {
    if (failOnMiss) {
      return NS_ERROR_CACHE_KEY_NOT_FOUND;
    }

    zip = new nsJAR();
    zip->SetZipReaderCache(this);
    rv = zip->Open(zipFile);
    if (NS_FAILED(rv)) {
      return rv;
    }

    MOZ_ASSERT(!mZips.Contains(uri));
    mZips.InsertOrUpdate(uri, RefPtr{zip});
  }
  zip.forget(result);
  return rv;
}

NS_IMETHODIMP
nsZipReaderCache::GetZipIfCached(nsIFile* zipFile, nsIZipReader** result) {
  return GetZip(zipFile, result, true);
}

NS_IMETHODIMP
nsZipReaderCache::GetZip(nsIFile* zipFile, nsIZipReader** result) {
  return GetZip(zipFile, result, false);
}

NS_IMETHODIMP
nsZipReaderCache::GetInnerZip(nsIFile* zipFile, const nsACString& entry,
                              nsIZipReader** result) {
  NS_ENSURE_ARG_POINTER(zipFile);

  nsCOMPtr<nsIZipReader> outerZipReader;
  nsresult rv = GetZip(zipFile, getter_AddRefs(outerZipReader));
  NS_ENSURE_SUCCESS(rv, rv);

  MutexAutoLock lock(mLock);

#ifdef ZIP_CACHE_HIT_RATE
  mZipCacheLookups++;
#endif

  nsAutoCString uri;
  rv = zipFile->GetPersistentDescriptor(uri);
  if (NS_FAILED(rv)) return rv;

  uri.InsertLiteral("jar:", 0);
  uri.AppendLiteral("!/");
  uri.Append(entry);

  RefPtr<nsJAR> zip;
  mZips.Get(uri, getter_AddRefs(zip));
  if (zip) {
#ifdef ZIP_CACHE_HIT_RATE
    mZipCacheHits++;
#endif
    zip->ClearReleaseTime();
  } else {
    zip = new nsJAR();
    zip->SetZipReaderCache(this);

    rv = zip->OpenInner(outerZipReader, entry);
    if (NS_FAILED(rv)) {
      return rv;
    }

    MOZ_ASSERT(!mZips.Contains(uri));
    mZips.InsertOrUpdate(uri, RefPtr{zip});
  }
  zip.forget(result);
  return rv;
}

NS_IMETHODIMP
nsZipReaderCache::GetFd(nsIFile* zipFile, PRFileDesc** aRetVal) {
#if defined(XP_WIN)
  MOZ_CRASH("Not implemented");
  return NS_ERROR_NOT_IMPLEMENTED;
#else
  if (!zipFile) {
    return NS_ERROR_FAILURE;
  }

  nsresult rv;
  nsAutoCString uri;
  rv = zipFile->GetPersistentDescriptor(uri);
  if (NS_FAILED(rv)) {
    return rv;
  }
  uri.InsertLiteral("file:", 0);

  MutexAutoLock lock(mLock);
  RefPtr<nsJAR> zip;
  mZips.Get(uri, getter_AddRefs(zip));
  if (!zip) {
    return NS_ERROR_FAILURE;
  }

  zip->ClearReleaseTime();
  rv = zip->GetNSPRFileDesc(aRetVal);
  // Do this to avoid possible deadlock on mLock with ReleaseZip().
  {
    MutexAutoUnlock unlock(mLock);
    zip = nullptr;
  }
  return rv;
#endif /* XP_WIN */
}

nsresult nsZipReaderCache::ReleaseZip(nsJAR* zip) {
  nsresult rv;
  MutexAutoLock lock(mLock);

  // It is possible that two thread compete for this zip. The dangerous
  // case is where one thread Releases the zip and discovers that the ref
  // count has gone to one. Before it can call this ReleaseZip method
  // another thread calls our GetZip method. The ref count goes to two. That
  // second thread then Releases the zip and the ref count goes to one. It
  // then tries to enter this ReleaseZip method and blocks while the first
  // thread is still here. The first thread continues and remove the zip from
  // the cache and calls its Release method sending the ref count to 0 and
  // deleting the zip. However, the second thread is still blocked at the
  // start of ReleaseZip, but the 'zip' param now hold a reference to a
  // deleted zip!
  //
  // So, we are going to try safeguarding here by searching our hashtable while
  // locked here for the zip. We return fast if it is not found.

  bool found = false;
  for (const auto& current : mZips.Values()) {
    if (zip == current) {
      found = true;
      break;
    }
  }

  if (!found) {
#ifdef ZIP_CACHE_HIT_RATE
    mZipSyncMisses++;
#endif
    return NS_OK;
  }

  zip->SetReleaseTime();

  if (mZips.Count() <= mCacheSize) return NS_OK;

  // Find the oldest zip.
  nsJAR* oldest = nullptr;
  for (const auto& current : mZips.Values()) {
    PRIntervalTime currentReleaseTime = current->GetReleaseTime();
    if (currentReleaseTime != PR_INTERVAL_NO_TIMEOUT) {
      if (oldest == nullptr || currentReleaseTime < oldest->GetReleaseTime()) {
        oldest = current;
      }
    }
  }

  // Because of the craziness above it is possible that there is no zip that
  // needs removing.
  if (!oldest) return NS_OK;

#ifdef ZIP_CACHE_HIT_RATE
  mZipCacheFlushes++;
#endif

  // remove from hashtable
  nsAutoCString uri;
  rv = oldest->GetFullJarPath(uri);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // Retrieving and removing the JAR should be done without an extra AddRef
  // and Release, or we'll trigger nsJAR::Release's magic refcount 1 case
  // an extra time.
  RefPtr<nsJAR> removed;
  mZips.Remove(uri, getter_AddRefs(removed));
  NS_ASSERTION(removed, "botched");
  NS_ASSERTION(oldest == removed, "removed wrong entry");

  if (removed) removed->SetZipReaderCache(nullptr);

  return NS_OK;
}

NS_IMETHODIMP
nsZipReaderCache::Observe(nsISupports* aSubject, const char* aTopic,
                          const char16_t* aSomeData) {
  if (strcmp(aTopic, "memory-pressure") == 0) {
    MutexAutoLock lock(mLock);
    for (auto iter = mZips.Iter(); !iter.Done(); iter.Next()) {
      RefPtr<nsJAR>& current = iter.Data();
      if (current->GetReleaseTime() != PR_INTERVAL_NO_TIMEOUT) {
        current->SetZipReaderCache(nullptr);
        iter.Remove();
      }
    }
  } else if (strcmp(aTopic, "chrome-flush-caches") == 0) {
    MutexAutoLock lock(mLock);
    for (const auto& current : mZips.Values()) {
      current->SetZipReaderCache(nullptr);
    }
    mZips.Clear();
  } else if (strcmp(aTopic, "flush-cache-entry") == 0) {
    nsCOMPtr<nsIFile> file;
    if (aSubject) {
      file = do_QueryInterface(aSubject);
    } else if (aSomeData) {
      nsDependentString fileName(aSomeData);
      (void)NS_NewLocalFile(fileName, getter_AddRefs(file));
    }

    if (!file) return NS_OK;

    nsAutoCString uri;
    if (NS_FAILED(file->GetPersistentDescriptor(uri))) return NS_OK;

    uri.InsertLiteral("file:", 0);

    MutexAutoLock lock(mLock);

    RefPtr<nsJAR> zip;
    mZips.Get(uri, getter_AddRefs(zip));
    if (!zip) return NS_OK;

#ifdef ZIP_CACHE_HIT_RATE
    mZipCacheFlushes++;
#endif

    zip->SetZipReaderCache(nullptr);

    mZips.Remove(uri);
  }
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
