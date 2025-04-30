/* -*- Mode: Java; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * vim: ts=4 sw=4 expandtab:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview;

import static android.os.Build.VERSION;

import android.app.Service;
import android.graphics.Rect;
import android.os.Bundle;
import android.os.LocaleList;
import android.os.Parcel;
import android.os.Parcelable;
import android.text.TextUtils;
import android.util.Log;
import androidx.annotation.AnyThread;
import androidx.annotation.IntDef;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import org.mozilla.gecko.EventDispatcher;
import org.mozilla.gecko.GeckoSystemStateListener;
import org.mozilla.gecko.util.GeckoBundle;
import org.mozilla.gecko.util.LocaleUtils;

@AnyThread
public final class GeckoRuntimeSettings extends RuntimeSettings {
  private static final String LOGTAG = "GeckoRuntimeSettings";

  /** Settings builder used to construct the settings object. */
  @AnyThread
  public static final class Builder extends RuntimeSettings.Builder<GeckoRuntimeSettings> {
    @Override
    protected @NonNull GeckoRuntimeSettings newSettings(
        final @Nullable GeckoRuntimeSettings settings) {
      return new GeckoRuntimeSettings(settings);
    }

    /**
     * Set the custom Gecko process arguments.
     *
     * @param args The Gecko process arguments.
     * @return This Builder instance.
     */
    public @NonNull Builder arguments(final @NonNull String[] args) {
      if (args == null) {
        throw new IllegalArgumentException("Arguments must not  be null");
      }
      getSettings().mArgs = args;
      return this;
    }

    /**
     * Set the custom Gecko intent extras.
     *
     * @param extras The Gecko intent extras.
     * @return This Builder instance.
     */
    public @NonNull Builder extras(final @NonNull Bundle extras) {
      if (extras == null) {
        throw new IllegalArgumentException("Extras must not  be null");
      }
      getSettings().mExtras = extras;
      return this;
    }

    /**
     * Path to configuration file from which GeckoView will read configuration options such as Gecko
     * process arguments, environment variables, and preferences.
     *
     * <p>Note: this feature is only available for <code>{@link VERSION#SDK_INT} &gt; 21</code>, on
     * older devices this will be silently ignored.
     *
     * @param configFilePath Configuration file path to read from, or <code>null</code> to use
     *     default location <code>/data/local/tmp/$PACKAGE-geckoview-config.yaml</code>.
     * @return This Builder instance.
     */
    public @NonNull Builder configFilePath(final @Nullable String configFilePath) {
      getSettings().mConfigFilePath = configFilePath;
      return this;
    }

    /**
     * Set whether Extensions Process support should be enabled.
     *
     * @param flag A flag determining whether Extensions Process support should be enabled. Default
     *     is false.
     * @return This Builder instance.
     */
    public @NonNull Builder extensionsProcessEnabled(final boolean flag) {
      getSettings().mExtensionsProcess.set(flag);
      return this;
    }

    /**
     * Set the crash threshold within the timeframe before spawning is disabled for the remote
     * extensions process.
     *
     * @param crashThreshold The crash threshold within the timeframe before spawning is disabled.
     * @return This Builder instance.
     */
    public @NonNull Builder extensionsProcessCrashThreshold(final @NonNull Integer crashThreshold) {
      getSettings().mExtensionsProcessCrashThreshold.set(crashThreshold);
      return this;
    }

    /**
     * Set the crash threshold timeframe before spawning is disabled for the remote extensions
     * process. Crashes that are older than the current time minus timeframeMs will not be counted
     * towards meeting the threshold.
     *
     * @param timeframeMs The timeframe for the crash threshold in milliseconds. Any crashes older
     *     than the current time minus the timeframeMs are not counted.
     * @return This Builder instance.
     */
    public @NonNull Builder extensionsProcessCrashTimeframe(final @NonNull Long timeframeMs) {
      getSettings().mExtensionsProcessCrashTimeframe.set(timeframeMs);
      return this;
    }

    /**
     * Set whether JavaScript support should be enabled.
     *
     * @param flag A flag determining whether JavaScript should be enabled. Default is true.
     * @return This Builder instance.
     */
    public @NonNull Builder javaScriptEnabled(final boolean flag) {
      getSettings().mJavaScript.set(flag);
      return this;
    }

    /**
     * Set whether Global Privacy Control should be enabled. GPC is a mechanism for people to tell
     * websites to respect their privacy rights. Once turned on, it sends a signal to the websites
     * users visit telling them that the user doesn't want to be tracked and doesn't want their data
     * to be sold.
     *
     * @param enabled A flag determining whether Global Privacy Control should be enabled.
     * @return The builder instance.
     */
    public @NonNull Builder globalPrivacyControlEnabled(final boolean enabled) {
      getSettings().setGlobalPrivacyControl(enabled);
      return this;
    }

    /**
     * Set whether remote debugging support should be enabled.
     *
     * @param enabled True if remote debugging should be enabled.
     * @return This Builder instance.
     */
    public @NonNull Builder remoteDebuggingEnabled(final boolean enabled) {
      getSettings().mRemoteDebugging.set(enabled);
      return this;
    }

    /**
     * Set whether support for web fonts should be enabled.
     *
     * @param flag A flag determining whether web fonts should be enabled. Default is true.
     * @return This Builder instance.
     */
    public @NonNull Builder webFontsEnabled(final boolean flag) {
      getSettings().mWebFonts.set(flag ? 1 : 0);
      return this;
    }

    /**
     * Set whether there should be a pause during startup. This is useful if you need to wait for a
     * debugger to attach.
     *
     * @param enabled A flag determining whether there will be a pause early in startup. Defaults to
     *     false.
     * @return This Builder.
     */
    public @NonNull Builder pauseForDebugger(final boolean enabled) {
      getSettings().mDebugPause = enabled;
      return this;
    }

    /**
     * Set whether the to report the full bit depth of the device.
     *
     * <p>By default, 24 bits are reported for high memory devices and 16 bits for low memory
     * devices. If set to true, the device's maximum bit depth is reported. On most modern devices
     * this will be 32 bit screen depth.
     *
     * @param enable A flag determining whether maximum screen depth should be used.
     * @return This Builder.
     */
    public @NonNull Builder useMaxScreenDepth(final boolean enable) {
      getSettings().mUseMaxScreenDepth = enable;
      return this;
    }

    /**
     * Set whether web manifest support is enabled.
     *
     * <p>This controls if Gecko actually downloads, or "obtains", web manifests and processes them.
     * Without setting this pref, trying to obtain a manifest throws.
     *
     * @param enabled A flag determining whether Web Manifest processing support is enabled.
     * @return The builder instance.
     */
    public @NonNull Builder webManifest(final boolean enabled) {
      getSettings().mWebManifest.set(enabled);
      return this;
    }

    /**
     * Set whether or not web console messages should go to logcat.
     *
     * <p>Note: If enabled, Gecko performance may be negatively impacted if content makes heavy use
     * of the console API.
     *
     * @param enabled A flag determining whether or not web console messages should be printed to
     *     logcat.
     * @return The builder instance.
     */
    public @NonNull Builder consoleOutput(final boolean enabled) {
      getSettings().mConsoleOutput.set(enabled);
      return this;
    }

    /**
     * Set whether or not font sizes in web content should be automatically scaled according to the
     * device's current system font scale setting.
     *
     * @param enabled A flag determining whether or not font sizes should be scaled automatically to
     *     match the device's system font scale.
     * @return The builder instance.
     */
    public @NonNull Builder automaticFontSizeAdjustment(final boolean enabled) {
      getSettings().setAutomaticFontSizeAdjustment(enabled);
      return this;
    }

    /**
     * Set a font size factor that will operate as a global text zoom. All font sizes will be
     * multiplied by this factor.
     *
     * <p>The default factor is 1.0.
     *
     * <p>This setting cannot be modified if {@link Builder#automaticFontSizeAdjustment automatic
     * font size adjustment} has already been enabled.
     *
     * @param fontSizeFactor The factor to be used for scaling all text. Setting a value of 0
     *     disables both this feature and {@link Builder#fontInflation font inflation}.
     * @return The builder instance.
     */
    public @NonNull Builder fontSizeFactor(final float fontSizeFactor) {
      getSettings().setFontSizeFactor(fontSizeFactor);
      return this;
    }

    /**
     * Enable the Enterprise Roots feature.
     *
     * <p>When Enabled, GeckoView will fetch the third-party root certificates added to the Android
     * OS CA store and will use them internally.
     *
     * @param enabled whether to enable this feature or not
     * @return The builder instance
     */
    public @NonNull Builder enterpriseRootsEnabled(final boolean enabled) {
      getSettings().setEnterpriseRootsEnabled(enabled);
      return this;
    }

    /**
     * Set whether or not font inflation for non mobile-friendly pages should be enabled. The
     * default value of this setting is <code>false</code>.
     *
     * <p>When enabled, font sizes will be increased on all pages that are lacking a &lt;meta&gt;
     * viewport tag and have been loaded in a session using {@link
     * GeckoSessionSettings#VIEWPORT_MODE_MOBILE}. To improve readability, the font inflation logic
     * will attempt to increase font sizes for the main text content of the page only.
     *
     * <p>The magnitude of font inflation applied depends on the {@link Builder#fontSizeFactor font
     * size factor} currently in use.
     *
     * <p>This setting cannot be modified if {@link Builder#automaticFontSizeAdjustment automatic
     * font size adjustment} has already been enabled.
     *
     * @param enabled A flag determining whether or not font inflation should be enabled.
     * @return The builder instance.
     */
    public @NonNull Builder fontInflation(final boolean enabled) {
      getSettings().setFontInflationEnabled(enabled);
      return this;
    }

    /**
     * Set the display density override.
     *
     * @param density The display density value to use for overriding the system default.
     * @return The builder instance.
     */
    public @NonNull Builder displayDensityOverride(final float density) {
      getSettings().mDisplayDensityOverride = density;
      return this;
    }

    /**
     * Set the display DPI override.
     *
     * @param dpi The display DPI value to use for overriding the system default.
     * @return The builder instance.
     */
    public @NonNull Builder displayDpiOverride(final int dpi) {
      getSettings().mDisplayDpiOverride = dpi;
      return this;
    }

    /**
     * Set the screen size override.
     *
     * @param width The screen width value to use for overriding the system default.
     * @param height The screen height value to use for overriding the system default.
     * @return The builder instance.
     */
    public @NonNull Builder screenSizeOverride(final int width, final int height) {
      getSettings().mScreenWidthOverride = width;
      getSettings().mScreenHeightOverride = height;
      return this;
    }

    /**
     * Set whether login forms should be filled automatically if only one viable candidate is
     * provided via {@link Autocomplete.StorageDelegate#onLoginFetch onLoginFetch}.
     *
     * @param enabled A flag determining whether login autofill should be enabled.
     * @return The builder instance.
     */
    public @NonNull Builder loginAutofillEnabled(final boolean enabled) {
      getSettings().setLoginAutofillEnabled(enabled);
      return this;
    }

    /**
     * Set whether Fission should be enabled or not. This must be set before startup. Note: Session
     * History in Parent (SHIP) will be enabled as well if Fission is enabled.
     *
     * @param enabled A flag determining whether fission should be enabled.
     * @return The builder instance.
     */
    public @NonNull Builder fissionEnabled(final boolean enabled) {
      getSettings().mFissionEnabled.set(enabled);
      return this;
    }

    /**
     * Set whether a candidate page should automatically offer a translation via a popup.
     *
     * @param enabled A flag determining whether the translations offer popup should be enabled.
     * @return The builder instance.
     */
    public @NonNull Builder translationsOfferPopup(final boolean enabled) {
      getSettings().setTranslationsOfferPopup(enabled);
      return this;
    }

    /**
     * Sets whether Session History in Parent (SHIP) should be disabled or not.
     *
     * @param value A flag determining whether SHIP should be disabled or not.
     * @return The builder instance.
     */
    public @NonNull Builder disableShip(final boolean value) {
      getSettings().mDisableShip.set(value);
      return this;
    }

    /**
     * When set, the specified {@link android.app.Service} will be started by an {@link
     * android.content.Intent} with action {@link GeckoRuntime#ACTION_CRASHED} when a crash is
     * encountered. Crash details can be found in the Intent extras, such as {@link
     * GeckoRuntime#EXTRA_MINIDUMP_PATH}. <br>
     * <br>
     * The crash handler Service must be declared to run in a different process from the {@link
     * GeckoRuntime}. Additionally, the handler will be run as a foreground service, so the normal
     * rules about activating a foreground service apply. <br>
     * <br>
     * In practice, you have one of three options once the crash handler is started:
     *
     * <ul>
     *   <li>Call {@link android.app.Service#startForeground(int, android.app.Notification)}. You
     *       can then take as much time as necessary to report the crash.
     *   <li>Start an activity. Unless you also call {@link android.app.Service#startForeground(int,
     *       android.app.Notification)} this should be in a different process from the crash
     *       handler, since Android will kill the crash handler process as part of the background
     *       execution limitations.
     *   <li>Schedule work via {@link android.app.job.JobScheduler}. This will allow you to do
     *       substantial work in the background without execution limits.
     * </ul>
     *
     * <br>
     * You can use {@link CrashReporter} to send the report to Mozilla, which provides Mozilla with
     * data needed to fix the crash. Be aware that the minidump may contain personally identifiable
     * information (PII). Consult Mozilla's <a href="https://www.mozilla.org/en-US/privacy/">privacy
     * policy</a> for information on how this data will be handled.
     *
     * @param handler The class for the crash handler Service.
     * @return This builder instance.
     * @see <a href="https://developer.android.com/about/versions/oreo/background">Android
     *     Background Execution Limits</a>
     * @see GeckoRuntime#ACTION_CRASHED
     */
    public @NonNull Builder crashHandler(final @Nullable Class<? extends Service> handler) {
      getSettings().mCrashHandler = handler;
      return this;
    }

    /**
     * Set the locale.
     *
     * @param requestedLocales List of locale codes in Gecko format ("en" or "en-US").
     * @return The builder instance.
     */
    public @NonNull Builder locales(final @Nullable String[] requestedLocales) {
      getSettings().mRequestedLocales = requestedLocales;
      return this;
    }

    @SuppressWarnings("checkstyle:javadocmethod")
    public @NonNull Builder contentBlocking(final @NonNull ContentBlocking.Settings cb) {
      getSettings().mContentBlocking = cb;
      return this;
    }

    /**
     * Sets the preferred color scheme override for web content.
     *
     * @param scheme The preferred color scheme. Must be one of the {@link
     *     GeckoRuntimeSettings#COLOR_SCHEME_LIGHT COLOR_SCHEME_*} constants.
     * @return This Builder instance.
     */
    public @NonNull Builder preferredColorScheme(final @ColorScheme int scheme) {
      getSettings().setPreferredColorScheme(scheme);
      return this;
    }

    /**
     * Set whether auto-zoom to editable fields should be enabled.
     *
     * @param flag True if auto-zoom should be enabled, false otherwise.
     * @return This Builder instance.
     */
    public @NonNull Builder inputAutoZoomEnabled(final boolean flag) {
      getSettings().mInputAutoZoom.set(flag);
      return this;
    }

    /**
     * Set whether double tap zooming should be enabled.
     *
     * @param flag True if double tap zooming should be enabled, false otherwise.
     * @return This Builder instance.
     */
    public @NonNull Builder doubleTapZoomingEnabled(final boolean flag) {
      getSettings().mDoubleTapZooming.set(flag);
      return this;
    }

    /**
     * Sets the WebGL MSAA level.
     *
     * @param level number of MSAA samples, 0 if MSAA should be disabled.
     * @return This Builder instance.
     */
    public @NonNull Builder glMsaaLevel(final int level) {
      getSettings().mGlMsaaLevel.set(level);
      return this;
    }

    /**
     * Set the {@link ExperimentDelegate} instance on this runtime, if any. This delegate is used to
     * send and receive experiment information from Nimbus.
     *
     * @param delegate The {@link ExperimentDelegate} sending and retrieving experiment information.
     * @return The builder instance.
     */
    @AnyThread
    public @NonNull Builder experimentDelegate(final @Nullable ExperimentDelegate delegate) {
      getSettings().mExperimentDelegate = delegate;
      return this;
    }

    /**
     * Enables GeckoView and Gecko Logging. Logging is on by default. Does not control all logging
     * in Gecko. Logging done in Java code must be stripped out at build time.
     *
     * @param enable True if logging is enabled.
     * @return This Builder instance.
     */
    public @NonNull Builder debugLogging(final boolean enable) {
      getSettings().mDevToolsConsoleToLogcat.set(enable);
      getSettings().mConsoleServiceToLogcat.set(enable);
      getSettings().mGeckoViewLogLevel.set(enable ? "Debug" : "Fatal");
      return this;
    }

    /**
     * Sets whether or not about:config should be enabled. This is a page that allows users to
     * directly modify Gecko preferences. Modification of some preferences may cause the app to
     * break in unpredictable ways -- crashes, performance issues, security vulnerabilities, etc.
     *
     * @param flag True if about:config should be enabled, false otherwise.
     * @return This Builder instance.
     */
    public @NonNull Builder aboutConfigEnabled(final boolean flag) {
      getSettings().mAboutConfig.set(flag);
      return this;
    }

    /**
     * Sets whether or not pinch-zooming should be enabled when <code>user-scalable=no</code> is set
     * on the viewport.
     *
     * @param flag True if force user scalable zooming should be enabled, false otherwise.
     * @return This Builder instance.
     */
    public @NonNull Builder forceUserScalableEnabled(final boolean flag) {
      getSettings().mForceUserScalable.set(flag);
      return this;
    }

    /**
     * Sets whether and where insecure (non-HTTPS) connections are allowed.
     *
     * @param level One of the {@link GeckoRuntimeSettings#ALLOW_ALL HttpsOnlyMode} constants.
     * @return This Builder instance.
     */
    public @NonNull Builder allowInsecureConnections(final @HttpsOnlyMode int level) {
      getSettings().setAllowInsecureConnections(level);
      return this;
    }

    /**
     * Sets whether the Add-on Manager web API (`mozAddonManager`) is enabled.
     *
     * @param flag True if the web API should be enabled, false otherwise.
     * @return This Builder instance.
     */
    public @NonNull Builder extensionsWebAPIEnabled(final boolean flag) {
      getSettings().mExtensionsWebAPIEnabled.set(flag);
      return this;
    }

    /**
     * Sets whether and how DNS-over-HTTPS (Trusted Recursive Resolver) is configured.
     *
     * @param mode One of the {@link GeckoRuntimeSettings#TRR_MODE_OFF TrustedRecursiveResolverMode}
     *     constants.
     * @return This Builder instance.
     */
    public @NonNull Builder trustedRecursiveResolverMode(
        final @TrustedRecursiveResolverMode int mode) {
      getSettings().setTrustedRecursiveResolverMode(mode);
      return this;
    }

    /**
     * Set the DNS-over-HTTPS server URI.
     *
     * @param uri URI of the DNS-over-HTTPS server.
     * @return This Builder instance.
     */
    public @NonNull Builder trustedRecursiveResolverUri(final @NonNull String uri) {
      getSettings().setTrustedRecursiveResolverUri(uri);
      return this;
    }

    /**
     * Set the default DNS-over-HTTPS server URI.
     *
     * @param uri default URI of the DNS-over-HTTPS server.
     * @return This Builder instance.
     */
    public @NonNull Builder defaultRecursiveResolverUri(final @NonNull String uri) {
      getSettings().setDefaultRecursiveResolverUri(uri);
      return this;
    }

    /**
     * Set the factor by which to increase the keepalive timeout when the NS_HTTP_LARGE_KEEPALIVE
     * flag is used for a connection.
     *
     * @param factor FACTOR by which to increase the keepalive timeout.
     * @return This Builder instance.
     */
    public @NonNull Builder largeKeepaliveFactor(final int factor) {
      getSettings().setLargeKeepaliveFactor(factor);
      return this;
    }

    /**
     * Set this flag to disable low-memory detection. Set this when running tests to avoid
     * unpredictable behavior at runtime.
     *
     * @param enable True if low-memory detection should be enabled, false otherwise.
     * @return This Builder instance.
     */
    public @NonNull Builder lowMemoryDetection(final boolean enable) {
      getSettings().mLowMemoryDetection = enable;
      return this;
    }
  }

  private GeckoRuntime mRuntime;
  /* package */ String[] mArgs;
  /* package */ Bundle mExtras;
  /* package */ String mConfigFilePath;

  /* package */ ContentBlocking.Settings mContentBlocking;

  @SuppressWarnings("checkstyle:javadocmethod")
  public @NonNull ContentBlocking.Settings getContentBlocking() {
    return mContentBlocking;
  }

  /* package */ final Pref<Boolean> mWebManifest = new Pref<Boolean>("dom.manifest.enabled", true);
  /* package */ final Pref<Boolean> mJavaScript = new Pref<Boolean>("javascript.enabled", true);
  /* package */ final Pref<Boolean> mRemoteDebugging =
      new Pref<Boolean>("devtools.debugger.remote-enabled", false);
  /* package */ final Pref<Integer> mWebFonts =
      new Pref<Integer>("browser.display.use_document_fonts", 1);
  /* package */ final Pref<Boolean> mConsoleOutput =
      new Pref<Boolean>("geckoview.console.enabled", false);
  /* package */ float mFontSizeFactor = 1f;
  /* package */ final Pref<Boolean> mEnterpriseRootsEnabled =
      new Pref<>("security.enterprise_roots.enabled", false);
  /* package */ final Pref<Integer> mFontInflationMinTwips =
      new Pref<>("font.size.inflation.minTwips", 0);
  /* package */ final Pref<Boolean> mInputAutoZoom = new Pref<>("formhelper.autozoom", true);
  /* package */ final Pref<Boolean> mDoubleTapZooming =
      new Pref<>("apz.allow_double_tap_zooming", true);
  /* package */ final Pref<Integer> mGlMsaaLevel = new Pref<>("webgl.msaa-samples", 4);
  /* package */ final Pref<String> mGeckoViewLogLevel =
      new Pref<>("geckoview.logging", BuildConfig.DEBUG_BUILD ? "Debug" : "Warn");
  /* package */ final Pref<Boolean> mConsoleServiceToLogcat =
      new Pref<>("consoleservice.logcat", true);
  /* package */ final Pref<Boolean> mDevToolsConsoleToLogcat =
      new Pref<>("devtools.console.stdout.chrome", true);
  /* package */ final Pref<Boolean> mAboutConfig = new Pref<>("general.aboutConfig.enable", false);
  /* package */ final PrefWithoutDefault<Boolean> mFissionEnabled =
      new PrefWithoutDefault<>("fission.autostart");
  /* package */ final Pref<Boolean> mForceUserScalable =
      new Pref<>("browser.ui.zoom.force-user-scalable", false);
  /* package */ final PrefWithoutDefault<Integer> mWebContentIsolationStrategy =
      new PrefWithoutDefault<>("fission.webContentIsolationStrategy");
  /* package */ final Pref<Boolean> mAutofillLogins =
      new Pref<Boolean>("signon.autofillForms", true);
  /* package */ final Pref<Boolean> mAutomaticallyOfferPopup =
      new Pref<Boolean>("browser.translations.automaticallyPopup", true);
  /* package */ final Pref<Boolean> mHttpsOnly =
      new Pref<Boolean>("dom.security.https_only_mode", false);
  /* package */ final Pref<Boolean> mHttpsOnlyPrivateMode =
      new Pref<Boolean>("dom.security.https_only_mode_pbm", false);
  /* package */ final PrefWithoutDefault<Integer> mTrustedRecursiveResolverMode =
      new PrefWithoutDefault<>("network.trr.mode");
  /* package */ final PrefWithoutDefault<String> mTrustedRecursiveResolverUri =
      new PrefWithoutDefault<>("network.trr.uri");
  /* package */ final PrefWithoutDefault<String> mDefaultRecursiveResolverUri =
      new PrefWithoutDefault<>("network.trr.default_provider_uri");
  /* package */ final PrefWithoutDefault<String> mTrustedRecursiveResolverExcludedDomains =
      new PrefWithoutDefault<>("network.trr.excluded-domains");
  /* package */ final PrefWithoutDefault<Integer> mLargeKeepalivefactor =
      new PrefWithoutDefault<>("network.http.largeKeepaliveFactor");
  /* package */ final Pref<Integer> mProcessCount = new Pref<>("dom.ipc.processCount", 2);
  /* package */ final Pref<Boolean> mExtensionsWebAPIEnabled =
      new Pref<>("extensions.webapi.enabled", false);
  /* package */ final PrefWithoutDefault<Boolean> mExtensionsProcess =
      new PrefWithoutDefault<Boolean>("extensions.webextensions.remote");
  /* package */ final PrefWithoutDefault<Long> mExtensionsProcessCrashTimeframe =
      new PrefWithoutDefault<Long>("extensions.webextensions.crash.timeframe");
  /* package */ final PrefWithoutDefault<Integer> mExtensionsProcessCrashThreshold =
      new PrefWithoutDefault<Integer>("extensions.webextensions.crash.threshold");
  /* package */ final Pref<Boolean> mGlobalPrivacyControlEnabled =
      new Pref<Boolean>("privacy.globalprivacycontrol.enabled", false);
  /* package */ final Pref<Boolean> mGlobalPrivacyControlEnabledPrivateMode =
      new Pref<Boolean>("privacy.globalprivacycontrol.pbmode.enabled", true);
  /* package */ final Pref<Boolean> mGlobalPrivacyControlFunctionalityEnabled =
      new Pref<Boolean>("privacy.globalprivacycontrol.functionality.enabled", true);
  /* package */ final PrefWithoutDefault<Boolean> mFingerprintingProtection =
      new PrefWithoutDefault<Boolean>("privacy.fingerprintingProtection");
  /* package */ final PrefWithoutDefault<Boolean> mFingerprintingProtectionPrivateMode =
      new PrefWithoutDefault<Boolean>("privacy.fingerprintingProtection.pbmode");
  /* package */ final PrefWithoutDefault<String> mFingerprintingProtectionOverrides =
      new PrefWithoutDefault<>("privacy.fingerprintingProtection.overrides");
  /* package */ final Pref<Boolean> mFdlibmMathEnabled =
      new Pref<Boolean>("javascript.options.use_fdlibm_for_sin_cos_tan", false);
  /* package */ final Pref<Integer> mUserCharacteristicPingCurrentVersion =
      new Pref<>("toolkit.telemetry.user_characteristics_ping.current_version", 0);
  /* package */ PrefWithoutDefault<Boolean> mDisableShip =
      new PrefWithoutDefault<Boolean>("fission.disableSessionHistoryInParent");
  /* package */ final Pref<Boolean> mFetchPriorityEnabled =
      new Pref<Boolean>("network.fetchpriority.enabled", false);
  /* package */ final Pref<Boolean> mParallelMarkingEnabled =
      new Pref<Boolean>("javascript.options.mem.gc_parallel_marking", false);
  /* package */ final Pref<Boolean> mCookieBehaviorOptInPartitioning =
      new Pref<Boolean>("network.cookie.cookieBehavior.optInPartitioning", false);
  /* package */ final Pref<Boolean> mCookieBehaviorOptInPartitioningPBM =
      new Pref<Boolean>("network.cookie.cookieBehavior.optInPartitioning.pbmode", false);
  /* package */ final Pref<Integer> mCertificateTransparencyMode =
      new Pref<Integer>("security.pki.certificate_transparency.mode", 0);
  /* package */ final Pref<Boolean> mPostQuantumKeyExchangeTLSEnabled =
      new Pref<Boolean>("security.tls.enable_kyber", false);
  /* package */ final Pref<Boolean> mPostQuantumKeyExchangeHttp3Enabled =
      new Pref<Boolean>("network.http.http3.enable_kyber", false);

  /* package */ int mPreferredColorScheme = COLOR_SCHEME_SYSTEM;

  /* package */ boolean mForceEnableAccessibility;
  /* package */ boolean mDebugPause;
  /* package */ boolean mUseMaxScreenDepth;
  /* package */ boolean mLowMemoryDetection = true;
  /* package */ float mDisplayDensityOverride = -1.0f;
  /* package */ int mDisplayDpiOverride;
  /* package */ int mScreenWidthOverride;
  /* package */ int mScreenHeightOverride;
  /* package */ Class<? extends Service> mCrashHandler;
  /* package */ String[] mRequestedLocales;
  /* package */ ExperimentDelegate mExperimentDelegate;

  /**
   * Attach and commit the settings to the given runtime.
   *
   * @param runtime The runtime to attach to.
   */
  /* package */ void attachTo(final @NonNull GeckoRuntime runtime) {
    mRuntime = runtime;
    commit();
  }

  @Override // RuntimeSettings
  public @Nullable GeckoRuntime getRuntime() {
    return mRuntime;
  }

  /* package */ GeckoRuntimeSettings() {
    this(null);
  }

  /* package */ GeckoRuntimeSettings(final @Nullable GeckoRuntimeSettings settings) {
    super(/* parent */ null);

    if (settings == null) {
      mArgs = new String[0];
      mExtras = new Bundle();
      mContentBlocking = new ContentBlocking.Settings(this /* parent */, null /* settings */);
      return;
    }

    updateSettings(settings);
  }

  private void updateSettings(final @NonNull GeckoRuntimeSettings settings) {
    updatePrefs(settings);

    mArgs = settings.getArguments().clone();
    mExtras = new Bundle(settings.getExtras());
    mContentBlocking = new ContentBlocking.Settings(this /* parent */, settings.mContentBlocking);

    mForceEnableAccessibility = settings.mForceEnableAccessibility;
    mDebugPause = settings.mDebugPause;
    mUseMaxScreenDepth = settings.mUseMaxScreenDepth;
    mLowMemoryDetection = settings.mLowMemoryDetection;
    mDisplayDensityOverride = settings.mDisplayDensityOverride;
    mDisplayDpiOverride = settings.mDisplayDpiOverride;
    mScreenWidthOverride = settings.mScreenWidthOverride;
    mScreenHeightOverride = settings.mScreenHeightOverride;
    mCrashHandler = settings.mCrashHandler;
    mRequestedLocales = settings.mRequestedLocales;
    mConfigFilePath = settings.mConfigFilePath;
    mExperimentDelegate = settings.mExperimentDelegate;
  }

  /* package */ void commit() {
    commitLocales();
    commitResetPrefs();
  }

  /**
   * Get the custom Gecko process arguments.
   *
   * @return The Gecko process arguments.
   */
  public @NonNull String[] getArguments() {
    return mArgs;
  }

  /**
   * Get the custom Gecko intent extras.
   *
   * @return The Gecko intent extras.
   */
  public @NonNull Bundle getExtras() {
    return mExtras;
  }

  /**
   * Path to configuration file from which GeckoView will read configuration options such as Gecko
   * process arguments, environment variables, and preferences.
   *
   * <p>Note: this feature is only available for <code>{@link VERSION#SDK_INT} &gt; 21</code>.
   *
   * @return Path to configuration file from which GeckoView will read configuration options, or
   *     <code>null</code> for default location <code>/data/local/tmp/$PACKAGE-geckoview-config.yaml
   *     </code>.
   */
  public @Nullable String getConfigFilePath() {
    return mConfigFilePath;
  }

  /**
   * Get whether JavaScript support is enabled.
   *
   * @return Whether JavaScript support is enabled.
   */
  public boolean getJavaScriptEnabled() {
    return mJavaScript.get();
  }

  /**
   * Set whether JavaScript support should be enabled.
   *
   * @param flag A flag determining whether JavaScript should be enabled.
   * @return This GeckoRuntimeSettings instance.
   */
  public @NonNull GeckoRuntimeSettings setJavaScriptEnabled(final boolean flag) {
    mJavaScript.commit(flag);
    return this;
  }

  /**
   * Enable the Global Privacy Control Feature.
   *
   * <p>Note: Global Privacy Control is always enabled in private mode.
   *
   * @param enabled A flag determining whether GPC should be enabled.
   * @return This GeckoRuntimeSettings instance
   */
  public @NonNull GeckoRuntimeSettings setGlobalPrivacyControl(final boolean enabled) {
    mGlobalPrivacyControlEnabled.commit(enabled);
    // Global Privacy Control Feature is enabled by default in private browsing.
    mGlobalPrivacyControlEnabledPrivateMode.commit(true);
    mGlobalPrivacyControlFunctionalityEnabled.commit(true);
    return this;
  }

  /**
   * Set the Fingerprint protection in all tabs.
   *
   * @param enabled Whether we set the pref to true or false
   * @return This GeckoRuntimeSettings instance
   */
  public @NonNull GeckoRuntimeSettings setFingerprintingProtection(final boolean enabled) {
    mFingerprintingProtection.commit(enabled);
    return this;
  }

  /**
   * Set the Fingerprint protection in private tabs.
   *
   * @param enabled Whether we set the pref to true or false
   * @return This GeckoRuntimeSettings instance
   */
  public @NonNull GeckoRuntimeSettings setFingerprintingProtectionPrivateBrowsing(
      final boolean enabled) {
    mFingerprintingProtectionPrivateMode.commit(enabled);
    return this;
  }

  /**
   * Set the Fingerprint protection overrides
   *
   * @param overrides The overrides value to add or remove fingerprinting protection targets. Please
   *     check RFPTargets.inc for all supported targets.
   * @return This GeckoRuntimeSettings instance
   */
  public @NonNull GeckoRuntimeSettings setFingerprintingProtectionOverrides(
      @NonNull final String overrides) {
    mFingerprintingProtectionOverrides.commit(overrides);
    return this;
  }

  /**
   * Set the pref to control whether to use fdlibm for Math.sin, Math.cos, and Math.tan.
   *
   * @param enabled Whether we set the pref to true or false
   * @return This GeckoRuntimeSettings instance
   */
  public @NonNull GeckoRuntimeSettings setFdlibmMathEnabled(final boolean enabled) {
    mFdlibmMathEnabled.commit(enabled);
    return this;
  }

  /**
   * Get whether Fingerprint protection is enabled in all tabs.
   *
   * @return Whether Fingerprint protection is enabled in all tabs.
   */
  public @Nullable Boolean getFingerprintingProtection() {

    return mFingerprintingProtection.get();
  }

  /**
   * Get whether Fingerprint protection is enabled private browsing mode.
   *
   * @return Whether Fingerprint protection is enabled private browsing mode.
   */
  public @Nullable Boolean getFingerprintingProtectionPrivateBrowsing() {
    return mFingerprintingProtectionPrivateMode.get();
  }

  /**
   * Get Fingerprint protection overrides.
   *
   * @return The string of the fingerprinting protection overrides.
   */
  public @NonNull String getFingerprintingProtectionOverrides() {
    return mFingerprintingProtectionOverrides.get();
  }

  /**
   * Get whether to use fdlibm for Math.sin, Math.cos, and Math.tan.
   *
   * @return Whether the fdlibm is used
   */
  public boolean getFdlibmMathEnabled() {
    return mFdlibmMathEnabled.get();
  }

  /**
   * Set the pref to control the cookie behavior opt-in partitioning.
   *
   * @param enabled Whether we set the pref to true or false
   * @return This GeckoRuntimeSettings instance
   */
  public @NonNull GeckoRuntimeSettings setCookieBehaviorOptInPartitioning(final boolean enabled) {
    mCookieBehaviorOptInPartitioning.commit(enabled);
    return this;
  }

  /**
   * Set the pref to control the cookie behavior opt-in partitioning in private browsing mode.
   *
   * @param enabled Whether we set the pref to true or false
   * @return This GeckoRuntimeSettings instance
   */
  public @NonNull GeckoRuntimeSettings setCookieBehaviorOptInPartitioningPBM(
      final boolean enabled) {
    mCookieBehaviorOptInPartitioningPBM.commit(enabled);
    return this;
  }

  /**
   * Get whether the cookie behavior opt-in partitioning is enabled.
   *
   * @return Whether the cookie behavior opt-in partitioning is enabled.
   */
  public boolean getCookieBehaviorOptInPartitioning() {
    return mCookieBehaviorOptInPartitioning.get();
  }

  /**
   * Get whether the cookie behavior opt-in partitioning in private browsing mode is enabled.
   *
   * @return Whether the cookie behavior opt-in partitioning in private browsing mode is enabled.
   */
  public boolean getCookieBehaviorOptInPartitioningPBM() {
    return mCookieBehaviorOptInPartitioningPBM.get();
  }

  /**
   * Get whether Extensions Process support is enabled.
   *
   * @return Whether Extensions Process support is enabled.
   */
  public @Nullable Boolean getExtensionsProcessEnabled() {
    return mExtensionsProcess.get();
  }

  /**
   * Set whether Extensions Process support should be enabled.
   *
   * @param flag A flag determining whether Extensions Process support should be enabled.
   * @return This GeckoRuntimeSettings instance.
   */
  public @NonNull GeckoRuntimeSettings setExtensionsProcessEnabled(final boolean flag) {
    mExtensionsProcess.commit(flag);
    return this;
  }

  /**
   * Get the crash threshold before spawning is disabled for the remote extensions process.
   *
   * @return the crash threshold
   */
  public @Nullable Integer getExtensionsProcessCrashThreshold() {
    return mExtensionsProcessCrashThreshold.get();
  }

  /**
   * Get the timeframe in milliseconds for the threshold before spawning is disabled for the remote
   * extensions process.
   *
   * @return the timeframe in milliseconds for the crash threshold
   */
  public @Nullable Long getExtensionsProcessCrashTimeframe() {
    return mExtensionsProcessCrashTimeframe.get();
  }

  /**
   * Set the crash threshold before disabling spawning of the extensions remote process.
   *
   * @param crashThreshold max crashes allowed
   * @return This GeckoRuntimeSettings instance.
   */
  public @NonNull GeckoRuntimeSettings setExtensionsProcessCrashThreshold(
      final @NonNull Integer crashThreshold) {
    mExtensionsProcessCrashThreshold.commit(crashThreshold);
    return this;
  }

  /**
   * Set the timeframe for the extensions process crash threshold. Any crashes older than the
   * current time minus the timeframe are not included in the crash count.
   *
   * @param timeframeMs time in milliseconds
   * @return This GeckoRuntimeSettings instance.
   */
  public @NonNull GeckoRuntimeSettings setExtensionsProcessCrashTimeframe(
      final @NonNull Long timeframeMs) {
    mExtensionsProcessCrashTimeframe.commit(timeframeMs);
    return this;
  }

  /**
   * Set the pref to control whether network.fetchpriority.enabled is enabled.
   *
   * @param enabled Whether to enable the Fetch Priority feature
   * @return This GeckoRuntimeSettings instance
   */
  public @NonNull GeckoRuntimeSettings setFetchPriorityEnabled(final boolean enabled) {
    mFetchPriorityEnabled.commit(enabled);
    return this;
  }

  /**
   * Get whether network.fetchpriority.enabled is enabled.
   *
   * @return Whether Fetch Priority is enabled
   */
  public boolean getFetchPriorityEnabled() {
    return mFetchPriorityEnabled.get();
  }

  /**
   * Set the pref to control security.pki.certificate_transparency.mode.
   *
   * @param mode What to set the certificate transparency mode to. 0 disables certificate
   *     transparency entirely. 1 enables certificate transparency, but only collects telemetry. 2
   *     enforces certificate transparency.
   * @return This GeckoRuntimeSettings instance
   */
  public @NonNull GeckoRuntimeSettings setCertificateTransparencyMode(final int mode) {
    mCertificateTransparencyMode.commit(mode);
    return this;
  }

  /**
   * Get the value of security.pki.certificate_transparency.mode.
   *
   * @return What certificate transparency mode has been set.
   */
  public @NonNull int getCertificateTransparencyMode() {
    return mCertificateTransparencyMode.get();
  }

  /**
   * Set the pref to control whether javascript.options.mem.gc_parallel_marking is enabled.
   *
   * @param enabled Whether to enable the JS GC Parallel Marking feature. This feature is purely a
   *     performance feature and should have no noticeable behavior change for the user.
   * @return This GeckoRuntimeSettings instance
   */
  public @NonNull GeckoRuntimeSettings setParallelMarkingEnabled(final boolean enabled) {
    mParallelMarkingEnabled.commit(enabled);
    return this;
  }

  /**
   * Get whether javascript.options.mem.gc_parallel_marking is enabled.
   *
   * @return Whether Parallel Marking is enabled
   */
  public boolean getParallelMarkingEnabled() {
    return mParallelMarkingEnabled.get();
  }

  /**
   * Get whether remote debugging support is enabled.
   *
   * @return True if remote debugging support is enabled.
   */
  public boolean getRemoteDebuggingEnabled() {
    return mRemoteDebugging.get();
  }

  /**
   * Set whether remote debugging support should be enabled.
   *
   * @param enabled True if remote debugging should be enabled.
   * @return This GeckoRuntimeSettings instance.
   */
  public @NonNull GeckoRuntimeSettings setRemoteDebuggingEnabled(final boolean enabled) {
    mRemoteDebugging.commit(enabled);
    return this;
  }

  /**
   * Get whether web fonts support is enabled.
   *
   * @return Whether web fonts support is enabled.
   */
  public boolean getWebFontsEnabled() {
    return mWebFonts.get() != 0;
  }

  /**
   * Set whether support for web fonts should be enabled.
   *
   * @param flag A flag determining whether web fonts should be enabled.
   * @return This GeckoRuntimeSettings instance.
   */
  public @NonNull GeckoRuntimeSettings setWebFontsEnabled(final boolean flag) {
    mWebFonts.commit(flag ? 1 : 0);
    return this;
  }

  /**
   * Gets whether the pause-for-debugger is enabled or not.
   *
   * @return True if the pause is enabled.
   */
  public boolean getPauseForDebuggerEnabled() {
    return mDebugPause;
  }

  /**
   * Gets whether accessibility is force enabled or not.
   *
   * @return true if accessibility is force enabled.
   */
  public boolean getForceEnableAccessibility() {
    return mForceEnableAccessibility;
  }

  /**
   * Sets whether accessibility is force enabled or not.
   *
   * <p>Useful when testing accessibility.
   *
   * @param value whether accessibility is force enabled or not
   * @return this GeckoRuntimeSettings instance.
   */
  public @NonNull GeckoRuntimeSettings setForceEnableAccessibility(final boolean value) {
    mForceEnableAccessibility = value;
    SessionAccessibility.setForceEnabled(value);
    return this;
  }

  /**
   * Gets whether the compositor should use the maximum screen depth when rendering.
   *
   * @return True if the maximum screen depth should be used.
   */
  public boolean getUseMaxScreenDepth() {
    return mUseMaxScreenDepth;
  }

  /**
   * Gets whether the runtime should detect low-memory conditions.
   *
   * @return True if low-memory detection should be enabled.
   */
  public boolean getLowMemoryDetection() {
    return mLowMemoryDetection;
  }

  /**
   * Gets the display density override value.
   *
   * @return Returns a positive number. Will return null if not set.
   */
  public @Nullable Float getDisplayDensityOverride() {
    if (mDisplayDensityOverride > 0.0f) {
      return mDisplayDensityOverride;
    }
    return null;
  }

  /**
   * Gets the display DPI override value.
   *
   * @return Returns a positive number. Will return null if not set.
   */
  public @Nullable Integer getDisplayDpiOverride() {
    if (mDisplayDpiOverride > 0) {
      return mDisplayDpiOverride;
    }
    return null;
  }

  @SuppressWarnings("checkstyle:javadocmethod")
  public @Nullable Class<? extends Service> getCrashHandler() {
    return mCrashHandler;
  }

  /**
   * Gets the screen size override value.
   *
   * @return Returns a Rect containing the dimensions to use for the window size. Will return null
   *     if not set.
   */
  public @Nullable Rect getScreenSizeOverride() {
    if ((mScreenWidthOverride > 0) && (mScreenHeightOverride > 0)) {
      return new Rect(0, 0, mScreenWidthOverride, mScreenHeightOverride);
    }
    return null;
  }

  /**
   * Gets the list of requested locales.
   *
   * @return A list of locale codes in Gecko format ("en" or "en-US").
   */
  public @Nullable String[] getLocales() {
    return mRequestedLocales;
  }

  /**
   * Set the locale.
   *
   * @param requestedLocales An ordered list of locales in Gecko format ("en-US").
   */
  public void setLocales(final @Nullable String[] requestedLocales) {
    mRequestedLocales = requestedLocales;
    commitLocales();
  }

  /**
   * Gets whether the Add-on Manager web API (`mozAddonManager`) is enabled.
   *
   * @return True when the web API is enabled, false otherwise.
   */
  public boolean getExtensionsWebAPIEnabled() {
    return mExtensionsWebAPIEnabled.get();
  }

  /**
   * Get whether or not Global Privacy Control is currently enabled for normal tabs.
   *
   * @return True if GPC is enabled in normal tabs.
   */
  public boolean getGlobalPrivacyControl() {
    return mGlobalPrivacyControlEnabled.get();
  }

  /**
   * Get whether or not Global Privacy Control is currently enabled for private tabs.
   *
   * @return True if GPC is enabled in private tabs.
   */
  public boolean getGlobalPrivacyControlPrivateMode() {
    return mGlobalPrivacyControlEnabledPrivateMode.get();
  }

  /**
   * Sets whether the Add-on Manager web API (`mozAddonManager`) is enabled.
   *
   * @param flag True if the web API should be enabled, false otherwise.
   * @return This GeckoRuntimeSettings instance.
   */
  public @NonNull GeckoRuntimeSettings setExtensionsWebAPIEnabled(final boolean flag) {
    mExtensionsWebAPIEnabled.commit(flag);
    return this;
  }

  private void commitLocales() {
    final GeckoBundle data = new GeckoBundle(1);
    data.putStringArray("requestedLocales", mRequestedLocales);
    data.putString("acceptLanguages", computeAcceptLanguages());
    EventDispatcher.getInstance().dispatch("GeckoView:SetLocale", data);
  }

  private String computeAcceptLanguages() {
    final LinkedHashMap<String, String> locales = new LinkedHashMap<>();

    // Explicitly-set app prefs come first:
    if (mRequestedLocales != null) {
      for (final String locale : mRequestedLocales) {
        locales.put(locale.toLowerCase(Locale.ROOT), locale);
      }
    }
    // OS prefs come second:
    for (final String locale : getSystemLocalesForAcceptLanguage()) {
      final String localeLowerCase = locale.toLowerCase(Locale.ROOT);
      if (!locales.containsKey(localeLowerCase)) {
        locales.put(localeLowerCase, locale);
      }
    }

    return TextUtils.join(",", locales.values());
  }

  private static String[] getSystemLocalesForAcceptLanguage() {
    if (VERSION.SDK_INT >= 24) {
      final LocaleList localeList = LocaleList.getDefault();
      final String[] locales = new String[localeList.size()];
      for (int i = 0; i < localeList.size(); i++) {
        // accept-language should be language or language-region format.
        locales[i] = LocaleUtils.getLanguageTagForAcceptLanguage(localeList.get(i));
      }
      return locales;
    }
    final String[] locales = new String[1];
    final Locale locale = Locale.getDefault();
    locales[0] = LocaleUtils.getLanguageTagForAcceptLanguage(locale);
    return locales;
  }

  /**
   * Sets whether Web Manifest processing support is enabled.
   *
   * @param enabled A flag determining whether Web Manifest processing support is enabled.
   * @return This GeckoRuntimeSettings instance.
   */
  public @NonNull GeckoRuntimeSettings setWebManifestEnabled(final boolean enabled) {
    mWebManifest.commit(enabled);
    return this;
  }

  /**
   * Get whether or not Web Manifest processing support is enabled.
   *
   * @return True if web manifest processing support is enabled.
   */
  public boolean getWebManifestEnabled() {
    return mWebManifest.get();
  }

  /**
   * Set whether or not web console messages should go to logcat.
   *
   * <p>Note: If enabled, Gecko performance may be negatively impacted if content makes heavy use of
   * the console API.
   *
   * @param enabled A flag determining whether or not web console messages should be printed to
   *     logcat.
   * @return This GeckoRuntimeSettings instance.
   */
  public @NonNull GeckoRuntimeSettings setConsoleOutputEnabled(final boolean enabled) {
    mConsoleOutput.commit(enabled);
    return this;
  }

  /**
   * Get whether or not web console messages are sent to logcat.
   *
   * @return True if console output is enabled.
   */
  public boolean getConsoleOutputEnabled() {
    return mConsoleOutput.get();
  }

  /**
   * Set whether or not font sizes in web content should be automatically scaled according to the
   * device's current system font scale setting. Enabling this will prevent modification of the
   * {@link GeckoRuntimeSettings#setFontSizeFactor font size factor}. Disabling this setting will
   * restore the previously used value for the {@link GeckoRuntimeSettings#getFontSizeFactor font
   * size factor}.
   *
   * @param enabled A flag determining whether or not font sizes should be scaled automatically to
   *     match the device's system font scale.
   * @return This GeckoRuntimeSettings instance.
   */
  public @NonNull GeckoRuntimeSettings setAutomaticFontSizeAdjustment(final boolean enabled) {
    GeckoFontScaleListener.getInstance().setEnabled(enabled);
    return this;
  }

  /**
   * Get whether or not the font sizes for web content are automatically adjusted to match the
   * device's system font scale setting.
   *
   * @return True if font sizes are automatically adjusted.
   */
  public boolean getAutomaticFontSizeAdjustment() {
    return GeckoFontScaleListener.getInstance().getEnabled();
  }

  private static final int FONT_INFLATION_BASE_VALUE = 120;

  /**
   * Set a font size factor that will operate as a global text zoom. All font sizes will be
   * multiplied by this factor.
   *
   * <p>The default factor is 1.0.
   *
   * <p>Currently, any changes only take effect after a reload of the session.
   *
   * <p>This setting cannot be modified while {@link
   * GeckoRuntimeSettings#setAutomaticFontSizeAdjustment automatic font size adjustment} is enabled.
   *
   * @param fontSizeFactor The factor to be used for scaling all text. Setting a value of 0 disables
   *     both this feature and {@link GeckoRuntimeSettings#setFontInflationEnabled font inflation}.
   * @return This GeckoRuntimeSettings instance.
   */
  public @NonNull GeckoRuntimeSettings setFontSizeFactor(final float fontSizeFactor) {
    if (getAutomaticFontSizeAdjustment()) {
      throw new IllegalStateException("Not allowed when automatic font size adjustment is enabled");
    }
    return setFontSizeFactorInternal(fontSizeFactor);
  }

  /**
   * Enable the Enteprise Roots feature.
   *
   * <p>When Enabled, GeckoView will fetch the third-party root certificates added to the Android OS
   * CA store and will use them internally.
   *
   * @param enabled Whether to enable this feature or not.
   * @return This GeckoRuntimeSettings instance.
   */
  public @NonNull GeckoRuntimeSettings setEnterpriseRootsEnabled(final boolean enabled) {
    mEnterpriseRootsEnabled.commit(enabled);
    return this;
  }

  /**
   * Gets whether the Enteprise Roots feature is enabled or not.
   *
   * @return true if the feature is enabled, false otherwise.
   */
  public boolean getEnterpriseRootsEnabled() {
    return mEnterpriseRootsEnabled.get();
  }

  private static final float DEFAULT_FONT_SIZE_FACTOR = 1f;

  private float sanitizeFontSizeFactor(final float fontSizeFactor) {
    if (fontSizeFactor < 0) {
      if (BuildConfig.DEBUG_BUILD) {
        throw new IllegalArgumentException("fontSizeFactor cannot be < 0");
      } else {
        Log.e(LOGTAG, "fontSizeFactor cannot be < 0");
        return DEFAULT_FONT_SIZE_FACTOR;
      }
    }

    return fontSizeFactor;
  }

  /* package */ @NonNull
  GeckoRuntimeSettings setFontSizeFactorInternal(final float fontSizeFactor) {
    final float newFactor = sanitizeFontSizeFactor(fontSizeFactor);
    if (mFontSizeFactor == newFactor) {
      return this;
    }
    mFontSizeFactor = newFactor;
    if (getFontInflationEnabled()) {
      final int scaledFontInflation = Math.round(FONT_INFLATION_BASE_VALUE * newFactor);
      mFontInflationMinTwips.commit(scaledFontInflation);
    }
    GeckoSystemStateListener.onDeviceChanged();
    return this;
  }

  /**
   * Gets the currently applied font size factor.
   *
   * @return The currently applied font size factor.
   */
  public float getFontSizeFactor() {
    return mFontSizeFactor;
  }

  /**
   * Set whether or not font inflation for non mobile-friendly pages should be enabled. The default
   * value of this setting is <code>false</code>.
   *
   * <p>When enabled, font sizes will be increased on all pages that are lacking a &lt;meta&gt;
   * viewport tag and have been loaded in a session using {@link
   * GeckoSessionSettings#VIEWPORT_MODE_MOBILE}. To improve readability, the font inflation logic
   * will attempt to increase font sizes for the main text content of the page only.
   *
   * <p>The magnitude of font inflation applied depends on the {@link
   * GeckoRuntimeSettings#setFontSizeFactor font size factor} currently in use.
   *
   * <p>Currently, any changes only take effect after a reload of the session.
   *
   * @param enabled A flag determining whether or not font inflation should be enabled.
   * @return This GeckoRuntimeSettings instance.
   */
  public @NonNull GeckoRuntimeSettings setFontInflationEnabled(final boolean enabled) {
    final int minTwips = enabled ? Math.round(FONT_INFLATION_BASE_VALUE * getFontSizeFactor()) : 0;
    mFontInflationMinTwips.commit(minTwips);
    return this;
  }

  /**
   * Get whether or not font inflation for non mobile-friendly pages is currently enabled.
   *
   * @return True if font inflation is enabled.
   */
  public boolean getFontInflationEnabled() {
    return mFontInflationMinTwips.get() > 0;
  }

  @Retention(RetentionPolicy.SOURCE)
  @IntDef({COLOR_SCHEME_LIGHT, COLOR_SCHEME_DARK, COLOR_SCHEME_SYSTEM})
  public @interface ColorScheme {}

  /** A light theme for web content is preferred. */
  public static final int COLOR_SCHEME_LIGHT = 0;

  /** A dark theme for web content is preferred. */
  public static final int COLOR_SCHEME_DARK = 1;

  /** The preferred color scheme will be based on system settings. */
  public static final int COLOR_SCHEME_SYSTEM = -1;

  /**
   * Gets the preferred color scheme override for web content.
   *
   * @return One of the {@link GeckoRuntimeSettings#COLOR_SCHEME_LIGHT COLOR_SCHEME_*} constants.
   */
  public @ColorScheme int getPreferredColorScheme() {
    return mPreferredColorScheme;
  }

  /**
   * Sets the preferred color scheme override for web content.
   *
   * @param scheme The preferred color scheme. Must be one of the {@link
   *     GeckoRuntimeSettings#COLOR_SCHEME_LIGHT COLOR_SCHEME_*} constants.
   * @return This GeckoRuntimeSettings instance.
   */
  public @NonNull GeckoRuntimeSettings setPreferredColorScheme(final @ColorScheme int scheme) {
    if (mPreferredColorScheme != scheme) {
      mPreferredColorScheme = scheme;
      GeckoSystemStateListener.onDeviceChanged();
    }
    return this;
  }

  /**
   * Gets whether auto-zoom to editable fields is enabled.
   *
   * @return True if auto-zoom is enabled, false otherwise.
   */
  public boolean getInputAutoZoomEnabled() {
    return mInputAutoZoom.get();
  }

  /**
   * Set whether auto-zoom to editable fields should be enabled.
   *
   * @param flag True if auto-zoom should be enabled, false otherwise.
   * @return This GeckoRuntimeSettings instance.
   */
  public @NonNull GeckoRuntimeSettings setInputAutoZoomEnabled(final boolean flag) {
    mInputAutoZoom.commit(flag);
    return this;
  }

  /**
   * Gets whether double-tap zooming is enabled.
   *
   * @return True if double-tap zooming is enabled, false otherwise.
   */
  public boolean getDoubleTapZoomingEnabled() {
    return mDoubleTapZooming.get();
  }

  /**
   * Sets whether double tap zooming is enabled.
   *
   * @param flag true if double tap zooming should be enabled, false otherwise.
   * @return This GeckoRuntimeSettings instance.
   */
  public @NonNull GeckoRuntimeSettings setDoubleTapZoomingEnabled(final boolean flag) {
    mDoubleTapZooming.commit(flag);
    return this;
  }

  /**
   * Gets the current WebGL MSAA level.
   *
   * @return number of MSAA samples, 0 if MSAA is disabled.
   */
  public int getGlMsaaLevel() {
    return mGlMsaaLevel.get();
  }

  /**
   * Sets the WebGL MSAA level.
   *
   * @param level number of MSAA samples, 0 if MSAA should be disabled.
   * @return This GeckoRuntimeSettings instance.
   */
  public @NonNull GeckoRuntimeSettings setGlMsaaLevel(final int level) {
    mGlMsaaLevel.commit(level);
    return this;
  }

  /**
   * Get the {@link ExperimentDelegate} instance set on this runtime, if any,
   *
   * @return The {@link ExperimentDelegate} set on this runtime.
   */
  @AnyThread
  public @Nullable ExperimentDelegate getExperimentDelegate() {
    return mExperimentDelegate;
  }

  /**
   * Gets whether about:config is enabled or not.
   *
   * @return True if about:config is enabled, false otherwise.
   */
  public boolean getAboutConfigEnabled() {
    return mAboutConfig.get();
  }

  /**
   * Sets whether or not about:config should be enabled. This is a page that allows users to
   * directly modify Gecko preferences. Modification of some preferences may cause the app to break
   * in unpredictable ways -- crashes, performance issues, security vulnerabilities, etc.
   *
   * @param flag True if about:config should be enabled, false otherwise.
   * @return This GeckoRuntimeSettings instance.
   */
  public @NonNull GeckoRuntimeSettings setAboutConfigEnabled(final boolean flag) {
    mAboutConfig.commit(flag);
    return this;
  }

  /** See the `WebContentIsolationStrategy` enum in `ProcessIsolation.cpp`. */
  @Retention(RetentionPolicy.SOURCE)
  @IntDef({STRATEGY_ISOLATE_NOTHING, STRATEGY_ISOLATE_EVERYTHING, STRATEGY_ISOLATE_HIGH_VALUE})
  public @interface WebContentIsolationStrategy {};

  /**
   * All web content is loaded into a shared `web` content process. This is similar to the
   * non-Fission behaviour, however remote subframes may still be used for sites with special
   * isolation behaviour, such as extension or mozillaweb content processes.
   */
  public static final int STRATEGY_ISOLATE_NOTHING = 0;

  /**
   * Web content is always isolated into its own `webIsolated` content process based on site-origin,
   * and will only load in a shared `web` content process if site-origin could not be determined.
   */
  public static final int STRATEGY_ISOLATE_EVERYTHING = 1;

  /**
   * Only isolates web content loaded by sites which are considered "high value". A site is
   * considered "high value" if it has been granted a `highValue*` permission by the permission
   * manager, which is done in response to certain actions.
   */
  public static final int STRATEGY_ISOLATE_HIGH_VALUE = 2;

  /**
   * Get the strategy used to control how sites are isolated into separate processes when Fission is
   * enabled. This pref has no effect if Fission is disabled.
   *
   * <p>Setting should conform to {@link WebContentIsolationStrategy}, but is not automatically
   * mapped.
   *
   * @return The web content isolation strategy.
   */
  public @Nullable Integer getWebContentIsolationStrategy() {
    return mWebContentIsolationStrategy.get();
  }

  /**
   * Set the strategy used to control how sites are isolated into separate processes when Fission is
   * enabled. This pref has no effect if Fission is disabled.
   *
   * <p>Setting must conform to {@link WebContentIsolationStrategy} options.
   *
   * @param strategy The specified strategy defined by {@link WebContentIsolationStrategy}.
   * @return This GeckoRuntimeSettings instance.
   */
  public @NonNull GeckoRuntimeSettings setWebContentIsolationStrategy(
      final @NonNull @WebContentIsolationStrategy Integer strategy) {
    mWebContentIsolationStrategy.commit(strategy);
    return this;
  }

  /**
   * Gets whether or not force user scalable zooming should be enabled or not.
   *
   * @return True if force user scalable zooming should be enabled, false otherwise.
   */
  public boolean getForceUserScalableEnabled() {
    return mForceUserScalable.get();
  }

  /**
   * Sets whether or not pinch-zooming should be enabled when <code>user-scalable=no</code> is set
   * on the viewport.
   *
   * @param flag True if force user scalable zooming should be enabled, false otherwise.
   * @return This GeckoRuntimeSettings instance.
   */
  public @NonNull GeckoRuntimeSettings setForceUserScalableEnabled(final boolean flag) {
    mForceUserScalable.commit(flag);
    return this;
  }

  /**
   * Get whether login form autofill is enabled.
   *
   * @return True if login autofill is enabled.
   */
  public boolean getLoginAutofillEnabled() {
    return mAutofillLogins.get();
  }

  /**
   * Gets whether fission is enabled or not. Note: There is no setter after startup. See {@link
   * Builder#fissionEnabled(boolean)} for setting.
   *
   * @return True if fission is enabled or false otherwise.
   */
  public @Nullable Boolean getFissionEnabled() {
    return mFissionEnabled.get();
  }

  /**
   * Set whether automatic popups should appear for offering translations on candidate pages.
   *
   * @param enabled A flag determining whether automatic offer popups should be enabled for
   *     translations.
   * @return The builder instance.
   */
  public @NonNull GeckoRuntimeSettings setTranslationsOfferPopup(final boolean enabled) {
    mAutomaticallyOfferPopup.commit(enabled);
    return this;
  }

  /**
   * Get whether automatic popups for translations is enabled.
   *
   * @return True if login automatic popups for translations are enabled.
   */
  public boolean getTranslationsOfferPopup() {
    return mAutomaticallyOfferPopup.get();
  }

  /**
   * Set whether login forms should be filled automatically if only one viable candidate is provided
   * via {@link Autocomplete.StorageDelegate#onLoginFetch onLoginFetch}.
   *
   * @param enabled A flag determining whether login autofill should be enabled.
   * @return The builder instance.
   */
  public @NonNull GeckoRuntimeSettings setLoginAutofillEnabled(final boolean enabled) {
    mAutofillLogins.commit(enabled);
    return this;
  }

  @Retention(RetentionPolicy.SOURCE)
  @IntDef({ALLOW_ALL, HTTPS_ONLY_PRIVATE, HTTPS_ONLY})
  public @interface HttpsOnlyMode {}

  /** Allow all insecure connections */
  public static final int ALLOW_ALL = 0;

  /** Allow insecure connections in normal browsing, but only HTTPS in private browsing. */
  public static final int HTTPS_ONLY_PRIVATE = 1;

  /** Only allow HTTPS connections. */
  public static final int HTTPS_ONLY = 2;

  /**
   * Get whether and where insecure (non-HTTPS) connections are allowed.
   *
   * @return One of the {@link GeckoRuntimeSettings#ALLOW_ALL HttpsOnlyMode} constants.
   */
  public @HttpsOnlyMode int getAllowInsecureConnections() {
    final boolean httpsOnly = mHttpsOnly.get();
    final boolean httpsOnlyPrivate = mHttpsOnlyPrivateMode.get();
    if (httpsOnly) {
      return HTTPS_ONLY;
    } else if (httpsOnlyPrivate) {
      return HTTPS_ONLY_PRIVATE;
    }
    return ALLOW_ALL;
  }

  /**
   * Set whether and where insecure (non-HTTPS) connections are allowed.
   *
   * @param level One of the {@link GeckoRuntimeSettings#ALLOW_ALL HttpsOnlyMode} constants.
   * @return This GeckoRuntimeSettings instance.
   */
  public @NonNull GeckoRuntimeSettings setAllowInsecureConnections(final @HttpsOnlyMode int level) {
    switch (level) {
      case ALLOW_ALL:
        mHttpsOnly.commit(false);
        mHttpsOnlyPrivateMode.commit(false);
        break;
      case HTTPS_ONLY_PRIVATE:
        mHttpsOnly.commit(false);
        mHttpsOnlyPrivateMode.commit(true);
        break;
      case HTTPS_ONLY:
        mHttpsOnly.commit(true);
        mHttpsOnlyPrivateMode.commit(false);
        break;
      default:
        throw new IllegalArgumentException("Invalid setting for setAllowInsecureConnections");
    }
    return this;
  }

  /** The trusted recursive resolver (TRR) modes. */
  @Retention(RetentionPolicy.SOURCE)
  @IntDef({TRR_MODE_OFF, TRR_MODE_FIRST, TRR_MODE_ONLY, TRR_MODE_DISABLED})
  public @interface TrustedRecursiveResolverMode {}

  /** Off (default). Use native DNS resolution by default. */
  public static final int TRR_MODE_OFF = 0;

  /**
   * First. Use TRR first, and only if the name resolve fails use the native resolver as a fallback.
   */
  public static final int TRR_MODE_FIRST = 2;

  /** Only. Only use TRR, never use the native resolver. */
  public static final int TRR_MODE_ONLY = 3;

  /**
   * Off by choice. This is the same as 0 but marks it as done by choice and not done by default.
   */
  public static final int TRR_MODE_DISABLED = 5;

  /**
   * Get whether and how DNS-over-HTTPS (Trusted Recursive Resolver) is configured.
   *
   * @return One of the {@link GeckoRuntimeSettings#TRR_MODE_OFF TrustedRecursiveResolverMode}
   *     constants.
   */
  public @TrustedRecursiveResolverMode int getTrustedRecusiveResolverMode() {
    final int mode = mTrustedRecursiveResolverMode.get();
    switch (mode) {
      case 2:
        return TRR_MODE_FIRST;
      case 3:
        return TRR_MODE_ONLY;
      case 5:
        return TRR_MODE_DISABLED;
      default:
      case 0:
        return TRR_MODE_OFF;
    }
  }

  /**
   * Get the factor by which to increase the keepalive timeout when the NS_HTTP_LARGE_KEEPALIVE flag
   * is used for a connection.
   *
   * @return An integer factor.
   */
  public @NonNull int getLargeKeepaliveFactor() {
    return mLargeKeepalivefactor.get();
  }

  /**
   * Set whether and how DNS-over-HTTPS (Trusted Recursive Resolver) is configured.
   *
   * @param mode One of the {@link GeckoRuntimeSettings#TRR_MODE_OFF TrustedRecursiveResolverMode}
   *     constants.
   * @return This GeckoRuntimeSettings instance.
   */
  public @NonNull GeckoRuntimeSettings setTrustedRecursiveResolverMode(
      final @TrustedRecursiveResolverMode int mode) {
    switch (mode) {
      case TRR_MODE_OFF:
      case TRR_MODE_FIRST:
      case TRR_MODE_ONLY:
      case TRR_MODE_DISABLED:
        mTrustedRecursiveResolverMode.commit(mode);
        break;
      default:
        throw new IllegalArgumentException("Invalid setting for setTrustedRecursiveResolverMode");
    }
    return this;
  }

  private static final int DEFAULT_LARGE_KEEPALIVE_FACTOR = 1;

  private int sanitizeLargeKeepaliveFactor(final int factor) {
    if (factor < 1 || factor > 10) {
      if (BuildConfig.DEBUG_BUILD) {
        throw new IllegalArgumentException(
            "largeKeepaliveFactor must be between 1 to 10 inclusive");
      } else {
        Log.e(LOGTAG, "largeKeepaliveFactor must be between 1 to 10 inclusive");
        return DEFAULT_LARGE_KEEPALIVE_FACTOR;
      }
    }

    return factor;
  }

  /**
   * Set the factor by which to increase the keepalive timeout when the NS_HTTP_LARGE_KEEPALIVE flag
   * is used for a connection.
   *
   * @param factor FACTOR by which to increase the keepalive timeout.
   * @return This GeckoRuntimeSettings instance.
   */
  public @NonNull GeckoRuntimeSettings setLargeKeepaliveFactor(final int factor) {
    final int newFactor = sanitizeLargeKeepaliveFactor(factor);
    mLargeKeepalivefactor.commit(newFactor);
    return this;
  }

  /**
   * Get the DNS-over-HTTPS (DoH) server URI.
   *
   * @return URI of the DoH server.
   */
  public @NonNull String getTrustedRecursiveResolverUri() {
    return mTrustedRecursiveResolverUri.get();
  }

  /**
   * Set the DNS-over-HTTPS server URI.
   *
   * @param uri URI of the DNS-over-HTTPS server.
   * @return This GeckoRuntimeSettings instance.
   */
  public @NonNull GeckoRuntimeSettings setTrustedRecursiveResolverUri(final @NonNull String uri) {
    mTrustedRecursiveResolverUri.commit(uri);
    return this;
  }

  /**
   * Get the default DNS-over-HTTPS (DoH) server URI.
   *
   * @return default URI of the DoH server.
   */
  public @Nullable String getDefaultRecursiveResolverUri() {
    return mDefaultRecursiveResolverUri.get();
  }

  /**
   * Set the default DNS-over-HTTPS server URI.
   *
   * @param uri default URI of the DNS-over-HTTPS server.
   * @return This GeckoRuntimeSettings instance.
   */
  public @NonNull GeckoRuntimeSettings setDefaultRecursiveResolverUri(final @NonNull String uri) {
    mDefaultRecursiveResolverUri.commit(uri);
    return this;
  }

  /**
   * Get the domains excluded from using DNS-over-HTTPS
   *
   * @return A list of strings containing the domains saved in the pref.
   */
  public @NonNull List<String> getTrustedRecursiveResolverExcludedDomains() {
    final String domains = mTrustedRecursiveResolverExcludedDomains.get();
    if (domains.isEmpty()) {
      return List.of();
    }
    return Arrays.asList(domains.split("[\\s,]+"));
  }

  /**
   * Set the DNS-over-HTTPS excluded domains
   *
   * @param domains list of domains that will be excluded from using DoH. They will use platform DNS
   *     instead.
   * @return This GeckoRuntimeSettings instance.
   */
  public @NonNull GeckoRuntimeSettings setTrustedRecursiveResolverExcludedDomains(
      final @NonNull List<String> domains) {
    mTrustedRecursiveResolverExcludedDomains.commit(String.join(",", domains));
    return this;
  }

  /**
   * Get the current user characteristic ping version.
   *
   * @return The current version.
   */
  public @NonNull int getUserCharacteristicPingCurrentVersion() {
    return mUserCharacteristicPingCurrentVersion.get();
  }

  /**
   * Set the current user characteristic ping version.
   *
   * @param version The version number.
   * @return This GeckoRuntimeSettings instance.
   */
  public @NonNull GeckoRuntimeSettings setUserCharacteristicPingCurrentVersion(final int version) {
    mUserCharacteristicPingCurrentVersion.commit(version);
    return this;
  }

  /**
   * Retrieve the status of the disable session history in parent (SHIP) preference. May be null if
   * the value hasn't been specifically initialized.
   *
   * <p>Note, there is no conventional setter because this may only be set before Gecko is
   * initialized.
   *
   * <p>Set before initialization using {@link Builder#disableShip(boolean)}.
   *
   * @return True if SHIP is disabled, false if SHIP is enabled.
   */
  public @Nullable Boolean getDisableShip() {
    return mDisableShip.get();
  }

  /**
   * Set the preferences that control the use of post-quantum key exchange mechanisms
   *
   * @param enable Whether to enable or disable the preferences.
   * @return This GeckoRuntimeSettings instance
   */
  public @NonNull GeckoRuntimeSettings setPostQuantumKeyExchangeEnabled(final boolean enable) {
    mPostQuantumKeyExchangeTLSEnabled.commit(enable);
    mPostQuantumKeyExchangeHttp3Enabled.commit(enable);
    return this;
  }

  /**
   * Get whether post-quantum key exchange mechanisms are enabled.
   *
   * @return Whether post-quantum key exchange mechanisms are enabled.
   */
  public @NonNull boolean getPostQuantumKeyExchangeEnabled() {
    return mPostQuantumKeyExchangeTLSEnabled.get() && mPostQuantumKeyExchangeHttp3Enabled.get();
  }

  // For internal use only
  /* protected */ @NonNull
  GeckoRuntimeSettings setProcessCount(final int processCount) {
    mProcessCount.commit(processCount);
    return this;
  }

  @Override // Parcelable
  public void writeToParcel(final Parcel out, final int flags) {
    super.writeToParcel(out, flags);

    out.writeStringArray(mArgs);
    mExtras.writeToParcel(out, flags);
    ParcelableUtils.writeBoolean(out, mForceEnableAccessibility);
    ParcelableUtils.writeBoolean(out, mDebugPause);
    ParcelableUtils.writeBoolean(out, mUseMaxScreenDepth);
    ParcelableUtils.writeBoolean(out, mLowMemoryDetection);
    out.writeFloat(mDisplayDensityOverride);
    out.writeInt(mDisplayDpiOverride);
    out.writeInt(mScreenWidthOverride);
    out.writeInt(mScreenHeightOverride);
    out.writeString(mCrashHandler != null ? mCrashHandler.getName() : null);
    out.writeStringArray(mRequestedLocales);
    out.writeString(mConfigFilePath);
  }

  // AIDL code may call readFromParcel even though it's not part of Parcelable.
  @SuppressWarnings("checkstyle:javadocmethod")
  public void readFromParcel(final @NonNull Parcel source) {
    super.readFromParcel(source);

    mArgs = source.createStringArray();
    mExtras.readFromParcel(source);
    mForceEnableAccessibility = ParcelableUtils.readBoolean(source);
    mDebugPause = ParcelableUtils.readBoolean(source);
    mUseMaxScreenDepth = ParcelableUtils.readBoolean(source);
    mLowMemoryDetection = ParcelableUtils.readBoolean(source);
    mDisplayDensityOverride = source.readFloat();
    mDisplayDpiOverride = source.readInt();
    mScreenWidthOverride = source.readInt();
    mScreenHeightOverride = source.readInt();

    final String crashHandlerName = source.readString();
    if (crashHandlerName != null) {
      try {
        @SuppressWarnings("unchecked")
        final Class<? extends Service> handler =
            (Class<? extends Service>) Class.forName(crashHandlerName);

        mCrashHandler = handler;
      } catch (final ClassNotFoundException e) {
      }
    }

    mRequestedLocales = source.createStringArray();
    mConfigFilePath = source.readString();
  }

  public static final Parcelable.Creator<GeckoRuntimeSettings> CREATOR =
      new Parcelable.Creator<GeckoRuntimeSettings>() {
        @Override
        public GeckoRuntimeSettings createFromParcel(final Parcel in) {
          final GeckoRuntimeSettings settings = new GeckoRuntimeSettings();
          settings.readFromParcel(in);
          return settings;
        }

        @Override
        public GeckoRuntimeSettings[] newArray(final int size) {
          return new GeckoRuntimeSettings[size];
        }
      };
}
