/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PermissionsPolicyParser.h"

#include "mozilla/BasePrincipal.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/Feature.h"
#include "mozilla/dom/PermissionsPolicyUtils.h"
#include "mozilla/dom/PolicyTokenizer.h"
#include "mozilla/dom/nsCSPParser.h"
#include "mozilla/net/SFV.h"
#include "nsIScriptError.h"
#include "nsIURI.h"
#include "nsNetUtil.h"

namespace mozilla::dom {

namespace {

void ReportToConsoleUnsupportedFeature(Document* aDocument,
                                       const nsString& aFeatureName) {
  if (!aDocument) {
    return;
  }

  AutoTArray<nsString, 1> params = {aFeatureName};

  nsContentUtils::ReportToConsole(
      nsIScriptError::warningFlag, "Permissions Policy"_ns, aDocument,
      PropertiesFile::SECURITY_PROPERTIES,
      "PermissionsPolicyUnsupportedFeatureName", params);
}

void ReportToConsoleInvalidEmptyAllowValue(Document* aDocument,
                                           const nsString& aFeatureName) {
  if (!aDocument) {
    return;
  }

  AutoTArray<nsString, 1> params = {aFeatureName};

  nsContentUtils::ReportToConsole(
      nsIScriptError::warningFlag, "Permissions Policy"_ns, aDocument,
      PropertiesFile::SECURITY_PROPERTIES,
      "PermissionsPolicyInvalidEmptyAllowValue", params);
}

void ReportToConsoleInvalidAllowValue(Document* aDocument,
                                      const nsString& aValue) {
  if (!aDocument) {
    return;
  }

  AutoTArray<nsString, 1> params = {aValue};

  nsContentUtils::ReportToConsole(nsIScriptError::warningFlag,
                                  "Permissions Policy"_ns, aDocument,
                                  PropertiesFile::SECURITY_PROPERTIES,
                                  "PermissionsPolicyInvalidAllowValue", params);
}

}  // namespace

/* static */
bool PermissionsPolicyParser::ParsePolicyFromAttribute(
    const nsAString& aPolicy, Document* aDocument, nsIPrincipal* aSelfOrigin,
    nsIPrincipal* aSrcOrigin, nsTArray<Feature>& aParsedFeatures) {
  MOZ_ASSERT(aSelfOrigin);

  nsTArray<CopyableTArray<nsString>> tokens;
  PolicyTokenizer::tokenizePolicy(aPolicy, tokens);

  nsTArray<Feature> parsedFeatures;

  for (const nsTArray<nsString>& featureTokens : tokens) {
    if (featureTokens.IsEmpty()) {
      continue;
    }

    if (!PermissionsPolicyUtils::IsSupportedFeature(featureTokens[0])) {
      ReportToConsoleUnsupportedFeature(aDocument, featureTokens[0]);
      continue;
    }

    Feature feature(featureTokens[0]);

    if (featureTokens.Length() == 1) {
      if (aSrcOrigin) {
        feature.AppendToAllowList(aSrcOrigin);
      } else {
        ReportToConsoleInvalidEmptyAllowValue(aDocument, featureTokens[0]);
        continue;
      }
    } else {
      // we gotta start at 1 here
      for (uint32_t i = 1; i < featureTokens.Length(); ++i) {
        const nsString& curVal = featureTokens[i];
        if (curVal.LowerCaseEqualsASCII("'none'")) {
          feature.SetAllowsNone();
          break;
        }

        if (curVal.EqualsLiteral("*")) {
          feature.SetAllowsAll();
          break;
        }

        if (curVal.LowerCaseEqualsASCII("'self'")) {
          feature.AppendToAllowList(aSelfOrigin);
          continue;
        }

        if (aSrcOrigin && curVal.LowerCaseEqualsASCII("'src'")) {
          feature.AppendToAllowList(aSrcOrigin);
          continue;
        }

        nsCOMPtr<nsIURI> uri;
        nsresult rv = NS_NewURI(getter_AddRefs(uri), curVal);
        if (NS_FAILED(rv)) {
          ReportToConsoleInvalidAllowValue(aDocument, curVal);
          continue;
        }

        nsCOMPtr<nsIPrincipal> origin = BasePrincipal::CreateContentPrincipal(
            uri, BasePrincipal::Cast(aSelfOrigin)->OriginAttributesRef());
        if (NS_WARN_IF(!origin)) {
          ReportToConsoleInvalidAllowValue(aDocument, curVal);
          continue;
        }

        feature.AppendToAllowList(origin);
      }
    }

    // No duplicate!
    bool found = false;
    for (const Feature& parsedFeature : parsedFeatures) {
      if (parsedFeature.Name() == feature.Name()) {
        found = true;
        break;
      }
    }

    if (!found) {
      parsedFeatures.AppendElement(feature);
    }
  }

  aParsedFeatures = std::move(parsedFeatures);
  return true;
}

static bool AppendOriginToFeature(const nsACString& aValue, Document* aDocument,
                                  nsIPrincipal* aSelfOrigin,
                                  Feature& aFeature) {
  NS_ConvertUTF8toUTF16 sourceExpression(aValue);
  UniquePtr<nsCSPBaseSrc> source =
      nsCSPParser::parseSingleSourceExpression(sourceExpression, aSelfOrigin);
  if (!source) {
    ReportToConsoleInvalidAllowValue(aDocument, sourceExpression);
    return false;
  }

  aFeature.AppendSourceExpression(std::move(source));
  return true;
}

/* static */
bool PermissionsPolicyParser::ParsePolicyFromHeader(
    const nsACString& aPolicy, Document* aDocument, nsIPrincipal* aSelfOrigin,
    nsTArray<Feature>& aParsedFeatures) {
  MOZ_ASSERT(aSelfOrigin);

  // 1. treats a malformed structured header as an empty policy.
  // https://w3c.github.io/webappsec-permissions-policy/#algo-process-response-policy
  aParsedFeatures.Clear();

  auto dictionary = net::SFV::ParseDict(aPolicy);
  if (!dictionary.IsValid()) {
    return false;
  }

  nsTArray<nsCString> keys;
  if (NS_FAILED(dictionary.GetKeys(keys))) {
    return false;
  }

  nsTArray<Feature> parsedFeatures;
  for (const nsCString& key : keys) {
    nsString featureName = NS_ConvertUTF8toUTF16(key);

    if (!PermissionsPolicyUtils::IsSupportedFeature(featureName)) {
      ReportToConsoleUnsupportedFeature(aDocument, featureName);
      continue;
    }

    Feature feature(featureName);
    auto innerList = dictionary.GetInnerList(key);

    // 2 handle SFV lists, e.g. camera=(self)
    if (innerList.IsValid()) {
      for (size_t i = 0; i < innerList.Length(); ++i) {
        auto item = innerList.GetItemAt(i);

        // 2.1 handle tokens such as * and self
        nsAutoCString token;
        if (NS_SUCCEEDED(item.GetValue<net::SFV::Token>(token))) {
          if (token.EqualsLiteral("*")) {
            feature.SetAllowsAll();
            break;
          }

          if (token.EqualsLiteral("self")) {
            feature.AppendToAllowList(aSelfOrigin);
          }

          continue;
        }

        // 2.2 Parse quoted allowlist entries including source expressions.
        nsAutoCString source;
        if (NS_SUCCEEDED(item.GetValue<net::SFV::SFVString>(source))) {
          AppendOriginToFeature(source, aDocument, aSelfOrigin, feature);
        }
      }

      parsedFeatures.AppendElement(std::move(feature));
      continue;
    }

    // 3. handle SFV individual values, e.g. camera=self
    nsAutoCString value;
    bool validValue = false;

    if (NS_SUCCEEDED(dictionary.GetItem<net::SFV::Token>(key, value))) {
      if (value.EqualsLiteral("*")) {
        feature.SetAllowsAll();
        validValue = true;
      } else if (value.EqualsLiteral("self")) {
        feature.AppendToAllowList(aSelfOrigin);
        validValue = true;
      }
    } else if (NS_SUCCEEDED(
                   dictionary.GetItem<net::SFV::SFVString>(key, value))) {
      validValue =
          AppendOriginToFeature(value, aDocument, aSelfOrigin, feature);
    }

    if (validValue) {
      parsedFeatures.AppendElement(std::move(feature));
    }
  }

  aParsedFeatures = std::move(parsedFeatures);
  return true;
}

}  // namespace mozilla::dom
