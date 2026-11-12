/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef js_loader_LoadedScript_h
#define js_loader_LoadedScript_h

#include "mozilla/dom/SRIMetadata.h"  // mozilla::dom::SRIMetadata
#include "mozilla/Encoding.h"  // mozilla::Encoding, ENCODING_NAME_MAX_LENGTH
#include "mozilla/Maybe.h"
#include "mozilla/MaybeOneOf.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/RefPtr.h"
#include "mozilla/UniquePtr.h"  // mozilla::UniquePtr
#include "mozilla/Utf8.h"       // mozilla::Utf8Unit
#include "mozilla/Variant.h"
#include "mozilla/Vector.h"

#include "jsapi.h"
#include "nsCOMPtr.h"
#include "nsCycleCollectionParticipant.h"
#include "nsICacheInfoChannel.h"  // nsICacheInfoChannel
#include "ResolvedModuleSet.h"
#include "ScriptFetchOptions.h"
#include "ScriptKind.h"

#include "js/AllocPolicy.h"
#include "js/experimental/JSStencil.h"
#include "js/Transcoding.h"

class nsIURI;

namespace JS::loader {

class ScriptLoadRequest;

using Utf8Unit = mozilla::Utf8Unit;

void HostAddRefScriptFetchInfo(const Value& aPrivate);
void HostReleaseScriptFetchInfo(const Value& aPrivate);

class ClassicScript;
class LoadedModuleScript;
class LoadContextBase;

// Information required to fetch scripts or module graphs.
//
// This class is separated than LoadedScript or ScriptLoadRequest, in order to
// store it into the script private and module private, to propagate the
// information to the module imports performed later.
//
// The fields are initialized from the request, and then updated from the
// responses.
class ScriptFetchInfo : public nsISupports {
 public:
  ScriptFetchInfo(ScriptKind aKind,
                  mozilla::dom::ReferrerPolicy aReferrerPolicy,
                  ScriptFetchOptions* aFetchOptions, nsIURI* aURI);

  NS_DECL_ISUPPORTS

  size_t SizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf) const;

  bool IsForModulePreload() const { return mIsForModulePreload; }
  void SetForModulePreload(bool aValue) { mIsForModulePreload = aValue; }

  bool HasPreloadedResolvedSet() const { return bool(mPreloadedResolvedSet); }
  JS::loader::ResolvedModuleSet* GetPreloadedResolvedSet();
  void ReleasePreloadedResolvedSet() { mPreloadedResolvedSet = nullptr; }

  bool IsForModuleScript() const { return mKind == ScriptKind::eModule; }
  bool IsForEvent() const { return mKind == ScriptKind::eEvent; }

  mozilla::dom::ReferrerPolicy ReferrerPolicy() const {
    return mReferrerPolicy;
  }
  void UpdateReferrerPolicy(mozilla::dom::ReferrerPolicy aReferrerPolicy) {
    mReferrerPolicy = aReferrerPolicy;
  }

  ScriptFetchOptions* FetchOptions() const { return mFetchOptions; }

  nsIURI* BaseURL() const { return mBaseURL; }
  void SetBaseURL(nsIURI* aBaseURL) { mBaseURL = aBaseURL; }

  /*
   * Set the mBaseURL, based on aChannel.
   * aOriginalURI is the result of aChannel->GetOriginalURI.
   */
  void SetBaseURLFromChannelAndOriginalURI(nsIChannel* aChannel,
                                           nsIURI* aOriginalURI);

  void AssociateWithScript(JSScript* aScript);
  void AssociateWithModule(JSObject* aModuleRecord);

 protected:
  virtual ~ScriptFetchInfo() = default;

 private:
  // Set to true if this is for a module imported as part of preload.
  //
  // This field can be overwritten based on the module import processing.
  bool mIsForModulePreload = false;

  // This should match the LoadedScript::mKind.
  ScriptKind mKind;

  // The referrer policy used for fetching this script, and going to be used for
  // fetching imported modules.
  //
  // This field can be overwritten based on the response.
  mozilla::dom::ReferrerPolicy mReferrerPolicy;

  // The fetch option used for fetching this script, and going to be used for
  // fetching imported modules.
  // This field is constant, and never overwritten from the response.
  RefPtr<ScriptFetchOptions> mFetchOptions;

  // The base URL used for resolving relative module imports.
  // This field is unused for Event script, and in that case the loader's base
  // URL should be used.
  //
  // This field can be overwritten based on the response.
  nsCOMPtr<nsIURI> mBaseURL;

  // The module specifiers resolved during preloading. This set is merged into
  // the global resolved module set when the preload request is reused.
  mozilla::UniquePtr<JS::loader::ResolvedModuleSet> mPreloadedResolvedSet;
};

// A LoadedScript is a place where the Script is stored once it is loaded. It is
// not unique to a load, and can be shared across loads as long as it is
// properly ref-counted by each load instance.
//
// When the load is not performed, the URI represents the resource to be loaded,
// and it is replaced by the absolute resource location once loaded.
//
// As the LoadedScript can be shared, using the SharedSubResourceCache, it is
// exposed to the memory reporter such that sharing might be accounted for
// properly.
class LoadedScript final : public nsISupports {
  ~LoadedScript() = default;

 public:
  LoadedScript(ScriptKind aKind, nsIURI* aURI);
  size_t SizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf) const;

 public:
  NS_DECL_ISUPPORTS

  uint16_t ClampedRefCountForTelemetry() const {
    uintptr_t count = mRefCnt.get();
    if (count > 100) {
      return 100;
    }
    return uint16_t(count);
  }

  bool IsClassicScript() const { return mKind == ScriptKind::eClassic; }
  bool IsModuleScript() const { return mKind == ScriptKind::eModule; }
  bool IsImportMapScript() const { return mKind == ScriptKind::eImportMap; }
  bool IsSpeculationRulesScript() const {
    return mKind == ScriptKind::eSpeculationRules;
  }

  nsIURI* GetURI() const { return mURI; }

  nsIURI* CachedBaseURL() const { return mCachedBaseURL; }
  mozilla::dom::ReferrerPolicy CachedReferrerPolicy() const {
    return mCachedReferrerPolicy;
  }

 public:
  // ===========================================================================
  // Encoding of the content provided by the network, or refined by the JS
  // engine.
  template <typename... Ts>
  using Variant = mozilla::Variant<Ts...>;

  template <typename... Ts>
  using VariantType = mozilla::VariantType<Ts...>;

  // Type of data this instance holds, which is either provided by the nsChannel
  // or retrieved from the cache.
  enum class DataType : uint8_t {
    // This script haven't yet received the data.
    eUnknown,

    // This script is received as a plain text from the channel.
    // mScriptData holds the text source, and mStencil holds the compiled
    // stencil.
    // mSRIAndSerializedStencil holds the SRI.
    eTextSource,

    // This script is received as a serialized stencil from the channel,
    // mSRIAndSerializedStencil holds the SRI and the serialized stencil, and
    // mCachedStencil is unused.
    eSerializedStencil,

    // This script is cached from the previous load.
    // mCachedStencil holds the cached stencil. mScriptData is unused.
    //
    // mSRIAndSerializedStencil can contain SRI only if this script is going to
    // be saved to disk:
    //   * If this was retrieved as eTextSource and then converted to
    //     eCachedStencil:
    //     * If this script is going to be saved to disk,
    //       mSRIAndSerializedStencil holds the SRI
    //     * If this script was already saved to disk,
    //       mSRIAndSerializedStencil was cleared before save, and is unused
    //     * If this script is not going to be saved to disk,
    //       mSRIAndSerializedStencil is unused
    //   * If this was retrieved as eSerializedStencil and then converted to
    //     eCachedStencil, the decoded stencil should not borrow the buffer.
    //     mSRIAndSerializedStencil was cleared on conversion, is unused
    eCachedStencil,

    // This was eCachedStencil, but the stencil reference is cleared
    // for the memory pressure.
    // Other fields are still valid.
    eInvalidatedCachedStencil,

    // This is a wasm module, which is used when the response mime type essence
    // is application/wasm.
    // mScriptData holds the wasm source as uint8_t from the channel.
    // mCachedStencil and mSRIAndSerializedStencil are unused.
    eWasmBytes,
  };

  // Use a vector backed by the JS allocator for script text so that contents
  // can be transferred in constant time to the JS engine, not copied in linear
  // time.
  template <typename Unit>
  using ScriptTextBuffer = mozilla::Vector<Unit, 0, js::MallocAllocPolicy>;

  using MaybeSourceText =
      mozilla::MaybeOneOf<SourceText<char16_t>, SourceText<Utf8Unit>>;

  // ==== Methods to query the data type ====

  bool IsUnknownDataType() const { return mDataType == DataType::eUnknown; }
  bool IsTextSource() const { return mDataType == DataType::eTextSource; }
  bool IsSerializedStencil() const {
    return mDataType == DataType::eSerializedStencil;
  }
  bool IsCachedStencil() const { return mDataType == DataType::eCachedStencil; }
  bool IsInvalidatedCachedStencil() const {
    return mDataType == DataType::eInvalidatedCachedStencil;
  }
  bool IsWasmBytes() const { return mDataType == DataType::eWasmBytes; }

  // ==== Methods to convert the data type ====

  void SetUnknownDataType() {
    mDataType = DataType::eUnknown;
    mScriptData.reset();
  }

  void SetTextSource(LoadContextBase* maybeLoadContext) {
    MOZ_ASSERT(IsUnknownDataType());
    mDataType = DataType::eTextSource;
    mScriptData.emplace(VariantType<ScriptTextBuffer<Utf8Unit>>());
  }

  void SetSerializedStencil() {
    MOZ_ASSERT(IsUnknownDataType());
    mDataType = DataType::eSerializedStencil;
  }

  void ConvertToCachedStencil(JS::Stencil* aStencil,
                              mozilla::dom::ReferrerPolicy aReferrerPolicy,
                              nsIURI* aBaseURL) {
    if (IsTextSource()) {
      // The text source is no longer necessary, given it's already compiled.
      // The SRI is still necessary in order to save it to the disk cache.
      ClearScriptText();
    } else {
      // The serialized stencil is no longer necessary, given it's already
      // decoded, without borrowing.
      // The SRI is also unnecessary given we don't save serialized stencil
      // again.
      MOZ_ASSERT(IsSerializedStencil());
      MOZ_ASSERT(!JS::StencilIsBorrowed(aStencil));
      DropSRIOrSRIAndSerializedStencil();
    }
    SetUnknownDataType();
    mDataType = DataType::eCachedStencil;
    mCachedStencil = aStencil;
    mCachedReferrerPolicy = aReferrerPolicy;
    mCachedBaseURL = aBaseURL;
  }

  void InvalidateCachedStencil() {
    MOZ_ASSERT(IsCachedStencil());
    mDataType = DataType::eInvalidatedCachedStencil;
    mCachedStencil = nullptr;
  }

  void SetWasmBytes() {
    MOZ_ASSERT(IsUnknownDataType());
    mDataType = DataType::eWasmBytes;
    mScriptData.emplace(VariantType<ScriptTextBuffer<uint8_t>>());
  }

  bool IsUTF16Text() const {
    return mScriptData->is<ScriptTextBuffer<char16_t>>();
  }
  bool IsUTF8Text() const {
    return mScriptData->is<ScriptTextBuffer<Utf8Unit>>();
  }

  // ==== Methods to access the text source ====

  template <typename Unit>
  const ScriptTextBuffer<Unit>& ScriptText() const {
    MOZ_ASSERT(IsTextSource());
    return mScriptData->as<ScriptTextBuffer<Unit>>();
  }
  template <typename Unit>
  ScriptTextBuffer<Unit>& ScriptText() {
    MOZ_ASSERT(IsTextSource());
    return mScriptData->as<ScriptTextBuffer<Unit>>();
  }

  ScriptTextBuffer<uint8_t>& WasmBytes() {
    MOZ_ASSERT(IsWasmBytes());
    return mScriptData->as<ScriptTextBuffer<uint8_t>>();
  }

  size_t ScriptTextLength() const {
    MOZ_ASSERT(IsTextSource());
    return IsUTF16Text() ? ScriptText<char16_t>().length()
                         : ScriptText<Utf8Unit>().length();
  }

  // Get source text.  On success |aMaybeSource| will contain either UTF-8 or
  // UTF-16 source; on failure it will remain in its initial state.
  nsresult GetScriptSource(JSContext* aCx, MaybeSourceText* aMaybeSource,
                           LoadContextBase* aMaybeLoadContext);

  void ClearScriptText() {
    MOZ_ASSERT(IsTextSource());
    return IsUTF16Text() ? ScriptText<char16_t>().clearAndFree()
                         : ScriptText<Utf8Unit>().clearAndFree();
  }

  size_t ReceivedScriptTextLength() const {
    MOZ_ASSERT(IsTextSource());
    return mReceivedScriptTextLength;
  }

  void SetReceivedScriptTextLength(size_t aLength) {
    MOZ_ASSERT(IsTextSource());
    mReceivedScriptTextLength = aLength;
  }

  // ==== Methods to access the serialized data or the SRI part ====
  // mSRIAndSerializedStencil field is shared between two separate consumers.
  // See mSRIAndSerializedStencil comment for more info.

  // ---- For SRI-only consumers ----

  bool CanHaveSRIOnly() const {
    return IsTextSource() || IsCachedStencil() || IsInvalidatedCachedStencil();
  }

  bool HasSRI() const {
    MOZ_ASSERT(CanHaveSRIOnly());
    return !mSRIAndSerializedStencil.empty();
  }

  TranscodeBuffer& SRI() {
    MOZ_ASSERT(CanHaveSRIOnly());
    return mSRIAndSerializedStencil;
  }

  void DropSRI() {
    MOZ_ASSERT(CanHaveSRIOnly());
    mSRIAndSerializedStencil.clearAndFree();
  }

  // ---- For SRI and serialized Stencil consumers ---

  bool CanHaveSRIAndSerializedStencil() const { return IsSerializedStencil(); }

  TranscodeBuffer& SRIAndSerializedStencil() {
    MOZ_ASSERT(CanHaveSRIAndSerializedStencil());
    return mSRIAndSerializedStencil;
  }
  TranscodeRange SerializedStencil() const {
    MOZ_ASSERT(CanHaveSRIAndSerializedStencil());
    const auto& buf = mSRIAndSerializedStencil;
    auto offset = mSerializedStencilOffset;
    // A decode task holds the bytes while it runs, and a cancelled one drops
    // them, so neither leaves the request to be processed further.
    MOZ_DIAGNOSTIC_ASSERT(buf.length() >= offset);
    return TranscodeRange(buf.begin() + offset, buf.length() - offset);
  }

  // Hands the bytes to a decode task, which returns or drops them.
  TranscodeBuffer TakeSRIAndSerializedStencil() {
    MOZ_ASSERT(CanHaveSRIAndSerializedStencil());
    return std::move(mSRIAndSerializedStencil);
  }

  void RestoreSRIAndSerializedStencil(TranscodeBuffer&& aBuffer) {
    MOZ_ASSERT(CanHaveSRIAndSerializedStencil());
    MOZ_ASSERT(mSRIAndSerializedStencil.empty());
    mSRIAndSerializedStencil = std::move(aBuffer);
    // A decoded stencil may borrow the buffer across the move.
    MOZ_ASSERT(mSRIAndSerializedStencil.isStorageConsistent());
  }

  // ---- Methods shared between both consumers ----

  size_t GetSerializedStencilOffset() const {
    MOZ_ASSERT(CanHaveSRIOnly() || CanHaveSRIAndSerializedStencil());
    return mSerializedStencilOffset;
  }

  // The header associated with a serialization of the loaded script.
  static constexpr size_t EncodingHeaderSize = 16;
  struct EncodingHeader {
    // A copy of LoadedScript::mDependsOnClassicScriptHintEncoding field value.
    uint8_t mDependsOnClassicScriptHintEncoding;

    static constexpr size_t ClassicScriptEncodingMaxLength =
        EncodingHeaderSize - 1;

    // The label of LoadedScript::mClassicScriptEncoding, padded with the null
    // terminator.
    // Empty string if the script always uses UTF-8, such as modules.
    char mClassicScriptEncoding[ClassicScriptEncodingMaxLength];
  };

  // Fill the encoding header with this script's information.
  void WriteIntoEncodingHeader(EncodingHeader* aHeader) const;

  // Fill this script's information from given header.
  // Returns true if the header is valid, otherwise false.
  bool ReadFromEncodingHeader(const EncodingHeader* aHeader);

  void SetAlignedSRILength(size_t aAlignedSRILength) {
    MOZ_ASSERT(CanHaveSRIOnly() || CanHaveSRIAndSerializedStencil());
    MOZ_ASSERT(JS::IsTranscodingBytecodeOffsetAligned(aAlignedSRILength));
    static_assert(sizeof(EncodingHeader) == EncodingHeaderSize,
                  "The struct size should match");
    static_assert(ENCODING_NAME_MAX_LENGTH < EncodingHeaderSize,
                  "The encoding name should fit the fixed-length header");
    mSerializedStencilOffset = aAlignedSRILength + EncodingHeaderSize;
  }

  bool HasNoSRIOrSRIAndSerializedStencil() const {
    MOZ_ASSERT(CanHaveSRIOnly() || CanHaveSRIAndSerializedStencil());
    return mSRIAndSerializedStencil.empty();
  }

  void DropSRIOrSRIAndSerializedStencil() {
    MOZ_ASSERT(CanHaveSRIOnly() || CanHaveSRIAndSerializedStencil());
    mSRIAndSerializedStencil.clearAndFree();
  }

  // ==== Methods to access the stencil ====

  Stencil* GetCachedStencil() const {
    MOZ_ASSERT(IsCachedStencil());
    return mCachedStencil;
  }

  // ==== Methods to access the disk cache reference ====

  // Check the reference to the cache info channel, which is used by the disk
  // cache.
  bool HasDiskCacheReference() const { return !!mCacheEntry; }

  // Drop the reference to the cache info channel.
  void DropDiskCacheReference() { mCacheEntry = nullptr; }

  void DropDiskCacheReferenceAndSRI() {
    DropDiskCacheReference();
    if (IsTextSource()) {
      DropSRI();
    }
  }

  // ==== Other methods ====

  void SetTookLongInPreviousRuns() { mTookLongInPreviousRuns = true; }
  bool TookLongInPreviousRuns() const { return mTookLongInPreviousRuns; }

  void SetIsEverHitFromMemoryCache() { mIsEverHitFromMemoryCache = true; }
  bool IsEverHitFromMemoryCache() const { return mIsEverHitFromMemoryCache; }

  bool IsDirty() const { return mIsDirty; }
  void SetDirty() {
    MOZ_ASSERT(HasCacheEntryId());
    mIsDirty = true;
  }
  void UnsetDirty() {
    MOZ_ASSERT(HasCacheEntryId());
    mIsDirty = false;
  }

  bool HasCacheEntryId() const { return mCacheEntryId != InvalidCacheEntryId; }
  uint64_t CacheEntryId() const {
    MOZ_ASSERT(HasCacheEntryId());
    return mCacheEntryId;
  }
  void SetCacheEntryId(uint64_t aId) {
    mCacheEntryId = aId;

    // mCacheEntryId is 48bits.  Verify no overflow happened.
    MOZ_ASSERT(mCacheEntryId == aId);
  }

  void AddFetchCount() {
    if (mFetchCount < UINT8_MAX) {
      mFetchCount++;
    }
  }

  void SetSRIMetadata(const mozilla::dom::SRIMetadata& aSRIMetadata);

  // Returns true if this script has compatible SRI metadata as the provided
  // one.
  bool IsSRIMetadataReusableBy(const mozilla::dom::SRIMetadata& aSRIMetadata);

  bool DependsOnClassicScriptHintEncoding() const {
    return mDependsOnClassicScriptHintEncoding;
  }

  const mozilla::Encoding* ClassicScriptEncoding() const {
    return mClassicScriptEncoding;
  }

  void SetClassicScriptEncodingFromBOM(const mozilla::Encoding* aEncoding) {
    mClassicScriptEncoding = aEncoding;
    mDependsOnClassicScriptHintEncoding = false;
  }

  void SetClassicScriptEncodingFromCharsetParameter(
      const mozilla::Encoding* aEncoding) {
    mClassicScriptEncoding = aEncoding;
    mDependsOnClassicScriptHintEncoding = false;
  }

  void SetClassicScriptEncodingFromFallback(
      const mozilla::Encoding* aEncoding) {
    mClassicScriptEncoding = aEncoding;
    mDependsOnClassicScriptHintEncoding = true;
  }

 public:
  // Fields.

  // Determine whether the mScriptData or mSRIAndSerializedStencil is used.
  // See DataType description for more info.
  DataType mDataType;

  // The consumer-defined number of times that this loaded script is used.
  //
  // In DOM ScriptLoader, this is used for counting the number of times that
  // the in-memory-cached script is used, clamped at UINT8_MAX.
  uint8_t mFetchCount = 0;

 private:
  const ScriptKind mKind;

  // The final ScriptFetchInfo::mReferrerPolicy value o the
  // initial request.
  // This field is set before this LoadedScript is stored into the
  // SharedScriptCache, and then propagated to the ScriptFetchInfo
  // for the request that uses this cache.
  mozilla::dom::ReferrerPolicy mCachedReferrerPolicy;

 public:
  // Offset of the serialized Stencil in mSRIAndSerializedStencil.
  uint32_t mSerializedStencilOffset;

 private:
  static constexpr uint64_t InvalidCacheEntryId = 0;

  // The cache entry ID of this script.
  //
  // 0 if the response doesn't have the corresponding cache entry,
  // or any other failure happened.
  //
  // This value comes from mozilla::net::CacheEntry::mCacheEntryId,
  // which comes from mozilla::net::CacheEntry::GetNextId.
  // It generates sequential IDs from 1 (thus 0 is treated as invalid value),
  // and the ID is valid within single browser session.
  //
  // In order to pack this field with mIsDirty below, we use shorter bits than
  // the original mozilla::net::CacheEntry::mCacheEntryId type (uint64_t).
  //
  // As long as the per-session sequential ID is the sole source of this value,
  // 48 bits should be sufficient.  1000 new IDs per second for 365 days
  // becomes 0x7_57b1_2c00, which is 35 bits.
  uint64_t mCacheEntryId : 48;

  // Set to true in the following situation:
  //   * this is cached in SharedScriptCache
  //   * A behavior around the network request is modified, and
  //     the cache needs validation on the necko side
  //
  // NOTE: In order to pack this with the mCacheEntryId above on windows,
  //       this must be uint64_t.
  uint64_t mIsDirty : 1;

  // Set to true if executing the top-level script takes long.
  // This can be used for scheduling the script execution in subsequent loads.
  // The threshold of "takes long" is user-defined.
  // See dom::ScriptLoader::EvaluateScript for the example case
  //
  // TODO: Move this into JS::Stencil, and save to the disk cache (bug 2005128)
  uint64_t mTookLongInPreviousRuns : 1;

  // Set to true if this entry is ever used in the current process.
  uint64_t mIsEverHitFromMemoryCache : 1;

  // Set to true when the response had no encoding information and the
  // script depends on the hint charset or the document encoding.
  // Set to false otherwise, which means the response had either
  // the BOM or the Content-Type header's charset parameter.
  uint64_t mDependsOnClassicScriptHintEncoding : 1;

  nsCOMPtr<nsIURI> mURI;

  // The final ScriptFetchInfo::mBaseURL value of the
  // initial request.
  // This field is set before this LoadedScript is stored into the
  // SharedScriptCache, and then propagated to the ScriptFetchInfo
  // for the request that uses this cache.
  nsCOMPtr<nsIURI> mCachedBaseURL;

  // An optional field to store the SRI metadata used by the request that
  // first creates this instance.
  // nullptr if the SRI metadata was empty, or not yet set.
  mozilla::UniquePtr<mozilla::dom::SRIMetadata> mSRIMetadata;

 public:
  // Holds script source data for non-inline scripts, or raw bytes for wasm
  // modules.
  mozilla::Maybe<Variant<ScriptTextBuffer<char16_t>, ScriptTextBuffer<Utf8Unit>,
                         ScriptTextBuffer<uint8_t>>>
      mScriptData;

  // The length of script source text, set when reading completes. This is used
  // since mScriptData is cleared when the source is passed to the JS engine.
  size_t mReceivedScriptTextLength;

  // Holds either of the following for non-inline scripts:
  //
  // * For scripts loaded as source text
  //   - SRI (variable length)
  //     calculated while receiving the source text
  //   - Padding (aligns to 4 bytes)
  // * For scripts loaded as serialized stencil
  //   - SRI (variable length)
  //   - Padding (aligns to 4 bytes)
  //   - Encoding header (16 bytes)
  //     0-terminated and 0-padded string of the encoding name
  //     empty string for modules
  //   - Serialized Stencil (variable length)
  //
  // For the serialized stencil case, this field becomes empty while a decode
  // task owns the bytes, and permanently if it is cancelled.
  TranscodeBuffer mSRIAndSerializedStencil;

  // Holds the stencil for the script, cached for the subsequent requests.
  RefPtr<Stencil> mCachedStencil;

  // The cache info channel used when saving the serialized Stencil to the
  // necko cache.
  //
  // This field is populated if the cache is enabled and this is either
  // IsTextSource() or IsCachedStencil(), and it's cleared after saving to the
  // necko cache, and thus, this field is used only once.
  nsCOMPtr<nsICacheEntryWriteHandle> mCacheEntry;

 private:
  // The encoding that's used for decoding the received script source.
  // This is an optional field, and can be left nullptr if the consumer
  // doesn't use it.
  const mozilla::Encoding* mClassicScriptEncoding = nullptr;
};

// Provide accessors for any classes `Derived` which is providing the
// `getLoadedScript` function as interface. The accessors are meant to be
// inherited by the `Derived` class.
template <typename Derived>
class LoadedScriptDelegate {
 private:
  // Use a static_cast<Derived> instead of declaring virtual functions. This is
  // meant to avoid relying on virtual table, and improve inlining for non-final
  // classes.
  const LoadedScript* GetLoadedScript() const {
    return static_cast<const Derived*>(this)->getLoadedScript();
  }
  LoadedScript* GetLoadedScript() {
    return static_cast<Derived*>(this)->getLoadedScript();
  }

 public:
  template <typename Unit>
  using ScriptTextBuffer = LoadedScript::ScriptTextBuffer<Unit>;
  using MaybeSourceText = LoadedScript::MaybeSourceText;

  nsIURI* URI() const { return GetLoadedScript()->GetURI(); }

  bool IsUnknownDataType() const {
    return GetLoadedScript()->IsUnknownDataType();
  }
  bool IsWasmBytes() const { return GetLoadedScript()->IsWasmBytes(); }

  void SetUnknownDataType() { GetLoadedScript()->SetUnknownDataType(); }

  void SetTextSource(LoadContextBase* maybeLoadContext) {
    GetLoadedScript()->SetTextSource(maybeLoadContext);
  }

  void SetWasmBytes() { GetLoadedScript()->SetWasmBytes(); }

  void SetSerializedStencil() { GetLoadedScript()->SetSerializedStencil(); }

  bool IsUTF16Text() const { return GetLoadedScript()->IsUTF16Text(); }
  bool IsUTF8Text() const { return GetLoadedScript()->IsUTF8Text(); }

  template <typename Unit>
  const ScriptTextBuffer<Unit>& ScriptText() const {
    const LoadedScript* loader = GetLoadedScript();
    return loader->ScriptText<Unit>();
  }
  template <typename Unit>
  ScriptTextBuffer<Unit>& ScriptText() {
    LoadedScript* loader = GetLoadedScript();
    return loader->ScriptText<Unit>();
  }

  ScriptTextBuffer<uint8_t>& WasmBytes() {
    LoadedScript* loader = GetLoadedScript();
    return loader->WasmBytes();
  }

  size_t ScriptTextLength() const {
    return GetLoadedScript()->ScriptTextLength();
  }

  size_t ReceivedScriptTextLength() const {
    return GetLoadedScript()->ReceivedScriptTextLength();
  }

  void SetReceivedScriptTextLength(size_t aLength) {
    GetLoadedScript()->SetReceivedScriptTextLength(aLength);
  }

  // Get source text.  On success |aMaybeSource| will contain either UTF-8 or
  // UTF-16 source; on failure it will remain in its initial state.
  nsresult GetScriptSource(JSContext* aCx, MaybeSourceText* aMaybeSource,
                           LoadContextBase* aLoadContext) {
    return GetLoadedScript()->GetScriptSource(aCx, aMaybeSource, aLoadContext);
  }

  bool HasNoSRIOrSRIAndSerializedStencil() const {
    return GetLoadedScript()->HasNoSRIOrSRIAndSerializedStencil();
  }

  TranscodeBuffer& SRI() { return GetLoadedScript()->SRI(); }
  TranscodeBuffer& SRIAndSerializedStencil() {
    return GetLoadedScript()->SRIAndSerializedStencil();
  }
  TranscodeRange SerializedStencil() const {
    return GetLoadedScript()->SerializedStencil();
  }
  TranscodeBuffer TakeSRIAndSerializedStencil() {
    return GetLoadedScript()->TakeSRIAndSerializedStencil();
  }
  void RestoreSRIAndSerializedStencil(TranscodeBuffer&& aBuffer) {
    GetLoadedScript()->RestoreSRIAndSerializedStencil(std::move(aBuffer));
  }

  size_t GetSerializedStencilOffset() const {
    return GetLoadedScript()->GetSerializedStencilOffset();
  }

  void SetAlignedSRILength(size_t aAlignedSRILength) {
    GetLoadedScript()->SetAlignedSRILength(aAlignedSRILength);
  }

  void SetTookLongInPreviousRuns() {
    GetLoadedScript()->SetTookLongInPreviousRuns();
  }
  bool TookLongInPreviousRuns() const {
    return GetLoadedScript()->TookLongInPreviousRuns();
  }
};

// A single module script. May be used to satisfy multiple load requests.

class ModuleScript final : public nsISupports {
  // Those fields are used only after instantiated, and they're reset to
  // null and false when stored into the cache as LoadedScript instance.
  Heap<JSObject*> mModuleRecord;
  Heap<Value> mParseError;
  Heap<Value> mErrorToRethrow;

  // A copy of ScriptLoadRequest::mFetchInfo, also used to access its
  // ScriptFetchInfo::mIsForModulePreload and preloaded resolved set fields.
  RefPtr<ScriptFetchInfo> mFetchInfo;

  bool mHadImportMap = false;

  ~ModuleScript();

 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(ModuleScript)

  explicit ModuleScript(ScriptFetchInfo* aFetchInfo);

  void SetModuleRecord(Handle<JSObject*> aModuleRecord);
  void SetParseError(const Value& aError);
  void SetErrorToRethrow(const Value& aError);
  void SetHadImportMap(bool aValue);

  JSObject* ModuleRecord() const { return mModuleRecord; }

  Value ParseError() const { return mParseError; }
  Value ErrorToRethrow() const { return mErrorToRethrow; }
  bool HasParseError() const { return !mParseError.isUndefined(); }
  bool HasErrorToRethrow() const { return !mErrorToRethrow.isUndefined(); }
  bool ForPreload() const { return mFetchInfo->IsForModulePreload(); }
  bool HadImportMap() const { return mHadImportMap; }

  ScriptFetchInfo* FetchInfo() const { return mFetchInfo; }

  // Clear the parse error and the error to rethrow recorded while loading a
  // preloaded module graph, so that a later load can retry.
  void ResetPreloadErrors();
  void Shutdown();

  friend void CheckModuleScriptPrivate(LoadedScript*, const Value&);
};

}  // namespace JS::loader

#endif  // js_loader_LoadedScript_h
