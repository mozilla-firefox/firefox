/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Base class for the XML and HTML content sinks, which construct a
 * DOM based on information from the parser.
 */

#ifndef _nsContentSink_h_
#define _nsContentSink_h_

// Base class for contentsink implementations.

#include "mozilla/Attributes.h"
#include "mozilla/Logging.h"
#include "mozilla/StaticPrefs_content.h"
#include "nsCOMPtr.h"
#include "nsCycleCollectionParticipant.h"
#include "nsICSSLoaderObserver.h"
#include "nsIContentSink.h"
#include "nsString.h"
#include "nsThreadUtils.h"
#include "nsWeakReference.h"

class nsIURI;
class nsIChannel;
class nsIDocShell;
class nsAtom;
class nsIChannel;
class nsIContent;
class nsNodeInfoManager;

namespace mozilla {
namespace css {
class Loader;
}  // namespace css

namespace dom {
class Document;
class ScriptLoader;
}  // namespace dom

namespace net {
struct LinkHeader;
};
}  // namespace mozilla

//----------------------------------------------------------------------

#define NS_CONTENTSINK_IID \
  {0x7c0e56f4, 0x5e12, 0x4c41, {0xbd, 0x3a, 0x3a, 0x4e, 0x7c, 0x8f, 0x9e, 0x12}}

class nsContentSink : public nsICSSLoaderObserver,
                      public nsSupportsWeakReference {
 public:
  NS_INLINE_DECL_STATIC_IID(NS_CONTENTSINK_IID)

 protected:
  using Document = mozilla::dom::Document;

 private:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS_AMBIGUOUS(nsContentSink, nsICSSLoaderObserver)

  // nsICSSLoaderObserver
  MOZ_CAN_RUN_SCRIPT_BOUNDARY NS_IMETHOD
  StyleSheetLoaded(mozilla::StyleSheet* aSheet, bool aWasDeferred,
                   nsresult aStatus) override;

  // nsIContentSink implementation helpers
  nsresult WillParseImpl(void);
  nsresult DidProcessATokenImpl(void);
  void WillBuildModelImpl(void);
  MOZ_CAN_RUN_SCRIPT void DidBuildModelImpl(bool aTerminated);
  void DropParserAndPerfHint(void);
  bool IsScriptExecutingImpl();
  void ContinueParsingDocumentAfterCurrentScriptImpl();

 protected:
  nsContentSink();
  virtual ~nsContentSink();

  nsresult Init(Document* aDoc, nsIURI* aURI, nsISupports* aContainer,
                nsIChannel* aChannel);

  nsresult ProcessHTTPHeaders(nsIChannel* aChannel);
  // aEarlyHintPreloaderId zero means no early hint channel to connect back
  MOZ_CAN_RUN_SCRIPT_BOUNDARY nsresult ProcessLinkFromHeader(
      const mozilla::net::LinkHeader& aHeader, uint64_t aEarlyHintPreloaderId);

  // @param aFetchPriority Accepts a case-insensitive fetch priority keyword and
  //                       other values too, see
  //                       <https://html.spec.whatwg.org/#fetch-priority-attribute>.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY virtual nsresult ProcessStyleLinkFromHeader(
      const nsAString& aHref, bool aAlternate, const nsAString& aTitle,
      const nsAString& aIntegrity, const nsAString& aType,
      const nsAString& aMedia, const nsAString& aCrossOrigin,
      const nsAString& aReferrerPolicy, const nsAString& aFetchPriority);

  void PrefetchHref(const nsAString& aHref, const nsAString& aAs,
                    const nsAString& aType, const nsAString& aMedia);
  void PreloadHref(const nsAString& aHref, const nsAString& aAs,
                   const nsAString& aRel, const nsAString& aType,
                   const nsAString& aMedia, const nsAString& aNonce,
                   const nsAString& aIntegrity, const nsAString& aSrcset,
                   const nsAString& aSizes, const nsAString& aCORS,
                   const nsAString& aReferrerPolicy,
                   uint64_t aEarlyHintPreloaderId,
                   const nsAString& aFetchPriority);

  void PreloadModule(const nsAString& aHref, const nsAString& aAs,
                     const nsAString& aMedia, const nsAString& aNonce,
                     const nsAString& aIntegrity, const nsAString& aCORS,
                     const nsAString& aReferrerPolicy,
                     uint64_t aEarlyHintPreloaderId,
                     const nsAString& aFetchPriority);

  // For PrefetchDNS() aHref can either be the usual
  // URI format or of the form "//www.hostname.com" without a scheme.
  void PrefetchDNS(const nsAString& aHref);

  // Gets the cache key (used to identify items in a cache) of the channel.
  nsresult GetChannelCacheKey(nsIChannel* aChannel, nsACString& aCacheKey);

 public:
  // For Preconnect() aHref can either be the usual
  // URI format or of the form "//www.hostname.com" without a scheme.
  void Preconnect(const nsAString& aHref, const nsAString& aCrossOrigin);

 protected:
  // Tries to scroll to the URI's named anchor. Once we've successfully
  // done that, further calls to this method will be ignored.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void ScrollToRef();

  // Start layout.  If aIgnorePendingSheets is true, this will happen even if
  // we still have stylesheet loads pending.  Otherwise, we'll wait until the
  // stylesheets are all done loading.
 public:
  void StartLayout(bool aIgnorePendingSheets);

  MOZ_CAN_RUN_SCRIPT static void NotifyDocElementCreated(Document* aDoc);

  Document* GetDocument() { return mDocument; }

  // Later on we might want to make this more involved somehow
  // (e.g. stop waiting after some timeout or whatnot).
  bool WaitForPendingSheets() { return mPendingSheetCount > 0; }

 protected:
  void DoProcessLinkHeader();

  void StopDeflecting() {
    mDeflectedCount = mozilla::StaticPrefs::content_sink_perf_deflect_count();
  }

 protected:
  RefPtr<Document> mDocument;
  RefPtr<nsParserBase> mParser;
  nsCOMPtr<nsIURI> mDocumentURI;
  nsCOMPtr<nsIDocShell> mDocShell;
  RefPtr<nsNodeInfoManager> mNodeInfoManager;
  RefPtr<mozilla::dom::ScriptLoader> mScriptLoader;

  uint8_t mLayoutStarted : 1;
  uint8_t mDynamicLowerValue : 1;
  // If true, we deferred starting layout until sheets load
  uint8_t mDeferredLayoutStart : 1;
  // True if this is parser is a fragment parser or an HTML DOMParser.
  // XML DOMParser leaves this to false for now!
  uint8_t mRunsToCompletion : 1;
  // True if we are blocking load event.
  bool mIsBlockingOnload : 1;

  //
  // -- Can interrupt parsing members --
  //

  // The number of tokens that have been processed since we measured
  // if it's time to return to the main event loop.
  uint32_t mDeflectedCount;

  // Is there currently a pending event?
  bool mHasPendingEvent;

  // When to return to the main event loop
  uint32_t mCurrentParseEndTime;

  int32_t mBeginLoadTime;

  // Last mouse event or keyboard event time sampled by the content
  // sink
  uint32_t mLastSampledUserEventTime;

  uint32_t mPendingSheetCount;

  nsRevocableEventPtr<nsRunnableMethod<nsContentSink, void, false> >
      mProcessLinkHeaderEvent;
};

#endif  // _nsContentSink_h_
