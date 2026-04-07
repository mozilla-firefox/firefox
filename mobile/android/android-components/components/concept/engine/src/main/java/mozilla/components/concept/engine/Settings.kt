/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.engine

import mozilla.components.concept.engine.EngineSession.CookieBannerHandlingMode
import mozilla.components.concept.engine.EngineSession.SafeBrowsingPolicy
import mozilla.components.concept.engine.EngineSession.TrackingProtectionPolicy
import mozilla.components.concept.engine.fission.WebContentIsolationStrategy
import mozilla.components.concept.engine.history.HistoryTrackingDelegate
import mozilla.components.concept.engine.mediaquery.PreferredColorScheme
import mozilla.components.concept.engine.request.RequestInterceptor
import kotlin.reflect.KProperty

/**
 * Holds settings of an engine or session. Concrete engine
 * implementations define how these settings are applied i.e.
 * whether a setting is applied on an engine or session instance.
 */
@Suppress("UnnecessaryAbstractClass")
abstract class Settings {
    /**
     * Setting to control whether or not JavaScript is enabled.
     */
    open var javascriptEnabled: Boolean by UnsupportedSetting()

    /**
     * Setting to control whether or not DOM Storage is enabled.
     */
    open var domStorageEnabled: Boolean by UnsupportedSetting()

    /**
     * Setting to control whether or not Web fonts are enabled.
     */
    open var webFontsEnabled: Boolean by UnsupportedSetting()

    /**
     * Setting to control whether the fonts adjust size with the system accessibility settings.
     */
    open var automaticFontSizeAdjustment: Boolean by UnsupportedSetting()

    /**
     * Setting to control whether the [Accept-Language] headers are altered with system locale
     * settings.
     */
    open var automaticLanguageAdjustment: Boolean by UnsupportedSetting()

    /**
     * Setting to control tracking protection.
     */
    open var trackingProtectionPolicy: TrackingProtectionPolicy? by UnsupportedSetting()

    /**
     * Setting to control the cookie banner handling feature.
     */
    open var cookieBannerHandlingMode: CookieBannerHandlingMode by UnsupportedSetting()

    /**
     * Setting to control the cookie banner handling feature in the private browsing mode.
     */
    open var cookieBannerHandlingModePrivateBrowsing: CookieBannerHandlingMode by UnsupportedSetting()

    /**
     * Setting to control tracking protection.
     */
    open var safeBrowsingPolicy: Array<SafeBrowsingPolicy> by UnsupportedSetting()

    /**
     * Setting to control the cookie banner handling feature detect only mode.
     */
    open var cookieBannerHandlingDetectOnlyMode: Boolean by UnsupportedSetting()

    /**
     * Setting to control the cookie banner handling global rules feature.
     */
    open var cookieBannerHandlingGlobalRules: Boolean by UnsupportedSetting()

    /**
     * Setting to control the cookie banner handling global rules subFrames feature.
     */
    open var cookieBannerHandlingGlobalRulesSubFrames: Boolean by UnsupportedSetting()

    /**
     * Setting to control the cookie banner enables / disables the URL query string
     * stripping in normal browsing mode which strips query parameters from loading
     * URIs to prevent bounce (redirect) tracking.
     */
    open var queryParameterStripping: Boolean by UnsupportedSetting()

    /**
     * Setting to control the cookie banner enables / disables the URL query string
     * stripping in private browsing mode which strips query parameters from loading
     * URIs to prevent bounce (redirect) tracking.
     */
    open var queryParameterStrippingPrivateBrowsing: Boolean by UnsupportedSetting()

    /**
     * Setting to control the list that contains sites where should
     * exempt from query stripping.
     */
    open var queryParameterStrippingAllowList: String by UnsupportedSetting()

    /**
     * Setting to control the list which contains query parameters that are needed to be stripped
     * from  URIs. The query parameters are separated by a space.
     */
    open var queryParameterStrippingStripList: String by UnsupportedSetting()

    /**
     * Setting to intercept and override requests.
     */
    open var requestInterceptor: RequestInterceptor? by UnsupportedSetting()

    /**
     * Setting to provide a history delegate to the engine.
     */
    open var historyTrackingDelegate: HistoryTrackingDelegate? by UnsupportedSetting()

    /**
     * Setting to provide a delegate for handling download requests initiated by the engine.
     */
    open var downloadDelegate: DownloadDelegate? by UnsupportedSetting()

    /**
     * Setting to control the user agent string.
     */
    open var userAgentString: String? by UnsupportedSetting()

    /**
     * Setting to control whether or not a user gesture is required to play media.
     */
    open var mediaPlaybackRequiresUserGesture: Boolean by UnsupportedSetting()

    /**
     * Setting to control whether or not window.open can be called from JavaScript.
     */
    open var javaScriptCanOpenWindowsAutomatically: Boolean by UnsupportedSetting()

    /**
     * Setting to control whether or not zoom controls should be displayed.
     */
    open var displayZoomControls: Boolean by UnsupportedSetting()

    /**
     * Setting to control whether or not the engine zooms out the content to fit on screen by width.
     */
    open var loadWithOverviewMode: Boolean by UnsupportedSetting()

    /**
     * Setting to control whether to support the viewport HTML meta tag or if a wide viewport
     * should be used. If not null, this value overrides useWideViePort webSettings in
     * [EngineSession.toggleDesktopMode].
     */
    open var useWideViewPort: Boolean? by UnsupportedSetting()

    /**
     * Setting to control whether or not file access is allowed.
     */
    open var allowFileAccess: Boolean by UnsupportedSetting()

    /**
     * Setting to control whether or not JavaScript running in the context of a file scheme URL
     * should be allowed to access content from other file scheme URLs.
     */
    open var allowFileAccessFromFileURLs: Boolean by UnsupportedSetting()

    /**
     * Setting to control whether or not JavaScript running in the context of a file scheme URL
     * should be allowed to access content from any origin.
     */
    open var allowUniversalAccessFromFileURLs: Boolean by UnsupportedSetting()

    /**
     * Setting to control whether or not the engine is allowed to load content from a content
     * provider installed in the system.
     */
    open var allowContentAccess: Boolean by UnsupportedSetting()

    /**
     * Setting to control whether or not vertical scrolling is enabled.
     */
    open var verticalScrollBarEnabled: Boolean by UnsupportedSetting()

    /**
     * Setting to control whether or not horizontal scrolling is enabled.
     */
    open var horizontalScrollBarEnabled: Boolean by UnsupportedSetting()

    /**
     * Setting to control whether or not remote debugging is enabled.
     */
    open var remoteDebuggingEnabled: Boolean by UnsupportedSetting()

    /**
     * Setting to control whether or not multiple windows are supported.
     */
    open var supportMultipleWindows: Boolean by UnsupportedSetting()

    /**
     * Setting to control whether or not testing mode is enabled.
     */
    open var testingModeEnabled: Boolean by UnsupportedSetting()

    /**
     * Setting to alert the content that the user prefers a particular theme. This affects the
     * [@media(prefers-color-scheme)] query.
     */
    open var preferredColorScheme: PreferredColorScheme by UnsupportedSetting()

    /**
     * Setting to control whether media should be suspended when the session is inactive.
     */
    open var suspendMediaWhenInactive: Boolean by UnsupportedSetting()

    /**
     * Setting to control whether font inflation is enabled.
     */
    open var fontInflationEnabled: Boolean? by UnsupportedSetting()

    /**
     * Setting to control the font size factor. All font sizes will be multiplied by this factor.
     */
    open var fontSizeFactor: Float? by UnsupportedSetting()

    /**
     * Setting to control login autofill.
     */
    open var loginAutofillEnabled: Boolean by UnsupportedSetting()

    /**
     * Setting to control the Firefox Relay feature state.
     */
    open var firefoxRelay: Engine.FirefoxRelayMode? by UnsupportedSetting()

    /**
     * Setting to force the ability to scale the content
     */
    open var forceUserScalableContent: Boolean by UnsupportedSetting()

    /**
     * Setting to control the clear color while drawing.
     */
    open var clearColor: Int? by UnsupportedSetting()

    /**
     * Setting to control whether enterprise root certs are enabled.
     */
    open var enterpriseRootsEnabled: Boolean by UnsupportedSetting()

    /**
     * Setting the HTTPS-Only mode for upgrading connections to HTTPS.
     */
    open var httpsOnlyMode: Engine.HttpsOnlyMode by UnsupportedSetting()

    /**
     * Setting the DNS over HTTPS mode for upgrading connections to HTTPS.
     */
    open var dohSettingsMode: Engine.DohSettingsMode by UnsupportedSetting()

    /**
     * The url of the current provider in the DNS over HTTPS mode
     */
    open var dohProviderUrl: String by UnsupportedSetting()

    /**
     * The url of the default provider in the DNS over HTTPS mode
     */
    open var dohDefaultProviderUrl: String? by UnsupportedSetting()

    /**
     * The exceptions in the DNS over HTTPS mode
     */
    open var dohExceptionsList: List<String> by UnsupportedSetting()

    /**
     * Setting to control whether Global Privacy Control isenabled.
     */
    open var globalPrivacyControlEnabled: Boolean by UnsupportedSetting()

    /**
     * Setting to control the email tracker blocking feature in the private browsing mode.
     */
    open var emailTrackerBlockingPrivateBrowsing: Boolean by UnsupportedSetting()

    /**
     * Setting to control whether privacy.fingerprintingProtection is enabled.
     * This is enabled by default in private browsing mode (see variable below)
     * and exposed in the ETP Custom UI as 'Suspected Fingerprinters'.
     */
    open var fingerprintingProtection: Boolean? by UnsupportedSetting()

    /**
     * Setting to control whether privacy.fingerprintingProtection.pbmode is enabled.
     */
    open var fingerprintingProtectionPrivateBrowsing: Boolean? by UnsupportedSetting()

    /**
     * Setting to enable or disable certain fingerprinting protection features.
     */
    open var fingerprintingProtectionOverrides: String? by UnsupportedSetting()

    /**
     * Setting to control whehter to use fdlibm for Math.sin, Math.cos, and Math.tan.
     */
    open var fdlibmMathEnabled: Boolean by UnsupportedSetting()

    /**
     * Setting to control the user characteristic ping current version.
     */
    open var userCharacteristicPingCurrentVersion: Int by UnsupportedSetting()

    /**
     * Setting to control whether privacy.baselineFingerprintingProtection is enabled.
     * This is enabled by default in all modes.
     */
    open var baselineFingerprintingProtection: Boolean? by UnsupportedSetting()

    /**
     * Setting to enable or disable certain fingerprinting protection features
     * of baseline fpp.
     */
    open var baselineFingerprintingProtectionOverrides: String? by UnsupportedSetting()

    /**
     * Setting to control whether the desktop user agent is used.
     */
    open val desktopModeEnabled: Boolean by UnsupportedSetting()

    /**
     * Setting to control the web content isolation strategy used by fission.
     */
    open var webContentIsolationStrategy: WebContentIsolationStrategy? by UnsupportedSetting()

    /**
     * Setting to control whether network.fetchpriority.enabled is enabled.
     */
    open var fetchPriorityEnabled: Boolean by UnsupportedSetting()

    /**
     * Setting to control whether javascript.options.mem.gc_parallel_marking is enabled.
     */
    open var parallelMarkingEnabled: Boolean by UnsupportedSetting()

    /**
     * Setting to control the cookie behavior opt-in partitioning.
     */
    open var cookieBehaviorOptInPartitioning: Boolean by UnsupportedSetting()

    /**
     * Setting to control the cookie behavior opt-in partitioning in private browsing mode.
     */
    open var cookieBehaviorOptInPartitioningPBM: Boolean by UnsupportedSetting()

    /**
     * Setting to control how Certificate Transparency information is processed.
     */
    open var certificateTransparencyMode: Int? by UnsupportedSetting()

    /**
     * Setting to control whether post-quantum key exchange mechanisms are used
     * in TLS and HTTP/3.
     */
    open var postQuantumKeyExchangeEnabled: Boolean? by UnsupportedSetting()

    /**
     * Setting to control whether a DoH provider will be automatically selected when in Default Protection mode.
     */
    open var dohAutoselectEnabled: Boolean by UnsupportedSetting()

    /**
     * Comma-separated list of destination ports that the application should block connections to.
     */
    open var bannedPorts: String by UnsupportedSetting()

    /**
     * Setting to control the request blocking feature of Local Network / Device Access blocking
     */
    open var lnaBlockingEnabled: Boolean by UnsupportedSetting()

    /**
     * Setting to control the tracker blocking feature of Local Network / Device Access blocking
     */
    open var lnaTrackerBlockingEnabled: Boolean by UnsupportedSetting()

    /**
     * Setting to control the overall Local Network / Device Access blocking feature. This is a
     * superset of [lnaBlockingEnabled] & [lnaTrackerBlockingEnabled]
     */
    open var lnaFeatureEnabled: Boolean by UnsupportedSetting()

    /**
     * Setting to control the CRLite certificate blocklist channel
     */
    open var crliteChannel: String? by UnsupportedSetting()

    /**
     * Setting to control whether Safe Browsing V5 is enabled.
     */
    open var safeBrowsingV5Enabled: Boolean? by UnsupportedSetting()

    /**
     * Setting to control whether Safe Browsing Global Cache is enabled.
     */
    open var safeBrowsingGlobalCacheEnabled: Boolean? by UnsupportedSetting()

    /**
     * Setting to control whether Safe Browsing Real-Time lookup is enabled.
     */
    open var safeBrowsingRealTimeEnabled: Boolean? by UnsupportedSetting()

    /**
     * Setting to control whether Safe Browsing Real-Time simulation is enabled.
     */
    open var safeBrowsingRealTimeSimulationEnabled: Boolean? by UnsupportedSetting()

    /**
     * Setting to control the hit probability for Safe Browsing Real-Time simulation.
     */
    open var safeBrowsingRealTimeSimulationHitProbability: Int? by UnsupportedSetting()

    /**
     * Setting to control the cache TTL in seconds for Safe Browsing Real-Time simulation.
     */
    open var safeBrowsingRealTimeSimulationCacheTTLSec: Int? by UnsupportedSetting()

    /**
     * Setting to control whether the negative cache for Safe Browsing Real-Time simulation is enabled.
     */
    open var safeBrowsingRealTimeSimulationNegativeCacheEnabled: Boolean? by UnsupportedSetting()

    /**
     * Setting to control the negative cache TTL in seconds for Safe Browsing Real-Time simulation.
     */
    open var safeBrowsingRealTimeSimulationNegativeCacheTTLSec: Int? by UnsupportedSetting()

    /**
     * Setting to control whether the browser platform AI API is enabled.
     */
    open var browserML: Boolean? by UnsupportedSetting()

    /**
     * Setting to control whether the extension platform AI API is enabled.
     */
    open var extensionsML: Boolean? by UnsupportedSetting()
}

/**
 * [Settings] implementation used to set defaults for [Engine] and [EngineSession].
 */
data class DefaultSettings(
    override var javascriptEnabled: Boolean = true,
    override var domStorageEnabled: Boolean = true,
    override var webFontsEnabled: Boolean = true,
    override var automaticFontSizeAdjustment: Boolean = true,
    override var automaticLanguageAdjustment: Boolean = true,
    override var mediaPlaybackRequiresUserGesture: Boolean = true,
    override var trackingProtectionPolicy: TrackingProtectionPolicy? = null,
    override var requestInterceptor: RequestInterceptor? = null,
    override var historyTrackingDelegate: HistoryTrackingDelegate? = null,
    override var userAgentString: String? = null,
    override var javaScriptCanOpenWindowsAutomatically: Boolean = false,
    override var displayZoomControls: Boolean = true,
    override var loadWithOverviewMode: Boolean = false,
    override var useWideViewPort: Boolean? = null,
    override var allowFileAccess: Boolean = true,
    override var allowFileAccessFromFileURLs: Boolean = false,
    override var allowUniversalAccessFromFileURLs: Boolean = false,
    override var allowContentAccess: Boolean = true,
    override var verticalScrollBarEnabled: Boolean = true,
    override var horizontalScrollBarEnabled: Boolean = true,
    override var remoteDebuggingEnabled: Boolean = false,
    override var supportMultipleWindows: Boolean = false,
    override var preferredColorScheme: PreferredColorScheme = PreferredColorScheme.System,
    override var testingModeEnabled: Boolean = false,
    override var suspendMediaWhenInactive: Boolean = false,
    override var firefoxRelay: Engine.FirefoxRelayMode? = null,
    override var fontInflationEnabled: Boolean? = null,
    override var fontSizeFactor: Float? = null,
    override var forceUserScalableContent: Boolean = false,
    override var loginAutofillEnabled: Boolean = false,
    override var clearColor: Int? = null,
    override var enterpriseRootsEnabled: Boolean = false,
    override var httpsOnlyMode: Engine.HttpsOnlyMode = Engine.HttpsOnlyMode.DISABLED,
    override var dohSettingsMode: Engine.DohSettingsMode = Engine.DohSettingsMode.DEFAULT,
    override var dohProviderUrl: String = "",
    override var dohDefaultProviderUrl: String? = "",
    override var dohExceptionsList: List<String> = emptyList(),
    override var globalPrivacyControlEnabled: Boolean = false,
    override var fingerprintingProtection: Boolean? = null,
    override var fingerprintingProtectionPrivateBrowsing: Boolean? = null,
    override var fingerprintingProtectionOverrides: String? = null,
    override var fdlibmMathEnabled: Boolean = false,
    override var baselineFingerprintingProtection: Boolean? = null,
    override var baselineFingerprintingProtectionOverrides: String? = null,
    override var cookieBannerHandlingMode: CookieBannerHandlingMode = CookieBannerHandlingMode.DISABLED,
    override var cookieBannerHandlingModePrivateBrowsing: CookieBannerHandlingMode =
        CookieBannerHandlingMode.DISABLED,
    override var cookieBannerHandlingDetectOnlyMode: Boolean = false,
    override var cookieBannerHandlingGlobalRules: Boolean = false,
    override var cookieBannerHandlingGlobalRulesSubFrames: Boolean = false,
    override var queryParameterStripping: Boolean = false,
    override var queryParameterStrippingPrivateBrowsing: Boolean = false,
    override var queryParameterStrippingAllowList: String = "",
    override var queryParameterStrippingStripList: String = "",
    override var emailTrackerBlockingPrivateBrowsing: Boolean = false,
    override var userCharacteristicPingCurrentVersion: Int = 0,
    override var webContentIsolationStrategy: WebContentIsolationStrategy? =
        WebContentIsolationStrategy.ISOLATE_HIGH_VALUE,
    override var fetchPriorityEnabled: Boolean = true,
    override var parallelMarkingEnabled: Boolean = false,
    val getDesktopMode: () -> Boolean = { false },
    override var cookieBehaviorOptInPartitioning: Boolean = false,
    override var cookieBehaviorOptInPartitioningPBM: Boolean = false,
    override var certificateTransparencyMode: Int? = null,
    override var postQuantumKeyExchangeEnabled: Boolean? = null,
    override var dohAutoselectEnabled: Boolean = false,
    override var bannedPorts: String = "",
    override var lnaBlockingEnabled: Boolean = false,
    override var lnaTrackerBlockingEnabled: Boolean = false,
    override var lnaFeatureEnabled: Boolean = false,
    override var crliteChannel: String? = null,
    override var safeBrowsingV5Enabled: Boolean? = null,
    override var downloadDelegate: DownloadDelegate? = null,
    override var safeBrowsingGlobalCacheEnabled: Boolean? = null,
    override var safeBrowsingRealTimeEnabled: Boolean? = null,
    override var safeBrowsingRealTimeSimulationEnabled: Boolean? = null,
    override var safeBrowsingRealTimeSimulationHitProbability: Int? = null,
    override var safeBrowsingRealTimeSimulationCacheTTLSec: Int? = null,
    override var safeBrowsingRealTimeSimulationNegativeCacheEnabled: Boolean? = null,
    override var safeBrowsingRealTimeSimulationNegativeCacheTTLSec: Int? = null,
    override var browserML: Boolean? = null,
    override var extensionsML: Boolean? = null,
) : Settings() {
    override val desktopModeEnabled: Boolean
        get() = getDesktopMode()
}

class UnsupportedSetting<T> {
    operator fun getValue(thisRef: Any?, prop: KProperty<*>): T {
        throw UnsupportedSettingException(
            "The setting ${prop.name} is not supported by this engine or session. " +
                "Check both the engine and engine session implementation.",
        )
    }

    operator fun setValue(thisRef: Any?, prop: KProperty<*>, value: T) {
        throw UnsupportedSettingException(
            "The setting ${prop.name} is not supported by this engine or session. " +
                "Check both the engine and engine session implementation.",
        )
    }
}

/**
 * Exception thrown by default if a setting is not supported by an engine or session.
 */
class UnsupportedSettingException(message: String = "Setting not supported by this engine") : RuntimeException(message)
