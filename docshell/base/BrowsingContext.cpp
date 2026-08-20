/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/BrowsingContext.h"

#include "ipc/IPCMessageUtils.h"

#ifdef ACCESSIBILITY
#  include "mozilla/a11y/DocAccessibleParent.h"
#  include "mozilla/a11y/Platform.h"
#  include "nsAccessibilityService.h"
#  if defined(XP_WIN)
#    include "mozilla/a11y/AccessibleWrap.h"
#    include "mozilla/a11y/Compatibility.h"
#    include "mozilla/a11y/nsWinUtils.h"
#  endif
#endif
#include "mozilla/AppShutdown.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/BindingIPCUtils.h"
#include "mozilla/dom/BrowserHost.h"
#include "mozilla/dom/BrowserChild.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/BrowsingContextGroup.h"
#include "mozilla/dom/BrowsingContextBinding.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentPictureInPicture.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/HTMLEmbedElement.h"
#include "mozilla/dom/HTMLIFrameElement.h"
#include "mozilla/dom/Location.h"
#include "mozilla/dom/LocationBinding.h"
#include "mozilla/dom/MediaDevices.h"
#include "mozilla/dom/Navigation.h"
#include "mozilla/dom/NavigationUtils.h"
#include "mozilla/dom/Navigator.h"
#include "mozilla/dom/PopupBlocker.h"
#include "mozilla/dom/ReferrerInfo.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/SessionStoreChild.h"
#include "mozilla/dom/SessionStorageManager.h"
#include "mozilla/dom/StructuredCloneTags.h"
#include "mozilla/dom/UserActivationIPCUtils.h"
#include "mozilla/dom/WindowBinding.h"
#include "mozilla/dom/WindowContext.h"
#include "mozilla/dom/WindowGlobalChild.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "mozilla/dom/WindowProxyHolder.h"
#include "mozilla/dom/workerinternals/RuntimeService.h"
#include "mozilla/dom/SyncedContextInlines.h"
#include "mozilla/dom/XULFrameElement.h"
#include "mozilla/ipc/ProtocolUtils.h"
#include "mozilla/net/DocumentLoadListener.h"
#include "mozilla/net/RequestContextService.h"
#include "mozilla/Assertions.h"
#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Components.h"
#include "mozilla/GeolocationService.h"
#include "mozilla/Logging.h"
#include "mozilla/MediaFeatureChange.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_fission.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/StaticPrefs_page_load.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/URLQueryStringStripper.h"
#include "mozilla/EventStateManager.h"
#include "mozilla/glean/DomMetrics.h"
#include "mozilla/StartupTimeline.h"
#include "GeckoProfiler.h"
#include "mozilla/ProfilerMarkers.h"
#include "nsIURIFixup.h"
#include "nsIXULRuntime.h"

#include "mozilla/dom/WorkerCommon.h"
#include "nsExternalHelperAppService.h"
#include "nsDocShell.h"
#include "nsDocShellLoadState.h"
#include "nsFocusManager.h"
#include "nsGlobalWindowInner.h"
#include "nsGlobalWindowOuter.h"
#include "PresShell.h"
#include "nsIObserverService.h"
#include "nsISHistory.h"
#include "nsJSUtils.h"
#include "nsContentUtils.h"
#include "nsPIDOMWindowInlines.h"
#include "nsQueryObject.h"
#include "nsSandboxFlags.h"
#include "nsScreen.h"
#include "nsScriptError.h"
#include "nsThreadUtils.h"
#include "xpcprivate.h"

#include "AutoplayPolicy.h"
#include "GVAutoplayRequestStatusIPC.h"

extern mozilla::LazyLogModule gAutoplayPermissionLog;
extern mozilla::LazyLogModule gNavigationAPILog;
extern mozilla::LazyLogModule gTimeoutDeferralLog;

#define AUTOPLAY_LOG(msg, ...) \
  MOZ_LOG(gAutoplayPermissionLog, LogLevel::Debug, (msg, ##__VA_ARGS__))

namespace IPC {
// Allow serialization and deserialization of OrientationType over IPC
template <>
struct ParamTraits<mozilla::dom::OrientationType>
    : public mozilla::dom::WebIDLEnumSerializer<mozilla::dom::OrientationType> {
};

template <>
struct ParamTraits<mozilla::dom::DisplayMode>
    : public mozilla::dom::WebIDLEnumSerializer<mozilla::dom::DisplayMode> {};

template <>
struct ParamTraits<mozilla::dom::PrefersColorSchemeOverride>
    : public mozilla::dom::WebIDLEnumSerializer<
          mozilla::dom::PrefersColorSchemeOverride> {};

template <>
struct ParamTraits<mozilla::dom::ForcedColorsOverride>
    : public mozilla::dom::WebIDLEnumSerializer<
          mozilla::dom::ForcedColorsOverride> {};

template <>
struct ParamTraits<mozilla::dom::PrefersReducedMotionOverride>
    : public mozilla::dom::WebIDLEnumSerializer<
          mozilla::dom::PrefersReducedMotionOverride> {};

template <>
struct ParamTraits<mozilla::dom::ExplicitActiveStatus>
    : public ContiguousEnumSerializer<
          mozilla::dom::ExplicitActiveStatus,
          mozilla::dom::ExplicitActiveStatus::None,
          mozilla::dom::ExplicitActiveStatus::EndGuard_> {};

// Allow serialization and deserialization of TouchEventsOverride over IPC
template <>
struct ParamTraits<mozilla::dom::TouchEventsOverride>
    : public mozilla::dom::WebIDLEnumSerializer<
          mozilla::dom::TouchEventsOverride> {};

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::dom::EmbedderColorSchemes, mUsed,
                                  mPreferred);

}  // namespace IPC

namespace mozilla {
namespace dom {

// Explicit specialization of the `Transaction` type. Required by the `extern
// template class` declaration in the header.
template class syncedcontext::Transaction<BrowsingContext>;

extern mozilla::LazyLogModule gUserInteractionPRLog;

#define USER_ACTIVATION_LOG(msg, ...) \
  MOZ_LOG(gUserInteractionPRLog, LogLevel::Debug, (msg, ##__VA_ARGS__))

static LazyLogModule gBrowsingContextLog("BrowsingContext");
static LazyLogModule gBrowsingContextSyncLog("BrowsingContextSync");

typedef nsTHashMap<nsUint64HashKey, BrowsingContext*> BrowsingContextMap;

// All BrowsingContexts indexed by Id
static StaticAutoPtr<BrowsingContextMap> sBrowsingContexts;
// Top-level Content BrowsingContexts only, indexed by BrowserId instead of Id
static StaticAutoPtr<BrowsingContextMap> sCurrentTopByBrowserId;

static bool gIPCEnabledAnnotation = false;
static bool gFissionEnabledAnnotation = false;

static void UnregisterBrowserId(BrowsingContext* aBrowsingContext) {
  if (!aBrowsingContext->IsTopContent() || !sCurrentTopByBrowserId) {
    return;
  }

  // Avoids an extra lookup
  auto browserIdEntry =
      sCurrentTopByBrowserId->Lookup(aBrowsingContext->BrowserId());
  if (browserIdEntry && browserIdEntry.Data() == aBrowsingContext) {
    browserIdEntry.Remove();
  }
}

static void Register(BrowsingContext* aBrowsingContext) {
  sBrowsingContexts->InsertOrUpdate(aBrowsingContext->Id(), aBrowsingContext);
  if (aBrowsingContext->IsTopContent()) {
    sCurrentTopByBrowserId->InsertOrUpdate(aBrowsingContext->BrowserId(),
                                           aBrowsingContext);
  }

  aBrowsingContext->Group()->Register(aBrowsingContext);
}

// static
void BrowsingContext::UpdateCurrentTopByBrowserId(
    BrowsingContext* aNewBrowsingContext) {
  if (aNewBrowsingContext->IsTopContent()) {
    sCurrentTopByBrowserId->InsertOrUpdate(aNewBrowsingContext->BrowserId(),
                                           aNewBrowsingContext);
  }
}

BrowsingContext* BrowsingContext::GetParent() const {
  return mParentWindow ? mParentWindow->GetBrowsingContext() : nullptr;
}

bool BrowsingContext::IsInSubtreeOf(BrowsingContext* aContext) {
  BrowsingContext* bc = this;
  do {
    if (bc == aContext) {
      return true;
    }
  } while ((bc = bc->GetParent()));
  return false;
}

BrowsingContext* BrowsingContext::Top() {
  BrowsingContext* bc = this;
  while (bc->mParentWindow) {
    bc = bc->GetParent();
  }
  return bc;
}

const BrowsingContext* BrowsingContext::Top() const {
  const BrowsingContext* bc = this;
  while (bc->mParentWindow) {
    bc = bc->GetParent();
  }
  return bc;
}

int32_t BrowsingContext::IndexOf(BrowsingContext* aChild) {
  int32_t index = -1;
  for (BrowsingContext* child : Children()) {
    ++index;
    if (child == aChild) {
      break;
    }
  }
  return index;
}

WindowContext* BrowsingContext::GetTopWindowContext() const {
  if (mParentWindow) {
    return mParentWindow->TopWindowContext();
  }
  return mCurrentWindowContext;
}

/* static */
void BrowsingContext::Init() {
  if (!sBrowsingContexts) {
    sBrowsingContexts = new BrowsingContextMap();
    sCurrentTopByBrowserId = new BrowsingContextMap();
    ClearOnShutdown(&sBrowsingContexts);
    ClearOnShutdown(&sCurrentTopByBrowserId);
    CrashReporter::RegisterAnnotationBool(
        CrashReporter::Annotation::DOMIPCEnabled, &gIPCEnabledAnnotation);
    CrashReporter::RegisterAnnotationBool(
        CrashReporter::Annotation::DOMFissionEnabled,
        &gFissionEnabledAnnotation);
  }
}

/* static */
LogModule* BrowsingContext::GetLog() { return gBrowsingContextLog; }

/* static */
LogModule* BrowsingContext::GetSyncLog() { return gBrowsingContextSyncLog; }

/* static */
already_AddRefed<BrowsingContext> BrowsingContext::Get(uint64_t aId) {
  if (sBrowsingContexts) {
    return do_AddRef(sBrowsingContexts->Get(aId));
  }

  return nullptr;
}

/* static */
already_AddRefed<BrowsingContext> BrowsingContext::GetCurrentTopByBrowserId(
    uint64_t aBrowserId) {
  // The map is cleared on shutdown but callers may still run afterwards (e.g.
  // during cycle collector teardown), so mirror Get() and null-check it.
  if (sCurrentTopByBrowserId) {
    return do_AddRef(sCurrentTopByBrowserId->Get(aBrowserId));
  }

  return nullptr;
}

/* static */
already_AddRefed<BrowsingContext> BrowsingContext::GetFromWindow(
    WindowProxyHolder& aProxy) {
  return do_AddRef(aProxy.get());
}

CanonicalBrowsingContext* BrowsingContext::Canonical() {
  return CanonicalBrowsingContext::Cast(this);
}

bool BrowsingContext::IsOwnedByProcess() const {
  return mIsInProcess && mDocShell &&
         !nsDocShell::Cast(mDocShell)->WillChangeProcess();
}

bool BrowsingContext::SameOriginWithTop() {
  MOZ_ASSERT(IsInProcess());
  // If the top BrowsingContext is not same-process to us, it is cross-origin
  if (!Top()->IsInProcess()) {
    return false;
  }

  nsIDocShell* docShell = GetDocShell();
  if (!docShell) {
    return false;
  }
  Document* doc = docShell->GetDocument();
  if (!doc) {
    return false;
  }
  nsIPrincipal* principal = doc->NodePrincipal();

  nsIDocShell* topDocShell = Top()->GetDocShell();
  if (!topDocShell) {
    return false;
  }
  Document* topDoc = topDocShell->GetDocument();
  if (!topDoc) {
    return false;
  }
  nsIPrincipal* topPrincipal = topDoc->NodePrincipal();

  return principal->Equals(topPrincipal);
}

/* static */
already_AddRefed<BrowsingContext> BrowsingContext::CreateDetached(
    nsGlobalWindowInner* aParent, BrowsingContext* aOpener,
    BrowsingContextGroup* aSpecificGroup, const nsAString& aName, Type aType,
    CreateDetachedOptions aOptions) {
  if (aParent) {
    MOZ_DIAGNOSTIC_ASSERT(aParent->GetWindowContext());
    MOZ_DIAGNOSTIC_ASSERT(aParent->GetBrowsingContext()->mType == aType);
    MOZ_DIAGNOSTIC_ASSERT(aParent->GetBrowsingContext()->GetBrowserId() != 0);
  }

  MOZ_DIAGNOSTIC_ASSERT(aType != Type::Chrome || XRE_IsParentProcess());

  uint64_t id = nsContentUtils::GenerateBrowsingContextId();

  MOZ_LOG(GetLog(), LogLevel::Debug,
          ("Creating 0x%08" PRIx64 " in %s", id,
           XRE_IsParentProcess() ? "Parent" : "Child"));

  RefPtr<BrowsingContext> parentBC =
      aParent ? aParent->GetBrowsingContext() : nullptr;
  RefPtr<WindowContext> parentWC =
      aParent ? aParent->GetWindowContext() : nullptr;
  BrowsingContext* inherit = parentBC ? parentBC.get() : aOpener;

  // Determine which BrowsingContextGroup this context should be created in.
  RefPtr<BrowsingContextGroup> group = aSpecificGroup;
  if (aType == Type::Chrome) {
    MOZ_DIAGNOSTIC_ASSERT(!group);
    group = BrowsingContextGroup::GetChromeGroup();
  } else if (!group) {
    group = BrowsingContextGroup::Select(parentWC, aOpener);
  }

  // Configure initial values for synced fields.
  FieldValues fields;
  fields.Get<IDX_Name>() = aName;

  if (aOpener) {
    MOZ_DIAGNOSTIC_ASSERT(!aParent,
                          "new BC with both initial opener and parent");
    MOZ_DIAGNOSTIC_ASSERT(aOpener->Group() == group);
    MOZ_DIAGNOSTIC_ASSERT(aOpener->mType == aType);
    fields.Get<IDX_OpenerId>() = aOpener->Id();
    fields.Get<IDX_HadOriginalOpener>() = true;
    fields.Get<IDX_MessageManagerGroup>() =
        aOpener->Top()->GetMessageManagerGroup();

    if (aType == Type::Chrome && !aParent) {
      // See SetOpener for why we do this inheritance.
      fields.Get<IDX_PrefersColorSchemeOverride>() =
          aOpener->Top()->GetPrefersColorSchemeOverride();
    }
  }

  if (aParent) {
    MOZ_DIAGNOSTIC_ASSERT(parentBC->Group() == group);
    MOZ_DIAGNOSTIC_ASSERT(parentBC->mType == aType);
    fields.Get<IDX_EmbedderInnerWindowId>() = aParent->WindowID();
    // Non-toplevel content documents are always embededed within content.
    fields.Get<IDX_EmbeddedInContentDocument>() =
        parentBC->mType == Type::Content;

    // XXX(farre): Can/Should we check aParent->IsLoading() here? (Bug
    // 1608448) Check if the parent was itself loading already
    auto readystate = aParent->GetDocument()->GetReadyStateEnum();
    fields.Get<IDX_AncestorLoading>() =
        parentBC->GetAncestorLoading() ||
        readystate == Document::ReadyState::READYSTATE_LOADING ||
        readystate == Document::ReadyState::READYSTATE_INTERACTIVE;
  }

  fields.Get<IDX_BrowserId>() =
      parentBC ? parentBC->GetBrowserId() : nsContentUtils::GenerateBrowserId();

  fields.Get<IDX_OpenerPolicy>() = nsILoadInfo::OPENER_POLICY_UNSAFE_NONE;
  if (aOpener && aOpener->SameOriginWithTop()) {
    // We inherit the opener policy if there is a creator and if the creator's
    // origin is same origin with the creator's top-level origin.
    // If it is cross origin we should not inherit the CrossOriginOpenerPolicy
    fields.Get<IDX_OpenerPolicy>() = aOpener->Top()->GetOpenerPolicy();

    // If we inherit a policy which is potentially cross-origin isolated, we
    // must be in a potentially cross-origin isolated BCG.
    bool isPotentiallyCrossOriginIsolated =
        fields.Get<IDX_OpenerPolicy>() ==
        nsILoadInfo::OPENER_POLICY_SAME_ORIGIN_EMBEDDER_POLICY_REQUIRE_CORP;
    MOZ_RELEASE_ASSERT(isPotentiallyCrossOriginIsolated ==
                       group->IsPotentiallyCrossOriginIsolated());
  } else if (aOpener) {
    // They are not same origin
    auto topPolicy = aOpener->Top()->GetOpenerPolicy();
    MOZ_RELEASE_ASSERT(
        topPolicy == nsILoadInfo::OPENER_POLICY_UNSAFE_NONE ||
        topPolicy == nsILoadInfo::OPENER_POLICY_SAME_ORIGIN_ALLOW_POPUPS ||
        aOptions.isForPrinting);
    if (aOptions.isForPrinting) {
      // Ensure our opener policy is consistent for printing for our top.
      fields.Get<IDX_OpenerPolicy>() = topPolicy;
    }
  } else if (!aParent && group->IsPotentiallyCrossOriginIsolated()) {
    // If we're creating a brand-new toplevel BC in a potentially cross-origin
    // isolated group, it should start out with a strict opener policy.
    fields.Get<IDX_OpenerPolicy>() =
        nsILoadInfo::OPENER_POLICY_SAME_ORIGIN_EMBEDDER_POLICY_REQUIRE_CORP;
  }

  fields.Get<IDX_HistoryID>() = nsID::GenerateUUID();
  fields.Get<IDX_ExplicitActive>() = [&] {
    if (parentBC || aType == Type::Content) {
      // Non-root browsing-contexts inherit their status from its parent.
      // Top-content either gets managed by the top chrome, or gets manually
      // managed by the front-end (see ManuallyManagesActiveness). In any case
      // we want to start off as inactive.
      return ExplicitActiveStatus::None;
    }
    // Chrome starts as active.
    return ExplicitActiveStatus::Active;
  }();

  fields.Get<IDX_FullZoom>() = parentBC ? parentBC->FullZoom() : 1.0f;
  fields.Get<IDX_TextZoom>() = parentBC ? parentBC->TextZoom() : 1.0f;

  bool allowContentRetargeting =
      inherit ? inherit->GetAllowContentRetargetingOnChildren() : true;
  fields.Get<IDX_AllowContentRetargeting>() = allowContentRetargeting;
  fields.Get<IDX_AllowContentRetargetingOnChildren>() = allowContentRetargeting;

  // Assume top allows fullscreen for its children unless otherwise stated.
  // Subframes start with it false unless otherwise noted in SetEmbedderElement.
  fields.Get<IDX_FullscreenAllowedByOwner>() = !aParent;

  fields.Get<IDX_DefaultLoadFlags>() =
      inherit ? inherit->GetDefaultLoadFlags() : nsIRequest::LOAD_NORMAL;

  fields.Get<IDX_OrientationLock>() = mozilla::hal::ScreenOrientation::None;

  fields.Get<IDX_UseGlobalHistory>() =
      inherit ? inherit->GetUseGlobalHistory() : false;

  fields.Get<IDX_UseErrorPages>() = true;

  fields.Get<IDX_TouchEventsOverrideInternal>() = TouchEventsOverride::None;

  fields.Get<IDX_AllowJavascript>() =
      inherit ? inherit->GetAllowJavascript() : true;

  fields.Get<IDX_IPAddressSpace>() = inherit
                                         ? inherit->GetIPAddressSpace()
                                         : nsILoadInfo::IPAddressSpace::Unknown;

  bool parentalControlsEnabled;
  if (inherit) {
    parentalControlsEnabled = inherit->GetParentalControlsEnabled();
  } else if (XRE_IsParentProcess()) {
    parentalControlsEnabled =
        CanonicalBrowsingContext::ShouldEnforceParentalControls();
  } else {
    parentalControlsEnabled = false;
  }

  fields.Get<IDX_ParentalControlsEnabled>() = parentalControlsEnabled;

  fields.Get<IDX_IsPopupRequested>() = aOptions.isPopupRequested;

  fields.Get<IDX_TopLevelCreatedByWebContent>() =
      aOptions.topLevelCreatedByWebContent;

  if (aOptions.isForPrinting && !parentBC) {
    fields.Get<IDX_IsPrinting>() = true;
  }

  if (!parentBC) {
    fields.Get<IDX_ShouldDelayMediaFromStart>() =
        StaticPrefs::media_block_autoplay_until_in_foreground();
  }

  fields.Get<IDX_AnimationsPlayBackRateMultiplier>() = 1.0;

  RefPtr<BrowsingContext> context;
  if (XRE_IsParentProcess()) {
    context = new CanonicalBrowsingContext(parentWC, group, id,
                                           /* aOwnerProcessId */ 0,
                                           /* aEmbedderProcessId */ 0, aType,
                                           std::move(fields));
  } else {
    context =
        new BrowsingContext(parentWC, group, id, aType, std::move(fields));
  }

  context->mWindowless = aOptions.windowless;
  context->mEmbeddedByThisProcess = XRE_IsParentProcess() || aParent;
  context->mCreatedDynamically = aOptions.createdDynamically;
  if (inherit) {
    context->mPrivateBrowsingId = inherit->mPrivateBrowsingId;
    context->mUseRemoteTabs = inherit->mUseRemoteTabs;
    context->mUseRemoteSubframes = inherit->mUseRemoteSubframes;
    context->mOriginAttributes = inherit->mOriginAttributes;
  }

  nsCOMPtr<nsIRequestContextService> rcsvc =
      net::RequestContextService::GetOrCreate();
  if (rcsvc) {
    nsCOMPtr<nsIRequestContext> requestContext;
    nsresult rv = rcsvc->NewRequestContext(getter_AddRefs(requestContext));
    if (NS_SUCCEEDED(rv) && requestContext) {
      context->mRequestContextId = requestContext->GetID();
    }
  }

  return context.forget();
}

already_AddRefed<BrowsingContext> BrowsingContext::CreateIndependent(
    Type aType, bool aWindowless) {
  MOZ_DIAGNOSTIC_ASSERT(XRE_IsParentProcess(),
                        "BCs created in the content process must be related to "
                        "some BrowserChild");
  RefPtr<BrowsingContext> bc(
      CreateDetached(nullptr, nullptr, nullptr, u""_ns, aType, {}));
  bc->mWindowless = aWindowless;
  bc->mEmbeddedByThisProcess = true;
  bc->EnsureAttached();
  return bc.forget();
}

void BrowsingContext::EnsureAttached() {
  if (!mEverAttached) {
    Register(this);

    // Attach the browsing context to the tree.
    Attach(/* aFromIPC */ false, /* aOriginProcess */ nullptr);
  }
}

/* static */
mozilla::ipc::IPCResult BrowsingContext::CreateFromIPC(
    BrowsingContext::IPCInitializer&& aInit, BrowsingContextGroup* aGroup,
    ContentParent* aOriginProcess) {
  MOZ_DIAGNOSTIC_ASSERT(aOriginProcess || XRE_IsContentProcess());
  MOZ_DIAGNOSTIC_ASSERT(aGroup);

  uint64_t originId = 0;
  if (aOriginProcess) {
    originId = aOriginProcess->ChildID();
    aGroup->EnsureHostProcess(aOriginProcess);
  }

  MOZ_LOG(GetLog(), LogLevel::Debug,
          ("Creating 0x%08" PRIx64 " from IPC (origin=0x%08" PRIx64 ")",
           aInit.mId, originId));

  RefPtr<WindowContext> parent = aInit.GetParent();

  RefPtr<BrowsingContext> context;
  if (XRE_IsParentProcess()) {
    // If the new BrowsingContext has a parent, it is a sub-frame embedded in
    // whatever process sent the message. If it doesn't, and is not windowless,
    // it is a new window or tab, and will be embedded in the parent process.
    uint64_t embedderProcessId = (aInit.mWindowless || parent) ? originId : 0;
    context = new CanonicalBrowsingContext(parent, aGroup, aInit.mId, originId,
                                           embedderProcessId, Type::Content,
                                           std::move(aInit.mFields));
  } else {
    context = new BrowsingContext(parent, aGroup, aInit.mId, Type::Content,
                                  std::move(aInit.mFields));
  }

  context->mWindowless = aInit.mWindowless;
  context->mCreatedDynamically = aInit.mCreatedDynamically;
  context->mChildOffset = aInit.mChildOffset;
  if (context->GetHasSessionHistory()) {
    context->CreateChildSHistory();
    context->GetChildSessionHistory()->SetIndexAndLength(
        aInit.mSessionHistoryIndex, aInit.mSessionHistoryCount, nsID());
  }

  // NOTE: Call through the `Set` methods for these values to ensure that any
  // relevant process-local state is also updated.
  context->SetOriginAttributes(aInit.mOriginAttributes);
  context->SetRemoteTabs(aInit.mUseRemoteTabs);
  context->SetRemoteSubframes(aInit.mUseRemoteSubframes);
  context->mRequestContextId = aInit.mRequestContextId;
  // NOTE: Private browsing ID is set by `SetOriginAttributes`.

  if (const char* failure =
          context->BrowsingContextCoherencyChecks(aOriginProcess)) {
    mozilla::ipc::IProtocol* actor = aOriginProcess;
    if (!actor) {
      actor = ContentChild::GetSingleton();
    }
    return IPC_FAIL_UNSAFE_PRINTF(actor, "Incoherent BrowsingContext: %s",
                                  failure);
  }

  Register(context);

  context->Attach(/* aFromIPC */ true, aOriginProcess);
  return IPC_OK();
}

BrowsingContext::BrowsingContext(WindowContext* aParentWindow,
                                 BrowsingContextGroup* aGroup,
                                 uint64_t aBrowsingContextId, Type aType,
                                 FieldValues&& aInit)
    : mFields(std::move(aInit)),
      mType(aType),
      mBrowsingContextId(aBrowsingContextId),
      mGroup(aGroup),
      mParentWindow(aParentWindow),
      mPrivateBrowsingId(0),
      mEverAttached(false),
      mIsInProcess(false),
      mIsDiscarded(false),
      mWindowless(false),
      mDanglingRemoteOuterProxies(false),
      mEmbeddedByThisProcess(false),
      mUseRemoteTabs(false),
      mUseRemoteSubframes(false),
      mCreatedDynamically(false),
      mIsInBFCache(false),
      mCanExecuteScripts(true),
      mChildOffset(0) {
  MOZ_RELEASE_ASSERT(!mParentWindow || mParentWindow->Group() == mGroup);
  MOZ_RELEASE_ASSERT(mBrowsingContextId != 0);
  MOZ_RELEASE_ASSERT(mGroup);
}

void BrowsingContext::SetDocShell(nsIDocShell* aDocShell) {
  // XXX(nika): We should communicate that we are now an active BrowsingContext
  // process to the parent & do other validation here.
  MOZ_DIAGNOSTIC_ASSERT(mEverAttached);
  MOZ_RELEASE_ASSERT(aDocShell->GetBrowsingContext() == this);
  mDocShell = aDocShell;
  mDanglingRemoteOuterProxies = !mIsInProcess;
  mIsInProcess = true;

  RecomputeCanExecuteScripts();
  ClearCachedValuesOfLocations();
}

// This class implements a callback that will return the remote window proxy for
// mBrowsingContext in that compartment, if it has one. It also removes the
// proxy from the map, because the object will be transplanted into another kind
// of object.
class MOZ_STACK_CLASS CompartmentRemoteProxyTransplantCallback
    : public js::CompartmentTransplantCallback {
 public:
  explicit CompartmentRemoteProxyTransplantCallback(
      BrowsingContext* aBrowsingContext)
      : mBrowsingContext(aBrowsingContext) {}

  virtual JSObject* getObjectToTransplant(
      JS::Compartment* compartment) override {
    auto* priv = xpc::CompartmentPrivate::Get(compartment);
    if (!priv) {
      return nullptr;
    }

    auto& map = priv->GetRemoteProxyMap();
    auto result = map.lookup(mBrowsingContext);
    if (!result) {
      return nullptr;
    }
    JSObject* resultObject = result->value();
    map.remove(result);

    return resultObject;
  }

 private:
  BrowsingContext* mBrowsingContext;
};

void BrowsingContext::CleanUpDanglingRemoteOuterWindowProxies(
    JSContext* aCx, JS::MutableHandle<JSObject*> aOuter) {
  if (!mDanglingRemoteOuterProxies) {
    return;
  }
  mDanglingRemoteOuterProxies = false;

  CompartmentRemoteProxyTransplantCallback cb(this);
  js::RemapRemoteWindowProxies(aCx, &cb, aOuter);
}

bool BrowsingContext::IsActive() const {
  const BrowsingContext* current = this;
  do {
    if (current->GetControlsDocumentPiP()) {
      // XXX Make Media PiP use a similar mechanism too (bug 2016174).
      MOZ_ASSERT(current->IsTop(), "Only top can open document PiP windows");
      return true;
    }
    auto explicit_ = current->GetExplicitActive();
    if (explicit_ != ExplicitActiveStatus::None) {
      return explicit_ == ExplicitActiveStatus::Active;
    }
    if (mParentWindow && !mParentWindow->IsCurrent()) {
      return false;
    }
  } while ((current = current->GetParent()));

  return false;
}

bool BrowsingContext::GetIsActiveBrowserWindow() {
  if (!XRE_IsParentProcess()) {
    return Top()->GetIsActiveBrowserWindowInternal();
  }

  // chrome:// urls loaded in the parent won't receive
  // their own activation so we defer to the top chrome
  // Browsing Context when in the parent process.
  return Canonical()
      ->TopCrossChromeBoundary()
      ->GetIsActiveBrowserWindowInternal();
}

void BrowsingContext::SetIsActiveBrowserWindow(bool aActive) {
  (void)SetIsActiveBrowserWindowInternal(aActive);
}

bool BrowsingContext::FullscreenAllowed() const {
  for (auto* current = this; current; current = current->GetParent()) {
    if (!current->GetFullscreenAllowedByOwner()) {
      return false;
    }
  }
  return true;
}

static bool OwnerAllowsFullscreen(const Element& aEmbedder) {
  if (aEmbedder.IsXULElement()) {
    return !aEmbedder.HasAttr(nsGkAtoms::disablefullscreen);
  }
  if (aEmbedder.IsHTMLElement(nsGkAtoms::iframe)) {
    // This is controlled by feature policy.
    return true;
  }
  if (const auto* embed = HTMLEmbedElement::FromNode(aEmbedder)) {
    return embed->AllowFullscreen();
  }
  return false;
}

void BrowsingContext::SetEmbedderElement(Element* aEmbedder) {
  mEmbeddedByThisProcess = true;

  if (RefPtr<WindowContext> parent = GetParentWindowContext()) {
    parent->ClearLightDOMChildren();
  }

  // Update embedder-element-specific fields in a shared transaction.
  // Don't do this when clearing our embedder, as we're being destroyed either
  // way.
  if (aEmbedder) {
    Transaction txn;
    txn.SetEmbedderElementType(Some(aEmbedder->LocalName()));
    txn.SetEmbeddedInContentDocument(
        aEmbedder->OwnerDoc()->IsContentDocument());
    if (nsCOMPtr<nsPIDOMWindowInner> inner =
            do_QueryInterface(aEmbedder->GetDocumentGlobal())) {
      txn.SetEmbedderInnerWindowId(inner->WindowID());
    }
    txn.SetFullscreenAllowedByOwner(OwnerAllowsFullscreen(*aEmbedder));
    if (XRE_IsParentProcess() && aEmbedder->IsXULElement() && IsTopContent()) {
      nsAutoString messageManagerGroup;
      aEmbedder->GetAttr(nsGkAtoms::messagemanagergroup, messageManagerGroup);
      txn.SetMessageManagerGroup(messageManagerGroup);
      txn.SetUseGlobalHistory(
          !aEmbedder->HasAttr(nsGkAtoms::disableglobalhistory));
      if (!aEmbedder->HasAttr(nsGkAtoms::manualactiveness)) {
        // We're active iff the parent cross-chrome-boundary is active. Note we
        // can't just use this->Canonical()->GetParentCrossChromeBoundary here,
        // since mEmbedderElement is still null at this point.
        RefPtr bc = aEmbedder->OwnerDoc()->GetBrowsingContext();
        const bool isActive = bc && bc->IsActive();
        txn.SetExplicitActive(isActive ? ExplicitActiveStatus::Active
                                       : ExplicitActiveStatus::Inactive);
        if (auto* bp = Canonical()->GetBrowserParent()) {
          bp->SetRenderLayers(isActive);
        }
      }
    }

    MOZ_ALWAYS_SUCCEEDS(txn.Commit(this));
  }

  if (XRE_IsParentProcess() && IsTopContent()) {
    Canonical()->MaybeSetPermanentKey(aEmbedder);
  }

  mEmbedderElement = aEmbedder;

  if (mEmbedderElement) {
    if (nsCOMPtr<nsIObserverService> obs = services::GetObserverService()) {
      obs->NotifyWhenScriptSafe(ToSupports(this),
                                "browsing-context-did-set-embedder", nullptr);
    }

    if (IsEmbedderTypeObjectOrEmbed()) {
      (void)SetIsSyntheticDocumentContainer(true);
    }
  }
}

bool BrowsingContext::IsEmbedderTypeObjectOrEmbed() {
  if (const Maybe<nsString>& type = GetEmbedderElementType()) {
    return nsGkAtoms::object->Equals(*type) || nsGkAtoms::embed->Equals(*type);
  }
  return false;
}

void BrowsingContext::Embed() {
  if (auto* frame = HTMLIFrameElement::FromNode(mEmbedderElement)) {
    frame->BindToBrowsingContext(this);
  }
}

nsGlobalWindowInner* BrowsingContext::GetOpenedDocumentPiPWindow() const {
  nsPIDOMWindowOuter* outer = GetDOMWindow();
  if (!outer) {
    return nullptr;
  }

  nsPIDOMWindowInner* inner = outer->GetCurrentInnerWindow();
  if (!inner) {
    return nullptr;
  }

  DocumentPictureInPicture* dpip = inner->GetExtantDocumentPictureInPicture();
  if (!dpip) {
    return nullptr;
  }
  return dpip->GetWindow();
}

const char* BrowsingContext::BrowsingContextCoherencyChecks(
    ContentParent* aOriginProcess) {
#define COHERENCY_ASSERT(condition) \
  if (!(condition)) return "Assertion " #condition " failed";

  if (mGroup->IsPotentiallyCrossOriginIsolated() !=
      (Top()->GetOpenerPolicy() ==
       nsILoadInfo::OPENER_POLICY_SAME_ORIGIN_EMBEDDER_POLICY_REQUIRE_CORP)) {
    return "Invalid CrossOriginIsolated state";
  }

  if (aOriginProcess && !IsContent()) {
    return "Content cannot create chrome BCs";
  }

  // LoadContext should generally match our opener or parent.
  if (IsContent()) {
    if (RefPtr<BrowsingContext> opener = GetOpener()) {
      COHERENCY_ASSERT(opener->mType == mType);
      COHERENCY_ASSERT(opener->mGroup == mGroup);
      COHERENCY_ASSERT(opener->mUseRemoteTabs == mUseRemoteTabs);
      COHERENCY_ASSERT(opener->mUseRemoteSubframes == mUseRemoteSubframes);
      COHERENCY_ASSERT(opener->mPrivateBrowsingId == mPrivateBrowsingId);
      COHERENCY_ASSERT(
          opener->mOriginAttributes.EqualsIgnoringFPD(mOriginAttributes));
    }
  }
  if (RefPtr<BrowsingContext> parent = GetParent()) {
    COHERENCY_ASSERT(parent->mType == mType);
    COHERENCY_ASSERT(parent->mGroup == mGroup);
    COHERENCY_ASSERT(parent->mUseRemoteTabs == mUseRemoteTabs);
    COHERENCY_ASSERT(parent->mUseRemoteSubframes == mUseRemoteSubframes);
    COHERENCY_ASSERT(parent->mPrivateBrowsingId == mPrivateBrowsingId);
    COHERENCY_ASSERT(
        parent->mOriginAttributes.EqualsIgnoringFPD(mOriginAttributes));
  }

  if (aOriginProcess) {
    if (GetBrowserId() == 0) {
      return "Content BC must have a nonzero BrowserId";
    }
    if (!GetParent()) {
      uint64_t browserProc =
          std::get<0>(nsContentUtils::SplitProcessSpecificId(GetBrowserId()));
      COHERENCY_ASSERT(browserProc == aOriginProcess->ChildID());
    }
  }

  // UseRemoteSubframes and UseRemoteTabs must match.
  if (mUseRemoteSubframes && !mUseRemoteTabs) {
    return "Cannot set useRemoteSubframes without also setting useRemoteTabs";
  }

  // Double-check OriginAttributes/Private Browsing
  // Chrome browsing contexts must not have a private browsing OriginAttribute
  // Content browsing contexts must maintain the equality:
  // mOriginAttributes.mPrivateBrowsingId == mPrivateBrowsingId
  if (IsChrome()) {
    COHERENCY_ASSERT(mOriginAttributes.mPrivateBrowsingId == 0);
  } else {
    COHERENCY_ASSERT(mOriginAttributes.mPrivateBrowsingId ==
                     mPrivateBrowsingId);
  }
#undef COHERENCY_ASSERT

  return nullptr;
}

void BrowsingContext::Attach(bool aFromIPC, ContentParent* aOriginProcess) {
  MOZ_DIAGNOSTIC_ASSERT(!mEverAttached);
  MOZ_DIAGNOSTIC_ASSERT_IF(aFromIPC, aOriginProcess || XRE_IsContentProcess());
  mEverAttached = true;

  if (MOZ_LOG_TEST(GetLog(), LogLevel::Debug)) {
    nsAutoCString suffix;
    mOriginAttributes.CreateSuffix(suffix);
    MOZ_LOG(GetLog(), LogLevel::Debug,
            ("%s: Connecting 0x%08" PRIx64 " to 0x%08" PRIx64
             " (private=%d, remote=%d, fission=%d, oa=%s)",
             XRE_IsParentProcess() ? "Parent" : "Child", Id(),
             GetParent() ? GetParent()->Id() : 0, (int)mPrivateBrowsingId,
             (int)mUseRemoteTabs, (int)mUseRemoteSubframes, suffix.get()));
  }

  MOZ_DIAGNOSTIC_ASSERT(mGroup);
  MOZ_DIAGNOSTIC_ASSERT(!mIsDiscarded);

#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  // We'll already have checked this if `aFromIPC` is set before calling this
  // function.
  if (!aFromIPC) {
    if (const char* failure = BrowsingContextCoherencyChecks(aOriginProcess)) {
      MOZ_CRASH_UNSAFE_PRINTF("Incoherent BrowsingContext: %s", failure);
    }
  }
#endif

  // Add ourselves either to our parent or BrowsingContextGroup's child list.
  // Important: We shouldn't return IPC_FAIL after this point, since the
  // BrowsingContext will have already been added to the tree.
  if (mParentWindow) {
    if (!aFromIPC) {
      MOZ_DIAGNOSTIC_ASSERT(!mParentWindow->IsDiscarded(),
                            "local attach in discarded window");
      MOZ_DIAGNOSTIC_ASSERT(!GetParent()->IsDiscarded(),
                            "local attach call in discarded bc");
      MOZ_DIAGNOSTIC_ASSERT(mParentWindow->GetWindowGlobalChild(),
                            "local attach call with oop parent window");
      MOZ_DIAGNOSTIC_ASSERT(mParentWindow->GetWindowGlobalChild()->CanSend(),
                            "local attach call with dead parent window");
    }
    mChildOffset =
        mCreatedDynamically ? -1 : mParentWindow->Children().Length();
    mParentWindow->AppendChildBrowsingContext(this);
    RecomputeCanExecuteScripts();
  } else {
    mGroup->Toplevels().AppendElement(this);
  }

  if (GetIsPopupSpam()) {
    PopupBlocker::RegisterOpenPopupSpam();
  }

  if (IsTop() && GetHasSessionHistory() && !mChildSessionHistory) {
    CreateChildSHistory();
  }

  // Why the context is being attached. This will always be "attach" in the
  // content process, but may be "replace" if it's known the context being
  // replaced in the parent process.
  const char16_t* why = u"attach";

  if (XRE_IsContentProcess() && !aFromIPC) {
    // Send attach to our parent if we need to.
    ContentChild::GetSingleton()->SendCreateBrowsingContext(
        mGroup->Id(), GetIPCInitializer());
  } else if (XRE_IsParentProcess()) {
    // If this window was created as a subframe by a content process, it must be
    // being hosted within the same BrowserParent as its mParentWindow.
    // Toplevel BrowsingContexts created by content have their BrowserParent
    // configured during `RecvConstructPopupBrowser`.
    if (mParentWindow && aOriginProcess) {
      MOZ_DIAGNOSTIC_ASSERT(
          mParentWindow->Canonical()->GetContentParent() == aOriginProcess,
          "Creator process isn't the same as our embedder?");
      Canonical()->SetCurrentBrowserParent(
          mParentWindow->Canonical()->GetBrowserParent());
    }

    mGroup->EachOtherParent(aOriginProcess, [&](ContentParent* aParent) {
      MOZ_DIAGNOSTIC_ASSERT(IsContent(),
                            "chrome BCG cannot be synced to content process");
      if (!Canonical()->IsEmbeddedInProcess(aParent->ChildID())) {
        (void)aParent->SendCreateBrowsingContext(mGroup->Id(),
                                                 GetIPCInitializer());
      }
    });

    if (IsTop() && IsContent() && Canonical()->GetWebProgress()) {
      why = u"replace";
    }

    // We want to create a BrowsingContextWebProgress for all content
    // BrowsingContexts.
    if (IsContent() && !Canonical()->mWebProgress) {
      Canonical()->mWebProgress =
          MakeRefPtr<BrowsingContextWebProgress>(Canonical());
    }
  }

  if (nsCOMPtr<nsIObserverService> obs = services::GetObserverService()) {
    obs->NotifyWhenScriptSafe(ToSupports(this), "browsing-context-attached",
                              why);
  }

  if (XRE_IsParentProcess()) {
    Canonical()->CanonicalAttach();
  }
}

void BrowsingContext::Detach(bool aFromIPC) {
  MOZ_LOG(GetLog(), LogLevel::Debug,
          ("%s: Detaching 0x%08" PRIx64 " from 0x%08" PRIx64,
           XRE_IsParentProcess() ? "Parent" : "Child", Id(),
           GetParent() ? GetParent()->Id() : 0));

  MOZ_DIAGNOSTIC_ASSERT(mEverAttached);
  MOZ_DIAGNOSTIC_ASSERT(!mIsDiscarded);

  if (XRE_IsParentProcess()) {
    Canonical()->AddPendingDiscard();
  }
  auto callListeners =
      MakeScopeExit([&, listeners = std::move(mDiscardListeners), id = Id()] {
        for (const auto& listener : listeners) {
          listener(id);
        }
        if (XRE_IsParentProcess()) {
          Canonical()->RemovePendingDiscard();
        }
      });

  nsCOMPtr<nsIRequestContextService> rcsvc =
      net::RequestContextService::GetOrCreate();
  if (rcsvc) {
    rcsvc->RemoveRequestContext(GetRequestContextId());
  }

  // This will only ever be null if the cycle-collector has unlinked us. Don't
  // try to detach ourselves in that case.
  if (NS_WARN_IF(!mGroup)) {
    MOZ_ASSERT_UNREACHABLE();
    return;
  }

  if (mParentWindow) {
    mParentWindow->RemoveChildBrowsingContext(this);
  } else {
    mGroup->Toplevels().RemoveElement(this);
  }

  if (XRE_IsParentProcess()) {
    RefPtr<CanonicalBrowsingContext> self{Canonical()};
    Group()->EachParent([&](ContentParent* aParent) {
      // Only the embedder process is allowed to initiate a BrowsingContext
      // detach, so if we've gotten here, the host process already knows we've
      // been detached, and there's no need to tell it again.
      //
      // If the owner process is not the same as the embedder process, its
      // BrowsingContext will be detached when its nsWebBrowser instance is
      // destroyed.
      bool doDiscard = !Canonical()->IsEmbeddedInProcess(aParent->ChildID()) &&
                       !Canonical()->IsOwnedByProcess(aParent->ChildID());

      // Hold a strong reference to ourself, and keep our BrowsingContextGroup
      // alive, until the responses comes back to ensure we don't die while
      // messages relating to this context are in-flight.
      //
      // When the callback is called, the keepalive on our group will be
      // destroyed, and the reference to the BrowsingContext will be dropped,
      // which may cause it to be fully destroyed.
      mGroup->AddKeepAlive();
      self->AddPendingDiscard();
      auto callback = [self](auto) {
        self->mGroup->RemoveKeepAlive();
        self->RemovePendingDiscard();
      };

      aParent->SendDiscardBrowsingContext(this, doDiscard, callback, callback);
    });
  } else {
    // Hold a strong reference to ourself until the responses come back to
    // ensure the BrowsingContext isn't cleaned up before the parent process
    // acknowledges the discard request.
    auto callback = [self = RefPtr{this}](auto) {};
    ContentChild::GetSingleton()->SendDiscardBrowsingContext(
        this, !aFromIPC, callback, callback);
  }

  mGroup->Unregister(this);
  UnregisterBrowserId(this);
  mIsDiscarded = true;

  if (XRE_IsParentProcess()) {
    nsFocusManager* fm = nsFocusManager::GetFocusManager();
    if (fm) {
      fm->BrowsingContextDetached(this);
    }
  }

  if (nsCOMPtr<nsIObserverService> obs = services::GetObserverService()) {
    // Why the context is being discarded. This will always be "discard" in the
    // content process, but may be "replace" if it's known the context being
    // replaced in the parent process.
    const char16_t* why = u"discard";
    if (XRE_IsParentProcess() && Canonical()->IsReplaced()) {
      why = u"replace";
    }
    obs->NotifyObservers(ToSupports(this), "browsing-context-discarded", why);
  }

  // NOTE: Doesn't use SetClosed, as it will be set in all processes
  // automatically by calls to Detach()
  mFields.SetWithoutSyncing<IDX_Closed>(true);

  if (GetIsPopupSpam()) {
    PopupBlocker::UnregisterOpenPopupSpam();
    // NOTE: Doesn't use SetIsPopupSpam, as it will be set all processes
    // automatically.
    mFields.SetWithoutSyncing<IDX_IsPopupSpam>(false);
  }

  AssertOriginAttributesMatchPrivateBrowsing();

  if (XRE_IsParentProcess()) {
    Canonical()->CanonicalDiscard();
  }
}

void BrowsingContext::AddDiscardListener(
    std::function<void(uint64_t)>&& aListener) {
  if (mIsDiscarded) {
    aListener(Id());
    return;
  }
  mDiscardListeners.AppendElement(std::move(aListener));
}

void BrowsingContext::PrepareForProcessChange() {
  MOZ_LOG(GetLog(), LogLevel::Debug,
          ("%s: Preparing 0x%08" PRIx64 " for a process change",
           XRE_IsParentProcess() ? "Parent" : "Child", Id()));

  MOZ_ASSERT(mIsInProcess, "Must currently be an in-process frame");
  MOZ_ASSERT(!mIsDiscarded, "We're already closed?");

  mIsInProcess = false;
  mUserGestureStart = TimeStamp();

  ClearCachedValuesOfLocations();

  // NOTE: For now, clear our nsDocShell reference, as we're primarily in a
  // different process now. This may need to change in the future with
  // Cross-Process BFCache.
  mDocShell = nullptr;

  if (!mWindowProxy) {
    return;
  }

  // We have to go through mWindowProxy rather than calling GetDOMWindow() on
  // mDocShell because the mDocshell reference gets cleared immediately after
  // the window is closed.
  nsGlobalWindowOuter::PrepareForProcessChange(mWindowProxy);
  MOZ_ASSERT(!mWindowProxy);
}

bool BrowsingContext::IsTargetable() const {
  return !GetClosed() && AncestorsAreCurrent();
}

void BrowsingContext::SetOpener(BrowsingContext* aOpener) {
  MOZ_DIAGNOSTIC_ASSERT(!aOpener || aOpener->Group() == Group());
  MOZ_DIAGNOSTIC_ASSERT(!aOpener || aOpener->mType == mType);

  MOZ_ALWAYS_SUCCEEDS(SetOpenerId(aOpener ? aOpener->Id() : 0));

  if (IsChrome() && IsTop() && aOpener) {
    // Inherit color scheme overrides from parent window. This is to inherit the
    // color scheme of dark themed PBM windows in dialogs opened by such
    // windows.
    auto openerOverride = aOpener->Top()->PrefersColorSchemeOverride();
    if (openerOverride != PrefersColorSchemeOverride()) {
      MOZ_ALWAYS_SUCCEEDS(SetPrefersColorSchemeOverride(openerOverride));
    }
  }
}

bool BrowsingContext::HasOpener() const {
  if (sBrowsingContexts) {
    return sBrowsingContexts->Contains(GetOpenerId());
  }

  return false;
}

bool BrowsingContext::AncestorsAreCurrent() const {
  const BrowsingContext* bc = this;
  while (true) {
    if (bc->IsDiscarded()) {
      return false;
    }

    if (WindowContext* wc = bc->GetParentWindowContext()) {
      if (!wc->IsCurrent() || wc->IsDiscarded()) {
        return false;
      }

      bc = wc->GetBrowsingContext();
    } else {
      return true;
    }
  }
}

bool BrowsingContext::IsInBFCache() const { return mIsInBFCache; }

void BrowsingContext::SetIsInBFCache(bool aIsInBFCache) {
  mIsInBFCache = aIsInBFCache;
}

void BrowsingContext::SetIsEnteringBFCache(bool aIsEnteringBFCache) {
  mIsEnteringBFCache = aIsEnteringBFCache;
}

void BrowsingContext::DeactivateDocuments() {
  MOZ_RELEASE_ASSERT(mozilla::BFCacheInParent());
  MOZ_DIAGNOSTIC_ASSERT(IsTop());

  if (XRE_IsContentProcess() && mDocShell) {
    nsDocShell::Cast(mDocShell)->MaybeDisconnectChildListenersOnPageHide();
  }

  PreOrderWalk([&](BrowsingContext* aContext) {
    nsCOMPtr<nsIDocShell> shell = aContext->GetDocShell();
    aContext->SetIsEnteringBFCache(/* aIsEnteringBFCache */ true);

    if (shell) {
      nsDocShell::Cast(shell)->FirePageHideShowNonRecursive(false);
    }
  });

  PreOrderWalk([&](BrowsingContext* aContext) {
    nsCOMPtr<nsIDocShell> shell = aContext->GetDocShell();
    if (shell) {
      nsDocShell::Cast(shell)->ThawFreezeNonRecursive(false);
      if (nsPresContext* pc = shell->GetPresContext()) {
        pc->EventStateManager()->ResetHoverState();
      }
    }
    aContext->SetIsInBFCache(true);
    Document* doc = aContext->GetDocument();
    if (doc) {
      // Notifying needs to happen after mIsInBFCache is set to true.
      doc->NotifyActivityChanged();
    }
  });
}

static void GetSubframeReactivationData(
    BrowsingContext* aBrowsingContext,
    Maybe<SessionHistoryInfo>& aReactivatedEntry,
    nsTArray<SessionHistoryInfo>& aNewSHEs,
    const Maybe<PreviousSessionHistoryInfo>& aPreviousEntryForActivation) {
  // See bug 1991680
  MOZ_LOG_FMT(gNavigationAPILog, LogLevel::Debug,
              "We currently don't know how to reactivate subframes");
}

void BrowsingContext::ReactivateDocuments(
    const Maybe<SessionHistoryInfo>& aReactivatedEntry,
    const nsTArray<SessionHistoryInfo>& aNewSHEs,
    const Maybe<PreviousSessionHistoryInfo>& aPreviousEntryForActivation) {
  UpdateCurrentTopByBrowserId(this);
  PreOrderWalk(
      [&](BrowsingContext* aContext) MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
        aContext->SetIsInBFCache(false);
        aContext->SetIsEnteringBFCache(/* aIsEnteringBFCache */ false);
        nsCOMPtr<nsIDocShell> shell = aContext->GetDocShell();
        // Before doing anything to reactivate, we need to thaw our suspended
        // docshells. If we don't, updating the navigation entries can't
        // succeed, since we don't have an active document.
        if (shell) {
          nsDocShell::Cast(shell)->ThawFreezeNonRecursive(true);
        }

        // We need to restore our navigation object state before calling
        // pageshow. This is ok, since we will fire any
        // NavigationHistoryEntry.dispose events async.
        if (aContext->IsTop()) {
          aContext->UpdateForReactivation(aReactivatedEntry, aNewSHEs,
                                          aPreviousEntryForActivation);
        } else {
          Maybe<SessionHistoryInfo> reactivatedEntry;
          nsTArray<SessionHistoryInfo> newSHEs;
          GetSubframeReactivationData(aContext, reactivatedEntry, newSHEs,
                                      aPreviousEntryForActivation);
          aContext->UpdateForReactivation(reactivatedEntry, newSHEs,
                                          aPreviousEntryForActivation);
        }
      });

  PostOrderWalk([&](BrowsingContext* aContext) {
    nsCOMPtr<nsIDocShell> shell = aContext->GetDocShell();
    if (shell) {
      nsDocShell::Cast(shell)->FirePageHideShowNonRecursive(true);
    }
  });
}

// https://html.spec.whatwg.org/#update-document-for-history-step-application
void BrowsingContext::UpdateForReactivation(
    const Maybe<SessionHistoryInfo>& aReactivatedEntry,
    const nsTArray<SessionHistoryInfo>& aNewSHEs,
    const Maybe<PreviousSessionHistoryInfo>& aPreviousEntryForActivation) {
  if (RefPtr docShell = nsDocShell::Cast(GetDocShell());
      docShell && aReactivatedEntry) {
    if (RefPtr window = docShell->GetActiveWindow()) {
      if (RefPtr navigation = window->Navigation()) {
        // The spec actually performs these to steps in reverse order, but we
        // can't. We need to reactivate to be able to have the navigation
        // history entries available to set the correct navigation activation.
        navigation->UpdateForReactivation(aNewSHEs, aReactivatedEntry.ptr());

        navigation->CreateNavigationActivationFrom(
            aPreviousEntryForActivation, Some(NavigationType::Traverse));
      }
    }
  }
}

Span<RefPtr<BrowsingContext>> BrowsingContext::Children() const {
  if (WindowContext* current = mCurrentWindowContext) {
    return current->Children();
  }
  return Span<RefPtr<BrowsingContext>>();
}

void BrowsingContext::GetChildren(
    nsTArray<RefPtr<BrowsingContext>>& aChildren) {
  aChildren.AppendElements(Children());
}

Span<RefPtr<BrowsingContext>> BrowsingContext::NonSyntheticChildren() const {
  if (WindowContext* current = mCurrentWindowContext) {
    return current->NonSyntheticChildren();
  }
  return Span<RefPtr<BrowsingContext>>();
}

BrowsingContext* BrowsingContext::NonSyntheticLightDOMChildAt(
    uint32_t aIndex) const {
  if (WindowContext* current = mCurrentWindowContext) {
    return current->NonSyntheticLightDOMChildAt(aIndex);
  }
  return nullptr;
}

uint32_t BrowsingContext::NonSyntheticLightDOMChildrenCount() const {
  if (WindowContext* current = mCurrentWindowContext) {
    return current->NonSyntheticLightDOMChildrenCount();
  }
  return 0;
}

void BrowsingContext::GetWindowContexts(
    nsTArray<RefPtr<WindowContext>>& aWindows) {
  aWindows.AppendElements(mWindowContexts);
}

void BrowsingContext::RegisterWindowContext(WindowContext* aWindow) {
  MOZ_ASSERT(!mWindowContexts.Contains(aWindow),
             "WindowContext already registered!");
  MOZ_ASSERT(aWindow->GetBrowsingContext() == this);

  mWindowContexts.AppendElement(aWindow);

  // If the newly registered WindowContext is for our current inner window ID,
  // re-run the `DidSet` handler to re-establish the relationship.
  if (aWindow->InnerWindowId() == GetCurrentInnerWindowId()) {
    DidSet(FieldIndex<IDX_CurrentInnerWindowId>());
    MOZ_DIAGNOSTIC_ASSERT(mCurrentWindowContext == aWindow);
  }
}

void BrowsingContext::UnregisterWindowContext(WindowContext* aWindow) {
  MOZ_ASSERT(mWindowContexts.Contains(aWindow),
             "WindowContext not registered!");
  mWindowContexts.RemoveElement(aWindow);

  // If our currently active window was unregistered, clear our reference to it.
  if (aWindow == mCurrentWindowContext) {
    // Re-read our `CurrentInnerWindowId` value and use it to set
    // `mCurrentWindowContext`. As `aWindow` is now unregistered and discarded,
    // we won't find it, and the value will be cleared back to `nullptr`.
    DidSet(FieldIndex<IDX_CurrentInnerWindowId>());
    MOZ_DIAGNOSTIC_ASSERT(mCurrentWindowContext == nullptr);
  }
}

void BrowsingContext::PreOrderWalkVoid(
    const std::function<void(BrowsingContext*)>& aCallback) {
  aCallback(this);

  AutoTArray<RefPtr<BrowsingContext>, 8> children;
  children.AppendElements(Children());

  for (auto& child : children) {
    child->PreOrderWalkVoid(aCallback);
  }
}

BrowsingContext::WalkFlag BrowsingContext::PreOrderWalkFlag(
    const std::function<WalkFlag(BrowsingContext*)>& aCallback) {
  switch (aCallback(this)) {
    case WalkFlag::Skip:
      return WalkFlag::Next;
    case WalkFlag::Stop:
      return WalkFlag::Stop;
    case WalkFlag::Next:
    default:
      break;
  }

  AutoTArray<RefPtr<BrowsingContext>, 8> children;
  children.AppendElements(Children());

  for (auto& child : children) {
    switch (child->PreOrderWalkFlag(aCallback)) {
      case WalkFlag::Stop:
        return WalkFlag::Stop;
      default:
        break;
    }
  }

  return WalkFlag::Next;
}

void BrowsingContext::PostOrderWalk(
    const std::function<void(BrowsingContext*)>& aCallback) {
  AutoTArray<RefPtr<BrowsingContext>, 8> children;
  children.AppendElements(Children());

  for (auto& child : children) {
    child->PostOrderWalk(aCallback);
  }

  aCallback(this);
}

void BrowsingContext::GetAllBrowsingContextsInSubtree(
    nsTArray<RefPtr<BrowsingContext>>& aBrowsingContexts) {
  PreOrderWalk([&](BrowsingContext* aContext) {
    aBrowsingContexts.AppendElement(aContext);
  });
}

BrowsingContext* BrowsingContext::FindChildWithName(
    const nsAString& aName, WindowGlobalChild& aRequestingWindow) {
  if (aName.IsEmpty()) {
    // You can't find a browsing context with the empty name.
    return nullptr;
  }

  for (BrowsingContext* child : NonSyntheticChildren()) {
    if (child->NameEquals(aName) && aRequestingWindow.CanNavigate(child) &&
        child->IsTargetable()) {
      return child;
    }
  }

  return nullptr;
}

BrowsingContext* BrowsingContext::FindWithSpecialName(
    const nsAString& aName, WindowGlobalChild& aRequestingWindow) {
  // TODO(farre): Neither BrowsingContext nor nsDocShell checks if the
  // browsing context pointed to by a special name is active. Should
  // it be? See Bug 1527913.
  if (aName.LowerCaseEqualsLiteral("_self")) {
    return this;
  }

  if (aName.LowerCaseEqualsLiteral("_parent")) {
    if (BrowsingContext* parent = GetParent()) {
      return aRequestingWindow.CanNavigate(parent) ? parent : nullptr;
    }
    return this;
  }

  if (aName.LowerCaseEqualsLiteral("_top")) {
    BrowsingContext* top = Top();

    return aRequestingWindow.CanNavigate(top) ? top : nullptr;
  }

  return nullptr;
}

BrowsingContext* BrowsingContext::FindWithNameInSubtree(
    const nsAString& aName, WindowGlobalChild* aRequestingWindow) {
  MOZ_DIAGNOSTIC_ASSERT(!aName.IsEmpty());

  if (NameEquals(aName) &&
      (!aRequestingWindow || aRequestingWindow->CanNavigate(this)) &&
      IsTargetable()) {
    return this;
  }

  for (BrowsingContext* child : NonSyntheticChildren()) {
    if (BrowsingContext* found =
            child->FindWithNameInSubtree(aName, aRequestingWindow)) {
      return found;
    }
  }

  return nullptr;
}

// https://html.spec.whatwg.org/#allowed-to-navigate
bool BrowsingContext::IsSandboxedFrom(BrowsingContext* aTarget) {
  // If no target then not sandboxed.
  if (!aTarget) {
    return false;
  }

  // We cannot be sandboxed from ourselves.
  if (aTarget == this) {
    return false;
  }

  // Default the sandbox flags to our flags, so that if we can't retrieve the
  // active document, we will still enforce our own.
  uint32_t sandboxFlags = GetSandboxFlags();
  if (mDocShell) {
    if (RefPtr<Document> doc = mDocShell->GetExtantDocument()) {
      sandboxFlags = doc->GetSandboxFlags();
    }
  }

  // If no flags, we are not sandboxed at all.
  if (!sandboxFlags) {
    return false;
  }

  // If aTarget has an ancestor, it is not top level.
  if (RefPtr<BrowsingContext> ancestorOfTarget = aTarget->GetParent()) {
    do {
      // We are not sandboxed if we are an ancestor of target.
      if (ancestorOfTarget == this) {
        return false;
      }
      ancestorOfTarget = ancestorOfTarget->GetParent();
    } while (ancestorOfTarget);

    // Otherwise, we are sandboxed from aTarget.
    return true;
  }

  // aTarget is top level, are we the "one permitted sandboxed
  // navigator", i.e. did we open aTarget?
  if (aTarget->GetOnePermittedSandboxedNavigatorId() == Id()) {
    return false;
  }

  // If SANDBOXED_TOPLEVEL_NAVIGATION flag is not on, we are not sandboxed
  // from our top.
  if (!(sandboxFlags & SANDBOXED_TOPLEVEL_NAVIGATION) && aTarget == Top()) {
    return false;
  }

  // If SANDBOXED_TOPLEVEL_NAVIGATION_USER_ACTIVATION flag is not on, we are not
  // sandboxed from our top if we have user interaction. We assume there is a
  // valid transient user gesture interaction if this check happens in the
  // target process given that we have checked in the triggering process
  // already.
  if (!(sandboxFlags & SANDBOXED_TOPLEVEL_NAVIGATION_USER_ACTIVATION) &&
      mCurrentWindowContext &&
      (!mCurrentWindowContext->IsInProcess() ||
       mCurrentWindowContext->HasValidTransientUserGestureActivation()) &&
      aTarget == Top()) {
    return false;
  }

  // Otherwise, we are sandboxed from aTarget.
  return true;
}

RefPtr<SessionStorageManager> BrowsingContext::GetSessionStorageManager() {
  RefPtr<SessionStorageManager>& manager = Top()->mSessionStorageManager;
  if (!manager) {
    manager = MakeRefPtr<SessionStorageManager>(this);
  }
  return manager;
}

bool BrowsingContext::CrossOriginIsolated() {
  MOZ_ASSERT(NS_IsMainThread());

  return StaticPrefs::
             dom_postMessage_sharedArrayBuffer_withCOOP_COEP_AtStartup() &&
         Top()->GetOpenerPolicy() ==
             nsILoadInfo::
                 OPENER_POLICY_SAME_ORIGIN_EMBEDDER_POLICY_REQUIRE_CORP &&
         XRE_IsContentProcess() &&
         StringBeginsWith(ContentChild::GetSingleton()->GetRemoteType(),
                          WITH_COOP_COEP_REMOTE_TYPE_PREFIX);
}

void BrowsingContext::SetTriggeringAndInheritPrincipals(
    nsIPrincipal* aTriggeringPrincipal, nsIPrincipal* aPrincipalToInherit,
    uint64_t aLoadIdentifier) {
  mTriggeringPrincipal = Some(
      PrincipalWithLoadIdentifierTuple(aTriggeringPrincipal, aLoadIdentifier));
  if (aPrincipalToInherit) {
    mPrincipalToInherit = Some(
        PrincipalWithLoadIdentifierTuple(aPrincipalToInherit, aLoadIdentifier));
  }
}

std::tuple<nsCOMPtr<nsIPrincipal>, nsCOMPtr<nsIPrincipal>>
BrowsingContext::GetTriggeringAndInheritPrincipalsForCurrentLoad() {
  nsCOMPtr<nsIPrincipal> triggeringPrincipal =
      GetSavedPrincipal(mTriggeringPrincipal);
  nsCOMPtr<nsIPrincipal> principalToInherit =
      GetSavedPrincipal(mPrincipalToInherit);
  return std::make_tuple(triggeringPrincipal, principalToInherit);
}

nsIPrincipal* BrowsingContext::GetSavedPrincipal(
    Maybe<PrincipalWithLoadIdentifierTuple> aPrincipalTuple) {
  if (aPrincipalTuple) {
    nsCOMPtr<nsIPrincipal> principal;
    uint64_t loadIdentifier;
    std::tie(principal, loadIdentifier) = *aPrincipalTuple;
    // We want to return a principal only if the load identifier for it
    // matches the current one for this BC.
    if (auto current = GetCurrentLoadIdentifier();
        current && *current == loadIdentifier) {
      return principal;
    }
  }
  return nullptr;
}

BrowsingContext::~BrowsingContext() {
  MOZ_DIAGNOSTIC_ASSERT(!mParentWindow ||
                        !mParentWindow->mChildren.Contains(this));
  MOZ_DIAGNOSTIC_ASSERT(!mGroup || !mGroup->Toplevels().Contains(this));

  mDeprioritizedLoadRunner.clear();

  if (sBrowsingContexts) {
    sBrowsingContexts->Remove(Id());
  }
  UnregisterBrowserId(this);

  ClearCachedValuesOfLocations();
  mLocations.clear();
}

/* static */
void BrowsingContext::DiscardFromContentParent(ContentParent* aCP) {
  MOZ_ASSERT(XRE_IsParentProcess());

  if (sBrowsingContexts) {
    AutoTArray<RefPtr<BrowsingContext>, 8> toDiscard;
    for (const auto& data : sBrowsingContexts->Values()) {
      auto* bc = data->Canonical();
      if (!bc->IsDiscarded() && bc->IsEmbeddedInProcess(aCP->ChildID())) {
        toDiscard.AppendElement(bc);
      }
    }

    for (BrowsingContext* bc : toDiscard) {
      bc->Detach(/* aFromIPC */ true);
    }
  }
}

nsISupports* BrowsingContext::GetParentObject() const {
  return xpc::NativeGlobal(xpc::PrivilegedJunkScope());
}

JSObject* BrowsingContext::WrapObject(JSContext* aCx,
                                      JS::Handle<JSObject*> aGivenProto) {
  return BrowsingContext_Binding::Wrap(aCx, this, aGivenProto);
}

bool BrowsingContext::WriteStructuredClone(JSContext* aCx,
                                           JSStructuredCloneWriter* aWriter,
                                           StructuredCloneHolder* aHolder) {
  MOZ_DIAGNOSTIC_ASSERT(mEverAttached);
  return (JS_WriteUint32Pair(aWriter, SCTAG_DOM_BROWSING_CONTEXT, 0) &&
          JS_WriteUint32Pair(aWriter, uint32_t(Id()), uint32_t(Id() >> 32)));
}

/* static */
JSObject* BrowsingContext::ReadStructuredClone(JSContext* aCx,
                                               JSStructuredCloneReader* aReader,
                                               StructuredCloneHolder* aHolder) {
  uint32_t idLow = 0;
  uint32_t idHigh = 0;
  if (!JS_ReadUint32Pair(aReader, &idLow, &idHigh)) {
    return nullptr;
  }
  uint64_t id = uint64_t(idHigh) << 32 | idLow;

  // Note: Do this check after reading our ID data. Returning null will abort
  // the decode operation anyway, but we should at least be as safe as possible.
  if (NS_WARN_IF(!NS_IsMainThread())) {
    MOZ_DIAGNOSTIC_CRASH(
        "We shouldn't be trying to decode a BrowsingContext "
        "on a background thread.");
    return nullptr;
  }

  JS::Rooted<JS::Value> val(aCx, JS::NullValue());
  // We'll get rooting hazard errors from the RefPtr destructor if it isn't
  // destroyed before we try to return a raw JSObject*, so create it in its own
  // scope.
  if (RefPtr<BrowsingContext> context = Get(id)) {
    if (!context->Group()->IsKnownForChildID(aHolder->GetOriginChildID())) {
      return nullptr;
    }

    if (!GetOrCreateDOMReflector(aCx, context, &val) || !val.isObject()) {
      return nullptr;
    }
  }
  return val.toObjectOrNull();
}

bool BrowsingContext::CanSetOriginAttributes() {
  // A discarded BrowsingContext has already been destroyed, and cannot modify
  // its OriginAttributes.
  if (NS_WARN_IF(IsDiscarded())) {
    return false;
  }

  // Before attaching is the safest time to set OriginAttributes, and the only
  // allowed time for content BrowsingContexts.
  if (!EverAttached()) {
    return true;
  }

  // Attached content BrowsingContexts may have been synced to other processes.
  if (NS_WARN_IF(IsContent())) {
    MOZ_CRASH();
    return false;
  }
  MOZ_DIAGNOSTIC_ASSERT(XRE_IsParentProcess());

  // Cannot set OriginAttributes after we've created our child BrowsingContext.
  if (NS_WARN_IF(!Children().IsEmpty())) {
    return false;
  }

  // Only allow setting OriginAttributes if we have no associated document, or
  // the document is still `about:blank`.
  // TODO: Bug 1273058 - should have no document when setting origin attributes.
  if (WindowGlobalParent* window = Canonical()->GetCurrentWindowGlobal()) {
    if (nsIURI* uri = window->GetDocumentURI()) {
      MOZ_ASSERT(NS_IsAboutBlank(uri));
      return NS_IsAboutBlank(uri);
    }
  }
  return true;
}

Nullable<WindowProxyHolder> BrowsingContext::GetAssociatedWindow() {
  // nsILoadContext usually only returns same-process windows,
  // so we intentionally return nullptr if this BC is out of
  // process.
  if (IsInProcess()) {
    return WindowProxyHolder(this);
  }
  return nullptr;
}

Nullable<WindowProxyHolder> BrowsingContext::GetTopWindow() {
  return Top()->GetAssociatedWindow();
}

Element* BrowsingContext::GetTopFrameElement() {
  return Top()->GetEmbedderElement();
}

void BrowsingContext::SetUsePrivateBrowsing(bool aUsePrivateBrowsing,
                                            ErrorResult& aError) {
  nsresult rv = SetUsePrivateBrowsing(aUsePrivateBrowsing);
  if (NS_FAILED(rv)) {
    aError.Throw(rv);
  }
}

void BrowsingContext::SetUseTrackingProtectionWebIDL(
    bool aUseTrackingProtection, ErrorResult& aRv) {
  SetForceEnableTrackingProtection(aUseTrackingProtection, aRv);
}

void BrowsingContext::GetOriginAttributes(JSContext* aCx,
                                          JS::MutableHandle<JS::Value> aVal,
                                          ErrorResult& aError) {
  AssertOriginAttributesMatchPrivateBrowsing();

  if (!ToJSValue(aCx, mOriginAttributes, aVal)) {
    aError.NoteJSContextException(aCx);
  }
}

NS_IMETHODIMP BrowsingContext::GetAssociatedWindow(
    mozIDOMWindowProxy** aAssociatedWindow) {
  nsCOMPtr<mozIDOMWindowProxy> win = GetDOMWindow();
  win.forget(aAssociatedWindow);
  return NS_OK;
}

NS_IMETHODIMP BrowsingContext::GetTopWindow(mozIDOMWindowProxy** aTopWindow) {
  return Top()->GetAssociatedWindow(aTopWindow);
}

NS_IMETHODIMP BrowsingContext::GetTopFrameElement(Element** aTopFrameElement) {
  RefPtr<Element> topFrameElement = GetTopFrameElement();
  topFrameElement.forget(aTopFrameElement);
  return NS_OK;
}

NS_IMETHODIMP BrowsingContext::GetIsContent(bool* aIsContent) {
  *aIsContent = IsContent();
  return NS_OK;
}

NS_IMETHODIMP BrowsingContext::GetUsePrivateBrowsing(
    bool* aUsePrivateBrowsing) {
  *aUsePrivateBrowsing = mPrivateBrowsingId > 0;
  return NS_OK;
}

NS_IMETHODIMP BrowsingContext::SetUsePrivateBrowsing(bool aUsePrivateBrowsing) {
  if (!CanSetOriginAttributes()) {
    bool changed = aUsePrivateBrowsing != (mPrivateBrowsingId > 0);
    if (changed) {
      NS_WARNING("SetUsePrivateBrowsing when !CanSetOriginAttributes()");
    }
    return changed ? NS_ERROR_FAILURE : NS_OK;
  }

  return SetPrivateBrowsing(aUsePrivateBrowsing);
}

NS_IMETHODIMP BrowsingContext::SetPrivateBrowsing(bool aPrivateBrowsing) {
  if (!CanSetOriginAttributes()) {
    NS_WARNING("Attempt to set PrivateBrowsing when !CanSetOriginAttributes");
    return NS_ERROR_FAILURE;
  }

  bool changed = aPrivateBrowsing != (mPrivateBrowsingId > 0);
  if (changed) {
    mPrivateBrowsingId = aPrivateBrowsing ? 1 : 0;
    if (IsContent()) {
      mOriginAttributes.SyncAttributesWithPrivateBrowsing(aPrivateBrowsing);
    }

    if (XRE_IsParentProcess()) {
      Canonical()->AdjustPrivateBrowsingCount(aPrivateBrowsing);
    }
  }
  AssertOriginAttributesMatchPrivateBrowsing();

  if (changed && mDocShell) {
    nsDocShell::Cast(mDocShell)->NotifyPrivateBrowsingChanged();
  }
  return NS_OK;
}

NS_IMETHODIMP BrowsingContext::GetUseRemoteTabs(bool* aUseRemoteTabs) {
  *aUseRemoteTabs = mUseRemoteTabs;
  return NS_OK;
}

NS_IMETHODIMP BrowsingContext::SetRemoteTabs(bool aUseRemoteTabs) {
  if (!CanSetOriginAttributes()) {
    NS_WARNING("Attempt to set RemoteTabs when !CanSetOriginAttributes");
    return NS_ERROR_FAILURE;
  }

  if (aUseRemoteTabs && !gIPCEnabledAnnotation) {
    gIPCEnabledAnnotation = true;
  }

  // Don't allow non-remote tabs with remote subframes.
  if (NS_WARN_IF(!aUseRemoteTabs && mUseRemoteSubframes)) {
    return NS_ERROR_UNEXPECTED;
  }

  mUseRemoteTabs = aUseRemoteTabs;
  return NS_OK;
}

NS_IMETHODIMP BrowsingContext::GetUseRemoteSubframes(
    bool* aUseRemoteSubframes) {
  *aUseRemoteSubframes = mUseRemoteSubframes;
  return NS_OK;
}

NS_IMETHODIMP BrowsingContext::SetRemoteSubframes(bool aUseRemoteSubframes) {
  if (!CanSetOriginAttributes()) {
    NS_WARNING("Attempt to set RemoteSubframes when !CanSetOriginAttributes");
    return NS_ERROR_FAILURE;
  }

  if (aUseRemoteSubframes && !gFissionEnabledAnnotation) {
    gFissionEnabledAnnotation = true;
  }

  // Don't allow non-remote tabs with remote subframes.
  if (NS_WARN_IF(aUseRemoteSubframes && !mUseRemoteTabs)) {
    return NS_ERROR_UNEXPECTED;
  }

  mUseRemoteSubframes = aUseRemoteSubframes;
  return NS_OK;
}

NS_IMETHODIMP BrowsingContext::GetUseTrackingProtection(
    bool* aUseTrackingProtection) {
  *aUseTrackingProtection = false;

  if (GetForceEnableTrackingProtection() ||
      StaticPrefs::privacy_trackingprotection_enabled() ||
      (UsePrivateBrowsing() &&
       StaticPrefs::privacy_trackingprotection_pbmode_enabled())) {
    *aUseTrackingProtection = true;
    return NS_OK;
  }

  if (GetParent()) {
    return GetParent()->GetUseTrackingProtection(aUseTrackingProtection);
  }

  return NS_OK;
}

NS_IMETHODIMP BrowsingContext::SetUseTrackingProtection(
    bool aUseTrackingProtection) {
  return SetForceEnableTrackingProtection(aUseTrackingProtection);
}

NS_IMETHODIMP BrowsingContext::GetScriptableOriginAttributes(
    JSContext* aCx, JS::MutableHandle<JS::Value> aVal) {
  AssertOriginAttributesMatchPrivateBrowsing();

  bool ok = ToJSValue(aCx, mOriginAttributes, aVal);
  NS_ENSURE_TRUE(ok, NS_ERROR_FAILURE);
  return NS_OK;
}

NS_IMETHODIMP_(void)
BrowsingContext::GetOriginAttributes(OriginAttributes& aAttrs) {
  aAttrs = mOriginAttributes;
  AssertOriginAttributesMatchPrivateBrowsing();
}

nsresult BrowsingContext::SetOriginAttributes(const OriginAttributes& aAttrs) {
  if (!CanSetOriginAttributes()) {
    NS_WARNING("Attempt to set OriginAttributes when !CanSetOriginAttributes");
    return NS_ERROR_FAILURE;
  }

  AssertOriginAttributesMatchPrivateBrowsing();
  mOriginAttributes = aAttrs;

  bool isPrivate = mOriginAttributes.mPrivateBrowsingId !=
                   nsIScriptSecurityManager::DEFAULT_PRIVATE_BROWSING_ID;
  // Chrome Browsing Context can not contain OriginAttributes.mPrivateBrowsingId
  if (IsChrome() && isPrivate) {
    mOriginAttributes.mPrivateBrowsingId =
        nsIScriptSecurityManager::DEFAULT_PRIVATE_BROWSING_ID;
  }
  SetPrivateBrowsing(isPrivate);
  AssertOriginAttributesMatchPrivateBrowsing();

  return NS_OK;
}

void BrowsingContext::AssertOriginAttributesMatchPrivateBrowsing() {
  // Chrome browsing contexts must not have a private browsing OriginAttribute
  // Content browsing contexts must maintain the equality:
  // mOriginAttributes.mPrivateBrowsingId == mPrivateBrowsingId
  if (IsChrome()) {
    MOZ_DIAGNOSTIC_ASSERT(mOriginAttributes.mPrivateBrowsingId == 0);
  } else {
    MOZ_DIAGNOSTIC_ASSERT(mOriginAttributes.mPrivateBrowsingId ==
                          mPrivateBrowsingId);
  }
}

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(BrowsingContext)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsILoadContext)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
  NS_INTERFACE_MAP_ENTRY_CONCRETE(BrowsingContext)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION_CLASS(BrowsingContext)

NS_IMPL_CYCLE_COLLECTING_ADDREF(BrowsingContext)
NS_IMPL_CYCLE_COLLECTING_RELEASE(BrowsingContext)

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(BrowsingContext)
  NS_IMPL_CYCLE_COLLECTION_TRACE_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(BrowsingContext)
  if (sBrowsingContexts) {
    sBrowsingContexts->Remove(tmp->Id());
  }
  UnregisterBrowserId(tmp);

  if (tmp->GetIsPopupSpam()) {
    PopupBlocker::UnregisterOpenPopupSpam();
    // NOTE: Doesn't use SetIsPopupSpam, as it will be set all processes
    // automatically.
    tmp->mFields.SetWithoutSyncing<IDX_IsPopupSpam>(false);
  }

  NS_IMPL_CYCLE_COLLECTION_UNLINK(
      mDocShell, mParentWindow, mGroup, mEmbedderElement, mWindowContexts,
      mCurrentWindowContext, mSessionStorageManager, mChildSessionHistory)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(BrowsingContext)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(
      mDocShell, mParentWindow, mGroup, mEmbedderElement, mWindowContexts,
      mCurrentWindowContext, mSessionStorageManager, mChildSessionHistory)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

static bool IsCertainlyAliveForCC(BrowsingContext* aContext) {
  return aContext->HasKnownLiveWrapper() ||
         (AppShutdown::GetCurrentShutdownPhase() ==
              ShutdownPhase::NotInShutdown &&
          aContext->EverAttached() && !aContext->IsDiscarded());
}

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_BEGIN(BrowsingContext)
  if (IsCertainlyAliveForCC(tmp)) {
    if (tmp->PreservingWrapper()) {
      tmp->MarkWrapperLive();
    }
    return true;
  }
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_IN_CC_BEGIN(BrowsingContext)
  return IsCertainlyAliveForCC(tmp) && tmp->HasNothingToTrace(tmp);
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_IN_CC_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_THIS_BEGIN(BrowsingContext)
  return IsCertainlyAliveForCC(tmp);
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_THIS_END

/* static */
void BrowsingContext::SweepWindowProxies(JSTracer* aTrc) {
  if (!sBrowsingContexts) {
    return;
  }

  for (BrowsingContext* bc : sBrowsingContexts->Values()) {
    if (bc->mWindowProxy) {
      JS_UpdateWeakPointerAfterGC(aTrc, &bc->mWindowProxy);
    }
  }
}

class RemoteLocationProxy
    : public RemoteObjectProxy<BrowsingContext::LocationProxy,
                               Location_Binding::sCrossOriginProperties> {
 public:
  typedef RemoteObjectProxy Base;

  constexpr RemoteLocationProxy()
      : RemoteObjectProxy(prototypes::id::Location) {}

  void NoteChildren(JSObject* aProxy,
                    nsCycleCollectionTraversalCallback& aCb) const override {
    auto location =
        static_cast<BrowsingContext::LocationProxy*>(GetNative(aProxy));
    CycleCollectionNoteChild(aCb, location->GetBrowsingContext(),
                             "JS::GetPrivate(obj)->GetBrowsingContext()");
  }
};

static const RemoteLocationProxy sSingleton;

template <>
const JSClass RemoteLocationProxy::Base::sClass = PROXY_CLASS_DEF(
    "Proxy", JSCLASS_HAS_RESERVED_SLOTS(js::SwappableProxyReservedSlots));

void BrowsingContext::Location(JSContext* aCx,
                               JS::MutableHandle<JSObject*> aLocation,
                               ErrorResult& aError) {
  aError.MightThrowJSException();
  sSingleton.GetProxyObject(aCx, &mLocation, /* aTransplantTo = */ nullptr,
                            aLocation);
  if (!aLocation) {
    aError.StealExceptionFromJSContext(aCx);
  }
}

bool BrowsingContext::RemoveRootFromBFCacheSync() {
  if (WindowContext* wc = GetParentWindowContext()) {
    if (RefPtr<Document> doc = wc->TopWindowContext()->GetDocument()) {
      return doc->RemoveFromBFCacheSync();
    }
  }
  return false;
}

nsresult BrowsingContext::CheckSandboxFlags(nsDocShellLoadState* aLoadState) {
  const auto& sourceBC = aLoadState->SourceBrowsingContext();
  if (sourceBC.IsNull()) {
    return NS_OK;
  }

  // We might be called after the source BC has been discarded, but before we've
  // destroyed our in-process instance of the BrowsingContext object in some
  // situations (e.g. after creating a new pop-up with window.open while the
  // window is being closed). In these situations we want to still perform the
  // sandboxing check against our in-process copy. If we've forgotten about the
  // context already, assume it is sanboxed. (bug 1643450)
  BrowsingContext* bc = sourceBC.GetMaybeDiscarded();
  if (!bc || bc->IsSandboxedFrom(this)) {
    return NS_ERROR_DOM_SECURITY_ERR;
  }
  return NS_OK;
}

nsresult BrowsingContext::CheckFramebusting(nsDocShellLoadState* aLoadState) {
  if (!StaticPrefs::dom_security_framebusting_intervention_enabled()) {
    return NS_OK;
  }

  // Only applies to top-level navigations.
  if (!IsTop()) {
    return NS_OK;
  }

  if (aLoadState->HasValidUserGestureActivation()) {
    return NS_OK;
  }

  if (aLoadState->LoadIsFromSessionHistory()) {
    return NS_OK;
  }

  const auto& sourceBC = aLoadState->SourceBrowsingContext();
  if (sourceBC.IsNull()) {
    return NS_OK;
  }

  if (BrowsingContext* bc = sourceBC.GetMaybeDiscarded()) {
    // If the source lives in a different top-level browser, it is a
    // cross-tab navigation (e.g. via window.opener) and allowed.
    if (bc->BrowserId() != BrowserId()) {
      return NS_OK;
    }

    if (bc->GetCurrentWindowContext() &&
        bc->GetCurrentWindowContext()->GetIsFramebustingAllowed()) {
      return NS_OK;
    }

    for (auto* context = bc->GetCurrentWindowContext(); context;
         context = context->GetParentWindowContext()) {
      if (context->CanFramebust()) {
        return NS_OK;
      }
    }

    if (bc->GetDOMWindow()) {
      nsGlobalWindowOuter::Cast(bc->GetDOMWindow())
          ->FireRedirectBlockedEvent(aLoadState->URI());
    }

    nsAutoCString frameURL;
    if (bc->GetDocument() &&
        NS_SUCCEEDED(
            bc->GetDocument()->GetPrincipal()->GetAsciiSpec(frameURL))) {
      nsContentUtils::ReportToConsoleNonLocalized(
          NS_ConvertUTF8toUTF16(nsPrintfCString(
              R"(Attempting to navigate the top-level browsing context from )"
              R"(frame with url "%s" which is neither same-origin nor has )"
              R"(the required user interaction.)",
              frameURL.get())),
          nsIScriptError::errorFlag, "DOM"_ns, bc->GetDocument());
    }
  }

  return NS_ERROR_DOM_SECURITY_ERR;
}

bool BrowsingContext::ComputeIsFramebustingAllowed() {
  MOZ_ASSERT(IsInProcess());

  if (IsTop()) {
    return true;
  }

  if (SameOriginWithTop()) {
    return true;
  }

  // The browsing context's sandbox flags are the iframe "sandbox" attribute
  // OR'ed with the parent's flags (CSP "sandbox" only applies to the
  // document). Check the parent too, otherwise a page could grant itself
  // "allow-top-navigation".
  uint32_t sandboxFlags = GetSandboxFlags();
  if (sandboxFlags && !(sandboxFlags & SANDBOXED_TOPLEVEL_NAVIGATION)) {
    return GetParentWindowContext() &&
           GetParentWindowContext()->GetIsFramebustingAllowed();
  }

  return false;
}

nsresult BrowsingContext::LoadURI(nsDocShellLoadState* aLoadState,
                                  bool aSetNavigating) {
  // Per spec, most load attempts are silently ignored when a BrowsingContext is
  // null (which in our code corresponds to discarded), so we simply fail
  // silently in those cases. Regardless, we cannot trigger loads in/from
  // discarded BrowsingContexts via IPC, so we need to abort in any case.
  if (IsDiscarded()) {
    return NS_OK;
  }

  MOZ_DIAGNOSTIC_ASSERT(aLoadState->TargetBrowsingContext().IsNull(),
                        "Targeting occurs in InternalLoad");
  aLoadState->AssertProcessCouldTriggerLoadIfSystem();

  // When this tab sets these load flags, we disable or force TRR for the
  // browsing context ensuring subsequent navigations will keep the same
  // TRR mode.
  if (aLoadState->HasLoadFlags(nsIWebNavigation::LOAD_FLAGS_DISABLE_TRR)) {
    (void)SetDefaultLoadFlags(GetDefaultLoadFlags() |
                              nsIRequest::LOAD_TRR_DISABLED_MODE);
  } else if (aLoadState->HasLoadFlags(nsIWebNavigation::LOAD_FLAGS_FORCE_TRR)) {
    (void)SetDefaultLoadFlags(GetDefaultLoadFlags() |
                              nsIRequest::LOAD_TRR_ONLY_MODE);
  }

  if (mDocShell) {
    nsCOMPtr<nsIDocShell> docShell = mDocShell;

    return docShell->LoadURI(aLoadState, aSetNavigating);
  }

  // Note: We do this check both here and in `nsDocShell::InternalLoad`, since
  // document-specific sandbox flags are only available in the process
  // triggering the load, and we don't want the target process to have to trust
  // the triggering process to do the appropriate checks for the
  // BrowsingContext's sandbox flags.
  MOZ_TRY(CheckSandboxFlags(aLoadState));
  SetTriggeringAndInheritPrincipals(aLoadState->TriggeringPrincipal(),
                                    aLoadState->PrincipalToInherit(),
                                    aLoadState->GetLoadIdentifier());

  const auto& sourceBC = aLoadState->SourceBrowsingContext();

  if (aLoadState->URI()->SchemeIs("javascript")) {
    if (!XRE_IsParentProcess()) {
      // Web content should only be able to load javascript: URIs into documents
      // whose principals the caller principal subsumes, which by definition
      // excludes any document in a cross-process BrowsingContext.
      return NS_ERROR_DOM_BAD_CROSS_ORIGIN_URI;
    }
    MOZ_DIAGNOSTIC_ASSERT(!sourceBC,
                          "Should never see a cross-process javascript: load "
                          "triggered from content");
  }

  // Note: We do this check both here and in `nsDocShell::InternalLoad`.
  // Same reason as for the sandbox flags.
  MOZ_TRY(CheckFramebusting(aLoadState));

  MOZ_DIAGNOSTIC_ASSERT(!sourceBC || sourceBC->Group() == Group());
  if (sourceBC && sourceBC->IsInProcess()) {
    nsCOMPtr<nsPIDOMWindowOuter> win(sourceBC->GetDOMWindow());
    if (WindowGlobalChild* wgc =
            win->GetCurrentInnerWindow()->GetWindowGlobalChild()) {
      if (!wgc->CanNavigate(this)) {
        return NS_ERROR_DOM_PROP_ACCESS_DENIED;
      }
      wgc->SendLoadURI(this, mozilla::WrapNotNull(aLoadState), aSetNavigating);
    }
  } else if (XRE_IsParentProcess()) {
    if (ContentParent* cp = Canonical()->GetContentParent()) {
      // Attempt to initiate this load immediately in the parent, if it
      // succeeds, aLoadState will have a reference to the pending
      // DocumentLoadListener, which will be recovered when the DocumentChannel
      // is created.
      Canonical()->AttemptSpeculativeLoadInParent(aLoadState);

#ifdef ANDROID
      uint32_t appLinkLaunchType = aLoadState->GetAppLinkLaunchType();
      Canonical()->SetAndroidAppLinkLaunchType(appLinkLaunchType);

      // Record timing for cold app link launches
      constexpr uint32_t APPLINK_COLD = 1;
      if (appLinkLaunchType == APPLINK_COLD) {
        const TimeStamp loadUriTime = TimeStamp::Now();

        // Process creation to load URI timing
        const TimeStamp processCreationTime = TimeStamp::ProcessCreation();
        if (!processCreationTime.IsNull()) {
          const TimeDuration delta = loadUriTime - processCreationTime;
          mozilla::glean::perf::cold_applink_process_launch_to_load_uri
              .AccumulateRawDuration(delta);

          PROFILER_MARKER("Cold App Link Process Creation to Load URI", NETWORK,
                          MarkerOptions(MarkerTiming::Interval(
                              processCreationTime, loadUriTime)),
                          Tracing, "AppLink");
        }

        // StartupTimeline::MAIN to load URI timing
        const TimeStamp mainTime = StartupTimeline::Get(StartupTimeline::MAIN);
        if (!mainTime.IsNull()) {
          const TimeDuration mainDelta = loadUriTime - mainTime;
          mozilla::glean::perf::cold_applink_main_to_load_uri
              .AccumulateRawDuration(mainDelta);

          PROFILER_MARKER(
              "Cold App Link Main to Load URI", NETWORK,
              MarkerOptions(MarkerTiming::Interval(mainTime, loadUriTime)),
              Tracing, "AppLink");
        }
      }

      PROFILER_MARKER_FMT("BrowsingContext::LoadURI", NETWORK, {},
                          "android appLinkLaunchType {} URL {}",
                          appLinkLaunchType,
                          aLoadState->URI()->GetSpecOrDefault().get());
#endif

      (void)cp->SendLoadURI(this, mozilla::WrapNotNull(aLoadState),
                            aSetNavigating);
    }
  } else {
    if (!sourceBC) {
      return NS_ERROR_UNEXPECTED;
    }
    // If we're in a content process and the source BC is no longer in-process,
    // just fail silently.
  }
  return NS_OK;
}

nsresult BrowsingContext::InternalLoad(nsDocShellLoadState* aLoadState) {
  if (IsDiscarded()) {
    return NS_OK;
  }
  SetTriggeringAndInheritPrincipals(aLoadState->TriggeringPrincipal(),
                                    aLoadState->PrincipalToInherit(),
                                    aLoadState->GetLoadIdentifier());

  MOZ_DIAGNOSTIC_ASSERT(aLoadState->Target().IsEmpty(),
                        "should already have retargeted");
  MOZ_DIAGNOSTIC_ASSERT(!aLoadState->TargetBrowsingContext().IsNull(),
                        "should have target bc set");
  MOZ_DIAGNOSTIC_ASSERT(aLoadState->TargetBrowsingContext() == this,
                        "must be targeting this BrowsingContext");
  aLoadState->AssertProcessCouldTriggerLoadIfSystem();

  if (mDocShell) {
    RefPtr<nsDocShell> docShell = nsDocShell::Cast(mDocShell);
    return docShell->InternalLoad(aLoadState);
  }

  // Note: We do this check both here and in `nsDocShell::InternalLoad`, since
  // document-specific sandbox flags are only available in the process
  // triggering the load, and we don't want the target process to have to trust
  // the triggering process to do the appropriate checks for the
  // BrowsingContext's sandbox flags.
  MOZ_TRY(CheckSandboxFlags(aLoadState));

  const auto& sourceBC = aLoadState->SourceBrowsingContext();

  if (aLoadState->URI()->SchemeIs("javascript")) {
    if (!XRE_IsParentProcess()) {
      // Web content should only be able to load javascript: URIs into documents
      // whose principals the caller principal subsumes, which by definition
      // excludes any document in a cross-process BrowsingContext.
      return NS_ERROR_DOM_BAD_CROSS_ORIGIN_URI;
    }
    MOZ_DIAGNOSTIC_ASSERT(!sourceBC,
                          "Should never see a cross-process javascript: load "
                          "triggered from content");
  }

  // Note: We do this check both here and in `nsDocShell::InternalLoad`.
  // Same reason as for the sandbox flags.
  MOZ_TRY(CheckFramebusting(aLoadState));

  if (XRE_IsParentProcess()) {
    ContentParent* cp = Canonical()->GetContentParent();
    if (!cp || !cp->CanSend()) {
      return NS_ERROR_FAILURE;
    }

    MOZ_ALWAYS_SUCCEEDS(
        SetCurrentLoadIdentifier(Some(aLoadState->GetLoadIdentifier())));
    (void)cp->SendInternalLoad(mozilla::WrapNotNull(aLoadState));
  } else {
    MOZ_DIAGNOSTIC_ASSERT(sourceBC);
    MOZ_DIAGNOSTIC_ASSERT(sourceBC->Group() == Group());

    nsCOMPtr<nsPIDOMWindowOuter> win(sourceBC->GetDOMWindow());
    WindowGlobalChild* wgc =
        win->GetCurrentInnerWindow()->GetWindowGlobalChild();
    if (!wgc || !wgc->CanSend()) {
      return NS_ERROR_FAILURE;
    }
    if (!wgc->CanNavigate(this)) {
      return NS_ERROR_DOM_PROP_ACCESS_DENIED;
    }

    MOZ_ALWAYS_SUCCEEDS(
        SetCurrentLoadIdentifier(Some(aLoadState->GetLoadIdentifier())));
    wgc->SendInternalLoad(mozilla::WrapNotNull(aLoadState));
  }

  return NS_OK;
}

already_AddRefed<nsDocShellLoadState>
BrowsingContext::CheckURLAndCreateLoadState(nsIURI* aURI,
                                            nsIPrincipal& aSubjectPrincipal,
                                            Document* aSourceDocument,
                                            ErrorResult& aRv) {
  nsCOMPtr<nsIPrincipal> triggeringPrincipal;
  nsCOMPtr<nsIURI> sourceURI;
  ReferrerPolicy referrerPolicy = ReferrerPolicy::_empty;
  nsCOMPtr<nsIReferrerInfo> referrerInfo;

  // Get security manager.
  nsIScriptSecurityManager* ssm = nsContentUtils::GetSecurityManager();
  if (NS_WARN_IF(!ssm)) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  // Check to see if URI is allowed.  We're not going to worry about a
  // window ID here because it's not 100% clear which window's id we
  // would want, and we're throwing a content-visible exception
  // anyway.
  nsresult rv = ssm->CheckLoadURIWithPrincipal(
      &aSubjectPrincipal, aURI, nsIScriptSecurityManager::STANDARD, 0);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    nsAutoCString spec;
    aURI->GetSpec(spec);
    aRv.ThrowTypeError<MSG_URL_NOT_LOADABLE>(spec);
    return nullptr;
  }

  // Create load info
  RefPtr loadState = MakeRefPtr<nsDocShellLoadState>(aURI);

  if (!aSourceDocument) {
    // No document; just use our subject principal as the triggering principal.
    loadState->SetTriggeringPrincipal(&aSubjectPrincipal);
    return loadState.forget();
  }

  nsCOMPtr<nsIURI> docOriginalURI, docCurrentURI, principalURI;
  docOriginalURI = aSourceDocument->GetOriginalURI();
  docCurrentURI = aSourceDocument->GetDocumentURI();
  nsCOMPtr<nsIPrincipal> principal = aSourceDocument->NodePrincipal();

  triggeringPrincipal = aSourceDocument->NodePrincipal();
  referrerPolicy = aSourceDocument->GetReferrerPolicy();

  bool urisEqual = false;
  if (docOriginalURI && docCurrentURI && principal) {
    principal->EqualsURI(docOriginalURI, &urisEqual);
  }
  if (urisEqual) {
    referrerInfo = MakeRefPtr<ReferrerInfo>(docCurrentURI, referrerPolicy);
  } else {
    principal->CreateReferrerInfo(referrerPolicy, getter_AddRefs(referrerInfo));
  }
  loadState->SetTriggeringPrincipal(triggeringPrincipal);
  loadState->SetTriggeringSandboxFlags(aSourceDocument->GetSandboxFlags());
  loadState->SetPolicyContainer(aSourceDocument->GetPolicyContainer());
  if (referrerInfo) {
    loadState->SetReferrerInfo(referrerInfo);
  }
  loadState->SetHasValidUserGestureActivation(
      aSourceDocument->HasValidTransientUserGestureActivation());

  loadState->SetTextDirectiveUserActivation(
      aSourceDocument->ConsumeTextDirectiveUserActivation() ||
      loadState->HasValidUserGestureActivation());
  loadState->SetTriggeringWindowId(aSourceDocument->InnerWindowID());
  loadState->SetTriggeringStorageAccess(aSourceDocument->UsingStorageAccess());
  loadState->SetTriggeringClassificationFlags(
      aSourceDocument->GetScriptTrackingFlags());

  return loadState.forget();
}

// https://html.spec.whatwg.org/#navigate
// In its current state, this method is not closely following the spec.
// https://bugzil.la/1974717 tracks the work to align this method with the spec.
void BrowsingContext::Navigate(
    nsIURI* aURI, Document* aSourceDocument, nsIPrincipal& aSubjectPrincipal,
    ErrorResult& aRv, NavigationHistoryBehavior aHistoryHandling,
    bool aNeedsCompletelyLoadedDocument,
    nsIStructuredCloneContainer* aNavigationAPIState,
    dom::NavigationAPIMethodTracker* aNavigationAPIMethodTracker) {
  MOZ_LOG_FMT(gNavigationAPILog, LogLevel::Debug, "Navigate to {} as {}", *aURI,
              aHistoryHandling);
  CallerType callerType = aSubjectPrincipal.IsSystemPrincipal()
                              ? CallerType::System
                              : CallerType::NonSystem;

  nsresult rv = CheckNavigationRateLimit(callerType);
  if (NS_FAILED(rv)) {
    aRv.Throw(rv);
    return;
  }

  RefPtr<nsDocShellLoadState> loadState =
      CheckURLAndCreateLoadState(aURI, aSubjectPrincipal, aSourceDocument, aRv);
  if (aRv.Failed()) {
    return;
  }

  loadState->SetNeedsCompletelyLoadedDocument(aNeedsCompletelyLoadedDocument);
  loadState->SetHistoryBehavior(aHistoryHandling);

  if (aHistoryHandling == NavigationHistoryBehavior::Replace) {
    loadState->SetLoadType(LOAD_STOP_CONTENT_AND_REPLACE);
  } else {
    loadState->SetLoadType(LOAD_STOP_CONTENT);
  }

  const auto snapShot = [&](auto& source) {
    loadState->SetSourceBrowsingContext(source->GetBrowsingContext());
    WindowContext* context = source->GetWindowContext();
    loadState->SetHasValidUserGestureActivation(
        context && context->HasValidTransientUserGestureActivation());

    // For protocols that would launch without a prompt (e.g. mailto), consume
    // the transient user gesture activation so a single gesture can't chain
    // multiple launches. The pre-consume value is already recorded on the load
    // state above. See bug 299116.
    nsAutoCString scheme;
    if (NS_SUCCEEDED(aURI->GetScheme(scheme))) {
      nsExternalHelperAppService::MaybeConsumeUserActivationForExternalScheme(
          context, loadState->TriggeringPrincipal(), scheme);
    }
  };

  // aSourceDocument is used for snapshot params and "allowed by sandboxing to
  // navigate" in https://html.spec.whatwg.org/#navigate first step 2 then 6.2.
  // When snap shotting we read the UA value
  // https://html.spec.whatwg.org/#snapshotting-source-snapshot-params
  if (aSourceDocument && aSourceDocument->GetBrowsingContext()) {
    snapShot(aSourceDocument);
  } else if (nsCOMPtr<nsPIDOMWindowInner> incumbentWindow =
                 nsContentUtils::IncumbentInnerWindow()) {
    // When BrowsingContext::Navigate can get called with browser UI involvement
    // snapshot with default params. See Bug 2010041
    snapShot(incumbentWindow);
  }

  loadState->SetLoadFlags(nsIWebNavigation::LOAD_FLAGS_NONE);
  loadState->SetFirstParty(true);
  loadState->SetNavigationAPIState(aNavigationAPIState);
  loadState->SetNavigationAPIMethodTracker(aNavigationAPIMethodTracker);

  rv = LoadURI(loadState);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    if (rv == NS_ERROR_DOM_BAD_CROSS_ORIGIN_URI &&
        loadState->URI()->SchemeIs("javascript")) {
      // Per spec[1], attempting to load a javascript: URI into a cross-origin
      // BrowsingContext is a no-op, and should not raise an exception.
      // Technically, Location setters run with exceptions enabled should only
      // throw an exception[2] when the caller is not allowed to navigate[3] the
      // target browsing context due to sandboxing flags or not being
      // closely-related enough, though in practice we currently throw for other
      // reasons as well.
      //
      // [1]:
      // https://html.spec.whatwg.org/multipage/browsing-the-web.html#javascript-protocol
      // [2]:
      // https://html.spec.whatwg.org/multipage/browsing-the-web.html#navigate
      // [3]:
      // https://html.spec.whatwg.org/multipage/browsers.html#allowed-to-navigate
      return;
    }
    aRv.Throw(rv);
    return;
  }

  Document* doc = GetDocument();
  if (doc && nsContentUtils::IsExternalProtocol(aURI)) {
    doc->EnsureNotEnteringAndExitFullscreen();
  }
}

void BrowsingContext::DisplayLoadError(const nsAString& aURI) {
  MOZ_LOG(GetLog(), LogLevel::Debug, ("DisplayLoadError"));
  MOZ_DIAGNOSTIC_ASSERT(!IsDiscarded());
  MOZ_DIAGNOSTIC_ASSERT(mDocShell || XRE_IsParentProcess());

  if (mDocShell) {
    bool didDisplayLoadError = false;
    nsCOMPtr<nsIDocShell> docShell = mDocShell;
    docShell->DisplayLoadError(NS_ERROR_MALFORMED_URI, nullptr,
                               PromiseFlatString(aURI).get(), nullptr,
                               &didDisplayLoadError);
  } else {
    if (ContentParent* cp = Canonical()->GetContentParent()) {
      (void)cp->SendDisplayLoadError(this, PromiseFlatString(aURI));
    }
  }
}

WindowProxyHolder BrowsingContext::Window() {
  return WindowProxyHolder(Self());
}

WindowProxyHolder BrowsingContext::GetFrames(ErrorResult& aError) {
  return Window();
}

void BrowsingContext::Close(CallerType aCallerType, ErrorResult& aError) {
  if (mIsDiscarded) {
    return;
  }

  if (IsSubframe()) {
    // .close() on frames is a no-op.
    return;
  }

  if (GetDOMWindow()) {
    nsGlobalWindowOuter::Cast(GetDOMWindow())
        ->CloseOuter(aCallerType == CallerType::System);
    return;
  }

  // This is a bit of a hack for webcompat. Content needs to see an updated
  // |window.closed| value as early as possible, so we set this before we
  // actually send the DOMWindowClose event, which happens in the process where
  // the document for this browsing context is loaded.
  MOZ_ALWAYS_SUCCEEDS(SetClosed(true));

  if (ContentChild* cc = ContentChild::GetSingleton()) {
    cc->SendWindowClose(this, aCallerType == CallerType::System);
  } else if (ContentParent* cp = Canonical()->GetContentParent()) {
    (void)cp->SendWindowClose(this, aCallerType == CallerType::System);
  }
}

template <typename FuncT>
inline bool ApplyToDocumentsForPopup(Document* doc, FuncT func) {
  // HACK: Some pages using bogus library + UA sniffing call window.open()
  // from a blank iframe, only on Firefox, see bug 1685056.
  //
  // This is a hack-around to preserve behavior in that particular and
  // specific case, by consuming activation on the parent document, so we
  // don't care about the InProcessParent bits not being fission-safe or what
  // not.
  if (func(doc)) {
    return true;
  }
  if (!doc->IsInitialDocument()) {
    return false;
  }
  Document* parentDoc = doc->GetInProcessParentDocument();
  if (!parentDoc || !parentDoc->NodePrincipal()->Equals(doc->NodePrincipal())) {
    return false;
  }
  return func(parentDoc);
}

PopupBlocker::PopupControlState BrowsingContext::RevisePopupAbuseLevel(
    PopupBlocker::PopupControlState aControl) {
  if (!IsContent()) {
    return PopupBlocker::openAllowed;
  }

  RefPtr<Document> doc = GetExtantDocument();
  PopupBlocker::PopupControlState abuse = aControl;
  switch (abuse) {
    case PopupBlocker::openControlled:
    case PopupBlocker::openOverridden:
      if (IsPopupAllowed()) {
        // Go down one state enum step:
        //   openControlled (1) -> openAllowed (0)
        //   openOverridden (4) -> openAbused (3)
        abuse = PopupBlocker::PopupControlState(abuse - 1);
      }
      break;
    case PopupBlocker::openAbused:
      if (IsPopupAllowed() ||
          (doc && doc->HasValidTransientUserGestureActivation())) {
        // Always go down to openControlled:
        //   openAbused (3)  -> openControlled (1), skip openBlocked (2)
        abuse = PopupBlocker::openControlled;
      }
      break;
    case PopupBlocker::openAllowed:
      break;
    case PopupBlocker::openBlocked:
      if (IsPopupAllowed() ||
          (doc && doc->HasValidTransientUserGestureActivation())) {
        // Go down one state enum step:
        //   openBlocked (2) -> openControlled (1)
        abuse = PopupBlocker::openControlled;
      }
      break;
    default:
      NS_WARNING("Strange PopupControlState!");
  }

  // limit the number of simultaneously open popups
  if (abuse == PopupBlocker::openAbused || abuse == PopupBlocker::openBlocked ||
      abuse == PopupBlocker::openControlled) {
    int32_t popupMax = StaticPrefs::dom_popup_maximum();
    if (popupMax >= 0 &&
        PopupBlocker::GetOpenPopupSpamCount() >= (uint32_t)popupMax) {
      abuse = PopupBlocker::openOverridden;
    }
  }

  // If we're currently in-process, attempt to consume transient user gesture
  // activations.
  if (doc) {
    auto ConsumeTransientUserActivationForMultiplePopupBlocking =
        [&]() -> bool {
      return ApplyToDocumentsForPopup(doc, [](Document* doc) {
        return doc->ConsumeTransientUserGestureActivation();
      });
    };

    // If this popup is allowed, let's block any other for this event, forcing
    // PopupBlocker::openBlocked state.
    if ((abuse == PopupBlocker::openAllowed ||
         abuse == PopupBlocker::openControlled) &&
        !IsPopupAllowed() &&
        !ConsumeTransientUserActivationForMultiplePopupBlocking()) {
      nsContentUtils::ReportToConsole(nsIScriptError::warningFlag, "DOM"_ns,
                                      doc, PropertiesFile::DOM_PROPERTIES,
                                      "MultiplePopupsBlockedNoUserActivation");
      abuse = PopupBlocker::openBlocked;
    }
  }

  return abuse;
}

void BrowsingContext::GetUserActivationModifiersForPopup(
    UserActivation::Modifiers* aModifiers) {
  RefPtr<Document> doc = GetExtantDocument();
  if (doc) {
    // Unlike RevisePopupAbuseLevel, modifiers can always be used regardless
    // of PopupControlState.
    (void)ApplyToDocumentsForPopup(doc, [&](Document* doc) {
      return doc->GetTransientUserGestureActivationModifiers(aModifiers);
    });
  }
}

void BrowsingContext::IncrementHistoryEntryCountForBrowsingContext() {
  (void)SetHistoryEntryCount(GetHistoryEntryCount() + 1);
}

std::tuple<bool, bool> BrowsingContext::CanFocusCheck(CallerType aCallerType) {
  nsFocusManager* fm = nsFocusManager::GetFocusManager();
  if (!fm) {
    return {false, false};
  }

  nsCOMPtr<nsPIDOMWindowInner> caller = do_QueryInterface(GetEntryGlobal());
  BrowsingContext* callerBC = caller ? caller->GetBrowsingContext() : nullptr;
  RefPtr<BrowsingContext> openerBC = GetOpener();
  MOZ_DIAGNOSTIC_ASSERT(!openerBC || openerBC->Group() == Group());

  // Enforce dom.disable_window_flip (for non-chrome), but still allow the
  // window which opened us to raise us at times when popups are allowed
  // (bugs 355482 and 369306).
  bool canFocus = aCallerType == CallerType::System ||
                  !Preferences::GetBool("dom.disable_window_flip", true);
  if (!canFocus && openerBC == callerBC) {
    canFocus =
        (callerBC ? callerBC : this)
            ->RevisePopupAbuseLevel(PopupBlocker::GetPopupControlState()) <
        PopupBlocker::openBlocked;
  }

  // https://wicg.github.io/document-picture-in-picture/#focusing-the-opener-window
  // Allow the opener to get system focus if the PIP window has transient
  // activation
  if (!canFocus && IsTopContent()) {
    if (nsGlobalWindowInner* pipWindow = GetOpenedDocumentPiPWindow()) {
      if (WindowContext* wc = pipWindow->GetWindowContext()) {
        if (wc->ConsumeTransientUserGestureActivation()) {
          canFocus = true;
        }
      }
    }
  }

  bool isActive = false;
  if (XRE_IsParentProcess()) {
    CanonicalBrowsingContext* chromeTop = Canonical()->TopCrossChromeBoundary();
    nsCOMPtr<nsPIDOMWindowOuter> activeWindow = fm->GetActiveWindow();
    isActive = activeWindow == chromeTop->GetDOMWindow();
  } else {
    isActive = fm->GetActiveBrowsingContext() == Top();
  }

  return {canFocus, isActive};
}

void BrowsingContext::Focus(CallerType aCallerType, ErrorResult& aError) {
  // These checks need to happen before the RequestFrameFocus call, which
  // is why they are done in an untrusted process. If we wanted to enforce
  // these in the parent, we'd need to do the checks there _also_.
  // These should be kept in sync with nsGlobalWindowOuter::FocusOuter.

  auto [canFocus, isActive] = CanFocusCheck(aCallerType);

  if (!(canFocus || isActive)) {
    return;
  }

  // Permission check passed

  if (mEmbedderElement) {
    // Make the activeElement in this process update synchronously.
    nsContentUtils::RequestFrameFocus(*mEmbedderElement, true, aCallerType);
  }
  uint64_t actionId = nsFocusManager::GenerateFocusActionId();
  if (ContentChild* cc = ContentChild::GetSingleton()) {
    cc->SendWindowFocus(this, aCallerType, actionId);
  } else if (ContentParent* cp = Canonical()->GetContentParent()) {
    (void)cp->SendWindowFocus(this, aCallerType, actionId);
  }
}

bool BrowsingContext::CanBlurCheck(CallerType aCallerType) {
  // If dom.disable_window_flip == true, then content should not be allowed
  // to do blur (this would allow popunders, bug 369306)
  return aCallerType == CallerType::System ||
         !Preferences::GetBool("dom.disable_window_flip", true);
}

void BrowsingContext::Blur(CallerType aCallerType, ErrorResult& aError) {
  if (!CanBlurCheck(aCallerType)) {
    return;
  }

  if (ContentChild* cc = ContentChild::GetSingleton()) {
    cc->SendWindowBlur(this, aCallerType);
  } else if (ContentParent* cp = Canonical()->GetContentParent()) {
    (void)cp->SendWindowBlur(this, aCallerType);
  }
}

Nullable<WindowProxyHolder> BrowsingContext::GetWindow() {
  if (XRE_IsParentProcess() && !IsInProcess()) {
    return nullptr;
  }
  return WindowProxyHolder(this);
}

Nullable<WindowProxyHolder> BrowsingContext::GetTop(ErrorResult& aError) {
  if (mIsDiscarded) {
    return nullptr;
  }

  // We never return null or throw an error, but the implementation in
  // nsGlobalWindow does and we need to use the same signature.
  return WindowProxyHolder(Top());
}

void BrowsingContext::GetOpener(JSContext* aCx,
                                JS::MutableHandle<JS::Value> aOpener,
                                ErrorResult& aError) const {
  RefPtr<BrowsingContext> opener = GetOpener();
  if (!opener) {
    aOpener.setNull();
    return;
  }

  if (!ToJSValue(aCx, WindowProxyHolder(opener), aOpener)) {
    aError.NoteJSContextException(aCx);
  }
}

// We never throw an error, but the implementation in nsGlobalWindow does and
// we need to use the same signature.
Nullable<WindowProxyHolder> BrowsingContext::GetParent(ErrorResult& aError) {
  if (mIsDiscarded) {
    return nullptr;
  }

  if (GetParent()) {
    return WindowProxyHolder(GetParent());
  }
  return WindowProxyHolder(this);
}

void BrowsingContext::PostMessageMoz(JSContext* aCx,
                                     JS::Handle<JS::Value> aMessage,
                                     const nsAString& aTargetOrigin,
                                     const Sequence<JSObject*>& aTransfer,
                                     nsIPrincipal& aSubjectPrincipal,
                                     ErrorResult& aError) {
  if (mIsDiscarded) {
    return;
  }

  RefPtr<BrowsingContext> sourceBc;
  PostMessageData data;
  data.targetOrigin() = aTargetOrigin;
  data.subjectPrincipal() = &aSubjectPrincipal;
  RefPtr<nsGlobalWindowInner> callerInnerWindow;
  nsAutoCString scriptLocation;
  // We don't need to get the caller's agentClusterId since that is used for
  // checking whether it's okay to sharing memory (and it's not allowed to share
  // memory cross processes)
  if (!nsGlobalWindowOuter::GatherPostMessageData(
          aCx, aTargetOrigin, getter_AddRefs(sourceBc), data.origin(),
          getter_AddRefs(data.targetOriginURI()),
          getter_AddRefs(data.callerPrincipal()),
          getter_AddRefs(callerInnerWindow), getter_AddRefs(data.callerURI()),
          /* aCallerAgentClusterId */ nullptr, &scriptLocation, aError)) {
    return;
  }
  if (sourceBc && sourceBc->IsDiscarded()) {
    return;
  }
  data.source() = sourceBc;
  data.isFromPrivateWindow() =
      callerInnerWindow &&
      nsScriptErrorBase::ComputeIsFromPrivateWindow(callerInnerWindow);
  data.innerWindowId() = callerInnerWindow ? callerInnerWindow->WindowID() : 0;
  data.scriptLocation() = std::move(scriptLocation);
  JS::Rooted<JS::Value> transferArray(aCx);
  aError = nsContentUtils::CreateJSValueFromSequenceOfObject(aCx, aTransfer,
                                                             &transferArray);
  if (NS_WARN_IF(aError.Failed())) {
    return;
  }

  JS::CloneDataPolicy clonePolicy;
  if (callerInnerWindow && callerInnerWindow->IsSharedMemoryAllowed()) {
    clonePolicy.allowSharedMemoryObjects();
  }

  // We will see if the message is required to be in the same process or it can
  // be in the different process after Write().
  auto message = MakeRefPtr<ipc::StructuredCloneData>(
      StructuredCloneHolder::StructuredCloneScope::UnknownDestination,
      StructuredCloneHolder::TransferringSupported);
  message->Write(aCx, aMessage, transferArray, clonePolicy, aError);
  if (NS_WARN_IF(aError.Failed())) {
    return;
  }

  // The clone scope gets set when we write the message data based on the
  // requirements of that data that we're writing.
  // If the message data contains a shared memory object, then CloneScope
  // would return SameProcess. Otherwise, it returns DifferentProcess.
  if (message->CloneScope() !=
      StructuredCloneHolder::StructuredCloneScope::DifferentProcess) {
    MOZ_ASSERT(message->CloneScope() ==
               StructuredCloneHolder::StructuredCloneScope::SameProcess);

    message = nullptr;

    nsContentUtils::ReportToConsole(
        nsIScriptError::warningFlag, "DOM Window"_ns,
        callerInnerWindow ? callerInnerWindow->GetDocument() : nullptr,
        PropertiesFile::DOM_PROPERTIES,
        "PostMessageSharedMemoryObjectToCrossOriginWarning");
  }

  if (ContentChild* cc = ContentChild::GetSingleton()) {
    cc->SendWindowPostMessage(this, message, data);
  } else if (ContentParent* cp = Canonical()->GetContentParent()) {
    (void)cp->SendWindowPostMessage(this, message, data);
  }
}

void BrowsingContext::PostMessageMoz(JSContext* aCx,
                                     JS::Handle<JS::Value> aMessage,
                                     const WindowPostMessageOptions& aOptions,
                                     nsIPrincipal& aSubjectPrincipal,
                                     ErrorResult& aError) {
  PostMessageMoz(aCx, aMessage, aOptions.mTargetOrigin, aOptions.mTransfer,
                 aSubjectPrincipal, aError);
}

void BrowsingContext::SendCommitTransaction(ContentParent* aParent,
                                            const BaseTransaction& aTxn,
                                            uint64_t aEpoch) {
  (void)aParent->SendCommitBrowsingContextTransaction(this, aTxn, aEpoch);
}

void BrowsingContext::SendCommitTransaction(ContentChild* aChild,
                                            const BaseTransaction& aTxn,
                                            uint64_t aEpoch) {
  aChild->SendCommitBrowsingContextTransaction(this, aTxn, aEpoch);
}

BrowsingContext::IPCInitializer BrowsingContext::GetIPCInitializer() {
  MOZ_DIAGNOSTIC_ASSERT(mEverAttached);
  MOZ_DIAGNOSTIC_ASSERT(mType == Type::Content);

  IPCInitializer init;
  init.mId = Id();
  init.mParentId = mParentWindow ? mParentWindow->Id() : 0;
  init.mWindowless = mWindowless;
  init.mUseRemoteTabs = mUseRemoteTabs;
  init.mUseRemoteSubframes = mUseRemoteSubframes;
  init.mCreatedDynamically = mCreatedDynamically;
  init.mChildOffset = mChildOffset;
  init.mOriginAttributes = mOriginAttributes;
  if (mChildSessionHistory) {
    init.mSessionHistoryIndex = mChildSessionHistory->Index();
    init.mSessionHistoryCount = mChildSessionHistory->Count();
  }
  init.mRequestContextId = mRequestContextId;
  init.mFields = mFields.RawValues();
  return init;
}

already_AddRefed<WindowContext> BrowsingContext::IPCInitializer::GetParent() {
  RefPtr<WindowContext> parent;
  if (mParentId != 0) {
    parent = WindowContext::GetById(mParentId);
    MOZ_RELEASE_ASSERT(parent);
  }
  return parent.forget();
}

already_AddRefed<BrowsingContext> BrowsingContext::IPCInitializer::GetOpener() {
  RefPtr<BrowsingContext> opener;
  if (GetOpenerId() != 0) {
    opener = BrowsingContext::Get(GetOpenerId());
    MOZ_RELEASE_ASSERT(opener);
  }
  return opener.forget();
}

void BrowsingContext::StartDelayedAutoplayMediaComponents() {
  if (!mDocShell) {
    return;
  }
  AUTOPLAY_LOG("%s : StartDelayedAutoplayMediaComponents for bc 0x%08" PRIx64,
               XRE_IsParentProcess() ? "Parent" : "Child", Id());
  mDocShell->StartDelayedAutoplayMediaComponents();
}

nsresult BrowsingContext::ResetGVAutoplayRequestStatus() {
  MOZ_ASSERT(IsTop(),
             "Should only set GVAudibleAutoplayRequestStatus in the top-level "
             "browsing context");

  Transaction txn;
  txn.SetGVAudibleAutoplayRequestStatus(GVAutoplayRequestStatus::eUNKNOWN);
  txn.SetGVInaudibleAutoplayRequestStatus(GVAutoplayRequestStatus::eUNKNOWN);
  return txn.Commit(this);
}

template <typename Callback>
void BrowsingContext::WalkPresContexts(Callback&& aCallback) {
  PreOrderWalk([&](BrowsingContext* aContext) {
    if (nsIDocShell* shell = aContext->GetDocShell()) {
      if (RefPtr pc = shell->GetPresContext()) {
        aCallback(pc.get());
      }
    }
  });
}

void BrowsingContext::PresContextAffectingFieldChanged() {
  WalkPresContexts([&](nsPresContext* aPc) {
    aPc->RecomputeBrowsingContextDependentData();
  });
}

void BrowsingContext::ActivenessChanged(bool aIsActive) {
  // This method should not be called if the activeness changed due to an
  // ancestor BC. Rather, it's to update dependents about ExplicitActive or
  // Document PiP.
  MOZ_ASSERT(IsTop(),
             "Currently, only top level activeness can change explicitly");
  MOZ_ASSERT(IsActive() == aIsActive, "Activeness should have already changed");

  Group()->UpdateToplevelsSuspendedIfNeeded();
  if (XRE_IsParentProcess()) {
    if (BrowserParent* bp = Canonical()->GetBrowserParent()) {
      bp->RecomputeProcessPriority();
#if defined(XP_WIN) && defined(ACCESSIBILITY)
      if (a11y::Compatibility::IsDolphin()) {
        // update active accessible documents on windows
        if (a11y::DocAccessibleParent* tabDoc =
                bp->GetTopLevelDocAccessible()) {
          HWND window = tabDoc->GetEmulatedWindowHandle();
          MOZ_ASSERT(window);
          if (window) {
            if (aIsActive) {
              a11y::nsWinUtils::ShowNativeWindow(window);
            } else {
              a11y::nsWinUtils::HideNativeWindow(window);
            }
          }
        }
      }
#endif
    }

    // NOTE(emilio): Ideally we'd want to reuse the ExplicitActiveStatus::None
    // set-up, but that's non-trivial to do because in content processes we
    // can't access the top-cross-chrome-boundary bc.
    auto manageTopDescendant = [&](auto* aChild) {
      if (!aChild->ManuallyManagesActiveness()) {
        aChild->SetIsActiveInternal(aIsActive, IgnoreErrors());
        if (BrowserParent* bp = aChild->GetBrowserParent()) {
          bp->SetRenderLayers(aIsActive);
        }
      }
      return CallState::Continue;
    };
    Canonical()->CallOnTopDescendants(
        manageTopDescendant,
        CanonicalBrowsingContext::TopDescendantKind::NonNested);
  }

  PreOrderWalk([&](BrowsingContext* aContext) {
    if (nsCOMPtr<nsIDocShell> ds = aContext->GetDocShell()) {
      if (auto* bc = BrowserChild::GetFrom(ds)) {
        bc->UpdateVisibility();
      }
      nsDocShell::Cast(ds)->ActivenessMaybeChanged();
    }
  });

  if (XRE_IsParentProcess()) {
    if (nsCOMPtr<nsIObserverService> observerService =
            mozilla::services::GetObserverService()) {
      observerService->NotifyObservers(
          ToSupports(this), "browsing-context-active-change", nullptr);
    }
  }
}

void BrowsingContext::DidSet(FieldIndex<IDX_SessionStoreEpoch>,
                             uint32_t aOldValue) {
  if (!mCurrentWindowContext) {
    return;
  }
  SessionStoreChild* sessionStoreChild =
      SessionStoreChild::From(mCurrentWindowContext->GetWindowGlobalChild());
  if (!sessionStoreChild) {
    return;
  }

  sessionStoreChild->SetEpoch(GetSessionStoreEpoch());
}

void BrowsingContext::DidSet(FieldIndex<IDX_GVAudibleAutoplayRequestStatus>) {
  MOZ_ASSERT(IsTop(),
             "Should only set GVAudibleAutoplayRequestStatus in the top-level "
             "browsing context");
}

void BrowsingContext::DidSet(FieldIndex<IDX_GVInaudibleAutoplayRequestStatus>) {
  MOZ_ASSERT(IsTop(),
             "Should only set GVAudibleAutoplayRequestStatus in the top-level "
             "browsing context");
}

bool BrowsingContext::CanSet(FieldIndex<IDX_ControlsDocumentPiP>, bool,
                             ContentParent* aSource) {
  const bool setByOwner =
      aSource ? Canonical()->IsOwnedByProcess(aSource->ChildID())
              : IsInProcess();
  return IsTopContent() && setByOwner;
}

void BrowsingContext::DidSet(FieldIndex<IDX_ControlsDocumentPiP>,
                             bool aOldValue) {
  const bool controlsPiP = GetControlsDocumentPiP();
  MOZ_ASSERT(IsTopContent());

  if (controlsPiP == aOldValue) {
    return;
  }

  if (XRE_IsParentProcess()) {
    CanonicalBrowsingContext* chromeTopBC =
        Canonical()->TopCrossChromeBoundary();

    if (controlsPiP) {
      chromeTopBC->IncrementDocumentPiPWindowCount();
    } else {
      chromeTopBC->DecrementDocumentPiPWindowCount();
    }
  } else {
    // Document PiP keeps PresShell active
    RefPtr<PresShell> presShell =
        mDocShell ? mDocShell->GetPresShell() : nullptr;
    if (presShell) {
      presShell->ActivenessMaybeChanged();
    }
  }

  const bool isActive = IsActive();
  const bool wasActive = [&] {
    if (aOldValue) {
      return true;
    }
    if (GetExplicitActive() != ExplicitActiveStatus::None) {
      return GetExplicitActive() == ExplicitActiveStatus::Active;
    }
    return GetParent() && GetParent()->IsActive();
  }();

  if (isActive != wasActive) {
    ActivenessChanged(isActive);
  }
}

bool BrowsingContext::CanSet(FieldIndex<IDX_ExplicitActive>,
                             const ExplicitActiveStatus&,
                             ContentParent* aSource) {
  return XRE_IsParentProcess() && IsTop() && !aSource;
}

void BrowsingContext::DidSet(FieldIndex<IDX_ExplicitActive>,
                             ExplicitActiveStatus aOldValue) {
  MOZ_ASSERT(IsTop());

  const bool isActive = IsActive();
  const bool wasActive = [&] {
    if (GetControlsDocumentPiP()) {
      return true;
    }
    if (aOldValue != ExplicitActiveStatus::None) {
      return aOldValue == ExplicitActiveStatus::Active;
    }
    return GetParent() && GetParent()->IsActive();
  }();

  if (isActive == wasActive) {
    return;
  }

  ActivenessChanged(isActive);
}

bool BrowsingContext::CanSet(FieldIndex<IDX_InRDMPane>, const bool&,
                             ContentParent* aSource) {
  return XRE_IsParentProcess() && IsTop() && !aSource;
}

void BrowsingContext::DidSet(FieldIndex<IDX_InRDMPane>, bool aOldValue) {
  MOZ_ASSERT(IsTop(),
             "Should only set InRDMPane in the top-level browsing context");
  if (GetInRDMPane() == aOldValue) {
    return;
  }

  // Reset screen orientation override when disabling RDM.
  if (!GetInRDMPane()) {
    ResetOrientationOverride();
  }

  PresContextAffectingFieldChanged();
}

void BrowsingContext::DidSet(FieldIndex<IDX_HasOrientationOverride>,
                             bool aOldValue) {
  bool hasOrientationOverride = GetHasOrientationOverride();
  OrientationType type = GetCurrentOrientationType();
  float angle = GetCurrentOrientationAngle();

  PreOrderWalk([&](BrowsingContext* aBrowsingContext) {
    if (RefPtr<WindowContext> windowContext =
            aBrowsingContext->GetCurrentWindowContext()) {
      if (nsCOMPtr<nsPIDOMWindowInner> window =
              windowContext->GetInnerWindow()) {
        ScreenOrientation* orientation =
            nsGlobalWindowInner::Cast(window)->Screen()->Orientation();

        float screenOrientationAngle =
            orientation->DeviceAngle(CallerType::System);
        OrientationType screenOrientationType =
            orientation->DeviceType(CallerType::System);

        bool overrideIsDifferentThanDevice =
            screenOrientationType != type || screenOrientationAngle != angle;

        // Reset orientation override.
        if (!hasOrientationOverride && aOldValue) {
          (void)aBrowsingContext->SetCurrentOrientation(screenOrientationType,
                                                        screenOrientationAngle);
        } else if (!aBrowsingContext->IsTop()) {
          // Sync orientation override in the existing frames.
          (void)aBrowsingContext->SetCurrentOrientation(type, angle);
        }

        orientation->MaybeDispatchEventsForOverride(
            aBrowsingContext, aOldValue, overrideIsDifferentThanDevice);
      }
    }
  });
}

void BrowsingContext::DidSet(FieldIndex<IDX_ForceDesktopViewport>,
                             bool aOldValue) {
  MOZ_ASSERT(IsTop(), "Should only set in the top-level browsing context");
  if (ForceDesktopViewport() == aOldValue) {
    return;
  }
  PresContextAffectingFieldChanged();
  if (nsIDocShell* shell = GetDocShell()) {
    if (RefPtr ps = shell->GetPresShell()) {
      ps->MaybeRecreateMobileViewportManager(/* aAfterInitialization= */ true);
    }
  }
}

bool BrowsingContext::CanSet(FieldIndex<IDX_PageAwakeRequestCount>,
                             uint32_t aNewValue, ContentParent* aSource) {
  return IsTop() && XRE_IsParentProcess() && !aSource;
}

void BrowsingContext::DidSet(FieldIndex<IDX_PageAwakeRequestCount>,
                             uint32_t aOldValue) {
  if (!IsTop() || aOldValue == GetPageAwakeRequestCount()) {
    return;
  }
  Group()->UpdateToplevelsSuspendedIfNeeded();
}

auto BrowsingContext::CanSet(FieldIndex<IDX_AllowJavascript>, bool aValue,
                             ContentParent* aSource) -> CanSetResult {
  return XRE_IsParentProcess() && !aSource ? CanSetResult::Allow
                                           : CanSetResult::Deny;
}

void BrowsingContext::DidSet(FieldIndex<IDX_AllowJavascript>, bool aOldValue) {
  RecomputeCanExecuteScripts();
}

void BrowsingContext::RecomputeCanExecuteScripts() {
  const bool old = mCanExecuteScripts;
  if (!AllowJavascript()) {
    // Scripting has been explicitly disabled on our BrowsingContext.
    mCanExecuteScripts = false;
  } else if (GetParentWindowContext()) {
    // Otherwise, inherit parent.
    mCanExecuteScripts = GetParentWindowContext()->CanExecuteScripts();
  } else {
    // Otherwise, we're the root of the tree, and we haven't explicitly disabled
    // script. Allow.
    mCanExecuteScripts = true;
  }

  if (old != mCanExecuteScripts) {
    for (WindowContext* wc : GetWindowContexts()) {
      wc->RecomputeCanExecuteScripts();
    }
  }
}

bool BrowsingContext::InactiveForSuspend() const {
  if (!StaticPrefs::dom_suspend_inactive_enabled()) {
    return false;
  }
  // We should suspend a page only when it's inactive and doesn't have any awake
  // request that is used to prevent page from being suspended because web page
  // might still need to run their script. Eg. waiting for media keys to resume
  // media, playing web audio, waiting in a video call conference room.
  return !IsActive() && GetPageAwakeRequestCount() == 0;
}

bool BrowsingContext::CanSet(FieldIndex<IDX_TouchEventsOverrideInternal>,
                             dom::TouchEventsOverride, ContentParent* aSource) {
  return XRE_IsParentProcess() && !aSource;
}

void BrowsingContext::DidSet(FieldIndex<IDX_TouchEventsOverrideInternal>,
                             dom::TouchEventsOverride&& aOldValue) {
  if (GetTouchEventsOverrideInternal() == aOldValue) {
    return;
  }
  WalkPresContexts([&](nsPresContext* aPc) {
    aPc->MediaFeatureValuesChanged(
        {MediaFeatureChangeReason::SystemMetricsChange},
        // We're already iterating through sub documents, so we don't need to
        // propagate the change again.
        MediaFeatureChangePropagation::JustThisDocument);
  });
}

void BrowsingContext::DidSet(FieldIndex<IDX_EmbedderColorSchemes>,
                             EmbedderColorSchemes&& aOldValue) {
  if (GetEmbedderColorSchemes() == aOldValue) {
    return;
  }
  PresContextAffectingFieldChanged();
}

void BrowsingContext::DidSet(FieldIndex<IDX_PrefersColorSchemeOverride>,
                             dom::PrefersColorSchemeOverride aOldValue) {
  MOZ_ASSERT(IsTop());
  if (PrefersColorSchemeOverride() == aOldValue) {
    return;
  }
  PresContextAffectingFieldChanged();
}

void BrowsingContext::DidSet(FieldIndex<IDX_ForcedColorsOverride>,
                             dom::ForcedColorsOverride aOldValue) {
  MOZ_ASSERT(IsTop());
  if (ForcedColorsOverride() == aOldValue) {
    return;
  }
  PresContextAffectingFieldChanged();
}

void BrowsingContext::DidSet(FieldIndex<IDX_PrefersReducedMotionOverride>,
                             dom::PrefersReducedMotionOverride aOldValue) {
  MOZ_ASSERT(IsTop());
  if (PrefersReducedMotionOverride() == aOldValue) {
    return;
  }

  WalkPresContexts([&](nsPresContext* aPc) {
    aPc->MediaFeatureValuesChanged(
        {MediaFeatureChangeReason::PreferenceChange},
        // We're already iterating through sub documents, so we don't need to
        // propagate the change again.
        MediaFeatureChangePropagation::JustThisDocument);
  });
}

void BrowsingContext::DidSet(FieldIndex<IDX_AnimationsPlayBackRateMultiplier>,
                             double aOldValue) {
  MOZ_ASSERT(IsTop());
  if (AnimationsPlayBackRateMultiplier() == aOldValue) {
    return;
  }
  PresContextAffectingFieldChanged();
}

void BrowsingContext::DidSet(FieldIndex<IDX_LanguageOverride>,
                             nsCString&& aOldValue) {
  MOZ_ASSERT(IsTop());

  const nsCString& languageOverride = GetLanguageOverride();

  workerinternals::RuntimeService* rts =
      workerinternals::RuntimeService::GetService();

  PreOrderWalk([&](BrowsingContext* aBrowsingContext) {
    if (RefPtr<WindowContext> windowContext =
            aBrowsingContext->GetCurrentWindowContext()) {
      if (nsCOMPtr<nsPIDOMWindowInner> window =
              windowContext->GetInnerWindow()) {
        JSObject* global =
            nsGlobalWindowInner::Cast(window)->GetGlobalJSObject();
        JS::Realm* realm = JS::GetObjectRealmOrNull(global);

        if (languageOverride.IsEmpty()) {
          JS::SetRealmLocaleOverride(realm, nullptr);
        } else {
          JS::SetRealmLocaleOverride(
              realm, PromiseFlatCString(languageOverride).get());
        }

        if (Navigator* navigator = window->Navigator()) {
          navigator->ClearLanguageCache();
        }

        if (rts) {
          rts->UpdateWorkersLanguageOverride(*window, languageOverride);
        }

        nsGlobalWindowInner::Cast(window)->UpdateSharedWorkersLanguageOverride(
            languageOverride);
      }
    }
  });
}

void BrowsingContext::DidSet(FieldIndex<IDX_MediumOverride>,
                             nsString&& aOldValue) {
  MOZ_ASSERT(IsTop());
  if (GetMediumOverride() == aOldValue) {
    return;
  }
  PresContextAffectingFieldChanged();
}

void BrowsingContext::DidSet(FieldIndex<IDX_DisplayMode>,
                             enum DisplayMode aOldValue) {
  MOZ_ASSERT(IsTop());

  if (GetDisplayMode() == aOldValue) {
    return;
  }

  WalkPresContexts([&](nsPresContext* aPc) {
    aPc->MediaFeatureValuesChanged(
        {MediaFeatureChangeReason::DisplayModeChange},
        // We're already iterating through sub documents, so we don't need
        // to propagate the change again.
        //
        // Images and other resources don't change their display-mode
        // evaluation, display-mode is a property of the browsing context.
        MediaFeatureChangePropagation::JustThisDocument);
  });
}

void BrowsingContext::DidSet(FieldIndex<IDX_Muted>) {
  MOZ_ASSERT(IsTop(), "Set muted flag on non top-level context!");
  USER_ACTIVATION_LOG("Set audio muted %d for %s browsing context 0x%08" PRIx64,
                      GetMuted(), XRE_IsParentProcess() ? "Parent" : "Child",
                      Id());
  PreOrderWalk([&](BrowsingContext* aContext) {
    nsPIDOMWindowOuter* win = aContext->GetDOMWindow();
    if (win) {
      win->RefreshMediaElementsVolume();
    }
  });
}

bool BrowsingContext::CanSet(FieldIndex<IDX_IsAppTab>, const bool& aValue,
                             ContentParent* aSource) {
  return XRE_IsParentProcess() && !aSource && IsTop();
}

bool BrowsingContext::CanSet(FieldIndex<IDX_HasSiblings>, const bool& aValue,
                             ContentParent* aSource) {
  return XRE_IsParentProcess() && !aSource && IsTop();
}

bool BrowsingContext::CanSet(FieldIndex<IDX_ShouldDelayMediaFromStart>,
                             const bool& aValue, ContentParent* aSource) {
  return IsTop();
}

void BrowsingContext::DidSet(FieldIndex<IDX_ShouldDelayMediaFromStart>,
                             bool aOldValue) {
  MOZ_ASSERT(IsTop(), "Set attribute on non top-level context!");
  if (aOldValue == GetShouldDelayMediaFromStart()) {
    return;
  }
  if (!GetShouldDelayMediaFromStart()) {
    PreOrderWalk([&](BrowsingContext* aContext) {
      if (nsPIDOMWindowOuter* win = aContext->GetDOMWindow()) {
        win->ActivateMediaComponents();
      }
    });
  }
}

bool BrowsingContext::CanSet(FieldIndex<IDX_OverrideDPPX>, const float& aValue,
                             ContentParent* aSource) {
  return XRE_IsParentProcess() && !aSource && IsTop();
}

void BrowsingContext::DidSet(FieldIndex<IDX_OverrideDPPX>, float aOldValue) {
  MOZ_ASSERT(IsTop());
  if (GetOverrideDPPX() == aOldValue) {
    return;
  }
  PresContextAffectingFieldChanged();
}

void BrowsingContext::SetCustomUserAgent(const nsAString& aUserAgent,
                                         ErrorResult& aRv) {
  Top()->SetUserAgentOverride(aUserAgent, aRv);
}

nsresult BrowsingContext::SetCustomUserAgent(const nsAString& aUserAgent) {
  return Top()->SetUserAgentOverride(aUserAgent);
}

void BrowsingContext::DidSet(FieldIndex<IDX_UserAgentOverride>) {
  MOZ_ASSERT(IsTop());

  PreOrderWalk([&](BrowsingContext* aContext) {
    nsIDocShell* shell = aContext->GetDocShell();
    if (shell) {
      shell->ClearCachedUserAgent();
    }

    if (nsCOMPtr<Document> doc = aContext->GetExtantDocument()) {
      if (nsCOMPtr<nsIHttpChannel> httpChannel =
              do_QueryInterface(doc->GetChannel())) {
        (void)httpChannel->SetIsUserAgentHeaderOutdated(true);
      }
    }
  });
}

void BrowsingContext::DidSet(FieldIndex<IDX_IsSyntheticDocumentContainer>) {
  if (WindowContext* parentWindowContext = GetParentWindowContext()) {
    parentWindowContext->UpdateChildSynthetic(
        this, GetIsSyntheticDocumentContainer());
  }
}

void BrowsingContext::SetCustomPlatform(const nsAString& aPlatform,
                                        ErrorResult& aRv) {
  Top()->SetPlatformOverride(aPlatform, aRv);
}

void BrowsingContext::DidSet(FieldIndex<IDX_PlatformOverride>) {
  MOZ_ASSERT(IsTop());

  PreOrderWalk([&](BrowsingContext* aContext) {
    nsIDocShell* shell = aContext->GetDocShell();
    if (shell) {
      shell->ClearCachedPlatform();
    }
  });
}

auto BrowsingContext::LegacyRevertIfNotOwningOrParentProcess(
    ContentParent* aSource) -> CanSetResult {
  if (aSource) {
    MOZ_ASSERT(XRE_IsParentProcess());

    if (!Canonical()->IsOwnedByProcess(aSource->ChildID())) {
      return CanSetResult::Revert;
    }
  } else if (!IsInProcess() && !XRE_IsParentProcess()) {
    // Don't allow this to be set from content processes that
    // don't own the BrowsingContext.
    return CanSetResult::Deny;
  }

  return CanSetResult::Allow;
}

bool BrowsingContext::CanSet(FieldIndex<IDX_IsActiveBrowserWindowInternal>,
                             const bool& aValue, ContentParent* aSource) {
  // Should only be set in the parent process.
  return XRE_IsParentProcess() && !aSource && IsTop();
}

void BrowsingContext::DidSet(FieldIndex<IDX_IsActiveBrowserWindowInternal>,
                             bool aOldValue) {
  bool isActivateEvent = GetIsActiveBrowserWindowInternal();
  // The browser window containing this context has changed
  // activation state so update window inactive document states
  // for all in-process documents.
  PreOrderWalk([isActivateEvent](BrowsingContext* aContext) {
    if (RefPtr<Document> doc = aContext->GetExtantDocument()) {
      doc->UpdateDocumentStates(DocumentState::WINDOW_INACTIVE, true);

      RefPtr<nsPIDOMWindowInner> win = doc->GetInnerWindow();
      if (win) {
        RefPtr<MediaDevices> devices;
        if (isActivateEvent && (devices = win->GetExtantMediaDevices())) {
          devices->BrowserWindowBecameActive();
        }

        if (XRE_IsContentProcess() &&
            (!aContext->GetParent() || !aContext->GetParent()->IsInProcess())) {
          // Send the inner window an activate/deactivate event if
          // the context is the top of a sub-tree of in-process
          // contexts.
          nsContentUtils::DispatchEventOnlyToChrome(
              doc, nsGlobalWindowInner::Cast(win),
              isActivateEvent ? u"activate"_ns : u"deactivate"_ns,
              CanBubble::eYes, Cancelable::eYes, nullptr);
        }
      }
    }
  });
}

bool BrowsingContext::CanSet(FieldIndex<IDX_OpenerPolicy>,
                             nsILoadInfo::CrossOriginOpenerPolicy aPolicy,
                             ContentParent* aSource) {
  // A potentially cross-origin isolated BC can't change opener policy, nor can
  // a BC become potentially cross-origin isolated. An unchanged policy is
  // always OK.
  return GetOpenerPolicy() == aPolicy ||
         (GetOpenerPolicy() !=
              nsILoadInfo::
                  OPENER_POLICY_SAME_ORIGIN_EMBEDDER_POLICY_REQUIRE_CORP &&
          aPolicy !=
              nsILoadInfo::
                  OPENER_POLICY_SAME_ORIGIN_EMBEDDER_POLICY_REQUIRE_CORP);
}

auto BrowsingContext::CanSet(FieldIndex<IDX_AllowContentRetargeting>,
                             const bool& aAllowContentRetargeting,
                             ContentParent* aSource) -> CanSetResult {
  return LegacyRevertIfNotOwningOrParentProcess(aSource);
}

auto BrowsingContext::CanSet(FieldIndex<IDX_AllowContentRetargetingOnChildren>,
                             const bool& aAllowContentRetargetingOnChildren,
                             ContentParent* aSource) -> CanSetResult {
  return LegacyRevertIfNotOwningOrParentProcess(aSource);
}

bool BrowsingContext::CanSet(FieldIndex<IDX_FullscreenAllowedByOwner>,
                             const bool& aAllowed, ContentParent* aSource) {
  return CheckOnlyEmbedderCanSet(aSource);
}

bool BrowsingContext::CanSet(FieldIndex<IDX_UseErrorPages>,
                             const bool& aUseErrorPages,
                             ContentParent* aSource) {
  return CheckOnlyEmbedderCanSet(aSource);
}

TouchEventsOverride BrowsingContext::TouchEventsOverride() const {
  for (const auto* bc = this; bc; bc = bc->GetParent()) {
    auto tev = bc->GetTouchEventsOverrideInternal();
    if (tev != dom::TouchEventsOverride::None) {
      return tev;
    }
  }
  return dom::TouchEventsOverride::None;
}

bool BrowsingContext::TargetTopLevelLinkClicksToBlank() const {
  return Top()->GetTargetTopLevelLinkClicksToBlankInternal();
}

// We map `watchedByDevTools` WebIDL attribute to `watchedByDevToolsInternal`
// BC field. And we map it to the top level BrowsingContext.
bool BrowsingContext::WatchedByDevTools() {
  return Top()->GetWatchedByDevToolsInternal();
}

// Enforce that the watchedByDevTools BC field can only be set on the top level
// Browsing Context.
bool BrowsingContext::CanSet(FieldIndex<IDX_WatchedByDevToolsInternal>,
                             const bool& aWatchedByDevTools,
                             ContentParent* aSource) {
  return IsTop();
}
void BrowsingContext::SetWatchedByDevTools(bool aWatchedByDevTools,
                                           ErrorResult& aRv) {
  if (!IsTop()) {
    aRv.ThrowInvalidModificationError(
        "watchedByDevTools can only be set on top BrowsingContext");
    return;
  }
  SetWatchedByDevToolsInternal(aWatchedByDevTools, aRv);
}

RefPtr<GeolocationService> BrowsingContext::GetGeolocationServiceOverride() {
  // Override can be set only to the top-level browsing context,
  // but when the geolocation coordinates are requested for iframe,
  // we should return the override which is set for its top-level context.
  return Top()->mGeolocationServiceOverride;
}

void BrowsingContext::SetGeolocationServiceOverride(
    const Optional<nsIDOMGeoPosition*>& aGeolocationOverride) {
  MOZ_ASSERT(
      IsTop(),
      "Should only set GeolocationServiceOverride in the top browsing context");
  if (aGeolocationOverride.WasPassed()) {
    if (!mGeolocationServiceOverride) {
      mGeolocationServiceOverride = MakeRefPtr<GeolocationService>();
      mGeolocationServiceOverride->Init();
    }
    mGeolocationServiceOverride->Update(aGeolocationOverride.Value());
  } else if (RefPtr<GeolocationService> serviceOverride =
                 mGeolocationServiceOverride.forget()) {
    // Create an original service and move the locators.
    RefPtr<GeolocationService> service =
        GeolocationService::GetGeolocationService();
    serviceOverride->MoveLocators(service);
  }
}

void BrowsingContext::DidSet(FieldIndex<IDX_TimezoneOverride>,
                             nsString&& aOldValue) {
  MOZ_ASSERT(IsTop());

  const nsString& timezoneOverride = GetTimezoneOverride();

  PreOrderWalk([&](BrowsingContext* aBrowsingContext) {
    if (RefPtr<WindowContext> windowContext =
            aBrowsingContext->GetCurrentWindowContext()) {
      if (nsCOMPtr<nsPIDOMWindowInner> window =
              windowContext->GetInnerWindow()) {
        JSObject* global =
            nsGlobalWindowInner::Cast(window)->GetGlobalJSObject();
        JS::Realm* realm = JS::GetObjectRealmOrNull(global);

        if (timezoneOverride.IsEmpty()) {
          JS::SetRealmTimezoneOverride(realm, nullptr);
        } else {
          JS::SetRealmTimezoneOverride(
              realm, NS_ConvertUTF16toUTF8(timezoneOverride).get());
        }

        UpdateTimezoneOverrideForWorkers(*window, timezoneOverride);
        nsGlobalWindowInner::Cast(window)->UpdateSharedWorkerTimezoneOverride(
            timezoneOverride);
      }
    }
  });
}

auto BrowsingContext::CanSet(FieldIndex<IDX_DefaultLoadFlags>,
                             const uint32_t& aDefaultLoadFlags,
                             ContentParent* aSource) -> CanSetResult {
  // Bug 1623565 - Are these flags only used by the debugger, which makes it
  // possible that this field can only be settable by the parent process?
  return LegacyRevertIfNotOwningOrParentProcess(aSource);
}

void BrowsingContext::DidSet(FieldIndex<IDX_DefaultLoadFlags>) {
  auto loadFlags = GetDefaultLoadFlags();
  if (GetDocShell()) {
    nsDocShell::Cast(GetDocShell())->SetLoadGroupDefaultLoadFlags(loadFlags);
  }

  if (XRE_IsParentProcess()) {
    PreOrderWalk([&](BrowsingContext* aContext) {
      if (aContext != this) {
        // Setting load flags on a discarded context has no effect.
        (void)aContext->SetDefaultLoadFlags(loadFlags);
      }
    });
  }
}

bool BrowsingContext::CanSet(FieldIndex<IDX_UseGlobalHistory>,
                             const bool& aUseGlobalHistory,
                             ContentParent* aSource) {
  // Should only be set in the parent process.
  //  return XRE_IsParentProcess() && !aSource;
  return true;
}

auto BrowsingContext::CanSet(FieldIndex<IDX_UserAgentOverride>,
                             const nsString& aUserAgent, ContentParent* aSource)
    -> CanSetResult {
  if (!IsTop()) {
    return CanSetResult::Deny;
  }

  return LegacyRevertIfNotOwningOrParentProcess(aSource);
}

auto BrowsingContext::CanSet(FieldIndex<IDX_PlatformOverride>,
                             const nsString& aPlatform, ContentParent* aSource)
    -> CanSetResult {
  if (!IsTop()) {
    return CanSetResult::Deny;
  }

  return LegacyRevertIfNotOwningOrParentProcess(aSource);
}

bool BrowsingContext::CheckOnlyEmbedderCanSet(ContentParent* aSource) {
  if (XRE_IsParentProcess()) {
    uint64_t childId = aSource ? aSource->ChildID() : 0;
    return Canonical()->IsEmbeddedInProcess(childId);
  }
  return mEmbeddedByThisProcess;
}

bool BrowsingContext::CanSet(FieldIndex<IDX_EmbedderInnerWindowId>,
                             const uint64_t& aValue, ContentParent* aSource) {
  // If we have a parent window, our embedder inner window ID must match it.
  if (mParentWindow) {
    return mParentWindow->Id() == aValue;
  }

  // For toplevel BrowsingContext instances, this value may only be set by the
  // parent process, or initialized to `0`.
  return CheckOnlyEmbedderCanSet(aSource);
}

bool BrowsingContext::CanSet(FieldIndex<IDX_EmbedderElementType>,
                             const Maybe<nsString>&, ContentParent* aSource) {
  return CheckOnlyEmbedderCanSet(aSource);
}

auto BrowsingContext::CanSet(FieldIndex<IDX_CurrentInnerWindowId>,
                             const uint64_t& aValue, ContentParent* aSource)
    -> CanSetResult {
  // Generally allow clearing this. We may want to be more precise about this
  // check in the future.
  if (aValue == 0) {
    return CanSetResult::Allow;
  }

  // We must have access to the specified context.
  RefPtr<WindowContext> window = WindowContext::GetById(aValue);
  if (!window || window->GetBrowsingContext() != this) {
    return CanSetResult::Deny;
  }

  if (aSource) {
    // If the sending process is no longer the current owner, revert
    MOZ_ASSERT(XRE_IsParentProcess());
    if (!Canonical()->IsOwnedByProcess(aSource->ChildID())) {
      return CanSetResult::Revert;
    }
  } else if (XRE_IsContentProcess() && !IsOwnedByProcess()) {
    return CanSetResult::Deny;
  }

  return CanSetResult::Allow;
}

bool BrowsingContext::CanSet(FieldIndex<IDX_ParentInitiatedNavigationEpoch>,
                             const uint64_t& aValue, ContentParent* aSource) {
  return XRE_IsParentProcess() && !aSource;
}

void BrowsingContext::DidSet(FieldIndex<IDX_CurrentInnerWindowId>) {
  RefPtr<WindowContext> prevWindowContext = mCurrentWindowContext.forget();
  mCurrentWindowContext = WindowContext::GetById(GetCurrentInnerWindowId());
  MOZ_ASSERT(
      !mCurrentWindowContext || mWindowContexts.Contains(mCurrentWindowContext),
      "WindowContext not registered?");

  // Clear our cached `children` value, to ensure that JS sees the up-to-date
  // value.
  BrowsingContext_Binding::ClearCachedChildrenValue(this);

  if (XRE_IsParentProcess()) {
    if (prevWindowContext != mCurrentWindowContext) {
      if (prevWindowContext) {
        prevWindowContext->Canonical()->DidBecomeCurrentWindowGlobal(false);
      }
      if (mCurrentWindowContext) {
        // We set a timer when we set the current inner window. This
        // will then flush the session storage to session store to
        // make sure that we don't miss to store session storage to
        // session store that is a result of navigation. This is due
        // to Bug 1700623. We wish to fix this in Bug 1711886, where
        // making sure to store everything would make this timer
        // unnecessary.
        Canonical()->MaybeScheduleSessionStoreUpdate();
        mCurrentWindowContext->Canonical()->DidBecomeCurrentWindowGlobal(true);
      }
    }
    BrowserParent::UpdateFocusFromBrowsingContext();
  }
}

bool BrowsingContext::CanSet(FieldIndex<IDX_IsPopupSpam>, const bool& aValue,
                             ContentParent* aSource) {
  // Ensure that we only mark a browsing context as popup spam once and never
  // unmark it.
  return aValue && !GetIsPopupSpam();
}

void BrowsingContext::DidSet(FieldIndex<IDX_IsPopupSpam>) {
  if (GetIsPopupSpam()) {
    PopupBlocker::RegisterOpenPopupSpam();
  }
}

bool BrowsingContext::CanSet(FieldIndex<IDX_MessageManagerGroup>,
                             const nsString& aMessageManagerGroup,
                             ContentParent* aSource) {
  // Should only be set in the parent process on toplevel.
  return XRE_IsParentProcess() && !aSource && IsTopContent();
}

bool BrowsingContext::CanSet(
    FieldIndex<IDX_OrientationLock>,
    const mozilla::hal::ScreenOrientation& aOrientationLock,
    ContentParent* aSource) {
  return IsTop();
}

bool BrowsingContext::IsLoading() {
  if (GetLoading()) {
    return true;
  }

  // If we're in the same process as the page, we're possibly just
  // updating the flag.
  nsIDocShell* shell = GetDocShell();
  if (shell) {
    Document* doc = shell->GetDocument();
    return doc && doc->GetReadyStateEnum() < Document::READYSTATE_COMPLETE;
  }

  return false;
}

void BrowsingContext::DidSet(FieldIndex<IDX_Loading>) {
  if (mFields.Get<IDX_Loading>()) {
    return;
  }

  while (!mDeprioritizedLoadRunner.isEmpty()) {
    nsCOMPtr<nsIRunnable> runner = mDeprioritizedLoadRunner.popFirst();
    NS_DispatchToCurrentThread(runner.forget());
  }

  if (IsTop()) {
    Group()->FlushPostMessageEvents();
  }
}

// Inform the Document for this context of the (potential) change in
// loading state
void BrowsingContext::DidSet(FieldIndex<IDX_AncestorLoading>) {
  nsPIDOMWindowOuter* outer = GetDOMWindow();
  if (!outer) {
    MOZ_LOG(gTimeoutDeferralLog, mozilla::LogLevel::Debug,
            ("DidSetAncestorLoading BC: %p -- No outer window", (void*)this));
    return;
  }
  Document* document = nsGlobalWindowOuter::Cast(outer)->GetExtantDoc();
  if (document) {
    MOZ_LOG(gTimeoutDeferralLog, mozilla::LogLevel::Debug,
            ("DidSetAncestorLoading BC: %p -- NotifyLoading(%d, %d, %d)",
             (void*)this, GetAncestorLoading(), document->GetReadyStateEnum(),
             document->GetReadyStateEnum()));
    document->NotifyLoading(GetAncestorLoading(), document->GetReadyStateEnum(),
                            document->GetReadyStateEnum());
  }
}

void BrowsingContext::DidSet(FieldIndex<IDX_AuthorStyleDisabledDefault>) {
  MOZ_ASSERT(IsTop(),
             "Should only set AuthorStyleDisabledDefault in the top "
             "browsing context");

  // We don't need to handle changes to this field, since PageStyleChild.sys.mjs
  // will respond to the PageStyle:Disable message in all content processes.
  //
  // But we store the state here on the top BrowsingContext so that the
  // docshell has somewhere to look for the current author style disabling
  // state when new iframes are inserted.
}

void BrowsingContext::DidSet(FieldIndex<IDX_TextZoom>, float aOldValue) {
  if (GetTextZoom() == aOldValue) {
    return;
  }

  if (IsInProcess()) {
    if (nsIDocShell* shell = GetDocShell()) {
      if (nsPresContext* pc = shell->GetPresContext()) {
        pc->RecomputeBrowsingContextDependentData();
      }
    }

    for (BrowsingContext* child : Children()) {
      // Setting text zoom on a discarded context has no effect.
      (void)child->SetTextZoom(GetTextZoom());
    }
  }

  if (IsTop() && XRE_IsParentProcess()) {
    if (Element* element = GetEmbedderElement()) {
      AsyncEventDispatcher::RunDOMEventWhenSafe(*element, u"TextZoomChange"_ns,
                                                CanBubble::eYes,
                                                ChromeOnlyDispatch::eYes);
    }
  }
}

// TODO(emilio): It'd be potentially nicer and cheaper to allow to set this only
// on the Top() browsing context, but there are a lot of tests that rely on
// zooming a subframe so...
void BrowsingContext::DidSet(FieldIndex<IDX_FullZoom>, float aOldValue) {
  if (GetFullZoom() == aOldValue) {
    return;
  }

  if (IsInProcess()) {
    if (nsIDocShell* shell = GetDocShell()) {
      if (nsPresContext* pc = shell->GetPresContext()) {
        pc->RecomputeBrowsingContextDependentData();
      }
    }

    for (BrowsingContext* child : Children()) {
      // When passing the outer document's full-zoom down to the inner
      // document, scale by the effective CSS 'zoom' on the embedder element:
      auto fullZoom = GetFullZoom();
      if (auto* elem = child->GetEmbedderElement()) {
        if (auto* frame = elem->GetPrimaryFrame()) {
          fullZoom = frame->Style()->EffectiveZoom().Zoom(fullZoom);
        }
      }
      // Setting full zoom on a discarded context has no effect.
      (void)child->SetFullZoom(fullZoom);
    }
  }

  if (IsTop() && XRE_IsParentProcess()) {
    if (Element* element = GetEmbedderElement()) {
      AsyncEventDispatcher::RunDOMEventWhenSafe(*element, u"FullZoomChange"_ns,
                                                CanBubble::eYes,
                                                ChromeOnlyDispatch::eYes);
    }
  }
}

void BrowsingContext::AddDeprioritizedLoadRunner(nsIRunnable* aRunner) {
  MOZ_ASSERT(IsLoading());
  MOZ_ASSERT(Top() == this);

  RefPtr runner = MakeRefPtr<DeprioritizedLoadRunner>(aRunner);
  mDeprioritizedLoadRunner.insertBack(runner);
  NS_DispatchToCurrentThreadQueue(runner.forget(), EventQueuePriority::Low);
}

bool BrowsingContext::IsDynamic() const {
  const BrowsingContext* current = this;
  do {
    if (current->CreatedDynamically()) {
      return true;
    }
  } while ((current = current->GetParent()));

  return false;
}

bool BrowsingContext::GetOffsetPath(nsTArray<uint32_t>& aPath) const {
  for (const BrowsingContext* current = this; current && current->GetParent();
       current = current->GetParent()) {
    if (current->CreatedDynamically()) {
      return false;
    }
    aPath.AppendElement(current->ChildOffset());
  }
  return true;
}

void BrowsingContext::GetHistoryID(JSContext* aCx,
                                   JS::MutableHandle<JS::Value> aVal,
                                   ErrorResult& aError) {
  if (!xpc::ID2JSValue(aCx, GetHistoryID(), aVal)) {
    aError.Throw(NS_ERROR_OUT_OF_MEMORY);
  }
}

void BrowsingContext::InitSessionHistory() {
  MOZ_ASSERT(!IsDiscarded());
  MOZ_ASSERT(IsTop());
  MOZ_ASSERT(EverAttached());

  if (!GetHasSessionHistory()) {
    MOZ_ALWAYS_SUCCEEDS(SetHasSessionHistory(true));
  }
}

ChildSHistory* BrowsingContext::GetChildSessionHistory() {
  return mChildSessionHistory;
}

void BrowsingContext::CreateChildSHistory() {
  MOZ_ASSERT(IsTop());
  MOZ_ASSERT(GetHasSessionHistory());
  MOZ_ASSERT(!mChildSessionHistory);

  // Because session history is global in a browsing context tree, every process
  // that has access to a browsing context tree needs access to its session
  // history. That is why we create the ChildSHistory object in every process
  // where we have access to this browsing context (which is the top one).
  mChildSessionHistory = MakeRefPtr<ChildSHistory>(this);
}

void BrowsingContext::DidSet(FieldIndex<IDX_HasSessionHistory>,
                             bool aOldValue) {
  MOZ_ASSERT(GetHasSessionHistory() || !aOldValue,
             "We don't support turning off session history.");

  if (GetHasSessionHistory() && !aOldValue) {
    CreateChildSHistory();
  }
}

bool BrowsingContext::CanSet(
    FieldIndex<IDX_TargetTopLevelLinkClicksToBlankInternal>,
    const bool& aTargetTopLevelLinkClicksToBlankInternal,
    ContentParent* aSource) {
  return XRE_IsParentProcess() && !aSource && IsTop();
}

bool BrowsingContext::CanSet(FieldIndex<IDX_BrowserId>, const uint64_t& aValue,
                             ContentParent* aSource) {
  if (XRE_IsParentProcess() && !aSource) {
    return true;
  }

  if (aSource && !Canonical()->IsOwnedByProcess(aSource->ChildID())) {
    return false;
  }

  return GetBrowserId() == 0 && Children().IsEmpty();
}

bool BrowsingContext::CanSet(FieldIndex<IDX_PendingInitialization>,
                             bool aNewValue, ContentParent* aSource) {
  // Can only be cleared from `true` to `false`, and should only ever be set on
  // the toplevel BrowsingContext.
  return IsTop() && GetPendingInitialization() && !aNewValue;
}

bool BrowsingContext::CanSet(FieldIndex<IDX_TopLevelCreatedByWebContent>,
                             const bool& aNewValue, ContentParent* aSource) {
  // Should only be set after creation in the parent process.
  return XRE_IsParentProcess() && !aSource && IsTop();
}

bool BrowsingContext::CanSet(FieldIndex<IDX_HasRestoreData>, bool aNewValue,
                             ContentParent* aSource) {
  return IsTop();
}

bool BrowsingContext::CanSet(FieldIndex<IDX_IsUnderHiddenEmbedderElement>,
                             const bool& aIsUnderHiddenEmbedderElement,
                             ContentParent* aSource) {
  return true;
}

bool BrowsingContext::CanSet(FieldIndex<IDX_ForceOffline>, bool aNewValue,
                             ContentParent* aSource) {
  return XRE_IsParentProcess() && !aSource;
}

void BrowsingContext::DidSet(FieldIndex<IDX_IsUnderHiddenEmbedderElement>,
                             bool aOldValue) {
  nsIDocShell* shell = GetDocShell();
  if (!shell) {
    return;
  }
  const bool newValue = IsUnderHiddenEmbedderElement();
  if (NS_WARN_IF(aOldValue == newValue)) {
    return;
  }

  if (auto* bc = BrowserChild::GetFrom(shell)) {
    bc->UpdateVisibility();
  }

  if (PresShell* presShell = shell->GetPresShell()) {
    presShell->SetIsUnderHiddenEmbedderElement(newValue);
  }

  // Propagate to children.
  auto PropagateToChild = [&newValue](BrowsingContext* aChild) {
    Element* embedderElement = aChild->GetEmbedderElement();
    if (!embedderElement) {
      // TODO: We shouldn't need to null check here since `child` and the
      // element returned by `child->GetEmbedderElement()` are in our
      // process (the actual browsing context represented by `child` may not
      // be, but that doesn't matter).  However, there are currently a very
      // small number of crashes due to `embedderElement` being null, somehow
      // - see bug 1551241.  For now we wallpaper the crash.
      return CallState::Continue;
    }

    bool embedderFrameIsHidden = true;
    if (auto* embedderFrame = embedderElement->GetPrimaryFrame()) {
      embedderFrameIsHidden = !embedderFrame->StyleVisibility()->IsVisible();
    }

    bool hidden = newValue || embedderFrameIsHidden;
    if (aChild->IsUnderHiddenEmbedderElement() != hidden) {
      (void)aChild->SetIsUnderHiddenEmbedderElement(hidden);
    }

    return CallState::Continue;
  };

  for (BrowsingContext* child : Children()) {
    PropagateToChild(child);
  }

  if (XRE_IsParentProcess()) {
    Canonical()->CallOnTopDescendants(
        PropagateToChild,
        CanonicalBrowsingContext::TopDescendantKind::ChildrenOnly);
  }
}

void BrowsingContext::DidSet(FieldIndex<IDX_ForceOffline>, bool aOldValue) {
  const bool newValue = ForceOffline();
  if (newValue == aOldValue) {
    return;
  }
  PreOrderWalk([&](BrowsingContext* aBrowsingContext) {
    if (RefPtr<WindowContext> windowContext =
            aBrowsingContext->GetCurrentWindowContext()) {
      if (nsCOMPtr<nsPIDOMWindowInner> window =
              windowContext->GetInnerWindow()) {
        nsGlobalWindowInner::Cast(window)->FireOfflineStatusEventIfChanged();
      }
    }
  });
}

bool BrowsingContext::IsPopupAllowed() {
  for (auto* context = GetCurrentWindowContext(); context;
       context = context->GetParentWindowContext()) {
    if (context->CanShowPopup()) {
      return true;
    }
  }

  return false;
}

/* static */
bool BrowsingContext::ShouldAddEntryForRefresh(
    nsIURI* aPreviousURI, const SessionHistoryInfo& aInfo) {
  return ShouldAddEntryForRefresh(aPreviousURI, aInfo.GetURI(),
                                  aInfo.HasPostData());
}

/* static */
bool BrowsingContext::ShouldAddEntryForRefresh(nsIURI* aPreviousURI,
                                               nsIURI* aNewURI,
                                               bool aHasPostData) {
  if (aHasPostData) {
    return true;
  }

  bool equalsURI = false;
  if (aPreviousURI) {
    aPreviousURI->Equals(aNewURI, &equalsURI);
  }
  return !equalsURI;
}

bool BrowsingContext::AddSHEntryWouldIncreaseLength(
    SessionHistoryInfo* aCurrentEntry) const {
  // nsSHistory::AddEntry and AddNestedSHEntry do a replace load if the current
  // entry is marked as transient.
  const bool isCurrentTransientEntry =
      aCurrentEntry && aCurrentEntry->IsTransient();

  // If this is the first entry for an iframe, it would be added to the parent
  // entry instead of creating a new top-level entry.
  const bool wouldAddToParentEntry = !IsTop() && !aCurrentEntry;

  return !isCurrentTransientEntry && !wouldAddToParentEntry;
}

void BrowsingContext::SessionHistoryCommit(
    const LoadingSessionHistoryInfo& aInfo, uint32_t aLoadType,
    nsIURI* aPreviousURI, SessionHistoryInfo* aPreviousActiveEntry,
    bool aCloneEntryChildren, bool aChannelExpired, uint32_t aCacheKey) {
  nsID changeID = {};
  if (XRE_IsContentProcess()) {
    RefPtr<ChildSHistory> rootSH = Top()->GetChildSessionHistory();
    if (rootSH) {
      if (!aInfo.mLoadIsFromSessionHistory) {
        // We try to mimic as closely as possible whether
        // CanonicalBrowsingContext::SessionHistoryCommit will increase
        // the session history length.
        // It is possible that this leads to wrong length temporarily, but
        // so would not having these checks.
        // The child process does not have access to the current entry, so we
        // use the previous active entry as the best approximation. When that's
        // not the current entry then the length might be wrong briefly, until
        // the parent process commits the actual length.
        const bool isReplaceLoad = LOAD_TYPE_HAS_FLAGS(
                       aLoadType, nsIWebNavigation::LOAD_FLAGS_REPLACE_HISTORY),
                   isRefreshLoad = LOAD_TYPE_HAS_FLAGS(
                       aLoadType, nsIWebNavigation::LOAD_FLAGS_IS_REFRESH);
        if (!isReplaceLoad &&
            AddSHEntryWouldIncreaseLength(aPreviousActiveEntry) &&
            ShouldUpdateSessionHistory(aLoadType) &&
            (!isRefreshLoad ||
             ShouldAddEntryForRefresh(aPreviousURI, aInfo.mInfo))) {
          changeID = rootSH->AddPendingHistoryChange();
        }
      } else {
        // History load doesn't change the length, only index.
        changeID = rootSH->AddPendingHistoryChange(aInfo.mOffset, 0);
      }
    }
    ContentChild* cc = ContentChild::GetSingleton();
    (void)cc->SendHistoryCommit(this, aInfo.mLoadId, changeID, aLoadType,
                                aCloneEntryChildren, aChannelExpired,
                                aCacheKey);
  } else {
    Canonical()->SessionHistoryCommit(aInfo.mLoadId, changeID, aLoadType,
                                      aCloneEntryChildren, aChannelExpired,
                                      aCacheKey);
  }
}

void BrowsingContext::SetActiveSessionHistoryEntry(
    const Maybe<nsPoint>& aPreviousScrollPos, SessionHistoryInfo* aInfo,
    SessionHistoryInfo* aPreviousActiveEntry, uint32_t aLoadType,
    uint32_t aUpdatedCacheKey, bool aUpdateLength) {
  if (XRE_IsContentProcess()) {
    // XXX Why we update cache key only in content process case?
    if (aUpdatedCacheKey != 0) {
      aInfo->SetCacheKey(aUpdatedCacheKey);
    }

    nsID changeID = {};
    if (aUpdateLength) {
      RefPtr<ChildSHistory> shistory = Top()->GetChildSessionHistory();
      if (shistory) {
        // We try to mimic what will happen in
        // CanonicalBrowsingContext::SendSetActiveSessionHistoryEntry
        if (AddSHEntryWouldIncreaseLength(aPreviousActiveEntry)) {
          changeID = shistory->AddPendingHistoryChange();
        }
      }
    }
    ContentChild::GetSingleton()->SendSetActiveSessionHistoryEntry(
        this, aPreviousScrollPos, *aInfo, aLoadType, aUpdatedCacheKey,
        changeID);
  } else {
    Canonical()->SetActiveSessionHistoryEntry(
        aPreviousScrollPos, aInfo, aLoadType, aUpdatedCacheKey, nsID());
  }
}

void BrowsingContext::ReplaceActiveSessionHistoryEntry(
    SessionHistoryInfo* aInfo) {
  if (XRE_IsContentProcess()) {
    ContentChild::GetSingleton()->SendReplaceActiveSessionHistoryEntry(this,
                                                                       *aInfo);
  } else {
    Canonical()->ReplaceActiveSessionHistoryEntry(aInfo);
  }
}

void BrowsingContext::RemoveDynEntriesFromActiveSessionHistoryEntry() {
  if (XRE_IsContentProcess()) {
    ContentChild::GetSingleton()
        ->SendRemoveDynEntriesFromActiveSessionHistoryEntry(this);
  } else {
    Canonical()->RemoveDynEntriesFromActiveSessionHistoryEntry();
  }
}

void BrowsingContext::RemoveFromSessionHistory(const nsID& aChangeID) {
  if (XRE_IsContentProcess()) {
    ContentChild::GetSingleton()->SendRemoveFromSessionHistory(this, aChangeID);
  } else {
    Canonical()->RemoveFromSessionHistory(aChangeID);
  }
}

void BrowsingContext::HistoryGo(
    int32_t aOffset, uint64_t aHistoryEpoch, bool aRequireUserInteraction,
    bool aUserActivation, std::function<void(Maybe<int32_t>&&)>&& aResolver) {
  // We always pass checkForCancelation as true here, since we'll never call
  // HistoryGo in a context where we beforehand know that we want to skip
  // onbeforeunload or onnavigate.
  bool checkForCancelation = true;
  if (XRE_IsContentProcess()) {
    ContentChild::GetSingleton()->SendHistoryGo(
        this, aOffset, aHistoryEpoch, aRequireUserInteraction, aUserActivation,
        checkForCancelation, std::move(aResolver),
        [](mozilla::ipc::
               ResponseRejectReason) { /* FIXME Is ignoring this fine? */ });
  } else {
    RefPtr<CanonicalBrowsingContext> self = Canonical();
    aResolver(self->HistoryGo(aOffset, aHistoryEpoch, aRequireUserInteraction,
                              aUserActivation, checkForCancelation,
                              self->GetContentParent()
                                  ? Some(self->GetContentParent()->ChildID())
                                  : Nothing()));
  }
}

void BrowsingContext::NavigationTraverse(
    const nsID& aKey, uint64_t aHistoryEpoch, bool aUserActivation,
    bool aCheckForCancelation, std::function<void(nsresult)>&& aResolver) {
  if (XRE_IsContentProcess()) {
    ContentChild::GetSingleton()->SendNavigationTraverse(
        this, aKey, aHistoryEpoch, aUserActivation, aCheckForCancelation,
        std::move(aResolver),
        [](mozilla::ipc::
               ResponseRejectReason) { /* FIXME Is ignoring this fine? */ });
  } else {
    RefPtr<CanonicalBrowsingContext> self = Canonical();
    self->NavigationTraverse(
        aKey, aHistoryEpoch, aUserActivation, aCheckForCancelation,
        self->GetContentParent() ? Some(self->GetContentParent()->ChildID())
                                 : Nothing(),
        std::move(aResolver));
  }
}

void BrowsingContext::SetChildSHistory(ChildSHistory* aChildSHistory) {
  mChildSessionHistory = aChildSHistory;
  mChildSessionHistory->SetBrowsingContext(this);
  mFields.SetWithoutSyncing<IDX_HasSessionHistory>(true);
}

bool BrowsingContext::ShouldUpdateSessionHistory(uint32_t aLoadType) {
  // We don't update session history on reload unless we're loading
  // an iframe in shift-reload case.
  return nsDocShell::ShouldUpdateGlobalHistory(aLoadType) &&
         (!(aLoadType & nsIDocShell::LOAD_CMD_RELOAD) ||
          (IsForceReloadType(aLoadType) && IsSubframe()));
}

nsresult BrowsingContext::CheckNavigationRateLimit(CallerType aCallerType) {
  // We only rate limit non system callers
  if (aCallerType == CallerType::System) {
    return NS_OK;
  }

  // Fetch rate limiting preferences
  uint32_t limitCount = StaticPrefs::dom_navigation_navigationRateLimit_count();
  uint32_t timeSpanSeconds =
      StaticPrefs::dom_navigation_navigationRateLimit_timespan();

  // Disable throttling if either of the preferences is set to 0.
  if (limitCount == 0 || timeSpanSeconds == 0) {
    return NS_OK;
  }

  TimeDuration throttleSpan = TimeDuration::FromSeconds(timeSpanSeconds);

  if (mNavigationRateLimitSpanStart.IsNull() ||
      ((TimeStamp::Now() - mNavigationRateLimitSpanStart) > throttleSpan)) {
    // Initial call or timespan exceeded, reset counter and timespan.
    mNavigationRateLimitSpanStart = TimeStamp::Now();
    mNavigationRateLimitCount = 1;
    return NS_OK;
  }

  if (mNavigationRateLimitCount >= limitCount) {
    // Rate limit reached

    Document* doc = GetDocument();
    if (doc) {
      nsContentUtils::ReportToConsole(nsIScriptError::errorFlag, "DOM"_ns, doc,
                                      PropertiesFile::DOM_PROPERTIES,
                                      "LocChangeFloodingPrevented");
    }

    return NS_ERROR_DOM_SECURITY_ERR;
  }

  mNavigationRateLimitCount++;
  return NS_OK;
}

void BrowsingContext::ResetNavigationRateLimit() {
  // Resetting the timestamp object will cause the check function to
  // init again and reset the rate limit.
  mNavigationRateLimitSpanStart = TimeStamp();
}

void BrowsingContext::LocationCreated(dom::Location* aLocation) {
  MOZ_ASSERT(!aLocation->isInList());
  mLocations.insertBack(aLocation);
}

void BrowsingContext::ClearCachedValuesOfLocations() {
  for (dom::Location* loc = mLocations.getFirst(); loc; loc = loc->getNext()) {
    loc->ClearCachedValues();
  }
}

// https://html.spec.whatwg.org/multipage/interaction.html#consume-history-action-user-activation
// Step 3 onward
void BrowsingContext::ConsumeHistoryActivation() {
  // 3. Let navigables be the inclusive descendant navigables of top's active
  // document.
  // 4. Let windows be the list of Window objects constructed by taking the
  // active window of each item in navigables.
  PreOrderWalk([&](BrowsingContext* aBrowsingContext) {
    RefPtr<WindowContext> windowContext =
        aBrowsingContext->GetCurrentWindowContext();
    // 5. For each window in windows, set window's last history-action
    // activation timestamp to window's last activation timestamp.
    if (aBrowsingContext->IsInProcess() && windowContext &&
        windowContext->GetUserActivationState() ==
            UserActivation::State::FullActivated) {
      windowContext->UpdateLastHistoryActivation();
    }
  });
}

void BrowsingContext::SynchronizeNavigationAPIState(
    nsIStructuredCloneContainer* aState) {
  if (!aState) {
    return;
  }

  if (XRE_IsContentProcess()) {
    MOZ_ASSERT(ContentChild::GetSingleton());
    ContentChild::GetSingleton()->SendSynchronizeNavigationAPIState(
        this, WrapNotNull(static_cast<nsStructuredCloneContainer*>(aState)));
  } else {
    Canonical()->SynchronizeNavigationAPIState(aState);
  }
}

}  // namespace dom
}  // namespace mozilla

namespace IPC {

using mozilla::dom::BrowsingContext;
using mozilla::dom::MaybeDiscarded;

void ParamTraits<MaybeDiscarded<BrowsingContext>>::Write(
    IPC::MessageWriter* aWriter,
    const MaybeDiscarded<BrowsingContext>& aParam) {
  MOZ_DIAGNOSTIC_ASSERT(!aParam.GetMaybeDiscarded() ||
                        aParam.GetMaybeDiscarded()->EverAttached());
  uint64_t id = aParam.ContextId();
  WriteParam(aWriter, id);
}

bool ParamTraits<MaybeDiscarded<BrowsingContext>>::Read(
    IPC::MessageReader* aReader, MaybeDiscarded<BrowsingContext>* aResult) {
  uint64_t id = 0;
  if (!ReadParam(aReader, &id)) {
    return false;
  }

  if (id == 0) {
    *aResult = nullptr;
  } else if (RefPtr<BrowsingContext> bc = BrowsingContext::Get(id)) {
    if (!bc->Group()->IsKnownForMessageReader(aReader)) {
      return false;
    }

    *aResult = std::move(bc);
  } else {
    aResult->SetDiscarded(id);
  }
  return true;
}

IMPLEMENT_IPC_SERIALIZER_WITH_FIELDS(BrowsingContext::IPCInitializer, mId,
                                     mParentId, mWindowless, mUseRemoteTabs,
                                     mUseRemoteSubframes, mCreatedDynamically,
                                     mChildOffset, mOriginAttributes,
                                     mRequestContextId, mSessionHistoryIndex,
                                     mSessionHistoryCount, mFields);

template struct ParamTraits<BrowsingContext::BaseTransaction>;

}  // namespace IPC
