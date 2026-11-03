/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Logging.h"

#include "gfxUserFontSet.h"
#include "gfxPlatform.h"
#include "gfxFontConstants.h"
#include "mozilla/Atomics.h"
#include "mozilla/FontPropertyTypes.h"
#include "mozilla/Preferences.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/Telemetry.h"
#include "mozilla/gfx/2D.h"
#include "gfxPlatformFontList.h"
#include "mozilla/PostTraversalTask.h"
#include "mozilla/dom/WorkerCommon.h"
#include "gfxOTSUtils.h"
#include "nsIFontLoadCompleteCallback.h"
#include "nsProxyRelease.h"
#include "nsTHashSet.h"

using namespace mozilla;

mozilla::LogModule* gfxUserFontSet::GetUserFontsLog() {
  static LazyLogModule sLog("userfonts");
  return sLog;
}

#define LOG(args) \
  MOZ_LOG(gfxUserFontSet::GetUserFontsLog(), mozilla::LogLevel::Debug, args)
#define LOG_ENABLED() \
  MOZ_LOG_TEST(gfxUserFontSet::GetUserFontsLog(), mozilla::LogLevel::Debug)

static Atomic<uint64_t> sFontSetGeneration(0);

gfxUserFontEntry::gfxUserFontEntry(nsTArray<gfxFontFaceSrc>&& aFontFaceSrcList,
                                   gfxUserFontAttributes&& aAttr)
    : gfxFontEntry("userfont"_ns),
      mUserFontLoadState(STATUS_NOT_LOADED),
      mFontDataLoadingState(NOT_LOADING),
      mSeenLocalSource(false),
      mUnsupportedFormat(false),
      mFontDisplay(aAttr.mFontDisplay),
      mLoader(nullptr) {
  mIsUserFontContainer = true;
  mSrcList = std::move(aFontFaceSrcList);
  mCurrentSrcIndex = 0;
  mWeightRange = aAttr.mWeight;
  mStretchRange = aAttr.mStretch;
  mStyleRange = aAttr.mStyle;
  mFeatureSettings = std::move(aAttr.mFeatureSettings);
  mVariationSettings = std::move(aAttr.mVariationSettings);
  mLanguageOverride = aAttr.mLanguageOverride;
  SetUnicodeRangeMap(std::move(aAttr.mUnicodeRanges));
  mRangeFlags = aAttr.mRangeFlags;
  mAscentOverride = aAttr.mAscentOverride;
  mDescentOverride = aAttr.mDescentOverride;
  mLineGapOverride = aAttr.mLineGapOverride;
  mSizeAdjust = aAttr.mSizeAdjust;
  mFamilyName = aAttr.mFamilyName;
}

void gfxUserFontEntry::UpdateAttributes(gfxUserFontAttributes&& aAttr) {
  MOZ_ASSERT(NS_IsMainThread());

  // Remove the entry from the user font cache, if present there, as the cache
  // key may no longer be correct with the new attributes.
  gfxUserFontSet::UserFontCache::ForgetFont(this);

  mFontDisplay = aAttr.mFontDisplay;
  mWeightRange = aAttr.mWeight;
  mStretchRange = aAttr.mStretch;
  mStyleRange = aAttr.mStyle;
  mFeatureSettings = std::move(aAttr.mFeatureSettings);
  mVariationSettings = std::move(aAttr.mVariationSettings);
  mLanguageOverride = aAttr.mLanguageOverride;
  SetUnicodeRangeMap(std::move(aAttr.mUnicodeRanges));
  mRangeFlags = aAttr.mRangeFlags;
  mAscentOverride = aAttr.mAscentOverride;
  mDescentOverride = aAttr.mDescentOverride;
  mLineGapOverride = aAttr.mLineGapOverride;
  mSizeAdjust = aAttr.mSizeAdjust;
}

gfxUserFontEntry::~gfxUserFontEntry() {
  // Assert that we don't drop any gfxUserFontEntry objects during a Servo
  // traversal, since PostTraversalTask objects can hold raw pointers to
  // gfxUserFontEntry objects.
  MOZ_ASSERT(!gfxFontUtils::IsInServoTraversal());
  // Ensure the platform font entry is destroyed on the main thread, to avoid
  // potential race updating the sUserFonts cache.
  NS_ReleaseOnMainThread("gfxUserFontEntry::mPlatformFontEntry",
                         mPlatformFontEntry.forget());
}

bool gfxUserFontEntry::Matches(const nsTArray<gfxFontFaceSrc>& aFontFaceSrcList,
                               const gfxUserFontAttributes& aAttr) {
  return mWeightRange == aAttr.mWeight && mStretchRange == aAttr.mStretch &&
         mStyleRange == aAttr.mStyle &&
         mFeatureSettings == aAttr.mFeatureSettings &&
         mVariationSettings == aAttr.mVariationSettings &&
         mLanguageOverride == aAttr.mLanguageOverride &&
         mSrcList == aFontFaceSrcList && mFontDisplay == aAttr.mFontDisplay &&
         mRangeFlags == aAttr.mRangeFlags &&
         mAscentOverride == aAttr.mAscentOverride &&
         mDescentOverride == aAttr.mDescentOverride &&
         mLineGapOverride == aAttr.mLineGapOverride &&
         mSizeAdjust == aAttr.mSizeAdjust &&
         ((!aAttr.mUnicodeRanges && !mCharacterMap) ||
          (aAttr.mUnicodeRanges && mCharacterMap &&
           GetCharacterMap()->Equals(aAttr.mUnicodeRanges)));
}

gfxFont* gfxUserFontEntry::CreateFontInstance(const gfxFontStyle* aFontStyle) {
  MOZ_ASSERT_UNREACHABLE(
      "should only be creating a gfxFont"
      " with an actual platform font entry");

  // userfont entry is a container, can't create font from the container
  return nullptr;
}

class MOZ_STACK_CLASS gfxOTSMessageContext : public gfxOTSContext {
 public:
  virtual ~gfxOTSMessageContext() {
    MOZ_ASSERT(mMessages.IsEmpty(), "should have called TakeMessages");
  }

  virtual void Message(int level, const char* format,
                       ...) MSGFUNC_FMT_ATTR override {
    va_list va;
    va_start(va, format);

    nsCString msg;
    msg.AppendVprintf(format, va);

    va_end(va);

    if (level > 0) {
      // For warnings (rather than errors that cause the font to fail),
      // we only report the first instance of any given message.
      if (!mWarningsIssued.EnsureInserted(msg)) {
        return;
      }
    }

    mMessages.AppendElement(gfxUserFontEntry::OTSMessage{msg, level});
  }

  bool Process(ots::OTSStream* aOutput, const uint8_t* aInput, size_t aLength,
               nsTArray<gfxUserFontEntry::OTSMessage>& aMessages) {
    bool ok = ots::OTSContext::Process(aOutput, aInput, aLength);
    aMessages = TakeMessages();
    return ok;
  }

  nsTArray<gfxUserFontEntry::OTSMessage>&& TakeMessages() {
    return std::move(mMessages);
  }

 private:
  nsTHashSet<nsCString> mWarningsIssued;
  nsTArray<gfxUserFontEntry::OTSMessage> mMessages;
};

// Call the OTS library to sanitize an sfnt before attempting to use it.
// Returns a newly-allocated block, or nullptr in case of fatal errors.
const uint8_t* gfxUserFontEntry::SanitizeOpenTypeData(
    const uint8_t* aData, uint32_t aLength, uint32_t& aSanitaryLength,
    gfxUserFontType& aFontType, nsTArray<OTSMessage>& aMessages) {
  aFontType = gfxFontUtils::DetermineFontDataType(aData, aLength);
  Telemetry::Accumulate(Telemetry::WEBFONT_FONTTYPE, uint32_t(aFontType));

  size_t lengthHint = gfxOTSContext::GuessSanitizedFontSize(aLength, aFontType);
  if (!lengthHint) {
    aSanitaryLength = 0;
    return nullptr;
  }

  gfxOTSExpandingMemoryStream<gfxOTSMozAlloc> output(lengthHint);

  gfxOTSMessageContext otsContext;
  if (!otsContext.Process(&output, aData, aLength, aMessages)) {
    // Failed to decode/sanitize the font, so discard it.
    aSanitaryLength = 0;
    return nullptr;
  }

  aSanitaryLength = output.Tell();
  return static_cast<const uint8_t*>(output.forget());
}

void gfxUserFontEntry::StoreUserFontData(gfxFontEntry* aFontEntry,
                                         uint32_t aSrcIndex, bool aPrivate,
                                         const nsACString& aOriginalName,
                                         FallibleTArray<uint8_t>* aMetadata,
                                         uint32_t aMetaOrigLen,
                                         uint8_t aCompression) {
  if (!aFontEntry->mUserFontData) {
    aFontEntry->mUserFontData = MakeUnique<gfxUserFontData>();
  }
  gfxUserFontData* userFontData = aFontEntry->mUserFontData.get();
  userFontData->mSrcIndex = aSrcIndex;
  const gfxFontFaceSrc& src = mSrcList[aSrcIndex];
  switch (src.mSourceType) {
    case gfxFontFaceSrc::eSourceType_Local:
      userFontData->mLocalName = src.mLocalName;
      break;
    case gfxFontFaceSrc::eSourceType_URL:
      userFontData->mURI = src.mURI;
      userFontData->mPrincipal = mPrincipal;
      break;
    case gfxFontFaceSrc::eSourceType_Buffer:
      userFontData->mIsBuffer = true;
      break;
  }
  userFontData->mPrivate = aPrivate;
  userFontData->mTechFlags = src.mTechFlags;
  userFontData->mFormatHint = src.mFormatHint;
  userFontData->mRealName = aOriginalName;
  if (aMetadata) {
    userFontData->mMetadata = std::move(*aMetadata);
    userFontData->mMetaOrigLen = aMetaOrigLen;
    userFontData->mCompression = aCompression;
  }
}

size_t gfxUserFontData::SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const {
  return aMallocSizeOf(this) +
         mMetadata.ShallowSizeOfExcludingThis(aMallocSizeOf) +
         mLocalName.SizeOfExcludingThisIfUnshared(aMallocSizeOf) +
         mRealName.SizeOfExcludingThisIfUnshared(aMallocSizeOf);
  // Not counting mURI and mPrincipal, as those will be shared.
}

/*virtual*/
gfxUserFontFamily::~gfxUserFontFamily() {
  // Should not be dropped by stylo
  MOZ_ASSERT(!gfxFontUtils::IsInServoTraversal());
}

already_AddRefed<gfxFontSrcPrincipal> gfxFontFaceSrc::LoadPrincipal(
    const gfxUserFontSet& aFontSet) const {
  MOZ_ASSERT(mSourceType == eSourceType_URL);
  if (mUseOriginPrincipal) {
    MOZ_ASSERT(mOriginPrincipal);
    return RefPtr{mOriginPrincipal}.forget();
  }
  return aFontSet.GetStandardFontLoadPrincipal();
}

void gfxUserFontEntry::GetFamilyNameAndURIForLogging(uint32_t aSrcIndex,
                                                     nsACString& aFamilyName,
                                                     nsACString& aURI) {
  aFamilyName = FamilyName();

  aURI.Truncate();
  if (aSrcIndex >= mSrcList.Length()) {
    aURI.AppendLiteral("(end of source list)");
  } else {
    if (mSrcList[aSrcIndex].mURI) {
      mSrcList[aSrcIndex].mURI->GetSpec(aURI);
      // If the source URI was very long, elide the middle of it.
      // In principle, the byte-oriented chopping here could leave us
      // with partial UTF-8 characters at the point where we cut it,
      // but it really doesn't matter as this is just for logging.
      const uint32_t kMaxURILengthForLogging = 256;
      // UTF-8 ellipsis, with spaces to allow additional wrap opportunities
      // in the resulting log message
      const char kEllipsis[] = {' ', '\xE2', '\x80', '\xA6', ' '};
      if (aURI.Length() > kMaxURILengthForLogging) {
        aURI.Replace(kMaxURILengthForLogging / 2,
                     aURI.Length() - kMaxURILengthForLogging, kEllipsis,
                     ArrayLength(kEllipsis));
      }
    } else {
      aURI.AppendLiteral("(invalid URI)");
    }
  }
}

struct WOFFHeader {
  AutoSwap_PRUint32 signature;
  AutoSwap_PRUint32 flavor;
  AutoSwap_PRUint32 length;
  AutoSwap_PRUint16 numTables;
  AutoSwap_PRUint16 reserved;
  AutoSwap_PRUint32 totalSfntSize;
  AutoSwap_PRUint16 majorVersion;
  AutoSwap_PRUint16 minorVersion;
  AutoSwap_PRUint32 metaOffset;
  AutoSwap_PRUint32 metaCompLen;
  AutoSwap_PRUint32 metaOrigLen;
  AutoSwap_PRUint32 privOffset;
  AutoSwap_PRUint32 privLen;
};

struct WOFF2Header {
  AutoSwap_PRUint32 signature;
  AutoSwap_PRUint32 flavor;
  AutoSwap_PRUint32 length;
  AutoSwap_PRUint16 numTables;
  AutoSwap_PRUint16 reserved;
  AutoSwap_PRUint32 totalSfntSize;
  AutoSwap_PRUint32 totalCompressedSize;
  AutoSwap_PRUint16 majorVersion;
  AutoSwap_PRUint16 minorVersion;
  AutoSwap_PRUint32 metaOffset;
  AutoSwap_PRUint32 metaCompLen;
  AutoSwap_PRUint32 metaOrigLen;
  AutoSwap_PRUint32 privOffset;
  AutoSwap_PRUint32 privLen;
};

template <typename HeaderT>
void CopyWOFFMetadata(const uint8_t* aFontData, uint32_t aLength,
                      FallibleTArray<uint8_t>* aMetadata,
                      uint32_t* aMetaOrigLen) {
  // This function may be called with arbitrary, unvalidated "font" data
  // from @font-face, so it needs to be careful to bounds-check, etc.,
  // before trying to read anything.
  // This just saves a copy of the compressed data block; it does NOT check
  // that the block can be successfully decompressed, or that it contains
  // well-formed/valid XML metadata.
  if (aLength < sizeof(HeaderT)) {
    return;
  }
  const HeaderT* woff = reinterpret_cast<const HeaderT*>(aFontData);
  uint32_t metaOffset = woff->metaOffset;
  uint32_t metaCompLen = woff->metaCompLen;
  if (!metaOffset || !metaCompLen || !woff->metaOrigLen) {
    return;
  }
  if (metaOffset >= aLength || metaCompLen > aLength - metaOffset) {
    return;
  }
  if (!aMetadata->SetLength(woff->metaCompLen, fallible)) {
    return;
  }
  memcpy(aMetadata->Elements(), aFontData + metaOffset, metaCompLen);
  *aMetaOrigLen = woff->metaOrigLen;
}

void gfxUserFontEntry::LoadNextSrc() {
  NS_ASSERTION(mCurrentSrcIndex < mSrcList.Length(),
               "already at the end of the src list for user font");
  NS_ASSERTION((mUserFontLoadState == STATUS_NOT_LOADED ||
                mUserFontLoadState == STATUS_LOAD_PENDING ||
                mUserFontLoadState == STATUS_LOADING) &&
                   mFontDataLoadingState < LOADING_FAILED,
               "attempting to load a font that has either completed or failed");

  if (mUserFontLoadState == STATUS_NOT_LOADED) {
    SetLoadState(STATUS_LOADING);
    mFontDataLoadingState = LOADING_STARTED;
    mUnsupportedFormat = false;
  } else {
    // we were already loading; move to the next source,
    // but don't reset state - if we've already timed out,
    // that counts against the new download
    mCurrentSrcIndex++;
  }

  DoLoadNextSrc(false);
}

void gfxUserFontEntry::ContinueLoad() {
  MOZ_ASSERT(mUserFontLoadState == STATUS_LOAD_PENDING);
  MOZ_ASSERT(mSrcList[mCurrentSrcIndex].mSourceType ==
             gfxFontFaceSrc::eSourceType_URL);

  SetLoadState(STATUS_LOADING);
  DoLoadNextSrc(true);
  if (LoadState() != STATUS_LOADING) {
    MOZ_ASSERT(mUserFontLoadState != STATUS_LOAD_PENDING,
               "Not in parallel traversal, shouldn't get LOAD_PENDING again");
    // Loading is synchronously finished (loaded from cache or failed). We
    // need to increment the generation so that we flush the style data to
    // use the new loaded font face.
    // Without parallel traversal, we would simply get the right font data
    // after the first call to DoLoadNextSrc() in this case, so we don't need
    // to touch the generation to trigger another restyle.
    // XXX We may want to return synchronously in parallel traversal in those
    // cases as well if possible, so that we don't have an additional restyle.
    // That doesn't work currently because Document::GetDocShell (called from
    // FontFaceSet::CheckFontLoad) dereferences a weak pointer, which is not
    // allowed in parallel traversal.
    IncrementGeneration();
  }
}

static bool IgnorePrincipal(gfxFontSrcURI* aURI) {
  return aURI->InheritsSecurityContext();
}

void gfxUserFontEntry::DoLoadNextSrc(bool aForceAsync) {
  RefPtr<gfxUserFontSet> fontSet = GetUserFontSet();
  if (NS_WARN_IF(!fontSet)) {
    LOG(("userfonts (%p) failed expired font set for (%s)\n", fontSet.get(),
         FamilyName().get()));
    mFontDataLoadingState = LOADING_FAILED;
    SetLoadState(STATUS_FAILED);
    return;
  }

  uint32_t numSrc = mSrcList.Length();

  // load each src entry in turn, until a local face is found
  // or a download begins successfully
  while (mCurrentSrcIndex < numSrc) {
    gfxFontFaceSrc& currSrc = mSrcList[mCurrentSrcIndex];

    // src local ==> lookup and load immediately

    if (currSrc.mSourceType == gfxFontFaceSrc::eSourceType_Local) {
      // Don't look up local fonts if the font whitelist is being used.
      gfxPlatformFontList* pfl = gfxPlatformFontList::PlatformFontList();
      gfxFontEntry* fe = nullptr;
      if (!pfl->IsFontFamilyWhitelistActive()) {
        fe = gfxPlatform::GetPlatform()->LookupLocalFont(
            fontSet->GetPresContext(), currSrc.mLocalName, Weight(), Stretch(),
            SlantStyle());
        // Note that we've attempted a local lookup, even if it failed,
        // as this means we are dependent on any updates to the font list.
        mSeenLocalSource = true;
        nsTArray<RefPtr<gfxUserFontSet>> fontSets;
        GetUserFontSets(fontSets);
        for (gfxUserFontSet* fontSet : fontSets) {
          // We need to note on each gfxUserFontSet that contains the user
          // font entry that we used a local() rule.
          fontSet->SetLocalRulesUsed();
        }
      }
      if (fe) {
        LOG(("userfonts (%p) [src %d] loaded local: (%s) for (%s) gen: %8.8x\n",
             fontSet.get(), mCurrentSrcIndex, currSrc.mLocalName.get(),
             FamilyName().get(), uint32_t(fontSet->mGeneration)));
        fe->mFeatureSettings.AppendElements(mFeatureSettings);
        fe->mVariationSettings.AppendElements(mVariationSettings);
        fe->mLanguageOverride = mLanguageOverride;
        fe->SetFamilyName(FamilyName());
        fe->mRangeFlags = mRangeFlags;
        fe->mAscentOverride = mAscentOverride;
        fe->mDescentOverride = mDescentOverride;
        fe->mLineGapOverride = mLineGapOverride;
        fe->mSizeAdjust = mSizeAdjust;
        // For src:local(), we don't care whether the request is from
        // a private window as there's no issue of caching resources;
        // local fonts are just available all the time.
        StoreUserFontData(fe, mCurrentSrcIndex, false, nsCString(), nullptr, 0,
                          gfxUserFontData::kUnknownCompression);
        SetPlatformFontEntry(std::move(fe));
        SetLoadState(STATUS_LOADED);
        Telemetry::Accumulate(Telemetry::WEBFONT_SRCTYPE,
                              currSrc.mSourceType + 1);
        return;
      } else {
        LOG(("userfonts (%p) [src %d] failed local: (%s) for (%s)\n",
             fontSet.get(), mCurrentSrcIndex, currSrc.mLocalName.get(),
             FamilyName().get()));
      }
    }

    // src url ==> start the load process
    else if (currSrc.mSourceType == gfxFontFaceSrc::eSourceType_URL) {
      if (gfxPlatform::GetPlatform()->IsFontFormatSupported(
              currSrc.mFormatHint, currSrc.mTechFlags)) {
        if (ServoStyleSet* set = gfxFontUtils::CurrentServoStyleSet()) {
          // Only support style worker threads synchronously getting
          // entries from the font cache when it's not a data: URI
          // @font-face that came from UA or user sheets, since we
          // were not able to call IsFontLoadAllowed ahead of time
          // for these entries.
          if (currSrc.mUseOriginPrincipal && IgnorePrincipal(currSrc.mURI)) {
            set->AppendTask(PostTraversalTask::LoadFontEntry(do_AddRef(this)));
            SetLoadState(STATUS_LOAD_PENDING);
            return;
          }
        }

        // see if we have an existing entry for this source
        RefPtr<gfxFontEntry> fe =
            gfxUserFontSet::UserFontCache::GetFont(currSrc, *this);
        if (fe) {
          SetPlatformFontEntry(std::move(fe));
          SetLoadState(STATUS_LOADED);
          if (LOG_ENABLED()) {
            LOG(
                ("userfonts (%p) [src %d] "
                 "loaded uri from cache: (%s) for (%s)\n",
                 fontSet.get(), mCurrentSrcIndex,
                 currSrc.mURI->GetSpecOrDefault().get(), FamilyName().get()));
          }
          return;
        }

        if (ServoStyleSet* set = gfxFontUtils::CurrentServoStyleSet()) {
          // If we need to start a font load and we're on a style
          // worker thread, we have to defer it.
          SetLoadState(STATUS_LOAD_PENDING);
          set->AppendTask(PostTraversalTask::LoadFontEntry(do_AddRef(this)));
          return;
        }

        if (dom::IsCurrentThreadRunningWorker()) {
          // TODO: Maybe support loading the font entry in workers, at least for
          // buffers or other sync sources?
          SetLoadState(STATUS_LOAD_PENDING);
          return;
        }

        // record the principal we should use for the load for use when
        // creating a channel and when caching the loaded entry.
        mPrincipal = currSrc.LoadPrincipal(*fontSet);

        bool loadDoesntSpin = !aForceAsync && currSrc.mURI->SyncLoadIsOK();

        if (loadDoesntSpin) {
          uint8_t* buffer = nullptr;
          uint32_t bufferLength = 0;

          // sync load font immediately
          nsresult rv =
              fontSet->SyncLoadFontData(this, &currSrc, buffer, bufferLength);

          if (NS_SUCCEEDED(rv) &&
              LoadPlatformFontSync(mCurrentSrcIndex, buffer, bufferLength)) {
            SetLoadState(STATUS_LOADED);
            Telemetry::Accumulate(Telemetry::WEBFONT_SRCTYPE,
                                  currSrc.mSourceType + 1);
            return;
          } else {
            fontSet->LogMessage(this, mCurrentSrcIndex, "font load failed",
                                nsIScriptError::errorFlag, rv);
          }

        } else {
          // otherwise load font async
          nsresult rv = fontSet->StartLoad(this, mCurrentSrcIndex);
          bool loadOK = NS_SUCCEEDED(rv);

          if (loadOK) {
            if (LOG_ENABLED()) {
              LOG(("userfonts (%p) [src %d] loading uri: (%s) for (%s)\n",
                   fontSet.get(), mCurrentSrcIndex,
                   currSrc.mURI->GetSpecOrDefault().get(), FamilyName().get()));
            }
            return;
          } else {
            fontSet->LogMessage(this, mCurrentSrcIndex,
                                "failed to start download",
                                nsIScriptError::errorFlag, rv);
          }
        }
      } else {
        // We don't log a warning to the web console yet,
        // as another source may load successfully
        mUnsupportedFormat = true;
      }
    }

    // FontFace buffer ==> load immediately

    else {
      MOZ_ASSERT(currSrc.mSourceType == gfxFontFaceSrc::eSourceType_Buffer);

      uint8_t* buffer = nullptr;
      uint32_t bufferLength = 0;

      // sync load font immediately
      currSrc.mBuffer->TakeBuffer(buffer, bufferLength);
      if (buffer &&
          LoadPlatformFontSync(mCurrentSrcIndex, buffer, bufferLength)) {
        // LoadPlatformFontSync takes ownership of the buffer, so no need
        // to free it here.
        SetLoadState(STATUS_LOADED);
        Telemetry::Accumulate(Telemetry::WEBFONT_SRCTYPE,
                              currSrc.mSourceType + 1);
        return;
      } else {
        fontSet->LogMessage(this, mCurrentSrcIndex, "font load failed",
                            nsIScriptError::errorFlag);
      }
    }

    mCurrentSrcIndex++;
  }

  if (mUnsupportedFormat) {
    fontSet->LogMessage(this, mCurrentSrcIndex, "no supported format found",
                        nsIScriptError::warningFlag);
  }

  // all src's failed; mark this entry as unusable (so fallback will occur)
  LOG(("userfonts (%p) failed all src for (%s)\n", fontSet.get(),
       FamilyName().get()));
  mFontDataLoadingState = LOADING_FAILED;
  SetLoadState(STATUS_FAILED);
}

void gfxUserFontEntry::SetLoadState(UserFontLoadState aLoadState) {
  mUserFontLoadState = aLoadState;
}

MOZ_DEFINE_MALLOC_SIZE_OF_ON_ALLOC(UserFontMallocSizeOfOnAlloc)

bool gfxUserFontEntry::LoadPlatformFontSync(uint32_t aSrcIndex,
                                            const uint8_t* aFontData,
                                            uint32_t aLength) {
  AUTO_PROFILER_LABEL("gfxUserFontEntry::LoadPlatformFontSync", OTHER);
  NS_ASSERTION((mUserFontLoadState == STATUS_NOT_LOADED ||
                mUserFontLoadState == STATUS_LOAD_PENDING ||
                mUserFontLoadState == STATUS_LOADING) &&
                   mFontDataLoadingState < LOADING_FAILED,
               "attempting to load a font that has either completed or failed");

  // Unwrap/decompress/sanitize or otherwise munge the downloaded data
  // to make a usable sfnt structure.

  // Call the OTS sanitizer; this will also decode WOFF to sfnt
  // if necessary. The original data in aFontData is left unchanged.
  uint32_t sanitaryLen;
  gfxUserFontType fontType;
  nsTArray<OTSMessage> messages;
  const uint8_t* sanitaryData =
      SanitizeOpenTypeData(aFontData, aLength, sanitaryLen, fontType, messages);

  return LoadPlatformFont(aSrcIndex, aFontData, aLength, fontType, sanitaryData,
                          sanitaryLen, std::move(messages));
}

void gfxUserFontEntry::StartPlatformFontLoadOnBackgroundThread(
    uint32_t aSrcIndex, const uint8_t* aFontData, uint32_t aLength,
    nsMainThreadPtrHandle<nsIFontLoadCompleteCallback> aCallback) {
  MOZ_ASSERT(!NS_IsMainThread());

  uint32_t sanitaryLen;
  gfxUserFontType fontType;
  nsTArray<OTSMessage> messages;
  const uint8_t* sanitaryData =
      SanitizeOpenTypeData(aFontData, aLength, sanitaryLen, fontType, messages);

  nsCOMPtr<nsIRunnable> event =
      NewRunnableMethod<uint32_t, const uint8_t*, uint32_t, gfxUserFontType,
                        const uint8_t*, uint32_t, nsTArray<OTSMessage>&&,
                        nsMainThreadPtrHandle<nsIFontLoadCompleteCallback>>(
          "gfxUserFontEntry::ContinuePlatformFontLoadOnMainThread", this,
          &gfxUserFontEntry::ContinuePlatformFontLoadOnMainThread, aSrcIndex,
          aFontData, aLength, fontType, sanitaryData, sanitaryLen,
          std::move(messages), aCallback);
  NS_DispatchToMainThread(event.forget());
}

bool gfxUserFontEntry::LoadPlatformFont(uint32_t aSrcIndex,
                                        const uint8_t* aOriginalFontData,
                                        uint32_t aOriginalLength,
                                        gfxUserFontType aFontType,
                                        const uint8_t* aSanitizedFontData,
                                        uint32_t aSanitizedLength,
                                        nsTArray<OTSMessage>&& aMessages) {
  MOZ_ASSERT(NS_IsMainThread());
  RefPtr<gfxUserFontSet> fontSet = GetUserFontSet();
  if (NS_WARN_IF(!fontSet)) {
    free((void*)aOriginalFontData);
    return false;
  }

  for (const auto& msg : aMessages) {
    fontSet->LogMessage(this, aSrcIndex, msg.mMessage.get(),
                        msg.mLevel > 0 ? nsIScriptError::warningFlag
                                       : nsIScriptError::errorFlag);
  }

  if (!aSanitizedFontData) {
    fontSet->LogMessage(this, aSrcIndex, "rejected by sanitizer");
  } else {
    // Check whether aSanitizedFontData is a known OpenType format; it might be
    // a TrueType Collection, which OTS would accept but we don't yet
    // know how to handle. If so, discard.
    if (gfxFontUtils::DetermineFontDataType(
            aSanitizedFontData, aSanitizedLength) != GFX_USERFONT_OPENTYPE) {
      fontSet->LogMessage(this, aSrcIndex, "not a supported OpenType format");
      free((void*)aSanitizedFontData);
      aSanitizedFontData = nullptr;
    }
  }

  // Because platform font activation code may replace the name table
  // in the font with a synthetic one, we save the original name so that
  // it can be reported via the InspectorUtils API.
  nsAutoCString originalFullName;

  RefPtr<gfxFontEntry> fe;
  uint32_t fontCompressionRatio = 0;
  size_t computedSize = 0;

  if (aSanitizedFontData) {
    if (aSanitizedLength) {
      fontCompressionRatio =
          uint32_t(100.0 * aOriginalLength / aSanitizedLength + 0.5);
      if (aFontType == GFX_USERFONT_WOFF || aFontType == GFX_USERFONT_WOFF2) {
        Telemetry::Accumulate(aFontType == GFX_USERFONT_WOFF
                                  ? Telemetry::WEBFONT_COMPRESSION_WOFF
                                  : Telemetry::WEBFONT_COMPRESSION_WOFF2,
                              fontCompressionRatio);
      }
    }

    // The sanitizer ensures that we have a valid sfnt and a usable
    // name table, so this should never fail unless we're out of
    // memory, and GetFullNameFromSFNT is not directly exposed to
    // arbitrary/malicious data from the web.
    gfxFontUtils::GetFullNameFromSFNT(aSanitizedFontData, aSanitizedLength,
                                      originalFullName);

    // Record size for memory reporting purposes. We measure this now
    // because by the time we potentially want to collect reports, this
    // data block may have been handed off to opaque OS font APIs that
    // don't allow us to retrieve or measure it directly.
    // The *OnAlloc function will also tell DMD about this block, as the
    // OS font code may hold on to it for an extended period.
    computedSize = UserFontMallocSizeOfOnAlloc(aSanitizedFontData);

    // Here ownership of aSanitizedFontData is passed to the platform,
    // which will delete it when no longer required
    fe = gfxPlatform::GetPlatform()->MakePlatformFont(
        mName, Weight(), Stretch(), SlantStyle(), aSanitizedFontData,
        aSanitizedLength);
    if (!fe) {
      fontSet->LogMessage(this, aSrcIndex, "not usable by platform");
    }
  }

  bool ok = true;
  if (fe) {
    fe->mComputedSizeOfUserFont = computedSize;

    // Save a copy of the metadata block (if present) for InspectorUtils
    // to use if required. Ownership of the metadata block will be passed
    // to the gfxUserFontData record below.
    FallibleTArray<uint8_t> metadata;
    uint32_t metaOrigLen = 0;
    uint8_t compression = gfxUserFontData::kUnknownCompression;
    if (aFontType == GFX_USERFONT_WOFF) {
      CopyWOFFMetadata<WOFFHeader>(aOriginalFontData, aOriginalLength,
                                   &metadata, &metaOrigLen);
      compression = gfxUserFontData::kZlibCompression;
    } else if (aFontType == GFX_USERFONT_WOFF2) {
      CopyWOFFMetadata<WOFF2Header>(aOriginalFontData, aOriginalLength,
                                    &metadata, &metaOrigLen);
      compression = gfxUserFontData::kBrotliCompression;
    }

    // copy OpenType feature/language settings from the userfont entry to the
    // newly-created font entry
    fe->mFeatureSettings.AppendElements(mFeatureSettings);
    fe->mVariationSettings.AppendElements(mVariationSettings);
    fe->mLanguageOverride = mLanguageOverride;
    fe->SetFamilyName(FamilyName());
    fe->mRangeFlags = mRangeFlags;
    fe->mAscentOverride = mAscentOverride;
    fe->mDescentOverride = mDescentOverride;
    fe->mLineGapOverride = mLineGapOverride;
    fe->mSizeAdjust = mSizeAdjust;
    StoreUserFontData(fe, aSrcIndex, fontSet->GetPrivateBrowsing(),
                      originalFullName, &metadata, metaOrigLen, compression);
    if (LOG_ENABLED()) {
      LOG((
          "userfonts (%p) [src %d] loaded uri: (%s) for (%s) "
          "(%p) gen: %8.8x compress: %d%%\n",
          fontSet.get(), aSrcIndex,
          mSrcList[aSrcIndex].mURI->GetSpecOrDefault().get(), FamilyName().get(),
          this, uint32_t(fontSet->mGeneration), fontCompressionRatio));
    }
    gfxUserFontSet::UserFontCache::CacheFont(fe);
    SetPlatformFontEntry(std::move(fe));
    SetLoadState(STATUS_LOADED);
  } else {
    if (LOG_ENABLED()) {
      LOG(
          ("userfonts (%p) [src %d] failed uri: (%s) for (%s)"
           " error making platform font\n",
           fontSet.get(), aSrcIndex,
           mSrcList[aSrcIndex].mURI->GetSpecOrDefault().get(),
           FamilyName().get()));
    }
    ok = false;
  }

  // The downloaded data can now be discarded; the font entry is using the
  // sanitized copy
  free((void*)aOriginalFontData);

  return ok;
}

void gfxUserFontEntry::Load() {
  if (mUserFontLoadState != STATUS_NOT_LOADED) {
    return;
  }
  if (dom::IsCurrentThreadRunningWorker()) {
    // TODO: Maybe support loading the font entry in workers, at least for
    // buffers or other sync sources?
    NS_DispatchToMainThread(NewRunnableMethod("gfxUserFontEntry::Load", this,
                                              &gfxUserFontEntry::Load));
    return;
  }
  LoadNextSrc();
}

void gfxUserFontEntry::IncrementGeneration() {
  nsTArray<RefPtr<gfxUserFontSet>> fontSets;
  GetUserFontSets(fontSets);
  for (gfxUserFontSet* fontSet : fontSets) {
    fontSet->IncrementGeneration();
  }
}

// This is called when a font download finishes.
// Ownership of aFontData passes in here, and the font set must
// ensure that it is eventually deleted via free().
void gfxUserFontEntry::FontDataDownloadComplete(
    uint32_t aSrcIndex, const uint8_t* aFontData, uint32_t aLength,
    nsresult aDownloadStatus, nsIFontLoadCompleteCallback* aCallback) {
  MOZ_ASSERT(NS_IsMainThread());

  // forget about the loader, as we no longer potentially need to cancel it
  // if the entry is obsoleted
  mLoader = nullptr;

  // download successful, make platform font using font data
  if (NS_SUCCEEDED(aDownloadStatus) &&
      mFontDataLoadingState != LOADING_TIMED_OUT) {
    if (StaticPrefs::gfx_downloadable_fonts_sanitize_omt()) {
      LoadPlatformFontAsync(aSrcIndex, aFontData, aLength, aCallback);
    } else {
      bool loaded = LoadPlatformFontSync(aSrcIndex, aFontData, aLength);
      aFontData = nullptr;
      if (loaded) {
        IncrementGeneration();
        aCallback->FontLoadComplete();
      } else {
        FontLoadFailed(aCallback);
      }
    }
    return;
  }

  RefPtr<gfxUserFontSet> fontSet = GetUserFontSet();
  if (fontSet) {
    // download failed or font-display timeout passed
    if (mFontDataLoadingState == LOADING_TIMED_OUT) {
      fontSet->LogMessage(this, aSrcIndex,
                          "font-display timeout, webfont not used",
                          nsIScriptError::infoFlag, aDownloadStatus);
    } else {
      fontSet->LogMessage(this, aSrcIndex, "download failed",
                          nsIScriptError::errorFlag, aDownloadStatus);
    }
  }

  if (aFontData) {
    free((void*)aFontData);
  }

  FontLoadFailed(aCallback);
}

void gfxUserFontEntry::LoadPlatformFontAsync(
    uint32_t aSrcIndex, const uint8_t* aFontData, uint32_t aLength,
    nsIFontLoadCompleteCallback* aCallback) {
  nsMainThreadPtrHandle<nsIFontLoadCompleteCallback> cb(
      new nsMainThreadPtrHolder<nsIFontLoadCompleteCallback>("FontLoader",
                                                             aCallback));

  // Do the OpenType sanitization over on the font loading thread.  Once that is
  // complete, we'll continue in ContinuePlatformFontLoadOnMainThread.
  //
  // We hold a strong reference to the gfxUserFontSet during this work, since
  // the document might be closed while we are OMT, and release it at the end
  // of ContinuePlatformFontLoadOnMainThread.
  //
  // If the set has already been freed, then the loading will fail when we
  // resume on the main thread.

  MOZ_ASSERT(!mLoadingFontSet);
  mLoadingFontSet = GetUserFontSet();

  nsCOMPtr<nsIRunnable> event =
      NewRunnableMethod<uint32_t, const uint8_t*, uint32_t,
                        nsMainThreadPtrHandle<nsIFontLoadCompleteCallback>>(
          "gfxUserFontEntry::StartPlatformFontLoadOnBackgroundThread", this,
          &gfxUserFontEntry::StartPlatformFontLoadOnBackgroundThread, aSrcIndex,
          aFontData, aLength, cb);
  MOZ_ALWAYS_SUCCEEDS(NS_DispatchBackgroundTask(event.forget()));
}

void gfxUserFontEntry::ContinuePlatformFontLoadOnMainThread(
    uint32_t aSrcIndex, const uint8_t* aOriginalFontData,
    uint32_t aOriginalLength, gfxUserFontType aFontType,
    const uint8_t* aSanitizedFontData, uint32_t aSanitizedLength,
    nsTArray<OTSMessage>&& aMessages,
    nsMainThreadPtrHandle<nsIFontLoadCompleteCallback> aCallback) {
  MOZ_ASSERT(NS_IsMainThread());

  bool loaded = LoadPlatformFont(aSrcIndex, aOriginalFontData, aOriginalLength,
                                 aFontType, aSanitizedFontData,
                                 aSanitizedLength, std::move(aMessages));
  aOriginalFontData = nullptr;
  aSanitizedFontData = nullptr;

  if (loaded) {
    IncrementGeneration();
    aCallback->FontLoadComplete();
  } else {
    FontLoadFailed(aCallback);
  }

  // Set in LoadPlatformFontAsync. If it is null, then the font set should have
  // already been freed and we would not succeed in loading the font.
  MOZ_ASSERT_IF(loaded, mLoadingFontSet);
  mLoadingFontSet = nullptr;
}

void gfxUserFontEntry::FontLoadFailed(nsIFontLoadCompleteCallback* aCallback) {
  MOZ_ASSERT(NS_IsMainThread());

  // Error occurred.  Make sure the FontFace's promise is rejected if the
  // load timed out, or else load the next src.
  if (mFontDataLoadingState == LOADING_TIMED_OUT) {
    mFontDataLoadingState = LOADING_FAILED;
    SetLoadState(STATUS_FAILED);
  } else {
    LoadNextSrc();
  }

  // We ignore the status returned by LoadNext();
  // even if loading failed, we need to bump the font-set generation
  // and return true in order to trigger reflow, so that fallback
  // will be used where the text was "masked" by the pending download
  IncrementGeneration();
  aCallback->FontLoadComplete();
}

void gfxUserFontEntry::GetUserFontSets(
    nsTArray<RefPtr<gfxUserFontSet>>& aResult) {
  aResult.Clear();
  RefPtr<gfxUserFontSet> fontSet = GetUserFontSet();
  if (fontSet) {
    aResult.AppendElement(std::move(fontSet));
  }
}

gfxUserFontSet::gfxUserFontSet()
    : mFontFamilies(4),
      mRebuildGeneration(0),
      mLocalRulesUsed(false),
      mRebuildLocalRules(false),
      mDownloadCount(0),
      mDownloadSize(0) {
  IncrementGeneration(true);
  gfxPlatformFontList* fp = gfxPlatformFontList::PlatformFontList();
  if (fp) {
    fp->AddUserFontSet(this);
  }
}

gfxUserFontSet::~gfxUserFontSet() { Destroy(); }

void gfxUserFontSet::Destroy() {
  gfxPlatformFontList* fp = gfxPlatformFontList::PlatformFontList();
  if (fp) {
    fp->RemoveUserFontSet(this);
  }

  mFontFamilies.Clear();
}

already_AddRefed<gfxUserFontEntry> gfxUserFontSet::FindOrCreateUserFontEntry(
    nsTArray<gfxFontFaceSrc>&& aFontFaceSrcList,
    gfxUserFontAttributes&& aAttr) {
  RefPtr<gfxUserFontEntry> entry;

  // If there's already a userfont entry in the family whose descriptors all
  // match, we can just move it to the end of the list instead of adding a new
  // face that will always "shadow" the old one.
  // Note that we can't do this for platform font entries, even if the
  // style descriptors match, as they might have had a different source list,
  // but we no longer have the old source list available to check.
  RefPtr<gfxUserFontFamily> family = LookupFamily(aAttr.mFamilyName);
  if (family) {
    entry = FindExistingUserFontEntry(family, aFontFaceSrcList, aAttr);
  }

  if (!entry) {
    entry = CreateUserFontEntry(std::move(aFontFaceSrcList), std::move(aAttr));
  }

  return entry.forget();
}

gfxUserFontEntry* gfxUserFontSet::FindExistingUserFontEntry(
    gfxUserFontFamily* aFamily,
    const nsTArray<gfxFontFaceSrc>& aFontFaceSrcList,
    const gfxUserFontAttributes& aAttr) {
  aFamily->ReadLock();
  const auto& fontList = aFamily->GetFontList();
  gfxUserFontEntry* result = nullptr;

  for (const auto& font : fontList) {
    if (!font->mIsUserFontContainer) {
      continue;
    }

    gfxUserFontEntry* ufe = static_cast<gfxUserFontEntry*>(font.get());
    if (ufe->Matches(aFontFaceSrcList, aAttr)) {
      result = ufe;
      break;
    }
  }
  aFamily->ReadUnlock();

  return result;
}

void gfxUserFontSet::AddUserFontEntry(const nsCString& aFamilyName,
                                      gfxUserFontEntry* aUserFontEntry) {
  RefPtr<gfxUserFontFamily> family = GetFamily(aFamilyName);
  family->AddFontEntry(aUserFontEntry);

  if (LOG_ENABLED()) {
    nsAutoCString weightString;
    aUserFontEntry->Weight().ToString(weightString);
    nsAutoCString stretchString;
    aUserFontEntry->Stretch().ToString(stretchString);
    LOG(
        ("userfonts (%p) added to \"%s\" (%p) style: %s weight: %s "
         "stretch: %s display: %d",
         this, aFamilyName.get(), aUserFontEntry,
         (aUserFontEntry->IsItalic()
              ? "italic"
              : (aUserFontEntry->IsOblique() ? "oblique" : "normal")),
         weightString.get(), stretchString.get(),
         static_cast<int>(aUserFontEntry->GetFontDisplay())));
  }
}

void gfxUserFontSet::IncrementGeneration(bool aIsRebuild) {
  // add one, increment again if zero
  do {
    mGeneration = ++sFontSetGeneration;
  } while (mGeneration == 0);
  if (aIsRebuild) {
    mRebuildGeneration = mGeneration;
  }
}

void gfxUserFontSet::RebuildLocalRules() {
  if (mLocalRulesUsed) {
    mRebuildLocalRules = true;
    DoRebuildUserFontSet();
  }
}

already_AddRefed<gfxUserFontFamily> gfxUserFontSet::LookupFamily(
    const nsACString& aFamilyName) const {
  nsAutoCString key(aFamilyName);
  ToLowerCase(key);

  return mFontFamilies.Get(key);
}

already_AddRefed<gfxUserFontFamily> gfxUserFontSet::GetFamily(
    const nsACString& aFamilyName) {
  nsAutoCString key(aFamilyName);
  ToLowerCase(key);

  return do_AddRef(mFontFamilies.GetOrInsertNew(key, aFamilyName));
}

void gfxUserFontSet::ForgetLocalFaces() {
  for (const auto& fam : mFontFamilies.Values()) {
    ForgetLocalFace(fam);
  }
}

void gfxUserFontSet::ForgetLocalFace(gfxUserFontFamily* aFontFamily) {
  aFontFamily->ReadLock();
  const auto& fonts = aFontFamily->GetFontList();
  for (const auto& f : fonts) {
    auto ufe = static_cast<gfxUserFontEntry*>(f.get());
    // If the user font entry has loaded an entry using src:local(),
    // discard it as no longer valid.
    RefPtr pfe = ufe->GetPlatformFontEntry();
    if (pfe && pfe->IsLocalUserFont()) {
      // Clear shmem pointers on the local-user-font entry: it is not tracked
      // in gfxPlatformFontList::mFontEntries, so InitFontList()'s cleanup loop
      // misses it.
      {
        AutoWriteLock lock(pfe->mLock);
        pfe->mShmemCharacterMap = nullptr;
        pfe->mShmemFace = nullptr;
        pfe->mShmemFamily = nullptr;
      }
      ufe->SetPlatformFontEntry(nullptr);
    }
    // We need to re-evaluate the source list in the context of the new
    // platform fontlist, whether or not the entry actually used a local()
    // source last time, as one might be newly available.
    if (ufe->mSeenLocalSource) {
      ufe->LoadCanceled();
    }
  }
  aFontFamily->ReadUnlock();
}

///////////////////////////////////////////////////////////////////////////////
// gfxUserFontSet::UserFontCache - re-use platform font entries for user fonts
// across pages/fontsets rather than instantiating new platform fonts.
//
// Entries are added to this cache when a platform font is instantiated from
// downloaded data, and removed when the platform font entry is destroyed.
// We don't need to use a timed expiration scheme here because the gfxFontEntry
// for a downloaded font will be kept alive by its corresponding gfxFont
// instance(s) until they are deleted, and *that* happens using an expiration
// tracker (gfxFontCache). The result is that the downloaded font instances
// recorded here will persist between pages and can get reused (provided the
// source URI and principal match, of course).
///////////////////////////////////////////////////////////////////////////////

nsTHashtable<gfxUserFontSet::UserFontCache::Entry>*
    gfxUserFontSet::UserFontCache::sUserFonts = nullptr;

NS_IMPL_ISUPPORTS(gfxUserFontSet::UserFontCache::Flusher, nsIObserver)

NS_IMETHODIMP
gfxUserFontSet::UserFontCache::Flusher::Observe(nsISupports* aSubject,
                                                const char* aTopic,
                                                const char16_t* aData) {
  if (!sUserFonts) {
    return NS_OK;
  }

  if (!strcmp(aTopic, "cacheservice:empty-cache")) {
    for (auto i = sUserFonts->Iter(); !i.Done(); i.Next()) {
      i.Remove();
    }
  } else if (!strcmp(aTopic, "last-pb-context-exited")) {
    for (auto i = sUserFonts->Iter(); !i.Done(); i.Next()) {
      if (i.Get()->IsPrivate()) {
        i.Remove();
      }
    }
  } else if (!strcmp(aTopic, "xpcom-shutdown")) {
    for (auto i = sUserFonts->Iter(); !i.Done(); i.Next()) {
      i.Get()->GetFontEntry()->DisconnectSVG();
    }
  } else {
    MOZ_ASSERT_UNREACHABLE("unexpected topic");
  }

  return NS_OK;
}

bool gfxUserFontSet::UserFontCache::Entry::KeyEquals(
    const KeyTypePointer aKey) const {
  const gfxFontEntry* fe = aKey->mFontEntry;

  if (!mURI->Equals(aKey->mURI)) {
    return false;
  }

  // For data: URIs, we don't care about the principal; otherwise, check it.
  if (!IgnorePrincipal(mURI)) {
    NS_ASSERTION(mPrincipal && aKey->mPrincipal,
                 "only data: URIs are allowed to omit the principal");
    if (!mPrincipal->Equals(aKey->mPrincipal)) {
      return false;
    }
  }

  if (mPrivate != aKey->mPrivate) {
    return false;
  }

  if (mFontEntry->SlantStyle() != fe->SlantStyle() ||
      mFontEntry->Weight() != fe->Weight() ||
      mFontEntry->Stretch() != fe->Stretch() ||
      mFontEntry->mRangeFlags != fe->mRangeFlags ||
      mFontEntry->mFeatureSettings != fe->mFeatureSettings ||
      mFontEntry->mVariationSettings != fe->mVariationSettings ||
      mFontEntry->mLanguageOverride != fe->mLanguageOverride ||
      mFontEntry->mAscentOverride != fe->mAscentOverride ||
      mFontEntry->mDescentOverride != fe->mDescentOverride ||
      mFontEntry->mLineGapOverride != fe->mLineGapOverride ||
      mFontEntry->mSizeAdjust != fe->mSizeAdjust ||
      mFontEntry->FamilyName() != fe->FamilyName()) {
    return false;
  }

  return true;
}

void gfxUserFontSet::UserFontCache::CacheFont(gfxFontEntry* aFontEntry) {
  MOZ_ASSERT(NS_IsMainThread());
  NS_ASSERTION(aFontEntry->FamilyName().Length() != 0,
               "caching a font associated with no family yet");

  // if caching is disabled, simply return
  if (Preferences::GetBool("gfx.downloadable_fonts.disable_cache")) {
    return;
  }

  gfxUserFontData* data = aFontEntry->mUserFontData.get();
  if (data->mIsBuffer) {
#ifdef DEBUG_USERFONT_CACHE
    printf("userfontcache skipped fontentry with buffer source: %p\n",
           aFontEntry);
#endif
    return;
  }

  if (!sUserFonts) {
    sUserFonts = new nsTHashtable<Entry>;

    nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
    if (obs) {
      Flusher* flusher = new Flusher;
      obs->AddObserver(flusher, "cacheservice:empty-cache", false);
      obs->AddObserver(flusher, "last-pb-context-exited", false);
      obs->AddObserver(flusher, "xpcom-shutdown", false);
    }

    // Create and register a memory reporter for sUserFonts.
    // This reporter is never unregistered, but that's OK because
    // the reporter checks whether sUserFonts is null, so it would
    // be safe to call even after UserFontCache::Shutdown has deleted
    // the cache.
    RegisterStrongMemoryReporter(new MemoryReporter());
  }

  // For data: URIs, the principal is ignored; anyone who has the same
  // data: URI is able to load it and get an equivalent font.
  // Otherwise, the principal is used as part of the cache key.
  gfxFontSrcPrincipal* principal;
  if (IgnorePrincipal(data->mURI)) {
    principal = nullptr;
  } else {
    principal = data->mPrincipal;
  }
  sUserFonts->PutEntry(Key(data->mURI, principal, aFontEntry, data->mPrivate));

#ifdef DEBUG_USERFONT_CACHE
  printf("userfontcache added fontentry: %p\n", aFontEntry);
  Dump();
#endif
}

void gfxUserFontSet::UserFontCache::ForgetFont(gfxFontEntry* aFontEntry) {
  // The only caller is ~gfxFontEntry; if this fires, there is a path to
  // off-main-thread destruction of a font entry with mIsDataUserFont set
  // that needs to be proxied to main thread (see ~gfxUserFontEntry).
  MOZ_ASSERT(NS_IsMainThread());
  if (!sUserFonts) {
    // if we've already deleted the cache (i.e. during shutdown),
    // just ignore this
    return;
  }

  // We can't simply use RemoveEntry here because it's possible the principal
  // may have changed since the font was cached, in which case the lookup
  // would no longer find the entry (bug 838105).
  for (auto i = sUserFonts->Iter(); !i.Done(); i.Next()) {
    if (i.Get()->GetFontEntry() == aFontEntry) {
      i.Remove();
    }
  }

#ifdef DEBUG_USERFONT_CACHE
  printf("userfontcache removed fontentry: %p\n", aFontEntry);
  Dump();
#endif
}

gfxFontEntry* gfxUserFontSet::UserFontCache::GetFont(
    const gfxFontFaceSrc& aSrc, const gfxUserFontEntry& aUserFontEntry) {
  if (!sUserFonts ||
      Preferences::GetBool("gfx.downloadable_fonts.disable_cache")) {
    return nullptr;
  }

  RefPtr<gfxUserFontSet> srcFontSet = aUserFontEntry.GetUserFontSet();
  if (NS_WARN_IF(!srcFontSet) || srcFontSet->BypassCache()) {
    return nullptr;
  }

  // Ignore principal when looking up a data: URI.
  RefPtr<gfxFontSrcPrincipal> principal =
      IgnorePrincipal(aSrc.mURI) ? nullptr : aSrc.LoadPrincipal(*srcFontSet);

  Entry* entry = sUserFonts->GetEntry(
      Key(aSrc.mURI, principal, const_cast<gfxUserFontEntry*>(&aUserFontEntry),
          srcFontSet->GetPrivateBrowsing()));
  if (!entry) {
    return nullptr;
  }

  // We have to perform another content policy check here to prevent
  // cache poisoning. E.g. a.com loads a font into the cache but
  // b.com has a CSP not allowing any fonts to be loaded.
  if (!srcFontSet->IsFontLoadAllowed(aSrc)) {
    return nullptr;
  }

  return entry->GetFontEntry();
}

void gfxUserFontSet::UserFontCache::Shutdown() {
  if (sUserFonts) {
    delete sUserFonts;
    sUserFonts = nullptr;
  }
}

MOZ_DEFINE_MALLOC_SIZE_OF(UserFontsMallocSizeOf)

void gfxUserFontSet::UserFontCache::Entry::ReportMemory(
    nsIHandleReportCallback* aHandleReport, nsISupports* aData,
    bool aAnonymize) {
  MOZ_ASSERT(mFontEntry);
  nsAutoCString path("explicit/gfx/user-fonts/font(");

  if (aAnonymize) {
    path.AppendPrintf("<anonymized-%p>", this);
  } else {
    path.AppendPrintf("family=%s", mFontEntry->FamilyName().get());
    if (mURI) {
      nsCString spec = mURI->GetSpecOrDefault();
      spec.ReplaceChar('/', '\\');
      // Some fonts are loaded using horrendously-long data: URIs;
      // truncate those before reporting them.
      if (mURI->get()->SchemeIs("data") && spec.Length() > 255) {
        spec.Truncate(252);
        spec.AppendLiteral("...");
      }
      path.AppendPrintf(", url=%s", spec.get());
    }
    if (mPrincipal) {
      nsAutoCString spec;
      mPrincipal->NodePrincipal()->GetAsciiSpec(spec);
      if (!spec.IsEmpty()) {
        // Include a clue as to who loaded this resource. (Note
        // that because of font entry sharing, other pages may now
        // be using this resource, and the original page may not
        // even be loaded any longer.)
        spec.ReplaceChar('/', '\\');
        path.AppendPrintf(", principal=%s", spec.get());
      }
    }
  }
  path.Append(')');

  aHandleReport->Callback(
      ""_ns, path, nsIMemoryReporter::KIND_HEAP, nsIMemoryReporter::UNITS_BYTES,
      mFontEntry->ComputedSizeOfExcludingThis(UserFontsMallocSizeOf),
      "Memory used by @font-face resource."_ns, aData);
}

NS_IMPL_ISUPPORTS(gfxUserFontSet::UserFontCache::MemoryReporter,
                  nsIMemoryReporter)

NS_IMETHODIMP
gfxUserFontSet::UserFontCache::MemoryReporter::CollectReports(
    nsIHandleReportCallback* aHandleReport, nsISupports* aData,
    bool aAnonymize) {
  if (!sUserFonts) {
    return NS_OK;
  }

  for (auto it = sUserFonts->Iter(); !it.Done(); it.Next()) {
    it.Get()->ReportMemory(aHandleReport, aData, aAnonymize);
  }

  MOZ_COLLECT_REPORT(
      "explicit/gfx/user-fonts/cache-overhead", KIND_HEAP, UNITS_BYTES,
      sUserFonts->ShallowSizeOfIncludingThis(UserFontsMallocSizeOf),
      "Memory used by the @font-face cache, not counting the actual font "
      "resources.");

  return NS_OK;
}

#ifdef DEBUG_USERFONT_CACHE

void gfxUserFontSet::UserFontCache::Entry::Dump() {
  nsresult rv;

  nsAutoCString principalURISpec("(null)");
  bool setDomain = false;

  if (mPrincipal) {
    nsCOMPtr<nsIURI> principalURI;
    rv = mPrincipal->NodePrincipal()->GetURI(getter_AddRefs(principalURI));
    if (NS_SUCCEEDED(rv)) {
      principalURI->GetSpec(principalURISpec);
    }

    nsCOMPtr<nsIURI> domainURI;
    mPrincipal->NodePrincipal()->GetDomain(getter_AddRefs(domainURI));
    if (domainURI) {
      setDomain = true;
    }
  }

  NS_ASSERTION(mURI, "null URI in userfont cache entry");

  printf(
      "userfontcache fontEntry: %p fonturihash: %8.8x "
      "family: %s domainset: %s principal: [%s]\n",
      mFontEntry, mURI->Hash(), mFontEntry->FamilyName().get(),
      setDomain ? "true" : "false", principalURISpec.get());
}

void gfxUserFontSet::UserFontCache::Dump() {
  if (!sUserFonts) {
    return;
  }

  printf("userfontcache dump count: %d ========\n", sUserFonts->Count());
  for (auto it = sUserFonts->Iter(); !it.Done(); it.Next()) {
    it.Get()->Dump();
  }
  printf("userfontcache dump ==================\n");
}

#endif

#undef LOG
#undef LOG_ENABLED
