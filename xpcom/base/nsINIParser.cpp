/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Moz headers (alphabetical)
#include "nsINIParser.h"

#include "mozilla/Try.h"
#include "mozilla/URLPreloader.h"
#include "nsCRTGlue.h"
#include "nsError.h"
#include "nsIFile.h"

using namespace mozilla;

nsresult nsINIParser::Init(nsIFile* aFile, bool* aContainedErrors) {
  nsCString result = MOZ_TRY(URLPreloader::ReadFile(aFile));

  return InitFromString(result, aContainedErrors);
}

static const char kNL[] = "\r\n";
static const char kEquals[] = "=";
static const char kWhitespace[] = " \t";
static const char kRBracket[] = "]";

nsresult nsINIParser::InitFromString(const nsCString& aStr,
                                     bool* aContainedErrors) {
  nsCString fileContents;
  char* buffer;

  if (aContainedErrors) {
    *aContainedErrors = false;
  }

  if (StringHead(aStr, 3) == "\xEF\xBB\xBF") {
    // Someone set us up the Utf-8 BOM
    // This case is easy, since we assume that BOM-less
    // files are Utf-8 anyway.  Just skip the BOM and process as usual.
    fileContents.Append(aStr);
    buffer = fileContents.BeginWriting() + 3;
  } else {
    if (StringHead(aStr, 2) == "\xFF\xFE") {
      // Someone set us up the Utf-16LE BOM
      nsDependentSubstring str(reinterpret_cast<const char16_t*>(aStr.get()),
                               aStr.Length() / 2);

      AppendUTF16toUTF8(Substring(str, 1), fileContents);
    } else {
      fileContents.Append(aStr);
    }

    buffer = fileContents.BeginWriting();
  }

  char* currSection = nullptr;

  // outer loop tokenizes into lines
  while (char* token = NS_strtok(kNL, &buffer)) {
    if (token[0] == '#' || token[0] == ';') {  // it's a comment
      continue;
    }

    token = (char*)NS_strspnp(kWhitespace, token);
    if (!*token) {  // empty line
      continue;
    }

    if (token[0] == '[') {  // section header!
      ++token;
      currSection = token;

      char* rb = NS_strtok(kRBracket, &token);
      if (!rb || NS_strtok(kWhitespace, &token)) {
        // there's either an unclosed [Section or a [Section]Moretext!
        // we could frankly decide that this INI file is malformed right
        // here and stop, but we won't... keep going, looking for
        // a well-formed [section] to continue working with
        currSection = nullptr;
        if (aContainedErrors) {
          *aContainedErrors = true;
        }
      }

      continue;
    }

    if (!currSection) {
      // If we haven't found a section header (or we found a malformed
      // section header), don't bother parsing this line.

      if (aContainedErrors) {
        *aContainedErrors = true;
      }

      continue;
    }

    char* key = token;
    char* e = NS_strtok(kEquals, &token);
    if (!e || !token) {
      if (aContainedErrors) {
        *aContainedErrors = true;
      }

      continue;
    }

    if (NS_FAILED(SetString(currSection, key, token)) && aContainedErrors) {
      *aContainedErrors = true;
    }
  }

  return NS_OK;
}

bool nsINIParser::IsValidSection(const char* aSection) {
  if (aSection[0] == '\0') {
    return false;
  }

  const char* found = strpbrk(aSection, "\r\n[]");
  return found == nullptr;
}

bool nsINIParser::IsValidKey(const char* aKey) {
  if (aKey[0] == '\0') {
    return false;
  }

  const char* found = strpbrk(aKey, "\r\n=");
  return found == nullptr;
}

bool nsINIParser::IsValidValue(const char* aValue) {
  const char* found = strpbrk(aValue, "\r\n");
  return found == nullptr;
}

nsresult nsINIParser::GetString(const char* aSection, const char* aKey,
                                nsACString& aResult) {
  if (!IsValidSection(aSection) || !IsValidKey(aKey)) {
    return NS_ERROR_INVALID_ARG;
  }

  INIValue* val;
  mSections.Get(aSection, &val);

  while (val) {
    if (strcmp(val->key, aKey) == 0) {
      aResult.Assign(val->value);
      return NS_OK;
    }

    val = val->next.get();
  }

  return NS_ERROR_FAILURE;
}

nsresult nsINIParser::GetString(const char* aSection, const char* aKey,
                                char* aResult, uint32_t aResultLen) {
  if (!IsValidSection(aSection) || !IsValidKey(aKey)) {
    return NS_ERROR_INVALID_ARG;
  }

  INIValue* val;
  mSections.Get(aSection, &val);

  while (val) {
    if (strcmp(val->key, aKey) == 0) {
      strncpy(aResult, val->value, aResultLen);
      aResult[aResultLen - 1] = '\0';
      if (strlen(val->value) >= aResultLen) {
        return NS_ERROR_LOSS_OF_SIGNIFICANT_DATA;
      }

      return NS_OK;
    }

    val = val->next.get();
  }

  return NS_ERROR_FAILURE;
}

nsresult nsINIParser::GetSections(
    std::function<bool(const char*)>&& aCallback) {
  for (const auto& key : mSections.Keys()) {
    if (!aCallback(key)) {
      break;
    }
  }
  return NS_OK;
}

nsresult nsINIParser::GetStrings(
    const char* aSection,
    std::function<bool(const char*, const char*)>&& aCallback) {
  if (!IsValidSection(aSection)) {
    return NS_ERROR_INVALID_ARG;
  }

  INIValue* val;

  for (mSections.Get(aSection, &val); val; val = val->next.get()) {
    if (!aCallback(val->key, val->value)) {
      return NS_OK;
    }
  }

  return NS_OK;
}

nsresult nsINIParser::SetString(const char* aSection, const char* aKey,
                                const char* aValue) {
  if (!IsValidSection(aSection) || !IsValidKey(aKey) || !IsValidValue(aValue)) {
    return NS_ERROR_INVALID_ARG;
  }

  mSections.WithEntryHandle(aSection, [&](auto&& entry) {
    if (!entry) {
      entry.Insert(std::make_unique<INIValue>(aKey, aValue));
      return;
    }

    INIValue* v = entry->get();

    // Check whether this key has already been specified; overwrite
    // if so, or append if not.
    while (v) {
      if (!strcmp(aKey, v->key)) {
        v->SetValue(aValue);
        break;
      }
      if (!v->next) {
        v->next = std::make_unique<INIValue>(aKey, aValue);
        break;
      }
      v = v->next.get();
    }
    NS_ASSERTION(v, "v should never be null coming out of this loop");
  });

  return NS_OK;
}

nsresult nsINIParser::DeleteString(const char* aSection, const char* aKey) {
  if (!IsValidSection(aSection) || !IsValidKey(aKey)) {
    return NS_ERROR_INVALID_ARG;
  }

  INIValue* val;
  if (!mSections.Get(aSection, &val)) {
    return NS_ERROR_FAILURE;
  }

  // Special case the first result
  if (strcmp(val->key, aKey) == 0) {
    if (!val->next) {
      mSections.Remove(aSection);
    } else {
      mSections.InsertOrUpdate(aSection, std::move(val->next));
    }
    return NS_OK;
  }

  while (val->next) {
    if (strcmp(val->next->key, aKey) == 0) {
      val->next = std::move(val->next->next);

      return NS_OK;
    }

    val = val->next.get();
  }

  return NS_ERROR_FAILURE;
}

nsresult nsINIParser::DeleteSection(const char* aSection) {
  if (!IsValidSection(aSection)) {
    return NS_ERROR_INVALID_ARG;
  }

  if (!mSections.Remove(aSection)) {
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

nsresult nsINIParser::RenameSection(const char* aSection,
                                    const char* aNewName) {
  if (!IsValidSection(aSection) || !IsValidSection(aNewName)) {
    return NS_ERROR_INVALID_ARG;
  }

  if (mSections.Contains(aNewName)) {
    return NS_ERROR_ILLEGAL_VALUE;
  }

  mozilla::UniquePtr<INIValue> val;
  if (mSections.Remove(aSection, &val)) {
    mSections.InsertOrUpdate(aNewName, std::move(val));
  } else {
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

nsresult nsINIParser::WriteToFile(nsIFile* aFile) {
  nsCString buffer;

  WriteToString(buffer);

  FILE* writeFile;
  nsresult rv = aFile->OpenANSIFileDesc("w", &writeFile);
  NS_ENSURE_SUCCESS(rv, rv);

  unsigned int length = buffer.Length();

  if (fwrite(buffer.get(), sizeof(char), length, writeFile) != length) {
    fclose(writeFile);
    return NS_ERROR_UNEXPECTED;
  }

  fclose(writeFile);
  return NS_OK;
}

void nsINIParser::WriteToString(nsACString& aOutput) {
  for (const auto& entry : mSections) {
    aOutput.AppendPrintf("[%s]\n", entry.GetKey());
    INIValue* val = entry.GetRawPtr();
    while (val) {
      aOutput.AppendPrintf("%s=%s\n", val->key, val->value);
      val = val->next.get();
    }
    aOutput.AppendLiteral("\n");
  }
}
