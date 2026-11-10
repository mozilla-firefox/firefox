/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_BlobURLProtocolHandler_h
#define mozilla_dom_BlobURLProtocolHandler_h

#include <functional>

#include "mozilla/Result.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/ThreadSafeWeakPtr.h"
#include "mozilla/dom/ipc/IdType.h"
#include "nsCOMPtr.h"
#include "nsIProtocolHandler.h"
#include "nsIURI.h"
#include "nsTArray.h"
#include "nsWeakReference.h"

#define BLOBURI_SCHEME "blob"

class nsIPrincipal;

namespace mozilla {
class BlobURLsReporter;
class OriginAttributes;

namespace dom {

class BlobImpl;
class BlobURL;
class BlobURLRegistrationData;
class BlobURLNullOriginEntry;
class ContentParent;
class MediaSource;

class BlobURLProtocolHandler final : public nsIProtocolHandler,
                                     public nsSupportsWeakReference {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIPROTOCOLHANDLER

  BlobURLProtocolHandler();

  static nsresult CreateNewURI(const nsACString& aSpec, const char* aCharset,
                               nsIURI* aBaseURI, nsIURI** result);

  // Methods for managing uri->object mapping
  // AddDataEntry creates the URI with the given scheme and returns it in aUri
  static nsresult AddDataEntry(BlobImpl*, nsIPrincipal*,
                               const nsCString& aPartitionKey,
                               nsACString& aUri);
  // IPC only (parent process)
  static nsresult AddDataEntryParent(const nsACString& aURI,
                                     nsIPrincipal* aPrincipal,
                                     const nsCString& aPartitionKey,
                                     BlobImpl* aBlobImpl,
                                     const ContentParentId& aContentParentId);

  // IPC only (content process)
  static nsresult AddDataEntryChild(const nsACString& aURI,
                                    nsIPrincipal* aPrincipal,
                                    const nsCString& aPartitionKey);

  // These methods revoke a list of blobURLs. Because some operations could
  // still be in progress, the revoking consists in marking the blobURL as
  // revoked and in removing it after RELEASING_TIMER milliseconds.
  static void RemoveDataEntries(const nsTArray<nsCString>& aUris,
                                bool aBroadcastToOTherProcesses = true);
  static void RemoveDataEntriesPerContentParent(
      const ContentParentId& aContentParentId);
  // Returns true if the entry was allowed to be removed.
  static bool RemoveDataEntry(const nsACString& aUri, nsIPrincipal* aPrincipal,
                              const nsCString& aPartitionKey);

  static void RemoveDataEntries();

  static bool HasDataEntryTypeBlob(const nsACString& aUri);

  static bool GetDataEntry(const nsACString& aUri, BlobImpl** aBlobImpl,
                           nsIPrincipal* aLoadingPrincipal,
                           nsIPrincipal* aTriggeringPrincipal,
                           const OriginAttributes& aOriginAttributes,
                           uint64_t aInnerWindowId,
                           const nsCString& aPartitionKey,
                           bool aAlsoIfRevoked = false);

  // Main-thread only method to invoke a helper function that gets called for
  // every known and recently revoked Blob URL. The helper function should
  // return true to keep going or false to stop enumerating (presumably because
  // of an unexpected XPCOM or IPC error). This method returns false if already
  // shutdown or if the helper method returns false, true otherwise.
  static bool ForEachBlobURL(
      std::function<bool(BlobImpl*, nsIPrincipal*, const nsCString&,
                         const nsACString&, bool aRevoked)>&& aCb);

  // This method extracts principal information from the given Blob URL, and
  // returns false if the principal cannot be determined.
  //
  // NOTE: This function does not confirm that a given Blob URL is valid and/or
  // non-revoked. This should only be checked by trying to load the Blob URL
  // using a channel.
  //
  // NOTE: The principal returned by this function may have different
  // OriginAttributes than the "true" principal of the underlying blob.
  static bool GetBlobURLPrincipal(nsIURI* aURI, const OriginAttributes& aAttrs,
                                  nsIPrincipal** aPrincipal);

  // Check if metadata about Blob URLs created with this principal should be
  // broadcast into every content process. This is currently the case for
  // extension blob URLs and system principal blob URLs, as they can be loaded
  // by system code and content scripts respectively.
  static bool IsBlobURLBroadcastPrincipal(nsIPrincipal* aPrincipal);

  // If principal is not null, its origin will be used to generate the URI.
  static nsresult GenerateURIString(nsIPrincipal* aPrincipal, nsACString& aUri);
  static nsresult GetURIPrefix(nsIPrincipal* aPrincipal,
                               nsACString& aUriPrefix);

  static bool IsBlobURLValid(nsIPrincipal* aPrincipal, const nsACString& aSpec);

  static nsresult GetBlobURLProtocolState(
      const nsACString& aSpecIgnoringRef, bool* aRevoked,
      BlobURLNullOriginEntry** aNullOriginEntry);

 private:
  ~BlobURLProtocolHandler();

  static void Init();
};

// Entry tracking the association between a `blob:null/` URI and its principal.
class BlobURLNullOriginEntry final
    : public SupportsThreadSafeWeakPtr<BlobURLNullOriginEntry> {
 public:
  MOZ_DECLARE_REFCOUNTED_TYPENAME(BlobURLNullOriginEntry)

  const nsCString mSpec;
  const nsCOMPtr<nsIPrincipal> mPrincipal;

  static Result<RefPtr<BlobURLNullOriginEntry>, nsresult> Intern(
      const nsACString& aSpec, nsIPrincipal* aPrincipal);

 private:
  friend class SupportsThreadSafeWeakPtr<BlobURLNullOriginEntry>;

  BlobURLNullOriginEntry(const nsACString& aSpec, nsIPrincipal* aPrincipal);
  ~BlobURLNullOriginEntry();
};

bool IsBlobURI(nsIURI* aUri);

}  // namespace dom
}  // namespace mozilla

#endif /* mozilla_dom_BlobURLProtocolHandler_h */
