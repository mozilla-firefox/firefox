/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsAppShell_h_
#define nsAppShell_h_

#ifdef MOZ_ENABLE_DBUS
#  include <gio/gio.h>
#  include "mozilla/RefPtr.h"
#  include "mozilla/GRefPtr.h"
#endif
#include <glib.h>
#include "nsBaseAppShell.h"

typedef enum {
  eSessionDefault = 0,
  eSessionRestoring = 1,
  eSessionRestoreFinished = 2,
} SessionRestoreState;

class nsAppShell : public nsBaseAppShell {
  static nsAppShell* sAppShell;

 public:
  nsAppShell() = default;

  // nsBaseAppShell overrides:
  nsresult Init();
  NS_IMETHOD Run() override;

  static SessionRestoreState UpdateAndGetSessionState();

  void ScheduleNativeEventCallback() override;
  bool ProcessNextNativeEvent(bool mayWait) override;

#ifdef MOZ_ENABLE_DBUS
  void StartDBusListening();
  void StopDBusListening();
  void RegisterHostApp();

  static void DBusSessionSleepCallback(GDBusProxy* aProxy, gchar* aSenderName,
                                       gchar* aSignalName,
                                       GVariant* aParameters,
                                       gpointer aUserData);
  static void DBusTimedatePropertiesChangedCallback(GDBusProxy* aProxy,
                                                    gchar* aSenderName,
                                                    gchar* aSignalName,
                                                    GVariant* aParameters,
                                                    gpointer aUserData);
  static void DBusConnectClientResponse(GObject* aObject, GAsyncResult* aResult,
                                        gpointer aUserData);
  static void DBusConnectionCheck();
  static void SetSessionDBus(GDBusConnection* aDBusConnectionSession);
  static void SetSystemDBus(GDBusConnection* aDBusConnectionSystem);
#endif

  static void InstallTermSignalHandler();

 private:
  virtual ~nsAppShell();

  NS_IMETHOD Observe(nsISupports* aSubject, const char* aTopic,
                     const char16_t* aData) override;
  static gboolean EventProcessorCallback(GIOChannel* source,
                                         GIOCondition condition, gpointer data);
  static void TermSignalHandler(int signo);

  void ScheduleQuitEvent();

  int mPipeFDs[2] = {0, 0};
  unsigned mTag = 0;
  bool mInitialized = false;

  SessionRestoreState mSessionRestoreState = eSessionDefault;

#ifdef MOZ_ENABLE_DBUS
  RefPtr<GDBusProxy> mLogin1Proxy;
  RefPtr<GCancellable> mLogin1ProxyCancellable;
  RefPtr<GDBusProxy> mTimedate1Proxy;
  RefPtr<GCancellable> mTimedate1ProxyCancellable;
  RefPtr<GDBusConnection> mDBusConnectionSession;
  RefPtr<GDBusConnection> mDBusConnectionSystem;
  RefPtr<GCancellable> mDBusGetCancellableSession;
  RefPtr<GCancellable> mDBusGetCancellableSystem;
#endif
};

#endif /* nsAppShell_h_ */
