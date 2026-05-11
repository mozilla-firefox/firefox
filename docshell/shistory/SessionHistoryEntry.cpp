/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SessionHistoryEntry.h"
#include "ipc/IPCMessageUtilsSpecializations.h"
#include "mozilla/dom/SessionHistoryEntry.h"
#include "nsCOMPtr.h"
#include "nsDocShell.h"
#include "nsDocShellLoadState.h"
#include "nsFrameLoader.h"
#include "nsIFormPOSTActionChannel.h"
#include "nsIHttpChannel.h"
#include "nsIUploadChannel2.h"
#include "nsIXULRuntime.h"
#include "nsSHEntryShared.h"
#include "nsSHistory.h"
#include "nsStreamUtils.h"
#include "nsStructuredCloneContainer.h"
#include "nsXULAppAPI.h"
#include "mozilla/PresState.h"
#include "mozilla/StaticPrefs_fission.h"
#include "mozilla/dom/BindingIPCUtils.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/CSPMessageUtils.h"
#include "mozilla/dom/PolicyContainerMessageUtils.h"
#include "mozilla/dom/DocumentBinding.h"
#include "mozilla/dom/DOMTypes.h"
#include "mozilla/dom/NavigationAPIIPCUtils.h"
#include "mozilla/dom/nsCSPContext.h"
#include "mozilla/dom/nsCSPUtils.h"
#include "mozilla/dom/PermissionMessageUtils.h"
#include "mozilla/dom/PolicyContainer.h"
#include "mozilla/dom/ReferrerInfoUtils.h"
#include "mozilla/ipc/ProtocolUtils.h"
#include "mozilla/ipc/URIUtils.h"

extern mozilla::LazyLogModule gSHLog;

namespace mozilla {
namespace dom {

SessionHistoryInfo::SessionHistoryInfo(nsDocShellLoadState* aLoadState,
                                       nsIChannel* aChannel)
    : mURI(aLoadState->URI()),
      mOriginalURI(aLoadState->OriginalURI()),
      mResultPrincipalURI(aLoadState->ResultPrincipalURI()),
      mUnstrippedURI(aLoadState->GetUnstrippedURI()),
      mLoadType(aLoadState->LoadType()),
      mSrcdocData(aLoadState->SrcdocData().IsVoid()
                      ? Nothing()
                      : Some(aLoadState->SrcdocData())),
      mBaseURI(aLoadState->BaseURI()),
      mNavigationAPIState(static_cast<nsStructuredCloneContainer*>(
          aLoadState->GetNavigationAPIState())),
      mLoadReplace(aLoadState->LoadReplace()),
      mHasUserActivation(aLoadState->HasValidUserGestureActivation()),
      mSharedState(SharedState::Create(
          aLoadState->TriggeringPrincipal(), aLoadState->PrincipalToInherit(),
          aLoadState->PartitionedPrincipalToInherit(),
          aLoadState->PolicyContainer(),
          /* FIXME Is this correct? */
          aLoadState->TypeHint())) {
  // Pull the upload stream off of the channel instead of the load state, as
  // ownership has already been transferred from the load state to the channel.
  if (nsCOMPtr<nsIUploadChannel2> postChannel = do_QueryInterface(aChannel)) {
    int64_t contentLength;
    MOZ_ALWAYS_SUCCEEDS(postChannel->CloneUploadStream(
        &contentLength, getter_AddRefs(mPostData)));
    MOZ_ASSERT_IF(mPostData, NS_InputStreamIsCloneable(mPostData));
  }

  if (nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aChannel)) {
    mReferrerInfo = httpChannel->GetReferrerInfo();
  }

  MaybeUpdateTitleFromURI();
}

SessionHistoryInfo::SessionHistoryInfo(
    const SessionHistoryInfo& aSharedStateFrom, nsIURI* aURI)
    : mURI(aURI), mSharedState(aSharedStateFrom.mSharedState) {
  MaybeUpdateTitleFromURI();
  mHasUserInteraction = aSharedStateFrom.mHasUserInteraction;
}

SessionHistoryInfo::SessionHistoryInfo(
    nsIURI* aURI, nsIPrincipal* aTriggeringPrincipal,
    nsIPrincipal* aPrincipalToInherit,
    nsIPrincipal* aPartitionedPrincipalToInherit,
    nsIPolicyContainer* aPolicyContainer, const nsACString& aContentType)
    : mURI(aURI),
      mSharedState(SharedState::Create(
          aTriggeringPrincipal, aPrincipalToInherit,
          aPartitionedPrincipalToInherit, aPolicyContainer, aContentType)) {
  MaybeUpdateTitleFromURI();
}

SessionHistoryInfo::SessionHistoryInfo(
    nsIChannel* aChannel, uint32_t aLoadType,
    nsIPrincipal* aPartitionedPrincipalToInherit,
    nsIPolicyContainer* aPolicyContainer) {
  if (NS_FAILED(NS_GetFinalChannelURI(aChannel, getter_AddRefs(mURI)))) {
    NS_WARNING("NS_GetFinalChannelURI somehow failed in SessionHistoryInfo?");
    aChannel->GetURI(getter_AddRefs(mURI));
  }
  mLoadType = aLoadType;

  nsCOMPtr<nsILoadInfo> loadInfo;
  aChannel->GetLoadInfo(getter_AddRefs(loadInfo));

  loadInfo->GetResultPrincipalURI(getter_AddRefs(mResultPrincipalURI));
  loadInfo->GetUnstrippedURI(getter_AddRefs(mUnstrippedURI));
  loadInfo->GetTriggeringPrincipal(
      getter_AddRefs(mSharedState.Get()->mTriggeringPrincipal));
  loadInfo->GetPrincipalToInherit(
      getter_AddRefs(mSharedState.Get()->mPrincipalToInherit));

  mSharedState.Get()->mPartitionedPrincipalToInherit =
      aPartitionedPrincipalToInherit;
  mSharedState.Get()->mPolicyContainer = aPolicyContainer;
  aChannel->GetContentType(mSharedState.Get()->mContentType);
  aChannel->GetOriginalURI(getter_AddRefs(mOriginalURI));

  uint32_t loadFlags;
  aChannel->GetLoadFlags(&loadFlags);
  mLoadReplace = !!(loadFlags & nsIChannel::LOAD_REPLACE);

  MaybeUpdateTitleFromURI();

  if (nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aChannel)) {
    mReferrerInfo = httpChannel->GetReferrerInfo();
  }
}

void SessionHistoryInfo::Reset(nsIURI* aURI, const nsID& aDocShellID,
                               bool aDynamicCreation,
                               nsIPrincipal* aTriggeringPrincipal,
                               nsIPrincipal* aPrincipalToInherit,
                               nsIPrincipal* aPartitionedPrincipalToInherit,
                               nsIPolicyContainer* aPolicyContainer,
                               const nsACString& aContentType) {
  mURI = aURI;
  mOriginalURI = nullptr;
  mResultPrincipalURI = nullptr;
  mUnstrippedURI = nullptr;
  mReferrerInfo = nullptr;
  // Default title is the URL.
  nsAutoCString spec;
  if (NS_SUCCEEDED(mURI->GetSpec(spec))) {
    CopyUTF8toUTF16(spec, mTitle);
  }
  mPostData = nullptr;
  mLoadType = 0;
  mScrollPositionX = 0;
  mScrollPositionY = 0;
  mStateData = nullptr;
  mSrcdocData = Nothing();
  mBaseURI = nullptr;
  mLoadReplace = false;
  mURIWasModified = false;
  mScrollRestorationIsManual = false;
  mTransient = false;
  mHasUserInteraction = false;
  mHasUserActivation = false;
  mNavigationAPIState = nullptr;

  mSharedState.Get()->mTriggeringPrincipal = aTriggeringPrincipal;
  mSharedState.Get()->mPrincipalToInherit = aPrincipalToInherit;
  mSharedState.Get()->mPartitionedPrincipalToInherit =
      aPartitionedPrincipalToInherit;
  mSharedState.Get()->mPolicyContainer = aPolicyContainer;
  mSharedState.Get()->mContentType = aContentType;
  mSharedState.Get()->mLayoutHistoryState = nullptr;
}

void SessionHistoryInfo::MaybeUpdateTitleFromURI() {
  if (mTitle.IsEmpty() && mURI) {
    // Default title is the URL.
    nsAutoCString spec;
    if (NS_SUCCEEDED(mURI->GetSpec(spec))) {
      AppendUTF8toUTF16(spec, mTitle);
    }
  }
}

already_AddRefed<nsIInputStream> SessionHistoryInfo::GetPostData() const {
  // Return a clone of our post data stream. Our caller will either be
  // transferring this stream to a different SessionHistoryInfo, or passing it
  // off to necko/another process which will consume it, and we want to preserve
  // our local instance.
  nsCOMPtr<nsIInputStream> postData;
  if (mPostData) {
    MOZ_ALWAYS_SUCCEEDS(
        NS_CloneInputStream(mPostData, getter_AddRefs(postData)));
  }
  return postData.forget();
}

void SessionHistoryInfo::SetPostData(nsIInputStream* aPostData) {
  MOZ_ASSERT_IF(aPostData, NS_InputStreamIsCloneable(aPostData));
  mPostData = aPostData;
}

uint64_t SessionHistoryInfo::SharedId() const {
  return mSharedState.Get()->mId;
}

nsILayoutHistoryState* SessionHistoryInfo::GetLayoutHistoryState() {
  return mSharedState.Get()->mLayoutHistoryState;
}

void SessionHistoryInfo::SetLayoutHistoryState(nsILayoutHistoryState* aState) {
  mSharedState.Get()->mLayoutHistoryState = aState;
  if (mSharedState.Get()->mLayoutHistoryState) {
    mSharedState.Get()->mLayoutHistoryState->SetScrollPositionOnly(
        !mSharedState.Get()->mSaveLayoutState);
  }
}

nsIPrincipal* SessionHistoryInfo::GetTriggeringPrincipal() const {
  return mSharedState.Get()->mTriggeringPrincipal;
}

nsIPrincipal* SessionHistoryInfo::GetPrincipalToInherit() const {
  return mSharedState.Get()->mPrincipalToInherit;
}

nsIPrincipal* SessionHistoryInfo::GetPartitionedPrincipalToInherit() const {
  return mSharedState.Get()->mPartitionedPrincipalToInherit;
}

void SessionHistoryInfo::SetPartitionedPrincipalToInherit(
    nsIPrincipal* aPartitionedPrincipal) {
  mSharedState.Get()->mPartitionedPrincipalToInherit = aPartitionedPrincipal;
}

nsIPolicyContainer* SessionHistoryInfo::GetPolicyContainer() const {
  return mSharedState.Get()->mPolicyContainer;
}

uint32_t SessionHistoryInfo::GetCacheKey() const {
  return mSharedState.Get()->mCacheKey;
}

void SessionHistoryInfo::SetCacheKey(uint32_t aCacheKey) {
  mSharedState.Get()->mCacheKey = aCacheKey;
}

bool SessionHistoryInfo::IsSubFrame() const {
  return mSharedState.Get()->mIsFrameNavigation;
}

nsIStructuredCloneContainer* SessionHistoryInfo::GetNavigationAPIState() const {
  return mNavigationAPIState.get();
}

void SessionHistoryInfo::SetNavigationAPIState(
    nsIStructuredCloneContainer* aState) {
  mNavigationAPIState = static_cast<nsStructuredCloneContainer*>(aState);
}

void SessionHistoryInfo::SetSaveLayoutStateFlag(bool aSaveLayoutStateFlag) {
  MOZ_ASSERT(XRE_IsParentProcess());
  static_cast<SHEntrySharedParentState*>(mSharedState.Get())->mSaveLayoutState =
      aSaveLayoutStateFlag;
}

void SessionHistoryInfo::FillLoadInfo(nsDocShellLoadState& aLoadState) const {
  aLoadState.SetOriginalURI(mOriginalURI);
  aLoadState.SetMaybeResultPrincipalURI(Some(mResultPrincipalURI));
  aLoadState.SetUnstrippedURI(mUnstrippedURI);
  aLoadState.SetLoadReplace(mLoadReplace);
  nsCOMPtr<nsIInputStream> postData = GetPostData();
  aLoadState.SetPostDataStream(postData);
  aLoadState.SetReferrerInfo(mReferrerInfo);

  aLoadState.SetTypeHint(mSharedState.Get()->mContentType);
  aLoadState.SetTriggeringPrincipal(mSharedState.Get()->mTriggeringPrincipal);
  aLoadState.SetPrincipalToInherit(mSharedState.Get()->mPrincipalToInherit);
  aLoadState.SetPartitionedPrincipalToInherit(
      mSharedState.Get()->mPartitionedPrincipalToInherit);
  aLoadState.SetPolicyContainer(mSharedState.Get()->mPolicyContainer);

  // Do not inherit principal from document (security-critical!);
  uint32_t flags = nsDocShell::InternalLoad::INTERNAL_LOAD_FLAGS_NONE;

  // Passing nullptr as aSourceDocShell gives the same behaviour as before
  // aSourceDocShell was introduced. According to spec we should be passing
  // the source browsing context that was used when the history entry was
  // first created. bug 947716 has been created to address this issue.
  nsAutoString srcdoc;
  nsCOMPtr<nsIURI> baseURI;
  if (mSrcdocData) {
    srcdoc = mSrcdocData.value();
    baseURI = mBaseURI;
    flags |= nsDocShell::InternalLoad::INTERNAL_LOAD_FLAGS_IS_SRCDOC;
  } else {
    srcdoc = VoidString();
  }
  aLoadState.SetSrcdocData(srcdoc);
  aLoadState.SetBaseURI(baseURI);
  aLoadState.SetInternalLoadFlags(flags);

  aLoadState.SetFirstParty(true);

  // When we create a load state from the history info we already know if
  // https-first was able to upgrade the request from http to https. There is no
  // point in re-retrying to upgrade. On a reload we still want to check,
  // because the exemptions set by the user could have changed.
  if ((mLoadType & nsIDocShell::LOAD_CMD_RELOAD) == 0) {
    aLoadState.SetIsExemptFromHTTPSFirstMode(true);
  }
}
/* static */
SessionHistoryInfo::SharedState SessionHistoryInfo::SharedState::Create(
    nsIPrincipal* aTriggeringPrincipal, nsIPrincipal* aPrincipalToInherit,
    nsIPrincipal* aPartitionedPrincipalToInherit,
    nsIPolicyContainer* aPolicyContainer, const nsACString& aContentType) {
  if (XRE_IsParentProcess()) {
    return SharedState(new SHEntrySharedParentState(
        aTriggeringPrincipal, aPrincipalToInherit,
        aPartitionedPrincipalToInherit, aPolicyContainer, aContentType));
  }

  return SharedState(MakeUnique<SHEntrySharedState>(
      aTriggeringPrincipal, aPrincipalToInherit, aPartitionedPrincipalToInherit,
      aPolicyContainer, aContentType));
}

SessionHistoryInfo::SharedState::SharedState() { Init(); }

SessionHistoryInfo::SharedState::SharedState(
    const SessionHistoryInfo::SharedState& aOther) {
  Init(aOther);
}

SessionHistoryInfo::SharedState::SharedState(
    const Maybe<const SessionHistoryInfo::SharedState&>& aOther) {
  if (aOther.isSome()) {
    Init(aOther.ref());
  } else {
    Init();
  }
}

SessionHistoryInfo::SharedState::~SharedState() {
  if (XRE_IsParentProcess()) {
    mParent
        .RefPtr<SHEntrySharedParentState>::~RefPtr<SHEntrySharedParentState>();
  } else {
    mChild.UniquePtr<SHEntrySharedState>::~unique_ptr<
        SHEntrySharedState, DefaultDelete<SHEntrySharedState>>();
  }
}

SessionHistoryInfo::SharedState& SessionHistoryInfo::SharedState::operator=(
    const SessionHistoryInfo::SharedState& aOther) {
  if (this != &aOther) {
    if (XRE_IsParentProcess()) {
      mParent = aOther.mParent;
    } else {
      mChild = MakeUnique<SHEntrySharedState>(*aOther.mChild);
    }
  }
  return *this;
}

SHEntrySharedState* SessionHistoryInfo::SharedState::Get() const {
  if (XRE_IsParentProcess()) {
    return mParent;
  }

  return mChild.get();
}

void SessionHistoryInfo::SharedState::ChangeId(uint64_t aId) {
  if (XRE_IsParentProcess()) {
    mParent->ChangeId(aId);
  } else {
    mChild->mId = aId;
  }
}

void SessionHistoryInfo::SharedState::Init() {
  if (XRE_IsParentProcess()) {
    new (&mParent)
        RefPtr<SHEntrySharedParentState>(new SHEntrySharedParentState());
  } else {
    new (&mChild)
        UniquePtr<SHEntrySharedState>(MakeUnique<SHEntrySharedState>());
  }
}

void SessionHistoryInfo::SharedState::Init(
    const SessionHistoryInfo::SharedState& aOther) {
  if (XRE_IsParentProcess()) {
    new (&mParent) RefPtr<SHEntrySharedParentState>(aOther.mParent);
  } else {
    new (&mChild) UniquePtr<SHEntrySharedState>(
        MakeUnique<SHEntrySharedState>(*aOther.mChild));
  }
}

static uint64_t gLoadingSessionHistoryInfoLoadId = 0;

nsTHashMap<nsUint64HashKey, SessionHistoryEntry::LoadingEntry>*
    SessionHistoryEntry::sLoadIdToEntry = nullptr;

LoadingSessionHistoryInfo::LoadingSessionHistoryInfo(
    SessionHistoryEntry* aEntry)
    : mInfo(aEntry->Info()), mLoadId(++gLoadingSessionHistoryInfoLoadId) {
  SessionHistoryEntry::SetByLoadId(mLoadId, aEntry);
}

LoadingSessionHistoryInfo::LoadingSessionHistoryInfo(
    SessionHistoryEntry* aEntry, const LoadingSessionHistoryInfo* aInfo)
    : mInfo(aEntry->Info()),
      mPreviousEntry(aInfo->mPreviousEntry),
      mTriggeringNavigationType(aInfo->mTriggeringNavigationType),
      mLoadId(aInfo->mLoadId),
      mLoadIsFromSessionHistory(aInfo->mLoadIsFromSessionHistory),
      mOffset(aInfo->mOffset),
      mLoadingCurrentEntry(aInfo->mLoadingCurrentEntry) {
  MOZ_ASSERT(SessionHistoryEntry::GetByLoadId(mLoadId)->mEntry == aEntry);
}

LoadingSessionHistoryInfo::LoadingSessionHistoryInfo(
    const SessionHistoryInfo& aInfo)
    : mInfo(aInfo), mLoadId(UINT64_MAX) {}

already_AddRefed<nsDocShellLoadState>
LoadingSessionHistoryInfo::CreateLoadInfo() const {
  RefPtr<nsDocShellLoadState> loadState(
      new nsDocShellLoadState(mInfo.GetURI()));

  mInfo.FillLoadInfo(*loadState);

  loadState->SetLoadingSessionHistoryInfo(*this);

  return loadState.forget();
}

static uint32_t gEntryID;

SessionHistoryEntry::LoadingEntry* SessionHistoryEntry::GetByLoadId(
    uint64_t aLoadId) {
  MOZ_ASSERT(XRE_IsParentProcess());
  if (!sLoadIdToEntry) {
    return nullptr;
  }

  return sLoadIdToEntry->Lookup(aLoadId).DataPtrOrNull();
}

void SessionHistoryEntry::SetByLoadId(uint64_t aLoadId,
                                      SessionHistoryEntry* aEntry) {
  if (!sLoadIdToEntry) {
    sLoadIdToEntry = new nsTHashMap<nsUint64HashKey, LoadingEntry>();
  }

  MOZ_LOG(
      gSHLog, LogLevel::Verbose,
      ("SessionHistoryEntry::SetByLoadId(%" PRIu64 " - %p)", aLoadId, aEntry));
  sLoadIdToEntry->InsertOrUpdate(
      aLoadId, LoadingEntry{
                   .mEntry = aEntry,
                   .mInfoSnapshotForValidation =
                       MakeUnique<SessionHistoryInfo>(aEntry->Info()),
               });
}

void SessionHistoryEntry::RemoveLoadId(uint64_t aLoadId) {
  MOZ_ASSERT(XRE_IsParentProcess());
  if (!sLoadIdToEntry) {
    return;
  }

  MOZ_LOG(gSHLog, LogLevel::Verbose,
          ("SHEntry::RemoveLoadId(%" PRIu64 ")", aLoadId));
  sLoadIdToEntry->Remove(aLoadId);
}

SessionHistoryEntry::SessionHistoryEntry()
    : mInfo(new SessionHistoryInfo()), mID(++gEntryID) {}

SessionHistoryEntry::SessionHistoryEntry(nsDocShellLoadState* aLoadState,
                                         nsIChannel* aChannel)
    : mInfo(new SessionHistoryInfo(aLoadState, aChannel)), mID(++gEntryID) {}

SessionHistoryEntry::SessionHistoryEntry(SessionHistoryInfo* aInfo)
    : mInfo(MakeUnique<SessionHistoryInfo>(*aInfo)), mID(++gEntryID) {}

SessionHistoryEntry::SessionHistoryEntry(const SessionHistoryEntry& aEntry)
    : mInfo(MakeUnique<SessionHistoryInfo>(*aEntry.mInfo)),
      mParent(aEntry.mParent),
      mID(aEntry.mID),
      mBCHistoryLength(aEntry.mBCHistoryLength) {}

SessionHistoryEntry::~SessionHistoryEntry() {
  // Null out the mParent pointers on all our kids.
  for (nsISHEntry* entry : mChildren) {
    if (entry) {
      entry->SetParent(nullptr);
    }
  }

  if (sLoadIdToEntry) {
    sLoadIdToEntry->RemoveIf(
        [this](auto& aIter) { return aIter.Data().mEntry == this; });
    if (sLoadIdToEntry->IsEmpty()) {
      delete sLoadIdToEntry;
      sLoadIdToEntry = nullptr;
    }
  }
}

NS_IMPL_ISUPPORTS(SessionHistoryEntry, nsISHEntry, SessionHistoryEntry,
                  nsISupportsWeakReference)

NS_IMETHODIMP
SessionHistoryEntry::GetURI(nsIURI** aURI) {
  nsCOMPtr<nsIURI> uri = mInfo->mURI;
  uri.forget(aURI);
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetURI(nsIURI* aURI) {
  mInfo->mURI = aURI;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetOriginalURI(nsIURI** aOriginalURI) {
  nsCOMPtr<nsIURI> originalURI = mInfo->mOriginalURI;
  originalURI.forget(aOriginalURI);
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetOriginalURI(nsIURI* aOriginalURI) {
  mInfo->mOriginalURI = aOriginalURI;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetResultPrincipalURI(nsIURI** aResultPrincipalURI) {
  nsCOMPtr<nsIURI> resultPrincipalURI = mInfo->mResultPrincipalURI;
  resultPrincipalURI.forget(aResultPrincipalURI);
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetResultPrincipalURI(nsIURI* aResultPrincipalURI) {
  mInfo->mResultPrincipalURI = aResultPrincipalURI;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetUnstrippedURI(nsIURI** aUnstrippedURI) {
  nsCOMPtr<nsIURI> unstrippedURI = mInfo->mUnstrippedURI;
  unstrippedURI.forget(aUnstrippedURI);
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetUnstrippedURI(nsIURI* aUnstrippedURI) {
  mInfo->mUnstrippedURI = aUnstrippedURI;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetLoadReplace(bool* aLoadReplace) {
  *aLoadReplace = mInfo->mLoadReplace;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetLoadReplace(bool aLoadReplace) {
  mInfo->mLoadReplace = aLoadReplace;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetTitle(nsAString& aTitle) {
  aTitle = mInfo->mTitle;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetTitle(const nsAString& aTitle) {
  if (aTitle.Equals(mInfo->GetTitle())) {
    return NS_OK;
  }

  mInfo->SetTitle(aTitle);
  if (nsCOMPtr<nsISHistory> sHistory =
          do_QueryReferent(SharedInfo()->mSHistory)) {
    sHistory->NotifyOnEntryUpdated(this);
  }
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetName(nsAString& aName) {
  aName = mInfo->mName;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetName(const nsAString& aName) {
  mInfo->mName = aName;
  if (nsCOMPtr<nsISHistory> sHistory =
          do_QueryReferent(SharedInfo()->mSHistory)) {
    sHistory->NotifyOnEntryUpdated(this);
  }
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetIsSubFrame(bool* aIsSubFrame) {
  *aIsSubFrame = SharedInfo()->mIsFrameNavigation;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetIsSubFrame(bool aIsSubFrame) {
  SharedInfo()->mIsFrameNavigation = aIsSubFrame;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetHasUserInteraction(bool* aFlag) {
  // The back button and menulist deal with root/top-level
  // session history entries, thus we annotate only the root entry.
  if (!mParent) {
    *aFlag = mInfo->mHasUserInteraction;
  } else {
    nsCOMPtr<nsISHEntry> root = nsSHistory::GetRootSHEntry(this);
    root->GetHasUserInteraction(aFlag);
  }
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetHasUserInteraction(bool aFlag) {
  // The back button and menulist deal with root/top-level
  // session history entries, thus we annotate only the root entry.
  if (!mParent) {
    mInfo->mHasUserInteraction = aFlag;
  } else {
    nsCOMPtr<nsISHEntry> root = nsSHistory::GetRootSHEntry(this);
    root->SetHasUserInteraction(aFlag);
  }
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetHasUserActivation(bool* aFlag) {
  *aFlag = mInfo->mHasUserActivation;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetHasUserActivation(bool aFlag) {
  mInfo->mHasUserActivation = aFlag;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetReferrerInfo(nsIReferrerInfo** aReferrerInfo) {
  nsCOMPtr<nsIReferrerInfo> referrerInfo = mInfo->mReferrerInfo;
  referrerInfo.forget(aReferrerInfo);
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetReferrerInfo(nsIReferrerInfo* aReferrerInfo) {
  mInfo->mReferrerInfo = aReferrerInfo;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetIsInBFCache(bool* aResult) {
  *aResult = !!SharedInfo()->mFrameLoader;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetSticky(bool* aSticky) {
  *aSticky = SharedInfo()->mSticky;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetSticky(bool aSticky) {
  SharedInfo()->mSticky = aSticky;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetPostData(nsIInputStream** aPostData) {
  *aPostData = mInfo->GetPostData().take();
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetPostData(nsIInputStream* aPostData) {
  mInfo->SetPostData(aPostData);
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetHasPostData(bool* aResult) {
  *aResult = mInfo->HasPostData();
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetLayoutHistoryState(
    nsILayoutHistoryState** aLayoutHistoryState) {
  nsCOMPtr<nsILayoutHistoryState> layoutHistoryState =
      SharedInfo()->mLayoutHistoryState;
  layoutHistoryState.forget(aLayoutHistoryState);
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetLayoutHistoryState(
    nsILayoutHistoryState* aLayoutHistoryState) {
  SharedInfo()->mLayoutHistoryState = aLayoutHistoryState;
  if (SharedInfo()->mLayoutHistoryState) {
    SharedInfo()->mLayoutHistoryState->SetScrollPositionOnly(
        !SharedInfo()->mSaveLayoutState);
  }
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetParent(nsISHEntry** aParent) {
  nsCOMPtr<nsISHEntry> parent = do_QueryReferent(mParent);
  parent.forget(aParent);
  return NS_OK;
}

already_AddRefed<SessionHistoryEntry> SessionHistoryEntry::GetParent() {
  RefPtr<SessionHistoryEntry> parent = do_QueryReferent(mParent);
  return parent.forget();
}

NS_IMETHODIMP
SessionHistoryEntry::SetParent(nsISHEntry* aParent) {
  mParent = do_GetWeakReference(aParent);
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetLoadType(uint32_t* aLoadType) {
  *aLoadType = mInfo->mLoadType;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetLoadType(uint32_t aLoadType) {
  mInfo->mLoadType = aLoadType;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetID(uint32_t* aID) {
  *aID = mID;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetID(uint32_t aID) {
  mID = aID;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetCacheKey(uint32_t* aCacheKey) {
  *aCacheKey = SharedInfo()->mCacheKey;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetCacheKey(uint32_t aCacheKey) {
  SharedInfo()->mCacheKey = aCacheKey;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetSaveLayoutStateFlag(bool* aSaveLayoutStateFlag) {
  *aSaveLayoutStateFlag = SharedInfo()->mSaveLayoutState;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetSaveLayoutStateFlag(bool aSaveLayoutStateFlag) {
  SharedInfo()->mSaveLayoutState = aSaveLayoutStateFlag;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetContentType(nsACString& aContentType) {
  aContentType = SharedInfo()->mContentType;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetContentType(const nsACString& aContentType) {
  SharedInfo()->mContentType = aContentType;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetURIWasModified(bool* aURIWasModified) {
  *aURIWasModified = mInfo->mURIWasModified;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetURIWasModified(bool aURIWasModified) {
  mInfo->mURIWasModified = aURIWasModified;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetTriggeringPrincipal(
    nsIPrincipal** aTriggeringPrincipal) {
  nsCOMPtr<nsIPrincipal> triggeringPrincipal =
      SharedInfo()->mTriggeringPrincipal;
  triggeringPrincipal.forget(aTriggeringPrincipal);
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetTriggeringPrincipal(
    nsIPrincipal* aTriggeringPrincipal) {
  SharedInfo()->mTriggeringPrincipal = aTriggeringPrincipal;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetPrincipalToInherit(nsIPrincipal** aPrincipalToInherit) {
  nsCOMPtr<nsIPrincipal> principalToInherit = SharedInfo()->mPrincipalToInherit;
  principalToInherit.forget(aPrincipalToInherit);
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetPrincipalToInherit(nsIPrincipal* aPrincipalToInherit) {
  SharedInfo()->mPrincipalToInherit = aPrincipalToInherit;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetPartitionedPrincipalToInherit(
    nsIPrincipal** aPartitionedPrincipalToInherit) {
  nsCOMPtr<nsIPrincipal> partitionedPrincipalToInherit =
      SharedInfo()->mPartitionedPrincipalToInherit;
  partitionedPrincipalToInherit.forget(aPartitionedPrincipalToInherit);
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetPartitionedPrincipalToInherit(
    nsIPrincipal* aPartitionedPrincipalToInherit) {
  SharedInfo()->mPartitionedPrincipalToInherit = aPartitionedPrincipalToInherit;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetPolicyContainer(nsIPolicyContainer** aPolicyContainer) {
  nsCOMPtr<nsIPolicyContainer> policyContainer = SharedInfo()->mPolicyContainer;
  policyContainer.forget(aPolicyContainer);
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetPolicyContainer(nsIPolicyContainer* aPolicyContainer) {
  nsCOMPtr<nsIURI> uri = mInfo->mURI;
  if (CSP_ShouldURIInheritCSP(uri)) {
    SharedInfo()->mPolicyContainer = aPolicyContainer;
  }
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetStateData(nsIStructuredCloneContainer** aStateData) {
  RefPtr<nsStructuredCloneContainer> stateData = mInfo->mStateData;
  stateData.forget(aStateData);
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetStateData(nsIStructuredCloneContainer* aStateData) {
  mInfo->mStateData = static_cast<nsStructuredCloneContainer*>(aStateData);
  return NS_OK;
}

const nsID& SessionHistoryEntry::DocshellID() const {
  return SharedInfo()->mDocShellID;
}

NS_IMETHODIMP
SessionHistoryEntry::GetDocshellID(nsID& aDocshellID) {
  aDocshellID = DocshellID();
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetDocshellID(const nsID& aDocshellID) {
  SharedInfo()->mDocShellID = aDocshellID;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetNavigationKey(nsID& aNavigationKey) {
  aNavigationKey = mInfo->NavigationKey();
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetNavigationKey(const nsID& aNavigationKey) {
  mInfo->mNavigationKey = aNavigationKey;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetNavigationId(nsID& aNavigationId) {
  aNavigationId = mInfo->NavigationId();
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetNavigationId(const nsID& aNavigationId) {
  mInfo->mNavigationId = aNavigationId;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetIsSrcdocEntry(bool* aIsSrcdocEntry) {
  *aIsSrcdocEntry = mInfo->mSrcdocData.isSome();
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetSrcdocData(nsAString& aSrcdocData) {
  aSrcdocData = mInfo->mSrcdocData.valueOr(EmptyString());
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetSrcdocData(const nsAString& aSrcdocData) {
  mInfo->mSrcdocData = Some(nsString(aSrcdocData));
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetBaseURI(nsIURI** aBaseURI) {
  nsCOMPtr<nsIURI> baseURI = mInfo->mBaseURI;
  baseURI.forget(aBaseURI);
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetBaseURI(nsIURI* aBaseURI) {
  mInfo->mBaseURI = aBaseURI;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetScrollRestorationIsManual(
    bool* aScrollRestorationIsManual) {
  *aScrollRestorationIsManual = mInfo->mScrollRestorationIsManual;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetScrollRestorationIsManual(
    bool aScrollRestorationIsManual) {
  mInfo->mScrollRestorationIsManual = aScrollRestorationIsManual;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetLoadedInThisProcess(bool* aLoadedInThisProcess) {
  // FIXME
  //*aLoadedInThisProcess = mInfo->mLoadedInThisProcess;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetShistory(nsISHistory** aShistory) {
  nsCOMPtr<nsISHistory> sHistory = do_QueryReferent(SharedInfo()->mSHistory);
  sHistory.forget(aShistory);
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetShistory(nsISHistory* aShistory) {
  nsWeakPtr shistory = do_GetWeakReference(aShistory);
  // mSHistory can not be changed once it's set
  MOZ_ASSERT(!SharedInfo()->mSHistory || (SharedInfo()->mSHistory == shistory));
  SharedInfo()->mSHistory = shistory;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetLastTouched(uint32_t* aLastTouched) {
  *aLastTouched = SharedInfo()->mLastTouched;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetLastTouched(uint32_t aLastTouched) {
  SharedInfo()->mLastTouched = aLastTouched;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetChildCount(int32_t* aChildCount) {
  *aChildCount = mChildren.Length();
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::IsTransient(bool* aIsTransient) {
  *aIsTransient = mInfo->IsTransient();
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetTransient() {
  mInfo->SetTransient();
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetScrollPosition(int32_t* aX, int32_t* aY) {
  *aX = mInfo->mScrollPositionX;
  *aY = mInfo->mScrollPositionY;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetScrollPosition(int32_t aX, int32_t aY) {
  mInfo->mScrollPositionX = aX;
  mInfo->mScrollPositionY = aY;
  return NS_OK;
}

NS_IMETHODIMP_(void)
SessionHistoryEntry::GetViewerBounds(nsIntRect& bounds) {
  bounds = SharedInfo()->mViewerBounds;
}

NS_IMETHODIMP_(void)
SessionHistoryEntry::SetViewerBounds(const nsIntRect& bounds) {
  SharedInfo()->mViewerBounds = bounds;
}

NS_IMETHODIMP
SessionHistoryEntry::InitLayoutHistoryState(
    nsILayoutHistoryState** aLayoutHistoryState) {
  if (!SharedInfo()->mLayoutHistoryState) {
    nsCOMPtr<nsILayoutHistoryState> historyState;
    historyState = NS_NewLayoutHistoryState();
    SetLayoutHistoryState(historyState);
  }

  return GetLayoutHistoryState(aLayoutHistoryState);
}

NS_IMETHODIMP
SessionHistoryEntry::Create(
    nsIURI* aURI, const nsAString& aTitle, nsIInputStream* aInputStream,
    uint32_t aCacheKey, const nsACString& aContentType,
    nsIPrincipal* aTriggeringPrincipal, nsIPrincipal* aPrincipalToInherit,
    nsIPrincipal* aPartitionedPrincipalToInherit,
    nsIPolicyContainer* aPolicyContainer, const nsID& aDocshellID,
    bool aDynamicCreation, nsIURI* aOriginalURI, nsIURI* aResultPrincipalURI,
    nsIURI* aUnstrippedURI, bool aLoadReplace, nsIReferrerInfo* aReferrerInfo,
    const nsAString& aSrcdoc, bool aSrcdocEntry, nsIURI* aBaseURI,
    bool aSaveLayoutState, bool aExpired, bool aUserActivation) {
  MOZ_CRASH("Might need to implement this");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
SessionHistoryEntry::Clone(nsISHEntry** aEntry) {
  RefPtr<SessionHistoryEntry> entry = new SessionHistoryEntry(*this);

  // These are not copied for some reason, we're not sure why.
  entry->mInfo->mLoadType = 0;
  entry->mInfo->mScrollPositionX = 0;
  entry->mInfo->mScrollPositionY = 0;
  entry->mInfo->mScrollRestorationIsManual = false;

  entry->mInfo->mHasUserInteraction = false;

  entry.forget(aEntry);

  return NS_OK;
}

NS_IMETHODIMP_(bool)
SessionHistoryEntry::IsDynamicallyAdded() {
  return SharedInfo()->mDynamicallyCreated;
}

void SessionHistoryEntry::SetWireframe(const Maybe<Wireframe>& aWireframe) {
  mWireframe = aWireframe;
}

void SessionHistoryEntry::SetIsDynamicallyAdded(bool aDynamic) {
  MOZ_ASSERT_IF(SharedInfo()->mDynamicallyCreated, aDynamic);
  SharedInfo()->mDynamicallyCreated = aDynamic;
}

NS_IMETHODIMP
SessionHistoryEntry::HasDynamicallyAddedChild(bool* aHasDynamicallyAddedChild) {
  for (const auto& child : mChildren) {
    if (child && child->IsDynamicallyAdded()) {
      *aHasDynamicallyAddedChild = true;
      return NS_OK;
    }
  }
  *aHasDynamicallyAddedChild = false;
  return NS_OK;
}

NS_IMETHODIMP_(bool)
SessionHistoryEntry::HasBFCacheEntry(SHEntrySharedParentState* aEntry) {
  return SharedInfo() == aEntry;
}

NS_IMETHODIMP
SessionHistoryEntry::AdoptBFCacheEntry(nsISHEntry* aEntry) {
  auto* entry = static_cast<SessionHistoryEntry*>(aEntry);
  NS_ENSURE_STATE(entry && entry->mInfo->mSharedState.Get());

  mInfo->mSharedState = entry->mInfo->mSharedState;

  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SharesDocumentWith(nsISHEntry* aEntry,
                                        bool* aSharesDocumentWith) {
  SessionHistoryEntry* entry = aEntry->GetAsSessionHistoryEntry();

  MOZ_ASSERT_IF(entry->SharedInfo() != SharedInfo(),
                entry->SharedInfo()->GetId() != SharedInfo()->GetId());

  *aSharesDocumentWith = entry->SharedInfo() == SharedInfo();
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetLoadTypeAsHistory() {
  mInfo->mLoadType = LOAD_HISTORY;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::AddChild(nsISHEntry* aChild, int32_t aOffset,
                              bool aUseRemoteSubframes) {
  AddChild(static_cast<SessionHistoryEntry*>(aChild), aOffset,
           aUseRemoteSubframes);

  return NS_OK;
}

void SessionHistoryEntry::AddChild(SessionHistoryEntry* aChild, int32_t aOffset,
                                   bool aUseRemoteSubframes) {
  if (aChild) {
    aChild->SetParent(this);
  }

  if (aOffset < 0) {
    mChildren.AppendElement(aChild);
    return;
  }

  //
  // Bug 52670: Ensure children are added in order.
  //
  //  Later frames in the child list may load faster and get appended
  //  before earlier frames, causing session history to be scrambled.
  //  By growing the list here, they are added to the right position.

  int32_t length = mChildren.Length();

  //  Assert that aOffset will not be so high as to grow us a lot.
  NS_ASSERTION(aOffset < length + 1023, "Large frames array!\n");

  // If the new child is dynamically added, try to add it to aOffset, but if
  // there are non-dynamically added children, the child must be after those.
  if (aChild && aChild->IsDynamicallyAdded()) {
    int32_t lastNonDyn = aOffset - 1;
    for (int32_t i = aOffset; i < length; ++i) {
      SessionHistoryEntry* entry = mChildren[i];
      if (entry) {
        if (entry->IsDynamicallyAdded()) {
          break;
        }

        lastNonDyn = i;
      }
    }

    // If aOffset is larger than Length(), we must first truncate the array.
    if (aOffset > length) {
      mChildren.SetLength(aOffset);
    }

    mChildren.InsertElementAt(lastNonDyn + 1, aChild);

    return;
  }

  // If the new child isn't dynamically added, it should be set to aOffset.
  // If there are dynamically added children before that, those must be moved
  // to be after aOffset.
  if (length > 0) {
    int32_t start = std::min(length - 1, aOffset);
    int32_t dynEntryIndex = -1;
    DebugOnly<SessionHistoryEntry*> dynEntry = nullptr;
    for (int32_t i = start; i >= 0; --i) {
      SessionHistoryEntry* entry = mChildren[i];
      if (entry) {
        if (!entry->IsDynamicallyAdded()) {
          break;
        }

        dynEntryIndex = i;
        dynEntry = entry;
      }
    }

    if (dynEntryIndex >= 0) {
      mChildren.InsertElementsAt(dynEntryIndex, aOffset - dynEntryIndex + 1);
      NS_ASSERTION(mChildren[aOffset + 1] == dynEntry, "Whaat?");
    }
  }

  // Make sure there isn't anything at aOffset.
  if ((uint32_t)aOffset < mChildren.Length()) {
    SessionHistoryEntry* oldChild = mChildren[aOffset];
    if (oldChild && oldChild != aChild) {
      // Under Fission, this can happen when a network-created iframe starts
      // out in-process, moves out-of-process, and then switches back. At that
      // point, we'll create a new network-created DocShell at the same index
      // where we already have an entry for the original network-created
      // DocShell.
      //
      // This should ideally stop being an issue once the Fission-aware
      // session history rewrite is complete.
      NS_ASSERTION(
          aUseRemoteSubframes || NS_IsAboutBlank(oldChild->Info().GetURI()),
          "Adding a child where we already have a child? This may misbehave");
      oldChild->SetParent(nullptr);
    }
  } else {
    mChildren.SetLength(aOffset + 1);
  }

  mChildren.ReplaceElementAt(aOffset, aChild);
}

NS_IMETHODIMP
SessionHistoryEntry::RemoveChild(nsISHEntry* aChild) {
  NS_ENSURE_TRUE(aChild, NS_ERROR_FAILURE);

  RefPtr<SessionHistoryEntry> child = aChild->GetAsSessionHistoryEntry();
  RemoveChild(child);

  return NS_OK;
}

void SessionHistoryEntry::RemoveChild(SessionHistoryEntry* aChild) {
  bool childRemoved = false;
  if (aChild->IsDynamicallyAdded()) {
    childRemoved = mChildren.RemoveElement(aChild);
  } else {
    int32_t index = mChildren.IndexOf(aChild);
    if (index >= 0) {
      // Other alive non-dynamic child docshells still keep mChildOffset,
      // so we don't want to change the indices here.
      mChildren.ReplaceElementAt(index, nullptr);
      childRemoved = true;
    }
  }

  if (childRemoved) {
    aChild->SetParent(nullptr);

    // reduce the child count, i.e. remove empty children at the end
    for (int32_t i = mChildren.Length() - 1; i >= 0 && !mChildren[i]; --i) {
      mChildren.RemoveElementAt(i);
    }
  }
}

NS_IMETHODIMP
SessionHistoryEntry::GetChildAt(int32_t aIndex, nsISHEntry** aChild) {
  nsCOMPtr<nsISHEntry> child = mChildren.SafeElementAt(aIndex);
  child.forget(aChild);
  return NS_OK;
}

void SessionHistoryEntry::GetChildAt(int32_t aIndex,
                                     SessionHistoryEntry** aChild) {
  RefPtr<SessionHistoryEntry> child = mChildren.SafeElementAt(aIndex);
  child.forget(aChild);
}

SessionHistoryEntry*
SessionHistoryEntry::GetChildSHEntryIfHasNoDynamicallyAddedChild(
    int32_t aChildOffset) {
  bool dynamicallyAddedChild = false;
  HasDynamicallyAddedChild(&dynamicallyAddedChild);
  if (dynamicallyAddedChild) {
    return nullptr;
  }

  // If the user did a shift-reload on this frameset page,
  // we don't want to load the subframes from history.
  if (IsForceReloadType(mInfo->mLoadType) || mInfo->mLoadType == LOAD_REFRESH) {
    return nullptr;
  }

  /* Before looking for the subframe's url, check
   * the expiration status of the parent. If the parent
   * has expired from cache, then subframes will not be
   * loaded from history in certain situations.
   * If the user pressed reload and the parent frame has expired
   *  from cache, we do not want to load the child frame from history.
   */
  if (SharedInfo()->mExpired && (mInfo->mLoadType == LOAD_RELOAD_NORMAL)) {
    // The parent has expired. Return null.
    return nullptr;
  }
  // Get the child subframe from session history.
  auto* child = mChildren.SafeElementAt(aChildOffset);
  if (child) {
    // Set the parent's Load Type on the child
    child->SetLoadType(mInfo->mLoadType);
  }
  return child;
}

NS_IMETHODIMP_(void)
SessionHistoryEntry::GetChildSHEntryIfHasNoDynamicallyAddedChild(
    int32_t aChildOffset, nsISHEntry** aChild) {
  *aChild = GetChildSHEntryIfHasNoDynamicallyAddedChild(aChildOffset);
}

NS_IMETHODIMP
SessionHistoryEntry::ReplaceChild(nsISHEntry* aNewChild) {
  NS_ENSURE_STATE(aNewChild);

  RefPtr<SessionHistoryEntry> newChild = aNewChild->GetAsSessionHistoryEntry();
  return ReplaceChild(newChild) ? NS_OK : NS_ERROR_FAILURE;
}

bool SessionHistoryEntry::ReplaceChild(SessionHistoryEntry* aNewChild) {
  const nsID& docshellID = aNewChild->DocshellID();

  for (uint32_t i = 0; i < mChildren.Length(); ++i) {
    if (mChildren[i] && docshellID == mChildren[i]->DocshellID()) {
      mChildren[i]->SetParent(nullptr);
      mChildren.ReplaceElementAt(i, aNewChild);
      aNewChild->SetParent(this);

      return true;
    }
  }

  return false;
}

NS_IMETHODIMP_(void)
SessionHistoryEntry::ClearEntry() {
  int32_t childCount = GetChildCount();
  // Remove all children of this entry
  for (int32_t i = childCount; i > 0; --i) {
    nsCOMPtr<nsISHEntry> child;
    GetChildAt(i - 1, getter_AddRefs(child));
    RemoveChild(child);
  }
}

NS_IMETHODIMP
SessionHistoryEntry::CreateLoadInfo(nsDocShellLoadState** aLoadState) {
  NS_WARNING("We shouldn't be calling this!");
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetBfcacheID(uint64_t* aBfcacheID) {
  *aBfcacheID = SharedInfo()->mId;
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::GetWireframe(JSContext* aCx,
                                  JS::MutableHandle<JS::Value> aOut) {
  if (mWireframe.isNothing()) {
    aOut.set(JS::NullValue());
  } else if (NS_WARN_IF(!mWireframe->ToObjectInternal(aCx, aOut))) {
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

NS_IMETHODIMP
SessionHistoryEntry::SetWireframe(JSContext* aCx, JS::Handle<JS::Value> aArg) {
  if (aArg.isNullOrUndefined()) {
    mWireframe = Nothing();
    return NS_OK;
  }

  Wireframe wireframe;
  if (aArg.isObject() && wireframe.Init(aCx, aArg)) {
    mWireframe = Some(std::move(wireframe));
    return NS_OK;
  }

  return NS_ERROR_INVALID_ARG;
}

NS_IMETHODIMP_(void)
SessionHistoryEntry::SyncTreesForSubframeNavigation(
    nsISHEntry* aEntry, mozilla::dom::BrowsingContext* aTopBC,
    mozilla::dom::BrowsingContext* aIgnoreBC) {
  // We need to sync up the browsing context and session history trees for
  // subframe navigation.  If the load was in a subframe, we forward up to
  // the top browsing context, which will then recursively sync up all browsing
  // contexts to their corresponding entries in the new session history tree. If
  // we don't do this, then we can cache a content viewer on the wrong cloned
  // entry, and subsequently restore it at the wrong time.
  RefPtr<SessionHistoryEntry> newRootEntry =
      nsSHistory::GetRootSHEntry(aEntry->GetAsSessionHistoryEntry());
  if (newRootEntry) {
    // newRootEntry is now the new root entry.
    // Find the old root entry as well.

    // Need a strong ref. on |oldRootEntry| so it isn't destroyed when
    // SetChildHistoryEntry() does SwapHistoryEntries() (bug 304639).
    RefPtr<SessionHistoryEntry> oldRootEntry = nsSHistory::GetRootSHEntry(this);

    if (oldRootEntry) {
      nsSHistory::SwapEntriesData data = {aIgnoreBC, newRootEntry, nullptr};
      nsSHistory::SetChildHistoryEntry(oldRootEntry, aTopBC, 0, &data);
    }
  }
}

void SessionHistoryEntry::ReplaceWith(const SessionHistoryEntry& aSource) {
  mInfo = MakeUnique<SessionHistoryInfo>(*aSource.mInfo);
  mChildren.Clear();
}

SHEntrySharedParentState* SessionHistoryEntry::SharedInfo() const {
  return static_cast<SHEntrySharedParentState*>(mInfo->mSharedState.Get());
}

void SessionHistoryEntry::SetFrameLoader(nsFrameLoader* aFrameLoader) {
  MOZ_DIAGNOSTIC_ASSERT(!aFrameLoader || !SharedInfo()->mFrameLoader);
  // If the pref is disabled, we still allow evicting the existing entries.
  MOZ_RELEASE_ASSERT(!aFrameLoader || mozilla::BFCacheInParent());
  SharedInfo()->SetFrameLoader(aFrameLoader);
  if (aFrameLoader) {
    if (BrowsingContext* bc = aFrameLoader->GetMaybePendingBrowsingContext()) {
      if (BrowserParent* bp = bc->Canonical()->GetBrowserParent()) {
        bp->VisitAll([&](BrowserParent* aBp) { aBp->Deactivated(); });
      }
    }

    // When a new frameloader is stored, try to evict some older
    // frameloaders. Non-SHIP session history has a similar call in
    // nsDocumentViewer::Show.
    nsCOMPtr<nsISHistory> shistory;
    GetShistory(getter_AddRefs(shistory));
    if (shistory) {
      int32_t index = 0;
      shistory->GetIndex(&index);
      shistory->EvictOutOfRangeDocumentViewers(index);
    }
  }
}

nsFrameLoader* SessionHistoryEntry::GetFrameLoader() {
  return SharedInfo()->mFrameLoader;
}

void SessionHistoryEntry::SetInfo(SessionHistoryInfo* aInfo) {
  // FIXME Assert that we're not changing shared state!
  mInfo = MakeUnique<SessionHistoryInfo>(*aInfo);
}

already_AddRefed<nsIURI> SessionHistoryInfo::GetURIOrInheritedForAboutBlank()
    const {
  if (mURI && NS_IsAboutBlankAllowQueryAndFragment(mURI)) {
    auto* principal = GetPrincipalToInherit();
    if (principal) {
      return principal->GetURI();
    }
  }
  return do_AddRef(mURI);
}

already_AddRefed<nsIURI> SessionHistoryEntry::GetURIOrInheritedForAboutBlank()
    const {
  return mInfo->GetURIOrInheritedForAboutBlank();
}

already_AddRefed<nsSHistory> SessionHistoryEntry::GetSessionHistory() {
  if (RefPtr<SessionHistoryEntry> rootSHEntry =
          nsSHistory::GetRootSHEntry(this)) {
    return rootSHEntry->GetShistory().downcast<nsSHistory>();
  }
  return nullptr;
}

/* static */
Maybe<PreviousSessionHistoryInfo>
PreviousSessionHistoryInfo::CreateValidatedPreviousEntry(
    const SessionHistoryInfo& aCurrentEntry,
    const Maybe<SessionHistoryInfo>& aPreviousEntryForActivation,
    Maybe<NavigationType> aNavigationType) {
  // https://html.spec.whatwg.org/#update-document-for-history-step-application
  // Step 7 If all the following are true:
  // * previousEntryForActivation is given;
  // * navigationType is non-null; and
  // * navigationType is "reload" or previousEntryForActivation's document is
  //   not document,
  if (!aPreviousEntryForActivation || !aNavigationType ||
      (*aNavigationType != NavigationType::Reload &&
       aCurrentEntry.SharesDocumentWith(*aPreviousEntryForActivation))) {
    return Nothing();
  }

  // 7.4 Otherwise, if all the following are true:
  //     navigationType is "replace";
  //     previousEntryForActivation's document state's origin is same origin
  //     with document's origin; and previousEntryForActivation's document's
  //     initial about:blank is false,
  // then set activation's old entry to a new NavigationHistoryEntry in
  // navigation's relevant realm, whose session history entry is
  // previousEntryForActivation.
  nsCOMPtr previousURI =
      aPreviousEntryForActivation->GetURIOrInheritedForAboutBlank();
  nsCOMPtr currentURI = aCurrentEntry.GetURIOrInheritedForAboutBlank();
  if (NS_FAILED(nsContentUtils::GetSecurityManager()->CheckSameOriginURI(
          currentURI, previousURI, false, false))) {
    return Some(PreviousSessionHistoryInfo{});
  }

  return Some(PreviousSessionHistoryInfo(aPreviousEntryForActivation));
}

SessionHistoryEntry* SessionHistoryEntry::GetAsSessionHistoryEntry() {
  return this;
}
}  // namespace dom
}  // namespace mozilla

namespace IPC {

void ParamTraits<mozilla::dom::SessionHistoryInfo>::Write(
    IPC::MessageWriter* aWriter,
    const mozilla::dom::SessionHistoryInfo& aParam) {
  nsCOMPtr<nsIInputStream> postData = aParam.GetPostData();

  WriteParam(aWriter, aParam.mURI);
  WriteParam(aWriter, aParam.mOriginalURI);
  WriteParam(aWriter, aParam.mResultPrincipalURI);
  WriteParam(aWriter, aParam.mUnstrippedURI);
  WriteParam(aWriter, aParam.mReferrerInfo);
  WriteParam(aWriter, aParam.mTitle);
  WriteParam(aWriter, aParam.mName);
  WriteParam(aWriter, postData);
  WriteParam(aWriter, aParam.mLoadType);
  WriteParam(aWriter, aParam.mScrollPositionX);
  WriteParam(aWriter, aParam.mScrollPositionY);
  WriteParam(aWriter, aParam.mStateData);
  WriteParam(aWriter, aParam.mSrcdocData);
  WriteParam(aWriter, aParam.mBaseURI);
  WriteParam(aWriter, aParam.mNavigationKey);
  WriteParam(aWriter, aParam.mNavigationId);
  WriteParam(aWriter, aParam.mLoadReplace);
  WriteParam(aWriter, aParam.mURIWasModified);
  WriteParam(aWriter, aParam.mScrollRestorationIsManual);
  WriteParam(aWriter, aParam.mTransient);
  WriteParam(aWriter, aParam.mHasUserInteraction);
  WriteParam(aWriter, aParam.mHasUserActivation);
  WriteParam(aWriter, aParam.mSharedState.Get()->mId);
  WriteParam(aWriter, aParam.mSharedState.Get()->mTriggeringPrincipal);
  WriteParam(aWriter, aParam.mSharedState.Get()->mPrincipalToInherit);
  WriteParam(aWriter,
             aParam.mSharedState.Get()->mPartitionedPrincipalToInherit);
  WriteParam(aWriter, aParam.mSharedState.Get()->mPolicyContainer);
  WriteParam(aWriter, aParam.mSharedState.Get()->mContentType);
  WriteParam(aWriter, aParam.mSharedState.Get()->mLayoutHistoryState);
  WriteParam(aWriter, aParam.mSharedState.Get()->mCacheKey);
  WriteParam(aWriter, aParam.mSharedState.Get()->mIsFrameNavigation);
  WriteParam(aWriter, aParam.mSharedState.Get()->mSaveLayoutState);
  WriteParam(aWriter, aParam.mNavigationAPIState);
}

bool ParamTraits<mozilla::dom::SessionHistoryInfo>::Read(
    IPC::MessageReader* aReader, mozilla::dom::SessionHistoryInfo* aResult) {
  uint64_t sharedId;
  if (!ReadParam(aReader, &aResult->mURI) ||
      !ReadParam(aReader, &aResult->mOriginalURI) ||
      !ReadParam(aReader, &aResult->mResultPrincipalURI) ||
      !ReadParam(aReader, &aResult->mUnstrippedURI) ||
      !ReadParam(aReader, &aResult->mReferrerInfo) ||
      !ReadParam(aReader, &aResult->mTitle) ||
      !ReadParam(aReader, &aResult->mName) ||
      !ReadParam(aReader, &aResult->mPostData) ||
      !ReadParam(aReader, &aResult->mLoadType) ||
      !ReadParam(aReader, &aResult->mScrollPositionX) ||
      !ReadParam(aReader, &aResult->mScrollPositionY) ||
      !ReadParam(aReader, &aResult->mStateData) ||
      !ReadParam(aReader, &aResult->mSrcdocData) ||
      !ReadParam(aReader, &aResult->mBaseURI) ||
      !ReadParam(aReader, &aResult->mNavigationKey) ||
      !ReadParam(aReader, &aResult->mNavigationId) ||
      !ReadParam(aReader, &aResult->mLoadReplace) ||
      !ReadParam(aReader, &aResult->mURIWasModified) ||
      !ReadParam(aReader, &aResult->mScrollRestorationIsManual) ||
      !ReadParam(aReader, &aResult->mTransient) ||
      !ReadParam(aReader, &aResult->mHasUserInteraction) ||
      !ReadParam(aReader, &aResult->mHasUserActivation) ||
      !ReadParam(aReader, &sharedId)) {
    aReader->FatalError("Error reading fields for SessionHistoryInfo");
    return false;
  }

  nsCOMPtr<nsIPrincipal> triggeringPrincipal;
  nsCOMPtr<nsIPrincipal> principalToInherit;
  nsCOMPtr<nsIPrincipal> partitionedPrincipalToInherit;
  nsCOMPtr<nsIPolicyContainer> policyContainer;
  nsCString contentType;
  if (!ReadParam(aReader, &triggeringPrincipal) ||
      !ReadParam(aReader, &principalToInherit) ||
      !ReadParam(aReader, &partitionedPrincipalToInherit) ||
      !ReadParam(aReader, &policyContainer) ||
      !ReadParam(aReader, &contentType)) {
    aReader->FatalError("Error reading fields for SessionHistoryInfo");
    return false;
  }

  // We should always see a cloneable input stream passed to SessionHistoryInfo.
  // This is because it will be cloneable when first read in the parent process
  // from the nsHttpChannel (which forces streams to be cloneable), and future
  // streams in content will be wrapped in
  // nsMIMEInputStream(RemoteLazyInputStream) which is also cloneable.
  if (aResult->mPostData && !NS_InputStreamIsCloneable(aResult->mPostData)) {
    aReader->FatalError(
        "Unexpected non-cloneable postData for SessionHistoryInfo");
    return false;
  }

  mozilla::dom::SHEntrySharedParentState* sharedState = nullptr;
  if (XRE_IsParentProcess()) {
    sharedState = mozilla::dom::SHEntrySharedParentState::Lookup(sharedId);
  }

  if (sharedState) {
    aResult->mSharedState.Set(sharedState);

    MOZ_ASSERT(triggeringPrincipal
                   ? triggeringPrincipal->Equals(
                         aResult->mSharedState.Get()->mTriggeringPrincipal)
                   : !aResult->mSharedState.Get()->mTriggeringPrincipal,
               "We don't expect this to change!");
    MOZ_ASSERT(principalToInherit
                   ? principalToInherit->Equals(
                         aResult->mSharedState.Get()->mPrincipalToInherit)
                   : !aResult->mSharedState.Get()->mPrincipalToInherit,
               "We don't expect this to change!");
    MOZ_ASSERT(
        partitionedPrincipalToInherit
            ? partitionedPrincipalToInherit->Equals(
                  aResult->mSharedState.Get()->mPartitionedPrincipalToInherit)
            : !aResult->mSharedState.Get()->mPartitionedPrincipalToInherit,
        "We don't expect this to change!");

    MOZ_ASSERT(policyContainer
                   ? PolicyContainer::Equals(
                         PolicyContainer::Cast(policyContainer),
                         PolicyContainer::Cast(
                             aResult->mSharedState.Get()->mPolicyContainer))
                   : !aResult->mSharedState.Get()->mPolicyContainer,
               "We don't expect this to change!");
    MOZ_ASSERT(contentType.Equals(aResult->mSharedState.Get()->mContentType),
               "We don't expect this to change!");
  } else {
    aResult->mSharedState.ChangeId(sharedId);
    aResult->mSharedState.Get()->mTriggeringPrincipal =
        triggeringPrincipal.forget();
    aResult->mSharedState.Get()->mPrincipalToInherit =
        principalToInherit.forget();
    aResult->mSharedState.Get()->mPartitionedPrincipalToInherit =
        partitionedPrincipalToInherit.forget();
    aResult->mSharedState.Get()->mPolicyContainer = policyContainer.forget();
    aResult->mSharedState.Get()->mContentType = contentType;
  }

  if (!ReadParam(aReader, &aResult->mSharedState.Get()->mLayoutHistoryState) ||
      !ReadParam(aReader, &aResult->mSharedState.Get()->mCacheKey) ||
      !ReadParam(aReader, &aResult->mSharedState.Get()->mIsFrameNavigation) ||
      !ReadParam(aReader, &aResult->mSharedState.Get()->mSaveLayoutState) ||
      !ReadParam(aReader, &aResult->mNavigationAPIState)) {
    aReader->FatalError("Error reading fields for SessionHistoryInfo");
    return false;
  }

  return true;
}

void ParamTraits<mozilla::dom::PreviousSessionHistoryInfo>::Write(
    IPC::MessageWriter* aWriter,
    const mozilla::dom::PreviousSessionHistoryInfo& aParam) {
  WriteParam(aWriter, aParam.mSameOriginSessionHistoryInfo);
}

bool ParamTraits<mozilla::dom::PreviousSessionHistoryInfo>::Read(
    IPC::MessageReader* aReader,
    mozilla::dom::PreviousSessionHistoryInfo* aResult) {
  if (!ReadParam(aReader, &aResult->mSameOriginSessionHistoryInfo)) {
    aReader->FatalError("Error reading fields for PreviousSessionHistoryInfo");
    return false;
  }

  return true;
}

void ParamTraits<mozilla::dom::LoadingSessionHistoryInfo>::Write(
    IPC::MessageWriter* aWriter,
    const mozilla::dom::LoadingSessionHistoryInfo& aParam) {
  WriteParam(aWriter, aParam.mInfo);
  WriteParam(aWriter, aParam.mContiguousEntries);
  WriteParam(aWriter, aParam.mPreviousEntry);
  WriteParam(aWriter, aParam.mTriggeringNavigationType);
  WriteParam(aWriter, aParam.mLoadId);
  WriteParam(aWriter, aParam.mLoadIsFromSessionHistory);
  WriteParam(aWriter, aParam.mOffset);
  WriteParam(aWriter, aParam.mLoadingCurrentEntry);
  WriteParam(aWriter, aParam.mForceMaybeResetName);
}

bool ParamTraits<mozilla::dom::LoadingSessionHistoryInfo>::Read(
    IPC::MessageReader* aReader,
    mozilla::dom::LoadingSessionHistoryInfo* aResult) {
  if (!ReadParam(aReader, &aResult->mInfo) ||
      !ReadParam(aReader, &aResult->mContiguousEntries) ||
      !ReadParam(aReader, &aResult->mPreviousEntry) ||
      !ReadParam(aReader, &aResult->mTriggeringNavigationType) ||
      !ReadParam(aReader, &aResult->mLoadId) ||
      !ReadParam(aReader, &aResult->mLoadIsFromSessionHistory) ||
      !ReadParam(aReader, &aResult->mOffset) ||
      !ReadParam(aReader, &aResult->mLoadingCurrentEntry) ||
      !ReadParam(aReader, &aResult->mForceMaybeResetName)) {
    aReader->FatalError("Error reading fields for LoadingSessionHistoryInfo");
    return false;
  }

  return true;
}

void ParamTraits<nsILayoutHistoryState*>::Write(IPC::MessageWriter* aWriter,
                                                nsILayoutHistoryState* aParam) {
  if (aParam) {
    WriteParam(aWriter, true);
    bool scrollPositionOnly = false;
    nsTArray<nsCString> keys;
    nsTArray<mozilla::PresState> states;
    aParam->GetContents(&scrollPositionOnly, keys, states);
    WriteParam(aWriter, scrollPositionOnly);
    WriteParam(aWriter, keys);
    WriteParam(aWriter, states);
  } else {
    WriteParam(aWriter, false);
  }
}

bool ParamTraits<nsILayoutHistoryState*>::Read(
    IPC::MessageReader* aReader, RefPtr<nsILayoutHistoryState>* aResult) {
  bool hasLayoutHistoryState = false;
  if (!ReadParam(aReader, &hasLayoutHistoryState)) {
    aReader->FatalError("Error reading fields for nsILayoutHistoryState");
    return false;
  }

  if (hasLayoutHistoryState) {
    bool scrollPositionOnly = false;
    nsTArray<nsCString> keys;
    nsTArray<mozilla::PresState> states;
    if (!ReadParam(aReader, &scrollPositionOnly) ||
        !ReadParam(aReader, &keys) || !ReadParam(aReader, &states)) {
      aReader->FatalError("Error reading fields for nsILayoutHistoryState");
    }

    if (keys.Length() != states.Length()) {
      aReader->FatalError("Error reading fields for nsILayoutHistoryState");
      return false;
    }

    *aResult = NS_NewLayoutHistoryState();
    (*aResult)->SetScrollPositionOnly(scrollPositionOnly);
    for (uint32_t i = 0; i < keys.Length(); ++i) {
      mozilla::PresState& state = states[i];
      auto newState = mozilla::MakeUnique<mozilla::PresState>(state);
      (*aResult)->AddState(keys[i], std::move(newState));
    }
  }
  return true;
}

void ParamTraits<mozilla::dom::Wireframe>::Write(
    IPC::MessageWriter* aWriter, const mozilla::dom::Wireframe& aParam) {
  WriteParam(aWriter, aParam.mCanvasBackground);
  WriteParam(aWriter, aParam.mRects);
}

bool ParamTraits<mozilla::dom::Wireframe>::Read(
    IPC::MessageReader* aReader, mozilla::dom::Wireframe* aResult) {
  return ReadParam(aReader, &aResult->mCanvasBackground) &&
         ReadParam(aReader, &aResult->mRects);
}

// Allow sending mozilla::dom::WireframeRectType enums over IPC.
template <>
struct ParamTraits<mozilla::dom::WireframeRectType>
    : public mozilla::dom::WebIDLEnumSerializer<
          mozilla::dom::WireframeRectType> {};

template <>
struct ParamTraits<mozilla::dom::WireframeTaggedRect> {
  static void Write(MessageWriter* aWriter,
                    const mozilla::dom::WireframeTaggedRect& aParam);
  static bool Read(MessageReader* aReader,
                   mozilla::dom::WireframeTaggedRect* aResult);
};

void ParamTraits<mozilla::dom::WireframeTaggedRect>::Write(
    MessageWriter* aWriter, const mozilla::dom::WireframeTaggedRect& aParam) {
  WriteParam(aWriter, aParam.mColor);
  WriteParam(aWriter, aParam.mType);
  WriteParam(aWriter, aParam.mX);
  WriteParam(aWriter, aParam.mY);
  WriteParam(aWriter, aParam.mWidth);
  WriteParam(aWriter, aParam.mHeight);
}

bool ParamTraits<mozilla::dom::WireframeTaggedRect>::Read(
    IPC::MessageReader* aReader, mozilla::dom::WireframeTaggedRect* aResult) {
  return ReadParam(aReader, &aResult->mColor) &&
         ReadParam(aReader, &aResult->mType) &&
         ReadParam(aReader, &aResult->mX) && ReadParam(aReader, &aResult->mY) &&
         ReadParam(aReader, &aResult->mWidth) &&
         ReadParam(aReader, &aResult->mHeight);
}
}  // namespace IPC
