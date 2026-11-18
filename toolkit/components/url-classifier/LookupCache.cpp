/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "LookupCache.h"
#include "LookupCacheV4.h"
#include "HashStore.h"
#include "nsIFileStreams.h"
#include "nsISeekableStream.h"
#include "mozilla/glean/UrlClassifierMetrics.h"
#include "mozilla/Logging.h"
#include "nsNetUtil.h"
#include "nsCheckSummedOutputStream.h"
#include "crc32c.h"
#include "prprf.h"
#include "Classifier.h"
#include "nsUrlClassifierInfo.h"
#include "nsUrlClassifierUtils.h"
#include "nsUrlClassifierDBService.h"
#include "mozilla/StaticPrefs_urlclassifier.h"
#include "mozilla/ProfilerMarkers.h"

#ifdef DEBUG
#  include "nsPrintfCString.h"
#endif

// We act as the main entry point for all the real lookups,
// so note that those are not done to the actual HashStore.
// The latter solely exists to store the data needed to handle
// the updates from the protocol.

// This module provides a front for PrefixSet, mUpdateCompletions,
// and mGetHashCache, which together contain everything needed to
// provide a classification as long as the data is up to date.

// PrefixSet stores and provides lookups for 4-byte prefixes.
// mUpdateCompletions contains 32-byte completions which were
// contained in updates. They are retrieved from HashStore/.sbtore
// on startup.
// mGetHashCache contains 32-byte completions which were
// returned from the gethash server. They are not serialized,
// only cached until the next update.

// MOZ_LOG=UrlClassifierDbService:5
extern mozilla::LazyLogModule gUrlClassifierDbServiceLog;
#define LOG(args) \
  MOZ_LOG(gUrlClassifierDbServiceLog, mozilla::LogLevel::Debug, args)
#define LOG_ENABLED() \
  MOZ_LOG_TEST(gUrlClassifierDbServiceLog, mozilla::LogLevel::Debug)

namespace mozilla {
namespace safebrowsing {

const uint32_t LookupCache::MAX_BUFFER_SIZE = 64 * 1024;

const int CacheResultV2::VER = CacheResult::V2;
const int CacheResultV4::VER = CacheResult::V4;

const int LookupCacheV2::VER = 2;
const uint32_t LookupCacheV2::VLPSET_MAGIC = 0xe5b862e7;
const uint32_t LookupCacheV2::VLPSET_VERSION = 1;

namespace {

//////////////////////////////////////////////////////////////////////////
// A set of lightweight functions for reading/writing value from/to file.
template <typename T>
struct ValueTraits {
  static_assert(sizeof(T) <= LookupCacheV4::MAX_METADATA_VALUE_LENGTH,
                "LookupCacheV4::MAX_METADATA_VALUE_LENGTH is too small.");
  static uint32_t Length(const T& aValue) { return sizeof(T); }
  static char* WritePtr(T& aValue, uint32_t aLength) { return (char*)&aValue; }
  static const char* ReadPtr(const T& aValue) { return (char*)&aValue; }
  static bool IsFixedLength() { return true; }
};

template <>
struct ValueTraits<nsACString> {
  static bool IsFixedLength() { return false; }

  static uint32_t Length(const nsACString& aValue) { return aValue.Length(); }

  static char* WritePtr(nsACString& aValue, uint32_t aLength) {
    aValue.SetLength(aLength);
    return aValue.BeginWriting();
  }

  static const char* ReadPtr(const nsACString& aValue) {
    return aValue.BeginReading();
  }
};

void CStringToHexString(const nsACString& aIn, nsACString& aOut) {
  static const char* const lut = "0123456789ABCDEF";

  size_t len = aIn.Length();
  MOZ_ASSERT(len <= COMPLETE_SIZE);

  aOut.SetCapacity(2 * len);
  for (size_t i = 0; i < aIn.Length(); ++i) {
    const char c = static_cast<char>(aIn[i]);
    aOut.Append(lut[(c >> 4) & 0x0F]);
    aOut.Append(lut[c & 15]);
  }
}

#ifdef DEBUG
nsCString GetFormattedTimeString(int64_t aCurTimeSec) {
  PRExplodedTime pret;
  PR_ExplodeTime(aCurTimeSec * PR_USEC_PER_SEC, PR_GMTParameters, &pret);

  return nsPrintfCString("%04d-%02d-%02d %02d:%02d:%02d UTC", pret.tm_year,
                         pret.tm_month + 1, pret.tm_mday, pret.tm_hour,
                         pret.tm_min, pret.tm_sec);
}
#endif

}  // end of unnamed namespace.
////////////////////////////////////////////////////////////////////////

template <typename T>
nsresult LookupCache::WriteValue(nsIOutputStream* aOutputStream,
                                 const T& aValue) {
  uint32_t writeLength = ValueTraits<T>::Length(aValue);
  MOZ_ASSERT(writeLength <= LookupCacheV4::MAX_METADATA_VALUE_LENGTH,
             "LookupCacheV4::MAX_METADATA_VALUE_LENGTH is too small.");
  if (!ValueTraits<T>::IsFixedLength()) {
    // We need to write out the variable value length.
    nsresult rv = WriteValue(aOutputStream, writeLength);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Write out the value.
  auto valueReadPtr = ValueTraits<T>::ReadPtr(aValue);
  uint32_t written;
  nsresult rv = aOutputStream->Write(valueReadPtr, writeLength, &written);
  NS_ENSURE_SUCCESS(rv, rv);
  if (NS_WARN_IF(written != writeLength)) {
    return NS_ERROR_FAILURE;
  }

  return rv;
}

template <typename T>
nsresult LookupCache::ReadValue(nsIInputStream* aInputStream, T& aValue) {
  nsresult rv;

  uint32_t readLength;
  if (ValueTraits<T>::IsFixedLength()) {
    readLength = ValueTraits<T>::Length(aValue);
  } else {
    // Read the variable value length from file.
    nsresult rv = ReadValue(aInputStream, readLength);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Sanity-check the readLength in case of disk corruption
  // (see bug 1433636).
  if (readLength > LookupCacheV4::MAX_METADATA_VALUE_LENGTH) {
    return NS_ERROR_FILE_CORRUPTED;
  }

  // Read the value.
  uint32_t read;
  auto valueWritePtr = ValueTraits<T>::WritePtr(aValue, readLength);
  rv = aInputStream->Read(valueWritePtr, readLength, &read);
  if (NS_FAILED(rv) || read != readLength) {
    LOG(("Failed to read the value."));
    return NS_FAILED(rv) ? rv : NS_ERROR_FAILURE;
  }

  return rv;
}

// These symbols are referenced from another compilation unit, but their
// implementation depends on local symbols. Workaround this by forcing their
// instantiation there.
template nsresult mozilla::safebrowsing::LookupCache::WriteValue(
    nsIOutputStream*, nsTSubstring<char> const&);
template nsresult mozilla::safebrowsing::LookupCache::ReadValue(
    nsIInputStream*, nsTSubstring<char>&);

LookupCache::LookupCache(const nsACString& aTableName,
                         const nsACString& aProvider,
                         nsCOMPtr<nsIFile>& aRootStoreDir)
    : mPrimed(false),
      mNeedCRC32Verification(false),
      mTableName(aTableName),
      mProvider(aProvider),
      mRootStoreDirectory(aRootStoreDir),
      mVLPrefixSet(nullptr) {
  UpdateRootDirHandle(mRootStoreDirectory);
}

nsresult LookupCache::Open() {
  LOG(("Loading PrefixSet for %s", mTableName.get()));
  nsresult rv;
  if (nsUrlClassifierUtils::IsMozTestTable(mTableName)) {
    // For built-in test table, we don't load it from disk,
    // test entries are directly added in memory.
    rv = LoadMozEntries();
  } else {
    rv = LoadPrefixSet();
  }

  (void)NS_WARN_IF(NS_FAILED(rv));

  return rv;
}

nsresult LookupCache::Init() {
  MOZ_ASSERT(!mVLPrefixSet);

  mVLPrefixSet = new VariableLengthPrefixSet();
  nsresult rv = mVLPrefixSet->Init(mTableName);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult LookupCache::UpdateRootDirHandle(
    nsCOMPtr<nsIFile>& aNewRootStoreDirectory) {
  nsresult rv;

  if (aNewRootStoreDirectory != mRootStoreDirectory) {
    rv = aNewRootStoreDirectory->Clone(getter_AddRefs(mRootStoreDirectory));
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = Classifier::GetPrivateStoreDirectory(mRootStoreDirectory, mTableName,
                                            mProvider,
                                            getter_AddRefs(mStoreDirectory));

  if (NS_FAILED(rv)) {
    LOG(("Failed to get private store directory for %s", mTableName.get()));
    mStoreDirectory = mRootStoreDirectory;
  }

  if (LOG_ENABLED()) {
    nsString path;
    mStoreDirectory->GetPath(path);
    LOG(("Private store directory for %s is %s", mTableName.get(),
         NS_ConvertUTF16toUTF8(path).get()));
  }

  return rv;
}

nsresult LookupCache::WriteFile() {
  if (nsUrlClassifierDBService::ShutdownHasStarted()) {
    return NS_ERROR_ABORT;
  }

  nsCOMPtr<nsIFile> psFile;
  nsresult rv = mStoreDirectory->Clone(getter_AddRefs(psFile));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  rv = psFile->AppendNative(mTableName + GetPrefixSetSuffix());
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  rv = StoreToFile(psFile);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    LOG(("Failed to store the prefixset for table %s", mTableName.get()));
    return rv;
  }

  return NS_OK;
}

nsresult LookupCache::CheckCache(const Completion& aCompletion, bool* aHas,
                                 bool* aConfirmed) {
  // Shouldn't call this function if prefix is not in the database.
  MOZ_ASSERT(*aHas);

  *aConfirmed = false;

  uint32_t prefix = aCompletion.ToUint32();

  CachedFullHashResponse* fullHashResponse = mFullHashCache.Get(prefix);
  if (!fullHashResponse) {
    return NS_OK;
  }

  int64_t nowSec = PR_Now() / PR_USEC_PER_SEC;
  int64_t expiryTimeSec;

  FullHashExpiryCache& fullHashes = fullHashResponse->fullHashes;
  nsDependentCSubstring completion(
      reinterpret_cast<const char*>(aCompletion.buf), COMPLETE_SIZE);

  // Check if we can find the fullhash in positive cache
  if (fullHashes.Get(completion, &expiryTimeSec)) {
    if (nowSec <= expiryTimeSec) {
      // Url is NOT safe.
      *aConfirmed = true;
      LOG(("Found a valid fullhash in the positive cache"));
    } else {
      // Trigger a gethash request in this case(aConfirmed is false).
      LOG(("Found an expired fullhash in the positive cache"));

      // Remove fullhash entry from the cache when the negative cache
      // is also expired because whether or not the fullhash is cached
      // locally, we will need to consult the server next time we
      // lookup this hash. We may as well remove it from our cache.
      if (fullHashResponse->negativeCacheExpirySec < expiryTimeSec) {
        fullHashes.Remove(completion);
        if (fullHashes.Count() == 0 &&
            fullHashResponse->negativeCacheExpirySec < nowSec) {
          mFullHashCache.Remove(prefix);
        }
      }
    }
    return NS_OK;
  }

  // Check negative cache.
  if (fullHashResponse->negativeCacheExpirySec >= nowSec) {
    // Url is safe.
    LOG(("Found a valid prefix in the negative cache"));
    *aHas = false;
  } else {
    LOG(("Found an expired prefix in the negative cache"));
    if (fullHashes.Count() == 0) {
      mFullHashCache.Remove(prefix);
    }
  }

  return NS_OK;
}

// This function remove cache entries whose negative cache time is expired.
// It is possible that a cache entry whose positive cache time is not yet
// expired but still being removed after calling this API. Right now we call
// this on every update.
void LookupCache::InvalidateExpiredCacheEntries() {
  int64_t nowSec = PR_Now() / PR_USEC_PER_SEC;

  for (auto iter = mFullHashCache.Iter(); !iter.Done(); iter.Next()) {
    CachedFullHashResponse* response = iter.UserData();
    if (response->negativeCacheExpirySec < nowSec) {
      iter.Remove();
    }
  }
}

void LookupCache::CopyFullHashCache(const LookupCache* aSource) {
  if (!aSource) {
    return;
  }

  CopyClassHashTable<FullHashResponseMap>(aSource->mFullHashCache,
                                          mFullHashCache);
}

void LookupCache::ClearCache() { mFullHashCache.Clear(); }

void LookupCache::ClearAll() {
  ClearCache();
  ClearPrefixes();
  mPrimed = false;
  mNeedCRC32Verification = false;
}

nsresult LookupCache::ClearPrefixes() {
  // Clear by seting a empty map
  PrefixStringMap map;
  return mVLPrefixSet->SetPrefixes(map);
}

bool LookupCache::IsEmpty() const {
  bool isEmpty;
  mVLPrefixSet->IsEmpty(&isEmpty);
  return isEmpty;
}

void LookupCache::GetCacheInfo(nsIUrlClassifierCacheInfo** aCache) const {
  MOZ_ASSERT(aCache);

  RefPtr<nsUrlClassifierCacheInfo> info = new nsUrlClassifierCacheInfo;
  info->table = mTableName;

  for (const auto& cacheEntry : mFullHashCache) {
    RefPtr<nsUrlClassifierCacheEntry> entry = new nsUrlClassifierCacheEntry;

    // Set prefix of the cache entry.
    nsAutoCString prefix(reinterpret_cast<const char*>(&cacheEntry.GetKey()),
                         PREFIX_SIZE);
    CStringToHexString(prefix, entry->prefix);

    // Set expiry of the cache entry.
    CachedFullHashResponse* response = cacheEntry.GetRawPtr();
    entry->expirySec = response->negativeCacheExpirySec;

    // Set positive cache.
    FullHashExpiryCache& fullHashes = response->fullHashes;
    for (const auto& fullHashEntry : fullHashes) {
      RefPtr<nsUrlClassifierPositiveCacheEntry> match =
          new nsUrlClassifierPositiveCacheEntry;

      // Set fullhash of positive cache entry.
      CStringToHexString(fullHashEntry.GetKey(), match->fullhash);

      // Set expiry of positive cache entry.
      match->expirySec = fullHashEntry.GetData();

      entry->matches.AppendElement(
          static_cast<nsIUrlClassifierPositiveCacheEntry*>(match));
    }

    info->entries.AppendElement(
        static_cast<nsIUrlClassifierCacheEntry*>(entry));
  }

  info.forget(aCache);
}

/* static */
bool LookupCache::IsCanonicalizedIP(const nsACString& aHost) {
  // The canonicalization process will have left IP addresses in dotted
  // decimal with no surprises.
  uint32_t i1, i2, i3, i4;
  char c;
  if (PR_sscanf(PromiseFlatCString(aHost).get(), "%u.%u.%u.%u%c", &i1, &i2, &i3,
                &i4, &c) == 4) {
    return (i1 <= 0xFF && i2 <= 0xFF && i3 <= 0xFF && i4 <= 0xFF);
  }

  return false;
}

// This is used when the URL is created by CreatePairwiseEntityListURI(),
// which returns an URI like "toplevel.page/?resource=third.party.domain"
// The fragment rule for the hostname(toplevel.page) is still the same
// as Safe Browsing protocol.
// The difference is that we always keep the path and query string and
// generate an additional fragment by removing the leading component of
// third.party.domain. This is to make sure we can find a match when a
// exceptionlisted domain is eTLD.
/* static */
void LookupCache::GetLookupEntitylistFragments(
    const nsACString& aSpec, nsTArray<nsCString>* aFragments) {
  aFragments->Clear();

  nsACString::const_iterator begin, end, iter, iter_end;
  aSpec.BeginReading(begin);
  aSpec.EndReading(end);

  iter = begin;
  iter_end = end;

  // Fallback to use default fragment rule when the URL doesn't contain
  // "/?resoruce=" because this means the URL is not generated in
  // CreatePairwiseEntityListURI()
  if (!FindInReadable("/?resource="_ns, iter, iter_end)) {
    GetLookupFragments(aSpec, aFragments);
    return;
  }

  const nsACString& topLevelURL = Substring(begin, iter++);
  const nsACString& thirdPartyURL = Substring(iter_end, end);

  /**
   * For the top-level URL, we follow the host fragment rule defined
   * in the Safe Browsing protocol.
   */
  nsTArray<nsCString> topLevelURLs;
  topLevelURLs.AppendElement(topLevelURL);

  if (!IsCanonicalizedIP(topLevelURL)) {
    topLevelURL.BeginReading(begin);
    topLevelURL.EndReading(end);
    int numTopLevelURLComponents = 0;
    while (RFindInReadable("."_ns, begin, end) &&
           numTopLevelURLComponents < MAX_HOST_COMPONENTS) {
      // don't bother checking toplevel domains
      if (++numTopLevelURLComponents >= 2) {
        topLevelURL.EndReading(iter);
        topLevelURLs.AppendElement(Substring(end, iter));
      }
      end = begin;
      topLevelURL.BeginReading(begin);
    }
  }

  /**
   * The whiltelisted domain in the entity list may be eTLD or eTLD+1.
   * Since the number of the domain name part in the third-party URL searching
   * is always less than or equal to eTLD+1, we remove the leading
   * component from the third-party domain to make sure we can find a match
   * if the exceptionlisted domain stoed in the entity list is eTLD.
   */
  nsTArray<nsCString> thirdPartyURLs;
  thirdPartyURLs.AppendElement(thirdPartyURL);

  if (!IsCanonicalizedIP(thirdPartyURL)) {
    thirdPartyURL.BeginReading(iter);
    thirdPartyURL.EndReading(end);
    if (FindCharInReadable('.', iter, end)) {
      iter++;
      nsAutoCString thirdPartyURLToAdd;
      thirdPartyURLToAdd.Assign(Substring(iter, end));

      // don't bother checking toplevel domains
      if (FindCharInReadable('.', iter, end)) {
        thirdPartyURLs.AppendElement(thirdPartyURLToAdd);
      }
    }
  }

  for (size_t i = 0; i < topLevelURLs.Length(); i++) {
    for (size_t j = 0; j < thirdPartyURLs.Length(); j++) {
      nsAutoCString key;
      key.Assign(topLevelURLs[i]);
      key.Append("/?resource=");
      key.Append(thirdPartyURLs[j]);

      aFragments->AppendElement(key);
    }
  }
}

/* static */
void LookupCache::GetLookupFragments(const nsACString& aSpec,
                                     nsTArray<nsCString>* aFragments)

{
  aFragments->Clear();

  nsACString::const_iterator begin, end, iter;
  aSpec.BeginReading(begin);
  aSpec.EndReading(end);

  iter = begin;
  if (!FindCharInReadable('/', iter, end)) {
    return;
  }

  const nsACString& host = Substring(begin, iter++);
  nsAutoCString path;
  path.Assign(Substring(iter, end));

  /**
   * From the protocol doc:
   * For the hostname, the client will try at most 5 different strings.  They
   * are:
   * a) The exact hostname of the url
   * b) The 4 hostnames formed by starting with the last 5 components and
   *    successivly removing the leading component.  The top-level component
   *    can be skipped. This is not done if the hostname is a numerical IP.
   */
  nsTArray<nsCString> hosts;
  hosts.AppendElement(host);

  if (!IsCanonicalizedIP(host)) {
    host.BeginReading(begin);
    host.EndReading(end);
    int numHostComponents = 0;
    while (RFindInReadable("."_ns, begin, end) &&
           numHostComponents < MAX_HOST_COMPONENTS) {
      // don't bother checking toplevel domains
      if (++numHostComponents >= 2) {
        host.EndReading(iter);
        hosts.AppendElement(Substring(end, iter));
      }
      end = begin;
      host.BeginReading(begin);
    }
  }

  /**
   * From the protocol doc:
   * For the path, the client will also try at most 6 different strings.
   * They are:
   * a) the exact path of the url, including query parameters
   * b) the exact path of the url, without query parameters
   * c) the 4 paths formed by starting at the root (/) and
   *    successively appending path components, including a trailing
   *    slash.  This behavior should only extend up to the next-to-last
   *    path component, that is, a trailing slash should never be
   *    appended that was not present in the original url.
   */
  nsTArray<nsCString> paths;
  nsAutoCString pathToAdd;

  path.BeginReading(begin);
  path.EndReading(end);
  iter = begin;
  if (FindCharInReadable('?', iter, end)) {
    pathToAdd = Substring(begin, iter);
    paths.AppendElement(pathToAdd);
    end = iter;
  }

  int numPathComponents = 1;
  iter = begin;
  while (FindCharInReadable('/', iter, end) &&
         numPathComponents < MAX_PATH_COMPONENTS) {
    iter++;
    pathToAdd.Assign(Substring(begin, iter));
    paths.AppendElement(pathToAdd);
    numPathComponents++;
  }

  // If we haven't already done so, add the full path
  if (!pathToAdd.Equals(path)) {
    paths.AppendElement(path);
  }
  // Check an empty path (for whole-domain blocklist entries)
  if (!paths.Contains(""_ns)) {
    paths.AppendElement(""_ns);
  }

  for (uint32_t hostIndex = 0; hostIndex < hosts.Length(); hostIndex++) {
    for (uint32_t pathIndex = 0; pathIndex < paths.Length(); pathIndex++) {
      nsCString key;
      key.Assign(hosts[hostIndex]);
      key.Append('/');
      key.Append(paths[pathIndex]);

      aFragments->AppendElement(key);
    }
  }
}

nsresult LookupCache::LoadPrefixSet() {
  nsCOMPtr<nsIFile> psFile;
  nsresult rv = mStoreDirectory->Clone(getter_AddRefs(psFile));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = psFile->AppendNative(mTableName + GetPrefixSetSuffix());
  NS_ENSURE_SUCCESS(rv, rv);

  bool exists;
  rv = psFile->Exists(&exists);
  NS_ENSURE_SUCCESS(rv, rv);

  if (exists) {
    LOG(("stored PrefixSet exists, loading from disk"));
    rv = LoadFromFile(psFile);
    if (NS_FAILED(rv)) {
      return rv;
    }
    mPrimed = true;
  } else {
    // The only scenario we load the old .pset file is when we haven't received
    // a SafeBrowsng update before. After receiving an update, new .vlpset will
    // be stored while old .pset will be removed.
    if (NS_SUCCEEDED(LoadLegacyFile())) {
      mPrimed = true;
    } else {
      LOG(("no (usable) stored PrefixSet found"));
    }
  }

#ifdef DEBUG
  if (mPrimed) {
    uint32_t size = SizeOfPrefixSet();
    LOG(("SB tree done, size = %d bytes\n", size));
  }
#endif

  return NS_OK;
}

size_t LookupCache::SizeOfPrefixSet() const {
  return mVLPrefixSet->SizeOfIncludingThis(moz_malloc_size_of);
}

#if defined(DEBUG)
void LookupCache::DumpCache() const {
  if (!LOG_ENABLED()) {
    return;
  }

  for (const auto& cacheEntry : mFullHashCache) {
    CachedFullHashResponse* response = cacheEntry.GetRawPtr();

    nsAutoCString prefix;
    CStringToHexString(
        nsCString(reinterpret_cast<const char*>(&cacheEntry.GetKey()),
                  PREFIX_SIZE),
        prefix);
    LOG(("Cache prefix(%s): %s, Expiry: %s", mTableName.get(), prefix.get(),
         GetFormattedTimeString(response->negativeCacheExpirySec).get()));

    FullHashExpiryCache& fullHashes = response->fullHashes;
    for (const auto& fullHashEntry : fullHashes) {
      nsAutoCString fullhash;
      CStringToHexString(fullHashEntry.GetKey(), fullhash);
      LOG(("  - %s, Expiry: %s", fullhash.get(),
           GetFormattedTimeString(fullHashEntry.GetData()).get()));
    }
  }
}
#endif

nsresult LookupCache::StoreToFile(nsCOMPtr<nsIFile>& aFile) {
  NS_ENSURE_ARG_POINTER(aFile);

  uint32_t fileSize = sizeof(Header) +
                      mVLPrefixSet->CalculatePreallocateSize() +
                      nsCrc32CheckSumedOutputStream::CHECKSUM_SIZE;

  nsCOMPtr<nsIOutputStream> localOutFile;
  nsresult rv =
      NS_NewSafeLocalFileOutputStream(getter_AddRefs(localOutFile), aFile,
                                      PR_WRONLY | PR_TRUNCATE | PR_CREATE_FILE);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // Preallocate the file storage
  {
    nsCOMPtr<nsIFileOutputStream> fos(do_QueryInterface(localOutFile));
    auto timer = glean::urlclassifier::vlps_fallocate_time.Measure();

    (void)fos->Preallocate(fileSize);
  }

  nsCOMPtr<nsIOutputStream> out;
  rv = NS_NewCrc32OutputStream(getter_AddRefs(out), localOutFile.forget(),
                               std::min(fileSize, MAX_BUFFER_SIZE));

  // Write header
  Header header;
  GetHeader(header);

  rv = WriteValue(out, header);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // Write prefixes
  rv = mVLPrefixSet->WritePrefixes(out);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // Write checksum
  nsCOMPtr<nsISafeOutputStream> safeOut = do_QueryInterface(out, &rv);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  rv = safeOut->Finish();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  LOG(("[%s] Storing PrefixSet successful", mTableName.get()));

  // This is to remove old ".pset" files if exist
  (void)ClearLegacyFile();
  return NS_OK;
}

nsresult LookupCache::LoadFromFile(nsCOMPtr<nsIFile>& aFile) {
  NS_ENSURE_ARG_POINTER(aFile);
  AUTO_PROFILER_MARKER_UNTYPED("LookupCache::LoadFromFile", OTHER,
                               MarkerTiming::IntervalStart());

  auto timer = glean::urlclassifier::vlps_fileload_time.Measure();

  nsCOMPtr<nsIInputStream> localInFile;
  nsresult rv = NS_NewLocalFileInputStream(getter_AddRefs(localInFile), aFile,
                                           PR_RDONLY | nsIFile::OS_READAHEAD);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // Calculate how big the file is, make sure our read buffer isn't bigger
  // than the file itself which is just wasting memory.
  int64_t fileSize;
  rv = aFile->GetFileSize(&fileSize);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (fileSize < 0 || fileSize > UINT32_MAX) {
    return NS_ERROR_FAILURE;
  }

  uint32_t bufferSize =
      std::min<uint32_t>(static_cast<uint32_t>(fileSize), MAX_BUFFER_SIZE);

  // Convert to buffered stream
  nsCOMPtr<nsIInputStream> in;
  rv = NS_NewBufferedInputStream(getter_AddRefs(in), localInFile.forget(),
                                 bufferSize);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // Load header
  Header header;
  rv = ReadValue(in, header);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    LOG(("Failed to read header for %s", mTableName.get()));
    return NS_ERROR_FILE_CORRUPTED;
  }

  rv = SanityCheck(header);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // Load data
  rv = mVLPrefixSet->LoadPrefixes(in);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (StaticPrefs::urlclassifier_delay_prefixes_crc32_check()) {
    // To improve the startup performance, we don't verify the CRC32 checksum
    // here. We will verify it in the next list update. So, we mark the
    // verification as needed.
    mNeedCRC32Verification = true;
  } else {
    // Load crc32 checksum and verify
    rv = VerifyCRC32(in);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
  }

  mPrimed = true;

  LOG(("[%s] Loading PrefixSet successful", mTableName.get()));
  return NS_OK;
}

bool LookupCache::MaybeVerifyCRC32() {
  // We only perform the CRC32 check if necessary. We set the flag after we load
  // prefix file from disk.
  if (!mNeedCRC32Verification) {
    return true;
  }

  // Clear the flag after we verify the CRC32 to stop unnecessary verification.
  mNeedCRC32Verification = false;

  // Prepare the buffer input stream for verifying CRC32.
  nsCOMPtr<nsIFile> psFile;
  nsresult rv = mStoreDirectory->Clone(getter_AddRefs(psFile));
  NS_ENSURE_SUCCESS(rv, false);

  rv = psFile->AppendNative(mTableName + GetPrefixSetSuffix());
  NS_ENSURE_SUCCESS(rv, false);

  nsCOMPtr<nsIInputStream> fileIn;
  rv = NS_NewLocalFileInputStream(getter_AddRefs(fileIn), psFile,
                                  PR_RDONLY | nsIFile::OS_READAHEAD);
  NS_ENSURE_SUCCESS(rv, false);

  int64_t fileSize;
  rv = psFile->GetFileSize(&fileSize);
  NS_ENSURE_SUCCESS(rv, false);

  uint32_t bufferSize =
      std::min<uint32_t>(static_cast<uint32_t>(fileSize), MAX_BUFFER_SIZE);

  nsCOMPtr<nsIInputStream> in;
  rv = NS_NewBufferedInputStream(getter_AddRefs(in), fileIn.forget(),
                                 bufferSize);
  NS_ENSURE_SUCCESS(rv, false);

  rv = VerifyCRC32(in);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return false;
  }

  return true;
}

// This function assumes CRC32 checksum is in the end of the input stream
nsresult LookupCache::VerifyCRC32(nsCOMPtr<nsIInputStream>& aIn) {
  nsCOMPtr<nsISeekableStream> seekIn = do_QueryInterface(aIn);
  nsresult rv = seekIn->Seek(nsISeekableStream::NS_SEEK_SET, 0);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  uint64_t len;
  rv = aIn->Available(&len);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  uint32_t calculateCrc32 = ~0;

  // We don't want to include the checksum itself
  len = len - nsCrc32CheckSumedOutputStream::CHECKSUM_SIZE;

  static const uint64_t STREAM_BUFFER_SIZE = 4096;
  char buffer[STREAM_BUFFER_SIZE];
  while (len) {
    uint32_t read;
    uint64_t readLimit = std::min<uint64_t>(STREAM_BUFFER_SIZE, len);

    rv = aIn->Read(buffer, readLimit, &read);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    calculateCrc32 = ComputeCrc32c(
        calculateCrc32, reinterpret_cast<const uint8_t*>(buffer), read);

    len -= read;
  }

  // Now read the CRC32
  uint32_t crc32;
  ReadValue(aIn, crc32);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (crc32 != calculateCrc32) {
    return NS_ERROR_FILE_CORRUPTED;
  }

  return NS_OK;
}

nsresult LookupCacheV2::Has(const Completion& aCompletion, bool* aHas,
                            uint32_t* aMatchLength, bool* aConfirmed) {
  *aHas = *aConfirmed = false;
  *aMatchLength = 0;

  uint32_t length = 0;
  nsDependentCSubstring fullhash;
  fullhash.Rebind((const char*)aCompletion.buf, COMPLETE_SIZE);

  uint32_t prefix = aCompletion.ToUint32();

  nsresult rv = mVLPrefixSet->Matches(prefix, fullhash, &length);
  NS_ENSURE_SUCCESS(rv, rv);

  if (length == 0) {
    return NS_OK;
  }

  MOZ_ASSERT(length == PREFIX_SIZE || length == COMPLETE_SIZE);

  *aHas = true;
  *aMatchLength = length;
  *aConfirmed = length == COMPLETE_SIZE;

  if (!(*aConfirmed)) {
    rv = CheckCache(aCompletion, aHas, aConfirmed);
  }

  return rv;
}

nsresult LookupCacheV2::Build(AddPrefixArray& aAddPrefixes,
                              AddCompleteArray& aAddCompletes) {
  nsresult rv = mVLPrefixSet->SetPrefixes(aAddPrefixes, aAddCompletes);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  mPrimed = true;

  return NS_OK;
}

nsresult LookupCacheV2::GetPrefixes(FallibleTArray<uint32_t>& aAddPrefixes) {
  if (!mPrimed) {
    // This can happen if its a new table, so no error.
    LOG(("GetPrefixes from empty LookupCache"));
    return NS_OK;
  }

  return mVLPrefixSet->GetFixedLengthPrefixes(&aAddPrefixes, nullptr);
}

nsresult LookupCacheV2::GetPrefixes(FallibleTArray<uint32_t>& aAddPrefixes,
                                    FallibleTArray<nsCString>& aAddCompletes) {
  if (!mPrimed) {
    // This can happen if its a new table, so no error.
    LOG(("GetHashes from empty LookupCache"));
    return NS_OK;
  }

  return mVLPrefixSet->GetFixedLengthPrefixes(&aAddPrefixes, &aAddCompletes);
}

nsresult LookupCacheV2::GetPrefixByIndex(uint32_t aIndex,
                                         uint32_t* aOutPrefix) const {
  NS_ENSURE_ARG_POINTER(aOutPrefix);

  return mVLPrefixSet->GetFixedLengthPrefixByIndex(aIndex, aOutPrefix);
}

void LookupCacheV2::AddGethashResultToCache(
    const AddCompleteArray& aAddCompletes, const MissPrefixArray& aMissPrefixes,
    int64_t aExpirySec) {
  static const int64_t CACHE_DURATION_SEC = 15 * 60;
  int64_t defaultExpirySec = PR_Now() / PR_USEC_PER_SEC + CACHE_DURATION_SEC;
  if (aExpirySec != 0) {
    defaultExpirySec = aExpirySec;
  }

  for (const AddComplete& add : aAddCompletes) {
    nsDependentCSubstring fullhash(
        reinterpret_cast<const char*>(add.CompleteHash().buf), COMPLETE_SIZE);

    CachedFullHashResponse* response =
        mFullHashCache.GetOrInsertNew(add.ToUint32());
    response->negativeCacheExpirySec = defaultExpirySec;

    FullHashExpiryCache& fullHashes = response->fullHashes;
    fullHashes.InsertOrUpdate(fullhash, defaultExpirySec);
  }

  for (const Prefix& prefix : aMissPrefixes) {
    CachedFullHashResponse* response =
        mFullHashCache.GetOrInsertNew(prefix.ToUint32());

    response->negativeCacheExpirySec = defaultExpirySec;
  }
}

void LookupCacheV2::GetHeader(Header& aHeader) {
  aHeader.magic = LookupCacheV2::VLPSET_MAGIC;
  aHeader.version = LookupCacheV2::VLPSET_VERSION;
}

nsresult LookupCacheV2::SanityCheck(const Header& aHeader) {
  if (aHeader.magic != LookupCacheV2::VLPSET_MAGIC) {
    return NS_ERROR_FILE_CORRUPTED;
  }

  if (aHeader.version != LookupCacheV2::VLPSET_VERSION) {
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

nsresult LookupCacheV2::LoadLegacyFile() {
  // Because mozilla Safe Browsing v2 server only includes completions
  // in the update, we can simplify this function by only loading .sbtore
  if (!mProvider.EqualsLiteral("mozilla")) {
    return NS_OK;
  }

  HashStore store(mTableName, mProvider, mRootStoreDirectory);

  // Support loading version 3 HashStore.
  nsresult rv = store.Open(3);
  NS_ENSURE_SUCCESS(rv, rv);

  if (store.AddChunks().Length() == 0 && store.SubChunks().Length() == 0) {
    // Return when file doesn't exist
    return NS_OK;
  }

  AddPrefixArray prefix;
  AddCompleteArray addComplete;

  rv = store.ReadCompletionsLegacyV3(addComplete);
  NS_ENSURE_SUCCESS(rv, rv);

  return Build(prefix, addComplete);
}

nsresult LookupCacheV2::ClearLegacyFile() {
  nsCOMPtr<nsIFile> file;
  nsresult rv = mStoreDirectory->Clone(getter_AddRefs(file));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  rv = file->AppendNative(mTableName + ".pset"_ns);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  bool exists;
  rv = file->Exists(&exists);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (exists) {
    rv = file->Remove(false);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    LOG(("[%s]Old PrefixSet is successfully removed!", mTableName.get()));
  }

  return NS_OK;
}

nsCString LookupCacheV2::GetPrefixSetSuffix() const { return ".vlpset"_ns; }

// Support creating built-in entries for phsihing, malware, unwanted, harmful,
// tracking/tracking exceptionlist and flash block tables.
//
nsresult LookupCacheV2::LoadMozEntries() {
  // We already have the entries, return
  if (!IsEmpty() || IsPrimed()) {
    return NS_OK;
  }

  nsTArray<nsLiteralCString> entries;

  if (mTableName.EqualsLiteral("moztest-phish-simple")) {
    // Entries for phishing table
    entries.AppendElement("itisatrap.org/firefox/its-a-trap.html"_ns);
  } else if (mTableName.EqualsLiteral("moztest-malware-simple")) {
    // Entries for malware table
    entries.AppendElement("itisatrap.org/firefox/its-an-attack.html"_ns);
  } else if (mTableName.EqualsLiteral("moztest-unwanted-simple")) {
    // Entries for unwanted table
    entries.AppendElement("itisatrap.org/firefox/unwanted.html"_ns);
  } else if (mTableName.EqualsLiteral("moztest-harmful-simple")) {
    // Entries for harmfule tables
    entries.AppendElement("itisatrap.org/firefox/harmful.html"_ns);
  } else if (mTableName.EqualsLiteral("moztest-track-simple")) {
    // Entries for tracking table
    entries.AppendElement("trackertest.org/"_ns);
    entries.AppendElement("itisatracker.org/"_ns);
  } else if (mTableName.EqualsLiteral("moztest-trackwhite-simple")) {
    // Entries for tracking entitylist table
    entries.AppendElement("itisatrap.org/?resource=itisatracker.org"_ns);
  } else if (mTableName.EqualsLiteral("moztest-block-simple")) {
    // Entries for flash block table
    entries.AppendElement("itisatrap.org/firefox/blocked.html"_ns);
  } else {
    MOZ_ASSERT_UNREACHABLE();
  }

  AddPrefixArray prefix;
  AddCompleteArray completes;
  for (const auto& entry : entries) {
    AddComplete add;
    if (NS_FAILED(add.complete.FromPlaintext(entry))) {
      continue;
    }
    if (!completes.AppendElement(add, fallible)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
  }

  return Build(prefix, completes);
}

}  // namespace safebrowsing
}  // namespace mozilla
