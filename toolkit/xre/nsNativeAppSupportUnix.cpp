/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsNativeAppSupportBase.h"
#include "nsComponentManagerUtils.h"
#include "nsCOMPtr.h"
#include "nsXPCOM.h"
#include "nsISupportsPrimitives.h"
#include "nsIObserverService.h"
#include "nsIAppStartup.h"
#include "nsServiceManagerUtils.h"
#include "prlink.h"
#include "nsXREDirProvider.h"
#include "nsReadableUtils.h"

#include "nsIFile.h"
#include "nsDirectoryServiceDefs.h"
#include "nsPIDOMWindow.h"
#include "nsIWidget.h"
#include "mozilla/Logging.h"
#include "mozilla/Services.h"
#include "mozilla/XREAppData.h"

#include <stdlib.h>
#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>

#ifdef MOZ_X11
#  include <gdk/gdkx.h>
#  include <X11/ICE/ICElib.h>
#  include <X11/SM/SMlib.h>
#  include <fcntl.h>
#  include "nsThreadUtils.h"

#  include <pwd.h>
#endif

#ifdef MOZ_ENABLE_DBUS
#  include <dbus/dbus.h>
#endif

#if MOZ_X11
#  undef IceSetIOErrorHandler
#  undef IceAddConnectionWatch
#  undef IceConnectionNumber
#  undef IceProcessMessages
#  undef IceGetConnectionContext
#  undef SmcInteractDone
#  undef SmcSaveYourselfDone
#  undef SmcInteractRequest
#  undef SmcCloseConnection
#  undef SmcOpenConnection
#  undef SmcSetProperties

typedef IceIOErrorHandler (*IceSetIOErrorHandlerFn)(IceIOErrorHandler);
typedef int (*IceAddConnectionWatchFn)(IceWatchProc, IcePointer);
typedef int (*IceConnectionNumberFn)(IceConn);
typedef IceProcessMessagesStatus (*IceProcessMessagesFn)(IceConn,
                                                         IceReplyWaitInfo*,
                                                         Bool*);
typedef IcePointer (*IceGetConnectionContextFn)(IceConn);

typedef void (*SmcInteractDoneFn)(SmcConn, Bool);
typedef void (*SmcSaveYourselfDoneFn)(SmcConn, Bool);
typedef int (*SmcInteractRequestFn)(SmcConn, int, SmcInteractProc, SmPointer);
typedef SmcCloseStatus (*SmcCloseConnectionFn)(SmcConn, int, char**);
typedef SmcConn (*SmcOpenConnectionFn)(char*, SmPointer, int, int,
                                       unsigned long, SmcCallbacks*,
                                       const char*, char**, int, char*);
typedef void (*SmcSetPropertiesFn)(SmcConn, int, SmProp**);

static IceSetIOErrorHandlerFn IceSetIOErrorHandlerPtr;
static IceAddConnectionWatchFn IceAddConnectionWatchPtr;
static IceConnectionNumberFn IceConnectionNumberPtr;
static IceProcessMessagesFn IceProcessMessagesPtr;
static IceGetConnectionContextFn IceGetConnectionContextPtr;
static SmcInteractDoneFn SmcInteractDonePtr;
static SmcSaveYourselfDoneFn SmcSaveYourselfDonePtr;
static SmcInteractRequestFn SmcInteractRequestPtr;
static SmcCloseConnectionFn SmcCloseConnectionPtr;
static SmcOpenConnectionFn SmcOpenConnectionPtr;
static SmcSetPropertiesFn SmcSetPropertiesPtr;

#  define IceSetIOErrorHandler IceSetIOErrorHandlerPtr
#  define IceAddConnectionWatch IceAddConnectionWatchPtr
#  define IceConnectionNumber IceConnectionNumberPtr
#  define IceProcessMessages IceProcessMessagesPtr
#  define IceGetConnectionContext IceGetConnectionContextPtr
#  define SmcInteractDone SmcInteractDonePtr
#  define SmcSaveYourselfDone SmcSaveYourselfDonePtr
#  define SmcInteractRequest SmcInteractRequestPtr
#  define SmcCloseConnection SmcCloseConnectionPtr
#  define SmcOpenConnection SmcOpenConnectionPtr
#  define SmcSetProperties SmcSetPropertiesPtr

enum ClientState {
  STATE_DISCONNECTED,
  STATE_REGISTERING,
  STATE_IDLE,
  STATE_INTERACTING,
  STATE_SHUTDOWN_CANCELLED
};

using namespace mozilla;

static const char* gClientStateTable[] = {"DISCONNECTED", "REGISTERING", "IDLE",
                                          "INTERACTING", "SHUTDOWN_CANCELLED"};

static LazyLogModule sMozSMLog("MozSM");
#endif /* MOZ_X11 */

class nsNativeAppSupportUnix : public nsNativeAppSupportBase {
 public:
#if MOZ_X11
  nsNativeAppSupportUnix()
      : mSessionConnection(nullptr), mClientState(STATE_DISCONNECTED) {};
  ~nsNativeAppSupportUnix() {
    // this goes out of scope after "web-workers-shutdown" async shutdown phase
    // so it's safe to disconnect here (i.e. the application won't lose data)
    DisconnectFromSM();
  };

  void DisconnectFromSM();
#endif
  NS_IMETHOD Start(bool* aRetVal) override;
  NS_IMETHOD Enable() override;

 private:
#if MOZ_X11
  static void SaveYourselfCB(SmcConn smc_conn, SmPointer client_data,
                             int save_style, Bool shutdown, int interact_style,
                             Bool fast);
  static void DieCB(SmcConn smc_conn, SmPointer client_data);
  static void InteractCB(SmcConn smc_conn, SmPointer client_data);
  static void SaveCompleteCB(SmcConn smc_conn, SmPointer client_data) {};
  static void ShutdownCancelledCB(SmcConn smc_conn, SmPointer client_data);
  void DoInteract();
  void SetClientState(ClientState aState) {
    mClientState = aState;
    MOZ_LOG(sMozSMLog, LogLevel::Debug,
            ("New state = %s\n", gClientStateTable[aState]));
  }

  SmcConn mSessionConnection;
  ClientState mClientState;
#endif
};

#if MOZ_X11
static gboolean process_ice_messages(IceConn connection) {
  IceProcessMessagesStatus status;

  status = IceProcessMessages(connection, nullptr, nullptr);

  switch (status) {
    case IceProcessMessagesSuccess:
      return TRUE;

    case IceProcessMessagesIOError: {
      nsNativeAppSupportUnix* native = static_cast<nsNativeAppSupportUnix*>(
          IceGetConnectionContext(connection));
      native->DisconnectFromSM();
    }
      return FALSE;

    case IceProcessMessagesConnectionClosed:
      return FALSE;

    default:
      g_assert_not_reached();
  }
}

static gboolean ice_iochannel_watch(GIOChannel* channel, GIOCondition condition,
                                    gpointer client_data) {
  return process_ice_messages(static_cast<IceConn>(client_data));
}

static void ice_connection_watch(IceConn connection, IcePointer client_data,
                                 Bool opening, IcePointer* watch_data) {
  guint watch_id;

  if (opening) {
    GIOChannel* channel;
    int fd = IceConnectionNumber(connection);

    fcntl(fd, F_SETFD, fcntl(fd, F_GETFD, 0) | FD_CLOEXEC);
    channel = g_io_channel_unix_new(fd);
    watch_id =
        g_io_add_watch(channel, static_cast<GIOCondition>(G_IO_IN | G_IO_ERR),
                       ice_iochannel_watch, connection);
    g_io_channel_unref(channel);

    *watch_data = GUINT_TO_POINTER(watch_id);
  } else {
    watch_id = GPOINTER_TO_UINT(*watch_data);
    g_source_remove(watch_id);
  }
}

static void ice_io_error_handler(IceConn connection) {
  // override the default handler which would exit the application;
  // do nothing and let ICELib handle the failure of the connection gracefully.
}

static void ice_init(void) {
  static bool initted = false;

  if (!initted) {
    IceSetIOErrorHandler(ice_io_error_handler);
    IceAddConnectionWatch(ice_connection_watch, nullptr);
    initted = true;
  }
}

void nsNativeAppSupportUnix::InteractCB(SmcConn smc_conn,
                                        SmPointer client_data) {
  nsNativeAppSupportUnix* self =
      static_cast<nsNativeAppSupportUnix*>(client_data);

  self->SetClientState(STATE_INTERACTING);

  // We do this asynchronously, as we spin the event loop recursively if
  // a dialog is displayed. If we do this synchronously, we don't finish
  // processing the current ICE event whilst the dialog is displayed, which
  // means we won't process any more. libsm hates us if we do the InteractDone
  // with a pending ShutdownCancelled, and we would certainly like to handle Die
  // whilst a dialog is displayed
  NS_DispatchToCurrentThread(
      NewRunnableMethod("nsNativeAppSupportUnix::DoInteract", self,
                        &nsNativeAppSupportUnix::DoInteract));
}

void nsNativeAppSupportUnix::DoInteract() {
  nsCOMPtr<nsIObserverService> obsServ =
      mozilla::services::GetObserverService();
  if (!obsServ) {
    SmcInteractDone(mSessionConnection, False);
    SmcSaveYourselfDone(mSessionConnection, True);
    SetClientState(STATE_IDLE);
    return;
  }

  nsCOMPtr<nsISupportsPRBool> cancelQuit =
      do_CreateInstance(NS_SUPPORTS_PRBOOL_CONTRACTID);

  bool abortQuit = false;
  if (cancelQuit) {
    cancelQuit->SetData(false);
    obsServ->NotifyObservers(cancelQuit, "quit-application-requested", nullptr);

    cancelQuit->GetData(&abortQuit);
  }

  if (!abortQuit && mClientState == STATE_DISCONNECTED) {
    // The session manager disappeared, whilst we were interacting, so
    // quit now
    nsCOMPtr<nsIAppStartup> appService =
        do_GetService("@mozilla.org/toolkit/app-startup;1");

    if (appService) {
      appService->Quit(nsIAppStartup::eForceQuit, 0);
    }
  } else {
    if (mClientState != STATE_SHUTDOWN_CANCELLED) {
      // Only do this if the shutdown wasn't cancelled
      SmcInteractDone(mSessionConnection, !!abortQuit);
      SmcSaveYourselfDone(mSessionConnection, !abortQuit);
    }

    SetClientState(STATE_IDLE);
  }
}

void nsNativeAppSupportUnix::SaveYourselfCB(SmcConn smc_conn,
                                            SmPointer client_data,
                                            int save_style, Bool shutdown,
                                            int interact_style, Bool fast) {
  nsNativeAppSupportUnix* self =
      static_cast<nsNativeAppSupportUnix*>(client_data);

  // Expect a SaveYourselfCB if we're registering a new client.
  // All properties are already set in Start() so just reply with
  // SmcSaveYourselfDone if the callback matches the expected signature.
  //
  // Ancient versions (?) of xsm do not follow such an early SaveYourself with
  // SaveComplete. This is a problem if the application freezes interaction
  // while waiting for a response to SmcSaveYourselfDone. So never freeze
  // interaction when in STATE_REGISTERING.
  //
  // That aside, we could treat each combination of flags appropriately and not
  // special-case this.
  if (self->mClientState == STATE_REGISTERING) {
    self->SetClientState(STATE_IDLE);

    if (save_style == SmSaveLocal && interact_style == SmInteractStyleNone &&
        !shutdown && !fast) {
      SmcSaveYourselfDone(self->mSessionConnection, True);
      return;
    }
  }

  if (self->mClientState == STATE_SHUTDOWN_CANCELLED) {
    // The last shutdown request was cancelled whilst we were interacting,
    // and we haven't finished interacting yet. Switch the state back again
    self->SetClientState(STATE_INTERACTING);
  }

  nsCOMPtr<nsIObserverService> obsServ =
      mozilla::services::GetObserverService();
  if (!obsServ) {
    SmcSaveYourselfDone(smc_conn, True);
    return;
  }

  bool status = false;
  nsCOMPtr<nsISupportsPRBool> didSaveSession =
      do_CreateInstance(NS_SUPPORTS_PRBOOL_CONTRACTID);

  if (!didSaveSession) {
    SmcSaveYourselfDone(smc_conn, True);
    return;
  }

  // Notify observers to save the session state
  didSaveSession->SetData(false);
  obsServ->NotifyObservers(didSaveSession, "session-save", nullptr);

  didSaveSession->GetData(&status);

  // If the interact style permits us to, we are shutting down and we didn't
  // manage to (or weren't asked to) save the local state, then notify the user
  // in advance that we are doing to quit (assuming that we aren't already
  // doing so)
  if (!status && shutdown && interact_style != SmInteractStyleNone) {
    if (self->mClientState != STATE_INTERACTING) {
      SmcInteractRequest(smc_conn, SmDialogNormal,
                         nsNativeAppSupportUnix::InteractCB, client_data);
    }
  } else {
    SmcSaveYourselfDone(smc_conn, True);
  }
}

void nsNativeAppSupportUnix::DieCB(SmcConn smc_conn, SmPointer client_data) {
  nsCOMPtr<nsIAppStartup> appService =
      do_GetService("@mozilla.org/toolkit/app-startup;1");

  if (appService) {
    appService->Quit(nsIAppStartup::eForceQuit, 0);
  }
  // Quit causes the shutdown to begin but the shutdown process is asynchronous
  // so we can't DisconnectFromSM() yet
}

void nsNativeAppSupportUnix::ShutdownCancelledCB(SmcConn smc_conn,
                                                 SmPointer client_data) {
  nsNativeAppSupportUnix* self =
      static_cast<nsNativeAppSupportUnix*>(client_data);

  // Interacting is the only time when we wouldn't already have called
  // SmcSaveYourselfDone. Do that now, then set the state to make sure we
  // don't send it again after finishing interacting
  if (self->mClientState == STATE_INTERACTING) {
    SmcSaveYourselfDone(smc_conn, False);
    self->SetClientState(STATE_SHUTDOWN_CANCELLED);
  }
}

void nsNativeAppSupportUnix::DisconnectFromSM() {
  // the SM is free to exit any time after we disconnect, so callers must be
  // sure to have reached a sufficiently advanced phase of shutdown that there
  // is no risk of data loss:
  // e.g. all async writes are complete by the end of "profile-before-change"
  if (mSessionConnection) {
    SetClientState(STATE_DISCONNECTED);
    SmcCloseConnection(mSessionConnection, 0, nullptr);
    mSessionConnection = nullptr;
    gdk_x11_set_sm_client_id(nullptr);  // follow gnome-client behaviour
  }
}

static void SetSMValue(SmPropValue& val, const nsCString& data) {
  val.value = static_cast<SmPointer>(const_cast<char*>(data.get()));
  val.length = data.Length();
}

static void SetSMProperty(SmProp& prop, const char* name, const char* type,
                          int numVals, SmPropValue vals[]) {
  prop.name = const_cast<char*>(name);
  prop.type = const_cast<char*>(type);
  prop.num_vals = numVals;
  prop.vals = vals;
}
#endif /* MOZ_X11 */

static void RemoveArg(char** argv) {
  do {
    *argv = *(argv + 1);
    ++argv;
  } while (*argv);

  --gArgc;
}

NS_IMETHODIMP
nsNativeAppSupportUnix::Start(bool* aRetVal) {
  NS_ASSERTION(gAppData, "gAppData must not be null.");

// The dbus library is used by both nsWifiScannerDBus and BluetoothDBusService,
// from diffrent threads. This could lead to race conditions if the dbus is not
// initialized before making any other library calls.
#ifdef MOZ_ENABLE_DBUS
  dbus_threads_init_default();
#endif

  *aRetVal = true;

#ifdef MOZ_X11
  gboolean sm_disable = FALSE;
  if (!getenv("SESSION_MANAGER")) {
    sm_disable = TRUE;
  }

  nsAutoCString prev_client_id;

  char** curarg = gArgv + 1;
  while (*curarg) {
    char* arg = *curarg;
    if (arg[0] == '-' && arg[1] == '-') {
      arg += 2;
      if (!strcmp(arg, "sm-disable")) {
        RemoveArg(curarg);
        sm_disable = TRUE;
        continue;
      } else if (!strcmp(arg, "sm-client-id")) {
        RemoveArg(curarg);
        if (*curarg[0] != '-') {
          prev_client_id = *curarg;
          RemoveArg(curarg);
        }
        continue;
      }
    }

    ++curarg;
  }

  if (prev_client_id.IsEmpty()) {
    prev_client_id = getenv("DESKTOP_AUTOSTART_ID");
  }

  // We don't want child processes to use the same ID
  unsetenv("DESKTOP_AUTOSTART_ID");

  char* client_id = nullptr;
  if (!sm_disable) {
    PRLibrary* iceLib = PR_LoadLibrary("libICE.so.6");
    if (!iceLib) {
      return NS_OK;
    }

    PRLibrary* smLib = PR_LoadLibrary("libSM.so.6");
    if (!smLib) {
      PR_UnloadLibrary(iceLib);
      return NS_OK;
    }

    IceSetIOErrorHandler = (IceSetIOErrorHandlerFn)PR_FindFunctionSymbol(
        iceLib, "IceSetIOErrorHandler");
    IceAddConnectionWatch = (IceAddConnectionWatchFn)PR_FindFunctionSymbol(
        iceLib, "IceAddConnectionWatch");
    IceConnectionNumber = (IceConnectionNumberFn)PR_FindFunctionSymbol(
        iceLib, "IceConnectionNumber");
    IceProcessMessages = (IceProcessMessagesFn)PR_FindFunctionSymbol(
        iceLib, "IceProcessMessages");
    IceGetConnectionContext = (IceGetConnectionContextFn)PR_FindFunctionSymbol(
        iceLib, "IceGetConnectionContext");
    if (!IceSetIOErrorHandler || !IceAddConnectionWatch ||
        !IceConnectionNumber || !IceProcessMessages ||
        !IceGetConnectionContext) {
      PR_UnloadLibrary(iceLib);
      PR_UnloadLibrary(smLib);
      return NS_OK;
    }

    SmcInteractDone =
        (SmcInteractDoneFn)PR_FindFunctionSymbol(smLib, "SmcInteractDone");
    SmcSaveYourselfDone = (SmcSaveYourselfDoneFn)PR_FindFunctionSymbol(
        smLib, "SmcSaveYourselfDone");
    SmcInteractRequest = (SmcInteractRequestFn)PR_FindFunctionSymbol(
        smLib, "SmcInteractRequest");
    SmcCloseConnection = (SmcCloseConnectionFn)PR_FindFunctionSymbol(
        smLib, "SmcCloseConnection");
    SmcOpenConnection =
        (SmcOpenConnectionFn)PR_FindFunctionSymbol(smLib, "SmcOpenConnection");
    SmcSetProperties =
        (SmcSetPropertiesFn)PR_FindFunctionSymbol(smLib, "SmcSetProperties");
    if (!SmcInteractDone || !SmcSaveYourselfDone || !SmcInteractRequest ||
        !SmcCloseConnection || !SmcOpenConnection || !SmcSetProperties) {
      PR_UnloadLibrary(iceLib);
      PR_UnloadLibrary(smLib);
      return NS_OK;
    }

    ice_init();

    // all callbacks are mandatory in libSM 1.0, so listen even if we don't
    // care.
    unsigned long mask = SmcSaveYourselfProcMask | SmcDieProcMask |
                         SmcSaveCompleteProcMask | SmcShutdownCancelledProcMask;

    SmcCallbacks callbacks;
    callbacks.save_yourself.callback = nsNativeAppSupportUnix::SaveYourselfCB;
    callbacks.save_yourself.client_data = static_cast<SmPointer>(this);

    callbacks.die.callback = nsNativeAppSupportUnix::DieCB;
    callbacks.die.client_data = static_cast<SmPointer>(this);

    callbacks.save_complete.callback = nsNativeAppSupportUnix::SaveCompleteCB;
    callbacks.save_complete.client_data = nullptr;

    callbacks.shutdown_cancelled.callback =
        nsNativeAppSupportUnix::ShutdownCancelledCB;
    callbacks.shutdown_cancelled.client_data = static_cast<SmPointer>(this);

    char errbuf[256];
    mSessionConnection = SmcOpenConnection(
        nullptr, this, SmProtoMajor, SmProtoMinor, mask, &callbacks,
        prev_client_id.get(), &client_id, sizeof(errbuf), errbuf);
  }

  if (!mSessionConnection) {
    return NS_OK;
  }

  LogModule::Init(
      gArgc, gArgv);  // need to make sure initialized before SetClientState
  if (prev_client_id.IsEmpty() ||
      (client_id && !prev_client_id.Equals(client_id))) {
    SetClientState(STATE_REGISTERING);
  } else {
    SetClientState(STATE_IDLE);
  }

  gdk_x11_set_sm_client_id(client_id);

  // Set SM Properties
  // SmCloneCommand, SmProgram, SmRestartCommand, SmUserID are required
  // properties so must be set, and must have a sensible fallback value.

  // Determine executable path to use for XSMP session restore

  // Is there a request to suppress default binary launcher?
  nsAutoCString path(getenv("MOZ_APP_LAUNCHER"));

  if (path.IsEmpty()) {
    NS_ASSERTION(gDirServiceProvider,
                 "gDirServiceProvider is NULL! This shouldn't happen!");
    nsCOMPtr<nsIFile> executablePath;
    nsresult rv;

    bool dummy;
    rv = gDirServiceProvider->GetFile(XRE_EXECUTABLE_FILE, &dummy,
                                      getter_AddRefs(executablePath));

    if (NS_SUCCEEDED(rv)) {
      // Strip off the -bin suffix to get the shell script we should run; this
      // is what Breakpad does
      nsAutoCString leafName;
      rv = executablePath->GetNativeLeafName(leafName);
      if (NS_SUCCEEDED(rv) && StringEndsWith(leafName, "-bin"_ns)) {
        leafName.SetLength(leafName.Length() - strlen("-bin"));
        executablePath->SetNativeLeafName(leafName);
      }

      executablePath->GetNativePath(path);
    }
  }

  if (path.IsEmpty()) {
    // can't determine executable path. Best fallback is name from
    // application.ini but it might not resolve to the same executable at
    // launch time.
    path = gAppData->name;  // will always be set
    ToLowerCase(path);
    MOZ_LOG(sMozSMLog, LogLevel::Warning,
            ("Could not determine executable path. Falling back to %s.",
             path.get()));
  }

  SmProp propRestart, propClone, propProgram, propUser, *props[4];
  SmPropValue valsRestart[3], valsClone[1], valsProgram[1], valsUser[1];
  int n = 0;

  constexpr auto kClientIDParam = "--sm-client-id"_ns;

  SetSMValue(valsRestart[0], path);
  SetSMValue(valsRestart[1], kClientIDParam);
  SetSMValue(valsRestart[2], nsDependentCString(client_id));
  SetSMProperty(propRestart, SmRestartCommand, SmLISTofARRAY8, 3, valsRestart);
  props[n++] = &propRestart;

  SetSMValue(valsClone[0], path);
  SetSMProperty(propClone, SmCloneCommand, SmLISTofARRAY8, 1, valsClone);
  props[n++] = &propClone;

  nsAutoCString appName(gAppData->name);  // will always be set
  ToLowerCase(appName);

  SetSMValue(valsProgram[0], appName);
  SetSMProperty(propProgram, SmProgram, SmARRAY8, 1, valsProgram);
  props[n++] = &propProgram;

  nsAutoCString userName;  // username that started the program
  struct passwd* pw = getpwuid(getuid());
  if (pw && pw->pw_name) {
    userName = pw->pw_name;
  } else {
    userName = "nobody"_ns;
    MOZ_LOG(
        sMozSMLog, LogLevel::Warning,
        ("Could not determine user-name. Falling back to %s.", userName.get()));
  }

  SetSMValue(valsUser[0], userName);
  SetSMProperty(propUser, SmUserID, SmARRAY8, 1, valsUser);
  props[n++] = &propUser;

  SmcSetProperties(mSessionConnection, n, props);

  g_free(client_id);
#endif /* MOZ_X11 */

  return NS_OK;
}

NS_IMETHODIMP
nsNativeAppSupportUnix::Enable() { return NS_OK; }

nsresult NS_CreateNativeAppSupport(nsINativeAppSupport** aResult) {
  nsNativeAppSupportBase* native = new nsNativeAppSupportUnix();
  if (!native) return NS_ERROR_OUT_OF_MEMORY;

  *aResult = native;
  NS_ADDREF(*aResult);

  return NS_OK;
}
