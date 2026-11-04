/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsCSPContext_h_
#define nsCSPContext_h_

#include "mozilla/Maybe.h"
#include "mozilla/StaticPrefs_security.h"
#include "mozilla/dom/CSPViolationData.h"
#include "mozilla/dom/SecurityPolicyViolationEvent.h"
#include "mozilla/dom/nsCSPUtils.h"
#include "nsIChannel.h"
#include "nsIChannelEventSink.h"
#include "nsIContentSecurityPolicy.h"
#include "nsIInterfaceRequestor.h"
#include "nsIStreamListener.h"
#include "nsIWeakReferenceUtils.h"
#include "nsXPCOM.h"

#define NS_CSPCONTEXT_CONTRACTID "@mozilla.org/cspcontext;1"
// 09d9ed1a-e5d4-4004-bfe0-27ceb923d9ac
#define NS_CSPCONTEXT_CID \
  {0x09d9ed1a, 0xe5d4, 0x4004, {0xbf, 0xe0, 0x27, 0xce, 0xb9, 0x23, 0xd9, 0xac}}

class nsINetworkInterceptController;
class nsIEventTarget;
struct ConsoleMsgQueueElem;

namespace mozilla {
namespace dom {
class Element;
}
namespace ipc {
class ContentSecurityPolicy;
}
}  // namespace mozilla

class nsCSPContext : public nsIContentSecurityPolicy {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSICONTENTSECURITYPOLICY
  NS_DECL_NSISERIALIZABLE

  NS_DEFINE_STATIC_CID_ACCESSOR(NS_CSPCONTEXT_CID)

 protected:
  virtual ~nsCSPContext();

 public:
  nsCSPContext();

  static bool Equals(nsIContentSecurityPolicy* aCSP,
                     nsIContentSecurityPolicy* aOtherCSP);

  // Init a CSP from a different CSP
  nsresult InitFromOther(nsCSPContext* otherContext);

  // Used to suppress errors and warnings produced by the parser.
  // Use this when doing an one-off parsing of the CSP.
  void SuppressParserLogMessages() { mSuppressParserLogMessages = true; }

  /**
   * SetRequestContextWithDocument() needs to be called before the
   * innerWindowID is initialized on the document. Use this function
   * to call back to flush queued up console messages and initialize
   * the innerWindowID. Node, If SetRequestContextWithPrincipal() was
   * called then we do not have a innerWindowID anyway and hence
   * we can not flush messages to the correct console.
   */
  void flushConsoleMessages();

  void logToConsole(const char* aName, const nsTArray<nsString>& aParams,
                    const nsACString& aSourceName, const nsAString& aSourceLine,
                    uint32_t aLineNumber, uint32_t aColumnNumber,
                    uint32_t aSeverityFlag);

  /**
   * Construct SecurityPolicyViolationEventInit structure.
   *
   * @param aOriginalUri
   *        The original URI if the blocked content is a redirect, else null
   * @param aViolatedDirective
   *        the directive that was violated (string).
   * @param aScriptSample
   *        a sample of the violating inline script
   * @param aViolationEventInit
   *        The output
   */
  nsresult GatherSecurityPolicyViolationEventData(
      nsIURI* aOriginalURI, const nsAString& aEffectiveDirective,
      const mozilla::dom::CSPViolationData& aCSPViolationData,
      bool aReportSample,
      mozilla::dom::SecurityPolicyViolationEventInit& aViolationEventInit);

  nsresult SendReports(
      const mozilla::dom::SecurityPolicyViolationEventInit& aViolationEventInit,
      uint32_t aViolatedPolicyIndex);

  nsresult SendReportsToEndpoints(
      nsAutoString& reportGroup,
      const mozilla::dom::SecurityPolicyViolationEventInit&
          aViolationEventInit);

  nsresult SendReportsToURIs(
      const nsTArray<nsString>& reportURIs,
      const mozilla::dom::SecurityPolicyViolationEventInit&
          aViolationEventInit);

  void HandleInternalPageViolation(
      const mozilla::dom::CSPViolationData& aCSPViolationData,
      const mozilla::dom::SecurityPolicyViolationEventInit& aViolationEventInit,
      const nsAString& aViolatedDirectiveNameAndValue);

  nsresult FireViolationEvent(
      mozilla::dom::Element* aTriggeringElement,
      nsICSPEventListener* aCSPEventListener,
      const mozilla::dom::SecurityPolicyViolationEventInit& aViolationEventInit,
      const nsAString& aReportGroupName);

  /**
   * Asynchronously notifies any nsIObservers listening to the CSP violation
   * topic that a violation occurred.  Also triggers report sending and console
   * logging.  All asynchronous on the main thread.
   *
   * @param aCSPEventListener Should be null when the violation stems from a
   *                          Window. Is required when the violation stems from
   *                          a Worker to be potentially notified about the
   *                          violation event.
   * @param aOriginalUri
   *        The original URI if the blocked content is a redirect, else null
   * @param aViolatedDirectiveName
   *        the directive that was violated (string).
   * @param aObserverSubject
   *        optional, subject sent to the nsIObservers listening to the CSP
   *        violation topic.
   */
  nsresult AsyncReportViolation(
      nsICSPEventListener* aCSPEventListener,
      mozilla::dom::CSPViolationData&& aCSPViolationData, nsIURI* aOriginalURI,
      const nsAString& aViolatedDirectiveName,
      const nsAString& aViolatedDirectiveNameAndValue,
      const nsAString& aObserverSubject, bool aReportSample);

  // Hands off! Don't call this method unless you know what you
  // are doing. It's only supposed to be called from within
  // the principal destructor to avoid a tangling pointer.
  void clearLoadingPrincipal() { mLoadingPrincipal = nullptr; }

  nsWeakPtr GetLoadingContext() { return mLoadingContext; }

  static uint32_t ScriptSampleMaxLength() {
    return std::max(
        mozilla::StaticPrefs::security_csp_reporting_script_sample_max_length(),
        0);
  }

  void AddIPCPolicy(const mozilla::ipc::ContentSecurityPolicy& aPolicy);
  void SerializePolicies(
      nsTArray<mozilla::ipc::ContentSecurityPolicy>& aPolicies);

  // Returns an empty string if aPolicyIndex > num of policies.
  nsString GetReportGroupFor(uint64_t aPolicyIndex) const;

  // Bug 2072122: Trusted Types readiness experiment only, to be removed.
  // Returns Enforce if any enforced policy is a strict CSP, otherwise Report if
  // any report-only policy is, otherwise Nothing. See nsCSPPolicy::isStrict.
  mozilla::Maybe<nsCSPPolicy::Disposition> GetStrictPolicyDisposition();

  static nsCSPContext* Cast(nsIContentSecurityPolicy* aCSP) {
    return static_cast<nsCSPContext*>(aCSP);
  }

  [[nodiscard]] nsresult PolicyContainerRead(
      nsIObjectInputStream* aInputStream);

 private:
  enum class ForceReportSample { Yes, No };

  /**
   * @param aCSPEventListener see `nsCSPContext::AsyncReportViolation`'s csp
   *                          event listener argument.
   */
  void LogViolationDetailsUnchecked(
      nsICSPEventListener* aCSPEventListener,
      mozilla::dom::CSPViolationData&& aCSPViolationData,
      const nsAString& aObserverSubject, ForceReportSample aForceReportSample);

  bool ShouldThrottleReport(
      const mozilla::dom::SecurityPolicyViolationEventInit&
          aViolationEventInit);

  bool permitsInternal(CSPDirective aDir,
                       mozilla::dom::Element* aTriggeringElement,
                       nsICSPEventListener* aCSPEventListener,
                       nsILoadInfo* aLoadInfo, nsIURI* aContentLocation,
                       nsIURI* aOriginalURIIfRedirect, bool aSpecific,
                       bool aSendViolationReports,
                       bool aSendContentLocationInViolationReports);

  // helper to report inline script/style violations
  void ReportInlineViolation(CSPDirective aDirective,
                             mozilla::dom::Element* aTriggeringElement,
                             nsICSPEventListener* aCSPEventListener,
                             const nsAString& aNonce, bool aReportSample,
                             const nsAString& aSourceCode,
                             const nsAString& aViolatedDirective,
                             const nsAString& aViolatedDirectiveString,
                             CSPDirective aEffectiveDirective,
                             uint32_t aViolatedPolicyIndex,
                             uint32_t aLineNumber, uint32_t aColumnNumber);

  enum class PolicyDataVersion {
    Pre136,   // Before v136
    Post136,  // v136-137 with bug 1901492 or v138+ with bug 1958259. (This is
              // the current version)
    V138_9PreRelease,  // v138 Beta/Nightly and v139 Nightly with bug 1942306
                       // and without bug 1958259
  };

  nsresult TryReadPolicies(PolicyDataVersion aVersion,
                           nsIObjectInputStream* aStream, uint32_t aNumPolicies,
                           bool aForPolicyContainer);

  [[nodiscard]] nsresult ReadImpl(nsIObjectInputStream* aStream,
                                  bool aForPolicyContainer);

  nsCString mReferrer;
  uint64_t mInnerWindowID;          // See `nsPIDOMWindowInner::mWindowID`.
  bool mSkipAllowInlineStyleCheck;  // used to allow Devtools to edit styles
  // When deserializing an nsCSPContext instance, we initially just keep the
  // policies unparsed. We will only reconstruct actual CSP policy instances
  // when there's an attempt to use the CSP. Given a better way to serialize/
  // deserialize individual nsCSPPolicy objects, this performance
  // optimization could go away.
  nsTArray<mozilla::ipc::ContentSecurityPolicy> mIPCPolicies;
  nsTArray<nsCSPPolicy*> mPolicies;
  nsCOMPtr<nsIURI> mSelfURI;
  nsCOMPtr<nsILoadGroup> mCallingChannelLoadGroup;
  nsWeakPtr mLoadingContext;
  nsCOMPtr<nsIPrincipal> mLoadingPrincipal;

  bool mSuppressParserLogMessages = false;

  // helper members used to queue up web console messages till
  // the windowID becomes available. see flushConsoleMessages()
  nsTArray<ConsoleMsgQueueElem> mConsoleMsgQueue;
  bool mQueueUpMessages;
  nsCOMPtr<nsIEventTarget> mEventTarget;

  mozilla::TimeStamp mSendReportLimitSpanStart;
  uint32_t mSendReportLimitCount = 1;
  bool mWarnedAboutTooManyReports = false;
  RequireTrustedTypesForDirectiveState mRequireTrustedTypesForDirectiveState =
      RequireTrustedTypesForDirectiveState::NONE;
};

// Class that listens to violation report transmission and logs errors.
class CSPViolationReportListener : public nsIStreamListener {
 public:
  NS_DECL_NSISTREAMLISTENER
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_ISUPPORTS

 public:
  CSPViolationReportListener();

 protected:
  virtual ~CSPViolationReportListener();
};

// The POST of the violation report (if it happens) should not follow
// redirects, per the spec. hence, we implement an nsIChannelEventSink
// with an object so we can tell XHR to abort if a redirect happens.
class CSPReportRedirectSink final : public nsIChannelEventSink,
                                    public nsIInterfaceRequestor {
 public:
  NS_DECL_NSICHANNELEVENTSINK
  NS_DECL_NSIINTERFACEREQUESTOR
  NS_DECL_ISUPPORTS

 public:
  CSPReportRedirectSink();

  void SetInterceptController(
      nsINetworkInterceptController* aInterceptController);

 protected:
  virtual ~CSPReportRedirectSink();

 private:
  nsCOMPtr<nsINetworkInterceptController> mInterceptController;
};

#endif /* nsCSPContext_h_ */
