/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <gdk/gdk.h>
#include "nsAppShell.h"
#include "nsBaseAppShell.h"
#include "nsWindow.h"
#include "mozilla/Logging.h"
#include "prenv.h"
#include "mozilla/BackgroundHangMonitor.h"
#include "mozilla/Hal.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/ProfilerThreadSleep.h"
#include "mozilla/GUniquePtr.h"
#include "mozilla/GRefPtr.h"
#include "mozilla/StaticPrefs_widget.h"
#include "mozilla/WidgetUtils.h"
#include "nsIPowerManagerService.h"
#include "mozilla/Preferences.h"
#ifdef MOZ_ENABLE_DBUS
#  include <gio/gio.h>
#  include "nsIObserverService.h"
#  include "WidgetUtilsGtk.h"
#endif
#include "WakeLockListener.h"
#include "gfxPlatform.h"
#include "nsAppRunner.h"
#include "mozilla/XREAppData.h"
#include "ScreenHelperGTK.h"
#include "HeadlessScreenHelper.h"
#include "mozilla/widget/ScreenManager.h"
#ifdef MOZ_WAYLAND
#  include "nsWaylandDisplay.h"
#endif

using namespace mozilla;
using namespace mozilla::widget;
using mozilla::widget::HeadlessScreenHelper;
using mozilla::widget::ScreenHelperGTK;
using mozilla::widget::ScreenManager;

#define NOTIFY_TOKEN 0xFA
#define QUIT_TOKEN 0xFB

LazyLogModule gWidgetLog("Widget");
LazyLogModule gWidgetDragLog("WidgetDrag");
LazyLogModule gWidgetWaylandLog("WidgetWayland");
LazyLogModule gWidgetPopupLog("WidgetPopup");
LazyLogModule gWidgetVsync("WidgetVSync");
LazyLogModule gDmabufLog("Dmabuf");
LazyLogModule gWidgetCompositorLog("WidgetCompositor");

#undef LOGW
#ifdef MOZ_LOGGING
#  define LOGW(...) MOZ_LOG(gWidgetLog, mozilla::LogLevel::Debug, (__VA_ARGS__))
#else
#  define LOGW(...)
#endif

static GPollFunc sPollFunc;

nsAppShell* nsAppShell::sAppShell = nullptr;

// Wrapper function to disable hang monitoring while waiting in poll().
static gint PollWrapper(GPollFD* aUfds, guint aNfsd, gint aTimeout) {
  if (aTimeout == 0) {
    // When the timeout is 0, there is no wait, so no point in notifying
    // the BackgroundHangMonitor and the profiler.
    return (*sPollFunc)(aUfds, aNfsd, aTimeout);
  }

  mozilla::BackgroundHangMonitor().NotifyWait();
  gint result;
  {
    gint timeout = aTimeout;
    gint64 begin = 0;
    if (aTimeout != -1) {
      begin = g_get_monotonic_time();
    }

    AUTO_PROFILER_LABEL("PollWrapper", IDLE);
    AUTO_PROFILER_THREAD_SLEEP;
    do {
      result = (*sPollFunc)(aUfds, aNfsd, timeout);

      // The result will be -1 with the EINTR error if the poll was interrupted
      // by a signal, typically the signal sent by the profiler to sample the
      // process. We are only done waiting if we are not in that case.
      if (result != -1 || errno != EINTR) {
        break;
      }

      if (aTimeout != -1) {
        // Adjust the timeout to account for the time already spent waiting.
        gint elapsedSinceBegin = (g_get_monotonic_time() - begin) / 1000;
        if (elapsedSinceBegin < aTimeout) {
          timeout = aTimeout - elapsedSinceBegin;
        } else {
          // poll returns 0 to indicate the call timed out before any fd
          // became ready.
          result = 0;
          break;
        }
      }
    } while (true);
  }
  mozilla::BackgroundHangMonitor().NotifyActivity();
  return result;
}

// Emit resume-events on GdkFrameClock if flush-events has not been
// balanced by resume-events at dispose.
// For https://bugzilla.gnome.org/show_bug.cgi?id=742636
static decltype(GObjectClass::constructed) sRealGdkFrameClockConstructed;
static decltype(GObjectClass::dispose) sRealGdkFrameClockDispose;
static GQuark sPendingResumeQuark;

static void OnFlushEvents(GObject* clock, gpointer) {
  g_object_set_qdata(clock, sPendingResumeQuark, GUINT_TO_POINTER(1));
}

static void OnResumeEvents(GObject* clock, gpointer) {
  g_object_set_qdata(clock, sPendingResumeQuark, nullptr);
}

static void WrapGdkFrameClockConstructed(GObject* object) {
  sRealGdkFrameClockConstructed(object);

  g_signal_connect(object, "flush-events", G_CALLBACK(OnFlushEvents), nullptr);
  g_signal_connect(object, "resume-events", G_CALLBACK(OnResumeEvents),
                   nullptr);
}

static void WrapGdkFrameClockDispose(GObject* object) {
  if (g_object_get_qdata(object, sPendingResumeQuark)) {
    g_signal_emit_by_name(object, "resume-events");
  }

  sRealGdkFrameClockDispose(object);
}

/*static*/
gboolean nsAppShell::EventProcessorCallback(GIOChannel* source,
                                            GIOCondition condition,
                                            gpointer data) {
  nsAppShell* self = static_cast<nsAppShell*>(data);

  unsigned char c;
  [[maybe_unused]] ssize_t _ = read(self->mPipeFDs[0], &c, 1);
  switch (c) {
    case NOTIFY_TOKEN:
      self->NativeEventCallback();
      break;
    case QUIT_TOKEN:
      self->Exit();
      break;
    default:
      NS_ASSERTION(false, "wrong token");
      break;
  }
  return TRUE;
}

nsAppShell::~nsAppShell() {
  sAppShell = nullptr;

#ifdef MOZ_ENABLE_DBUS
  StopDBusListening();
#endif
  mozilla::hal::Shutdown();

  if (mTag) g_source_remove(mTag);
  if (mPipeFDs[0]) close(mPipeFDs[0]);
  if (mPipeFDs[1]) close(mPipeFDs[1]);

  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  if (obs) {
    obs->RemoveObserver(this, "sessionstore-restoring-on-startup");
    obs->RemoveObserver(this, "sessionstore-windows-restored");
  }
}

mozilla::StaticRefPtr<WakeLockListener> sWakeLockListener;
static void AddScreenWakeLockListener() {
  nsCOMPtr<nsIPowerManagerService> powerManager =
      do_GetService(POWERMANAGERSERVICE_CONTRACTID);
  if (powerManager) {
    sWakeLockListener = new WakeLockListener();
    powerManager->AddWakeLockListener(sWakeLockListener);
  } else {
    NS_WARNING(
        "Failed to retrieve PowerManagerService, wakelocks will be broken!");
  }
}

static void RemoveScreenWakeLockListener() {
  nsCOMPtr<nsIPowerManagerService> powerManager =
      do_GetService(POWERMANAGERSERVICE_CONTRACTID);
  if (powerManager) {
    powerManager->RemoveWakeLockListener(sWakeLockListener);
    sWakeLockListener = nullptr;
  }
}

#ifdef MOZ_ENABLE_DBUS
void nsAppShell::DBusSessionSleepCallback(GDBusProxy* aProxy,
                                          gchar* aSenderName,
                                          gchar* aSignalName,
                                          GVariant* aParameters,
                                          gpointer aUserData) {
  if (g_strcmp0(aSignalName, "PrepareForSleep")) {
    return;
  }
  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();
  if (!observerService) {
    return;
  }
  if (!g_variant_is_of_type(aParameters, G_VARIANT_TYPE_TUPLE) ||
      g_variant_n_children(aParameters) != 1) {
    NS_WARNING(
        nsPrintfCString("Unexpected location updated signal params type: %s\n",
                        g_variant_get_type_string(aParameters))
            .get());
    return;
  }

  RefPtr<GVariant> variant =
      dont_AddRef(g_variant_get_child_value(aParameters, 0));
  if (!g_variant_is_of_type(variant, G_VARIANT_TYPE_BOOLEAN)) {
    NS_WARNING(
        nsPrintfCString("Unexpected location updated signal params type: %s\n",
                        g_variant_get_type_string(aParameters))
            .get());
    return;
  }

  gboolean suspend = g_variant_get_boolean(variant);
  if (suspend) {
    // Post sleep_notification
    observerService->NotifyObservers(nullptr, NS_WIDGET_SLEEP_OBSERVER_TOPIC,
                                     nullptr);
  } else {
    // Post wake_notification
    observerService->NotifyObservers(nullptr, NS_WIDGET_WAKE_OBSERVER_TOPIC,
                                     nullptr);
  }
}

void nsAppShell::DBusTimedatePropertiesChangedCallback(GDBusProxy* aProxy,
                                                       gchar* aSenderName,
                                                       gchar* aSignalName,
                                                       GVariant* aParameters,
                                                       gpointer aUserData) {
  if (g_strcmp0(aSignalName, "PropertiesChanged")) {
    return;
  }
  nsBaseAppShell::OnSystemTimezoneChange();
}

void nsAppShell::DBusConnectClientResponse(GObject* aObject,
                                           GAsyncResult* aResult,
                                           gpointer aUserData) {
  GUniquePtr<GError> error;
  RefPtr<GDBusProxy> proxyClient =
      dont_AddRef(g_dbus_proxy_new_finish(aResult, getter_Transfers(error)));
  if (!proxyClient) {
    if (!IsCancelledGError(error.get())) {
      NS_WARNING(
          nsPrintfCString("Failed to connect to client: %s\n", error->message)
              .get());
    }
    return;
  }

  RefPtr self = static_cast<nsAppShell*>(aUserData);
  if (!strcmp(g_dbus_proxy_get_name(proxyClient), "org.freedesktop.login1")) {
    self->mLogin1Proxy = std::move(proxyClient);
    g_signal_connect(self->mLogin1Proxy, "g-signal",
                     G_CALLBACK(DBusSessionSleepCallback), self);
  } else {
    self->mTimedate1Proxy = std::move(proxyClient);
    g_signal_connect(self->mTimedate1Proxy, "g-signal",
                     G_CALLBACK(DBusTimedatePropertiesChangedCallback), self);
  }
}

void nsAppShell::DBusConnectionCheck() {
  if (sAppShell && sAppShell->mDBusConnectionSession &&
      sAppShell->mDBusConnectionSystem) {
    MOZ_DIAGNOSTIC_ASSERT(
        ((GObject*)sAppShell->mDBusConnectionSession.get())->ref_count > 1,
        "Released mDBusConnectionSession connection?!");
    MOZ_DIAGNOSTIC_ASSERT(
        ((GObject*)sAppShell->mDBusConnectionSystem.get())->ref_count > 1,
        "Released mDBusConnectionSystem connection?!");
  }
}

void nsAppShell::SetSessionDBus(GDBusConnection* aDBusConnectionSession) {
  if (sAppShell) {
    sAppShell->mDBusConnectionSession = aDBusConnectionSession;
    DBusConnectionCheck();
  }
}

void nsAppShell::SetSystemDBus(GDBusConnection* aDBusConnectionSystem) {
  if (sAppShell) {
    sAppShell->mDBusConnectionSystem = aDBusConnectionSystem;
    DBusConnectionCheck();
  }
}

// Based on
// https://github.com/lcp/NetworkManager/blob/240f47c892b4e935a3e92fc09eb15163d1fa28d8/src/nm-sleep-monitor-systemd.c
// Use login1 to signal sleep and wake notifications.
void nsAppShell::StartDBusListening() {
  MOZ_DIAGNOSTIC_ASSERT(!mLogin1Proxy, "Already configured?");
  MOZ_DIAGNOSTIC_ASSERT(!mTimedate1Proxy, "Already configured?");
  MOZ_DIAGNOSTIC_ASSERT(!mLogin1ProxyCancellable, "Already configured?");
  MOZ_DIAGNOSTIC_ASSERT(!mTimedate1ProxyCancellable, "Already configured?");

  mLogin1ProxyCancellable = dont_AddRef(g_cancellable_new());
  mTimedate1ProxyCancellable = dont_AddRef(g_cancellable_new());

  g_dbus_proxy_new_for_bus(
      G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, nullptr,
      "org.freedesktop.login1", "/org/freedesktop/login1",
      "org.freedesktop.login1.Manager", mLogin1ProxyCancellable,
      reinterpret_cast<GAsyncReadyCallback>(DBusConnectClientResponse), this);

  g_dbus_proxy_new_for_bus(
      G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, nullptr,
      "org.freedesktop.timedate1", "/org/freedesktop/timedate1",
      "org.freedesktop.DBus.Properties", mTimedate1ProxyCancellable,
      reinterpret_cast<GAsyncReadyCallback>(DBusConnectClientResponse), this);

  // Don't grab reference to DBus connect from xpcshell, it fails
  // to release nsAppShell singleton and causes test timeout.
  if (!g_getenv("XPCSHELL_TEST")) {
    // Obtain session DBus connection
    mDBusGetCancellableSession = dont_AddRef(g_cancellable_new());
    g_bus_get(
        G_BUS_TYPE_SESSION, mDBusGetCancellableSession,
        [](GObject* aSourceObject, GAsyncResult* aRes, gpointer aUserData) {
          GUniquePtr<GError> error;
          GDBusConnection* conn =
              g_bus_get_finish(aRes, getter_Transfers(error));
          if (!conn) {
            if (!IsCancelledGError(error.get())) {
              NS_WARNING(
                  nsPrintfCString("Failure at g_bus_get_finish: %s",
                                  error ? error->message : "Unknown Error")
                      .get());
            }
            return;
          }
          nsAppShell::SetSessionDBus(conn);
        },
        this);

    // Obtain system-wide DBus connection
    mDBusGetCancellableSystem = dont_AddRef(g_cancellable_new());
    g_bus_get(
        G_BUS_TYPE_SYSTEM, mDBusGetCancellableSystem,
        [](GObject* aSourceObject, GAsyncResult* aRes, gpointer aUserData) {
          GUniquePtr<GError> error;
          GDBusConnection* conn =
              g_bus_get_finish(aRes, getter_Transfers(error));
          if (!conn) {
            if (!IsCancelledGError(error.get())) {
              NS_WARNING(
                  nsPrintfCString("Failure at g_bus_get_finish: %s",
                                  error ? error->message : "Unknown Error")
                      .get());
            }
            return;
          }
          nsAppShell::SetSystemDBus(conn);
        },
        this);
  }
}

void nsAppShell::StopDBusListening() {
  if (mLogin1Proxy) {
    g_signal_handlers_disconnect_matched(mLogin1Proxy, G_SIGNAL_MATCH_DATA, 0,
                                         0, nullptr, nullptr, this);
  }
  if (mLogin1ProxyCancellable) {
    g_cancellable_cancel(mLogin1ProxyCancellable);
    mLogin1ProxyCancellable = nullptr;
  }
  mLogin1Proxy = nullptr;

  if (mTimedate1Proxy) {
    g_signal_handlers_disconnect_matched(mTimedate1Proxy, G_SIGNAL_MATCH_DATA,
                                         0, 0, nullptr, nullptr, this);
  }
  if (mTimedate1ProxyCancellable) {
    g_cancellable_cancel(mTimedate1ProxyCancellable);
    mTimedate1ProxyCancellable = nullptr;
  }
  mTimedate1Proxy = nullptr;

  DBusConnectionCheck();
  if (mDBusGetCancellableSession) {
    g_cancellable_cancel(mDBusGetCancellableSession);
    mDBusGetCancellableSession = nullptr;
  }
  if (mDBusGetCancellableSystem) {
    g_cancellable_cancel(mDBusGetCancellableSystem);
    mDBusGetCancellableSystem = nullptr;
  }
  mDBusConnectionSession = nullptr;
  mDBusConnectionSystem = nullptr;
}
#endif

void nsAppShell::TermSignalHandler(int signo) {
  if (signo != SIGTERM) {
    NS_WARNING("Wrong signal!");
    return;
  }
  sAppShell->ScheduleQuitEvent();
}

void nsAppShell::InstallTermSignalHandler() {
  if (!XRE_IsParentProcess() || PR_GetEnv("MOZ_DISABLE_SIG_HANDLER") ||
      !sAppShell) {
    return;
  }

  struct sigaction act = {}, oldact;
  act.sa_handler = TermSignalHandler;
  sigfillset(&act.sa_mask);

  if (NS_WARN_IF(sigaction(SIGTERM, nullptr, &oldact) != 0)) {
    return;
  }
  if (oldact.sa_handler != SIG_DFL) {
    NS_WARNING("SIGTERM signal handler is already set?");
  }

  sigaction(SIGTERM, &act, nullptr);
}

nsresult nsAppShell::Init() {
  MOZ_ASSERT(!sAppShell);
  mozilla::hal::Init();

#ifdef MOZ_ENABLE_DBUS
  if (XRE_IsParentProcess()) {
    StartDBusListening();
  }
#endif

  if (!sPollFunc) {
    sPollFunc = g_main_context_get_poll_func(nullptr);
    g_main_context_set_poll_func(nullptr, &PollWrapper);
  }

  if (XRE_IsParentProcess()) {
    ScreenManager& screenManager = ScreenManager::GetSingleton();
    if (gfxPlatform::IsHeadless()) {
      screenManager.SetHelper(mozilla::MakeUnique<HeadlessScreenHelper>());
    } else {
      screenManager.SetHelper(mozilla::MakeUnique<ScreenHelperGTK>());
    }

    if (gtk_check_version(3, 16, 3) == nullptr) {
      // Before 3.16.3, GDK cannot override classname by --class command line
      // option when program uses gdk_set_program_class().
      //
      // See https://bugzilla.gnome.org/show_bug.cgi?id=747634
      //
      // Only bother doing this for the parent process, since it's the one
      // creating top-level windows.
      if (gAppData) {
        gdk_set_program_class(gAppData->remotingName);
      }
    }

    nsCOMPtr<nsIObserverService> obsServ =
        mozilla::services::GetObserverService();
    if (obsServ) {
      obsServ->AddObserver(this, "sessionstore-restoring-on-startup", false);
      obsServ->AddObserver(this, "sessionstore-windows-restored", false);
    }
  }

  if (!sPendingResumeQuark &&
      gtk_check_version(3, 14, 7) != nullptr) {  // GTK 3.0 to GTK 3.14.7.
    // GTK 3.8 - 3.14 registered this type when creating the frame clock
    // for the root window of the display when the display was opened.
    GType gdkFrameClockIdleType = g_type_from_name("GdkFrameClockIdle");
    if (gdkFrameClockIdleType) {  // not in versions prior to 3.8
      sPendingResumeQuark = g_quark_from_string("moz-resume-is-pending");
      auto gdk_frame_clock_idle_class =
          G_OBJECT_CLASS(g_type_class_peek_static(gdkFrameClockIdleType));
      auto constructed = &gdk_frame_clock_idle_class->constructed;
      sRealGdkFrameClockConstructed = *constructed;
      *constructed = WrapGdkFrameClockConstructed;
      auto dispose = &gdk_frame_clock_idle_class->dispose;
      sRealGdkFrameClockDispose = *dispose;
      *dispose = WrapGdkFrameClockDispose;
    }
  }

  // Workaround for bug 1209659 which is fixed by Gtk3.20
  if (gtk_check_version(3, 20, 0) != nullptr) {
    unsetenv("GTK_CSD");
  }

  // Whitelist of only common, stable formats - see bugs 1197059 and 1203078
  GSList* pixbufFormats = gdk_pixbuf_get_formats();
  for (GSList* iter = pixbufFormats; iter; iter = iter->next) {
    GdkPixbufFormat* format = static_cast<GdkPixbufFormat*>(iter->data);
    gchar* name = gdk_pixbuf_format_get_name(format);
    if (strcmp(name, "jpeg") && strcmp(name, "png") && strcmp(name, "gif") &&
        strcmp(name, "bmp") && strcmp(name, "ico") && strcmp(name, "xpm") &&
        strcmp(name, "svg") && strcmp(name, "webp") && strcmp(name, "avif")) {
      gdk_pixbuf_format_set_disabled(format, TRUE);
    }
    g_free(name);
  }
  g_slist_free(pixbufFormats);

  int err = pipe(mPipeFDs);
  if (err) return NS_ERROR_OUT_OF_MEMORY;

  GIOChannel* ioc;
  GSource* source;

  // make the pipe nonblocking

  int flags = fcntl(mPipeFDs[0], F_GETFL, 0);
  if (flags == -1) goto failed;
  err = fcntl(mPipeFDs[0], F_SETFL, flags | O_NONBLOCK);
  if (err == -1) goto failed;
  flags = fcntl(mPipeFDs[1], F_GETFL, 0);
  if (flags == -1) goto failed;
  err = fcntl(mPipeFDs[1], F_SETFL, flags | O_NONBLOCK);
  if (err == -1) goto failed;

  ioc = g_io_channel_unix_new(mPipeFDs[0]);
  source = g_io_create_watch(ioc, G_IO_IN);
  g_io_channel_unref(ioc);
  g_source_set_callback(source, (GSourceFunc)EventProcessorCallback, this,
                        nullptr);
  g_source_set_can_recurse(source, TRUE);
  mTag = g_source_attach(source, nullptr);
  g_source_unref(source);

  sAppShell = this;

  return nsBaseAppShell::Init();
failed:
  close(mPipeFDs[0]);
  close(mPipeFDs[1]);
  mPipeFDs[0] = mPipeFDs[1] = 0;
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP nsAppShell::Run() {
  if (XRE_IsParentProcess()) {
    AddScreenWakeLockListener();
  }

  nsresult rv = nsBaseAppShell::Run();

  if (XRE_IsParentProcess()) {
    RemoveScreenWakeLockListener();
  }
  return rv;
}

void nsAppShell::ScheduleNativeEventCallback() {
  unsigned char buf[] = {NOTIFY_TOKEN};
  [[maybe_unused]] ssize_t _ = write(mPipeFDs[1], buf, 1);
}

void nsAppShell::ScheduleQuitEvent() {
  unsigned char buf[] = {QUIT_TOKEN};
  [[maybe_unused]] ssize_t _ = write(mPipeFDs[1], buf, 1);
}

bool nsAppShell::ProcessNextNativeEvent(bool mayWait) {
  if (mSuspendNativeCount) {
    return false;
  }
  bool didProcessEvent = g_main_context_iteration(nullptr, mayWait);
  return didProcessEvent;
}

SessionRestoreState nsAppShell::UpdateAndGetSessionState() {
  if (!sAppShell) {
    return eSessionRestoreFinished;
  }
#ifdef MOZ_WAYLAND
  // If session restore is not supported, report 'finished' as we don't want to
  // block window creation.
  if (!WaylandDisplayGet()->GetSessionManager()) {
    return eSessionRestoreFinished;
  }
#endif
  // Check for browser.startup.page / re-open previous windows and tabs (session
  // restore enabled).
  if (sAppShell->mSessionRestoreState == eSessionDefault &&
      Preferences::GetInt("browser.startup.page", 0) == 3) {
    sAppShell->mSessionRestoreState = eSessionRestoring;
  }
  LOGW("nsAppShell::GetSessionState() state %d",
       int(sAppShell->mSessionRestoreState));
  return sAppShell->mSessionRestoreState;
}

NS_IMETHODIMP
nsAppShell::Observe(nsISupports* aSubject, const char* aTopic,
                    const char16_t* aData) {
  LOGW("nsAppShell::Observe() topic %s", aTopic);
  if (!nsCRT::strcmp(aTopic, "sessionstore-restoring-on-startup")) {
    mSessionRestoreState = eSessionRestoring;
  } else if (!nsCRT::strcmp(aTopic, "sessionstore-windows-restored")) {
    mSessionRestoreState = eSessionRestoreFinished;
    nsWindow::SessionRestoreFinished();
  } else {
    return nsBaseAppShell::Observe(aSubject, aTopic, aData);
  }
  return NS_OK;
}
