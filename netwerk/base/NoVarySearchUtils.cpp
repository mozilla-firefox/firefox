/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "NoVarySearchUtils.h"

#include <algorithm>

#include "mozilla/net/SFV.h"
#include "nsIURI.h"
#include "nsURLHelper.h"

namespace mozilla::net {

/**
 * Extracts string values from an SFV InnerListResult into aOut, skipping
 * non-string items. Used to collect parameter names from the "params" and
 * "except" fields of a No-Vary-Search header.
 *
 * @param aList  SFV InnerListResult to read items from.
 * @param aOut   [out] Receives the extracted string values.
 */
static void CollectInnerListStrings(const SFV::InnerListResult& aList,
                                    nsTArray<nsCString>& aOut) {
  for (size_t i = 0; i < aList.Length(); i++) {
    nsAutoCString val;
    if (NS_SUCCEEDED(aList.GetItemAt(i).GetValue<SFV::SFVString>(val))) {
      aOut.AppendElement(val);
    }
  }
}

NoVarySearchData ParseNoVarySearchHeader(const nsACString& aHeader,
                                         bool* aParseError) {
  NoVarySearchData data;
  if (aHeader.IsEmpty()) {
    return data;
  }

  auto dict = SFV::ParseDict(aHeader);
  if (!dict.IsValid()) {
    if (aParseError) {
      *aParseError = true;
    }
    return data;  // spec §5.1: parse error → default config (ExactMatch)
  }

  // key-order: if present and true, query parameter order is insignificant.
  // https://www.ietf.org/archive/id/draft-ietf-httpbis-no-vary-search-05.html#section-5.1-4
  bool keyOrder = false;
  if (NS_SUCCEEDED(dict.GetItem<SFV::SFVBool>("key-order"_ns, keyOrder))) {
    data.varyOnKeyOrder = !keyOrder;
  }

  // "params" may be a boolean item or an inner list of strings (§5.1 steps
  // 5-6):
  // https://www.ietf.org/archive/id/draft-ietf-httpbis-no-vary-search-05.html#section-5.1-6
  //   params=?1            → IgnoreAll (or Allowlist when except is present)
  //   params=(...)         → Blocklist
  //   params=?1 + except   → Allowlist
  //   params=(...) + except → invalid → ExactMatch
  //   except without params → invalid → ExactMatch
  bool paramsBool = false;
  bool paramsIsBool =
      NS_SUCCEEDED(dict.GetItem<SFV::SFVBool>("params"_ns, paramsBool));
  auto paramsInnerList = dict.GetInnerList("params"_ns);

  auto exceptInnerList = dict.GetInnerList("except"_ns);
  bool hasExcept = exceptInnerList.IsValid();

  if (paramsIsBool && paramsBool) {
    if (hasExcept) {
      // params=?1 + except=(...) → Allowlist
      // https://www.ietf.org/archive/id/draft-ietf-httpbis-no-vary-search-05.html#section-5.1-7
      data.paramsRule = NoVarySearchData::ParamsRule::Allowlist;
      CollectInnerListStrings(exceptInnerList, data.paramNames);
    } else {
      data.paramsRule = NoVarySearchData::ParamsRule::IgnoreAll;
    }
  } else if (paramsInnerList.IsValid()) {
    if (hasExcept) {
      if (aParseError) {
        *aParseError = true;
      }
      return NoVarySearchData{};  // params=(...) + except → invalid →
                                  // ExactMatch
    }
    data.paramsRule = NoVarySearchData::ParamsRule::Blocklist;
    CollectInnerListStrings(paramsInnerList, data.paramNames);
  } else if (hasExcept) {
    if (aParseError) {
      *aParseError = true;
    }
    return NoVarySearchData{};  // except without valid params → invalid →
                                // ExactMatch
  }

  return data;
}

nsLiteralCString NoVarySearchRuleLabel(NoVarySearchData::ParamsRule aRule) {
  switch (aRule) {
    case NoVarySearchData::ParamsRule::ExactMatch:
      return "exact_match"_ns;
    case NoVarySearchData::ParamsRule::IgnoreAll:
      return "ignore_all"_ns;
    case NoVarySearchData::ParamsRule::Blocklist:
      return "blocklist"_ns;
    case NoVarySearchData::ParamsRule::Allowlist:
      return "allowlist"_ns;
    default:
      MOZ_ASSERT_UNREACHABLE("Unknown NoVarySearch ParamsRule");
      return "exact_match"_ns;
  }
}

using Param = std::pair<nsCString, nsCString>;

/**
 * Parses an application/x-www-form-urlencoded query string into an ordered
 * list of (name, value) pairs.
 *
 * @param aQuery  Query string without the leading '?'.
 * @return        Ordered list of (name, value) pairs.
 */
static nsTArray<Param> ParseQueryParams(const nsACString& aQuery) {
  nsTArray<Param> params;
  URLParams::Parse(
      aQuery, true, [&params](nsCString&& name, nsCString&& value) {
        params.AppendElement(Param{std::move(name), std::move(value)});
        return true;
      });
  return params;
}

/**
 * Filters a list of query (name, value) pairs according to the No-Vary-Search
 * variation config, removing parameters that should not affect URL equivalence.
 *
 * @param aParams  [in/out] Parameter list to filter in place.
 * @param aData    Parsed URL variation config controlling which params to keep.
 */
static void FilterParams(nsTArray<Param>& aParams,
                         const NoVarySearchData& aData) {
  switch (aData.paramsRule) {
    case NoVarySearchData::ParamsRule::IgnoreAll:
      aParams.Clear();
      break;
    case NoVarySearchData::ParamsRule::Blocklist:
      aParams.RemoveElementsBy(
          [&](const Param& p) { return aData.paramNames.Contains(p.first); });
      break;
    case NoVarySearchData::ParamsRule::Allowlist:
      aParams.RemoveElementsBy(
          [&](const Param& p) { return !aData.paramNames.Contains(p.first); });
      break;
    case NoVarySearchData::ParamsRule::ExactMatch:
      break;
  }
}

bool URLsAreEquivalentModuloVariationConfig(nsIURI* aURIA, nsIURI* aURIB,
                                            const NoVarySearchData& aData) {
  // Step 1: scheme, host, and port must match exactly.
  // https://www.ietf.org/archive/id/draft-ietf-httpbis-no-vary-search-05.html#section-6-1
  nsAutoCString prePathA, prePathB;
  aURIA->GetPrePath(prePathA);
  aURIB->GetPrePath(prePathB);
  if (!prePathA.Equals(prePathB)) {
    return false;
  }

  // Path must also match exactly — NVS only varies query parameters.
  nsAutoCString filePathA, filePathB;
  aURIA->GetFilePath(filePathA);
  aURIB->GetFilePath(filePathB);
  if (!filePathA.Equals(filePathB)) {
    return false;
  }

  // Fast path: no param filtering and order matters → compare query strings
  // directly.
  // https://www.ietf.org/archive/id/draft-ietf-httpbis-no-vary-search-05.html#section-6-2
  if (aData.paramsRule == NoVarySearchData::ParamsRule::ExactMatch &&
      aData.varyOnKeyOrder) {
    nsAutoCString queryA, queryB;
    aURIA->GetQuery(queryA);
    aURIB->GetQuery(queryB);
    return queryA.Equals(queryB);
  }

  // Steps 3-5: parse query strings into (name, value) pairs.
  // https://www.ietf.org/archive/id/draft-ietf-httpbis-no-vary-search-05.html#section-6-3
  nsAutoCString queryA, queryB;
  aURIA->GetQuery(queryA);
  aURIB->GetQuery(queryB);

  nsTArray<Param> paramsA = ParseQueryParams(queryA);
  nsTArray<Param> paramsB = ParseQueryParams(queryB);

  // Steps 6-7: remove parameters that should not affect equivalence.
  // https://www.ietf.org/archive/id/draft-ietf-httpbis-no-vary-search-05.html#section-6-6
  FilterParams(paramsA, aData);
  FilterParams(paramsB, aData);

  // Step 8: if key-order=?1, sort both lists by parameter name so that
  // reordered parameters compare as equal.
  // https://www.ietf.org/archive/id/draft-ietf-httpbis-no-vary-search-05.html#section-6-8
  if (!aData.varyOnKeyOrder) {
    auto cmp = [](const Param& a, const Param& b) { return a.first < b.first; };
    std::stable_sort(paramsA.begin(), paramsA.end(), cmp);
    std::stable_sort(paramsB.begin(), paramsB.end(), cmp);
  }

  // Steps 9-11: compare filtered (and optionally sorted) parameter lists.
  // https://www.ietf.org/archive/id/draft-ietf-httpbis-no-vary-search-05.html#section-6-9
  if (paramsA.Length() != paramsB.Length()) {
    return false;
  }

  for (size_t i = 0; i < paramsA.Length(); ++i) {
    if (paramsA[i].first != paramsB[i].first ||
        paramsA[i].second != paramsB[i].second) {
      return false;
    }
  }

  return true;
}

nsresult ExtractNoVarySearchBasePath(nsIURI* aURI, nsACString& aBasePath) {
  MOZ_ASSERT(aURI, "aURI must not be null");
  if (!aURI) {
    return NS_ERROR_INVALID_ARG;
  }
  nsAutoCString prePath, filePath;
  MOZ_TRY(aURI->GetPrePath(prePath));
  MOZ_TRY(aURI->GetFilePath(filePath));
  aBasePath.Assign(prePath);
  aBasePath.Append(filePath);
  return NS_OK;
}

}  // namespace mozilla::net
