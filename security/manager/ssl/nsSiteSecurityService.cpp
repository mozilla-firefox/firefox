/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsSiteSecurityService.h"

#include "PublicKeyPinningService.h"
#include "mozilla/Assertions.h"
#include "mozilla/Base64.h"
#include "mozilla/LinkedList.h"
#include "mozilla/Logging.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/StaticPrefs_test.h"
#include "mozilla/Tokenizer.h"
#include "mozilla/dom/PContent.h"
#include "mozilla/dom/ToJSValue.h"
#include "nsCOMArray.h"
#include "nsIScriptSecurityManager.h"
#include "nsISocketProvider.h"
#include "nsIURI.h"
#include "nsNSSComponent.h"
#include "nsNetUtil.h"
#include "nsPromiseFlatString.h"
#include "nsReadableUtils.h"
#include "nsSecurityHeaderParser.h"
#include "nsURLHelper.h"
#include "nsVariant.h"
#include "nsXULAppAPI.h"
#include "prnetdb.h"

// A note about the preload list:
// When a site specifically disables HSTS by sending a header with
// 'max-age: 0', we keep a "knockout" value that means "we have no information
// regarding the HSTS state of this host" (any ancestor of "this host" can still
// influence its HSTS status via include subdomains, however).
// This prevents the preload list from overriding the site's current
// desired HSTS status.
#include "nsSTSPreloadListGenerated.inc"

using namespace mozilla;
using namespace mozilla::psm;

static LazyLogModule gSSSLog("nsSSService");

#define SSSLOG(args) MOZ_LOG(gSSSLog, mozilla::LogLevel::Debug, args)

static const nsLiteralCString kHSTSKeySuffix = ":HSTS"_ns;

////////////////////////////////////////////////////////////////////////////////

namespace {

class SSSTokenizer final : public Tokenizer {
 public:
  explicit SSSTokenizer(const nsACString& source) : Tokenizer(source) {}

  [[nodiscard]] bool ReadBool(/*out*/ bool& value) {
    uint8_t rawValue;
    if (!ReadInteger(&rawValue)) {
      return false;
    }

    if (rawValue != 0 && rawValue != 1) {
      return false;
    }

    value = (rawValue == 1);
    return true;
  }

  [[nodiscard]] bool ReadState(/*out*/ SecurityPropertyState& state) {
    uint32_t rawValue;
    if (!ReadInteger(&rawValue)) {
      return false;
    }

    state = static_cast<SecurityPropertyState>(rawValue);
    switch (state) {
      case SecurityPropertyKnockout:
      case SecurityPropertySet:
      case SecurityPropertyUnset:
        break;
      default:
        return false;
    }

    return true;
  }
};

// Parses a state string like "1500918564034,1,1" into its constituent parts.
bool ParseHSTSState(const nsCString& stateString,
                    /*out*/ PRTime& expireTime,
                    /*out*/ SecurityPropertyState& state,
                    /*out*/ bool& includeSubdomains) {
  SSSTokenizer tokenizer(stateString);
  SSSLOG(("Parsing state from %s", stateString.get()));

  if (!tokenizer.ReadInteger(&expireTime)) {
    return false;
  }

  if (!tokenizer.CheckChar(',')) {
    return false;
  }

  if (!tokenizer.ReadState(state)) {
    return false;
  }

  if (!tokenizer.CheckChar(',')) {
    return false;
  }

  if (!tokenizer.ReadBool(includeSubdomains)) {
    return false;
  }

  if (tokenizer.CheckChar(',')) {
    // Read now-unused "source" field.
    uint32_t unused;
    if (!tokenizer.ReadInteger(&unused)) {
      return false;
    }
  }

  return tokenizer.CheckEOF();
}

}  // namespace

SiteHSTSState::SiteHSTSState(const nsCString& aHost,
                             const OriginAttributes& aOriginAttributes,
                             const nsCString& aStateString)
    : mHostname(aHost),
      mOriginAttributes(aOriginAttributes),
      mHSTSExpireTime(0),
      mHSTSState(SecurityPropertyUnset),
      mHSTSIncludeSubdomains(false) {
  bool valid = ParseHSTSState(aStateString, mHSTSExpireTime, mHSTSState,
                              mHSTSIncludeSubdomains);
  if (!valid) {
    SSSLOG(("%s is not a valid SiteHSTSState", aStateString.get()));
    mHSTSExpireTime = 0;
    mHSTSState = SecurityPropertyUnset;
    mHSTSIncludeSubdomains = false;
  }
}

SiteHSTSState::SiteHSTSState(const nsCString& aHost,
                             const OriginAttributes& aOriginAttributes,
                             PRTime aHSTSExpireTime,
                             SecurityPropertyState aHSTSState,
                             bool aHSTSIncludeSubdomains)

    : mHostname(aHost),
      mOriginAttributes(aOriginAttributes),
      mHSTSExpireTime(aHSTSExpireTime),
      mHSTSState(aHSTSState),
      mHSTSIncludeSubdomains(aHSTSIncludeSubdomains) {}

void SiteHSTSState::ToString(nsCString& aString) {
  aString.Truncate();
  aString.AppendInt(mHSTSExpireTime);
  aString.Append(',');
  aString.AppendInt(mHSTSState);
  aString.Append(',');
  aString.AppendInt(static_cast<uint32_t>(mHSTSIncludeSubdomains));
}

nsSiteSecurityService::nsSiteSecurityService() : mDafsa(kDafsa) {}

nsSiteSecurityService::~nsSiteSecurityService() = default;

NS_IMPL_ISUPPORTS(nsSiteSecurityService, nsISiteSecurityService)

nsresult nsSiteSecurityService::Init() {
  nsCOMPtr<nsIDataStorageManager> dataStorageManager(
      do_GetService("@mozilla.org/security/datastoragemanager;1"));
  if (!dataStorageManager) {
    return NS_ERROR_FAILURE;
  }
  nsresult rv =
      dataStorageManager->Get(nsIDataStorageManager::SiteSecurityServiceState,
                              getter_AddRefs(mSiteStateStorage));
  if (NS_FAILED(rv)) {
    return rv;
  }
  if (!mSiteStateStorage) {
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

nsresult nsSiteSecurityService::GetHost(nsIURI* aURI, nsACString& aResult) {
  nsCOMPtr<nsIURI> innerURI = NS_GetInnermostURI(aURI);
  if (!innerURI) {
    return NS_ERROR_FAILURE;
  }

  nsAutoCString host;
  nsresult rv = innerURI->GetAsciiHost(host);
  if (NS_FAILED(rv)) {
    return rv;
  }

  aResult.Assign(PublicKeyPinningService::CanonicalizeHostname(host.get()));
  if (aResult.IsEmpty()) {
    return NS_ERROR_UNEXPECTED;
  }

  return NS_OK;
}

static void NormalizePartitionKey(nsString& partitionKey) {
  // If present, the partitionKey will be of the form
  // "(<scheme>,<domain>[,port>])" (where "<scheme>" will be "https" or "http"
  // and "<port>", if present, will be a port number). This normalizes the
  // scheme to "https" and strips the port so that a domain noted as HSTS will
  // be HSTS regardless of scheme and port, as per the RFC.
  Tokenizer16 tokenizer(partitionKey, nullptr, u".-_");
  if (!tokenizer.CheckChar(u'(')) {
    return;
  }
  nsString scheme;
  if (!(tokenizer.ReadWord(scheme))) {
    return;
  }
  if (!tokenizer.CheckChar(u',')) {
    return;
  }
  nsString host;
  if (!tokenizer.ReadWord(host)) {
    return;
  }
  partitionKey.Assign(u"(https,");
  partitionKey.Append(host);
  partitionKey.Append(u")");
}

// Uses the previous format of storage key. Only to be used for migrating old
// entries.
static void GetOldStorageKey(const nsACString& hostname,
                             const OriginAttributes& aOriginAttributes,
                             /*out*/ nsAutoCString& storageKey) {
  storageKey = hostname;

  // Don't isolate by userContextId.
  OriginAttributes originAttributesNoUserContext = aOriginAttributes;
  originAttributesNoUserContext.mUserContextId =
      nsIScriptSecurityManager::DEFAULT_USER_CONTEXT_ID;
  nsAutoCString originAttributesSuffix;
  originAttributesNoUserContext.CreateSuffix(originAttributesSuffix);
  storageKey.Append(originAttributesSuffix);
  storageKey.Append(kHSTSKeySuffix);
}

static void GetNormalizedStorageKey(const nsACString& hostname,
                                    const OriginAttributes& aOriginAttributes,
                                    /*out*/ nsAutoCString& storageKey) {
  storageKey = hostname;

  // Don't isolate by userContextId.
  OriginAttributes originAttributesNoUserContext = aOriginAttributes;
  originAttributesNoUserContext.mUserContextId =
      nsIScriptSecurityManager::DEFAULT_USER_CONTEXT_ID;
  NormalizePartitionKey(originAttributesNoUserContext.mPartitionKey);
  nsAutoCString originAttributesSuffix;
  originAttributesNoUserContext.CreateSuffix(originAttributesSuffix);
  storageKey.Append(originAttributesSuffix);
}

// Expire times are in millis.  Since Headers max-age is in seconds, and
// PR_Now() is in micros, normalize the units at milliseconds.
static int64_t ExpireTimeFromMaxAge(uint64_t maxAge) {
  return (PR_Now() / PR_USEC_PER_MSEC) + ((int64_t)maxAge * PR_MSEC_PER_SEC);
}

inline uint64_t AbsoluteDifference(int64_t a, int64_t b) {
  if (a <= b) {
    return b - a;
  }
  return a - b;
}

const uint64_t sOneDayInMilliseconds = 24 * 60 * 60 * 1000;

nsresult nsSiteSecurityService::SetHSTSState(
    const char* aHost, int64_t maxage, bool includeSubdomains,
    SecurityPropertyState aHSTSState,
    const OriginAttributes& aOriginAttributes) {
  nsAutoCString hostname(aHost);
  // If max-age is zero, the host is no longer considered HSTS. If the host was
  // preloaded, we store an entry indicating that this host is not HSTS, causing
  // the preloaded information to be ignored.
  if (maxage == 0) {
    return MarkHostAsNotHSTS(hostname, aOriginAttributes);
  }

  MOZ_ASSERT(aHSTSState == SecurityPropertySet,
             "HSTS State must be SecurityPropertySet");

  int64_t expiretime = ExpireTimeFromMaxAge(maxage);
  SiteHSTSState siteState(hostname, aOriginAttributes, expiretime, aHSTSState,
                          includeSubdomains);
  nsAutoCString stateString;
  siteState.ToString(stateString);
  SSSLOG(("SSS: setting state for %s", hostname.get()));
  bool isPrivate = aOriginAttributes.IsPrivateBrowsing();
  nsIDataStorage::DataType storageType =
      isPrivate ? nsIDataStorage::DataType::Private
                : nsIDataStorage::DataType::Persistent;
  SSSLOG(("SSS: storing HSTS site entry for %s", hostname.get()));
  nsAutoCString value;
  nsresult rv =
      GetWithMigration(hostname, aOriginAttributes, storageType, value);
  // If this fails for a reason other than nothing by that key exists,
  // propagate the failure.
  if (NS_FAILED(rv) && rv != NS_ERROR_NOT_AVAILABLE) {
    return rv;
  }
  // This is an entirely new entry.
  if (rv == NS_ERROR_NOT_AVAILABLE) {
    nsAutoCString storageKey;
    GetNormalizedStorageKey(hostname, aOriginAttributes, storageKey);
    return mSiteStateStorage->Put(storageKey, stateString, storageType);
  }
  // Otherwise, only update the backing storage if the currently-stored state
  // is different. In the case of expiration time, "different" means "is
  // different by more than a day".
  SiteHSTSState curSiteState(hostname, aOriginAttributes, value);
  if (curSiteState.mHSTSState != siteState.mHSTSState ||
      curSiteState.mHSTSIncludeSubdomains != siteState.mHSTSIncludeSubdomains ||
      AbsoluteDifference(curSiteState.mHSTSExpireTime,
                         siteState.mHSTSExpireTime) > sOneDayInMilliseconds) {
    rv =
        PutWithMigration(hostname, aOriginAttributes, storageType, stateString);
    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  return NS_OK;
}

// Helper function to mark a host as not HSTS. In the general case, we can just
// remove the HSTS state. However, for preloaded entries, we have to store an
// entry that indicates this host is not HSTS to prevent the implementation
// using the preloaded information.
nsresult nsSiteSecurityService::MarkHostAsNotHSTS(
    const nsAutoCString& aHost, const OriginAttributes& aOriginAttributes) {
  bool isPrivate = aOriginAttributes.IsPrivateBrowsing();
  nsIDataStorage::DataType storageType =
      isPrivate ? nsIDataStorage::DataType::Private
                : nsIDataStorage::DataType::Persistent;
  if (GetPreloadStatus(aHost)) {
    SSSLOG(("SSS: storing knockout entry for %s", aHost.get()));
    SiteHSTSState siteState(aHost, aOriginAttributes, 0,
                            SecurityPropertyKnockout, false);
    nsAutoCString stateString;
    siteState.ToString(stateString);
    nsresult rv =
        PutWithMigration(aHost, aOriginAttributes, storageType, stateString);
    NS_ENSURE_SUCCESS(rv, rv);
  } else {
    SSSLOG(("SSS: removing entry for %s", aHost.get()));
    RemoveWithMigration(aHost, aOriginAttributes, storageType);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsSiteSecurityService::ResetState(nsIURI* aURI,
                                  JS::Handle<JS::Value> aOriginAttributes,
                                  nsISiteSecurityService::ResetStateBy aScope,
                                  JSContext* aCx, uint8_t aArgc) {
  if (!aURI) {
    return NS_ERROR_INVALID_ARG;
  }

  OriginAttributes originAttributes;
  if (aArgc > 0) {
    // OriginAttributes were passed in.
    if (!aOriginAttributes.isObject() ||
        !originAttributes.Init(aCx, aOriginAttributes)) {
      return NS_ERROR_INVALID_ARG;
    }
  }
  nsISiteSecurityService::ResetStateBy scope =
      nsISiteSecurityService::ResetStateBy::ExactDomain;
  if (aArgc > 1) {
    // ResetStateBy scope was passed in
    scope = aScope;
  }

  return ResetStateInternal(aURI, originAttributes, scope);
}

// Helper function to reset stored state of the given type for the host
// identified by the given URI. If there is preloaded information for the host,
// that information will be used for future queries. C.f. MarkHostAsNotHSTS,
// which will store a knockout entry for preloaded HSTS hosts that have sent a
// header with max-age=0 (meaning preloaded information will then not be used
// for that host).
nsresult nsSiteSecurityService::ResetStateInternal(
    nsIURI* aURI, const OriginAttributes& aOriginAttributes,
    nsISiteSecurityService::ResetStateBy aScope) {
  if (!aURI) {
    return NS_ERROR_INVALID_ARG;
  }
  nsAutoCString hostname;
  nsresult rv = GetHost(aURI, hostname);
  if (NS_FAILED(rv)) {
    return rv;
  }

  OriginAttributes normalizedOriginAttributes(aOriginAttributes);
  NormalizePartitionKey(normalizedOriginAttributes.mPartitionKey);

  if (aScope == ResetStateBy::ExactDomain) {
    ResetStateForExactDomain(hostname, normalizedOriginAttributes);
    return NS_OK;
  }

  nsTArray<RefPtr<nsIDataStorageItem>> items;
  rv = mSiteStateStorage->GetAll(items);
  if (NS_FAILED(rv)) {
    return rv;
  }
  for (const auto& item : items) {
    static const nsLiteralCString kHPKPKeySuffix = ":HPKP"_ns;
    nsAutoCString key;
    rv = item->GetKey(key);
    if (NS_FAILED(rv)) {
      return rv;
    }
    nsAutoCString value;
    rv = item->GetValue(value);
    if (NS_FAILED(rv)) {
      return rv;
    }
    if (StringEndsWith(key, kHPKPKeySuffix)) {
      (void)mSiteStateStorage->Remove(key,
                                      nsIDataStorage::DataType::Persistent);
      continue;
    }
    size_t suffixLength =
        StringEndsWith(key, kHSTSKeySuffix) ? kHSTSKeySuffix.Length() : 0;
    nsCString origin(StringHead(key, key.Length() - suffixLength));
    nsAutoCString itemHostname;
    OriginAttributes itemOriginAttributes;
    if (!itemOriginAttributes.PopulateFromOrigin(origin, itemHostname)) {
      continue;
    }
    bool hasRootDomain = false;
    nsresult rv = net::HasRootDomain(itemHostname, hostname, &hasRootDomain);
    if (NS_FAILED(rv)) {
      continue;
    }
    if (hasRootDomain) {
      ResetStateForExactDomain(itemHostname, itemOriginAttributes);
    } else if (aScope == ResetStateBy::BaseDomain) {
      mozilla::dom::PartitionKeyPatternDictionary partitionKeyPattern;
      partitionKeyPattern.mBaseDomain.Construct(
          NS_ConvertUTF8toUTF16(hostname));
      OriginAttributesPattern originAttributesPattern;
      originAttributesPattern.mPartitionKeyPattern.Construct(
          partitionKeyPattern);
      if (originAttributesPattern.Matches(itemOriginAttributes)) {
        ResetStateForExactDomain(itemHostname, itemOriginAttributes);
      }
    }
  }
  return NS_OK;
}

void nsSiteSecurityService::ResetStateForExactDomain(
    const nsCString& aHostname, const OriginAttributes& aOriginAttributes) {
  bool isPrivate = aOriginAttributes.IsPrivateBrowsing();
  nsIDataStorage::DataType storageType =
      isPrivate ? nsIDataStorage::DataType::Private
                : nsIDataStorage::DataType::Persistent;
  RemoveWithMigration(aHostname, aOriginAttributes, storageType);
}

bool nsSiteSecurityService::HostIsIPAddress(const nsCString& hostname) {
  PRNetAddr hostAddr;
  PRErrorCode prv = PR_StringToNetAddr(hostname.get(), &hostAddr);
  return (prv == PR_SUCCESS);
}

NS_IMETHODIMP
nsSiteSecurityService::ProcessHeaderScriptable(
    nsIURI* aSourceURI, const nsACString& aHeader,
    JS::Handle<JS::Value> aOriginAttributes, uint64_t* aMaxAge,
    bool* aIncludeSubdomains, uint32_t* aFailureResult, JSContext* aCx,
    uint8_t aArgc) {
  OriginAttributes originAttributes;
  if (aArgc > 0) {
    if (!aOriginAttributes.isObject() ||
        !originAttributes.Init(aCx, aOriginAttributes)) {
      return NS_ERROR_INVALID_ARG;
    }
  }
  return ProcessHeader(aSourceURI, aHeader, originAttributes, aMaxAge,
                       aIncludeSubdomains, aFailureResult);
}

NS_IMETHODIMP
nsSiteSecurityService::ProcessHeader(nsIURI* aSourceURI,
                                     const nsACString& aHeader,
                                     const OriginAttributes& aOriginAttributes,
                                     uint64_t* aMaxAge,
                                     bool* aIncludeSubdomains,
                                     uint32_t* aFailureResult) {
  if (aFailureResult) {
    *aFailureResult = nsISiteSecurityService::ERROR_UNKNOWN;
  }
  return ProcessHeaderInternal(aSourceURI, PromiseFlatCString(aHeader),
                               aOriginAttributes, aMaxAge, aIncludeSubdomains,
                               aFailureResult);
}

nsresult nsSiteSecurityService::ProcessHeaderInternal(
    nsIURI* aSourceURI, const nsCString& aHeader,
    const OriginAttributes& aOriginAttributes, uint64_t* aMaxAge,
    bool* aIncludeSubdomains, uint32_t* aFailureResult) {
  if (aFailureResult) {
    *aFailureResult = nsISiteSecurityService::ERROR_UNKNOWN;
  }
  if (aMaxAge != nullptr) {
    *aMaxAge = 0;
  }

  if (aIncludeSubdomains != nullptr) {
    *aIncludeSubdomains = false;
  }

  nsAutoCString host;
  nsresult rv = GetHost(aSourceURI, host);
  NS_ENSURE_SUCCESS(rv, rv);
  if (HostIsIPAddress(host)) {
    /* Don't process headers if a site is accessed by IP address. */
    return NS_OK;
  }

  return ProcessSTSHeader(aSourceURI, aHeader, aOriginAttributes, aMaxAge,
                          aIncludeSubdomains, aFailureResult);
}

static uint32_t ParseSSSHeaders(const nsCString& aHeader,
                                bool& foundIncludeSubdomains, bool& foundMaxAge,
                                bool& foundUnrecognizedDirective,
                                uint64_t& maxAge) {
  // "Strict-Transport-Security" ":" OWS
  //      STS-d  *( OWS ";" OWS STS-d  OWS)
  //
  //  ; STS directive
  //  STS-d      = maxAge / includeSubDomains
  //
  //  maxAge     = "max-age" "=" delta-seconds v-ext
  //
  //  includeSubDomains = [ "includeSubDomains" ]
  //
  //  The order of the directives is not significant.
  //  All directives must appear only once.
  //  Directive names are case-insensitive.
  //  The entire header is invalid if a directive not conforming to the
  //  syntax is encountered.
  //  Unrecognized directives (that are otherwise syntactically valid) are
  //  ignored, and the rest of the header is parsed as normal.

  constexpr auto max_age_var = "max-age"_ns;
  constexpr auto include_subd_var = "includesubdomains"_ns;

  nsSecurityHeaderParser parser(aHeader);
  nsresult rv = parser.Parse();
  if (NS_FAILED(rv)) {
    SSSLOG(("SSS: could not parse header"));
    return nsISiteSecurityService::ERROR_COULD_NOT_PARSE_HEADER;
  }
  mozilla::LinkedList<nsSecurityHeaderDirective>* directives =
      parser.GetDirectives();

  for (nsSecurityHeaderDirective* directive = directives->getFirst();
       directive != nullptr; directive = directive->getNext()) {
    SSSLOG(("SSS: found directive %s\n", directive->mName.get()));
    if (directive->mName.EqualsIgnoreCase(max_age_var)) {
      if (foundMaxAge) {
        SSSLOG(("SSS: found two max-age directives"));
        return nsISiteSecurityService::ERROR_MULTIPLE_MAX_AGES;
      }

      SSSLOG(("SSS: found max-age directive"));
      foundMaxAge = true;

      if (directive->mValue.isNothing()) {
        SSSLOG(("SSS: max-age directive didn't include value"));
        return nsISiteSecurityService::ERROR_INVALID_MAX_AGE;
      }

      Tokenizer tokenizer(*(directive->mValue));
      if (!tokenizer.ReadInteger(&maxAge)) {
        SSSLOG(("SSS: could not parse delta-seconds"));
        return nsISiteSecurityService::ERROR_INVALID_MAX_AGE;
      }

      if (!tokenizer.CheckEOF()) {
        SSSLOG(("SSS: invalid value for max-age directive"));
        return nsISiteSecurityService::ERROR_INVALID_MAX_AGE;
      }

      SSSLOG(("SSS: parsed delta-seconds: %" PRIu64, maxAge));
    } else if (directive->mName.EqualsIgnoreCase(include_subd_var)) {
      if (foundIncludeSubdomains) {
        SSSLOG(("SSS: found two includeSubdomains directives"));
        return nsISiteSecurityService::ERROR_MULTIPLE_INCLUDE_SUBDOMAINS;
      }

      SSSLOG(("SSS: found includeSubdomains directive"));
      foundIncludeSubdomains = true;

      if (directive->mValue.isSome()) {
        SSSLOG(("SSS: includeSubdomains directive unexpectedly had value '%s'",
                directive->mValue->get()));
        return nsISiteSecurityService::ERROR_INVALID_INCLUDE_SUBDOMAINS;
      }
    } else {
      SSSLOG(("SSS: ignoring unrecognized directive '%s'",
              directive->mName.get()));
      foundUnrecognizedDirective = true;
    }
  }
  return nsISiteSecurityService::Success;
}

// 100 years is wildly longer than anyone will ever need.
const uint64_t sMaxMaxAgeInSeconds = UINT64_C(60 * 60 * 24 * 365 * 100);

nsresult nsSiteSecurityService::ProcessSTSHeader(
    nsIURI* aSourceURI, const nsCString& aHeader,
    const OriginAttributes& aOriginAttributes, uint64_t* aMaxAge,
    bool* aIncludeSubdomains, uint32_t* aFailureResult) {
  if (aFailureResult) {
    *aFailureResult = nsISiteSecurityService::ERROR_UNKNOWN;
  }
  SSSLOG(("SSS: processing HSTS header '%s'", aHeader.get()));

  bool foundMaxAge = false;
  bool foundIncludeSubdomains = false;
  bool foundUnrecognizedDirective = false;
  uint64_t maxAge = 0;

  uint32_t sssrv = ParseSSSHeaders(aHeader, foundIncludeSubdomains, foundMaxAge,
                                   foundUnrecognizedDirective, maxAge);
  if (sssrv != nsISiteSecurityService::Success) {
    if (aFailureResult) {
      *aFailureResult = sssrv;
    }
    return NS_ERROR_FAILURE;
  }

  // after processing all the directives, make sure we came across max-age
  // somewhere.
  if (!foundMaxAge) {
    SSSLOG(("SSS: did not encounter required max-age directive"));
    if (aFailureResult) {
      *aFailureResult = nsISiteSecurityService::ERROR_NO_MAX_AGE;
    }
    return NS_ERROR_FAILURE;
  }

  // Cap the specified max-age.
  if (maxAge > sMaxMaxAgeInSeconds) {
    maxAge = sMaxMaxAgeInSeconds;
  }

  nsAutoCString hostname;
  nsresult rv = GetHost(aSourceURI, hostname);
  NS_ENSURE_SUCCESS(rv, rv);

  // record the successfully parsed header data.
  rv = SetHSTSState(hostname.get(), maxAge, foundIncludeSubdomains,
                    SecurityPropertySet, aOriginAttributes);
  if (NS_FAILED(rv)) {
    SSSLOG(("SSS: failed to set STS state"));
    if (aFailureResult) {
      *aFailureResult = nsISiteSecurityService::ERROR_COULD_NOT_SAVE_STATE;
    }
    return rv;
  }

  if (aMaxAge != nullptr) {
    *aMaxAge = maxAge;
  }

  if (aIncludeSubdomains != nullptr) {
    *aIncludeSubdomains = foundIncludeSubdomains;
  }

  return foundUnrecognizedDirective ? NS_SUCCESS_LOSS_OF_INSIGNIFICANT_DATA
                                    : NS_OK;
}

NS_IMETHODIMP
nsSiteSecurityService::IsSecureURIScriptable(
    nsIURI* aURI, JS::Handle<JS::Value> aOriginAttributes, JSContext* aCx,
    uint8_t aArgc, bool* aResult) {
  OriginAttributes originAttributes;
  if (aArgc > 0) {
    if (!aOriginAttributes.isObject() ||
        !originAttributes.Init(aCx, aOriginAttributes)) {
      return NS_ERROR_INVALID_ARG;
    }
  }
  return IsSecureURI(aURI, originAttributes, aResult);
}

NS_IMETHODIMP
nsSiteSecurityService::IsSecureURI(nsIURI* aURI,
                                   const OriginAttributes& aOriginAttributes,
                                   bool* aResult) {
  NS_ENSURE_ARG(aURI);
  NS_ENSURE_ARG(aResult);

  nsAutoCString hostname;
  nsresult rv = GetHost(aURI, hostname);
  NS_ENSURE_SUCCESS(rv, rv);
  *aResult = false;

  /* An IP address never qualifies as a secure URI. */
  const nsCString& flatHost = PromiseFlatCString(hostname);
  if (HostIsIPAddress(flatHost)) {
    return NS_OK;
  }

  /* Host name is invalid, can't be secure */
  if (!net_IsValidDNSHost(flatHost)) {
    return NS_OK;
  }

  nsAutoCString host(
      PublicKeyPinningService::CanonicalizeHostname(flatHost.get()));

  // First check the exact host.
  bool hostMatchesHSTSEntry = false;
  rv = HostMatchesHSTSEntry(host, false, aOriginAttributes,
                            hostMatchesHSTSEntry);
  if (NS_FAILED(rv)) {
    return rv;
  }
  if (hostMatchesHSTSEntry) {
    *aResult = true;
    return NS_OK;
  }

  SSSLOG(("%s not congruent match for any known HSTS host", host.get()));
  const char* superdomain;

  uint32_t offset = 0;
  for (offset = host.FindChar('.', offset) + 1; offset > 0;
       offset = host.FindChar('.', offset) + 1) {
    superdomain = host.get() + offset;

    // If we get an empty string, don't continue.
    if (strlen(superdomain) < 1) {
      break;
    }

    // Do the same thing as with the exact host except now we're looking at
    // ancestor domains of the original host and, therefore, we have to require
    // that the entry asserts includeSubdomains.
    nsAutoCString superdomainString(superdomain);
    hostMatchesHSTSEntry = false;
    rv = HostMatchesHSTSEntry(superdomainString, true, aOriginAttributes,
                              hostMatchesHSTSEntry);
    if (NS_FAILED(rv)) {
      return rv;
    }
    if (hostMatchesHSTSEntry) {
      *aResult = true;
      return NS_OK;
    }

    SSSLOG(
        ("superdomain %s not known HSTS host (or includeSubdomains not set), "
         "walking up domain",
         superdomain));
  }

  // If we get here, there was no congruent match, and no superdomain matched
  // while asserting includeSubdomains, so this host is not HSTS.
  *aResult = false;
  return NS_OK;
}

// Checks if the given host is in the preload list.
//
// @param aHost The host to match. Only does exact host matching.
// @param aIncludeSubdomains Out, optional. Indicates whether or not to include
//        subdomains. Only set if the host is matched and this function returns
//        true.
//
// @return True if the host is matched, false otherwise.
bool nsSiteSecurityService::GetPreloadStatus(const nsACString& aHost,
                                             bool* aIncludeSubdomains) const {
  const int kIncludeSubdomains = 1;
  bool found = false;

  PRTime currentTime =
      PR_Now() +
      (StaticPrefs::test_currentTimeOffsetSeconds() * (PRTime)PR_USEC_PER_SEC);
  if (StaticPrefs::network_stricttransportsecurity_preloadlist() &&
      currentTime < gPreloadListExpirationTime) {
    int result = mDafsa.Lookup(aHost);
    found = (result != mozilla::Dafsa::kKeyNotFound);
    if (found && aIncludeSubdomains) {
      *aIncludeSubdomains = (result == kIncludeSubdomains);
    }
  }

  return found;
}

nsresult nsSiteSecurityService::GetWithMigration(
    const nsACString& aHostname, const OriginAttributes& aOriginAttributes,
    nsIDataStorage::DataType aDataStorageType, nsACString& aValue) {
  // First see if this entry exists and has already been migrated.
  nsAutoCString storageKey;
  GetNormalizedStorageKey(aHostname, aOriginAttributes, storageKey);
  nsresult rv = mSiteStateStorage->Get(storageKey, aDataStorageType, aValue);
  if (NS_SUCCEEDED(rv)) {
    return NS_OK;
  }
  if (NS_FAILED(rv) && rv != NS_ERROR_NOT_AVAILABLE) {
    return rv;
  }
  // Otherwise, it potentially needs to be migrated, if it's persistent data.
  if (aDataStorageType != nsIDataStorage::DataType::Persistent) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  nsAutoCString oldStorageKey;
  GetOldStorageKey(aHostname, aOriginAttributes, oldStorageKey);
  rv = mSiteStateStorage->Get(oldStorageKey,
                              nsIDataStorage::DataType::Persistent, aValue);
  if (NS_FAILED(rv)) {
    return rv;
  }
  // If there was a value, remove the old entry, insert a new one with the new
  // key, and return the value.
  rv = mSiteStateStorage->Remove(oldStorageKey,
                                 nsIDataStorage::DataType::Persistent);
  if (NS_FAILED(rv)) {
    return rv;
  }
  return mSiteStateStorage->Put(storageKey, aValue,
                                nsIDataStorage::DataType::Persistent);
}

nsresult nsSiteSecurityService::PutWithMigration(
    const nsACString& aHostname, const OriginAttributes& aOriginAttributes,
    nsIDataStorage::DataType aDataStorageType, const nsACString& aStateString) {
  // Only persistent data needs migrating.
  if (aDataStorageType == nsIDataStorage::DataType::Persistent) {
    // Since the intention is to overwrite the previously-stored data anyway,
    // the old entry can be removed.
    nsAutoCString oldStorageKey;
    GetOldStorageKey(aHostname, aOriginAttributes, oldStorageKey);
    nsresult rv = mSiteStateStorage->Remove(
        oldStorageKey, nsIDataStorage::DataType::Persistent);
    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  nsAutoCString storageKey;
  GetNormalizedStorageKey(aHostname, aOriginAttributes, storageKey);
  return mSiteStateStorage->Put(storageKey, aStateString, aDataStorageType);
}

nsresult nsSiteSecurityService::RemoveWithMigration(
    const nsACString& aHostname, const OriginAttributes& aOriginAttributes,
    nsIDataStorage::DataType aDataStorageType) {
  // Only persistent data needs migrating.
  if (aDataStorageType == nsIDataStorage::DataType::Persistent) {
    nsAutoCString oldStorageKey;
    GetOldStorageKey(aHostname, aOriginAttributes, oldStorageKey);
    nsresult rv = mSiteStateStorage->Remove(
        oldStorageKey, nsIDataStorage::DataType::Persistent);
    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  nsAutoCString storageKey;
  GetNormalizedStorageKey(aHostname, aOriginAttributes, storageKey);
  return mSiteStateStorage->Remove(storageKey, aDataStorageType);
}

// Determines whether or not there is a matching HSTS entry for the given host.
// If aRequireIncludeSubdomains is set, then for there to be a matching HSTS
// entry, it must assert includeSubdomains.
nsresult nsSiteSecurityService::HostMatchesHSTSEntry(
    const nsAutoCString& aHost, bool aRequireIncludeSubdomains,
    const OriginAttributes& aOriginAttributes, bool& aHostMatchesHSTSEntry) {
  aHostMatchesHSTSEntry = false;
  // First we check for an entry in site security storage. If that entry exists,
  // we don't want to check in the preload lists. We only want to use the
  // stored value if it is not a knockout entry, however.
  // Additionally, if it is a knockout entry, we want to stop looking for data
  // on the host, because the knockout entry indicates "we have no information
  // regarding the security status of this host".
  bool isPrivate = aOriginAttributes.IsPrivateBrowsing();
  nsIDataStorage::DataType storageType =
      isPrivate ? nsIDataStorage::DataType::Private
                : nsIDataStorage::DataType::Persistent;
  SSSLOG(("Seeking HSTS entry for %s", aHost.get()));
  nsAutoCString value;
  nsresult rv = GetWithMigration(aHost, aOriginAttributes, storageType, value);
  // If this fails for a reason other than nothing by that key exists,
  // propagate the failure.
  if (NS_FAILED(rv) && rv != NS_ERROR_NOT_AVAILABLE) {
    return rv;
  }
  bool checkPreloadList = true;
  // If something by that key does exist, decode and process that information.
  if (NS_SUCCEEDED(rv)) {
    SiteHSTSState siteState(aHost, aOriginAttributes, value);
    if (siteState.mHSTSState != SecurityPropertyUnset) {
      SSSLOG(("Found HSTS entry for %s", aHost.get()));
      bool expired = siteState.IsExpired();
      if (!expired) {
        SSSLOG(("Entry for %s is not expired", aHost.get()));
        if (siteState.mHSTSState == SecurityPropertySet) {
          aHostMatchesHSTSEntry = aRequireIncludeSubdomains
                                      ? siteState.mHSTSIncludeSubdomains
                                      : true;
          return NS_OK;
        }
      }

      if (expired) {
        SSSLOG(
            ("Entry %s is expired - checking for preload state", aHost.get()));
        if (!GetPreloadStatus(aHost)) {
          SSSLOG(("No static preload - removing expired entry"));
          nsAutoCString storageKey;
          GetNormalizedStorageKey(aHost, aOriginAttributes, storageKey);
          rv = mSiteStateStorage->Remove(storageKey, storageType);
          if (NS_FAILED(rv)) {
            return rv;
          }
        }
      }
      return NS_OK;
    }
    checkPreloadList = false;
  }

  bool includeSubdomains = false;
  // Finally look in the static preload list.
  if (checkPreloadList && GetPreloadStatus(aHost, &includeSubdomains)) {
    SSSLOG(("%s is a preloaded HSTS host", aHost.get()));
    aHostMatchesHSTSEntry =
        aRequireIncludeSubdomains ? includeSubdomains : true;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsSiteSecurityService::ClearAll() { return mSiteStateStorage->Clear(); }
