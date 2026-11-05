/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_OpaqueResponseUtils_h
#define mozilla_net_OpaqueResponseUtils_h

#include "ipc/EnumSerializer.h"
#include "mozilla/Logging.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/glean/NetwerkProtocolHttpMetrics.h"
#include "nsCOMPtr.h"
#include "nsIContentPolicy.h"
#include "nsIEncodedChannel.h"
#include "nsIHttpChannel.h"
#include "nsIStreamListener.h"
#include "nsMimeTypes.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nsUnknownDecoder.h"

class nsIContentSniffer;

namespace mozilla::dom {
class JSValidatorParent;
}

namespace mozilla::ipc {
class Shmem;
}

namespace mozilla::net {

class HttpBaseChannel;
class nsHttpResponseHead;

enum class OpaqueResponseBlockedReason : uint32_t {
  ALLOWED_SAFE_LISTED,
  ALLOWED_SAFE_LISTED_SPEC_BREAKING,
  BLOCKED_BLOCKLISTED_NEVER_SNIFFED,
  BLOCKED_206_AND_BLOCKLISTED,
  BLOCKED_NOSNIFF_AND_EITHER_BLOCKLISTED_OR_TEXTPLAIN,
  BLOCKED_SHOULD_SNIFF
};

using OpaqueResponseBlockedTelemetryReason = glean::orb::BlockReasonLabel;

enum class OpaqueResponse { Block, Allow, SniffCompressed, Sniff };

OpaqueResponseBlockedReason GetOpaqueResponseBlockedReason(
    const nsACString& aContentType, uint16_t aStatus, bool aNoSniff);

OpaqueResponseBlockedReason GetOpaqueResponseBlockedReason(
    nsHttpResponseHead& aResponseHead);

// Returns a tuple of (rangeStart, rangeEnd, rangeTotal) from the input range
// header string if succeed.
Result<std::tuple<int64_t, int64_t, int64_t>, nsresult>
ParseContentRangeHeaderString(const nsAutoCString& aRangeStr);

bool IsFirstPartialResponse(nsHttpResponseHead& aResponseHead);

LogModule* GetORBLog();

// Helper class to filter data for opaque responses destined for `Window.fetch`.
// See https://fetch.spec.whatwg.org/#concept-filtered-response-opaque.
class OpaqueResponseFilter final : public nsIStreamListener {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_NSISTREAMLISTENER;

  explicit OpaqueResponseFilter(nsIStreamListener* aNext);

 private:
  virtual ~OpaqueResponseFilter() = default;

  nsCOMPtr<nsIStreamListener> mNext;
};

class OpaqueResponseBlocker final : public nsIStreamListener {
  enum class State { Sniffing, Allowed, Blocked };

 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_NSISTREAMLISTENER;

  OpaqueResponseBlocker(nsIStreamListener* aNext, HttpBaseChannel* aChannel,
                        const nsCString& aContentType, bool aNoSniff);

  bool IsSniffing() const;
  void AllowResponse();
  void BlockResponse(HttpBaseChannel* aChannel, nsresult aStatus);
  void FilterResponse();

  nsresult EnsureOpaqueResponseIsAllowedAfterSniff(nsIRequest* aRequest);

  OpaqueResponse EnsureOpaqueResponseIsAllowedAfterJavaScriptValidation(
      HttpBaseChannel* aChannel, bool aAllow);

  // The four possible results for validation. `JavaScript` and `JSON` are
  // self-explanatory. `JavaScript` is the only successful result, in the sense
  // that it will allow the opaque response, whereas `JSON` will block. `Other`
  // is the case where validation fails, because the response is neither
  // `JavaScript` nor `JSON`, but the framework itself works as intended.
  // `Failure` implies that something has gone wrong, such as allocation, etc.
  enum class ValidatorResult : uint32_t { JavaScript, JSON, Other, Failure };

 private:
  virtual ~OpaqueResponseBlocker() = default;

  nsresult ValidateJavaScript(HttpBaseChannel* aChannel, nsIURI* aURI,
                              nsILoadInfo* aLoadInfo);

  void ResolveAndProcessData(HttpBaseChannel* aChannel, bool aAllowed,
                             Maybe<mozilla::ipc::Shmem>& aSharedData);

  void MaybeRunOnStopRequest(HttpBaseChannel* aChannel);

  nsCOMPtr<nsIStreamListener> mNext;

  const nsCString mContentType;
  const bool mNoSniff;
  bool mShouldFilter = false;

  State mState = State::Sniffing;
  nsresult mStatus = NS_OK;

  TimeStamp mStartOfJavaScriptValidation;

  RefPtr<dom::JSValidatorParent> mJSValidator;

  Maybe<nsresult> mPendingOnStopRequestStatus{Nothing()};
};

class nsCompressedAudioVideoImageDetector : public nsUnknownDecoder {
  const std::function<void(void*, const uint8_t*, uint32_t)> mCallback;

 public:
  nsCompressedAudioVideoImageDetector(
      nsIStreamListener* aListener,
      std::function<void(void*, const uint8_t*, uint32_t)>&& aCallback)
      : nsUnknownDecoder(aListener), mCallback(std::move(aCallback)) {}

 protected:
  virtual void DetermineContentType(nsIRequest* aRequest) override {
    nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aRequest);
    if (!httpChannel) {
      return;
    }

    const char* testData = mBuffer;
    uint32_t testDataLen = mBufferLen;
    // Check if data are compressed.
    nsAutoCString decodedData;

    // ConvertEncodedData is always called only on a single thread for each
    // instance of an object.
    nsresult rv = ConvertEncodedData(aRequest, mBuffer, mBufferLen);
    if (NS_SUCCEEDED(rv)) {
      MutexAutoLock lock(mMutex);
      decodedData = mDecodedData;
    }
    if (!decodedData.IsEmpty()) {
      testData = decodedData.get();
      testDataLen = std::min<uint32_t>(decodedData.Length(), 512u);
    }

    mCallback(httpChannel, (const uint8_t*)testData, testDataLen);

    nsAutoCString contentType;
    httpChannel->GetContentType(contentType);

    MutexAutoLock lock(mMutex);
    if (!contentType.IsEmpty()) {
      mContentType = contentType;
    } else {
      mContentType = UNKNOWN_CONTENT_TYPE;
    }

    nsCOMPtr<nsIEncodedChannel> encodedChannel = do_QueryInterface(httpChannel);
    if (encodedChannel) {
      encodedChannel->SetHasContentDecompressed(true);
    }
  }
};
}  // namespace mozilla::net

namespace IPC {
template <>
struct ParamTraits<mozilla::net::OpaqueResponseBlocker::ValidatorResult>
    : public ContiguousEnumSerializerInclusive<
          mozilla::net::OpaqueResponseBlocker::ValidatorResult,
          mozilla::net::OpaqueResponseBlocker::ValidatorResult::JavaScript,
          mozilla::net::OpaqueResponseBlocker::ValidatorResult::Failure> {};
}  // namespace IPC

#endif  // mozilla_net_OpaqueResponseUtils_h
