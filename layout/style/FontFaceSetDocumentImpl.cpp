/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FontFaceSetDocumentImpl.h"

#include "mozilla/FontLoaderUtils.h"
#include "mozilla/LoadInfo.h"
#include "mozilla/PresShell.h"
#include "mozilla/PresShellInlines.h"
#include "mozilla/css/Loader.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/FontFaceImpl.h"
#include "mozilla/dom/FontFaceSet.h"
#include "nsContentPolicyUtils.h"
#include "nsDOMNavigationTiming.h"
#include "nsFontFaceLoader.h"
#include "nsIDocShell.h"
#include "nsISupportsPriority.h"
#include "nsIWebNavigation.h"
#include "nsPresContext.h"

using namespace mozilla;
using namespace mozilla::css;
using namespace mozilla::dom;

#define LOG(args) \
  MOZ_LOG(gfxUserFontSet::GetUserFontsLog(), mozilla::LogLevel::Debug, args)
#define LOG_ENABLED() \
  MOZ_LOG_TEST(gfxUserFontSet::GetUserFontsLog(), LogLevel::Debug)

NS_IMPL_ISUPPORTS_INHERITED(FontFaceSetDocumentImpl, FontFaceSetImpl,
                            nsIDOMEventListener, nsICSSLoaderObserver)

FontFaceSetDocumentImpl::FontFaceSetDocumentImpl(FontFaceSet* aOwner,
                                                 dom::Document* aDocument)
    : FontFaceSetImpl(aOwner), mDocument(aDocument) {}

FontFaceSetDocumentImpl::~FontFaceSetDocumentImpl() = default;

void FontFaceSetDocumentImpl::Initialize() {
  RecursiveMutexAutoLock lock(mMutex);

  MOZ_ASSERT(mDocument, "We should get a valid document from the caller!");

  // Record the state of the "bypass cache" flags from the docshell now,
  // since we want to look at them from style worker threads, and we can
  // only get to the docshell through a weak pointer (which is only
  // possible on the main thread).
  //
  // In theory the load type of a docshell could change after the document
  // is loaded, but handling that doesn't seem too important.
  if (nsCOMPtr<nsIDocShell> docShell = mDocument->GetDocShell()) {
    if (((docShell->GetLoadType() >> 16) &
         nsIWebNavigation::LOAD_FLAGS_BYPASS_CACHE) ||
        (docShell->GetDefaultLoadFlags() & nsIRequest::LOAD_BYPASS_CACHE)) {
      mBypassCache = true;
    }
  }

  // Same for the "private browsing" flag.
  if (nsCOMPtr<nsILoadContext> loadContext = mDocument->GetLoadContext()) {
    mPrivateBrowsing = loadContext->UsePrivateBrowsing();
  }

  if (!mDocument->DidFireDOMContentLoaded()) {
    mDocument->AddSystemEventListener(u"DOMContentLoaded"_ns, this, false,
                                      false);
  } else {
    // In some cases we can't rely on CheckLoadingFinished being called from
    // the refresh driver.  For example, documents in display:none iframes.
    // Or if the document has finished loading and painting at the time that
    // script requests document.fonts and causes us to get here.
    CheckLoadingFinished();
  }

  mDocument->EnsureCSSLoader().AddObserver(this);

  mStandardFontLoadPrincipal = MakeRefPtr<gfxFontSrcPrincipal>(
      mDocument->NodePrincipal(), mDocument->PartitionedPrincipal());
}

void FontFaceSetDocumentImpl::Destroy() {
  RemoveDOMContentLoadedListener();

  if (mDocument && mDocument->GetExistingCSSLoader()) {
    // We're null checking CSSLoader() since FontFaceSetImpl::Disconnect() might
    // be being called during unlink, at which time the loader may already have
    // been unlinked from the document.
    mDocument->GetExistingCSSLoader()->RemoveObserver(this);
  }

  mRuleFaces.Clear();

  // Since the base class may depend on the document remaining set, we need to
  // clear mDocument after.
  FontFaceSetImpl::Destroy();

  mDocument = nullptr;
}

bool FontFaceSetDocumentImpl::IsOnOwningThread() { return NS_IsMainThread(); }

#ifdef DEBUG
void FontFaceSetDocumentImpl::AssertIsOnOwningThread() {
  MOZ_ASSERT(NS_IsMainThread());
}
#endif

void FontFaceSetDocumentImpl::DispatchToOwningThread(
    const char* aName, std::function<void()>&& aFunc) {
  class FontFaceSetDocumentRunnable final : public Runnable {
   public:
    FontFaceSetDocumentRunnable(const char* aName,
                                std::function<void()>&& aFunc)
        : Runnable(aName), mFunc(std::move(aFunc)) {}

    NS_IMETHOD Run() final {
      mFunc();
      return NS_OK;
    }

   private:
    std::function<void()> mFunc;
  };

  auto runnable =
      MakeRefPtr<FontFaceSetDocumentRunnable>(aName, std::move(aFunc));
  NS_DispatchToMainThread(runnable.forget());
}

uint64_t FontFaceSetDocumentImpl::GetInnerWindowID() {
  MOZ_ASSERT(NS_IsMainThread());
  if (!mDocument) {
    return 0;
  }

  return mDocument->InnerWindowID();
}

FontVisibilityProvider* FontFaceSetDocumentImpl::GetFontVisibilityProvider()
    const {
  mozilla::AssertIsMainThreadOrServoFontMetricsLocked();
  if (!mDocument) {
    return nullptr;
  }

  return mDocument->GetPresContext();
}

void FontFaceSetDocumentImpl::RefreshStandardFontLoadPrincipal() {
  MOZ_ASSERT(NS_IsMainThread());
  RecursiveMutexAutoLock lock(mMutex);
  if (NS_WARN_IF(!mDocument)) {
    return;
  }
  mStandardFontLoadPrincipal = MakeRefPtr<gfxFontSrcPrincipal>(
      mDocument->NodePrincipal(), mDocument->PartitionedPrincipal());
  FontFaceSetImpl::RefreshStandardFontLoadPrincipal();
}

already_AddRefed<URLExtraData> FontFaceSetDocumentImpl::GetURLExtraData() {
  if (!mDocument) {
    return nullptr;
  }
  return do_AddRef(mDocument->DefaultStyleAttrURLData());
}

void FontFaceSetDocumentImpl::RemoveDOMContentLoadedListener() {
  if (mDocument) {
    mDocument->RemoveSystemEventListener(u"DOMContentLoaded"_ns, this, false);
  }
}

void FontFaceSetDocumentImpl::FindMatchingFontFaces(
    const nsTHashSet<FontFace*>& aMatchingFaces,
    nsTArray<FontFace*>& aFontFaces) {
  FontFaceSetImpl::FindMatchingFontFaces(aMatchingFaces, aFontFaces);
  for (FontFaceRecord& record : mRuleFaces) {
    FontFace* owner = record.mFontFace->GetOwner();
    if (owner && aMatchingFaces.Contains(owner)) {
      aFontFaces.AppendElement(owner);
    }
  }
}

TimeStamp FontFaceSetDocumentImpl::GetNavigationStartTimeStamp() {
  TimeStamp navStart;
  RefPtr<nsDOMNavigationTiming> timing(mDocument->GetNavigationTiming());
  if (timing) {
    navStart = timing->GetNavigationStartTimeStamp();
  }
  return navStart;
}

// There may be outstanding style changes that will trigger the loading of new
// fonts. If we're in the loaded state already, we need to flush layout to
// initiate any such loads so that if mReady is currently resolved we replace it
// with a new pending Promise. (That replacement will happen under this flush
// call.)
void FontFaceSetDocumentImpl::EnsureReady() {
  MOZ_ASSERT(NS_IsMainThread());
  {
    RecursiveMutexAutoLock lock(mMutex);
    if (mStatus == FontFaceSetLoadStatus::Loading) {
      return;
    }
  }

  if (mDocument) {
    mDocument->FlushPendingNotifications(FlushType::Layout);
  }
}

#ifdef DEBUG
bool FontFaceSetDocumentImpl::HasRuleFontFace(FontFaceImpl* aFontFace) {
  for (const auto& record : mRuleFaces) {
    if (record.mFontFace == aFontFace) {
      return true;
    }
  }
  return false;
}
#endif

bool FontFaceSetDocumentImpl::Add(FontFaceImpl* aFontFace, ErrorResult& aRv) {
  if (NS_WARN_IF(!mDocument)) {
    return false;
  }

  if (!FontFaceSetImpl::Add(aFontFace, aRv)) {
    return false;
  }

  RefPtr<dom::Document> clonedDoc = mDocument->GetLatestStaticClone();
  if (clonedDoc) {
    // The document is printing, copy the font to the static clone as well.
    nsCOMPtr<nsIPrincipal> principal = mDocument->GetPrincipal();
    if (principal->IsSystemPrincipal() || nsContentUtils::IsPDFJS(principal)) {
      ErrorResult rv;
      clonedDoc->Fonts()->Add(*aFontFace->GetOwner(), rv);
      MOZ_ASSERT(!rv.Failed());
    }
  }

  return true;
}

nsresult FontFaceSetDocumentImpl::StartLoad(gfxUserFontEntry* aUserFontEntry,
                                            uint32_t aSrcIndex) {
  if (NS_WARN_IF(!mDocument)) {
    return NS_ERROR_FAILURE;
  }

  nsresult rv;

  nsCOMPtr<nsIStreamLoader> streamLoader;
  RefPtr<nsFontFaceLoader> fontLoader;

  const gfxFontFaceSrc& src = aUserFontEntry->SourceAt(aSrcIndex);

  auto preloadKey =
      PreloadHashKey::CreateAsFont(src.mURI->get(), CORS_ANONYMOUS);
  RefPtr<PreloaderBase> preload =
      mDocument->Preloads().LookupPreload(preloadKey);

  if (preload) {
    fontLoader = new nsFontFaceLoader(aUserFontEntry, aSrcIndex, this,
                                      preload->Channel());
    rv = NS_NewStreamLoader(getter_AddRefs(streamLoader), fontLoader,
                            fontLoader);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = preload->AsyncConsume(streamLoader);

    // We don't want this to hang around regardless of the result, there will be
    // no coalescing of later found <link preload> tags for fonts.
    preload->RemoveSelf(mDocument);
  } else {
    // No preload found, open a channel.
    rv = NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsILoadGroup> loadGroup(mDocument->GetDocumentLoadGroup());
  if (NS_FAILED(rv)) {
    nsCOMPtr<nsIChannel> channel;
    rv = FontLoaderUtils::BuildChannel(
        getter_AddRefs(channel), src.mURI->get(), CORS_ANONYMOUS,
        dom::ReferrerPolicy::_empty /* not used */, aUserFontEntry, &src,
        mDocument, loadGroup, nullptr, false,
        nsISupportsPriority::PRIORITY_HIGH);
    NS_ENSURE_SUCCESS(rv, rv);

    fontLoader = new nsFontFaceLoader(aUserFontEntry, aSrcIndex, this, channel);

    if (LOG_ENABLED()) {
      nsCOMPtr<nsIURI> referrer = src.mReferrerInfo
                                      ? src.mReferrerInfo->GetOriginalReferrer()
                                      : nullptr;
      LOG((
          "userfonts (%p) download start - font uri: (%s) referrer uri: (%s)\n",
          fontLoader.get(), src.mURI->GetSpecOrDefault().get(),
          referrer ? referrer->GetSpecOrDefault().get() : ""));
    }

    rv = NS_NewStreamLoader(getter_AddRefs(streamLoader), fontLoader,
                            fontLoader);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = channel->AsyncOpen(streamLoader);
    if (NS_FAILED(rv)) {
      fontLoader->DropChannel();  // explicitly need to break ref cycle
    }
  }

  {
    RecursiveMutexAutoLock lock(mMutex);
    mLoaders.PutEntry(fontLoader);
  }

  if (NS_SUCCEEDED(rv)) {
    fontLoader->StartedLoading(streamLoader);
    // let the font entry remember the loader, in case we need to cancel it
    aUserFontEntry->SetLoader(fontLoader);
  }

  return rv;
}

bool FontFaceSetDocumentImpl::IsFontLoadAllowed(const gfxFontFaceSrc& aSrc) {
  MOZ_ASSERT(aSrc.mSourceType == gfxFontFaceSrc::eSourceType_URL);

  if (ServoStyleSet::IsInServoTraversal()) {
    RecursiveMutexAutoLock lock(mMutex);
    auto entry = mAllowedFontLoads.Lookup(&aSrc);
    MOZ_DIAGNOSTIC_ASSERT(entry, "Missed an update?");
    return entry ? *entry : false;
  }

  MOZ_ASSERT(NS_IsMainThread());

  if (aSrc.mUseOriginPrincipal) {
    return true;
  }

  if (NS_WARN_IF(!mDocument)) {
    return false;
  }

  RefPtr<gfxFontSrcPrincipal> gfxPrincipal =
      aSrc.mURI->InheritsSecurityContext() ? nullptr
                                           : aSrc.LoadPrincipal(*this);

  nsIPrincipal* principal =
      gfxPrincipal ? gfxPrincipal->NodePrincipal() : nullptr;

  Result<RefPtr<net::LoadInfo>, nsresult> maybeLoadInfo = net::LoadInfo::Create(
      mDocument->NodePrincipal(),  // loading principal
      principal,                   // triggering principal
      mDocument, nsILoadInfo::SEC_ONLY_FOR_EXPLICIT_CONTENTSEC_CHECK,
      nsIContentPolicy::TYPE_FONT);
  if (NS_WARN_IF(maybeLoadInfo.isErr())) {
    return false;
  }
  RefPtr<net::LoadInfo> secCheckLoadInfo = maybeLoadInfo.unwrap();

  int16_t shouldLoad = nsIContentPolicy::ACCEPT;
  nsresult rv =
      NS_CheckContentLoadPolicy(aSrc.mURI->get(), secCheckLoadInfo, &shouldLoad,
                                nsContentUtils::GetContentPolicy());

  return NS_SUCCEEDED(rv) && NS_CP_ACCEPTED(shouldLoad);
}

nsresult FontFaceSetDocumentImpl::CreateChannelForSyncLoadFontData(
    nsIChannel** aOutChannel, gfxUserFontEntry* aFontToLoad,
    const gfxFontFaceSrc* aFontFaceSrc) {
  gfxFontSrcPrincipal* principal = aFontToLoad->GetPrincipal();

  // Note we are calling NS_NewChannelWithTriggeringPrincipal() with both a
  // node and a principal.  This is because the document where the font is
  // being loaded might have a different origin from the principal of the
  // stylesheet that initiated the font load.
  // Further, we only get here for data: loads, so it doesn't really matter
  // whether we use SEC_ALLOW_CROSS_ORIGIN_INHERITS_SEC_CONTEXT or not, to be
  // more restrictive we use SEC_REQUIRE_SAME_ORIGIN_INHERITS_SEC_CONTEXT.
  return NS_NewChannelWithTriggeringPrincipal(
      aOutChannel, aFontFaceSrc->mURI->get(), mDocument,
      principal ? principal->NodePrincipal() : nullptr,
      nsILoadInfo::SEC_REQUIRE_SAME_ORIGIN_INHERITS_SEC_CONTEXT,
      aFontFaceSrc->mUseOriginPrincipal ? nsIContentPolicy::TYPE_UA_FONT
                                        : nsIContentPolicy::TYPE_FONT);
}

bool FontFaceSetDocumentImpl::UpdateRules(
    const nsTArray<nsFontFaceRuleContainer>& aRules) {
  RecursiveMutexAutoLock lock(mMutex);

  // If there was a change to the mNonRuleFaces array, then there could
  // have been a modification to the user font set.
  bool modified = mNonRuleFacesDirty;
  mNonRuleFacesDirty = false;

  // The @font-face rules that make up the user font set have changed,
  // so we need to update the set. However, we want to preserve existing
  // font entries wherever possible, so that we don't discard and then
  // re-download resources in the (common) case where at least some of the
  // same rules are still present.
  nsTArray<FontFaceRecord> oldRecords = std::move(mRuleFaces);

  // We reverse the oldRecords array because we will most likely be using the
  // entries in the order they were originally added, and constantly removing
  // the first element is inefficient if the array is large; it's better if
  // we're most often removing elements from the end.
  oldRecords.Reverse();

  // Remove faces from the font family records; we need to re-insert them
  // because we might end up with faces in a different order even if they're
  // the same font entries as before. (The order can affect font selection
  // where multiple faces match the requested style, perhaps with overlapping
  // unicode-range coverage.)
  for (const auto& fontFamily : mFontFamilies.Values()) {
    fontFamily->DetachFontEntries();
  }

  // Sometimes aRules has duplicate @font-face rules in it; we should make
  // that not happen, but in the meantime, don't try to insert the same
  // FontFace object more than once into mRuleFaces.  We track which
  // ones we've handled in this table.
  nsTHashSet<StyleLockedFontFaceRule*> handledRules;

  for (const nsFontFaceRuleContainer& container : aRules) {
    // Insert each FontFace objects for each rule into our list, migrating old
    // font entries if possible rather than creating new ones; set  modified  to
    // true if we detect that rule ordering has changed, or if a new entry is
    // created.
    StyleLockedFontFaceRule* rule = container.mRule;
    if (!handledRules.EnsureInserted(rule)) {
      // rule was already present in the hashtable
      continue;
    }
    modified |= InsertRuleFontFace(rule, container.mOrigin, oldRecords);
  }

  for (const FontFaceRecord& record : mNonRuleFaces) {
    // Do the same for the non rule backed FontFace objects.
    InsertNonRuleFontFace(record.mFontFace);
  }

  // Remove any residual families that have no font entries (i.e., they were
  // not defined at all by the updated set of @font-face rules).
  for (auto it = mFontFamilies.Iter(); !it.Done(); it.Next()) {
    if (!it.Data()->FontListLength()) {
      it.Remove();
    }
  }

  // If any FontFace objects for rules are left in the old list, note that the
  // set has changed (even if the new set was built entirely by migrating old
  // font entries).
  if (!oldRecords.IsEmpty()) {
    modified = true;
    // Any in-progress loaders for obsolete rules should be cancelled,
    // as the resource being downloaded will no longer be required.
    // We need to explicitly remove any loaders here, otherwise the loaders
    // will keep their "orphaned" font entries alive until they complete,
    // even after the oldRules array is deleted.
    //
    // XXX Now that it is possible for the author to hold on to a rule backed
    // FontFace object, we shouldn't cancel loading here; instead we should do
    // it when the FontFace is GCed, if we can detect that.
    for (const FontFaceRecord& record : oldRecords) {
      RefPtr<FontFaceImpl> f = record.mFontFace;
      if (gfxUserFontEntry* userFontEntry = f->GetUserFontEntry()) {
        if (nsFontFaceLoader* loader = userFontEntry->GetLoader()) {
          // Cancel() removes the loader from its registering set's mLoaders.
          loader->Cancel();
        }
      }

      // Any left over FontFace objects should also cease being rule backed.
      f->DisconnectFromRule();
    }
  }

  if (modified) {
    IncrementGenerationLocked(true);
    mHasLoadingFontFacesIsDirty = true;
    CheckLoadingStarted();
    CheckLoadingFinished();
  }

  // if local rules needed to be rebuilt, they have been rebuilt at this point
  if (mRebuildLocalRules) {
    mLocalRulesUsed = false;
    mRebuildLocalRules = false;
  }

  if (LOG_ENABLED() && !mRuleFaces.IsEmpty()) {
    LOG(("userfonts (%p) userfont rules update (%s) rule count: %d", this,
         (modified ? "modified" : "not modified"), (int)(mRuleFaces.Length())));
  }

  return modified;
}

bool FontFaceSetDocumentImpl::InsertRuleFontFace(
    StyleLockedFontFaceRule* aRule, StyleOrigin aSheetType,
    nsTArray<FontFaceRecord>& aOldRecords) {
  RecursiveMutexAutoLock lock(mMutex);

  if (MOZ_UNLIKELY(!mOwner)) {
    return false;
  }

  gfxUserFontAttributes attr;
  if (!FontFaceImpl::GetAttributesFromRule(aRule, attr)) {
    // If there is no family name, this rule cannot contribute a
    // usable font, so there is no point in processing it further.
    return false;
  }

  bool remove = false;
  size_t removeIndex;

  // This is a rule backed FontFace.  First, we check in aOldRecords; if
  // the FontFace for the rule exists there, just move it to the new record
  // list, and put the entry into the appropriate family.
  // Note that aOldRecords was reversed, so we search it from the end.
  for (size_t i = aOldRecords.Length(); i > 0;) {
    FontFaceRecord& rec = aOldRecords[--i];

    const bool matches =
        rec.mOrigin == Some(aSheetType) &&
        Servo_FontFaceRule_Equals(rec.mFontFace->GetData(), aRule);
    if (!matches) {
      continue;
    }

    FontFace* owner = rec.mFontFace->GetOwner();
    // if local rules were used, don't use the old font entry
    // for rules containing src local usage
    if (mLocalRulesUsed && mRebuildLocalRules) {
      const bool hasLocalSource = [&] {
        for (auto& source : attr.mSources) {
          if (source.IsLocal()) {
            return true;
          }
        }
        return false;
      }();

      if (hasLocalSource) {
        // Remove the old record, but wait to see if we successfully create a
        // new user font entry below.
        remove = true;
        removeIndex = i;
        break;
      }
    }

    rec.mFontFace->SetRule(aRule);
    gfxUserFontEntry* entry = rec.mFontFace->GetUserFontEntry();
    MOZ_ASSERT(entry, "FontFace should have a gfxUserFontEntry by now");

    AddUserFontEntry(attr.mFamilyName, entry);

    MOZ_ASSERT(!HasRuleFontFace(rec.mFontFace),
               "FontFace should not occur in mRuleFaces twice");

    mRuleFaces.AppendElement(rec);
    aOldRecords.RemoveElementAt(i);

    if (owner) {
      mOwner->InsertRuleFontFace(owner, aSheetType);
    }

    // Return that the set has been modified if an old rule was skipped to find
    // this one: something has been dropped, or ordering changed.
    // Note that the record at index i has been removed, so Length() is now the
    // original last-element index.
    return i < aOldRecords.Length();
  }

  RefPtr<FontFace> fontFace =
      FontFace::CreateForRule(mOwner->GetParentObject(), mOwner, aRule);
  RefPtr<FontFaceImpl> impl = fontFace->GetImpl();
  // this is a new rule:
  nsAutoCString family(attr.mFamilyName);
  RefPtr<gfxUserFontEntry> entry =
      FindOrCreateUserFontEntryFromFontFace(impl, std::move(attr), aSheetType);

  if (!entry) {
    return false;
  }

  if (remove) {
    // Although we broke out of the aOldRecords loop above, since we found
    // src local usage, and we're not using the old user font entry, we still
    // are adding a record to mRuleFaces with the same FontFace object.
    // Remove the old record so that we don't have the same FontFace listed
    // in both mRuleFaces and oldRecords, which would cause us to call
    // DisconnectFromRule on a FontFace that should still be rule backed.
    aOldRecords.RemoveElementAt(removeIndex);
  }

  FontFaceRecord rec;
  rec.mFontFace = impl;
  rec.mOrigin = Some(aSheetType);

  impl->SetUserFontEntry(entry);

  MOZ_ASSERT(!HasRuleFontFace(impl),
             "FontFace should not occur in mRuleFaces twice");

  mRuleFaces.AppendElement(rec);

  mOwner->InsertRuleFontFace(fontFace, aSheetType);

  // Add the entry to the end of the list.  If an existing userfont entry was
  // returned by FindOrCreateUserFontEntryFromFontFace that was already stored
  // on the family, gfxUserFontFamily::AddFontEntry(), which AddUserFontEntry
  // calls, will automatically remove the earlier occurrence of the same
  // userfont entry.
  AddUserFontEntry(family, entry);
  return true;
}

StyleLockedFontFaceRule* FontFaceSetDocumentImpl::FindRuleForEntry(
    gfxFontEntry* aFontEntry) {
  NS_ASSERTION(!aFontEntry->mIsUserFontContainer, "only platform font entries");
  for (uint32_t i = 0; i < mRuleFaces.Length(); ++i) {
    FontFaceImpl* f = mRuleFaces[i].mFontFace;
    if (gfxUserFontEntry* entry = f->GetUserFontEntry()) {
      if (entry->PlatformFontEntryEquals(aFontEntry)) {
        return f->GetRule();
      }
    }
  }
  return nullptr;
}

StyleLockedFontFaceRule* FontFaceSetDocumentImpl::FindRuleForUserFontEntry(
    gfxUserFontEntry* aUserFontEntry) {
  for (uint32_t i = 0; i < mRuleFaces.Length(); ++i) {
    FontFaceImpl* f = mRuleFaces[i].mFontFace;
    if (f->GetUserFontEntry() == aUserFontEntry) {
      return f->GetRule();
    }
  }
  return nullptr;
}

void FontFaceSetDocumentImpl::CacheFontLoadability() {
  RecursiveMutexAutoLock lock(mMutex);

  // TODO(emilio): We could do it a bit more incrementally maybe?
  for (const auto& fontFamily : mFontFamilies.Values()) {
    fontFamily->ReadLock();
    for (const gfxFontEntry* entry : fontFamily->GetFontList()) {
      if (!entry->mIsUserFontContainer) {
        continue;
      }

      const auto& sourceList =
          static_cast<const gfxUserFontEntry*>(entry)->SourceList();
      for (const gfxFontFaceSrc& src : sourceList) {
        if (src.mSourceType != gfxFontFaceSrc::eSourceType_URL) {
          continue;
        }
        mAllowedFontLoads.LookupOrInsertWith(
            &src, [&] { return IsFontLoadAllowed(src); });
      }
    }
    fontFamily->ReadUnlock();
  }
}

void FontFaceSetDocumentImpl::DidRefresh() { CheckLoadingFinished(); }

void FontFaceSetDocumentImpl::UpdateHasLoadingFontFaces() {
  RecursiveMutexAutoLock lock(mMutex);
  FontFaceSetImpl::UpdateHasLoadingFontFaces();

  if (mHasLoadingFontFaces) {
    return;
  }

  for (size_t i = 0; i < mRuleFaces.Length(); i++) {
    FontFaceImpl* f = mRuleFaces[i].mFontFace;
    if (f->Status() == FontFaceLoadStatus::Loading) {
      mHasLoadingFontFaces = true;
      return;
    }
  }
}

bool FontFaceSetDocumentImpl::MightHavePendingFontLoads() {
  if (FontFaceSetImpl::MightHavePendingFontLoads()) {
    return true;
  }

  if (!mDocument) {
    return false;
  }

  // Check for pending restyles or reflows, as they might cause fonts to
  // load as new styles apply and text runs are rebuilt.
  PresShell* ps = mDocument->GetPresShell();
  if (ps && ps->MightHavePendingFontLoads()) {
    return true;
  }

  // We defer resolving mReady until the document as fully loaded.
  if (!mDocument->DidFireDOMContentLoaded()) {
    return true;
  }

  // And we also wait for any CSS style sheets to finish loading, as their
  // styles might cause new fonts to load.
  if (css::Loader* loader = mDocument->GetExistingCSSLoader()) {
    if (loader->HasPendingLoads()) {
      return true;
    }
  }

  return false;
}

// nsIDOMEventListener

NS_IMETHODIMP
FontFaceSetDocumentImpl::HandleEvent(Event* aEvent) {
  nsString type;
  aEvent->GetType(type);

  if (!type.EqualsLiteral("DOMContentLoaded")) {
    return NS_ERROR_FAILURE;
  }

  RemoveDOMContentLoadedListener();
  CheckLoadingFinished();

  return NS_OK;
}

// nsICSSLoaderObserver

NS_IMETHODIMP
FontFaceSetDocumentImpl::StyleSheetLoaded(StyleSheet* aSheet, bool aWasDeferred,
                                          nsresult aStatus) {
  CheckLoadingFinished();
  return NS_OK;
}

void FontFaceSetDocumentImpl::FlushUserFontSet() {
  if (mDocument) {
    mDocument->FlushUserFontSet();
  }
}

void FontFaceSetDocumentImpl::MarkUserFontSetDirty() {
  if (mDocument) {
    // Ensure we trigger at least a style flush, that will eventually flush the
    // user font set. Otherwise the font loads that that flush may cause could
    // never be triggered.
    if (PresShell* presShell = mDocument->GetPresShell()) {
      presShell->EnsureStyleFlush();
    }
    mDocument->MarkUserFontSetDirty();
  }
}

#undef LOG_ENABLED
#undef LOG
