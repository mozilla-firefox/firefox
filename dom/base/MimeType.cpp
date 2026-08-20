/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MimeType.h"

#include "nsNetUtil.h"
#include "nsUnicharUtils.h"

template <typename char_type>
/* static */ RefPtr<TMimeType<char_type>> TMimeType<char_type>::Parse(
    const nsTSubstring<char_type>& aMimeType) {
  // See https://mimesniff.spec.whatwg.org/#parsing-a-mime-type

  // Steps 1-2
  const char_type* pos = aMimeType.BeginReading();
  const char_type* end = aMimeType.EndReading();
  while (pos < end && NS_IsHTTPWhitespace(*pos)) {
    ++pos;
  }
  if (pos == end) {
    return nullptr;
  }
  while (end > pos && NS_IsHTTPWhitespace(*(end - 1))) {
    --end;
  }

  // Steps 3-4
  const char_type* typeStart = pos;
  while (pos < end && *pos != '/') {
    if (!NS_IsHTTPTokenPoint(*pos)) {
      return nullptr;
    }
    ++pos;
  }
  const char_type* typeEnd = pos;
  if (typeStart == typeEnd) {
    return nullptr;
  }

  // Step 5
  if (pos == end) {
    return nullptr;
  }

  // Step 6
  ++pos;

  // Step 7-9
  const char_type* subtypeStart = pos;
  const char_type* subtypeEnd = nullptr;
  while (pos < end && *pos != ';') {
    if (!NS_IsHTTPTokenPoint(*pos)) {
      // If we hit a whitespace, check that the rest of
      // the subtype is whitespace, otherwise fail.
      if (NS_IsHTTPWhitespace(*pos)) {
        subtypeEnd = pos;
        ++pos;
        while (pos < end && *pos != ';') {
          if (!NS_IsHTTPWhitespace(*pos)) {
            return nullptr;
          }
          ++pos;
        }
        break;
      }
      return nullptr;
    }
    ++pos;
  }
  if (subtypeEnd == nullptr) {
    subtypeEnd = pos;
  }
  if (subtypeStart == subtypeEnd) {
    return nullptr;
  }

  // Step 10
  nsTString<char_type> type;
  nsTString<char_type> subtype;
  for (const char_type* c = typeStart; c < typeEnd; ++c) {
    type.Append(ToLowerCaseASCII(*c));
  }
  for (const char_type* c = subtypeStart; c < subtypeEnd; ++c) {
    subtype.Append(ToLowerCaseASCII(*c));
  }
  RefPtr<TMimeType<char_type>> mimeType =
      new TMimeType<char_type>(type, subtype);

  // Step 11
  while (pos < end) {
    // Step 11.1
    ++pos;

    // Step 11.2
    while (pos < end && NS_IsHTTPWhitespace(*pos)) {
      ++pos;
    }

    const char_type* namePos = pos;

    // Steps 11.3 and 11.4
    nsTString<char_type> paramName;
    bool paramNameHadInvalidChars = false;
    while (pos < end && *pos != ';' && *pos != '=') {
      if (!NS_IsHTTPTokenPoint(*pos)) {
        paramNameHadInvalidChars = true;
      }
      paramName.Append(ToLowerCaseASCII(*pos));
      ++pos;
    }

    // Might as well check for base64 now. Note that the loop above may have
    // stopped because it reached the end of the input, in which case there is
    // no code point to look at.
    if (pos == end || *pos != '=') {
      // trim leading and trailing spaces
      while (namePos < pos && NS_IsHTTPWhitespace(*namePos)) {
        ++namePos;
      }
      if (namePos < pos && ToLowerCaseASCII(*namePos) == 'b' &&
          ++namePos < pos && ToLowerCaseASCII(*namePos) == 'a' &&
          ++namePos < pos && ToLowerCaseASCII(*namePos) == 's' &&
          ++namePos < pos && ToLowerCaseASCII(*namePos) == 'e' &&
          ++namePos < pos && ToLowerCaseASCII(*namePos) == '6' &&
          ++namePos < pos && ToLowerCaseASCII(*namePos) == '4') {
        while (++namePos < pos && NS_IsHTTPWhitespace(*namePos)) {
        }
        mimeType->mIsBase64 = namePos == pos;
      }
    }

    // Step 11.5
    if (pos < end) {
      if (*pos == ';') {
        continue;
      }
      ++pos;
    }

    // Step 11.6
    if (pos == end) {
      break;
    }

    // Step 11.7
    ParameterValue paramValue;
    bool paramValueHadInvalidChars = false;

    // Step 11.8
    if (*pos == '"') {
      // Step 11.8.1
      ++pos;

      // Step 11.8.2
      while (true) {
        // Step 11.8.2.1
        while (pos < end && *pos != '"' && *pos != '\\') {
          if (!NS_IsHTTPQuotedStringTokenPoint(*pos)) {
            paramValueHadInvalidChars = true;
          }
          if (!NS_IsHTTPTokenPoint(*pos)) {
            paramValue.mRequiresQuoting = true;
          }
          paramValue.Append(*pos);
          ++pos;
        }

        // Step 11.8.2.2
        if (pos < end && *pos == '\\') {
          // Step 11.8.2.2.1
          ++pos;

          // Step 11.8.2.2.2
          if (pos < end) {
            if (!NS_IsHTTPQuotedStringTokenPoint(*pos)) {
              paramValueHadInvalidChars = true;
            }
            if (!NS_IsHTTPTokenPoint(*pos)) {
              paramValue.mRequiresQuoting = true;
            }
            paramValue.Append(*pos);
            ++pos;
            continue;
          }

          // Step 11.8.2.2.3
          paramValue.Append('\\');
          paramValue.mRequiresQuoting = true;
        }

        // Step 11.8.2.3
        break;
      }

      // Step 11.8.3
      while (pos < end && *pos != ';') {
        ++pos;
      }

      // Step 11.9
    } else {
      // Step 11.9.1
      const char_type* paramValueStart = pos;
      while (pos < end && *pos != ';') {
        ++pos;
      }

      // Step 11.9.2
      const char_type* paramValueLastChar = pos - 1;
      while (paramValueLastChar >= paramValueStart &&
             NS_IsHTTPWhitespace(*paramValueLastChar)) {
        --paramValueLastChar;
      }

      // Step 11.9.3
      if (paramValueStart > paramValueLastChar) {
        continue;
      }

      for (const char_type* c = paramValueStart; c <= paramValueLastChar; ++c) {
        if (!NS_IsHTTPQuotedStringTokenPoint(*c)) {
          paramValueHadInvalidChars = true;
        }
        if (!NS_IsHTTPTokenPoint(*c)) {
          paramValue.mRequiresQuoting = true;
        }
        paramValue.Append(*c);
      }
    }

    // Step 11.10
    if (!paramName.IsEmpty() && !paramNameHadInvalidChars &&
        !paramValueHadInvalidChars) {
      // XXX Is the assigned value used anywhere?
      paramValue = mimeType->mParameters.LookupOrInsertWith(paramName, [&] {
        mimeType->mParameterNames.AppendElement(paramName);
        return paramValue;
      });
    }
  }

  // Step 12
  return mimeType;
}

template <typename char_type>
/* static */ nsTArray<nsTDependentSubstring<char_type>>
TMimeType<char_type>::SplitMimetype(const nsTSubstring<char_type>& aMimeType) {
  nsTArray<nsTDependentSubstring<char_type>> mimeTypeParts;
  bool inQuotes = false;
  size_t start = 0;

  for (size_t i = 0; i < aMimeType.Length(); i++) {
    char_type c = aMimeType[i];

    // https://fetch.spec.whatwg.org/#collect-an-http-quoted-string : a
    // backslash only escapes inside a quoted string, and it consumes the code
    // point that follows it, so an escaped backslash does not escape the next
    // character.
    if (inQuotes && c == '\\') {
      ++i;
    } else if (c == '"') {
      inQuotes = !inQuotes;
    } else if (c == ',' && !inQuotes) {
      mimeTypeParts.AppendElement(Substring(aMimeType, start, i - start));
      start = i + 1;
    }
  }
  if (start < aMimeType.Length()) {
    mimeTypeParts.AppendElement(Substring(aMimeType, start));
  }
  return mimeTypeParts;
}

template <typename char_type>
/* static */ bool TMimeType<char_type>::Parse(
    const nsTSubstring<char_type>& aMimeType,
    nsTSubstring<char_type>& aOutEssence,
    nsTSubstring<char_type>& aOutCharset) {
  // https://fetch.spec.whatwg.org/#concept-header-extract-mime-type
  static char_type kCHARSET[] = {'c', 'h', 'a', 'r', 's', 'e', 't'};
  static nsTDependentSubstring<char_type> kCharset(kCHARSET, 7);

  RefPtr<TMimeType<char_type>> parsed;
  nsTAutoString<char_type> prevContentType;
  nsTAutoString<char_type> prevCharset;

  aOutEssence.Truncate();
  aOutCharset.Truncate();

  nsTArray<nsTDependentSubstring<char_type>> mimeTypeParts =
      SplitMimetype(aMimeType);

  for (auto& mimeTypeString : mimeTypeParts) {
    if (mimeTypeString.EqualsLiteral("error")) {
      continue;
    }

    parsed = Parse(mimeTypeString);

    if (!parsed) {
      continue;
    }

    parsed->GetEssence(aOutEssence);

    if (aOutEssence.EqualsLiteral("*/*")) {
      aOutEssence.Assign(prevContentType);
      continue;
    }

    bool eq = !prevContentType.IsEmpty() && aOutEssence.Equals(prevContentType);

    if (!eq) {
      prevContentType.Assign(aOutEssence);
    }

    bool typeHasCharset = false;
    if (parsed->GetParameterValue(kCharset, aOutCharset, false, false)) {
      typeHasCharset = true;
    } else if (eq) {
      aOutCharset.Assign(prevCharset);
    }

    if ((!eq && !prevCharset.IsEmpty()) || typeHasCharset) {
      prevCharset.Assign(aOutCharset);
    }
  }

  if (aOutEssence.IsEmpty()) {
    return false;
  }

  return true;
}

template <typename char_type>
void TMimeType<char_type>::Serialize(nsTSubstring<char_type>& aOutput) const {
  aOutput.Assign(mType);
  aOutput.Append('/');
  aOutput.Append(mSubtype);
  for (uint32_t i = 0; i < mParameterNames.Length(); i++) {
    auto name = mParameterNames[i];
    aOutput.Append(';');
    aOutput.Append(name);
    aOutput.Append('=');
    GetParameterValue(name, aOutput, true);
  }
}

template <typename char_type>
void TMimeType<char_type>::GetEssence(nsTSubstring<char_type>& aOutput) const {
  aOutput.Assign(mType);
  aOutput.Append('/');
  GetSubtype(aOutput);
}

template <typename char_type>
void TMimeType<char_type>::GetSubtype(nsTSubstring<char_type>& aOutput) const {
  aOutput.Append(mSubtype);
}

template <typename char_type>
bool TMimeType<char_type>::HasParameter(
    const nsTSubstring<char_type>& aName) const {
  return mParameters.Get(aName, nullptr);
}

template <typename char_type>
bool TMimeType<char_type>::GetParameterValue(
    const nsTSubstring<char_type>& aName, nsTSubstring<char_type>& aOutput,
    bool aAppend, bool aWithQuotes) const {
  if (!aAppend) {
    aOutput.Truncate();
  }

  ParameterValue value;
  if (!mParameters.Get(aName, &value)) {
    return false;
  }

  if (aWithQuotes && (value.mRequiresQuoting || value.IsEmpty())) {
    aOutput.Append('"');
    const char_type* vcur = value.BeginReading();
    const char_type* vend = value.EndReading();
    while (vcur < vend) {
      if (*vcur == '"' || *vcur == '\\') {
        aOutput.Append('\\');
      }
      aOutput.Append(*vcur);
      vcur++;
    }
    aOutput.Append('"');
  } else {
    aOutput.Append(value);
  }

  return true;
}

template <typename char_type>
void TMimeType<char_type>::SetParameterValue(
    const nsTSubstring<char_type>& aName,
    const nsTSubstring<char_type>& aValue) {
  mParameters.WithEntryHandle(aName, [&](auto&& entry) {
    if (!entry) {
      mParameterNames.AppendElement(aName);
    }
    ParameterValue value;
    value.Append(aValue);
    entry.InsertOrUpdate(std::move(value));
  });
}

template RefPtr<TMimeType<char16_t>> TMimeType<char16_t>::Parse(
    const nsTSubstring<char16_t>& aMimeType);
template RefPtr<TMimeType<char>> TMimeType<char>::Parse(
    const nsTSubstring<char>& aMimeType);
template bool TMimeType<char16_t>::Parse(
    const nsTSubstring<char16_t>& aMimeType,
    nsTSubstring<char16_t>& aOutEssence, nsTSubstring<char16_t>& aOutCharset);
template bool TMimeType<char>::Parse(const nsTSubstring<char>& aMimeType,
                                     nsTSubstring<char>& aOutEssence,
                                     nsTSubstring<char>& aOutCharset);
template nsTArray<nsTDependentSubstring<char>> TMimeType<char>::SplitMimetype(
    const nsTSubstring<char>& aMimeType);
template void TMimeType<char16_t>::Serialize(
    nsTSubstring<char16_t>& aOutput) const;
template void TMimeType<char>::Serialize(nsTSubstring<char>& aOutput) const;
template void TMimeType<char16_t>::GetEssence(
    nsTSubstring<char16_t>& aOutput) const;
template void TMimeType<char>::GetEssence(nsTSubstring<char>& aOutput) const;
template void TMimeType<char16_t>::GetSubtype(
    nsTSubstring<char16_t>& aOutput) const;
template void TMimeType<char>::GetSubtype(nsTSubstring<char>& aOutput) const;
template bool TMimeType<char16_t>::HasParameter(
    const nsTSubstring<char16_t>& aName) const;
template bool TMimeType<char>::HasParameter(
    const nsTSubstring<char>& aName) const;
template bool TMimeType<char16_t>::GetParameterValue(
    const nsTSubstring<char16_t>& aName, nsTSubstring<char16_t>& aOutput,
    bool aAppend, bool aWithQuotes) const;
template bool TMimeType<char>::GetParameterValue(
    const nsTSubstring<char>& aName, nsTSubstring<char>& aOutput, bool aAppend,
    bool aWithQuotes) const;
template void TMimeType<char16_t>::SetParameterValue(
    const nsTSubstring<char16_t>& aName, const nsTSubstring<char16_t>& aValue);
template void TMimeType<char>::SetParameterValue(
    const nsTSubstring<char>& aName, const nsTSubstring<char>& aValue);
