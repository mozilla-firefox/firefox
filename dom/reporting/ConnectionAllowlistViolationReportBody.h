/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_ConnectionAllowlistViolationReportBody_h
#define mozilla_dom_ConnectionAllowlistViolationReportBody_h

#include "mozilla/dom/ReportBody.h"
#include "mozilla/dom/ReportingBinding.h"
#include "nsTArray.h"

namespace mozilla::dom {

// The body of a `connection-allowlist` report. The specification calls this
// dictionary ConnectionAllowlistViolationReport.
// https://w3c.github.io/connection-allowlists/#reporting
class ConnectionAllowlistViolationReportBody final : public ReportBody {
 public:
  ConnectionAllowlistViolationReportBody(
      nsIGlobalObject* aGlobal, const nsACString& aURL,
      const nsACString& aConnection, nsTArray<nsCString>&& aAllowlist,
      ConnectionAllowlistDisposition aDisposition);

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

  void GetUrl(nsACString& aURL) const;

  void GetConnection(nsACString& aConnection) const;

  void GetAllowlist(nsTArray<nsCString>& aAllowlist) const;

  ConnectionAllowlistDisposition Disposition() const;

 protected:
  void ToJSON(JSONWriter& aJSONWriter) const override;

 private:
  ~ConnectionAllowlistViolationReportBody();

  const nsCString mURL;
  const nsCString mConnection;
  const nsTArray<nsCString> mAllowlist;
  const ConnectionAllowlistDisposition mDisposition;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_ConnectionAllowlistViolationReportBody_h
