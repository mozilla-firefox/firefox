/* -*- Mode: Java; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * vim: ts=4 sw=4 expandtab:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview;

import android.annotation.SuppressLint;
import android.app.ActivityManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.pm.ServiceInfo;
import android.content.res.Configuration;
import android.os.Build;
import android.os.Bundle;
import android.os.Parcel;
import android.os.Parcelable;
import android.os.Process;
import android.provider.Settings;
import android.text.format.DateFormat;
import android.util.Log;
import androidx.annotation.AnyThread;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.StringDef;
import androidx.annotation.UiThread;
import androidx.lifecycle.Lifecycle;
import androidx.lifecycle.LifecycleObserver;
import androidx.lifecycle.OnLifecycleEvent;
import androidx.lifecycle.ProcessLifecycleOwner;
import java.io.File;
import java.io.FileNotFoundException;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.List;
import java.util.Map;
import org.mozilla.gecko.Clipboard;
import org.mozilla.gecko.EventDispatcher;
import org.mozilla.gecko.GeckoAppShell;
import org.mozilla.gecko.GeckoNetworkManager;
import org.mozilla.gecko.GeckoScreenChangeListener;
import org.mozilla.gecko.GeckoScreenOrientation;
import org.mozilla.gecko.GeckoScreenOrientation.ScreenOrientation;
import org.mozilla.gecko.GeckoSystemStateListener;
import org.mozilla.gecko.GeckoThread;
import org.mozilla.gecko.annotation.WrapForJNI;
import org.mozilla.gecko.crashhelper.CrashHelper;
import org.mozilla.gecko.process.MemoryController;
import org.mozilla.gecko.util.BundleEventListener;
import org.mozilla.gecko.util.DebugConfig;
import org.mozilla.gecko.util.EventCallback;
import org.mozilla.gecko.util.GeckoBundle;
import org.mozilla.gecko.util.ThreadUtils;

public final class GeckoRuntime implements Parcelable {
  private static final String LOGTAG = "GeckoRuntime";
  private static final boolean DEBUG = false;

  private static final String CONFIG_FILE_PATH_TEMPLATE =
      "/data/local/tmp/%s-geckoview-config.yaml";

  /**
   * Intent action sent to the crash handler when a crash is encountered.
   *
   * @see GeckoRuntimeSettings.Builder#crashHandler(Class)
   */
  public static final String ACTION_CRASHED = "org.mozilla.gecko.ACTION_CRASHED";

  /**
   * This is a key for extra data sent with {@link #ACTION_CRASHED}. It refers to a String with the
   * path to a Breakpad minidump file containing information about the crash. Several crash
   * reporters are able to ingest this in a crash report, including <a
   * href="https://sentry.io">Sentry</a> and Mozilla's <a
   * href="https://wiki.mozilla.org/Socorro">Socorro</a>. <br>
   * <br>
   * Be aware, the minidump can contain personally identifiable information. Ensure you are obeying
   * all applicable laws and policies before sending this to a remote server.
   *
   * @see GeckoRuntimeSettings.Builder#crashHandler(Class)
   */
  public static final String EXTRA_MINIDUMP_PATH = "minidumpPath";

  /**
   * This is a key for extra data sent with {@link #ACTION_CRASHED}. It refers to a string with the
   * path to a file containing extra metadata about the crash. The file contains key-value pairs in
   * the form
   *
   * <pre>Key=Value</pre>
   *
   * Be aware, it may contain sensitive data such as the URI that was loaded at the time of the
   * crash.
   */
  public static final String EXTRA_EXTRAS_PATH = "extrasPath";

  /**
   * This is a key for extra data sent with {@link #ACTION_CRASHED}. The value is a String matching
   * one of the `CRASHED_PROCESS_TYPE_*` constants, describing what type of process the crash
   * occurred in.
   *
   * @see GeckoSession.ContentDelegate#onCrash(GeckoSession)
   */
  public static final String EXTRA_CRASH_PROCESS_TYPE = "processType";

  /**
   * This is a key for extra data sent with {@link #ACTION_CRASHED}. The value is a String
   * containing the content process type, which might not be available even for child processes.
   *
   * @see GeckoSession.ContentDelegate#onCrash(GeckoSession)
   */
  public static final String EXTRA_CRASH_REMOTE_TYPE = "remoteType";

  /**
   * Value for {@link #EXTRA_CRASH_PROCESS_TYPE} indicating the main application process was
   * affected by the crash, which is therefore fatal.
   */
  public static final String CRASHED_PROCESS_TYPE_MAIN = "MAIN";

  /**
   * Value for {@link #EXTRA_CRASH_PROCESS_TYPE} indicating a foreground child process, such as a
   * content process, crashed. The application may be able to recover from this crash, but it was
   * likely noticable to the user.
   */
  public static final String CRASHED_PROCESS_TYPE_FOREGROUND_CHILD = "FOREGROUND_CHILD";

  /**
   * Value for {@link #EXTRA_CRASH_PROCESS_TYPE} indicating a background child process crashed. This
   * should have been recovered from automatically, and will have had minimal impact to the user, if
   * any.
   */
  public static final String CRASHED_PROCESS_TYPE_BACKGROUND_CHILD = "BACKGROUND_CHILD";

  private final MemoryController mMemoryController = new MemoryController();

  @Retention(RetentionPolicy.SOURCE)
  @StringDef(
      value = {
        CRASHED_PROCESS_TYPE_MAIN,
        CRASHED_PROCESS_TYPE_FOREGROUND_CHILD,
        CRASHED_PROCESS_TYPE_BACKGROUND_CHILD
      })
  public @interface CrashedProcessType {}

  private final class LifecycleListener implements LifecycleObserver {
    private boolean mPaused = false;

    public LifecycleListener() {}

    @OnLifecycleEvent(Lifecycle.Event.ON_CREATE)
    void onCreate() {
      Log.d(LOGTAG, "Lifecycle: onCreate");
    }

    @OnLifecycleEvent(Lifecycle.Event.ON_START)
    void onStart() {
      Log.d(LOGTAG, "Lifecycle: onStart");
    }

    @OnLifecycleEvent(Lifecycle.Event.ON_RESUME)
    void onResume() {
      Log.d(LOGTAG, "Lifecycle: onResume");
      if (mPaused) {
        // Do not trigger the first onResume event because it breaks nsAppShell::sPauseCount counter
        // thresholds.
        GeckoThread.onResume();
      } else {
        // Notify Gecko when the application has been moved in the foreground for the first time
        // after being created and started (used by the ExtensionProcessCrashObserver on the Gecko
        // side to adjust the appIsForeground property when the application-foreground or
        // application-background topics are not notified).
        EventDispatcher.getInstance().dispatch("GeckoView:InitialForeground", null);
      }
      mPaused = false;
      // Can resume location services, checks if was in use before going to background
      GeckoAppShell.resumeLocation();
      // Monitor network status and send change notifications to Gecko
      // while active.
      GeckoNetworkManager.getInstance().start(GeckoAppShell.getApplicationContext());

      // Set settings that may have changed between last app opening
      GeckoAppShell.setIs24HourFormat(
          DateFormat.is24HourFormat(GeckoAppShell.getApplicationContext()));

      // OnPrimaryClipChangedListener() won’t be triggered for a background
      // application, so update the clipboard sequence number once the
      // application returns to the foreground.
      ThreadUtils.sGeckoHandler.post(
          () -> {
            Clipboard.updateSequenceNumber(GeckoAppShell.getApplicationContext());
          });
    }

    @OnLifecycleEvent(Lifecycle.Event.ON_PAUSE)
    void onPause() {
      Log.d(LOGTAG, "Lifecycle: onPause");
      mPaused = true;
      // Pause listening for locations when in background
      GeckoAppShell.pauseLocation();
      // Stop monitoring network status while inactive.
      GeckoNetworkManager.getInstance().stop();
      GeckoThread.onPause();
      Clipboard.onPause();
    }
  }

  private static GeckoRuntime sDefaultRuntime;

  /**
   * Get the default runtime for the given context. This will create and initialize the runtime with
   * the default settings.
   *
   * <p>Note: Only use this for session-less apps. For regular apps, use create() instead.
   *
   * @param context An application context for the default runtime.
   * @return The (static) default runtime for the context.
   */
  @UiThread
  public static synchronized @NonNull GeckoRuntime getDefault(final @NonNull Context context) {
    ThreadUtils.assertOnUiThread();
    if (DEBUG) {
      Log.d(LOGTAG, "getDefault");
    }
    if (sDefaultRuntime == null) {
      sDefaultRuntime = new GeckoRuntime();
      sDefaultRuntime.attachTo(context);
      sDefaultRuntime.init(context, new GeckoRuntimeSettings());
    }

    return sDefaultRuntime;
  }

  private static GeckoRuntime sRuntime;
  private GeckoRuntimeSettings mSettings;
  private Delegate mDelegate;
  private ServiceWorkerDelegate mServiceWorkerDelegate;
  private WebNotificationDelegate mNotificationDelegate;
  private ActivityDelegate mActivityDelegate;
  private OrientationController mOrientationController;
  private StorageController mStorageController;
  private final WebExtensionController mWebExtensionController;
  private WebPushController mPushController;
  private final ContentBlockingController mContentBlockingController;
  private final Autocomplete.StorageProxy mAutocompleteStorageProxy;
  private final ProfilerController mProfilerController;
  private final GeckoScreenChangeListener mScreenChangeListener;

  private GeckoRuntime() {
    mWebExtensionController = new WebExtensionController(this);
    mContentBlockingController = new ContentBlockingController();
    mAutocompleteStorageProxy = new Autocomplete.StorageProxy();
    mProfilerController = new ProfilerController();
    mScreenChangeListener = new GeckoScreenChangeListener();

    if (sRuntime != null) {
      throw new IllegalStateException("Only one GeckoRuntime instance is allowed");
    }
    sRuntime = this;
  }

  @WrapForJNI
  @UiThread
  /* package */ @Nullable
  static GeckoRuntime getInstance() {
    return sRuntime;
  }

  /**
   * Called by mozilla::dom::ClientOpenWindow to retrieve the window id to use for a
   * ServiceWorkerClients.openWindow() request.
   *
   * @param url validated Url being requested to be opened in a new window.
   * @return SessionID to use for the request.
   */
  @SuppressLint("WrongThread") // for .isOpen() which is called on the UI thread
  @UiThread
  private static @NonNull GeckoResult<GeckoSession> serviceWorkerOpenWindow(
      final @NonNull String url) {
    if (sRuntime != null && sRuntime.mServiceWorkerDelegate != null) {
      ThreadUtils.assertOnUiThread();
      return sRuntime.mServiceWorkerDelegate.onOpenWindow(url);
    }
    return GeckoResult.fromException(
        new java.lang.RuntimeException("No available Service Worker delegate."));
  }

  /**
   * Attach the runtime to the given context.
   *
   * @param context The new context to attach to.
   */
  @UiThread
  public void attachTo(final @NonNull Context context) {
    ThreadUtils.assertOnUiThread();
    if (DEBUG) {
      Log.d(LOGTAG, "attachTo " + context.getApplicationContext());
    }
    final Context appContext = context.getApplicationContext();
    if (!appContext.equals(GeckoAppShell.getApplicationContext())) {
      GeckoAppShell.setApplicationContext(appContext);
    }
  }

  private final BundleEventListener mEventListener =
      new BundleEventListener() {
        @Override
        public void handleMessage(
            final String event, final GeckoBundle message, final EventCallback callback) {
          final Class<?> crashHandler = GeckoRuntime.this.getSettings().mCrashHandler;

          if ("Gecko:Exited".equals(event) && mDelegate != null) {
            mDelegate.onShutdown();
            EventDispatcher.getInstance()
                .unregisterUiThreadListener(mEventListener, "Gecko:Exited");
          } else if ("GeckoView:Test:NewTab".equals(event)) {
            final String url = message.getString("url", "about:blank");
            serviceWorkerOpenWindow(url)
                .then(
                    (GeckoResult.OnValueListener<GeckoSession, Void>)
                        session -> {
                          if (session == null) {
                            callback.sendSuccess(null);
                            return null;
                          }
                          if (!session.isOpen()) {
                            session.open(sRuntime);
                          }
                          callback.sendSuccess(session.getId());
                          return null;
                        })
                .exceptionally(
                    (GeckoResult.OnExceptionListener<Void>)
                        error -> {
                          callback.sendError(error + " Could not open tab.");
                          return null;
                        });
          } else if ("GeckoView:ChildCrashReport".equals(event) && crashHandler != null) {
            final Context context = GeckoAppShell.getApplicationContext();
            final Intent i = new Intent(ACTION_CRASHED, null, context, crashHandler);
            i.putExtra(EXTRA_MINIDUMP_PATH, message.getString(EXTRA_MINIDUMP_PATH));
            i.putExtra(EXTRA_EXTRAS_PATH, message.getString(EXTRA_EXTRAS_PATH));
            i.putExtra(EXTRA_CRASH_PROCESS_TYPE, message.getString(EXTRA_CRASH_PROCESS_TYPE));
            i.putExtra(EXTRA_CRASH_REMOTE_TYPE, message.getString(EXTRA_CRASH_REMOTE_TYPE));

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
              context.startForegroundService(i);
            } else {
              context.startService(i);
            }
          } else if ("GeckoView:ServiceWorkerOpenWindow".equals(event)) {
            final String url = message.getString("url", "about:blank");
            serviceWorkerOpenWindow(url)
                .then(
                    (GeckoResult.OnValueListener<GeckoSession, Void>)
                        session -> {
                          if (session == null) {
                            callback.sendSuccess(null);
                            return null;
                          }
                          final boolean isOpen = session.isOpen();
                          if (!isOpen) {
                            session.open(sRuntime);
                          }
                          final GeckoBundle bundle = new GeckoBundle();
                          bundle.putBoolean("isOpen", isOpen);
                          bundle.putString("sessionId", session.getId());
                          callback.sendSuccess(bundle);
                          return null;
                        })
                .exceptionally(
                    (GeckoResult.OnExceptionListener<Void>)
                        error -> {
                          callback.sendError(error + " Could not open tab.");
                          return null;
                        });
          }
        }
      };

  private static String getProcessName(final Context context) {
    final ActivityManager manager =
        (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
    final List<ActivityManager.RunningAppProcessInfo> infos = manager.getRunningAppProcesses();
    if (infos == null) {
      return null;
    }
    for (final ActivityManager.RunningAppProcessInfo info : infos) {
      if (info.pid == Process.myPid()) {
        return info.processName;
      }
    }

    return null;
  }

  private int[] startCrashHelper() {
    final Context context = GeckoAppShell.getApplicationContext();
    final CrashHelper.Pipes pipes = CrashHelper.createCrashHelperPipes(context);

    if (pipes == null) {
      Log.e(LOGTAG, "Could not create the crash reporter IPC pipes");
      return new int[] {-1, -1};
    }

    try {
      @SuppressWarnings("unchecked")
      final Class<? extends Service> cls =
          (Class<? extends Service>) Class.forName("org.mozilla.gecko.crashhelper.CrashHelper");
      final Intent i = new Intent(context, cls);
      final File minidumps = new File(context.getFilesDir(), "minidumps");
      context.bindService(
          i,
          CrashHelper.createConnection(
              pipes.mBreakpadServer, minidumps.getPath(), pipes.mListener, pipes.mServer),
          Context.BIND_AUTO_CREATE);
    } catch (final ClassNotFoundException e) {
      Log.w(LOGTAG, "Couldn't find the crash helper class");
    }

    return new int[] {pipes.mBreakpadClient.detachFd(), pipes.mClient.detachFd()};
  }

  /* package */ boolean init(
      final @NonNull Context context, final @NonNull GeckoRuntimeSettings settings) {
    if (DEBUG) {
      Log.d(LOGTAG, "init");
    }
    int flags = GeckoThread.FLAG_PRELOAD_CHILD;

    if (settings.getPauseForDebuggerEnabled()) {
      flags |= GeckoThread.FLAG_DEBUGGING;
    }

    if (!settings.getLowMemoryDetection()) {
      flags |= GeckoThread.FLAG_DISABLE_LOW_MEMORY_DETECTION;
    }

    final Class<?> crashHandler = settings.getCrashHandler();
    if (crashHandler != null) {
      try {
        final ServiceInfo info =
            context.getPackageManager().getServiceInfo(new ComponentName(context, crashHandler), 0);
        if (info.processName.equals(getProcessName(context))) {
          throw new IllegalArgumentException(
              "Crash handler service must run in a separate process");
        }

        EventDispatcher.getInstance()
            .registerUiThreadListener(mEventListener, "GeckoView:ChildCrashReport");

        flags |= GeckoThread.FLAG_ENABLE_NATIVE_CRASHREPORTER;
      } catch (final PackageManager.NameNotFoundException e) {
        throw new IllegalArgumentException("Crash handler must be registered as a service");
      }
    }

    GeckoAppShell.useMaxScreenDepth(settings.getUseMaxScreenDepth());
    GeckoAppShell.setDisplayDensityOverride(settings.getDisplayDensityOverride());
    GeckoAppShell.setDisplayDpiOverride(settings.getDisplayDpiOverride());
    GeckoAppShell.setScreenSizeOverride(settings.getScreenSizeOverride());
    GeckoAppShell.setCrashHandlerService(settings.getCrashHandler());
    GeckoFontScaleListener.getInstance().attachToContext(context, settings);

    Bundle extras = settings.getExtras();
    String[] args = settings.getArguments();
    Map<String, Object> prefs = settings.getPrefsMap();

    // Older versions have problems with SnakeYaml
    String configFilePath = settings.getConfigFilePath();
    if (configFilePath == null) {
      // Default to /data/local/tmp/$PACKAGE-geckoview-config.yaml if android:debuggable="true"
      // or if this application is the current Android "debug_app", and to not read configuration
      // from a file otherwise.
      if (isApplicationDebuggable(context) || isApplicationCurrentDebugApp(context)) {
        configFilePath =
            String.format(CONFIG_FILE_PATH_TEMPLATE, context.getApplicationInfo().packageName);
      }
    }

    if (configFilePath != null && !configFilePath.isEmpty()) {
      try {
        final DebugConfig debugConfig = DebugConfig.fromFile(new File(configFilePath));
        Log.i(LOGTAG, "Adding debug configuration from: " + configFilePath);
        prefs = debugConfig.mergeIntoPrefs(prefs);
        args = debugConfig.mergeIntoArgs(args);
        extras = debugConfig.mergeIntoExtras(extras);
      } catch (final DebugConfig.ConfigException e) {
        Log.w(LOGTAG, "Failed to add debug configuration from: " + configFilePath, e);
      } catch (final FileNotFoundException e) {
      }
    }

    final int[] fds = startCrashHelper();

    final GeckoThread.InitInfo info =
        GeckoThread.InitInfo.builder()
            .args(args)
            .extras(extras)
            .flags(flags)
            .prefs(prefs)
            .outFilePath(extras != null ? extras.getString("out_file") : null)
            .fds(fds)
            .build();

    if (info.xpcshell
        && !"org.mozilla.geckoview.test_runner"
            .equals(context.getApplicationContext().getPackageName())) {
      throw new IllegalArgumentException("Only the test app can run -xpcshell.");
    }

    if (info.xpcshell) {
      // Xpcshell tests need multi-e10s to work properly
      settings.setProcessCount(BuildConfig.MOZ_ANDROID_CONTENT_SERVICE_COUNT);
    }

    if (!GeckoThread.init(info)) {
      Log.w(LOGTAG, "init failed (could not initiate GeckoThread)");
      return false;
    }

    if (!GeckoThread.launch()) {
      Log.w(LOGTAG, "init failed (GeckoThread already launched)");
      return false;
    }

    mSettings = settings;

    // Bug 1453062 -- the EventDispatcher should really live here (or in GeckoThread)
    EventDispatcher.getInstance()
        .registerUiThreadListener(
            mEventListener,
            "Gecko:Exited",
            "GeckoView:Test:NewTab",
            "GeckoView:ServiceWorkerOpenWindow");

    // Attach and commit settings.
    mSettings.attachTo(this);

    // Initialize the system ClipboardManager by accessing it on the main thread.
    GeckoAppShell.getApplicationContext().getSystemService(Context.CLIPBOARD_SERVICE);

    // Add process lifecycle listener to react to backgrounding events.
    ProcessLifecycleOwner.get().getLifecycle().addObserver(new LifecycleListener());

    // Add Display Manager listener to listen screen orientation change.
    if (mScreenChangeListener != null) {
      mScreenChangeListener.enable();
    }

    mProfilerController.addMarker(
        "GeckoView Initialization START", mProfilerController.getProfilerTime());
    return true;
  }

  private boolean isApplicationDebuggable(final @NonNull Context context) {
    final ApplicationInfo applicationInfo = context.getApplicationInfo();
    return (applicationInfo.flags & ApplicationInfo.FLAG_DEBUGGABLE) != 0;
  }

  private boolean isApplicationCurrentDebugApp(final @NonNull Context context) {
    final ApplicationInfo applicationInfo = context.getApplicationInfo();

    final String currentDebugApp =
        Settings.Global.getString(context.getContentResolver(), Settings.Global.DEBUG_APP);
    return applicationInfo.packageName.equals(currentDebugApp);
  }

  /* package */ void setDefaultPrefs(final GeckoBundle prefs) {
    EventDispatcher.getInstance().dispatch("GeckoView:SetDefaultPrefs", prefs);
  }

  /**
   * Create a new runtime with default settings and attach it to the given context.
   *
   * <p>Create will throw if there is already an active Gecko instance running, to prevent that,
   * bind the runtime to the process lifetime instead of the activity lifetime.
   *
   * @param context The context of the runtime.
   * @return An initialized runtime.
   */
  @UiThread
  public static @NonNull GeckoRuntime create(final @NonNull Context context) {
    ThreadUtils.assertOnUiThread();
    return create(context, new GeckoRuntimeSettings());
  }

  /**
   * Returns a WebExtensionController for this GeckoRuntime.
   *
   * @return an instance of {@link WebExtensionController}.
   */
  @UiThread
  public @NonNull WebExtensionController getWebExtensionController() {
    return mWebExtensionController;
  }

  /**
   * Returns the ContentBlockingController for this GeckoRuntime.
   *
   * @return An instance of {@link ContentBlockingController}.
   */
  @UiThread
  public @NonNull ContentBlockingController getContentBlockingController() {
    return mContentBlockingController;
  }

  /**
   * Returns a ProfilerController for this GeckoRuntime.
   *
   * @return an instance of {@link ProfilerController}.
   */
  @UiThread
  public @NonNull ProfilerController getProfilerController() {
    return mProfilerController;
  }

  /**
   * Create a new runtime with the given settings and attach it to the given context.
   *
   * <p>Create will throw if there is already an active Gecko instance running, to prevent that,
   * bind the runtime to the process lifetime instead of the activity lifetime.
   *
   * @param context The context of the runtime.
   * @param settings The settings for the runtime.
   * @return An initialized runtime.
   */
  @UiThread
  public static @NonNull GeckoRuntime create(
      final @NonNull Context context, final @NonNull GeckoRuntimeSettings settings) {
    ThreadUtils.assertOnUiThread();
    if (DEBUG) {
      Log.d(LOGTAG, "create " + context);
    }

    final GeckoRuntime runtime = new GeckoRuntime();
    runtime.attachTo(context);

    if (!runtime.init(context, settings)) {
      throw new IllegalStateException("Failed to initialize GeckoRuntime");
    }

    context.registerComponentCallbacks(runtime.mMemoryController);

    return runtime;
  }

  /** Shutdown the runtime. This will invalidate all attached sessions. */
  @AnyThread
  public void shutdown() {
    if (DEBUG) {
      Log.d(LOGTAG, "shutdown");
    }

    GeckoSystemStateListener.getInstance().shutdown();

    if (mScreenChangeListener != null) {
      mScreenChangeListener.disable();
    }

    GeckoThread.forceQuit();
  }

  public interface Delegate {
    /**
     * This is called when the runtime shuts down. Any GeckoSession instances that were opened with
     * this instance are now considered closed.
     */
    @UiThread
    void onShutdown();
  }

  /**
   * Set a delegate for receiving callbacks relevant to to this GeckoRuntime.
   *
   * @param delegate an implementation of {@link GeckoRuntime.Delegate}.
   */
  @UiThread
  public void setDelegate(final @Nullable Delegate delegate) {
    ThreadUtils.assertOnUiThread();
    mDelegate = delegate;
  }

  /**
   * Returns the current delegate, if any.
   *
   * @return an instance of {@link GeckoRuntime.Delegate} or null if no delegate has been set.
   */
  @UiThread
  public @Nullable Delegate getDelegate() {
    return mDelegate;
  }

  /**
   * Set the {@link Autocomplete.StorageDelegate} instance on this runtime. This delegate is
   * required for handling autocomplete storage requests.
   *
   * @param delegate The {@link Autocomplete.StorageDelegate} handling autocomplete storage
   *     requests.
   */
  @UiThread
  public void setAutocompleteStorageDelegate(
      final @Nullable Autocomplete.StorageDelegate delegate) {
    ThreadUtils.assertOnUiThread();
    mAutocompleteStorageProxy.setDelegate(delegate);
  }

  /**
   * Get the {@link Autocomplete.StorageDelegate} instance set on this runtime.
   *
   * @return The {@link Autocomplete.StorageDelegate} set on this runtime.
   */
  @UiThread
  public @Nullable Autocomplete.StorageDelegate getAutocompleteStorageDelegate() {
    ThreadUtils.assertOnUiThread();
    return mAutocompleteStorageProxy.getDelegate();
  }

  @UiThread
  public interface ServiceWorkerDelegate {

    /**
     * This is called when a service worker tries to open a new window using client.openWindow() The
     * GeckoView application should provide an open {@link GeckoSession} to open the url.
     *
     * @param url Url which the Service Worker wishes to open in a new window.
     * @return New or existing open {@link GeckoSession} in which to open the requested url.
     * @see <a href="https://developer.mozilla.org/en-US/docs/Web/API/Service_Worker_API">Service
     *     Worker API</a>
     * @see <a
     *     href="https://developer.mozilla.org/en-US/docs/Web/API/Clients/openWindow">openWindow()</a>
     */
    @UiThread
    @NonNull
    GeckoResult<GeckoSession> onOpenWindow(@NonNull String url);
  }

  /**
   * Sets the {@link ServiceWorkerDelegate} to be used for Service Worker requests.
   *
   * @param serviceWorkerDelegate An instance of {@link ServiceWorkerDelegate}.
   * @see <a href="https://developer.mozilla.org/en-US/docs/Web/API/Service_Worker_API">Service
   *     Worker API</a>
   */
  @UiThread
  public void setServiceWorkerDelegate(
      final @Nullable ServiceWorkerDelegate serviceWorkerDelegate) {
    mServiceWorkerDelegate = serviceWorkerDelegate;
  }

  /**
   * Gets the {@link ServiceWorkerDelegate} to be used for Service Worker requests.
   *
   * @return the {@link ServiceWorkerDelegate} instance set by {@link #setServiceWorkerDelegate}
   */
  @UiThread
  @Nullable
  public ServiceWorkerDelegate getServiceWorkerDelegate() {
    return mServiceWorkerDelegate;
  }

  /**
   * Sets the delegate to be used for handling Web Notifications.
   *
   * @param delegate An instance of {@link WebNotificationDelegate}.
   * @see <a href="https://developer.mozilla.org/en-US/docs/Web/API/Notification">Web
   *     Notifications</a>
   */
  @UiThread
  public void setWebNotificationDelegate(final @Nullable WebNotificationDelegate delegate) {
    mNotificationDelegate = delegate;
  }

  @WrapForJNI
  /* package */ float textScaleFactor() {
    return getSettings().getFontSizeFactor();
  }

  @WrapForJNI
  /* package */ boolean usesDarkTheme() {
    switch (getSettings().getPreferredColorScheme()) {
      case GeckoRuntimeSettings.COLOR_SCHEME_SYSTEM:
        return GeckoSystemStateListener.getInstance().isNightMode();
      case GeckoRuntimeSettings.COLOR_SCHEME_DARK:
        return true;
      case GeckoRuntimeSettings.COLOR_SCHEME_LIGHT:
      default:
        return false;
    }
  }

  /**
   * Returns the current WebNotificationDelegate, if any
   *
   * @return an instance of WebNotificationDelegate or null if no delegate has been set
   */
  @WrapForJNI
  @UiThread
  public @Nullable WebNotificationDelegate getWebNotificationDelegate() {
    return mNotificationDelegate;
  }

  @WrapForJNI
  @AnyThread
  private void notifyOnShow(final WebNotification notification) {
    ThreadUtils.runOnUiThread(
        () -> {
          if (mNotificationDelegate != null) {
            mNotificationDelegate.onShowNotification(notification);
          }
        });
  }

  @WrapForJNI
  @AnyThread
  private void notifyOnClose(final WebNotification notification) {
    ThreadUtils.runOnUiThread(
        () -> {
          if (mNotificationDelegate != null) {
            mNotificationDelegate.onCloseNotification(notification);
          }
        });
  }

  /**
   * This is used to allow GeckoRuntime to start activities via the embedding application (and
   * {@link android.app.Activity}). Currently this is used to invoke the Google Play FIDO Activity
   * in order to integrate with the Web Authentication API.
   *
   * @see <a href="https://developer.mozilla.org/en-US/docs/Web/API/Web_Authentication_API">Web
   *     Authentication API</a>
   */
  public interface ActivityDelegate {
    /**
     * Sometimes GeckoView needs the application to perform a {@link
     * android.app.Activity#startActivityForResult(Intent, int)} on its behalf. Implementations of
     * this method should call that based on the information in the passed {@link PendingIntent},
     * collect the result, and resolve the returned {@link GeckoResult} with that data. If the
     * Activity does not return {@link android.app.Activity#RESULT_OK}, the {@link GeckoResult} must
     * be completed with an exception of your choosing.
     *
     * @param intent The {@link PendingIntent} to launch
     * @return A {@link GeckoResult} that is eventually resolved with the Activity result.
     */
    @UiThread
    @Nullable
    GeckoResult<Intent> onStartActivityForResult(@NonNull PendingIntent intent);
  }

  /**
   * Set the {@link ActivityDelegate} instance on this runtime. This delegate is used to provide
   * GeckoView support for launching external activities and receiving results from those
   * activities.
   *
   * @param delegate The {@link ActivityDelegate} handling intent launching requests.
   */
  @UiThread
  public void setActivityDelegate(final @Nullable ActivityDelegate delegate) {
    ThreadUtils.assertOnUiThread();
    mActivityDelegate = delegate;
  }

  /**
   * Get the {@link ActivityDelegate} instance set on this runtime, if any,
   *
   * @return The {@link ActivityDelegate} set on this runtime.
   */
  @UiThread
  public @Nullable ActivityDelegate getActivityDelegate() {
    ThreadUtils.assertOnUiThread();
    return mActivityDelegate;
  }

  @AnyThread
  /* package */ GeckoResult<Intent> startActivityForResult(final @NonNull PendingIntent intent) {
    if (!ThreadUtils.isOnUiThread()) {
      // Delegates expect to be called on the UI thread.
      final GeckoResult<Intent> result = new GeckoResult<>();

      ThreadUtils.runOnUiThread(
          () -> {
            final GeckoResult<Intent> delegateResult = startActivityForResult(intent);
            if (delegateResult != null) {
              delegateResult.accept(
                  val -> result.complete(val), e -> result.completeExceptionally(e));
            } else {
              result.completeExceptionally(new IllegalStateException("No result"));
            }
          });

      return result;
    }

    if (mActivityDelegate == null) {
      return GeckoResult.fromException(new IllegalStateException("No delegate attached"));
    }

    @SuppressLint("WrongThread")
    GeckoResult<Intent> result = mActivityDelegate.onStartActivityForResult(intent);
    if (result == null) {
      result = GeckoResult.fromException(new IllegalStateException("No result"));
    }

    return result;
  }

  @AnyThread
  @SuppressWarnings("checkstyle:javadocmethod")
  public @NonNull GeckoRuntimeSettings getSettings() {
    return mSettings;
  }

  /** Notify Gecko that the screen orientation has changed. */
  @UiThread
  public void orientationChanged() {
    ThreadUtils.assertOnUiThread();
    GeckoScreenOrientation.getInstance().update();
  }

  /**
   * Notify Gecko that the device configuration has changed.
   *
   * @param newConfig The new Configuration object, {@link android.content.res.Configuration}.
   */
  @UiThread
  public void configurationChanged(final @NonNull Configuration newConfig) {
    ThreadUtils.assertOnUiThread();
    GeckoSystemStateListener.getInstance().updateNightMode(newConfig.uiMode);
  }

  /**
   * Notify Gecko that the screen orientation has changed.
   *
   * @param newOrientation The new screen orientation, as retrieved e.g. from the current {@link
   *     android.content.res.Configuration}.
   */
  @UiThread
  public void orientationChanged(final int newOrientation) {
    ThreadUtils.assertOnUiThread();
    GeckoScreenOrientation.getInstance().update(newOrientation);
  }

  /**
   * Get the orientation controller for this runtime. The orientation controller can be used to
   * manage changes to and locking of the screen orientation.
   *
   * @return The {@link OrientationController} for this instance.
   */
  @UiThread
  public @NonNull OrientationController getOrientationController() {
    ThreadUtils.assertOnUiThread();

    if (mOrientationController == null) {
      mOrientationController = new OrientationController();
    }
    return mOrientationController;
  }

  /**
   * Converts GeckoScreenOrientation to ActivityInfo orientation
   *
   * @return A {@link ActivityInfo} orientation.
   */
  @AnyThread
  private int toAndroidOrientation(final int geckoOrientation) {
    if (geckoOrientation == ScreenOrientation.PORTRAIT_PRIMARY.value) {
      return ActivityInfo.SCREEN_ORIENTATION_PORTRAIT;
    } else if (geckoOrientation == ScreenOrientation.PORTRAIT_SECONDARY.value) {
      return ActivityInfo.SCREEN_ORIENTATION_REVERSE_PORTRAIT;
    } else if (geckoOrientation == ScreenOrientation.LANDSCAPE_PRIMARY.value) {
      return ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE;
    } else if (geckoOrientation == ScreenOrientation.LANDSCAPE_SECONDARY.value) {
      return ActivityInfo.SCREEN_ORIENTATION_REVERSE_LANDSCAPE;
    } else if (geckoOrientation == ScreenOrientation.DEFAULT.value) {
      return ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED;
    } else if (geckoOrientation == ScreenOrientation.PORTRAIT.value) {
      return ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT;
    } else if (geckoOrientation == ScreenOrientation.LANDSCAPE.value) {
      return ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE;
    } else if (geckoOrientation == ScreenOrientation.ANY.value) {
      return ActivityInfo.SCREEN_ORIENTATION_FULL_SENSOR;
    }
    return ActivityInfo.SCREEN_ORIENTATION_PORTRAIT;
  }

  /**
   * Lock screen orientation using OrientationController's onOrientationLock.
   *
   * @return A {@link GeckoResult} that resolves an orientation lock.
   */
  @WrapForJNI(calledFrom = "gecko")
  private @NonNull GeckoResult<Boolean> lockScreenOrientation(final int aOrientation) {
    final GeckoResult<Boolean> res = new GeckoResult<>();
    ThreadUtils.runOnUiThread(
        () -> {
          final OrientationController.OrientationDelegate delegate =
              getOrientationController().getDelegate();
          if (delegate == null) {
            // Delegate is not set
            res.completeExceptionally(new Exception("Not supported"));
            return;
          }
          final GeckoResult<AllowOrDeny> response =
              delegate.onOrientationLock(toAndroidOrientation(aOrientation));
          if (response == null) {
            // Delegate is default. So lock orientation is not implemented
            res.completeExceptionally(new Exception("Not supported"));
            return;
          }
          res.completeFrom(response.map(v -> v == AllowOrDeny.ALLOW));
        });
    return res;
  }

  /** Unlock screen orientation using OrientationController's onOrientationUnlock. */
  @WrapForJNI(calledFrom = "gecko")
  private void unlockScreenOrientation() {
    ThreadUtils.runOnUiThread(
        () -> {
          final OrientationController.OrientationDelegate delegate =
              getOrientationController().getDelegate();
          if (delegate != null) {
            delegate.onOrientationUnlock();
          }
        });
  }

  /**
   * Get the storage controller for this runtime. The storage controller can be used to manage
   * persistent storage data accumulated by {@link GeckoSession}.
   *
   * @return The {@link StorageController} for this instance.
   */
  @UiThread
  public @NonNull StorageController getStorageController() {
    ThreadUtils.assertOnUiThread();

    if (mStorageController == null) {
      mStorageController = new StorageController();
    }
    return mStorageController;
  }

  /**
   * Get the Web Push controller for this runtime. The Web Push controller can be used to allow
   * content to use the Web Push API.
   *
   * @return The {@link WebPushController} for this instance.
   */
  @UiThread
  public @NonNull WebPushController getWebPushController() {
    ThreadUtils.assertOnUiThread();

    if (mPushController == null) {
      mPushController = new WebPushController();
    }

    return mPushController;
  }

  /**
   * Appends notes to crash report.
   *
   * @param notes The application notes to append to the crash report.
   */
  @AnyThread
  public void appendAppNotesToCrashReport(@NonNull final String notes) {
    final String notesWithNewLine = notes + "\n";
    if (GeckoThread.isStateAtLeast(GeckoThread.State.PROFILE_READY)) {
      GeckoAppShell.nativeAppendAppNotesToCrashReport(notesWithNewLine);
    } else {
      GeckoThread.queueNativeCallUntil(
          GeckoThread.State.PROFILE_READY,
          GeckoAppShell.class,
          "nativeAppendAppNotesToCrashReport",
          String.class,
          notesWithNewLine);
    }
    // This function already adds a newline
    GeckoAppShell.appendAppNotesToCrashReport(notes);
  }

  @Override // Parcelable
  @AnyThread
  public int describeContents() {
    return 0;
  }

  @Override // Parcelable
  @AnyThread
  public void writeToParcel(final Parcel out, final int flags) {
    out.writeParcelable(mSettings, flags);
  }

  // AIDL code may call readFromParcel even though it's not part of Parcelable.
  @AnyThread
  @SuppressWarnings("checkstyle:javadocmethod")
  public void readFromParcel(final @NonNull Parcel source) {
    mSettings = source.readParcelable(getClass().getClassLoader());
  }

  public static final Parcelable.Creator<GeckoRuntime> CREATOR =
      new Parcelable.Creator<GeckoRuntime>() {
        @Override
        @AnyThread
        public GeckoRuntime createFromParcel(final Parcel in) {
          final GeckoRuntime runtime = new GeckoRuntime();
          runtime.readFromParcel(in);
          return runtime;
        }

        @Override
        @AnyThread
        public GeckoRuntime[] newArray(final int size) {
          return new GeckoRuntime[size];
        }
      };

  /**
   * Whether the default `interactive-widget` is `resizes-visual`.
   *
   * @return True the default `interactive-widget` is `resizes-visual`, false otherwise.
   */
  @AnyThread
  public boolean isInteractiveWidgetDefaultResizesVisual() {
    if (!GeckoThread.isStateAtLeast(GeckoThread.State.JNI_READY)) {
      return false;
    }
    return GeckoAppShell.isInteractiveWidgetDefaultResizesVisual();
  }
}
