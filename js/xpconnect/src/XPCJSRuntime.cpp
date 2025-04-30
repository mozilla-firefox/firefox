/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Per JSRuntime object */

#include "mozilla/ArrayUtils.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/UniquePtr.h"

#include "xpcprivate.h"
#include "xpcpublic.h"
#include "XPCMaps.h"
#include "XPCJSMemoryReporter.h"
#include "XrayWrapper.h"
#include "WrapperFactory.h"
#include "mozJSModuleLoader.h"
#include "nsNetUtil.h"
#include "nsContentSecurityUtils.h"

#include "nsExceptionHandler.h"
#include "nsIMemoryInfoDumper.h"
#include "nsIMemoryReporter.h"
#include "nsIObserverService.h"
#include "mozilla/dom/Document.h"
#include "nsIRunnable.h"
#include "nsPIDOMWindow.h"
#include "nsPrintfCString.h"
#include "nsScriptSecurityManager.h"
#include "nsWindowSizes.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/Preferences.h"
#include "mozilla/Telemetry.h"
#include "mozilla/Services.h"
#include "mozilla/dom/ScriptLoader.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/glean/JsXpconnectMetrics.h"
#include "mozilla/glean/XpcomMetrics.h"

#include "nsContentUtils.h"
#include "nsCCUncollectableMarker.h"
#include "nsCycleCollectionNoteRootCallback.h"
#include "nsCycleCollector.h"
#include "jsapi.h"
#include "js/BuildId.h"  // JS::BuildIdCharVector, JS::SetProcessBuildIdOp
#include "js/experimental/SourceHook.h"  // js::{,Set}SourceHook
#include "js/GCAPI.h"
#include "js/MemoryFunctions.h"
#include "js/MemoryMetrics.h"
#include "js/Object.h"  // JS::GetClass
#include "js/RealmIterators.h"
#include "js/SliceBudget.h"
#include "js/UbiNode.h"
#include "js/UbiNodeUtils.h"
#include "js/friend/UsageStatistics.h"  // JSMetric, JS_SetAccumulateTelemetryCallback
#include "js/friend/WindowProxy.h"  // js::SetWindowProxyClass
#include "js/friend/XrayJitInfo.h"  // JS::SetXrayJitInfo
#include "js/Utility.h"             // JS::UniqueTwoByteChars
#include "mozilla/dom/AbortSignalBinding.h"
#include "mozilla/dom/GeneratedAtomList.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/FetchUtil.h"
#include "mozilla/dom/WindowBinding.h"
#include "mozilla/Atomics.h"
#include "mozilla/Attributes.h"
#include "mozilla/ProcessHangMonitor.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/Sprintf.h"
#include "mozilla/UniquePtrExtensions.h"
#include "mozilla/Unused.h"
#include "AccessCheck.h"
#include "nsGlobalWindowInner.h"
#include "nsAboutProtocolUtils.h"

#include "NodeUbiReporting.h"
#include "ExpandedPrincipal.h"
#include "nsIInputStream.h"
#include "nsJSPrincipals.h"
#include "nsJSEnvironment.h"
#include "XPCInlines.h"

#ifdef XP_WIN
#  include <windows.h>
#endif

using namespace mozilla;
using namespace mozilla::dom;
using namespace xpc;
using namespace JS;
using namespace js;
using mozilla::dom::PerThreadAtomCache;

/***************************************************************************/

const char* const XPCJSRuntime::mStrings[] = {
    "constructor",      // IDX_CONSTRUCTOR
    "toString",         // IDX_TO_STRING
    "toSource",         // IDX_TO_SOURCE
    "value",            // IDX_VALUE
    "QueryInterface",   // IDX_QUERY_INTERFACE
    "Components",       // IDX_COMPONENTS
    "Cc",               // IDX_CC
    "Ci",               // IDX_CI
    "Cr",               // IDX_CR
    "Cu",               // IDX_CU
    "Services",         // IDX_SERVICES
    "wrappedJSObject",  // IDX_WRAPPED_JSOBJECT
    "prototype",        // IDX_PROTOTYPE
    "eval",             // IDX_EVAL
    "controllers",      // IDX_CONTROLLERS
    "Controllers",      // IDX_CONTROLLERS_CLASS
    "length",           // IDX_LENGTH
    "name",             // IDX_NAME
    "undefined",        // IDX_UNDEFINED
    "",                 // IDX_EMPTYSTRING
    "fileName",         // IDX_FILENAME
    "lineNumber",       // IDX_LINENUMBER
    "columnNumber",     // IDX_COLUMNNUMBER
    "stack",            // IDX_STACK
    "message",          // IDX_MESSAGE
    "cause",            // IDX_CAUSE
    "errors",           // IDX_ERRORS
    "lastIndex",        // IDX_LASTINDEX
    "then",             // IDX_THEN
    "isInstance",       // IDX_ISINSTANCE
    "Infinity",         // IDX_INFINITY
    "NaN",              // IDX_NAN
    "classId",          // IDX_CLASS_ID
    "interfaceId",      // IDX_INTERFACE_ID
    "initializer",      // IDX_INITIALIZER
    "print",            // IDX_PRINT
    "fetch",            // IDX_FETCH
    "crypto",           // IDX_CRYPTO
    "indexedDB",        // IDX_INDEXEDDB
    "structuredClone",  // IDX_STRUCTUREDCLONE
    "locks",            // IDX_LOCKS
#ifdef ENABLE_EXPLICIT_RESOURCE_MANAGEMENT
    "suppressed",  // IDX_SUPPRESSED
    "error",       // IDX_ERROR
#endif
};

/***************************************************************************/

// *Some* NativeSets are referenced from mClassInfo2NativeSetMap.
// *All* NativeSets are referenced from mNativeSetMap.
// So, in mClassInfo2NativeSetMap we just clear references to the unmarked.
// In mNativeSetMap we clear the references to the unmarked *and* delete them.

class AsyncFreeSnowWhite : public Runnable {
 public:
  NS_IMETHOD Run() override {
    AUTO_PROFILER_LABEL_RELEVANT_FOR_JS("Incremental CC", GCCC);
    AUTO_PROFILER_LABEL("AsyncFreeSnowWhite::Run", GCCC_FreeSnowWhite);

    auto timerId = glean::cycle_collector::async_snow_white_freeing.Start();
    // 2 ms budget, given that kICCSliceBudget is only 3 ms
    SliceBudget budget = SliceBudget(TimeBudget(2));
    bool hadSnowWhiteObjects =
        nsCycleCollector_doDeferredDeletionWithBudget(budget);
    glean::cycle_collector::async_snow_white_freeing.StopAndAccumulate(
        std::move(timerId));
    if (hadSnowWhiteObjects && !mContinuation) {
      mContinuation = true;
      if (NS_FAILED(Dispatch())) {
        mActive = false;
      }
    } else {
      mActive = false;
    }
    return NS_OK;
  }

  nsresult Dispatch() {
    nsCOMPtr<nsIRunnable> self(this);
    return NS_DispatchToCurrentThreadQueue(self.forget(), 1000,
                                           EventQueuePriority::Idle);
  }

  void Start(bool aContinuation = false, bool aPurge = false) {
    if (mContinuation) {
      mContinuation = aContinuation;
    }
    mPurge = aPurge;
    if (!mActive && NS_SUCCEEDED(Dispatch())) {
      mActive = true;
    }
  }

  AsyncFreeSnowWhite()
      : Runnable("AsyncFreeSnowWhite"),
        mContinuation(false),
        mActive(false),
        mPurge(false) {}

 public:
  bool mContinuation;
  bool mActive;
  bool mPurge;
};

namespace xpc {

CompartmentPrivate::CompartmentPrivate(
    JS::Compartment* c, mozilla::UniquePtr<XPCWrappedNativeScope> scope,
    mozilla::BasePrincipal* origin, const SiteIdentifier& site)
    : originInfo(origin, site),
      wantXrays(false),
      allowWaivers(true),
      isWebExtensionContentScript(false),
      isUAWidgetCompartment(false),
      hasExclusiveExpandos(false),
      wasShutdown(false),
      mWrappedJSMap(mozilla::MakeUnique<JSObject2WrappedJSMap>()),
      mScope(std::move(scope)) {
  MOZ_COUNT_CTOR(xpc::CompartmentPrivate);
}

CompartmentPrivate::~CompartmentPrivate() {
  MOZ_COUNT_DTOR(xpc::CompartmentPrivate);
}

void CompartmentPrivate::SystemIsBeingShutDown() {
  // We may call this multiple times when the compartment contains more than one
  // realm.
  if (!wasShutdown) {
    mWrappedJSMap->ShutdownMarker();
    wasShutdown = true;
  }
}

RealmPrivate::RealmPrivate(JS::Realm* realm) : scriptability(realm) {
  mozilla::PodArrayZero(wrapperDenialWarnings);
}

/* static */
void RealmPrivate::Init(HandleObject aGlobal, const SiteIdentifier& aSite) {
  MOZ_ASSERT(aGlobal);
  DebugOnly<const JSClass*> clasp = JS::GetClass(aGlobal);
  MOZ_ASSERT(clasp->slot0IsISupports() || dom::IsDOMClass(clasp));

  Realm* realm = GetObjectRealmOrNull(aGlobal);

  // Create the realm private.
  RealmPrivate* realmPriv = new RealmPrivate(realm);
  MOZ_ASSERT(!GetRealmPrivate(realm));
  SetRealmPrivate(realm, realmPriv);

  nsIPrincipal* principal = GetRealmPrincipal(realm);
  Compartment* c = JS::GetCompartment(aGlobal);

  // Create the compartment private if needed.
  if (CompartmentPrivate* priv = CompartmentPrivate::Get(c)) {
    MOZ_ASSERT(priv->originInfo.IsSameOrigin(principal));
  } else {
    auto scope = mozilla::MakeUnique<XPCWrappedNativeScope>(c, aGlobal);
    priv = new CompartmentPrivate(c, std::move(scope),
                                  BasePrincipal::Cast(principal), aSite);
    JS_SetCompartmentPrivate(c, priv);
  }
}

// As XPCJSRuntime can live longer than when we shutdown the observer service,
// we have our own getter to account for this.
static nsCOMPtr<nsIObserverService> GetObserverService() {
  if (AppShutdown::IsInOrBeyond(ShutdownPhase::XPCOMShutdownFinal)) {
    return nullptr;
  }
  return mozilla::services::GetObserverService();
}

static bool TryParseLocationURICandidate(
    const nsACString& uristr, RealmPrivate::LocationHint aLocationHint,
    nsIURI** aURI) {
  static constexpr auto kGRE = "resource://gre/"_ns;
  static constexpr auto kToolkit = "chrome://global/"_ns;
  static constexpr auto kBrowser = "chrome://browser/"_ns;

  if (aLocationHint == RealmPrivate::LocationHintAddon) {
    // Blacklist some known locations which are clearly not add-on related.
    if (StringBeginsWith(uristr, kGRE) || StringBeginsWith(uristr, kToolkit) ||
        StringBeginsWith(uristr, kBrowser)) {
      return false;
    }

    // -- GROSS HACK ALERT --
    // The Yandex Elements 8.10.2 extension implements its own "xb://" URL
    // scheme. If we call NS_NewURI() on an "xb://..." URL, we'll end up
    // calling into the extension's own JS-implemented nsIProtocolHandler
    // object, which we can't allow while we're iterating over the JS heap.
    // So just skip any such URL.
    // -- GROSS HACK ALERT --
    if (StringBeginsWith(uristr, "xb"_ns)) {
      return false;
    }
  }

  nsCOMPtr<nsIURI> uri;
  if (NS_FAILED(NS_NewURI(getter_AddRefs(uri), uristr))) {
    return false;
  }

  nsAutoCString scheme;
  if (NS_FAILED(uri->GetScheme(scheme))) {
    return false;
  }

  // Cannot really map data: and blob:.
  // Also, data: URIs are pretty memory hungry, which is kinda bad
  // for memory reporter use.
  if (scheme.EqualsLiteral("data") || scheme.EqualsLiteral("blob")) {
    return false;
  }

  uri.forget(aURI);
  return true;
}

bool RealmPrivate::TryParseLocationURI(RealmPrivate::LocationHint aLocationHint,
                                       nsIURI** aURI) {
  if (!aURI) {
    return false;
  }

  // Need to parse the URI.
  if (location.IsEmpty()) {
    return false;
  }

  // Handle Sandbox location strings.
  // A sandbox string looks like this, for anonymous sandboxes, and builds
  // where Sandbox location tagging is enabled:
  //
  // <sandboxName> (from: <js-stack-frame-filename>:<lineno>)
  //
  // where <sandboxName> is user-provided via Cu.Sandbox()
  // and <js-stack-frame-filename> and <lineno> is the stack frame location
  // from where Cu.Sandbox was called.
  //
  // Otherwise, it is simply the caller-provided name, which is usually a URI.
  //
  // <js-stack-frame-filename> furthermore is "free form", often using a
  // "uri -> uri -> ..." chain. The following code will and must handle this
  // common case.
  //
  // It should be noted that other parts of the code may already rely on the
  // "format" of these strings.

  static const nsDependentCString from("(from: ");
  static const nsDependentCString arrow(" -> ");
  static const size_t fromLength = from.Length();
  static const size_t arrowLength = arrow.Length();

  // See: XPCComponents.cpp#AssembleSandboxMemoryReporterName
  int32_t idx = location.Find(from);
  if (idx < 0) {
    return TryParseLocationURICandidate(location, aLocationHint, aURI);
  }

  // When parsing we're looking for the right-most URI. This URI may be in
  // <sandboxName>, so we try this first.
  if (TryParseLocationURICandidate(Substring(location, 0, idx), aLocationHint,
                                   aURI)) {
    return true;
  }

  // Not in <sandboxName> so we need to inspect <js-stack-frame-filename> and
  // the chain that is potentially contained within and grab the rightmost
  // item that is actually a URI.

  // First, hack off the :<lineno>) part as well
  int32_t ridx = location.RFind(":"_ns);
  nsAutoCString chain(
      Substring(location, idx + fromLength, ridx - idx - fromLength));

  // Loop over the "->" chain. This loop also works for non-chains, or more
  // correctly chains with only one item.
  for (;;) {
    idx = chain.RFind(arrow);
    if (idx < 0) {
      // This is the last chain item. Try to parse what is left.
      return TryParseLocationURICandidate(chain, aLocationHint, aURI);
    }

    // Try to parse current chain item
    if (TryParseLocationURICandidate(Substring(chain, idx + arrowLength),
                                     aLocationHint, aURI)) {
      return true;
    }

    // Current chain item couldn't be parsed.
    // Strip current item and continue.
    chain = Substring(chain, 0, idx);
  }

  MOZ_CRASH("Chain parser loop does not terminate");
}

static bool PrincipalImmuneToScriptPolicy(nsIPrincipal* aPrincipal) {
  // System principal gets a free pass.
  if (aPrincipal->IsSystemPrincipal()) {
    return true;
  }

  auto* principal = BasePrincipal::Cast(aPrincipal);

  // ExpandedPrincipal gets a free pass.
  if (principal->Is<ExpandedPrincipal>()) {
    return true;
  }

  // WebExtension principals get a free pass.
  if (principal->AddonPolicy()) {
    return true;
  }

  // pdf.js is a special-case too.
  if (nsContentUtils::IsPDFJS(principal)) {
    return true;
  }

  // Check whether our URI is an "about:" URI that allows scripts.  If it is,
  // we need to allow JS to run.
  if (aPrincipal->SchemeIs("about")) {
    uint32_t flags;
    nsresult rv = aPrincipal->GetAboutModuleFlags(&flags);
    if (NS_SUCCEEDED(rv) && (flags & nsIAboutModule::ALLOW_SCRIPT)) {
      return true;
    }
  }

  return false;
}

void RealmPrivate::RegisterStackFrame(JSStackFrameBase* aFrame) {
  mJSStackFrames.PutEntry(aFrame);
}

void RealmPrivate::UnregisterStackFrame(JSStackFrameBase* aFrame) {
  mJSStackFrames.RemoveEntry(aFrame);
}

void RealmPrivate::NukeJSStackFrames() {
  for (const auto& key : mJSStackFrames.Keys()) {
    key->Clear();
  }

  mJSStackFrames.Clear();
}

void RegisterJSStackFrame(JS::Realm* aRealm, JSStackFrameBase* aStackFrame) {
  RealmPrivate* realmPrivate = RealmPrivate::Get(aRealm);
  if (!realmPrivate) {
    return;
  }

  realmPrivate->RegisterStackFrame(aStackFrame);
}

void UnregisterJSStackFrame(JS::Realm* aRealm, JSStackFrameBase* aStackFrame) {
  RealmPrivate* realmPrivate = RealmPrivate::Get(aRealm);
  if (!realmPrivate) {
    return;
  }

  realmPrivate->UnregisterStackFrame(aStackFrame);
}

void NukeJSStackFrames(JS::Realm* aRealm) {
  RealmPrivate* realmPrivate = RealmPrivate::Get(aRealm);
  if (!realmPrivate) {
    return;
  }

  realmPrivate->NukeJSStackFrames();
}

Scriptability::Scriptability(JS::Realm* realm)
    : mScriptBlocks(0),
      mWindowAllowsScript(true),
      mScriptBlockedByPolicy(false) {
  nsIPrincipal* prin = nsJSPrincipals::get(JS::GetRealmPrincipals(realm));

  mImmuneToScriptPolicy = PrincipalImmuneToScriptPolicy(prin);
  if (mImmuneToScriptPolicy) {
    return;
  }
  // If we're not immune, we should have a real principal with a URI.
  // Check the principal against the new-style domain policy.
  bool policyAllows;
  nsresult rv = prin->GetIsScriptAllowedByPolicy(&policyAllows);
  if (NS_SUCCEEDED(rv)) {
    mScriptBlockedByPolicy = !policyAllows;
    return;
  }
  // Something went wrong - be safe and block script.
  mScriptBlockedByPolicy = true;
}

bool Scriptability::Allowed() {
  return mWindowAllowsScript && !mScriptBlockedByPolicy && mScriptBlocks == 0;
}

bool Scriptability::IsImmuneToScriptPolicy() { return mImmuneToScriptPolicy; }

void Scriptability::Block() { ++mScriptBlocks; }

void Scriptability::Unblock() {
  MOZ_ASSERT(mScriptBlocks > 0);
  --mScriptBlocks;
}

void Scriptability::SetWindowAllowsScript(bool aAllowed) {
  mWindowAllowsScript = aAllowed || mImmuneToScriptPolicy;
}

/* static */
bool Scriptability::AllowedIfExists(JSObject* aScope) {
  RealmPrivate* realmPrivate = RealmPrivate::Get(aScope);
  return realmPrivate ? realmPrivate->scriptability.Allowed() : true;
}

/* static */
Scriptability& Scriptability::Get(JSObject* aScope) {
  return RealmPrivate::Get(aScope)->scriptability;
}

bool IsUAWidgetCompartment(JS::Compartment* compartment) {
  // We always eagerly create compartment privates for UA Widget compartments.
  CompartmentPrivate* priv = CompartmentPrivate::Get(compartment);
  return priv && priv->isUAWidgetCompartment;
}

bool IsUAWidgetScope(JS::Realm* realm) {
  return IsUAWidgetCompartment(JS::GetCompartmentForRealm(realm));
}

bool IsInUAWidgetScope(JSObject* obj) {
  return IsUAWidgetCompartment(JS::GetCompartment(obj));
}

bool CompartmentOriginInfo::MightBeWebContent() const {
  // Compartments with principals that are either the system principal or an
  // expanded principal are definitely not web content.
  return !nsContentUtils::IsSystemOrExpandedPrincipal(mOrigin);
}

bool MightBeWebContentCompartment(JS::Compartment* compartment) {
  if (CompartmentPrivate* priv = CompartmentPrivate::Get(compartment)) {
    return priv->originInfo.MightBeWebContent();
  }

  // No CompartmentPrivate; try IsSystemCompartment.
  return !js::IsSystemCompartment(compartment);
}

bool CompartmentOriginInfo::IsSameOrigin(nsIPrincipal* aOther) const {
  return mOrigin->FastEquals(aOther);
}

/* static */
bool CompartmentOriginInfo::Subsumes(JS::Compartment* aCompA,
                                     JS::Compartment* aCompB) {
  CompartmentPrivate* apriv = CompartmentPrivate::Get(aCompA);
  CompartmentPrivate* bpriv = CompartmentPrivate::Get(aCompB);
  MOZ_ASSERT(apriv);
  MOZ_ASSERT(bpriv);
  return apriv->originInfo.mOrigin->FastSubsumes(bpriv->originInfo.mOrigin);
}

/* static */
bool CompartmentOriginInfo::SubsumesIgnoringFPD(JS::Compartment* aCompA,
                                                JS::Compartment* aCompB) {
  CompartmentPrivate* apriv = CompartmentPrivate::Get(aCompA);
  CompartmentPrivate* bpriv = CompartmentPrivate::Get(aCompB);
  MOZ_ASSERT(apriv);
  MOZ_ASSERT(bpriv);
  return apriv->originInfo.mOrigin->FastSubsumesIgnoringFPD(
      bpriv->originInfo.mOrigin);
}

void SetCompartmentChangedDocumentDomain(JS::Compartment* compartment) {
  // Note: we call this for all compartments that contain realms with a
  // particular principal. Not all of these compartments have a
  // CompartmentPrivate (for instance the temporary compartment/realm
  // created by the JS engine for off-thread parsing).
  if (CompartmentPrivate* priv = CompartmentPrivate::Get(compartment)) {
    priv->originInfo.SetChangedDocumentDomain();
  }
}

JSObject* UnprivilegedJunkScope() {
  return XPCJSRuntime::Get()->UnprivilegedJunkScope();
}

JSObject* UnprivilegedJunkScope(const fallible_t&) {
  return XPCJSRuntime::Get()->UnprivilegedJunkScope(fallible);
}

bool IsUnprivilegedJunkScope(JSObject* obj) {
  return XPCJSRuntime::Get()->IsUnprivilegedJunkScope(obj);
}

JSObject* NACScope(JSObject* global) {
  // If we're a chrome global, just use ourselves.
  if (AccessCheck::isChrome(global)) {
    return global;
  }

  JSObject* scope = UnprivilegedJunkScope();
  JS::ExposeObjectToActiveJS(scope);
  return scope;
}

JSObject* PrivilegedJunkScope() {
  return mozJSModuleLoader::Get()->GetSharedGlobal();
}

JSObject* CompilationScope() {
  return mozJSModuleLoader::Get()->GetSharedGlobal();
}

nsGlobalWindowInner* WindowOrNull(JSObject* aObj) {
  MOZ_ASSERT(aObj);
  MOZ_ASSERT(!js::IsWrapper(aObj));

  nsGlobalWindowInner* win = nullptr;
  UNWRAP_NON_WRAPPER_OBJECT(Window, aObj, win);
  return win;
}

nsGlobalWindowInner* WindowGlobalOrNull(JSObject* aObj) {
  MOZ_ASSERT(aObj);
  JSObject* glob = JS::GetNonCCWObjectGlobal(aObj);

  return WindowOrNull(glob);
}

JSObject* SandboxPrototypeOrNull(JSContext* aCx, JSObject* aObj) {
  MOZ_ASSERT(aObj);

  if (!IsSandbox(aObj)) {
    return nullptr;
  }

  // Sandbox can't be a Proxy so it must have a static prototype.
  JSObject* proto = GetStaticPrototype(aObj);
  if (!proto || !IsSandboxPrototypeProxy(proto)) {
    return nullptr;
  }

  return js::CheckedUnwrapDynamic(proto, aCx, /* stopAtWindowProxy = */ false);
}

nsGlobalWindowInner* CurrentWindowOrNull(JSContext* cx) {
  JSObject* glob = JS::CurrentGlobalOrNull(cx);
  return glob ? WindowOrNull(glob) : nullptr;
}

// Nukes all wrappers into or out of the given realm, and prevents new
// wrappers from being created. Additionally marks the realm as
// unscriptable after wrappers have been nuked.
//
// Note: This should *only* be called for browser or extension realms.
// Wrappers between web compartments must never be cut in web-observable
// ways.
void NukeAllWrappersForRealm(
    JSContext* cx, JS::Realm* realm,
    js::NukeReferencesToWindow nukeReferencesToWindow) {
  // We do the following:
  // * Nuke all wrappers into the realm.
  // * Nuke all wrappers out of the realm's compartment, once we have nuked all
  //   realms in it.
  js::NukeCrossCompartmentWrappers(cx, js::AllCompartments(), realm,
                                   nukeReferencesToWindow,
                                   js::NukeAllReferences);

  // Mark the realm as unscriptable.
  xpc::RealmPrivate::Get(realm)->scriptability.Block();
}

}  // namespace xpc

static void CompartmentDestroyedCallback(JS::GCContext* gcx,
                                         JS::Compartment* compartment) {
  // NB - This callback may be called in JS_DestroyContext, which happens
  // after the XPCJSRuntime has been torn down.

  // Get the current compartment private into a UniquePtr (which will do the
  // cleanup for us), and null out the private (which may already be null).
  mozilla::UniquePtr<CompartmentPrivate> priv(
      CompartmentPrivate::Get(compartment));
  JS_SetCompartmentPrivate(compartment, nullptr);
}

static size_t CompartmentSizeOfIncludingThisCallback(
    MallocSizeOf mallocSizeOf, JS::Compartment* compartment) {
  CompartmentPrivate* priv = CompartmentPrivate::Get(compartment);
  return priv ? priv->SizeOfIncludingThis(mallocSizeOf) : 0;
}

/*
 * Return true if there exists a non-system inner window which is a current
 * inner window and whose reflector is gray.  We don't merge system
 * compartments, so we don't use them to trigger merging CCs.
 */
bool XPCJSRuntime::UsefulToMergeZones() const {
  MOZ_ASSERT(NS_IsMainThread());

  // Turns out, actually making this return true often enough makes Windows
  // mochitest-gl OOM a lot.  Need to figure out what's going on there; see
  // bug 1277036.

  return false;
}

void XPCJSRuntime::TraceAdditionalNativeBlackRoots(JSTracer* trc) {
  if (CycleCollectedJSContext* ccx = GetContext()) {
    const auto* cx = static_cast<const XPCJSContext*>(ccx);
    if (AutoMarkingPtr* roots = cx->mAutoRoots) {
      roots->TraceJSAll(trc);
    }
  }

  if (mIID2NativeInterfaceMap) {
    mIID2NativeInterfaceMap->Trace(trc);
  }

  dom::TraceBlackJS(trc);
}

void XPCJSRuntime::TraceAdditionalNativeGrayRoots(JSTracer* trc) {
  XPCWrappedNativeScope::TraceWrappedNativesInAllScopes(this, trc);
}

void XPCJSRuntime::TraverseAdditionalNativeRoots(
    nsCycleCollectionNoteRootCallback& cb) {
  XPCWrappedNativeScope::SuspectAllWrappers(cb);

  auto* parti = NS_CYCLE_COLLECTION_PARTICIPANT(nsXPCWrappedJS);
  for (auto* wjs : mSubjectToFinalizationWJS) {
    MOZ_DIAGNOSTIC_ASSERT(wjs->IsSubjectToFinalization());
    cb.NoteXPCOMRoot(ToSupports(wjs), parti);
  }
}

void XPCJSRuntime::UnmarkSkippableJSHolders() {
  CycleCollectedJSRuntime::UnmarkSkippableJSHolders();
}

void XPCJSRuntime::PrepareForForgetSkippable() {
  nsCCUncollectableMarker::CleanupForForgetSkippable();
}

void XPCJSRuntime::BeginCycleCollectionCallback(CCReason aReason) {
  nsJSContext::BeginCycleCollectionCallback(aReason);

  nsCOMPtr<nsIObserverService> obs = xpc::GetObserverService();
  if (obs) {
    obs->NotifyObservers(nullptr, "cycle-collector-begin", nullptr);
  }
}

void XPCJSRuntime::EndCycleCollectionCallback(CycleCollectorResults& aResults) {
  nsJSContext::EndCycleCollectionCallback(aResults);

  nsCOMPtr<nsIObserverService> obs = xpc::GetObserverService();
  if (obs) {
    obs->NotifyObservers(nullptr, "cycle-collector-end", nullptr);
  }
}

void XPCJSRuntime::DispatchDeferredDeletion(bool aContinuation, bool aPurge) {
  mAsyncSnowWhiteFreer->Start(aContinuation, aPurge);
}

void xpc_UnmarkSkippableJSHolders() {
  if (nsXPConnect::GetRuntimeInstance()) {
    nsXPConnect::GetRuntimeInstance()->UnmarkSkippableJSHolders();
  }
}

/* static */
void XPCJSRuntime::GCSliceCallback(JSContext* cx, JS::GCProgress progress,
                                   const JS::GCDescription& desc) {
  XPCJSRuntime* self = nsXPConnect::GetRuntimeInstance();
  if (!self) {
    return;
  }

  nsCOMPtr<nsIObserverService> obs = xpc::GetObserverService();
  if (obs) {
    switch (progress) {
      case JS::GC_CYCLE_BEGIN:
        obs->NotifyObservers(nullptr, "garbage-collector-begin", nullptr);
        break;
      case JS::GC_CYCLE_END:
        obs->NotifyObservers(nullptr, "garbage-collector-end", nullptr);
        break;
      default:
        break;
    }
  }

  CrashReporter::SetGarbageCollecting(progress == JS::GC_CYCLE_BEGIN);

  if (self->mPrevGCSliceCallback) {
    (*self->mPrevGCSliceCallback)(cx, progress, desc);
  }
}

/* static */
void XPCJSRuntime::DoCycleCollectionCallback(JSContext* cx) {
  // The GC has detected that a CC at this point would collect a tremendous
  // amount of garbage that is being revivified unnecessarily.
  //
  // The GC_WAITING reason is a little overloaded here, but we want to do
  // a CC to allow Realms to be collected when they are referenced by a cycle.
  NS_DispatchToCurrentThread(NS_NewRunnableFunction(
      "XPCJSRuntime::DoCycleCollectionCallback",
      []() { nsJSContext::CycleCollectNow(CCReason::GC_WAITING, nullptr); }));

  XPCJSRuntime* self = nsXPConnect::GetRuntimeInstance();
  if (!self) {
    return;
  }

  if (self->mPrevDoCycleCollectionCallback) {
    (*self->mPrevDoCycleCollectionCallback)(cx);
  }
}

void XPCJSRuntime::CustomGCCallback(JSGCStatus status) {
  nsTArray<xpcGCCallback> callbacks(extraGCCallbacks.Clone());
  for (uint32_t i = 0; i < callbacks.Length(); ++i) {
    callbacks[i](status);
  }
}

/* static */
void XPCJSRuntime::FinalizeCallback(JS::GCContext* gcx, JSFinalizeStatus status,
                                    void* data) {
  XPCJSRuntime* self = nsXPConnect::GetRuntimeInstance();
  if (!self) {
    return;
  }

  switch (status) {
    case JSFINALIZE_GROUP_PREPARE: {
      MOZ_ASSERT(!self->mDoingFinalization, "bad state");

      MOZ_ASSERT(!self->mGCIsRunning, "bad state");
      self->mGCIsRunning = true;

      self->mDoingFinalization = true;

      break;
    }
    case JSFINALIZE_GROUP_START: {
      MOZ_ASSERT(self->mDoingFinalization, "bad state");

      MOZ_ASSERT(self->mGCIsRunning, "bad state");
      self->mGCIsRunning = false;

      break;
    }
    case JSFINALIZE_GROUP_END: {
      MOZ_ASSERT(self->mDoingFinalization, "bad state");
      self->mDoingFinalization = false;

      break;
    }
    case JSFINALIZE_COLLECTION_END: {
      MOZ_ASSERT(!self->mGCIsRunning, "bad state");
      self->mGCIsRunning = true;

      if (CycleCollectedJSContext* ccx = self->GetContext()) {
        const auto* cx = static_cast<const XPCJSContext*>(ccx);
        if (AutoMarkingPtr* roots = cx->mAutoRoots) {
          roots->MarkAfterJSFinalizeAll();
        }

        // Now we are going to recycle any unused WrappedNativeTearoffs.
        // We do this by iterating all the live callcontexts
        // and marking the tearoffs in use. And then we
        // iterate over all the WrappedNative wrappers and sweep their
        // tearoffs.
        //
        // This allows us to perhaps minimize the growth of the
        // tearoffs. And also makes us not hold references to interfaces
        // on our wrapped natives that we are not actually using.
        //
        // XXX We may decide to not do this on *every* gc cycle.

        XPCCallContext* ccxp = cx->GetCallContext();
        while (ccxp) {
          // Deal with the strictness of callcontext that
          // complains if you ask for a tearoff when
          // it is in a state where the tearoff could not
          // possibly be valid.
          if (ccxp->CanGetTearOff()) {
            XPCWrappedNativeTearOff* to = ccxp->GetTearOff();
            if (to) {
              to->Mark();
            }
          }
          ccxp = ccxp->GetPrevCallContext();
        }
      }

      XPCWrappedNativeScope::SweepAllWrappedNativeTearOffs();

      // Now we need to kill the 'Dying' XPCWrappedNativeProtos.
      //
      // We transferred these native objects to this list when their JSObjects
      // were finalized. We did not destroy them immediately at that point
      // because the ordering of JS finalization is not deterministic and we did
      // not yet know if any wrappers that might still be referencing the protos
      // were still yet to be finalized and destroyed. We *do* know that the
      // protos' JSObjects would not have been finalized if there were any
      // wrappers that referenced the proto but were not themselves slated for
      // finalization in this gc cycle.
      //
      // At this point we know that any and all wrappers that might have been
      // referencing the protos in the dying list are themselves dead. So, we
      // can safely delete all the protos in the list.
      self->mDyingWrappedNativeProtos.clear();

      MOZ_ASSERT(self->mGCIsRunning, "bad state");
      self->mGCIsRunning = false;

      break;
    }
  }
}

/* static */
void XPCJSRuntime::WeakPointerZonesCallback(JSTracer* trc, void* data) {
  // Called before each sweeping slice -- after processing any final marking
  // triggered by barriers -- to clear out any references to things that are
  // about to be finalized and update any pointers to moved GC things.
  XPCJSRuntime* self = static_cast<XPCJSRuntime*>(data);

  // This callback is always called from within the GC so set the mGCIsRunning
  // flag to prevent AssertInvalidWrappedJSNotInTable from trying to call back
  // into the JS API. This has often already been set by FinalizeCallback by the
  // time we get here, but it may not be if we are doing a shutdown GC or if we
  // are called for compacting GC.
  AutoRestore<bool> restoreState(self->mGCIsRunning);
  self->mGCIsRunning = true;

  self->mWrappedJSMap->UpdateWeakPointersAfterGC(trc);
  self->mUAWidgetScopeMap.traceWeak(trc);
}

/* static */
void XPCJSRuntime::WeakPointerCompartmentCallback(JSTracer* trc,
                                                  JS::Compartment* comp,
                                                  void* data) {
  // Called immediately after the ZoneGroup weak pointer callback, but only
  // once for each compartment that is being swept.
  CompartmentPrivate* xpcComp = CompartmentPrivate::Get(comp);
  if (xpcComp) {
    xpcComp->UpdateWeakPointersAfterGC(trc);
  }
}

void CompartmentPrivate::UpdateWeakPointersAfterGC(JSTracer* trc) {
  mRemoteProxies.traceWeak(trc);
  mWrappedJSMap->UpdateWeakPointersAfterGC(trc);
  mScope->UpdateWeakPointersAfterGC(trc);
}

void XPCJSRuntime::CustomOutOfMemoryCallback() {
  if (!Preferences::GetBool("memory.dump_reports_on_oom")) {
    return;
  }

  nsCOMPtr<nsIMemoryInfoDumper> dumper =
      do_GetService("@mozilla.org/memory-info-dumper;1");
  if (!dumper) {
    return;
  }

  // If this fails, it fails silently.
  dumper->DumpMemoryInfoToTempDir(u"due-to-JS-OOM"_ns,
                                  /* anonymize = */ false,
                                  /* minimizeMemoryUsage = */ false);
}

void XPCJSRuntime::OnLargeAllocationFailure() {
  CycleCollectedJSRuntime::SetLargeAllocationFailure(OOMState::Reporting);

  nsCOMPtr<nsIObserverService> os = xpc::GetObserverService();
  if (os) {
    os->NotifyObservers(nullptr, "memory-pressure", u"heap-minimize");
  }

  CycleCollectedJSRuntime::SetLargeAllocationFailure(OOMState::Reported);
}

class LargeAllocationFailureRunnable final : public Runnable {
  Mutex mMutex MOZ_UNANNOTATED;
  CondVar mCondVar;
  bool mWaiting;

  virtual ~LargeAllocationFailureRunnable() { MOZ_ASSERT(!mWaiting); }

 protected:
  NS_IMETHOD Run() override {
    MOZ_ASSERT(NS_IsMainThread());

    XPCJSRuntime::Get()->OnLargeAllocationFailure();

    MutexAutoLock lock(mMutex);
    MOZ_ASSERT(mWaiting);

    mWaiting = false;
    mCondVar.Notify();
    return NS_OK;
  }

 public:
  LargeAllocationFailureRunnable()
      : mozilla::Runnable("LargeAllocationFailureRunnable"),
        mMutex("LargeAllocationFailureRunnable::mMutex"),
        mCondVar(mMutex, "LargeAllocationFailureRunnable::mCondVar"),
        mWaiting(true) {
    MOZ_ASSERT(!NS_IsMainThread());
  }

  void BlockUntilDone() {
    MOZ_ASSERT(!NS_IsMainThread());

    MutexAutoLock lock(mMutex);
    while (mWaiting) {
      mCondVar.Wait();
    }
  }
};

static void OnLargeAllocationFailureCallback() {
  // This callback can be called from any thread, including internal JS helper
  // and DOM worker threads. We need to send the low-memory event via the
  // observer service which can only be called on the main thread, so proxy to
  // the main thread if we're not there already. The purpose of this callback
  // is to synchronously free some memory so the caller can retry a failed
  // allocation, so block on the completion.

  if (NS_IsMainThread()) {
    XPCJSRuntime::Get()->OnLargeAllocationFailure();
    return;
  }

  RefPtr<LargeAllocationFailureRunnable> r = new LargeAllocationFailureRunnable;
  if (NS_WARN_IF(NS_FAILED(NS_DispatchToMainThread(r)))) {
    return;
  }

  r->BlockUntilDone();
}

// Usually this is used through nsIPlatformInfo. However, being able to query
// this interface on all threads risk triggering some main-thread assertions
// which is not guaranteed by the callers of GetBuildId.
extern const char gToolkitBuildID[];

bool mozilla::GetBuildId(JS::BuildIdCharVector* aBuildID) {
  size_t length = std::char_traits<char>::length(gToolkitBuildID);
  return aBuildID->append(gToolkitBuildID, length);
}

size_t XPCJSRuntime::SizeOfIncludingThis(MallocSizeOf mallocSizeOf) {
  size_t n = 0;
  n += mallocSizeOf(this);
  n += mWrappedJSMap->SizeOfIncludingThis(mallocSizeOf);
  n += mIID2NativeInterfaceMap->SizeOfIncludingThis(mallocSizeOf);
  n += mClassInfo2NativeSetMap->ShallowSizeOfIncludingThis(mallocSizeOf);
  n += mNativeSetMap->SizeOfIncludingThis(mallocSizeOf);

  n += CycleCollectedJSRuntime::SizeOfExcludingThis(mallocSizeOf);

  // There are other XPCJSRuntime members that could be measured; the above
  // ones have been seen by DMD to be worth measuring.  More stuff may be
  // added later.

  return n;
}

size_t CompartmentPrivate::SizeOfIncludingThis(MallocSizeOf mallocSizeOf) {
  size_t n = mallocSizeOf(this);
  n += mWrappedJSMap->SizeOfIncludingThis(mallocSizeOf);
  n += mWrappedJSMap->SizeOfWrappedJS(mallocSizeOf);
  return n;
}

/***************************************************************************/

void XPCJSRuntime::Shutdown(JSContext* cx) {
  // This destructor runs before ~CycleCollectedJSContext, which does the actual
  // JS_DestroyContext() call. But destroying the context triggers one final GC,
  // which can call back into the context with various callbacks if we aren't
  // careful. Remove the relevant callbacks, but leave the weak pointer
  // callbacks to clear out any remaining table entries.
  JS_RemoveFinalizeCallback(cx, FinalizeCallback);
  xpc_DelocalizeRuntime(JS_GetRuntime(cx));

  JS::SetGCSliceCallback(cx, mPrevGCSliceCallback);

  nsScriptSecurityManager::ClearJSCallbacks(cx);

  // Clean up and destroy maps. Any remaining entries in mWrappedJSMap will be
  // cleaned up by the weak pointer callbacks.
  mIID2NativeInterfaceMap = nullptr;

  mClassInfo2NativeSetMap = nullptr;

  mNativeSetMap = nullptr;

  // Prevent ~LinkedList assertion failures if we leaked things.
  mWrappedNativeScopes.clear();

  mSubjectToFinalizationWJS.clear();

  CycleCollectedJSRuntime::Shutdown(cx);
}

XPCJSRuntime::~XPCJSRuntime() {
  MOZ_COUNT_DTOR_INHERITED(XPCJSRuntime, CycleCollectedJSRuntime);
}

// If |*anonymizeID| is non-zero and this is a user realm, the name will
// be anonymized.
static void GetRealmName(JS::Realm* realm, nsCString& name, int* anonymizeID,
                         bool replaceSlashes) {
  if (*anonymizeID && !js::IsSystemRealm(realm)) {
    name.AppendPrintf("<anonymized-%d>", *anonymizeID);
    *anonymizeID += 1;
  } else if (JSPrincipals* principals = JS::GetRealmPrincipals(realm)) {
    nsresult rv = nsJSPrincipals::get(principals)->GetScriptLocation(name);
    if (NS_FAILED(rv)) {
      name.AssignLiteral("(unknown)");
    }

    // If the realm's location (name) differs from the principal's script
    // location, append the realm's location to allow differentiation of
    // multiple realms owned by the same principal (e.g. components owned
    // by the system or null principal).
    RealmPrivate* realmPrivate = RealmPrivate::Get(realm);
    if (realmPrivate) {
      const nsACString& location = realmPrivate->GetLocation();
      if (!location.IsEmpty() && !location.Equals(name)) {
        name.AppendLiteral(", ");
        name.Append(location);
      }
    }

    if (*anonymizeID) {
      // We might have a file:// URL that includes a path from the local
      // filesystem, which should be omitted if we're anonymizing.
      static const char* filePrefix = "file://";
      int filePos = name.Find(filePrefix);
      if (filePos >= 0) {
        int pathPos = filePos + strlen(filePrefix);
        int lastSlashPos = -1;
        for (int i = pathPos; i < int(name.Length()); i++) {
          if (name[i] == '/' || name[i] == '\\') {
            lastSlashPos = i;
          }
        }
        if (lastSlashPos != -1) {
          name.ReplaceLiteral(pathPos, lastSlashPos - pathPos, "<anonymized>");
        } else {
          // Something went wrong. Anonymize the entire path to be
          // safe.
          name.Truncate(pathPos);
          name += "<anonymized?!>";
        }
      }

      // We might have a location like this:
      //   inProcessBrowserChildGlobal?ownedBy=http://www.example.com/
      // The owner should be omitted if it's not a chrome: URI and we're
      // anonymizing.
      static const char* ownedByPrefix = "inProcessBrowserChildGlobal?ownedBy=";
      int ownedByPos = name.Find(ownedByPrefix);
      if (ownedByPos >= 0) {
        const char* chrome = "chrome:";
        int ownerPos = ownedByPos + strlen(ownedByPrefix);
        const nsDependentCSubstring& ownerFirstPart =
            Substring(name, ownerPos, strlen(chrome));
        if (!ownerFirstPart.EqualsASCII(chrome)) {
          name.Truncate(ownerPos);
          name += "<anonymized>";
        }
      }
    }

    // A hack: replace forward slashes with '\\' so they aren't
    // treated as path separators.  Users of the reporters
    // (such as about:memory) have to undo this change.
    if (replaceSlashes) {
      name.ReplaceChar('/', '\\');
    }
  } else {
    name.AssignLiteral("null-principal");
  }
}

extern void xpc::GetCurrentRealmName(JSContext* cx, nsCString& name) {
  RootedObject global(cx, JS::CurrentGlobalOrNull(cx));
  if (!global) {
    name.AssignLiteral("no global");
    return;
  }

  JS::Realm* realm = GetNonCCWObjectRealm(global);
  int anonymizeID = 0;
  GetRealmName(realm, name, &anonymizeID, false);
}

void xpc::AddGCCallback(xpcGCCallback cb) {
  XPCJSRuntime::Get()->AddGCCallback(cb);
}

void xpc::RemoveGCCallback(xpcGCCallback cb) {
  XPCJSRuntime::Get()->RemoveGCCallback(cb);
}

static int64_t JSMainRuntimeGCHeapDistinguishedAmount() {
  JSContext* cx = danger::GetJSContext();
  return int64_t(JS_GetGCParameter(cx, JSGC_TOTAL_CHUNKS)) * js::gc::ChunkSize;
}

static int64_t JSMainRuntimeTemporaryPeakDistinguishedAmount() {
  JSContext* cx = danger::GetJSContext();
  return JS::PeakSizeOfTemporary(cx);
}

static int64_t JSMainRuntimeCompartmentsSystemDistinguishedAmount() {
  JSContext* cx = danger::GetJSContext();
  return JS::SystemCompartmentCount(cx);
}

static int64_t JSMainRuntimeCompartmentsUserDistinguishedAmount() {
  JSContext* cx = XPCJSContext::Get()->Context();
  return JS::UserCompartmentCount(cx);
}

static int64_t JSMainRuntimeRealmsSystemDistinguishedAmount() {
  JSContext* cx = danger::GetJSContext();
  return JS::SystemRealmCount(cx);
}

static int64_t JSMainRuntimeRealmsUserDistinguishedAmount() {
  JSContext* cx = XPCJSContext::Get()->Context();
  return JS::UserRealmCount(cx);
}

class JSMainRuntimeTemporaryPeakReporter final : public nsIMemoryReporter {
  ~JSMainRuntimeTemporaryPeakReporter() = default;

 public:
  NS_DECL_ISUPPORTS

  NS_IMETHOD CollectReports(nsIHandleReportCallback* aHandleReport,
                            nsISupports* aData, bool aAnonymize) override {
    MOZ_COLLECT_REPORT(
        "js-main-runtime-temporary-peak", KIND_OTHER, UNITS_BYTES,
        JSMainRuntimeTemporaryPeakDistinguishedAmount(),
        "Peak transient data size in the main JSRuntime (the current size "
        "of which is reported as "
        "'explicit/js-non-window/runtime/temporary').");

    return NS_OK;
  }
};

NS_IMPL_ISUPPORTS(JSMainRuntimeTemporaryPeakReporter, nsIMemoryReporter)

// The REPORT* macros do an unconditional report.  The ZRREPORT* macros are for
// realms and zones; they aggregate any entries smaller than
// SUNDRIES_THRESHOLD into the "sundries/gc-heap" and "sundries/malloc-heap"
// entries for the realm.

#define SUNDRIES_THRESHOLD js::MemoryReportingSundriesThreshold()

#define REPORT(_path, _kind, _units, _amount, _desc)             \
  handleReport->Callback(""_ns, _path, nsIMemoryReporter::_kind, \
                         nsIMemoryReporter::_units, _amount,     \
                         nsLiteralCString(_desc), data);

#define REPORT_BYTES(_path, _kind, _amount, _desc) \
  REPORT(_path, _kind, UNITS_BYTES, _amount, _desc);

#define REPORT_GC_BYTES(_path, _amount, _desc)                            \
  do {                                                                    \
    size_t amount = _amount; /* evaluate _amount only once */             \
    handleReport->Callback(""_ns, _path, nsIMemoryReporter::KIND_NONHEAP, \
                           nsIMemoryReporter::UNITS_BYTES, amount,        \
                           nsLiteralCString(_desc), data);                \
    gcTotal += amount;                                                    \
  } while (0)

// Report realm/zone non-GC (KIND_HEAP) bytes.
#define ZRREPORT_BYTES(_path, _amount, _desc)                            \
  do {                                                                   \
    /* Assign _descLiteral plus "" into a char* to prove that it's */    \
    /* actually a literal. */                                            \
    size_t amount = _amount; /* evaluate _amount only once */            \
    if (amount >= SUNDRIES_THRESHOLD) {                                  \
      handleReport->Callback(""_ns, _path, nsIMemoryReporter::KIND_HEAP, \
                             nsIMemoryReporter::UNITS_BYTES, amount,     \
                             nsLiteralCString(_desc), data);             \
    } else {                                                             \
      sundriesMallocHeap += amount;                                      \
    }                                                                    \
  } while (0)

// Report realm/zone GC bytes.
#define ZRREPORT_GC_BYTES(_path, _amount, _desc)                            \
  do {                                                                      \
    size_t amount = _amount; /* evaluate _amount only once */               \
    if (amount >= SUNDRIES_THRESHOLD) {                                     \
      handleReport->Callback(""_ns, _path, nsIMemoryReporter::KIND_NONHEAP, \
                             nsIMemoryReporter::UNITS_BYTES, amount,        \
                             nsLiteralCString(_desc), data);                \
      gcTotal += amount;                                                    \
    } else {                                                                \
      sundriesGCHeap += amount;                                             \
    }                                                                       \
  } while (0)

// Report realm/zone non-heap bytes.
#define ZRREPORT_NONHEAP_BYTES(_path, _amount, _desc)                       \
  do {                                                                      \
    size_t amount = _amount; /* evaluate _amount only once */               \
    if (amount >= SUNDRIES_THRESHOLD) {                                     \
      handleReport->Callback(""_ns, _path, nsIMemoryReporter::KIND_NONHEAP, \
                             nsIMemoryReporter::UNITS_BYTES, amount,        \
                             nsLiteralCString(_desc), data);                \
    } else {                                                                \
      sundriesNonHeap += amount;                                            \
    }                                                                       \
  } while (0)

// Report runtime bytes.
#define RREPORT_BYTES(_path, _kind, _amount, _desc)                \
  do {                                                             \
    size_t amount = _amount; /* evaluate _amount only once */      \
    handleReport->Callback(""_ns, _path, nsIMemoryReporter::_kind, \
                           nsIMemoryReporter::UNITS_BYTES, amount, \
                           nsLiteralCString(_desc), data);         \
    rtTotal += amount;                                             \
  } while (0)

// Report GC thing bytes.
#define MREPORT_BYTES(_path, _kind, _amount, _desc)                \
  do {                                                             \
    size_t amount = _amount; /* evaluate _amount only once */      \
    handleReport->Callback(""_ns, _path, nsIMemoryReporter::_kind, \
                           nsIMemoryReporter::UNITS_BYTES, amount, \
                           nsLiteralCString(_desc), data);         \
    gcThingTotal += amount;                                        \
  } while (0)

MOZ_DEFINE_MALLOC_SIZE_OF(JSMallocSizeOf)

namespace xpc {

static void ReportZoneStats(const JS::ZoneStats& zStats,
                            const xpc::ZoneStatsExtras& extras,
                            nsIHandleReportCallback* handleReport,
                            nsISupports* data, bool anonymize,
                            size_t* gcTotalOut = nullptr) {
  const nsCString& pathPrefix = extras.pathPrefix;
  size_t gcTotal = 0;
  size_t sundriesGCHeap = 0;
  size_t sundriesMallocHeap = 0;
  size_t sundriesNonHeap = 0;

  MOZ_ASSERT(!gcTotalOut == zStats.isTotals);

  ZRREPORT_GC_BYTES(pathPrefix + "symbols/gc-heap"_ns, zStats.symbolsGCHeap,
                    "Symbols.");

  ZRREPORT_GC_BYTES(
      pathPrefix + "gc-heap-arena-admin"_ns, zStats.gcHeapArenaAdmin,
      "Bookkeeping information and alignment padding within GC arenas.");

  ZRREPORT_GC_BYTES(pathPrefix + "unused-gc-things"_ns,
                    zStats.unusedGCThings.totalSize(),
                    "Unused GC thing cells within non-empty arenas.");

  ZRREPORT_BYTES(pathPrefix + "unique-id-map"_ns, zStats.uniqueIdMap,
                 "Address-independent cell identities.");

  ZRREPORT_BYTES(pathPrefix + "propmap-tables"_ns, zStats.initialPropMapTable,
                 "Tables storing property map information.");

  ZRREPORT_BYTES(pathPrefix + "shape-tables"_ns, zStats.shapeTables,
                 "Tables storing shape information.");

  ZRREPORT_BYTES(pathPrefix + "compartments/compartment-objects"_ns,
                 zStats.compartmentObjects,
                 "The JS::Compartment objects in this zone.");

  ZRREPORT_BYTES(
      pathPrefix + "compartments/cross-compartment-wrapper-tables"_ns,
      zStats.crossCompartmentWrappersTables,
      "The cross-compartment wrapper tables.");

  ZRREPORT_BYTES(
      pathPrefix + "compartments/private-data"_ns,
      zStats.compartmentsPrivateData,
      "Extra data attached to each compartment by XPConnect, including "
      "its wrapped-js.");

  ZRREPORT_GC_BYTES(pathPrefix + "bigints/gc-heap"_ns, zStats.bigIntsGCHeap,
                    "BigInt values.");

  ZRREPORT_BYTES(pathPrefix + "bigints/malloc-heap"_ns,
                 zStats.bigIntsMallocHeap, "BigInt values.");

  ZRREPORT_GC_BYTES(pathPrefix + "jit-codes-gc-heap"_ns, zStats.jitCodesGCHeap,
                    "References to executable code pools used by the JITs.");

  ZRREPORT_GC_BYTES(pathPrefix + "getter-setters-gc-heap"_ns,
                    zStats.getterSettersGCHeap,
                    "Information for getter/setter properties.");

  ZRREPORT_GC_BYTES(pathPrefix + "property-maps/gc-heap/compact"_ns,
                    zStats.compactPropMapsGCHeap,
                    "Information about object properties.");

  ZRREPORT_GC_BYTES(pathPrefix + "property-maps/gc-heap/normal"_ns,
                    zStats.normalPropMapsGCHeap,
                    "Information about object properties.");

  ZRREPORT_GC_BYTES(pathPrefix + "property-maps/gc-heap/dict"_ns,
                    zStats.dictPropMapsGCHeap,
                    "Information about dictionary mode object properties.");

  ZRREPORT_BYTES(pathPrefix + "property-maps/malloc-heap/children"_ns,
                 zStats.propMapChildren, "Tables for PropMap children.");

  ZRREPORT_BYTES(pathPrefix + "property-maps/malloc-heap/tables"_ns,
                 zStats.propMapTables, "HashTables for PropMaps.");

  ZRREPORT_GC_BYTES(pathPrefix + "scopes/gc-heap"_ns, zStats.scopesGCHeap,
                    "Scope information for scripts.");

  ZRREPORT_BYTES(pathPrefix + "scopes/malloc-heap"_ns, zStats.scopesMallocHeap,
                 "Arrays of binding names and other binding-related data.");

  ZRREPORT_GC_BYTES(pathPrefix + "regexp-shareds/gc-heap"_ns,
                    zStats.regExpSharedsGCHeap, "Shared compiled regexp data.");

  ZRREPORT_BYTES(pathPrefix + "regexp-shareds/malloc-heap"_ns,
                 zStats.regExpSharedsMallocHeap,
                 "Shared compiled regexp data.");

  // zStats.smallBuffersGCHeap is not reported as a separate item here as it's
  // reported as part of the owning cell. We must still count it as part of the
  // total heap size.
  gcTotal += zStats.smallBuffersGCHeap;

  ZRREPORT_BYTES(pathPrefix + "zone-object"_ns, zStats.zoneObject,
                 "The JS::Zone object itself.");

  ZRREPORT_BYTES(pathPrefix + "regexp-zone"_ns, zStats.regexpZone,
                 "The regexp zone and regexp data.");

  ZRREPORT_BYTES(pathPrefix + "jit-zone"_ns, zStats.jitZone, "The JIT zone.");

  ZRREPORT_BYTES(pathPrefix + "cacheir-stubs"_ns, zStats.cacheIRStubs,
                 "The JIT's IC stubs (excluding code).");

  ZRREPORT_BYTES(pathPrefix + "script-counts-map"_ns, zStats.scriptCountsMap,
                 "Profiling-related information for scripts.");

  ZRREPORT_NONHEAP_BYTES(pathPrefix + "code/ion"_ns, zStats.code.ion,
                         "Code generated by the IonMonkey JIT.");

  ZRREPORT_NONHEAP_BYTES(pathPrefix + "code/baseline"_ns, zStats.code.baseline,
                         "Code generated by the Baseline JIT.");

  ZRREPORT_NONHEAP_BYTES(pathPrefix + "code/regexp"_ns, zStats.code.regexp,
                         "Code generated by the regexp JIT.");

  ZRREPORT_NONHEAP_BYTES(
      pathPrefix + "code/other"_ns, zStats.code.other,
      "Code generated by the JITs for wrappers and trampolines.");

  ZRREPORT_NONHEAP_BYTES(pathPrefix + "code/unused"_ns, zStats.code.unused,
                         "Memory allocated by one of the JITs to hold code, "
                         "but which is currently unused.");

  size_t stringsNotableAboutMemoryGCHeap = 0;
  size_t stringsNotableAboutMemoryMallocHeap = 0;

#define MAYBE_INLINE "The characters may be inline or on the malloc heap."
#define MAYBE_OVERALLOCATED \
  "Sometimes over-allocated to simplify string concatenation."

  for (size_t i = 0; i < zStats.notableStrings.length(); i++) {
    const JS::NotableStringInfo& info = zStats.notableStrings[i];

    MOZ_ASSERT(!zStats.isTotals);

    // We don't do notable string detection when anonymizing, because
    // there's a good chance its for crash submission, and the memory
    // required for notable string detection is high.
    MOZ_ASSERT(!anonymize);

    nsDependentCString notableString(info.buffer.get());

    // Viewing about:memory generates many notable strings which contain
    // "string(length=".  If we report these as notable, then we'll create
    // even more notable strings the next time we open about:memory (unless
    // there's a GC in the meantime), and so on ad infinitum.
    //
    // To avoid cluttering up about:memory like this, we stick notable
    // strings which contain "string(length=" into their own bucket.
#define STRING_LENGTH "string(length="
    if (FindInReadable(nsLiteralCString(STRING_LENGTH), notableString)) {
      stringsNotableAboutMemoryGCHeap += info.gcHeapLatin1;
      stringsNotableAboutMemoryGCHeap += info.gcHeapTwoByte;
      stringsNotableAboutMemoryMallocHeap += info.mallocHeapLatin1;
      stringsNotableAboutMemoryMallocHeap += info.mallocHeapTwoByte;
      continue;
    }

    // Escape / to \ before we put notableString into the memory reporter
    // path, because we don't want any forward slashes in the string to
    // count as path separators.
    nsCString escapedString(notableString);
    escapedString.ReplaceSubstring("/", "\\");

    bool truncated = notableString.Length() < info.length;

    nsCString path =
        pathPrefix +
        nsPrintfCString("strings/" STRING_LENGTH "%zu, copies=%d, \"%s\"%s)/",
                        info.length, info.numCopies, escapedString.get(),
                        truncated ? " (truncated)" : "");

    if (info.gcHeapLatin1 > 0) {
      REPORT_GC_BYTES(path + "gc-heap/latin1"_ns, info.gcHeapLatin1,
                      "Latin1 strings. " MAYBE_INLINE);
    }

    if (info.gcHeapTwoByte > 0) {
      REPORT_GC_BYTES(path + "gc-heap/two-byte"_ns, info.gcHeapTwoByte,
                      "TwoByte strings. " MAYBE_INLINE);
    }

    if (info.mallocHeapLatin1 > 0) {
      REPORT_BYTES(path + "malloc-heap/latin1"_ns, KIND_HEAP,
                   info.mallocHeapLatin1,
                   "Non-inline Latin1 string characters. " MAYBE_OVERALLOCATED);
    }

    if (info.mallocHeapTwoByte > 0) {
      REPORT_BYTES(
          path + "malloc-heap/two-byte"_ns, KIND_HEAP, info.mallocHeapTwoByte,
          "Non-inline TwoByte string characters. " MAYBE_OVERALLOCATED);
    }
  }

  nsCString nonNotablePath = pathPrefix;
  nonNotablePath += (zStats.isTotals || anonymize)
                        ? "strings/"_ns
                        : "strings/string(<non-notable strings>)/"_ns;

  if (zStats.stringInfo.gcHeapLatin1 > 0) {
    REPORT_GC_BYTES(nonNotablePath + "gc-heap/latin1"_ns,
                    zStats.stringInfo.gcHeapLatin1,
                    "Latin1 strings. " MAYBE_INLINE);
  }

  if (zStats.stringInfo.gcHeapTwoByte > 0) {
    REPORT_GC_BYTES(nonNotablePath + "gc-heap/two-byte"_ns,
                    zStats.stringInfo.gcHeapTwoByte,
                    "TwoByte strings. " MAYBE_INLINE);
  }

  if (zStats.stringInfo.mallocHeapLatin1 > 0) {
    REPORT_BYTES(nonNotablePath + "malloc-heap/latin1"_ns, KIND_HEAP,
                 zStats.stringInfo.mallocHeapLatin1,
                 "Non-inline Latin1 string characters. " MAYBE_OVERALLOCATED);
  }

  if (zStats.stringInfo.mallocHeapTwoByte > 0) {
    REPORT_BYTES(nonNotablePath + "malloc-heap/two-byte"_ns, KIND_HEAP,
                 zStats.stringInfo.mallocHeapTwoByte,
                 "Non-inline TwoByte string characters. " MAYBE_OVERALLOCATED);
  }

  if (stringsNotableAboutMemoryGCHeap > 0) {
    MOZ_ASSERT(!zStats.isTotals);
    REPORT_GC_BYTES(
        pathPrefix + "strings/string(<about-memory>)/gc-heap"_ns,
        stringsNotableAboutMemoryGCHeap,
        "Strings that contain the characters '" STRING_LENGTH
        "', which "
        "are probably from about:memory itself." MAYBE_INLINE
        " We filter them out rather than display them, because displaying "
        "them would create even more such strings every time about:memory "
        "is refreshed.");
  }

  if (stringsNotableAboutMemoryMallocHeap > 0) {
    MOZ_ASSERT(!zStats.isTotals);
    REPORT_BYTES(
        pathPrefix + "strings/string(<about-memory>)/malloc-heap"_ns, KIND_HEAP,
        stringsNotableAboutMemoryMallocHeap,
        "Non-inline string characters of strings that contain the "
        "characters '" STRING_LENGTH
        "', which are probably from "
        "about:memory itself. " MAYBE_OVERALLOCATED
        " We filter them out rather than display them, because displaying "
        "them would create even more such strings every time about:memory "
        "is refreshed.");
  }

  const JS::ShapeInfo& shapeInfo = zStats.shapeInfo;
  if (shapeInfo.shapesGCHeapShared > 0) {
    REPORT_GC_BYTES(pathPrefix + "shapes/gc-heap/shared"_ns,
                    shapeInfo.shapesGCHeapShared, "Shared shapes.");
  }

  if (shapeInfo.shapesGCHeapDict > 0) {
    REPORT_GC_BYTES(pathPrefix + "shapes/gc-heap/dict"_ns,
                    shapeInfo.shapesGCHeapDict, "Shapes in dictionary mode.");
  }

  if (shapeInfo.shapesGCHeapBase > 0) {
    REPORT_GC_BYTES(pathPrefix + "shapes/gc-heap/base"_ns,
                    shapeInfo.shapesGCHeapBase,
                    "Base shapes, which collate data common to many shapes.");
  }

  if (shapeInfo.shapesMallocHeapCache > 0) {
    REPORT_BYTES(pathPrefix + "shapes/malloc-heap/shape-cache"_ns, KIND_HEAP,
                 shapeInfo.shapesMallocHeapCache,
                 "Shape cache hash set for adding properties.");
  }

  if (sundriesGCHeap > 0) {
    // We deliberately don't use ZRREPORT_GC_BYTES here.
    REPORT_GC_BYTES(
        pathPrefix + "sundries/gc-heap"_ns, sundriesGCHeap,
        "The sum of all 'gc-heap' measurements that are too small to be "
        "worth showing individually.");
  }

  if (sundriesMallocHeap > 0) {
    // We deliberately don't use ZRREPORT_BYTES here.
    REPORT_BYTES(
        pathPrefix + "sundries/malloc-heap"_ns, KIND_HEAP, sundriesMallocHeap,
        "The sum of all 'malloc-heap' measurements that are too small to "
        "be worth showing individually.");
  }

  if (sundriesNonHeap > 0) {
    // We deliberately don't use ZRREPORT_NONHEAP_BYTES here.
    REPORT_BYTES(pathPrefix + "sundries/other-heap"_ns, KIND_NONHEAP,
                 sundriesNonHeap,
                 "The sum of non-malloc/gc measurements that are too small to "
                 "be worth showing individually.");
  }

  if (gcTotalOut) {
    *gcTotalOut += gcTotal;
  }

#undef STRING_LENGTH
}

static void ReportClassStats(const ClassInfo& classInfo, const nsACString& path,
                             nsIHandleReportCallback* handleReport,
                             nsISupports* data, size_t& gcTotal) {
  // We deliberately don't use ZRREPORT_BYTES, so that these per-class values
  // don't go into sundries.

  if (classInfo.objectsGCHeap > 0) {
    REPORT_GC_BYTES(path + "objects/gc-heap"_ns, classInfo.objectsGCHeap,
                    "Objects, including fixed slots.");
  }

  if (classInfo.objectsMallocHeapSlots > 0) {
    REPORT_BYTES(path + "objects/gc-buffers/slots"_ns, KIND_NONHEAP,
                 classInfo.objectsMallocHeapSlots, "Non-fixed object slots.");
  }

  if (classInfo.objectsMallocHeapElementsNormal > 0) {
    REPORT_BYTES(path + "objects/gc-buffers/elements/normal"_ns, KIND_NONHEAP,
                 classInfo.objectsMallocHeapElementsNormal,
                 "Normal (non-wasm) indexed elements.");
  }

  if (classInfo.objectsMallocHeapElementsAsmJS > 0) {
    REPORT_BYTES(path + "objects/malloc-heap/elements/asm.js"_ns, KIND_HEAP,
                 classInfo.objectsMallocHeapElementsAsmJS,
                 "asm.js array buffer elements allocated in the malloc heap.");
  }

  if (classInfo.objectsMallocHeapGlobalData > 0) {
    REPORT_BYTES(path + "objects/malloc-heap/global-data"_ns, KIND_HEAP,
                 classInfo.objectsMallocHeapGlobalData,
                 "Data for global objects.");
  }

  if (classInfo.objectsMallocHeapMisc > 0) {
    REPORT_BYTES(path + "objects/malloc-heap/misc"_ns, KIND_HEAP,
                 classInfo.objectsMallocHeapMisc, "Miscellaneous object data.");
  }

  if (classInfo.objectsNonHeapElementsNormal > 0) {
    REPORT_BYTES(path + "objects/non-heap/elements/normal"_ns, KIND_NONHEAP,
                 classInfo.objectsNonHeapElementsNormal,
                 "Memory-mapped non-shared array buffer elements.");
  }

  if (classInfo.objectsNonHeapElementsShared > 0) {
    REPORT_BYTES(
        path + "objects/non-heap/elements/shared"_ns, KIND_NONHEAP,
        classInfo.objectsNonHeapElementsShared,
        "Memory-mapped shared array buffer elements. These elements are "
        "shared between one or more runtimes; the reported size is divided "
        "by the buffer's refcount.");
  }

  // WebAssembly memories are always non-heap-allocated (mmap). We never put
  // these under sundries, because (a) in practice they're almost always
  // larger than the sundries threshold, and (b) we'd need a third category of
  // sundries ("non-heap"), which would be a pain.
  if (classInfo.objectsNonHeapElementsWasm > 0) {
    REPORT_BYTES(path + "objects/non-heap/elements/wasm"_ns, KIND_NONHEAP,
                 classInfo.objectsNonHeapElementsWasm,
                 "wasm/asm.js array buffer elements allocated outside both the "
                 "malloc heap and the GC heap.");
  }
  if (classInfo.objectsNonHeapElementsWasmShared > 0) {
    REPORT_BYTES(
        path + "objects/non-heap/elements/wasm-shared"_ns, KIND_NONHEAP,
        classInfo.objectsNonHeapElementsWasmShared,
        "wasm/asm.js array buffer elements allocated outside both the "
        "malloc heap and the GC heap. These elements are shared between "
        "one or more runtimes; the reported size is divided by the "
        "buffer's refcount.");
  }

  if (classInfo.objectsNonHeapCodeWasm > 0) {
    REPORT_BYTES(path + "objects/non-heap/code/wasm"_ns, KIND_NONHEAP,
                 classInfo.objectsNonHeapCodeWasm,
                 "AOT-compiled wasm/asm.js code.");
  }
}

static void ReportRealmStats(const JS::RealmStats& realmStats,
                             const xpc::RealmStatsExtras& extras,
                             nsIHandleReportCallback* handleReport,
                             nsISupports* data, size_t* gcTotalOut = nullptr) {
  static const nsDependentCString addonPrefix("explicit/add-ons/");

  size_t gcTotal = 0, sundriesGCHeap = 0, sundriesMallocHeap = 0;
  nsAutoCString realmJSPathPrefix(extras.jsPathPrefix);
  nsAutoCString realmDOMPathPrefix(extras.domPathPrefix);

  MOZ_ASSERT(!gcTotalOut == realmStats.isTotals);

  nsCString nonNotablePath = realmJSPathPrefix;
  nonNotablePath += realmStats.isTotals
                        ? "classes/"_ns
                        : "classes/class(<non-notable classes>)/"_ns;

  ReportClassStats(realmStats.classInfo, nonNotablePath, handleReport, data,
                   gcTotal);

  for (size_t i = 0; i < realmStats.notableClasses.length(); i++) {
    MOZ_ASSERT(!realmStats.isTotals);
    const JS::NotableClassInfo& classInfo = realmStats.notableClasses[i];

    nsCString classPath =
        realmJSPathPrefix +
        nsPrintfCString("classes/class(%s)/", classInfo.className_.get());

    ReportClassStats(classInfo, classPath, handleReport, data, gcTotal);
  }

  // Note that we use realmDOMPathPrefix here.  This is because we measure
  // orphan DOM nodes in the JS reporter, but we want to report them in a "dom"
  // sub-tree rather than a "js" sub-tree.
  ZRREPORT_BYTES(
      realmDOMPathPrefix + "orphan-nodes"_ns, realmStats.objectsPrivate,
      "Orphan DOM nodes, i.e. those that are only reachable from JavaScript "
      "objects.");

  ZRREPORT_GC_BYTES(
      realmJSPathPrefix + "scripts/gc-heap"_ns, realmStats.scriptsGCHeap,
      "JSScript instances. There is one per user-defined function in a "
      "script, and one for the top-level code in a script.");

  ZRREPORT_BYTES(realmJSPathPrefix + "scripts/malloc-heap/data"_ns,
                 realmStats.scriptsMallocHeapData,
                 "Various variable-length tables in JSScripts.");

  ZRREPORT_BYTES(realmJSPathPrefix + "baseline/data"_ns,
                 realmStats.baselineData,
                 "The Baseline JIT's compilation data (BaselineScripts).");

  ZRREPORT_BYTES(realmJSPathPrefix + "alloc-sites"_ns, realmStats.allocSites,
                 "GC allocation site data associated with IC stubs.");

  ZRREPORT_BYTES(realmJSPathPrefix + "ion-data"_ns, realmStats.ionData,
                 "The IonMonkey JIT's compilation data (IonScripts).");

  ZRREPORT_BYTES(realmJSPathPrefix + "jit-scripts"_ns, realmStats.jitScripts,
                 "JIT data associated with scripts.");

  ZRREPORT_BYTES(realmJSPathPrefix + "realm-object"_ns, realmStats.realmObject,
                 "The JS::Realm object itself.");

  ZRREPORT_BYTES(
      realmJSPathPrefix + "realm-tables"_ns, realmStats.realmTables,
      "Realm-wide tables storing object group information and wasm instances.");

  ZRREPORT_BYTES(realmJSPathPrefix + "inner-views"_ns,
                 realmStats.innerViewsTable,
                 "The table for array buffer inner views.");

  ZRREPORT_BYTES(
      realmJSPathPrefix + "object-metadata"_ns, realmStats.objectMetadataTable,
      "The table used by debugging tools for tracking object metadata");

  ZRREPORT_BYTES(realmJSPathPrefix + "saved-stacks-set"_ns,
                 realmStats.savedStacksSet, "The saved stacks set.");

  ZRREPORT_BYTES(realmJSPathPrefix + "non-syntactic-lexical-scopes-table"_ns,
                 realmStats.nonSyntacticLexicalScopesTable,
                 "The non-syntactic lexical scopes table.");

  if (sundriesGCHeap > 0) {
    // We deliberately don't use ZRREPORT_GC_BYTES here.
    REPORT_GC_BYTES(
        realmJSPathPrefix + "sundries/gc-heap"_ns, sundriesGCHeap,
        "The sum of all 'gc-heap' measurements that are too small to be "
        "worth showing individually.");
  }

  if (sundriesMallocHeap > 0) {
    // We deliberately don't use ZRREPORT_BYTES here.
    REPORT_BYTES(
        realmJSPathPrefix + "sundries/malloc-heap"_ns, KIND_HEAP,
        sundriesMallocHeap,
        "The sum of all 'malloc-heap' measurements that are too small to "
        "be worth showing individually.");
  }

  if (gcTotalOut) {
    *gcTotalOut += gcTotal;
  }
}

static void ReportScriptSourceStats(const ScriptSourceInfo& scriptSourceInfo,
                                    const nsACString& path,
                                    nsIHandleReportCallback* handleReport,
                                    nsISupports* data, size_t& rtTotal) {
  if (scriptSourceInfo.misc > 0) {
    RREPORT_BYTES(path + "misc"_ns, KIND_HEAP, scriptSourceInfo.misc,
                  "Miscellaneous data relating to JavaScript source code.");
  }
}

void ReportJSRuntimeExplicitTreeStats(const JS::RuntimeStats& rtStats,
                                      const nsACString& rtPath,
                                      nsIHandleReportCallback* handleReport,
                                      nsISupports* data, bool anonymize,
                                      size_t* rtTotalOut) {
  size_t gcTotal = 0;

  for (const auto& zStats : rtStats.zoneStatsVector) {
    const xpc::ZoneStatsExtras* extras =
        static_cast<const xpc::ZoneStatsExtras*>(zStats.extra);
    ReportZoneStats(zStats, *extras, handleReport, data, anonymize, &gcTotal);
  }

  for (const auto& realmStats : rtStats.realmStatsVector) {
    const xpc::RealmStatsExtras* extras =
        static_cast<const xpc::RealmStatsExtras*>(realmStats.extra);

    ReportRealmStats(realmStats, *extras, handleReport, data, &gcTotal);
  }

  // Report the rtStats.runtime numbers under "runtime/", and compute their
  // total for later.

  size_t rtTotal = 0;

  RREPORT_BYTES(rtPath + "runtime/runtime-object"_ns, KIND_HEAP,
                rtStats.runtime.object, "The JSRuntime object.");

  RREPORT_BYTES(rtPath + "runtime/atoms-table"_ns, KIND_HEAP,
                rtStats.runtime.atomsTable, "The atoms table.");

  RREPORT_BYTES(rtPath + "runtime/atoms-mark-bitmaps"_ns, KIND_HEAP,
                rtStats.runtime.atomsMarkBitmaps,
                "Mark bitmaps for atoms held by each zone.");

  RREPORT_BYTES(rtPath + "runtime/self-host-stencil"_ns, KIND_HEAP,
                rtStats.runtime.selfHostStencil,
                "The self-hosting CompilationStencil.");

  RREPORT_BYTES(rtPath + "runtime/contexts"_ns, KIND_HEAP,
                rtStats.runtime.contexts,
                "JSContext objects and structures that belong to them.");

  RREPORT_BYTES(
      rtPath + "runtime/temporary"_ns, KIND_HEAP, rtStats.runtime.temporary,
      "Transient data (mostly parse nodes) held by the JSRuntime during "
      "compilation.");

  RREPORT_BYTES(rtPath + "runtime/interpreter-stack"_ns, KIND_HEAP,
                rtStats.runtime.interpreterStack, "JS interpreter frames.");

  RREPORT_BYTES(
      rtPath + "runtime/shared-immutable-strings-cache"_ns, KIND_HEAP,
      rtStats.runtime.sharedImmutableStringsCache,
      "Immutable strings (such as JS scripts' source text) shared across all "
      "JSRuntimes.");

  RREPORT_BYTES(rtPath + "runtime/shared-intl-data"_ns, KIND_HEAP,
                rtStats.runtime.sharedIntlData,
                "Shared internationalization data.");

  RREPORT_BYTES(rtPath + "runtime/uncompressed-source-cache"_ns, KIND_HEAP,
                rtStats.runtime.uncompressedSourceCache,
                "The uncompressed source code cache.");

  RREPORT_BYTES(rtPath + "runtime/script-data"_ns, KIND_HEAP,
                rtStats.runtime.scriptData,
                "The table holding script data shared in the runtime.");

  nsCString nonNotablePath =
      rtPath +
      nsPrintfCString(
          "runtime/script-sources/source(scripts=%d, <non-notable files>)/",
          rtStats.runtime.scriptSourceInfo.numScripts);

  ReportScriptSourceStats(rtStats.runtime.scriptSourceInfo, nonNotablePath,
                          handleReport, data, rtTotal);

  for (size_t i = 0; i < rtStats.runtime.notableScriptSources.length(); i++) {
    const JS::NotableScriptSourceInfo& scriptSourceInfo =
        rtStats.runtime.notableScriptSources[i];

    // Escape / to \ before we put the filename into the memory reporter
    // path, because we don't want any forward slashes in the string to
    // count as path separators. Consumers of memory reporters (e.g.
    // about:memory) will convert them back to / after doing path
    // splitting.
    nsCString escapedFilename;
    if (anonymize) {
      escapedFilename.AppendPrintf("<anonymized-source-%d>", int(i));
    } else {
      nsDependentCString filename(scriptSourceInfo.filename_.get());
      escapedFilename.Append(filename);
      escapedFilename.ReplaceSubstring("/", "\\");
    }

    nsCString notablePath =
        rtPath +
        nsPrintfCString("runtime/script-sources/source(scripts=%d, %s)/",
                        scriptSourceInfo.numScripts, escapedFilename.get());

    ReportScriptSourceStats(scriptSourceInfo, notablePath, handleReport, data,
                            rtTotal);
  }

  RREPORT_BYTES(rtPath + "runtime/gc/marker"_ns, KIND_HEAP,
                rtStats.runtime.gc.marker, "The GC mark stack and gray roots.");

  RREPORT_BYTES(rtPath + "runtime/gc/nursery-committed"_ns, KIND_NONHEAP,
                rtStats.runtime.gc.nurseryCommitted,
                "Memory being used by the GC's nursery.");

  RREPORT_BYTES(
      rtPath + "runtime/gc/nursery-malloced-buffers"_ns, KIND_HEAP,
      rtStats.runtime.gc.nurseryMallocedBuffers,
      "Out-of-line slots and elements belonging to objects in the nursery.");

  RREPORT_BYTES(rtPath + "runtime/gc/store-buffer/vals"_ns, KIND_HEAP,
                rtStats.runtime.gc.storeBufferVals,
                "Values in the store buffer.");

  RREPORT_BYTES(rtPath + "runtime/gc/store-buffer/cells"_ns, KIND_HEAP,
                rtStats.runtime.gc.storeBufferCells,
                "Cells in the store buffer.");

  RREPORT_BYTES(rtPath + "runtime/gc/store-buffer/slots"_ns, KIND_HEAP,
                rtStats.runtime.gc.storeBufferSlots,
                "Slots in the store buffer.");

  RREPORT_BYTES(rtPath + "runtime/gc/store-buffer/whole-cells"_ns, KIND_HEAP,
                rtStats.runtime.gc.storeBufferWholeCells,
                "Whole cells in the store buffer.");

  RREPORT_BYTES(rtPath + "runtime/gc/store-buffer/generics"_ns, KIND_HEAP,
                rtStats.runtime.gc.storeBufferGenerics,
                "Generic things in the store buffer.");

  RREPORT_BYTES(rtPath + "runtime/jit-lazylink"_ns, KIND_HEAP,
                rtStats.runtime.jitLazyLink,
                "IonMonkey compilations waiting for lazy linking.");

  if (rtTotalOut) {
    *rtTotalOut = rtTotal;
  }

  // Report GC numbers that don't belong to a realm.

  // We don't want to report decommitted memory in "explicit", so we just
  // change the leading "explicit/" to "decommitted/".
  nsCString rtPath2(rtPath);
  rtPath2.ReplaceLiteral(0, strlen("explicit"), "decommitted");

  REPORT_GC_BYTES(
      rtPath2 + "gc-heap/decommitted-pages"_ns, rtStats.gcHeapDecommittedPages,
      "GC arenas in non-empty chunks that is decommitted, i.e. it takes up "
      "address space but no physical memory or swap space.");

  REPORT_GC_BYTES(
      rtPath + "gc-heap/unused-chunks"_ns, rtStats.gcHeapUnusedChunks,
      "Empty GC chunks which will soon be released unless claimed for new "
      "allocations.");

  REPORT_GC_BYTES(rtPath + "gc-heap/unused-arenas"_ns,
                  rtStats.gcHeapUnusedArenas,
                  "Empty GC arenas within non-empty chunks.");

  REPORT_GC_BYTES(rtPath + "gc-heap/chunk-admin"_ns, rtStats.gcHeapChunkAdmin,
                  "Bookkeeping information within GC chunks.");

  // gcTotal is the sum of everything we've reported for the GC heap.  It
  // should equal rtStats.gcHeapChunkTotal.
  MOZ_ASSERT(gcTotal == rtStats.gcHeapChunkTotal);
}

}  // namespace xpc

class JSMainRuntimeRealmsReporter final : public nsIMemoryReporter {
  ~JSMainRuntimeRealmsReporter() = default;

 public:
  NS_DECL_ISUPPORTS

  struct Data {
    int anonymizeID;
    js::Vector<nsCString, 0, js::SystemAllocPolicy> paths;
  };

  static void RealmCallback(JSContext* cx, void* vdata, Realm* realm,
                            const JS::AutoRequireNoGC& nogc) {
    // silently ignore OOM errors
    Data* data = static_cast<Data*>(vdata);
    nsCString path;
    GetRealmName(realm, path, &data->anonymizeID, /* replaceSlashes = */ true);
    path.Insert(js::IsSystemRealm(realm) ? "js-main-runtime-realms/system/"_ns
                                         : "js-main-runtime-realms/user/"_ns,
                0);
    mozilla::Unused << data->paths.append(path);
  }

  NS_IMETHOD CollectReports(nsIHandleReportCallback* handleReport,
                            nsISupports* data, bool anonymize) override {
    // First we collect the realm paths.  Then we report them.  Doing
    // the two steps interleaved is a bad idea, because calling
    // |handleReport| from within RealmCallback() leads to all manner
    // of assertions.

    Data d;
    d.anonymizeID = anonymize ? 1 : 0;
    JS::IterateRealms(XPCJSContext::Get()->Context(), &d, RealmCallback);

    for (auto& path : d.paths) {
      REPORT(nsCString(path), KIND_OTHER, UNITS_COUNT, 1,
             "A live realm in the main JSRuntime.");
    }

    return NS_OK;
  }
};

NS_IMPL_ISUPPORTS(JSMainRuntimeRealmsReporter, nsIMemoryReporter)

MOZ_DEFINE_MALLOC_SIZE_OF(OrphanMallocSizeOf)

namespace xpc {

class OrphanReporter : public JS::ObjectPrivateVisitor {
 public:
  explicit OrphanReporter(GetISupportsFun aGetISupports)
      : JS::ObjectPrivateVisitor(aGetISupports), mState(OrphanMallocSizeOf) {}

  virtual size_t sizeOfIncludingThis(nsISupports* aSupports) override {
    nsCOMPtr<nsINode> node = do_QueryInterface(aSupports);
    if (!node || node->IsInComposedDoc()) {
      return 0;
    }

    // This is an orphan node.  If we haven't already handled the sub-tree that
    // this node belongs to, measure the sub-tree's size and then record its
    // root so we don't measure it again.
    nsCOMPtr<nsINode> orphanTree = node->SubtreeRoot();
    if (!orphanTree || mState.HaveSeenPtr(orphanTree.get())) {
      return 0;
    }

    nsWindowSizes sizes(mState);
    mozilla::dom::Document::AddSizeOfNodeTree(*orphanTree, sizes);

    // We combine the node size with nsStyleSizes here. It's not ideal, but it's
    // hard to get the style structs measurements out to nsWindowMemoryReporter.
    // Also, we drop mServoData in UnbindFromTree(), so in theory any
    // non-in-tree element won't have any style data to measure.
    //
    // FIXME(emilio): We should ideally not do this, since ShadowRoots keep
    // their StyleSheets alive even when detached from a document, and those
    // could be significant in theory.
    return sizes.getTotalSize();
  }

 private:
  SizeOfState mState;
};

#ifdef DEBUG
static bool StartsWithExplicit(nsACString& s) {
  return StringBeginsWith(s, "explicit/"_ns);
}
#endif

class XPCJSRuntimeStats : public JS::RuntimeStats {
  WindowPaths* mWindowPaths;
  WindowPaths* mTopWindowPaths;
  int mAnonymizeID;

 public:
  XPCJSRuntimeStats(WindowPaths* windowPaths, WindowPaths* topWindowPaths,
                    bool anonymize)
      : JS::RuntimeStats(JSMallocSizeOf),
        mWindowPaths(windowPaths),
        mTopWindowPaths(topWindowPaths),
        mAnonymizeID(anonymize ? 1 : 0) {}

  ~XPCJSRuntimeStats() {
    for (size_t i = 0; i != realmStatsVector.length(); ++i) {
      delete static_cast<xpc::RealmStatsExtras*>(realmStatsVector[i].extra);
    }

    for (size_t i = 0; i != zoneStatsVector.length(); ++i) {
      delete static_cast<xpc::ZoneStatsExtras*>(zoneStatsVector[i].extra);
    }
  }

  virtual void initExtraZoneStats(JS::Zone* zone, JS::ZoneStats* zStats,
                                  const JS::AutoRequireNoGC& nogc) override {
    xpc::ZoneStatsExtras* extras = new xpc::ZoneStatsExtras;
    extras->pathPrefix.AssignLiteral("explicit/js-non-window/zones/");

    // Get some global in this zone.
    Rooted<Realm*> realm(dom::RootingCx(), js::GetAnyRealmInZone(zone));
    if (realm) {
      RootedObject global(dom::RootingCx(), JS::GetRealmGlobalOrNull(realm));
      if (global) {
        RefPtr<nsGlobalWindowInner> window;
        if (NS_SUCCEEDED(UNWRAP_NON_WRAPPER_OBJECT(Window, global, window))) {
          // The global is a |window| object.  Use the path prefix that
          // we should have already created for it.
          if (mTopWindowPaths->Get(window->WindowID(), &extras->pathPrefix)) {
            extras->pathPrefix.AppendLiteral("/js-");
          }
        }
      }
    }

    extras->pathPrefix += nsPrintfCString("zone(0x%p)/", (void*)zone);

    MOZ_ASSERT(StartsWithExplicit(extras->pathPrefix));

    zStats->extra = extras;
  }

  virtual void initExtraRealmStats(Realm* realm, JS::RealmStats* realmStats,
                                   const JS::AutoRequireNoGC& nogc) override {
    xpc::RealmStatsExtras* extras = new xpc::RealmStatsExtras;
    nsCString rName;
    GetRealmName(realm, rName, &mAnonymizeID, /* replaceSlashes = */ true);

    // Get the realm's global.
    bool needZone = true;
    RootedObject global(dom::RootingCx(), JS::GetRealmGlobalOrNull(realm));
    if (global) {
      RefPtr<nsGlobalWindowInner> window;
      if (NS_SUCCEEDED(UNWRAP_NON_WRAPPER_OBJECT(Window, global, window))) {
        // The global is a |window| object.  Use the path prefix that
        // we should have already created for it.
        if (mWindowPaths->Get(window->WindowID(), &extras->jsPathPrefix)) {
          extras->domPathPrefix.Assign(extras->jsPathPrefix);
          extras->domPathPrefix.AppendLiteral("/dom/");
          extras->jsPathPrefix.AppendLiteral("/js-");
          needZone = false;
        } else {
          extras->jsPathPrefix.AssignLiteral("explicit/js-non-window/zones/");
          extras->domPathPrefix.AssignLiteral(
              "explicit/dom/unknown-window-global?!/");
        }
      } else {
        extras->jsPathPrefix.AssignLiteral("explicit/js-non-window/zones/");
        extras->domPathPrefix.AssignLiteral(
            "explicit/dom/non-window-global?!/");
      }
    } else {
      extras->jsPathPrefix.AssignLiteral("explicit/js-non-window/zones/");
      extras->domPathPrefix.AssignLiteral("explicit/dom/no-global?!/");
    }

    if (needZone) {
      extras->jsPathPrefix +=
          nsPrintfCString("zone(0x%p)/", (void*)js::GetRealmZone(realm));
    }

    extras->jsPathPrefix += "realm("_ns + rName + ")/"_ns;

    // extras->jsPathPrefix is used for almost all the realm-specific
    // reports. At this point it has the form
    // "<something>realm(<rname>)/".
    //
    // extras->domPathPrefix is used for DOM orphan nodes, which are
    // counted by the JS reporter but reported as part of the DOM
    // measurements. At this point it has the form "<something>/dom/" if
    // this realm belongs to an nsGlobalWindow, and
    // "explicit/dom/<something>?!/" otherwise (in which case it shouldn't
    // be used, because non-nsGlobalWindow realms shouldn't have
    // orphan DOM nodes).

    MOZ_ASSERT(StartsWithExplicit(extras->jsPathPrefix));
    MOZ_ASSERT(StartsWithExplicit(extras->domPathPrefix));

    realmStats->extra = extras;
  }
};

void JSReporter::CollectReports(WindowPaths* windowPaths,
                                WindowPaths* topWindowPaths,
                                nsIHandleReportCallback* handleReport,
                                nsISupports* data, bool anonymize) {
  XPCJSRuntime* xpcrt = nsXPConnect::GetRuntimeInstance();

  // In the first step we get all the stats and stash them in a local
  // data structure.  In the second step we pass all the stashed stats to
  // the callback.  Separating these steps is important because the
  // callback may be a JS function, and executing JS while getting these
  // stats seems like a bad idea.

  XPCJSRuntimeStats rtStats(windowPaths, topWindowPaths, anonymize);
  OrphanReporter orphanReporter(XPCConvert::GetISupportsFromJSObject);
  JSContext* cx = XPCJSContext::Get()->Context();
  if (!JS::CollectRuntimeStats(cx, &rtStats, &orphanReporter, anonymize)) {
    return;
  }

  // Collect JS stats not associated with a Runtime such as helper threads or
  // global tracelogger data. We do this here in JSReporter::CollectReports
  // as this is used for the main Runtime in process.
  JS::GlobalStats gStats(JSMallocSizeOf);
  if (!JS::CollectGlobalStats(&gStats)) {
    return;
  }

  size_t xpcJSRuntimeSize = xpcrt->SizeOfIncludingThis(JSMallocSizeOf);

  size_t wrappedJSSize =
      xpcrt->GetMultiCompartmentWrappedJSMap()->SizeOfWrappedJS(JSMallocSizeOf);

  XPCWrappedNativeScope::ScopeSizeInfo sizeInfo(JSMallocSizeOf);
  XPCWrappedNativeScope::AddSizeOfAllScopesIncludingThis(cx, &sizeInfo);

  mozJSModuleLoader* loader = mozJSModuleLoader::Get();
  size_t jsModuleLoaderSize =
      loader ? loader->SizeOfIncludingThis(JSMallocSizeOf) : 0;
  mozJSModuleLoader* devToolsLoader = mozJSModuleLoader::GetDevToolsLoader();
  size_t jsDevToolsModuleLoaderSize =
      devToolsLoader ? devToolsLoader->SizeOfIncludingThis(JSMallocSizeOf) : 0;

  // This is the second step (see above).  First we report stuff in the
  // "explicit" tree, then we report other stuff.

  size_t rtTotal = 0;
  xpc::ReportJSRuntimeExplicitTreeStats(rtStats, "explicit/js-non-window/"_ns,
                                        handleReport, data, anonymize,
                                        &rtTotal);

  // Report the sums of the realm numbers.
  xpc::RealmStatsExtras realmExtrasTotal;
  realmExtrasTotal.jsPathPrefix.AssignLiteral("js-main-runtime/realms/");
  realmExtrasTotal.domPathPrefix.AssignLiteral("window-objects/dom/");
  ReportRealmStats(rtStats.realmTotals, realmExtrasTotal, handleReport, data);

  xpc::ZoneStatsExtras zExtrasTotal;
  zExtrasTotal.pathPrefix.AssignLiteral("js-main-runtime/zones/");
  ReportZoneStats(rtStats.zTotals, zExtrasTotal, handleReport, data, anonymize);

  // Report the sum of the runtime/ numbers.
  REPORT_BYTES(
      "js-main-runtime/runtime"_ns, KIND_OTHER, rtTotal,
      "The sum of all measurements under 'explicit/js-non-window/runtime/'.");

  // Report the number of HelperThread

  REPORT("js-helper-threads/idle"_ns, KIND_OTHER, UNITS_COUNT,
         gStats.helperThread.idleThreadCount,
         "The current number of idle JS HelperThreads.");

  REPORT(
      "js-helper-threads/active"_ns, KIND_OTHER, UNITS_COUNT,
      gStats.helperThread.activeThreadCount,
      "The current number of active JS HelperThreads. Memory held by these is"
      " not reported.");

  // Report the numbers for memory used by wasm Runtime state.
  REPORT_BYTES("wasm-runtime"_ns, KIND_OTHER, rtStats.runtime.wasmRuntime,
               "The memory used for wasm runtime bookkeeping.");

  // Although wasm guard pages aren't committed in memory they can be very
  // large and contribute greatly to vsize and so are worth reporting.
  if (rtStats.runtime.wasmGuardPages > 0) {
    REPORT_BYTES(
        "wasm-guard-pages"_ns, KIND_OTHER, rtStats.runtime.wasmGuardPages,
        "Guard pages mapped after the end of wasm memories, reserved for "
        "optimization tricks, but not committed and thus never contributing"
        " to RSS, only vsize.");
  }

  // Report the numbers for memory outside of realms.

  REPORT_BYTES("js-main-runtime/gc-heap/unused-chunks"_ns, KIND_OTHER,
               rtStats.gcHeapUnusedChunks,
               "The same as 'explicit/js-non-window/gc-heap/unused-chunks'.");

  REPORT_BYTES("js-main-runtime/gc-heap/unused-arenas"_ns, KIND_OTHER,
               rtStats.gcHeapUnusedArenas,
               "The same as 'explicit/js-non-window/gc-heap/unused-arenas'.");

  REPORT_BYTES("js-main-runtime/gc-heap/chunk-admin"_ns, KIND_OTHER,
               rtStats.gcHeapChunkAdmin,
               "The same as 'explicit/js-non-window/gc-heap/chunk-admin'.");

  // Report a breakdown of the committed GC space.

  REPORT_BYTES("js-main-runtime-gc-heap-committed/unused/chunks"_ns, KIND_OTHER,
               rtStats.gcHeapUnusedChunks,
               "The same as 'explicit/js-non-window/gc-heap/unused-chunks'.");

  REPORT_BYTES("js-main-runtime-gc-heap-committed/unused/arenas"_ns, KIND_OTHER,
               rtStats.gcHeapUnusedArenas,
               "The same as 'explicit/js-non-window/gc-heap/unused-arenas'.");

  REPORT_BYTES("js-main-runtime-gc-heap-committed/unused/gc-things/objects"_ns,
               KIND_OTHER, rtStats.zTotals.unusedGCThings.object,
               "Unused object cells within non-empty arenas.");

  REPORT_BYTES("js-main-runtime-gc-heap-committed/unused/gc-things/strings"_ns,
               KIND_OTHER, rtStats.zTotals.unusedGCThings.string,
               "Unused string cells within non-empty arenas.");

  REPORT_BYTES("js-main-runtime-gc-heap-committed/unused/gc-things/symbols"_ns,
               KIND_OTHER, rtStats.zTotals.unusedGCThings.symbol,
               "Unused symbol cells within non-empty arenas.");

  REPORT_BYTES("js-main-runtime-gc-heap-committed/unused/gc-things/shapes"_ns,
               KIND_OTHER, rtStats.zTotals.unusedGCThings.shape,
               "Unused shape cells within non-empty arenas.");

  REPORT_BYTES(
      "js-main-runtime-gc-heap-committed/unused/gc-things/base-shapes"_ns,
      KIND_OTHER, rtStats.zTotals.unusedGCThings.baseShape,
      "Unused base shape cells within non-empty arenas.");

  REPORT_BYTES("js-main-runtime-gc-heap-committed/unused/gc-things/bigints"_ns,
               KIND_OTHER, rtStats.zTotals.unusedGCThings.bigInt,
               "Unused BigInt cells within non-empty arenas.");

  REPORT_BYTES(
      "js-main-runtime-gc-heap-committed/unused/gc-things/getter-setters"_ns,
      KIND_OTHER, rtStats.zTotals.unusedGCThings.getterSetter,
      "Unused getter-setter cells within non-empty arenas.");

  REPORT_BYTES(
      "js-main-runtime-gc-heap-committed/unused/gc-things/property-maps"_ns,
      KIND_OTHER, rtStats.zTotals.unusedGCThings.propMap,
      "Unused property map cells within non-empty arenas.");

  REPORT_BYTES("js-main-runtime-gc-heap-committed/unused/gc-things/scopes"_ns,
               KIND_OTHER, rtStats.zTotals.unusedGCThings.scope,
               "Unused scope cells within non-empty arenas.");

  REPORT_BYTES("js-main-runtime-gc-heap-committed/unused/gc-things/scripts"_ns,
               KIND_OTHER, rtStats.zTotals.unusedGCThings.script,
               "Unused script cells within non-empty arenas.");

  REPORT_BYTES("js-main-runtime-gc-heap-committed/unused/gc-things/jitcode"_ns,
               KIND_OTHER, rtStats.zTotals.unusedGCThings.jitcode,
               "Unused jitcode cells within non-empty arenas.");

  REPORT_BYTES(
      "js-main-runtime-gc-heap-committed/unused/gc-things/regexp-shareds"_ns,
      KIND_OTHER, rtStats.zTotals.unusedGCThings.regExpShared,
      "Unused regexpshared cells within non-empty arenas.");

  REPORT_BYTES(
      "js-main-runtime-gc-heap-committed/unused/gc-things/small-buffers"_ns,
      KIND_OTHER, rtStats.zTotals.unusedGCThings.smallBuffer,
      "Unused small buffer cells within non-empty arenas.");

  REPORT_BYTES("js-main-runtime-gc-heap-committed/used/chunk-admin"_ns,
               KIND_OTHER, rtStats.gcHeapChunkAdmin,
               "The same as 'explicit/js-non-window/gc-heap/chunk-admin'.");

  REPORT_BYTES("js-main-runtime-gc-heap-committed/used/arena-admin"_ns,
               KIND_OTHER, rtStats.zTotals.gcHeapArenaAdmin,
               "The same as 'js-main-runtime/zones/gc-heap-arena-admin'.");

  size_t gcThingTotal = 0;

  MREPORT_BYTES("js-main-runtime-gc-heap-committed/used/gc-things/objects"_ns,
                KIND_OTHER, rtStats.realmTotals.classInfo.objectsGCHeap,
                "Used object cells.");

  MREPORT_BYTES("js-main-runtime-gc-heap-committed/used/gc-things/strings"_ns,
                KIND_OTHER, rtStats.zTotals.stringInfo.sizeOfLiveGCThings(),
                "Used string cells.");

  MREPORT_BYTES("js-main-runtime-gc-heap-committed/used/gc-things/symbols"_ns,
                KIND_OTHER, rtStats.zTotals.symbolsGCHeap,
                "Used symbol cells.");

  MREPORT_BYTES("js-main-runtime-gc-heap-committed/used/gc-things/shapes"_ns,
                KIND_OTHER,
                rtStats.zTotals.shapeInfo.shapesGCHeapShared +
                    rtStats.zTotals.shapeInfo.shapesGCHeapDict,
                "Used shape cells.");

  MREPORT_BYTES(
      "js-main-runtime-gc-heap-committed/used/gc-things/base-shapes"_ns,
      KIND_OTHER, rtStats.zTotals.shapeInfo.shapesGCHeapBase,
      "Used base shape cells.");

  MREPORT_BYTES("js-main-runtime-gc-heap-committed/used/gc-things/bigints"_ns,
                KIND_OTHER, rtStats.zTotals.bigIntsGCHeap,
                "Used BigInt cells.");

  MREPORT_BYTES(
      "js-main-runtime-gc-heap-committed/used/gc-things/getter-setters"_ns,
      KIND_OTHER, rtStats.zTotals.getterSettersGCHeap,
      "Used getter/setter cells.");

  MREPORT_BYTES(
      "js-main-runtime-gc-heap-committed/used/gc-things/property-maps"_ns,
      KIND_OTHER,
      rtStats.zTotals.dictPropMapsGCHeap +
          rtStats.zTotals.compactPropMapsGCHeap +
          rtStats.zTotals.normalPropMapsGCHeap,
      "Used property map cells.");

  MREPORT_BYTES("js-main-runtime-gc-heap-committed/used/gc-things/scopes"_ns,
                KIND_OTHER, rtStats.zTotals.scopesGCHeap, "Used scope cells.");

  MREPORT_BYTES("js-main-runtime-gc-heap-committed/used/gc-things/scripts"_ns,
                KIND_OTHER, rtStats.realmTotals.scriptsGCHeap,
                "Used script cells.");

  MREPORT_BYTES("js-main-runtime-gc-heap-committed/used/gc-things/jitcode"_ns,
                KIND_OTHER, rtStats.zTotals.jitCodesGCHeap,
                "Used jitcode cells.");

  MREPORT_BYTES(
      "js-main-runtime-gc-heap-committed/used/gc-things/regexp-shareds"_ns,
      KIND_OTHER, rtStats.zTotals.regExpSharedsGCHeap,
      "Used regexpshared cells.");

  MREPORT_BYTES(
      "js-main-runtime-gc-heap-committed/used/gc-things/small-buffers"_ns,
      KIND_OTHER, rtStats.zTotals.smallBuffersGCHeap,
      "Used small buffer cells.");

  MOZ_ASSERT(gcThingTotal == rtStats.gcHeapGCThings);
  (void)gcThingTotal;

  // Report totals from per-zone GC buffer allocators.

  MREPORT_BYTES("js-main-runtime-gc-buffers/used"_ns, KIND_OTHER,
                rtStats.zTotals.gcBuffers.usedBytes,
                "Bookeeping information and padding within GC buffer memeory.");

  MREPORT_BYTES("js-main-runtime-gc-buffers/free"_ns, KIND_OTHER,
                rtStats.zTotals.gcBuffers.freeBytes,
                "Free space within GC buffer memeory.");

  MREPORT_BYTES("js-main-runtime-gc-buffers/admin"_ns, KIND_OTHER,
                rtStats.zTotals.gcBuffers.adminBytes,
                "Bookeeping information and padding within GC buffer memeory.");

  REPORT("js-main-runtime-zone-count"_ns, KIND_OTHER, UNITS_COUNT,
         rtStats.zoneStatsVector.length(), "Count of GC zones in the runtime.");

  // Report xpconnect.

  REPORT_BYTES("explicit/xpconnect/runtime"_ns, KIND_HEAP, xpcJSRuntimeSize,
               "The XPConnect runtime.");

  REPORT_BYTES("explicit/xpconnect/wrappedjs"_ns, KIND_HEAP, wrappedJSSize,
               "Wrappers used to implement XPIDL interfaces with JS.");

  REPORT_BYTES("explicit/xpconnect/scopes"_ns, KIND_HEAP,
               sizeInfo.mScopeAndMapSize, "XPConnect scopes.");

  REPORT_BYTES("explicit/xpconnect/proto-iface-cache"_ns, KIND_HEAP,
               sizeInfo.mProtoAndIfaceCacheSize,
               "Prototype and interface binding caches.");

  REPORT_BYTES("explicit/xpconnect/js-module-loader"_ns, KIND_HEAP,
               jsModuleLoaderSize, "XPConnect's JS module loader.");
  REPORT_BYTES("explicit/xpconnect/js-devtools-module-loader"_ns, KIND_HEAP,
               jsDevToolsModuleLoaderSize, "DevTools's JS module loader.");

  // Report HelperThreadState.

  REPORT_BYTES("explicit/js-non-window/helper-thread/heap-other"_ns, KIND_HEAP,
               gStats.helperThread.stateData,
               "Memory used by HelperThreadState.");

  REPORT_BYTES(
      "explicit/js-non-window/helper-thread/ion-compile-task"_ns, KIND_HEAP,
      gStats.helperThread.ionCompileTask,
      "The memory used by IonCompileTasks waiting in HelperThreadState.");

  REPORT_BYTES(
      "explicit/js-non-window/helper-thread/wasm-compile"_ns, KIND_HEAP,
      gStats.helperThread.wasmCompile,
      "The memory used by Wasm compilations waiting in HelperThreadState.");

  REPORT_BYTES("explicit/js-non-window/helper-thread/contexts"_ns, KIND_HEAP,
               gStats.helperThread.contexts,
               "The memory used by the JSContexts in HelperThreadState.");
}

static nsresult JSSizeOfTab(JSObject* objArg, size_t* jsObjectsSize,
                            size_t* jsStringsSize, size_t* jsPrivateSize,
                            size_t* jsOtherSize) {
  JSContext* cx = XPCJSContext::Get()->Context();
  JS::RootedObject obj(cx, objArg);

  TabSizes sizes;
  OrphanReporter orphanReporter(XPCConvert::GetISupportsFromJSObject);
  NS_ENSURE_TRUE(
      JS::AddSizeOfTab(cx, obj, moz_malloc_size_of, &orphanReporter, &sizes),
      NS_ERROR_OUT_OF_MEMORY);

  *jsObjectsSize = sizes.objects_;
  *jsStringsSize = sizes.strings_;
  *jsPrivateSize = sizes.private_;
  *jsOtherSize = sizes.other_;
  return NS_OK;
}

}  // namespace xpc

// Temporary workaround until bug 1949494 can land.
namespace TelemetryHistogram {
void Accumulate(mozilla::Telemetry::HistogramID aHistogram, uint32_t aSample);
}

static void AccumulateTelemetryCallback(JSMetric id, uint32_t sample) {
  // clang-format off
  switch (id) {
#define CASE_ACCUMULATE(NAME, _)                                \
    case JSMetric::NAME:                                        \
      TelemetryHistogram::Accumulate(Telemetry::NAME, sample);  \
      break;

    FOR_EACH_JS_LEGACY_METRIC(CASE_ACCUMULATE)
#undef CASE_ACCUMULATE

    default:
      break;
  }
  // clang-format on

  switch (id) {
// Disable clone.deserialize metrics on Android for perf (bug 1898515).
#ifndef MOZ_WIDGET_ANDROID
    case JSMetric::DESERIALIZE_BYTES:
      glean::performance_clone_deserialize::size.Accumulate(sample);
      break;
    case JSMetric::DESERIALIZE_ITEMS:
      glean::performance_clone_deserialize::items.AccumulateSingleSample(
          sample);
      break;
    case JSMetric::DESERIALIZE_US:
      glean::performance_clone_deserialize::time.AccumulateRawDuration(
          TimeDuration::FromMicroseconds(sample));
      // GLAM EXPERIMENT
      // This metric is temporary, disabled by default, and will be enabled only
      // for the purpose of experimenting with client-side sampling of data for
      // GLAM use. See Bug 1947604 for more information.
      glean::glam_experiment::time.AccumulateRawDuration(
          TimeDuration::FromMicroseconds(sample));
      break;
#endif  // MOZ_WIDGET_ANDROID
    case JSMetric::GC_MS:
      glean::javascript_gc::total_time.AccumulateRawDuration(
          TimeDuration::FromMilliseconds(sample));
      break;
    case JSMetric::GC_MINOR_US:
      glean::javascript_gc::minor_time.AccumulateRawDuration(
          TimeDuration::FromMicroseconds(sample));
      break;
    case JSMetric::GC_PREPARE_MS:
      glean::javascript_gc::prepare_time.AccumulateRawDuration(
          TimeDuration::FromMilliseconds(sample));
      break;
    case JSMetric::GC_MARK_ROOTS_US:
      glean::javascript_gc::mark_roots_time.AccumulateRawDuration(
          TimeDuration::FromMicroseconds(sample));
      break;
    case JSMetric::GC_MARK_MS:
      glean::javascript_gc::mark_time.AccumulateRawDuration(
          TimeDuration::FromMilliseconds(sample));
      break;
    case JSMetric::GC_SWEEP_MS:
      glean::javascript_gc::sweep_time.AccumulateRawDuration(
          TimeDuration::FromMilliseconds(sample));
      break;
    case JSMetric::GC_COMPACT_MS:
      glean::javascript_gc::compact_time.AccumulateRawDuration(
          TimeDuration::FromMilliseconds(sample));
      break;
    case JSMetric::GC_SLICE_MS:
      glean::javascript_gc::slice_time.AccumulateRawDuration(
          TimeDuration::FromMilliseconds(sample));
      break;
    case JSMetric::ION_COMPILE_TIME:
      glean::javascript_ion::compile_time.AccumulateRawDuration(
          TimeDuration::FromMicroseconds(sample));
      break;
    case JSMetric::GC_BUDGET_MS_2:
      glean::javascript_gc::budget.AccumulateRawDuration(
          TimeDuration::FromMilliseconds(sample));
      break;
    case JSMetric::GC_BUDGET_OVERRUN:
      glean::javascript_gc::budget_overrun.AccumulateRawDuration(
          TimeDuration::FromMicroseconds(sample));
      break;
    case JSMetric::GC_ANIMATION_MS:
      glean::javascript_gc::animation.AccumulateRawDuration(
          TimeDuration::FromMilliseconds(sample));
      break;
    case JSMetric::GC_MAX_PAUSE_MS_2:
      glean::javascript_gc::max_pause.AccumulateRawDuration(
          TimeDuration::FromMilliseconds(sample));
      break;
    case JSMetric::GC_MARK_GRAY_MS_2:
      glean::javascript_gc::mark_gray.AccumulateRawDuration(
          TimeDuration::FromMilliseconds(sample));
      break;
    case JSMetric::GC_MARK_WEAK_MS:
      glean::javascript_gc::mark_weak.AccumulateRawDuration(
          TimeDuration::FromMilliseconds(sample));
      break;
    case JSMetric::GC_TIME_BETWEEN_S:
      glean::javascript_gc::time_between.AccumulateRawDuration(
          TimeDuration::FromSeconds(sample));
      break;
    case JSMetric::GC_TIME_BETWEEN_SLICES_MS:
      glean::javascript_gc::time_between_slices.AccumulateRawDuration(
          TimeDuration::FromMilliseconds(sample));
      break;
    case JSMetric::GC_TASK_START_DELAY_US:
      glean::javascript_gc::task_start_delay.AccumulateRawDuration(
          TimeDuration::FromMicroseconds(sample));
      break;
    case JSMetric::GC_MMU_50:
      glean::javascript_gc::mmu_50.AccumulateSingleSample(sample);
      break;
    case JSMetric::GC_NURSERY_PROMOTION_RATE:
      glean::javascript_gc::nursery_promotion_rate.AccumulateSingleSample(
          sample);
      break;
    case JSMetric::GC_TENURED_SURVIVAL_RATE:
      glean::javascript_gc::tenured_survival_rate.AccumulateSingleSample(
          sample);
      break;
    case JSMetric::GC_PARALLEL_MARK_UTILIZATION:
      glean::javascript_gc::parallel_mark_utilization.AccumulateSingleSample(
          sample);
      break;
    case JSMetric::GC_NURSERY_BYTES_2:
      glean::javascript_gc::nursery_bytes.Accumulate(sample);
      break;
    case JSMetric::GC_EFFECTIVENESS:
      glean::javascript_gc::effectiveness.AccumulateSingleSample(sample);
      break;
    case JSMetric::GC_ZONE_COUNT:
      glean::javascript_gc::zone_count.AccumulateSingleSample(sample);
      break;
    case JSMetric::GC_ZONES_COLLECTED:
      glean::javascript_gc::zones_collected.AccumulateSingleSample(sample);
      break;
    case JSMetric::GC_PRETENURE_COUNT_2:
      glean::javascript_gc::pretenure_count.AccumulateSingleSample(sample);
      break;
    case JSMetric::GC_MARK_RATE_2:
      glean::javascript_gc::mark_rate.AccumulateSingleSample(sample);
      break;
    case JSMetric::GC_SLICE_COUNT:
      glean::javascript_gc::slice_count.AccumulateSingleSample(sample);
      break;
    case JSMetric::GC_PARALLEL_MARK_SPEEDUP:
      glean::javascript_gc::parallel_mark_speedup.AccumulateSingleSample(
          sample);
      break;
    case JSMetric::GC_PARALLEL_MARK_INTERRUPTIONS:
      glean::javascript_gc::parallel_mark_interruptions.AccumulateSingleSample(
          sample);
      break;
    case JSMetric::GC_IS_COMPARTMENTAL:
      if (sample) {
        glean::javascript_gc::is_zone_gc
            .EnumGet(glean::javascript_gc::IsZoneGcLabel::eTrue)
            .Add(1);
      } else {
        glean::javascript_gc::is_zone_gc
            .EnumGet(glean::javascript_gc::IsZoneGcLabel::eFalse)
            .Add(1);
      }
      break;
    case JSMetric::GC_BUDGET_WAS_INCREASED:
      if (sample) {
        glean::javascript_gc::budget_was_increased
            .EnumGet(glean::javascript_gc::BudgetWasIncreasedLabel::eTrue)
            .Add(1);
      } else {
        glean::javascript_gc::budget_was_increased
            .EnumGet(glean::javascript_gc::BudgetWasIncreasedLabel::eFalse)
            .Add(1);
      }
      break;
    case JSMetric::GC_SLICE_WAS_LONG:
      if (sample) {
        glean::javascript_gc::slice_was_long
            .EnumGet(glean::javascript_gc::SliceWasLongLabel::eTrue)
            .Add(1);
      } else {
        glean::javascript_gc::slice_was_long
            .EnumGet(glean::javascript_gc::SliceWasLongLabel::eFalse)
            .Add(1);
      }
      break;
    case JSMetric::GC_RESET:
      if (sample) {
        glean::javascript_gc::reset
            .EnumGet(glean::javascript_gc::ResetLabel::eTrue)
            .Add(1);
      } else {
        glean::javascript_gc::reset
            .EnumGet(glean::javascript_gc::ResetLabel::eFalse)
            .Add(1);
      }
      break;
    case JSMetric::GC_NON_INCREMENTAL:
      if (sample) {
        glean::javascript_gc::non_incremental
            .EnumGet(glean::javascript_gc::NonIncrementalLabel::eTrue)
            .Add(1);
      } else {
        glean::javascript_gc::non_incremental
            .EnumGet(glean::javascript_gc::NonIncrementalLabel::eFalse)
            .Add(1);
      }
      break;
    case JSMetric::GC_PARALLEL_MARK:
      if (sample) {
        glean::javascript_gc::parallel_mark_used
            .EnumGet(glean::javascript_gc::ParallelMarkUsedLabel::eTrue)
            .Add(1);
      } else {
        glean::javascript_gc::parallel_mark_used
            .EnumGet(glean::javascript_gc::ParallelMarkUsedLabel::eFalse)
            .Add(1);
      }
      break;
    case JSMetric::GC_REASON_2: {
      // Assert that every reason has an associated glean label.
      static_assert(static_cast<uint8_t>(JS::GCReason::LAST_FIREFOX_REASON) ==
                        static_cast<uint8_t>(
                            glean::javascript_gc::ReasonLabel::e__Other__),
                    "GC reason enum and glean::javascript_gc::reason labels do "
                    "not match.");
      MOZ_ASSERT(static_cast<JS::GCReason>(sample) <=
                     JS::GCReason::LAST_FIREFOX_REASON,
                 "Invalid GC Reason.");

      nsAutoCString reason(
          JS::ExplainGCReason(static_cast<JS::GCReason>(sample)));
      glean::javascript_gc::reason.Get(reason).Add(1);
    } break;
    case JSMetric::GC_RESET_REASON: {
      MOZ_ASSERT(
          sample < static_cast<uint32_t>(
                       glean::javascript_gc::ResetReasonLabel::e__Other__),
          "Reason does not exist in the reset_reason labels list.");
      nsAutoCString reason(JS::ExplainGCAbortReason(sample));
      glean::javascript_gc::reset_reason.Get(reason).Add(1);
    } break;
    case JSMetric::GC_NON_INCREMENTAL_REASON: {
      MOZ_ASSERT(
          sample <
              static_cast<uint32_t>(
                  glean::javascript_gc::NonIncrementalReasonLabel::e__Other__),
          "Reason does not exist in the non_incremental_reason labels list.");
      nsAutoCString reason(JS::ExplainGCAbortReason(sample));
      glean::javascript_gc::non_incremental_reason.Get(reason).Add(1);
    } break;
    case JSMetric::GC_MINOR_REASON: {
      // Assert that every reason has an associated glean label.
      static_assert(
          static_cast<uint8_t>(JS::GCReason::LAST_FIREFOX_REASON) ==
              static_cast<uint8_t>(
                  glean::javascript_gc::MinorReasonLabel::e__Other__),
          "GC reason enum and glean::javascript_gc::reason labels do not "
          "match.");
      MOZ_ASSERT(static_cast<JS::GCReason>(sample) <=
                     JS::GCReason::LAST_FIREFOX_REASON,
                 "Invalid GC Reason.");

      nsAutoCString reason(
          JS::ExplainGCReason(static_cast<JS::GCReason>(sample)));
      glean::javascript_gc::minor_reason.Get(reason).Add(1);
    } break;
    case JSMetric::GC_MINOR_REASON_LONG: {
      // Assert that every reason has an associated glean label.
      static_assert(
          static_cast<uint8_t>(JS::GCReason::LAST_FIREFOX_REASON) ==
              static_cast<uint8_t>(
                  glean::javascript_gc::MinorReasonLongLabel::e__Other__),
          "GC reason enum and glean::javascript_gc::reason labels do not "
          "match.");
      MOZ_ASSERT(static_cast<JS::GCReason>(sample) <=
                     JS::GCReason::LAST_FIREFOX_REASON,
                 "Invalid GC Reason.");

      nsAutoCString reason(
          JS::ExplainGCReason(static_cast<JS::GCReason>(sample)));
      glean::javascript_gc::minor_reason_long.Get(reason).Add(1);
    } break;
    case JSMetric::GC_GLEAN_SLOW_PHASE: {
      MOZ_ASSERT(sample < static_cast<uint32_t>(
                              glean::javascript_gc::SlowPhaseLabel::e__Other__),
                 "Phase does not exist in the slow_phase labels list.");
      nsAutoCString phase(JS::GetGCPhaseName(sample));
      glean::javascript_gc::slow_phase.Get(phase).Add(1);
    } break;
    case JSMetric::GC_GLEAN_SLOW_TASK: {
      MOZ_ASSERT(sample < static_cast<uint32_t>(
                              glean::javascript_gc::SlowTaskLabel::e__Other__),
                 "Phase does not exist in the slow_task labels list.");
      nsAutoCString phase(JS::GetGCPhaseName(sample));
      glean::javascript_gc::slow_task.Get(phase).Add(1);
    } break;

    default:
      // The rest aren't relayed to Glean.
      break;
  }
}

static void SetUseCounterCallback(JSObject* obj, JSUseCounter counter) {
  switch (counter) {
    case JSUseCounter::ASMJS:
      SetUseCounter(obj, eUseCounter_custom_JS_asmjs);
      return;
    case JSUseCounter::WASM:
      SetUseCounter(obj, eUseCounter_custom_JS_wasm);
      return;
    case JSUseCounter::WASM_LEGACY_EXCEPTIONS:
      SetUseCounter(obj, eUseCounter_custom_JS_wasm_legacy_exceptions);
      return;
    case JSUseCounter::ISHTMLDDA_FUSE:
      SetUseCounter(obj, eUseCounter_custom_JS_isHTMLDDA_fuse);
      return;
    case JSUseCounter::OPTIMIZE_GET_ITERATOR_FUSE:
      SetUseCounter(obj, eUseCounter_custom_JS_optimizeGetIterator_fuse);
      return;
    case JSUseCounter::OPTIMIZE_ARRAY_SPECIES_FUSE:
      SetUseCounter(obj, eUseCounter_custom_JS_optimizeArraySpecies_fuse);
      return;
    case JSUseCounter::OPTIMIZE_PROMISE_LOOKUP_FUSE:
      SetUseCounter(obj, eUseCounter_custom_JS_optimizePromiseLookup_fuse);
      return;
    case JSUseCounter::THENABLE_USE:
      SetUseCounter(obj, eUseCounter_custom_JS_thenable);
      return;
    case JSUseCounter::THENABLE_USE_PROTO:
      SetUseCounter(obj, eUseCounter_custom_JS_thenable_proto);
      return;
    case JSUseCounter::THENABLE_USE_STANDARD_PROTO:
      SetUseCounter(obj, eUseCounter_custom_JS_thenable_standard_proto);
      return;
    case JSUseCounter::THENABLE_USE_OBJECT_PROTO:
      SetUseCounter(obj, eUseCounter_custom_JS_thenable_object_proto);
      return;
    case JSUseCounter::LEGACY_LANG_SUBTAG:
      SetUseCounter(obj, eUseCounter_custom_JS_legacy_lang_subtag);
      return;
    case JSUseCounter::IC_STUB_TOO_LARGE:
      SetUseCounter(obj, eUseCounter_custom_JS_ic_stub_too_large);
      return;
    case JSUseCounter::IC_STUB_OOM:
      SetUseCounter(obj, eUseCounter_custom_JS_ic_stub_oom);
      return;
    case JSUseCounter::ERRORSTACK_GETTER:
      SetUseCounter(obj, eUseCounter_custom_JS_errorstack_getter);
      return;
    case JSUseCounter::ERRORSTACK_GETTER_NO_ERRORDATA:
      SetUseCounter(obj, eUseCounter_custom_JS_errorstack_getter_no_errordata);
      return;
    case JSUseCounter::ERRORSTACK_SETTER:
      SetUseCounter(obj, eUseCounter_custom_JS_errorstack_setter);
      return;
    case JSUseCounter::ERRORSTACK_SETTER_NONSTRING:
      SetUseCounter(obj, eUseCounter_custom_JS_errorstack_setter_nonstring);
      return;
    case JSUseCounter::ERRORSTACK_SETTER_NO_ERRORDATA:
      SetUseCounter(obj, eUseCounter_custom_JS_errorstack_setter_no_errordata);
      return;
    case JSUseCounter::DATEPARSE:
      SetUseCounter(obj, eUseCounter_custom_JS_dateparse);
      return;
    case JSUseCounter::DATEPARSE_IMPL_DEF:
      SetUseCounter(obj, eUseCounter_custom_JS_dateparse_impl_def);
      return;
    case JSUseCounter::REGEXP_SYMBOL_PROTOCOL_ON_PRIMITIVE:
      SetUseCounter(obj,
                    eUseCounter_custom_JS_regexp_symbol_protocol_on_primitive);
      return;
    case JSUseCounter::COUNT:
      break;
  }
  MOZ_ASSERT_UNREACHABLE("Unexpected JSUseCounter id");
}

static void GetRealmNameCallback(JSContext* cx, Realm* realm, char* buf,
                                 size_t bufsize,
                                 const JS::AutoRequireNoGC& nogc) {
  nsCString name;
  // This is called via the JSAPI and isn't involved in memory reporting, so
  // we don't need to anonymize realm names.
  int anonymizeID = 0;
  GetRealmName(realm, name, &anonymizeID, /* replaceSlashes = */ false);
  if (name.Length() >= bufsize) {
    name.Truncate(bufsize - 1);
  }
  memcpy(buf, name.get(), name.Length() + 1);
}

static void DestroyRealm(JS::GCContext* gcx, JS::Realm* realm) {
  // Get the current compartment private into an AutoPtr (which will do the
  // cleanup for us), and null out the private field.
  mozilla::UniquePtr<RealmPrivate> priv(RealmPrivate::Get(realm));
  JS::SetRealmPrivate(realm, nullptr);
}

static bool PreserveWrapper(JSContext* cx, JS::Handle<JSObject*> obj) {
  MOZ_ASSERT(cx);
  MOZ_ASSERT(obj);
  MOZ_ASSERT(mozilla::dom::IsDOMObject(obj));

  if (!mozilla::dom::TryPreserveWrapper(obj)) {
    return false;
  }

  MOZ_ASSERT(!mozilla::dom::HasReleasedWrapper(obj),
             "There should be no released wrapper since we just preserved it");

  return true;
}

static nsresult ReadSourceFromFilename(JSContext* cx, const char* filename,
                                       char16_t** twoByteSource,
                                       char** utf8Source, size_t* len) {
  MOZ_ASSERT(*len == 0);
  MOZ_ASSERT((twoByteSource != nullptr) != (utf8Source != nullptr),
             "must be called requesting only one of UTF-8 or UTF-16 source");
  MOZ_ASSERT_IF(twoByteSource, !*twoByteSource);
  MOZ_ASSERT_IF(utf8Source, !*utf8Source);

  nsresult rv;

  // mozJSSubScriptLoader prefixes the filenames of the scripts it loads with
  // the filename of its caller. Axe that if present.
  const char* arrow;
  while ((arrow = strstr(filename, " -> "))) {
    filename = arrow + strlen(" -> ");
  }

  // Get the URI.
  nsCOMPtr<nsIURI> uri;
  rv = NS_NewURI(getter_AddRefs(uri), filename);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIChannel> scriptChannel;
  rv = NS_NewChannel(getter_AddRefs(scriptChannel), uri,
                     nsContentUtils::GetSystemPrincipal(),
                     nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
                     nsIContentPolicy::TYPE_OTHER);
  NS_ENSURE_SUCCESS(rv, rv);

  // Only allow local reading.
  nsCOMPtr<nsIURI> actualUri;
  rv = scriptChannel->GetURI(getter_AddRefs(actualUri));
  NS_ENSURE_SUCCESS(rv, rv);
  nsCString scheme;
  rv = actualUri->GetScheme(scheme);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!scheme.EqualsLiteral("file") && !scheme.EqualsLiteral("jar")) {
    return NS_OK;
  }

  // Explicitly set the content type so that we don't load the
  // exthandler to guess it.
  scriptChannel->SetContentType("text/plain"_ns);

  nsCOMPtr<nsIInputStream> scriptStream;
  rv = scriptChannel->Open(getter_AddRefs(scriptStream));
  NS_ENSURE_SUCCESS(rv, rv);

  uint64_t rawLen;
  rv = scriptStream->Available(&rawLen);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!rawLen) {
    return NS_ERROR_FAILURE;
  }

  // Technically, this should be SIZE_MAX, but we don't run on machines
  // where that would be less than UINT32_MAX, and the latter is already
  // well beyond a reasonable limit.
  if (rawLen > UINT32_MAX) {
    return NS_ERROR_FILE_TOO_BIG;
  }

  // Allocate a buffer the size of the file to initially fill with the UTF-8
  // contents of the file.  Use the JS allocator so that if UTF-8 source was
  // requested, we can return this memory directly.
  JS::UniqueChars buf(js_pod_malloc<char>(rawLen));
  if (!buf) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  char* ptr = buf.get();
  char* end = ptr + rawLen;
  while (ptr < end) {
    uint32_t bytesRead;
    rv = scriptStream->Read(ptr, PointerRangeSize(ptr, end), &bytesRead);
    if (NS_FAILED(rv)) {
      return rv;
    }
    MOZ_ASSERT(bytesRead > 0, "stream promised more bytes before EOF");
    ptr += bytesRead;
  }

  if (utf8Source) {
    // |buf| is already UTF-8, so we can directly return it.
    *len = rawLen;
    *utf8Source = buf.release();
  } else {
    MOZ_ASSERT(twoByteSource != nullptr);

    // |buf| can't be directly returned -- convert it to UTF-16.

    // On success this overwrites |chars| and |*len|.
    JS::UniqueTwoByteChars chars;
    rv = ScriptLoader::ConvertToUTF16(
        scriptChannel, reinterpret_cast<const unsigned char*>(buf.get()),
        rawLen, u"UTF-8"_ns, nullptr, chars, *len);
    NS_ENSURE_SUCCESS(rv, rv);

    if (!chars) {
      return NS_ERROR_FAILURE;
    }

    *twoByteSource = chars.release();
  }

  return NS_OK;
}

// The JS engine calls this object's 'load' member function when it needs
// the source for a chrome JS function. See the comment in the XPCJSRuntime
// constructor.
class XPCJSSourceHook : public js::SourceHook {
  bool load(JSContext* cx, const char* filename, char16_t** twoByteSource,
            char** utf8Source, size_t* length) override {
    MOZ_ASSERT((twoByteSource != nullptr) != (utf8Source != nullptr),
               "must be called requesting only one of UTF-8 or UTF-16 source");

    *length = 0;
    if (twoByteSource) {
      *twoByteSource = nullptr;
    } else {
      *utf8Source = nullptr;
    }

    if (!nsContentUtils::IsSystemCaller(cx)) {
      return true;
    }

    if (!filename) {
      return true;
    }

    nsresult rv =
        ReadSourceFromFilename(cx, filename, twoByteSource, utf8Source, length);
    if (NS_FAILED(rv)) {
      xpc::Throw(cx, rv);
      return false;
    }

    return true;
  }
};

static const JSWrapObjectCallbacks WrapObjectCallbacks = {
    xpc::WrapperFactory::Rewrap, xpc::WrapperFactory::PrepareForWrapping};

XPCJSRuntime::XPCJSRuntime(JSContext* aCx)
    : CycleCollectedJSRuntime(aCx),
      mWrappedJSMap(mozilla::MakeUnique<JSObject2WrappedJSMap>()),
      mIID2NativeInterfaceMap(mozilla::MakeUnique<IID2NativeInterfaceMap>()),
      mClassInfo2NativeSetMap(mozilla::MakeUnique<ClassInfo2NativeSetMap>()),
      mNativeSetMap(mozilla::MakeUnique<NativeSetMap>()),
      mGCIsRunning(false),
      mDoingFinalization(false),
      mAsyncSnowWhiteFreer(new AsyncFreeSnowWhite()) {
  MOZ_COUNT_CTOR_INHERITED(XPCJSRuntime, CycleCollectedJSRuntime);
}

/* static */
XPCJSRuntime* XPCJSRuntime::Get() { return nsXPConnect::GetRuntimeInstance(); }

// Subclass of JS::ubi::Base for DOM reflector objects for the JS::ubi::Node
// memory analysis framework; see js/public/UbiNode.h. In
// XPCJSRuntime::Initialize, we register the ConstructUbiNode function as a hook
// with the SpiderMonkey runtime for it to use to construct ubi::Nodes of this
// class for JSObjects whose class has the JSCLASS_IS_DOMJSCLASS flag set.
// ReflectorNode specializes Concrete<JSObject> for DOM reflector nodes,
// reporting the edge from the JSObject to the nsINode it represents, in
// addition to the usual edges departing any normal JSObject.
namespace JS {
namespace ubi {
class ReflectorNode : public Concrete<JSObject> {
 protected:
  explicit ReflectorNode(JSObject* ptr) : Concrete<JSObject>(ptr) {}

 public:
  static void construct(void* storage, JSObject* ptr) {
    new (storage) ReflectorNode(ptr);
  }
  js::UniquePtr<JS::ubi::EdgeRange> edges(JSContext* cx,
                                          bool wantNames) const override;
};

js::UniquePtr<EdgeRange> ReflectorNode::edges(JSContext* cx,
                                              bool wantNames) const {
  js::UniquePtr<SimpleEdgeRange> range(static_cast<SimpleEdgeRange*>(
      Concrete<JSObject>::edges(cx, wantNames).release()));
  if (!range) {
    return nullptr;
  }
  // UNWRAP_NON_WRAPPER_OBJECT assumes the object is completely initialized,
  // but ours may not be. Luckily, UnwrapDOMObjectToISupports checks for the
  // uninitialized case (and returns null if uninitialized), so we can use that
  // to guard against uninitialized objects.
  nsISupports* supp = UnwrapDOMObjectToISupports(&get());
  if (supp) {
    JS::AutoSuppressGCAnalysis nogc;  // bug 1582326

    nsINode* node;
    // UnwrapDOMObjectToISupports can only return non-null if its argument is
    // an actual DOM object, not a cross-compartment wrapper.
    if (NS_SUCCEEDED(UNWRAP_NON_WRAPPER_OBJECT(Node, &get(), node))) {
      char16_t* edgeName = nullptr;
      if (wantNames) {
        edgeName = NS_xstrdup(u"Reflected Node");
      }
      if (!range->addEdge(Edge(edgeName, node))) {
        return nullptr;
      }
    }
  }
  return js::UniquePtr<EdgeRange>(range.release());
}

}  // Namespace ubi
}  // Namespace JS

void ConstructUbiNode(void* storage, JSObject* ptr) {
  JS::ubi::ReflectorNode::construct(storage, ptr);
}

void XPCJSRuntime::Initialize(JSContext* cx) {
  // these jsids filled in later when we have a JSContext to work with.
  mStrIDs[0] = JS::PropertyKey::Void();

  nsScriptSecurityManager::GetScriptSecurityManager()->InitJSCallbacks(cx);

  // Unconstrain the runtime's threshold on nominal heap size, to avoid
  // triggering GC too often if operating continuously near an arbitrary
  // finite threshold (0xffffffff is infinity for uint32_t parameters).
  // This leaves the maximum-JS_malloc-bytes threshold still in effect
  // to cause period, and we hope hygienic, last-ditch GCs from within
  // the GC's allocator.
  JS_SetGCParameter(cx, JSGC_MAX_BYTES, 0xffffffff);

  JS_SetDestroyCompartmentCallback(cx, CompartmentDestroyedCallback);
  JS_SetSizeOfIncludingThisCompartmentCallback(
      cx, CompartmentSizeOfIncludingThisCallback);
  JS::SetDestroyRealmCallback(cx, DestroyRealm);
  JS::SetRealmNameCallback(cx, GetRealmNameCallback);
  mPrevGCSliceCallback = JS::SetGCSliceCallback(cx, GCSliceCallback);
  mPrevDoCycleCollectionCallback =
      JS::SetDoCycleCollectionCallback(cx, DoCycleCollectionCallback);
  JS_AddFinalizeCallback(cx, FinalizeCallback, nullptr);
  JS_AddWeakPointerZonesCallback(cx, WeakPointerZonesCallback, this);
  JS_AddWeakPointerCompartmentCallback(cx, WeakPointerCompartmentCallback,
                                       this);
  JS_SetWrapObjectCallbacks(cx, &WrapObjectCallbacks);
  if (XRE_IsE10sParentProcess()) {
    JS::SetFilenameValidationCallback(
        nsContentSecurityUtils::ValidateScriptFilename);
  }
  js::SetPreserveWrapperCallbacks(cx, PreserveWrapper, HasReleasedWrapper);
  JS_InitReadPrincipalsCallback(cx, nsJSPrincipals::ReadPrincipals);
  JS_SetAccumulateTelemetryCallback(cx, AccumulateTelemetryCallback);
  JS_SetSetUseCounterCallback(cx, SetUseCounterCallback);

  js::SetWindowProxyClass(cx, &OuterWindowProxyClass);

  JS::SetXrayJitInfo(&gXrayJitInfo);
  JS::SetProcessLargeAllocationFailureCallback(
      OnLargeAllocationFailureCallback);

  // The WasmAltDataType is build by the JS engine from the build id.
  JS::SetProcessBuildIdOp(GetBuildId);
  FetchUtil::InitWasmAltDataType();

  // The JS engine needs to keep the source code around in order to implement
  // Function.prototype.toSource(). It'd be nice to not have to do this for
  // chrome code and simply stub out requests for source on it. Life is not so
  // easy, unfortunately. Nobody relies on chrome toSource() working in core
  // browser code, but chrome tests use it. The worst offenders are addons,
  // which like to monkeypatch chrome functions by calling toSource() on them
  // and using regular expressions to modify them. We avoid keeping most browser
  // JS source code in memory by setting LAZY_SOURCE on JS::CompileOptions when
  // compiling some chrome code. This causes the JS engine not save the source
  // code in memory. When the JS engine is asked to provide the source for a
  // function compiled with LAZY_SOURCE, it calls SourceHook to load it.
  ///
  // Note we do have to retain the source code in memory for scripts compiled in
  // isRunOnce mode and compiled function bodies (from
  // JS::CompileFunction). In practice, this means content scripts and event
  // handlers.
  mozilla::UniquePtr<XPCJSSourceHook> hook(new XPCJSSourceHook);
  js::SetSourceHook(cx, std::move(hook));

  // Register memory reporters and distinguished amount functions.
  RegisterStrongMemoryReporter(new JSMainRuntimeRealmsReporter());
  RegisterStrongMemoryReporter(new JSMainRuntimeTemporaryPeakReporter());
  RegisterJSMainRuntimeGCHeapDistinguishedAmount(
      JSMainRuntimeGCHeapDistinguishedAmount);
  RegisterJSMainRuntimeTemporaryPeakDistinguishedAmount(
      JSMainRuntimeTemporaryPeakDistinguishedAmount);
  RegisterJSMainRuntimeCompartmentsSystemDistinguishedAmount(
      JSMainRuntimeCompartmentsSystemDistinguishedAmount);
  RegisterJSMainRuntimeCompartmentsUserDistinguishedAmount(
      JSMainRuntimeCompartmentsUserDistinguishedAmount);
  RegisterJSMainRuntimeRealmsSystemDistinguishedAmount(
      JSMainRuntimeRealmsSystemDistinguishedAmount);
  RegisterJSMainRuntimeRealmsUserDistinguishedAmount(
      JSMainRuntimeRealmsUserDistinguishedAmount);
  mozilla::RegisterJSSizeOfTab(JSSizeOfTab);

  // Set the callback for reporting memory to ubi::Node.
  JS::ubi::SetConstructUbiNodeForDOMObjectCallback(cx, &ConstructUbiNode);

  xpc_LocalizeRuntime(JS_GetRuntime(cx));
}

bool XPCJSRuntime::InitializeStrings(JSContext* cx) {
  // if it is our first context then we need to generate our string ids
  if (mStrIDs[0].isVoid()) {
    RootedString str(cx);
    for (unsigned i = 0; i < XPCJSContext::IDX_TOTAL_COUNT; i++) {
      str = JS_AtomizeAndPinString(cx, mStrings[i]);
      if (!str) {
        mStrIDs[0] = JS::PropertyKey::Void();
        return false;
      }
      mStrIDs[i] = PropertyKey::fromPinnedString(str);
    }

    if (!mozilla::dom::DefineStaticJSVals(cx)) {
      return false;
    }
  }

  return true;
}

bool XPCJSRuntime::DescribeCustomObjects(JSObject* obj, const JSClass* clasp,
                                         char (&name)[72]) const {
  if (clasp != &XPC_WN_Proto_JSClass) {
    return false;
  }

  XPCWrappedNativeProto* p = XPCWrappedNativeProto::Get(obj);
  // Nothing here can GC. The analysis would otherwise think that ~nsCOMPtr
  // could GC, but that's only possible if nsIXPCScriptable::GetJSClass()
  // somehow released a reference to the nsIXPCScriptable, which isn't going to
  // happen.
  JS::AutoSuppressGCAnalysis nogc;
  nsCOMPtr<nsIXPCScriptable> scr = p->GetScriptable();
  if (!scr) {
    return false;
  }

  SprintfLiteral(name, "JS Object (%s - %s)", clasp->name,
                 scr->GetJSClass()->name);
  return true;
}

bool XPCJSRuntime::NoteCustomGCThingXPCOMChildren(
    const JSClass* clasp, JSObject* obj,
    nsCycleCollectionTraversalCallback& cb) const {
  if (clasp != &XPC_WN_Tearoff_JSClass) {
    return false;
  }

  // A tearoff holds a strong reference to its native object
  // (see XPCWrappedNative::FlatJSObjectFinalized). Its XPCWrappedNative
  // will be held alive through tearoff's XPC_WN_TEAROFF_FLAT_OBJECT_SLOT,
  // which points to the XPCWrappedNative's mFlatJSObject.
  XPCWrappedNativeTearOff* to = XPCWrappedNativeTearOff::Get(obj);
  NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(
      cb, "XPCWrappedNativeTearOff::Get(obj)->mNative");
  cb.NoteXPCOMChild(to->GetNative());
  return true;
}

/***************************************************************************/

void XPCJSRuntime::DebugDump(int16_t depth) {
#ifdef DEBUG
  depth--;
  XPC_LOG_ALWAYS(("XPCJSRuntime @ %p", this));
  XPC_LOG_INDENT();

  // iterate wrappers...
  XPC_LOG_ALWAYS(("mWrappedJSMap @ %p with %d wrappers(s)", mWrappedJSMap.get(),
                  mWrappedJSMap->Count()));
  if (depth && mWrappedJSMap->Count()) {
    XPC_LOG_INDENT();
    mWrappedJSMap->Dump(depth);
    XPC_LOG_OUTDENT();
  }

  XPC_LOG_ALWAYS(("mIID2NativeInterfaceMap @ %p with %d interface(s)",
                  mIID2NativeInterfaceMap.get(),
                  mIID2NativeInterfaceMap->Count()));

  XPC_LOG_ALWAYS(("mClassInfo2NativeSetMap @ %p with %d sets(s)",
                  mClassInfo2NativeSetMap.get(),
                  mClassInfo2NativeSetMap->Count()));

  XPC_LOG_ALWAYS(("mNativeSetMap @ %p with %d sets(s)", mNativeSetMap.get(),
                  mNativeSetMap->Count()));

  // iterate sets...
  if (depth && mNativeSetMap->Count()) {
    XPC_LOG_INDENT();
    for (auto i = mNativeSetMap->Iter(); !i.done(); i.next()) {
      i.get()->DebugDump(depth);
    }
    XPC_LOG_OUTDENT();
  }

  XPC_LOG_OUTDENT();
#endif
}

/***************************************************************************/

void XPCJSRuntime::AddGCCallback(xpcGCCallback cb) {
  MOZ_ASSERT(cb, "null callback");
  extraGCCallbacks.AppendElement(cb);
}

void XPCJSRuntime::RemoveGCCallback(xpcGCCallback cb) {
  MOZ_ASSERT(cb, "null callback");
  bool found = extraGCCallbacks.RemoveElement(cb);
  if (!found) {
    NS_ERROR("Removing a callback which was never added.");
  }
}

JSObject* XPCJSRuntime::GetUAWidgetScope(JSContext* cx,
                                         nsIPrincipal* principal) {
  MOZ_ASSERT(!principal->IsSystemPrincipal(), "Running UA Widget in chrome");

  RootedObject scope(cx);
  do {
    RefPtr<BasePrincipal> key = BasePrincipal::Cast(principal);
    if (Principal2JSObjectMap::Ptr p = mUAWidgetScopeMap.lookup(key)) {
      scope = p->value();
      break;  // Need ~RefPtr to run, and potentially GC, before returning.
    }

    SandboxOptions options;
    options.sandboxName.AssignLiteral("UA Widget Scope");
    options.wantXrays = false;
    options.wantComponents = false;
    options.isUAWidgetScope = true;

    // Use an ExpandedPrincipal to create asymmetric security.
    MOZ_ASSERT(!nsContentUtils::IsExpandedPrincipal(principal));
    nsTArray<nsCOMPtr<nsIPrincipal>> principalAsArray{principal};
    RefPtr<ExpandedPrincipal> ep = ExpandedPrincipal::Create(
        principalAsArray, principal->OriginAttributesRef());

    // Create the sandbox.
    RootedValue v(cx);
    nsresult rv = CreateSandboxObject(
        cx, &v, static_cast<nsIExpandedPrincipal*>(ep), options);
    NS_ENSURE_SUCCESS(rv, nullptr);
    scope = &v.toObject();

    JSObject* unwrapped = js::UncheckedUnwrap(scope);
    MOZ_ASSERT(xpc::IsInUAWidgetScope(unwrapped));

    MOZ_ALWAYS_TRUE(mUAWidgetScopeMap.putNew(key, unwrapped));
  } while (false);

  return scope;
}

JSObject* XPCJSRuntime::UnprivilegedJunkScope(const mozilla::fallible_t&) {
  if (!mUnprivilegedJunkScope) {
    dom::AutoJSAPI jsapi;
    jsapi.Init();
    JSContext* cx = jsapi.cx();

    SandboxOptions options;
    options.sandboxName.AssignLiteral("XPConnect Junk Compartment");
    options.invisibleToDebugger = true;

    RootedValue sandbox(cx);
    nsresult rv = CreateSandboxObject(cx, &sandbox, nullptr, options);
    NS_ENSURE_SUCCESS(rv, nullptr);

    mUnprivilegedJunkScope =
        SandboxPrivate::GetPrivate(sandbox.toObjectOrNull());
  }
  MOZ_ASSERT(mUnprivilegedJunkScope->GetWrapper(),
             "Wrapper should have same lifetime as weak reference");
  return mUnprivilegedJunkScope->GetWrapper();
}

JSObject* XPCJSRuntime::UnprivilegedJunkScope() {
  JSObject* scope = UnprivilegedJunkScope(fallible);
  MOZ_RELEASE_ASSERT(scope);
  return scope;
}

bool XPCJSRuntime::IsUnprivilegedJunkScope(JSObject* obj) {
  return mUnprivilegedJunkScope && obj == mUnprivilegedJunkScope->GetWrapper();
}

void XPCJSRuntime::DeleteSingletonScopes() {
  // We're pretty late in shutdown, so we call ReleaseWrapper on the scopes.
  // This way the GC can collect them immediately, and we don't rely on the CC
  // to clean up.
  if (RefPtr<SandboxPrivate> sandbox = mUnprivilegedJunkScope.get()) {
    sandbox->ReleaseWrapper(sandbox);
    mUnprivilegedJunkScope = nullptr;
  }
}

uint32_t GetAndClampCPUCount() {
  // See HelperThreads.cpp for why we want between 2-8 threads
  int32_t proc = GetNumberOfProcessors();
  if (proc < 2) {
    return 2;
  }
  return std::min(proc, 8);
}
