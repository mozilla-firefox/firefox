/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview;

import android.annotation.SuppressLint;
import android.app.ForegroundServiceStartNotAllowedException;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Process;
import android.util.Log;
import androidx.annotation.AnyThread;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.UUID;
import org.json.JSONException;
import org.json.JSONObject;
import org.mozilla.gecko.GeckoAppShell;
import org.mozilla.gecko.util.GeckoBundle;

/** Handler for processing application crashes and sending crash reports. */
public class CrashHandler implements Thread.UncaughtExceptionHandler {

  private static final String LOGTAG = "GeckoCrashHandler";
  private static final Thread MAIN_THREAD = Thread.currentThread();
  private static final String DEFAULT_SERVER_URL =
      "https://crash-reports.mozilla.com/submit?id=%1$s&version=%2$s&buildid=%3$s";

  // Context for getting device information
  private @Nullable final Context mAppContext;
  // Thread that this handler applies to, or null for a global handler
  private @Nullable final Thread mHandlerThread;
  private final @Nullable Thread.UncaughtExceptionHandler systemUncaughtHandler;

  private boolean mCrashing;
  private boolean mUnregistered;

  private @Nullable final Class<? extends Service> mHandlerService;

  /**
   * Get the root exception from the 'cause' chain of an exception.
   *
   * @param exc An exception
   * @return The root exception
   */
  @AnyThread
  @NonNull
  public static Throwable getRootException(@NonNull final Throwable exc) {
    Throwable cause;
    Throwable result = exc;
    for (cause = exc; cause != null; cause = cause.getCause()) {
      result = cause;
    }

    return result;
  }

  /**
   * Get the standard stack trace string of an exception.
   *
   * @param exc An exception
   * @return The exception stack trace.
   */
  @AnyThread
  @NonNull
  public static String getExceptionStackTrace(@NonNull final Throwable exc) {
    final StringWriter sw = new StringWriter();
    final PrintWriter pw = new PrintWriter(sw);
    exc.printStackTrace(pw);
    pw.flush();
    return sw.toString();
  }

  /** Terminate the current process. */
  @AnyThread
  public static void terminateProcess() {
    Process.killProcess(Process.myPid());
  }

  /**
   * Create and register a CrashHandler for all threads and thread groups.
   *
   * @param handlerService Service receiving native code crashes
   */
  public CrashHandler(@Nullable final Class<? extends Service> handlerService) {
    this((Context) null, handlerService);
  }

  /**
   * Create and register a CrashHandler for all threads and thread groups.
   *
   * @param aAppContext A Context for retrieving application information.
   * @param aHandlerService Service receiving native code crashes
   */
  @SuppressWarnings("this-escape")
  public CrashHandler(
      @Nullable final Context aAppContext,
      @Nullable final Class<? extends Service> aHandlerService) {
    this.mAppContext = aAppContext;
    this.mHandlerThread = null;
    this.mHandlerService = aHandlerService;
    this.systemUncaughtHandler = Thread.getDefaultUncaughtExceptionHandler();
    Thread.setDefaultUncaughtExceptionHandler(this);
  }

  /**
   * Create and register a CrashHandler for a particular thread.
   *
   * @param thread A thread to register the CrashHandler
   * @param handlerService Service receiving native code crashes
   */
  public CrashHandler(final Thread thread, final Class<? extends Service> handlerService) {
    this(thread, null, handlerService);
  }

  /**
   * Create and register a CrashHandler for a particular thread.
   *
   * @param thread A thread to register the CrashHandler
   * @param aAppContext A Context for retrieving application information.
   * @param aHandlerService Service receiving native code crashes
   */
  @SuppressWarnings("this-escape")
  public CrashHandler(
      @Nullable final Thread thread,
      final Context aAppContext,
      final Class<? extends Service> aHandlerService) {
    this.mAppContext = aAppContext;
    this.mHandlerThread = thread;
    this.mHandlerService = aHandlerService;
    this.systemUncaughtHandler = thread.getUncaughtExceptionHandler();
    thread.setUncaughtExceptionHandler(this);
  }

  /** Unregister this CrashHandler for exception handling. */
  @AnyThread
  public void unregister() {
    mUnregistered = true;

    // Restore the previous handler if we are still the topmost handler.
    // If not, we are part of a chain of handlers, and we cannot just restore the previous
    // handler, because that would replace whatever handler that's above us in the chain.

    if (mHandlerThread != null) {
      if (mHandlerThread.getUncaughtExceptionHandler() == this) {
        mHandlerThread.setUncaughtExceptionHandler(systemUncaughtHandler);
      }
    } else {
      if (Thread.getDefaultUncaughtExceptionHandler() == this) {
        Thread.setDefaultUncaughtExceptionHandler(systemUncaughtHandler);
      }
    }
  }

  /**
   * Record an exception stack in logs.
   *
   * @param thread The exception thread
   * @param exc An exception
   */
  @AnyThread
  public static void logException(@NonNull final Thread thread, @NonNull final Throwable exc) {
    try {
      Log.e(
          LOGTAG,
          ">>> REPORTING UNCAUGHT EXCEPTION FROM THREAD "
              + thread.getId()
              + " (\""
              + thread.getName()
              + "\")",
          exc);

      if (MAIN_THREAD != thread) {
        Log.e(LOGTAG, "Main thread (" + MAIN_THREAD.getId() + ") stack:");
        for (final StackTraceElement ste : MAIN_THREAD.getStackTrace()) {
          Log.e(LOGTAG, "    " + ste.toString());
        }
      }
    } catch (final Throwable e) {
      // If something throws here, we want to continue to report the exception,
      // so we catch all exceptions and ignore them.
    }
  }

  private static long getCrashTime() {
    return System.currentTimeMillis() / 1000;
  }

  private static long getStartupTime() {
    // Process start time is also the proc file modified time.
    final long uptimeMins = (new File("/proc/self/cmdline")).lastModified();
    if (uptimeMins == 0L) {
      return getCrashTime();
    }
    return uptimeMins / 1000;
  }

  private static String getJavaPackageName() {
    return CrashHandler.class.getPackage().getName();
  }

  @Nullable
  private static String getProcessName() {
    try {
      final FileReader reader = new FileReader("/proc/self/cmdline");
      final char[] buffer = new char[64];
      try {
        if (reader.read(buffer) > 0) {
          // cmdline is delimited by '\0', and we want the first token.
          final int nul = Arrays.asList(buffer).indexOf('\0');
          return (new String(buffer, 0, nul < 0 ? buffer.length : nul)).trim();
        }
      } finally {
        reader.close();
      }
    } catch (final IOException e) {
    }

    return null;
  }

  /**
   * {@return the application package name if context is not null; if context is null,
   * CrashHandler's package name will be returned}
   */
  @Nullable
  @AnyThread
  public String getAppPackageName() {
    final Context context = getAppContext();

    if (context != null) {
      return context.getPackageName();
    }

    // Package name is also the process name in most cases.
    final String processName = getProcessName();
    if (processName != null) {
      return processName;
    }

    // Fallback to using CrashHandler's package name.
    return getJavaPackageName();
  }

  /** {@return the application context} */
  @AnyThread
  @Nullable
  public Context getAppContext() {
    return mAppContext;
  }

  /**
   * Get the crash "extras" to be reported.
   *
   * @param thread The exception thread
   * @param exc An exception
   * @return "Extras" in the from of a Bundle
   */
  @AnyThread
  @NonNull
  public Bundle getCrashExtras(@NonNull final Thread thread, @NonNull final Throwable exc) {
    final Context context = getAppContext();
    final Bundle extras = new Bundle();
    final String pkgName = getAppPackageName();

    extras.putLong(CrashReport.Annotation.CrashTime.toString(), getCrashTime());
    extras.putLong(CrashReport.Annotation.StartupTime.toString(), getStartupTime());
    extras.putString(CrashReport.Annotation.Android_ProcessName.toString(), getProcessName());
    extras.putString(CrashReport.Annotation.Android_PackageName.toString(), pkgName);

    final String notes = GeckoAppShell.getAppNotes();
    if (notes != null) {
      extras.putString(CrashReport.Annotation.Notes.toString(), notes);
    }

    if (context != null) {
      final PackageManager pkgMgr = context.getPackageManager();
      try {
        final PackageInfo pkgInfo = pkgMgr.getPackageInfo(pkgName, 0);
        extras.putString(CrashReport.Annotation.Version.toString(), pkgInfo.versionName);
        extras.putInt(CrashReport.Annotation.BuildID.toString(), pkgInfo.versionCode);
        extras.putLong(
            CrashReport.Annotation.InstallTime.toString(), pkgInfo.lastUpdateTime / 1000);
      } catch (final PackageManager.NameNotFoundException e) {
        Log.i(LOGTAG, "Error getting package info", e);
      }
    }

    extras.putString(CrashReport.Annotation.JavaStackTrace.toString(), getExceptionStackTrace(exc));
    return extras;
  }

  /**
   * Get the crash minidump content to be reported.
   *
   * @param thread The exception thread
   * @param exc An exception
   * @return Minidump content
   */
  @NonNull
  @AnyThread
  public byte[] getCrashDump(@Nullable final Thread thread, @Nullable final Throwable exc) {
    return new byte[0]; // No minidump.
  }

  @AnyThread
  @NonNull
  private static String normalizeUrlString(@Nullable final String str) {
    if (str == null) {
      return "";
    }
    return Uri.encode(str);
  }

  /**
   * Get the server URL to send the crash report to.
   *
   * @param extras The crash extras Bundle
   * @return the URL that the crash reporter will submit reports to.
   */
  @NonNull
  @AnyThread
  public String getServerUrl(@NonNull final Bundle extras) {
    return String.format(
        DEFAULT_SERVER_URL,
        normalizeUrlString(extras.getString(CrashReport.Annotation.ProductID.toString())),
        normalizeUrlString(extras.getString(CrashReport.Annotation.Version.toString())),
        normalizeUrlString(extras.getString(CrashReport.Annotation.BuildID.toString())));
  }

  /**
   * Launch the crash reporter activity that sends the crash report to the server.
   *
   * @param dumpFile Path for the minidump file
   * @param extraFile Path for the crash extra file
   * @return Whether the crash reporter was successfully launched
   */
  @AnyThread
  public boolean launchCrashReporter(
      @NonNull final String dumpFile, @NonNull final String extraFile) {
    if (mHandlerService == null) {
      Log.w(LOGTAG, "No crash handler service defined, unable to report crash");
      return false;
    }

    return launchCrashReporter(
        mHandlerService,
        getAppContext(),
        getAppPackageName(),
        dumpFile,
        extraFile,
        GeckoRuntime.CRASHED_PROCESS_VISIBILITY_MAIN,
        "main",
        null);
  }

  @AnyThread
  private static boolean launchCrashReporter(
      @NonNull final Class<?> handlerService,
      @Nullable final Context context,
      @NonNull final String appPackageName,
      @NonNull final String dumpFile,
      @NonNull final String extraFile,
      @NonNull final String visibility,
      @NonNull final String processType,
      @Nullable final String remoteType) {
    try {
      if (context != null) {
        final Intent intent =
            new Intent(GeckoRuntime.ACTION_CRASHED, null, context, handlerService);
        intent.putExtra(GeckoRuntime.EXTRA_MINIDUMP_PATH, dumpFile);
        intent.putExtra(GeckoRuntime.EXTRA_EXTRAS_PATH, extraFile);
        intent.putExtra(GeckoRuntime.EXTRA_CRASH_PROCESS_VISIBILITY, visibility);
        intent.putExtra(GeckoRuntime.EXTRA_CRASH_PROCESS_TYPE, processType);
        if (remoteType != null) {
          intent.putExtra(GeckoRuntime.EXTRA_CRASH_REMOTE_TYPE, remoteType);
        }
        context.startForegroundService(intent);
        return true;
      }

      final List<String> args =
          new ArrayList<>(
              Arrays.asList(
                  "/system/bin/am",
                  "start-foreground-service",
                  "--user",
                  /* USER_CURRENT_OR_SELF */ "-3",
                  "-a",
                  GeckoRuntime.ACTION_CRASHED,
                  "-n",
                  appPackageName + '/' + handlerService.getName(),
                  "--es",
                  GeckoRuntime.EXTRA_MINIDUMP_PATH,
                  dumpFile,
                  "--es",
                  GeckoRuntime.EXTRA_EXTRAS_PATH,
                  extraFile,
                  "--es",
                  GeckoRuntime.EXTRA_CRASH_PROCESS_VISIBILITY,
                  visibility,
                  "--es",
                  GeckoRuntime.EXTRA_CRASH_PROCESS_TYPE,
                  processType));
      if (remoteType != null) {
        args.add("--es");
        args.add(GeckoRuntime.EXTRA_CRASH_REMOTE_TYPE);
        args.add(remoteType);
      }

      final ProcessBuilder pb = new ProcessBuilder();
      pb.command(args).start().waitFor();
    } catch (final IOException e) {
      Log.e(LOGTAG, "Error launching crash reporter", e);
      return false;

    } catch (final IllegalStateException e) {
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S
          && e instanceof ForegroundServiceStartNotAllowedException) {
        Log.e(LOGTAG, "Error launching crash reporter", e);
        return false;
      }
      throw e;

    } catch (final InterruptedException e) {
      Log.i(LOGTAG, "Interrupted while waiting to launch crash reporter", e);
      // Fall-through
    }
    return true;
  }

  @AnyThread
  /* package */ static boolean launchCrashReporter(
      final Class<?> handlerService, final Context context, final GeckoBundle bundle) {
    return launchCrashReporter(
        handlerService,
        context,
        context.getPackageName(),
        bundle.getString(GeckoRuntime.EXTRA_MINIDUMP_PATH),
        bundle.getString(GeckoRuntime.EXTRA_EXTRAS_PATH),
        bundle.getString(GeckoRuntime.EXTRA_CRASH_PROCESS_VISIBILITY),
        bundle.getString(GeckoRuntime.EXTRA_CRASH_PROCESS_TYPE),
        bundle.getString(GeckoRuntime.EXTRA_CRASH_REMOTE_TYPE));
  }

  /**
   * Report an exception to Socorro.
   *
   * @param thread The exception thread
   * @param exc An exception
   * @return Whether the exception was successfully reported
   */
  @AnyThread
  @SuppressLint("SdCardPath")
  public boolean reportException(@NonNull final Thread thread, @NonNull final Throwable exc) {
    final Context context = getAppContext();
    final String id = UUID.randomUUID().toString();

    // Use the cache directory under the app directory to store crash files.
    final File dir;
    if (context != null) {
      dir = context.getCacheDir();
    } else {
      dir = new File("/data/data/" + getAppPackageName() + "/cache");
    }

    dir.mkdirs();
    if (!dir.exists()) {
      return false;
    }

    final File dmpFile = new File(dir, id + ".dmp");
    final File extraFile = new File(dir, id + ".extra");

    try {
      // Write out minidump file as binary.

      final byte[] minidump = getCrashDump(thread, exc);
      final FileOutputStream dmpStream = new FileOutputStream(dmpFile);
      try {
        dmpStream.write(minidump);
      } finally {
        dmpStream.close();
      }

    } catch (final IOException e) {
      Log.e(LOGTAG, "Error writing minidump file", e);
      return false;
    }

    try {
      // Write out crash extra file as text.

      final Bundle extras = getCrashExtras(thread, exc);
      final String url = getServerUrl(extras);
      extras.putString(CrashReport.Annotation.ServerURL.toString(), url);

      final JSONObject json = new JSONObject();
      for (final String key : extras.keySet()) {
        json.put(key, extras.get(key));
      }

      final BufferedWriter extraWriter = new BufferedWriter(new FileWriter(extraFile));
      try {
        extraWriter.write(json.toString());
      } finally {
        extraWriter.close();
      }
    } catch (final IOException | JSONException e) {
      Log.e(LOGTAG, "Error writing extra file", e);
      return false;
    }

    return launchCrashReporter(dmpFile.getAbsolutePath(), extraFile.getAbsolutePath());
  }

  /**
   * Implements the default behavior for handling uncaught exceptions.
   *
   * @param thread The exception thread
   * @param exc An uncaught exception
   */
  @Override
  public void uncaughtException(@Nullable final Thread thread, @NonNull final Throwable exc) {
    if (this.mCrashing) {
      // Prevent possible infinite recusions.
      return;
    }

    Thread resolvedThread = thread;
    if (resolvedThread == null) {
      // Gecko may pass in null for thread to denote the current thread.
      resolvedThread = Thread.currentThread();
    }

    try {
      Throwable rootException = exc;
      if (!this.mUnregistered) {
        // Only process crash ourselves if we have not been unregistered.

        this.mCrashing = true;
        rootException = getRootException(exc);
        logException(resolvedThread, rootException);

        if (reportException(resolvedThread, rootException)) {
          // Reporting succeeded; we can terminate our process now.
          return;
        }
      }

      if (systemUncaughtHandler != null) {
        // Follow the chain of uncaught handlers.
        systemUncaughtHandler.uncaughtException(resolvedThread, rootException);
      }
    } finally {
      terminateProcess();
    }
  }

  /**
   * Return a default CrashHandler object for all threads and thread groups.
   *
   * @param context application context
   * @return a default CrashHandler object
   */
  @AnyThread
  @NonNull
  public static CrashHandler createDefaultCrashHandler(@NonNull final Context context) {
    return new CrashHandler(context, null) {
      @Override
      public Bundle getCrashExtras(final Thread thread, final Throwable exc) {
        final Bundle extras = super.getCrashExtras(thread, exc);

        extras.putString(
            CrashReport.Annotation.ProductName.toString(), BuildConfig.MOZ_APP_BASENAME);
        extras.putString(CrashReport.Annotation.ProductID.toString(), BuildConfig.MOZ_APP_ID);
        extras.putString(CrashReport.Annotation.Version.toString(), BuildConfig.MOZ_APP_VERSION);
        extras.putString(CrashReport.Annotation.BuildID.toString(), BuildConfig.MOZ_APP_BUILDID);
        extras.putString(CrashReport.Annotation.Vendor.toString(), BuildConfig.MOZ_APP_VENDOR);
        extras.putString(
            CrashReport.Annotation.ReleaseChannel.toString(), BuildConfig.MOZ_UPDATE_CHANNEL);
        return extras;
      }

      @Override
      public boolean reportException(final Thread thread, final Throwable exc) {
        if (BuildConfig.MOZ_CRASHREPORTER && BuildConfig.MOZILLA_OFFICIAL) {
          // Only use Java crash reporter if enabled on official build.
          return super.reportException(thread, exc);
        }
        return false;
      }
    };
  }
}
