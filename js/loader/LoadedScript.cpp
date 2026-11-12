/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "LoadedScript.h"

#include "mozilla/dom/ScriptLoadContext.h"  // ScriptLoadContext
#include "mozilla/HoldDropJSObjects.h"
#include "mozilla/RefPtr.h"     // RefPtr, mozilla::MakeRefPtr
#include "mozilla/Span.h"       // mozilla::Span
#include "mozilla/Sprintf.h"    // SprintfLiteral
#include "mozilla/UniquePtr.h"  // mozilla::UniquePtr, std::make_unique

#include "jsfriendapi.h"
#include "LoadContextBase.h"  // LoadContextBase
#include "nsIChannel.h"       // nsIChannel
#include "nsIURI.h"           // nsIURI::GetSpecOrDefault

#include "js/experimental/JSStencil.h"  // JS::SizeOfStencil
#include "js/Modules.h"                 // JS::{Get,Set}ModulePrivate

namespace JS::loader {

NS_IMPL_ISUPPORTS(ScriptFetchInfo, nsISupports);

ScriptFetchInfo::ScriptFetchInfo(ScriptKind aKind,
                                 mozilla::dom::ReferrerPolicy aReferrerPolicy,
                                 ScriptFetchOptions* aFetchOptions,
                                 nsIURI* aURI)
    : mKind(aKind),
      mReferrerPolicy(aReferrerPolicy),
      mFetchOptions(aFetchOptions),
      mBaseURL(aURI) {
  MOZ_ASSERT(mFetchOptions);
}

size_t ScriptFetchInfo::SizeOfIncludingThis(
    mozilla::MallocSizeOf aMallocSizeOf) const {
  return aMallocSizeOf(this) +
         mFetchOptions->SizeOfIncludingThis(aMallocSizeOf);
}

ResolvedModuleSet* ScriptFetchInfo::GetPreloadedResolvedSet() {
  if (!mPreloadedResolvedSet) {
    mPreloadedResolvedSet = std::make_unique<ResolvedModuleSet>();
  }

  return mPreloadedResolvedSet.get();
}

static bool IsInternalURIScheme(nsIURI* uri) {
  return uri->SchemeIs("moz-extension") || uri->SchemeIs("resource") ||
         uri->SchemeIs("moz-src") || uri->SchemeIs("chrome");
}

void ScriptFetchInfo::SetBaseURLFromChannelAndOriginalURI(
    nsIChannel* aChannel, nsIURI* aOriginalURI) {
  // Fixup moz-extension: and resource: URIs, because the channel URI will
  // point to file:, which won't be allowed to load.
  if (aOriginalURI && IsInternalURIScheme(aOriginalURI)) {
    mBaseURL = aOriginalURI;
  } else {
    aChannel->GetURI(getter_AddRefs(mBaseURL));
  }
}

void ScriptFetchInfo::AssociateWithScript(JSScript* aScript) {
  // Verify that the rewritten URL is available when manipulating the referrer.
  MOZ_ASSERT(mBaseURL);

  // Set a JSScript's private value to point to this object. The JS engine will
  // increment our reference count by calling
  // HostAddRefScriptFetchInfo(). This is decremented by
  // HostReleaseScriptFetchInfo() below when the JSScript dies.

  MOZ_ASSERT(GetScriptPrivate(aScript).isUndefined());
  SetScriptPrivate(aScript, PrivateValue(this));
}

void ScriptFetchInfo::AssociateWithModule(JSObject* aModuleRecord) {
  MOZ_ASSERT(mBaseURL);

  // Make module's host defined field point to this object. The JS engine
  // will increment our reference count by calling
  // HostAddRefScriptFetchInfo(). This is decremented when the
  // module record dies.
  MOZ_ASSERT(GetModulePrivate(aModuleRecord).isUndefined());
  SetModulePrivate(aModuleRecord, PrivateValue(this));
}

void HostAddRefScriptFetchInfo(const Value& aPrivate) {
  // Increment the reference count of a ScriptFetchInfo object that is
  // now pointed to by a JSScript. The reference count is decremented by
  // HostReleaseScriptFetchInfo() below.

  auto fetchInfo = static_cast<ScriptFetchInfo*>(aPrivate.toPrivate());
  fetchInfo->AddRef();
}

void HostReleaseScriptFetchInfo(const Value& aPrivate) {
  // Decrement the reference count of a ScriptFetchInfo object that was
  // pointed to by a JSScript. The reference count was originally incremented by
  // HostAddRefScriptFetchInfo() above.

  auto fetchInfo = static_cast<ScriptFetchInfo*>(aPrivate.toPrivate());
  fetchInfo->Release();
}

//////////////////////////////////////////////////////////////
// LoadedScript
//////////////////////////////////////////////////////////////

NS_IMPL_ISUPPORTS(LoadedScript, nsISupports)

LoadedScript::LoadedScript(ScriptKind aKind, nsIURI* aURI)
    : mDataType(DataType::eUnknown),
      mKind(aKind),
      mSerializedStencilOffset(0),
      mCacheEntryId(InvalidCacheEntryId),
      mIsDirty(false),
      mTookLongInPreviousRuns(false),
      mIsEverHitFromMemoryCache(false),
      mDependsOnClassicScriptHintEncoding(false),
      mURI(aURI),
      mReceivedScriptTextLength(0) {
  MOZ_ASSERT(mURI);
}

size_t LoadedScript::SizeOfIncludingThis(
    mozilla::MallocSizeOf aMallocSizeOf) const {
  size_t bytes = aMallocSizeOf(this);

  if (mSRIMetadata) {
    bytes += mSRIMetadata->SizeOfIncludingThis(aMallocSizeOf);
  }

  if (IsTextSource()) {
    if (IsUTF16Text()) {
      bytes += ScriptText<char16_t>().sizeOfExcludingThis(aMallocSizeOf);
    } else {
      bytes += ScriptText<Utf8Unit>().sizeOfExcludingThis(aMallocSizeOf);
    }
  }

  bytes += mSRIAndSerializedStencil.sizeOfExcludingThis(aMallocSizeOf);

  if (mCachedStencil) {
    bytes += JS::SizeOfStencil(mCachedStencil, aMallocSizeOf);
  }

  return bytes;
}

nsresult LoadedScript::GetScriptSource(JSContext* aCx,
                                       MaybeSourceText* aMaybeSource,
                                       LoadContextBase* aMaybeLoadContext) {
  // If there's no script text, we try to get it from the element
  bool isWindowContext =
      aMaybeLoadContext && aMaybeLoadContext->IsWindowContext();
  if (isWindowContext && aMaybeLoadContext->AsWindowContext()->mIsInline) {
    nsAutoString inlineData;
    auto* scriptLoadContext = aMaybeLoadContext->AsWindowContext();
    scriptLoadContext->GetInlineScriptText(inlineData);

    size_t nbytes = inlineData.Length() * sizeof(char16_t);
    UniqueTwoByteChars chars(static_cast<char16_t*>(JS_malloc(aCx, nbytes)));
    if (!chars) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    memcpy(chars.get(), inlineData.get(), nbytes);

    SourceText<char16_t> srcBuf;
    if (!srcBuf.init(aCx, std::move(chars), inlineData.Length())) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    aMaybeSource->construct<SourceText<char16_t>>(std::move(srcBuf));
    return NS_OK;
  }

  size_t length = ScriptTextLength();
  if (IsUTF16Text()) {
    UniqueTwoByteChars chars;
    chars.reset(ScriptText<char16_t>().extractOrCopyRawBuffer());
    if (!chars) {
      JS_ReportOutOfMemory(aCx);
      return NS_ERROR_OUT_OF_MEMORY;
    }

    SourceText<char16_t> srcBuf;
    if (!srcBuf.init(aCx, std::move(chars), length)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    aMaybeSource->construct<SourceText<char16_t>>(std::move(srcBuf));
    return NS_OK;
  }

  MOZ_ASSERT(IsUTF8Text());
  mozilla::UniquePtr<Utf8Unit[], FreePolicy> chars;
  chars.reset(ScriptText<Utf8Unit>().extractOrCopyRawBuffer());
  if (!chars) {
    JS_ReportOutOfMemory(aCx);
    return NS_ERROR_OUT_OF_MEMORY;
  }

  SourceText<Utf8Unit> srcBuf;
  if (!srcBuf.init(aCx, std::move(chars), length)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  aMaybeSource->construct<SourceText<Utf8Unit>>(std::move(srcBuf));
  return NS_OK;
}

void LoadedScript::SetSRIMetadata(
    const mozilla::dom::SRIMetadata& aSRIMetadata) {
  if (aSRIMetadata.IsEmpty()) {
    return;
  }

  mSRIMetadata = std::make_unique<mozilla::dom::SRIMetadata>(aSRIMetadata);
}

bool LoadedScript::IsSRIMetadataReusableBy(
    const mozilla::dom::SRIMetadata& aSRIMetadata) {
  if (aSRIMetadata.IsEmpty()) {
    return true;
  }

  if (!mSRIMetadata) {
    return false;
  }

  return aSRIMetadata.CanTrustBeDelegatedTo(*mSRIMetadata);
}

void LoadedScript::WriteIntoEncodingHeader(EncodingHeader* aHeader) const {
  if (!IsClassicScript() || !mClassicScriptEncoding) {
    memset(aHeader, 0, EncodingHeaderSize);
    return;
  }

  aHeader->mDependsOnClassicScriptHintEncoding =
      mDependsOnClassicScriptHintEncoding ? 1 : 0;

  nsAutoCString name;
  mClassicScriptEncoding->Name(name);
  MOZ_ASSERT(name.Length() <
             EncodingHeader::ClassicScriptEncodingMaxLength - 1);

  memset(aHeader->mClassicScriptEncoding, 0,
         EncodingHeader::ClassicScriptEncodingMaxLength);
  memcpy(aHeader->mClassicScriptEncoding, name.get(), name.Length());
}

bool LoadedScript::ReadFromEncodingHeader(const EncodingHeader* aHeader) {
  if (!IsClassicScript()) {
    if (aHeader->mDependsOnClassicScriptHintEncoding) {
      return false;
    }
    if (aHeader->mClassicScriptEncoding[0] != '\0') {
      return false;
    }
    mDependsOnClassicScriptHintEncoding = false;
    mClassicScriptEncoding = nullptr;
    return true;
  }

  mDependsOnClassicScriptHintEncoding =
      aHeader->mDependsOnClassicScriptHintEncoding;

  size_t encodingStringLen =
      strnlen(aHeader->mClassicScriptEncoding,
              EncodingHeader::ClassicScriptEncodingMaxLength);
  if (encodingStringLen == EncodingHeader::ClassicScriptEncodingMaxLength) {
    return false;
  }

  if (encodingStringLen == 0) {
    mClassicScriptEncoding = nullptr;
    return true;
  }

  mClassicScriptEncoding = mozilla::Encoding::ForLabel(
      mozilla::Span(aHeader->mClassicScriptEncoding, encodingStringLen));
  if (!mClassicScriptEncoding) {
    return false;
  }
  return true;
}

//////////////////////////////////////////////////////////////
// ModuleScript
//////////////////////////////////////////////////////////////

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ModuleScript)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION_CLASS(ModuleScript)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(ModuleScript)
  tmp->mModuleRecord = nullptr;
  tmp->mParseError.setUndefined();
  tmp->mErrorToRethrow.setUndefined();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(ModuleScript)
NS_IMPL_CYCLE_COLLECTING_RELEASE(ModuleScript)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(ModuleScript)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(ModuleScript)
  NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(mModuleRecord)
  NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(mParseError)
  NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(mErrorToRethrow)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

ModuleScript::ModuleScript(ScriptFetchInfo* aFetchInfo)
    : mFetchInfo(aFetchInfo) {
  MOZ_ASSERT(!ModuleRecord());
  MOZ_ASSERT(!HasParseError());
  MOZ_ASSERT(!HasErrorToRethrow());
}

void ModuleScript::Shutdown() {
  if (mModuleRecord) {
    ClearModuleEnvironment(mModuleRecord);
  }

  mModuleRecord = nullptr;
}

ModuleScript::~ModuleScript() {
  // The object may be destroyed without being unlinked first.
  mModuleRecord = nullptr;
  mozilla::DropJSObjects(this);
}

void ModuleScript::SetModuleRecord(Handle<JSObject*> aModuleRecord) {
  MOZ_ASSERT(!mModuleRecord);
  MOZ_ASSERT(!HasParseError());
  MOZ_ASSERT(!HasErrorToRethrow());

  mModuleRecord = aModuleRecord;

#ifdef DEBUG
  // Sync the [[PreloadSlot]] in ModuleObject.
  if (mModuleRecord) {
    SetModulePreload(mModuleRecord, mFetchInfo->IsForModulePreload());
  }
#endif

  mozilla::HoldJSObjects(this);
}

void ModuleScript::SetParseError(const Value& aError) {
  MOZ_ASSERT(!aError.isUndefined());
  MOZ_ASSERT(!HasParseError());
  MOZ_ASSERT(!HasErrorToRethrow());

  mModuleRecord = nullptr;
  mParseError = aError;
  mozilla::HoldJSObjects(this);
}

void ModuleScript::SetErrorToRethrow(const Value& aError) {
  MOZ_ASSERT(!aError.isUndefined());

  // This is only called after SetModuleRecord() or SetParseError() so we don't
  // need to call HoldJSObjects() here.
  MOZ_ASSERT(ModuleRecord() || HasParseError());

  mErrorToRethrow = aError;
}

void ModuleScript::SetHadImportMap(bool aValue) { mHadImportMap = aValue; }

void ModuleScript::ResetPreloadErrors() {
  MOZ_ASSERT(mFetchInfo->IsForModulePreload());

  if (HasParseError()) {
    mParseError = UndefinedValue();
  }

  if (HasErrorToRethrow()) {
    mErrorToRethrow = UndefinedValue();
  }
}

}  // namespace JS::loader
