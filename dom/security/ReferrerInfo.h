/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_ReferrerInfo_h
#define mozilla_dom_ReferrerInfo_h

#include "mozilla/HashFunctions.h"
#include "mozilla/Maybe.h"
#include "mozilla/dom/ReferrerPolicyBinding.h"
#include "nsCOMPtr.h"
#include "nsIReferrerInfo.h"
#include "nsReadableUtils.h"

namespace IPC {
class MessageReader;
class MessageWriter;
}  // namespace IPC

#define REFERRERINFO_CONTRACTID "@mozilla.org/referrer-info;1"
// 041a129f-10ce-4bda-a60d-e027a26d5ed0
#define REFERRERINFO_CID \
  {0x041a129f, 0x10ce, 0x4bda, {0xa6, 0x0d, 0xe0, 0x27, 0xa2, 0x6d, 0x5e, 0xd0}}

class nsIHttpChannel;
class nsIURI;
class nsIChannel;
class nsILoadInfo;
class nsINode;
class nsIPrincipal;

namespace mozilla {
class StyleSheet;
class URLAndReferrerInfo;

namespace net {
class HttpBaseChannel;
class nsHttpChannel;
}  // namespace net
}  // namespace mozilla

namespace mozilla::dom {

/**
 * The ReferrerInfo class holds the raw referrer and potentially a referrer
 * policy which allows to query the computed referrer which should be applied to
 * a channel as the actual referrer value.
 *
 * The ReferrerInfo class solely contains readonly fields and represents a 1:1
 * sync to the referrer header of the corresponding channel. In turn that means
 * the class is immutable - so any modifications require to clone the current
 * ReferrerInfo.
 *
 * For example if a request undergoes a redirect, the new channel
 * will need a new ReferrerInfo clone with members being updated accordingly.
 */

class ReferrerInfo : public nsIReferrerInfo {
 public:
  typedef enum ReferrerPolicy ReferrerPolicyEnum;
  ReferrerInfo();

  explicit ReferrerInfo(
      nsIURI* aOriginalReferrer,
      ReferrerPolicyEnum aPolicy = ReferrerPolicy::_empty,
      bool aSendReferrer = true,
      const Maybe<nsCString>& aComputedReferrer = Maybe<nsCString>());

  // Creates already initialized ReferrerInfo from an element or a document.
  explicit ReferrerInfo(const Element&);
  explicit ReferrerInfo(const Document&, const bool = true);

  // Creates already initialized ReferrerInfo from an element or a document with
  // a specific referrer policy.
  ReferrerInfo(const Element&, ReferrerPolicyEnum);

  // create an exact copy of the ReferrerInfo
  already_AddRefed<ReferrerInfo> Clone() const;

  void Serialize(IPC::MessageWriter* aWriter) const;
  static bool Deserialize(IPC::MessageReader* aReader,
                          RefPtr<nsIReferrerInfo>* aResult);

  // create an copy of the ReferrerInfo with new referrer policy
  already_AddRefed<ReferrerInfo> CloneWithNewPolicy(
      ReferrerPolicyEnum aPolicy) const;

  // create an copy of the ReferrerInfo with new original referrer
  already_AddRefed<ReferrerInfo> CloneWithNewOriginalReferrer(
      nsIURI* aOriginalReferrer) const;

  /*
   * Helper function to create a new ReferrerInfo object from a given document
   * and override referrer policy if needed (for example, when parsing link
   * header or speculative loading).
   *
   * @param aDocument the document to init referrerInfo object.
   * @param aPolicyOverride referrer policy to override if necessary.
   */
  static already_AddRefed<nsIReferrerInfo> CreateFromDocumentAndPolicyOverride(
      Document* aDoc, ReferrerPolicyEnum aPolicyOverride);

  /*
   * Implements step 3.1 and 3.3 of the Determine request's Referrer algorithm
   * from the Referrer Policy specification.
   *
   * https://w3c.github.io/webappsec/specs/referrer-policy/#determine-requests-referrer
   */
  static already_AddRefed<nsIReferrerInfo> CreateForFetch(
      nsIPrincipal* aPrincipal, Document* aDoc);

  /**
   * Helper function to create new ReferrerInfo object from a given external
   * stylesheet. The returned nsIReferrerInfo object will be used for any
   * requests or resources referenced by the sheet.
   *
   * @param aExternalSheet the stylesheet.
   * @param aExternalSheetURI the stylesheet URI.
   * @param aPolicy referrer policy from header if there's any.
   */
  static already_AddRefed<nsIReferrerInfo> CreateForExternalCSSResources(
      StyleSheet* aExternalSheet, nsIURI* aExternalSheetURI,
      ReferrerPolicyEnum aPolicy = ReferrerPolicy::_empty);

  /**
   * Helper function to create new ReferrerInfo object from a given document.
   * The returned nsIReferrerInfo object will be used for any requests or
   * resources referenced by internal stylesheet (for example style="" or
   * wrapped by <style> tag), as well as SVG resources.
   *
   * @param aDocument the document to init referrerInfo object.
   */
  static already_AddRefed<nsIReferrerInfo> CreateForInternalCSSAndSVGResources(
      Document* aDocument);

  /**
   * Check whether the given referrer's scheme is allowed to be computed and
   * sent. The allowlist schemes are: http, https.
   */
  static bool IsReferrerSchemeAllowed(nsIURI* aReferrer);

  /*
   * The Referrer Policy should be inherited for nested browsing contexts that
   * are not created from responses. Such as: srcdoc, data, blob.
   */
  static bool ShouldResponseInheritReferrerInfo(nsIChannel* aChannel);

  /*
   * Check whether referrer is allowed to send in secure to insecure scenario.
   */
  static nsresult HandleSecureToInsecureReferral(nsIURI* aOriginalURI,
                                                 nsIURI* aURI,
                                                 ReferrerPolicyEnum aPolicy,
                                                 bool& aAllowed);

  /**
   * Returns true if the given channel is cross-origin request
   *
   * Computing whether the request is cross-origin may be expensive, so please
   * do that in cases where we're going to use this information later on.
   */
  static bool IsCrossOriginRequest(nsIHttpChannel* aChannel);

  /**
   * Returns true if aReferrer's origin and aChannel's URI are cross-origin.
   */
  static bool IsReferrerCrossOrigin(nsIHttpChannel* aChannel,
                                    nsIURI* aReferrer);

  /**
   * Returns true if the given channel is cross-site request.
   */
  static bool IsCrossSiteRequest(nsIHttpChannel* aChannel);

  /**
   * Returns true if the given channel is suppressed by Referrer-Policy header
   * and should set "null" to Origin header.
   */
  static bool ShouldSetNullOriginHeader(net::HttpBaseChannel* aChannel,
                                        nsIURI* aOriginURI);

  /**
   * Getter for network.http.sendRefererHeader.
   */
  static uint32_t GetUserReferrerSendingPolicy();

  /**
   * Getter for network.http.referer.XOriginPolicy.
   */
  static uint32_t GetUserXOriginSendingPolicy();

  /**
   * Getter for network.http.referer.trimmingPolicy.
   */
  static uint32_t GetUserTrimmingPolicy();

  /**
   * Getter for network.http.referer.XOriginTrimmingPolicy.
   */
  static uint32_t GetUserXOriginTrimmingPolicy();

  /**
   * Return default referrer policy which is controlled by user
   * prefs:
   * network.http.referer.defaultPolicy for regular mode
   * network.http.referer.defaultPolicy.trackers for third-party trackers
   * in regular mode
   * network.http.referer.defaultPolicy.pbmode for private mode
   * network.http.referer.defaultPolicy.trackers.pbmode for third-party trackers
   * in private mode
   */
  static ReferrerPolicyEnum GetDefaultReferrerPolicy(
      nsIHttpChannel* aChannel = nullptr, nsIURI* aURI = nullptr,
      bool aPrivateBrowsing = false);

  /**
   * Return default referrer policy for third party which is controlled by user
   * prefs:
   * network.http.referer.defaultPolicy.trackers for regular mode
   * network.http.referer.defaultPolicy.trackers.pbmode for private mode
   */
  static ReferrerPolicyEnum GetDefaultThirdPartyReferrerPolicy(
      bool aPrivateBrowsing = false);

  /*
   * Helper function to parse ReferrerPolicy from meta tag referrer content.
   * For example: <meta name="referrer" content="origin">
   *
   * @param aContent content string to be transformed into ReferrerPolicyEnum,
   *                 e.g. "origin".
   */
  static ReferrerPolicyEnum ReferrerPolicyFromMetaString(
      const nsAString& aContent);

  /*
   * Helper function to parse ReferrerPolicy from string content of
   * referrerpolicy attribute.
   * For example: <a href="http://example.com" referrerpolicy="no-referrer">
   *
   * @param aContent content string to be transformed into ReferrerPolicyEnum,
   *                 e.g. "no-referrer".
   */
  static ReferrerPolicyEnum ReferrerPolicyAttributeFromString(
      const nsAString& aContent);

  /*
   * Helper function to parse ReferrerPolicy from string content of
   * Referrer-Policy header.
   * For example: Referrer-Policy: origin no-referrer
   * https://www.w3.org/tr/referrer-policy/#parse-referrer-policy-from-header
   *
   * @param aContent content string to be transformed into ReferrerPolicyEnum.
   *                e.g. "origin no-referrer"
   */
  static ReferrerPolicyEnum ReferrerPolicyFromHeaderString(
      const nsAString& aContent);

  /**
   * Hash function for this object
   */
  HashNumber Hash() const;

  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIREFERRERINFO
  NS_DECL_NSISERIALIZABLE

 private:
  virtual ~ReferrerInfo() = default;

  ReferrerInfo(const ReferrerInfo& rhs);

  /*
   * Trimming policy when compute referrer, indicate how much information in the
   * referrer will be sent. Order matters here.
   */
  enum TrimmingPolicy : uint32_t {
    ePolicyFullURI = 0,
    ePolicySchemeHostPortPath = 1,
    ePolicySchemeHostPort = 2,
  };

  /*
   * Referrer sending policy, indicates type of action could trigger to send
   * referrer header, not send at all, send only with user's action (click on a
   * link) or send even with inline content request (image request).
   * Order matters here.
   */
  enum ReferrerSendingPolicy : uint32_t {
    ePolicyNotSend = 0,
    ePolicySendWhenUserTrigger = 1,
    ePolicySendInlineContent = 2,
  };

  /*
   * Sending referrer when cross origin policy, indicates when referrer should
   * be send when compare 2 origins. Order matters here.
   */
  enum XOriginSendingPolicy : uint32_t {
    ePolicyAlwaysSend = 0,
    ePolicySendWhenSameDomain = 1,
    ePolicySendWhenSameHost = 2,
  };

  /*
   * Handle user controlled pref network.http.referer.XOriginPolicy
   */
  nsresult HandleUserXOriginSendingPolicy(nsIURI* aURI, nsIURI* aReferrer,
                                          bool& aAllowed) const;

  /*
   * Handle user controlled pref network.http.sendRefererHeader
   */
  nsresult HandleUserReferrerSendingPolicy(nsIHttpChannel* aChannel,
                                           bool& aAllowed) const;

  /*
   * Compute trimming policy from user controlled prefs.
   * This function is called when we already made sure a nonempty referrer is
   * allowed to send.
   */
  TrimmingPolicy ComputeTrimmingPolicy(nsIHttpChannel* aChannel,
                                       nsIURI* aReferrer) const;

  // HttpBaseChannel could access IsInitialized() and ComputeReferrer();
  friend class mozilla::net::HttpBaseChannel;

  /*
   * Compute referrer for a given channel. The computation result then will be
   * stored in this class and then used to set the actual referrer header of
   * the channel. The computation could be controlled by several user prefs
   * which are defined in StaticPrefList.yaml (see StaticPrefList.yaml for more
   * details):
   *  network.http.sendRefererHeader
   *  network.http.referer.spoofSource
   *  network.http.referer.hideOnionSource
   *  network.http.referer.XOriginPolicy
   *  network.http.referer.trimmingPolicy
   *  network.http.referer.XOriginTrimmingPolicy
   */
  nsresult ComputeReferrer(nsIHttpChannel* aChannel);

  /*
   * Check whether the ReferrerInfo has been initialized or not.
   */
  bool IsInitialized() { return mInitialized; }

  // nsHttpChannel, Document could access IsPolicyOverrided();
  friend class mozilla::net::nsHttpChannel;
  friend class mozilla::dom::Document;
  /*
   * Check whether if unset referrer policy is overrided by default or not
   */
  bool IsPolicyOverrided() { return mOverridePolicyByDefault; }

  /*
   *  Get origin string from a given valid referrer URI (http, https)
   *
   *  @aReferrer - the full referrer URI
   *  @aResult - the resulting aReferrer in string format.
   */
  nsresult GetOriginFromReferrerURI(nsIURI* aReferrer,
                                    nsACString& aResult) const;

  /*
   * Trim a given referrer with a given a trimming policy,
   */
  nsresult TrimReferrerWithPolicy(nsIURI* aReferrer,
                                  TrimmingPolicy aTrimmingPolicy,
                                  nsACString& aResult) const;

  /**
   * Returns true if we should ignore less restricted referrer policies,
   * including 'unsafe_url', 'no_referrer_when_downgrade' and
   * 'origin_when_cross_origin', for the given channel. We only apply this
   * restriction for cross-site requests. For the same-site request, we will
   * still allow overriding the default referrer policy with less restricted
   * one.
   *
   * Note that the channel triggered by the system and the extension will be
   * exempt from this restriction.
   */
  bool ShouldIgnoreLessRestrictedPolicies(
      nsIHttpChannel* aChannel, const ReferrerPolicyEnum aPolicy) const;

  /*
   *  Limit referrer length using the following ruleset:
   *   - If the length of referrer URL is over max length, strip down to origin.
   *   - If the origin is still over max length, remove the referrer entirely.
   *
   *  This function comlements TrimReferrerPolicy and needs to be called right
   *  after TrimReferrerPolicy.
   *
   *  @aChannel - used to query information needed for logging to the console.
   *  @aReferrer - the full referrer URI; needs to be identical to aReferrer
   *               passed to TrimReferrerPolicy.
   *  @aTrimmingPolicy - represents the trimming policy which was applied to the
   *                     referrer; needs to be identical to aTrimmingPolicy
   *                     passed to TrimReferrerPolicy.
   *  @aInAndOutTrimmedReferrer -  an in and outgoing argument representing the
   *                               referrer value. Please pass the result of
   *                               TrimReferrerWithPolicy as
   *                               aInAndOutTrimmedReferrer which will then be
   *                               reduced to the origin or completely truncated
   *                               in case the referrer value exceeds the length
   *                               limitation.
   */
  nsresult LimitReferrerLength(nsIHttpChannel* aChannel, nsIURI* aReferrer,
                               TrimmingPolicy aTrimmingPolicy,
                               nsACString& aInAndOutTrimmedReferrer) const;

  /**
   * The helper function to read the old data format before gecko 100 for
   * deserialization.
   */
  nsresult ReadTailDataBeforeGecko100(const uint32_t& aData,
                                      nsIObjectInputStream* aInputStream);

  /*
   * Write message to the error console
   */
  void LogMessageToConsole(nsIHttpChannel* aChannel, const char* aMsg,
                           const nsTArray<nsString>& aParams) const;

  friend class mozilla::URLAndReferrerInfo;

  nsCOMPtr<nsIURI> mOriginalReferrer;

  ReferrerPolicyEnum mPolicy;

  // The referrer policy that has been set originally for the channel. Note that
  // the policy may have been overridden by the default referrer policy, so we
  // need to keep track of this if we need to recover the original referrer
  // policy.
  ReferrerPolicyEnum mOriginalPolicy;

  // Indicates if the referrer should be sent or not even when it's available
  // (default is true).
  bool mSendReferrer;

  // Since the ReferrerInfo is immutable, we use this member as a helper to
  // ensure no one can call e.g. init() twice to modify state of the
  // ReferrerInfo.
  bool mInitialized;

  // Indicates if unset referrer policy is overrided by default
  bool mOverridePolicyByDefault;

  // Store a computed referrer for a given channel
  Maybe<nsCString> mComputedReferrer;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_ReferrerInfo_h
