/* -*- Mode: Java; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.mozglue;

import android.content.Context;
import android.os.Build;
import android.os.Environment;
import android.util.Log;
import dalvik.system.BaseDexClassLoader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.util.Collection;
import java.util.Locale;
import java.util.Map;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import org.mozilla.gecko.GeckoAppShell;
import org.mozilla.gecko.annotation.JNITarget;
import org.mozilla.gecko.annotation.RobocopTarget;

public final class GeckoLoader {
  private static final String LOGTAG = "GeckoLoader";

  private static File sGREDir;

  /* Synchronized on GeckoLoader.class. */
  private static boolean sSQLiteLibsLoaded;
  private static boolean sNSSLibsLoaded;
  private static boolean sMozGlueLoaded;

  private GeckoLoader() {
    // prevent instantiation
  }

  public static File getGREDir(final Context context) {
    if (sGREDir == null) {
      sGREDir = new File(context.getApplicationInfo().dataDir);
    }
    return sGREDir;
  }

  private static void setupDownloadEnvironment(final Context context) {
    try {
      File downloadDir =
          Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS);
      File updatesDir = context.getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS);
      if (downloadDir == null) {
        downloadDir = new File(Environment.getExternalStorageDirectory().getPath(), "download");
      }
      if (updatesDir == null) {
        updatesDir = downloadDir;
      }
      putenv("DOWNLOADS_DIRECTORY=" + downloadDir.getPath());
      putenv("UPDATES_DIRECTORY=" + updatesDir.getPath());
    } catch (final Exception e) {
      Log.w(LOGTAG, "No download directory found.", e);
    }
  }

  private static void delTree(final File file) {
    if (file.isDirectory()) {
      final File[] children = file.listFiles();
      for (final File child : children) {
        delTree(child);
      }
    }
    file.delete();
  }

  private static File getTmpDir(final Context context) {
    // It's important that this folder is in the cache directory so users can actually
    // clear it when it gets too big.
    return new File(context.getCacheDir(), "gecko_temp");
  }

  private static String escapeDoubleQuotes(final String str) {
    return str.replaceAll("\"", "\\\"");
  }

  private static void setupInitialPrefs(final Map<String, Object> prefs) {
    if (prefs != null) {
      final StringBuilder prefsEnv = new StringBuilder("MOZ_DEFAULT_PREFS=");
      for (final String key : prefs.keySet()) {
        final Object value = prefs.get(key);
        if (value == null) {
          continue;
        }
        prefsEnv.append(String.format("pref(\"%s\",", escapeDoubleQuotes(key)));
        if (value instanceof String) {
          prefsEnv.append(String.format("\"%s\"", escapeDoubleQuotes(value.toString())));
        } else if (value instanceof Boolean) {
          prefsEnv.append((Boolean) value ? "true" : "false");
        } else {
          prefsEnv.append(value.toString());
        }

        prefsEnv.append(");\n");
      }

      putenv(prefsEnv.toString());
    }
  }

  @SuppressWarnings("deprecation") // for Build.CPU_ABI
  public static synchronized void setupGeckoEnvironment(
      final Context context,
      final boolean isChildProcess,
      final String profilePath,
      final Collection<String> env,
      final Map<String, Object> prefs,
      final boolean xpcshell) {
    for (final String e : env) {
      putenv(e);
    }

    putenv("MOZ_ANDROID_PACKAGE_NAME=" + context.getPackageName());

    if (!isChildProcess) {
      setupDownloadEnvironment(context);

      // profile home path
      putenv("HOME=" + profilePath);

      // setup the downloads path
      File f = Environment.getDownloadCacheDirectory();
      putenv("EXTERNAL_STORAGE=" + f.getPath());

      // setup the app-specific cache path
      f = context.getCacheDir();
      putenv("CACHE_DIRECTORY=" + f.getPath());

      f = context.getExternalFilesDir(null);
      if (f != null) {
        putenv("PUBLIC_STORAGE=" + f.getPath());
      }

      final android.os.UserManager um =
          (android.os.UserManager) context.getSystemService(Context.USER_SERVICE);
      if (um != null) {
        putenv(
            "MOZ_ANDROID_USER_SERIAL_NUMBER="
                + um.getSerialNumberForUser(android.os.Process.myUserHandle()));
      } else {
        Log.d(
            LOGTAG,
            "Unable to obtain user manager service on a device with SDK version "
                + Build.VERSION.SDK_INT);
      }

      setupInitialPrefs(prefs);
    }

    // Xpcshell tests set up their own temp directory
    if (!xpcshell) {
      // setup the tmp path
      final File f = getTmpDir(context);
      if (!f.exists()) {
        f.mkdirs();
      }
      putenv("TMPDIR=" + f.getPath());
    }

    putenv("LANG=" + Locale.getDefault().toString());

    final Class<?> crashHandler = GeckoAppShell.getCrashHandlerService();
    if (crashHandler != null) {
      putenv(
          "MOZ_ANDROID_CRASH_HANDLER=" + context.getPackageName() + "/" + crashHandler.getName());
    }

    putenv("MOZ_ANDROID_DEVICE_SDK_VERSION=" + Build.VERSION.SDK_INT);
    putenv("MOZ_ANDROID_CPU_ABI=" + Build.CPU_ABI);

    // env from extras could have reset out linker flags; set them again.
    loadLibsSetupLocked(context);
  }

  // Adapted from
  // https://source.chromium.org/chromium/chromium/src/+/main:base/android/java/src/org/chromium/base/BundleUtils.java;l=196;drc=c0fedddd4a1444653235912cfae3d44b544ded01
  private static String getLibraryPath(final String libraryName) {
    // Due to b/171269960 isolated split class loaders have an empty library path, so check
    // the base module class loader first which loaded GeckoAppShell. If the library is not
    // found there, attempt to construct the correct library path from the split.
    String path =
        ((BaseDexClassLoader) GeckoAppShell.class.getClassLoader()).findLibrary(libraryName);
    if (path != null) {
      return path;
    }

    // SplitCompat is installed on the application context, so check there for library paths
    // which were added to that ClassLoader.
    final ClassLoader classLoader = GeckoAppShell.getApplicationContext().getClassLoader();
    if (classLoader instanceof BaseDexClassLoader) {
      path = ((BaseDexClassLoader) classLoader).findLibrary(libraryName);
      if (path != null) {
        return path;
      }
    }

    throw new RuntimeException("Could not find mozglue path.");
  }

  private static String getLibraryBase() {
    final String mozglue = getLibraryPath("mozglue");
    final int lastSlash = mozglue.lastIndexOf('/');
    if (lastSlash < 0) {
      throw new IllegalStateException("Invalid library path for libmozglue.so: " + mozglue);
    }
    final String base = mozglue.substring(0, lastSlash);
    Log.d(LOGTAG, "Library base=" + base);
    return base;
  }

  private static void loadLibsSetupLocked(final Context context) {
    putenv("GRE_HOME=" + getGREDir(context).getPath());
    putenv("MOZ_ANDROID_LIBDIR=" + getLibraryBase());
  }

  @RobocopTarget
  public static synchronized void loadSQLiteLibs(final Context context) {
    if (sSQLiteLibsLoaded) {
      return;
    }

    loadMozGlue(context);
    loadLibsSetupLocked(context);
    loadSQLiteLibsNative();
    sSQLiteLibsLoaded = true;
  }

  public static synchronized void loadNSSLibs(final Context context) {
    if (sNSSLibsLoaded) {
      return;
    }

    loadMozGlue(context);
    loadLibsSetupLocked(context);
    loadNSSLibsNative();
    sNSSLibsLoaded = true;
  }

  @SuppressWarnings("deprecation")
  private static String getCPUABI() {
    return android.os.Build.CPU_ABI;
  }

  /**
   * Copy a library out of our APK.
   *
   * @param context a Context.
   * @param lib the name of the library; e.g., "mozglue".
   * @param outDir the output directory for the .so. No trailing slash.
   * @return true on success, false on failure.
   */
  private static boolean extractLibrary(
      final Context context, final String lib, final String outDir) {
    final String apkPath = context.getApplicationInfo().sourceDir;

    // Sanity check.
    if (!apkPath.endsWith(".apk")) {
      Log.w(LOGTAG, "sourceDir is not an APK.");
      return false;
    }

    // Try to extract the named library from the APK.
    final File outDirFile = new File(outDir);
    if (!outDirFile.isDirectory()) {
      if (!outDirFile.mkdirs()) {
        Log.e(LOGTAG, "Couldn't create " + outDir);
        return false;
      }
    }

    final String[] abis = Build.SUPPORTED_ABIS;
    for (final String abi : abis) {
      if (tryLoadWithABI(lib, outDir, apkPath, abi)) {
        return true;
      }
    }
    return false;
  }

  private static boolean tryLoadWithABI(
      final String lib, final String outDir, final String apkPath, final String abi) {
    try {
      final ZipFile zipFile = new ZipFile(new File(apkPath));
      try {
        final String libPath = "lib/" + abi + "/lib" + lib + ".so";
        final ZipEntry entry = zipFile.getEntry(libPath);
        if (entry == null) {
          Log.w(LOGTAG, libPath + " not found in APK " + apkPath);
          return false;
        }

        final InputStream in = zipFile.getInputStream(entry);
        try {
          final String outPath = outDir + "/lib" + lib + ".so";
          final FileOutputStream out = new FileOutputStream(outPath);
          final byte[] bytes = new byte[1024];
          int read;

          Log.d(LOGTAG, "Copying " + libPath + " to " + outPath);
          boolean failed = false;
          try {
            while ((read = in.read(bytes, 0, 1024)) != -1) {
              out.write(bytes, 0, read);
            }
          } catch (final Exception e) {
            Log.w(LOGTAG, "Failing library copy.", e);
            failed = true;
          } finally {
            out.close();
          }

          if (failed) {
            // Delete the partial copy so we don't fail to load it.
            // Don't bother to check the return value -- there's nothing
            // we can do about a failure.
            new File(outPath).delete();
          } else {
            // Mark the file as executable. This doesn't seem to be
            // necessary for the loader, but it's the normal state of
            // affairs.
            Log.d(LOGTAG, "Marking " + outPath + " as executable.");
            new File(outPath).setExecutable(true);
          }

          return !failed;
        } finally {
          in.close();
        }
      } finally {
        zipFile.close();
      }
    } catch (final Exception e) {
      Log.e(LOGTAG, "Failed to extract lib from APK.", e);
      return false;
    }
  }

  private static boolean attemptLoad(final String path) {
    try {
      System.loadLibrary(path);
      return true;
    } catch (final Throwable e) {
      Log.wtf(LOGTAG, "Couldn't load " + path + ": " + e);
    }

    return false;
  }

  /**
   * The first two attempts at loading a library: directly, and then using the app library path.
   *
   * <p>Returns null or the cause exception.
   */
  public static Throwable doLoadLibrary(final Context context, final String lib) {
    try {
      // Attempt 1: the way that should work.
      System.loadLibrary(lib);
      return null;
    } catch (final Throwable e) {
      final String libPath = getLibraryPath(lib);
      // Does it even exist?
      if (new File(libPath).exists()) {
        if (attemptLoad(libPath)) {
          // Success!
          return null;
        }
        throw new RuntimeException(
            "Library exists but couldn't load." + "Path: " + libPath + " lib: " + lib, e);
      }
      throw new RuntimeException(
          "Library doesn't exist when it should." + "Path: " + libPath + " lib: " + lib, e);
    }
  }

  public static synchronized void loadMozGlue(final Context context) {
    if (sMozGlueLoaded) {
      return;
    }

    doLoadLibrary(context, "mozglue");
    sMozGlueLoaded = true;
  }

  public static synchronized void loadGeckoLibs(final Context context) {
    loadLibsSetupLocked(context);
    loadGeckoLibsNative();
  }

  @SuppressWarnings("serial")
  public static class AbortException extends Exception {
    public AbortException(final String msg) {
      super(msg);
    }
  }

  @JNITarget
  public static void abort(final String msg) {
    final Thread thread = Thread.currentThread();
    final Thread.UncaughtExceptionHandler uncaughtHandler = thread.getUncaughtExceptionHandler();
    if (uncaughtHandler != null) {
      uncaughtHandler.uncaughtException(thread, new AbortException(msg));
    }
  }

  // These methods are implemented in mozglue/android/nsGeckoUtils.cpp
  private static native void putenv(String map);

  // These are mirrored in mozglue/android/APKOpen.cpp
  public static final int PROCESS_TYPE_MAIN = 0;
  public static final int PROCESS_TYPE_CHILD = 1;
  public static final int PROCESS_TYPE_XPCSHELL = 2;

  // These methods are implemented in mozglue/android/APKOpen.cpp
  public static native void nativeRun(
      String[] args, int[] fds, int processType, String outFilePath);

  private static native void loadGeckoLibsNative();

  private static native void loadSQLiteLibsNative();

  private static native void loadNSSLibsNative();

  public static native void suppressCrashDialog();
}
