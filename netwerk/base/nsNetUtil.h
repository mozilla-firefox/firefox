/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsNetUtil_h_
#define nsNetUtil_h_

#include <functional>

#include "mozilla/Maybe.h"
#include "mozilla/NotNull.h"
#include "mozilla/ResultExtensions.h"
#include "mozilla/Services.h"
#include "mozilla/net/MozURL_ffi.h"
#include "mozilla/net/idna_glue.h"
#include "nsAttrValue.h"
#include "nsCOMPtr.h"
#include "nsIIOService.h"
#include "nsIInterfaceRequestor.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsILoadGroup.h"
#include "nsILoadInfo.h"
#include "nsINestedURI.h"
#include "nsINetUtil.h"
#include "nsIRequest.h"
#include "nsIURI.h"
#include "nsNetCID.h"
#include "nsReadableUtils.h"
#include "nsServiceManagerUtils.h"
#include "nsString.h"
#include "nsTArray.h"

class nsIPrincipal;
class nsIAsyncStreamCopier;
class nsIAuthPrompt;
class nsIAuthPrompt2;
class nsIChannel;
class nsIChannelPolicy;
class nsIConsoleReportCollector;
class nsICookieJarSettings;
class nsIDownloadObserver;
class nsIEventTarget;
class nsIFileProtocolHandler;
class nsIFileRandomAccessStream;
class nsIHttpChannel;
class nsIInputStream;
class nsIInputStreamPump;
class nsIInterfaceRequestor;
class nsIOutputStream;
class nsIParentChannel;
class nsIPersistentProperties;
class nsIProxyInfo;
class nsIRandomAccessStream;
class nsIRequestObserver;
class nsISerialEventTarget;
class nsIStreamListener;
class nsIStreamLoader;
class nsIStreamLoaderObserver;
class nsIIncrementalStreamLoader;
class nsIIncrementalStreamLoaderObserver;

namespace mozilla {
class Encoding;
class OriginAttributes;
class OriginTrials;
namespace dom {
class ClientInfo;
class PerformanceStorage;
class ServiceWorkerDescriptor;
}  // namespace dom

namespace ipc {
class FileDescriptor;
}  // namespace ipc

}  // namespace mozilla

template <class>
class nsCOMPtr;
template <typename>
struct already_AddRefed;

already_AddRefed<nsIIOService> do_GetIOService(nsresult* error = nullptr);

already_AddRefed<nsINetUtil> do_GetNetUtil(nsresult* error = nullptr);

// private little helper function... don't call this directly!
nsresult net_EnsureIOService(nsIIOService** ios, nsCOMPtr<nsIIOService>& grip);

nsresult NS_NewURI(nsIURI** aURI, const nsACString& spec,
                   const char* charset = nullptr, nsIURI* baseURI = nullptr);

nsresult NS_NewURI(nsIURI** result, const nsACString& spec,
                   mozilla::NotNull<const mozilla::Encoding*> encoding,
                   nsIURI* baseURI = nullptr);

nsresult NS_NewURI(nsIURI** result, const nsAString& spec,
                   const char* charset = nullptr, nsIURI* baseURI = nullptr);

nsresult NS_NewURI(nsIURI** result, const nsAString& spec,
                   mozilla::NotNull<const mozilla::Encoding*> encoding,
                   nsIURI* baseURI = nullptr);

nsresult NS_NewURI(nsIURI** result, const char* spec,
                   nsIURI* baseURI = nullptr);

nsresult NS_NewFileURI(
    nsIURI** result, nsIFile* spec,
    nsIIOService* ioService =
        nullptr);  // pass in nsIIOService to optimize callers

// Functions for adding additional encoding to a URL for compatibility with
// Apple's NSURL class URLWithString method.
//
// @param aResult
//        Out parameter for the encoded URL spec
// @param aSpec
//        The spec for the URL to be encoded
nsresult NS_GetSpecWithNSURLEncoding(nsACString& aResult,
                                     const nsACString& aSpec);
// @param aResult
//        Out parameter for the encoded URI
// @param aSpec
//        The spec for the URL to be encoded
nsresult NS_NewURIWithNSURLEncoding(nsIURI** aResult, const nsACString& aSpec);

// These methods will only mutate the URI if the ref of aInput doesn't already
// match the ref we are trying to set.
// If aInput has no ref, and we are calling NS_GetURIWithoutRef, or
// NS_GetURIWithNewRef with an empty string, then aOutput will be the same
// as aInput. The same is true if aRef is already equal to the ref of aInput.
// This is OK because URIs are immutable and threadsafe.
// If the URI doesn't support ref fragments aOutput will be the same as aInput.
nsresult NS_GetURIWithNewRef(nsIURI* aInput, const nsACString& aRef,
                             nsIURI** aOutput);
nsresult NS_GetURIWithoutRef(nsIURI* aInput, nsIURI** aOutput);

nsresult NS_GetSanitizedURIStringFromURI(nsIURI* aUri,
                                         nsACString& aSanitizedSpec);

// Assigns to aSanitizedSpec the spec safe to expose for aSpec: aSpec itself
// when it carries no password, a password-hidden form when it does, and an
// empty string if a password is present but cannot be hidden.
void NS_GetSanitizedSpecFromSpec(const nsACString& aSpec,
                                 nsACString& aSanitizedSpec);

/*
 * How to create a new Channel, using NS_NewChannel,
 * NS_NewChannelWithTriggeringPrincipal,
 * NS_NewInputStreamChannel, NS_NewChannelInternal
 * and it's variations:
 *
 * What specific API function to use:
 * * The NS_NewChannelInternal functions should almost never be directly
 *   called outside of necko code.
 * * If possible, use NS_NewChannel() providing a loading *nsINode*
 * * If no loading *nsINode* is available, try calling NS_NewChannel() providing
 *   a loading *ClientInfo*.
 * * If no loading *nsINode* or *ClientInfo* are available, call NS_NewChannel()
 *   providing a loading *nsIPrincipal*.
 * * Call NS_NewChannelWithTriggeringPrincipal if the triggeringPrincipal
 *   is different from the loadingPrincipal.
 * * Call NS_NewChannelInternal() providing aLoadInfo object in cases where
 *   you already have loadInfo object, e.g in case of a channel redirect.
 *
 * @param aURI
 *        nsIURI from which to make a channel
 * @param aLoadingNode
 * @param aLoadingPrincipal
 * @param aTriggeringPrincipal
 * @param aSecurityFlags
 * @param aContentPolicyType
 *        These will be used as values for the nsILoadInfo object on the
 *        created channel. For details, see nsILoadInfo in nsILoadInfo.idl
 *
 * Please note, if you provide both a loadingNode and a loadingPrincipal,
 * then loadingPrincipal must be equal to loadingNode->NodePrincipal().
 * But less error prone is to just supply a loadingNode.
 *
 * Note, if you provide a loading ClientInfo its principal must match the
 * loading principal.  Currently you must pass both as the loading principal
 * may have additional mutable values like CSP on it.  In the future these
 * will be removed from nsIPrincipal and the API can be changed to take just
 * the loading ClientInfo.
 *
 * Keep in mind that URIs coming from a webpage should *never* use the
 * systemPrincipal as the loadingPrincipal.
 */
nsresult NS_NewChannelInternal(
    nsIChannel** outChannel, nsIURI* aUri, nsINode* aLoadingNode,
    nsIPrincipal* aLoadingPrincipal, nsIPrincipal* aTriggeringPrincipal,
    const mozilla::Maybe<mozilla::dom::ClientInfo>& aLoadingClientInfo,
    const mozilla::Maybe<mozilla::dom::ServiceWorkerDescriptor>& aController,
    nsSecurityFlags aSecurityFlags, nsContentPolicyType aContentPolicyType,
    nsICookieJarSettings* aCookieJarSettings = nullptr,
    mozilla::dom::PerformanceStorage* aPerformanceStorage = nullptr,
    nsILoadGroup* aLoadGroup = nullptr,
    nsIInterfaceRequestor* aCallbacks = nullptr,
    nsLoadFlags aLoadFlags = nsIRequest::LOAD_NORMAL,
    nsIIOService* aIoService = nullptr, uint32_t aSandboxFlags = 0,
    uint64_t aAssociatedBrowsingContextID = 0);

// See NS_NewChannelInternal for usage and argument description
nsresult NS_NewChannelInternal(
    nsIChannel** outChannel, nsIURI* aUri, nsILoadInfo* aLoadInfo,
    mozilla::dom::PerformanceStorage* aPerformanceStorage = nullptr,
    nsILoadGroup* aLoadGroup = nullptr,
    nsIInterfaceRequestor* aCallbacks = nullptr,
    nsLoadFlags aLoadFlags = nsIRequest::LOAD_NORMAL,
    nsIIOService* aIoService = nullptr);

// See NS_NewChannelInternal for usage and argument description
nsresult /*NS_NewChannelWithNodeAndTriggeringPrincipal */
NS_NewChannelWithTriggeringPrincipal(
    nsIChannel** outChannel, nsIURI* aUri, nsINode* aLoadingNode,
    nsIPrincipal* aTriggeringPrincipal, nsSecurityFlags aSecurityFlags,
    nsContentPolicyType aContentPolicyType,
    mozilla::dom::PerformanceStorage* aPerformanceStorage = nullptr,
    nsILoadGroup* aLoadGroup = nullptr,
    nsIInterfaceRequestor* aCallbacks = nullptr,
    nsLoadFlags aLoadFlags = nsIRequest::LOAD_NORMAL,
    nsIIOService* aIoService = nullptr);

// See NS_NewChannelInternal for usage and argument description
nsresult NS_NewChannelWithTriggeringPrincipal(
    nsIChannel** outChannel, nsIURI* aUri, nsIPrincipal* aLoadingPrincipal,
    nsIPrincipal* aTriggeringPrincipal, nsSecurityFlags aSecurityFlags,
    nsContentPolicyType aContentPolicyType,
    nsICookieJarSettings* aCookieJarSettings = nullptr,
    mozilla::dom::PerformanceStorage* aPerformanceStorage = nullptr,
    nsILoadGroup* aLoadGroup = nullptr,
    nsIInterfaceRequestor* aCallbacks = nullptr,
    nsLoadFlags aLoadFlags = nsIRequest::LOAD_NORMAL,
    nsIIOService* aIoService = nullptr);

// See NS_NewChannelInternal for usage and argument description
nsresult NS_NewChannelWithTriggeringPrincipal(
    nsIChannel** outChannel, nsIURI* aUri, nsIPrincipal* aLoadingPrincipal,
    nsIPrincipal* aTriggeringPrincipal,
    const mozilla::dom::ClientInfo& aLoadingClientInfo,
    const mozilla::Maybe<mozilla::dom::ServiceWorkerDescriptor>& aController,
    nsSecurityFlags aSecurityFlags, nsContentPolicyType aContentPolicyType,
    nsICookieJarSettings* aCookieJarSettings = nullptr,
    mozilla::dom::PerformanceStorage* aPerformanceStorage = nullptr,
    nsILoadGroup* aLoadGroup = nullptr,
    nsIInterfaceRequestor* aCallbacks = nullptr,
    nsLoadFlags aLoadFlags = nsIRequest::LOAD_NORMAL,
    nsIIOService* aIoService = nullptr);

// See NS_NewChannelInternal for usage and argument description
nsresult NS_NewChannel(
    nsIChannel** outChannel, nsIURI* aUri, nsINode* aLoadingNode,
    nsSecurityFlags aSecurityFlags, nsContentPolicyType aContentPolicyType,
    mozilla::dom::PerformanceStorage* aPerformanceStorage = nullptr,
    nsILoadGroup* aLoadGroup = nullptr,
    nsIInterfaceRequestor* aCallbacks = nullptr,
    nsLoadFlags aLoadFlags = nsIRequest::LOAD_NORMAL,
    nsIIOService* aIoService = nullptr, uint32_t aSandboxFlags = 0);

// See NS_NewChannelInternal for usage and argument description
nsresult NS_NewChannel(
    nsIChannel** outChannel, nsIURI* aUri, nsIPrincipal* aLoadingPrincipal,
    nsSecurityFlags aSecurityFlags, nsContentPolicyType aContentPolicyType,
    nsICookieJarSettings* aCookieJarSettings = nullptr,
    mozilla::dom::PerformanceStorage* aPerformanceStorage = nullptr,
    nsILoadGroup* aLoadGroup = nullptr,
    nsIInterfaceRequestor* aCallbacks = nullptr,
    nsLoadFlags aLoadFlags = nsIRequest::LOAD_NORMAL,
    nsIIOService* aIoService = nullptr, uint32_t aSandboxFlags = 0);

// See NS_NewChannelInternal for usage and argument description
nsresult NS_NewChannel(
    nsIChannel** outChannel, nsIURI* aUri, nsIPrincipal* aLoadingPrincipal,
    const mozilla::dom::ClientInfo& aLoadingClientInfo,
    const mozilla::Maybe<mozilla::dom::ServiceWorkerDescriptor>& aController,
    nsSecurityFlags aSecurityFlags, nsContentPolicyType aContentPolicyType,
    nsICookieJarSettings* aCookieJarSettings = nullptr,
    mozilla::dom::PerformanceStorage* aPerformanceStorage = nullptr,
    nsILoadGroup* aLoadGroup = nullptr,
    nsIInterfaceRequestor* aCallbacks = nullptr,
    nsLoadFlags aLoadFlags = nsIRequest::LOAD_NORMAL,
    nsIIOService* aIoService = nullptr, uint32_t aSandboxFlags = 0,
    uint64_t aAssociatedBrowsingContextID = 0);

nsresult NS_GetIsDocumentChannel(nsIChannel* aChannel, bool* aIsDocument);

nsresult NS_MakeAbsoluteURI(nsACString& result, const nsACString& spec,
                            nsIURI* baseURI);

nsresult NS_MakeAbsoluteURI(char** result, const char* spec, nsIURI* baseURI);

nsresult NS_MakeAbsoluteURI(nsAString& result, const nsAString& spec,
                            nsIURI* baseURI);

/**
 * This function is a helper function to get a scheme's default port.
 */
int32_t NS_GetDefaultPort(const char* scheme,
                          nsIIOService* ioService = nullptr);

/**
 * The UTS #46 ToASCII operation as parametrized by the WHATWG URL Standard.
 *
 * Use this function to prepare a host name for network protocols.
 *
 * Do not try to optimize and avoid calling this function if you already
 * have ASCII. This function optimizes internally, and calling it is
 * required for correctness!
 *
 * Use `NS_DomainToDisplayAndASCII` if you need both this function and
 * `NS_DomainToDisplay` together.
 *
 * The function is available to privileged JavaScript callers via
 * nsIIDNService.
 *
 * Rust callers that don't happen to be using XPCOM strings are better
 * off using the `idna` crate directly.
 */
inline nsresult NS_DomainToASCII(const nsACString& aDomain,
                                 nsACString& aASCII) {
  return mozilla_net_domain_to_ascii_impl(&aDomain, false, &aASCII);
}

/**
 * Bogus variant for callers that try pass through IPv6 addresses or even port
 * numbers!
 */
inline nsresult NS_DomainToASCIIAllowAnyGlyphfulASCII(const nsACString& aDomain,
                                                      nsACString& aASCII) {
  return mozilla_net_domain_to_ascii_impl(&aDomain, true, &aASCII);
}

/**
 * The UTS #46 ToUnicode operation as parametrized by the WHATWG URL Standard,
 * except potentially misleading labels are treated according to ToASCII
 * instead.
 *
 * Use this function to prepare a host name for display to the user.
 *
 * Use `NS_DomainToDisplayAndASCII` if you need both this function and
 * `NS_DomainToASCII` together.
 *
 * The function is available to privileged JavaScript callers via
 * nsIIDNService.
 *
 * Rust callers that don't happen to be using XPCOM strings are better
 * off using the `idna` crate directly. (See `idna_glue` for what policy
 * closure to use.)
 */
inline nsresult NS_DomainToDisplay(const nsACString& aDomain,
                                   nsACString& aDisplay) {
  return mozilla_net_domain_to_display_impl(&aDomain, false, &aDisplay);
}

/**
 * Bogus variant for callers that try pass through IPv6 addresses or even port
 * numbers!
 */
inline nsresult NS_DomainToDisplayAllowAnyGlyphfulASCII(
    const nsACString& aDomain, nsACString& aDisplay) {
  return mozilla_net_domain_to_display_impl(&aDomain, true, &aDisplay);
}

/**
 * The UTS #46 ToUnicode operation as parametrized by the WHATWG URL Standard.
 *
 * It's most likely INCORRECT to call this function, and `NS_DomainToDisplay`
 * should typically be called instead. Please avoid adding new callers, so
 * that this conversion could be removed entirely!
 *
 * The function is available to privileged JavaScript callers via
 * nsIIDNService.
 *
 * Rust callers that don't happen to be using XPCOM strings are better
 * off using the `idna` crate directly.
 */
inline nsresult NS_DomainToUnicode(const nsACString& aDomain,
                                   nsACString& aUnicode) {
  return mozilla_net_domain_to_unicode_impl(&aDomain, false, &aUnicode);
}

/**
 * Bogus variant for callers that try pass through IPv6 addresses or even port
 * numbers!
 */
inline nsresult NS_DomainToUnicodeAllowAnyGlyphfulASCII(
    const nsACString& aDomain, nsACString& aDisplay) {
  return mozilla_net_domain_to_unicode_impl(&aDomain, true, &aDisplay);
}

/**
 * This function is a helper function to get a protocol's default port if the
 * URI does not specify a port explicitly. Returns -1 if this protocol has no
 * concept of ports or if there was an error getting the port.
 */
int32_t NS_GetRealPort(nsIURI* aURI);

nsresult NS_NewInputStreamChannelInternal(
    nsIChannel** outChannel, nsIURI* aUri,
    already_AddRefed<nsIInputStream> aStream, const nsACString& aContentType,
    const nsACString& aContentCharset, nsILoadInfo* aLoadInfo);

nsresult NS_NewInputStreamChannelInternal(
    nsIChannel** outChannel, nsIURI* aUri,
    already_AddRefed<nsIInputStream> aStream, const nsACString& aContentType,
    const nsACString& aContentCharset, nsINode* aLoadingNode,
    nsIPrincipal* aLoadingPrincipal, nsIPrincipal* aTriggeringPrincipal,
    nsSecurityFlags aSecurityFlags, nsContentPolicyType aContentPolicyType);

nsresult NS_NewInputStreamChannel(nsIChannel** outChannel, nsIURI* aUri,
                                  already_AddRefed<nsIInputStream> aStream,
                                  nsIPrincipal* aLoadingPrincipal,
                                  nsSecurityFlags aSecurityFlags,
                                  nsContentPolicyType aContentPolicyType,
                                  const nsACString& aContentType = ""_ns,
                                  const nsACString& aContentCharset = ""_ns);

nsresult NS_NewInputStreamChannelInternal(
    nsIChannel** outChannel, nsIURI* aUri, const nsAString& aData,
    const nsACString& aContentType, nsINode* aLoadingNode,
    nsIPrincipal* aLoadingPrincipal, nsIPrincipal* aTriggeringPrincipal,
    nsSecurityFlags aSecurityFlags, nsContentPolicyType aContentPolicyType,
    bool aIsSrcdocChannel = false);

nsresult NS_NewInputStreamChannelInternal(nsIChannel** outChannel, nsIURI* aUri,
                                          const nsAString& aData,
                                          const nsACString& aContentType,
                                          nsILoadInfo* aLoadInfo,
                                          bool aIsSrcdocChannel = false);

nsresult NS_NewInputStreamChannel(nsIChannel** outChannel, nsIURI* aUri,
                                  const nsAString& aData,
                                  const nsACString& aContentType,
                                  nsIPrincipal* aLoadingPrincipal,
                                  nsSecurityFlags aSecurityFlags,
                                  nsContentPolicyType aContentPolicyType,
                                  bool aIsSrcdocChannel = false);

nsresult NS_NewInputStreamPump(
    nsIInputStreamPump** aResult, already_AddRefed<nsIInputStream> aStream,
    uint32_t aSegsize = 0, uint32_t aSegcount = 0, bool aCloseWhenDone = false,
    nsISerialEventTarget* aMainThreadTarget = nullptr);

nsresult NS_NewLoadGroup(nsILoadGroup** result, nsIRequestObserver* obs);

// Create a new nsILoadGroup that will match the given principal.
nsresult NS_NewLoadGroup(nsILoadGroup** aResult, nsIPrincipal* aPrincipal);

// Determine if the given loadGroup/principal pair will produce a principal
// with similar permissions when passed to NS_NewChannel().  This checks for
// things like making sure the browser element flag matches.  Without
// an appropriate load group these values can be lost when getting the result
// principal back out of the channel.  Null principals are also always allowed
// as they do not have permissions to actually use the load group.
bool NS_LoadGroupMatchesPrincipal(nsILoadGroup* aLoadGroup,
                                  nsIPrincipal* aPrincipal);

nsresult NS_NewDownloader(nsIStreamListener** result,
                          nsIDownloadObserver* observer,
                          nsIFile* downloadLocation = nullptr);

nsresult NS_NewStreamLoader(nsIStreamLoader** result,
                            nsIStreamLoaderObserver* observer,
                            nsIRequestObserver* requestObserver = nullptr);

nsresult NS_NewIncrementalStreamLoader(
    nsIIncrementalStreamLoader** result,
    nsIIncrementalStreamLoaderObserver* observer);

nsresult NS_NewStreamLoaderInternal(
    nsIStreamLoader** outStream, nsIURI* aUri,
    nsIStreamLoaderObserver* aObserver, nsINode* aLoadingNode,
    nsIPrincipal* aLoadingPrincipal, nsSecurityFlags aSecurityFlags,
    nsContentPolicyType aContentPolicyType, nsILoadGroup* aLoadGroup = nullptr,
    nsIInterfaceRequestor* aCallbacks = nullptr,
    nsLoadFlags aLoadFlags = nsIRequest::LOAD_NORMAL);

nsresult NS_NewStreamLoader(nsIStreamLoader** outStream, nsIURI* aUri,
                            nsIStreamLoaderObserver* aObserver,
                            nsINode* aLoadingNode,
                            nsSecurityFlags aSecurityFlags,
                            nsContentPolicyType aContentPolicyType,
                            nsILoadGroup* aLoadGroup = nullptr,
                            nsIInterfaceRequestor* aCallbacks = nullptr,
                            nsLoadFlags aLoadFlags = nsIRequest::LOAD_NORMAL);

nsresult NS_NewStreamLoader(nsIStreamLoader** outStream, nsIURI* aUri,
                            nsIStreamLoaderObserver* aObserver,
                            nsIPrincipal* aLoadingPrincipal,
                            nsSecurityFlags aSecurityFlags,
                            nsContentPolicyType aContentPolicyType,
                            nsILoadGroup* aLoadGroup = nullptr,
                            nsIInterfaceRequestor* aCallbacks = nullptr,
                            nsLoadFlags aLoadFlags = nsIRequest::LOAD_NORMAL);

nsresult NS_NewSyncStreamListener(nsIStreamListener** result,
                                  nsIInputStream** stream);

/**
 * Implement the nsIChannel::Open(nsIInputStream**) method using the channel's
 * AsyncOpen method.
 *
 * NOTE: Reading from the returned nsIInputStream may spin the current
 * thread's event queue, which could result in any event being processed.
 */
nsresult NS_ImplementChannelOpen(nsIChannel* channel, nsIInputStream** result);

nsresult NS_NewRequestObserverProxy(nsIRequestObserver** result,
                                    nsIRequestObserver* observer,
                                    nsISupports* context);

nsresult NS_NewSimpleStreamListener(nsIStreamListener** result,
                                    nsIOutputStream* sink,
                                    nsIRequestObserver* observer = nullptr);

nsresult NS_CheckPortSafety(int32_t port, const char* scheme,
                            nsIIOService* ioService = nullptr);

// Determine if this URI is using a safe port.
nsresult NS_CheckPortSafety(nsIURI* uri);

nsresult NS_GetFileProtocolHandler(nsIFileProtocolHandler** result,
                                   nsIIOService* ioService = nullptr);

nsresult NS_GetFileFromURLSpec(const nsACString& inURL, nsIFile** result,
                               nsIIOService* ioService = nullptr);

nsresult NS_GetURLSpecFromFile(nsIFile* file, nsACString& url,
                               nsIIOService* ioService = nullptr);

/**
 * Converts the nsIFile to the corresponding URL string.
 * Should only be called on files which are not directories,
 * is otherwise identical to NS_GetURLSpecFromFile, but is
 * usually more efficient.
 * Warning: this restriction may not be enforced at runtime!
 */
nsresult NS_GetURLSpecFromActualFile(nsIFile* file, nsACString& url,
                                     nsIIOService* ioService = nullptr);

/**
 * Converts the nsIFile to the corresponding URL string.
 * Should only be called on files which are directories,
 * is otherwise identical to NS_GetURLSpecFromFile, but is
 * usually more efficient.
 * Warning: this restriction may not be enforced at runtime!
 */
nsresult NS_GetURLSpecFromDir(nsIFile* file, nsACString& url,
                              nsIIOService* ioService = nullptr);

/**
 * Obtains the referrer for a given channel.  This first tries to obtain the
 * referrer from the property docshell.internalReferrer, and if that doesn't
 * work and the channel is an nsIHTTPChannel, we check it's referrer property.
 *
 */
void NS_GetReferrerFromChannel(nsIChannel* channel, nsIURI** referrer);

nsresult NS_ParseRequestContentType(const nsACString& rawContentType,
                                    nsCString& contentType,
                                    nsCString& contentCharset);

nsresult NS_ParseResponseContentType(const nsACString& rawContentType,
                                     nsCString& contentType,
                                     nsCString& contentCharset);

nsresult NS_ExtractCharsetFromContentType(const nsACString& rawContentType,
                                          nsCString& contentCharset,
                                          bool* hadCharset,
                                          int32_t* charsetStart,
                                          int32_t* charsetEnd);

nsresult NS_NewLocalFileInputStream(nsIInputStream** result, nsIFile* file,
                                    int32_t ioFlags = -1, int32_t perm = -1,
                                    int32_t behaviorFlags = 0);

mozilla::Result<nsCOMPtr<nsIInputStream>, nsresult> NS_NewLocalFileInputStream(
    nsIFile* file, int32_t ioFlags = -1, int32_t perm = -1,
    int32_t behaviorFlags = 0);

nsresult NS_NewLocalFileOutputStream(nsIOutputStream** result, nsIFile* file,
                                     int32_t ioFlags = -1, int32_t perm = -1,
                                     int32_t behaviorFlags = 0);

mozilla::Result<nsCOMPtr<nsIOutputStream>, nsresult>
NS_NewLocalFileOutputStream(nsIFile* file, int32_t ioFlags = -1,
                            int32_t perm = -1, int32_t behaviorFlags = 0);

nsresult NS_NewLocalFileOutputStream(nsIOutputStream** result,
                                     const mozilla::ipc::FileDescriptor& fd);

// returns a file output stream which can be QI'ed to nsISafeOutputStream.
nsresult NS_NewAtomicFileOutputStream(nsIOutputStream** result, nsIFile* file,
                                      int32_t ioFlags = -1, int32_t perm = -1,
                                      int32_t behaviorFlags = 0);

// returns a file output stream which can be QI'ed to nsISafeOutputStream.
nsresult NS_NewSafeLocalFileOutputStream(nsIOutputStream** result,
                                         nsIFile* file, int32_t ioFlags = -1,
                                         int32_t perm = -1,
                                         int32_t behaviorFlags = 0);

nsresult NS_NewLocalFileRandomAccessStream(nsIRandomAccessStream** result,
                                           nsIFile* file, int32_t ioFlags = -1,
                                           int32_t perm = -1,
                                           int32_t behaviorFlags = 0);

mozilla::Result<nsCOMPtr<nsIRandomAccessStream>, nsresult>
NS_NewLocalFileRandomAccessStream(nsIFile* file, int32_t ioFlags = -1,
                                  int32_t perm = -1, int32_t behaviorFlags = 0);

[[nodiscard]] nsresult NS_NewBufferedInputStream(
    nsIInputStream** aResult, already_AddRefed<nsIInputStream> aInputStream,
    uint32_t aBufferSize);

mozilla::Result<nsCOMPtr<nsIInputStream>, nsresult> NS_NewBufferedInputStream(
    already_AddRefed<nsIInputStream> aInputStream, uint32_t aBufferSize);

// note: the resulting stream can be QI'ed to nsISafeOutputStream iff the
// provided stream supports it.
nsresult NS_NewBufferedOutputStream(
    nsIOutputStream** aResult, already_AddRefed<nsIOutputStream> aOutputStream,
    uint32_t aBufferSize);

/**
 * This function reads an inputStream and stores its content into a buffer. In
 * general, you should avoid using this function because, it blocks the current
 * thread until the operation is done.
 * If the inputStream is async, the reading happens on an I/O thread.
 *
 * @param aInputStream the inputStream.
 * @param aDest the destination buffer. if *aDest is null, it will be allocated
 *              with the size of the written data. if aDest is not null, aCount
 *              must greater than 0.
 * @param aCount the amount of data to read. Use -1 if you want that all the
 *               stream is read.
 * @param aWritten this pointer will be used to store the number of data
 *                 written in the buffer. If you don't need, pass nullptr.
 */
nsresult NS_ReadInputStreamToBuffer(nsIInputStream* aInputStream, void** aDest,
                                    int64_t aCount,
                                    uint64_t* aWritten = nullptr);

/**
 * See the comment for NS_ReadInputStreamToBuffer
 */
nsresult NS_ReadInputStreamToString(nsIInputStream* aInputStream,
                                    nsACString& aDest, int64_t aCount,
                                    uint64_t* aWritten = nullptr);

nsresult NS_LoadPersistentPropertiesFromURISpec(
    nsIPersistentProperties** outResult, const nsACString& aSpec);

/**
 * NS_QueryNotificationCallbacks implements the canonical algorithm for
 * querying interfaces from a channel's notification callbacks.  It first
 * searches the channel's notificationCallbacks attribute, and if the interface
 * is not found there, then it inspects the notificationCallbacks attribute of
 * the channel's loadGroup.
 *
 * Note: templatized only because nsIWebSocketChannel is currently not an
 * nsIChannel.
 */
template <class T>
inline void NS_QueryNotificationCallbacks(T* channel, const nsIID& iid,
                                          void** result) {
  MOZ_ASSERT(channel, "null channel");
  *result = nullptr;

  nsCOMPtr<nsIInterfaceRequestor> cbs;
  (void)channel->GetNotificationCallbacks(getter_AddRefs(cbs));
  if (cbs) cbs->GetInterface(iid, result);
  if (!*result) {
    // try load group's notification callbacks...
    nsCOMPtr<nsILoadGroup> loadGroup;
    (void)channel->GetLoadGroup(getter_AddRefs(loadGroup));
    if (loadGroup) {
      loadGroup->GetNotificationCallbacks(getter_AddRefs(cbs));
      if (cbs) cbs->GetInterface(iid, result);
    }
  }
}

// template helper:
// Note: "class C" templatized only because nsIWebSocketChannel is currently not
// an nsIChannel.

template <class C, class T>
inline void NS_QueryNotificationCallbacks(C* channel, nsCOMPtr<T>& result) {
  NS_QueryNotificationCallbacks(channel, NS_GET_IID(T), getter_AddRefs(result));
}

/**
 * Alternate form of NS_QueryNotificationCallbacks designed for use by
 * nsIChannel implementations.
 */
inline void NS_QueryNotificationCallbacks(nsIInterfaceRequestor* callbacks,
                                          nsILoadGroup* loadGroup,
                                          const nsIID& iid, void** result) {
  *result = nullptr;

  if (callbacks) callbacks->GetInterface(iid, result);
  if (!*result) {
    // try load group's notification callbacks...
    if (loadGroup) {
      nsCOMPtr<nsIInterfaceRequestor> cbs;
      loadGroup->GetNotificationCallbacks(getter_AddRefs(cbs));
      if (cbs) cbs->GetInterface(iid, result);
    }
  }
}

/**
 * Returns true if channel is using Private Browsing, or false if not.
 * Returns false if channel's callbacks don't implement nsILoadContext.
 */
bool NS_UsePrivateBrowsing(nsIChannel* channel);

/**
 * Returns true if the channel has visited any cross-origin URLs on any
 * URLs that it was redirected through.
 */
bool NS_HasBeenCrossOrigin(nsIChannel* aChannel, bool aReport = false);

/**
 * Returns true if the channel has a safe method.
 */
bool NS_IsSafeMethodNav(nsIChannel* aChannel);

// Unique first-party domain for separating the safebrowsing cookie.
// Note if this value is changed, code in test_cookiejars_safebrowsing.js and
// nsUrlClassifierHashCompleter.js should also be changed.
#define NECKO_SAFEBROWSING_FIRST_PARTY_DOMAIN \
  "safebrowsing.86868755-6b82-4842-b301-72671a0db32e.mozilla"

// Unique first-party domain for separating about uri.
#define ABOUT_URI_FIRST_PARTY_DOMAIN \
  "about.ef2a7dd5-93bc-417f-a698-142c3116864f.mozilla"

/**
 * Wraps an nsIAuthPrompt so that it can be used as an nsIAuthPrompt2. This
 * method is provided mainly for use by other methods in this file.
 *
 * *aAuthPrompt2 should be set to null before calling this function.
 */
void NS_WrapAuthPrompt(nsIAuthPrompt* aAuthPrompt,
                       nsIAuthPrompt2** aAuthPrompt2);

/**
 * Gets an auth prompt from an interface requestor. This takes care of wrapping
 * an nsIAuthPrompt so that it can be used as an nsIAuthPrompt2.
 */
void NS_QueryAuthPrompt2(nsIInterfaceRequestor* aCallbacks,
                         nsIAuthPrompt2** aAuthPrompt);

/**
 * Gets an nsIAuthPrompt2 from a channel. Use this instead of
 * NS_QueryNotificationCallbacks for better backwards compatibility.
 */
void NS_QueryAuthPrompt2(nsIChannel* aChannel, nsIAuthPrompt2** aAuthPrompt);

/* template helper */
template <class T>
inline void NS_QueryNotificationCallbacks(nsIInterfaceRequestor* callbacks,
                                          nsILoadGroup* loadGroup,
                                          nsCOMPtr<T>& result) {
  NS_QueryNotificationCallbacks(callbacks, loadGroup, NS_GET_IID(T),
                                getter_AddRefs(result));
}

/* template helper */
template <class T>
inline void NS_QueryNotificationCallbacks(
    const nsCOMPtr<nsIInterfaceRequestor>& aCallbacks,
    const nsCOMPtr<nsILoadGroup>& aLoadGroup, nsCOMPtr<T>& aResult) {
  NS_QueryNotificationCallbacks(aCallbacks.get(), aLoadGroup.get(), aResult);
}

/* template helper */
template <class T>
inline void NS_QueryNotificationCallbacks(const nsCOMPtr<nsIChannel>& aChannel,
                                          nsCOMPtr<T>& aResult) {
  NS_QueryNotificationCallbacks(aChannel.get(), aResult);
}

/**
 * This function returns a nsIInterfaceRequestor instance that returns the
 * same result as NS_QueryNotificationCallbacks when queried.  It is useful
 * as the value for nsISocketTransport::securityCallbacks.
 */
nsresult NS_NewNotificationCallbacksAggregation(
    nsIInterfaceRequestor* callbacks, nsILoadGroup* loadGroup,
    nsIEventTarget* target, nsIInterfaceRequestor** result);

nsresult NS_NewNotificationCallbacksAggregation(
    nsIInterfaceRequestor* callbacks, nsILoadGroup* loadGroup,
    nsIInterfaceRequestor** result);

/**
 * Helper function for testing online/offline state of the browser.
 */
bool NS_IsOffline();

/**
 * Helper functions for implementing nsINestedURI::innermostURI.
 *
 * Note that NS_DoImplGetInnermostURI is "private" -- call
 * NS_ImplGetInnermostURI instead.
 */
nsresult NS_DoImplGetInnermostURI(nsINestedURI* nestedURI, nsIURI** result);

nsresult NS_ImplGetInnermostURI(nsINestedURI* nestedURI, nsIURI** result);

/**
 * Helper function for testing whether the given URI, or any of its
 * inner URIs, has all the given protocol flags.
 */
nsresult NS_URIChainHasFlags(nsIURI* uri, uint32_t flags, bool* result);

/**
 * Helper function for getting the innermost URI for a given URI.  The return
 * value could be just the object passed in if it's not a nested URI.
 */
already_AddRefed<nsIURI> NS_GetInnermostURI(nsIURI* aURI);

/**
 * Helper function for getting the host name of the innermost URI for a given
 * URI.  The return value could be the host name of the URI passed in if it's
 * not a nested URI.
 */
inline nsresult NS_GetInnermostURIHost(nsIURI* aURI, nsACString& aHost) {
  aHost.Truncate();

  // This block is optimized in order to avoid the overhead of calling
  // NS_GetInnermostURI() which incurs a lot of overhead in terms of
  // AddRef/Release calls.
  nsCOMPtr<nsINestedURI> nestedURI = do_QueryInterface(aURI);
  if (nestedURI) {
    // We have a nested URI!
    nsCOMPtr<nsIURI> uri;
    nsresult rv = nestedURI->GetInnermostURI(getter_AddRefs(uri));
    if (NS_FAILED(rv)) {
      return rv;
    }

    rv = uri->GetAsciiHost(aHost);
    if (NS_FAILED(rv)) {
      return rv;
    }
  } else {
    // We have a non-nested URI!
    nsresult rv = aURI->GetAsciiHost(aHost);
    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  return NS_OK;
}

/**
 * Get the "final" URI for a channel.  This is either channel's load info
 * resultPrincipalURI, if set, or GetOriginalURI.  In most cases (but not all)
 * load info resultPrincipalURI, if set, corresponds to URI of the channel if
 * it's required to represent the actual principal for the channel.
 */
nsresult NS_GetFinalChannelURI(nsIChannel* channel, nsIURI** uri);

bool NS_SecurityCompareURIs(nsIURI* aSourceURI, nsIURI* aTargetURI,
                            bool aStrictFileOriginPolicy);

bool NS_URIIsLocalFile(nsIURI* aURI);

bool NS_IsInternalSameURIRedirect(nsIChannel* aOldChannel,
                                  nsIChannel* aNewChannel, uint32_t aFlags);

bool NS_IsHSTSUpgradeRedirect(nsIChannel* aOldChannel, nsIChannel* aNewChannel,
                              uint32_t aFlags);

bool NS_ShouldRemoveAuthHeaderOnRedirect(nsIChannel* aOldChannel,
                                         nsIChannel* aNewChannel,
                                         uint32_t aFlags);

// aContentParentId identifies the process requesting the link (0 for the
// parent process). The link only succeeds if the channel was registered for
// that same process.
nsresult NS_LinkRedirectChannels(uint64_t channelId, uint64_t aContentParentId,
                                 nsIParentChannel* parentChannel,
                                 nsIChannel** _result);

/**
 * Returns nsILoadInfo::EMBEDDER_POLICY_REQUIRE_CORP if `aHeader` is
 * "require-corp" and nsILoadInfo::EMBEDDER_POLICY_NULL otherwise.
 *
 * See: https://mikewest.github.io/corpp/#parsing
 */
nsILoadInfo::CrossOriginEmbedderPolicy
NS_GetCrossOriginEmbedderPolicyFromHeader(
    const nsACString& aHeader, bool aIsOriginTrialCoepCredentiallessEnabled);

/**
 * Return true if the header is a dictionary where the key force-load-at-top
 * has the value true. Otherwise, return false.
 */
bool NS_GetForceLoadAtTopFromHeader(const nsACString& aHeader);

/** Given the first (disposition) token from a Content-Disposition header,
 * tell whether it indicates the content is inline or attachment
 * @param aDispToken the disposition token from the content-disposition header
 */
uint32_t NS_GetContentDispositionFromToken(const nsAString& aDispToken);

/** Determine the disposition (inline/attachment) of the content based on the
 * Content-Disposition header
 * @param aHeader the content-disposition header (full value)
 * @param aChan the channel the header came from
 */
uint32_t NS_GetContentDispositionFromHeader(const nsACString& aHeader,
                                            nsIChannel* aChan = nullptr);

/** Extracts the filename out of a content-disposition header
 * @param aFilename [out] The filename. Can be empty on error.
 * @param aDisposition Value of a Content-Disposition header

 */
nsresult NS_GetFilenameFromDisposition(nsAString& aFilename,
                                       const nsACString& aDisposition);

/**
 * Make sure Personal Security Manager is initialized
 */
void net_EnsurePSMInit();

/**
 * Test whether a URI is "about:blank".  |uri| must not be null
 */
bool NS_IsAboutBlank(nsIURI* uri);

/**
 * Test whether a URI is "about:blank", possibly with fragment or query.  |uri|
 * must not be null
 */
bool NS_IsAboutBlankAllowQueryAndFragment(nsIURI* uri);

/**
 * Test whether a URI is "about:srcdoc".  |uri| must not be null
 */
bool NS_IsAboutSrcdoc(nsIURI* uri);

/**
 * Test whether a URI is "about:blank" or "about:srcdoc", possibly with fragment
 * or query.  |uri| must not be null
 */
bool NS_IsAboutBlankOrSrcdoc(nsIURI* uri);

/**
 * Test whether a URI has an "about", "blob", "data", "file", or an HTTP(S)
 * scheme.
 */
bool NS_IsFetchScheme(nsIURI* uri);

nsresult NS_GenerateHostPort(const nsCString& host, int32_t port,
                             nsACString& hostLine);

/**
 * Sniff the content type for a given request or a given buffer.
 *
 * aSnifferType can be either NS_CONTENT_SNIFFER_CATEGORY or
 * NS_DATA_SNIFFER_CATEGORY.  The function returns the sniffed content type
 * in the aSniffedType argument.  This argument will not be modified if the
 * content type could not be sniffed.
 */
void NS_SniffContent(const char* aSnifferType, nsIRequest* aRequest,
                     const uint8_t* aData, uint32_t aLength,
                     nsACString& aSniffedType);

/**
 * Whether the channel was created to load a srcdoc document.
 * Note that view-source:about:srcdoc is classified as a srcdoc document by
 * this function, which may not be applicable everywhere.
 */
bool NS_IsSrcdocChannel(nsIChannel* aChannel);

/**
 * Return true if the given string is a reasonable HTTP header value given the
 * definition in RFC 2616 section 4.2.  Currently we don't pay the cost to do
 * full, sctrict validation here since it would require fulling parsing the
 * value.
 */
bool NS_IsReasonableHTTPHeaderValue(const nsACString& aValue);

/**
 * Return true if the given string is a valid HTTP token per RFC 2616 section
 * 2.2.
 */
bool NS_IsValidHTTPToken(const nsACString& aToken);

/**
 * Strip the leading or trailing HTTP whitespace per fetch spec section 2.2.
 */
void NS_TrimHTTPWhitespace(const nsACString& aSource, nsACString& aDest);

template <typename Char>
constexpr bool NS_IsHTTPTokenPoint(Char aChar) {
  using UnsignedChar = typename mozilla::detail::MakeUnsignedChar<Char>::Type;
  auto c = static_cast<UnsignedChar>(aChar);
  return c == '!' || c == '#' || c == '$' || c == '%' || c == '&' ||
         c == '\'' || c == '*' || c == '+' || c == '-' || c == '.' ||
         c == '^' || c == '_' || c == '`' || c == '|' || c == '~' ||
         mozilla::IsAsciiAlphanumeric(c);
}

template <typename Char>
constexpr bool NS_IsHTTPQuotedStringTokenPoint(Char aChar) {
  using UnsignedChar = typename mozilla::detail::MakeUnsignedChar<Char>::Type;
  auto c = static_cast<UnsignedChar>(aChar);
  return c == 0x9 || (c >= ' ' && c <= '~') || mozilla::IsNonAsciiLatin1(c);
}

template <typename Char>
constexpr bool NS_IsHTTPWhitespace(Char aChar) {
  using UnsignedChar = typename mozilla::detail::MakeUnsignedChar<Char>::Type;
  auto c = static_cast<UnsignedChar>(aChar);
  return c == 0x9 || c == 0xA || c == 0xD || c == 0x20;
}

/**
 * Return true if the given request must be upgraded to HTTPS.
 * If |aResultCallback| is provided and the storage is not ready to read, the
 * result will be sent back through the callback and |aWillCallback| will be
 * true. Otherwiew, the result will be set to |aShouldUpgrade| and
 * |aWillCallback| is false.
 * A console message describing an HSTS upgrade is added to |aReportCollector|,
 * which is flushed to the web console once the loading document is known.
 */
nsresult NS_ShouldSecureUpgrade(
    nsIURI* aURI, nsILoadInfo* aLoadInfo,
    nsIConsoleReportCollector* aReportCollector,
    nsIPrincipal* aChannelResultPrincipal, bool aAllowSTS,
    const mozilla::OriginAttributes& aOriginAttributes, bool& aShouldUpgrade,
    std::function<void(bool, nsresult)>&& aResultCallback, bool& aWillCallback);

/**
 * Return true if NS_ShouldSecureUpgrade will upgrade |aURI| to https because
 * of upgrade-insecure-requests or the mixed content auto-upgrade. Unlike
 * NS_ShouldSecureUpgrade, this has no side effects and works in any process.
 */
bool NS_WillUpgradeInsecureRequest(nsIURI* aURI, nsILoadInfo* aLoadInfo);

/**
 * Returns an https URI for channels that need to go through secure upgrades.
 */
nsresult NS_GetSecureUpgradedURI(nsIURI* aURI, nsIURI** aUpgradedURI);

/**
 * Adds a "|aMechanism|: Upgrading insecure request [URL] ..." warning to
 * |aReportCollector| under the console category |aMechanism|.
 */
void NS_LogSecureUpgradeToConsole(nsIURI* aURI, nsILoadInfo* aLoadInfo,
                                  nsIConsoleReportCollector* aReportCollector,
                                  const nsACString& aMechanism);

nsresult NS_CompareLoadInfoAndLoadContext(nsIChannel* aChannel);

// The type of classification to perform.
enum class ClassifyType : uint8_t {
  SafeBrowsing,  // Perform Safe Browsing classification.
  ETP,           // Perform URL Classification for Enhanced Tracking Protection.
};

/**
 * Return true if this channel should be classified by the URL classifier.
 */
bool NS_ShouldClassifyChannel(nsIChannel* aChannel, ClassifyType aType);

/**
 * Helper to set the blocking reason on loadinfo of the channel.
 */
nsresult NS_SetRequestBlockingReason(nsIChannel* channel, uint32_t reason);
nsresult NS_SetRequestBlockingReason(nsILoadInfo* loadInfo, uint32_t reason);
nsresult NS_SetRequestBlockingReasonIfNull(nsILoadInfo* loadInfo,
                                           uint32_t reason);

namespace mozilla {
namespace net {

const static uint64_t kJS_MAX_SAFE_UINTEGER = +9007199254740991ULL;
const static int64_t kJS_MIN_SAFE_INTEGER = -9007199254740991LL;
const static int64_t kJS_MAX_SAFE_INTEGER = +9007199254740991LL;

// Make sure a 64bit value can be captured by JS MAX_SAFE_INTEGER
bool InScriptableRange(int64_t val);

// Make sure a 64bit value can be captured by JS MAX_SAFE_INTEGER
bool InScriptableRange(uint64_t val);

/**
 * Given the value of a single header field  (such as
 * Content-Disposition and Content-Type) and the name of a parameter
 * (e.g. filename, name, charset), returns the value of the parameter.
 * See nsIMIMEHeaderParam.idl for more information.
 *
 * @param  aHeaderVal        a header string to get the value of a parameter
 *                           from.
 * @param  aParamName        the name of a MIME header parameter (e.g.
 *                           filename, name, charset). If empty or nullptr,
 *                           returns the first (possibly) _unnamed_ 'parameter'.
 * @return the value of <code>aParamName</code> in Unichar(UTF-16).
 */
nsresult GetParameterHTTP(const nsACString& aHeaderVal, const char* aParamName,
                          nsAString& aResult);

/**
 * Helper function that determines if channel is an HTTP POST.
 *
 * @param aChannel
 *        The channel to test
 *
 * @return True if channel is an HTTP post.
 */
bool ChannelIsPost(nsIChannel* aChannel);

/**
 * Convenience functions for checking whether the scheme is HTTP or HTTPS.
 */
bool SchemeIsHttpOrHttps(nsIURI* aURI);
bool SchemeIsHttpOrHttps(nsIPrincipal* aPrincipal);

// Helper functions for SetProtocol methods to follow
// step 2.1 in https://url.spec.whatwg.org/#scheme-state
bool SchemeIsSpecial(const nsACString&);
bool IsSchemeChangePermitted(nsIURI*, const nsACString&);
already_AddRefed<nsIURI> TryChangeProtocol(nsIURI*, const nsACString&);

struct LinkHeader {
  nsString mHref;
  nsString mRel;
  nsString mTitle;
  nsString mNonce;
  nsString mIntegrity;
  nsString mSrcset;
  nsString mSizes;
  nsString mType;
  nsString mMedia;
  nsString mAnchor;
  nsString mCrossOrigin;
  nsString mReferrerPolicy;
  nsString mAs;
  nsString mFetchPriority;

  LinkHeader();
  void Reset();

  nsresult NewResolveHref(nsIURI** aOutURI, nsIURI* aBaseURI) const;

  bool operator==(const LinkHeader& rhs) const;

  void MaybeUpdateAttribute(const nsAString& aAttribute,
                            const char16_t* aValue);

  auto MutTiedFields() {
    return std::tie(mHref, mRel, mTitle, mNonce, mIntegrity, mSrcset, mSizes,
                    mType, mMedia, mAnchor, mCrossOrigin, mReferrerPolicy, mAs,
                    mFetchPriority);
  }
};

// Implements roughly step 2 to 4 of
// <https://httpwg.org/specs/rfc8288.html#parse-set>.
nsTArray<LinkHeader> ParseLinkHeader(const nsAString& aLinkData);

enum ASDestination : uint8_t {
  DESTINATION_INVALID,
  DESTINATION_AUDIO,
  DESTINATION_DOCUMENT,
  DESTINATION_EMBED,
  DESTINATION_FONT,
  DESTINATION_IMAGE,
  DESTINATION_MANIFEST,
  DESTINATION_OBJECT,
  DESTINATION_REPORT,
  DESTINATION_SCRIPT,
  DESTINATION_SERVICEWORKER,
  DESTINATION_SHAREDWORKER,
  DESTINATION_STYLE,
  DESTINATION_TRACK,
  DESTINATION_VIDEO,
  DESTINATION_WORKER,
  DESTINATION_XSLT,
  DESTINATION_FETCH,
  DESTINATION_JSON,
  DESTINATION_TEXT
};

void ParseAsValue(const nsAString& aValue, nsAttrValue& aResult);
nsContentPolicyType AsValueToContentPolicy(const nsAttrValue& aValue);
bool IsScriptLikeOrInvalid(const nsAString& aAs);

bool CheckPreloadAttrs(const nsAttrValue& aAs, const nsAString& aType,
                       const nsAString& aMedia,
                       mozilla::dom::Document* aDocument);
void WarnIgnoredPreload(const mozilla::dom::Document& aDoc, nsIURI* aURI,
                        const nsAString& aSrcset = nsString());

// Implements parsing of Use-As-Dictionary headers for Compression Dictionary
// support.
bool NS_ParseUseAsDictionary(const nsACString& aValue, nsACString& aMatch,
                             nsACString& aMatchId,
                             nsTArray<nsCString>& aMatchDestItems,
                             nsACString& aType);

/**
 * Returns true if the |aInput| in is part of the root domain of |aHost|.
 * For example, if |aInput| is "www.mozilla.org", and we pass in
 * "mozilla.org" as |aHost|, this will return true.  It would return false
 * the other way around.
 *
 * @param aInput The host to be analyzed.
 * @param aHost  The host to compare to.
 */
nsresult HasRootDomain(const nsACString& aInput, const nsACString& aHost,
                       bool* aResult);

void CheckForBrokenChromeURL(nsILoadInfo* aLoadInfo, nsIURI* aURI);

bool IsCoepCredentiallessEnabled(bool aIsOriginTrialCoepCredentiallessEnabled);

void ParseSimpleURISchemes(const nsACString& schemeList);

nsresult AddExtraHeaders(nsIHttpChannel* aHttpChannel,
                         const nsACString& aExtraHeaders, bool aMerge = true);

// Returns the IP address space the load is initiated from, which is what a
// Local Network Access check compares the target address space against.
nsILoadInfo::IPAddressSpace GetParentIPAddressSpace(nsILoadInfo* aLoadInfo);

bool IsLocalOrPrivateNetworkAccess(
    nsILoadInfo::IPAddressSpace aParentIPAddressSpace,
    nsILoadInfo::IPAddressSpace aTargetIPAddressSpace);
bool IsPrivateNetworkAccess(
    const nsILoadInfo::IPAddressSpace aParentIPAddressSpace,
    const nsILoadInfo::IPAddressSpace aTargetIPAddressSpace);
bool IsLocalHostAccess(const nsILoadInfo::IPAddressSpace aParentIPAddressSpace,
                       const nsILoadInfo::IPAddressSpace aTargetIPAddressSpace);

enum ActivateStorageAccessVariant {
  // The server's response instructs the user agent to activate storage access
  // before continuing with the load of the resource. (This is only relevant
  // when loading a new document)
  //     Activate-Storage-Access: load
  Load,
  // The server's response instructs the user agent to activate storage access,
  // then retry the request. The "allowed-origin" parameter allowlists the
  // request's origin.
  //     Activate-Storage-Access: retry; allowed-origin="https://foo.bar"
  RetryOrigin,
  // Same as above, but using a wildcard instead of explicitly naming the
  // request's origin.
  //     Activate-Storage-Access: retry; allowed-origin=*
  RetryAny,
};

struct ActivateStorageAccess {
  ActivateStorageAccessVariant variant;
  // string from allowed-origin in case ActivateOrigin of ActivateOrigin-Variant
  // above
  nsCString origin;
};

Result<ActivateStorageAccess, nsresult> ParseActivateStorageAccess(
    const nsACString& aActivateStorageAcess);

}  // namespace net
}  // namespace mozilla

#endif  // !nsNetUtil_h_
