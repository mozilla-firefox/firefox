/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SiteClientCertEnrollmentBackend_h
#define SiteClientCertEnrollmentBackend_h

#include <utility>

#include "ScopedNSSTypes.h"
#include "nsISiteClientCertEnrollmentBackend.h"
#include "nsString.h"
#include "nsTArray.h"

namespace mozilla::psm {

class SiteClientCertEnrollmentBackend final
    : public nsISiteClientCertEnrollmentBackend {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSISITECLIENTCERTENROLLMENTBACKEND

  SiteClientCertEnrollmentBackend() = default;

 private:
  ~SiteClientCertEnrollmentBackend() = default;

  struct PendingEnrollment {
    PendingEnrollment(nsCString&& aRequestId, nsCString&& aCsrPem,
                      UniqueSECKEYPrivateKey&& aPrivateKey)
        : mRequestId(std::move(aRequestId)),
          mCsrPem(std::move(aCsrPem)),
          mPrivateKey(std::move(aPrivateKey)) {}

    PendingEnrollment(PendingEnrollment&&) = default;
    PendingEnrollment& operator=(PendingEnrollment&&) = default;
    PendingEnrollment(const PendingEnrollment&) = delete;
    PendingEnrollment& operator=(const PendingEnrollment&) = delete;

    nsCString mRequestId;
    nsCString mCsrPem;
    UniqueSECKEYPrivateKey mPrivateKey;
  };

  PendingEnrollment* FindPendingEnrollment(const nsACString& aRequestId);

  uint64_t mNextRequestId = 0;
  nsTArray<PendingEnrollment> mPendingEnrollments;
};

}  // namespace mozilla::psm

#endif  // SiteClientCertEnrollmentBackend_h
