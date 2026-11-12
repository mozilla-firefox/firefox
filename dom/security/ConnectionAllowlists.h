/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_ConnectionAllowlists_h_
#define mozilla_dom_ConnectionAllowlists_h_

#include <cstdint>
#include <memory>
#include <type_traits>

#include "mozilla/Maybe.h"
#include "mozilla/Variant.h"
#include "mozilla/net/urlpattern_glue.h"
#include "nsCOMPtr.h"
#include "nsISupportsImpl.h"
#include "nsIURI.h"
#include "nsString.h"
#include "nsTArray.h"

class nsPIDOMWindowInner;
class nsILoadInfo;

namespace mozilla::ipc {
class ConnectionAllowlistEntry;
class ConnectionAllowlistsArgs;
}  // namespace mozilla::ipc

namespace mozilla::dom {

// A parsed connection allowlist, holding the enforced and report-only
// allowlists delivered by the `Connection-Allowlist` and
// `Connection-Allowlist-Report-Only` response headers.
// https://w3c.github.io/connection-allowlists/
class ConnectionAllowlists final {
 public:
  NS_INLINE_DECL_REFCOUNTING(ConnectionAllowlists)

  ConnectionAllowlists() = default;

  // Parses the `Connection-Allowlist` and `Connection-Allowlist-Report-Only`
  // response header values. Returns null in |aResult| when neither header
  // yields a valid allowlist.
  static nsresult ParseHeaders(const nsACString& aHeader,
                               const nsACString& aReportOnlyHeader,
                               ConnectionAllowlists** aResult);
  void SetResponseURI(nsIURI* aURI);
  // Freeze the list after it's inherited by another document, verified via
  // debug asserts.
  void Freeze() {
#ifdef DEBUG
    mFrozen = true;
#endif
  }

  bool ShouldLoad(nsIURI* aURI, nsILoadInfo* aLoadInfo) const;

  bool ShouldBlockConnection(nsIURI* aURI, uint64_t aInnerWindowID) const;
  bool ShouldBlockRedirect(nsIURI* aOriginalURI, nsILoadInfo* aLoadInfo) const;
  bool ShouldBlockWebRTC(nsPIDOMWindowInner* aWindow) const;

  void ToArgs(mozilla::ipc::ConnectionAllowlistsArgs& aArgs) const;
  static already_AddRefed<ConnectionAllowlists> FromArgs(
      const mozilla::ipc::ConnectionAllowlistsArgs& aArgs);

 private:
  ~ConnectionAllowlists() = default;

  enum class Disposition : uint8_t { Enforce, Report };
  enum class Redirects : uint8_t { Block, Allow };
  enum class WebRTC : uint8_t { Block, Allow };

  // UrlPatternGlue is an opaque `void*` handle, so the managed pointee is
  // `void` and the handle is freed via urlpattern_pattern_free.
  struct UrlPatternDeleter {
    void operator()(UrlPatternGlue aPattern) const {
      urlpattern_pattern_free(aPattern);
    }
  };
  using UrlPattern =
      std::unique_ptr<std::remove_pointer_t<UrlPatternGlue>, UrlPatternDeleter>;

  // A single parsed "connection allowlist" struct.
  // https://w3c.github.io/connection-allowlists/#connection-allowlist
  struct Allowlist {
    // Parses |aSerializedPattern| and if successful appends it to
    // |aAllowlist|.
    void AppendPattern(const nsACString& aSerializedPattern);

    void ToEntryArgs(mozilla::ipc::ConnectionAllowlistEntry& aEntry) const;
    static Allowlist FromEntryArgs(
        const mozilla::ipc::ConnectionAllowlistEntry& aEntry,
        Disposition aDisposition);

    nsTArray<UrlPattern> mPatterns;
    // The serialized form of |mPatterns|, kept around because a parsed pattern
    // cannot be serialized back and has to be re-parsed when sent over IPC.
    nsTArray<nsCString> mSerializedPatterns;
    // Whether the `response-origin` token was present. The token is kept
    // unresolved, because an allowlist is inherited together with the policy
    // container and has to resolve against the response URL of the document
    // it is applied to, not the one that delivered the header.
    bool mMatchesResponseOrigin = false;

    // The `report-to` reporting endpoint, or empty when unset.
    nsCString mReportingEndpoint;
    Disposition mDisposition = Disposition::Enforce;
    Redirects mRedirects = Redirects::Block;
    WebRTC mWebRTC = WebRTC::Block;
  };

  bool ShouldBlockURL(nsIURI* aURI, uint64_t aInnerWindowID,
                      bool aReportToConsole) const;

  using Resource = Variant<nsIURI*, nsCString>;

  // Reports a violation of |aAllowlist| by the connection to |aURI| to the
  // reporting endpoint, and to the console if |aReportToConsole|.
  // |aIsRedirect| is whether the request violated the allowlist by being
  // redirected.
  static void ReportBlockedRequest(nsIURI* aURI, uint64_t aInnerWindowID,
                                   const Allowlist& aAllowlist,
                                   bool aReportToConsole, bool aIsRedirect);

  static void ReportViolation(const Resource& aResource,
                              nsPIDOMWindowInner* aWindow,
                              const Allowlist& aAllowlist);

  static void ReportToConsole(const Resource& aResource,
                              uint64_t aInnerWindowID,
                              const Allowlist& aAllowlist,
                              bool aIsRedirect = false);

  static Maybe<Allowlist> ParseConnectionAllowlistHeader(
      const nsACString& aHeader, Disposition aDisposition);

  bool MatchURL(nsIURI* aURI, const Allowlist& aAllowlist) const;

  Maybe<Allowlist> mEnforcement;
  Maybe<Allowlist> mReportOnly;
  nsCOMPtr<nsIURI> mResponseURI;
#ifdef DEBUG
  bool mFrozen = false;
#endif
};

}  // namespace mozilla::dom

#endif /* mozilla_dom_ConnectionAllowlists_h_ */
