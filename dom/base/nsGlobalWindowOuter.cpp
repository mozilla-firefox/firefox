/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Assertions.h"
#include "mozilla/ScopeExit.h"
#include "nsGlobalWindowOuter.h"
#include "nsGlobalWindowInner.h"

#include <algorithm>

#include "mozilla/MemoryReporting.h"

// Local Includes
#include "Navigator.h"
#include "nsContentSecurityManager.h"
#include "nsGlobalWindowOuter.h"
#include "nsScreen.h"
#include "nsHistory.h"
#include "nsDOMNavigationTiming.h"
#include "nsIDOMStorageManager.h"
#include "nsISecureBrowserUI.h"
#include "nsIWebProgressListener.h"
#include "mozilla/AntiTrackingUtils.h"
#include "mozilla/Result.h"
#include "mozilla/dom/AutoPrintEventDispatcher.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/BrowserChild.h"
#include "mozilla/dom/BrowsingContextBinding.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/ContentFrameMessageManager.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/EventTarget.h"
#include "mozilla/dom/HTMLIFrameElement.h"
#include "mozilla/dom/LocalStorage.h"
#include "mozilla/dom/LSObject.h"
#include "mozilla/dom/Storage.h"
#include "mozilla/dom/MaybeCrossOriginObject.h"
#include "mozilla/dom/Performance.h"
#include "mozilla/dom/ProxyHandlerUtils.h"
#include "mozilla/dom/StorageEvent.h"
#include "mozilla/dom/StorageEventBinding.h"
#include "mozilla/dom/StorageNotifierService.h"
#include "mozilla/dom/StorageUtils.h"
#include "mozilla/dom/Timeout.h"
#include "mozilla/dom/TimeoutHandler.h"
#include "mozilla/dom/TimeoutManager.h"
#include "mozilla/dom/UserActivation.h"
#include "mozilla/dom/WindowContext.h"
#include "mozilla/dom/WindowFeatures.h"  // WindowFeatures
#include "mozilla/dom/WindowProxyHolder.h"
#include "mozilla/IntegerPrintfMacros.h"
#include "mozilla/StorageAccessAPIHelper.h"
#include "nsBaseCommandController.h"
#include "nsError.h"
#include "nsICookieService.h"
#include "nsISizeOfEventTarget.h"
#include "nsDOMJSUtils.h"
#include "nsArrayUtils.h"
#include "nsIDocShellTreeOwner.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIPermissionManager.h"
#include "nsIScriptContext.h"
#include "nsWindowMemoryReporter.h"
#include "nsWindowSizes.h"
#include "nsWindowWatcher.h"
#include "WindowNamedPropertiesHandler.h"
#include "nsFrameSelection.h"
#include "nsNetUtil.h"
#include "nsVariant.h"
#include "nsPrintfCString.h"
#include "mozilla/intl/LocaleService.h"
#include "WindowDestroyedEvent.h"
#include "nsDocShellLoadState.h"
#include "mozilla/dom/WindowGlobalChild.h"

// Helper Classes
#include "nsJSUtils.h"
#include "jsapi.h"
#include "jsfriendapi.h"
#include "js/CallAndConstruct.h"    // JS::Call
#include "js/friend/StackLimits.h"  // js::AutoCheckRecursionLimit
#include "js/friend/WindowProxy.h"  // js::IsWindowProxy, js::SetWindowProxy
#include "js/PropertyAndElement.h"  // JS_DefineObject, JS_GetProperty
#include "js/PropertySpec.h"
#include "js/RealmIterators.h"
#include "js/Wrapper.h"
#include "nsLayoutUtils.h"
#include "nsReadableUtils.h"
#include "nsJSEnvironment.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/Preferences.h"
#include "mozilla/Likely.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/Sprintf.h"
#include "mozilla/Unused.h"

// Other Classes
#include "mozilla/dom/BarProps.h"
#include "nsLayoutStatics.h"
#include "nsCCUncollectableMarker.h"
#include "mozilla/dom/WorkerCommon.h"
#include "mozilla/dom/ToJSValue.h"
#include "nsJSPrincipals.h"
#include "mozilla/Attributes.h"
#include "mozilla/Components.h"
#include "mozilla/Debug.h"
#include "mozilla/EventListenerManager.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/PresShell.h"
#include "mozilla/ProcessHangMonitor.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_full_screen_api.h"
#include "mozilla/StaticPrefs_print.h"
#include "mozilla/StaticPrefs_fission.h"
#include "mozilla/ThrottledEventQueue.h"
#include "AudioChannelService.h"
#include "nsAboutProtocolUtils.h"
#include "nsCharTraits.h"  // NS_IS_HIGH/LOW_SURROGATE
#include "PostMessageEvent.h"
#include "mozilla/dom/DocGroup.h"
#include "mozilla/net/CookieJarSettings.h"

// Interfaces Needed
#include "nsIFrame.h"
#include "nsCanvasFrame.h"
#include "nsIWidget.h"
#include "nsIWidgetListener.h"
#include "nsIBaseWindow.h"
#include "nsIDeviceSensors.h"
#include "nsIContent.h"
#include "nsIDocShell.h"
#include "mozilla/dom/Document.h"
#include "Crypto.h"
#include "nsDOMString.h"
#include "nsThreadUtils.h"
#include "nsILoadContext.h"
#include "nsView.h"
#include "nsViewManager.h"
#include "nsIPrompt.h"
#include "nsIPromptService.h"
#include "nsIPromptFactory.h"
#include "nsIWritablePropertyBag2.h"
#include "nsIWebNavigation.h"
#include "nsIWebBrowserChrome.h"
#include "nsIWebBrowserFind.h"  // For window.find()
#include "nsComputedDOMStyle.h"
#include "nsDOMCID.h"
#include "nsDOMWindowUtils.h"
#include "nsIWindowWatcher.h"
#include "nsPIWindowWatcher.h"
#include "nsIDocumentViewer.h"
#include "nsIScriptError.h"
#include "nsISHistory.h"
#include "nsIControllers.h"
#include "nsGlobalWindowCommands.h"
#include "nsQueryObject.h"
#include "nsContentUtils.h"
#include "nsCSSProps.h"
#include "nsIURIFixup.h"
#include "nsIURIMutator.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/EventStateManager.h"
#include "mozilla/ScrollContainerFrame.h"
#include "nsIObserverService.h"
#include "nsFocusManager.h"
#include "nsIAppWindow.h"
#include "nsServiceManagerUtils.h"
#include "mozilla/dom/CustomEvent.h"
#include "nsIScreenManager.h"
#include "nsIClassifiedChannel.h"
#include "nsIXULRuntime.h"
#include "xpcprivate.h"

#ifdef NS_PRINTING
#  include "nsIPrintSettings.h"
#  include "nsIPrintSettingsService.h"
#  include "nsIWebBrowserPrint.h"
#endif

#include "nsWindowRoot.h"
#include "nsNetCID.h"
#include "nsIArray.h"

#include "nsIDOMXULCommandDispatcher.h"

#include "mozilla/GlobalKeyListener.h"

#include "nsIDragService.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/Selection.h"
#include "nsFrameLoader.h"
#include "nsFrameLoaderOwner.h"
#include "nsXPCOMCID.h"
#include "mozilla/Logging.h"
#include "mozilla/ProfilerMarkers.h"
#include "prenv.h"

#include "mozilla/dom/IDBFactory.h"
#include "mozilla/dom/MessageChannel.h"
#include "mozilla/dom/Promise.h"

#include "mozilla/dom/Gamepad.h"
#include "mozilla/dom/GamepadManager.h"

#include "gfxVR.h"
#include "VRShMem.h"
#include "FxRWindowManager.h"
#include "mozilla/dom/VRDisplay.h"
#include "mozilla/dom/VRDisplayEvent.h"
#include "mozilla/dom/VRDisplayEventBinding.h"
#include "mozilla/dom/VREventObserver.h"

#include "nsRefreshDriver.h"

#include "mozilla/extensions/WebExtensionPolicy.h"

#include "mozilla/BasePrincipal.h"
#include "mozilla/Services.h"
#include "mozilla/glean/DomMetrics.h"
#include "mozilla/dom/Location.h"
#include "nsHTMLDocument.h"
#include "nsWrapperCacheInlines.h"
#include "mozilla/DOMEventTargetHelper.h"
#include "prrng.h"
#include "nsSandboxFlags.h"
#include "nsXULControllers.h"
#include "mozilla/dom/AudioContext.h"
#include "mozilla/dom/BrowsingContextGroup.h"
#include "mozilla/dom/cache/CacheStorage.h"
#include "mozilla/dom/Console.h"
#include "mozilla/dom/Fetch.h"
#include "mozilla/dom/FunctionBinding.h"
#include "mozilla/dom/HashChangeEvent.h"
#include "mozilla/dom/IntlUtils.h"
#include "mozilla/dom/PopStateEvent.h"
#include "mozilla/dom/PopupBlockedEvent.h"
#include "mozilla/dom/PrimitiveConversions.h"
#include "mozilla/dom/WindowBinding.h"
#include "nsIBrowserChild.h"
#include "mozilla/dom/MediaQueryList.h"
#include "mozilla/dom/NavigatorBinding.h"
#include "mozilla/dom/ImageBitmap.h"
#include "mozilla/dom/ImageBitmapBinding.h"
#include "mozilla/dom/ServiceWorkerRegistration.h"
#include "mozilla/dom/WebIDLGlobalNameHash.h"
#include "mozilla/dom/Worklet.h"
#include "AccessCheck.h"

#ifdef MOZ_WEBSPEECH
#  include "mozilla/dom/SpeechSynthesis.h"
#endif

#ifdef ANDROID
#  include <android/log.h>
#endif

#ifdef XP_WIN
#  include <process.h>
#  define getpid _getpid
#else
#  include <unistd.h>  // for getpid()
#endif

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::dom::ipc;
using mozilla::BasePrincipal;
using mozilla::OriginAttributes;
using mozilla::TimeStamp;
using mozilla::layout::RemotePrintJobChild;

static inline nsGlobalWindowInner* GetCurrentInnerWindowInternal(
    const nsGlobalWindowOuter* aOuter) {
  return nsGlobalWindowInner::Cast(aOuter->GetCurrentInnerWindow());
}

#define FORWARD_TO_INNER(method, args, err_rval)           \
  PR_BEGIN_MACRO                                           \
  if (!mInnerWindow) {                                     \
    NS_WARNING("No inner window available!");              \
    return err_rval;                                       \
  }                                                        \
  return GetCurrentInnerWindowInternal(this)->method args; \
  PR_END_MACRO

#define FORWARD_TO_INNER_VOID(method, args)         \
  PR_BEGIN_MACRO                                    \
  if (!mInnerWindow) {                              \
    NS_WARNING("No inner window available!");       \
    return;                                         \
  }                                                 \
  GetCurrentInnerWindowInternal(this)->method args; \
  return;                                           \
  PR_END_MACRO

// Same as FORWARD_TO_INNER, but this will create a fresh inner if an
// inner doesn't already exists.
#define FORWARD_TO_INNER_CREATE(method, args, err_rval)    \
  PR_BEGIN_MACRO                                           \
  if (!mInnerWindow) {                                     \
    if (mIsClosed) {                                       \
      return err_rval;                                     \
    }                                                      \
    nsCOMPtr<Document> kungFuDeathGrip = GetDoc();         \
    ::mozilla::Unused << kungFuDeathGrip;                  \
    if (!mInnerWindow) {                                   \
      return err_rval;                                     \
    }                                                      \
  }                                                        \
  return GetCurrentInnerWindowInternal(this)->method args; \
  PR_END_MACRO

static LazyLogModule gDOMLeakPRLogOuter("DOMLeakOuter");
extern LazyLogModule gPageCacheLog;

#ifdef DEBUG
static LazyLogModule gDocShellAndDOMWindowLeakLogging(
    "DocShellAndDOMWindowLeak");
#endif

nsGlobalWindowOuter::OuterWindowByIdTable*
    nsGlobalWindowOuter::sOuterWindowsById = nullptr;

/* static */
nsPIDOMWindowOuter* nsPIDOMWindowOuter::GetFromCurrentInner(
    nsPIDOMWindowInner* aInner) {
  if (!aInner) {
    return nullptr;
  }

  nsPIDOMWindowOuter* outer = aInner->GetOuterWindow();
  if (!outer || outer->GetCurrentInnerWindow() != aInner) {
    return nullptr;
  }

  return outer;
}

//*****************************************************************************
// nsOuterWindowProxy: Outer Window Proxy
//*****************************************************************************

// Give OuterWindowProxyClass 2 reserved slots, like the other wrappers, so
// JSObject::swap can swap it with CrossCompartmentWrappers without requiring
// malloc.
//
// We store the nsGlobalWindowOuter* in our first slot.
//
// We store our holder weakmap in the second slot.
const JSClass OuterWindowProxyClass = PROXY_CLASS_DEF(
    "Proxy", JSCLASS_HAS_RESERVED_SLOTS(2)); /* additional class flags */

static const size_t OUTER_WINDOW_SLOT = 0;
static const size_t HOLDER_WEAKMAP_SLOT = 1;

class nsOuterWindowProxy : public MaybeCrossOriginObject<js::Wrapper> {
  using Base = MaybeCrossOriginObject<js::Wrapper>;

 public:
  constexpr nsOuterWindowProxy() : Base(0) {}

  bool finalizeInBackground(const JS::Value& priv) const override {
    return false;
  }

  // Standard internal methods
  /**
   * Implementation of [[GetOwnProperty]] as defined at
   * https://html.spec.whatwg.org/multipage/window-object.html#windowproxy-getownproperty
   *
   * "proxy" is the WindowProxy object involved.  It may not be same-compartment
   * with cx.
   */
  bool getOwnPropertyDescriptor(
      JSContext* cx, JS::Handle<JSObject*> proxy, JS::Handle<jsid> id,
      JS::MutableHandle<Maybe<JS::PropertyDescriptor>> desc) const override;

  /*
   * Implementation of the same-origin case of
   * <https://html.spec.whatwg.org/multipage/window-object.html#windowproxy-getownproperty>.
   */
  bool definePropertySameOrigin(JSContext* cx, JS::Handle<JSObject*> proxy,
                                JS::Handle<jsid> id,
                                JS::Handle<JS::PropertyDescriptor> desc,
                                JS::ObjectOpResult& result) const override;

  /**
   * Implementation of [[OwnPropertyKeys]] as defined at
   *
   * https://html.spec.whatwg.org/multipage/window-object.html#windowproxy-ownpropertykeys
   *
   * "proxy" is the WindowProxy object involved.  It may not be same-compartment
   * with cx.
   */
  bool ownPropertyKeys(JSContext* cx, JS::Handle<JSObject*> proxy,
                       JS::MutableHandleVector<jsid> props) const override;
  /**
   * Implementation of [[Delete]] as defined at
   * https://html.spec.whatwg.org/multipage/window-object.html#windowproxy-delete
   *
   * "proxy" is the WindowProxy object involved.  It may not be same-compartment
   * with cx.
   */
  bool delete_(JSContext* cx, JS::Handle<JSObject*> proxy, JS::Handle<jsid> id,
               JS::ObjectOpResult& result) const override;

  /**
   * Implementaton of hook for superclass getPrototype() method.
   */
  JSObject* getSameOriginPrototype(JSContext* cx) const override;

  /**
   * Implementation of [[HasProperty]] internal method as defined at
   * https://tc39.github.io/ecma262/#sec-ordinary-object-internal-methods-and-internal-slots-hasproperty-p
   *
   * "proxy" is the WindowProxy object involved.  It may not be same-compartment
   * with cx.
   *
   * Note that the HTML spec does not define an override for this internal
   * method, so we just want the "normal object" behavior.  We have to override
   * it, because js::Wrapper also overrides, with "not normal" behavior.
   */
  bool has(JSContext* cx, JS::Handle<JSObject*> proxy, JS::Handle<jsid> id,
           bool* bp) const override;

  /**
   * Implementation of [[Get]] internal method as defined at
   * <https://html.spec.whatwg.org/multipage/window-object.html#windowproxy-get>.
   *
   * "proxy" is the WindowProxy object involved.  It may or may not be
   * same-compartment with "cx".
   *
   * "receiver" is the receiver ("this") for the get.  It will be
   * same-compartment with "cx".
   *
   * "vp" is the return value.  It will be same-compartment with "cx".
   */
  bool get(JSContext* cx, JS::Handle<JSObject*> proxy,
           JS::Handle<JS::Value> receiver, JS::Handle<jsid> id,
           JS::MutableHandle<JS::Value> vp) const override;

  /**
   * Implementation of [[Set]] internal method as defined at
   * <https://html.spec.whatwg.org/multipage/window-object.html#windowproxy-set>.
   *
   * "proxy" is the WindowProxy object involved.  It may or may not be
   * same-compartment with "cx".
   *
   * "v" is the value being set.  It will be same-compartment with "cx".
   *
   * "receiver" is the receiver ("this") for the set.  It will be
   * same-compartment with "cx".
   */
  bool set(JSContext* cx, JS::Handle<JSObject*> proxy, JS::Handle<jsid> id,
           JS::Handle<JS::Value> v, JS::Handle<JS::Value> receiver,
           JS::ObjectOpResult& result) const override;

  // SpiderMonkey extensions
  /**
   * Implementation of SpiderMonkey extension which just checks whether this
   * object has the property.  Basically Object.getOwnPropertyDescriptor(obj,
   * prop) !== undefined. but does not require reifying the descriptor.
   *
   * We have to override this because js::Wrapper overrides it, but we want
   * different behavior from js::Wrapper.
   *
   * "proxy" is the WindowProxy object involved.  It may not be same-compartment
   * with cx.
   */
  bool hasOwn(JSContext* cx, JS::Handle<JSObject*> proxy, JS::Handle<jsid> id,
              bool* bp) const override;

  /**
   * Implementation of SpiderMonkey extension which is used as a fast path for
   * enumerating.
   *
   * We have to override this because js::Wrapper overrides it, but we want
   * different behavior from js::Wrapper.
   *
   * "proxy" is the WindowProxy object involved.  It may not be same-compartment
   * with cx.
   */
  bool getOwnEnumerablePropertyKeys(
      JSContext* cx, JS::Handle<JSObject*> proxy,
      JS::MutableHandleVector<jsid> props) const override;

  /**
   * Hook used by SpiderMonkey to implement Object.prototype.toString.
   */
  const char* className(JSContext* cx,
                        JS::Handle<JSObject*> wrapper) const override;

  void finalize(JS::GCContext* gcx, JSObject* proxy) const override;
  size_t objectMoved(JSObject* proxy, JSObject* old) const override;

  bool isCallable(JSObject* obj) const override { return false; }
  bool isConstructor(JSObject* obj) const override { return false; }

  static const nsOuterWindowProxy singleton;

  static nsGlobalWindowOuter* GetOuterWindow(JSObject* proxy) {
    nsGlobalWindowOuter* outerWindow =
        nsGlobalWindowOuter::FromSupports(static_cast<nsISupports*>(
            js::GetProxyReservedSlot(proxy, OUTER_WINDOW_SLOT).toPrivate()));
    return outerWindow;
  }

 protected:
  // False return value means we threw an exception.  True return value
  // but false "found" means we didn't have a subframe at that index.
  bool GetSubframeWindow(JSContext* cx, JS::Handle<JSObject*> proxy,
                         JS::Handle<jsid> id, JS::MutableHandle<JS::Value> vp,
                         bool& found) const;

  // Returns a non-null window only if id is an index and we have a
  // window at that index.
  Nullable<WindowProxyHolder> GetSubframeWindow(JSContext* cx,
                                                JS::Handle<JSObject*> proxy,
                                                JS::Handle<jsid> id) const;

  bool AppendIndexedPropertyNames(JSObject* proxy,
                                  JS::MutableHandleVector<jsid> props) const;

  using MaybeCrossOriginObjectMixins::EnsureHolder;
  bool EnsureHolder(JSContext* cx, JS::Handle<JSObject*> proxy,
                    JS::MutableHandle<JSObject*> holder) const override;

  // Helper method for creating a special "print" method that allows printing
  // our PDF-viewer documents even if you're not same-origin with them.
  //
  // aProxy must be our nsOuterWindowProxy.  It will not be same-compartment
  // with aCx, since we only use this on the different-origin codepath!
  //
  // Can return true without filling in aDesc, which corresponds to not exposing
  // a "print" method.
  static bool MaybeGetPDFJSPrintMethod(
      JSContext* cx, JS::Handle<JSObject*> proxy,
      JS::MutableHandle<Maybe<JS::PropertyDescriptor>> desc);

  // The actual "print" method we use for the PDFJS case.
  static bool PDFJSPrintMethod(JSContext* cx, unsigned argc, JS::Value* vp);

  // Helper method to get the pre-PDF-viewer-messing-with-it principal from an
  // inner window.  Will return null if this is not a PDF-viewer inner or if the
  // principal could not be found for some reason.
  static already_AddRefed<nsIPrincipal> GetNoPDFJSPrincipal(
      nsGlobalWindowInner* inner);
};

const char* nsOuterWindowProxy::className(JSContext* cx,
                                          JS::Handle<JSObject*> proxy) const {
  MOZ_ASSERT(js::IsProxy(proxy));

  if (!IsPlatformObjectSameOrigin(cx, proxy)) {
    return "Object";
  }

  return "Window";
}

void nsOuterWindowProxy::finalize(JS::GCContext* gcx, JSObject* proxy) const {
  nsGlobalWindowOuter* outerWindow = GetOuterWindow(proxy);
  if (outerWindow) {
    outerWindow->ClearWrapper(proxy);
    BrowsingContext* bc = outerWindow->GetBrowsingContext();
    if (bc) {
      bc->ClearWindowProxy();
    }

    // Ideally we would use OnFinalize here, but it's possible that
    // EnsureScriptEnvironment will later be called on the window, and we don't
    // want to create a new script object in that case. Therefore, we need to
    // write a non-null value that will reliably crash when dereferenced.
    outerWindow->PoisonOuterWindowProxy(proxy);
  }
}

bool nsOuterWindowProxy::getOwnPropertyDescriptor(
    JSContext* cx, JS::Handle<JSObject*> proxy, JS::Handle<jsid> id,
    JS::MutableHandle<Maybe<JS::PropertyDescriptor>> desc) const {
  // First check for indexed access.  This is
  // https://html.spec.whatwg.org/multipage/window-object.html#windowproxy-getownproperty
  // step 2, mostly.
  JS::Rooted<JS::Value> subframe(cx);
  bool found;
  if (!GetSubframeWindow(cx, proxy, id, &subframe, found)) {
    return false;
  }
  if (found) {
    // Step 2.4.

    desc.set(Some(JS::PropertyDescriptor::Data(
        subframe, {
                      JS::PropertyAttribute::Configurable,
                      JS::PropertyAttribute::Enumerable,
                  })));
    return true;
  }

  bool isSameOrigin = IsPlatformObjectSameOrigin(cx, proxy);

  // If we did not find a subframe, we could still have an indexed property
  // access.  In that case we should throw a SecurityError in the cross-origin
  // case.
  if (!isSameOrigin && IsArrayIndex(GetArrayIndexFromId(id))) {
    // Step 2.5.2.
    return ReportCrossOriginDenial(cx, id, "access"_ns);
  }

  // Step 2.5.1 is handled via the forwarding to js::Wrapper; it saves us an
  // IsArrayIndex(GetArrayIndexFromId(id)) here.  We'll never have a property on
  // the Window whose name is an index, because our defineProperty doesn't pass
  // those on to the Window.

  // Step 3.
  if (isSameOrigin) {
    if (StaticPrefs::dom_missing_prop_counters_enabled() && id.isAtom()) {
      Window_Binding::CountMaybeMissingProperty(proxy, id);
    }

    // Fall through to js::Wrapper.
    {  // Scope for JSAutoRealm while we are dealing with js::Wrapper.
      // When forwarding to js::Wrapper, we should just enter the Realm of proxy
      // for now.  That's what js::Wrapper expects, and since we're same-origin
      // anyway this is not changing any security behavior.
      JSAutoRealm ar(cx, proxy);
      JS_MarkCrossZoneId(cx, id);
      bool ok = js::Wrapper::getOwnPropertyDescriptor(cx, proxy, id, desc);
      if (!ok) {
        return false;
      }

#if 0
      // See https://github.com/tc39/ecma262/issues/672 for more information.
      if (desc.isSome() &&
          !IsNonConfigurableReadonlyPrimitiveGlobalProp(cx, id)) {
        (*desc).setConfigurable(true);
      }
#endif
    }

    // Now wrap our descriptor back into the Realm that asked for it.
    return JS_WrapPropertyDescriptor(cx, desc);
  }

  // Step 4.
  if (!CrossOriginGetOwnPropertyHelper(cx, proxy, id, desc)) {
    return false;
  }

  // Step 5
  if (desc.isSome()) {
    return true;
  }

  // Non-spec step for the PDF viewer's window.print().  This comes before we
  // check for named subframes, because in the same-origin case print() would
  // shadow those.
  if (id == GetJSIDByIndex(cx, XPCJSContext::IDX_PRINT)) {
    if (!MaybeGetPDFJSPrintMethod(cx, proxy, desc)) {
      return false;
    }

    if (desc.isSome()) {
      return true;
    }
  }

  // Step 6 -- check for named subframes.
  if (id.isString()) {
    nsAutoJSString name;
    if (!name.init(cx, id.toString())) {
      return false;
    }
    nsGlobalWindowOuter* win = GetOuterWindow(proxy);
    if (RefPtr<BrowsingContext> childDOMWin = win->GetChildWindow(name)) {
      JS::Rooted<JS::Value> childValue(cx);
      if (!ToJSValue(cx, WindowProxyHolder(childDOMWin), &childValue)) {
        return false;
      }
      desc.set(Some(JS::PropertyDescriptor::Data(
          childValue, {JS::PropertyAttribute::Configurable})));
      return true;
    }
  }

  // And step 7.
  return CrossOriginPropertyFallback(cx, proxy, id, desc);
}

bool nsOuterWindowProxy::definePropertySameOrigin(
    JSContext* cx, JS::Handle<JSObject*> proxy, JS::Handle<jsid> id,
    JS::Handle<JS::PropertyDescriptor> desc, JS::ObjectOpResult& result) const {
  if (IsArrayIndex(GetArrayIndexFromId(id))) {
    // Spec says to Reject whether this is a supported index or not,
    // since we have no indexed setter or indexed creator.  It is up
    // to the caller to decide whether to throw a TypeError.
    return result.failCantDefineWindowElement();
  }

  JS::ObjectOpResult ourResult;
  bool ok = js::Wrapper::defineProperty(cx, proxy, id, desc, ourResult);
  if (!ok) {
    return false;
  }

  if (!ourResult.ok()) {
    // It's possible that this failed because the page got the existing
    // descriptor (which we force to claim to be configurable) and then tried to
    // redefine the property with the descriptor it got but a different value.
    // We want to allow this case to succeed, so check for it and if we're in
    // that case try again but now with an attempt to define a non-configurable
    // property.
    if (!desc.hasConfigurable() || !desc.configurable()) {
      // The incoming descriptor was not explicitly marked "configurable: true",
      // so it failed for some other reason.  Just propagate that reason out.
      result = ourResult;
      return true;
    }

    JS::Rooted<Maybe<JS::PropertyDescriptor>> existingDesc(cx);
    ok = js::Wrapper::getOwnPropertyDescriptor(cx, proxy, id, &existingDesc);
    if (!ok) {
      return false;
    }
    if (existingDesc.isNothing() || existingDesc->configurable()) {
      // We have no existing property, or its descriptor is already configurable
      // (on the Window itself, where things really can be non-configurable).
      // So we failed for some other reason, which we should propagate out.
      result = ourResult;
      return true;
    }

    JS::Rooted<JS::PropertyDescriptor> updatedDesc(cx, desc);
    updatedDesc.setConfigurable(false);

    JS::ObjectOpResult ourNewResult;
    ok = js::Wrapper::defineProperty(cx, proxy, id, updatedDesc, ourNewResult);
    if (!ok) {
      return false;
    }

    if (!ourNewResult.ok()) {
      // Twiddling the configurable flag didn't help.  Just return this failure
      // out to the caller.
      result = ourNewResult;
      return true;
    }
  }

#if 0
  // See https://github.com/tc39/ecma262/issues/672 for more information.
  if (desc.hasConfigurable() && !desc.configurable() &&
      !IsNonConfigurableReadonlyPrimitiveGlobalProp(cx, id)) {
    // Give callers a way to detect that they failed to "really" define a
    // non-configurable property.
    result.failCantDefineWindowNonConfigurable();
    return true;
  }
#endif

  result.succeed();
  return true;
}

bool nsOuterWindowProxy::ownPropertyKeys(
    JSContext* cx, JS::Handle<JSObject*> proxy,
    JS::MutableHandleVector<jsid> props) const {
  // Just our indexed stuff followed by our "normal" own property names.
  if (!AppendIndexedPropertyNames(proxy, props)) {
    return false;
  }

  if (IsPlatformObjectSameOrigin(cx, proxy)) {
    // When forwarding to js::Wrapper, we should just enter the Realm of proxy
    // for now.  That's what js::Wrapper expects, and since we're same-origin
    // anyway this is not changing any security behavior.
    JS::RootedVector<jsid> innerProps(cx);
    {  // Scope for JSAutoRealm so we can mark the ids once we exit it
      JSAutoRealm ar(cx, proxy);
      if (!js::Wrapper::ownPropertyKeys(cx, proxy, &innerProps)) {
        return false;
      }
    }
    for (auto& id : innerProps) {
      JS_MarkCrossZoneId(cx, id);
    }
    return js::AppendUnique(cx, props, innerProps);
  }

  // In the cross-origin case we purposefully exclude subframe names from the
  // list of property names we report here.
  JS::Rooted<JSObject*> holder(cx);
  if (!EnsureHolder(cx, proxy, &holder)) {
    return false;
  }

  JS::RootedVector<jsid> crossOriginProps(cx);
  if (!js::GetPropertyKeys(cx, holder,
                           JSITER_OWNONLY | JSITER_HIDDEN | JSITER_SYMBOLS,
                           &crossOriginProps) ||
      !js::AppendUnique(cx, props, crossOriginProps)) {
    return false;
  }

  // Add the "print" property if needed.
  nsGlobalWindowOuter* outer = GetOuterWindow(proxy);
  nsGlobalWindowInner* inner =
      nsGlobalWindowInner::Cast(outer->GetCurrentInnerWindow());
  if (inner) {
    nsCOMPtr<nsIPrincipal> targetPrincipal = GetNoPDFJSPrincipal(inner);
    if (targetPrincipal &&
        nsContentUtils::SubjectPrincipal(cx)->Equals(targetPrincipal)) {
      JS::RootedVector<jsid> printProp(cx);
      if (!printProp.append(GetJSIDByIndex(cx, XPCJSContext::IDX_PRINT)) ||
          !js::AppendUnique(cx, props, printProp)) {
        return false;
      }
    }
  }

  return xpc::AppendCrossOriginWhitelistedPropNames(cx, props);
}

bool nsOuterWindowProxy::delete_(JSContext* cx, JS::Handle<JSObject*> proxy,
                                 JS::Handle<jsid> id,
                                 JS::ObjectOpResult& result) const {
  if (!IsPlatformObjectSameOrigin(cx, proxy)) {
    return ReportCrossOriginDenial(cx, id, "delete"_ns);
  }

  if (!GetSubframeWindow(cx, proxy, id).IsNull()) {
    // Fail (which means throw if strict, else return false).
    return result.failCantDeleteWindowElement();
  }

  if (IsArrayIndex(GetArrayIndexFromId(id))) {
    // Indexed, but not supported.  Spec says return true.
    return result.succeed();
  }

  // We're same-origin, so it should be safe to enter the Realm of "proxy".
  // Let's do that, just in case, to avoid cross-compartment issues in our
  // js::Wrapper caller..
  JSAutoRealm ar(cx, proxy);
  JS_MarkCrossZoneId(cx, id);
  return js::Wrapper::delete_(cx, proxy, id, result);
}

JSObject* nsOuterWindowProxy::getSameOriginPrototype(JSContext* cx) const {
  return Window_Binding::GetProtoObjectHandle(cx);
}

bool nsOuterWindowProxy::has(JSContext* cx, JS::Handle<JSObject*> proxy,
                             JS::Handle<jsid> id, bool* bp) const {
  // We could just directly forward this method to js::BaseProxyHandler, but
  // that involves reifying the actual property descriptor, which might be more
  // work than we have to do for has() on the Window.

  if (!IsPlatformObjectSameOrigin(cx, proxy)) {
    // In the cross-origin case we only have own properties.  Just call hasOwn
    // directly.
    return hasOwn(cx, proxy, id, bp);
  }

  if (!GetSubframeWindow(cx, proxy, id).IsNull()) {
    *bp = true;
    return true;
  }

  // Just to be safe in terms of compartment asserts, enter the Realm of
  // "proxy".  We're same-origin with it, so this should be safe.
  JSAutoRealm ar(cx, proxy);
  JS_MarkCrossZoneId(cx, id);
  return js::Wrapper::has(cx, proxy, id, bp);
}

bool nsOuterWindowProxy::hasOwn(JSContext* cx, JS::Handle<JSObject*> proxy,
                                JS::Handle<jsid> id, bool* bp) const {
  // We could just directly forward this method to js::BaseProxyHandler, but
  // that involves reifying the actual property descriptor, which might be more
  // work than we have to do for hasOwn() on the Window.

  if (!IsPlatformObjectSameOrigin(cx, proxy)) {
    // Avoiding reifying the property descriptor here would require duplicating
    // a bunch of "is this property exposed cross-origin" logic, which is
    // probably not worth it.  Just forward this along to the base
    // implementation.
    //
    // It's very important to not forward this to js::Wrapper, because that will
    // not do the right security and cross-origin checks and will pass through
    // the call to the Window.
    //
    // The BaseProxyHandler code is OK with this happening without entering the
    // compartment of "proxy".
    return js::BaseProxyHandler::hasOwn(cx, proxy, id, bp);
  }

  if (!GetSubframeWindow(cx, proxy, id).IsNull()) {
    *bp = true;
    return true;
  }

  // Just to be safe in terms of compartment asserts, enter the Realm of
  // "proxy".  We're same-origin with it, so this should be safe.
  JSAutoRealm ar(cx, proxy);
  JS_MarkCrossZoneId(cx, id);
  return js::Wrapper::hasOwn(cx, proxy, id, bp);
}

bool nsOuterWindowProxy::get(JSContext* cx, JS::Handle<JSObject*> proxy,
                             JS::Handle<JS::Value> receiver,
                             JS::Handle<jsid> id,
                             JS::MutableHandle<JS::Value> vp) const {
  if (id == GetJSIDByIndex(cx, XPCJSContext::IDX_WRAPPED_JSOBJECT) &&
      xpc::AccessCheck::isChrome(js::GetContextCompartment(cx))) {
    vp.set(JS::ObjectValue(*proxy));
    return MaybeWrapValue(cx, vp);
  }

  if (!IsPlatformObjectSameOrigin(cx, proxy)) {
    return CrossOriginGet(cx, proxy, receiver, id, vp);
  }

  bool found;
  if (!GetSubframeWindow(cx, proxy, id, vp, found)) {
    return false;
  }

  if (found) {
    return true;
  }

  if (StaticPrefs::dom_missing_prop_counters_enabled() && id.isAtom()) {
    Window_Binding::CountMaybeMissingProperty(proxy, id);
  }

  {  // Scope for JSAutoRealm
    // Enter "proxy"'s Realm.  We're in the same-origin case, so this should be
    // safe.
    JSAutoRealm ar(cx, proxy);

    JS_MarkCrossZoneId(cx, id);

    JS::Rooted<JS::Value> wrappedReceiver(cx, receiver);
    if (!MaybeWrapValue(cx, &wrappedReceiver)) {
      return false;
    }

    // Fall through to js::Wrapper.
    if (!js::Wrapper::get(cx, proxy, wrappedReceiver, id, vp)) {
      return false;
    }
  }

  // Make sure our return value is in the caller compartment.
  return MaybeWrapValue(cx, vp);
}

bool nsOuterWindowProxy::set(JSContext* cx, JS::Handle<JSObject*> proxy,
                             JS::Handle<jsid> id, JS::Handle<JS::Value> v,
                             JS::Handle<JS::Value> receiver,
                             JS::ObjectOpResult& result) const {
  if (!IsPlatformObjectSameOrigin(cx, proxy)) {
    return CrossOriginSet(cx, proxy, id, v, receiver, result);
  }

  if (IsArrayIndex(GetArrayIndexFromId(id))) {
    // Reject the set.  It's up to the caller to decide whether to throw a
    // TypeError.  If the caller is strict mode JS code, it'll throw.
    return result.failReadOnly();
  }

  // Do the rest in the Realm of "proxy", since we're in the same-origin case.
  JSAutoRealm ar(cx, proxy);
  JS::Rooted<JS::Value> wrappedArg(cx, v);
  if (!MaybeWrapValue(cx, &wrappedArg)) {
    return false;
  }
  JS::Rooted<JS::Value> wrappedReceiver(cx, receiver);
  if (!MaybeWrapValue(cx, &wrappedReceiver)) {
    return false;
  }

  JS_MarkCrossZoneId(cx, id);

  return js::Wrapper::set(cx, proxy, id, wrappedArg, wrappedReceiver, result);
}

bool nsOuterWindowProxy::getOwnEnumerablePropertyKeys(
    JSContext* cx, JS::Handle<JSObject*> proxy,
    JS::MutableHandleVector<jsid> props) const {
  // We could just stop overring getOwnEnumerablePropertyKeys and let our
  // superclasses deal (by falling back on the BaseProxyHandler implementation
  // that uses a combination of ownPropertyKeys and getOwnPropertyDescriptor to
  // only return the enumerable ones.  But maybe there's value in having
  // somewhat faster for-in iteration on Window objects...

  // Like ownPropertyKeys, our indexed stuff followed by our "normal" enumerable
  // own property names.
  if (!AppendIndexedPropertyNames(proxy, props)) {
    return false;
  }

  if (!IsPlatformObjectSameOrigin(cx, proxy)) {
    // All the cross-origin properties other than the indexed props are
    // non-enumerable, so we're done here.
    return true;
  }

  // When forwarding to js::Wrapper, we should just enter the Realm of proxy
  // for now.  That's what js::Wrapper expects, and since we're same-origin
  // anyway this is not changing any security behavior.
  JS::RootedVector<jsid> innerProps(cx);
  {  // Scope for JSAutoRealm so we can mark the ids once we exit it.
    JSAutoRealm ar(cx, proxy);
    if (!js::Wrapper::getOwnEnumerablePropertyKeys(cx, proxy, &innerProps)) {
      return false;
    }
  }

  for (auto& id : innerProps) {
    JS_MarkCrossZoneId(cx, id);
  }

  return js::AppendUnique(cx, props, innerProps);
}

bool nsOuterWindowProxy::GetSubframeWindow(JSContext* cx,
                                           JS::Handle<JSObject*> proxy,
                                           JS::Handle<jsid> id,
                                           JS::MutableHandle<JS::Value> vp,
                                           bool& found) const {
  Nullable<WindowProxyHolder> frame = GetSubframeWindow(cx, proxy, id);
  if (frame.IsNull()) {
    found = false;
    return true;
  }

  found = true;
  return WrapObject(cx, frame.Value(), vp);
}

Nullable<WindowProxyHolder> nsOuterWindowProxy::GetSubframeWindow(
    JSContext* cx, JS::Handle<JSObject*> proxy, JS::Handle<jsid> id) const {
  uint32_t index = GetArrayIndexFromId(id);
  if (!IsArrayIndex(index)) {
    return nullptr;
  }

  nsGlobalWindowOuter* win = GetOuterWindow(proxy);
  return win->IndexedGetterOuter(index);
}

bool nsOuterWindowProxy::AppendIndexedPropertyNames(
    JSObject* proxy, JS::MutableHandleVector<jsid> props) const {
  uint32_t length = GetOuterWindow(proxy)->Length();
  MOZ_ASSERT(int32_t(length) >= 0);
  if (!props.reserve(props.length() + length)) {
    return false;
  }
  for (int32_t i = 0; i < int32_t(length); ++i) {
    if (!props.append(JS::PropertyKey::Int(i))) {
      return false;
    }
  }

  return true;
}

bool nsOuterWindowProxy::EnsureHolder(
    JSContext* cx, JS::Handle<JSObject*> proxy,
    JS::MutableHandle<JSObject*> holder) const {
  return EnsureHolder(cx, proxy, HOLDER_WEAKMAP_SLOT,
                      Window_Binding::sCrossOriginProperties, holder);
}

size_t nsOuterWindowProxy::objectMoved(JSObject* obj, JSObject* old) const {
  nsGlobalWindowOuter* outerWindow = GetOuterWindow(obj);
  if (outerWindow) {
    outerWindow->UpdateWrapper(obj, old);
    BrowsingContext* bc = outerWindow->GetBrowsingContext();
    if (bc) {
      bc->UpdateWindowProxy(obj, old);
    }
  }
  return 0;
}

enum { PDFJS_SLOT_CALLEE = 0 };

// static
bool nsOuterWindowProxy::MaybeGetPDFJSPrintMethod(
    JSContext* cx, JS::Handle<JSObject*> proxy,
    JS::MutableHandle<Maybe<JS::PropertyDescriptor>> desc) {
  MOZ_ASSERT(proxy);
  MOZ_ASSERT(!desc.isSome());

  nsGlobalWindowOuter* outer = GetOuterWindow(proxy);
  nsGlobalWindowInner* inner =
      nsGlobalWindowInner::Cast(outer->GetCurrentInnerWindow());
  if (!inner) {
    // No print method to expose.
    return true;
  }

  nsCOMPtr<nsIPrincipal> targetPrincipal = GetNoPDFJSPrincipal(inner);
  if (!targetPrincipal) {
    // Nothing special to be done.
    return true;
  }

  if (!nsContentUtils::SubjectPrincipal(cx)->Equals(targetPrincipal)) {
    // Not our origin's PDF document.
    return true;
  }

  // Get the function we plan to actually call.
  JS::Rooted<JSObject*> innerObj(cx, inner->GetGlobalJSObject());
  if (!innerObj) {
    // Really should not happen, but ok, let's just return.
    return true;
  }

  JS::Rooted<JS::Value> targetFunc(cx);
  {
    JSAutoRealm ar(cx, innerObj);
    if (!JS_GetProperty(cx, innerObj, "print", &targetFunc)) {
      return false;
    }
  }

  if (!targetFunc.isObject()) {
    // Who knows what's going on.  Just return.
    return true;
  }

  // The Realm of cx is the realm our caller is in and the realm we
  // should create our function in.  Note that we can't use the
  // standard XPConnect function forwarder machinery because our
  // "this" is cross-origin, so we have to do thus by hand.

  // Make sure targetFunc is wrapped into the right compartment.
  if (!MaybeWrapValue(cx, &targetFunc)) {
    return false;
  }

  JSFunction* fun =
      js::NewFunctionWithReserved(cx, PDFJSPrintMethod, 0, 0, "print");
  if (!fun) {
    return false;
  }

  JS::Rooted<JSObject*> funObj(cx, JS_GetFunctionObject(fun));
  js::SetFunctionNativeReserved(funObj, PDFJS_SLOT_CALLEE, targetFunc);

  // { value: <print>, writable: true, enumerable: true, configurable: true }
  // because that's what it would have been in the same-origin case without
  // the PDF viewer messing with things.
  desc.set(Some(JS::PropertyDescriptor::Data(
      JS::ObjectValue(*funObj),
      {JS::PropertyAttribute::Configurable, JS::PropertyAttribute::Enumerable,
       JS::PropertyAttribute::Writable})));
  return true;
}

// static
bool nsOuterWindowProxy::PDFJSPrintMethod(JSContext* cx, unsigned argc,
                                          JS::Value* vp) {
  JS::CallArgs args = CallArgsFromVp(argc, vp);

  JS::Rooted<JSObject*> realCallee(
      cx, &js::GetFunctionNativeReserved(&args.callee(), PDFJS_SLOT_CALLEE)
               .toObject());
  // Unchecked unwrap, because we want to extract the thing we really had
  // before.
  realCallee = js::UncheckedUnwrap(realCallee);

  JS::Rooted<JS::Value> thisv(cx, args.thisv());
  if (thisv.isNullOrUndefined()) {
    // Replace it with the global of our stashed callee, simulating the
    // global-assuming behavior of DOM methods.
    JS::Rooted<JSObject*> global(cx, JS::GetNonCCWObjectGlobal(realCallee));
    if (!MaybeWrapObject(cx, &global)) {
      return false;
    }
    thisv.setObject(*global);
  } else if (!thisv.isObject()) {
    return ThrowInvalidThis(cx, args, false, prototypes::id::Window);
  }

  // We want to do an UncheckedUnwrap here, because we're going to directly
  // examine the principal of the inner window, if we have an inner window.
  JS::Rooted<JSObject*> unwrappedObj(cx,
                                     js::UncheckedUnwrap(&thisv.toObject()));
  nsGlobalWindowInner* inner = nullptr;
  {
    // Do the unwrap in the Realm of the object we're looking at.
    JSAutoRealm ar(cx, unwrappedObj);
    UNWRAP_MAYBE_CROSS_ORIGIN_OBJECT(Window, &unwrappedObj, inner, cx);
  }
  if (!inner) {
    return ThrowInvalidThis(cx, args, false, prototypes::id::Window);
  }

  nsIPrincipal* callerPrincipal = nsContentUtils::SubjectPrincipal(cx);
  if (!callerPrincipal->SubsumesConsideringDomain(inner->GetPrincipal())) {
    // Check whether it's a PDF viewer from our origin.
    nsCOMPtr<nsIPrincipal> pdfPrincipal = GetNoPDFJSPrincipal(inner);
    if (!pdfPrincipal || !callerPrincipal->Equals(pdfPrincipal)) {
      // Security error.
      return ThrowInvalidThis(cx, args, true, prototypes::id::Window);
    }
  }

  // Go ahead and enter the Realm of our real callee to call it.  We'll pass it
  // our "thisv", just in case someone grabs a "print" method off one PDF
  // document and .call()s it on another one.
  {
    JSAutoRealm ar(cx, realCallee);
    if (!MaybeWrapValue(cx, &thisv)) {
      return false;
    }

    // Don't bother passing through the args; they will get ignored anyway.

    if (!JS::Call(cx, thisv, realCallee, JS::HandleValueArray::empty(),
                  args.rval())) {
      return false;
    }
  }

  // Wrap the return value (not that there should be any!) into the right
  // compartment.
  return MaybeWrapValue(cx, args.rval());
}

// static
already_AddRefed<nsIPrincipal> nsOuterWindowProxy::GetNoPDFJSPrincipal(
    nsGlobalWindowInner* inner) {
  if (!nsContentUtils::IsPDFJS(inner->GetPrincipal())) {
    return nullptr;
  }

  if (Document* doc = inner->GetExtantDoc()) {
    if (nsCOMPtr<nsIPropertyBag2> propBag =
            do_QueryInterface(doc->GetChannel())) {
      nsCOMPtr<nsIPrincipal> principal(
          do_GetProperty(propBag, u"noPDFJSPrincipal"_ns));
      return principal.forget();
    }
  }
  return nullptr;
}

const nsOuterWindowProxy nsOuterWindowProxy::singleton;

class nsChromeOuterWindowProxy : public nsOuterWindowProxy {
 public:
  constexpr nsChromeOuterWindowProxy() = default;

  const char* className(JSContext* cx,
                        JS::Handle<JSObject*> wrapper) const override;

  static const nsChromeOuterWindowProxy singleton;
};

const char* nsChromeOuterWindowProxy::className(
    JSContext* cx, JS::Handle<JSObject*> proxy) const {
  MOZ_ASSERT(js::IsProxy(proxy));

  return "ChromeWindow";
}

const nsChromeOuterWindowProxy nsChromeOuterWindowProxy::singleton;

static JSObject* NewOuterWindowProxy(JSContext* cx,
                                     JS::Handle<JSObject*> global,
                                     bool isChrome) {
  MOZ_ASSERT(JS_IsGlobalObject(global));

  JSAutoRealm ar(cx, global);

  js::WrapperOptions options;
  options.setClass(&OuterWindowProxyClass);
  JSObject* obj =
      js::Wrapper::New(cx, global,
                       isChrome ? &nsChromeOuterWindowProxy::singleton
                                : &nsOuterWindowProxy::singleton,
                       options);
  MOZ_ASSERT_IF(obj, js::IsWindowProxy(obj));
  return obj;
}

//*****************************************************************************
//***    nsGlobalWindowOuter: Object Management
//*****************************************************************************

nsGlobalWindowOuter::nsGlobalWindowOuter(uint64_t aWindowID)
    : nsPIDOMWindowOuter(aWindowID),
      mFullscreenHasChangedDuringProcessing(false),
      mForceFullScreenInWidget(false),
      mIsClosed(false),
      mInClose(false),
      mHavePendingClose(false),
      mBlockScriptedClosingFlag(false),
      mWasOffline(false),
      mCreatingInnerWindow(false),
      mIsChrome(false),
      mAllowScriptsToClose(false),
      mTopLevelOuterContentWindow(false),
      mDelayedPrintUntilAfterLoad(false),
      mDelayedCloseForPrinting(false),
      mShouldDelayPrintUntilAfterLoad(false),
#ifdef DEBUG
      mSerial(0),
      mSetOpenerWindowCalled(false),
#endif
      mCleanedUp(false),
      mCanSkipCCGeneration(0),
      mAutoActivateVRDisplayID(0) {
  AssertIsOnMainThread();
  SetIsOnMainThread();
  nsLayoutStatics::AddRef();

  // Initialize the PRCList (this).
  PR_INIT_CLIST(this);

  // |this| is an outer window. Outer windows start out frozen and
  // remain frozen until they get an inner window.
  MOZ_ASSERT(IsFrozen());

  // We could have failed the first time through trying
  // to create the entropy collector, so we should
  // try to get one until we succeed.

#ifdef DEBUG
  mSerial = nsContentUtils::InnerOrOuterWindowCreated();

  MOZ_LOG(gDocShellAndDOMWindowLeakLogging, LogLevel::Info,
          ("++DOMWINDOW == %d (%p) [pid = %d] [serial = %d] [outer = %p]\n",
           nsContentUtils::GetCurrentInnerOrOuterWindowCount(),
           static_cast<void*>(ToCanonicalSupports(this)), getpid(), mSerial,
           nullptr));
#endif

  MOZ_LOG(gDOMLeakPRLogOuter, LogLevel::Debug,
          ("DOMWINDOW %p created outer=nullptr", this));

  // Add ourselves to the outer windows list.
  MOZ_ASSERT(sOuterWindowsById, "Outer Windows hash table must be created!");

  // |this| is an outer window, add to the outer windows list.
  MOZ_ASSERT(!sOuterWindowsById->Contains(mWindowID),
             "This window shouldn't be in the hash table yet!");
  // We seem to see crashes in release builds because of null
  // |sOuterWindowsById|.
  if (sOuterWindowsById) {
    sOuterWindowsById->InsertOrUpdate(mWindowID, this);
  }
}

#ifdef DEBUG

/* static */
void nsGlobalWindowOuter::AssertIsOnMainThread() {
  MOZ_ASSERT(NS_IsMainThread());
}

#endif  // DEBUG

/* static */
void nsGlobalWindowOuter::Init() {
  AssertIsOnMainThread();

  NS_ASSERTION(gDOMLeakPRLogOuter,
               "gDOMLeakPRLogOuter should have been initialized!");

  sOuterWindowsById = new OuterWindowByIdTable();
}

nsGlobalWindowOuter::~nsGlobalWindowOuter() {
  AssertIsOnMainThread();

  if (sOuterWindowsById) {
    sOuterWindowsById->Remove(mWindowID);
  }

  nsContentUtils::InnerOrOuterWindowDestroyed();

#ifdef DEBUG
  if (MOZ_LOG_TEST(gDocShellAndDOMWindowLeakLogging, LogLevel::Info)) {
    nsAutoCString url;
    if (mLastOpenedURI) {
      url = mLastOpenedURI->GetSpecOrDefault();

      // Data URLs can be very long, so truncate to avoid flooding the log.
      const uint32_t maxURLLength = 1000;
      if (url.Length() > maxURLLength) {
        url.Truncate(maxURLLength);
      }
    }

    MOZ_LOG(
        gDocShellAndDOMWindowLeakLogging, LogLevel::Info,
        ("--DOMWINDOW == %d (%p) [pid = %d] [serial = %d] [outer = %p] [url = "
         "%s]\n",
         nsContentUtils::GetCurrentInnerOrOuterWindowCount(),
         static_cast<void*>(ToCanonicalSupports(this)), getpid(), mSerial,
         nullptr, url.get()));
  }
#endif

  MOZ_LOG(gDOMLeakPRLogOuter, LogLevel::Debug,
          ("DOMWINDOW %p destroyed", this));

  JSObject* proxy = GetWrapperMaybeDead();
  if (proxy) {
    if (mBrowsingContext && mBrowsingContext->GetUnbarrieredWindowProxy()) {
      nsGlobalWindowOuter* outer = nsOuterWindowProxy::GetOuterWindow(
          mBrowsingContext->GetUnbarrieredWindowProxy());
      // Check that the current WindowProxy object corresponds to this
      // nsGlobalWindowOuter, because we don't want to clear the WindowProxy if
      // we've replaced it with a cross-process WindowProxy.
      if (outer == this) {
        mBrowsingContext->ClearWindowProxy();
      }
    }
    js::SetProxyReservedSlot(proxy, OUTER_WINDOW_SLOT,
                             JS::PrivateValue(nullptr));
  }

  // An outer window is destroyed with inner windows still possibly
  // alive, iterate through the inner windows and null out their
  // back pointer to this outer, and pull them out of the list of
  // inner windows.
  //
  // Our linked list of inner windows both contains (an nsGlobalWindowOuter),
  // and our inner windows (nsGlobalWindowInners). This means that we need to
  // use PRCList*. We can then compare that PRCList* to `this` to see if its an
  // inner or outer window.
  PRCList* w;
  while ((w = PR_LIST_HEAD(this)) != this) {
    PR_REMOVE_AND_INIT_LINK(w);
  }

  DropOuterWindowDocs();

  // Outer windows are always supposed to call CleanUp before letting themselves
  // be destroyed.
  MOZ_ASSERT(mCleanedUp);

  nsCOMPtr<nsIDeviceSensors> ac = do_GetService(NS_DEVICE_SENSORS_CONTRACTID);
  if (ac) ac->RemoveWindowAsListener(this);

  nsLayoutStatics::Release();
}

// static
void nsGlobalWindowOuter::ShutDown() {
  AssertIsOnMainThread();

  delete sOuterWindowsById;
  sOuterWindowsById = nullptr;
}

void nsGlobalWindowOuter::DropOuterWindowDocs() {
  MOZ_ASSERT_IF(mDoc, !mDoc->EventHandlingSuppressed());
  mDoc = nullptr;
  mSuspendedDocs.Clear();
}

void nsGlobalWindowOuter::CleanUp() {
  // Guarantee idempotence.
  if (mCleanedUp) return;
  mCleanedUp = true;

  StartDying();

  mWindowUtils = nullptr;

  ClearControllers();

  mContext = nullptr;             // Forces Release
  mChromeEventHandler = nullptr;  // Forces Release
  mParentTarget = nullptr;
  mMessageManager = nullptr;

  mArguments = nullptr;
}

void nsGlobalWindowOuter::ClearControllers() {
  if (mControllers) {
    uint32_t count;
    mControllers->GetControllerCount(&count);

    while (count--) {
      nsCOMPtr<nsIController> controller;
      mControllers->GetControllerAt(count, getter_AddRefs(controller));

      nsCOMPtr<nsIControllerContext> context = do_QueryInterface(controller);
      if (context) context->SetCommandContext(nullptr);
    }

    mControllers = nullptr;
  }
}

//*****************************************************************************
// nsGlobalWindowOuter::nsISupports
//*****************************************************************************

// QueryInterface implementation for nsGlobalWindowOuter
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsGlobalWindowOuter)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, EventTarget)
  NS_INTERFACE_MAP_ENTRY(nsIDOMWindow)
  NS_INTERFACE_MAP_ENTRY(nsIGlobalObject)
  NS_INTERFACE_MAP_ENTRY(nsIScriptGlobalObject)
  NS_INTERFACE_MAP_ENTRY(nsIScriptObjectPrincipal)
  NS_INTERFACE_MAP_ENTRY(mozilla::dom::EventTarget)
  NS_INTERFACE_MAP_ENTRY(nsPIDOMWindowOuter)
  NS_INTERFACE_MAP_ENTRY(mozIDOMWindowProxy)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
  NS_INTERFACE_MAP_ENTRY(nsIInterfaceRequestor)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsGlobalWindowOuter)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsGlobalWindowOuter)

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_BEGIN(nsGlobalWindowOuter)
  if (tmp->IsBlackForCC(false)) {
    if (nsCCUncollectableMarker::InGeneration(tmp->mCanSkipCCGeneration)) {
      return true;
    }
    tmp->mCanSkipCCGeneration = nsCCUncollectableMarker::sGeneration;
    if (EventListenerManager* elm = tmp->GetExistingListenerManager()) {
      elm->MarkForCC();
    }
    return true;
  }
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_IN_CC_BEGIN(nsGlobalWindowOuter)
  return tmp->IsBlackForCC(true);
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_IN_CC_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_THIS_BEGIN(nsGlobalWindowOuter)
  return tmp->IsBlackForCC(false);
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_THIS_END

NS_IMPL_CYCLE_COLLECTION_CLASS(nsGlobalWindowOuter)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INTERNAL(nsGlobalWindowOuter)
  if (MOZ_UNLIKELY(cb.WantDebugInfo())) {
    char name[512];
    nsAutoCString uri;
    if (tmp->mDoc && tmp->mDoc->GetDocumentURI()) {
      uri = tmp->mDoc->GetDocumentURI()->GetSpecOrDefault();
    }
    SprintfLiteral(name, "nsGlobalWindowOuter # %" PRIu64 " outer %s",
                   tmp->mWindowID, uri.get());
    cb.DescribeRefCountedNode(tmp->mRefCnt.get(), name);
  } else {
    NS_IMPL_CYCLE_COLLECTION_DESCRIBE(nsGlobalWindowOuter, tmp->mRefCnt.get())
  }

  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mContext)

  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mControllers)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mArguments)

  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mLocalStorage)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mSuspendedDocs)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDocumentPrincipal)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDocumentCookiePrincipal)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDocumentStoragePrincipal)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDocumentPartitionedPrincipal)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDoc)

  // Traverse stuff from nsPIDOMWindow
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mChromeEventHandler)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mParentTarget)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mMessageManager)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mFrameElement)

  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDocShell)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mBrowsingContext)

  tmp->TraverseObjectsInGlobal(cb);

  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mChromeFields.mBrowserDOMWindow)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsGlobalWindowOuter)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_WEAK_REFERENCE
  if (sOuterWindowsById) {
    sOuterWindowsById->Remove(tmp->mWindowID);
  }

  NS_IMPL_CYCLE_COLLECTION_UNLINK(mContext)

  NS_IMPL_CYCLE_COLLECTION_UNLINK(mControllers)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mArguments)

  NS_IMPL_CYCLE_COLLECTION_UNLINK(mLocalStorage)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mSuspendedDocs)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDocumentPrincipal)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDocumentCookiePrincipal)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDocumentStoragePrincipal)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDocumentPartitionedPrincipal)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDoc)

  // Unlink stuff from nsPIDOMWindow
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mChromeEventHandler)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mParentTarget)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mMessageManager)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mFrameElement)

  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDocShell)
  if (tmp->mBrowsingContext) {
    if (tmp->mBrowsingContext->GetUnbarrieredWindowProxy()) {
      nsGlobalWindowOuter* outer = nsOuterWindowProxy::GetOuterWindow(
          tmp->mBrowsingContext->GetUnbarrieredWindowProxy());
      // Check that the current WindowProxy object corresponds to this
      // nsGlobalWindowOuter, because we don't want to clear the WindowProxy if
      // we've replaced it with a cross-process WindowProxy.
      if (outer == tmp) {
        tmp->mBrowsingContext->ClearWindowProxy();
      }
    }
    tmp->mBrowsingContext = nullptr;
  }

  tmp->UnlinkObjectsInGlobal();

  if (tmp->IsChromeWindow()) {
    NS_IMPL_CYCLE_COLLECTION_UNLINK(mChromeFields.mBrowserDOMWindow)
  }

  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(nsGlobalWindowOuter)
  NS_IMPL_CYCLE_COLLECTION_TRACE_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_TRACE_END

bool nsGlobalWindowOuter::IsBlackForCC(bool aTracingNeeded) {
  if (!nsCCUncollectableMarker::sGeneration) {
    return false;
  }

  // Unlike most wrappers, the outer window wrapper is not a wrapper for
  // the outer window. Instead, the outer window wrapper holds the inner
  // window binding object, which in turn holds the nsGlobalWindowInner, which
  // has a strong reference to the nsGlobalWindowOuter. We're using the
  // mInnerWindow pointer as a flag for that whole chain.
  return (nsCCUncollectableMarker::InGeneration(GetMarkedCCGeneration()) ||
          (mInnerWindow && HasKnownLiveWrapper())) &&
         (!aTracingNeeded || HasNothingToTrace(ToSupports(this)));
}

//*****************************************************************************
// nsGlobalWindowOuter::nsIScriptGlobalObject
//*****************************************************************************

bool nsGlobalWindowOuter::ShouldResistFingerprinting(RFPTarget aTarget) const {
  if (mDoc) {
    return mDoc->ShouldResistFingerprinting(aTarget);
  }
  return nsContentUtils::ShouldResistFingerprinting(
      "If we do not have a document then we do not have any context"
      "to make an informed RFP choice, so we fall back to the global pref",
      aTarget);
}

OriginTrials nsGlobalWindowOuter::Trials() const {
  return mInnerWindow ? nsGlobalWindowInner::Cast(mInnerWindow)->Trials()
                      : OriginTrials();
}

FontFaceSet* nsGlobalWindowOuter::GetFonts() {
  if (mDoc) {
    return mDoc->Fonts();
  }
  return nullptr;
}

nsresult nsGlobalWindowOuter::EnsureScriptEnvironment() {
  if (GetWrapperPreserveColor()) {
    return NS_OK;
  }

  NS_ENSURE_STATE(!mCleanedUp);

  NS_ASSERTION(!GetCurrentInnerWindowInternal(this),
               "No cached wrapper, but we have an inner window?");
  NS_ASSERTION(!mContext, "Will overwrite mContext!");

  // If this window is an [i]frame, don't bother GC'ing when the frame's context
  // is destroyed since a GC will happen when the frameset or host document is
  // destroyed anyway.
  mContext = new nsJSContext(mBrowsingContext->IsTop(), this);
  return NS_OK;
}

nsIScriptContext* nsGlobalWindowOuter::GetScriptContext() { return mContext; }

bool nsGlobalWindowOuter::WouldReuseInnerWindow(Document* aNewDocument) {
  // We reuse the inner window when:
  // a. We are currently at our original document.
  // b. At least one of the following conditions are true:
  // -- The new document is the same as the old document. This means that we're
  //    getting called from document.open().
  // -- The new document has the same origin as what we have loaded right now.

  if (!mDoc || !aNewDocument) {
    return false;
  }

  if (!mDoc->IsInitialDocument()) {
    return false;
  }

#ifdef DEBUG
  {
    nsCOMPtr<nsIURI> uri;
    NS_GetURIWithoutRef(mDoc->GetDocumentURI(), getter_AddRefs(uri));
    NS_ASSERTION(NS_IsAboutBlank(uri), "How'd this happen?");
  }
#endif

  // Great, we're the original document, check for one of the other
  // conditions.

  if (mDoc == aNewDocument) {
    return true;
  }

  if (aNewDocument->IsStaticDocument()) {
    return false;
  }

  if (BasePrincipal::Cast(mDoc->NodePrincipal())
          ->FastEqualsConsideringDomain(aNewDocument->NodePrincipal())) {
    // The origin is the same.
    return true;
  }

  return false;
}

void nsGlobalWindowOuter::SetInitialPrincipal(
    nsIPrincipal* aNewWindowPrincipal, nsIContentSecurityPolicy* aCSP,
    const Maybe<nsILoadInfo::CrossOriginEmbedderPolicy>& aCOEP) {
  // We should never create windows with an expanded principal.
  // If we have a system principal, make sure we're not using it for a content
  // docshell.
  // NOTE: Please keep this logic in sync with
  // nsAppShellService::JustCreateTopWindow
  if (nsContentUtils::IsExpandedPrincipal(aNewWindowPrincipal) ||
      (aNewWindowPrincipal->IsSystemPrincipal() &&
       GetBrowsingContext()->IsContent())) {
    aNewWindowPrincipal = nullptr;
  }

  // If there's an existing document, bail if it either:
  if (mDoc) {
    // (a) is not an initial about:blank document, or
    if (!mDoc->IsInitialDocument()) return;
    // (b) already has the correct principal.
    if (mDoc->NodePrincipal() == aNewWindowPrincipal) return;

#ifdef DEBUG
    // If we have a document loaded at this point, it had better be about:blank.
    // Otherwise, something is really weird. An about:blank page has a
    // NullPrincipal.
    bool isNullPrincipal;
    MOZ_ASSERT(NS_SUCCEEDED(mDoc->NodePrincipal()->GetIsNullPrincipal(
                   &isNullPrincipal)) &&
               isNullPrincipal);
#endif
  }

  // Use the subject (or system) principal as the storage principal too until
  // the new window finishes navigating and gets a real storage principal.
  nsDocShell::Cast(GetDocShell())
      ->CreateAboutBlankDocumentViewer(aNewWindowPrincipal, aNewWindowPrincipal,
                                       aCSP, nullptr,
                                       /* aIsInitialDocument */ true, aCOEP);

  if (mDoc) {
    MOZ_ASSERT(mDoc->IsInitialDocument(),
               "document should be initial document");
  }

  RefPtr<PresShell> presShell = GetDocShell()->GetPresShell();
  if (presShell && !presShell->DidInitialize()) {
    // Ensure that if someone plays with this document they will get
    // layout happening.
    presShell->Initialize();
  }
}

#define WINDOWSTATEHOLDER_IID \
  {0x0b917c3e, 0xbd50, 0x4683, {0xaf, 0xc9, 0xc7, 0x81, 0x07, 0xae, 0x33, 0x26}}

class WindowStateHolder final : public nsISupports {
 public:
  NS_DECLARE_STATIC_IID_ACCESSOR(WINDOWSTATEHOLDER_IID)
  NS_DECL_ISUPPORTS

  explicit WindowStateHolder(nsGlobalWindowInner* aWindow);

  nsGlobalWindowInner* GetInnerWindow() { return mInnerWindow; }

  void DidRestoreWindow() {
    mInnerWindow = nullptr;
    mInnerWindowReflector = nullptr;
  }

 protected:
  ~WindowStateHolder();

  nsGlobalWindowInner* mInnerWindow;
  // We hold onto this to make sure the inner window doesn't go away. The outer
  // window ends up recalculating it anyway.
  JS::PersistentRooted<JSObject*> mInnerWindowReflector;
};

NS_DEFINE_STATIC_IID_ACCESSOR(WindowStateHolder, WINDOWSTATEHOLDER_IID)

WindowStateHolder::WindowStateHolder(nsGlobalWindowInner* aWindow)
    : mInnerWindow(aWindow),
      mInnerWindowReflector(RootingCx(), aWindow->GetWrapper()) {
  MOZ_ASSERT(aWindow, "null window");

  aWindow->Suspend();

  // When a global goes into the bfcache, we disable script.
  xpc::Scriptability::Get(mInnerWindowReflector).SetWindowAllowsScript(false);
}

WindowStateHolder::~WindowStateHolder() {
  if (mInnerWindow) {
    // This window was left in the bfcache and is now going away. We need to
    // free it up.
    // Note that FreeInnerObjects may already have been called on the
    // inner window if its outer has already had SetDocShell(null)
    // called.
    mInnerWindow->FreeInnerObjects();
  }
}

NS_IMPL_ISUPPORTS(WindowStateHolder, WindowStateHolder)

bool nsGlobalWindowOuter::ComputeIsSecureContext(Document* aDocument,
                                                 SecureContextFlags aFlags) {
  nsCOMPtr<nsIPrincipal> principal = aDocument->NodePrincipal();
  if (principal->IsSystemPrincipal()) {
    return true;
  }

  // Implement https://w3c.github.io/webappsec-secure-contexts/#settings-object
  // With some modifications to allow for aFlags.

  bool hadNonSecureContextCreator = false;

  if (WindowContext* parentWindow =
          GetBrowsingContext()->GetParentWindowContext()) {
    hadNonSecureContextCreator = !parentWindow->GetIsSecureContext();
  }

  if (hadNonSecureContextCreator) {
    return false;
  }

  if (nsContentUtils::HttpsStateIsModern(aDocument)) {
    return true;
  }

  if (principal->GetIsNullPrincipal()) {
    // If the NullPrincipal has a valid precursor URI we want to use it to
    // construct the principal otherwise we fall back to the original document
    // URI.
    nsCOMPtr<nsIPrincipal> precursorPrin = principal->GetPrecursorPrincipal();
    nsCOMPtr<nsIURI> uri = precursorPrin ? precursorPrin->GetURI() : nullptr;
    if (!uri) {
      uri = aDocument->GetOriginalURI();
    }
    // IsOriginPotentiallyTrustworthy doesn't care about origin attributes so
    // it doesn't actually matter what we use here, but reusing the document
    // principal's attributes is convenient.
    const OriginAttributes& attrs = principal->OriginAttributesRef();
    // CreateContentPrincipal correctly gets a useful principal for blob: and
    // other URI_INHERITS_SECURITY_CONTEXT URIs.
    principal = BasePrincipal::CreateContentPrincipal(uri, attrs);
    if (NS_WARN_IF(!principal)) {
      return false;
    }
  }

  return principal->GetIsOriginPotentiallyTrustworthy();
}

static bool InitializeLegacyNetscapeObject(JSContext* aCx,
                                           JS::Handle<JSObject*> aGlobal) {
  JSAutoRealm ar(aCx, aGlobal);

  // Note: MathJax depends on window.netscape being exposed. See bug 791526.
  JS::Rooted<JSObject*> obj(aCx);
  obj = JS_DefineObject(aCx, aGlobal, "netscape", nullptr);
  NS_ENSURE_TRUE(obj, false);

  obj = JS_DefineObject(aCx, obj, "security", nullptr);
  NS_ENSURE_TRUE(obj, false);

  return true;
}

struct MOZ_STACK_CLASS CompartmentFinderState {
  explicit CompartmentFinderState(nsIPrincipal* aPrincipal)
      : principal(aPrincipal), compartment(nullptr) {}

  // Input: we look for a compartment which is same-origin with the
  // given principal.
  nsIPrincipal* principal;

  // Output: We set this member if we find a compartment.
  JS::Compartment* compartment;
};

static JS::CompartmentIterResult FindSameOriginCompartment(
    JSContext* aCx, void* aData, JS::Compartment* aCompartment) {
  auto* data = static_cast<CompartmentFinderState*>(aData);
  MOZ_ASSERT(!data->compartment, "Why are we getting called?");

  // If this compartment is not safe to share across globals, don't do
  // anything with it; in particular we should not be getting a
  // CompartmentPrivate from such a compartment, because it may be in
  // the middle of being collected and its CompartmentPrivate may no
  // longer be valid.
  if (!js::IsSharableCompartment(aCompartment)) {
    return JS::CompartmentIterResult::KeepGoing;
  }

  auto* compartmentPrivate = xpc::CompartmentPrivate::Get(aCompartment);
  if (!compartmentPrivate->CanShareCompartmentWith(data->principal)) {
    // Can't reuse this one, keep going.
    return JS::CompartmentIterResult::KeepGoing;
  }

  // We have a winner!
  data->compartment = aCompartment;
  return JS::CompartmentIterResult::Stop;
}

static JS::RealmCreationOptions& SelectZone(
    JSContext* aCx, nsIPrincipal* aPrincipal, nsGlobalWindowInner* aNewInner,
    JS::RealmCreationOptions& aOptions) {
  // Use the shared system compartment for chrome windows.
  if (aPrincipal->IsSystemPrincipal()) {
    return aOptions.setExistingCompartment(xpc::PrivilegedJunkScope());
  }

  BrowsingContext* bc = aNewInner->GetBrowsingContext();
  if (bc->IsTop()) {
    // We're a toplevel load.  Use a new zone.  This way, when we do
    // zone-based compartment sharing we won't share compartments
    // across navigations.
    return aOptions.setNewCompartmentAndZone();
  }

  // Find the in-process ancestor highest in the hierarchy.
  nsGlobalWindowInner* ancestor = nullptr;
  for (WindowContext* wc = bc->GetParentWindowContext(); wc;
       wc = wc->GetParentWindowContext()) {
    if (nsGlobalWindowInner* win = wc->GetInnerWindow()) {
      ancestor = win;
    }
  }

  // If we have an ancestor window, use its zone.
  if (ancestor && ancestor->GetGlobalJSObject()) {
    JS::Zone* zone = JS::GetObjectZone(ancestor->GetGlobalJSObject());
    // Now try to find an existing compartment that's same-origin
    // with our principal.
    CompartmentFinderState data(aPrincipal);
    JS_IterateCompartmentsInZone(aCx, zone, &data, FindSameOriginCompartment);
    if (data.compartment) {
      return aOptions.setExistingCompartment(data.compartment);
    }
    return aOptions.setNewCompartmentInExistingZone(
        ancestor->GetGlobalJSObject());
  }

  return aOptions.setNewCompartmentAndZone();
}

/**
 * Create a new global object that will be used for an inner window.
 * Return the native global and an nsISupports 'holder' that can be used
 * to manage the lifetime of it.
 */
static nsresult CreateNativeGlobalForInner(
    JSContext* aCx, nsGlobalWindowInner* aNewInner, Document* aDocument,
    JS::MutableHandle<JSObject*> aGlobal, bool aIsSecureContext,
    bool aDefineSharedArrayBufferConstructor) {
  MOZ_ASSERT(aCx);
  MOZ_ASSERT(aNewInner);

  nsCOMPtr<nsIURI> uri = aDocument->GetDocumentURI();
  nsCOMPtr<nsIPrincipal> principal = aDocument->NodePrincipal();
  MOZ_ASSERT(principal);

  // DOMWindow with nsEP is not supported, we have to make sure
  // no one creates one accidentally.
  nsCOMPtr<nsIExpandedPrincipal> nsEP = do_QueryInterface(principal);
  MOZ_RELEASE_ASSERT(!nsEP, "DOMWindow with nsEP is not supported");

  JS::RealmOptions options;
  JS::RealmCreationOptions& creationOptions = options.creationOptions();

  SelectZone(aCx, principal, aNewInner, creationOptions);

  // Define the SharedArrayBuffer global constructor property only if shared
  // memory may be used and structured-cloned (e.g. through postMessage).
  //
  // When the global constructor property isn't defined, the SharedArrayBuffer
  // constructor can still be reached through Web Assembly.  Omitting the global
  // property just prevents feature-tests from being misled.  See bug 1624266.
  creationOptions.setDefineSharedArrayBufferConstructor(
      aDefineSharedArrayBufferConstructor);

  xpc::InitGlobalObjectOptions(
      options, principal->IsSystemPrincipal(), aIsSecureContext,
      aDocument->ShouldResistFingerprinting(RFPTarget::JSDateTimeUTC),
      aDocument->ShouldResistFingerprinting(RFPTarget::JSMathFdlibm),
      aDocument->ShouldResistFingerprinting(RFPTarget::JSLocale));

  // Determine if we need the Components object.
  bool needComponents = principal->IsSystemPrincipal();
  uint32_t flags = needComponents ? 0 : xpc::OMIT_COMPONENTS_OBJECT;
  flags |= xpc::DONT_FIRE_ONNEWGLOBALHOOK;

  if (!Window_Binding::Wrap(aCx, aNewInner, aNewInner, options,
                            nsJSPrincipals::get(principal), aGlobal) ||
      !xpc::InitGlobalObject(aCx, aGlobal, flags)) {
    return NS_ERROR_FAILURE;
  }

  MOZ_ASSERT(aNewInner->GetWrapperPreserveColor() == aGlobal);

  // Set the location information for the new global, so that tools like
  // about:memory may use that information
  xpc::SetLocationForGlobal(aGlobal, uri);

  if (!InitializeLegacyNetscapeObject(aCx, aGlobal)) {
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

nsresult nsGlobalWindowOuter::SetNewDocument(Document* aDocument,
                                             nsISupports* aState,
                                             bool aForceReuseInnerWindow,
                                             WindowGlobalChild* aActor) {
  MOZ_ASSERT(mDocumentPrincipal == nullptr,
             "mDocumentPrincipal prematurely set!");
  MOZ_ASSERT(mDocumentCookiePrincipal == nullptr,
             "mDocumentCookiePrincipal prematurely set!");
  MOZ_ASSERT(mDocumentStoragePrincipal == nullptr,
             "mDocumentStoragePrincipal prematurely set!");
  MOZ_ASSERT(mDocumentPartitionedPrincipal == nullptr,
             "mDocumentPartitionedPrincipal prematurely set!");
  MOZ_ASSERT(aDocument);

  // Bail out early if we're in process of closing down the window.
  NS_ENSURE_STATE(!mCleanedUp);

  NS_ASSERTION(!GetCurrentInnerWindow() ||
                   GetCurrentInnerWindow()->GetExtantDoc() == mDoc,
               "Uh, mDoc doesn't match the current inner window "
               "document!");
  bool wouldReuseInnerWindow = WouldReuseInnerWindow(aDocument);
  if (aForceReuseInnerWindow && !wouldReuseInnerWindow && mDoc &&
      mDoc->NodePrincipal() != aDocument->NodePrincipal()) {
    NS_ERROR("Attempted forced inner window reuse while changing principal");
    return NS_ERROR_UNEXPECTED;
  }

  if (!mBrowsingContext->AncestorsAreCurrent()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  RefPtr<Document> oldDoc = mDoc;
  MOZ_RELEASE_ASSERT(oldDoc != aDocument);

  AutoJSAPI jsapi;
  jsapi.Init();
  JSContext* cx = jsapi.cx();

  // Check if we're anywhere near the stack limit before we reach the
  // transplanting code, since it has no good way to handle errors. This uses
  // the untrusted script limit, which is not strictly necessary since no
  // actual script should run.
  js::AutoCheckRecursionLimit recursion(cx);
  if (!recursion.checkConservativeDontReport(cx)) {
    NS_WARNING("Overrecursion in SetNewDocument");
    return NS_ERROR_FAILURE;
  }

  if (!mDoc) {
    // First document load.

    // Get our private root. If it is equal to us, then we need to
    // attach our global key bindings that handles browser scrolling
    // and other browser commands.
    nsPIDOMWindowOuter* privateRoot = GetPrivateRoot();

    if (privateRoot == this) {
      RootWindowGlobalKeyListener::AttachKeyHandler(mChromeEventHandler);
    }
  }

  MaybeResetWindowName(aDocument);

  /* No mDocShell means we're already been partially closed down.  When that
     happens, setting status isn't a big requirement, so don't. (Doesn't happen
     under normal circumstances, but bug 49615 describes a case.) */

  nsContentUtils::AddScriptRunner(
      NewRunnableMethod("nsGlobalWindowOuter::ClearStatus", this,
                        &nsGlobalWindowOuter::ClearStatus));

  // Sometimes, WouldReuseInnerWindow() returns true even if there's no inner
  // window (see bug 776497). Be safe.
  bool reUseInnerWindow = (aForceReuseInnerWindow || wouldReuseInnerWindow) &&
                          GetCurrentInnerWindowInternal(this);

  nsresult rv;

  // We set mDoc even though this is an outer window to avoid
  // having to *always* reach into the inner window to find the
  // document.
  mDoc = aDocument;

  nsDocShell::Cast(mDocShell)->MaybeRestoreWindowName();

  // We drop the print request for the old document on the floor, it never made
  // it. We don't close the window here either even if we were asked to.
  mShouldDelayPrintUntilAfterLoad = true;
  mDelayedCloseForPrinting = false;
  mDelayedPrintUntilAfterLoad = false;

  // Take this opportunity to clear mSuspendedDocs. Our old inner window is now
  // responsible for unsuspending it.
  mSuspendedDocs.Clear();

#ifdef DEBUG
  mLastOpenedURI = aDocument->GetDocumentURI();
#endif

  RefPtr<nsGlobalWindowInner> currentInner =
      GetCurrentInnerWindowInternal(this);

  if (currentInner && currentInner->mNavigator) {
    currentInner->mNavigator->OnNavigation();
  }

  RefPtr<nsGlobalWindowInner> newInnerWindow;
  bool createdInnerWindow = false;

  bool thisChrome = IsChromeWindow();

  nsCOMPtr<WindowStateHolder> wsh = do_QueryInterface(aState);
  NS_ASSERTION(!aState || wsh,
               "What kind of weird state are you giving me here?");

  bool doomCurrentInner = false;

  // Only non-gray (i.e. exposed to JS) objects should be assigned to
  // newInnerGlobal.
  JS::Rooted<JSObject*> newInnerGlobal(cx);
  if (reUseInnerWindow) {
    // We're reusing the current inner window.
    NS_ASSERTION(!currentInner->IsFrozen(),
                 "We should never be reusing a shared inner window");
    newInnerWindow = currentInner;
    newInnerGlobal = currentInner->GetWrapper();

    // We're reusing the inner window, but this still counts as a navigation,
    // so all expandos and such defined on the outer window should go away.
    // Force all Xray wrappers to be recomputed.
    JS::Rooted<JSObject*> rootedObject(cx, GetWrapper());
    if (!JS_RefreshCrossCompartmentWrappers(cx, rootedObject)) {
      return NS_ERROR_FAILURE;
    }

    // Inner windows are only reused for same-origin principals, but the
    // principals don't necessarily match exactly. Update the principal on the
    // realm to match the new document. NB: We don't just call
    // currentInner->RefreshRealmPrincipals() here because we haven't yet set
    // its mDoc to aDocument.
    JS::Realm* realm = js::GetNonCCWObjectRealm(newInnerGlobal);
#ifdef DEBUG
    bool sameOrigin = false;
    nsIPrincipal* existing = nsJSPrincipals::get(JS::GetRealmPrincipals(realm));
    aDocument->NodePrincipal()->Equals(existing, &sameOrigin);
    MOZ_ASSERT(sameOrigin);
#endif
    JS::SetRealmPrincipals(realm,
                           nsJSPrincipals::get(aDocument->NodePrincipal()));
  } else {
    if (aState) {
      newInnerWindow = wsh->GetInnerWindow();
      newInnerGlobal = newInnerWindow->GetWrapper();
    } else {
      newInnerWindow = nsGlobalWindowInner::Create(this, thisChrome, aActor);
      if (StaticPrefs::dom_timeout_defer_during_load() &&
          !aDocument->NodePrincipal()->IsURIInPrefList(
              "dom.timeout.defer_during_load.force-disable")) {
        // ensure the initial loading state is known
        newInnerWindow->SetActiveLoadingState(
            aDocument->GetReadyStateEnum() ==
            Document::ReadyState::READYSTATE_LOADING);
      }

      // The outer window is automatically treated as frozen when we
      // null out the inner window. As a result, initializing classes
      // on the new inner won't end up reaching into the old inner
      // window for classes etc.
      //
      // [This happens with Object.prototype when XPConnect creates
      // a temporary global while initializing classes; the reason
      // being that xpconnect creates the temp global w/o a parent
      // and proto, which makes the JS engine look up classes in
      // cx->globalObject, i.e. this outer window].

      mInnerWindow = nullptr;

      mCreatingInnerWindow = true;

      // The SharedArrayBuffer global constructor property should not be present
      // in a fresh global object when shared memory objects aren't allowed
      // (because COOP/COEP support isn't enabled, or because COOP/COEP don't
      // act to isolate this page to a separate process).

      // Every script context we are initialized with must create a
      // new global.
      rv = CreateNativeGlobalForInner(
          cx, newInnerWindow, aDocument, &newInnerGlobal,
          ComputeIsSecureContext(aDocument),
          newInnerWindow->IsSharedMemoryAllowedInternal(
              aDocument->NodePrincipal()));
      NS_ASSERTION(
          NS_SUCCEEDED(rv) && newInnerGlobal &&
              newInnerWindow->GetWrapperPreserveColor() == newInnerGlobal,
          "Failed to get script global");

      mCreatingInnerWindow = false;
      createdInnerWindow = true;

      NS_ENSURE_SUCCESS(rv, rv);
    }

    if (currentInner && currentInner->GetWrapperPreserveColor()) {
      // Don't free objects on our current inner window if it's going to be
      // held in the bfcache.
      if (!currentInner->IsFrozen()) {
        doomCurrentInner = true;
      }
    }

    mInnerWindow = newInnerWindow;
    MOZ_ASSERT(mInnerWindow);
    mInnerWindow->TryToCacheTopInnerWindow();

    if (!GetWrapperPreserveColor()) {
      JS::Rooted<JSObject*> outer(
          cx, NewOuterWindowProxy(cx, newInnerGlobal, thisChrome));
      NS_ENSURE_TRUE(outer, NS_ERROR_FAILURE);

      mBrowsingContext->CleanUpDanglingRemoteOuterWindowProxies(cx, &outer);
      MOZ_ASSERT(js::IsWindowProxy(outer));

      js::SetProxyReservedSlot(outer, OUTER_WINDOW_SLOT,
                               JS::PrivateValue(ToSupports(this)));

      // Inform the nsJSContext, which is the canonical holder of the outer.
      mContext->SetWindowProxy(outer);

      SetWrapper(mContext->GetWindowProxy());
    } else {
      JS::Rooted<JSObject*> outerObject(
          cx, NewOuterWindowProxy(cx, newInnerGlobal, thisChrome));
      if (!outerObject) {
        NS_ERROR("out of memory");
        return NS_ERROR_FAILURE;
      }

      JS::Rooted<JSObject*> obj(cx, GetWrapper());

      MOZ_ASSERT(js::IsWindowProxy(obj));

      js::SetProxyReservedSlot(obj, OUTER_WINDOW_SLOT,
                               JS::PrivateValue(nullptr));
      js::SetProxyReservedSlot(outerObject, OUTER_WINDOW_SLOT,
                               JS::PrivateValue(nullptr));
      js::SetProxyReservedSlot(obj, HOLDER_WEAKMAP_SLOT, JS::UndefinedValue());

      outerObject = xpc::TransplantObjectNukingXrayWaiver(cx, obj, outerObject);

      if (!outerObject) {
        mBrowsingContext->ClearWindowProxy();
        NS_ERROR("unable to transplant wrappers, probably OOM");
        return NS_ERROR_FAILURE;
      }

      js::SetProxyReservedSlot(outerObject, OUTER_WINDOW_SLOT,
                               JS::PrivateValue(ToSupports(this)));

      SetWrapper(outerObject);

      MOZ_ASSERT(JS::GetNonCCWObjectGlobal(outerObject) == newInnerGlobal);

      // Inform the nsJSContext, which is the canonical holder of the outer.
      mContext->SetWindowProxy(outerObject);
    }

    // Enter the new global's realm.
    JSAutoRealm ar(cx, GetWrapperPreserveColor());

    {
      JS::Rooted<JSObject*> outer(cx, GetWrapperPreserveColor());
      js::SetWindowProxy(cx, newInnerGlobal, outer);
      mBrowsingContext->SetWindowProxy(outer);
    }

    // Set scriptability based on the state of the WindowContext.
    WindowContext* wc = mInnerWindow->GetWindowContext();
    bool allow =
        wc ? wc->CanExecuteScripts() : mBrowsingContext->CanExecuteScripts();
    xpc::Scriptability::Get(GetWrapperPreserveColor())
        .SetWindowAllowsScript(allow);

    if (!aState) {
      // Get the "window" property once so it will be cached on our inner.  We
      // have to do this here, not in binding code, because this has to happen
      // after we've created the outer window proxy and stashed it in the outer
      // nsGlobalWindowOuter, so GetWrapperPreserveColor() on that outer
      // nsGlobalWindowOuter doesn't return null and
      // nsGlobalWindowOuter::OuterObject works correctly.
      JS::Rooted<JS::Value> unused(cx);
      if (!JS_GetProperty(cx, newInnerGlobal, "window", &unused)) {
        NS_ERROR("can't create the 'window' property");
        return NS_ERROR_FAILURE;
      }

      // And same thing for the "self" property.
      if (!JS_GetProperty(cx, newInnerGlobal, "self", &unused)) {
        NS_ERROR("can't create the 'self' property");
        return NS_ERROR_FAILURE;
      }
    }
  }

  JSAutoRealm ar(cx, GetWrapperPreserveColor());

  if (!aState && !reUseInnerWindow) {
    // Loading a new page and creating a new inner window, *not*
    // restoring from session history.

    // Now that both the the inner and outer windows are initialized
    // let the script context do its magic to hook them together.
    MOZ_ASSERT(mContext->GetWindowProxy() == GetWrapperPreserveColor());
#ifdef DEBUG
    JS::Rooted<JSObject*> rootedJSObject(cx, GetWrapperPreserveColor());
    JS::Rooted<JSObject*> proto1(cx), proto2(cx);
    JS_GetPrototype(cx, rootedJSObject, &proto1);
    JS_GetPrototype(cx, newInnerGlobal, &proto2);
    NS_ASSERTION(proto1 == proto2,
                 "outer and inner globals should have the same prototype");
#endif

    mInnerWindow->SyncStateFromParentWindow();
  }

  // Add an extra ref in case we release mContext during GC.
  nsCOMPtr<nsIScriptContext> kungFuDeathGrip(mContext);

  // Make sure the inner's document is set correctly before we call
  // SetScriptGlobalObject, because that might try to examine document-dependent
  // state.  Unfortunately, we can't do some of the other clearing/resetting
  // work we do below until after SetScriptGlobalObject(), because it might
  // depend on the document having the right scope object.
  if (aState) {
    MOZ_RELEASE_ASSERT(newInnerWindow->mDoc == aDocument);
  } else {
    if (reUseInnerWindow) {
      MOZ_RELEASE_ASSERT(newInnerWindow->mDoc != aDocument);
    }
    newInnerWindow->mDoc = aDocument;
  }

  aDocument->SetScriptGlobalObject(newInnerWindow);

  MOZ_RELEASE_ASSERT(newInnerWindow->mDoc == aDocument);

  if (mBrowsingContext->IsTopContent()) {
    net::CookieJarSettings::Cast(aDocument->CookieJarSettings())
        ->SetTopLevelWindowContextId(aDocument->InnerWindowID());
  }

  newInnerWindow->RefreshReduceTimerPrecisionCallerType();

  if (!aState) {
    if (reUseInnerWindow) {
      // The StorageAccess state may have changed. Invalidate the cached
      // StorageAllowed field, so that the next call to StorageAllowedForWindow
      // recomputes it.
      newInnerWindow->ClearStorageAllowedCache();

      // The storage objects contain the URL of the window. We have to
      // recreate them when the innerWindow is reused.
      newInnerWindow->mLocalStorage = nullptr;
      newInnerWindow->mSessionStorage = nullptr;
      newInnerWindow->mPerformance = nullptr;

      // This must be called after nullifying the internal objects because
      // here we could recreate them, calling the getter methods, and store
      // them into the JS slots. If we nullify them after, the slot values and
      // the objects will be out of sync.
      newInnerWindow->ClearDocumentDependentSlots(cx);
    } else {
      newInnerWindow->InitDocumentDependentState(cx);

      // Initialize DOM classes etc on the inner window.
      JS::Rooted<JSObject*> obj(cx, newInnerGlobal);
      rv = kungFuDeathGrip->InitClasses(obj);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    // When replacing an initial about:blank document we call
    // ExecutionReady again to update the client creation URL.
    rv = newInnerWindow->ExecutionReady();
    NS_ENSURE_SUCCESS(rv, rv);

    if (mArguments) {
      newInnerWindow->DefineArgumentsProperty(mArguments);
      mArguments = nullptr;
    }

    // Give the new inner window our chrome event handler (since it
    // doesn't have one).
    newInnerWindow->mChromeEventHandler = mChromeEventHandler;
  }

  if (!aState && reUseInnerWindow) {
    // Notify our WindowGlobalChild that it has a new document. If `aState` was
    // passed, we're restoring the window from the BFCache, so the document
    // hasn't changed.
    // If we didn't have a window global child before, then initializing
    // it will have set all the required state, so we don't need to do
    // it again.
    mInnerWindow->GetWindowGlobalChild()->OnNewDocument(aDocument);
  }

  // Update the current window for our BrowsingContext.
  RefPtr<BrowsingContext> bc = GetBrowsingContext();

  if (bc->IsOwnedByProcess()) {
    MOZ_ALWAYS_SUCCEEDS(bc->SetCurrentInnerWindowId(mInnerWindow->WindowID()));
  }

  // We no longer need the old inner window.  Start its destruction if
  // its not being reused and clear our reference.
  if (doomCurrentInner) {
    currentInner->FreeInnerObjects();
  }
  currentInner = nullptr;

  // We wait to fire the debugger hook until the window is all set up and hooked
  // up with the outer. See bug 969156.
  if (createdInnerWindow) {
    nsContentUtils::AddScriptRunner(NewRunnableMethod(
        "nsGlobalWindowInner::FireOnNewGlobalObject", newInnerWindow,
        &nsGlobalWindowInner::FireOnNewGlobalObject));
  }

  if (!newInnerWindow->mHasNotifiedGlobalCreated && mDoc) {
    // We should probably notify. However if this is the, arguably bad,
    // situation when we're creating a temporary non-chrome-about-blank
    // document in a chrome docshell, don't notify just yet. Instead wait
    // until we have a real chrome doc.
    const bool isContentAboutBlankInChromeDocshell = [&] {
      if (!mDocShell) {
        return false;
      }

      RefPtr<BrowsingContext> bc = mDocShell->GetBrowsingContext();
      if (!bc || bc->GetType() != BrowsingContext::Type::Chrome) {
        return false;
      }

      return !mDoc->NodePrincipal()->IsSystemPrincipal();
    }();

    if (!isContentAboutBlankInChromeDocshell) {
      newInnerWindow->mHasNotifiedGlobalCreated = true;
      nsContentUtils::AddScriptRunner(NewRunnableMethod(
          "nsGlobalWindowOuter::DispatchDOMWindowCreated", this,
          &nsGlobalWindowOuter::DispatchDOMWindowCreated));
    }
  }

  PreloadLocalStorage();

  // Do this here rather than in say the Document constructor, since
  // we need a WindowContext available.
  mDoc->InitUseCounters();

  return NS_OK;
}

/* static */
void nsGlobalWindowOuter::PrepareForProcessChange(JSObject* aProxy) {
  JS::Rooted<JSObject*> localProxy(RootingCx(), aProxy);
  MOZ_ASSERT(js::IsWindowProxy(localProxy));

  RefPtr<nsGlobalWindowOuter> outerWindow =
      nsOuterWindowProxy::GetOuterWindow(localProxy);
  if (!outerWindow) {
    return;
  }

  AutoJSAPI jsapi;
  jsapi.Init();
  JSContext* cx = jsapi.cx();

  JSAutoRealm ar(cx, localProxy);

  // Clear out existing references from the browsing context and outer window to
  // the proxy, and from the proxy to the outer window. These references will
  // become invalid once the proxy is transplanted. Clearing the window proxy
  // from the browsing context is also necessary to indicate that it is for an
  // out of process window.
  outerWindow->ClearWrapper(localProxy);
  RefPtr<BrowsingContext> bc = outerWindow->GetBrowsingContext();
  MOZ_ASSERT(bc);
  MOZ_ASSERT(bc->GetWindowProxy() == localProxy);
  bc->ClearWindowProxy();
  js::SetProxyReservedSlot(localProxy, OUTER_WINDOW_SLOT,
                           JS::PrivateValue(nullptr));
  js::SetProxyReservedSlot(localProxy, HOLDER_WEAKMAP_SLOT,
                           JS::UndefinedValue());

  // Create a new remote outer window proxy, and transplant to it.
  JS::Rooted<JSObject*> remoteProxy(cx);

  if (!mozilla::dom::GetRemoteOuterWindowProxy(cx, bc, localProxy,
                                               &remoteProxy)) {
    MOZ_CRASH("PrepareForProcessChange GetRemoteOuterWindowProxy");
  }

  if (!xpc::TransplantObjectNukingXrayWaiver(cx, localProxy, remoteProxy)) {
    MOZ_CRASH("PrepareForProcessChange TransplantObject");
  }
}

void nsGlobalWindowOuter::PreloadLocalStorage() {
  if (!Storage::StoragePrefIsEnabled()) {
    return;
  }

  if (IsChromeWindow()) {
    return;
  }

  nsIPrincipal* principal = GetPrincipal();
  nsIPrincipal* storagePrincipal = GetEffectiveStoragePrincipal();
  if (!principal || !storagePrincipal) {
    return;
  }

  nsresult rv;

  nsCOMPtr<nsIDOMStorageManager> storageManager =
      do_GetService("@mozilla.org/dom/localStorage-manager;1", &rv);
  if (NS_FAILED(rv)) {
    return;
  }

  // private browsing windows do not persist local storage to disk so we should
  // only try to precache storage when we're not a private browsing window.
  if (!principal->GetIsInPrivateBrowsing()) {
    RefPtr<Storage> storage;
    rv = storageManager->PrecacheStorage(principal, storagePrincipal,
                                         getter_AddRefs(storage));
    if (NS_SUCCEEDED(rv)) {
      mLocalStorage = storage;
    }
  }
}

void nsGlobalWindowOuter::DispatchDOMWindowCreated() {
  if (!mDoc) {
    return;
  }

  // Fire DOMWindowCreated at chrome event listeners
  nsContentUtils::DispatchChromeEvent(mDoc, mDoc, u"DOMWindowCreated"_ns,
                                      CanBubble::eYes, Cancelable::eNo);

  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();

  // The event dispatching could possibly cause docshell destory, and
  // consequently cause mDoc to be set to nullptr by DropOuterWindowDocs(),
  // so check it again here.
  if (observerService && mDoc) {
    nsAutoString origin;
    nsIPrincipal* principal = mDoc->NodePrincipal();
    nsContentUtils::GetWebExposedOriginSerialization(principal, origin);
    observerService->NotifyObservers(static_cast<nsIDOMWindow*>(this),
                                     principal->IsSystemPrincipal()
                                         ? "chrome-document-global-created"
                                         : "content-document-global-created",
                                     origin.get());
  }
}

void nsGlobalWindowOuter::ClearStatus() { SetStatusOuter(u""_ns); }

void nsGlobalWindowOuter::SetDocShell(nsDocShell* aDocShell) {
  MOZ_ASSERT(aDocShell);

  if (aDocShell == mDocShell) {
    return;
  }

  mDocShell = aDocShell;
  mBrowsingContext = aDocShell->GetBrowsingContext();

  RefPtr<BrowsingContext> parentContext = mBrowsingContext->GetParent();

  MOZ_RELEASE_ASSERT(!parentContext ||
                     GetBrowsingContextGroup() == parentContext->Group());

  mTopLevelOuterContentWindow = mBrowsingContext->IsTopContent();

  // Get our enclosing chrome shell and retrieve its global window impl, so
  // that we can do some forwarding to the chrome document.
  RefPtr<EventTarget> chromeEventHandler;
  mDocShell->GetChromeEventHandler(getter_AddRefs(chromeEventHandler));
  mChromeEventHandler = chromeEventHandler;
  if (!mChromeEventHandler) {
    // We have no chrome event handler. If we have a parent,
    // get our chrome event handler from the parent. If
    // we don't have a parent, then we need to make a new
    // window root object that will function as a chrome event
    // handler and receive all events that occur anywhere inside
    // our window.
    nsCOMPtr<nsPIDOMWindowOuter> parentWindow = GetInProcessParent();
    if (parentWindow.get() != this) {
      mChromeEventHandler = parentWindow->GetChromeEventHandler();
    } else {
      mChromeEventHandler = NS_NewWindowRoot(this);
      mIsRootOuterWindow = true;
    }
  }

  SetIsBackgroundInternal(!mBrowsingContext->IsActive());
}

void nsGlobalWindowOuter::DetachFromDocShell(bool aIsBeingDiscarded) {
  // DetachFromDocShell means the window is being torn down. Drop our
  // reference to the script context, allowing it to be deleted
  // later. Meanwhile, keep our weak reference to the script object
  // so that it can be retrieved later (until it is finalized by the JS GC).

  // Call FreeInnerObjects on all inner windows, not just the current
  // one, since some could be held by WindowStateHolder objects that
  // are GC-owned.
  RefPtr<nsGlobalWindowInner> inner;
  for (PRCList* node = PR_LIST_HEAD(this); node != this;
       node = PR_NEXT_LINK(inner)) {
    // This cast is safe because `node != this`. Non-this nodes are inner
    // windows.
    inner = static_cast<nsGlobalWindowInner*>(node);
    MOZ_ASSERT(!inner->mOuterWindow || inner->mOuterWindow == this);
    inner->FreeInnerObjects();
  }

  // Don't report that we were detached to the nsWindowMemoryReporter, as it
  // only tracks inner windows.

  NotifyWindowIDDestroyed("outer-window-destroyed");

  nsGlobalWindowInner* currentInner = GetCurrentInnerWindowInternal(this);

  if (currentInner) {
    NS_ASSERTION(mDoc, "Must have doc!");

    // Remember the document's principal and URI.
    mDocumentPrincipal = mDoc->NodePrincipal();
    mDocumentCookiePrincipal = mDoc->EffectiveCookiePrincipal();
    mDocumentStoragePrincipal = mDoc->EffectiveStoragePrincipal();
    mDocumentPartitionedPrincipal = mDoc->PartitionedPrincipal();
    mDocumentURI = mDoc->GetDocumentURI();

    // Release our document reference
    DropOuterWindowDocs();
  }

  ClearControllers();

  mChromeEventHandler = nullptr;  // force release now

  if (mContext) {
    // When we're about to destroy a top level content window
    // (for example a tab), we trigger a full GC by passing null as the last
    // param. We also trigger a full GC for chrome windows.
    nsJSContext::PokeGC(JS::GCReason::SET_DOC_SHELL,
                        (mTopLevelOuterContentWindow || mIsChrome)
                            ? nullptr
                            : GetWrapperPreserveColor());
    mContext = nullptr;
  }

  if (aIsBeingDiscarded) {
    // If our BrowsingContext is being discarded, make a note that our current
    // inner window was active at the time it went away.
    if (nsGlobalWindowInner* currentInner =
            GetCurrentInnerWindowInternal(this)) {
      currentInner->SetWasCurrentInnerWindow();
    }
  }

  mDocShell = nullptr;
  mBrowsingContext->ClearDocShell();

  CleanUp();
}

void nsGlobalWindowOuter::UpdateParentTarget() {
  // NOTE: This method is nearly identical to
  // nsGlobalWindowInner::UpdateParentTarget(). IF YOU UPDATE THIS METHOD,
  // UPDATE THE OTHER ONE TOO!  The one difference is that this method updates
  // mMessageManager as well, which inner windows don't have.

  // Try to get our frame element's tab child global (its in-process message
  // manager).  If that fails, fall back to the chrome event handler's tab
  // child global, and if it doesn't have one, just use the chrome event
  // handler itself.

  nsCOMPtr<Element> frameElement = GetFrameElementInternal();
  mMessageManager = nsContentUtils::TryGetBrowserChildGlobal(frameElement);

  if (!mMessageManager) {
    nsGlobalWindowOuter* topWin = GetInProcessScriptableTopInternal();
    if (topWin) {
      frameElement = topWin->GetFrameElementInternal();
      mMessageManager = nsContentUtils::TryGetBrowserChildGlobal(frameElement);
    }
  }

  if (!mMessageManager) {
    mMessageManager =
        nsContentUtils::TryGetBrowserChildGlobal(mChromeEventHandler);
  }

  if (mMessageManager) {
    mParentTarget = mMessageManager;
  } else {
    mParentTarget = mChromeEventHandler;
  }
}

EventTarget* nsGlobalWindowOuter::GetTargetForEventTargetChain() {
  return GetCurrentInnerWindowInternal(this);
}

void nsGlobalWindowOuter::GetEventTargetParent(EventChainPreVisitor& aVisitor) {
  MOZ_CRASH("The outer window should not be part of an event path");
}

bool nsGlobalWindowOuter::ShouldPromptToBlockDialogs() {
  if (!nsContentUtils::GetCurrentJSContext()) {
    return false;  // non-scripted caller.
  }

  BrowsingContextGroup* group = GetBrowsingContextGroup();
  if (!group) {
    return true;
  }

  return group->DialogsAreBeingAbused();
}

bool nsGlobalWindowOuter::AreDialogsEnabled() {
  BrowsingContextGroup* group = mBrowsingContext->Group();
  if (!group) {
    NS_ERROR("AreDialogsEnabled() called without a browsing context group?");
    return false;
  }

  // Dialogs are blocked if the content viewer is hidden
  if (mDocShell) {
    nsCOMPtr<nsIDocumentViewer> viewer;
    mDocShell->GetDocViewer(getter_AddRefs(viewer));

    bool isHidden;
    viewer->GetIsHidden(&isHidden);
    if (isHidden) {
      return false;
    }
  }

  // Dialogs are also blocked if the document is sandboxed with SANDBOXED_MODALS
  // (or if we have no document, of course).  Which document?  Who knows; the
  // spec is daft.  See <https://github.com/whatwg/html/issues/1206>.  For now
  // just go ahead and check mDoc, since in everything except edge cases in
  // which a frame is allow-same-origin but not allow-scripts and is being poked
  // at by some other window this should be the right thing anyway.
  if (!mDoc || (mDoc->GetSandboxFlags() & SANDBOXED_MODALS)) {
    return false;
  }

  return group->GetAreDialogsEnabled();
}

void nsGlobalWindowOuter::DisableDialogs() {
  BrowsingContextGroup* group = mBrowsingContext->Group();
  if (!group) {
    NS_ERROR("DisableDialogs() called without a browsing context group?");
    return;
  }

  if (group) {
    group->SetAreDialogsEnabled(false);
  }
}

void nsGlobalWindowOuter::EnableDialogs() {
  BrowsingContextGroup* group = mBrowsingContext->Group();
  if (!group) {
    NS_ERROR("EnableDialogs() called without a browsing context group?");
    return;
  }

  if (group) {
    group->SetAreDialogsEnabled(true);
  }
}

nsresult nsGlobalWindowOuter::PostHandleEvent(EventChainPostVisitor& aVisitor) {
  MOZ_CRASH("The outer window should not be part of an event path");
}

void nsGlobalWindowOuter::PoisonOuterWindowProxy(JSObject* aObject) {
  if (aObject == GetWrapperMaybeDead()) {
    PoisonWrapper();
  }
}

nsresult nsGlobalWindowOuter::SetArguments(nsIArray* aArguments) {
  nsresult rv;

  // We've now mostly separated them, but the difference is still opaque to
  // nsWindowWatcher (the caller of SetArguments in this little back-and-forth
  // embedding waltz we do here).
  //
  // So we need to demultiplex the two cases here.
  nsGlobalWindowInner* currentInner = GetCurrentInnerWindowInternal(this);

  mArguments = aArguments;
  rv = currentInner->DefineArgumentsProperty(aArguments);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

//*****************************************************************************
// nsGlobalWindowOuter::nsIScriptObjectPrincipal
//*****************************************************************************

nsIPrincipal* nsGlobalWindowOuter::GetPrincipal() {
  if (mDoc) {
    // If we have a document, get the principal from the document
    return mDoc->NodePrincipal();
  }

  if (mDocumentPrincipal) {
    return mDocumentPrincipal;
  }

  // If we don't have a principal and we don't have a document we
  // ask the parent window for the principal. This can happen when
  // loading a frameset that has a <frame src="javascript:xxx">, in
  // that case the global window is used in JS before we've loaded
  // a document into the window.

  nsCOMPtr<nsIScriptObjectPrincipal> objPrincipal =
      do_QueryInterface(GetInProcessParentInternal());

  if (objPrincipal) {
    return objPrincipal->GetPrincipal();
  }

  return nullptr;
}

nsIPrincipal* nsGlobalWindowOuter::GetEffectiveCookiePrincipal() {
  if (mDoc) {
    // If we have a document, get the principal from the document
    return mDoc->EffectiveCookiePrincipal();
  }

  if (mDocumentCookiePrincipal) {
    return mDocumentCookiePrincipal;
  }

  // If we don't have a cookie principal and we don't have a document we ask
  // the parent window for the cookie principal.

  nsCOMPtr<nsIScriptObjectPrincipal> objPrincipal =
      do_QueryInterface(GetInProcessParentInternal());

  if (objPrincipal) {
    return objPrincipal->GetEffectiveCookiePrincipal();
  }

  return nullptr;
}

nsIPrincipal* nsGlobalWindowOuter::GetEffectiveStoragePrincipal() {
  if (mDoc) {
    // If we have a document, get the principal from the document
    return mDoc->EffectiveStoragePrincipal();
  }

  if (mDocumentStoragePrincipal) {
    return mDocumentStoragePrincipal;
  }

  // If we don't have a storage principal and we don't have a document we ask
  // the parent window for the storage principal.

  nsCOMPtr<nsIScriptObjectPrincipal> objPrincipal =
      do_QueryInterface(GetInProcessParentInternal());

  if (objPrincipal) {
    return objPrincipal->GetEffectiveStoragePrincipal();
  }

  return nullptr;
}

nsIPrincipal* nsGlobalWindowOuter::PartitionedPrincipal() {
  if (mDoc) {
    // If we have a document, get the principal from the document
    return mDoc->PartitionedPrincipal();
  }

  if (mDocumentPartitionedPrincipal) {
    return mDocumentPartitionedPrincipal;
  }

  // If we don't have a partitioned principal and we don't have a document we
  // ask the parent window for the partitioned principal.

  nsCOMPtr<nsIScriptObjectPrincipal> objPrincipal =
      do_QueryInterface(GetInProcessParentInternal());

  if (objPrincipal) {
    return objPrincipal->PartitionedPrincipal();
  }

  return nullptr;
}

//*****************************************************************************
// nsGlobalWindowOuter::nsIDOMWindow
//*****************************************************************************

Element* nsPIDOMWindowOuter::GetFrameElementInternal() const {
  return mFrameElement;
}

void nsPIDOMWindowOuter::SetFrameElementInternal(Element* aFrameElement) {
  mFrameElement = aFrameElement;
}

Navigator* nsGlobalWindowOuter::GetNavigator() {
  FORWARD_TO_INNER(Navigator, (), nullptr);
}

nsScreen* nsGlobalWindowOuter::GetScreen() {
  FORWARD_TO_INNER(Screen, (), nullptr);
}

void nsPIDOMWindowOuter::ActivateMediaComponents() {
  if (!ShouldDelayMediaFromStart()) {
    return;
  }
  MOZ_LOG(AudioChannelService::GetAudioChannelLog(), LogLevel::Debug,
          ("nsPIDOMWindowOuter, ActiveMediaComponents, "
           "no longer to delay media from start, this = %p\n",
           this));
  if (BrowsingContext* bc = GetBrowsingContext()) {
    Unused << bc->Top()->SetShouldDelayMediaFromStart(false);
  }
  NotifyResumingDelayedMedia();
}

bool nsPIDOMWindowOuter::ShouldDelayMediaFromStart() const {
  BrowsingContext* bc = GetBrowsingContext();
  return bc && bc->Top()->GetShouldDelayMediaFromStart();
}

void nsPIDOMWindowOuter::NotifyResumingDelayedMedia() {
  RefPtr<AudioChannelService> service = AudioChannelService::GetOrCreate();
  if (service) {
    service->NotifyResumingDelayedMedia(this);
  }
}

bool nsPIDOMWindowOuter::GetAudioMuted() const {
  BrowsingContext* bc = GetBrowsingContext();
  return bc && bc->Top()->GetMuted();
}

void nsPIDOMWindowOuter::RefreshMediaElementsVolume() {
  RefPtr<AudioChannelService> service = AudioChannelService::GetOrCreate();
  if (service) {
    // TODO: RefreshAgentsVolume can probably be simplified further.
    service->RefreshAgentsVolume(this, 1.0f, GetAudioMuted());
  }
}

mozilla::dom::BrowsingContextGroup*
nsPIDOMWindowOuter::GetBrowsingContextGroup() const {
  return mBrowsingContext ? mBrowsingContext->Group() : nullptr;
}

Nullable<WindowProxyHolder> nsGlobalWindowOuter::GetParentOuter() {
  BrowsingContext* bc = GetBrowsingContext();
  return bc ? bc->GetParent(IgnoreErrors()) : nullptr;
}

/**
 * GetInProcessScriptableParent used to be called when a script read
 * window.parent. Under Fission, that is now handled by
 * BrowsingContext::GetParent, and the result is a WindowProxyHolder rather than
 * an actual global window. This method still exists for legacy callers which
 * relied on the old logic, and require in-process windows. However, it only
 * works correctly when no out-of-process frames exist between this window and
 * the top-level window, so it should not be used in new code.
 *
 * In contrast to GetRealParent, GetInProcessScriptableParent respects <iframe
 * mozbrowser> boundaries, so if |this| is contained by an <iframe
 * mozbrowser>, we will return |this| as its own parent.
 */
nsPIDOMWindowOuter* nsGlobalWindowOuter::GetInProcessScriptableParent() {
  if (!mDocShell) {
    return nullptr;
  }

  if (BrowsingContext* parentBC = GetBrowsingContext()->GetParent()) {
    if (nsCOMPtr<nsPIDOMWindowOuter> parent = parentBC->GetDOMWindow()) {
      return parent;
    }
  }
  return this;
}

/**
 * Behavies identically to GetInProcessScriptableParent extept that it returns
 * null if GetInProcessScriptableParent would return this window.
 */
nsPIDOMWindowOuter* nsGlobalWindowOuter::GetInProcessScriptableParentOrNull() {
  nsPIDOMWindowOuter* parent = GetInProcessScriptableParent();
  return (nsGlobalWindowOuter::Cast(parent) == this) ? nullptr : parent;
}

already_AddRefed<nsPIDOMWindowOuter> nsGlobalWindowOuter::GetInProcessParent() {
  if (!mDocShell) {
    return nullptr;
  }

  if (auto* parentBC = GetBrowsingContext()->GetParent()) {
    if (auto* parent = parentBC->GetDOMWindow()) {
      return do_AddRef(parent);
    }
  }
  return do_AddRef(this);
}

static nsresult GetTopImpl(nsGlobalWindowOuter* aWin, nsIURI* aURIBeingLoaded,
                           nsPIDOMWindowOuter** aTop, bool aScriptable,
                           bool aExcludingExtensionAccessibleContentFrames) {
  *aTop = nullptr;

  MOZ_ASSERT_IF(aExcludingExtensionAccessibleContentFrames, !aScriptable);

  // Walk up the parent chain.

  nsCOMPtr<nsPIDOMWindowOuter> prevParent = aWin;
  nsCOMPtr<nsPIDOMWindowOuter> parent = aWin;
  do {
    if (!parent) {
      break;
    }

    prevParent = parent;

    if (aScriptable) {
      parent = parent->GetInProcessScriptableParent();
    } else {
      parent = parent->GetInProcessParent();
    }

    if (aExcludingExtensionAccessibleContentFrames) {
      if (auto* p = nsGlobalWindowOuter::Cast(parent)) {
        nsGlobalWindowInner* currentInner = GetCurrentInnerWindowInternal(p);
        nsIURI* uri = prevParent->GetDocumentURI();
        if (!uri) {
          // If our parent doesn't have a URI yet, we have a document that is in
          // the process of being loaded.  In that case, our caller is
          // responsible for passing in the URI for the document that is being
          // loaded, so we fall back to using that URI here.
          uri = aURIBeingLoaded;
        }

        if (currentInner && uri) {
          // If we find an inner window, we better find the uri for the current
          // window we're looking at.  If we can't find it directly, it is the
          // responsibility of our caller to provide it to us.
          MOZ_DIAGNOSTIC_ASSERT(uri);

          // If the new parent has permission to load the current page, we're
          // at a moz-extension:// frame which has a host permission that allows
          // it to load the document that we've loaded.  In that case, stop at
          // this frame and consider it the top-level frame.
          //
          // Note that it's possible for the set of URIs accepted by
          // AddonAllowsLoad() to change at runtime, but we don't need to cache
          // the result of this check, since the important consumer of this code
          // (which is nsIHttpChannelInternal.topWindowURI) already caches the
          // result after computing it the first time.
          if (BasePrincipal::Cast(p->GetPrincipal())
                  ->AddonAllowsLoad(uri, true)) {
            parent = prevParent;
            break;
          }
        }
      }
    }

  } while (parent != prevParent);

  if (parent) {
    parent.swap(*aTop);
  }

  return NS_OK;
}

/**
 * GetInProcessScriptableTop used to be called when a script read window.top.
 * Under Fission, that is now handled by BrowsingContext::Top, and the result is
 * a WindowProxyHolder rather than an actual global window. This method still
 * exists for legacy callers which relied on the old logic, and require
 * in-process windows. However, it only works correctly when no out-of-process
 * frames exist between this window and the top-level window, so it should not
 * be used in new code.
 *
 * In contrast to GetRealTop, GetInProcessScriptableTop respects <iframe
 * mozbrowser> boundaries.  If we encounter a window owned by an <iframe
 * mozbrowser> while walking up the window hierarchy, we'll stop and return that
 * window.
 */
nsPIDOMWindowOuter* nsGlobalWindowOuter::GetInProcessScriptableTop() {
  nsCOMPtr<nsPIDOMWindowOuter> window;
  GetTopImpl(this, /* aURIBeingLoaded = */ nullptr, getter_AddRefs(window),
             /* aScriptable = */ true,
             /* aExcludingExtensionAccessibleContentFrames = */ false);
  return window.get();
}

already_AddRefed<nsPIDOMWindowOuter> nsGlobalWindowOuter::GetInProcessTop() {
  nsCOMPtr<nsPIDOMWindowOuter> window;
  GetTopImpl(this, /* aURIBeingLoaded = */ nullptr, getter_AddRefs(window),
             /* aScriptable = */ false,
             /* aExcludingExtensionAccessibleContentFrames = */ false);
  return window.forget();
}

already_AddRefed<nsPIDOMWindowOuter>
nsGlobalWindowOuter::GetTopExcludingExtensionAccessibleContentFrames(
    nsIURI* aURIBeingLoaded) {
  // There is a parent-process equivalent of this in DocumentLoadListener.cpp
  // GetTopWindowExcludingExtensionAccessibleContentFrames
  nsCOMPtr<nsPIDOMWindowOuter> window;
  GetTopImpl(this, aURIBeingLoaded, getter_AddRefs(window),
             /* aScriptable = */ false,
             /* aExcludingExtensionAccessibleContentFrames = */ true);
  return window.forget();
}

void nsGlobalWindowOuter::GetContentOuter(JSContext* aCx,
                                          JS::MutableHandle<JSObject*> aRetval,
                                          CallerType aCallerType,
                                          ErrorResult& aError) {
  RefPtr<BrowsingContext> content = GetContentInternal(aCallerType, aError);
  if (aError.Failed()) {
    return;
  }

  if (!content) {
    aRetval.set(nullptr);
    return;
  }

  JS::Rooted<JS::Value> val(aCx);
  if (!ToJSValue(aCx, WindowProxyHolder{content}, &val)) {
    aError.Throw(NS_ERROR_UNEXPECTED);
    return;
  }

  MOZ_ASSERT(val.isObjectOrNull());
  aRetval.set(val.toObjectOrNull());
}

already_AddRefed<BrowsingContext> nsGlobalWindowOuter::GetContentInternal(
    CallerType aCallerType, ErrorResult& aError) {
  // First check for a named frame named "content"
  if (RefPtr<BrowsingContext> named = GetChildWindow(u"content"_ns)) {
    return named.forget();
  }

  // If we're in the parent process, and being called by system code, `content`
  // should return the current primary content frame (if it's in-process).
  //
  // We return `nullptr` if the current primary content frame is out-of-process,
  // rather than a remote window proxy, as that is the existing behaviour as of
  // bug 1597437.
  if (XRE_IsParentProcess() && aCallerType == CallerType::System) {
    nsCOMPtr<nsIDocShellTreeOwner> treeOwner = GetTreeOwner();
    if (!treeOwner) {
      aError.Throw(NS_ERROR_FAILURE);
      return nullptr;
    }

    nsCOMPtr<nsIDocShellTreeItem> primaryContent;
    treeOwner->GetPrimaryContentShell(getter_AddRefs(primaryContent));
    if (!primaryContent) {
      return nullptr;
    }

    return do_AddRef(primaryContent->GetBrowsingContext());
  }

  // For legacy untrusted callers we always return the same value as
  // `window.top`
  if (mDoc && aCallerType != CallerType::System) {
    mDoc->WarnOnceAbout(DeprecatedOperations::eWindowContentUntrusted);
  }

  MOZ_ASSERT(mBrowsingContext->IsContent());
  return do_AddRef(mBrowsingContext->Top());
}

nsresult nsGlobalWindowOuter::GetPrompter(nsIPrompt** aPrompt) {
  if (!mDocShell) return NS_ERROR_FAILURE;

  nsCOMPtr<nsIPrompt> prompter(do_GetInterface(mDocShell));
  NS_ENSURE_TRUE(prompter, NS_ERROR_NO_INTERFACE);

  prompter.forget(aPrompt);
  return NS_OK;
}

bool nsGlobalWindowOuter::GetClosedOuter() {
  // If someone called close(), or if we don't have a docshell, we're closed.
  return mIsClosed || !mDocShell;
}

bool nsGlobalWindowOuter::Closed() { return GetClosedOuter(); }

Nullable<WindowProxyHolder> nsGlobalWindowOuter::IndexedGetterOuter(
    uint32_t aIndex) {
  BrowsingContext* bc = GetBrowsingContext();
  NS_ENSURE_TRUE(bc, nullptr);

  BrowsingContext* child = bc->NonSyntheticLightDOMChildAt(aIndex);

  if (child) {
    return WindowProxyHolder(child);
  }
  return nullptr;
}

nsIControllers* nsGlobalWindowOuter::GetControllersOuter(ErrorResult& aError) {
  if (!mControllers) {
    mControllers = new nsXULControllers();
    if (!mControllers) {
      aError.Throw(NS_ERROR_FAILURE);
      return nullptr;
    }

    // Add in the default controller
    RefPtr<nsBaseCommandController> commandController =
        nsBaseCommandController::CreateWindowController();
    if (!commandController) {
      aError.Throw(NS_ERROR_FAILURE);
      return nullptr;
    }

    mControllers->InsertControllerAt(0, commandController);
    commandController->SetCommandContext(static_cast<nsIDOMWindow*>(this));
  }

  return mControllers;
}

nsresult nsGlobalWindowOuter::GetControllers(nsIControllers** aResult) {
  FORWARD_TO_INNER(GetControllers, (aResult), NS_ERROR_UNEXPECTED);
}

already_AddRefed<BrowsingContext>
nsGlobalWindowOuter::GetOpenerBrowsingContext() {
  RefPtr<BrowsingContext> opener = GetBrowsingContext()->GetOpener();
  MOZ_DIAGNOSTIC_ASSERT(!opener ||
                        opener->Group() == GetBrowsingContext()->Group());
  if (!opener || opener->Group() != GetBrowsingContext()->Group()) {
    return nullptr;
  }

  // Catch the case where we're chrome but the opener is not...
  if (nsContentUtils::LegacyIsCallerChromeOrNativeCode() &&
      GetPrincipal() == nsContentUtils::GetSystemPrincipal()) {
    auto* openerWin = nsGlobalWindowOuter::Cast(opener->GetDOMWindow());
    if (!openerWin ||
        openerWin->GetPrincipal() != nsContentUtils::GetSystemPrincipal()) {
      return nullptr;
    }
  }

  return opener.forget();
}

nsPIDOMWindowOuter* nsGlobalWindowOuter::GetSameProcessOpener() {
  if (RefPtr<BrowsingContext> opener = GetOpenerBrowsingContext()) {
    return opener->GetDOMWindow();
  }
  return nullptr;
}

Nullable<WindowProxyHolder> nsGlobalWindowOuter::GetOpenerWindowOuter() {
  if (RefPtr<BrowsingContext> opener = GetOpenerBrowsingContext()) {
    return WindowProxyHolder(std::move(opener));
  }
  return nullptr;
}

Nullable<WindowProxyHolder> nsGlobalWindowOuter::GetOpener() {
  return GetOpenerWindowOuter();
}

void nsGlobalWindowOuter::GetStatusOuter(nsAString& aStatus) {
  aStatus = mStatus;
}

void nsGlobalWindowOuter::SetStatusOuter(const nsAString& aStatus) {
  mStatus = aStatus;

  // We don't support displaying window.status in the UI, so there's nothing
  // left to do here.
}

void nsGlobalWindowOuter::GetNameOuter(nsAString& aName) {
  if (mDocShell) {
    mDocShell->GetName(aName);
  }
}

void nsGlobalWindowOuter::SetNameOuter(const nsAString& aName,
                                       mozilla::ErrorResult& aError) {
  if (mDocShell) {
    aError = mDocShell->SetName(aName);
  }
}

// NOTE: The idea of this function is that it should return the same as
// nsPresContext::CSSToDeviceScale() if it was in aWindow synchronously. For
// that, we use the UnscaledDevicePixelsPerCSSPixel() (which contains the device
// scale and the OS zoom scale) and then account for the browsing context full
// zoom. See the declaration of this function for context about why this is
// needed.
CSSToLayoutDeviceScale nsGlobalWindowOuter::CSSToDevScaleForBaseWindow(
    nsIBaseWindow* aWindow) {
  MOZ_ASSERT(aWindow);
  auto scale = aWindow->UnscaledDevicePixelsPerCSSPixel();
  if (mBrowsingContext) {
    scale.scale *= mBrowsingContext->FullZoom();
  }
  return scale;
}

nsresult nsGlobalWindowOuter::GetInnerSize(CSSSize& aSize) {
  EnsureSizeAndPositionUpToDate();

  NS_ENSURE_STATE(mDocShell);

  RefPtr<nsPresContext> presContext = mDocShell->GetPresContext();
  PresShell* presShell = mDocShell->GetPresShell();

  if (!presContext || !presShell) {
    aSize = {};
    return NS_OK;
  }

  // Whether or not the css viewport has been overridden, we can get the
  // correct value by looking at the visible area of the presContext.
  if (RefPtr<nsViewManager> viewManager = presShell->GetViewManager()) {
    viewManager->FlushDelayedResize();
  }

  // FIXME: Bug 1598487 - Return the layout viewport instead of the ICB.
  nsSize viewportSize = presContext->GetVisibleArea().Size();
  if (presContext->GetDynamicToolbarState() == DynamicToolbarState::Collapsed) {
    viewportSize =
        nsLayoutUtils::ExpandHeightForViewportUnits(presContext, viewportSize);
  }

  aSize = CSSPixel::FromAppUnits(viewportSize);

  switch (StaticPrefs::dom_innerSize_rounding()) {
    case 1:
      aSize.width = std::roundf(aSize.width);
      aSize.height = std::roundf(aSize.height);
      break;
    case 2:
      aSize.width = std::truncf(aSize.width);
      aSize.height = std::truncf(aSize.height);
      break;
    default:
      break;
  }

  return NS_OK;
}

double nsGlobalWindowOuter::GetInnerWidthOuter(ErrorResult& aError) {
  CSSSize size;
  aError = GetInnerSize(size);
  return size.width;
}

nsresult nsGlobalWindowOuter::GetInnerWidth(double* aInnerWidth) {
  FORWARD_TO_INNER(GetInnerWidth, (aInnerWidth), NS_ERROR_UNEXPECTED);
}

double nsGlobalWindowOuter::GetInnerHeightOuter(ErrorResult& aError) {
  CSSSize size;
  aError = GetInnerSize(size);
  return size.height;
}

nsresult nsGlobalWindowOuter::GetInnerHeight(double* aInnerHeight) {
  FORWARD_TO_INNER(GetInnerHeight, (aInnerHeight), NS_ERROR_UNEXPECTED);
}

CSSIntSize nsGlobalWindowOuter::GetOuterSize(CallerType aCallerType,
                                             ErrorResult& aError) {
  if (nsIGlobalObject::ShouldResistFingerprinting(aCallerType,
                                                  RFPTarget::WindowOuterSize)) {
    if (BrowsingContext* bc = GetBrowsingContext()) {
      return bc->Top()->GetTopInnerSizeForRFP();
    }
    return {};
  }

  // Windows showing documents in RDM panes and any subframes within them
  // return the simulated device size.
  if (mDoc) {
    Maybe<CSSIntSize> deviceSize = GetRDMDeviceSize(*mDoc);
    if (deviceSize.isSome()) {
      return *deviceSize;
    }
  }

  nsCOMPtr<nsIBaseWindow> treeOwnerAsWin = GetTreeOwnerWindow();
  if (!treeOwnerAsWin) {
    aError.Throw(NS_ERROR_FAILURE);
    return {};
  }

  return RoundedToInt(treeOwnerAsWin->GetSize() /
                      CSSToDevScaleForBaseWindow(treeOwnerAsWin));
}

int32_t nsGlobalWindowOuter::GetOuterWidthOuter(CallerType aCallerType,
                                                ErrorResult& aError) {
  return GetOuterSize(aCallerType, aError).width;
}

int32_t nsGlobalWindowOuter::GetOuterHeightOuter(CallerType aCallerType,
                                                 ErrorResult& aError) {
  return GetOuterSize(aCallerType, aError).height;
}

CSSPoint nsGlobalWindowOuter::ScreenEdgeSlop() {
  if (NS_WARN_IF(!mDocShell)) {
    return {};
  }
  RefPtr<nsPresContext> pc = mDocShell->GetPresContext();
  if (NS_WARN_IF(!pc)) {
    return {};
  }
  nsCOMPtr<nsIWidget> widget = GetMainWidget();
  if (NS_WARN_IF(!widget)) {
    return {};
  }
  LayoutDeviceIntPoint pt = widget->GetScreenEdgeSlop();
  auto auPoint =
      LayoutDeviceIntPoint::ToAppUnits(pt, pc->AppUnitsPerDevPixel());
  return CSSPoint::FromAppUnits(auPoint);
}

CSSIntPoint nsGlobalWindowOuter::GetScreenXY(CallerType aCallerType,
                                             ErrorResult& aError) {
  // When resisting fingerprinting, always return (0,0)
  if (nsIGlobalObject::ShouldResistFingerprinting(aCallerType,
                                                  RFPTarget::WindowScreenXY)) {
    return CSSIntPoint(0, 0);
  }

  nsCOMPtr<nsIBaseWindow> treeOwnerAsWin = GetTreeOwnerWindow();
  if (!treeOwnerAsWin) {
    aError.Throw(NS_ERROR_FAILURE);
    return CSSIntPoint(0, 0);
  }

  LayoutDeviceIntPoint windowPos;
  aError = treeOwnerAsWin->GetPosition(&windowPos.x.value, &windowPos.y.value);

  RefPtr<nsPresContext> presContext = mDocShell->GetPresContext();
  if (!presContext) {
    // XXX Fishy LayoutDevice to CSS conversion?
    return CSSIntPoint(windowPos.x, windowPos.y);
  }

  nsDeviceContext* context = presContext->DeviceContext();
  auto windowPosAppUnits = LayoutDeviceIntPoint::ToAppUnits(
      windowPos, context->AppUnitsPerDevPixel());
  return CSSIntPoint::FromAppUnitsRounded(windowPosAppUnits);
}

int32_t nsGlobalWindowOuter::GetScreenXOuter(CallerType aCallerType,
                                             ErrorResult& aError) {
  return GetScreenXY(aCallerType, aError).x;
}

nsRect nsGlobalWindowOuter::GetInnerScreenRect() {
  if (!mDocShell) {
    return nsRect();
  }

  EnsureSizeAndPositionUpToDate();

  if (!mDocShell) {
    return nsRect();
  }

  PresShell* presShell = mDocShell->GetPresShell();
  if (!presShell) {
    return nsRect();
  }
  nsIFrame* rootFrame = presShell->GetRootFrame();
  if (!rootFrame) {
    return nsRect();
  }

  return rootFrame->GetScreenRectInAppUnits();
}

Maybe<CSSIntSize> nsGlobalWindowOuter::GetRDMDeviceSize(
    const Document& aDocument) {
  // RDM device size should reflect the simulated device resolution, and
  // be independent of any full zoom or resolution zoom applied to the
  // content. To get this value, we get the "unscaled" browser child size,
  // and divide by the full zoom. "Unscaled" in this case means unscaled
  // from device to screen but it has been affected (multiplied) by the
  // full zoom and we need to compensate for that.
  MOZ_RELEASE_ASSERT(NS_IsMainThread());

  // Bug 1576256: This does not work for cross-process subframes.
  const Document* topInProcessContentDoc =
      aDocument.GetTopLevelContentDocumentIfSameProcess();
  BrowsingContext* bc = topInProcessContentDoc
                            ? topInProcessContentDoc->GetBrowsingContext()
                            : nullptr;
  if (bc && bc->InRDMPane()) {
    nsIDocShell* docShell = topInProcessContentDoc->GetDocShell();
    if (docShell) {
      nsPresContext* presContext = docShell->GetPresContext();
      if (presContext) {
        nsCOMPtr<nsIBrowserChild> child = docShell->GetBrowserChild();
        if (child) {
          // We intentionally use GetFullZoom here instead of
          // GetDeviceFullZoom, because the unscaledInnerSize is based
          // on the full zoom and not the device full zoom (which is
          // rounded to result in integer device pixels).
          float zoom = presContext->GetFullZoom();
          BrowserChild* bc = static_cast<BrowserChild*>(child.get());
          CSSSize unscaledSize = bc->GetUnscaledInnerSize();
          return Some(CSSIntSize(gfx::RoundedToInt(unscaledSize / zoom)));
        }
      }
    }
  }
  return Nothing();
}

float nsGlobalWindowOuter::GetMozInnerScreenXOuter(CallerType aCallerType) {
  // When resisting fingerprinting, always return 0.
  if (nsIGlobalObject::ShouldResistFingerprinting(
          aCallerType, RFPTarget::WindowInnerScreenXY)) {
    return 0.0;
  }

  nsRect r = GetInnerScreenRect();
  return nsPresContext::AppUnitsToFloatCSSPixels(r.x);
}

float nsGlobalWindowOuter::GetMozInnerScreenYOuter(CallerType aCallerType) {
  // Return 0 to prevent fingerprinting.
  if (nsIGlobalObject::ShouldResistFingerprinting(
          aCallerType, RFPTarget::WindowInnerScreenXY)) {
    return 0.0;
  }

  nsRect r = GetInnerScreenRect();
  return nsPresContext::AppUnitsToFloatCSSPixels(r.y);
}

int32_t nsGlobalWindowOuter::GetScreenYOuter(CallerType aCallerType,
                                             ErrorResult& aError) {
  return GetScreenXY(aCallerType, aError).y;
}

// NOTE: Arguments to this function should have values scaled to
// CSS pixels, not device pixels.
void nsGlobalWindowOuter::CheckSecurityWidthAndHeight(int32_t* aWidth,
                                                      int32_t* aHeight,
                                                      CallerType aCallerType) {
  if (aCallerType != CallerType::System) {
    // if attempting to resize the window, hide any open popups
    nsContentUtils::HidePopupsInDocument(mDoc);
  }

  // This one is easy. Just ensure the variable is greater than 100;
  if ((aWidth && *aWidth < 100) || (aHeight && *aHeight < 100)) {
    // Check security state for use in determing window dimensions

    if (aCallerType != CallerType::System) {
      // sec check failed
      if (aWidth && *aWidth < 100) {
        *aWidth = 100;
      }
      if (aHeight && *aHeight < 100) {
        *aHeight = 100;
      }
    }
  }
}

// NOTE: Arguments to this function should have values in app units
void nsGlobalWindowOuter::SetCSSViewportWidthAndHeight(nscoord aInnerWidth,
                                                       nscoord aInnerHeight) {
  RefPtr<nsPresContext> presContext = mDocShell->GetPresContext();

  nsRect shellArea = presContext->GetVisibleArea();
  shellArea.SetHeight(aInnerHeight);
  shellArea.SetWidth(aInnerWidth);

  // FIXME(emilio): This doesn't seem to be ok, this doesn't reflow or
  // anything... Should go through PresShell::ResizeReflow.
  //
  // But I don't think this can be reached by content, as we don't allow to set
  // inner{Width,Height}.
  presContext->SetVisibleArea(shellArea);
}

// NOTE: Arguments to this function should have values scaled to
// CSS pixels, not device pixels.
void nsGlobalWindowOuter::CheckSecurityLeftAndTop(int32_t* aLeft, int32_t* aTop,
                                                  CallerType aCallerType) {
  // This one is harder. We have to get the screen size and window dimensions.

  // Check security state for use in determing window dimensions

  if (aCallerType != CallerType::System) {
    // if attempting to move the window, hide any open popups
    nsContentUtils::HidePopupsInDocument(mDoc);

    if (nsGlobalWindowOuter* rootWindow =
            nsGlobalWindowOuter::Cast(GetPrivateRoot())) {
      rootWindow->FlushPendingNotifications(FlushType::Layout);
    }

    nsCOMPtr<nsIBaseWindow> treeOwnerAsWin = GetTreeOwnerWindow();

    RefPtr<nsScreen> screen = GetScreen();

    if (treeOwnerAsWin && screen) {
      CSSToLayoutDeviceScale scale = CSSToDevScaleForBaseWindow(treeOwnerAsWin);
      CSSIntRect winRect =
          CSSIntRect::Round(treeOwnerAsWin->GetPositionAndSize() / scale);

      // Get the screen dimensions
      // XXX This should use nsIScreenManager once it's fully fleshed out.
      int32_t screenLeft = screen->AvailLeft();
      int32_t screenWidth = screen->AvailWidth();
      int32_t screenHeight = screen->AvailHeight();
#if defined(XP_MACOSX)
      /* The mac's coordinate system is different from the assumed Windows'
         system. It offsets by the height of the menubar so that a window
         placed at (0,0) will be entirely visible. Unfortunately that
         correction is made elsewhere (in Widget) and the meaning of
         the Avail... coordinates is overloaded. Here we allow a window
         to be placed at (0,0) because it does make sense to do so.
      */
      int32_t screenTop = screen->Top();
#else
      int32_t screenTop = screen->AvailTop();
#endif

      if (aLeft) {
        if (screenLeft + screenWidth < *aLeft + winRect.width)
          *aLeft = screenLeft + screenWidth - winRect.width;
        if (screenLeft > *aLeft) *aLeft = screenLeft;
      }
      if (aTop) {
        if (screenTop + screenHeight < *aTop + winRect.height)
          *aTop = screenTop + screenHeight - winRect.height;
        if (screenTop > *aTop) *aTop = screenTop;
      }
    } else {
      if (aLeft) *aLeft = 0;
      if (aTop) *aTop = 0;
    }
  }
}

int32_t nsGlobalWindowOuter::GetScrollBoundaryOuter(Side aSide) {
  FlushPendingNotifications(FlushType::Layout);
  if (ScrollContainerFrame* sf = GetScrollContainerFrame()) {
    return nsPresContext::AppUnitsToIntCSSPixels(
        sf->GetScrollRange().Edge(aSide));
  }
  return 0;
}

CSSPoint nsGlobalWindowOuter::GetScrollXY(bool aDoFlush) {
  if (aDoFlush) {
    FlushPendingNotifications(FlushType::Layout);
  } else {
    EnsureSizeAndPositionUpToDate();
  }

  ScrollContainerFrame* sf = GetScrollContainerFrame();
  if (!sf) {
    return CSSIntPoint(0, 0);
  }

  nsPoint scrollPos = sf->GetScrollPosition();
  if (scrollPos != nsPoint(0, 0) && !aDoFlush) {
    // Oh, well.  This is the expensive case -- the window is scrolled and we
    // didn't actually flush yet.  Repeat, but with a flush, since the content
    // may get shorter and hence our scroll position may decrease.
    return GetScrollXY(true);
  }

  return CSSPoint::FromAppUnits(scrollPos);
}

double nsGlobalWindowOuter::GetScrollXOuter() { return GetScrollXY(false).x; }

double nsGlobalWindowOuter::GetScrollYOuter() { return GetScrollXY(false).y; }

uint32_t nsGlobalWindowOuter::Length() {
  BrowsingContext* bc = GetBrowsingContext();
  NS_ENSURE_TRUE(bc, 0);
  return bc->NonSyntheticLightDOMChildrenCount();
}

Nullable<WindowProxyHolder> nsGlobalWindowOuter::GetTopOuter() {
  BrowsingContext* bc = GetBrowsingContext();
  return bc ? bc->GetTop(IgnoreErrors()) : nullptr;
}

already_AddRefed<BrowsingContext> nsGlobalWindowOuter::GetChildWindow(
    const nsAString& aName) {
  NS_ENSURE_TRUE(mBrowsingContext, nullptr);
  NS_ENSURE_TRUE(mInnerWindow, nullptr);
  NS_ENSURE_TRUE(mInnerWindow->GetWindowGlobalChild(), nullptr);

  return do_AddRef(mBrowsingContext->FindChildWithName(
      aName, *mInnerWindow->GetWindowGlobalChild()));
}

bool nsGlobalWindowOuter::DispatchCustomEvent(
    const nsAString& aEventName, ChromeOnlyDispatch aChromeOnlyDispatch) {
  bool defaultActionEnabled = true;

  if (aChromeOnlyDispatch == ChromeOnlyDispatch::eYes) {
    nsContentUtils::DispatchEventOnlyToChrome(mDoc, this, aEventName,
                                              CanBubble::eYes, Cancelable::eYes,
                                              &defaultActionEnabled);
  } else {
    nsContentUtils::DispatchTrustedEvent(mDoc, this, aEventName,
                                         CanBubble::eYes, Cancelable::eYes,
                                         &defaultActionEnabled);
  }

  return defaultActionEnabled;
}

bool nsGlobalWindowOuter::WindowExists(const nsAString& aName,
                                       bool aForceNoOpener,
                                       bool aLookForCallerOnJSStack) {
  MOZ_ASSERT(mDocShell, "Must have docshell");

  if (aForceNoOpener) {
    return aName.LowerCaseEqualsLiteral("_self") ||
           aName.LowerCaseEqualsLiteral("_top") ||
           aName.LowerCaseEqualsLiteral("_parent");
  }

  if (WindowGlobalChild* wgc = mInnerWindow->GetWindowGlobalChild()) {
    return wgc->FindBrowsingContextWithName(aName, aLookForCallerOnJSStack);
  }
  return false;
}

already_AddRefed<nsIWidget> nsGlobalWindowOuter::GetMainWidget() {
  nsCOMPtr<nsIBaseWindow> treeOwnerAsWin = GetTreeOwnerWindow();

  nsCOMPtr<nsIWidget> widget;

  if (treeOwnerAsWin) {
    treeOwnerAsWin->GetMainWidget(getter_AddRefs(widget));
  }

  return widget.forget();
}

nsIWidget* nsGlobalWindowOuter::GetNearestWidget() const {
  nsIDocShell* docShell = GetDocShell();
  if (!docShell) {
    return nullptr;
  }
  PresShell* presShell = docShell->GetPresShell();
  if (!presShell) {
    return nullptr;
  }
  nsIFrame* rootFrame = presShell->GetRootFrame();
  if (!rootFrame) {
    return nullptr;
  }
  return rootFrame->GetView()->GetNearestWidget(nullptr);
}

void nsGlobalWindowOuter::SetFullscreenOuter(bool aFullscreen,
                                             mozilla::ErrorResult& aError) {
  aError =
      SetFullscreenInternal(FullscreenReason::ForFullscreenMode, aFullscreen);
}

nsresult nsGlobalWindowOuter::SetFullScreen(bool aFullscreen) {
  return SetFullscreenInternal(FullscreenReason::ForFullscreenMode,
                               aFullscreen);
}

static void FinishDOMFullscreenChange(Document* aDoc, bool aInDOMFullscreen) {
  if (aInDOMFullscreen) {
    // Ask the document to handle any pending DOM fullscreen change.
    if (!Document::HandlePendingFullscreenRequests(aDoc)) {
      // If we don't end up having anything in fullscreen,
      // async request exiting fullscreen.
      Document::AsyncExitFullscreen(aDoc);
    }
  } else {
    // If the window is leaving fullscreen state, also ask the document
    // to exit from DOM Fullscreen.
    Document::ExitFullscreenInDocTree(aDoc);
  }
}

struct FullscreenTransitionDuration {
  // The unit of the durations is millisecond
  uint16_t mFadeIn = 0;
  uint16_t mFadeOut = 0;
  bool IsSuppressed() const { return mFadeIn == 0 && mFadeOut == 0; }
};

static void GetFullscreenTransitionDuration(
    bool aEnterFullscreen, FullscreenTransitionDuration* aDuration) {
  const char* pref = aEnterFullscreen
                         ? "full-screen-api.transition-duration.enter"
                         : "full-screen-api.transition-duration.leave";
  nsAutoCString prefValue;
  Preferences::GetCString(pref, prefValue);
  if (!prefValue.IsEmpty()) {
    sscanf(prefValue.get(), "%hu%hu", &aDuration->mFadeIn,
           &aDuration->mFadeOut);
  }
}

class FullscreenTransitionTask : public Runnable {
 public:
  FullscreenTransitionTask(const FullscreenTransitionDuration& aDuration,
                           nsGlobalWindowOuter* aWindow, bool aFullscreen,
                           nsIWidget* aWidget, nsISupports* aTransitionData)
      : mozilla::Runnable("FullscreenTransitionTask"),
        mWindow(aWindow),
        mWidget(aWidget),
        mTransitionData(aTransitionData),
        mDuration(aDuration),
        mStage(eBeforeToggle),
        mFullscreen(aFullscreen) {}

  NS_IMETHOD Run() override;

 private:
  ~FullscreenTransitionTask() override = default;

  /**
   * The flow of fullscreen transition:
   *
   *         parent process         |         child process
   * ----------------------------------------------------------------
   *
   *                                    | request/exit fullscreen
   *                              <-----|
   *         BeforeToggle stage |
   *                            |
   *  ToggleFullscreen stage *1 |----->
   *                                    | HandleFullscreenRequests
   *                                    |
   *                              <-----| MozAfterPaint event
   *       AfterToggle stage *2 |
   *                            |
   *                  End stage |
   *
   * Note we also start a timer at *1 so that if we don't get MozAfterPaint
   * from the child process in time, we continue going to *2.
   */
  enum Stage {
    // BeforeToggle stage happens before we enter or leave fullscreen
    // state. In this stage, the task triggers the pre-toggle fullscreen
    // transition on the widget.
    eBeforeToggle,
    // ToggleFullscreen stage actually executes the fullscreen toggle,
    // and wait for the next paint on the content to continue.
    eToggleFullscreen,
    // AfterToggle stage happens after we toggle the fullscreen state.
    // In this stage, the task triggers the post-toggle fullscreen
    // transition on the widget.
    eAfterToggle,
    // End stage is triggered after the final transition finishes.
    eEnd
  };

  class Observer final : public nsIObserver, public nsINamed {
   public:
    NS_DECL_ISUPPORTS
    NS_DECL_NSIOBSERVER
    NS_DECL_NSINAMED

    explicit Observer(FullscreenTransitionTask* aTask) : mTask(aTask) {}

   private:
    ~Observer() = default;

    RefPtr<FullscreenTransitionTask> mTask;
  };

  static const char* const kPaintedTopic;

  RefPtr<nsGlobalWindowOuter> mWindow;
  nsCOMPtr<nsIWidget> mWidget;
  nsCOMPtr<nsITimer> mTimer;
  nsCOMPtr<nsISupports> mTransitionData;

  TimeStamp mFullscreenChangeStartTime;
  FullscreenTransitionDuration mDuration;
  Stage mStage;
  bool mFullscreen;
};

const char* const FullscreenTransitionTask::kPaintedTopic =
    "fullscreen-painted";

NS_IMETHODIMP
FullscreenTransitionTask::Run() {
  Stage stage = mStage;
  mStage = Stage(mStage + 1);
  if (MOZ_UNLIKELY(mWidget->Destroyed())) {
    // If the widget has been destroyed before we get here, don't try to
    // do anything more. Just let it go and release ourselves.
    NS_WARNING("The widget to fullscreen has been destroyed");
    mWindow->mIsInFullScreenTransition = false;
    return NS_OK;
  }
  if (stage == eBeforeToggle) {
    PROFILER_MARKER_UNTYPED("Fullscreen transition start", DOM);

    mWindow->mIsInFullScreenTransition = true;

    nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
    NS_ENSURE_TRUE(obs, NS_ERROR_FAILURE);
    obs->NotifyObservers(nullptr, "fullscreen-transition-start", nullptr);

    mWidget->PerformFullscreenTransition(nsIWidget::eBeforeFullscreenToggle,
                                         mDuration.mFadeIn, mTransitionData,
                                         this);
  } else if (stage == eToggleFullscreen) {
    PROFILER_MARKER_UNTYPED("Fullscreen toggle start", DOM);
    mFullscreenChangeStartTime = TimeStamp::Now();
    // Toggle the fullscreen state on the widget
    if (!mWindow->SetWidgetFullscreen(FullscreenReason::ForFullscreenAPI,
                                      mFullscreen, mWidget)) {
      // Fail to setup the widget, call FinishFullscreenChange to
      // complete fullscreen change directly.
      mWindow->FinishFullscreenChange(mFullscreen);
    }
    // Set observer for the next content paint.
    nsCOMPtr<nsIObserver> observer = new Observer(this);
    nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
    obs->AddObserver(observer, kPaintedTopic, false);
    // There are several edge cases where we may never get the paint
    // notification, including:
    // 1. the window/tab is closed before the next paint;
    // 2. the user has switched to another tab before we get here.
    // Completely fixing those cases seems to be tricky, and since they
    // should rarely happen, it probably isn't worth to fix. Hence we
    // simply add a timeout here to ensure we never hang forever.
    // In addition, if the page is complicated or the machine is less
    // powerful, layout could take a long time, in which case, staying
    // in black screen for that long could hurt user experience even
    // more than exposing an intermediate state.
    uint32_t timeout =
        Preferences::GetUint("full-screen-api.transition.timeout", 1000);
    NS_NewTimerWithObserver(getter_AddRefs(mTimer), observer, timeout,
                            nsITimer::TYPE_ONE_SHOT);
  } else if (stage == eAfterToggle) {
    glean::dom::fullscreen_transition_black.AccumulateRawDuration(
        TimeStamp::Now() - mFullscreenChangeStartTime);
    mWidget->PerformFullscreenTransition(nsIWidget::eAfterFullscreenToggle,
                                         mDuration.mFadeOut, mTransitionData,
                                         this);
  } else if (stage == eEnd) {
    PROFILER_MARKER_UNTYPED("Fullscreen transition end", DOM);

    mWindow->mIsInFullScreenTransition = false;

    nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
    NS_ENSURE_TRUE(obs, NS_ERROR_FAILURE);
    obs->NotifyObservers(nullptr, "fullscreen-transition-end", nullptr);

    mWidget->CleanupFullscreenTransition();
  }
  return NS_OK;
}

NS_IMPL_ISUPPORTS(FullscreenTransitionTask::Observer, nsIObserver, nsINamed)

NS_IMETHODIMP
FullscreenTransitionTask::Observer::Observe(nsISupports* aSubject,
                                            const char* aTopic,
                                            const char16_t* aData) {
  bool shouldContinue = false;
  if (strcmp(aTopic, FullscreenTransitionTask::kPaintedTopic) == 0) {
    nsCOMPtr<nsPIDOMWindowInner> win(do_QueryInterface(aSubject));
    nsCOMPtr<nsIWidget> widget =
        win ? nsGlobalWindowInner::Cast(win)->GetMainWidget() : nullptr;
    if (widget == mTask->mWidget) {
      // The paint notification arrives first. Cancel the timer.
      mTask->mTimer->Cancel();
      shouldContinue = true;
      PROFILER_MARKER_UNTYPED("Fullscreen toggle end", DOM);
    }
  } else {
#ifdef DEBUG
    MOZ_ASSERT(strcmp(aTopic, NS_TIMER_CALLBACK_TOPIC) == 0,
               "Should only get fullscreen-painted or timer-callback");
    nsCOMPtr<nsITimer> timer(do_QueryInterface(aSubject));
    MOZ_ASSERT(timer && timer == mTask->mTimer,
               "Should only trigger this with the timer the task created");
#endif
    shouldContinue = true;
    PROFILER_MARKER_UNTYPED("Fullscreen toggle timeout", DOM);
  }
  if (shouldContinue) {
    nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
    obs->RemoveObserver(this, kPaintedTopic);
    mTask->mTimer = nullptr;
    mTask->Run();
  }
  return NS_OK;
}

NS_IMETHODIMP
FullscreenTransitionTask::Observer::GetName(nsACString& aName) {
  aName.AssignLiteral("FullscreenTransitionTask");
  return NS_OK;
}

static bool MakeWidgetFullscreen(nsGlobalWindowOuter* aWindow,
                                 FullscreenReason aReason, bool aFullscreen) {
  nsCOMPtr<nsIWidget> widget = aWindow->GetMainWidget();
  if (!widget) {
    return false;
  }

  FullscreenTransitionDuration duration;
  bool performTransition = false;
  nsCOMPtr<nsISupports> transitionData;
  if (aReason == FullscreenReason::ForFullscreenAPI) {
    GetFullscreenTransitionDuration(aFullscreen, &duration);
    if (!duration.IsSuppressed()) {
      performTransition = widget->PrepareForFullscreenTransition(
          getter_AddRefs(transitionData));
    }
  }

  if (!performTransition) {
    return aWindow->SetWidgetFullscreen(aReason, aFullscreen, widget);
  }

  nsCOMPtr<nsIRunnable> task = new FullscreenTransitionTask(
      duration, aWindow, aFullscreen, widget, transitionData);
  task->Run();
  return true;
}

nsresult nsGlobalWindowOuter::ProcessWidgetFullscreenRequest(
    FullscreenReason aReason, bool aFullscreen) {
  mInProcessFullscreenRequest.emplace(aReason, aFullscreen);

  // Prevent chrome documents which are still loading from resizing
  // the window after we set fullscreen mode.
  nsCOMPtr<nsIBaseWindow> treeOwnerAsWin = GetTreeOwnerWindow();
  nsCOMPtr<nsIAppWindow> appWin(do_GetInterface(treeOwnerAsWin));
  if (aFullscreen && appWin) {
    appWin->SetIntrinsicallySized(false);
  }

  // Sometimes we don't want the top-level widget to actually go fullscreen:
  // - in the B2G desktop client, we don't want the emulated screen dimensions
  //   to appear to increase when entering fullscreen mode; we just want the
  //   content to fill the entire client area of the emulator window.
  // - in FxR Desktop, we don't want fullscreen to take over the monitor, but
  //   instead we want fullscreen to fill the FxR window in the the headset.
  if (!StaticPrefs::full_screen_api_ignore_widgets() &&
      !mForceFullScreenInWidget) {
    if (MakeWidgetFullscreen(this, aReason, aFullscreen)) {
      // The rest of code for switching fullscreen is in nsGlobalWindowOuter::
      // FinishFullscreenChange() which will be called after sizemodechange
      // event is dispatched.
      return NS_OK;
    }
  }

#if defined(NIGHTLY_BUILD) && defined(XP_WIN)
  if (FxRWindowManager::GetInstance()->IsFxRWindow(mWindowID)) {
    mozilla::gfx::VRShMem shmem(nullptr, true /*aRequiresMutex*/);
    shmem.SendFullscreenState(mWindowID, aFullscreen);
  }
#endif  // NIGHTLY_BUILD && XP_WIN
  FinishFullscreenChange(aFullscreen);
  return NS_OK;
}

nsresult nsGlobalWindowOuter::SetFullscreenInternal(FullscreenReason aReason,
                                                    bool aFullscreen) {
  MOZ_ASSERT(nsContentUtils::IsSafeToRunScript(),
             "Requires safe to run script as it "
             "may call FinishDOMFullscreenChange");

  NS_ENSURE_TRUE(mDocShell, NS_ERROR_FAILURE);

  MOZ_ASSERT(
      aReason != FullscreenReason::ForForceExitFullscreen || !aFullscreen,
      "FullscreenReason::ForForceExitFullscreen can "
      "only be used with exiting fullscreen");

  // Only chrome can change our fullscreen mode. Otherwise, the state
  // can only be changed for DOM fullscreen.
  if (aReason == FullscreenReason::ForFullscreenMode &&
      !nsContentUtils::LegacyIsCallerChromeOrNativeCode()) {
    return NS_OK;
  }

  // SetFullscreen needs to be called on the root window, so get that
  // via the DocShell tree, and if we are not already the root,
  // call SetFullscreen on that window instead.
  nsCOMPtr<nsIDocShellTreeItem> rootItem;
  mDocShell->GetInProcessRootTreeItem(getter_AddRefs(rootItem));
  nsCOMPtr<nsPIDOMWindowOuter> window =
      rootItem ? rootItem->GetWindow() : nullptr;
  if (!window) return NS_ERROR_FAILURE;
  if (rootItem != mDocShell)
    return window->SetFullscreenInternal(aReason, aFullscreen);

  // make sure we don't try to set full screen on a non-chrome window,
  // which might happen in embedding world
  if (mDocShell->ItemType() != nsIDocShellTreeItem::typeChrome)
    return NS_ERROR_FAILURE;

  // FullscreenReason::ForForceExitFullscreen can only be used with exiting
  // fullscreen
  MOZ_ASSERT_IF(
      mFullscreen.isSome(),
      mFullscreen.value() != FullscreenReason::ForForceExitFullscreen);

  // If we are already in full screen mode, just return, we don't care about the
  // reason here, because,
  // - If we are in fullscreen mode due to browser fullscreen mode, requesting
  //   DOM fullscreen does not change anything.
  // - If we are in fullscreen mode due to DOM fullscreen, requesting browser
  //   fullscreen should not change anything, either. Note that we should not
  //   update reason to ForFullscreenMode, otherwise the subsequent DOM
  //   fullscreen exit will be ignored and user will be confused. And ideally
  //   this should never happen as `window.fullscreen` returns `true` for DOM
  //   fullscreen as well.
  if (mFullscreen.isSome() == aFullscreen) {
    // How come we get browser fullscreen request while we are already in DOM
    // fullscreen?
    MOZ_ASSERT_IF(aFullscreen && aReason == FullscreenReason::ForFullscreenMode,
                  mFullscreen.value() != FullscreenReason::ForFullscreenAPI);
    return NS_OK;
  }

  // Note that although entering DOM fullscreen could also cause
  // consequential calls to this method, those calls will be skipped
  // at the condition above.
  if (aReason == FullscreenReason::ForFullscreenMode) {
    if (!aFullscreen && mFullscreen &&
        mFullscreen.value() == FullscreenReason::ForFullscreenAPI) {
      // If we are exiting fullscreen mode, but we actually didn't
      // entered browser fullscreen mode, the fullscreen state was only for
      // the Fullscreen API. Change the reason here so that we can
      // perform transition for it.
      aReason = FullscreenReason::ForFullscreenAPI;
    }
  } else {
    // If we are exiting from DOM fullscreen while we initially make
    // the window fullscreen because of browser fullscreen mode, don't restore
    // the window. But we still need to exit the DOM fullscreen state.
    if (!aFullscreen && mFullscreen &&
        mFullscreen.value() == FullscreenReason::ForFullscreenMode) {
      // If there is a in-process fullscreen request, FinishDOMFullscreenChange
      // will be called when the request is finished.
      if (!mInProcessFullscreenRequest.isSome()) {
        FinishDOMFullscreenChange(mDoc, false);
      }
      return NS_OK;
    }
  }

  // Set this before so if widget sends an event indicating its
  // gone full screen, the state trap above works.
  if (aFullscreen) {
    mFullscreen.emplace(aReason);
  } else {
    mFullscreen.reset();
  }

  // If we are in process of fullscreen request, only keep the latest fullscreen
  // state, we will sync up later while the processing request is finished.
  if (mInProcessFullscreenRequest.isSome()) {
    mFullscreenHasChangedDuringProcessing = true;
    return NS_OK;
  }

  return ProcessWidgetFullscreenRequest(aReason, aFullscreen);
}

// Support a per-window, dynamic equivalent of enabling
// full-screen-api.ignore-widgets
void nsGlobalWindowOuter::ForceFullScreenInWidget() {
  MOZ_DIAGNOSTIC_ASSERT(XRE_IsParentProcess());

  mForceFullScreenInWidget = true;
}

bool nsGlobalWindowOuter::SetWidgetFullscreen(FullscreenReason aReason,
                                              bool aIsFullscreen,
                                              nsIWidget* aWidget) {
  MOZ_ASSERT(this == GetInProcessTopInternal(),
             "Only topmost window should call this");
  MOZ_ASSERT(!GetFrameElementInternal(), "Content window should not call this");
  MOZ_ASSERT(XRE_GetProcessType() == GeckoProcessType_Default);

  if (!NS_WARN_IF(!IsChromeWindow())) {
    if (!NS_WARN_IF(mChromeFields.mFullscreenPresShell)) {
      if (PresShell* presShell = mDocShell->GetPresShell()) {
        if (nsRefreshDriver* rd = presShell->GetRefreshDriver()) {
          mChromeFields.mFullscreenPresShell = do_GetWeakReference(presShell);
          MOZ_ASSERT(mChromeFields.mFullscreenPresShell);
          rd->SetIsResizeSuppressed();
          rd->Freeze();
        }
      }
    }
  }
  nsresult rv = aReason == FullscreenReason::ForFullscreenMode
                    ?
                    // If we enter fullscreen for fullscreen mode, we want
                    // the native system behavior.
                    aWidget->MakeFullScreenWithNativeTransition(aIsFullscreen)
                    : aWidget->MakeFullScreen(aIsFullscreen);
  return NS_SUCCEEDED(rv);
}

/* virtual */
void nsGlobalWindowOuter::FullscreenWillChange(bool aIsFullscreen) {
  if (!mInProcessFullscreenRequest.isSome()) {
    // If there is no in-process fullscreen request, the fullscreen state change
    // is triggered from the OS directly, e.g. user use built-in window button
    // to enter/exit fullscreen on macOS.
    mInProcessFullscreenRequest.emplace(FullscreenReason::ForFullscreenMode,
                                        aIsFullscreen);
    if (mFullscreen.isSome() != aIsFullscreen) {
      if (aIsFullscreen) {
        mFullscreen.emplace(FullscreenReason::ForFullscreenMode);
      } else {
        mFullscreen.reset();
      }
    } else {
      // It is possible that FullscreenWillChange is notified with current
      // fullscreen state, e.g. browser goes into fullscreen when widget
      // fullscreen is prevented, and then user triggers fullscreen from the OS
      // directly again.
      MOZ_ASSERT(StaticPrefs::full_screen_api_ignore_widgets() ||
                     mForceFullScreenInWidget,
                 "This should only happen when widget fullscreen is prevented");
    }
  }
  if (aIsFullscreen) {
    DispatchCustomEvent(u"willenterfullscreen"_ns, ChromeOnlyDispatch::eYes);
  } else {
    DispatchCustomEvent(u"willexitfullscreen"_ns, ChromeOnlyDispatch::eYes);
  }
}

/* virtual */
void nsGlobalWindowOuter::FinishFullscreenChange(bool aIsFullscreen) {
  mozilla::Maybe<FullscreenRequest> currentInProcessRequest =
      std::move(mInProcessFullscreenRequest);
  if (!mFullscreenHasChangedDuringProcessing &&
      aIsFullscreen != mFullscreen.isSome()) {
    NS_WARNING("Failed to toggle fullscreen state of the widget");
    // We failed to make the widget enter fullscreen.
    // Stop further changes and restore the state.
    if (!aIsFullscreen) {
      mFullscreen.reset();
    } else {
#ifndef XP_MACOSX
      MOZ_ASSERT_UNREACHABLE("Failed to exit fullscreen?");
#endif
      // Restore fullscreen state with FullscreenReason::ForFullscreenAPI reason
      // in order to make subsequent DOM fullscreen exit request can exit
      // browser fullscreen mode.
      mFullscreen.emplace(FullscreenReason::ForFullscreenAPI);
    }
    return;
  }

  // Note that we must call this to toggle the DOM fullscreen state
  // of the document before dispatching the "fullscreen" event, so
  // that the chrome can distinguish between browser fullscreen mode
  // and DOM fullscreen.
  FinishDOMFullscreenChange(mDoc, aIsFullscreen);

  // dispatch a "fullscreen" DOM event so that XUL apps can
  // respond visually if we are kicked into full screen mode
  DispatchCustomEvent(u"fullscreen"_ns, ChromeOnlyDispatch::eYes);

  if (!NS_WARN_IF(!IsChromeWindow())) {
    if (RefPtr<PresShell> presShell =
            do_QueryReferent(mChromeFields.mFullscreenPresShell)) {
      if (nsRefreshDriver* rd = presShell->GetRefreshDriver()) {
        rd->Thaw();
      }
      mChromeFields.mFullscreenPresShell = nullptr;
    }
  }

  // If fullscreen state has changed during processing fullscreen request, we
  // need to ensure widget matches our latest fullscreen state here.
  if (mFullscreenHasChangedDuringProcessing) {
    mFullscreenHasChangedDuringProcessing = false;
    // Widget doesn't care about the reason that makes it entering/exiting
    // fullscreen, so here we just need to ensure the fullscreen state is
    // matched.
    if (aIsFullscreen != mFullscreen.isSome()) {
      // If we end up need to exit fullscreen, use the same reason that brings
      // us into fullscreen mode, so that we will perform the same fullscreen
      // transistion effect for exiting.
      ProcessWidgetFullscreenRequest(
          mFullscreen.isSome() ? mFullscreen.value()
                               : currentInProcessRequest.value().mReason,
          mFullscreen.isSome());
    }
  }
}

/* virtual */
void nsGlobalWindowOuter::MacFullscreenMenubarOverlapChanged(
    mozilla::DesktopCoord aOverlapAmount) {
  ErrorResult res;
  RefPtr<Event> domEvent =
      mDoc->CreateEvent(u"CustomEvent"_ns, CallerType::System, res);
  if (res.Failed()) {
    return;
  }

  AutoJSAPI jsapi;
  jsapi.Init();
  JSContext* cx = jsapi.cx();
  JSAutoRealm ar(cx, GetWrapperPreserveColor());

  JS::Rooted<JS::Value> detailValue(cx);
  if (!ToJSValue(cx, aOverlapAmount, &detailValue)) {
    return;
  }

  CustomEvent* customEvent = static_cast<CustomEvent*>(domEvent.get());
  customEvent->InitCustomEvent(cx, u"MacFullscreenMenubarRevealUpdate"_ns,
                               /* aCanBubble = */ true,
                               /* aCancelable = */ true, detailValue);
  domEvent->SetTrusted(true);
  domEvent->WidgetEventPtr()->mFlags.mOnlyChromeDispatch = true;

  nsCOMPtr<EventTarget> target = this;
  domEvent->SetTarget(target);

  target->DispatchEvent(*domEvent, CallerType::System, IgnoreErrors());
}

bool nsGlobalWindowOuter::Fullscreen() const {
  NS_ENSURE_TRUE(mDocShell, mFullscreen.isSome());

  // Get the fullscreen value of the root window, to always have the value
  // accurate, even when called from content.
  nsCOMPtr<nsIDocShellTreeItem> rootItem;
  mDocShell->GetInProcessRootTreeItem(getter_AddRefs(rootItem));
  if (rootItem == mDocShell) {
    if (!XRE_IsContentProcess()) {
      // We are the root window. Return our internal value.
      return mFullscreen.isSome();
    }
    if (nsCOMPtr<nsIWidget> widget = GetNearestWidget()) {
      // We are in content process, figure out the value from
      // the sizemode of the puppet widget.
      return widget->SizeMode() == nsSizeMode_Fullscreen;
    }
    return false;
  }

  nsCOMPtr<nsPIDOMWindowOuter> window = rootItem->GetWindow();
  NS_ENSURE_TRUE(window, mFullscreen.isSome());

  return nsGlobalWindowOuter::Cast(window)->Fullscreen();
}

bool nsGlobalWindowOuter::GetFullscreenOuter() { return Fullscreen(); }

bool nsGlobalWindowOuter::GetFullScreen() {
  FORWARD_TO_INNER(GetFullScreen, (), false);
}

void nsGlobalWindowOuter::EnsureReflowFlushAndPaint() {
  NS_ASSERTION(mDocShell,
               "EnsureReflowFlushAndPaint() called with no "
               "docshell!");

  if (!mDocShell) return;

  RefPtr<PresShell> presShell = mDocShell->GetPresShell();
  if (!presShell) {
    return;
  }

  // Flush pending reflows.
  if (mDoc) {
    mDoc->FlushPendingNotifications(FlushType::Layout);
  }

  // Unsuppress painting.
  presShell->UnsuppressPainting();
}

// static
void nsGlobalWindowOuter::MakeMessageWithPrincipal(
    nsAString& aOutMessage, nsIPrincipal* aSubjectPrincipal, bool aUseHostPort,
    const char* aNullMessage, const char* aContentMessage,
    const char* aFallbackMessage) {
  MOZ_ASSERT(aSubjectPrincipal);

  aOutMessage.Truncate();

  // Try to get a host from the running principal -- this will do the
  // right thing for javascript: and data: documents.

  nsAutoCString contentDesc;

  if (aSubjectPrincipal->GetIsNullPrincipal()) {
    nsContentUtils::GetLocalizedString(
        nsContentUtils::eCOMMON_DIALOG_PROPERTIES, aNullMessage, aOutMessage);
  } else {
    auto* addonPolicy = BasePrincipal::Cast(aSubjectPrincipal)->AddonPolicy();
    if (addonPolicy) {
      nsContentUtils::FormatLocalizedString(
          aOutMessage, nsContentUtils::eCOMMON_DIALOG_PROPERTIES,
          aContentMessage, addonPolicy->Name());
    } else {
      nsresult rv = NS_ERROR_FAILURE;
      if (aUseHostPort) {
        nsCOMPtr<nsIURI> uri = aSubjectPrincipal->GetURI();
        if (uri) {
          rv = uri->GetDisplayHostPort(contentDesc);
        }
      }
      if (!aUseHostPort || NS_FAILED(rv)) {
        rv = aSubjectPrincipal->GetExposablePrePath(contentDesc);
      }
      if (NS_SUCCEEDED(rv) && !contentDesc.IsEmpty()) {
        NS_ConvertUTF8toUTF16 ucsPrePath(contentDesc);
        nsContentUtils::FormatLocalizedString(
            aOutMessage, nsContentUtils::eCOMMON_DIALOG_PROPERTIES,
            aContentMessage, ucsPrePath);
      }
    }
  }

  if (aOutMessage.IsEmpty()) {
    // We didn't find a host so use the generic heading
    nsContentUtils::GetLocalizedString(
        nsContentUtils::eCOMMON_DIALOG_PROPERTIES, aFallbackMessage,
        aOutMessage);
  }

  // Just in case
  if (aOutMessage.IsEmpty()) {
    NS_WARNING(
        "could not get ScriptDlgGenericHeading string from string bundle");
    aOutMessage.AssignLiteral("[Script]");
  }
}

bool nsGlobalWindowOuter::CanMoveResizeWindows(CallerType aCallerType) {
  // When called from chrome, we can avoid the following checks.
  if (aCallerType != CallerType::System) {
    // Don't allow scripts to move or resize windows that were not opened by a
    // script.
    if (!mBrowsingContext->GetTopLevelCreatedByWebContent()) {
      return false;
    }

    if (!CanSetProperty("dom.disable_window_move_resize")) {
      return false;
    }

    // Ignore the request if we have more than one tab in the window.
    if (mBrowsingContext->Top()->HasSiblings()) {
      return false;
    }
  }

  if (mDocShell) {
    bool allow;
    nsresult rv = mDocShell->GetAllowWindowControl(&allow);
    if (NS_SUCCEEDED(rv) && !allow) return false;
  }

  if (nsGlobalWindowInner::sMouseDown &&
      !nsGlobalWindowInner::sDragServiceDisabled) {
    nsCOMPtr<nsIDragService> ds =
        do_GetService("@mozilla.org/widget/dragservice;1");
    if (ds) {
      nsGlobalWindowInner::sDragServiceDisabled = true;
      ds->Suppress();
    }
  }
  return true;
}

bool nsGlobalWindowOuter::AlertOrConfirm(bool aAlert, const nsAString& aMessage,
                                         nsIPrincipal& aSubjectPrincipal,
                                         ErrorResult& aError) {
  // XXX This method is very similar to nsGlobalWindowOuter::Prompt, make
  // sure any modifications here don't need to happen over there!
  if (!AreDialogsEnabled()) {
    // Just silently return.  In the case of alert(), the return value is
    // ignored.  In the case of confirm(), returning false is the same thing as
    // would happen if the user cancels.
    return false;
  }

  // Reset popup state while opening a modal dialog, and firing events
  // about the dialog, to prevent the current state from being active
  // the whole time a modal dialog is open.
  AutoPopupStatePusher popupStatePusher(PopupBlocker::openAbused, true);

  // Before bringing up the window, unsuppress painting and flush
  // pending reflows.
  EnsureReflowFlushAndPaint();

  nsAutoString title;
  MakeMessageWithPrincipal(title, &aSubjectPrincipal, false,
                           "ScriptDlgNullPrincipalHeading", "ScriptDlgHeading",
                           "ScriptDlgGenericHeading");

  // Remove non-terminating null characters from the
  // string. See bug #310037.
  nsAutoString final;
  nsContentUtils::StripNullChars(aMessage, final);
  nsContentUtils::PlatformToDOMLineBreaks(final);

  nsresult rv;
  nsCOMPtr<nsIPromptFactory> promptFac =
      do_GetService("@mozilla.org/prompter;1", &rv);
  if (NS_FAILED(rv)) {
    aError.Throw(rv);
    return false;
  }

  nsCOMPtr<nsIPrompt> prompt;
  aError =
      promptFac->GetPrompt(this, NS_GET_IID(nsIPrompt), getter_AddRefs(prompt));
  if (aError.Failed()) {
    return false;
  }

  // Always allow content modal prompts for alert and confirm.
  if (nsCOMPtr<nsIWritablePropertyBag2> promptBag = do_QueryInterface(prompt)) {
    promptBag->SetPropertyAsUint32(u"modalType"_ns,
                                   nsIPrompt::MODAL_TYPE_CONTENT);
  }

  bool result = false;
  nsAutoSyncOperation sync(mDoc, SyncOperationBehavior::eSuspendInput);
  if (ShouldPromptToBlockDialogs()) {
    bool disallowDialog = false;
    nsAutoString label;
    MakeMessageWithPrincipal(
        label, &aSubjectPrincipal, true, "ScriptDialogLabelNullPrincipal",
        "ScriptDialogLabelContentPrincipal", "ScriptDialogLabelNullPrincipal");

    aError = aAlert
                 ? prompt->AlertCheck(title.get(), final.get(), label.get(),
                                      &disallowDialog)
                 : prompt->ConfirmCheck(title.get(), final.get(), label.get(),
                                        &disallowDialog, &result);

    if (disallowDialog) {
      DisableDialogs();
    }
  } else {
    aError = aAlert ? prompt->Alert(title.get(), final.get())
                    : prompt->Confirm(title.get(), final.get(), &result);
  }

  return result;
}

void nsGlobalWindowOuter::AlertOuter(const nsAString& aMessage,
                                     nsIPrincipal& aSubjectPrincipal,
                                     ErrorResult& aError) {
  AlertOrConfirm(/* aAlert = */ true, aMessage, aSubjectPrincipal, aError);
}

bool nsGlobalWindowOuter::ConfirmOuter(const nsAString& aMessage,
                                       nsIPrincipal& aSubjectPrincipal,
                                       ErrorResult& aError) {
  return AlertOrConfirm(/* aAlert = */ false, aMessage, aSubjectPrincipal,
                        aError);
}

void nsGlobalWindowOuter::PromptOuter(const nsAString& aMessage,
                                      const nsAString& aInitial,
                                      nsAString& aReturn,
                                      nsIPrincipal& aSubjectPrincipal,
                                      ErrorResult& aError) {
  // XXX This method is very similar to nsGlobalWindowOuter::AlertOrConfirm,
  // make sure any modifications here don't need to happen over there!
  SetDOMStringToNull(aReturn);

  if (!AreDialogsEnabled()) {
    // Return null, as if the user just canceled the prompt.
    return;
  }

  // Reset popup state while opening a modal dialog, and firing events
  // about the dialog, to prevent the current state from being active
  // the whole time a modal dialog is open.
  AutoPopupStatePusher popupStatePusher(PopupBlocker::openAbused, true);

  // Before bringing up the window, unsuppress painting and flush
  // pending reflows.
  EnsureReflowFlushAndPaint();

  nsAutoString title;
  MakeMessageWithPrincipal(title, &aSubjectPrincipal, false,
                           "ScriptDlgNullPrincipalHeading", "ScriptDlgHeading",
                           "ScriptDlgGenericHeading");

  // Remove non-terminating null characters from the
  // string. See bug #310037.
  nsAutoString fixedMessage, fixedInitial;
  nsContentUtils::StripNullChars(aMessage, fixedMessage);
  nsContentUtils::PlatformToDOMLineBreaks(fixedMessage);
  nsContentUtils::StripNullChars(aInitial, fixedInitial);

  nsresult rv;
  nsCOMPtr<nsIPromptFactory> promptFac =
      do_GetService("@mozilla.org/prompter;1", &rv);
  if (NS_FAILED(rv)) {
    aError.Throw(rv);
    return;
  }

  nsCOMPtr<nsIPrompt> prompt;
  aError =
      promptFac->GetPrompt(this, NS_GET_IID(nsIPrompt), getter_AddRefs(prompt));
  if (aError.Failed()) {
    return;
  }

  // Always allow content modal prompts for prompt.
  if (nsCOMPtr<nsIWritablePropertyBag2> promptBag = do_QueryInterface(prompt)) {
    promptBag->SetPropertyAsUint32(u"modalType"_ns,
                                   nsIPrompt::MODAL_TYPE_CONTENT);
  }

  // Pass in the default value, if any.
  char16_t* inoutValue = ToNewUnicode(fixedInitial);
  bool disallowDialog = false;

  nsAutoString label;
  label.SetIsVoid(true);
  if (ShouldPromptToBlockDialogs()) {
    nsContentUtils::GetLocalizedString(
        nsContentUtils::eCOMMON_DIALOG_PROPERTIES, "ScriptDialogLabel", label);
  }

  nsAutoSyncOperation sync(mDoc, SyncOperationBehavior::eSuspendInput);
  bool ok;
  aError = prompt->Prompt(title.get(), fixedMessage.get(), &inoutValue,
                          label.IsVoid() ? nullptr : label.get(),
                          &disallowDialog, &ok);

  if (disallowDialog) {
    DisableDialogs();
  }

  // XXX Doesn't this leak inoutValue?
  if (aError.Failed()) {
    return;
  }

  nsString outValue;
  outValue.Adopt(inoutValue);
  if (ok && inoutValue) {
    aReturn = std::move(outValue);
  }
}

void nsGlobalWindowOuter::FocusOuter(CallerType aCallerType,
                                     bool aFromOtherProcess,
                                     uint64_t aActionId) {
  RefPtr<nsFocusManager> fm = nsFocusManager::GetFocusManager();
  if (MOZ_UNLIKELY(!fm)) {
    return;
  }

  auto [canFocus, isActive] = GetBrowsingContext()->CanFocusCheck(aCallerType);
  if (aFromOtherProcess) {
    // We trust that the check passed in a process that's, in principle,
    // untrusted, because we don't have the required caller context available
    // here. Also, the worst that the other process can do in this case is to
    // raise a window it's not supposed to be allowed to raise.
    // https://bugzilla.mozilla.org/show_bug.cgi?id=1677899
    MOZ_ASSERT(XRE_IsContentProcess(),
               "Parent should not trust other processes.");
    canFocus = true;
  }

  nsCOMPtr<nsIBaseWindow> treeOwnerAsWin = GetTreeOwnerWindow();
  if (treeOwnerAsWin && (canFocus || isActive)) {
    bool isEnabled = true;
    if (NS_SUCCEEDED(treeOwnerAsWin->GetEnabled(&isEnabled)) && !isEnabled) {
      NS_WARNING("Should not try to set the focus on a disabled window");
      return;
    }
  }

  if (!mDocShell) {
    return;
  }

  // If the window has a child frame focused, clear the focus. This
  // ensures that focus will be in this frame and not in a child.
  if (nsIContent* content = GetFocusedElement()) {
    if (HTMLIFrameElement::FromNode(content)) {
      fm->ClearFocus(this);
    }
  }

  RefPtr<BrowsingContext> parent;
  BrowsingContext* bc = GetBrowsingContext();
  if (bc) {
    parent = bc->GetParent();
    if (!parent && XRE_IsParentProcess()) {
      parent = bc->Canonical()->GetParentCrossChromeBoundary();
    }
  }
  if (parent) {
    if (!parent->IsInProcess()) {
      if (isActive) {
        OwningNonNull<nsGlobalWindowOuter> kungFuDeathGrip(*this);
        fm->WindowRaised(kungFuDeathGrip, aActionId);
      } else {
        ContentChild* contentChild = ContentChild::GetSingleton();
        MOZ_ASSERT(contentChild);
        contentChild->SendFinalizeFocusOuter(bc, canFocus, aCallerType);
      }
      return;
    }

    MOZ_ASSERT(mDoc, "Call chain should have ensured document creation.");
    if (mDoc) {
      if (Element* frame = mDoc->GetEmbedderElement()) {
        nsContentUtils::RequestFrameFocus(*frame, canFocus, aCallerType);
      }
    }
    return;
  }

  if (canFocus) {
    // if there is no parent, this must be a toplevel window, so raise the
    // window if canFocus is true. If this is a child process, the raise
    // window request will get forwarded to the parent by the puppet widget.
    OwningNonNull<nsGlobalWindowOuter> kungFuDeathGrip(*this);
    fm->RaiseWindow(kungFuDeathGrip, aCallerType, aActionId);
  }
}

nsresult nsGlobalWindowOuter::Focus(CallerType aCallerType) {
  FORWARD_TO_INNER(Focus, (aCallerType), NS_ERROR_UNEXPECTED);
}

void nsGlobalWindowOuter::BlurOuter(CallerType aCallerType) {
  if (!GetBrowsingContext()->CanBlurCheck(aCallerType)) {
    return;
  }

  nsCOMPtr<nsIWebBrowserChrome> chrome = GetWebBrowserChrome();
  if (chrome) {
    chrome->Blur();
  }
}

void nsGlobalWindowOuter::StopOuter(ErrorResult& aError) {
  // IsNavigationAllowed checks are usually done in nsDocShell directly,
  // however nsDocShell::Stop has a bunch of internal users that would fail
  // the IsNavigationAllowed check.
  if (!mDocShell || !nsDocShell::Cast(mDocShell)->IsNavigationAllowed()) {
    return;
  }

  nsCOMPtr<nsIWebNavigation> webNav(do_QueryInterface(mDocShell));
  if (webNav) {
    aError = webNav->Stop(nsIWebNavigation::STOP_ALL);
  }
}

void nsGlobalWindowOuter::PrintOuter(ErrorResult& aError) {
  if (!AreDialogsEnabled()) {
    // Per spec, silently return. https://github.com/whatwg/html/commit/21a1de1
    return;
  }

  // Printing is disabled, silently return.
  if (!StaticPrefs::print_enabled()) {
    return;
  }

  // If we're loading, queue the print for later. This is a special-case that
  // only applies to the window.print() call, for compat with other engines and
  // pre-existing behavior.
  if (mShouldDelayPrintUntilAfterLoad) {
    if (nsIDocShell* docShell = GetDocShell()) {
      if (docShell->GetBusyFlags() & nsIDocShell::BUSY_FLAGS_PAGE_LOADING) {
        mDelayedPrintUntilAfterLoad = true;
        return;
      }
    }
  }

#ifdef NS_PRINTING
  RefPtr<BrowsingContext> top =
      mBrowsingContext ? mBrowsingContext->Top() : nullptr;
  if (NS_WARN_IF(top && top->GetIsPrinting())) {
    return;
  }

  if (top) {
    Unused << top->SetIsPrinting(true);
  }

  auto unset = MakeScopeExit([&] {
    if (top) {
      Unused << top->SetIsPrinting(false);
    }
  });

  const bool forPreview =
      !StaticPrefs::print_always_print_silent() &&
      !Preferences::GetBool("print.prefer_system_dialog", false);
  Print(nullptr, nullptr, nullptr, nullptr, IsPreview(forPreview),
        IsForWindowDotPrint::Yes, nullptr, nullptr, aError);
#endif
}

class MOZ_RAII AutoModalState {
 public:
  explicit AutoModalState(nsGlobalWindowOuter& aWin)
      : mModalStateWin(aWin.EnterModalState()) {}

  ~AutoModalState() {
    if (mModalStateWin) {
      mModalStateWin->LeaveModalState();
    }
  }

  RefPtr<nsGlobalWindowOuter> mModalStateWin;
};

Nullable<WindowProxyHolder> nsGlobalWindowOuter::Print(
    nsIPrintSettings* aPrintSettings, RemotePrintJobChild* aRemotePrintJob,
    nsIWebProgressListener* aListener, nsIDocShell* aDocShellToCloneInto,
    IsPreview aIsPreview, IsForWindowDotPrint aForWindowDotPrint,
    PrintPreviewResolver&& aPrintPreviewCallback,
    RefPtr<BrowsingContext>* aCachedBrowsingContext, ErrorResult& aError) {
#ifdef NS_PRINTING
  nsCOMPtr<nsIPrintSettingsService> printSettingsService =
      do_GetService("@mozilla.org/gfx/printsettings-service;1");
  if (!printSettingsService) {
    // we currently return here in headless mode - should we?
    aError.ThrowNotSupportedError("No print settings service");
    return nullptr;
  }

  nsCOMPtr<nsIPrintSettings> ps = aPrintSettings;
  if (!ps) {
    // We shouldn't need this once bug 1776169 is fixed.
    printSettingsService->GetDefaultPrintSettingsForPrinting(
        getter_AddRefs(ps));
  }

  RefPtr<Document> docToPrint = mDoc;
  if (NS_WARN_IF(!docToPrint)) {
    aError.ThrowNotSupportedError("Document is gone");
    return nullptr;
  }

  RefPtr<BrowsingContext> sourceBC = docToPrint->GetBrowsingContext();
  MOZ_DIAGNOSTIC_ASSERT(sourceBC);
  if (!sourceBC) {
    aError.ThrowNotSupportedError("No browsing context for source document");
    return nullptr;
  }

  nsAutoSyncOperation sync(docToPrint, SyncOperationBehavior::eAllowInput);
  AutoModalState modalState(*this);

  nsCOMPtr<nsIDocumentViewer> viewer;
  RefPtr<BrowsingContext> bc;
  bool hasPrintCallbacks = false;
  bool wasStaticDocument = docToPrint->IsStaticDocument();
  bool usingCachedBrowsingContext = false;
  if (aCachedBrowsingContext && *aCachedBrowsingContext) {
    MOZ_ASSERT(!wasStaticDocument,
               "Why pass in non-empty aCachedBrowsingContext if original "
               "document is already static?");
    if (!wasStaticDocument) {
      // The passed in document is not a static clone and the caller passed in a
      // static clone to reuse, so swap it in.
      docToPrint = (*aCachedBrowsingContext)->GetDocument();
      MOZ_ASSERT(docToPrint);
      MOZ_ASSERT(docToPrint->IsStaticDocument());
      wasStaticDocument = true;
      usingCachedBrowsingContext = true;
    }
  }
  if (wasStaticDocument) {
    if (aForWindowDotPrint == IsForWindowDotPrint::Yes) {
      aError.ThrowNotSupportedError(
          "Calling print() from a print preview is unsupported, did you intend "
          "to call printPreview() instead?");
      return nullptr;
    }
    if (usingCachedBrowsingContext) {
      bc = docToPrint->GetBrowsingContext();
    } else {
      // We're already a print preview window, just reuse our browsing context /
      // content viewer.
      bc = sourceBC;
    }
    nsCOMPtr<nsIDocShell> docShell = bc->GetDocShell();
    if (!docShell) {
      aError.ThrowNotSupportedError("No docshell");
      return nullptr;
    }
    // We could handle this if needed.
    if (aDocShellToCloneInto && aDocShellToCloneInto != docShell) {
      aError.ThrowNotSupportedError(
          "We don't handle cloning a print preview doc into a different "
          "docshell");
      return nullptr;
    }
    docShell->GetDocViewer(getter_AddRefs(viewer));
    MOZ_DIAGNOSTIC_ASSERT(viewer);
  } else {
    if (aDocShellToCloneInto) {
      // Ensure the content viewer is created if needed.
      Unused << aDocShellToCloneInto->GetDocument();
      bc = aDocShellToCloneInto->GetBrowsingContext();
    } else {
      AutoNoJSAPI nojsapi;
      auto printKind = aForWindowDotPrint == IsForWindowDotPrint::Yes
                           ? PrintKind::WindowDotPrint
                           : PrintKind::InternalPrint;
      // For PrintKind::WindowDotPrint, this call will not only make the parent
      // process create a CanonicalBrowsingContext for the returned `bc`, but
      // it will also make the parent process initiate the print/print preview.
      // See the handling of OPEN_PRINT_BROWSER in browser.js.
      aError = OpenInternal(""_ns, u""_ns, u""_ns,
                            false,    // aDialog
                            true,     // aCalledNoScript
                            false,    // aDoJSFixups
                            true,     // aNavigate
                            nullptr,  // No args
                            nullptr,  // aLoadState
                            false,    // aForceNoOpener
                            printKind, getter_AddRefs(bc));
      if (NS_WARN_IF(aError.Failed())) {
        return nullptr;
      }
      if (aCachedBrowsingContext) {
        MOZ_ASSERT(!*aCachedBrowsingContext);
        *aCachedBrowsingContext = bc;
      }
    }
    if (!bc) {
      aError.ThrowNotAllowedError("No browsing context");
      return nullptr;
    }

    Unused << bc->Top()->SetIsPrinting(true);
    nsCOMPtr<nsIDocShell> cloneDocShell = bc->GetDocShell();
    MOZ_DIAGNOSTIC_ASSERT(cloneDocShell);
    cloneDocShell->GetDocViewer(getter_AddRefs(viewer));
    MOZ_DIAGNOSTIC_ASSERT(viewer);
    if (!viewer) {
      aError.ThrowNotSupportedError("Didn't end up with a content viewer");
      return nullptr;
    }

    if (bc != sourceBC) {
      MOZ_ASSERT(bc->IsTopContent());
      // If we are cloning from a document in a different BrowsingContext, we
      // need to make sure to copy over our opener policy information from that
      // BrowsingContext. In the case where the source is an iframe, this
      // information needs to be copied from the toplevel source
      // BrowsingContext, as we may be making a static clone of a single
      // subframe.
      MOZ_ALWAYS_SUCCEEDS(
          bc->SetOpenerPolicy(sourceBC->Top()->GetOpenerPolicy()));
      MOZ_DIAGNOSTIC_ASSERT(bc->Group() == sourceBC->Group());
    }

    if (RefPtr<Document> doc = viewer->GetDocument()) {
      if (doc->IsShowing()) {
        // We're going to drop this document on the floor, in the SetDocument
        // call below. Make sure to run OnPageHide() to keep state consistent
        // and avoids assertions in the document destructor.
        doc->OnPageHide(false, nullptr);
      }
    }

    AutoPrintEventDispatcher dispatcher(*docToPrint);

    nsAutoScriptBlocker blockScripts;
    RefPtr<Document> clone = docToPrint->CreateStaticClone(
        cloneDocShell, viewer, ps, &hasPrintCallbacks);
    if (!clone) {
      aError.ThrowNotSupportedError("Clone operation for printing failed");
      return nullptr;
    }
  }

  nsCOMPtr<nsIWebBrowserPrint> webBrowserPrint = do_QueryInterface(viewer);
  if (!webBrowserPrint) {
    aError.ThrowNotSupportedError(
        "Content viewer didn't implement nsIWebBrowserPrint");
    return nullptr;
  }
  bool closeWindowAfterPrint;
  if (wasStaticDocument) {
    // Here the document was a static clone to begin with that this code did not
    // create, so we should not clean it up.
    // The exception is if we're using the passed-in aCachedBrowsingContext, in
    // which case this is the second print with this static document clone that
    // we created the first time through, and we are responsible for cleaning it
    // up.
    closeWindowAfterPrint = usingCachedBrowsingContext;
  } else {
    // In this case the document was not a static clone, so we made a static
    // clone for printing purposes and must clean it up after the print is done.
    // The exception is if aCachedBrowsingContext is non-NULL, meaning the
    // caller is intending to print this document again, so we need to defer the
    // cleanup until after the second print.
    closeWindowAfterPrint = !aCachedBrowsingContext;
  }
  webBrowserPrint->SetCloseWindowAfterPrint(closeWindowAfterPrint);

  // For window.print(), we postpone making these calls until the round-trip to
  // the parent process (triggered by the OpenInternal call above) calls us
  // again. Only a call from the parent can provide a valid nsPrintSettings
  // object and RemotePrintJobChild object.
  if (aForWindowDotPrint == IsForWindowDotPrint::No) {
    if (aIsPreview == IsPreview::Yes) {
      aError = webBrowserPrint->PrintPreview(ps, aListener,
                                             std::move(aPrintPreviewCallback));
      if (aError.Failed()) {
        return nullptr;
      }
    } else {
      // Historically we've eaten this error.
      webBrowserPrint->Print(ps, aRemotePrintJob, aListener);
    }
  }

  // When using window.print() with the new UI, we usually want to block until
  // the print dialog is hidden. But we can't really do that if we have print
  // callbacks, because we are inside a sync operation, and we want to run
  // microtasks / etc that the print callbacks may create. It is really awkward
  // to have this subtle behavior difference...
  //
  // We also want to do this for fuzzing, so that they can test window.print().
  const bool shouldBlock = [&] {
    if (aForWindowDotPrint == IsForWindowDotPrint::No) {
      return false;
    }
    if (aIsPreview == IsPreview::Yes) {
      return !hasPrintCallbacks;
    }
    return StaticPrefs::dom_window_print_fuzzing_block_while_printing();
  }();

  if (shouldBlock) {
    SpinEventLoopUntil("nsGlobalWindowOuter::Print"_ns,
                       [&] { return bc->IsDiscarded(); });
  }

  return WindowProxyHolder(std::move(bc));
#else
  return nullptr;
#endif  // NS_PRINTING
}

void nsGlobalWindowOuter::MoveToOuter(int32_t aXPos, int32_t aYPos,
                                      CallerType aCallerType,
                                      ErrorResult& aError) {
  /*
   * If caller is not chrome and the user has not explicitly exempted the site,
   * prevent window.moveTo() by exiting early
   */

  if (!CanMoveResizeWindows(aCallerType) || mBrowsingContext->IsSubframe()) {
    return;
  }

  nsCOMPtr<nsIBaseWindow> treeOwnerAsWin = GetTreeOwnerWindow();
  if (!treeOwnerAsWin) {
    aError.Throw(NS_ERROR_FAILURE);
    return;
  }

  // We need to do the same transformation GetScreenXY does.
  RefPtr<nsPresContext> presContext = mDocShell->GetPresContext();
  if (!presContext) {
    return;
  }

  CSSIntPoint cssPos(aXPos, aYPos);
  CheckSecurityLeftAndTop(&cssPos.x.value, &cssPos.y.value, aCallerType);

  nsDeviceContext* context = presContext->DeviceContext();

  auto devPos = LayoutDeviceIntPoint::FromAppUnitsRounded(
      CSSIntPoint::ToAppUnits(cssPos), context->AppUnitsPerDevPixel());

  aError = treeOwnerAsWin->SetPosition(devPos.x, devPos.y);
  CheckForDPIChange();
}

void nsGlobalWindowOuter::MoveByOuter(int32_t aXDif, int32_t aYDif,
                                      CallerType aCallerType,
                                      ErrorResult& aError) {
  /*
   * If caller is not chrome and the user has not explicitly exempted the site,
   * prevent window.moveBy() by exiting early
   */

  if (!CanMoveResizeWindows(aCallerType) || mBrowsingContext->IsSubframe()) {
    return;
  }

  nsCOMPtr<nsIBaseWindow> treeOwnerAsWin = GetTreeOwnerWindow();
  if (!treeOwnerAsWin) {
    aError.Throw(NS_ERROR_FAILURE);
    return;
  }

  // To do this correctly we have to convert what we get from GetPosition
  // into CSS pixels, add the arguments, do the security check, and
  // then convert back to device pixels for the call to SetPosition.

  int32_t x, y;
  aError = treeOwnerAsWin->GetPosition(&x, &y);
  if (aError.Failed()) {
    return;
  }

  auto cssScale = CSSToDevScaleForBaseWindow(treeOwnerAsWin);
  CSSIntPoint cssPos = RoundedToInt(treeOwnerAsWin->GetPosition() / cssScale);

  cssPos.x += aXDif;
  cssPos.y += aYDif;

  CheckSecurityLeftAndTop(&cssPos.x.value, &cssPos.y.value, aCallerType);

  LayoutDeviceIntPoint newDevPos = RoundedToInt(cssPos * cssScale);
  aError = treeOwnerAsWin->SetPosition(newDevPos.x, newDevPos.y);

  CheckForDPIChange();
}

nsresult nsGlobalWindowOuter::MoveBy(int32_t aXDif, int32_t aYDif) {
  ErrorResult rv;
  MoveByOuter(aXDif, aYDif, CallerType::System, rv);

  return rv.StealNSResult();
}

void nsGlobalWindowOuter::ResizeToOuter(int32_t aWidth, int32_t aHeight,
                                        CallerType aCallerType,
                                        ErrorResult& aError) {
  /*
   * If caller is not chrome and the user has not explicitly exempted the site,
   * prevent window.resizeTo() by exiting early
   */

  if (!CanMoveResizeWindows(aCallerType) || mBrowsingContext->IsSubframe()) {
    return;
  }

  nsCOMPtr<nsIBaseWindow> treeOwnerAsWin = GetTreeOwnerWindow();
  if (!treeOwnerAsWin) {
    aError.Throw(NS_ERROR_FAILURE);
    return;
  }

  CSSIntSize cssSize(aWidth, aHeight);
  CheckSecurityWidthAndHeight(&cssSize.width, &cssSize.height, aCallerType);

  LayoutDeviceIntSize devSize =
      RoundedToInt(cssSize * CSSToDevScaleForBaseWindow(treeOwnerAsWin));
  aError = treeOwnerAsWin->SetSize(devSize.width, devSize.height, true);

  CheckForDPIChange();
}

void nsGlobalWindowOuter::ResizeByOuter(int32_t aWidthDif, int32_t aHeightDif,
                                        CallerType aCallerType,
                                        ErrorResult& aError) {
  /*
   * If caller is not chrome and the user has not explicitly exempted the site,
   * prevent window.resizeBy() by exiting early
   */

  if (!CanMoveResizeWindows(aCallerType) || mBrowsingContext->IsSubframe()) {
    return;
  }

  nsCOMPtr<nsIBaseWindow> treeOwnerAsWin = GetTreeOwnerWindow();
  if (!treeOwnerAsWin) {
    aError.Throw(NS_ERROR_FAILURE);
    return;
  }

  LayoutDeviceIntSize size = treeOwnerAsWin->GetSize();

  // To do this correctly we have to convert what we got from GetSize
  // into CSS pixels, add the arguments, do the security check, and
  // then convert back to device pixels for the call to SetSize.

  auto scale = CSSToDevScaleForBaseWindow(treeOwnerAsWin);
  CSSIntSize cssSize = RoundedToInt(size / scale);

  cssSize.width += aWidthDif;
  cssSize.height += aHeightDif;

  CheckSecurityWidthAndHeight(&cssSize.width, &cssSize.height, aCallerType);

  LayoutDeviceIntSize newDevSize = RoundedToInt(cssSize * scale);

  aError = treeOwnerAsWin->SetSize(newDevSize.width, newDevSize.height, true);

  CheckForDPIChange();
}

void nsGlobalWindowOuter::SizeToContentOuter(
    const SizeToContentConstraints& aConstraints, ErrorResult& aError) {
  if (!mDocShell) {
    return;
  }

  if (mBrowsingContext->IsSubframe()) {
    return;
  }

  // The content viewer does a check to make sure that it's a content
  // viewer for a toplevel docshell.
  nsCOMPtr<nsIDocumentViewer> viewer;
  mDocShell->GetDocViewer(getter_AddRefs(viewer));
  if (!viewer) {
    return aError.Throw(NS_ERROR_FAILURE);
  }

  auto contentSize = viewer->GetContentSize(
      aConstraints.mMaxWidth, aConstraints.mMaxHeight, aConstraints.mPrefWidth);
  if (!contentSize) {
    return aError.Throw(NS_ERROR_FAILURE);
  }

  nsCOMPtr<nsIDocShellTreeOwner> treeOwner = GetTreeOwner();
  if (!treeOwner) {
    return aError.Throw(NS_ERROR_FAILURE);
  }

  // Don't use DevToCSSIntPixelsForBaseWindow() nor
  // CSSToDevIntPixelsForBaseWindow() here because contentSize is comes from
  // nsIDocumentViewer::GetContentSize() and it's computed with nsPresContext so
  // that we need to work with nsPresContext here too.
  RefPtr<nsPresContext> presContext = viewer->GetPresContext();
  MOZ_ASSERT(
      presContext,
      "Should be non-nullptr if nsIDocumentViewer::GetContentSize() succeeded");
  CSSIntSize cssSize = *contentSize;

  LayoutDeviceIntSize newDevSize(
      presContext->CSSPixelsToDevPixels(cssSize.width),
      presContext->CSSPixelsToDevPixels(cssSize.height));

  nsCOMPtr<nsIDocShell> docShell = mDocShell;
  aError =
      treeOwner->SizeShellTo(docShell, newDevSize.width, newDevSize.height);
}

already_AddRefed<nsPIWindowRoot> nsGlobalWindowOuter::GetTopWindowRoot() {
  nsPIDOMWindowOuter* piWin = GetPrivateRoot();
  if (!piWin) {
    return nullptr;
  }

  nsCOMPtr<nsPIWindowRoot> window =
      do_QueryInterface(piWin->GetChromeEventHandler());
  return window.forget();
}

void nsGlobalWindowOuter::FirePopupBlockedEvent(
    Document* aDoc, nsIURI* aPopupURI, const nsAString& aPopupWindowName,
    const nsAString& aPopupWindowFeatures) {
  MOZ_ASSERT(aDoc);

  // Fire a "DOMPopupBlocked" event so that the UI can hear about
  // blocked popups.
  PopupBlockedEventInit init;
  init.mBubbles = true;
  init.mCancelable = true;
  // XXX: This is a different object, but webidl requires an inner window here
  // now.
  init.mRequestingWindow = GetCurrentInnerWindowInternal(this);
  init.mPopupWindowURI = aPopupURI;
  init.mPopupWindowName = aPopupWindowName;
  init.mPopupWindowFeatures = aPopupWindowFeatures;

  RefPtr<PopupBlockedEvent> event =
      PopupBlockedEvent::Constructor(aDoc, u"DOMPopupBlocked"_ns, init);

  event->SetTrusted(true);

  aDoc->DispatchEvent(*event);
}

// static
bool nsGlobalWindowOuter::CanSetProperty(const char* aPrefName) {
  // Chrome can set any property.
  if (nsContentUtils::LegacyIsCallerChromeOrNativeCode()) {
    return true;
  }

  // If the pref is set to true, we can not set the property
  // and vice versa.
  return !Preferences::GetBool(aPrefName, true);
}

/* If a window open is blocked, fire the appropriate DOM events. */
void nsGlobalWindowOuter::FireAbuseEvents(
    const nsACString& aPopupURL, const nsAString& aPopupWindowName,
    const nsAString& aPopupWindowFeatures) {
  // fetch the URI of the window requesting the opened window
  nsCOMPtr<Document> currentDoc = GetDoc();
  nsCOMPtr<nsIURI> popupURI;

  // build the URI of the would-have-been popup window
  // (see nsWindowWatcher::URIfromURL)

  // first, fetch the opener's base URI

  nsIURI* baseURL = nullptr;

  nsCOMPtr<Document> doc = GetEntryDocument();
  if (doc) baseURL = doc->GetDocBaseURI();

  // use the base URI to build what would have been the popup's URI
  Unused << NS_NewURI(getter_AddRefs(popupURI), aPopupURL, nullptr, baseURL);

  // fire an event block full of informative URIs
  FirePopupBlockedEvent(currentDoc, popupURI, aPopupWindowName,
                        aPopupWindowFeatures);
}

Nullable<WindowProxyHolder> nsGlobalWindowOuter::OpenOuter(
    const nsAString& aUrl, const nsAString& aName, const nsAString& aOptions,
    ErrorResult& aError) {
  RefPtr<BrowsingContext> bc;
  NS_ConvertUTF16toUTF8 url(aUrl);
  nsresult rv = OpenJS(url, aName, aOptions, getter_AddRefs(bc));
  if (rv == NS_ERROR_MALFORMED_URI) {
    aError.ThrowSyntaxError("Unable to open a window with invalid URL '"_ns +
                            url + "'."_ns);
    return nullptr;
  }

  // XXX Is it possible that some internal errors are thrown here?
  aError = rv;

  if (!bc) {
    return nullptr;
  }
  return WindowProxyHolder(std::move(bc));
}

nsresult nsGlobalWindowOuter::Open(const nsACString& aUrl,
                                   const nsAString& aName,
                                   const nsAString& aOptions,
                                   nsDocShellLoadState* aLoadState,
                                   bool aForceNoOpener,
                                   BrowsingContext** _retval) {
  return OpenInternal(aUrl, aName, aOptions,
                      false,    // aDialog
                      true,     // aCalledNoScript
                      false,    // aDoJSFixups
                      true,     // aNavigate
                      nullptr,  // No args
                      aLoadState, aForceNoOpener, PrintKind::None, _retval);
}

nsresult nsGlobalWindowOuter::OpenJS(const nsACString& aUrl,
                                     const nsAString& aName,
                                     const nsAString& aOptions,
                                     BrowsingContext** _retval) {
  return OpenInternal(aUrl, aName, aOptions,
                      false,    // aDialog
                      false,    // aCalledNoScript
                      true,     // aDoJSFixups
                      true,     // aNavigate
                      nullptr,  // No args
                      nullptr,  // aLoadState
                      false,    // aForceNoOpener
                      PrintKind::None, _retval);
}

// like Open, but attaches to the new window any extra parameters past
// [features] as a JS property named "arguments"
nsresult nsGlobalWindowOuter::OpenDialog(const nsACString& aUrl,
                                         const nsAString& aName,
                                         const nsAString& aOptions,
                                         nsIArray* aArguments,
                                         BrowsingContext** _retval) {
  return OpenInternal(aUrl, aName, aOptions,
                      true,        // aDialog
                      true,        // aCalledNoScript
                      false,       // aDoJSFixups
                      true,        // aNavigate
                      aArguments,  // Arguments
                      nullptr,     // aLoadState
                      false,       // aForceNoOpener
                      PrintKind::None, _retval);
}

// Like Open, but passes aNavigate=false.
/* virtual */
nsresult nsGlobalWindowOuter::OpenNoNavigate(const nsACString& aUrl,
                                             const nsAString& aName,
                                             const nsAString& aOptions,
                                             BrowsingContext** _retval) {
  return OpenInternal(aUrl, aName, aOptions,
                      false,    // aDialog
                      true,     // aCalledNoScript
                      false,    // aDoJSFixups
                      false,    // aNavigate
                      nullptr,  // No args
                      nullptr,  // aLoadState
                      false,    // aForceNoOpener
                      PrintKind::None, _retval);
}

Nullable<WindowProxyHolder> nsGlobalWindowOuter::OpenDialogOuter(
    JSContext* aCx, const nsAString& aUrl, const nsAString& aName,
    const nsAString& aOptions, const Sequence<JS::Value>& aExtraArgument,
    ErrorResult& aError) {
  nsCOMPtr<nsIJSArgArray> argvArray;
  aError =
      NS_CreateJSArgv(aCx, aExtraArgument.Length(), aExtraArgument.Elements(),
                      getter_AddRefs(argvArray));
  if (aError.Failed()) {
    return nullptr;
  }

  RefPtr<BrowsingContext> dialog;
  aError = OpenInternal(NS_ConvertUTF16toUTF8(aUrl), aName, aOptions,
                        true,       // aDialog
                        false,      // aCalledNoScript
                        false,      // aDoJSFixups
                        true,       // aNavigate
                        argvArray,  // Arguments
                        nullptr,    // aLoadState
                        false,      // aForceNoOpener
                        PrintKind::None, getter_AddRefs(dialog));
  if (!dialog) {
    return nullptr;
  }
  return WindowProxyHolder(std::move(dialog));
}

WindowProxyHolder nsGlobalWindowOuter::GetFramesOuter() {
  RefPtr<nsPIDOMWindowOuter> frames(this);
  FlushPendingNotifications(FlushType::ContentAndNotify);
  return WindowProxyHolder(mBrowsingContext);
}

/* static */
bool nsGlobalWindowOuter::GatherPostMessageData(
    JSContext* aCx, const nsAString& aTargetOrigin, BrowsingContext** aSource,
    nsAString& aOrigin, nsIURI** aTargetOriginURI,
    nsIPrincipal** aCallerPrincipal, nsGlobalWindowInner** aCallerInnerWindow,
    nsIURI** aCallerURI, Maybe<nsID>* aCallerAgentClusterId,
    nsACString* aScriptLocation, ErrorResult& aError) {
  //
  // Window.postMessage is an intentional subversion of the same-origin policy.
  // As such, this code must be particularly careful in the information it
  // exposes to calling code.
  //
  // http://www.whatwg.org/specs/web-apps/current-work/multipage/section-crossDocumentMessages.html
  //

  // First, get the caller's window
  RefPtr<nsGlobalWindowInner> callerInnerWin =
      nsContentUtils::IncumbentInnerWindow();
  nsIPrincipal* callerPrin;
  if (callerInnerWin) {
    RefPtr<Document> doc = callerInnerWin->GetExtantDoc();
    if (!doc) {
      return false;
    }
    NS_IF_ADDREF(*aCallerURI = doc->GetDocumentURI());

    // Compute the caller's origin either from its principal or, in the case the
    // principal doesn't carry a URI (e.g. the system principal), the caller's
    // document.  We must get this now instead of when the event is created and
    // dispatched, because ultimately it is the identity of the calling window
    // *now* that determines who sent the message (and not an identity which
    // might have changed due to intervening navigations).
    callerPrin = callerInnerWin->GetPrincipal();
  } else {
    // In case the global is not a window, it can be a sandbox, and the
    // sandbox's principal can be used for the security check.
    nsIGlobalObject* global = GetIncumbentGlobal();
    NS_ASSERTION(global, "Why is there no global object?");
    callerPrin = global->PrincipalOrNull();
    if (callerPrin) {
      BasePrincipal::Cast(callerPrin)->GetScriptLocation(*aScriptLocation);
    }
  }
  if (!callerPrin) {
    return false;
  }

  // if the principal has a URI, use that to generate the origin
  if (!callerPrin->IsSystemPrincipal()) {
    nsAutoCString webExposedOriginSerialization;
    callerPrin->GetWebExposedOriginSerialization(webExposedOriginSerialization);
    CopyUTF8toUTF16(webExposedOriginSerialization, aOrigin);
  } else if (callerInnerWin) {
    if (!*aCallerURI) {
      return false;
    }
    // otherwise use the URI of the document to generate origin
    nsContentUtils::GetWebExposedOriginSerialization(*aCallerURI, aOrigin);
  } else {
    // in case of a sandbox with a system principal origin can be empty
    if (!callerPrin->IsSystemPrincipal()) {
      return false;
    }
  }
  NS_IF_ADDREF(*aCallerPrincipal = callerPrin);

  // "/" indicates same origin as caller, "*" indicates no specific origin is
  // required.
  if (!aTargetOrigin.EqualsASCII("/") && !aTargetOrigin.EqualsASCII("*")) {
    nsCOMPtr<nsIURI> targetOriginURI;
    if (NS_FAILED(NS_NewURI(getter_AddRefs(targetOriginURI), aTargetOrigin))) {
      aError.Throw(NS_ERROR_DOM_SYNTAX_ERR);
      return false;
    }

    nsresult rv = NS_MutateURI(targetOriginURI)
                      .SetUserPass(""_ns)
                      .SetPathQueryRef(""_ns)
                      .Finalize(aTargetOriginURI);
    if (NS_FAILED(rv)) {
      return false;
    }
  }

  if (!nsContentUtils::IsCallerChrome() && callerInnerWin &&
      callerInnerWin->GetOuterWindowInternal()) {
    NS_ADDREF(*aSource = callerInnerWin->GetOuterWindowInternal()
                             ->GetBrowsingContext());
  } else {
    *aSource = nullptr;
  }

  if (aCallerAgentClusterId && callerInnerWin &&
      callerInnerWin->GetDocGroup()) {
    *aCallerAgentClusterId =
        Some(callerInnerWin->GetDocGroup()->AgentClusterId());
  }

  callerInnerWin.forget(aCallerInnerWindow);

  return true;
}

bool nsGlobalWindowOuter::GetPrincipalForPostMessage(
    const nsAString& aTargetOrigin, nsIURI* aTargetOriginURI,
    nsIPrincipal* aCallerPrincipal, nsIPrincipal& aSubjectPrincipal,
    nsIPrincipal** aProvidedPrincipal) {
  //
  // Window.postMessage is an intentional subversion of the same-origin policy.
  // As such, this code must be particularly careful in the information it
  // exposes to calling code.
  //
  // http://www.whatwg.org/specs/web-apps/current-work/multipage/section-crossDocumentMessages.html
  //

  // Convert the provided origin string into a URI for comparison purposes.
  nsCOMPtr<nsIPrincipal> providedPrincipal;

  if (aTargetOrigin.EqualsASCII("/")) {
    providedPrincipal = aCallerPrincipal;
  }
  // "*" indicates no specific origin is required.
  else if (!aTargetOrigin.EqualsASCII("*")) {
    OriginAttributes attrs = aSubjectPrincipal.OriginAttributesRef();
    if (aSubjectPrincipal.IsSystemPrincipal()) {
      auto principal = BasePrincipal::Cast(GetPrincipal());

      if (attrs != principal->OriginAttributesRef()) {
        nsAutoCString targetURL;
        nsAutoCString sourceOrigin;
        nsAutoCString targetOrigin;

        if (NS_FAILED(principal->GetAsciiSpec(targetURL)) ||
            NS_FAILED(principal->GetOrigin(targetOrigin)) ||
            NS_FAILED(aSubjectPrincipal.GetOrigin(sourceOrigin))) {
          NS_WARNING("Failed to get source and target origins");
          return false;
        }

        nsContentUtils::LogSimpleConsoleError(
            NS_ConvertUTF8toUTF16(nsPrintfCString(
                R"(Attempting to post a message to window with url "%s" and )"
                R"(origin "%s" from a system principal scope with mismatched )"
                R"(origin "%s".)",
                targetURL.get(), targetOrigin.get(), sourceOrigin.get())),
            "DOM"_ns, !!principal->PrivateBrowsingId(),
            principal->IsSystemPrincipal());

        attrs = principal->OriginAttributesRef();
      }
    }

    // Create a nsIPrincipal inheriting the app/browser attributes from the
    // caller.
    providedPrincipal =
        BasePrincipal::CreateContentPrincipal(aTargetOriginURI, attrs);
    if (NS_WARN_IF(!providedPrincipal)) {
      return false;
    }
  } else {
    // We still need to check the originAttributes if the target origin is '*'.
    // But we will ingore the FPD here since the FPDs are possible to be
    // different.
    auto principal = BasePrincipal::Cast(GetPrincipal());
    NS_ENSURE_TRUE(principal, false);

    OriginAttributes targetAttrs = principal->OriginAttributesRef();
    OriginAttributes sourceAttrs = aSubjectPrincipal.OriginAttributesRef();
    // We have to exempt the check of OA if the subject prioncipal is a system
    // principal since there are many tests try to post messages to content from
    // chrome with a mismatch OA. For example, using the ContentTask.spawn() to
    // post a message into a private browsing window. The injected code in
    // ContentTask.spawn() will be executed under the system principal and the
    // OA of the system principal mismatches with the OA of a private browsing
    // window.
    MOZ_DIAGNOSTIC_ASSERT(aSubjectPrincipal.IsSystemPrincipal() ||
                          sourceAttrs.EqualsIgnoringFPD(targetAttrs));

    // If 'privacy.firstparty.isolate.block_post_message' is true, we will block
    // postMessage across different first party domains.
    if (OriginAttributes::IsBlockPostMessageForFPI() &&
        !aSubjectPrincipal.IsSystemPrincipal() &&
        sourceAttrs.mFirstPartyDomain != targetAttrs.mFirstPartyDomain) {
      return false;
    }
  }

  providedPrincipal.forget(aProvidedPrincipal);
  return true;
}

void nsGlobalWindowOuter::PostMessageMozOuter(JSContext* aCx,
                                              JS::Handle<JS::Value> aMessage,
                                              const nsAString& aTargetOrigin,
                                              JS::Handle<JS::Value> aTransfer,
                                              nsIPrincipal& aSubjectPrincipal,
                                              ErrorResult& aError) {
  RefPtr<BrowsingContext> sourceBc;
  nsAutoString origin;
  nsCOMPtr<nsIURI> targetOriginURI;
  nsCOMPtr<nsIPrincipal> callerPrincipal;
  RefPtr<nsGlobalWindowInner> callerInnerWindow;
  nsCOMPtr<nsIURI> callerURI;
  Maybe<nsID> callerAgentClusterId = Nothing();
  nsAutoCString scriptLocation;
  if (!GatherPostMessageData(
          aCx, aTargetOrigin, getter_AddRefs(sourceBc), origin,
          getter_AddRefs(targetOriginURI), getter_AddRefs(callerPrincipal),
          getter_AddRefs(callerInnerWindow), getter_AddRefs(callerURI),
          &callerAgentClusterId, &scriptLocation, aError)) {
    return;
  }

  nsCOMPtr<nsIPrincipal> providedPrincipal;
  if (!GetPrincipalForPostMessage(aTargetOrigin, targetOriginURI,
                                  callerPrincipal, aSubjectPrincipal,
                                  getter_AddRefs(providedPrincipal))) {
    return;
  }

  // Create and asynchronously dispatch a runnable which will handle actual DOM
  // event creation and dispatch.
  RefPtr<PostMessageEvent> event = new PostMessageEvent(
      sourceBc, origin, this, providedPrincipal,
      callerInnerWindow ? callerInnerWindow->WindowID() : 0, callerURI,
      scriptLocation, callerAgentClusterId);

  JS::CloneDataPolicy clonePolicy;

  if (GetDocGroup() && callerAgentClusterId.isSome() &&
      GetDocGroup()->AgentClusterId().Equals(callerAgentClusterId.value())) {
    clonePolicy.allowIntraClusterClonableSharedObjects();
  }

  if (callerInnerWindow && callerInnerWindow->IsSharedMemoryAllowed()) {
    clonePolicy.allowSharedMemoryObjects();
  }

  event->Write(aCx, aMessage, aTransfer, clonePolicy, aError);
  if (NS_WARN_IF(aError.Failed())) {
    return;
  }

  event->DispatchToTargetThread(aError);
}

class nsCloseEvent : public Runnable {
  RefPtr<nsGlobalWindowOuter> mWindow;
  bool mIndirect;

  nsCloseEvent(nsGlobalWindowOuter* aWindow, bool aIndirect)
      : mozilla::Runnable("nsCloseEvent"),
        mWindow(aWindow),
        mIndirect(aIndirect) {}

 public:
  static nsresult PostCloseEvent(nsGlobalWindowOuter* aWindow, bool aIndirect) {
    nsCOMPtr<nsIRunnable> ev = new nsCloseEvent(aWindow, aIndirect);
    return aWindow->Dispatch(ev.forget());
  }

  NS_IMETHOD Run() override {
    if (mWindow) {
      if (mIndirect) {
        return PostCloseEvent(mWindow, false);
      }
      mWindow->ReallyCloseWindow();
    }
    return NS_OK;
  }
};

bool nsGlobalWindowOuter::CanClose() {
  if (mIsChrome) {
    nsCOMPtr<nsIBrowserDOMWindow> bwin = GetBrowserDOMWindow();

    bool canClose = true;
    if (bwin && NS_SUCCEEDED(bwin->CanClose(&canClose))) {
      return canClose;
    }
  }

  if (!mDocShell) {
    return true;
  }

  nsCOMPtr<nsIDocumentViewer> viewer;
  mDocShell->GetDocViewer(getter_AddRefs(viewer));
  if (viewer) {
    bool canClose;
    nsresult rv = viewer->PermitUnload(&canClose);
    if (NS_SUCCEEDED(rv) && !canClose) return false;
  }

  // If we still have to print, we delay the closing until print has happened.
  if (mShouldDelayPrintUntilAfterLoad && mDelayedPrintUntilAfterLoad) {
    mDelayedCloseForPrinting = true;
    return false;
  }

  return true;
}

void nsGlobalWindowOuter::CloseOuter(bool aTrustedCaller) {
  if (!mDocShell || IsInModalState() || mBrowsingContext->IsSubframe()) {
    // window.close() is called on a frame in a frameset, on a window
    // that's already closed, or on a window for which there's
    // currently a modal dialog open. Ignore such calls.
    return;
  }

  if (mHavePendingClose) {
    // We're going to be closed anyway; do nothing since we don't want
    // to double-close
    return;
  }

  if (mBlockScriptedClosingFlag) {
    // A script's popup has been blocked and we don't want
    // the window to be closed directly after this event,
    // so the user can see that there was a blocked popup.
    return;
  }

  // Don't allow scripts from content to close non-neterror windows that
  // were not opened by script.
  if (mDoc) {
    nsAutoString url;
    nsresult rv = mDoc->GetURL(url);
    NS_ENSURE_SUCCESS_VOID(rv);

    RefPtr<ChildSHistory> csh =
        nsDocShell::Cast(mDocShell)->GetSessionHistory();

    if (!StringBeginsWith(url, u"about:neterror"_ns) &&
        !mBrowsingContext->GetTopLevelCreatedByWebContent() &&
        !aTrustedCaller && csh && csh->Count() > 1) {
      bool allowClose =
          mAllowScriptsToClose ||
          Preferences::GetBool("dom.allow_scripts_to_close_windows", true);
      if (!allowClose) {
        // We're blocking the close operation
        // report localized error msg in JS console
        nsContentUtils::ReportToConsole(nsIScriptError::warningFlag,
                                        "DOM Window"_ns,
                                        mDoc,  // Better name for the category?
                                        nsContentUtils::eDOM_PROPERTIES,
                                        "WindowCloseByScriptBlockedWarning");

        return;
      }
    }
  }

  if (!mInClose && !mIsClosed && !CanClose()) {
    return;
  }

  // Fire a DOM event notifying listeners that this window is about to
  // be closed. The tab UI code may choose to cancel the default
  // action for this event, if so, we won't actually close the window
  // (since the tab UI code will close the tab in stead). Sure, this
  // could be abused by content code, but do we care? I don't think
  // so...

  bool wasInClose = mInClose;
  mInClose = true;

  if (!DispatchCustomEvent(u"DOMWindowClose"_ns, ChromeOnlyDispatch::eYes)) {
    // Someone chose to prevent the default action for this event, if
    // so, let's not close this window after all...

    mInClose = wasInClose;
    return;
  }

  FinalClose();
}

nsresult nsGlobalWindowOuter::Close() {
  CloseOuter(/* aTrustedCaller = */ true);
  return NS_OK;
}

void nsGlobalWindowOuter::ForceClose() {
  MOZ_ASSERT(XRE_GetProcessType() == GeckoProcessType_Default);

  if (mBrowsingContext->IsSubframe() || !mDocShell) {
    // This may be a frame in a frameset, or a window that's already closed.
    // Ignore such calls.
    return;
  }

  if (mHavePendingClose) {
    // We're going to be closed anyway; do nothing since we don't want
    // to double-close
    return;
  }

  mInClose = true;

  DispatchCustomEvent(u"DOMWindowClose"_ns, ChromeOnlyDispatch::eYes);

  FinalClose();
}

void nsGlobalWindowOuter::FinalClose() {
  // Flag that we were closed.
  mIsClosed = true;

  if (!mBrowsingContext->IsDiscarded()) {
    MOZ_ALWAYS_SUCCEEDS(mBrowsingContext->SetClosed(true));
  }

  // If we get here from CloseOuter then it means that the parent process is
  // going to close our window for us. It's just important to set mIsClosed.
  if (XRE_GetProcessType() == GeckoProcessType_Content) {
    return;
  }

  // This stuff is non-sensical but incredibly fragile. The reasons for the
  // behavior here don't make sense today and may not have ever made sense,
  // but various bits of frontend code break when you change them. If you need
  // to fix up this behavior, feel free to. It's a righteous task, but involves
  // wrestling with various download manager tests, frontend code, and possible
  // broken addons. The chrome tests in toolkit/mozapps/downloads are a good
  // testing ground.
  //
  // In particular, if some inner of |win| is the entry global, we must
  // complete _two_ round-trips to the event loop before the call to
  // ReallyCloseWindow. This allows setTimeout handlers that are set after
  // FinalClose() is called to run before the window is torn down.
  nsCOMPtr<nsPIDOMWindowInner> entryWindow =
      do_QueryInterface(GetEntryGlobal());
  bool indirect = entryWindow && entryWindow->GetOuterWindow() == this;
  if (NS_FAILED(nsCloseEvent::PostCloseEvent(this, indirect))) {
    ReallyCloseWindow();
  } else {
    mHavePendingClose = true;
  }
}

void nsGlobalWindowOuter::ReallyCloseWindow() {
  // Make sure we never reenter this method.
  mHavePendingClose = true;

  nsCOMPtr<nsIBaseWindow> treeOwnerAsWin = GetTreeOwnerWindow();
  if (!treeOwnerAsWin) {
    return;
  }

  treeOwnerAsWin->Destroy();
  CleanUp();
}

void nsGlobalWindowOuter::SuppressEventHandling() {
  if (mSuppressEventHandlingDepth == 0) {
    if (BrowsingContext* bc = GetBrowsingContext()) {
      bc->PreOrderWalk([&](BrowsingContext* aBC) {
        if (nsCOMPtr<nsPIDOMWindowOuter> win = aBC->GetDOMWindow()) {
          if (RefPtr<Document> doc = win->GetExtantDoc()) {
            mSuspendedDocs.AppendElement(doc);
            // Note: Document::SuppressEventHandling will also automatically
            // suppress event handling for any in-process sub-documents.
            // However, since we need to deal with cases where remote
            // BrowsingContexts may be interleaved with in-process ones, we
            // still need to walk the entire tree ourselves. This may be
            // slightly redundant in some cases, but since event handling
            // suppressions maintain a count of current blockers, it does not
            // cause any problems.
            doc->SuppressEventHandling();
          }
        }
      });
    }
  }
  mSuppressEventHandlingDepth++;
}

void nsGlobalWindowOuter::UnsuppressEventHandling() {
  MOZ_ASSERT(mSuppressEventHandlingDepth != 0);
  mSuppressEventHandlingDepth--;

  if (mSuppressEventHandlingDepth == 0 && mSuspendedDocs.Length()) {
    RefPtr<Document> currentDoc = GetExtantDoc();
    bool fireEvent = currentDoc == mSuspendedDocs[0];
    nsTArray<RefPtr<Document>> suspendedDocs = std::move(mSuspendedDocs);
    for (const auto& doc : suspendedDocs) {
      doc->UnsuppressEventHandlingAndFireEvents(fireEvent);
    }
  }
}

nsGlobalWindowOuter* nsGlobalWindowOuter::EnterModalState() {
  // GetInProcessScriptableTop, not GetInProcessTop, so that EnterModalState
  // works properly with <iframe mozbrowser>.
  nsGlobalWindowOuter* topWin = GetInProcessScriptableTopInternal();

  if (!topWin) {
    NS_ERROR("Uh, EnterModalState() called w/o a reachable top window?");
    return nullptr;
  }

  // If there is an active ESM in this window, clear it. Otherwise, this can
  // cause a problem if a modal state is entered during a mouseup event.
  EventStateManager* activeESM = static_cast<EventStateManager*>(
      EventStateManager::GetActiveEventStateManager());
  if (activeESM && activeESM->GetPresContext()) {
    PresShell* activePresShell = activeESM->GetPresContext()->GetPresShell();
    if (activePresShell && (nsContentUtils::ContentIsCrossDocDescendantOf(
                                activePresShell->GetDocument(), mDoc) ||
                            nsContentUtils::ContentIsCrossDocDescendantOf(
                                mDoc, activePresShell->GetDocument()))) {
      EventStateManager::ClearGlobalActiveContent(activeESM);

      PresShell::ReleaseCapturingContent();

      if (activePresShell) {
        RefPtr<nsFrameSelection> frameSelection =
            activePresShell->FrameSelection();
        frameSelection->SetDragState(false);
      }
    }
  }

  // If there are any drag and drop operations in flight, try to end them.
  nsCOMPtr<nsIDragService> ds =
      do_GetService("@mozilla.org/widget/dragservice;1");
  if (ds && topWin->GetDocShell()) {
    if (PresShell* presShell = topWin->GetDocShell()->GetPresShell()) {
      if (nsViewManager* vm = presShell->GetViewManager()) {
        RefPtr<nsIWidget> widget = vm->GetRootWidget();
        if (nsCOMPtr<nsIDragSession> session = ds->GetCurrentSession(widget)) {
          session->EndDragSession(true, 0);
        }
      }
    }
  }

  // Clear the capturing content if it is under topDoc.
  // Usually the activeESM check above does that, but there are cases when
  // we don't have activeESM, or it is for different document.
  Document* topDoc = topWin->GetExtantDoc();
  nsIContent* capturingContent = PresShell::GetCapturingContent();
  if (capturingContent && topDoc &&
      nsContentUtils::ContentIsCrossDocDescendantOf(capturingContent, topDoc)) {
    PresShell::ReleaseCapturingContent();
  }

  if (topWin->mModalStateDepth == 0) {
    topWin->SuppressEventHandling();

    if (nsGlobalWindowInner* inner = GetCurrentInnerWindowInternal(topWin)) {
      inner->Suspend();
    }
  }
  topWin->mModalStateDepth++;
  return topWin;
}

void nsGlobalWindowOuter::LeaveModalState() {
  {
    nsGlobalWindowOuter* topWin = GetInProcessScriptableTopInternal();
    if (!topWin) {
      NS_WARNING("Uh, LeaveModalState() called w/o a reachable top window?");
      return;
    }

    if (topWin != this) {
      MOZ_ASSERT(IsSuspended());
      return topWin->LeaveModalState();
    }
  }

  MOZ_ASSERT(mModalStateDepth != 0);
  MOZ_ASSERT(IsSuspended());
  mModalStateDepth--;

  nsGlobalWindowInner* inner = GetCurrentInnerWindowInternal(this);
  if (mModalStateDepth == 0) {
    if (inner) {
      inner->Resume();
    }

    UnsuppressEventHandling();
  }

  // Remember the time of the last dialog quit.
  if (auto* bcg = GetBrowsingContextGroup()) {
    bcg->SetLastDialogQuitTime(TimeStamp::Now());
  }

  if (mModalStateDepth == 0) {
    RefPtr<Event> event = NS_NewDOMEvent(inner, nullptr, nullptr);
    event->InitEvent(u"endmodalstate"_ns, true, false);
    event->SetTrusted(true);
    event->WidgetEventPtr()->mFlags.mOnlyChromeDispatch = true;
    DispatchEvent(*event);
  }
}

bool nsGlobalWindowOuter::IsInModalState() {
  nsGlobalWindowOuter* topWin = GetInProcessScriptableTopInternal();

  if (!topWin) {
    // IsInModalState() getting called w/o a reachable top window is a bit
    // iffy, but valid enough not to make noise about it.  See bug 404828
    return false;
  }

  return topWin->mModalStateDepth != 0;
}

void nsGlobalWindowOuter::NotifyWindowIDDestroyed(const char* aTopic) {
  nsCOMPtr<nsIRunnable> runnable =
      new WindowDestroyedEvent(this, mWindowID, aTopic);
  Dispatch(runnable.forget());
}

Element* nsGlobalWindowOuter::GetFrameElement(nsIPrincipal& aSubjectPrincipal) {
  // Per HTML5, the frameElement getter returns null in cross-origin situations.
  Element* element = GetFrameElement();
  if (!element) {
    return nullptr;
  }

  if (!aSubjectPrincipal.SubsumesConsideringDomain(element->NodePrincipal())) {
    return nullptr;
  }

  return element;
}

Element* nsGlobalWindowOuter::GetFrameElement() {
  if (!mBrowsingContext || mBrowsingContext->IsTop()) {
    return nullptr;
  }
  return mBrowsingContext->GetEmbedderElement();
}

namespace {
class ChildCommandDispatcher : public Runnable {
 public:
  ChildCommandDispatcher(nsPIWindowRoot* aRoot, nsIBrowserChild* aBrowserChild,
                         nsPIDOMWindowOuter* aWindow, const nsAString& aAction)
      : mozilla::Runnable("ChildCommandDispatcher"),
        mRoot(aRoot),
        mBrowserChild(aBrowserChild),
        mWindow(aWindow),
        mAction(aAction) {}

  NS_IMETHOD Run() override {
    AutoTArray<nsCString, 70> enabledCommands, disabledCommands;
    mRoot->GetEnabledDisabledCommands(enabledCommands, disabledCommands);
    if (enabledCommands.Length() || disabledCommands.Length()) {
      BrowserChild* bc = static_cast<BrowserChild*>(mBrowserChild.get());
      bc->SendEnableDisableCommands(mWindow->GetBrowsingContext(), mAction,
                                    enabledCommands, disabledCommands);
    }

    return NS_OK;
  }

 private:
  nsCOMPtr<nsPIWindowRoot> mRoot;
  nsCOMPtr<nsIBrowserChild> mBrowserChild;
  nsCOMPtr<nsPIDOMWindowOuter> mWindow;
  nsString mAction;
};

class CommandDispatcher : public Runnable {
 public:
  CommandDispatcher(nsIDOMXULCommandDispatcher* aDispatcher,
                    const nsAString& aAction)
      : mozilla::Runnable("CommandDispatcher"),
        mDispatcher(aDispatcher),
        mAction(aAction) {}

  // TODO: Convert this to MOZ_CAN_RUN_SCRIPT (bug 1415230, bug 1535398)
  MOZ_CAN_RUN_SCRIPT_BOUNDARY NS_IMETHOD Run() override {
    return mDispatcher->UpdateCommands(mAction);
  }

  const nsCOMPtr<nsIDOMXULCommandDispatcher> mDispatcher;
  nsString mAction;
};
}  // anonymous namespace

void nsGlobalWindowOuter::UpdateCommands(const nsAString& anAction) {
  // If this is a child process, redirect to the parent process.
  if (nsIDocShell* docShell = GetDocShell()) {
    if (nsCOMPtr<nsIBrowserChild> child = docShell->GetBrowserChild()) {
      nsCOMPtr<nsPIWindowRoot> root = GetTopWindowRoot();
      if (root) {
        nsContentUtils::AddScriptRunner(
            new ChildCommandDispatcher(root, child, this, anAction));
      }
      return;
    }
  }

  nsPIDOMWindowOuter* rootWindow = GetPrivateRoot();
  if (!rootWindow) {
    return;
  }

  Document* doc = rootWindow->GetExtantDoc();

  if (!doc) {
    return;
  }

  // Retrieve the command dispatcher and call updateCommands on it.
  nsIDOMXULCommandDispatcher* xulCommandDispatcher =
      doc->GetCommandDispatcher();
  if (xulCommandDispatcher) {
    nsContentUtils::AddScriptRunner(
        new CommandDispatcher(xulCommandDispatcher, anAction));
  }
}

Selection* nsGlobalWindowOuter::GetSelectionOuter() {
  if (!mDocShell) {
    return nullptr;
  }

  PresShell* presShell = mDocShell->GetPresShell();
  if (!presShell) {
    return nullptr;
  }
  return presShell->GetCurrentSelection(SelectionType::eNormal);
}

already_AddRefed<Selection> nsGlobalWindowOuter::GetSelection() {
  RefPtr<Selection> selection = GetSelectionOuter();
  return selection.forget();
}

bool nsGlobalWindowOuter::FindOuter(const nsAString& aString,
                                    bool aCaseSensitive, bool aBackwards,
                                    bool aWrapAround, bool aWholeWord,
                                    bool aSearchInFrames, bool aShowDialog,
                                    ErrorResult& aError) {
  Unused << aShowDialog;

  nsCOMPtr<nsIWebBrowserFind> finder(do_GetInterface(mDocShell));
  if (!finder) {
    aError.Throw(NS_ERROR_NOT_AVAILABLE);
    return false;
  }

  // Set the options of the search
  aError = finder->SetSearchString(aString);
  if (aError.Failed()) {
    return false;
  }
  finder->SetMatchCase(aCaseSensitive);
  finder->SetFindBackwards(aBackwards);
  finder->SetWrapFind(aWrapAround);
  finder->SetEntireWord(aWholeWord);
  finder->SetSearchFrames(aSearchInFrames);

  // the nsIWebBrowserFind is initialized to use this window
  // as the search root, but uses focus to set the current search
  // frame. If we're being called from JS (as here), this window
  // should be the current search frame.
  nsCOMPtr<nsIWebBrowserFindInFrames> framesFinder(do_QueryInterface(finder));
  if (framesFinder) {
    framesFinder->SetRootSearchFrame(this);  // paranoia
    framesFinder->SetCurrentSearchFrame(this);
  }

  if (aString.IsEmpty()) {
    return false;
  }

  // Launch the search with the passed in search string
  bool didFind = false;
  aError = finder->FindNext(&didFind);
  return didFind;
}

//*****************************************************************************
// EventTarget
//*****************************************************************************

nsPIDOMWindowOuter* nsGlobalWindowOuter::GetOwnerGlobalForBindingsInternal() {
  return this;
}

nsIGlobalObject* nsGlobalWindowOuter::GetOwnerGlobal() const {
  return GetCurrentInnerWindowInternal(this);
}

bool nsGlobalWindowOuter::DispatchEvent(Event& aEvent, CallerType aCallerType,
                                        ErrorResult& aRv) {
  FORWARD_TO_INNER(DispatchEvent, (aEvent, aCallerType, aRv), false);
}

bool nsGlobalWindowOuter::ComputeDefaultWantsUntrusted(ErrorResult& aRv) {
  // It's OK that we just return false here on failure to create an
  // inner.  GetOrCreateListenerManager() will likewise fail, and then
  // we won't be adding any listeners anyway.
  FORWARD_TO_INNER_CREATE(ComputeDefaultWantsUntrusted, (aRv), false);
}

EventListenerManager* nsGlobalWindowOuter::GetOrCreateListenerManager() {
  FORWARD_TO_INNER_CREATE(GetOrCreateListenerManager, (), nullptr);
}

EventListenerManager* nsGlobalWindowOuter::GetExistingListenerManager() const {
  FORWARD_TO_INNER(GetExistingListenerManager, (), nullptr);
}

//*****************************************************************************
// nsGlobalWindowOuter::nsPIDOMWindow
//*****************************************************************************

nsPIDOMWindowOuter* nsGlobalWindowOuter::GetPrivateParent() {
  nsCOMPtr<nsPIDOMWindowOuter> parent = GetInProcessParent();

  if (this == parent) {
    nsCOMPtr<nsIContent> chromeElement(do_QueryInterface(mChromeEventHandler));
    if (!chromeElement)
      return nullptr;  // This is ok, just means a null parent.

    Document* doc = chromeElement->GetComposedDoc();
    if (!doc) return nullptr;  // This is ok, just means a null parent.

    return doc->GetWindow();
  }

  return parent;
}

nsPIDOMWindowOuter* nsGlobalWindowOuter::GetPrivateRoot() {
  nsCOMPtr<nsPIDOMWindowOuter> top = GetInProcessTop();

  nsCOMPtr<nsIContent> chromeElement(do_QueryInterface(mChromeEventHandler));
  if (chromeElement) {
    Document* doc = chromeElement->GetComposedDoc();
    if (doc) {
      nsCOMPtr<nsPIDOMWindowOuter> parent = doc->GetWindow();
      if (parent) {
        top = parent->GetInProcessTop();
      }
    }
  }

  return top;
}

// This has a caller in Windows-only code (nsNativeAppSupportWin).
Location* nsGlobalWindowOuter::GetLocation() {
  // This method can be called on the outer window as well.
  FORWARD_TO_INNER(Location, (), nullptr);
}

void nsGlobalWindowOuter::SetIsBackground(bool aIsBackground) {
  bool changed = aIsBackground != IsBackground();
  SetIsBackgroundInternal(aIsBackground);

  nsGlobalWindowInner* inner = GetCurrentInnerWindowInternal(this);

  if (inner && changed) {
    inner->UpdateBackgroundState();
  }

  if (aIsBackground) {
    // Notify gamepadManager we are at the background window,
    // we need to stop vibrate.
    // Stop the vr telemery time spent when it switches to
    // the background window.
    if (inner && changed) {
      inner->StopGamepadHaptics();
      inner->StopVRActivity();
    }
    return;
  }

  if (inner) {
    inner->SyncGamepadState();
    inner->StartVRActivity();
  }
}

void nsGlobalWindowOuter::SetIsBackgroundInternal(bool aIsBackground) {
  mIsBackground = aIsBackground;
}

void nsGlobalWindowOuter::SetChromeEventHandler(
    EventTarget* aChromeEventHandler) {
  SetChromeEventHandlerInternal(aChromeEventHandler);
  // update the chrome event handler on all our inner windows
  RefPtr<nsGlobalWindowInner> inner;
  for (PRCList* node = PR_LIST_HEAD(this); node != this;
       node = PR_NEXT_LINK(inner)) {
    // This cast is only safe if `node != this`, as nsGlobalWindowOuter is also
    // in the list.
    inner = static_cast<nsGlobalWindowInner*>(node);
    NS_ASSERTION(!inner->mOuterWindow || inner->mOuterWindow == this,
                 "bad outer window pointer");
    inner->SetChromeEventHandlerInternal(aChromeEventHandler);
  }
}

void nsGlobalWindowOuter::SetFocusedElement(Element* aElement,
                                            uint32_t aFocusMethod,
                                            bool aNeedsFocus) {
  FORWARD_TO_INNER_VOID(SetFocusedElement,
                        (aElement, aFocusMethod, aNeedsFocus));
}

uint32_t nsGlobalWindowOuter::GetFocusMethod() {
  FORWARD_TO_INNER(GetFocusMethod, (), 0);
}

bool nsGlobalWindowOuter::ShouldShowFocusRing() {
  FORWARD_TO_INNER(ShouldShowFocusRing, (), false);
}

bool nsGlobalWindowOuter::TakeFocus(bool aFocus, uint32_t aFocusMethod) {
  FORWARD_TO_INNER(TakeFocus, (aFocus, aFocusMethod), false);
}

void nsGlobalWindowOuter::SetReadyForFocus() {
  FORWARD_TO_INNER_VOID(SetReadyForFocus, ());
}

void nsGlobalWindowOuter::PageHidden(bool aIsEnteringBFCacheInParent) {
  FORWARD_TO_INNER_VOID(PageHidden, (aIsEnteringBFCacheInParent));
}

already_AddRefed<nsICSSDeclaration>
nsGlobalWindowOuter::GetComputedStyleHelperOuter(Element& aElt,
                                                 const nsAString& aPseudoElt,
                                                 bool aDefaultStylesOnly,
                                                 ErrorResult& aRv) {
  if (!mDoc) {
    return nullptr;
  }

  RefPtr<nsICSSDeclaration> compStyle = NS_NewComputedDOMStyle(
      &aElt, aPseudoElt, mDoc,
      aDefaultStylesOnly ? nsComputedDOMStyle::StyleType::DefaultOnly
                         : nsComputedDOMStyle::StyleType::All,
      aRv);

  return compStyle.forget();
}

//*****************************************************************************
// nsGlobalWindowOuter::nsIInterfaceRequestor
//*****************************************************************************

nsresult nsGlobalWindowOuter::GetInterfaceInternal(const nsIID& aIID,
                                                   void** aSink) {
  NS_ENSURE_ARG_POINTER(aSink);
  *aSink = nullptr;

  if (aIID.Equals(NS_GET_IID(nsIWebNavigation))) {
    nsCOMPtr<nsIWebNavigation> webNav(do_QueryInterface(mDocShell));
    webNav.forget(aSink);
  } else if (aIID.Equals(NS_GET_IID(nsIDocShell))) {
    nsCOMPtr<nsIDocShell> docShell = mDocShell;
    docShell.forget(aSink);
  }
#ifdef NS_PRINTING
  else if (aIID.Equals(NS_GET_IID(nsIWebBrowserPrint))) {
    if (mDocShell) {
      nsCOMPtr<nsIDocumentViewer> viewer;
      mDocShell->GetDocViewer(getter_AddRefs(viewer));
      if (viewer) {
        nsCOMPtr<nsIWebBrowserPrint> webBrowserPrint(do_QueryInterface(viewer));
        webBrowserPrint.forget(aSink);
      }
    }
  }
#endif
  else if (aIID.Equals(NS_GET_IID(nsILoadContext))) {
    nsCOMPtr<nsILoadContext> loadContext(do_QueryInterface(mDocShell));
    loadContext.forget(aSink);
  }

  return *aSink ? NS_OK : NS_ERROR_NO_INTERFACE;
}

NS_IMETHODIMP
nsGlobalWindowOuter::GetInterface(const nsIID& aIID, void** aSink) {
  nsresult rv = GetInterfaceInternal(aIID, aSink);
  if (rv == NS_ERROR_NO_INTERFACE) {
    return QueryInterface(aIID, aSink);
  }
  return rv;
}

bool nsGlobalWindowOuter::IsSuspended() const {
  MOZ_ASSERT(NS_IsMainThread());
  // No inner means we are effectively suspended
  if (!mInnerWindow) {
    return true;
  }
  return nsGlobalWindowInner::Cast(mInnerWindow)->IsSuspended();
}

bool nsGlobalWindowOuter::IsFrozen() const {
  MOZ_ASSERT(NS_IsMainThread());
  // No inner means we are effectively frozen
  if (!mInnerWindow) {
    return true;
  }
  return nsGlobalWindowInner::Cast(mInnerWindow)->IsFrozen();
}

nsresult nsGlobalWindowOuter::FireDelayedDOMEvents(bool aIncludeSubWindows) {
  FORWARD_TO_INNER(FireDelayedDOMEvents, (aIncludeSubWindows),
                   NS_ERROR_UNEXPECTED);
}

//*****************************************************************************
// nsGlobalWindowOuter: Window Control Functions
//*****************************************************************************

nsPIDOMWindowOuter* nsGlobalWindowOuter::GetInProcessParentInternal() {
  nsCOMPtr<nsPIDOMWindowOuter> parent = GetInProcessParent();

  if (parent && parent != this) {
    return parent;
  }

  return nullptr;
}

void nsGlobalWindowOuter::UnblockScriptedClosing() {
  mBlockScriptedClosingFlag = false;
}

class AutoUnblockScriptClosing {
 private:
  RefPtr<nsGlobalWindowOuter> mWin;

 public:
  explicit AutoUnblockScriptClosing(nsGlobalWindowOuter* aWin) : mWin(aWin) {
    MOZ_ASSERT(mWin);
  }
  ~AutoUnblockScriptClosing() {
    void (nsGlobalWindowOuter::*run)() =
        &nsGlobalWindowOuter::UnblockScriptedClosing;
    nsCOMPtr<nsIRunnable> caller = NewRunnableMethod(
        "AutoUnblockScriptClosing::~AutoUnblockScriptClosing", mWin, run);
    mWin->Dispatch(caller.forget());
  }
};

nsresult nsGlobalWindowOuter::OpenInternal(
    const nsACString& aUrl, const nsAString& aName, const nsAString& aOptions,
    bool aDialog, bool aCalledNoScript, bool aDoJSFixups, bool aNavigate,
    nsIArray* aArguments, nsDocShellLoadState* aLoadState, bool aForceNoOpener,
    PrintKind aPrintKind, BrowsingContext** aReturn) {
  mozilla::Maybe<AutoUnblockScriptClosing> closeUnblocker;

  // Calls to window.open from script should navigate.
  MOZ_ASSERT(aCalledNoScript || aNavigate);

  *aReturn = nullptr;

  nsCOMPtr<nsIWebBrowserChrome> chrome = GetWebBrowserChrome();
  if (!chrome) {
    // No chrome means we don't want to go through with this open call
    // -- see nsIWindowWatcher.idl
    return NS_ERROR_NOT_AVAILABLE;
  }

  NS_ASSERTION(mDocShell, "Must have docshell here");

  NS_ConvertUTF16toUTF8 optionsUtf8(aOptions);

  WindowFeatures features;
  if (!features.Tokenize(optionsUtf8)) {
    return NS_ERROR_FAILURE;
  }

  bool forceNoOpener = aForceNoOpener;
  if (features.Exists("noopener")) {
    forceNoOpener = features.GetBool("noopener");
    features.Remove("noopener");
  }

  bool forceNoReferrer = false;
  if (features.Exists("noreferrer")) {
    forceNoReferrer = features.GetBool("noreferrer");
    if (forceNoReferrer) {
      // noreferrer implies noopener
      forceNoOpener = true;
    }
    features.Remove("noreferrer");
  }

  nsAutoCString options;
  features.Stringify(options);

  // If noopener is force-enabled for the current document, then set noopener to
  // true, and clear the name to "_blank".
  nsAutoString windowName(aName);
  if (nsDocShell::Cast(GetDocShell())->NoopenerForceEnabled() &&
      aPrintKind == PrintKind::None) {
    MOZ_DIAGNOSTIC_ASSERT(aNavigate,
                          "cannot OpenNoNavigate if noopener is force-enabled");

    forceNoOpener = true;
    windowName = u"_blank"_ns;
  }

  bool windowExists = WindowExists(windowName, forceNoOpener, !aCalledNoScript);

  // XXXbz When this gets fixed to not use LegacyIsCallerNativeCode()
  // (indirectly) maybe we can nix the AutoJSAPI usage OnLinkClickEvent::Run.
  // But note that if you change this to GetEntryGlobal(), say, then
  // OnLinkClickEvent::Run will need a full-blown AutoEntryScript. (Bug 1930445)
  const bool checkForPopup = [&]() {
    if (aDialog) {
      return false;
    }
    if (windowExists) {
      return false;
    }
    if (aLoadState && aLoadState->IsFormSubmission()) {
      return true;
    }
    return !nsContentUtils::LegacyIsCallerChromeOrNativeCode();
  }();

  nsCOMPtr<nsIURI> uri;

  // It's important to do this security check before determining whether this
  // window opening should be blocked, to ensure that we don't FireAbuseEvents
  // for a window opening that wouldn't have succeeded in the first place.
  if (!aUrl.IsEmpty()) {
    // It's safe to skip the security check below if we're a dialog because
    // window.openDialog is not callable from content script. See bug 56851.
    //
    // If we're not navigating, we assume that whoever *does* navigate the
    // window will do a security check of their own.
    auto result =
        URIfromURLAndMaybeDoSecurityCheck(aUrl, !aDialog && aNavigate);
    if (result.isErr()) {
      return result.unwrapErr();
    }

    uri = result.unwrap();
  } else if (mDoc) {
    mDoc->SetUseCounter(eUseCounter_custom_WindowOpenEmptyUrl);
  }

  UserActivation::Modifiers modifiers;
  mBrowsingContext->GetUserActivationModifiersForPopup(&modifiers);

  // Need to create loadState before the user activation is consumed in
  // BrowsingContext::RevisePopupAbuseLevel() below.
  RefPtr<nsDocShellLoadState> loadState = aLoadState;
  if (!loadState && aNavigate && uri) {
    loadState = nsWindowWatcher::CreateLoadState(uri, this);
  }

  PopupBlocker::PopupControlState abuseLevel =
      PopupBlocker::GetPopupControlState();
  if (checkForPopup) {
    abuseLevel = mBrowsingContext->RevisePopupAbuseLevel(abuseLevel);
    if (abuseLevel >= PopupBlocker::openBlocked) {
      if (!aCalledNoScript) {
        // If script in some other window is doing a window.open on us and
        // it's being blocked, then it's OK to close us afterwards, probably.
        // But if we're doing a window.open on ourselves and block the popup,
        // prevent this window from closing until after this script terminates
        // so that whatever popup blocker UI the app has will be visible.
        nsCOMPtr<nsPIDOMWindowInner> entryWindow =
            do_QueryInterface(GetEntryGlobal());
        // Note that entryWindow can be null here if some JS component was the
        // place where script was entered for this JS execution.
        if (entryWindow && entryWindow->GetOuterWindow() == this) {
          mBlockScriptedClosingFlag = true;
          closeUnblocker.emplace(this);
        }
      }

      FireAbuseEvents(aUrl, windowName, aOptions);
      return aDoJSFixups ? NS_OK : NS_ERROR_FAILURE;
    }
  }

  // Per https://github.com/whatwg/html/pull/10547, we should always consume
  // user activation when opening a new window, even if the popup blocker is
  // disabled or the website has popup permission.
  if (!windowExists && mDoc) {
    mDoc->ConsumeTransientUserGestureActivation();
  }

  RefPtr<BrowsingContext> domReturn;

  nsresult rv = NS_OK;
  nsCOMPtr<nsIWindowWatcher> wwatch =
      do_GetService(NS_WINDOWWATCHER_CONTRACTID, &rv);
  NS_ENSURE_TRUE(wwatch, rv);

  NS_ConvertUTF16toUTF8 name(windowName);

  nsCOMPtr<nsPIWindowWatcher> pwwatch(do_QueryInterface(wwatch));
  NS_ENSURE_STATE(pwwatch);

  MOZ_ASSERT_IF(checkForPopup, abuseLevel < PopupBlocker::openBlocked);
  // At this point we should know for a fact that if checkForPopup then
  // abuseLevel < PopupBlocker::openBlocked, so we could just check for
  // abuseLevel == PopupBlocker::openControlled.  But let's be defensive just in
  // case and treat anything that fails the above assert as a spam popup too, if
  // it ever happens.
  bool isPopupSpamWindow =
      checkForPopup && (abuseLevel >= PopupBlocker::openControlled);

  const auto wwPrintKind = [&] {
    switch (aPrintKind) {
      case PrintKind::None:
        return nsPIWindowWatcher::PRINT_NONE;
      case PrintKind::InternalPrint:
        return nsPIWindowWatcher::PRINT_INTERNAL;
      case PrintKind::WindowDotPrint:
        return nsPIWindowWatcher::PRINT_WINDOW_DOT_PRINT;
    }
    MOZ_ASSERT_UNREACHABLE("Wat");
    return nsPIWindowWatcher::PRINT_NONE;
  }();

  {
    // Reset popup state while opening a window to prevent the
    // current state from being active the whole time a modal
    // dialog is open.
    AutoPopupStatePusher popupStatePusher(PopupBlocker::openAbused, true);

    if (!aCalledNoScript) {
      // We asserted at the top of this function that aNavigate is true for
      // !aCalledNoScript.
      rv = pwwatch->OpenWindow2(this, uri, name, options, modifiers,
                                /* aCalledFromScript = */ true, aDialog,
                                aNavigate, aArguments, isPopupSpamWindow,
                                forceNoOpener, forceNoReferrer, wwPrintKind,
                                loadState, getter_AddRefs(domReturn));
    } else {
      // Force a system caller here so that the window watcher won't screw us
      // up.  We do NOT want this case looking at the JS context on the stack
      // when searching.  Compare comments on
      // nsIDOMWindow::OpenWindow and nsIWindowWatcher::OpenWindow.

      // Note: Because nsWindowWatcher is so broken, it's actually important
      // that we don't force a system caller here, because that screws it up
      // when it tries to compute the caller principal to associate with dialog
      // arguments. That whole setup just really needs to be rewritten. :-(
      AutoNoJSAPI nojsapi;
      rv = pwwatch->OpenWindow2(this, uri, name, options, modifiers,
                                /* aCalledFromScript = */ false, aDialog,
                                aNavigate, aArguments, isPopupSpamWindow,
                                forceNoOpener, forceNoReferrer, wwPrintKind,
                                loadState, getter_AddRefs(domReturn));
    }
  }

  NS_ENSURE_SUCCESS(rv, rv);

  // success!

  if (!aCalledNoScript && !windowExists && uri && !forceNoOpener) {
    MaybeAllowStorageForOpenedWindow(uri);
  }

  if (domReturn && aDoJSFixups) {
    nsPIDOMWindowOuter* outer = domReturn->GetDOMWindow();
    if (outer && !nsGlobalWindowOuter::Cast(outer)->IsChromeWindow()) {
      // A new non-chrome window was created from a call to
      // window.open() from JavaScript, make sure there's a document in
      // the new window. We do this by simply asking the new window for
      // its document, this will synchronously create an empty document
      // if there is no document in the window.
      // XXXbz should this just use EnsureInnerWindow()?

      // Force document creation.
      nsCOMPtr<Document> doc = outer->GetDoc();
      Unused << doc;
    }
  }

  domReturn.forget(aReturn);
  return NS_OK;
}

void nsGlobalWindowOuter::MaybeAllowStorageForOpenedWindow(nsIURI* aURI) {
  nsGlobalWindowInner* inner = GetCurrentInnerWindowInternal(this);
  if (NS_WARN_IF(!inner)) {
    return;
  }

  // Don't trigger the heuristic for third-party trackers.
  if (StaticPrefs::
          privacy_restrict3rdpartystorage_heuristic_exclude_third_party_trackers() &&
      nsContentUtils::IsThirdPartyTrackingResourceWindow(inner)) {
    return;
  }

  // No 3rd party URL/window.
  if (!AntiTrackingUtils::IsThirdPartyWindow(inner, aURI)) {
    return;
  }

  Document* doc = inner->GetDoc();
  if (!doc) {
    return;
  }
  nsCOMPtr<nsIPrincipal> principal = BasePrincipal::CreateContentPrincipal(
      aURI, doc->NodePrincipal()->OriginAttributesRef());

  // We don't care when the asynchronous work finishes here.
  // Without e10s or fission enabled this is run in the parent process.
  if (XRE_IsParentProcess()) {
    Unused << StorageAccessAPIHelper::AllowAccessForOnParentProcess(
        principal, GetBrowsingContext(), ContentBlockingNotifier::eOpener);
  } else {
    Unused << StorageAccessAPIHelper::AllowAccessForOnChildProcess(
        principal, GetBrowsingContext(), ContentBlockingNotifier::eOpener);
  }
}

//*****************************************************************************
// nsGlobalWindowOuter: Helper Functions
//*****************************************************************************

already_AddRefed<nsIDocShellTreeOwner> nsPIDOMWindowOuter::GetTreeOwner() {
  // If there's no docShellAsItem, this window must have been closed,
  // in that case there is no tree owner.

  if (!mDocShell) {
    return nullptr;
  }

  nsCOMPtr<nsIDocShellTreeOwner> treeOwner;
  mDocShell->GetTreeOwner(getter_AddRefs(treeOwner));
  return treeOwner.forget();
}

already_AddRefed<nsIBaseWindow> nsPIDOMWindowOuter::GetTreeOwnerWindow() {
  nsCOMPtr<nsIDocShellTreeOwner> treeOwner;

  // If there's no mDocShell, this window must have been closed,
  // in that case there is no tree owner.

  if (mDocShell) {
    mDocShell->GetTreeOwner(getter_AddRefs(treeOwner));
  }

  nsCOMPtr<nsIBaseWindow> baseWindow = do_QueryInterface(treeOwner);
  return baseWindow.forget();
}

already_AddRefed<nsIWebBrowserChrome>
nsPIDOMWindowOuter::GetWebBrowserChrome() {
  nsCOMPtr<nsIDocShellTreeOwner> treeOwner = GetTreeOwner();

  nsCOMPtr<nsIWebBrowserChrome> browserChrome = do_GetInterface(treeOwner);
  return browserChrome.forget();
}

ScrollContainerFrame* nsGlobalWindowOuter::GetScrollContainerFrame() {
  if (!mDocShell) {
    return nullptr;
  }

  PresShell* presShell = mDocShell->GetPresShell();
  if (presShell) {
    return presShell->GetRootScrollContainerFrame();
  }
  return nullptr;
}

Result<already_AddRefed<nsIURI>, nsresult>
nsGlobalWindowOuter::URIfromURLAndMaybeDoSecurityCheck(const nsACString& aURL,
                                                       bool aSecurityCheck) {
  nsCOMPtr<nsPIDOMWindowInner> sourceWindow =
      do_QueryInterface(GetEntryGlobal());
  if (!sourceWindow) {
    sourceWindow = GetCurrentInnerWindow();
  }

  // Resolve the baseURI, which could be relative to the calling window.
  //
  // Note the algorithm to get the base URI should match the one
  // used to actually kick off the load in nsWindowWatcher.cpp.
  nsCOMPtr<Document> doc = sourceWindow->GetDoc();
  nsIURI* baseURI = nullptr;
  auto encoding = UTF_8_ENCODING;  // default to utf-8
  if (doc) {
    baseURI = doc->GetDocBaseURI();
    encoding = doc->GetDocumentCharacterSet();
  }
  nsCOMPtr<nsIURI> uri;
  nsresult rv = NS_NewURI(getter_AddRefs(uri), aURL, encoding, baseURI);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return Err(NS_ERROR_DOM_SYNTAX_ERR);
  }

  if (aSecurityCheck) {
    AutoJSContext cx;
    nsGlobalWindowInner* sourceWin = nsGlobalWindowInner::Cast(sourceWindow);
    JSAutoRealm ar(cx, sourceWin->GetGlobalJSObject());

    if (NS_FAILED(nsContentUtils::GetSecurityManager()->CheckLoadURIFromScript(
            cx, uri))) {
      return Err(NS_ERROR_FAILURE);
    }
  }

  return uri.forget();
}

void nsGlobalWindowOuter::FlushPendingNotifications(FlushType aType) {
  if (mDoc) {
    mDoc->FlushPendingNotifications(aType);
  }
}

void nsGlobalWindowOuter::EnsureSizeAndPositionUpToDate() {
  // If we're a subframe, make sure our size is up to date.  Make sure to go
  // through the document chain rather than the window chain to not flush on
  // detached iframes, see bug 1545516.
  if (mDoc && mDoc->StyleOrLayoutObservablyDependsOnParentDocumentLayout()) {
    RefPtr<Document> parent = mDoc->GetInProcessParentDocument();
    parent->FlushPendingNotifications(FlushType::Layout);
  }
}

already_AddRefed<nsISupports> nsGlobalWindowOuter::SaveWindowState() {
  MOZ_ASSERT(!mozilla::SessionHistoryInParent());

  if (!mContext || !GetWrapperPreserveColor()) {
    // The window may be getting torn down; don't bother saving state.
    return nullptr;
  }

  nsGlobalWindowInner* inner = GetCurrentInnerWindowInternal(this);
  NS_ASSERTION(inner, "No inner window to save");

  if (WindowContext* wc = inner->GetWindowContext()) {
    MOZ_ASSERT(!wc->GetWindowStateSaved());
    Unused << wc->SetWindowStateSaved(true);
  }

  // Don't do anything else to this inner window! After this point, all
  // calls to SetTimeoutOrInterval will create entries in the timeout
  // list that will only run after this window has come out of the bfcache.
  // Also, while we're frozen, we won't dispatch online/offline events
  // to the page.
  inner->Freeze();

  nsCOMPtr<nsISupports> state = new WindowStateHolder(inner);

  MOZ_LOG(gPageCacheLog, LogLevel::Debug,
          ("saving window state, state = %p", (void*)state));

  return state.forget();
}

nsresult nsGlobalWindowOuter::RestoreWindowState(nsISupports* aState) {
  MOZ_ASSERT(!mozilla::SessionHistoryInParent());

  if (!mContext || !GetWrapperPreserveColor()) {
    // The window may be getting torn down; don't bother restoring state.
    return NS_OK;
  }

  nsCOMPtr<WindowStateHolder> holder = do_QueryInterface(aState);
  NS_ENSURE_TRUE(holder, NS_ERROR_FAILURE);

  MOZ_LOG(gPageCacheLog, LogLevel::Debug,
          ("restoring window state, state = %p", (void*)holder));

  // And we're ready to go!
  nsGlobalWindowInner* inner = GetCurrentInnerWindowInternal(this);

  // if a link is focused, refocus with the FLAG_SHOWRING flag set. This makes
  // it easy to tell which link was last clicked when going back a page.
  RefPtr<Element> focusedElement = inner->GetFocusedElement();
  if (nsContentUtils::ContentIsLink(focusedElement)) {
    if (RefPtr<nsFocusManager> fm = nsFocusManager::GetFocusManager()) {
      fm->SetFocus(focusedElement, nsIFocusManager::FLAG_NOSCROLL |
                                       nsIFocusManager::FLAG_SHOWRING);
    }
  }

  if (WindowContext* wc = inner->GetWindowContext()) {
    MOZ_ASSERT(wc->GetWindowStateSaved());
    Unused << wc->SetWindowStateSaved(false);
  }

  inner->Thaw();

  holder->DidRestoreWindow();

  return NS_OK;
}

void nsGlobalWindowOuter::AddSizeOfIncludingThis(
    nsWindowSizes& aWindowSizes) const {
  aWindowSizes.mDOMSizes.mDOMOtherSize +=
      aWindowSizes.mState.mMallocSizeOf(this);
}

uint32_t nsGlobalWindowOuter::GetAutoActivateVRDisplayID() {
  uint32_t retVal = mAutoActivateVRDisplayID;
  mAutoActivateVRDisplayID = 0;
  return retVal;
}

void nsGlobalWindowOuter::SetAutoActivateVRDisplayID(
    uint32_t aAutoActivateVRDisplayID) {
  mAutoActivateVRDisplayID = aAutoActivateVRDisplayID;
}

already_AddRefed<nsWindowRoot> nsGlobalWindowOuter::GetWindowRootOuter() {
  nsCOMPtr<nsPIWindowRoot> root = GetTopWindowRoot();
  return root.forget().downcast<nsWindowRoot>();
}

nsIDOMWindowUtils* nsGlobalWindowOuter::WindowUtils() {
  if (!mWindowUtils) {
    mWindowUtils = new nsDOMWindowUtils(this);
  }
  return mWindowUtils;
}

bool nsGlobalWindowOuter::IsInSyncOperation() {
  return GetExtantDoc() && GetExtantDoc()->IsInSyncOperation();
}

// Note: This call will lock the cursor, it will not change as it moves.
// To unlock, the cursor must be set back to Auto.
void nsGlobalWindowOuter::SetCursorOuter(const nsACString& aCursor,
                                         ErrorResult& aError) {
  auto cursor = StyleCursorKind::Auto;
  if (!Servo_CursorKind_Parse(&aCursor, &cursor)) {
    // FIXME: It's a bit weird that this doesn't throw but stuff below does, but
    // matches previous behavior so...
    return;
  }

  RefPtr<nsPresContext> presContext;
  if (mDocShell) {
    presContext = mDocShell->GetPresContext();
  }

  if (presContext) {
    // Need root widget.
    PresShell* presShell = mDocShell->GetPresShell();
    if (!presShell) {
      aError.Throw(NS_ERROR_FAILURE);
      return;
    }

    nsViewManager* vm = presShell->GetViewManager();
    if (!vm) {
      aError.Throw(NS_ERROR_FAILURE);
      return;
    }

    nsView* rootView = vm->GetRootView();
    if (!rootView) {
      aError.Throw(NS_ERROR_FAILURE);
      return;
    }

    nsIWidget* widget = rootView->GetNearestWidget(nullptr);
    if (!widget) {
      aError.Throw(NS_ERROR_FAILURE);
      return;
    }

    // Call esm and set cursor.
    aError = presContext->EventStateManager()->SetCursor(
        cursor, nullptr, {}, Nothing(), widget, true);
  }
}

nsIBrowserDOMWindow* nsGlobalWindowOuter::GetBrowserDOMWindow() {
  MOZ_RELEASE_ASSERT(IsChromeWindow());
  return mChromeFields.mBrowserDOMWindow;
}

void nsGlobalWindowOuter::SetBrowserDOMWindowOuter(
    nsIBrowserDOMWindow* aBrowserWindow) {
  MOZ_ASSERT(IsChromeWindow());
  mChromeFields.mBrowserDOMWindow = aBrowserWindow;
}

ChromeMessageBroadcaster* nsGlobalWindowOuter::GetMessageManager() {
  if (!mInnerWindow) {
    NS_WARNING("No inner window available!");
    return nullptr;
  }
  return GetCurrentInnerWindowInternal(this)->MessageManager();
}

ChromeMessageBroadcaster* nsGlobalWindowOuter::GetGroupMessageManager(
    const nsAString& aGroup) {
  if (!mInnerWindow) {
    NS_WARNING("No inner window available!");
    return nullptr;
  }
  return GetCurrentInnerWindowInternal(this)->GetGroupMessageManager(aGroup);
}

void nsGlobalWindowOuter::InitWasOffline() { mWasOffline = NS_IsOffline(); }

#if defined(_WINDOWS_) && !defined(MOZ_WRAPPED_WINDOWS_H)
#  pragma message( \
      "wrapper failure reason: " MOZ_WINDOWS_WRAPPER_DISABLED_REASON)
#  error "Never include unwrapped windows.h in this file!"
#endif

// Helper called by methods that move/resize the window,
// to ensure the presContext (if any) is aware of resolution
// change that may happen in multi-monitor configuration.
void nsGlobalWindowOuter::CheckForDPIChange() {
  if (mDocShell) {
    RefPtr<nsPresContext> presContext = mDocShell->GetPresContext();
    if (presContext) {
      if (presContext->DeviceContext()->CheckDPIChange()) {
        presContext->UIResolutionChanged();
      }
    }
  }
}

nsresult nsGlobalWindowOuter::Dispatch(
    already_AddRefed<nsIRunnable>&& aRunnable) const {
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  return NS_DispatchToCurrentThread(std::move(aRunnable));
}

nsISerialEventTarget* nsGlobalWindowOuter::SerialEventTarget() const {
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  return GetMainThreadSerialEventTarget();
}

void nsGlobalWindowOuter::MaybeResetWindowName(Document* aNewDocument) {
  MOZ_ASSERT(aNewDocument);

  if (!StaticPrefs::privacy_window_name_update_enabled()) {
    return;
  }

  const LoadingSessionHistoryInfo* info =
      nsDocShell::Cast(mDocShell)->GetLoadingSessionHistoryInfo();
  if (!info || info->mForceMaybeResetName.isNothing()) {
    // We only reset the window name for the top-level content as well as
    // storing in session entries.
    if (!GetBrowsingContext()->IsTopContent()) {
      return;
    }

    // Following implements https://html.spec.whatwg.org/#history-traversal:
    // Step 4.2. Check if the loading document has a different origin than the
    // previous document.

    // We don't need to do anything if we haven't loaded a non-initial document.
    if (!GetBrowsingContext()->GetHasLoadedNonInitialDocument()) {
      return;
    }

    // If we have an existing document, directly check the document prinicpals
    // with the new document to know if it is cross-origin.
    //
    // Note that there will be an issue of initial document handling in Fission
    // when running the WPT unset_context_name-1.html. In the test, the first
    // about:blank page would be loaded with the principal of the testing domain
    // in Fission and the window.name will be set there. Then, The window.name
    // won't be reset after navigating to the testing page because the principal
    // is the same. But, it won't be the case for non-Fission mode that the
    // first about:blank will be loaded with a null principal and the
    // window.name will be reset when loading the test page.
    if (mDoc && mDoc->NodePrincipal()->Equals(aNewDocument->NodePrincipal())) {
      return;
    }

    // If we don't have an existing document, and if it's not the initial
    // about:blank, we could be loading a document because of the
    // process-switching. In this case, this should be a cross-origin
    // navigation.
  } else if (!info->mForceMaybeResetName.ref()) {
    return;
  }

  // Step 4.2.2 Store the window.name into all session history entries that have
  // the same origin as the previous document.
  nsDocShell::Cast(mDocShell)->StoreWindowNameToSHEntries();

  // Step 4.2.3 Clear the window.name if the browsing context is the top-level
  // content and doesn't have an opener.

  // We need to reset the window name in case of a cross-origin navigation,
  // without an opener.
  RefPtr<BrowsingContext> opener = GetOpenerBrowsingContext();
  if (opener) {
    return;
  }

  Unused << mBrowsingContext->SetName(EmptyString());
}

nsGlobalWindowOuter::TemporarilyDisableDialogs::TemporarilyDisableDialogs(
    BrowsingContext* aBC) {
  BrowsingContextGroup* group = aBC->Group();
  if (!group) {
    NS_ERROR(
        "nsGlobalWindowOuter::TemporarilyDisableDialogs called without a "
        "browsing context group?");
    return;
  }

  if (group) {
    mGroup = group;
    mSavedDialogsEnabled = group->GetAreDialogsEnabled();
    group->SetAreDialogsEnabled(false);
  }
}

nsGlobalWindowOuter::TemporarilyDisableDialogs::~TemporarilyDisableDialogs() {
  if (mGroup) {
    mGroup->SetAreDialogsEnabled(mSavedDialogsEnabled);
  }
}

/* static */
already_AddRefed<nsGlobalWindowOuter> nsGlobalWindowOuter::Create(
    nsDocShell* aDocShell, bool aIsChrome) {
  uint64_t outerWindowID = aDocShell->GetOuterWindowID();
  RefPtr<nsGlobalWindowOuter> window = new nsGlobalWindowOuter(outerWindowID);
  if (aIsChrome) {
    window->mIsChrome = true;
  }
  window->SetDocShell(aDocShell);

  window->InitWasOffline();
  return window.forget();
}

nsIURI* nsPIDOMWindowOuter::GetDocumentURI() const {
  return mDoc ? mDoc->GetDocumentURI() : mDocumentURI.get();
}

void nsPIDOMWindowOuter::MaybeCreateDoc() {
  MOZ_ASSERT(!mDoc);
  if (nsIDocShell* docShell = GetDocShell()) {
    // Note that |document| here is the same thing as our mDoc, but we
    // don't have to explicitly set the member variable because the docshell
    // has already called SetNewDocument().
    nsCOMPtr<Document> document = docShell->GetDocument();
    Unused << document;
  }
}

void nsPIDOMWindowOuter::SetChromeEventHandlerInternal(
    EventTarget* aChromeEventHandler) {
  // Out-of-line so we don't need to include ContentFrameMessageManager.h in
  // nsPIDOMWindow.h.
  mChromeEventHandler = aChromeEventHandler;

  // mParentTarget and mMessageManager will be set when the next event is
  // dispatched or someone asks for our message manager.
  mParentTarget = nullptr;
  mMessageManager = nullptr;
}

mozilla::dom::DocGroup* nsPIDOMWindowOuter::GetDocGroup() const {
  Document* doc = GetExtantDoc();
  if (doc) {
    return doc->GetDocGroup();
  }
  return nullptr;
}

nsPIDOMWindowOuter::nsPIDOMWindowOuter(uint64_t aWindowID)
    : mFrameElement(nullptr),
      mModalStateDepth(0),
      mSuppressEventHandlingDepth(0),
      mIsBackground(false),
      mIsRootOuterWindow(false),
      mInnerWindow(nullptr),
      mWindowID(aWindowID),
      mMarkedCCGeneration(0) {}

nsPIDOMWindowOuter::~nsPIDOMWindowOuter() = default;
