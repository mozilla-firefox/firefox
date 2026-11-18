// Originally based on Chrome sources:
// Copyright (c) 2010 The Chromium Authors. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//    * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//    * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//    * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "HashStore.h"
#include "nsICryptoHash.h"
#include "nsISeekableStream.h"
#include "nsNetUtil.h"
#include "nsCheckSummedOutputStream.h"
#include "prio.h"
#include "mozilla/Logging.h"
#include "zlib.h"
#include "Classifier.h"
#include "nsUrlClassifierDBService.h"

// Main store for SafeBrowsing protocol data. We store
// known add/sub chunks, prefixes and completions in memory
// during an update, and serialize to disk.
// We do not store the add prefixes, those are retrieved by
// decompressing the PrefixSet cache whenever we need to apply
// an update.
//
// byte slicing: Many of the 4-byte values stored here are strongly
// correlated in the upper bytes, and uncorrelated in the lower
// bytes. Because zlib/DEFLATE requires match lengths of at least
// 3 to achieve good compression, and we don't get those if only
// the upper 16-bits are correlated, it is worthwhile to slice 32-bit
// values into 4 1-byte slices and compress the slices individually.
// The slices corresponding to MSBs will compress very well, and the
// slice corresponding to LSB almost nothing. Because of this, we
// only apply DEFLATE to the 3 most significant bytes, and store the
// LSB uncompressed.
//
// byte sliced (numValues) data format:
//    uint32_t compressed-size
//    compressed-size bytes    zlib DEFLATE data
//        0...numValues        byte MSB of 4-byte numValues data
//    uint32_t compressed-size
//    compressed-size bytes    zlib DEFLATE data
//        0...numValues        byte 2nd byte of 4-byte numValues data
//    uint32_t compressed-size
//    compressed-size bytes    zlib DEFLATE data
//        0...numValues        byte 3rd byte of 4-byte numValues data
//    0...numValues            byte LSB of 4-byte numValues data
//
// Store data format:
//    uint32_t magic
//    uint32_t version
//    uint32_t numAddChunks
//    uint32_t numSubChunks
//    uint32_t numAddPrefixes
//    uint32_t numSubPrefixes
//    uint32_t numAddCompletes
//    uint32_t numSubCompletes
//    0...numAddChunks               uint32_t addChunk
//    0...numSubChunks               uint32_t subChunk
//    byte sliced (numAddPrefixes)   uint32_t add chunk of AddPrefixes
//    byte sliced (numSubPrefixes)   uint32_t add chunk of SubPrefixes
//    byte sliced (numSubPrefixes)   uint32_t sub chunk of SubPrefixes
//    byte sliced (numSubPrefixes)   uint32_t SubPrefixes
//    byte sliced (numAddCompletes)  uint32_t add chunk of AddCompletes
//    0...numSubCompletes            32-byte Completions + uint32_t addChunk
//                                                       + uint32_t subChunk
//    16-byte MD5 of all preceding data

// Name of the SafeBrowsing store
#define STORE_SUFFIX ".sbstore"

// MOZ_LOG=UrlClassifierDbService:5
extern mozilla::LazyLogModule gUrlClassifierDbServiceLog;
#define LOG(args) \
  MOZ_LOG(gUrlClassifierDbServiceLog, mozilla::LogLevel::Debug, args)
#define LOG_ENABLED() \
  MOZ_LOG_TEST(gUrlClassifierDbServiceLog, mozilla::LogLevel::Debug)

namespace mozilla::safebrowsing {

const uint32_t STORE_MAGIC = 0x1231af3b;
const uint32_t CURRENT_VERSION = 4;

nsresult TableUpdateV2::NewAddPrefix(uint32_t aAddChunk, const Prefix& aHash) {
  AddPrefix* add = mAddPrefixes.AppendElement(fallible);
  if (!add) return NS_ERROR_OUT_OF_MEMORY;
  add->addChunk = aAddChunk;
  add->prefix = aHash;
  return NS_OK;
}

nsresult TableUpdateV2::NewSubPrefix(uint32_t aAddChunk, const Prefix& aHash,
                                     uint32_t aSubChunk) {
  SubPrefix* sub = mSubPrefixes.AppendElement(fallible);
  if (!sub) return NS_ERROR_OUT_OF_MEMORY;
  sub->addChunk = aAddChunk;
  sub->prefix = aHash;
  sub->subChunk = aSubChunk;
  return NS_OK;
}

nsresult TableUpdateV2::NewAddComplete(uint32_t aAddChunk,
                                       const Completion& aHash) {
  AddComplete* add = mAddCompletes.AppendElement(fallible);
  if (!add) return NS_ERROR_OUT_OF_MEMORY;
  add->addChunk = aAddChunk;
  add->complete = aHash;
  return NS_OK;
}

nsresult TableUpdateV2::NewSubComplete(uint32_t aAddChunk,
                                       const Completion& aHash,
                                       uint32_t aSubChunk) {
  SubComplete* sub = mSubCompletes.AppendElement(fallible);
  if (!sub) return NS_ERROR_OUT_OF_MEMORY;
  sub->addChunk = aAddChunk;
  sub->complete = aHash;
  sub->subChunk = aSubChunk;
  return NS_OK;
}

nsresult TableUpdateV2::NewMissPrefix(const Prefix& aPrefix) {
  Prefix* prefix = mMissPrefixes.AppendElement(aPrefix, fallible);
  if (!prefix) return NS_ERROR_OUT_OF_MEMORY;
  return NS_OK;
}

void TableUpdateV4::NewPrefixes(int32_t aSize, const nsACString& aPrefixes) {
  NS_ENSURE_TRUE_VOID(aSize >= 4 && aSize <= COMPLETE_SIZE);
  NS_ENSURE_TRUE_VOID(aPrefixes.Length() % aSize == 0);
  NS_ENSURE_TRUE_VOID(!mPrefixesMap.Contains(aSize));

  int numOfPrefixes = aPrefixes.Length() / aSize;

  if (aSize <= 4 && LOG_ENABLED()) {
    const uint32_t* p =
        reinterpret_cast<const uint32_t*>(aPrefixes.BeginReading());

    // Dump the first/last 10 fixed-length prefixes for debugging.
    LOG(("* The first 10 (maximum) fixed-length prefixes: "));
    for (int i = 0; i < std::min(10, numOfPrefixes); i++) {
      const uint8_t* c = reinterpret_cast<const uint8_t*>(&p[i]);
      LOG(("%.2X%.2X%.2X%.2X", c[0], c[1], c[2], c[3]));
    }

    LOG(("* The last 10 (maximum) fixed-length prefixes: "));
    for (int i = std::max(0, numOfPrefixes - 10); i < numOfPrefixes; i++) {
      const uint8_t* c = reinterpret_cast<const uint8_t*>(&p[i]);
      LOG(("%.2X%.2X%.2X%.2X", c[0], c[1], c[2], c[3]));
    }

    LOG(("---- %zu fixed-length prefixes in total.",
         aPrefixes.Length() / aSize));
  }

  mPrefixesMap.InsertOrUpdate(aSize, std::make_unique<nsCString>(aPrefixes));
}

nsresult TableUpdateV4::NewRemovalIndices(const uint32_t* aIndices,
                                          size_t aNumOfIndices) {
  MOZ_ASSERT(mRemovalIndiceArray.IsEmpty(),
             "mRemovalIndiceArray must be empty");

  if (!mRemovalIndiceArray.SetCapacity(aNumOfIndices, fallible)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  for (size_t i = 0; i < aNumOfIndices; i++) {
    mRemovalIndiceArray.AppendElement(aIndices[i]);
  }
  return NS_OK;
}

void TableUpdateV4::SetSHA256(const std::string& aSHA256) {
  mSHA256.Assign(aSHA256.data(), aSHA256.size());
}

nsresult TableUpdateV4::NewFullHashResponse(
    const Prefix& aPrefix, const CachedFullHashResponse& aResponse) {
  CachedFullHashResponse* response =
      mFullHashResponseMap.GetOrInsertNew(aPrefix.ToUint32());
  if (!response) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  *response = aResponse;
  return NS_OK;
}

void TableUpdateV4::Clear() {
  mPrefixesMap.Clear();
  mRemovalIndiceArray.Clear();
}

HashStore::HashStore(const nsACString& aTableName, const nsACString& aProvider,
                     nsIFile* aRootStoreDir)
    : mTableName(aTableName), mInUpdate(false), mFileSize(0) {
  nsresult rv = Classifier::GetPrivateStoreDirectory(
      aRootStoreDir, aTableName, aProvider, getter_AddRefs(mStoreDirectory));
  if (NS_FAILED(rv)) {
    LOG(("Failed to get private store directory for %s", mTableName.get()));
    mStoreDirectory = aRootStoreDir;
  }
}

HashStore::~HashStore() = default;

nsresult HashStore::Reset() {
  LOG(("HashStore resetting"));

  // Close InputStream before removing the file
  if (mInputStream) {
    nsresult rv = mInputStream->Close();
    NS_ENSURE_SUCCESS(rv, rv);

    mInputStream = nullptr;
  }

  nsCOMPtr<nsIFile> storeFile;
  nsresult rv = mStoreDirectory->Clone(getter_AddRefs(storeFile));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = storeFile->AppendNative(mTableName + nsLiteralCString(STORE_SUFFIX));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = storeFile->Remove(false);
  NS_ENSURE_SUCCESS(rv, rv);

  mFileSize = 0;

  return NS_OK;
}

nsresult HashStore::CheckChecksum(uint32_t aFileSize) {
  if (!mInputStream) {
    return NS_OK;
  }

  // Check for file corruption by
  // comparing the stored checksum to actual checksum of data
  nsAutoCString hash;
  nsAutoCString compareHash;
  uint32_t read;

  nsresult rv = CalculateChecksum(hash, aFileSize, true);
  NS_ENSURE_SUCCESS(rv, rv);

  compareHash.SetLength(hash.Length());

  if (hash.Length() > aFileSize) {
    NS_WARNING("SafeBrowsing file not long enough to store its hash");
    return NS_ERROR_FAILURE;
  }
  nsCOMPtr<nsISeekableStream> seekIn = do_QueryInterface(mInputStream);
  rv = seekIn->Seek(nsISeekableStream::NS_SEEK_SET, aFileSize - hash.Length());
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mInputStream->Read(compareHash.BeginWriting(), hash.Length(), &read);
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ASSERTION(read == hash.Length(), "Could not read hash bytes");

  if (!hash.Equals(compareHash)) {
    NS_WARNING("SafeBrowsing file failed checksum.");
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

nsresult HashStore::Open(uint32_t aVersion) {
  nsCOMPtr<nsIFile> storeFile;
  nsresult rv = mStoreDirectory->Clone(getter_AddRefs(storeFile));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = storeFile->AppendNative(mTableName + ".sbstore"_ns);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIInputStream> origStream;
  rv = NS_NewLocalFileInputStream(getter_AddRefs(origStream), storeFile,
                                  PR_RDONLY | nsIFile::OS_READAHEAD);

  if (rv == NS_ERROR_FILE_NOT_FOUND) {
    UpdateHeader();
    return NS_OK;
  }
  NS_ENSURE_SUCCESS(rv, rv);

  int64_t fileSize;
  rv = storeFile->GetFileSize(&fileSize);
  NS_ENSURE_SUCCESS(rv, rv);

  if (fileSize < 0 || fileSize > UINT32_MAX) {
    return NS_ERROR_FAILURE;
  }

  mFileSize = static_cast<uint32_t>(fileSize);
  rv = NS_NewBufferedInputStream(getter_AddRefs(mInputStream),
                                 origStream.forget(), mFileSize);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = ReadHeader();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    LOG(("Failed to read header for %s", mTableName.get()));
    return NS_ERROR_FILE_CORRUPTED;
  }

  rv = SanityCheck(aVersion);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult HashStore::ReadHeader() {
  if (!mInputStream) {
    UpdateHeader();
    return NS_OK;
  }

  nsCOMPtr<nsISeekableStream> seekable = do_QueryInterface(mInputStream);
  nsresult rv = seekable->Seek(nsISeekableStream::NS_SEEK_SET, 0);
  NS_ENSURE_SUCCESS(rv, rv);

  void* buffer = &mHeader;
  rv = NS_ReadInputStreamToBuffer(mInputStream, &buffer, sizeof(Header));
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult HashStore::SanityCheck(uint32_t aVersion) const {
  const uint32_t VER = aVersion == 0 ? CURRENT_VERSION : aVersion;
  if (mHeader.magic != STORE_MAGIC || mHeader.version != VER) {
    NS_WARNING("Unexpected header data in the store.");
    // Version mismatch is also considered file corrupted,
    // We need this error code to know if we should remove the file.
    return NS_ERROR_FILE_CORRUPTED;
  }

  return NS_OK;
}

nsresult HashStore::CalculateChecksum(nsAutoCString& aChecksum,
                                      uint32_t aFileSize,
                                      bool aChecksumPresent) {
  aChecksum.Truncate();

  // Reset mInputStream to start
  nsCOMPtr<nsISeekableStream> seekable = do_QueryInterface(mInputStream);
  nsresult rv = seekable->Seek(nsISeekableStream::NS_SEEK_SET, 0);

  nsCOMPtr<nsICryptoHash> hash =
      do_CreateInstance(NS_CRYPTO_HASH_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  // Size of MD5 hash in bytes
  const uint32_t CHECKSUM_SIZE = 16;

  // MD5 is not a secure hash function, but since this is a filesystem integrity
  // check, this usage is ok.
  rv = hash->Init(nsICryptoHash::MD5);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!aChecksumPresent) {
    // Hash entire file
    rv = hash->UpdateFromStream(mInputStream, UINT32_MAX);
  } else {
    // Hash everything but last checksum bytes
    if (aFileSize < CHECKSUM_SIZE) {
      NS_WARNING("SafeBrowsing file isn't long enough to store its checksum");
      return NS_ERROR_FAILURE;
    }
    rv = hash->UpdateFromStream(mInputStream, aFileSize - CHECKSUM_SIZE);
  }
  NS_ENSURE_SUCCESS(rv, rv);

  rv = hash->Finish(false, aChecksum);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

void HashStore::UpdateHeader() {
  mHeader.magic = STORE_MAGIC;
  mHeader.version = CURRENT_VERSION;

  mHeader.numAddChunks = mAddChunks.Length();
  mHeader.numSubChunks = mSubChunks.Length();
  mHeader.numAddPrefixes = mAddPrefixes.Length();
  mHeader.numSubPrefixes = mSubPrefixes.Length();
  mHeader.numAddCompletes = mAddCompletes.Length();
  mHeader.numSubCompletes = mSubCompletes.Length();
}

nsresult HashStore::ReadChunkNumbers() {
  if (!mInputStream) {
    return NS_OK;
  }

  nsCOMPtr<nsISeekableStream> seekable = do_QueryInterface(mInputStream);
  nsresult rv = seekable->Seek(nsISeekableStream::NS_SEEK_SET, sizeof(Header));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mAddChunks.Read(mInputStream, mHeader.numAddChunks);
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ASSERTION(mAddChunks.Length() == mHeader.numAddChunks,
               "Read the right amount of add chunks.");

  rv = mSubChunks.Read(mInputStream, mHeader.numSubChunks);
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ASSERTION(mSubChunks.Length() == mHeader.numSubChunks,
               "Read the right amount of sub chunks.");

  return NS_OK;
}

nsresult HashStore::ReadHashes() {
  if (!mInputStream) {
    // BeginUpdate has been called but Open hasn't initialized mInputStream,
    // because the existing HashStore is empty.
    return NS_OK;
  }

  nsCOMPtr<nsISeekableStream> seekable = do_QueryInterface(mInputStream);

  uint32_t offset = sizeof(Header);
  offset += (mHeader.numAddChunks + mHeader.numSubChunks) * sizeof(uint32_t);
  nsresult rv = seekable->Seek(nsISeekableStream::NS_SEEK_SET, offset);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = ReadAddPrefixes();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = ReadSubPrefixes();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = ReadAddCompletes();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = ReadTArray(mInputStream, &mSubCompletes, mHeader.numSubCompletes);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult HashStore::PrepareForUpdate() {
  nsresult rv = CheckChecksum(mFileSize);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = ReadChunkNumbers();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = ReadHashes();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult HashStore::BeginUpdate() {
  // Check wether the file is corrupted and read the rest of the store
  // in memory.
  nsresult rv = PrepareForUpdate();
  NS_ENSURE_SUCCESS(rv, rv);

  // Close input stream, won't be needed any more and
  // we will rewrite ourselves.
  if (mInputStream) {
    rv = mInputStream->Close();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  mInUpdate = true;

  return NS_OK;
}

template <class T>
static nsresult Merge(ChunkSet* aStoreChunks, FallibleTArray<T>* aStorePrefixes,
                      const ChunkSet& aUpdateChunks,
                      FallibleTArray<T>& aUpdatePrefixes,
                      bool aAllowMerging = false) {
  EntrySort(aUpdatePrefixes);

  auto storeIter = aStorePrefixes->begin();
  auto storeEnd = aStorePrefixes->end();

  // use a separate array so we can keep the iterators valid
  // if the nsTArray grows
  nsTArray<T> adds;

  for (const auto& updatePrefix : aUpdatePrefixes) {
    // skip this chunk if we already have it, unless we're
    // merging completions, in which case we'll always already
    // have the chunk from the original prefix
    if (aStoreChunks->Has(updatePrefix.Chunk()))
      if (!aAllowMerging) continue;
    // XXX: binary search for insertion point might be faster in common
    // case?
    while (storeIter < storeEnd && (storeIter->Compare(updatePrefix) < 0)) {
      // skip forward to matching element (or not...)
      storeIter++;
    }
    // no match, add
    if (storeIter == storeEnd || storeIter->Compare(updatePrefix) != 0) {
      if (!adds.AppendElement(updatePrefix, fallible)) {
        return NS_ERROR_OUT_OF_MEMORY;
      }
    }
  }

  // Chunks can be empty, but we should still report we have them
  // to make the chunkranges continuous.
  aStoreChunks->Merge(aUpdateChunks);

  if (!aStorePrefixes->AppendElements(adds, fallible))
    return NS_ERROR_OUT_OF_MEMORY;

  EntrySort(*aStorePrefixes);

  return NS_OK;
}

nsresult HashStore::ApplyUpdate(RefPtr<TableUpdateV2> aUpdate) {
  MOZ_ASSERT(mTableName.Equals(aUpdate->TableName()));

  nsresult rv = mAddExpirations.Merge(aUpdate->AddExpirations());
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mSubExpirations.Merge(aUpdate->SubExpirations());
  NS_ENSURE_SUCCESS(rv, rv);

  rv = Expire();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = Merge(&mAddChunks, &mAddPrefixes, aUpdate->AddChunks(),
             aUpdate->AddPrefixes());
  NS_ENSURE_SUCCESS(rv, rv);

  rv = Merge(&mAddChunks, &mAddCompletes, aUpdate->AddChunks(),
             aUpdate->AddCompletes(), true);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = Merge(&mSubChunks, &mSubPrefixes, aUpdate->SubChunks(),
             aUpdate->SubPrefixes());
  NS_ENSURE_SUCCESS(rv, rv);

  rv = Merge(&mSubChunks, &mSubCompletes, aUpdate->SubChunks(),
             aUpdate->SubCompletes(), true);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult HashStore::Rebuild() {
  NS_ASSERTION(mInUpdate, "Must be in update to rebuild.");

  nsresult rv = ProcessSubs();
  NS_ENSURE_SUCCESS(rv, rv);

  UpdateHeader();

  return NS_OK;
}

template <class T>
static void ExpireEntries(FallibleTArray<T>* aEntries, ChunkSet& aExpirations) {
  auto addIter = aEntries->begin();

  for (const auto& entry : *aEntries) {
    if (!aExpirations.Has(entry.Chunk())) {
      *addIter = entry;
      addIter++;
    }
  }

  aEntries->TruncateLength(addIter - aEntries->begin());
}

nsresult HashStore::Expire() {
  ExpireEntries(&mAddPrefixes, mAddExpirations);
  ExpireEntries(&mAddCompletes, mAddExpirations);
  ExpireEntries(&mSubPrefixes, mSubExpirations);
  ExpireEntries(&mSubCompletes, mSubExpirations);

  mAddChunks.Remove(mAddExpirations);
  mSubChunks.Remove(mSubExpirations);

  mAddExpirations.Clear();
  mSubExpirations.Clear();

  return NS_OK;
}

template <class T>
nsresult DeflateWriteTArray(nsIOutputStream* aStream, nsTArray<T>& aIn) {
  uLongf insize = aIn.Length() * sizeof(T);
  uLongf outsize = compressBound(insize);
  FallibleTArray<char> outBuff;
  if (!outBuff.SetLength(outsize, fallible)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  int zerr = compress(reinterpret_cast<Bytef*>(outBuff.Elements()), &outsize,
                      reinterpret_cast<const Bytef*>(aIn.Elements()), insize);
  if (zerr != Z_OK) {
    return NS_ERROR_FAILURE;
  }
  LOG(("DeflateWriteTArray: %lu in %lu out", insize, outsize));

  outBuff.TruncateLength(outsize);

  // Length of compressed data stream
  uint32_t dataLen = outBuff.Length();
  uint32_t written;
  nsresult rv = aStream->Write(reinterpret_cast<char*>(&dataLen),
                               sizeof(dataLen), &written);
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ASSERTION(written == sizeof(dataLen), "Error writing deflate length");

  // Store to stream
  rv = WriteTArray(aStream, outBuff);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

template <class T>
nsresult InflateReadTArray(nsIInputStream* aStream, FallibleTArray<T>* aOut,
                           uint32_t aExpectedSize) {
  uint32_t inLen;
  uint32_t read;
  nsresult rv =
      aStream->Read(reinterpret_cast<char*>(&inLen), sizeof(inLen), &read);
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ASSERTION(read == sizeof(inLen), "Error reading inflate length");

  FallibleTArray<char> inBuff;
  if (!inBuff.SetLength(inLen, fallible)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  rv = ReadTArray(aStream, &inBuff, inLen);
  NS_ENSURE_SUCCESS(rv, rv);

  uLongf insize = inLen;
  uLongf outsize = aExpectedSize * sizeof(T);
  if (!aOut->SetLength(aExpectedSize, fallible)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  int zerr =
      uncompress(reinterpret_cast<Bytef*>(aOut->Elements()), &outsize,
                 reinterpret_cast<const Bytef*>(inBuff.Elements()), insize);
  if (zerr != Z_OK) {
    return NS_ERROR_FAILURE;
  }
  LOG(("InflateReadTArray: %lu in %lu out", insize, outsize));

  NS_ASSERTION(outsize == aExpectedSize * sizeof(T),
               "Decompression size mismatch");

  return NS_OK;
}

static nsresult ByteSliceWrite(nsIOutputStream* aOut,
                               nsTArray<uint32_t>& aData) {
  nsTArray<uint8_t> slice;
  uint32_t count = aData.Length();

  // Only process one slice at a time to avoid using too much memory.
  if (!slice.SetLength(count, fallible)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  // Process slice 1.
  for (uint32_t i = 0; i < count; i++) {
    slice[i] = (aData[i] >> 24);
  }

  nsresult rv = DeflateWriteTArray(aOut, slice);
  NS_ENSURE_SUCCESS(rv, rv);

  // Process slice 2.
  for (uint32_t i = 0; i < count; i++) {
    slice[i] = ((aData[i] >> 16) & 0xFF);
  }

  rv = DeflateWriteTArray(aOut, slice);
  NS_ENSURE_SUCCESS(rv, rv);

  // Process slice 3.
  for (uint32_t i = 0; i < count; i++) {
    slice[i] = ((aData[i] >> 8) & 0xFF);
  }

  rv = DeflateWriteTArray(aOut, slice);
  NS_ENSURE_SUCCESS(rv, rv);

  // Process slice 4.
  for (uint32_t i = 0; i < count; i++) {
    slice[i] = (aData[i] & 0xFF);
  }

  // The LSB slice is generally uncompressible, don't bother
  // compressing it.
  rv = WriteTArray(aOut, slice);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

static nsresult ByteSliceRead(nsIInputStream* aInStream,
                              FallibleTArray<uint32_t>* aData, uint32_t count) {
  FallibleTArray<uint8_t> slice1;
  FallibleTArray<uint8_t> slice2;
  FallibleTArray<uint8_t> slice3;
  FallibleTArray<uint8_t> slice4;

  nsresult rv = InflateReadTArray(aInStream, &slice1, count);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = InflateReadTArray(aInStream, &slice2, count);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = InflateReadTArray(aInStream, &slice3, count);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = ReadTArray(aInStream, &slice4, count);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!aData->SetCapacity(count, fallible)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  for (uint32_t i = 0; i < count; i++) {
    // SetCapacity was just called, these cannot fail.
    (void)aData->AppendElement(
        (slice1[i] << 24) | (slice2[i] << 16) | (slice3[i] << 8) | (slice4[i]),
        fallible);
  }

  return NS_OK;
}

nsresult HashStore::ReadAddPrefixes() {
  FallibleTArray<uint32_t> chunks;
  uint32_t count = mHeader.numAddPrefixes;

  nsresult rv = ByteSliceRead(mInputStream, &chunks, count);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!mAddPrefixes.SetCapacity(count, fallible)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  for (uint32_t i = 0; i < count; i++) {
    AddPrefix* add = mAddPrefixes.AppendElement(fallible);
    add->prefix.FromUint32(0);
    add->addChunk = chunks[i];
  }

  return NS_OK;
}

nsresult HashStore::ReadAddCompletes() {
  FallibleTArray<uint32_t> chunks;
  uint32_t count = mHeader.numAddCompletes;

  nsresult rv = ByteSliceRead(mInputStream, &chunks, count);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!mAddCompletes.SetCapacity(count, fallible)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  for (uint32_t i = 0; i < count; i++) {
    AddComplete* add = mAddCompletes.AppendElement(fallible);
    add->addChunk = chunks[i];
  }

  return NS_OK;
}

nsresult HashStore::ReadSubPrefixes() {
  FallibleTArray<uint32_t> addchunks;
  FallibleTArray<uint32_t> subchunks;
  FallibleTArray<uint32_t> prefixes;
  uint32_t count = mHeader.numSubPrefixes;

  nsresult rv = ByteSliceRead(mInputStream, &addchunks, count);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = ByteSliceRead(mInputStream, &subchunks, count);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = ByteSliceRead(mInputStream, &prefixes, count);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!mSubPrefixes.SetCapacity(count, fallible)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  for (uint32_t i = 0; i < count; i++) {
    SubPrefix* sub = mSubPrefixes.AppendElement(fallible);
    sub->addChunk = addchunks[i];
    sub->prefix.FromUint32(prefixes[i]);
    sub->subChunk = subchunks[i];
  }

  return NS_OK;
}

// Split up PrefixArray back into the constituents
nsresult HashStore::WriteAddPrefixChunks(nsIOutputStream* aOut) {
  nsTArray<uint32_t> chunks;
  uint32_t count = mAddPrefixes.Length();
  if (!chunks.SetCapacity(count, fallible)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  for (uint32_t i = 0; i < count; i++) {
    chunks.AppendElement(mAddPrefixes[i].Chunk());
  }

  nsresult rv = ByteSliceWrite(aOut, chunks);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult HashStore::WriteAddCompleteChunks(nsIOutputStream* aOut) {
  nsTArray<uint32_t> chunks;
  uint32_t count = mAddCompletes.Length();
  if (!chunks.SetCapacity(count, fallible)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  for (uint32_t i = 0; i < count; i++) {
    chunks.AppendElement(mAddCompletes[i].Chunk());
  }

  nsresult rv = ByteSliceWrite(aOut, chunks);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult HashStore::WriteSubPrefixes(nsIOutputStream* aOut) {
  nsTArray<uint32_t> addchunks;
  nsTArray<uint32_t> subchunks;
  nsTArray<uint32_t> prefixes;
  uint32_t count = mSubPrefixes.Length();
  if (!addchunks.SetCapacity(count, fallible) ||
      !subchunks.SetCapacity(count, fallible) ||
      !prefixes.SetCapacity(count, fallible)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  for (uint32_t i = 0; i < count; i++) {
    addchunks.AppendElement(mSubPrefixes[i].AddChunk());
    prefixes.AppendElement(mSubPrefixes[i].PrefixHash().ToUint32());
    subchunks.AppendElement(mSubPrefixes[i].Chunk());
  }

  nsresult rv = ByteSliceWrite(aOut, addchunks);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = ByteSliceWrite(aOut, subchunks);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = ByteSliceWrite(aOut, prefixes);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult HashStore::WriteFile() {
  NS_ASSERTION(mInUpdate, "Must be in update to write database.");
  if (nsUrlClassifierDBService::ShutdownHasStarted()) {
    return NS_ERROR_ABORT;
  }

  nsCOMPtr<nsIFile> storeFile;
  nsresult rv = mStoreDirectory->Clone(getter_AddRefs(storeFile));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = storeFile->AppendNative(mTableName + ".sbstore"_ns);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIOutputStream> out;
  rv = NS_NewCheckSummedOutputStream(getter_AddRefs(out), storeFile);
  NS_ENSURE_SUCCESS(rv, rv);

  uint32_t written;
  rv = out->Write(reinterpret_cast<char*>(&mHeader), sizeof(mHeader), &written);
  NS_ENSURE_SUCCESS(rv, rv);

  // Write chunk numbers.
  rv = mAddChunks.Write(out);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mSubChunks.Write(out);
  NS_ENSURE_SUCCESS(rv, rv);

  // Write hashes.
  rv = WriteAddPrefixChunks(out);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = WriteSubPrefixes(out);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = WriteAddCompleteChunks(out);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = WriteTArray(out, mSubCompletes);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsISafeOutputStream> safeOut = do_QueryInterface(out, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = safeOut->Finish();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult HashStore::ReadCompletionsLegacyV3(AddCompleteArray& aCompletes) {
  if (mHeader.version != 3) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIFile> storeFile;
  nsresult rv = mStoreDirectory->Clone(getter_AddRefs(storeFile));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = storeFile->AppendNative(mTableName + nsLiteralCString(STORE_SUFFIX));
  NS_ENSURE_SUCCESS(rv, rv);

  uint32_t offset = mFileSize -
                    sizeof(struct AddComplete) * mHeader.numAddCompletes -
                    sizeof(struct SubComplete) * mHeader.numSubCompletes -
                    nsCheckSummedOutputStream::CHECKSUM_SIZE;

  nsCOMPtr<nsISeekableStream> seekable = do_QueryInterface(mInputStream);

  rv = seekable->Seek(nsISeekableStream::NS_SEEK_SET, offset);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = ReadTArray(mInputStream, &aCompletes, mHeader.numAddCompletes);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

template <class T>
static void Erase(FallibleTArray<T>* array,
                  typename FallibleTArray<T>::iterator& iterStart,
                  typename FallibleTArray<T>::iterator& iterEnd) {
  array->RemoveElementsRange(iterStart, iterEnd);
}

// Find items matching between |subs| and |adds|, and remove them,
// recording the item from |adds| in |adds_removed|.  To minimize
// copies, the inputs are processing in parallel, so |subs| and |adds|
// should be compatibly ordered (either by SBAddPrefixLess or
// SBAddPrefixHashLess).
//
// |predAS| provides add < sub, |predSA| provides sub < add, for the
// tightest compare appropriate (see calls in SBProcessSubs).
template <class TSub, class TAdd>
static void KnockoutSubs(FallibleTArray<TSub>* aSubs,
                         FallibleTArray<TAdd>* aAdds) {
  // Keep a pair of output iterators for writing kept items.  Due to
  // deletions, these may lag the main iterators.  Using erase() on
  // individual items would result in O(N^2) copies.  Using a list
  // would work around that, at double or triple the memory cost.
  auto addOut = aAdds->begin();
  auto addIter = aAdds->begin();

  auto subOut = aSubs->begin();
  auto subIter = aSubs->begin();

  auto addEnd = aAdds->end();
  auto subEnd = aSubs->end();

  while (addIter != addEnd && subIter != subEnd) {
    // additer compare, so it compares on add chunk
    int32_t cmp = addIter->Compare(*subIter);
    if (cmp > 0) {
      // If |*sub_iter| < |*add_iter|, retain the sub.
      *subOut = *subIter;
      ++subOut;
      ++subIter;
    } else if (cmp < 0) {
      // If |*add_iter| < |*sub_iter|, retain the add.
      *addOut = *addIter;
      ++addOut;
      ++addIter;
    } else {
      // Drop equal items
      ++addIter;
      ++subIter;
    }
  }

  Erase(aAdds, addOut, addIter);
  Erase(aSubs, subOut, subIter);
}

// Remove items in |removes| from |fullHashes|.  |fullHashes| and
// |removes| should be ordered by SBAddPrefix component.
template <class T>
static void RemoveMatchingPrefixes(const SubPrefixArray& aSubs,
                                   FallibleTArray<T>* aFullHashes) {
  // Where to store kept items.
  auto out = aFullHashes->begin();
  auto hashIter = aFullHashes->begin();
  auto hashEnd = aFullHashes->end();

  auto removeIter = aSubs.begin();
  auto removeEnd = aSubs.end();

  while (hashIter != hashEnd && removeIter != removeEnd) {
    int32_t cmp = removeIter->CompareAlt(*hashIter);
    if (cmp > 0) {
      // Keep items less than |*removeIter|.
      *out = *hashIter;
      ++out;
      ++hashIter;
    } else if (cmp < 0) {
      // No hit for |*removeIter|, bump it forward.
      ++removeIter;
    } else {
      // Drop equal items, there may be multiple hits.
      do {
        ++hashIter;
      } while (hashIter != hashEnd && !(removeIter->CompareAlt(*hashIter) < 0));
      ++removeIter;
    }
  }
  Erase(aFullHashes, out, hashIter);
}

static void RemoveDeadSubPrefixes(SubPrefixArray& aSubs, ChunkSet& aAddChunks) {
  auto subIter = aSubs.begin();

  for (const auto& sub : aSubs) {
    bool hasChunk = aAddChunks.Has(sub.AddChunk());
    // Keep the subprefix if the chunk it refers to is one
    // we haven't seen it yet.
    if (!hasChunk) {
      *subIter = sub;
      subIter++;
    }
  }

  LOG(("Removed %" PRId64 " dead SubPrefix entries.",
       static_cast<int64_t>(aSubs.end() - subIter)));
  aSubs.TruncateLength(subIter - aSubs.begin());
}

#ifdef DEBUG
template <class T>
static void EnsureSorted(FallibleTArray<T>* aArray) {
  auto start = aArray->begin();
  auto end = aArray->end();
  auto iter = start;
  auto previous = start;

  while (iter != end) {
    previous = iter;
    ++iter;
    if (iter != end) {
      MOZ_ASSERT(iter->Compare(*previous) >= 0);
    }
  }
}
#endif

nsresult HashStore::ProcessSubs() {
#ifdef DEBUG
  EnsureSorted(&mAddPrefixes);
  EnsureSorted(&mSubPrefixes);
  EnsureSorted(&mAddCompletes);
  EnsureSorted(&mSubCompletes);
  LOG(("All databases seem to have a consistent sort order."));
#endif

  RemoveMatchingPrefixes(mSubPrefixes, &mAddCompletes);
  RemoveMatchingPrefixes(mSubPrefixes, &mSubCompletes);

  // Remove any remaining subbed prefixes from both addprefixes
  // and addcompletes.
  KnockoutSubs(&mSubPrefixes, &mAddPrefixes);
  KnockoutSubs(&mSubCompletes, &mAddCompletes);

  // Remove any remaining subprefixes referring to addchunks that
  // we have (and hence have been processed above).
  RemoveDeadSubPrefixes(mSubPrefixes, mAddChunks);

#ifdef DEBUG
  EnsureSorted(&mAddPrefixes);
  EnsureSorted(&mSubPrefixes);
  EnsureSorted(&mAddCompletes);
  EnsureSorted(&mSubCompletes);
  LOG(("All databases seem to have a consistent sort order."));
#endif

  return NS_OK;
}

nsresult HashStore::AugmentAdds(const nsTArray<uint32_t>& aPrefixes,
                                const nsTArray<nsCString>& aCompletes) {
  if (aPrefixes.Length() != mAddPrefixes.Length() ||
      aCompletes.Length() != mAddCompletes.Length()) {
    LOG((
        "Amount of prefixes/completes in cache not consistent with store prefixes(%zu vs %zu), \
          store completes(%zu vs %zu)",
        aPrefixes.Length(), mAddPrefixes.Length(), aCompletes.Length(),
        mAddCompletes.Length()));
    return NS_ERROR_FAILURE;
  }

  for (size_t i = 0; i < mAddPrefixes.Length(); i++) {
    mAddPrefixes[i].prefix.FromUint32(aPrefixes[i]);
  }

  for (size_t i = 0; i < mAddCompletes.Length(); i++) {
    mAddCompletes[i].complete.Assign(aCompletes[i]);
  }

  return NS_OK;
}

ChunkSet& HashStore::AddChunks() {
  ReadChunkNumbers();

  return mAddChunks;
}

ChunkSet& HashStore::SubChunks() {
  ReadChunkNumbers();

  return mSubChunks;
}

}  // namespace mozilla::safebrowsing
