/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsZipArchive_h_
#define nsZipArchive_h_

#define ZIP_TABSIZE 256
#define ZIP_BUFLEN \
  (4 * 1024) /* Used as output buffer when deflating items to a file */

#include "zlib.h"
#include "zipstruct.h"
#include "nsIFile.h"
#include "nsISupportsImpl.h"  // For mozilla::ThreadSafeAutoRefCnt
#include "mozilla/ArenaAllocator.h"
#include "mozilla/FileUtils.h"
#include "mozilla/FileLocation.h"
#include "mozilla/Mutex.h"
#include "mozilla/UniquePtr.h"

class nsZipFind;
struct PRFileDesc;

/**
 * This file defines some of the basic structures used by libjar to
 * read Zip files. It makes use of zlib in order to do the decompression.
 *
 * A few notes on the classes/structs:
 * nsZipArchive   represents a single Zip file, and maintains an index
 *                of all the items in the file.
 * nsZipItem      represents a single item (file) in the Zip archive.
 * nsZipFind      represents the metadata involved in doing a search,
 *                and current state of the iteration of found objects.
 * 'MT''safe' reading from the zipfile is performed through JARInputStream,
 * which maintains its own file descriptor, allowing for multiple reads
 * concurrently from the same zip file.
 *
 * nsZipArchives are accessed from multiple threads.
 */

/**
 * nsZipItem -- a helper struct for nsZipArchive
 *
 * each nsZipItem represents one file in the archive and all the
 * information needed to manipulate it.
 */
class nsZipItem final {
 public:
  nsZipItem();

  const char* Name() { return ((const char*)central) + ZIPCENTRAL_SIZE; }

  uint32_t LocalOffset();
  uint32_t Size();
  uint32_t RealSize();
  uint32_t CRC32();
  uint16_t Date();
  uint16_t Time();
  uint16_t Compression();
  bool IsDirectory();
  uint16_t Mode();
  const uint8_t* GetExtraField(uint16_t aTag, uint16_t* aBlockSize);
  PRTime LastModTime();

  nsZipItem* next;
  const ZipCentral* central;
  uint16_t nameLength;
  bool isSynthetic;
};

class nsZipHandle;

/**
 * nsZipArchive -- a class for reading the PKZIP file format.
 *
 */
class nsZipArchive final {
  friend class nsZipFind;

  /** destructing the object closes the archive */
  ~nsZipArchive();

 public:
  nsZipArchive& operator=(const nsZipArchive& rhs) = delete;
  nsZipArchive(const nsZipArchive& rhs) = delete;

  static const char* sFileCorruptedReason;

  /**
   * OpenArchive
   *
   * @param   aZipHandle  The nsZipHandle used to access the zip
   * @param   aFd         Optional PRFileDesc for Windows readahead optimization
   * @param   aAllowOptimizedLayout
   *                      Whether to trust the Central Directory found at
   *                      offset 4 (the omni.jar layout from bug 559961)
   *                      instead of the one found via the normal End Of
   *                      Central Directory scan. Only pass true for our
   *                      own build-generated omni.jar.
   * @return  status code
   */
  static already_AddRefed<nsZipArchive> OpenArchive(
      nsZipHandle* aZipHandle, PRFileDesc* aFd = nullptr,
      bool aAllowOptimizedLayout = false);

  /**
   * OpenArchive
   *
   * Convenience function that generates nsZipHandle
   *
   * @param   aFile         The file used to access the zip
   * @param   aAllowOptimizedLayout See the other OpenArchive overload.
   * @return  status code
   */
  static already_AddRefed<nsZipArchive> OpenArchive(
      nsIFile* aFile, bool aAllowOptimizedLayout = false);

  /**
   * Test the integrity of items in this archive by running
   * a CRC check after extracting each item into a memory
   * buffer.  If an entry name is supplied only the
   * specified item is tested.  Else, if null is supplied
   * then all the items in the archive are tested.
   *
   * @return  status code
   */
  nsresult Test(const nsACString& aEntryName);

  /**
   * GetItem
   * @param   aEntryName Name of file in the archive
   * @return  pointer to nsZipItem
   */
  nsZipItem* GetItem(const nsACString& aEntryName);

  /**
   * ExtractFile
   *
   * @param   zipEntry   Name of file in archive to extract
   * @param   outFD      Filedescriptor to write contents to
   * @param   outname    Name of file to write to
   * @return  status code
   */
  nsresult ExtractFile(nsZipItem* zipEntry, nsIFile* outFile,
                       PRFileDesc* outFD);

  /**
   * FindInit
   *
   * Initializes a search for files in the archive. FindNext() returns
   * the actual matches. The nsZipFind must be deleted when you're done
   *
   * @param   aPattern    a string or RegExp pattern to search for
   *                      (may be nullptr to find all files in archive)
   * @param   aFind       a pointer to a pointer to a structure used
   *                      in FindNext.  In the case of an error this
   *                      will be set to nullptr.
   * @return  status code
   */
  nsresult FindInit(const char* aPattern, nsZipFind** aFind);

  /*
   * Gets an undependent handle to the mapped file.
   */
  nsZipHandle* GetFD() const;

  /**
   * Gets the data offset.
   * @param   aItem       Pointer to nsZipItem
   * returns 0 on failure.
   */
  uint32_t GetDataOffset(nsZipItem* aItem);

  /**
   * Get pointer to the data of the item.
   * @param   aItem       Pointer to nsZipItem
   * reutrns null when zip file is corrupt.
   */
  const uint8_t* GetData(nsZipItem* aItem);

  /**
   * Gets the amount of memory taken up by the archive's mapping.
   * @return the size
   */
  int64_t SizeOfMapping();

  /*
   * Refcounting
   */
  NS_METHOD_(MozExternalRefCountType) AddRef(void);
  NS_METHOD_(MozExternalRefCountType) Release(void);

 private:
  nsZipArchive(nsZipHandle* aZipHandle, PRFileDesc* aFd,
               bool aAllowOptimizedLayout, nsresult& aRv);

  //--- private members ---
  mozilla::ThreadSafeAutoRefCnt mRefCnt; /* ref count */
  NS_DECL_OWNINGTHREAD

  // These fields are all effectively const after the constructor
  // file handle
  const RefPtr<nsZipHandle> mFd;
  // file URI, for logging
  nsCString mURI;
  // Is true if we use zipLog to log accesses in jar/zip archives. This helper
  // variable avoids grabbing zipLog's lock when not necessary.
  // Effectively const after constructor
  bool mUseZipLog;

  mozilla::Mutex mLock{"nsZipArchive"};
  // all of the following members are guarded by mLock:
  nsZipItem* mFiles[ZIP_TABSIZE] MOZ_GUARDED_BY(mLock);
  mozilla::ArenaAllocator<1024, sizeof(void*)> mArena MOZ_GUARDED_BY(mLock);
  // Whether we synthesized the directory entries
  bool mBuiltSynthetics MOZ_GUARDED_BY(mLock);

 private:
  //--- private methods ---
  nsZipItem* CreateZipItem() MOZ_REQUIRES(mLock);
  nsresult BuildFileList(PRFileDesc* aFd, bool aAllowOptimizedLayout);
  nsresult BuildSynthetics();
};

/**
 * nsZipFind
 *
 * a helper class for nsZipArchive, representing a search
 */
class nsZipFind final {
 public:
  nsZipFind(nsZipArchive* aZip, char* aPattern, bool regExp);
  ~nsZipFind();

  nsZipFind& operator=(const nsZipFind& rhs) = delete;
  nsZipFind(const nsZipFind& rhs) = delete;

  nsresult FindNext(const char** aResult, uint16_t* aNameLen);

 private:
  RefPtr<nsZipArchive> mArchive;
  char* mPattern;
  nsZipItem* mItem;
  uint16_t mSlot;
  bool mRegExp;
};

/**
 * nsZipCursor -- a low-level class for reading the individual items in a zip.
 */
class nsZipCursor final {
 public:
  /**
   * Initializes the cursor
   *
   * @param   aItem       Item of interest
   * @param   aZip        Archive
   * @param   aBuf        Buffer used for decompression.
   *                      This determines the maximum Read() size in the
   *                      compressed case.
   * @param   aBufSize    Buffer size
   * @param   doCRC       When set to true Read() will check crc
   */
  nsZipCursor(nsZipItem* aItem, nsZipArchive* aZip, uint8_t* aBuf = nullptr,
              uint32_t aBufSize = 0, bool doCRC = false);

  ~nsZipCursor();

  /**
   * Performs reads. In the compressed case it uses aBuf(passed in constructor),
   * for stored files it returns a zero-copy buffer.
   *
   * @param   aBytesRead  Outparam for number of bytes read.
   * @return  data read or nullptr if item is corrupted.
   */
  uint8_t* Read(uint32_t* aBytesRead) { return ReadOrCopy(aBytesRead, false); }

  /**
   * Performs a copy. It always uses aBuf(passed in constructor).
   *
   * @param   aBytesRead  Outparam for number of bytes read.
   * @return  data read or nullptr if item is corrupted.
   */
  uint8_t* Copy(uint32_t* aBytesRead) { return ReadOrCopy(aBytesRead, true); }

 private:
  /* Actual implementation for both Read and Copy above */
  uint8_t* ReadOrCopy(uint32_t* aBytesRead, bool aCopy);

  nsZipItem* mItem;
  uint8_t* mBuf;
  uint32_t mBufSize;
  z_stream mZs;
  uint32_t mCRC;
  bool mDoCRC;
};

/**
 * nsZipItemPtr - a RAII convenience class for reading the individual items in a
 * zip. It reads whole files and does zero-copy IO for stored files. A buffer is
 * allocated for decompression. Do not use when the file may be very large.
 */
class nsZipItemPtr_base {
 public:
  /**
   * Initializes the reader
   *
   * @param   aZip        Archive
   * @param   aEntryName  Archive membername
   * @param   doCRC       When set to true Read() will check crc
   */
  nsZipItemPtr_base(nsZipArchive* aZip, const nsACString& aEntryName,
                    bool doCRC);

  uint32_t Length() const { return mReadlen; }

 protected:
  RefPtr<nsZipHandle> mZipHandle;
  mozilla::UniquePtr<uint8_t[]> mAutoBuf;
  uint8_t* mReturnBuf;
  uint32_t mReadlen;
};

template <class T>
class nsZipItemPtr final : public nsZipItemPtr_base {
  static_assert(sizeof(T) == sizeof(char),
                "This class cannot be used with larger T without re-examining"
                " a number of assumptions.");

 public:
  nsZipItemPtr(nsZipArchive* aZip, const nsACString& aEntryName,
               bool doCRC = false)
      : nsZipItemPtr_base(aZip, aEntryName, doCRC) {}
  /**
   * @return buffer containing the whole zip member or nullptr on error.
   * The returned buffer is owned by nsZipItemReader.
   */
  const T* Buffer() const { return (const T*)mReturnBuf; }

  operator const T*() const { return Buffer(); }

  /**
   * Relinquish ownership of zip member if compressed.
   * Copy member into a new buffer if uncompressed.
   * @return a buffer with whole zip member. It is caller's responsibility to
   * free() it.
   */
  mozilla::UniquePtr<T[]> Forget() {
    if (!mReturnBuf) return nullptr;
    // In uncompressed mmap case, give up buffer
    if (mAutoBuf.get() == mReturnBuf) {
      mReturnBuf = nullptr;
      return mozilla::UniquePtr<T[]>(reinterpret_cast<T*>(mAutoBuf.release()));
    }
    auto ret = mozilla::MakeUnique<T[]>(Length());
    memcpy(ret.get(), mReturnBuf, Length());
    mReturnBuf = nullptr;
    return ret;
  }
};

class nsZipHandle final {
  friend class nsZipArchive;
  friend class nsZipFind;
  friend class mozilla::FileLocation;
  friend class nsJARInputStream;
#if defined(XP_UNIX) && !defined(XP_DARWIN)
  friend class MmapAccessScope;
#endif

 public:
  static nsresult Init(nsIFile* file, nsZipHandle** ret,
                       PRFileDesc** aFd = nullptr);
  static nsresult Init(nsZipArchive* zip, const nsACString& entry,
                       nsZipHandle** ret);
  static nsresult Init(const uint8_t* aData, uint32_t aLen, nsZipHandle** aRet);

  NS_METHOD_(MozExternalRefCountType) AddRef(void);
  NS_METHOD_(MozExternalRefCountType) Release(void);

  int64_t SizeOfMapping();

  nsresult GetNSPRFileDesc(PRFileDesc** aNSPRFileDesc);

 protected:
  const uint8_t* mFileData;    /* pointer to zip data */
  uint32_t mLen;               /* length of zip data */
  mozilla::FileLocation mFile; /* source file if any, for logging */

 private:
  nsZipHandle();
  ~nsZipHandle();

  nsresult findDataStart();

  PRFileMap* mMap; /* nspr datastructure for mmap */
  mozilla::AutoFDClose mNSPRFileDesc;
  mozilla::UniquePtr<nsZipItemPtr<uint8_t> > mBuf;
  mozilla::ThreadSafeAutoRefCnt mRefCnt; /* ref count */
  NS_DECL_OWNINGTHREAD

  const uint8_t* mFileStart; /* pointer to mmaped file */
  uint32_t mTotalLen;        /* total length of the mmaped file */

  /* Magic number for CRX type expressed in Big Endian since it is a literal */
  static const uint32_t kCRXMagic = 0x34327243;
};

nsresult gZlibInit(z_stream* zs);

#endif /* nsZipArchive_h_ */
