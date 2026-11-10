/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/InternalHeaders.h"

#include "FetchUtil.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/dom/FetchTypes.h"
#include "nsCharSeparatedTokenizer.h"
#include "nsContentUtils.h"
#include "nsIHttpChannel.h"
#include "nsIHttpHeaderVisitor.h"
#include "nsNetUtil.h"
#include "nsReadableUtils.h"

namespace mozilla::dom {

InternalHeaders::InternalHeaders(nsTArray<Entry>&& aHeaders,
                                 HeadersGuardEnum aGuard)
    : mGuard(aGuard), mList(std::move(aHeaders)), mListDirty(true) {}

InternalHeaders::InternalHeaders(
    const nsTArray<HeadersEntry>& aHeadersEntryList, HeadersGuardEnum aGuard)
    : mGuard(aGuard), mListDirty(true) {
  for (const HeadersEntry& headersEntry : aHeadersEntryList) {
    mList.AppendElement(Entry(headersEntry.name(), headersEntry.value()));
  }
}

void InternalHeaders::ToIPC(nsTArray<HeadersEntry>& aIPCHeaders,
                            HeadersGuardEnum& aGuard) {
  aGuard = mGuard;

  aIPCHeaders.Clear();
  for (Entry& entry : mList) {
    aIPCHeaders.AppendElement(HeadersEntry(entry.mName, entry.mValue));
  }
}

bool InternalHeaders::IsValidHeaderValue(const nsCString& aLowerName,
                                         const nsCString& aNormalizedValue,
                                         ErrorResult& aRv) {
  // Steps 2 to 6 for ::Set() and ::Append() in the spec.

  // Step 2
  if (IsInvalidName(aLowerName, aRv) || IsInvalidValue(aNormalizedValue, aRv)) {
    return false;
  }

  // Step 3
  if (IsImmutable(aRv)) {
    return false;
  }

  // Step 4
  if (mGuard == HeadersGuardEnum::Request) {
    if (IsForbiddenRequestHeader(aLowerName, aNormalizedValue)) {
      return false;
    }
  }
  // Step 5
  if (mGuard == HeadersGuardEnum::Request_no_cors) {
    nsAutoCString tempValue;
    Get(aLowerName, tempValue, aRv);

    if (tempValue.IsVoid()) {
      tempValue = aNormalizedValue;
    } else {
      tempValue.Append(", ");
      tempValue.Append(aNormalizedValue);
    }

    if (!nsContentUtils::IsCORSSafelistedRequestHeader(aLowerName, tempValue)) {
      return false;
    }
  }

  // Step 6
  else if (IsForbiddenResponseHeader(aLowerName)) {
    return false;
  }

  return true;
}

void InternalHeaders::Append(const nsACString& aName, const nsACString& aValue,
                             ErrorResult& aRv) {
  // Step 1
  nsAutoCString trimValue;
  NS_TrimHTTPWhitespace(aValue, trimValue);

  // Steps 2 to 6
  nsAutoCString lowerName;
  ToLowerCase(aName, lowerName);
  if (!IsValidHeaderValue(lowerName, trimValue, aRv)) {
    return;
  }

  // Step 7
  nsAutoCString name(aName);
  ReuseExistingNameIfExists(name);
  SetListDirty();
  mList.AppendElement(Entry(name, trimValue));

  // Step 8
  if (mGuard == HeadersGuardEnum::Request_no_cors) {
    RemovePrivilegedNoCorsRequestHeaders();
  }
}

void InternalHeaders::RemovePrivilegedNoCorsRequestHeaders() {
  bool dirty = false;

  // remove in reverse order to minimize copying
  for (int32_t i = mList.Length() - 1; i >= 0; --i) {
    if (IsPrivilegedNoCorsRequestHeaderName(mList[i].mName)) {
      mList.RemoveElementAt(i);
      dirty = true;
    }
  }

  if (dirty) {
    SetListDirty();
  }
}

bool InternalHeaders::DeleteInternal(const nsCString& aLowerName,
                                     ErrorResult& aRv) {
  bool dirty = false;

  // remove in reverse order to minimize copying
  for (int32_t i = mList.Length() - 1; i >= 0; --i) {
    if (mList[i].mName.EqualsIgnoreCase(aLowerName.get())) {
      mList.RemoveElementAt(i);
      dirty = true;
    }
  }

  if (dirty) {
    SetListDirty();
  }

  return dirty;
}

void InternalHeaders::Delete(const nsACString& aName, ErrorResult& aRv) {
  // See https://fetch.spec.whatwg.org/#dom-headers-delete
  nsAutoCString lowerName;
  ToLowerCase(aName, lowerName);

  // Step 1
  if (IsInvalidName(lowerName, aRv)) {
    return;
  }

  if (IsImmutable(aRv)) {
    return;
  }

  nsAutoCString value;
  GetInternal(lowerName, value, aRv);
  if (IsForbiddenRequestHeader(lowerName, value)) {
    return;
  }

  // Step 2
  if (mGuard == HeadersGuardEnum::Request_no_cors &&
      !IsNoCorsSafelistedRequestHeaderName(lowerName) &&
      !IsPrivilegedNoCorsRequestHeaderName(lowerName)) {
    return;
  }

  if (IsForbiddenResponseHeader(lowerName)) {
    return;
  }

  // Steps 3, 4, and 5
  if (!DeleteInternal(lowerName, aRv)) {
    return;
  }

  // Step 6
  if (mGuard == HeadersGuardEnum::Request_no_cors) {
    RemovePrivilegedNoCorsRequestHeaders();
  }
}

void InternalHeaders::Get(const nsACString& aName, nsACString& aValue,
                          ErrorResult& aRv) const {
  nsAutoCString lowerName;
  ToLowerCase(aName, lowerName);

  if (IsInvalidName(lowerName, aRv)) {
    return;
  }

  GetInternal(lowerName, aValue, aRv);
}

void InternalHeaders::GetInternal(const nsCString& aLowerName,
                                  nsACString& aValue, ErrorResult& aRv) const {
  const char* delimiter = ", ";
  bool firstValueFound = false;

  for (uint32_t i = 0; i < mList.Length(); ++i) {
    if (mList[i].mName.EqualsIgnoreCase(aLowerName.get())) {
      if (firstValueFound) {
        aValue += delimiter;
      }
      aValue += mList[i].mValue;
      firstValueFound = true;
    }
  }

  // No value found, so return null to content
  if (!firstValueFound) {
    aValue.SetIsVoid(true);
  }
}

void InternalHeaders::GetSetCookie(nsTArray<nsCString>& aValues) const {
  for (uint32_t i = 0; i < mList.Length(); ++i) {
    if (mList[i].mName.EqualsIgnoreCase("Set-Cookie")) {
      aValues.AppendElement(mList[i].mValue);
    }
  }
}

void InternalHeaders::GetFirst(const nsACString& aName, nsACString& aValue,
                               ErrorResult& aRv) const {
  nsAutoCString lowerName;
  ToLowerCase(aName, lowerName);

  if (IsInvalidName(lowerName, aRv)) {
    return;
  }

  for (uint32_t i = 0; i < mList.Length(); ++i) {
    if (mList[i].mName.EqualsIgnoreCase(lowerName.get())) {
      aValue = mList[i].mValue;
      return;
    }
  }

  // No value found, so return null to content
  aValue.SetIsVoid(true);
}

bool InternalHeaders::Has(const nsACString& aName, ErrorResult& aRv) const {
  nsAutoCString lowerName;
  ToLowerCase(aName, lowerName);

  if (IsInvalidName(lowerName, aRv)) {
    return false;
  }

  for (uint32_t i = 0; i < mList.Length(); ++i) {
    if (mList[i].mName.EqualsIgnoreCase(lowerName.get())) {
      return true;
    }
  }
  return false;
}

void InternalHeaders::Set(const nsACString& aName, const nsACString& aValue,
                          ErrorResult& aRv) {
  // Step 1
  nsAutoCString trimValue;
  NS_TrimHTTPWhitespace(aValue, trimValue);

  // Steps 2 to 6
  nsAutoCString lowerName;
  ToLowerCase(aName, lowerName);
  if (!IsValidHeaderValue(lowerName, trimValue, aRv)) {
    return;
  }

  // Step 7
  SetListDirty();

  int32_t firstIndex = INT32_MAX;

  // remove in reverse order to minimize copying
  for (int32_t i = mList.Length() - 1; i >= 0; --i) {
    if (mList[i].mName.EqualsIgnoreCase(lowerName.get())) {
      firstIndex = std::min(firstIndex, i);
      mList.RemoveElementAt(i);
    }
  }

  if (firstIndex < INT32_MAX) {
    Entry* entry = mList.InsertElementAt(firstIndex);
    entry->mName = aName;
    entry->mValue = std::move(trimValue);
  } else {
    mList.AppendElement(Entry(aName, std::move(trimValue)));
  }

  // Step 8
  if (mGuard == HeadersGuardEnum::Request_no_cors) {
    RemovePrivilegedNoCorsRequestHeaders();
  }
}

void InternalHeaders::Clear() {
  SetListDirty();
  mList.Clear();
}

void InternalHeaders::SetGuard(HeadersGuardEnum aGuard, ErrorResult& aRv) {
  // The guard is only checked during ::Set() and ::Append() in the spec.  It
  // does not require revalidating headers already set.
  mGuard = aGuard;
}

InternalHeaders::~InternalHeaders() = default;

// static
bool InternalHeaders::IsNoCorsSafelistedRequestHeaderName(
    const nsCString& aName) {
  return aName.EqualsIgnoreCase("accept") ||
         aName.EqualsIgnoreCase("accept-language") ||
         aName.EqualsIgnoreCase("content-language") ||
         aName.EqualsIgnoreCase("content-type");
}

// static
bool InternalHeaders::IsPrivilegedNoCorsRequestHeaderName(
    const nsCString& aName) {
  return aName.EqualsIgnoreCase("range");
}

// static
bool InternalHeaders::IsRevalidationHeader(const nsCString& aName) {
  return aName.EqualsIgnoreCase("if-modified-since") ||
         aName.EqualsIgnoreCase("if-none-match") ||
         aName.EqualsIgnoreCase("if-unmodified-since") ||
         aName.EqualsIgnoreCase("if-match") ||
         aName.EqualsIgnoreCase("if-range");
}

// static
bool InternalHeaders::IsInvalidName(const nsACString& aName, ErrorResult& aRv) {
  if (!NS_IsValidHTTPToken(aName)) {
    aRv.ThrowTypeError<MSG_INVALID_HEADER_NAME>(aName);
    return true;
  }

  return false;
}

// static
bool InternalHeaders::IsInvalidValue(const nsACString& aValue,
                                     ErrorResult& aRv) {
  if (!NS_IsReasonableHTTPHeaderValue(aValue)) {
    aRv.ThrowTypeError<MSG_INVALID_HEADER_VALUE>(aValue);
    return true;
  }
  return false;
}

bool InternalHeaders::IsImmutable(ErrorResult& aRv) const {
  if (mGuard == HeadersGuardEnum::Immutable) {
    aRv.ThrowTypeError("Headers are immutable and cannot be modified.");
    return true;
  }
  return false;
}

bool InternalHeaders::IsForbiddenRequestHeader(const nsCString& aName,
                                               const nsACString& aValue) const {
  return mGuard == HeadersGuardEnum::Request &&
         nsContentUtils::IsForbiddenRequestHeader(aName, aValue);
}

bool InternalHeaders::IsForbiddenRequestNoCorsHeader(
    const nsCString& aName) const {
  return mGuard == HeadersGuardEnum::Request_no_cors &&
         !nsContentUtils::IsCORSSafelistedRequestHeader(aName, ""_ns);
}

bool InternalHeaders::IsForbiddenRequestNoCorsHeader(
    const nsCString& aName, const nsACString& aValue) const {
  return mGuard == HeadersGuardEnum::Request_no_cors &&
         !nsContentUtils::IsCORSSafelistedRequestHeader(aName, aValue);
}

bool InternalHeaders::IsForbiddenResponseHeader(const nsCString& aName) const {
  return mGuard == HeadersGuardEnum::Response &&
         nsContentUtils::IsForbiddenResponseHeader(aName);
}

void InternalHeaders::Fill(const InternalHeaders& aInit, ErrorResult& aRv) {
  const nsTArray<Entry>& list = aInit.mList;
  for (uint32_t i = 0; i < list.Length() && !aRv.Failed(); ++i) {
    const Entry& entry = list[i];
    Append(entry.mName, entry.mValue, aRv);
  }
}

void InternalHeaders::Fill(const Sequence<Sequence<nsCString>>& aInit,
                           ErrorResult& aRv) {
  for (uint32_t i = 0; i < aInit.Length() && !aRv.Failed(); ++i) {
    const Sequence<nsCString>& tuple = aInit[i];
    if (tuple.Length() != 2) {
      aRv.ThrowTypeError(
          "Headers require name/value tuples when being initialized by a "
          "sequence.");
      return;
    }
    Append(tuple[0], tuple[1], aRv);
  }
}

void InternalHeaders::Fill(const Record<nsCString, nsCString>& aInit,
                           ErrorResult& aRv) {
  for (auto& entry : aInit.Entries()) {
    Append(entry.mKey, entry.mValue, aRv);
    if (aRv.Failed()) {
      return;
    }
  }
}

namespace {

class FillHeaders final : public nsIHttpHeaderVisitor {
  RefPtr<InternalHeaders> mInternalHeaders;

  ~FillHeaders() = default;

 public:
  NS_DECL_ISUPPORTS

  explicit FillHeaders(InternalHeaders* aInternalHeaders)
      : mInternalHeaders(aInternalHeaders) {
    MOZ_DIAGNOSTIC_ASSERT(mInternalHeaders);
  }

  NS_IMETHOD
  VisitHeader(const nsACString& aHeader, const nsACString& aValue) override {
    mInternalHeaders->Append(aHeader, aValue, IgnoreErrors());
    return NS_OK;
  }
};

NS_IMPL_ISUPPORTS(FillHeaders, nsIHttpHeaderVisitor)

}  // namespace

void InternalHeaders::FillResponseHeaders(nsIRequest* aRequest) {
  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aRequest);
  if (!httpChannel) {
    return;
  }

  RefPtr<FillHeaders> visitor = new FillHeaders(this);
  nsresult rv = httpChannel->VisitResponseHeaders(visitor);
  if (NS_FAILED(rv)) {
    NS_WARNING("failed to fill headers");
  }
}

bool InternalHeaders::HasOnlySimpleHeaders() const {
  for (uint32_t i = 0; i < mList.Length(); ++i) {
    if (!nsContentUtils::IsCORSSafelistedRequestHeader(mList[i].mName,
                                                       mList[i].mValue)) {
      return false;
    }
  }

  return true;
}

bool InternalHeaders::HasRevalidationHeaders() const {
  for (uint32_t i = 0; i < mList.Length(); ++i) {
    if (IsRevalidationHeader(mList[i].mName)) {
      return true;
    }
  }

  return false;
}

// static
already_AddRefed<InternalHeaders> InternalHeaders::BasicHeaders(
    InternalHeaders* aHeaders) {
  RefPtr<InternalHeaders> basic = new InternalHeaders(*aHeaders);
  ErrorResult result;
  // The Set-Cookie headers cannot be invalid mutable headers, so the Delete
  // must succeed.
  basic->Delete("Set-Cookie"_ns, result);
  MOZ_ASSERT(!result.Failed());
  basic->Delete("Set-Cookie2"_ns, result);
  MOZ_ASSERT(!result.Failed());
  return basic.forget();
}

// static
already_AddRefed<InternalHeaders> InternalHeaders::CORSHeaders(
    InternalHeaders* aHeaders, RequestCredentials aCredentialsMode) {
  RefPtr<InternalHeaders> cors = new InternalHeaders(aHeaders->mGuard);
  ErrorResult result;

  nsAutoCString acExposedNames;
  aHeaders->Get("Access-Control-Expose-Headers"_ns, acExposedNames, result);
  MOZ_ASSERT(!result.Failed());

  bool allowAllHeaders = false;
  AutoTArray<nsCString, 5> exposeNamesArray;
  for (const nsACString& token :
       nsCCharSeparatedTokenizer(acExposedNames, ',').ToRange()) {
    if (token.IsEmpty()) {
      continue;
    }

    if (!NS_IsValidHTTPToken(token)) {
      NS_WARNING(
          "Got invalid HTTP token in Access-Control-Expose-Headers. Header "
          "value is:");
      NS_WARNING(acExposedNames.get());
      exposeNamesArray.Clear();
      break;
    }

    if (token.EqualsLiteral("*") &&
        aCredentialsMode != RequestCredentials::Include) {
      allowAllHeaders = true;
    }

    exposeNamesArray.AppendElement(token);
  }

  nsCaseInsensitiveCStringArrayComparator comp;
  for (uint32_t i = 0; i < aHeaders->mList.Length(); ++i) {
    const Entry& entry = aHeaders->mList[i];
    if (allowAllHeaders) {
      cors->Append(entry.mName, entry.mValue, result);
      MOZ_ASSERT(!result.Failed());
    } else if (entry.mName.EqualsIgnoreCase("cache-control") ||
               entry.mName.EqualsIgnoreCase("content-language") ||
               entry.mName.EqualsIgnoreCase("content-type") ||
               entry.mName.EqualsIgnoreCase("content-length") ||
               entry.mName.EqualsIgnoreCase("expires") ||
               entry.mName.EqualsIgnoreCase("last-modified") ||
               entry.mName.EqualsIgnoreCase("pragma") ||
               exposeNamesArray.Contains(entry.mName, comp)) {
      cors->Append(entry.mName, entry.mValue, result);
      MOZ_ASSERT(!result.Failed());
    }
  }

  return cors.forget();
}

void InternalHeaders::GetEntries(
    nsTArray<InternalHeaders::Entry>& aEntries) const {
  MOZ_ASSERT(aEntries.IsEmpty());
  aEntries.AppendElements(mList);
}

void InternalHeaders::GetUnsafeHeaders(nsTArray<nsCString>& aNames) const {
  MOZ_ASSERT(aNames.IsEmpty());

  // https://fetch.spec.whatwg.org/#cors-unsafe-request-header-names
  AutoTArray<nsCString, 5> unsafeNames;
  AutoTArray<nsCString, 5> potentiallyUnsafeNames;
  size_t safelistValueSize = 0;

  for (const Entry& header : mList) {
    if (!nsContentUtils::IsCORSSafelistedRequestHeader(header.mName,
                                                       header.mValue)) {
      unsafeNames.AppendElement(header.mName);
    } else {
      potentiallyUnsafeNames.AppendElement(header.mName);
      safelistValueSize += header.mValue.Length();
    }
  }

  if (safelistValueSize > 1024) {
    unsafeNames.AppendElements(std::move(potentiallyUnsafeNames));
  }

  // Convert header names to a sorted-lowercase set.
  for (nsCString& name : unsafeNames) {
    ToLowerCase(name);
  }
  unsafeNames.Sort();
  for (nsCString& name : unsafeNames) {
    if (aNames.IsEmpty() || aNames.LastElement() != name) {
      aNames.AppendElement(std::move(name));
    }
  }
}

void InternalHeaders::MaybeSortList() {
  class Comparator {
   public:
    bool Equals(const Entry& aA, const Entry& aB) const {
      return aA.mName == aB.mName;
    }

    bool LessThan(const Entry& aA, const Entry& aB) const {
      return aA.mName < aB.mName;
    }
  };

  if (!mListDirty) {
    return;
  }

  mListDirty = false;

  Comparator comparator;

  mSortedList.Clear();
  for (const Entry& entry : mList) {
    bool found = false;

    // We combine every header but Set-Cookie.
    if (!entry.mName.EqualsIgnoreCase("Set-Cookie")) {
      for (Entry& sortedEntry : mSortedList) {
        if (sortedEntry.mName.EqualsIgnoreCase(entry.mName.get())) {
          sortedEntry.mValue += ", ";
          sortedEntry.mValue += entry.mValue;
          found = true;
          break;
        }
      }
    }

    if (!found) {
      Entry newEntry = entry;
      ToLowerCase(newEntry.mName);
      mSortedList.InsertElementSorted(newEntry, comparator);
    }
  }
}

void InternalHeaders::SetListDirty() {
  mSortedList.Clear();
  mListDirty = true;
}

void InternalHeaders::ReuseExistingNameIfExists(nsCString& aName) const {
  for (const Entry& entry : mList) {
    if (entry.mName.EqualsIgnoreCase(aName.get())) {
      aName = entry.mName;
      break;
    }
  }
}

}  // namespace mozilla::dom
