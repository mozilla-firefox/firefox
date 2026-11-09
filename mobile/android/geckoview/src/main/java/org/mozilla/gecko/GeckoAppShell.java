/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko;

import android.annotation.SuppressLint;
import android.app.ActivityManager;
import android.app.Service;
import android.content.ComponentCallbacks;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.ActivityInfo;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.content.res.Configuration;
import android.content.res.TypedArray;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.PixelFormat;
import android.graphics.Point;
import android.graphics.Rect;
import android.graphics.drawable.BitmapDrawable;
import android.graphics.drawable.Drawable;
import android.hardware.display.DisplayManager;
import android.location.Criteria;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.media.AudioManager;
import android.net.ConnectivityManager;
import android.net.LinkProperties;
import android.net.Network;
import android.net.NetworkInfo;
import android.os.Build;
import android.os.Bundle;
import android.os.Debug;
import android.os.LocaleList;
import android.os.Looper;
import android.os.PowerManager;
import android.os.Vibrator;
import android.provider.Settings;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.ContextThemeWrapper;
import android.view.Display;
import android.view.Display.HdrCapabilities;
import android.view.InputDevice;
import android.view.ViewConfiguration;
import android.view.WindowManager;
import android.view.inputmethod.InputMethodManager;
import android.view.inputmethod.InputMethodSubtype;
import android.webkit.MimeTypeMap;
import androidx.annotation.Nullable;
import androidx.annotation.RequiresApi;
import androidx.collection.SimpleArrayMap;
import androidx.core.content.res.ResourcesCompat;
import java.net.InetSocketAddress;
import java.net.Proxy;
import java.nio.ByteBuffer;
import java.util.List;
import java.util.StringTokenizer;
import org.jetbrains.annotations.NotNull;
import org.mozilla.gecko.annotation.WrapForJNI;
import org.mozilla.gecko.util.HardwareUtils;
import org.mozilla.gecko.util.InputDeviceUtils;
import org.mozilla.gecko.util.ProxySelector;
import org.mozilla.gecko.util.ThreadUtils;
import org.mozilla.geckoview.BuildConfig;
import org.mozilla.geckoview.CrashHandler;
import org.mozilla.geckoview.R;

public class GeckoAppShell {
  private static final String LOGTAG = "GeckoAppShell";

  // We have static members only.
  private GeckoAppShell() {}

  // Name for app-scoped prefs
  public static final String APP_PREFS_NAME = "GeckoApp";

  private static class GeckoCrashHandler extends CrashHandler {

    public GeckoCrashHandler(final Class<? extends Service> handlerService) {
      super(handlerService);
    }

    @Override
    public String getAppPackageName() {
      final Context appContext = getAppContext();
      if (appContext == null) {
        return "<unknown>";
      }
      return appContext.getPackageName();
    }

    @Override
    public Context getAppContext() {
      return getApplicationContext();
    }

    @SuppressLint("ApplySharedPref")
    @Override
    public boolean reportException(final Thread thread, final Throwable exc) {
      try {
        if (exc instanceof OutOfMemoryError) {
          final SharedPreferences prefs =
              getApplicationContext().getSharedPreferences(APP_PREFS_NAME, 0);
          final SharedPreferences.Editor editor = prefs.edit();
          editor.putBoolean(PREFS_OOM_EXCEPTION, true);

          // Synchronously write to disk so we know it's done before we
          // shutdown
          editor.commit();
        }

        reportJavaCrash(exc, getExceptionStackTrace(exc));

      } catch (final Throwable e) {
      }

      // reportJavaCrash should have caused us to hard crash. If we're still here,
      // it probably means Gecko is not loaded, and we should do something else.
      if (BuildConfig.MOZ_CRASHREPORTER && BuildConfig.MOZILLA_OFFICIAL) {
        // Only use Java crash reporter if enabled on official build.
        return super.reportException(thread, exc);
      }
      return false;
    }
  }

  private static String sAppNotes;
  private static CrashHandler sCrashHandler;

  public static synchronized CrashHandler ensureCrashHandling(
      final Class<? extends Service> handler) {
    if (sCrashHandler == null) {
      sCrashHandler = new GeckoCrashHandler(handler);
    }

    return sCrashHandler;
  }

  private static Class<? extends Service> sCrashHandlerService;

  public static synchronized void setCrashHandlerService(
      final Class<? extends Service> handlerService) {
    sCrashHandlerService = handlerService;
  }

  public static synchronized Class<? extends Service> getCrashHandlerService() {
    return sCrashHandlerService;
  }

  @WrapForJNI(exceptionMode = "ignore")
  public static synchronized String getAppNotes() {
    return sAppNotes;
  }

  public static synchronized void appendAppNotesToCrashReport(final String notes) {
    if (sAppNotes == null) {
      sAppNotes = notes;
    } else {
      sAppNotes += '\n' + notes;
    }
  }

  private static volatile boolean locationHighAccuracyEnabled;
  private static volatile boolean locationListeningRequested = false;
  private static volatile boolean locationPaused = false;

  // See also HardwareUtils.LOW_MEMORY_THRESHOLD_MB.
  private static final int HIGH_MEMORY_DEVICE_THRESHOLD_MB = 768;

  /*
   * Device RAM threshold requirement for adding additional headers.
   * Keep in sync with RAM_THRESHOLD_MEGABYTES defined in
   * https://searchfox.org/mozilla-central/rev/55944eaee1e358b5443eaedc8adcd37e3fd23fd3/mobile/android/fenix/app/src/main/java/org/mozilla/fenix/FenixApplication.kt#120
   */
  private static final int ADDITIONAL_SEARCH_HEADER_RAM_THRESHOLD_MEGABYTES = 1024;

  private static int sScreenDepth;
  private static boolean sUseMaxScreenDepth;
  private static Float sScreenRefreshRate;

  /*
   * Time (in System.nanoTime() units) when the currently-playing vibration
   * is scheduled to end. This value could be zero when the vibration is
   * cancelled. `System.nanoTime() > sVibrationEndTime` means there is not a
   * playing vibration now.
   */
  private static long sVibrationEndTime;

  /*
   * Keep in sync with constants found here:
   * http://searchfox.org/firefox-main/source/uriloader/base/nsIWebProgressListener.idl
   */
  public static final int WPL_STATE_START = 0x00000001;
  public static final int WPL_STATE_STOP = 0x00000010;
  public static final int WPL_STATE_IS_DOCUMENT = 0x00020000;
  public static final int WPL_STATE_IS_NETWORK = 0x00040000;

  /* Keep in sync with constants found here:
    http://searchfox.org/firefox-main/source/netwerk/base/nsINetworkLinkService.idl
  */
  public static final int LINK_TYPE_UNKNOWN = 0;
  public static final int LINK_TYPE_ETHERNET = 1;
  public static final int LINK_TYPE_USB = 2;
  public static final int LINK_TYPE_WIFI = 3;
  public static final int LINK_TYPE_WIMAX = 4;
  public static final int LINK_TYPE_MOBILE = 9;

  public static final String PREFS_OOM_EXCEPTION = "OOMException";

  /* The Android-side API: API methods that Android calls */

  // helper methods
  @WrapForJNI
  /* package */ static native void reportJavaCrash(Throwable exc, String stackTrace);

  private static Rect sScreenSizeOverride;
  private static int sDensityDpiOverride;
  private static Float sDensityOverride;

  @WrapForJNI(stubName = "NotifyObservers", dispatchTo = "gecko")
  private static native void nativeNotifyObservers(String topic, String data);

  @WrapForJNI(stubName = "AppendAppNotesToCrashReport", dispatchTo = "gecko")
  public static native void nativeAppendAppNotesToCrashReport(final String notes);

  public static void notifyObservers(final String topic, final String data) {
    notifyObservers(topic, data, GeckoThread.State.RUNNING);
  }

  public static void notifyObservers(
      final String topic, final String data, final GeckoThread.State state) {
    if (GeckoThread.isStateAtLeast(state)) {
      nativeNotifyObservers(topic, data);
    } else {
      GeckoThread.queueNativeCallUntil(
          state,
          GeckoAppShell.class,
          "nativeNotifyObservers",
          String.class,
          topic,
          String.class,
          data);
    }
  }

  /*
   *  The Gecko-side API: API methods that Gecko calls
   */

  @WrapForJNI(exceptionMode = "ignore")
  private static String getExceptionStackTrace(final Throwable e) {
    return CrashHandler.getExceptionStackTrace(CrashHandler.getRootException(e));
  }

  @WrapForJNI(exceptionMode = "ignore")
  private static synchronized void handleUncaughtException(final Throwable e) {
    if (sCrashHandler != null) {
      sCrashHandler.uncaughtException(null, e);
    }
  }

  private static float getLocationAccuracy(final Location location) {
    final float radius = location.getAccuracy();
    return (location.hasAccuracy() && radius > 0) ? radius : 1001;
  }

  private static Location determineReliableLocation(
      @NotNull final Location locA, @NotNull final Location locB) {
    // The 6 seconds were chosen arbitrarily
    final long closeTime = 6000000000L;
    final boolean isNearSameTime =
        Math.abs((locA.getElapsedRealtimeNanos() - locB.getElapsedRealtimeNanos())) <= closeTime;
    final boolean isAMoreAccurate = getLocationAccuracy(locA) < getLocationAccuracy(locB);
    final boolean isAMoreRecent = locA.getElapsedRealtimeNanos() > locB.getElapsedRealtimeNanos();
    if (isNearSameTime) {
      return isAMoreAccurate ? locA : locB;
    }
    return isAMoreRecent ? locA : locB;
  }

  // Permissions are explicitly checked when requesting content permission.
  @SuppressLint("MissingPermission")
  private static @Nullable Location getLastKnownLocation(final LocationManager lm) {
    Location lastKnownLocation = null;
    final List<String> providers = lm.getAllProviders();

    for (final String provider : providers) {
      final Location location = lm.getLastKnownLocation(provider);
      if (location == null) {
        continue;
      }

      if (lastKnownLocation == null) {
        lastKnownLocation = location;
        continue;
      }
      lastKnownLocation = determineReliableLocation(lastKnownLocation, location);
    }
    return lastKnownLocation;
  }

  // Toggles the location listeners on/off, which will then provide/stop location information
  @WrapForJNI(calledFrom = "gecko")
  private static synchronized boolean enableLocationUpdates(final boolean enable) {
    locationListeningRequested = enable;
    final boolean canListen = updateLocationListeners();
    // canListen will be true even if paused. During paused, we keep requesting status.
    if (!canListen && locationListeningRequested) {
      // Didn't successfully start listener when requested
      locationListeningRequested = false;
    }
    return canListen;
  }

  // Permissions are explicitly checked when requesting content permission.
  @SuppressLint("MissingPermission")
  private static synchronized boolean updateLocationListeners() {
    final boolean shouldListen = locationListeningRequested && !locationPaused;
    final LocationManager lm = getLocationManager(getApplicationContext());
    if (lm == null) {
      return false;
    }

    if (!shouldListen) {
      if (!locationListeningRequested) {
        // We are paused, so stop listening.
        lm.removeUpdates(sAndroidListeners);
      }
      return true;
    }

    if (!lm.isProviderEnabled(LocationManager.GPS_PROVIDER)
        && !lm.isProviderEnabled(LocationManager.NETWORK_PROVIDER)) {
      return false;
    }

    final Location lastKnownLocation = getLastKnownLocation(lm);
    if (lastKnownLocation != null) {
      sAndroidListeners.onLocationChanged(lastKnownLocation);
    }

    final Criteria criteria = new Criteria();
    criteria.setSpeedRequired(false);
    criteria.setBearingRequired(false);
    criteria.setAltitudeRequired(false);
    if (locationHighAccuracyEnabled) {
      criteria.setAccuracy(Criteria.ACCURACY_FINE);
    } else {
      criteria.setAccuracy(Criteria.ACCURACY_COARSE);
    }

    final String provider = lm.getBestProvider(criteria, true);
    if (provider == null) {
      return false;
    }

    final Looper l = Looper.getMainLooper();
    lm.requestLocationUpdates(provider, 100, 0.5f, sAndroidListeners, l);
    return true;
  }

  public static void pauseLocation() {
    locationPaused = true;
    updateLocationListeners();
  }

  public static void resumeLocation() {
    locationPaused = false;
    updateLocationListeners();
  }

  private static LocationManager getLocationManager(final Context context) {
    try {
      return (LocationManager) context.getSystemService(Context.LOCATION_SERVICE);
    } catch (final NoSuchFieldError e) {
      // Some Tegras throw exceptions about missing the CONTROL_LOCATION_UPDATES permission,
      // which allows enabling/disabling location update notifications from the cell radio.
      // CONTROL_LOCATION_UPDATES is not for use by normal applications, but we might be
      // hitting this problem if the Tegras are confused about missing cell radios.
      Log.e(LOGTAG, "LOCATION_SERVICE not found?!", e);
      return null;
    }
  }

  @WrapForJNI(calledFrom = "gecko")
  private static void enableLocationHighAccuracy(final boolean enable) {
    locationHighAccuracyEnabled = enable;
  }

  @WrapForJNI(calledFrom = "any", dispatchTo = "gecko")
  /* package */ static native void onLocationChanged(
      double latitude,
      double longitude,
      double altitude,
      float accuracy,
      float altitudeAccuracy,
      float heading,
      float speed);

  private static class AndroidListeners implements LocationListener {
    // Geolocation.
    @Override
    public void onLocationChanged(final Location location) {
      // No logging here: user-identifying information.

      final double altitude = location.hasAltitude() ? location.getAltitude() : Double.NaN;

      final float accuracy = location.hasAccuracy() ? location.getAccuracy() : Float.NaN;

      final float altitudeAccuracy =
          location.hasVerticalAccuracy() ? location.getVerticalAccuracyMeters() : Float.NaN;

      final float speed = location.hasSpeed() ? location.getSpeed() : Float.NaN;

      final float heading = location.hasBearing() ? location.getBearing() : Float.NaN;

      // nsGeoPositionCoords will convert NaNs to null for optional
      // properties of the JavaScript Coordinates object.
      GeckoAppShell.onLocationChanged(
          location.getLatitude(),
          location.getLongitude(),
          altitude,
          accuracy,
          altitudeAccuracy,
          heading,
          speed);
    }

    @Override
    public void onProviderDisabled(final String provider) {}

    @Override
    public void onProviderEnabled(final String provider) {}

    @Override
    public void onStatusChanged(final String provider, final int status, final Bundle extras) {}
  }

  private static final AndroidListeners sAndroidListeners = new AndroidListeners();

  private static SimpleArrayMap<String, PowerManager.WakeLock> sWakeLocks;

  /** Wake-lock for the CPU. */
  static final String WAKE_LOCK_CPU = "cpu";

  /** Wake-lock for the screen. */
  static final String WAKE_LOCK_SCREEN = "screen";

  /** Wake-lock for the audio-playing, eqaul to LOCK_CPU. */
  static final String WAKE_LOCK_AUDIO_PLAYING = "audio-playing";

  /** Wake-lock for the video-playing, eqaul to LOCK_SCREEN.. */
  static final String WAKE_LOCK_VIDEO_PLAYING = "video-playing";

  static final int WAKE_LOCKS_COUNT = 2;

  /** No one holds the wake-lock. */
  static final int WAKE_LOCK_STATE_UNLOCKED = 0;

  /** The wake-lock is held by a foreground window. */
  static final int WAKE_LOCK_STATE_LOCKED_FOREGROUND = 1;

  /** The wake-lock is held by a background window. */
  static final int WAKE_LOCK_STATE_LOCKED_BACKGROUND = 2;

  // Gecko acquires and releases these locks in separate calls, for as long as it needs them.
  @SuppressLint({"Wakelock", "WakelockTimeout"})
  private static void setWakeLockState(final String lock, final int state) {
    if (sWakeLocks == null) {
      sWakeLocks = new SimpleArrayMap<>(WAKE_LOCKS_COUNT);
    }

    PowerManager.WakeLock wl = sWakeLocks.get(lock);

    // we should still hold the lock for background audio.
    if (WAKE_LOCK_AUDIO_PLAYING.equals(lock) && state == WAKE_LOCK_STATE_LOCKED_BACKGROUND) {
      return;
    }

    if (state == WAKE_LOCK_STATE_LOCKED_FOREGROUND && wl == null) {
      final PowerManager pm =
          (PowerManager) getApplicationContext().getSystemService(Context.POWER_SERVICE);

      if (WAKE_LOCK_CPU.equals(lock) || WAKE_LOCK_AUDIO_PLAYING.equals(lock)) {
        wl = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, lock);
      } else if (WAKE_LOCK_SCREEN.equals(lock) || WAKE_LOCK_VIDEO_PLAYING.equals(lock)) {
        // ON_AFTER_RELEASE is set, the user activity timer will be reset when the
        // WakeLock is released, causing the illumination to remain on a bit longer.
        wl =
            pm.newWakeLock(
                PowerManager.SCREEN_BRIGHT_WAKE_LOCK | PowerManager.ON_AFTER_RELEASE, lock);
      } else {
        Log.w(LOGTAG, "Unsupported wake-lock: " + lock);
        return;
      }

      wl.acquire();
      sWakeLocks.put(lock, wl);
    } else if (state != WAKE_LOCK_STATE_LOCKED_FOREGROUND && wl != null) {
      wl.release();
      sWakeLocks.remove(lock);
    }
  }

  @WrapForJNI(calledFrom = "gecko")
  private static void moveTaskToBack() {
    final Context applicationContext = getApplicationContext();
    final Intent intent = new Intent(Intent.ACTION_MAIN);
    intent.addCategory(Intent.CATEGORY_HOME);
    intent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
    applicationContext.startActivity(intent);
  }

  @WrapForJNI(calledFrom = "gecko")
  public static String getExtensionFromMimeType(final String aMimeType) {
    return MimeTypeMap.getSingleton().getExtensionFromMimeType(aMimeType);
  }

  @WrapForJNI(calledFrom = "gecko")
  public static String getMimeTypeFromExtensions(final String aFileExt) {
    final StringTokenizer st = new StringTokenizer(aFileExt, ".,; ");
    String type = null;
    String subType = null;
    while (st.hasMoreElements()) {
      final String ext = st.nextToken();
      final String mt = getMimeTypeFromExtension(ext);
      if (mt == null) continue;
      final int slash = mt.indexOf('/');
      final String tmpType = mt.substring(0, slash);
      if (!tmpType.equalsIgnoreCase(type)) type = type == null ? tmpType : "*";
      final String tmpSubType = mt.substring(slash + 1);
      if (!tmpSubType.equalsIgnoreCase(subType)) subType = subType == null ? tmpSubType : "*";
    }
    if (type == null) type = "*";
    if (subType == null) subType = "*";
    return type + "/" + subType;
  }

  @WrapForJNI(dispatchTo = "gecko")
  private static native void notifyAlertListener(
      String name, String topic, String action, String origin);

  /**
   * Called by the NotificationListener to notify Gecko that a notification has shown.
   *
   * @param name the notification name
   * @param cookie the notification cookie
   * @param origin the origin that created the notification
   */
  public static void onNotificationShow(
      final String name, final String cookie, @NotNull final String origin) {
    if (GeckoThread.isRunning()) {
      notifyAlertListener(name, "alertshow", cookie, origin);
    }
  }

  /**
   * Called by the NotificationListener to notify Gecko that a previously shown notification has
   * been closed.
   *
   * @param name the notification name
   * @param origin the origin that created the notification
   */
  public static void onNotificationClose(@NotNull final String name, @NotNull final String origin) {
    if (GeckoThread.isRunning()) {
      notifyAlertListener(name, "alertfinished", null, origin);
    }
  }

  /**
   * Called by the NotificationListener to notify Gecko that a previously shown notification has
   * been clicked on.
   *
   * @param name the notification name
   * @param action the action that was clicked, or null for the notification itself
   * @param origin the origin that created the notification
   */
  public static void onNotificationClick(
      @NotNull final String name, @Nullable final String action, @NotNull final String origin) {
    if (GeckoThread.isRunning()) {
      notifyAlertListener(name, "alertclickcallback", action, origin);
    } else {
      GeckoThread.queueNativeCallUntil(
          GeckoThread.State.PROFILE_READY,
          GeckoAppShell.class,
          "notifyAlertListener",
          name,
          "alertclickcallback",
          String.class,
          action,
          origin);
    }
  }

  public static synchronized void setDisplayDpiOverride(@Nullable final Integer dpi) {
    if (dpi == null) {
      return;
    }
    if (sDensityDpiOverride != 0) {
      Log.e(LOGTAG, "Tried to override screen DPI after it's already been set");
      return;
    }
    sDensityDpiOverride = dpi;
  }

  @WrapForJNI(calledFrom = "gecko")
  private static synchronized int getDpi() {
    if (sDensityDpiOverride != 0) {
      return sDensityDpiOverride;
    }
    return sScreenCompat.getDensityDpi(sDisplayId);
  }

  public static synchronized void setDisplayDensityOverride(@Nullable final Float density) {
    if (density == null) {
      return;
    }
    if (sDensityOverride != null) {
      Log.e(LOGTAG, "Tried to override screen density after it's already been set");
      return;
    }
    sDensityOverride = density;
  }

  @WrapForJNI(calledFrom = "gecko")
  private static synchronized float getDensity() {
    if (sDensityOverride != null) {
      return sDensityOverride;
    }

    return sScreenCompat.getDensity(sDisplayId);
  }

  private static int sTotalRam;

  private static int getTotalRam(final Context context) {
    if (sTotalRam == 0) {
      final ActivityManager.MemoryInfo memInfo = new ActivityManager.MemoryInfo();
      final ActivityManager am =
          (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
      am.getMemoryInfo(memInfo); // `getMemoryInfo()` returns a value in B. Convert to MB.
      sTotalRam = (int) (memInfo.totalMem / (1024 * 1024));
      Log.d(LOGTAG, "System memory: " + sTotalRam + "MB.");
    }

    return sTotalRam;
  }

  @WrapForJNI(calledFrom = "gecko")
  private static synchronized boolean isDeviceRamThresholdOkay() {
    final Context applicationContext = getApplicationContext();
    return getTotalRam(applicationContext) > ADDITIONAL_SEARCH_HEADER_RAM_THRESHOLD_MEGABYTES;
  }

  private static boolean isHighMemoryDevice(final Context context) {
    return getTotalRam(context) > HIGH_MEMORY_DEVICE_THRESHOLD_MB;
  }

  public static synchronized void useMaxScreenDepth(final boolean enable) {
    sUseMaxScreenDepth = enable;
  }

  /**
   * Returns the colour depth of the default screen. This will typically be 24 as by default it only
   * includes the bits used for color values, not alpha. However, if useMaxScreenDepth() has been
   * enabled this will return the full depth.
   */
  @WrapForJNI(calledFrom = "gecko")
  public static synchronized int getScreenDepth() {
    if (sScreenDepth == 0) {
      sScreenDepth = 24;
      final Context applicationContext = getApplicationContext();
      final PixelFormat info = new PixelFormat();
      PixelFormat.getPixelFormatInfo(sScreenCompat.getDisplay(sDisplayId).getPixelFormat(), info);
      if (info.bitsPerPixel >= 24) {
        sScreenDepth = sUseMaxScreenDepth ? info.bitsPerPixel : 24;
      }
    }

    return sScreenDepth;
  }

  @WrapForJNI(calledFrom = "gecko")
  private static synchronized float getScreenRefreshRate() {
    if (sScreenRefreshRate != null) {
      return sScreenRefreshRate;
    }

    final float refreshRate = sScreenCompat.getDisplay(sDisplayId).getRefreshRate();
    // Android 11+ supports multiple refresh rate. So we have to get refresh rate per call.
    // https://source.android.com/docs/core/graphics/multiple-refresh-rate
    if (Build.VERSION.SDK_INT < 30) {
      // Until Android 10, refresh rate is fixed, so we can cache it.
      sScreenRefreshRate = Float.valueOf(refreshRate);
    }
    return refreshRate;
  }

  @WrapForJNI(calledFrom = "gecko")
  private static boolean hasHDRScreen() {
    final Display display = sScreenCompat.getDisplay(sDisplayId);
    return display != null && display.isHdr();
  }

  @WrapForJNI(calledFrom = "gecko")
  private static float getSDRContentBrightness() {
    // We need API level 34 (Android 14) for getHdrCapabilities and thus
    // getHdrSdrRatio.
    if (Build.VERSION.SDK_INT < 34) {
      return 80.0f;
    }
    final Display display = sScreenCompat.getDisplay(sDisplayId);
    if (display != null) {
      final HdrCapabilities hdrCapabilities = display.getHdrCapabilities();
      if (hdrCapabilities != null) {
        return hdrCapabilities.getDesiredMaxLuminance() / display.getHdrSdrRatio();
      }
    }
    return 80.0f;
  }

  @WrapForJNI(calledFrom = "gecko")
  private static float getHDRPeakBrightness() {
    final Display display = sScreenCompat.getDisplay(sDisplayId);
    if (display != null) {
      final HdrCapabilities hdrCapabilities = display.getHdrCapabilities();
      if (hdrCapabilities != null) {
        return hdrCapabilities.getDesiredMaxLuminance();
      }
    }
    return 80.0f;
  }

  private static Vibrator vibrator() {
    return (Vibrator) getApplicationContext().getSystemService(Context.VIBRATOR_SERVICE);
  }

  @SuppressLint("MissingPermission")
  @WrapForJNI(calledFrom = "gecko")
  private static void vibrate(final long milliseconds) {
    sVibrationEndTime = System.nanoTime() + milliseconds * 1000000;
    try {
      vibrator().vibrate(milliseconds);
    } catch (final SecurityException ignore) {
      Log.w(LOGTAG, "No VIBRATE permission");
    }
  }

  @SuppressLint("MissingPermission")
  @WrapForJNI(calledFrom = "gecko")
  private static void vibrate(final long[] pattern, final int repeat) {
    // If pattern.length is odd, the last element in the pattern is a
    // meaningless delay, so don't include it in vibrationDuration.
    long vibrationDuration = 0;
    final int iterLen = pattern.length & ~1;
    for (int i = 0; i < iterLen; i++) {
      vibrationDuration += pattern[i];
    }

    sVibrationEndTime = System.nanoTime() + vibrationDuration * 1000000;
    try {
      vibrator().vibrate(pattern, repeat);
    } catch (final SecurityException ignore) {
      Log.w(LOGTAG, "No VIBRATE permission");
    }
  }

  @SuppressLint("MissingPermission")
  @WrapForJNI(calledFrom = "gecko")
  private static void cancelVibrate() {
    sVibrationEndTime = 0;
    try {
      vibrator().cancel();
    } catch (final SecurityException ignore) {
      Log.w(LOGTAG, "No VIBRATE permission");
    }
  }

  private static ConnectivityManager sConnectivityManager;

  private static void ensureConnectivityManager() {
    if (sConnectivityManager == null) {
      sConnectivityManager =
          (ConnectivityManager)
              getApplicationContext().getSystemService(Context.CONNECTIVITY_SERVICE);
    }
  }

  @WrapForJNI(calledFrom = "gecko")
  private static boolean isNetworkLinkUp() {
    ensureConnectivityManager();
    try {
      final NetworkInfo info = sConnectivityManager.getActiveNetworkInfo();
      if (info == null || !info.isConnected()) return false;
    } catch (final SecurityException se) {
      return false;
    }
    return true;
  }

  @WrapForJNI(calledFrom = "gecko")
  private static boolean isNetworkLinkKnown() {
    ensureConnectivityManager();
    try {
      if (sConnectivityManager.getActiveNetworkInfo() == null) return false;
    } catch (final SecurityException se) {
      return false;
    }
    return true;
  }

  @WrapForJNI(calledFrom = "gecko")
  private static int getNetworkLinkType() {
    ensureConnectivityManager();
    final NetworkInfo info = sConnectivityManager.getActiveNetworkInfo();
    if (info == null) {
      return LINK_TYPE_UNKNOWN;
    }

    switch (info.getType()) {
      case ConnectivityManager.TYPE_ETHERNET:
        return LINK_TYPE_ETHERNET;
      case ConnectivityManager.TYPE_WIFI:
        return LINK_TYPE_WIFI;
      case ConnectivityManager.TYPE_WIMAX:
        return LINK_TYPE_WIMAX;
      case ConnectivityManager.TYPE_MOBILE:
        return LINK_TYPE_MOBILE;
      default:
        Log.w(LOGTAG, "Ignoring the current network type.");
        return LINK_TYPE_UNKNOWN;
    }
  }

  @WrapForJNI(calledFrom = "gecko", exceptionMode = "nsresult")
  private static String getDNSDomains() {
    ensureConnectivityManager();
    final Network net = sConnectivityManager.getActiveNetwork();
    if (net == null) {
      return "";
    }

    final LinkProperties lp = sConnectivityManager.getLinkProperties(net);
    if (lp == null) {
      return "";
    }

    return lp.getDomains();
  }

  @SuppressLint("ResourceType")
  @WrapForJNI(calledFrom = "gecko")
  private static int[] getSystemColors() {
    // attrsAppearance[] must correspond to AndroidSystemColors structure in android/nsLookAndFeel.h
    final int[] attrsAppearance = {
      android.R.attr.textColorPrimary,
      android.R.attr.textColorPrimaryInverse,
      android.R.attr.textColorSecondary,
      android.R.attr.textColorSecondaryInverse,
      android.R.attr.textColorTertiary,
      android.R.attr.textColorTertiaryInverse,
      android.R.attr.textColorHighlight,
      android.R.attr.colorForeground,
      android.R.attr.colorBackground,
      android.R.attr.panelColorForeground,
      android.R.attr.panelColorBackground,
      android.R.attr.colorAccent,
    };

    final int[] result = new int[attrsAppearance.length];

    final ContextThemeWrapper contextThemeWrapper =
        new ContextThemeWrapper(getApplicationContext(), android.R.style.TextAppearance);

    final TypedArray appearance = contextThemeWrapper.obtainStyledAttributes(attrsAppearance);

    if (appearance != null) {
      for (int i = 0; i < appearance.getIndexCount(); i++) {
        final int idx = appearance.getIndex(i);
        final int color = appearance.getColor(idx, 0);
        result[idx] = color;
      }
      appearance.recycle();
    }

    return result;
  }

  @WrapForJNI(calledFrom = "gecko")
  private static String getKeyboardLayout() {
    final InputMethodManager imm =
        (InputMethodManager) getApplicationContext().getSystemService(Context.INPUT_METHOD_SERVICE);
    final InputMethodSubtype ims = imm.getCurrentInputMethodSubtype();
    if (ims == null) {
      return null;
    }

    // TODO(m_kato):
    // Android 16 will have `layout related APIs such as setLayoutLabelNonLocalized
    // to get keyboard layout label.
    return ims.getLanguageTag();
  }

  @WrapForJNI(calledFrom = "gecko")
  private static byte[] getIconForExtension(final String aExt, final int iconSize) {
    try {
      int resolvedIconSize = iconSize;
      if (iconSize <= 0) {
        resolvedIconSize = 16;
      }

      String resolvedExt = aExt;
      if (aExt != null && aExt.length() > 1 && aExt.charAt(0) == '.') {
        resolvedExt = aExt.substring(1);
      }

      final PackageManager pm = getApplicationContext().getPackageManager();
      Drawable icon = getDrawableForExtension(pm, resolvedExt);
      if (icon == null) {
        // Use a generic icon.
        icon =
            ResourcesCompat.getDrawable(
                getApplicationContext().getResources(),
                R.drawable.ic_generic_file,
                getApplicationContext().getTheme());
      }

      Bitmap bitmap = getBitmapFromDrawable(icon);
      if (bitmap.getWidth() != resolvedIconSize || bitmap.getHeight() != resolvedIconSize) {
        bitmap = Bitmap.createScaledBitmap(bitmap, resolvedIconSize, resolvedIconSize, true);
      }

      final ByteBuffer buf = ByteBuffer.allocate(resolvedIconSize * resolvedIconSize * 4);
      bitmap.copyPixelsToBuffer(buf);

      return buf.array();
    } catch (final Exception e) {
      Log.w(LOGTAG, "getIconForExtension failed.", e);
      return null;
    }
  }

  private static Bitmap getBitmapFromDrawable(final Drawable drawable) {
    if (drawable instanceof BitmapDrawable) {
      return ((BitmapDrawable) drawable).getBitmap();
    }

    int width = drawable.getIntrinsicWidth();
    width = width > 0 ? width : 1;
    int height = drawable.getIntrinsicHeight();
    height = height > 0 ? height : 1;

    final Bitmap bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
    final Canvas canvas = new Canvas(bitmap);
    drawable.setBounds(0, 0, canvas.getWidth(), canvas.getHeight());
    drawable.draw(canvas);

    return bitmap;
  }

  public static String getMimeTypeFromExtension(final String ext) {
    final MimeTypeMap mtm = MimeTypeMap.getSingleton();
    return mtm.getMimeTypeFromExtension(ext);
  }

  private static Drawable getDrawableForExtension(final PackageManager pm, final String aExt) {
    final Intent intent = new Intent(Intent.ACTION_VIEW);
    final String mimeType = getMimeTypeFromExtension(aExt);
    if (mimeType != null && mimeType.length() > 0) intent.setType(mimeType);
    else return null;

    final List<ResolveInfo> list = pm.queryIntentActivities(intent, 0);
    if (list.size() == 0) return null;

    final ResolveInfo resolveInfo = list.get(0);

    if (resolveInfo == null) return null;

    final ActivityInfo activityInfo = resolveInfo.activityInfo;

    return activityInfo.loadIcon(pm);
  }

  @WrapForJNI(calledFrom = "gecko")
  private static boolean getShowPasswordSetting() {
    try {
      final int showPassword =
          Settings.System.getInt(
              getApplicationContext().getContentResolver(), Settings.System.TEXT_SHOW_PASSWORD, 1);
      return (showPassword > 0);
    } catch (final Exception e) {
      return true;
    }
  }

  private static Context sApplicationContext;
  private static ViewConfiguration sViewConfiguration;
  private static Boolean sIs24HourFormat = true;

  @WrapForJNI
  public static Context getApplicationContext() {
    return sApplicationContext;
  }

  public static void setApplicationContext(final Context context) {
    sApplicationContext = context;
  }

  /*
   * Battery API related methods.
   */
  @WrapForJNI(calledFrom = "gecko")
  private static void enableBatteryNotifications() {
    GeckoBatteryManager.enableNotifications();
  }

  @WrapForJNI(calledFrom = "gecko")
  private static void disableBatteryNotifications() {
    GeckoBatteryManager.disableNotifications();
  }

  @WrapForJNI(calledFrom = "gecko")
  private static double[] getCurrentBatteryInformation() {
    return GeckoBatteryManager.getCurrentInformation(getApplicationContext());
  }

  /* Called by JNI from AndroidBridge, and by reflection from tests/BaseTest.java.in */
  @WrapForJNI(calledFrom = "gecko")
  public static boolean isTablet() {
    return HardwareUtils.isTablet(getApplicationContext());
  }

  @WrapForJNI(calledFrom = "gecko")
  private static double[] getCurrentNetworkInformation() {
    return GeckoNetworkManager.getInstance().getCurrentInformation();
  }

  @WrapForJNI(calledFrom = "gecko")
  private static void enableNetworkNotifications() {
    ThreadUtils.runOnUiThread(() -> GeckoNetworkManager.getInstance().enableNotifications());
  }

  @WrapForJNI(calledFrom = "gecko")
  private static void disableNetworkNotifications() {
    ThreadUtils.runOnUiThread(
        new Runnable() {
          @Override
          public void run() {
            GeckoNetworkManager.getInstance().disableNotifications();
          }
        });
  }

  @WrapForJNI(calledFrom = "gecko")
  private static short getScreenOrientation() {
    return GeckoScreenOrientation.getInstance().getScreenOrientation().value;
  }

  /* package */ static int getRotation() {
    return sScreenCompat.getRotation(sDisplayId);
  }

  @WrapForJNI(calledFrom = "gecko")
  private static int getScreenAngle() {
    return GeckoScreenOrientation.getInstance().getAngle();
  }

  @WrapForJNI(calledFrom = "gecko")
  private static void notifyWakeLockChanged(final String topic, final String state) {
    final int intState;
    if ("unlocked".equals(state)) {
      intState = WAKE_LOCK_STATE_UNLOCKED;
    } else if ("locked-foreground".equals(state)) {
      intState = WAKE_LOCK_STATE_LOCKED_FOREGROUND;
    } else if ("locked-background".equals(state)) {
      intState = WAKE_LOCK_STATE_LOCKED_BACKGROUND;
    } else {
      throw new IllegalArgumentException();
    }
    setWakeLockState(topic, intState);
  }

  @WrapForJNI(calledFrom = "gecko")
  private static String getProxyForURI(
      final String spec, final String scheme, final String host, final int port) {
    final ProxySelector ps = new ProxySelector();

    final Proxy proxy = ps.select(scheme, host);
    if (Proxy.NO_PROXY.equals(proxy)) {
      return "DIRECT";
    }

    final InetSocketAddress proxyAddress = (InetSocketAddress) proxy.address();
    final String proxyString = proxyAddress.getHostString() + ":" + proxyAddress.getPort();

    switch (proxy.type()) {
      case HTTP:
        return "PROXY " + proxyString;
      case SOCKS:
        return "SOCKS " + proxyString;
    }

    return "DIRECT";
  }

  @WrapForJNI(calledFrom = "gecko")
  private static int getMaxTouchPoints() {
    final PackageManager pm = getApplicationContext().getPackageManager();
    if (pm.hasSystemFeature(PackageManager.FEATURE_TOUCHSCREEN_MULTITOUCH_JAZZHAND)) {
      // at least, 5+ fingers.
      return 5;
    } else if (pm.hasSystemFeature(PackageManager.FEATURE_TOUCHSCREEN_MULTITOUCH_DISTINCT)) {
      // at least, 2+ fingers.
      return 2;
    } else if (pm.hasSystemFeature(PackageManager.FEATURE_TOUCHSCREEN_MULTITOUCH)) {
      // 2 fingers
      return 2;
    } else if (pm.hasSystemFeature(PackageManager.FEATURE_TOUCHSCREEN)) {
      // 1 finger
      return 1;
    }
    return 0;
  }

  /*
   * Keep in sync with PointerCapabilities in ServoTypes.h
   */
  private static final int NO_POINTER = 0x00000000;
  private static final int COARSE_POINTER = 0x00000001;
  private static final int FINE_POINTER = 0x00000002;
  private static final int HOVER_CAPABLE_POINTER = 0x00000004;

  private static int getPointerCapabilities(final InputDevice inputDevice) {
    int result = NO_POINTER;
    final int sources = inputDevice.getSources();

    // Blink checks fine pointer at first, then it check coarse pointer.
    // So, we should use same order for compatibility.
    // Also, if using Chrome OS, source may be SOURCE_MOUSE | SOURCE_TOUCHSCREEN | SOURCE_STYLUS
    // even if no touch screen. So we shouldn't check TOUCHSCREEN at first.

    if (hasInputDeviceSource(sources, InputDevice.SOURCE_MOUSE)
        || hasInputDeviceSource(sources, InputDevice.SOURCE_STYLUS)
        || hasInputDeviceSource(sources, InputDevice.SOURCE_TOUCHPAD)
        || hasInputDeviceSource(sources, InputDevice.SOURCE_TRACKBALL)) {
      result |= FINE_POINTER;
    } else if (hasInputDeviceSource(sources, InputDevice.SOURCE_TOUCHSCREEN)
        || hasInputDeviceSource(sources, InputDevice.SOURCE_JOYSTICK)) {
      result |= COARSE_POINTER;
    }

    if (hasInputDeviceSource(sources, InputDevice.SOURCE_MOUSE)
        || hasInputDeviceSource(sources, InputDevice.SOURCE_TOUCHPAD)
        || hasInputDeviceSource(sources, InputDevice.SOURCE_TRACKBALL)
        || hasInputDeviceSource(sources, InputDevice.SOURCE_JOYSTICK)) {
      result |= HOVER_CAPABLE_POINTER;
    }

    return result;
  }

  // For any-pointer and any-hover media queries features.
  /* package */ static int getAllPointerCapabilities() {
    int result = NO_POINTER;

    for (final int deviceId : InputDevice.getDeviceIds()) {
      final InputDevice inputDevice = InputDevice.getDevice(deviceId);
      if (inputDevice == null || !InputDeviceUtils.isPointerTypeDevice(inputDevice)) {
        continue;
      }

      result |= getPointerCapabilities(inputDevice);
    }

    return result;
  }

  /*
   * Keep in sync with PointingDevices in LookAndFeel.h
   */
  private static final int POINTING_DEVICE_NONE = 0x00000000;
  private static final int POINTING_DEVICE_MOUSE = 0x00000001;
  private static final int POINTING_DEVICE_TOUCH = 0x00000002;
  private static final int POINTING_DEVICE_PEN = 0x00000004;

  private static int getPointingDeviceKinds(final InputDevice inputDevice) {
    int result = POINTING_DEVICE_NONE;
    final int sources = inputDevice.getSources();

    // TODO(krosylight): For now this code is for telemetry purpose, but ultimately we want to
    // replace the capabilities code above and move the capabilities computation into layout. We'll
    // then have to add all the extra devices too that are not mouse/touch/pen. (Bug 1918207)
    // We don't treat other devices properly for pointerType after all:
    // https://searchfox.org/mozilla-central/rev/3b59c739df66574d94022a684596845cd05e7c65/mobile/android/geckoview/src/main/java/org/mozilla/geckoview/PanZoomController.java#749-761

    if (hasInputDeviceSource(sources, InputDevice.SOURCE_MOUSE)) {
      result |= POINTING_DEVICE_MOUSE;
    }
    if (hasInputDeviceSource(sources, InputDevice.SOURCE_TOUCHSCREEN)) {
      result |= POINTING_DEVICE_TOUCH;
    }
    if (hasInputDeviceSource(sources, InputDevice.SOURCE_STYLUS)) {
      result |= POINTING_DEVICE_PEN;
    }
    if (hasInputDeviceSource(sources, InputDevice.SOURCE_BLUETOOTH_STYLUS)) {
      result |= POINTING_DEVICE_PEN;
    }

    return result;
  }

  // For pointing devices telemetry.
  /* package */ static int getPointingDeviceKinds() {
    int result = POINTING_DEVICE_NONE;

    for (final int deviceId : InputDevice.getDeviceIds()) {
      final InputDevice inputDevice = InputDevice.getDevice(deviceId);
      if (inputDevice == null || !InputDeviceUtils.isPointerTypeDevice(inputDevice)) {
        continue;
      }

      result |= getPointingDeviceKinds(inputDevice);
    }

    return result;
  }

  private static boolean hasInputDeviceSource(final int sources, final int inputDeviceSource) {
    return (sources & inputDeviceSource) == inputDeviceSource;
  }

  public static synchronized void setScreenSizeOverride(final Rect size) {
    sScreenSizeOverride = size;
  }

  static final ScreenCompat sScreenCompat;

  private interface ScreenCompat {
    Rect getScreenSize(int displayId);

    int getRotation(int displayId);

    int getDensityDpi(int displayId);

    float getDensity(int displayId);

    Display getDisplay(int displayId);

    void onDisplayRemoved(int displayId);
  }

  private static class JellyBeanMR1ScreenCompat implements ScreenCompat {
    private int mDensityDpi = 0;
    private Float mDensity = null;

    private static DisplayMetrics getDisplayMetrics() {
      return getApplicationContext().getResources().getDisplayMetrics();
    }

    @Override
    public Rect getScreenSize(final int displayId) {
      final Display disp = getDisplay(displayId);
      final Point size = new Point();
      disp.getRealSize(size);
      return new Rect(0, 0, size.x, size.y);
    }

    @Override
    public int getRotation(final int displayId) {
      return getDisplay(displayId).getRotation();
    }

    @Override
    public int getDensityDpi(final int displayId) {
      if (mDensityDpi == 0) {
        mDensityDpi = getDisplayMetrics().densityDpi;
      }
      return mDensityDpi;
    }

    @Override
    public float getDensity(final int displayId) {
      if (mDensity == null) {
        mDensity = getDisplayMetrics().density;
      }
      return mDensity;
    }

    @Override
    public Display getDisplay(final int displayId) {
      final DisplayManager displayManager =
          (DisplayManager) getApplicationContext().getSystemService(Context.DISPLAY_SERVICE);
      final Display display = displayManager.getDisplay(displayId);
      if (display != null) {
        return display;
      }
      if (displayId != Display.DEFAULT_DISPLAY) {
        return getDisplay(Display.DEFAULT_DISPLAY);
      }
      // No display found even if default display. This should not happen.
      throw new RuntimeException("No default display found");
    }

    @Override
    public void onDisplayRemoved(final int displayId) {}
  }

  @RequiresApi(Build.VERSION_CODES.S)
  private static class AndroidSScreenCompat implements ScreenCompat {
    private final SimpleArrayMap<Integer, Context> mWindowContextMap = new SimpleArrayMap<>();

    private final ComponentCallbacks mComponentCallbacks =
        new ComponentCallbacks() {
          @Override
          public void onConfigurationChanged(final Configuration newConfig) {
            if (GeckoScreenOrientation.getInstance().update()) {
              // refreshScreenInfo is already called.
              return;
            }
            ScreenManagerHelper.refreshScreenInfo();
          }

          @Override
          public void onLowMemory() {}
        };

    private synchronized Context getWindowContext(final int displayId) {
      Context windowContext = mWindowContextMap.get(displayId);
      if (windowContext != null) {
        return windowContext;
      }

      final DisplayManager displayManager =
          (DisplayManager) getApplicationContext().getSystemService(Context.DISPLAY_SERVICE);
      final Display display = displayManager.getDisplay(displayId);
      if (display == null) {
        if (displayId != Display.DEFAULT_DISPLAY) {
          return getWindowContext(Display.DEFAULT_DISPLAY);
        }
        // No display found even if default display. This should not happen.
        throw new RuntimeException("No default display found");
      }
      windowContext =
          getApplicationContext()
              .createWindowContext(display, WindowManager.LayoutParams.TYPE_APPLICATION, null);
      windowContext.registerComponentCallbacks(mComponentCallbacks);
      mWindowContextMap.put(displayId, windowContext);

      return windowContext;
    }

    private DisplayMetrics getDisplayMetrics(final int displayId) {
      return getWindowContext(displayId).getResources().getDisplayMetrics();
    }

    @Override
    public Rect getScreenSize(final int displayId) {
      final WindowManager windowManager =
          getWindowContext(displayId).getSystemService(WindowManager.class);
      return windowManager.getCurrentWindowMetrics().getBounds();
    }

    @Override
    public int getRotation(final int displayId) {
      return getDisplay(displayId).getRotation();
    }

    @Override
    public int getDensityDpi(final int displayId) {
      return getDisplayMetrics(displayId).densityDpi;
    }

    @Override
    public float getDensity(final int displayId) {
      return getDisplayMetrics(displayId).density;
    }

    @Override
    public Display getDisplay(final int displayId) {
      final Display display = getWindowContext(displayId).getDisplay();
      if (display != null) {
        return display;
      }
      if (displayId != Display.DEFAULT_DISPLAY) {
        return getWindowContext(Display.DEFAULT_DISPLAY).getDisplay();
      }
      // No display found even if default display. This should not happen.
      throw new RuntimeException("No default display found");
    }

    @Override
    public synchronized void onDisplayRemoved(final int displayId) {
      final Context context = mWindowContextMap.remove(displayId);
      if (context != null) {
        context.unregisterComponentCallbacks(mComponentCallbacks);
      }
    }
  }

  static {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
      sScreenCompat = new AndroidSScreenCompat();
    } else {
      sScreenCompat = new JellyBeanMR1ScreenCompat();
    }
  }

  /** The display id that is associated with the GeckoView object. */
  private static volatile int sDisplayId = Display.DEFAULT_DISPLAY;

  /** Initialize screen information. This should be called at startup */
  public static void maybeInitScreen() {
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S) {
      return;
    }

    ThreadUtils.postToBackgroundThread(
        new Runnable() {
          @Override
          public void run() {
            // Since creating window context is expensive, we initialize it at startup.
            sScreenCompat.getDisplay(sDisplayId);
          }
        });
  }

  /* package */ static Rect getScreenSizeIgnoreOverride() {
    return sScreenCompat.getScreenSize(sDisplayId);
  }

  @WrapForJNI(calledFrom = "gecko")
  private static synchronized Rect getScreenSize() {
    if (sScreenSizeOverride != null) {
      return sScreenSizeOverride;
    }

    return getScreenSizeIgnoreOverride();
  }

  /* package */ static void onDisplayRemoved(final int displayId) {
    sScreenCompat.onDisplayRemoved(displayId);
  }

  /* package */ static int getDisplayId() {
    return sDisplayId;
  }

  /**
   * Set the display id that is associated with the GeckoView object.
   *
   * @param displayId The display id.
   */
  public static void setDisplayId(final int displayId) {
    final boolean isDisplayIdChanged = sDisplayId != displayId;
    sDisplayId = displayId;

    if (isDisplayIdChanged) {
      if (GeckoScreenOrientation.getInstance().update()) {
        // refreshScreenInfo is already called.
        return;
      }
      ScreenManagerHelper.refreshScreenInfo();
    }
  }

  @WrapForJNI
  public static String getPackageResourcePath() {
    return getApplicationContext().getPackageResourcePath();
  }

  @WrapForJNI(calledFrom = "any")
  public static void setCommunicationAudioModeOn(final boolean on) {
    final AudioManager am =
        (AudioManager) getApplicationContext().getSystemService(Context.AUDIO_SERVICE);
    if (am == null) {
      return;
    }

    try {
      if (on) {
        Log.e(LOGTAG, "Setting communication mode ON");
        // This shouldn't throw, but does throw NullPointerException on a very
        // small number of devices.
        am.startBluetoothSco();
        am.setBluetoothScoOn(true);
      } else {
        Log.e(LOGTAG, "Setting communication mode OFF");
        am.stopBluetoothSco();
        am.setBluetoothScoOn(false);
      }
    } catch (final SecurityException | NullPointerException e) {
      Log.e(LOGTAG, "could not set communication mode", e);
    }
  }

  @WrapForJNI
  public static String[] getDefaultLocales() {
    final LocaleList list = LocaleList.getDefault();
    final int n = list.size();
    final String[] locales = new String[n];
    for (int i = 0; i < n; i++) {
      locales[i] = list.get(i).toLanguageTag();
    }
    return locales;
  }

  public static void setIs24HourFormat(final Boolean is24HourFormat) {
    sIs24HourFormat = is24HourFormat;
  }

  @WrapForJNI
  public static boolean getIs24HourFormat() {
    return sIs24HourFormat;
  }

  @WrapForJNI
  private static int getTextCursorBlinkIntervalMillis() {
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.BAKLAVA
        || Build.VERSION.SDK_INT_FULL < Build.VERSION_CODES_FULL.BAKLAVA_1) {
      return 500;
    }
    if (sViewConfiguration == null) {
      sViewConfiguration = ViewConfiguration.get(getApplicationContext());
    }
    return sViewConfiguration.getTextCursorBlinkIntervalMillis();
  }

  @WrapForJNI
  public static String getAppName() {
    final Context context = getApplicationContext();
    final ApplicationInfo info = context.getApplicationInfo();
    final int id = info.labelRes;
    return id == 0 ? info.nonLocalizedLabel.toString() : context.getString(id);
  }

  @WrapForJNI(calledFrom = "gecko")
  private static int getMemoryUsage(final String stateName) {
    final Debug.MemoryInfo memInfo = new Debug.MemoryInfo();
    Debug.getMemoryInfo(memInfo);
    final String usage = memInfo.getMemoryStat(stateName);
    if (usage == null) {
      return -1;
    }
    try {
      return Integer.parseInt(usage);
    } catch (final NumberFormatException e) {
      return -1;
    }
  }

  @WrapForJNI
  private static void crashByUncaughtException() {
    final Thread crashThread =
        new Thread("UncaughtExceptionThread") {
          @Override
          public void run() {
            throw new IllegalStateException();
          }
        };
    crashThread.start();
  }

  @WrapForJNI
  public static native boolean isParentProcess();

  @WrapForJNI
  public static native boolean isGpuProcessEnabled();

  @WrapForJNI
  public static native boolean isInteractiveWidgetDefaultResizesVisual();

  @WrapForJNI
  @SuppressLint("NewApi")
  public static boolean isIsolatedProcess() {
    // This method was added in SDK 16 but remained hidden until SDK 28, meaning we are okay to call
    // this on any SDK level but must suppress the new API lint.
    return android.os.Process.isIsolated();
  }

  @WrapForJNI(dispatchTo = "gecko")
  public static native void onSystemLocaleChanged();

  @WrapForJNI(dispatchTo = "gecko")
  public static native void onTimezoneChanged();

  @WrapForJNI
  public static native void logGpuProcessLaunchFailure(String aMessage);
}
