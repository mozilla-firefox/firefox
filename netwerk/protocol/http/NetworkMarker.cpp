/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "NetworkMarker.h"

#include "HttpBaseChannel.h"
#include "mozilla/ErrorNames.h"
#include "mozilla/Perfetto.h"
#include "nsHttpHandler.h"
#include "nsIChannelEventSink.h"
#include "nsIClassOfService.h"

namespace mozilla::net {
struct NetworkMarker : public BaseMarkerType<NetworkMarker> {
  static constexpr const char* Name = "Network";
  static constexpr bool UseSpecialFrontendLocation = true;
  static constexpr bool ETWStoreName = true;

  static void StreamJSONMarkerData(
      baseprofiler::SpliceableJSONWriter& aWriter, mozilla::TimeStamp aStart,
      mozilla::TimeStamp aEnd, int64_t aID, const ProfilerString8View& aURI,
      const ProfilerString8View& aRequestMethod, NetworkLoadType aType,
      int32_t aPri, int64_t aCount,
      nsICacheInfoChannel::CacheDisposition aCacheDisposition,
      bool aIsPrivateBrowsing, const net::TimingStruct& aTimings,
      const ProfilerString8View& aRedirectURI,
      const ProfilerString8View& aContentType, uint32_t aRedirectFlags,
      int64_t aRedirectChannelId, uint32_t aClassOfServiceFlags,
      bool aClassOfServiceIncremental, nsresult aRequestStatus,
      const mozilla::Maybe<mozilla::net::HttpVersion> aHttpVersion,
      mozilla::Maybe<uint32_t> aResponseStatus,
      const ProfilerString8View& aSecPurpose, bool aActivatedFromPrefetch) {
    // This payload still streams a startTime and endTime property because it
    // made the migration to MarkerTiming on the front-end easier.
    aWriter.TimeProperty("startTime", aStart);
    aWriter.TimeProperty("endTime", aEnd);

    aWriter.IntProperty("id", aID);
    aWriter.StringProperty("status", GetNetworkState(aType));

    // Bug 1919148 - Moved aClassOfServiceStr here to ensure that we call
    // aWriter.StringProperty before the lifetime of nsAutoCString ends
    nsAutoCString aClassOfServiceStr;
    GetClassOfService(aClassOfServiceStr, aClassOfServiceFlags);
    MOZ_ASSERT(aClassOfServiceStr.Length() > 0,
               "aClassOfServiceStr should be set after GetClassOfService");
    aWriter.StringProperty("classOfService",
                           MakeStringSpan(aClassOfServiceStr.get()));

    uint8_t urgency =
        nsHttpHandler::UrgencyFromCoSFlags(aClassOfServiceFlags, aPri);
    nsAutoCString priorityHeader;
    priorityHeader.AppendPrintf("u=%d", urgency);
    if (aClassOfServiceIncremental) {
      priorityHeader.Append(", i");
    }
    aWriter.StringProperty("priorityHeader",
                           MakeStringSpan(priorityHeader.get()));

    nsAutoCString aRequestStatusStr;
    GetErrorName(aRequestStatus, aRequestStatusStr);
    aWriter.StringProperty("requestStatus",
                           MakeStringSpan(aRequestStatusStr.get()));

    if (Span<const char> cacheString = GetCacheState(aCacheDisposition);
        !cacheString.IsEmpty()) {
      aWriter.StringProperty("cache", cacheString);
    }
    aWriter.IntProperty("pri", aPri);
    if (aCount > 0) {
      aWriter.IntProperty("count", aCount);
    }
    if (aURI.Length() != 0) {
      aWriter.StringProperty("URI", aURI);
    }
    if (aRedirectURI.Length() != 0) {
      aWriter.StringProperty("RedirectURI", aRedirectURI);
      aWriter.StringProperty("redirectType", getRedirectType(aRedirectFlags));
      aWriter.BoolProperty(
          "isHttpToHttpsRedirect",
          aRedirectFlags & nsIChannelEventSink::REDIRECT_STS_UPGRADE);

      if (aRedirectChannelId != 0) {
        aWriter.IntProperty("redirectId", aRedirectChannelId);
      }
    }

    aWriter.StringProperty("requestMethod", aRequestMethod);
    if (aHttpVersion) {
      const char* version = nullptr;
      // GetProtocolVersion's warning would re-enter the profiler's buffer lock.
      switch (*aHttpVersion) {
        case HttpVersion::v1_0:
        case HttpVersion::v1_1:
        case HttpVersion::v2_0:
        case HttpVersion::v3_0:
          version = nsHttp::GetProtocolVersion(*aHttpVersion);
          break;
        case HttpVersion::UNKNOWN:
        case HttpVersion::v0_9:
          version = "unknown";
          break;
      }
      MOZ_ASSERT(version, "Unexpected HttpVersion");
      if (version) {
        aWriter.StringProperty(
            "httpVersion",
            ProfilerString8View::WrapNullTerminatedString(version));
      }
    }
    if (aResponseStatus) {
      aWriter.IntProperty("responseStatus", *aResponseStatus);
    }

    if (aContentType.Length() != 0) {
      aWriter.StringProperty("contentType", aContentType);
    } else {
      aWriter.NullProperty("contentType");
    }

    if (aIsPrivateBrowsing) {
      aWriter.BoolProperty("isPrivateBrowsing", aIsPrivateBrowsing);
    }

    if (aSecPurpose.Length() != 0) {
      aWriter.StringProperty("secPurpose", aSecPurpose);
    }

    if (aActivatedFromPrefetch) {
      // Same vocabulary as PerformanceNavigationTiming's deliveryType.
      aWriter.StringProperty("deliveryType",
                             MakeStringSpan("navigational-prefetch"));
    }

    if (aType != NetworkLoadType::LOAD_START) {
      aWriter.TimeProperty("domainLookupStart", aTimings.domainLookupStart);
      aWriter.TimeProperty("domainLookupEnd", aTimings.domainLookupEnd);
      aWriter.TimeProperty("connectStart", aTimings.connectStart);
      aWriter.TimeProperty("tcpConnectEnd", aTimings.tcpConnectEnd);
      aWriter.TimeProperty("secureConnectionStart",
                           aTimings.secureConnectionStart);
      aWriter.TimeProperty("connectEnd", aTimings.connectEnd);
      aWriter.TimeProperty("requestStart", aTimings.requestStart);
      aWriter.TimeProperty("responseStart", aTimings.responseStart);
      aWriter.TimeProperty("responseEnd", aTimings.responseEnd);
    }
  }

  static Span<const char> GetNetworkState(NetworkLoadType aType) {
    switch (aType) {
      case NetworkLoadType::LOAD_START:
        return MakeStringSpan("STATUS_START");
      case NetworkLoadType::LOAD_STOP:
        return MakeStringSpan("STATUS_STOP");
      case NetworkLoadType::LOAD_REDIRECT:
        return MakeStringSpan("STATUS_REDIRECT");
      case NetworkLoadType::LOAD_CANCEL:
        return MakeStringSpan("STATUS_CANCEL");
      default:
        MOZ_ASSERT(false, "Unexpected NetworkLoadType enum value.");
        return MakeStringSpan("");
    }
  }

  static Span<const char> GetCacheState(
      nsICacheInfoChannel::CacheDisposition aCacheDisposition) {
    switch (aCacheDisposition) {
      case nsICacheInfoChannel::kCacheUnresolved:
        return MakeStringSpan("Unresolved");
      case nsICacheInfoChannel::kCacheHit:
        return MakeStringSpan("Hit");
      case nsICacheInfoChannel::kCacheHitViaReval:
        return MakeStringSpan("HitViaReval");
      case nsICacheInfoChannel::kCacheMissedViaReval:
        return MakeStringSpan("MissedViaReval");
      case nsICacheInfoChannel::kCacheMissed:
        return MakeStringSpan("Missed");
      case nsICacheInfoChannel::kCacheUnknown:
        return MakeStringSpan("");
      default:
        MOZ_ASSERT(false, "Unexpected CacheDisposition enum value.");
        return MakeStringSpan("");
    }
  }

  static Span<const char> getRedirectType(uint32_t aRedirectFlags) {
    MOZ_ASSERT(aRedirectFlags != 0, "aRedirectFlags should be non-zero");
    if (aRedirectFlags & nsIChannelEventSink::REDIRECT_TEMPORARY) {
      return MakeStringSpan("Temporary");
    }
    if (aRedirectFlags & nsIChannelEventSink::REDIRECT_PERMANENT) {
      return MakeStringSpan("Permanent");
    }
    if (aRedirectFlags & nsIChannelEventSink::REDIRECT_INTERNAL) {
      return MakeStringSpan("Internal");
    }
    MOZ_ASSERT(false, "Couldn't find a redirect type from aRedirectFlags");
    return MakeStringSpan("");
  }

  // Update an empty string aClassOfServiceStr based on aClassOfServiceFlag
  static void GetClassOfService(nsAutoCString& aClassOfServiceStr,
                                uint32_t aClassOfServiceFlag) {
    MOZ_ASSERT(aClassOfServiceStr.IsEmpty(),
               "Flags should not be appended to aClassOfServiceStr before "
               "calling GetClassOfService");

    if (aClassOfServiceFlag & nsIClassOfService::Leader) {
      aClassOfServiceStr.Append("Leader | ");
    }
    if (aClassOfServiceFlag & nsIClassOfService::Follower) {
      aClassOfServiceStr.Append("Follower | ");
    }
    if (aClassOfServiceFlag & nsIClassOfService::Speculative) {
      aClassOfServiceStr.Append("Speculative | ");
    }
    if (aClassOfServiceFlag & nsIClassOfService::Background) {
      aClassOfServiceStr.Append("Background | ");
    }
    if (aClassOfServiceFlag & nsIClassOfService::Unblocked) {
      aClassOfServiceStr.Append("Unblocked | ");
    }
    if (aClassOfServiceFlag & nsIClassOfService::Throttleable) {
      aClassOfServiceStr.Append("Throttleable | ");
    }
    if (aClassOfServiceFlag & nsIClassOfService::UrgentStart) {
      aClassOfServiceStr.Append("UrgentStart | ");
    }
    if (aClassOfServiceFlag & nsIClassOfService::DontThrottle) {
      aClassOfServiceStr.Append("DontThrottle | ");
    }
    if (aClassOfServiceFlag & nsIClassOfService::Tail) {
      aClassOfServiceStr.Append("Tail | ");
    }
    if (aClassOfServiceFlag & nsIClassOfService::TailAllowed) {
      aClassOfServiceStr.Append("TailAllowed | ");
    }
    if (aClassOfServiceFlag & nsIClassOfService::TailForbidden) {
      aClassOfServiceStr.Append("TailForbidden | ");
    }

    if (aClassOfServiceStr.IsEmpty()) {
      aClassOfServiceStr.Append("Unset");
      return;
    }

    MOZ_ASSERT(aClassOfServiceStr.Length() > 3,
               "aClassOfServiceStr must be at least 4 characters long to "
               "include two blank spaces and a '|' character.");
    // Remove the trailing '|'
    aClassOfServiceStr.Truncate(aClassOfServiceStr.Length() - 3);
  }
};
}  // namespace mozilla::net

#ifdef MOZ_PERFETTO
// Define a specialization for NetworkMarker since the payloads are
// not trivial to translate directly.
template <>
void EmitPerfettoTrackEvent<mozilla::net::NetworkMarker, mozilla::TimeStamp,
                            mozilla::TimeStamp, int64_t, nsAutoCStringN<2048>,
                            nsACString, mozilla::net::NetworkLoadType, int32_t,
                            int64_t, nsICacheInfoChannel::CacheDisposition,
                            bool, mozilla::net::TimingStruct, nsAutoCString,
                            mozilla::ProfilerString8View, uint32_t, uint64_t>(
    const mozilla::ProfilerString8View& aName,
    const mozilla::MarkerCategory& aCategory,
    const mozilla::MarkerOptions& aOptions,
    mozilla::net::NetworkMarker aMarkerType, const mozilla::TimeStamp& aStart,
    const mozilla::TimeStamp& aEnd, const int64_t& aID,
    const nsAutoCStringN<2048>& aURI, const nsACString& aRequestMethod,
    const mozilla::net::NetworkLoadType& aType, const int32_t& aPri,
    const int64_t& aCount,
    const nsICacheInfoChannel::CacheDisposition& aCacheDisposition,
    const bool& aIsPrivateBrowsing, const mozilla::net::TimingStruct& aTimings,
    const nsAutoCString& aRedirectURI,
    const mozilla::ProfilerString8View& aContentType,
    const uint32_t& aRedirectFlags, const uint64_t& aRedirectChannelId) {
  MOZ_ASSERT(!aOptions.IsTimingUnspecified(),
             "Timing should be properly defined.");
  auto nameSv = aName.StringView();
  const char* nameStr = nameSv.data();
  if (!nameStr) {
    return;
  }

  mozilla::TimeStamp startTime, endTime;
  startTime = aOptions.Timing().StartTime();
  endTime = aOptions.Timing().EndTime();

  perfetto::DynamicCategory category{"LOAD"};

  MOZ_ASSERT(
      aOptions.Timing().MarkerPhase() == mozilla::MarkerTiming::Phase::Interval,
      "Expecting an interval phase only.");

  // Create a unique id for each marker.
  mozilla::HashNumber hash =
      mozilla::HashString(nameStr, aName.StringView().length());
  hash = mozilla::AddToHash(hash,
                            startTime.RawClockMonotonicNanosecondsSinceBoot());
  hash =
      mozilla::AddToHash(hash, endTime.RawClockMonotonicNanosecondsSinceBoot());
  perfetto::Track track(hash);

  auto desc = track.Serialize();
  desc.set_name(nameStr);
  perfetto::TrackEvent::SetTrackDescriptor(track, desc);

  PERFETTO_TRACE_EVENT_BEGIN(
      category, (perfetto::DynamicString{nameStr, nameSv.length()}), track,
      startTime);
  PERFETTO_TRACE_EVENT_END(
      category, track, endTime, [&](perfetto::EventContext ctx) {
        auto* urlArg = ctx.event()->add_debug_annotations();
        urlArg->set_name("url");
        urlArg->set_string_value(aURI.get());

        auto* reqMethodArg = ctx.event()->add_debug_annotations();
        reqMethodArg->set_name("requestMethod");
        reqMethodArg->set_string_value(nsAutoCString(aRequestMethod).get());

        auto* statusArg = ctx.event()->add_debug_annotations();
        statusArg->set_name("status");
        statusArg->set_string_value(
            mozilla::net::NetworkMarker::GetNetworkState(aType).data());

        auto* cacheArg = ctx.event()->add_debug_annotations();
        cacheArg->set_name("cache");
        cacheArg->set_string_value(
            mozilla::net::NetworkMarker::GetCacheState(aCacheDisposition)
                .data());

        if (aContentType.Length() != 0) {
          auto* contentTypeArg = ctx.event()->add_debug_annotations();
          contentTypeArg->set_name("contentType");
          contentTypeArg->set_string_value(aContentType.StringView().data());
        }

        auto* priorityArg = ctx.event()->add_debug_annotations();
        priorityArg->set_name("priority");
        priorityArg->set_int_value(aPri);

        if (aCount > 0) {
          auto* countArg = ctx.event()->add_debug_annotations();
          countArg->set_name("count");
          countArg->set_int_value(aCount);
        }

        if (aRedirectURI.Length() != 0) {
          auto* redirectURIArg = ctx.event()->add_debug_annotations();
          redirectURIArg->set_name("RedirectURI");
          redirectURIArg->set_string_value(aRedirectURI.get());

          auto* redirectTypeArg = ctx.event()->add_debug_annotations();
          redirectTypeArg->set_name("redirectType");
          redirectTypeArg->set_string_value(
              mozilla::net::NetworkMarker::getRedirectType(aRedirectFlags)
                  .data());

          auto* httpToHttpsArg = ctx.event()->add_debug_annotations();
          httpToHttpsArg->set_name("isHttpToHttpsRedirect");
          httpToHttpsArg->set_bool_value(
              aRedirectFlags & nsIChannelEventSink::REDIRECT_STS_UPGRADE);

          if (aRedirectChannelId != 0) {
            auto* redirectIdArg = ctx.event()->add_debug_annotations();
            redirectIdArg->set_name("redirectId");
            redirectIdArg->set_int_value(aRedirectChannelId);
          }
        }

        if (aIsPrivateBrowsing) {
          auto* privateBrowsingArg = ctx.event()->add_debug_annotations();
          privateBrowsingArg->set_name("isPrivateBrowsing");
          privateBrowsingArg->set_bool_value(aIsPrivateBrowsing);
        }

        if (aType != mozilla::net::NetworkLoadType::LOAD_START) {
          mozilla::TimeStamp startTime;
          auto addNetworkTimingAnnotation =
              [&startTime, &ctx, &aStart](const mozilla::TimeStamp& endTime,
                                          const char* name) {
                if (endTime) {
                  // If startTime is not defined, redefine the name of this to
                  // "Waiting for Socket Thread".
                  if (!startTime) {
                    name = "Waiting for Socket Thread (us)";
                    startTime = aStart;
                  }
                  mozilla::TimeDuration duration = endTime - startTime;
                  auto* arg = ctx.event()->add_debug_annotations();
                  arg->set_name(name);
                  arg->set_int_value(duration.ToMilliseconds());
                  startTime = endTime;
                }
              };

          addNetworkTimingAnnotation(aTimings.domainLookupStart,
                                     "Waiting for Socket Thread");
          addNetworkTimingAnnotation(aTimings.domainLookupEnd, "DNS Request");
          addNetworkTimingAnnotation(aTimings.connectStart,
                                     "After DNS Request");
          addNetworkTimingAnnotation(aTimings.tcpConnectEnd, "TCP connection");
          addNetworkTimingAnnotation(aTimings.secureConnectionStart,
                                     "After TCP connection");
          addNetworkTimingAnnotation(aTimings.connectEnd,
                                     "Establishing TLS session");
          addNetworkTimingAnnotation(aTimings.requestStart,
                                     "Waiting for HTTP request");
          addNetworkTimingAnnotation(aTimings.responseStart,
                                     "HTTP request and waiting for response");
          addNetworkTimingAnnotation(aTimings.responseEnd, "HTTP response");
          addNetworkTimingAnnotation(aEnd, "Waiting to transmit the response");
        }
      });
}
#endif  // MOZ_PERFETTO

namespace mozilla::net {
static constexpr net::TimingStruct scEmptyNetTimingStruct;

void profiler_add_network_marker(
    nsIURI* aURI, const nsACString& aRequestMethod, int32_t aPriority,
    uint64_t aChannelId, NetworkLoadType aType, mozilla::TimeStamp aStart,
    mozilla::TimeStamp aEnd, int64_t aCount,
    nsICacheInfoChannel::CacheDisposition aCacheDisposition,
    uint64_t aInnerWindowID, bool aIsPrivateBrowsing,
    nsIClassOfService* aClassOfService, nsresult aRequestStatus,
    const nsACString& aSecPurpose, bool aActivatedFromPrefetch,
    const mozilla::net::TimingStruct* aTimings,
    UniquePtr<ProfileChunkedBuffer> aSource,
    const Maybe<mozilla::net::HttpVersion> aHttpVersion,
    const Maybe<uint32_t> aResponseStatus,
    const Maybe<nsDependentCString>& aContentType, nsIURI* aRedirectURI,
    uint32_t aRedirectFlags, uint64_t aRedirectChannelId) {
  if (!profiler_thread_is_being_profiled_for_markers()) {
    return;
  }

  nsAutoCStringN<2048> name;
  name.AppendASCII("Load ");
  // top 32 bits are process id of the load
  name.AppendInt(aChannelId & 0xFFFFFFFFu);

  // These can do allocations/frees/etc; avoid if not active
  nsAutoCStringN<2048> spec;
  if (aURI) {
    aURI->GetAsciiSpec(spec);
    name.AppendASCII(": ");
    name.Append(spec);
  }

  nsAutoCString redirect_spec;
  if (aRedirectURI) {
    aRedirectURI->GetAsciiSpec(redirect_spec);
  }

  uint32_t classOfServiceFlags = 0;
  bool classOfServiceIncremental = false;
  if (aClassOfService) {
    aClassOfService->GetClassFlags(&classOfServiceFlags);
    aClassOfService->GetIncremental(&classOfServiceIncremental);
  }

  profiler_add_marker(
      name, geckoprofiler::category::NETWORK,
      {MarkerTiming::Interval(aStart, aEnd),
       MarkerStack::TakeBacktrace(std::move(aSource)),
       MarkerInnerWindowId(aInnerWindowID)},
      NetworkMarker{}, aStart, aEnd, static_cast<int64_t>(aChannelId), spec,
      aRequestMethod, aType, aPriority, aCount, aCacheDisposition,
      aIsPrivateBrowsing, aTimings ? *aTimings : scEmptyNetTimingStruct,
      redirect_spec,
      aContentType ? ProfilerString8View(*aContentType) : ProfilerString8View(),
      aRedirectFlags, aRedirectChannelId, classOfServiceFlags,
      classOfServiceIncremental, aRequestStatus, aHttpVersion, aResponseStatus,
      aSecPurpose, aActivatedFromPrefetch);
}
}  // namespace mozilla::net
