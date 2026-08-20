/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef PageInformation_h
#define PageInformation_h

#include "mozilla/Maybe.h"
#include "mozilla/MemoryReporting.h"
#include "nsISupportsImpl.h"
#include "nsString.h"

namespace mozilla {
namespace baseprofiler {
class SpliceableJSONWriter;
}  // namespace baseprofiler
}  // namespace mozilla

// This class contains information that's relevant to a single page only
// while the page information is important and registered with the profiler,
// but regardless of whether the profiler is running. All accesses to it are
// protected by the profiler state lock.
// When the page gets unregistered, we keep the profiler buffer position
// to determine if we are still using this page. If not, we unregister
// it in the next page registration.
class PageInformation final {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(PageInformation)
  PageInformation(uint64_t aTabID, uint64_t aInnerWindowID,
                  const nsCString& aUrl, uint64_t aEmbedderInnerWindowID,
                  bool aIsPrivateBrowsing);

  size_t SizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf) const;
  void StreamJSON(mozilla::baseprofiler::SpliceableJSONWriter& aWriter) const;

  uint64_t InnerWindowID() const { return mInnerWindowID; }
  uint64_t TabID() const { return mTabID; }
  const nsCString& Url() const { return mUrl; }
  uint64_t EmbedderInnerWindowID() const { return mEmbedderInnerWindowID; }
  bool IsPrivateBrowsing() const { return mIsPrivateBrowsing; }

  mozilla::Maybe<uint64_t> BufferPositionWhenUnregistered() const {
    return mBufferPositionWhenUnregistered;
  }

  void NotifyUnregistered(uint64_t aBufferPosition) {
    mBufferPositionWhenUnregistered = mozilla::Some(aBufferPosition);
  }

 private:
  const uint64_t mTabID;
  const uint64_t mInnerWindowID;
  const nsCString mUrl;
  const uint64_t mEmbedderInnerWindowID;
  const bool mIsPrivateBrowsing;

  // Holds the buffer position when page is unregistered.
  // It's used to determine if we still use this page in the profiler or
  // not.
  mozilla::Maybe<uint64_t> mBufferPositionWhenUnregistered;

  virtual ~PageInformation() = default;
};

#endif  // PageInformation_h
