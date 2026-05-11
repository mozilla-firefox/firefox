/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MemoryBlobImpl.h"

#include "mozilla/IntegerPrintfMacros.h"
#include "mozilla/SHA1.h"
#include "mozilla/ipc/InputStreamUtils.h"
#include "nsIMemoryReporter.h"
#include "nsPrintfCString.h"
#include "nsRFPService.h"
#include "nsStringStream.h"
#include "prtime.h"

namespace mozilla::dom {

// static
already_AddRefed<MemoryBlobImpl> MemoryBlobImpl::CreateWithCustomLastModified(
    void* aMemoryBuffer, uint64_t aLength, const nsAString& aName,
    const nsAString& aContentType, int64_t aLastModifiedDate) {
  RefPtr<MemoryBlobImpl> blobImpl = new MemoryBlobImpl(
      aMemoryBuffer, aLength, aName, aContentType, aLastModifiedDate);
  return blobImpl.forget();
}

// static
already_AddRefed<MemoryBlobImpl> MemoryBlobImpl::CreateWithLastModifiedNow(
    void* aMemoryBuffer, uint64_t aLength, const nsAString& aName,
    const nsAString& aContentType, RTPCallerType aRTPCallerType) {
  int64_t lastModificationDate =
      nsRFPService::ReduceTimePrecisionAsUSecs(PR_Now(), 0, aRTPCallerType);
  return CreateWithCustomLastModified(aMemoryBuffer, aLength, aName,
                                      aContentType, lastModificationDate);
}

nsresult MemoryBlobImpl::DataOwnerAdapter::Create(DataOwner* aDataOwner,
                                                  size_t aStart, size_t aLength,
                                                  nsIInputStream** _retval) {
  MOZ_ASSERT(aDataOwner, "Uh ...");
  Span data{static_cast<const char*>(aDataOwner->mData) + aStart, aLength};
  RefPtr adapter = new MemoryBlobImpl::DataOwnerAdapter(aDataOwner, data);
  return NS_NewByteInputStream(_retval, adapter);
}

already_AddRefed<BlobImpl> MemoryBlobImpl::CreateSlice(
    uint64_t aStart, uint64_t aLength, const nsAString& aContentType,
    ErrorResult& aRv) const {
  RefPtr<BlobImpl> impl =
      new MemoryBlobImpl(this, aStart, aLength, aContentType);
  return impl.forget();
}

void MemoryBlobImpl::CreateInputStream(nsIInputStream** aStream,
                                       ErrorResult& aRv) const {
  if (mLength >= INT32_MAX) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  aRv = MemoryBlobImpl::DataOwnerAdapter::Create(mDataOwner, mStart, mLength,
                                                 aStream);
}

/* static */
StaticMutex MemoryBlobImpl::DataOwner::sDataOwnerMutex;

/* static */ StaticAutoPtr<LinkedList<MemoryBlobImpl::DataOwner>>
    MemoryBlobImpl::DataOwner::sDataOwners;

/* static */
bool MemoryBlobImpl::DataOwner::sMemoryReporterRegistered = false;

MOZ_DEFINE_MALLOC_SIZE_OF(MemoryFileDataOwnerMallocSizeOf)

class MemoryBlobImplDataOwnerMemoryReporter final : public nsIMemoryReporter {
  ~MemoryBlobImplDataOwnerMemoryReporter() = default;

 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  NS_IMETHOD CollectReports(nsIHandleReportCallback* aHandleReport,
                            nsISupports* aData, bool aAnonymize) override {
    using DataOwner = MemoryBlobImpl::DataOwner;

    StaticMutexAutoLock lock(DataOwner::sDataOwnerMutex);

    if (!DataOwner::sDataOwners) {
      return NS_OK;
    }

    const size_t LARGE_OBJECT_MIN_SIZE = 8 * 1024;
    size_t smallObjectsTotal = 0;

    for (DataOwner* owner = DataOwner::sDataOwners->getFirst(); owner;
         owner = owner->getNext()) {
      size_t size = MemoryFileDataOwnerMallocSizeOf(owner->mData);

      if (size < LARGE_OBJECT_MIN_SIZE) {
        smallObjectsTotal += size;
      } else {
        SHA1Sum sha1;
        sha1.update(owner->mData, owner->mLength);
        uint8_t digest[SHA1Sum::kHashSize];  // SHA1 digests are 20 bytes long.
        sha1.finish(digest);

        nsAutoCString digestString;
        for (unsigned char i : digest) {
          digestString.AppendPrintf("%02x", i);
        }

        aHandleReport->Callback(
            /* process */ ""_ns,
            nsPrintfCString(
                "explicit/dom/memory-file-data/large/file(length=%" PRIu64
                ", sha1=%s)",
                owner->mLength,
                aAnonymize ? "<anonymized>" : digestString.get()),
            KIND_HEAP, UNITS_BYTES, size,
            nsPrintfCString(
                "Memory used to back a memory file of length %" PRIu64
                " bytes.  The file "
                "has a sha1 of %s.\n\n"
                "Note that the allocator may round up a memory file's length "
                "-- "
                "that is, an N-byte memory file may take up more than N bytes "
                "of "
                "memory.",
                owner->mLength, digestString.get()),
            aData);
      }
    }

    if (smallObjectsTotal > 0) {
      aHandleReport->Callback(
          /* process */ ""_ns, "explicit/dom/memory-file-data/small"_ns,
          KIND_HEAP, UNITS_BYTES, smallObjectsTotal,
          nsPrintfCString(
              "Memory used to back small memory files (i.e. those taking up "
              "less "
              "than %zu bytes of memory each).\n\n"
              "Note that the allocator may round up a memory file's length -- "
              "that is, an N-byte memory file may take up more than N bytes of "
              "memory.",
              LARGE_OBJECT_MIN_SIZE),
          aData);
    }

    return NS_OK;
  }
};

NS_IMPL_ISUPPORTS(MemoryBlobImplDataOwnerMemoryReporter, nsIMemoryReporter)

/* static */
void MemoryBlobImpl::DataOwner::EnsureMemoryReporterRegistered() {
  sDataOwnerMutex.AssertCurrentThreadOwns();
  if (sMemoryReporterRegistered) {
    return;
  }

  RegisterStrongMemoryReporter(
      MakeAndAddRef<MemoryBlobImplDataOwnerMemoryReporter>());

  sMemoryReporterRegistered = true;
}

}  // namespace mozilla::dom
