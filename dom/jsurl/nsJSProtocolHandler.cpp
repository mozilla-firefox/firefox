/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsJSProtocolHandler.h"

#include "DefaultURI.h"
#include "js/CompilationAndEvaluation.h"
#include "js/Wrapper.h"
#include "jsapi.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/LoadInfo.h"
#include "mozilla/Maybe.h"
#include "mozilla/SourceLocation.h"
#include "mozilla/TextUtils.h"
#include "mozilla/dom/AutoEntryScript.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/DOMSecurityMonitor.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/JSExecutionUtils.h"  // mozilla::dom::Compile, mozilla::dom::EvaluationExceptionToNSResult
#include "mozilla/dom/PolicyContainer.h"
#include "mozilla/dom/PopupBlocker.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/ipc/URIUtils.h"
#include "nsCOMPtr.h"
#include "nsCRT.h"
#include "nsContentSecurityManager.h"
#include "nsContentUtils.h"
#include "nsError.h"
#include "nsEscape.h"
#include "nsGlobalWindowInner.h"
#include "nsIClassInfoImpl.h"
#include "nsIContentSecurityPolicy.h"
#include "nsIDocShell.h"
#include "nsIDocumentViewer.h"
#include "nsIInterfaceRequestor.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIObjectInputStream.h"
#include "nsIObjectOutputStream.h"
#include "nsIPrincipal.h"
#include "nsIScriptChannel.h"
#include "nsIScriptContext.h"
#include "nsIScriptGlobalObject.h"
#include "nsIStreamListener.h"
#include "nsIURI.h"
#include "nsIWebNavigation.h"
#include "nsIWritablePropertyBag2.h"
#include "nsJSUtils.h"
#include "nsNetUtil.h"
#include "nsPIDOMWindow.h"
#include "nsQueryObject.h"
#include "nsReadableUtils.h"
#include "nsSandboxFlags.h"
#include "nsString.h"
#include "nsStringStream.h"
#include "nsTextToSubURI.h"
#include "nsThreadUtils.h"

using mozilla::IsAscii;
using mozilla::Maybe;
using mozilla::dom::AutoEntryScript;
using mozilla::dom::PermissionsPolicyInfo;

static NS_DEFINE_CID(kJSURICID, NS_JSURI_CID);

// A stream class used to handle javascript: URLs.
class JSURLInputStream : public nsIInputStream {
 public:
  JSURLInputStream();

  NS_DECL_THREADSAFE_ISUPPORTS
  NS_FORWARD_SAFE_NSIINPUTSTREAM(mInnerStream)

  nsresult Init(nsIURI* uri);

  // @param aExecutionPolicy `nsIScriptChannel::NO_EXECUTION` or
  //                         `nsIScriptChannel::EXECUTE_NORMAL`.
  nsresult EvaluateScript(
      nsIChannel* aChannel,
      mozilla::dom::PopupBlocker::PopupControlState aPopupState,
      uint32_t aExecutionPolicy, nsPIDOMWindowInner* aOriginalInnerWindow,
      const mozilla::JSCallingLocation& aJSCallingLocation);

 protected:
  virtual ~JSURLInputStream();

  nsCOMPtr<nsIInputStream> mInnerStream;
  nsCString mScript;
  nsCString mURL;
};

//
// nsISupports implementation...
//
NS_IMPL_ISUPPORTS(JSURLInputStream, nsIInputStream)

JSURLInputStream::JSURLInputStream() = default;

JSURLInputStream::~JSURLInputStream() = default;

nsresult JSURLInputStream::Init(nsIURI* uri) {
  NS_ENSURE_ARG_POINTER(uri);

  // Get the script string to evaluate...
  nsresult rv = uri->GetPathQueryRef(mScript);
  if (NS_FAILED(rv)) return rv;

  // Get the url.
  rv = uri->GetSpec(mURL);
  if (NS_FAILED(rv)) return rv;

  return NS_OK;
}

static bool IsISO88591(const nsString& aString) {
  for (nsString::const_char_iterator c = aString.BeginReading(),
                                     c_end = aString.EndReading();
       c < c_end; ++c) {
    if (*c > 255) return false;
  }
  return true;
}

static nsIScriptGlobalObject* GetGlobalObject(nsIChannel* aChannel) {
  // Get the global object owner from the channel
  nsCOMPtr<nsIDocShell> docShell;
  NS_QueryNotificationCallbacks(aChannel, docShell);
  if (!docShell) {
    NS_WARNING("Unable to get a docShell from the channel!");
    return nullptr;
  }

  // So far so good: get the script global from its docshell
  nsIScriptGlobalObject* global = docShell->GetScriptGlobalObject();

  NS_ASSERTION(global,
               "Unable to get an nsIScriptGlobalObject from the "
               "docShell!");
  return global;
}

// https://w3c.github.io/webappsec-csp/#should-block-navigation-request
// specialized for type "navigation" requests with "javascript: URLs".
// Excluding step 2.
static bool AllowedByCSP(nsIContentSecurityPolicy* aCSP,
                         const nsACString& aJavaScriptURL,
                         const mozilla::JSCallingLocation& aJSCallingLocation) {
  if (!aCSP) {
    return true;
  }

  // Step 3. If result is "Allowed", and if navigation request’s current URL’s
  // scheme is javascript:
  //
  // Step 3.1.1.2 If directive’s inline check returns "Allowed" when executed
  // upon null, "navigation" and navigation request’s current URL, skip to the
  // next directive.
  //
  // This means /type/ is "navigation" and /source string/ is the
  // "navigation request’s current URL".
  //
  // Per
  // https://w3c.github.io/webappsec-csp/#effective-directive-for-inline-check
  // type "navigation" maps to the effective directive script-src-elem.
  bool allowsInlineScript = true;

  nsresult rv =
      aCSP->GetAllowsInline(nsIContentSecurityPolicy::SCRIPT_SRC_ELEM_DIRECTIVE,
                            true,     // aHasUnsafeHash
                            u""_ns,   // aNonce
                            true,     // aParserCreated
                            nullptr,  // aElement,
                            nullptr,  // nsICSPEventListener
                            NS_ConvertASCIItoUTF16(aJavaScriptURL),  // aContent
                            aJSCallingLocation.mLine,    // aLineNumber
                            aJSCallingLocation.mColumn,  // aColumnNumber
                            &allowsInlineScript);

  return (NS_SUCCEEDED(rv) && allowsInlineScript);
}

// https://html.spec.whatwg.org/#evaluate-a-javascript:-url
// Steps 7-10.
//
// If the execution result is a string, |aRv| is set to success, and
// |aRetValue| is set to the string.
//
// If the execution result is not a string, |aRv| is set to success, and
// |aRetValue| is set to undefined.
//
// In case of a evaluation failure during the execution, |aRv| is set to the
// corresponding result code and |aRetValue| remains unchanged.
static void ExecScriptAndGetString(JSContext* aCx,
                                   JS::Handle<JSScript*> aScript,
                                   JS::MutableHandle<JS::Value> aRetValue,
                                   mozilla::ErrorResult& aRv) {
  MOZ_ASSERT(aScript);

  // Step 7. Let evaluationStatus be the result of running the classic script
  //         script.
  if (!JS_ExecuteScript(aCx, aScript, aRetValue)) {
    aRv.NoteJSContextException(aCx);
    return;
  }

  // Step 8. Let result be null.
  // Step 9. If evaluationStatus is a normal completion, and
  //         evaluationStatus.[[Value]] is a String, then set result to
  //         evaluationStatus.[[Value]].
  if (aRetValue.isString()) {
    return;
  }

  // Step 10. Otherwise, return null.
  //
  // NOTE: The `null` here is the return value of the entire algorithm.
  //       This function returns `undefined` for all cases and let the caller
  //       handle it.
  aRetValue.setUndefined();
}

nsresult JSURLInputStream::EvaluateScript(
    nsIChannel* aChannel,
    mozilla::dom::PopupBlocker::PopupControlState aPopupState,
    uint32_t aExecutionPolicy, nsPIDOMWindowInner* aOriginalInnerWindow,
    const mozilla::JSCallingLocation& aJSCallingLocation) {
  if (aExecutionPolicy == nsIScriptChannel::NO_EXECUTION) {
    // Nothing to do here.
    return NS_ERROR_DOM_RETVAL_UNDEFINED;
  }

  NS_ENSURE_ARG_POINTER(aChannel);
  MOZ_ASSERT(aOriginalInnerWindow,
             "We should not have gotten here if this was null!");

  // Set the channel's resultPrincipalURI to the active document's URI.  This
  // corresponds to treating that URI as the URI of our channel's response.  In
  // the spec we're supposed to use the URL of the active document, but since
  // we bail out of here if the inner window has changed, and GetDocumentURI()
  // on the inner window returns the URL of the active document if the inner
  // window is current, this is equivalent to the spec behavior.
  nsCOMPtr<nsIURI> docURI = aOriginalInnerWindow->GetDocumentURI();
  if (!docURI) {
    // We're not going to be able to have a sane URL, so just don't run the
    // script at all.
    return NS_ERROR_DOM_RETVAL_UNDEFINED;
  }
  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
  loadInfo->SetResultPrincipalURI(docURI);

#ifdef DEBUG
  DOMSecurityMonitor::AuditUseOfJavaScriptURI(aChannel);
#endif

  // Get principal of code for execution
  nsCOMPtr<nsISupports> owner;
  aChannel->GetOwner(getter_AddRefs(owner));
  nsCOMPtr<nsIPrincipal> principal = do_QueryInterface(owner);
  if (!principal) {
    if (loadInfo->GetForceInheritPrincipal()) {
      principal = loadInfo->FindPrincipalToInherit(aChannel);
    } else {
      // No execution without a principal!
      NS_ASSERTION(!owner, "Non-principal owner?");
      NS_WARNING("No principal to execute JS with");
      return NS_ERROR_DOM_RETVAL_UNDEFINED;
    }
  }

  nsresult rv;

  // CSP check: javascript: URIs are disabled unless "inline" scripts
  // are allowed by both the CSP of the thing that started the load
  // (which is the CSPToInherit of the loadinfo) and the CSP of the
  // target document.  The target document check is performed below,
  // once we have determined the target document.
  nsCOMPtr<nsIPolicyContainer> policyContainer =
      loadInfo->GetPolicyContainerToInherit();
  nsCOMPtr<nsIContentSecurityPolicy> csp =
      PolicyContainer::GetCSP(policyContainer);

  if (!AllowedByCSP(csp, mURL, aJSCallingLocation)) {
    return NS_ERROR_DOM_RETVAL_UNDEFINED;
  }

  // Get the global object we should be running on.
  nsIScriptGlobalObject* global = GetGlobalObject(aChannel);
  if (!global) {
    return NS_ERROR_FAILURE;
  }

  // Make sure we still have the same inner window as we used to.
  nsCOMPtr<nsPIDOMWindowOuter> win = do_QueryInterface(global);
  nsPIDOMWindowInner* innerWin = win->GetCurrentInnerWindow();

  if (innerWin != aOriginalInnerWindow) {
    return NS_ERROR_UNEXPECTED;
  }

  mozilla::dom::Document* targetDoc = innerWin->GetExtantDoc();

  // Sandboxed document check: javascript: URI execution is disabled in a
  // sandboxed document unless 'allow-scripts' was specified.
  if ((targetDoc && !targetDoc->IsScriptEnabled()) ||
      (loadInfo->GetTriggeringSandboxFlags() & SANDBOXED_SCRIPTS)) {
    if (nsCOMPtr<nsIObserverService> obs =
            mozilla::services::GetObserverService()) {
      obs->NotifyWhenScriptSafe(ToSupports(innerWin),
                                "javascript-uri-blocked-by-sandbox");
    }
    return NS_ERROR_DOM_RETVAL_UNDEFINED;
  }

  if (targetDoc) {
    // Perform a Security check against the CSP of the document we are
    // running against. javascript: URIs are disabled unless "inline"
    // scripts are allowed. We only do that if targetDoc->NodePrincipal()
    // subsumes loadInfo->TriggeringPrincipal(). If it doesn't, then
    // someone more-privileged (our UI or an extension) started the
    // load and hence the load should not be subject to the target
    // document's CSP.
    //
    // The "more privileged" assumption is safe, because if the triggering
    // principal does not subsume targetDoc->NodePrincipal() we won't run the
    // script at all.  More precisely, we test that "principal" subsumes the
    // target's principal, but "principal" should never be higher-privilege
    // than the triggering principal here: it's either the triggering
    // principal, or the principal of the document we started the load
    // against if the triggering principal is system.
    if (targetDoc->NodePrincipal()->Subsumes(loadInfo->TriggeringPrincipal())) {
      nsCOMPtr<nsIContentSecurityPolicy> targetCSP =
          PolicyContainer::GetCSP(targetDoc->GetPolicyContainer());
      if (!AllowedByCSP(targetCSP, mURL, aJSCallingLocation)) {
        return NS_ERROR_DOM_RETVAL_UNDEFINED;
      }
    }

    // https://html.spec.whatwg.org/#evaluate-a-javascript:-url
    // Step 12. Let policyContainer be targetNavigable's active document's
    //          policy container.
    //
    // A document created from the string this script evaluates to inherits the
    // target document's policy container, not the initiating document's one
    // that docshell stored on the loadinfo for the check above. Store an actual
    // copy so that modifications done by the new document (such as its meta
    // CSP) don't propagate back into the target document.
    RefPtr policyContainerToInherit = mozilla::MakeRefPtr<PolicyContainer>();
    policyContainerToInherit->InitFromOther(
        PolicyContainer::Cast(targetDoc->GetPolicyContainer()));
    loadInfo->SetPolicyContainerToInherit(policyContainerToInherit);

    // If the original channel's nsILoadInfo has a PermissionsPolicyInfo,
    // copy it to the new channel.
    if (nsCOMPtr<nsIChannel> originalChannel = targetDoc->GetChannel()) {
      nsCOMPtr<nsILoadInfo> originalLoadInfo = originalChannel->LoadInfo();
      if (Maybe<PermissionsPolicyInfo> containerPolicy =
              originalLoadInfo->GetContainerPermissionsPolicyInfo()) {
        loadInfo->SetContainerPermissionsPolicyInfo(*containerPolicy);
      }
    }
  }

  // Push our popup control state
  AutoPopupStatePusher popupStatePusher(aPopupState);

  nsCOMPtr<nsIScriptGlobalObject> innerGlobal = do_QueryInterface(innerWin);

  // So far so good: get the script context from its owner.
  nsCOMPtr<nsIScriptContext> scriptContext = global->GetContext();
  if (!scriptContext) return NS_ERROR_FAILURE;

  // New script entry point required, due to the "Create a script" step of
  // http://www.whatwg.org/specs/web-apps/current-work/#javascript-protocol
  mozilla::nsAutoMicroTask mt;
  AutoEntryScript aes(innerGlobal, "javascript: URI", true);
  // Bug 2072122: Trusted Types readiness experiment only, to be removed.
  if (mozilla::dom::Document* doc = innerWin->GetExtantDoc()) {
    doc->SetTrustedTypesReadinessSignal(
        mozilla::dom::TrustedTypesReadinessSignal::RanScript);
  }
  JSContext* cx = aes.cx();
  JS::Rooted<JSObject*> globalJSObject(cx, innerGlobal->GetGlobalJSObject());
  NS_ENSURE_TRUE(globalJSObject, NS_ERROR_UNEXPECTED);

  //-- Don't execute unless the script principal subsumes the
  //   principal of the context.
  nsIPrincipal* objectPrincipal =
      nsContentUtils::ObjectPrincipal(globalJSObject);

  bool subsumes;
  rv = principal->Subsumes(objectPrincipal, &subsumes);
  if (NS_FAILED(rv)) return rv;

  if (!subsumes) {
    return NS_ERROR_DOM_RETVAL_UNDEFINED;
  }

  // Fail if someone tries to execute in a global with system principal.
  if (objectPrincipal->IsSystemPrincipal()) {
    return NS_ERROR_DOM_SECURITY_ERR;
  }

  nsAutoCString script(mScript);
  // Unescape the script
  NS_UnescapeURL(script);

  JS::Rooted<JS::Value> v(cx, JS::UndefinedValue());
  // Finally, we have everything needed to evaluate the expression.
  JS::CompileOptions options(cx);
  options.setFileAndLine(mURL.get(), 1);
  options.setIntroductionType("javascriptURL");
  {
    mozilla::ErrorResult erv;
    if (MOZ_LIKELY(xpc::Scriptability::Get(globalJSObject).Allowed())) {
      mozilla::AutoProfilerLabel autoProfilerLabel(
          "JSExecutionContext",
          /* dynamicStr */ nullptr, JS::ProfilingCategoryPair::JS);
      JSAutoRealm autoRealm(cx, globalJSObject);
      RefPtr<JS::Stencil> stencil;
      JS::Rooted<JSScript*> compiledScript(cx);
      mozilla::dom::Compile(cx, options, NS_ConvertUTF8toUTF16(script), stencil,
                            erv);
      if (stencil) {
        JS::InstantiateOptions instantiateOptions(options);
        MOZ_ASSERT(!instantiateOptions.deferDebugMetadata);
        compiledScript.set(JS::InstantiateGlobalStencil(
            aes.cx(), instantiateOptions, stencil, /* storage */ nullptr));
        if (!compiledScript) {
          erv.NoteJSContextException(aes.cx());
        }
      }

      if (!erv.Failed()) {
        MOZ_ASSERT(!options.noScriptRval);
        ExecScriptAndGetString(cx, compiledScript, &v, erv);
      }
    }
    rv = mozilla::dom::EvaluationExceptionToNSResult(erv);
  }

  js::AssertSameCompartment(cx, v);
  MOZ_ASSERT(v.isString() || v.isUndefined());

  if (NS_FAILED(rv)) {
    return NS_ERROR_MALFORMED_URI;
  }
  if (v.isUndefined()) {
    return NS_ERROR_DOM_RETVAL_UNDEFINED;
  }
  MOZ_ASSERT(rv != NS_SUCCESS_DOM_SCRIPT_EVALUATION_THREW,
             "How did we get a non-undefined return value?");
  nsAutoJSString result;
  if (!result.init(cx, v)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  char* bytes;
  uint32_t bytesLen;
  constexpr auto isoCharset = "windows-1252"_ns;
  constexpr auto utf8Charset = "UTF-8"_ns;
  const nsLiteralCString* charset;
  if (IsISO88591(result)) {
    // For compatibility, if the result is ISO-8859-1, we use
    // windows-1252, so that people can compatibly create images
    // using javascript: URLs.
    bytes = ToNewCString(result, mozilla::fallible);
    bytesLen = result.Length();
    charset = &isoCharset;
  } else {
    bytes = ToNewUTF8String(result, &bytesLen);
    charset = &utf8Charset;
  }
  aChannel->SetContentCharset(*charset);
  if (bytes) {
    rv = NS_NewByteInputStream(getter_AddRefs(mInnerStream),
                               mozilla::Span(bytes, bytesLen),
                               NS_ASSIGNMENT_ADOPT);
  } else {
    rv = NS_ERROR_OUT_OF_MEMORY;
  }

  return rv;
}

////////////////////////////////////////////////////////////////////////////////

class nsJSChannel : public nsIChannel,
                    public nsIStreamListener,
                    public nsIScriptChannel,
                    public nsIPropertyBag2 {
 public:
  nsJSChannel();

  NS_DECL_ISUPPORTS
  NS_DECL_NSIREQUEST
  NS_DECL_NSICHANNEL
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_NSISTREAMLISTENER
  NS_DECL_NSISCRIPTCHANNEL
  NS_FORWARD_SAFE_NSIPROPERTYBAG(mPropertyBag)
  NS_FORWARD_SAFE_NSIPROPERTYBAG2(mPropertyBag)

  nsresult Init(nsIURI* aURI, nsILoadInfo* aLoadInfo);

  // Actually evaluate the script.
  void EvaluateScript();

 protected:
  virtual ~nsJSChannel();

  nsresult StopAll();

  void NotifyListener();

  void CleanupStrongRefs();

  nsCOMPtr<nsIChannel> mStreamChannel;
  nsCOMPtr<nsIPropertyBag2> mPropertyBag;
  nsCOMPtr<nsIStreamListener> mListener;              // Our final listener
  nsCOMPtr<nsPIDOMWindowInner> mOriginalInnerWindow;  // The inner window our
                                                      // load started against.
  // If we blocked onload on a document in AsyncOpen, this is the document we
  // did it on.
  RefPtr<mozilla::dom::Document> mDocumentOnloadBlockedOn;

  nsresult mStatus;  // Our status

  nsLoadFlags mLoadFlags;
  nsLoadFlags mActualLoadFlags;  // See AsyncOpen

  RefPtr<JSURLInputStream> mJSURIStream;
  mozilla::dom::PopupBlocker::PopupControlState mPopupState;
  mozilla::JSCallingLocation mJSCallingLocation;
  uint32_t mExecutionPolicy;
  bool mIsAsync;
  bool mIsActive;
  bool mOpenedStreamChannel;
};

nsJSChannel::nsJSChannel()
    : mStatus(NS_OK),
      mLoadFlags(LOAD_NORMAL),
      mActualLoadFlags(LOAD_NORMAL),
      mPopupState(mozilla::dom::PopupBlocker::openOverridden),
      mExecutionPolicy(NO_EXECUTION),
      mIsAsync(true),
      mIsActive(false),
      mOpenedStreamChannel(false) {}

nsJSChannel::~nsJSChannel() = default;

nsresult nsJSChannel::StopAll() {
  nsresult rv = NS_ERROR_UNEXPECTED;
  nsCOMPtr<nsIWebNavigation> webNav;
  NS_QueryNotificationCallbacks(mStreamChannel, webNav);

  NS_ASSERTION(webNav, "Can't get nsIWebNavigation from channel!");
  if (webNav) {
    rv = webNav->Stop(nsIWebNavigation::STOP_ALL);
  }

  return rv;
}

nsresult nsJSChannel::Init(nsIURI* aURI, nsILoadInfo* aLoadInfo) {
  RefPtr<nsJSURI> jsURI;
  nsresult rv = aURI->QueryInterface(kJSURICID, getter_AddRefs(jsURI));
  NS_ENSURE_SUCCESS(rv, rv);

  // Defensively mark the current process as untrusted.
  mozilla::dom::ContentChild::MaybeBecomeUntrusted();

  // Create the nsIStreamIO layer used by the nsIStreamIOChannel.
  mJSURIStream = new JSURLInputStream();

  // Create a stock input stream channel...
  // Remember, until AsyncOpen is called, the script will not be evaluated
  // and the underlying Input Stream will not be created...
  nsCOMPtr<nsIChannel> channel;
  RefPtr<JSURLInputStream> jsURIStream = mJSURIStream;
  rv = NS_NewInputStreamChannelInternal(getter_AddRefs(channel), aURI,
                                        jsURIStream.forget(), "text/html"_ns,
                                        ""_ns, aLoadInfo);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mJSURIStream->Init(aURI);
  if (NS_SUCCEEDED(rv)) {
    mStreamChannel = channel;
    mPropertyBag = do_QueryInterface(channel);
    nsCOMPtr<nsIWritablePropertyBag2> writableBag = do_QueryInterface(channel);
    if (writableBag && jsURI->GetBaseURI()) {
      writableBag->SetPropertyAsInterface(u"baseURI"_ns, jsURI->GetBaseURI());
    }
  }

  return rv;
}

NS_IMETHODIMP
nsJSChannel::GetIsDocument(bool* aIsDocument) {
  return NS_GetIsDocumentChannel(this, aIsDocument);
}

//
// nsISupports implementation...
//

NS_IMPL_ISUPPORTS(nsJSChannel, nsIChannel, nsIRequest, nsIRequestObserver,
                  nsIStreamListener, nsIScriptChannel, nsIPropertyBag,
                  nsIPropertyBag2)

//
// nsIRequest implementation...
//

NS_IMETHODIMP
nsJSChannel::GetName(nsACString& aResult) {
  return mStreamChannel->GetName(aResult);
}

NS_IMETHODIMP
nsJSChannel::IsPending(bool* aResult) {
  *aResult = mIsActive;
  return NS_OK;
}

NS_IMETHODIMP
nsJSChannel::GetStatus(nsresult* aResult) {
  if (NS_SUCCEEDED(mStatus) && mOpenedStreamChannel) {
    return mStreamChannel->GetStatus(aResult);
  }

  *aResult = mStatus;

  return NS_OK;
}

NS_IMETHODIMP nsJSChannel::SetCanceledReason(const nsACString& aReason) {
  return SetCanceledReasonImpl(aReason);
}

NS_IMETHODIMP nsJSChannel::GetCanceledReason(nsACString& aReason) {
  return GetCanceledReasonImpl(aReason);
}

NS_IMETHODIMP nsJSChannel::CancelWithReason(nsresult aStatus,
                                            const nsACString& aReason) {
  return CancelWithReasonImpl(aStatus, aReason);
}

NS_IMETHODIMP
nsJSChannel::Cancel(nsresult aStatus) {
  mStatus = aStatus;

  if (mOpenedStreamChannel) {
    mStreamChannel->Cancel(aStatus);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsJSChannel::GetCanceled(bool* aCanceled) {
  nsresult status = NS_ERROR_FAILURE;
  GetStatus(&status);
  *aCanceled = NS_FAILED(status);
  return NS_OK;
}

NS_IMETHODIMP
nsJSChannel::Suspend() { return mStreamChannel->Suspend(); }

NS_IMETHODIMP
nsJSChannel::Resume() { return mStreamChannel->Resume(); }

//
// nsIChannel implementation
//

NS_IMETHODIMP
nsJSChannel::GetOriginalURI(nsIURI** aURI) {
  return mStreamChannel->GetOriginalURI(aURI);
}

NS_IMETHODIMP
nsJSChannel::SetOriginalURI(nsIURI* aURI) {
  return mStreamChannel->SetOriginalURI(aURI);
}

NS_IMETHODIMP
nsJSChannel::GetURI(nsIURI** aURI) { return mStreamChannel->GetURI(aURI); }

NS_IMETHODIMP
nsJSChannel::Open(nsIInputStream** aStream) {
  nsCOMPtr<nsIStreamListener> listener;
  nsresult rv =
      nsContentSecurityManager::doContentSecurityCheck(this, listener);
  NS_ENSURE_SUCCESS(rv, rv);

  mJSCallingLocation = mozilla::JSCallingLocation::Get();
  rv = mJSURIStream->EvaluateScript(mStreamChannel, mPopupState,
                                    mExecutionPolicy, mOriginalInnerWindow,
                                    mJSCallingLocation);
  NS_ENSURE_SUCCESS(rv, rv);

  return mStreamChannel->Open(aStream);
}

NS_IMETHODIMP
nsJSChannel::AsyncOpen(nsIStreamListener* aListener) {
  NS_ENSURE_ARG(aListener);

  nsCOMPtr<nsIStreamListener> listener = aListener;
  nsresult rv =
      nsContentSecurityManager::doContentSecurityCheck(this, listener);
  NS_ENSURE_SUCCESS(rv, rv);

#ifdef DEBUG
  {
    nsCOMPtr<nsILoadInfo> loadInfo = nsIChannel::LoadInfo();
    MOZ_ASSERT(!loadInfo || loadInfo->GetSecurityMode() == 0 ||
                   loadInfo->GetInitialSecurityCheckDone(),
               "security flags in loadInfo but asyncOpen() not called");
  }
#endif

  // First make sure that we have a usable inner window; we'll want to make
  // sure that we execute against that inner and no other.
  nsIScriptGlobalObject* global = GetGlobalObject(this);
  if (!global) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsCOMPtr<nsPIDOMWindowOuter> win(do_QueryInterface(global));
  NS_ASSERTION(win, "Our global is not a window??");

  // Make sure we create a new inner window if one doesn't already exist (see
  // bug 306630).
  mOriginalInnerWindow = win->EnsureInnerWindow();
  if (!mOriginalInnerWindow) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  mListener = aListener;

  mIsActive = true;

  // Temporarily set the LOAD_BACKGROUND flag to suppress load group observer
  // notifications (and hence nsIWebProgressListener notifications) from
  // being dispatched.  This is required since we suppress LOAD_DOCUMENT_URI,
  // which means that the DocLoader would not generate document start and
  // stop notifications (see bug 257875).
  mActualLoadFlags = mLoadFlags;
  mLoadFlags |= LOAD_BACKGROUND;

  // Add the javascript channel to its loadgroup so that we know if
  // network loads were canceled or not...
  nsCOMPtr<nsILoadGroup> loadGroup;
  mStreamChannel->GetLoadGroup(getter_AddRefs(loadGroup));
  if (loadGroup) {
    rv = loadGroup->AddRequest(this, nullptr);
    if (NS_FAILED(rv)) {
      mIsActive = false;
      CleanupStrongRefs();
      return rv;
    }
  }

  mDocumentOnloadBlockedOn = mOriginalInnerWindow->GetExtantDoc();
  if (mDocumentOnloadBlockedOn) {
    // If we're a document channel, we need to actually block onload on our
    // _parent_ document.  This is because we don't actually set our
    // LOAD_DOCUMENT_URI flag, so a docloader we're loading in as the
    // document channel will claim to not be busy, and our parent's onload
    // could fire too early.
    nsLoadFlags loadFlags;
    mStreamChannel->GetLoadFlags(&loadFlags);
    if (loadFlags & LOAD_DOCUMENT_URI) {
      mDocumentOnloadBlockedOn =
          mDocumentOnloadBlockedOn->GetInProcessParentDocument();
    }
  }
  if (mDocumentOnloadBlockedOn) {
    mDocumentOnloadBlockedOn->BlockOnload();
  }

  mPopupState = mozilla::dom::PopupBlocker::GetPopupControlState();

  void (nsJSChannel::*method)();
  const char* name;

  // The calling location is unavailable in the runnable, hence getting it here.
  mJSCallingLocation = mozilla::JSCallingLocation::Get();

  if (mIsAsync) {
    // post an event to do the rest
    method = &nsJSChannel::EvaluateScript;
    name = "nsJSChannel::EvaluateScript";
  } else {
    EvaluateScript();
    if (mOpenedStreamChannel) {
      // That will handle notifying things
      return NS_OK;
    }

    NS_ASSERTION(NS_FAILED(mStatus), "We should have failed _somehow_");

    // mStatus is going to be NS_ERROR_DOM_RETVAL_UNDEFINED if we didn't
    // have any content resulting from the execution and NS_BINDING_ABORTED
    // if something we did causes our own load to be stopped.  Return
    // success in those cases, and error out in all others.
    if (mStatus != NS_ERROR_DOM_RETVAL_UNDEFINED &&
        mStatus != NS_BINDING_ABORTED) {
      // Note that calling EvaluateScript() handled removing us from the
      // loadgroup and marking us as not active anymore.
      CleanupStrongRefs();
      return mStatus;
    }

    // We're returning success from asyncOpen(), but we didn't open a
    // stream channel.  We'll have to notify ourselves, but make sure to do
    // it asynchronously.
    method = &nsJSChannel::NotifyListener;
    name = "nsJSChannel::NotifyListener";
  }

  nsCOMPtr<nsIRunnable> runnable =
      mozilla::NewRunnableMethod(name, this, method);
  nsGlobalWindowInner* window = nsGlobalWindowInner::Cast(mOriginalInnerWindow);
  rv = window->Dispatch(runnable.forget());

  if (NS_FAILED(rv)) {
    loadGroup->RemoveRequest(this, nullptr, rv);
    mIsActive = false;
    CleanupStrongRefs();
  }
  return rv;
}

void nsJSChannel::EvaluateScript() {
  // Synchronously execute the script...
  // mIsActive is used to indicate the the request is 'busy' during the
  // the script evaluation phase.  This means that IsPending() will
  // indicate the the request is busy while the script is executing...

  // Note that we want to be in the loadgroup and pending while we evaluate
  // the script, so that we find out if the loadgroup gets canceled by the
  // script execution (in which case we shouldn't pump out data even if the
  // script returns it).

  if (NS_SUCCEEDED(mStatus)) {
    nsresult rv = mJSURIStream->EvaluateScript(
        mStreamChannel, mPopupState, mExecutionPolicy, mOriginalInnerWindow,
        mJSCallingLocation);

    // Note that evaluation may have canceled us, so recheck mStatus again
    if (NS_FAILED(rv) && NS_SUCCEEDED(mStatus)) {
      mStatus = rv;
    }
  }

  // Remove the javascript channel from its loadgroup...
  nsCOMPtr<nsILoadGroup> loadGroup;
  mStreamChannel->GetLoadGroup(getter_AddRefs(loadGroup));
  if (loadGroup) {
    loadGroup->RemoveRequest(this, nullptr, mStatus);
  }

  // Reset load flags to their original value...
  mLoadFlags = mActualLoadFlags;

  // We're no longer active, it's now up to the stream channel to do
  // the loading, if needed.
  mIsActive = false;

  if (NS_FAILED(mStatus)) {
    if (mIsAsync) {
      NotifyListener();
    }
    return;
  }

  // EvaluateScript() succeeded, and we were not canceled, that
  // means there's data to parse as a result of evaluating the
  // script.

  // Get the stream channels load flags (!= mLoadFlags).
  nsLoadFlags loadFlags;
  mStreamChannel->GetLoadFlags(&loadFlags);

  uint32_t disposition;
  if (NS_FAILED(mStreamChannel->GetContentDisposition(&disposition)))
    disposition = nsIChannel::DISPOSITION_INLINE;
  if (loadFlags & LOAD_DOCUMENT_URI &&
      disposition != nsIChannel::DISPOSITION_ATTACHMENT) {
    // We're loaded as the document channel and not expecting to download
    // the result. If we go on, we'll blow away the current document. Make
    // sure that's ok. If so, stop all pending network loads.

    nsCOMPtr<nsIDocShell> docShell;
    NS_QueryNotificationCallbacks(mStreamChannel, docShell);
    if (docShell) {
      nsCOMPtr<nsIDocumentViewer> viewer;
      docShell->GetDocViewer(getter_AddRefs(viewer));

      if (viewer) {
        bool okToUnload;

        if (NS_SUCCEEDED(viewer->PermitUnload(&okToUnload)) && !okToUnload) {
          // The user didn't want to unload the current
          // page, translate this into an undefined
          // return from the javascript: URL...
          mStatus = NS_ERROR_DOM_RETVAL_UNDEFINED;
        }
        // Note: `docShell` may have been destroyed in `PermitUnload`, so don't
        // add uses of `docShell` later in this method!
      }
    }

    if (NS_SUCCEEDED(mStatus)) {
      mStatus = StopAll();
    }
  }

  if (NS_FAILED(mStatus)) {
    if (mIsAsync) {
      NotifyListener();
    }
    return;
  }

  mStatus = mStreamChannel->AsyncOpen(this);
  if (NS_SUCCEEDED(mStatus)) {
    // mStreamChannel will call OnStartRequest and OnStopRequest on
    // us, so we'll be sure to call them on our listener.
    mOpenedStreamChannel = true;

    // Now readd ourselves to the loadgroup so we can receive
    // cancellation notifications.
    mIsActive = true;
    if (loadGroup) {
      mStatus = loadGroup->AddRequest(this, nullptr);

      // If AddRequest failed, that's OK.  The key is to make sure we get
      // cancelled if needed, and that call just canceled us if it
      // failed.  We'll still get notified by the stream channel when it
      // finishes.
    }

  } else if (mIsAsync) {
    NotifyListener();
  }
}

void nsJSChannel::NotifyListener() {
  nsCOMPtr<nsIStreamListener> listener = mListener;
  listener->OnStartRequest(this);
  listener->OnStopRequest(this, mStatus);

  CleanupStrongRefs();
}

void nsJSChannel::CleanupStrongRefs() {
  mListener = nullptr;
  mOriginalInnerWindow = nullptr;
  if (mDocumentOnloadBlockedOn) {
    mDocumentOnloadBlockedOn->UnblockOnload(false);
    mDocumentOnloadBlockedOn = nullptr;
  }
}

NS_IMETHODIMP
nsJSChannel::GetLoadFlags(nsLoadFlags* aLoadFlags) {
  *aLoadFlags = mLoadFlags;

  return NS_OK;
}

NS_IMETHODIMP
nsJSChannel::SetLoadFlags(nsLoadFlags aLoadFlags) {
  // Figure out whether the LOAD_BACKGROUND bit in aLoadFlags is
  // actually right.
  bool bogusLoadBackground = false;
  if (mIsActive && !(mActualLoadFlags & LOAD_BACKGROUND) &&
      (aLoadFlags & LOAD_BACKGROUND)) {
    // We're getting a LOAD_BACKGROUND, but it's probably just our own fake
    // flag being mirrored to us.  The one exception is if our loadgroup is
    // LOAD_BACKGROUND.
    bool loadGroupIsBackground = false;
    nsCOMPtr<nsILoadGroup> loadGroup;
    mStreamChannel->GetLoadGroup(getter_AddRefs(loadGroup));
    if (loadGroup) {
      nsLoadFlags loadGroupFlags;
      loadGroup->GetLoadFlags(&loadGroupFlags);
      loadGroupIsBackground = ((loadGroupFlags & LOAD_BACKGROUND) != 0);
    }
    bogusLoadBackground = !loadGroupIsBackground;
  }

  // Since the javascript channel is never the actual channel that
  // any data is loaded through, don't ever set the
  // LOAD_DOCUMENT_URI flag on it, since that could lead to two
  // 'document channels' in the loadgroup if a javascript: URL is
  // loaded while a document is being loaded in the same window.

  // XXXbz this, and a whole lot of other hackery, could go away if we'd just
  // cancel the current document load on javascript: load start like IE does.

  mLoadFlags = aLoadFlags & ~LOAD_DOCUMENT_URI;

  if (bogusLoadBackground) {
    aLoadFlags = aLoadFlags & ~LOAD_BACKGROUND;
  }

  mActualLoadFlags = aLoadFlags;

  // ... but the underlying stream channel should get this bit, if
  // set, since that'll be the real document channel if the
  // javascript: URL generated data.

  return mStreamChannel->SetLoadFlags(aLoadFlags);
}

NS_IMETHODIMP
nsJSChannel::GetTRRMode(nsIRequest::TRRMode* aTRRMode) {
  return GetTRRModeImpl(aTRRMode);
}

NS_IMETHODIMP
nsJSChannel::SetTRRMode(nsIRequest::TRRMode aTRRMode) {
  return SetTRRModeImpl(aTRRMode);
}

NS_IMETHODIMP
nsJSChannel::GetLoadGroup(nsILoadGroup** aLoadGroup) {
  return mStreamChannel->GetLoadGroup(aLoadGroup);
}

NS_IMETHODIMP
nsJSChannel::SetLoadGroup(nsILoadGroup* aLoadGroup) {
  if (aLoadGroup) {
    bool streamPending;
    nsresult rv = mStreamChannel->IsPending(&streamPending);
    NS_ENSURE_SUCCESS(rv, rv);

    if (streamPending) {
      nsCOMPtr<nsILoadGroup> curLoadGroup;
      mStreamChannel->GetLoadGroup(getter_AddRefs(curLoadGroup));

      if (aLoadGroup != curLoadGroup) {
        // Move the stream channel to our new loadgroup.  Make sure to
        // add it before removing it, so that we don't trigger onload
        // by accident.
        aLoadGroup->AddRequest(mStreamChannel, nullptr);
        if (curLoadGroup) {
          curLoadGroup->RemoveRequest(mStreamChannel, nullptr,
                                      NS_BINDING_RETARGETED);
        }
      }
    }
  }

  return mStreamChannel->SetLoadGroup(aLoadGroup);
}

NS_IMETHODIMP
nsJSChannel::GetOwner(nsISupports** aOwner) {
  return mStreamChannel->GetOwner(aOwner);
}

NS_IMETHODIMP
nsJSChannel::SetOwner(nsISupports* aOwner) {
  return mStreamChannel->SetOwner(aOwner);
}

NS_IMETHODIMP
nsJSChannel::GetLoadInfo(nsILoadInfo** aLoadInfo) {
  return mStreamChannel->GetLoadInfo(aLoadInfo);
}

NS_IMETHODIMP
nsJSChannel::SetLoadInfo(nsILoadInfo* aLoadInfo) {
  MOZ_RELEASE_ASSERT(aLoadInfo, "loadinfo can't be null");
  return mStreamChannel->SetLoadInfo(aLoadInfo);
}

NS_IMETHODIMP
nsJSChannel::GetParentProcessChannelHandle(
    mozilla::dom::ParentProcessChannelHandle** aValue) {
  return mStreamChannel->GetParentProcessChannelHandle(aValue);
}

NS_IMETHODIMP
nsJSChannel::SetParentProcessChannelHandle(
    mozilla::dom::ParentProcessChannelHandle* aValue) {
  return mStreamChannel->SetParentProcessChannelHandle(aValue);
}

NS_IMETHODIMP
nsJSChannel::GetNotificationCallbacks(nsIInterfaceRequestor** aCallbacks) {
  return mStreamChannel->GetNotificationCallbacks(aCallbacks);
}

NS_IMETHODIMP
nsJSChannel::SetNotificationCallbacks(nsIInterfaceRequestor* aCallbacks) {
  return mStreamChannel->SetNotificationCallbacks(aCallbacks);
}

NS_IMETHODIMP
nsJSChannel::GetSecurityInfo(nsITransportSecurityInfo** aSecurityInfo) {
  return mStreamChannel->GetSecurityInfo(aSecurityInfo);
}

NS_IMETHODIMP
nsJSChannel::GetContentType(nsACString& aContentType) {
  return mStreamChannel->GetContentType(aContentType);
}

NS_IMETHODIMP
nsJSChannel::SetContentType(const nsACString& aContentType) {
  return mStreamChannel->SetContentType(aContentType);
}

NS_IMETHODIMP
nsJSChannel::GetContentCharset(nsACString& aContentCharset) {
  return mStreamChannel->GetContentCharset(aContentCharset);
}

NS_IMETHODIMP
nsJSChannel::SetContentCharset(const nsACString& aContentCharset) {
  return mStreamChannel->SetContentCharset(aContentCharset);
}

NS_IMETHODIMP
nsJSChannel::GetContentDisposition(uint32_t* aContentDisposition) {
  return mStreamChannel->GetContentDisposition(aContentDisposition);
}

NS_IMETHODIMP
nsJSChannel::SetContentDisposition(uint32_t aContentDisposition) {
  return mStreamChannel->SetContentDisposition(aContentDisposition);
}

NS_IMETHODIMP
nsJSChannel::GetContentDispositionFilename(
    nsAString& aContentDispositionFilename) {
  return mStreamChannel->GetContentDispositionFilename(
      aContentDispositionFilename);
}

NS_IMETHODIMP
nsJSChannel::SetContentDispositionFilename(
    const nsAString& aContentDispositionFilename) {
  return mStreamChannel->SetContentDispositionFilename(
      aContentDispositionFilename);
}

NS_IMETHODIMP
nsJSChannel::GetContentDispositionHeader(
    nsACString& aContentDispositionHeader) {
  return mStreamChannel->GetContentDispositionHeader(aContentDispositionHeader);
}

NS_IMETHODIMP
nsJSChannel::GetContentLength(int64_t* aContentLength) {
  return mStreamChannel->GetContentLength(aContentLength);
}

NS_IMETHODIMP
nsJSChannel::SetContentLength(int64_t aContentLength) {
  return mStreamChannel->SetContentLength(aContentLength);
}

NS_IMETHODIMP
nsJSChannel::OnStartRequest(nsIRequest* aRequest) {
  NS_ENSURE_TRUE(aRequest == mStreamChannel, NS_ERROR_UNEXPECTED);

  nsCOMPtr<nsIStreamListener> listener = mListener;
  return listener->OnStartRequest(this);
}

NS_IMETHODIMP
nsJSChannel::OnDataAvailable(nsIRequest* aRequest, nsIInputStream* aInputStream,
                             uint64_t aOffset, uint32_t aCount) {
  NS_ENSURE_TRUE(aRequest == mStreamChannel, NS_ERROR_UNEXPECTED);

  nsCOMPtr<nsIStreamListener> listener = mListener;
  return listener->OnDataAvailable(this, aInputStream, aOffset, aCount);
}

NS_IMETHODIMP
nsJSChannel::OnStopRequest(nsIRequest* aRequest, nsresult aStatus) {
  NS_ENSURE_TRUE(aRequest == mStreamChannel, NS_ERROR_UNEXPECTED);

  nsCOMPtr<nsIStreamListener> listener = mListener;

  CleanupStrongRefs();

  // Make sure aStatus matches what GetStatus() returns
  if (NS_FAILED(mStatus)) {
    aStatus = mStatus;
  }

  nsresult rv = listener->OnStopRequest(this, aStatus);

  nsCOMPtr<nsILoadGroup> loadGroup;
  mStreamChannel->GetLoadGroup(getter_AddRefs(loadGroup));
  if (loadGroup) {
    loadGroup->RemoveRequest(this, nullptr, mStatus);
  }

  mIsActive = false;

  return rv;
}

NS_IMETHODIMP
nsJSChannel::SetExecutionPolicy(uint32_t aPolicy) {
  NS_ENSURE_ARG(aPolicy <= EXECUTE_NORMAL);

  mExecutionPolicy = aPolicy;
  return NS_OK;
}

NS_IMETHODIMP
nsJSChannel::GetExecutionPolicy(uint32_t* aPolicy) {
  *aPolicy = mExecutionPolicy;
  return NS_OK;
}

NS_IMETHODIMP
nsJSChannel::SetExecuteAsync(bool aIsAsync) {
  if (!mIsActive) {
    mIsAsync = aIsAsync;
  }
  // else ignore this call
  NS_WARNING_ASSERTION(!mIsActive,
                       "Calling SetExecuteAsync on active channel?");

  return NS_OK;
}

NS_IMETHODIMP
nsJSChannel::GetExecuteAsync(bool* aIsAsync) {
  *aIsAsync = mIsAsync;
  return NS_OK;
}

bool nsJSChannel::GetIsDocumentLoad() {
  // Our LOAD_DOCUMENT_URI flag, if any, lives on our stream channel.
  nsLoadFlags flags;
  mStreamChannel->GetLoadFlags(&flags);
  return flags & LOAD_DOCUMENT_URI;
}

////////////////////////////////////////////////////////////////////////////////

nsJSProtocolHandler::nsJSProtocolHandler() = default;

nsJSProtocolHandler::~nsJSProtocolHandler() = default;

NS_IMPL_ISUPPORTS(nsJSProtocolHandler, nsIProtocolHandler)

/* static */ nsresult nsJSProtocolHandler::EnsureUTF8Spec(
    const nsCString& aSpec, const char* aCharset, nsACString& aUTF8Spec) {
  aUTF8Spec.Truncate();

  nsAutoString uStr;
  nsresult rv = nsTextToSubURI::UnEscapeNonAsciiURI(
      nsDependentCString(aCharset), aSpec, uStr);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!IsAscii(uStr)) {
    rv = NS_EscapeURL(NS_ConvertUTF16toUTF8(uStr),
                      esc_AlwaysCopy | esc_OnlyNonASCII, aUTF8Spec,
                      mozilla::fallible);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// nsIProtocolHandler methods:

NS_IMETHODIMP
nsJSProtocolHandler::GetScheme(nsACString& result) {
  result = "javascript";
  return NS_OK;
}

/* static */ nsresult nsJSProtocolHandler::CreateNewURI(const nsACString& aSpec,
                                                        const char* aCharset,
                                                        nsIURI* aBaseURI,
                                                        nsIURI** result) {
  nsresult rv = NS_OK;

  // javascript: URLs (currently) have no additional structure beyond that
  // provided by standard URLs, so there is no "outer" object given to
  // CreateInstance.

  NS_MutateURI mutator(new nsJSURI::Mutator());
  nsCOMPtr<nsIURI> base(aBaseURI);
  mutator.Apply(&nsIJSURIMutator::SetBase, base);
  if (!aCharset || !nsCRT::strcasecmp("UTF-8", aCharset)) {
    mutator.SetSpec(aSpec);
  } else {
    nsAutoCString utf8Spec;
    rv = EnsureUTF8Spec(PromiseFlatCString(aSpec), aCharset, utf8Spec);
    if (NS_FAILED(rv)) {
      return rv;
    }
    if (utf8Spec.IsEmpty()) {
      mutator.SetSpec(aSpec);
    } else {
      mutator.SetSpec(utf8Spec);
    }
  }

  nsCOMPtr<nsIURI> url;
  rv = mutator.Finalize(url);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // use DefaultURI to check for validity when we have possible hostnames
  // since nsSimpleURI doesn't know about hostnames
  auto pos = aSpec.Find("javascript:");
  if (pos != kNotFound) {
    nsDependentCSubstring rest(aSpec, pos + sizeof("javascript:") - 1, -1);
    if (StringBeginsWith(rest, "//"_ns)) {
      nsCOMPtr<nsIURI> uriWithHost;
      rv = NS_MutateURI(new mozilla::net::DefaultURI::Mutator())
               .SetSpec(aSpec)
               .Finalize(uriWithHost);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  url.forget(result);
  return rv;
}

NS_IMETHODIMP
nsJSProtocolHandler::NewChannel(nsIURI* uri, nsILoadInfo* aLoadInfo,
                                nsIChannel** result) {
  nsresult rv;

  NS_ENSURE_ARG_POINTER(uri);
  RefPtr<nsJSChannel> channel = new nsJSChannel();
  if (!channel) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  rv = channel->Init(uri, aLoadInfo);
  NS_ENSURE_SUCCESS(rv, rv);

  if (NS_SUCCEEDED(rv)) {
    channel.forget(result);
  }
  return rv;
}

NS_IMETHODIMP
nsJSProtocolHandler::AllowPort(int32_t port, const char* scheme,
                               bool* _retval) {
  // don't override anything.
  *_retval = false;
  return NS_OK;
}

////////////////////////////////////////////////////////////
// nsJSURI implementation

NS_IMPL_ADDREF_INHERITED(nsJSURI, mozilla::net::nsSimpleURI)
NS_IMPL_RELEASE_INHERITED(nsJSURI, mozilla::net::nsSimpleURI)

NS_IMPL_CLASSINFO(nsJSURI, nullptr, nsIClassInfo::THREADSAFE, NS_JSURI_CID);
// Empty CI getter. We only need nsIClassInfo for Serialization
NS_IMPL_CI_INTERFACE_GETTER0(nsJSURI)

NS_INTERFACE_MAP_BEGIN(nsJSURI)
  if (aIID.Equals(NS_GET_IID(nsSimpleURI))) {
    // Need to return explicitly here, because if we just set foundInterface
    // to null the NS_INTERFACE_MAP_END_INHERITING will end up calling into
    // nsSimpleURI::QueryInterface and finding something for this CID.
    *aInstancePtr = nullptr;
    return NS_NOINTERFACE;
  }

  NS_IMPL_QUERY_CLASSINFO(nsJSURI)
  NS_INTERFACE_MAP_ENTRY_CONCRETE(nsJSURI)
NS_INTERFACE_MAP_END_INHERITING(mozilla::net::nsSimpleURI)

// nsISerializable methods:

NS_IMETHODIMP
nsJSURI::Read(nsIObjectInputStream* aStream) {
  MOZ_ASSERT_UNREACHABLE("Use nsIURIMutator.read() instead");
  return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult nsJSURI::ReadPrivate(nsIObjectInputStream* aStream) {
  nsresult rv = mozilla::net::nsSimpleURI::ReadPrivate(aStream);
  if (NS_FAILED(rv)) return rv;

  bool haveBase;
  rv = aStream->ReadBoolean(&haveBase);
  if (NS_FAILED(rv)) return rv;

  if (haveBase) {
    nsCOMPtr<nsISupports> supports;
    rv = aStream->ReadObject(true, getter_AddRefs(supports));
    if (NS_FAILED(rv)) return rv;
    mBaseURI = do_QueryInterface(supports);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsJSURI::Write(nsIObjectOutputStream* aStream) {
  nsresult rv = mozilla::net::nsSimpleURI::Write(aStream);
  if (NS_FAILED(rv)) return rv;

  rv = aStream->WriteBoolean(mBaseURI != nullptr);
  if (NS_FAILED(rv)) return rv;

  if (mBaseURI) {
    rv = aStream->WriteObject(mBaseURI, true);
    if (NS_FAILED(rv)) return rv;
  }

  return NS_OK;
}

void nsJSURI::Serialize(mozilla::ipc::URIParams& aParams) {
  using namespace mozilla::ipc;

  JSURIParams jsParams;
  URIParams simpleParams;

  mozilla::net::nsSimpleURI::Serialize(simpleParams);

  jsParams.simpleParams() = simpleParams;
  if (mBaseURI) {
    SerializeURI(mBaseURI, jsParams.baseURI());
  } else {
    jsParams.baseURI() = mozilla::Nothing();
  }

  aParams = jsParams;
}

bool nsJSURI::Deserialize(const mozilla::ipc::URIParams& aParams) {
  using namespace mozilla::ipc;

  if (aParams.type() != URIParams::TJSURIParams) {
    NS_ERROR("Received unknown parameters from the other process!");
    return false;
  }

  const JSURIParams& jsParams = aParams.get_JSURIParams();
  if (!mozilla::net::nsSimpleURI::Deserialize(jsParams.simpleParams())) {
    return false;
  }

  if (jsParams.baseURI().isSome()) {
    mBaseURI = DeserializeURI(jsParams.baseURI().ref());
  } else {
    mBaseURI = nullptr;
  }
  return true;
}

// nsSimpleURI methods:
/* virtual */ already_AddRefed<mozilla::net::nsSimpleURI>
nsJSURI::StartClone() {
  RefPtr<nsJSURI> url = new nsJSURI(mBaseURI);
  return url.forget();
}

// Queries this list of interfaces. If none match, it queries mURI.
NS_IMPL_NSIURIMUTATOR_ISUPPORTS(nsJSURI::Mutator, nsIURISetters, nsIURIMutator,
                                nsISerializable, nsIJSURIMutator)

NS_IMETHODIMP
nsJSURI::Mutate(nsIURIMutator** aMutator) {
  RefPtr<nsJSURI::Mutator> mutator = new nsJSURI::Mutator();
  nsresult rv = mutator->InitFromURI(this);
  if (NS_FAILED(rv)) {
    return rv;
  }
  mutator.forget(aMutator);
  return NS_OK;
}

/* virtual */
nsresult nsJSURI::EqualsInternal(
    nsIURI* aOther, mozilla::net::nsSimpleURI::RefHandlingEnum aRefHandlingMode,
    bool* aResult) {
  NS_ENSURE_ARG_POINTER(aOther);
  MOZ_ASSERT(aResult, "null pointer for outparam");

  RefPtr<nsJSURI> otherJSURI = do_QueryObject(aOther);
  if (!otherJSURI) {
    *aResult = false;  // aOther is not a nsJSURI --> not equal.
    return NS_OK;
  }

  // Compare the member data that our base class knows about.
  if (!mozilla::net::nsSimpleURI::EqualsInternal(otherJSURI,
                                                 aRefHandlingMode)) {
    *aResult = false;
    return NS_OK;
  }

  // Compare the piece of additional member data that we add to base class.
  nsIURI* otherBaseURI = otherJSURI->GetBaseURI();

  if (mBaseURI) {
    // (As noted in StartClone, we always honor refs on mBaseURI)
    return mBaseURI->Equals(otherBaseURI, aResult);
  }

  *aResult = !otherBaseURI;
  return NS_OK;
}
