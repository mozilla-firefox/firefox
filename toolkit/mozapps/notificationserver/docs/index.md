# Windows Native Notifications

Gecko has a concept of **alerts**: pop-up windows displaying small amounts of information. Such alerts are how [Web Notifications](https://developer.mozilla.org/en-US/docs/Web/API/Notifications_API) are displayed. On Windows, Gecko supports "native notifications": alerts rendered and managed by the OS. On Windows, such native notifications are referred to as [toast notifications](https://docs.microsoft.com/en-us/windows/apps/design/shell/tiles-and-notifications/toast-notifications-overview). This functionality is behind the `alerts.useSystemBackend` Gecko preference.

Native notifications can persist beyond the Firefox main process lifetime, visible both on the Desktop and in the Windows Action Center. When the user interacts with a Windows toast notification, the producing application can be notified in multiple ways. In Desktop Firefox, Windows launches the `notificationserver.dll` COM server, which provides a Win32 [`INotificationActivationCallback`](https://docs.microsoft.com/en-us/windows/win32/api/notificationactivationcallback/nn-notificationactivationcallback-inotificationactivationcallback) object that redirects the activation to Firefox. This document outlines how this activation and redirection is arranged.

## Notification click sequence diagrams

The following diagrams show the major actors in the native notification lifecycle. Note that multiple instances of Firefox are involved. This is the case for two reasons:
1. A notification is handled by a different instance of Firefox than the one that created it if Firefox closes before the notification is interacted with. This is differentiated as `(previous session)` and `(new session)` where relevant.
2. The notification server invokes Firefox with command line arguments to ensure it is running. If Firefox was already running (`remote server`), the newly launched Firefox instance (`remote client`) will forward the command line arguments to it.

### When Firefox remains open until the notification is clicked

```{mermaid}
sequenceDiagram
    participant FH as Firefox<br/>(remote server)
    participant W as Windows

    FH->>W: Show notification
    create participant C as notificationserver.dll<br/>COM Server
    W->>C: Notification clicked
    Note left of C: Named pipe created
    create participant FC as Firefox<br/>(remote client)
    C->>FC: Launch browser
    destroy FC
    FC->>FH: Redirect command line
    rect grey
    Note left of C: Pipe IPC
    FH->>C: Request foreground privilege
    C->>FH: Success or failure of AllowSetForegroundWindow
    end
    destroy C
    C->>W: INotificationActivationCallback::Activate<br/>returns
    W->>FH: Call in-memory<br/>ToastNotification::Activated callback
    Note over FH: Notification handled
```

:::{note}
The `ToastNotification::Activated` in-memory notification callbacks are currently only registered with the application instance that created the notification. Additionally, the callback is only called by the system _after_ `INotificationActivationCallback::Activate` returns; if needed we could register `ToastNotification::Activated` before returning from `INotificationActivationCallback::Activate`.
:::

### When Firefox closes before the notification is clicked

```{mermaid}
sequenceDiagram
    participant FP as Firefox<br/>(previous session)
    participant W as Windows

    FP->>W: Show notification
    destroy FP
    FP-->W: Firefox closed
    create participant C as notificationserver.dll<br/>COM Server
    W->>C: Notification clicked
    Note right of C: Named pipe created
    create participant FN as Firefox<br/>(new session)
    C->>FN: Launch browser
    rect grey
    Note right of C: Pipe IPC
    FN->>C: Request foreground privilege
    C->>FN: Success or failure of<br/>AllowSetForegroundWindow
    end
    destroy C
    C->>W: INotificationActivationCallback::Activate<br/>returns
    Note over FN: Notification handled
```

### When Firefox closes and reopens before the notification is clicked

```{mermaid}
sequenceDiagram
    participant FP as Firefox<br/>(previous session)
    participant W as Windows

    FP->>W: Show notification
    destroy FP
    FP-->W: Firefox closed
    create participant FS as Firefox<br/>(remote server)<br/>(new session)
    W-->FS: Firefox reopened
    create participant C as notificationserver.dll<br/>COM Server
    W->>C: Notification clicked
    Note left of C: Named pipe created
    create participant FC as Firefox<br/>(remote client)
    C->>FC: Launch browser
    destroy FC
    FC->>FS: Redirect command line
    rect grey
    Note left of C: Pipe IPC
    FS->>C: Request foreground privilege
    C->>FS: Success or failure of<br/>AllowSetForegroundWindow
    end
    destroy C
    C->>W: INotificationActivationCallback::Activate returns
    Note over FS: Notification handled
```

Firefox creates Windows toast notifications using the Win32 `CreateToastNotifierWithId` API. The toast notification includes sufficient information to invoke Firefox and identify a particular toast notification. If an appropriate Firefox is already running, the new invocation will use the [`remote` component](https://searchfox.org/mozilla-central/source/toolkit/components/remote) (not to be confused with various remote protocols for automation) to forward the command line to the running Firefox. The running Firefox uses the command line arguments to identify the toast notification and invoke appropriate callbacks, etc. If an appropriate Firefox is *not* running, the new invocation will navigate to a location appropriate to the notification. See [`nsDefaultCommandLineHandler`](https://searchfox.org/firefox-main/source/browser/components/BrowserContentHandler.sys.mjs) for details.

## Registering the COM server DLL with Windows

The `notificationserver.dll` must be registered with Windows. This registration is done by adding various values to the Windows registry; the registration includes details about the COM server and about the visual display of notifications. The installer does this in `HKEY_CLASSES_ROOT` at install time and post-update time. (The uninstaller removes any added registry values.) If Firefox does not find these values, it dynamically registers the COM server DLL at runtime.

To observe and debug the COM server registration process, set `MOZ_LOG=WindowsAlertService:5` in the Firefox process environment. To debug the notification server itself, set `alerts.useSystemBackend.windows.notificationserver.verbose` to `true`. The notification server's logged messages can be viewed in Windows Event Viewer under `Windows Logs/Application` with Source `[MOZ_APP_DISPLAYNAME] Notification Server`.

Once registered, the notification server will always be called as a result of the notification being clicked. Not all use cases for notifications are supported by the notification server, notably the {doc}`Default Browser Agent </toolkit/mozapps/defaultagent/default-browser-agent/index>`. In these instances, the notification server processing can be short-circuited via the `alerts.useSystemBackend.windows.notificationserver.enabled` pref.

## Transferring Foreground Privilege

A process must have foreground privilege in order to set focus for its windows.

Foreground privilege is passed to `notificationserver.dll` when `INotificationActivationCallback` is called (likely via `CoAllowSetForegroundWindow`). We can't pass this privilege directly to the launched Firefox instance. The exact reason was never verified, but it is likely the result of a break in the chain of foreground privilege passing between the [launcher process], command line remoting, and the final receiving Firefox instance, either due to privileges not being passed, or privileges not being receivable because the non-terminal instances have no UI.

In order to pass the foreground privilege, the notification server provides a named pipe identifier for the notification handling Firefox instance to communicate its PID back. The notification server then transfers foreground privileges via `AllowSetForegroundWindow`.

:::{warning}
Applications attached to a debugger always have foreground privilege, which leads to Heisenbugs when debugging window focus issues. See [SetForegroundWindow restrictions](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow#remarks).
:::

[launcher process]: https://wiki.mozilla.org/Platform/Integration/InjectEject/Launcher_Process/
