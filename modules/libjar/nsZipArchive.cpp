/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * This module implements a simple archive extractor for the PKZIP format.
 */

#define READTYPE int32_t
#include "zlib.h"
#include "nsISupportsUtils.h"
#include "mozilla/MmapFaultHandler.h"
#include "prio.h"
#include "mozilla/Attributes.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/Logging.h"
#include "mozilla/MemUtils.h"
#include "mozilla/UniquePtrExtensions.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/StaticPrefs_network.h"
#include "stdlib.h"
#include "nsDirectoryService.h"
#include "nsWildCard.h"
#include "nsXULAppAPI.h"
#include "nsZipArchive.h"
#include "nsString.h"
#include "prenv.h"
#if defined(XP_WIN)
#  include <windows.h>
#endif

// For placement new used for arena allocations of zip file list
#include <new>
#define ZIP_ARENABLOCKSIZE (1 * 1024)

#ifdef XP_UNIX
#  include <sys/mman.h>
#  include <sys/types.h>
#  include <sys/stat.h>
#  include <limits.h>
#  include <unistd.h>
#elif defined(XP_WIN)
#  include <io.h>
#endif

#ifdef __SYMBIAN32__
#  include <sys/syslimits.h>
#endif /*__SYMBIAN32__*/

#ifndef XP_UNIX /* we need some constants defined in limits.h and unistd.h */
#  ifndef S_IFMT
#    define S_IFMT 0170000
#  endif
#  ifndef S_IFLNK
#    define S_IFLNK 0120000
#  endif
#  ifndef PATH_MAX
#    define PATH_MAX 1024
#  endif
#endif /* XP_UNIX */

#ifdef XP_WIN
#  include "private/pprio.h"  // To get PR_ImportFile
#endif

using namespace mozilla;

static LazyLogModule gZipLog("nsZipArchive");

#ifdef LOG
#  undef LOG
#endif
#ifdef LOG_ENABLED
#  undef LOG_ENABLED
#endif

#define LOG(args) MOZ_LOG(gZipLog, mozilla::LogLevel::Debug, args)
#define LOG_ENABLED() MOZ_LOG_TEST(gZipLog, mozilla::LogLevel::Debug)

static const uint32_t kMaxNameLength = PATH_MAX; /* Maximum name length */
// For synthetic zip entries. Date/time corresponds to 1980-01-01 00:00.
static const uint16_t kSyntheticTime = 0;
static const uint16_t kSyntheticDate = (1 + (1 << 5) + (0 << 9));

static uint16_t xtoint(const uint8_t* ii);
static uint32_t xtolong(const uint8_t* ll);
static uint32_t HashName(const char* aName, uint16_t nameLen);

class ZipArchiveLogger {
 public:
  void Init(const char* env) {
    StaticMutexAutoLock lock(sLock);

    // AddRef
    MOZ_ASSERT(mRefCnt >= 0);
    ++mRefCnt;

    if (!mFd) {
      nsCOMPtr<nsIFile> logFile;
      nsresult rv =
          NS_NewLocalFile(NS_ConvertUTF8toUTF16(env), getter_AddRefs(logFile));
      if (NS_FAILED(rv)) return;

      // Create the log file and its parent directory (in case it doesn't exist)
      rv = logFile->Create(nsIFile::NORMAL_FILE_TYPE, 0644);
      if (NS_FAILED(rv)) return;

      PRFileDesc* file;
#ifdef XP_WIN
      // PR_APPEND is racy on Windows, so open a handle ourselves with flags
      // that will work, and use PR_ImportFile to make it a PRFileDesc. This can
      // go away when bug 840435 is fixed.
      nsAutoString path;
      logFile->GetPath(path);
      if (path.IsEmpty()) return;
      HANDLE handle =
          CreateFileW(path.get(), FILE_APPEND_DATA, FILE_SHARE_WRITE, nullptr,
                      OPEN_ALWAYS, 0, nullptr);
      if (handle == INVALID_HANDLE_VALUE) return;
      file = PR_ImportFile((PROsfd)handle);
      if (!file) return;
#else
      rv = logFile->OpenNSPRFileDesc(
          PR_WRONLY | PR_CREATE_FILE | PR_APPEND | PR_SYNC, 0644, &file);
      if (NS_FAILED(rv)) return;
#endif
      mFd = file;
    }
  }

  void Write(const nsACString& zip, const char* entry) {
    StaticMutexAutoLock lock(sLock);

    if (mFd) {
      nsCString buf(zip);
      buf.Append(' ');
      buf.Append(entry);
      buf.Append('\n');
      PR_Write(mFd, buf.get(), buf.Length());
    }
  }

  void Release() {
    StaticMutexAutoLock lock(sLock);

    MOZ_ASSERT(mRefCnt > 0);
    if ((0 == --mRefCnt) && mFd) {
      PR_Close(mFd);
      mFd = nullptr;
    }
  }

 private:
  static StaticMutex sLock;
  int mRefCnt MOZ_GUARDED_BY(sLock);
  PRFileDesc* mFd MOZ_GUARDED_BY(sLock);
};

StaticMutex ZipArchiveLogger::sLock;
static ZipArchiveLogger zipLog;

//***********************************************************
// For every inflation the following allocations are done:
// malloc(1 * 9520)
// malloc(32768 * 1)
//***********************************************************

nsresult gZlibInit(z_stream* zs) {
  memset(zs, 0, sizeof(z_stream));
  int zerr = inflateInit2(zs, -MAX_WBITS);
  if (zerr != Z_OK) return NS_ERROR_OUT_OF_MEMORY;

  return NS_OK;
}

nsZipHandle::nsZipHandle()
    : mFileData(nullptr),
      mLen(0),
      mMap(nullptr),
      mRefCnt(0),
      mFileStart(nullptr),
      mTotalLen(0) {}

NS_IMPL_ADDREF(nsZipHandle)
NS_IMPL_RELEASE(nsZipHandle)

nsresult nsZipHandle::Init(nsIFile* file, nsZipHandle** ret, PRFileDesc** aFd) {
  mozilla::AutoFDClose fd;
  int32_t flags = PR_RDONLY;
#if defined(XP_WIN)
  flags |= nsIFile::OS_READAHEAD;
#endif
  LOG(("ZipHandle::Init %s", file->HumanReadablePath().get()));
  nsresult rv = file->OpenNSPRFileDesc(flags, 0000, getter_Transfers(fd));
  if (NS_FAILED(rv)) return rv;

  int64_t size = PR_Available64(fd.get());
  if (size >= INT32_MAX) return NS_ERROR_FILE_TOO_BIG;

  PRFileMap* map = PR_CreateFileMap(fd.get(), size, PR_PROT_READONLY);
  if (!map) return NS_ERROR_FAILURE;

  uint8_t* buf = (uint8_t*)PR_MemMap(map, 0, (uint32_t)size);
  // Bug 525755: PR_MemMap fails when fd points at something other than a normal
  // file.
  if (!buf) {
    PR_CloseFileMap(map);
    return NS_ERROR_FAILURE;
  }

  RefPtr<nsZipHandle> handle = new nsZipHandle();
  if (!handle) {
    PR_MemUnmap(buf, (uint32_t)size);
    PR_CloseFileMap(map);
    return NS_ERROR_OUT_OF_MEMORY;
  }

#if defined(XP_UNIX)
  // Prefetch the archive into the page cache ahead of the ZIP directory scan
  // and entry reads at startup.
  madvise(buf, (size_t)size, MADV_WILLNEED);
#  if defined(XP_LINUX)
  // Exclude large archive mappings from core dumps; the file is on disk.
  madvise(buf, (size_t)size, MADV_DONTDUMP);
#  endif
#endif

#if defined(XP_WIN)
  if (aFd) {
    *aFd = fd.release();
  }
#else
  handle->mNSPRFileDesc = std::move(fd);
#endif
  handle->mFile.Init(file);
  handle->mTotalLen = (uint32_t)size;
  handle->mFileStart = buf;
  rv = handle->findDataStart();
  if (NS_FAILED(rv)) {
    PR_MemUnmap(buf, (uint32_t)size);
    handle->mFileStart = nullptr;
    PR_CloseFileMap(map);
    return rv;
  }
  handle->mMap = map;
  handle.forget(ret);
  return NS_OK;
}

nsresult nsZipHandle::Init(nsZipArchive* zip, const nsACString& entry,
                           nsZipHandle** ret) {
  RefPtr<nsZipHandle> handle = new nsZipHandle();
  if (!handle) return NS_ERROR_OUT_OF_MEMORY;

  LOG(("ZipHandle::Init entry %s", PromiseFlatCString(entry).get()));

  nsZipItem* item = zip->GetItem(entry);
  if (item && item->Compression() == DEFLATED &&
      StaticPrefs::network_jar_max_entry_size()) {
    if (item->RealSize() > StaticPrefs::network_jar_max_entry_size()) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
  }

  handle->mBuf = std::make_unique<nsZipItemPtr<uint8_t>>(zip, entry);
  if (!handle->mBuf) return NS_ERROR_OUT_OF_MEMORY;

  if (!handle->mBuf->Buffer()) return NS_ERROR_UNEXPECTED;

  handle->mMap = nullptr;
  handle->mFile.Init(zip, entry);
  handle->mTotalLen = handle->mBuf->Length();
  handle->mFileStart = handle->mBuf->Buffer();
  nsresult rv = handle->findDataStart();
  if (NS_FAILED(rv)) {
    return rv;
  }
  handle.forget(ret);
  return NS_OK;
}

nsresult nsZipHandle::Init(const uint8_t* aData, uint32_t aLen,
                           nsZipHandle** aRet) {
  RefPtr<nsZipHandle> handle = new nsZipHandle();

  handle->mFileStart = aData;
  handle->mTotalLen = aLen;
  nsresult rv = handle->findDataStart();
  if (NS_FAILED(rv)) {
    return rv;
  }
  handle.forget(aRet);
  return NS_OK;
}

// This function finds the start of the ZIP data. If the file is a regular ZIP,
// this is just the start of the file. If the file is a CRX file, the start of
// the data is after the CRX header.
//
// CRX header reference, version 2:
//    Header requires little-endian byte ordering with 4-byte alignment.
//    32 bits       : magicNumber   - Defined as a |char m[] = "Cr24"|.
//                                    Equivilant to |uint32_t m = 0x34327243|.
//    32 bits       : version       - Unsigned integer representing the CRX file
//                                    format version. Currently equal to 2.
//    32 bits       : pubKeyLength  - Unsigned integer representing the length
//                                    of the public key in bytes.
//    32 bits       : sigLength     - Unsigned integer representing the length
//                                    of the signature in bytes.
//    pubKeyLength  : publicKey     - Contents of the author's public key.
//    sigLength     : signature     - Signature of the ZIP content.
//                                    Signature is created using the RSA
//                                    algorithm with the SHA-1 hash function.
//
// CRX header reference, version 3:
//    Header requires little-endian byte ordering with 4-byte alignment.
//    32 bits       : magicNumber   - Defined as a |char m[] = "Cr24"|.
//                                    Equivilant to |uint32_t m = 0x34327243|.
//    32 bits       : version       - Unsigned integer representing the CRX file
//                                    format version. Currently equal to 3.
//    32 bits       : headerLength  - Unsigned integer representing the length
//                                    of the CRX header in bytes.
//    headerLength  : header        - CRXv3 header.
nsresult nsZipHandle::findDataStart() {
  // In the CRX header, integers are 32 bits. Our pointer to the file is of
  // type |uint8_t|, which is guaranteed to be 8 bits.
  const uint32_t CRXIntSize = 4;

  MMAP_FAULT_HANDLER_BEGIN_HANDLE(this)
  if (mTotalLen > CRXIntSize * 4 && xtolong(mFileStart) == kCRXMagic) {
    const uint8_t* headerData = mFileStart;
    headerData += CRXIntSize;  // Skip magic number
    uint32_t version = xtolong(headerData);
    headerData += CRXIntSize;  // Skip version
    mozilla::CheckedInt<uint32_t> checkedHeaderSize = CRXIntSize * 2;
    if (version == 3) {
      uint32_t subHeaderSize = xtolong(headerData);
      checkedHeaderSize += CRXIntSize;
      checkedHeaderSize += subHeaderSize;
    } else if (version < 3) {
      uint32_t pubKeyLength = xtolong(headerData);
      headerData += CRXIntSize;
      uint32_t sigLength = xtolong(headerData);
      checkedHeaderSize += CRXIntSize * 2;
      checkedHeaderSize += pubKeyLength;
      checkedHeaderSize += sigLength;
    } else {
      return NS_ERROR_FILE_CORRUPTED;
    }
    if (!checkedHeaderSize.isValid()) {
      return NS_ERROR_FILE_CORRUPTED;
    }
    uint32_t headerSize = checkedHeaderSize.value();
    if (mTotalLen > headerSize) {
      mLen = mTotalLen - headerSize;
      mFileData = mFileStart + headerSize;
      return NS_OK;
    }
    return NS_ERROR_FILE_CORRUPTED;
  }
  mLen = mTotalLen;
  mFileData = mFileStart;
  MMAP_FAULT_HANDLER_CATCH(NS_ERROR_FAILURE)
  return NS_OK;
}

int64_t nsZipHandle::SizeOfMapping() { return mTotalLen; }

nsresult nsZipHandle::GetNSPRFileDesc(PRFileDesc** aNSPRFileDesc) {
  if (!aNSPRFileDesc) {
    return NS_ERROR_ILLEGAL_VALUE;
  }

  *aNSPRFileDesc = mNSPRFileDesc.get();
  if (!mNSPRFileDesc) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  return NS_OK;
}

nsZipHandle::~nsZipHandle() {
  if (mMap) {
    PR_MemUnmap((void*)mFileStart, mTotalLen);
    PR_CloseFileMap(mMap);
  }
  mFileStart = nullptr;
  mFileData = nullptr;
  mMap = nullptr;
  mBuf = nullptr;
}

//***********************************************************
//      nsZipArchive  --  public methods
//***********************************************************

//---------------------------------------------
//  nsZipArchive::OpenArchive
//---------------------------------------------
/* static */
already_AddRefed<nsZipArchive> nsZipArchive::OpenArchive(
    nsZipHandle* aZipHandle, PRFileDesc* aFd, bool aAllowOptimizedLayout) {
  nsresult rv;
  RefPtr<nsZipArchive> self(
      new nsZipArchive(aZipHandle, aFd, aAllowOptimizedLayout, rv));
  LOG(("ZipHandle::OpenArchive[%p]", self.get()));
  if (NS_FAILED(rv)) {
    self = nullptr;
  }
  return self.forget();
}

/* static */
already_AddRefed<nsZipArchive> nsZipArchive::OpenArchive(
    nsIFile* aFile, bool aAllowOptimizedLayout) {
  RefPtr<nsZipHandle> handle;
#if defined(XP_WIN)
  mozilla::AutoFDClose fd;
  nsresult rv =
      nsZipHandle::Init(aFile, getter_AddRefs(handle), getter_Transfers(fd));
#else
  nsresult rv = nsZipHandle::Init(aFile, getter_AddRefs(handle));
#endif
  if (NS_FAILED(rv)) return nullptr;

#if defined(XP_WIN)
  return OpenArchive(handle, fd.get(), aAllowOptimizedLayout);
#else
  return OpenArchive(handle, nullptr, aAllowOptimizedLayout);
#endif
}

//---------------------------------------------
//  nsZipArchive::Test
//---------------------------------------------
nsresult nsZipArchive::Test(const nsACString& aEntryName) {
  nsZipItem* currItem;

  if (aEntryName.Length())  // only test specified item
  {
    currItem = GetItem(aEntryName);
    if (!currItem) return NS_ERROR_FILE_NOT_FOUND;
    //-- don't test (synthetic) directory items
    if (currItem->IsDirectory()) return NS_OK;
    return ExtractFile(currItem, nullptr, nullptr);
  }

  // test all items in archive
  for (auto* item : mFiles) {
    for (currItem = item; currItem; currItem = currItem->next) {
      //-- don't test (synthetic) directory items
      if (currItem->IsDirectory()) continue;
      nsresult rv = ExtractFile(currItem, nullptr, nullptr);
      if (rv != NS_OK) return rv;
    }
  }

  return NS_OK;
}

//---------------------------------------------
// nsZipArchive::GetItem
//---------------------------------------------
nsZipItem* nsZipArchive::GetItem(const nsACString& aEntryName) {
  MutexAutoLock lock(mLock);

  LOG(("ZipHandle::GetItem[%p] %s", this,
       PromiseFlatCString(aEntryName).get()));
  if (aEntryName.Length()) {
    uint32_t len = aEntryName.Length();
    //-- If the request is for a directory, make sure that synthetic entries
    //-- are created for the directories without their own entry.
    if (!mBuiltSynthetics) {
      if ((len > 0) && (aEntryName[len - 1] == '/')) {
        if (BuildSynthetics() != NS_OK) return nullptr;
      }
    }
    MMAP_FAULT_HANDLER_BEGIN_HANDLE(mFd)
    nsZipItem* item = mFiles[HashName(aEntryName.BeginReading(), len)];
    while (item) {
      if ((len == item->nameLength) &&
          (!memcmp(aEntryName.BeginReading(), item->Name(), len))) {
        // Successful GetItem() is a good indicator that the file is about to be
        // read
        if (mUseZipLog && mURI.Length()) {
          zipLog.Write(mURI, PromiseFlatCString(aEntryName).get());
        }
        return item;  //-- found it
      }
      item = item->next;
    }
    MMAP_FAULT_HANDLER_CATCH(nullptr)
  }
  return nullptr;
}

//---------------------------------------------
// nsZipArchive::ExtractFile
// This extracts the item to the filehandle provided.
// If 'aFd' is null, it only tests the extraction.
// On extraction error(s) it removes the file.
//---------------------------------------------
nsresult nsZipArchive::ExtractFile(nsZipItem* item, nsIFile* outFile,
                                   PRFileDesc* aFd) {
  MutexAutoLock lock(mLock);
  LOG(("ZipHandle::ExtractFile[%p]", this));
  if (!item) return NS_ERROR_ILLEGAL_VALUE;
  if (!mFd) return NS_ERROR_FAILURE;

  // Directory extraction is handled in nsJAR::Extract,
  // so the item to be extracted should never be a directory
  MOZ_ASSERT(!item->IsDirectory());

  Bytef outbuf[ZIP_BUFLEN];

  nsZipCursor cursor(item, this, outbuf, ZIP_BUFLEN, true);

  nsresult rv = NS_OK;

  while (true) {
    uint32_t count = 0;
    uint8_t* buf = cursor.Read(&count);
    if (!buf) {
      rv = NS_ERROR_FILE_CORRUPTED;
      break;
    }
    if (count == 0) {
      break;
    }

    if (aFd && PR_Write(aFd, buf, count) < (READTYPE)count) {
      rv = NS_ERROR_FILE_NO_DEVICE_SPACE;
      break;
    }
  }

  //-- delete the file on errors
  if (aFd) {
    PR_Close(aFd);
    if (NS_FAILED(rv) && outFile) {
      outFile->Remove(false);
    }
  }

  return rv;
}

//---------------------------------------------
// nsZipArchive::FindInit
//---------------------------------------------
nsresult nsZipArchive::FindInit(const char* aPattern, nsZipFind** aFind) {
  if (!aFind) return NS_ERROR_ILLEGAL_VALUE;

  MutexAutoLock lock(mLock);

  LOG(("ZipHandle::FindInit[%p]", this));
  // null out param in case an error happens
  *aFind = nullptr;

  bool regExp = false;
  char* pattern = nullptr;

  // Create synthetic directory entries on demand
  nsresult rv = BuildSynthetics();
  if (rv != NS_OK) return rv;

  // validate the pattern
  if (aPattern) {
    switch (NS_WildCardValid((char*)aPattern)) {
      case INVALID_SXP:
        return NS_ERROR_ILLEGAL_VALUE;

      case NON_SXP:
        regExp = false;
        break;

      case VALID_SXP:
        regExp = true;
        break;

      default:
        // undocumented return value from RegExpValid!
        MOZ_ASSERT(false);
        return NS_ERROR_ILLEGAL_VALUE;
    }

    pattern = strdup(aPattern);
    if (!pattern) return NS_ERROR_OUT_OF_MEMORY;
  }

  *aFind = new nsZipFind(this, pattern, regExp);
  if (!*aFind) {
    free(pattern);
    return NS_ERROR_OUT_OF_MEMORY;
  }

  return NS_OK;
}

//---------------------------------------------
// nsZipFind::FindNext
//---------------------------------------------
nsresult nsZipFind::FindNext(const char** aResult, uint16_t* aNameLen) {
  if (!aResult || !aNameLen) return NS_ERROR_ILLEGAL_VALUE;
  NS_ENSURE_TRUE(mArchive, NS_ERROR_FILE_NOT_FOUND);

  MutexAutoLock lock(mArchive->mLock);
  *aResult = nullptr;
  *aNameLen = 0;
  MMAP_FAULT_HANDLER_BEGIN_HANDLE(mArchive->GetFD())
  // we start from last match, look for next
  while (mSlot < ZIP_TABSIZE) {
    // move to next in current chain, or move to new slot
    mItem = mItem ? mItem->next : mArchive->mFiles[mSlot];

    bool found = false;
    if (!mItem)
      ++mSlot;  // no more in this chain, move to next slot
    else if (!mPattern)
      found = true;  // always match
    else if (mRegExp) {
      char buf[kMaxNameLength + 1];
      memcpy(buf, mItem->Name(), mItem->nameLength);
      buf[mItem->nameLength] = '\0';
      found = (NS_WildCardMatch(buf, mPattern, false) == MATCH);
    } else
      found = ((mItem->nameLength == strlen(mPattern)) &&
               (memcmp(mItem->Name(), mPattern, mItem->nameLength) == 0));
    if (found) {
      // Need also to return the name length, as it is NOT zero-terminatdd...
      *aResult = mItem->Name();
      *aNameLen = mItem->nameLength;
      LOG(("ZipHandle::FindNext[%p] %s", this, *aResult));
      return NS_OK;
    }
  }
  MMAP_FAULT_HANDLER_CATCH(NS_ERROR_FAILURE)
  LOG(("ZipHandle::FindNext[%p] not found %s", this, mPattern));
  // Release the archive.
  mArchive = nullptr;
  mItem = nullptr;
  return NS_ERROR_FILE_NOT_FOUND;
}

//***********************************************************
//      nsZipArchive  --  private implementation
//***********************************************************

//---------------------------------------------
//  nsZipArchive::CreateZipItem
//---------------------------------------------
nsZipItem* nsZipArchive::CreateZipItem() {
  // Arena allocate the nsZipItem
  return (nsZipItem*)mArena.Allocate(sizeof(nsZipItem), mozilla::fallible);
}

//---------------------------------------------
//  nsZipArchive::BuildFileList
//---------------------------------------------
nsresult nsZipArchive::BuildFileList(
    PRFileDesc* aFd, bool aAllowOptimizedLayout) MOZ_NO_THREAD_SAFETY_ANALYSIS {
  // We're only called from the constructor, but need to call
  // CreateZipItem(), which touches locked data, and modify mFiles.  Turn
  // off thread-safety, which normally doesn't apply for constructors
  // anyways

  // Get archive size using end pos
  const uint8_t* buf;
  const uint8_t* startp = mFd->mFileData;
  const uint8_t* endp = startp + mFd->mLen;
  MMAP_FAULT_HANDLER_BEGIN_HANDLE(mFd)
  uint32_t centralOffset = 4;
  LOG(("ZipHandle::BuildFileList[%p]", this));
  // Only perform readahead in the parent process. Children processes
  // don't need readahead when the file has already been readahead by
  // the parent process, and readahead only really happens for omni.ja,
  // which is used in the parent process.
  //
  // The optimized layout (Central Directory right at offset 4, with no
  // End Of Central Directory record) is specific to our own build-time
  // packaged omni.ja (bug 559961), so only trust it when the caller has
  // independently established that this is in fact our own omni.ja.
  if (aAllowOptimizedLayout && XRE_IsParentProcess() &&
      mFd->mLen > ZIPCENTRAL_SIZE &&
      xtolong(startp + centralOffset) == CENTRALSIG) {
    // Success means optimized jar layout from bug 559961 is in effect
    uint32_t readaheadLength = xtolong(startp);
    mozilla::PrefetchMemory(const_cast<uint8_t*>(startp), readaheadLength);
  } else {
    for (buf = endp - ZIPEND_SIZE; buf > startp; buf--) {
      if (xtolong(buf) == ENDSIG) {
        centralOffset = xtolong(((ZipEnd*)buf)->offset_central_dir);
        break;
      }
    }
  }

  if (!centralOffset) {
    return NS_ERROR_FILE_CORRUPTED;
  }

  buf = startp + centralOffset;

  // avoid overflow of startp + centralOffset.
  if (buf < startp) {
    return NS_ERROR_FILE_CORRUPTED;
  }

  //-- Read the central directory headers
  uint32_t sig = 0;
  while ((buf + int32_t(sizeof(uint32_t)) > buf) &&
         (buf + int32_t(sizeof(uint32_t)) <= endp) &&
         ((sig = xtolong(buf)) == CENTRALSIG)) {
    // Make sure there is enough data available.
    if ((buf > endp) || (endp - buf < ZIPCENTRAL_SIZE)) {
      return NS_ERROR_FILE_CORRUPTED;
    }

    // Read the fixed-size data.
    ZipCentral* central = (ZipCentral*)buf;

    uint16_t namelen = xtoint(central->filename_len);
    uint16_t extralen = xtoint(central->extrafield_len);
    uint16_t commentlen = xtoint(central->commentfield_len);
    uint32_t diff = ZIPCENTRAL_SIZE + namelen + extralen + commentlen;

    // Sanity check variable sizes and refuse to deal with
    // anything too big: it's likely a corrupt archive.
    if (namelen < 1 || namelen > kMaxNameLength) {
      return NS_ERROR_FILE_CORRUPTED;
    }
    if (buf >= buf + diff ||  // No overflow
        buf >= endp - diff) {
      return NS_ERROR_FILE_CORRUPTED;
    }

    // Point to the next item at the top of loop
    buf += diff;

    nsZipItem* item = CreateZipItem();
    if (!item) return NS_ERROR_OUT_OF_MEMORY;

    item->central = central;
    item->nameLength = namelen;
    item->isSynthetic = false;

    // Add item to file table
#ifdef DEBUG
    nsDependentCSubstring name(item->Name(), namelen);
    LOG(("   %s", PromiseFlatCString(name).get()));
#endif
    uint32_t hash = HashName(item->Name(), namelen);
    item->next = mFiles[hash];
    mFiles[hash] = item;

    sig = 0;
  } /* while reading central directory records */

  if (sig != ENDSIG && sig != ENDSIG64) {
    return NS_ERROR_FILE_CORRUPTED;
  }

  MMAP_FAULT_HANDLER_CATCH(NS_ERROR_FAILURE)
  return NS_OK;
}

//---------------------------------------------
//  nsZipArchive::BuildSynthetics
//---------------------------------------------
nsresult nsZipArchive::BuildSynthetics() {
  mLock.AssertCurrentThreadOwns();

  if (mBuiltSynthetics) return NS_OK;
  mBuiltSynthetics = true;

  MMAP_FAULT_HANDLER_BEGIN_HANDLE(mFd)
  // Create synthetic entries for any missing directories.
  // Do this when all ziptable has scanned to prevent double entries.
  for (auto* item : mFiles) {
    for (; item != nullptr; item = item->next) {
      if (item->isSynthetic) continue;

      //-- add entries for directories in the current item's path
      //-- go from end to beginning, because then we can stop trying
      //-- to create diritems if we find that the diritem we want to
      //-- create already exists
      //-- start just before the last char so as to not add the item
      //-- twice if it's a directory
      uint16_t namelen = item->nameLength;
      MOZ_ASSERT(namelen > 0,
                 "Attempt to build synthetic for zero-length entry name!");
      const char* name = item->Name();
      for (uint16_t dirlen = namelen - 1; dirlen > 0; dirlen--) {
        if (name[dirlen - 1] != '/') continue;

        // The character before this is '/', so if this is also '/' then we
        // have an empty path component. Skip it.
        if (name[dirlen] == '/') continue;

        // Is the directory already in the file table?
        uint32_t hash = HashName(item->Name(), dirlen);
        bool found = false;
        for (nsZipItem* zi = mFiles[hash]; zi != nullptr; zi = zi->next) {
          if ((dirlen == zi->nameLength) &&
              (0 == memcmp(item->Name(), zi->Name(), dirlen))) {
            // we've already added this dir and all its parents
            found = true;
            break;
          }
        }
        // if the directory was found, break out of the directory
        // creation loop now that we know all implicit directories
        // are there -- otherwise, start creating the zip item
        if (found) break;

        nsZipItem* diritem = CreateZipItem();
        if (!diritem) return NS_ERROR_OUT_OF_MEMORY;

        // Point to the central record of the original item for the name part.
        diritem->central = item->central;
        diritem->nameLength = dirlen;
        diritem->isSynthetic = true;

        // add diritem to the file table
        diritem->next = mFiles[hash];
        mFiles[hash] = diritem;
      } /* end processing of dirs in item's name */
    }
  }
  MMAP_FAULT_HANDLER_CATCH(NS_ERROR_FAILURE)
  return NS_OK;
}

//---------------------------------------------
// nsZipArchive::GetFD
//---------------------------------------------
nsZipHandle* nsZipArchive::GetFD() const { return mFd.get(); }

//---------------------------------------------
// nsZipArchive::GetDataOffset
// Returns 0 on an error; 0 is not a valid result for any success case
//---------------------------------------------
uint32_t nsZipArchive::GetDataOffset(nsZipItem* aItem) {
  MOZ_ASSERT(aItem);
  MOZ_DIAGNOSTIC_ASSERT(mFd);

  uint32_t offset;
  MMAP_FAULT_HANDLER_BEGIN_HANDLE(mFd)
  //-- read local header to get variable length values and calculate
  //-- the real data offset
  uint32_t len = mFd->mLen;
  MOZ_DIAGNOSTIC_ASSERT(len <= UINT32_MAX, "mLen > 2GB");
  const uint8_t* data = mFd->mFileData;
  offset = aItem->LocalOffset();
  if (len < ZIPLOCAL_SIZE || offset > len - ZIPLOCAL_SIZE) {
    return 0;
  }
  // Check there's enough space for the signature
  if (offset > mFd->mLen) {
    NS_WARNING("Corrupt local offset in JAR file");
    return 0;
  }

  // -- check signature before using the structure, in case the zip file is
  // corrupt
  ZipLocal* Local = (ZipLocal*)(data + offset);
  if ((xtolong(Local->signature) != LOCALSIG)) return 0;

  //-- NOTE: extralen is different in central header and local header
  //--       for archives created using the Unix "zip" utility. To set
  //--       the offset accurately we need the _local_ extralen.
  offset += ZIPLOCAL_SIZE + xtoint(Local->filename_len) +
            xtoint(Local->extrafield_len);
  // Check data points inside the file.
  if (offset > mFd->mLen) {
    NS_WARNING("Corrupt data offset in JAR file");
    return 0;
  }

  MMAP_FAULT_HANDLER_CATCH(0)
  // can't be 0
  return offset;
}

//---------------------------------------------
// nsZipArchive::GetData
//---------------------------------------------
const uint8_t* nsZipArchive::GetData(nsZipItem* aItem) {
  MOZ_DIAGNOSTIC_ASSERT(aItem);
  if (!aItem) {
    return nullptr;
  }
  uint32_t offset = GetDataOffset(aItem);

  MMAP_FAULT_HANDLER_BEGIN_HANDLE(mFd)
  // -- check if there is enough source data in the file
  if (!offset || mFd->mLen < aItem->Size() ||
      offset > mFd->mLen - aItem->Size() ||
      (aItem->Compression() == STORED && aItem->Size() != aItem->RealSize())) {
    return nullptr;
  }
  MMAP_FAULT_HANDLER_CATCH(nullptr)

  return mFd->mFileData + offset;
}

//---------------------------------------------
// nsZipArchive::SizeOfMapping
//---------------------------------------------
int64_t nsZipArchive::SizeOfMapping() { return mFd ? mFd->SizeOfMapping() : 0; }

//------------------------------------------
// nsZipArchive constructor and destructor
//------------------------------------------

nsZipArchive::nsZipArchive(nsZipHandle* aZipHandle, PRFileDesc* aFd,
                           bool aAllowOptimizedLayout, nsresult& aRv)
    : mRefCnt(0), mFd(aZipHandle), mUseZipLog(false), mBuiltSynthetics(false) {
  // initialize the table to nullptr
  memset(mFiles, 0, sizeof(mFiles));
  MOZ_DIAGNOSTIC_ASSERT(aZipHandle);

  //-- get table of contents for archive
  aRv = BuildFileList(aFd, aAllowOptimizedLayout);
  if (NS_FAILED(aRv)) {
    return;  // whomever created us must destroy us in this case
  }
  if (aZipHandle->mFile && XRE_IsParentProcess()) {
    static char* env = PR_GetEnv("MOZ_JAR_LOG_FILE");
    if (env) {
      mUseZipLog = true;

      zipLog.Init(env);
      // We only log accesses in jar/zip archives within the NS_GRE_DIR
      // and/or the APK on Android. For the former, we log the archive path
      // relative to NS_GRE_DIR, and for the latter, the nested-archive
      // path within the APK. This makes the path match the path of the
      // archives relative to the packaged dist/$APP_NAME directory in a
      // build.
      if (aZipHandle->mFile.IsZip()) {
        // Nested archive, likely omni.ja in APK.
        aZipHandle->mFile.GetPath(mURI);
      } else if (nsDirectoryService::gService) {
        // We can reach here through the initialization of Omnijar from
        // XRE_InitCommandLine, which happens before the directory service
        // is initialized. When that happens, it means the opened archive is
        // the APK, and we don't care to log that one, so we just skip
        // when the directory service is not initialized.
        nsCOMPtr<nsIFile> dir = aZipHandle->mFile.GetBaseFile();
        nsCOMPtr<nsIFile> gre_dir;
        nsAutoCString path;
        if (NS_SUCCEEDED(nsDirectoryService::gService->Get(
                NS_GRE_DIR, NS_GET_IID(nsIFile), getter_AddRefs(gre_dir)))) {
          nsAutoCString leaf;
          nsCOMPtr<nsIFile> parent;
          while (NS_SUCCEEDED(dir->GetNativeLeafName(leaf)) &&
                 NS_SUCCEEDED(dir->GetParent(getter_AddRefs(parent)))) {
            if (!parent) {
              break;
            }
            dir = parent;
            if (path.Length()) {
              path.Insert('/', 0);
            }
            path.Insert(leaf, 0);
            bool equals;
            if (NS_SUCCEEDED(dir->Equals(gre_dir, &equals)) && equals) {
              mURI.Assign(path);
              break;
            }
          }
        }
      }
    }
  }
}

NS_IMPL_ADDREF(nsZipArchive)
NS_IMPL_RELEASE(nsZipArchive)

nsZipArchive::~nsZipArchive() {
  LOG(("Closing nsZipArchive[%p]", this));
  if (mUseZipLog) {
    zipLog.Release();
  }
}

//------------------------------------------
// nsZipFind constructor and destructor
//------------------------------------------

nsZipFind::nsZipFind(nsZipArchive* aZip, char* aPattern, bool aRegExp)
    : mArchive(aZip),
      mPattern(aPattern),
      mItem(nullptr),
      mSlot(0),
      mRegExp(aRegExp) {
  MOZ_COUNT_CTOR(nsZipFind);
  MOZ_ASSERT(mArchive);
}

nsZipFind::~nsZipFind() {
  free(mPattern);

  MOZ_COUNT_DTOR(nsZipFind);
}

//------------------------------------------
// helper functions
//------------------------------------------

/*
 * HashName
 *
 * returns a hash key for the entry name
 */
MOZ_NO_SANITIZE_UNSIGNED_OVERFLOW
static uint32_t HashName(const char* aName, uint16_t len) {
  MOZ_ASSERT(aName != nullptr);

  const uint8_t* p = (const uint8_t*)aName;
  const uint8_t* endp = p + len;
  uint32_t val = 0;
  while (p != endp) {
    val = val * 37 + *p++;
  }

  return (val % ZIP_TABSIZE);
}

/*
 *  x t o i n t
 *
 *  Converts a two byte ugly endianed integer
 *  to our platform's integer.
 */
static uint16_t xtoint(const uint8_t* ii) {
  return (uint16_t)((ii[0]) | (ii[1] << 8));
}

/*
 *  x t o l o n g
 *
 *  Converts a four byte ugly endianed integer
 *  to our platform's integer.
 */
static uint32_t xtolong(const uint8_t* ll) {
  return (uint32_t)((ll[0] << 0) | (ll[1] << 8) | (ll[2] << 16) |
                    (ll[3] << 24));
}

/*
 * GetModTime
 *
 * returns last modification time in microseconds
 */
static PRTime GetModTime(uint16_t aDate, uint16_t aTime) {
  // Note that on DST shift we can't handle correctly the hour that is valid
  // in both DST zones
  PRExplodedTime time;

  time.tm_usec = 0;

  time.tm_hour = (aTime >> 11) & 0x1F;
  time.tm_min = (aTime >> 5) & 0x3F;
  time.tm_sec = (aTime & 0x1F) * 2;

  time.tm_year = (aDate >> 9) + 1980;
  time.tm_month = ((aDate >> 5) & 0x0F) - 1;
  time.tm_mday = aDate & 0x1F;

  time.tm_params.tp_gmt_offset = 0;
  time.tm_params.tp_dst_offset = 0;

  PR_NormalizeTime(&time, PR_GMTParameters);
  time.tm_params.tp_gmt_offset = PR_LocalTimeParameters(&time).tp_gmt_offset;
  PR_NormalizeTime(&time, PR_GMTParameters);
  time.tm_params.tp_dst_offset = PR_LocalTimeParameters(&time).tp_dst_offset;

  return PR_ImplodeTime(&time);
}

nsZipItem::nsZipItem()
    : next(nullptr), central(nullptr), nameLength(0), isSynthetic(false) {}

uint32_t nsZipItem::LocalOffset() { return xtolong(central->localhdr_offset); }

uint32_t nsZipItem::Size() { return isSynthetic ? 0 : xtolong(central->size); }

uint32_t nsZipItem::RealSize() {
  return isSynthetic ? 0 : xtolong(central->orglen);
}

uint32_t nsZipItem::CRC32() {
  return isSynthetic ? 0 : xtolong(central->crc32);
}

uint16_t nsZipItem::Date() {
  return isSynthetic ? kSyntheticDate : xtoint(central->date);
}

uint16_t nsZipItem::Time() {
  return isSynthetic ? kSyntheticTime : xtoint(central->time);
}

uint16_t nsZipItem::Compression() {
  return isSynthetic ? STORED : xtoint(central->method);
}

bool nsZipItem::IsDirectory() {
  return isSynthetic || ((nameLength > 0) && ('/' == Name()[nameLength - 1]));
}

uint16_t nsZipItem::Mode() {
  if (isSynthetic) return 0755;
  return ((uint16_t)(central->external_attributes[2]) | 0x100);
}

const uint8_t* nsZipItem::GetExtraField(uint16_t aTag, uint16_t* aBlockSize) {
  if (isSynthetic) return nullptr;

  const unsigned char* buf =
      ((const unsigned char*)central) + ZIPCENTRAL_SIZE + nameLength;
  uint32_t buflen;

  MMAP_FAULT_HANDLER_BEGIN_BUFFER(central, ZIPCENTRAL_SIZE + nameLength)
  buflen = (uint32_t)xtoint(central->extrafield_len);
  MMAP_FAULT_HANDLER_CATCH(nullptr)

  uint32_t pos = 0;
  uint16_t tag, blocksize;

  MMAP_FAULT_HANDLER_BEGIN_BUFFER(buf, buflen)
  while (buf && (pos + 4) <= buflen) {
    tag = xtoint(buf + pos);
    blocksize = xtoint(buf + pos + 2);

    if (aTag == tag && (pos + 4 + blocksize) <= buflen) {
      *aBlockSize = blocksize;
      return buf + pos;
    }

    pos += blocksize + 4;
  }
  MMAP_FAULT_HANDLER_CATCH(nullptr)

  return nullptr;
}

PRTime nsZipItem::LastModTime() {
  if (isSynthetic) return GetModTime(kSyntheticDate, kSyntheticTime);

  // Try to read timestamp from extra field
  uint16_t blocksize;
  const uint8_t* tsField = GetExtraField(EXTENDED_TIMESTAMP_FIELD, &blocksize);
  if (tsField && blocksize >= 5 && tsField[4] & EXTENDED_TIMESTAMP_MODTIME) {
    return (PRTime)(xtolong(tsField + 5)) * PR_USEC_PER_SEC;
  }

  return GetModTime(Date(), Time());
}

nsZipCursor::nsZipCursor(nsZipItem* item, nsZipArchive* aZip, uint8_t* aBuf,
                         uint32_t aBufSize, bool doCRC)
    : mItem(item),
      mBuf(aBuf),
      mBufSize(aBufSize),
      mZs(),
      mCRC(0),
      mDoCRC(doCRC) {
  if (mItem->Compression() == DEFLATED) {
#ifdef DEBUG
    nsresult status =
#endif
        gZlibInit(&mZs);
    NS_ASSERTION(status == NS_OK, "Zlib failed to initialize");
    NS_ASSERTION(aBuf, "Must pass in a buffer for DEFLATED nsZipItem");
  }

  mZs.avail_in = item->Size();
  mZs.next_in = (Bytef*)aZip->GetData(item);

  if (doCRC) mCRC = crc32(0L, Z_NULL, 0);
}

nsZipCursor::~nsZipCursor() {
  if (mItem->Compression() == DEFLATED) {
    inflateEnd(&mZs);
  }
}

uint8_t* nsZipCursor::ReadOrCopy(uint32_t* aBytesRead, bool aCopy) {
  int zerr;
  uint8_t* buf = nullptr;
  bool verifyCRC = true;

  if (!mZs.next_in) return nullptr;
  MMAP_FAULT_HANDLER_BEGIN_BUFFER(mZs.next_in, mZs.avail_in)
  switch (mItem->Compression()) {
    case STORED:
      if (!aCopy) {
        *aBytesRead = mZs.avail_in;
        buf = mZs.next_in;
        mZs.next_in += mZs.avail_in;
        mZs.avail_in = 0;
      } else {
        *aBytesRead = mZs.avail_in > mBufSize ? mBufSize : mZs.avail_in;
        memcpy(mBuf, mZs.next_in, *aBytesRead);
        buf = mBuf;
        mZs.avail_in -= *aBytesRead;
        mZs.next_in += *aBytesRead;
      }
      break;
    case DEFLATED:
      buf = mBuf;
      mZs.next_out = buf;
      mZs.avail_out = mBufSize;

      zerr = inflate(&mZs, Z_PARTIAL_FLUSH);
      if (zerr != Z_OK && zerr != Z_STREAM_END) return nullptr;

      *aBytesRead = mZs.next_out - buf;
      verifyCRC = (zerr == Z_STREAM_END);
      break;
    default:
      return nullptr;
  }

  if (mDoCRC) {
    mCRC = crc32(mCRC, (const unsigned char*)buf, *aBytesRead);
    if (verifyCRC && mCRC != mItem->CRC32()) return nullptr;
  }
  MMAP_FAULT_HANDLER_CATCH(nullptr)
  return buf;
}

nsZipItemPtr_base::nsZipItemPtr_base(nsZipArchive* aZip,
                                     const nsACString& aEntryName, bool doCRC)
    : mReturnBuf(nullptr), mReadlen(0) {
  // make sure the ziparchive hangs around
  mZipHandle = aZip->GetFD();

  nsZipItem* item = aZip->GetItem(aEntryName);
  if (!item) return;

  uint32_t size = 0;
  bool compressed = (item->Compression() == DEFLATED);
  if (compressed) {
    size = item->RealSize();
    mAutoBuf = MakeUniqueFallible<uint8_t[]>(size);
    if (!mAutoBuf) {
      return;
    }
  }

  nsZipCursor cursor(item, aZip, mAutoBuf.get(), size, doCRC);
  mReturnBuf = cursor.Read(&mReadlen);
  if (!mReturnBuf) {
    return;
  }

  if (mReadlen != item->RealSize()) {
    NS_ASSERTION(mReadlen == item->RealSize(), "nsZipCursor underflow");
    mReturnBuf = nullptr;
    return;
  }
}
