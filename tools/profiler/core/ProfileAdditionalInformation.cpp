/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ProfileAdditionalInformation.h"

#include "ipc/IPCMessageUtilsSpecializations.h"
#include "jsapi.h"
#include "js/JSON.h"
#include "js/PropertyAndElement.h"
#include "js/Value.h"
#include "mozilla/Assertions.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/JSONStringWriteFuncs.h"
#include "mozilla/Span.h"
#include "mozilla/ipc/BigBuffer.h"
#include "platform.h"
#include "mozilla/scache/StartupCacheUtils.h"
#include "nsDirectoryServiceDefs.h"
#include "nsIFile.h"
#include "nsIFileURL.h"
#include "nsNetUtil.h"

#include <cstring>

namespace {

// Collapses ".." / "." segments via the URL parser. Unlike nsIFile::Normalize()
// it does not resolve symlinks, which non-packaged builds use to point
// chrome/resource sources at the source tree from within the GRE directory.
already_AddRefed<nsIFile> CollapseRelativePathSegments(nsIFile* aFile) {
  nsAutoCString spec;
  nsCOMPtr<nsIURI> uri;
  nsCOMPtr<nsIFileURL> fileURL;
  nsCOMPtr<nsIFile> collapsed;
  if (NS_FAILED(NS_GetURLSpecFromActualFile(aFile, spec)) ||
      NS_FAILED(NS_NewURI(getter_AddRefs(uri), spec)) ||
      !(fileURL = do_QueryInterface(uri)) ||
      NS_FAILED(fileURL->GetFile(getter_AddRefs(collapsed)))) {
    return nullptr;
  }
  return collapsed.forget();
}

bool IsSourceFileWithinGreDir(nsIURI* aURI) {
  // We need to compare the actual backing file against the GRE directory. For
  // nested URIs, NS_GetInnermostURI gives us the innermost backing URI. For
  // example, jar:file:///path/omni.ja!/foo.js unwraps to file:///path/omni.ja.
  // Non-nested URIs are returned unchanged, so plain file: URIs already work.
  // chrome:, resource:, and moz-src: URIs are also non-nested, but they are
  // aliases for another URI through the chrome registry or a substituting
  // protocol handler. Resolve them to their concrete file: or jar: target
  // before unwrapping.
  nsCOMPtr<nsIURI> uri = aURI;
  nsCString scheme;
  if (NS_FAILED(uri->GetScheme(scheme))) {
    return false;
  }
  if (scheme.EqualsLiteral("chrome") || scheme.EqualsLiteral("resource") ||
      scheme.EqualsLiteral("moz-src")) {
    nsCOMPtr<nsIURI> resolved;
    if (NS_FAILED(mozilla::scache::ResolveURI(uri, getter_AddRefs(resolved))) ||
        !resolved) {
      return false;
    }
    uri = std::move(resolved);
  }

  nsCOMPtr<nsIURI> innermost = NS_GetInnermostURI(uri);
  nsCOMPtr<nsIFileURL> fileURL = do_QueryInterface(innermost);
  nsCOMPtr<nsIFile> scriptFile;
  if (!fileURL || NS_FAILED(fileURL->GetFile(getter_AddRefs(scriptFile)))) {
    return false;
  }
  nsCOMPtr<nsIFile> cleanFile = CollapseRelativePathSegments(scriptFile);
  nsCOMPtr<nsIFile> greDir;
  bool contains = false;
  if (!cleanFile ||
      NS_FAILED(NS_GetSpecialDirectory(NS_GRE_DIR, getter_AddRefs(greDir))) ||
      NS_FAILED(greDir->Contains(cleanFile, &contains))) {
    return false;
  }
  return contains;
}

}  // namespace

JSString* mozilla::ProfileGenerationAdditionalInformation::
    MaybeCreateJSStringFromSourceData(
        JSContext* aCx, const ProfilerJSSourceData& aSourceData) const {
  JS::Rooted<JSString*> result(aCx);
  aSourceData.data().match(
      [&](const ProfilerJSSourceData::SourceTextUTF16& srcText) {
        result =
            JS_NewUCStringCopyN(aCx, srcText.chars().get(), srcText.length());
      },
      [&](const ProfilerJSSourceData::SourceTextUTF8& srcText) {
        result = JS_NewStringCopyUTF8N(
            aCx, JS::UTF8Chars(srcText.chars().get(), srcText.length()));
      },
      [&](const ProfilerJSSourceData::RetrievableFile&) {
        const char* filename = aSourceData.filePath();
        // Keep it in sync with what ReadSourceFromFilename does.
        const char* arrow;
        while ((arrow = strstr(filename, " -> "))) {
          filename = arrow + strlen(" -> ");
        }

        nsCOMPtr<nsIURI> uri;
        if (NS_FAILED(
                NS_NewURI(getter_AddRefs(uri), nsDependentCString(filename)))) {
          return;
        }

        if (!IsSourceFileWithinGreDir(uri)) {
          return;
        }

        ProfilerJSSourceData retrievedData =
            js::RetrieveProfilerSourceContent(aCx, aSourceData.filePath());
        const auto& data = retrievedData.data();
        if (!data.is<ProfilerJSSourceData::SourceTextUTF8>()) {
          return;
        }

        const auto& srcText = data.as<ProfilerJSSourceData::SourceTextUTF8>();
        result = JS_NewStringCopyUTF8N(
            aCx, JS::UTF8Chars(srcText.chars().get(), srcText.length()));
      },
      [&](const ProfilerJSSourceData::Unavailable&) {});
  return result;
}

void mozilla::ProfileGenerationAdditionalInformation::ToJSValue(
    JSContext* aCx, JS::MutableHandle<JS::Value> aRetVal) const {
  // Get the shared libraries array.
  JS::Rooted<JS::Value> sharedLibrariesVal(aCx);
  {
    JSONStringWriteFunc<nsCString> buffer;
    JSONWriter w(buffer, JSONWriter::SingleLineStyle);
    w.StartArrayElement();
    AppendSharedLibraries(w, mSharedLibraries);
    w.EndArray();
    NS_ConvertUTF8toUTF16 buffer16(buffer.StringCRef());
    MOZ_ALWAYS_TRUE(JS_ParseJSON(aCx,
                                 static_cast<const char16_t*>(buffer16.get()),
                                 buffer16.Length(), &sharedLibrariesVal));
  }

  // Create jsSources object, mapping source ID to an object with sourceText,
  // url, and sourceMapURL fields for WebChannel.
  JS::Rooted<JSObject*> jsSourcesObj(aCx, JS_NewPlainObject(aCx));
  if (jsSourcesObj) {
    for (const auto& entry : mJSSourceEntries) {
      JS::Rooted<JSObject*> entryObj(aCx, JS_NewPlainObject(aCx));
      if (!entryObj) {
        continue;
      }

      // Only emit an entry if it has sourceText (for GET_JS_SOURCES) or
      // sourceMapURL (for GET_SOURCE_MAP). A url alone is not useful to
      // either webchannel operation.
      bool hasData = false;

      JSString* sourceStr =
          MaybeCreateJSStringFromSourceData(aCx, entry.sourceData);
      if (sourceStr) {
        JS::Rooted<JS::Value> sourceVal(aCx, JS::StringValue(sourceStr));
        JS_SetProperty(aCx, entryObj, "sourceText", sourceVal);
        hasData = true;
      }

      if (entry.sourceData.filePathLength() > 0) {
        JSString* urlStr = JS_NewStringCopyUTF8N(
            aCx, JS::UTF8Chars(entry.sourceData.filePath(),
                               entry.sourceData.filePathLength()));
        if (urlStr) {
          JS::Rooted<JS::Value> urlVal(aCx, JS::StringValue(urlStr));
          JS_SetProperty(aCx, entryObj, "url", urlVal);
        }
      }

      if (entry.sourceData.sourceMapURLLength() > 0) {
        JSString* sourceMapURLStr =
            JS_NewUCStringCopyN(aCx, entry.sourceData.sourceMapURL(),
                                entry.sourceData.sourceMapURLLength());
        if (sourceMapURLStr) {
          JS::Rooted<JS::Value> sourceMapURLVal(
              aCx, JS::StringValue(sourceMapURLStr));
          JS_SetProperty(aCx, entryObj, "sourceMapURL", sourceMapURLVal);
          hasData = true;
        }
      }

      if (hasData) {
        JS::Rooted<JS::Value> entryVal(aCx, JS::ObjectValue(*entryObj));
        JS_SetProperty(aCx, jsSourcesObj, PromiseFlatCString(entry.id).get(),
                       entryVal);
      }
    }
  }

  JS::Rooted<JSObject*> additionalInfoObj(aCx, JS_NewPlainObject(aCx));
  JS::Rooted<JS::Value> jsSourcesVal(aCx, JS::ObjectValue(*jsSourcesObj));
  JS_SetProperty(aCx, additionalInfoObj, "sharedLibraries", sharedLibrariesVal);
  JS_SetProperty(aCx, additionalInfoObj, "jsSources", jsSourcesVal);
  aRetVal.setObject(*additionalInfoObj);
}

namespace IPC {

DEFINE_IPC_SERIALIZER_WITH_FIELDS(SharedLibrary, mStart, mEnd, mOffset,
                                  mBreakpadId, mCodeId, mModuleName,
                                  mModulePath, mDebugName, mDebugPath, mVersion,
                                  mArch);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(SharedLibraryInfo, mEntries);

// Type tags for ProfilerJSSourceData IPC serialization
constexpr uint8_t kSourceTextUTF16Tag = 0;
constexpr uint8_t kSourceTextUTF8Tag = 1;
constexpr uint8_t kRetrievableFileTag = 2;
constexpr uint8_t kUnavailableTag = 3;

namespace {

// Everything in a JSSourceEntry except the source map URL and the source text,
// which are sent separately in one buffer shared by all the entries.
struct JSSourceEntryMetadata {
  nsCString id;
  uint32_t sourceId = 0;
  JS::UniqueChars filePath;
  size_t filePathLength = 0;
  uint32_t startLine = 1;
  uint32_t startColumn = 1;
  JS::UniqueTwoByteChars sourceMapURL;
  size_t sourceMapURLLength = 0;
  uint8_t typeTag = kUnavailableTag;
  size_t textLength = 0;
};

}  // namespace

// Bounded, overflow-safe read of a length-prefixed char buffer from an IPC
// message. The caller has already read aLength (as size_t) from the wire; this
// validates it, allocates, reads the payload, and null-terminates.
template <typename CharT>
static bool ReadSourceBuffer(
    IPC::MessageReader* aReader, size_t aLength,
    mozilla::UniquePtr<CharT[], JS::FreePolicy>* aOut) {
  constexpr size_t kMaxLength = (UINT32_MAX / sizeof(CharT)) - 1;
  if (aLength > kMaxLength) {
    return false;
  }
  uint32_t byteLen = static_cast<uint32_t>(aLength * sizeof(CharT));
  if (!aReader->HasBytesAvailable(byteLen)) {
    return false;
  }
  CharT* chars = static_cast<CharT*>(js_malloc((aLength + 1) * sizeof(CharT)));
  if (!chars) {
    return false;
  }
  if (!aReader->ReadBytesInto(chars, byteLen)) {
    js_free(chars);
    return false;
  }
  chars[aLength] = CharT(0);
  aOut->reset(chars);
  return true;
}

static size_t SourceTextCharSize(uint8_t aTypeTag) {
  switch (aTypeTag) {
    case kSourceTextUTF16Tag:
      return sizeof(char16_t);
    case kSourceTextUTF8Tag:
      return sizeof(char);
    default:
      return 0;
  }
}

static mozilla::Span<const uint8_t> SourceMapURLBytes(
    const ProfilerJSSourceData& aData) {
  if (aData.sourceMapURLLength() == 0) {
    return {};
  }
  return mozilla::Span(reinterpret_cast<const uint8_t*>(aData.sourceMapURL()),
                       aData.sourceMapURLLength() * sizeof(char16_t));
}

static mozilla::Span<const uint8_t> SourceTextBytes(
    const ProfilerJSSourceData& aData) {
  using Bytes = mozilla::Span<const uint8_t>;
  return aData.data().match(
      [](const ProfilerJSSourceData::SourceTextUTF16& aText) {
        return Bytes(reinterpret_cast<const uint8_t*>(aText.chars().get()),
                     aText.length() * sizeof(char16_t));
      },
      [](const ProfilerJSSourceData::SourceTextUTF8& aText) {
        return Bytes(reinterpret_cast<const uint8_t*>(aText.chars().get()),
                     aText.length() * sizeof(char));
      },
      [](const ProfilerJSSourceData::RetrievableFile&) { return Bytes(); },
      [](const ProfilerJSSourceData::Unavailable&) { return Bytes(); });
}

// Copies the next aLength chars out of aSourceBytes, and advances it past them.
// The caller must have checked that aSourceBytes is long enough.
template <typename CharT>
static bool CopySourceChars(mozilla::Span<const uint8_t>& aSourceBytes,
                            size_t aLength,
                            mozilla::UniquePtr<CharT[], JS::FreePolicy>* aOut) {
  CharT* chars = static_cast<CharT*>(js_malloc((aLength + 1) * sizeof(CharT)));
  if (!chars) {
    return false;
  }
  const size_t byteLength = aLength * sizeof(CharT);
  memcpy(chars, aSourceBytes.Elements(), byteLength);
  chars[aLength] = CharT(0);
  aOut->reset(chars);
  aSourceBytes = aSourceBytes.From(byteLength);
  return true;
}

// When aIncludeSourceBuffer is false, the source map URL is left out and the
// source text is sent as unavailable.
static void WriteSourceMetadata(MessageWriter* aWriter,
                                const mozilla::JSSourceEntry& aEntry,
                                bool aIncludeSourceBuffer) {
  const ProfilerJSSourceData& source = aEntry.sourceData;
  WriteParam(aWriter, aEntry.id);
  WriteParam(aWriter, source.sourceId());
  WriteParam(aWriter, source.filePathLength());
  if (source.filePathLength() > 0) {
    aWriter->WriteBytes(source.filePath(),
                        source.filePathLength() * sizeof(char));
  }

  WriteParam(aWriter, source.startLine());
  WriteParam(aWriter, source.startColumn());

  WriteParam(aWriter,
             aIncludeSourceBuffer ? source.sourceMapURLLength() : size_t(0));

  source.data().match(
      [&](const ProfilerJSSourceData::SourceTextUTF16& srcText) {
        if (!aIncludeSourceBuffer) {
          WriteParam(aWriter, kUnavailableTag);
          return;
        }
        WriteParam(aWriter, kSourceTextUTF16Tag);
        WriteParam(aWriter, srcText.length());
      },
      [&](const ProfilerJSSourceData::SourceTextUTF8& srcText) {
        if (!aIncludeSourceBuffer) {
          WriteParam(aWriter, kUnavailableTag);
          return;
        }
        WriteParam(aWriter, kSourceTextUTF8Tag);
        WriteParam(aWriter, srcText.length());
      },
      [&](const ProfilerJSSourceData::RetrievableFile&) {
        WriteParam(aWriter, kRetrievableFileTag);
      },
      [&](const ProfilerJSSourceData::Unavailable&) {
        WriteParam(aWriter, kUnavailableTag);
      });
}

static bool ReadSourceMetadata(MessageReader* aReader,
                               JSSourceEntryMetadata* aResult) {
  if (!ReadParam(aReader, &aResult->id) ||
      !ReadParam(aReader, &aResult->sourceId) ||
      !ReadParam(aReader, &aResult->filePathLength)) {
    return false;
  }
  if (aResult->filePathLength > 0 &&
      !ReadSourceBuffer(aReader, aResult->filePathLength, &aResult->filePath)) {
    return false;
  }

  if (!ReadParam(aReader, &aResult->startLine) ||
      !ReadParam(aReader, &aResult->startColumn)) {
    return false;
  }

  if (!ReadParam(aReader, &aResult->sourceMapURLLength) ||
      !ReadParam(aReader, &aResult->typeTag)) {
    return false;
  }
  switch (aResult->typeTag) {
    case kSourceTextUTF16Tag:
    case kSourceTextUTF8Tag:
      return ReadParam(aReader, &aResult->textLength);
    case kRetrievableFileTag:
    case kUnavailableTag:
      return true;
    default:
      return false;
  }
}

static bool ReadSourceData(mozilla::Span<const uint8_t>& aSourceBytes,
                           JSSourceEntryMetadata& aMetadata,
                           ProfilerJSSourceData* aResult) {
  if (aMetadata.sourceMapURLLength > 0 &&
      !CopySourceChars(aSourceBytes, aMetadata.sourceMapURLLength,
                       &aMetadata.sourceMapURL)) {
    return false;
  }

  switch (aMetadata.typeTag) {
    case kSourceTextUTF16Tag: {
      JS::UniqueTwoByteChars chars;
      if (aMetadata.textLength > 0 &&
          !CopySourceChars(aSourceBytes, aMetadata.textLength, &chars)) {
        return false;
      }
      *aResult = ProfilerJSSourceData(
          aMetadata.sourceId, std::move(chars), aMetadata.textLength,
          std::move(aMetadata.filePath), aMetadata.filePathLength,
          aMetadata.startLine, aMetadata.startColumn,
          std::move(aMetadata.sourceMapURL), aMetadata.sourceMapURLLength);
      return true;
    }
    case kSourceTextUTF8Tag: {
      JS::UniqueChars chars;
      if (aMetadata.textLength > 0 &&
          !CopySourceChars(aSourceBytes, aMetadata.textLength, &chars)) {
        return false;
      }
      *aResult = ProfilerJSSourceData(
          aMetadata.sourceId, std::move(chars), aMetadata.textLength,
          std::move(aMetadata.filePath), aMetadata.filePathLength,
          aMetadata.startLine, aMetadata.startColumn,
          std::move(aMetadata.sourceMapURL), aMetadata.sourceMapURLLength);
      return true;
    }
    case kRetrievableFileTag:
      *aResult = ProfilerJSSourceData::CreateRetrievableFile(
          aMetadata.sourceId, std::move(aMetadata.filePath),
          aMetadata.filePathLength, aMetadata.startLine, aMetadata.startColumn,
          std::move(aMetadata.sourceMapURL), aMetadata.sourceMapURLLength);
      return true;
    case kUnavailableTag:
      *aResult = ProfilerJSSourceData(
          aMetadata.sourceId, std::move(aMetadata.filePath),
          aMetadata.filePathLength, aMetadata.startLine, aMetadata.startColumn,
          std::move(aMetadata.sourceMapURL), aMetadata.sourceMapURLLength);
      return true;
    default:
      return false;
  }
}

// The source map URL and the source text of all the entries, which can both be
// very long, are sent in a single BigBuffer after their metadata. Above a small
// threshold, this puts them in one shared memory region, instead of inlining
// them in the message and exceeding IPC::Channel::kMaximumMessageSize.
void ParamTraits<mozilla::ProfileGenerationAdditionalInformation>::Write(
    MessageWriter* aWriter, const paramType& aParam) {
  WriteParam(aWriter, aParam.mSharedLibraries);

  const nsTArray<mozilla::JSSourceEntry>& entries = aParam.mJSSourceEntries;
  mozilla::CheckedInt<size_t> sourceBufferLength = 0;
  for (const mozilla::JSSourceEntry& entry : entries) {
    sourceBufferLength += SourceMapURLBytes(entry.sourceData).Length();
    sourceBufferLength += SourceTextBytes(entry.sourceData).Length();
  }

  // If the buffer can't be allocated, send the metadata without the source map
  // URLs and the text.
  mozilla::ipc::BigBuffer sourceBuffer;
  if (sourceBufferLength.isValid() && sourceBufferLength.value() > 0) {
    sourceBuffer =
        mozilla::ipc::BigBuffer::TryAlloc(sourceBufferLength.value());
    NS_WARNING_ASSERTION(sourceBuffer.Size() > 0,
                         "Couldn't allocate the JS source buffer");
  }
  const bool includeSourceBuffer =
      sourceBufferLength.isValid() &&
      sourceBuffer.Size() == sourceBufferLength.value();

  WriteParam(aWriter, entries.Length());
  for (const mozilla::JSSourceEntry& entry : entries) {
    WriteSourceMetadata(aWriter, entry, includeSourceBuffer);
  }

  if (includeSourceBuffer) {
    uint8_t* sourceCursor = sourceBuffer.Data();
    for (const mozilla::JSSourceEntry& entry : entries) {
      for (mozilla::Span<const uint8_t> bytes :
           {SourceMapURLBytes(entry.sourceData),
            SourceTextBytes(entry.sourceData)}) {
        if (!bytes.IsEmpty()) {
          memcpy(sourceCursor, bytes.Elements(), bytes.Length());
          sourceCursor += bytes.Length();
        }
      }
    }
  }
  WriteParam(aWriter, std::move(sourceBuffer));
}

bool ParamTraits<mozilla::ProfileGenerationAdditionalInformation>::Read(
    MessageReader* aReader, paramType* aResult) {
  size_t entryCount;
  if (!ReadParam(aReader, &aResult->mSharedLibraries) ||
      !ReadParam(aReader, &entryCount)) {
    return false;
  }

  nsTArray<JSSourceEntryMetadata> metadata;
  mozilla::CheckedInt<size_t> sourceBufferLength = 0;
  for (size_t i = 0; i < entryCount; ++i) {
    JSSourceEntryMetadata* entryMetadata = metadata.AppendElement();
    if (!ReadSourceMetadata(aReader, entryMetadata)) {
      return false;
    }
    sourceBufferLength +=
        mozilla::CheckedInt<size_t>(entryMetadata->sourceMapURLLength) *
        sizeof(char16_t);
    sourceBufferLength +=
        mozilla::CheckedInt<size_t>(entryMetadata->textLength) *
        SourceTextCharSize(entryMetadata->typeTag);
  }

  mozilla::ipc::BigBuffer sourceBuffer;
  if (!sourceBufferLength.isValid() || !ReadParam(aReader, &sourceBuffer) ||
      sourceBuffer.Size() != sourceBufferLength.value()) {
    return false;
  }

  mozilla::Span<const uint8_t> sourceBytes = sourceBuffer.AsSpan();
  aResult->mJSSourceEntries.SetCapacity(entryCount);
  for (JSSourceEntryMetadata& entryMetadata : metadata) {
    ProfilerJSSourceData sourceData;
    if (!ReadSourceData(sourceBytes, entryMetadata, &sourceData)) {
      return false;
    }
    aResult->mJSSourceEntries.AppendElement(mozilla::JSSourceEntry(
        std::move(entryMetadata.id), std::move(sourceData)));
  }
  return true;
}

IMPLEMENT_IPC_SERIALIZER_WITH_FIELDS(mozilla::ProfileAndAdditionalInformation,
                                     mProfile, mAdditionalInformation);

}  // namespace IPC
