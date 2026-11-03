/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _nsDBusRemoteServer_h_
#define _nsDBusRemoteServer_h_

#include "nsRemoteServer.h"
#include "nsUnixRemoteServer.h"
#include "mozilla/Span.h"

#include <gio/gio.h>
#include "mozilla/RefPtr.h"
#include "mozilla/GRefPtr.h"

class nsDBusRemoteServer final : public nsRemoteServer,
                                 public nsUnixRemoteServer {
 public:
  ~nsDBusRemoteServer() override { Shutdown(); }

  nsresult Startup(const char* aAppName, const char* aProfileName) override;
  void Shutdown() override;

  void OnBusAcquired(GDBusConnection* aConnection);

  bool HandleOpenURL(const gchar* aInterfaceName, const gchar* aMethodName,
                     mozilla::Span<const gchar> aParam);

 private:
  uint mDBusID = 0;
  uint mRegistrationId = 0;
  RefPtr<GDBusConnection> mConnection;
  RefPtr<GDBusNodeInfo> mIntrospectionData;

  nsCString mAppName;
  nsCString mPathName;
};

#endif  // _nsDBusRemoteServer_h_
