/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_clearsitedata_h
#define mozilla_clearsitedata_h

#include "nsIObserver.h"
#include "nsTArray.h"

class nsIHttpChannel;
class nsIPrincipal;
class nsIURI;

namespace mozilla {

class ClearSiteData final : public nsIObserver {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  static void Initialize();

 private:
  ClearSiteData();
  ~ClearSiteData() = default;

  static void Shutdown();

  class PendingCleanupHolder;

  // Starts the cleanup if the channel contains the Clear-Site-Data header and
  // if the URI is secure.
  void ClearDataFromChannel(nsIHttpChannel* aChannel);

  // From the Clear-Site-Data header, it returns a bitmap with Type values.
  uint32_t ParseHeader(nsIHttpChannel* aChannel, nsIURI* aURI) const;

  enum Type {
    eCache = 0x01,
    eCookies = 0x02,
    eStorage = 0x04,
    ePrefetchCache = 0x08,
  };

  // This method writes a console message when a cleanup operation is going to
  // be executed.
  void LogOpToConsole(nsIHttpChannel* aChannel, nsIURI* aURI, Type aType) const;

  // Logging of an unknown type value.
  void LogErrorToConsole(nsIHttpChannel* aChannel, nsIURI* aURI,
                         const nsACString& aUnknownType) const;

  void LogToConsoleInternal(nsIHttpChannel* aChannel, nsIURI* aURI,
                            const char* aMsg,
                            const nsTArray<nsString>& aParams) const;

  // This method converts a Type to the corrisponding string format.
  void TypeToString(Type aType, nsAString& aStr) const;
};

}  // namespace mozilla

#endif  // mozilla_clearsitedata_h
