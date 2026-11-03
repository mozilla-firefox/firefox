/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.helpers

import androidx.test.platform.app.InstrumentationRegistry
import mozilla.components.feature.sitepermissions.SitePermissionsRules
import mozilla.components.lib.crash.store.CrashReportOption
import org.mozilla.fenix.ext.components

/** Helper for querying the status and modifying various features and settings in the application. */
interface FeatureSettingsHelper {
    /** Whether the Pocket stories feature is enabled or not. */
    var isPocketEnabled: Boolean

    /** Whether the onboarding dialog for choosing wallpapers should be shown or not. */
    var isWallpaperOnboardingEnabled: Boolean

    /** Whether the "Bookmarks" home screen section is enabled or not. It can show up to 8 bookmarks. */
    var isBookmarksHomeFeatureEnabled: Boolean

    /** Whether the homepage as a new tab feature is enabled or not. */
    var isHomepageAsNewTabEnabled: Boolean

    /**
     * Whether the "Continue" homescreen section is enabled or not. It shows the last visited tab on this device and on
     * other synced devices.
     */
    var isRecentTabsFeatureEnabled: Boolean

    /**
     * Whether the "Recently visited" homescreen section is enabled or not. It can show up to 9 history highlights and
     * history groups.
     */
    var isRecentlyVisitedFeatureEnabled: Boolean

    /** Whether the "Site permissions" option is checked in the "Delete browsing data" screen or not. */
    var isDeleteSitePermissionsEnabled: Boolean

    /**
     * The current "Enhanced Tracking Protection" policy.
     *
     * @see ETPPolicy
     */
    var etpPolicy: ETPPolicy

    /** Enable or disable open in app banner. */
    var isOpenInAppBannerEnabled: Boolean

    /** Enable or disable all location permission requests. */
    var isLocationPermissionEnabled: SitePermissionsRules.Action

    /** Enable or disable the new microsurvey feature. */
    var isMicrosurveyEnabled: Boolean

    /** Enable or disable bottom toolbar position. */
    var shouldUseBottomToolbar: Boolean

    /** Enable or disable the onboarding feature. */
    var onboardingFeatureEnabled: Boolean

    /** Enable or disable the tab swipe CFR. */
    var isTabSwipeCFREnabled: Boolean

    /** Accept or not the terms of service. */
    var isTermsOfServiceAccepted: Boolean

    /**
     * The Open links in External apps settings, between the following options:
     * - Ask before opening
     * - Never
     * - Always open in app
     */
    var openLinksInExternalApp: OpenLinksInApp

    /** Enable or disable the Tab Manager's opening animation. */
    var tabManagerOpeningAnimationEnabled: Boolean

    /** Indicates if the shake to summarize toolbar CFR was displayed to the user. */
    var hasSeenShakeToSummarizeToolbarCfr: Boolean

    /** Enable or disable the shake to summarize feature flag. */
    var shakeToSummarizeFeatureFlagEnabled: Boolean

    /** Enable or disable expanded toolbar layout. */
    var shouldUseExpandedToolbar: Boolean

    /** Enable or disable the tab strip (the "Show tab bar" customization). */
    var isTabStripEnabled: Boolean

    /** Whether the Native Share Sheet feature is enabled. */
    var nativeShareSheetEnabled: Boolean

    /** Whether the voice search entry point is shown in the display-mode browser toolbar. */
    var showVoiceSearchInDisplayToolbar: Boolean

    /** Whether trending and recent searches are shown on the Homepage search. */
    var isHomepageTrendingRecentSearchEnabled: Boolean

    /** Whether Google Lens feature is enabled. */
    var isGoogleLensIntegrationEnabled: Boolean

    /** Whether Google Lens integration is enabled for the user. */
    var isGoogleLensIntegrationUserEnabled: Boolean

    /** Whether FxSuggest feature is enabled. */
    var isFxSuggestEnabled: Boolean

    /** Whether the Android PDF tools are shown in the PDF viewer. */
    var isPdfToolsEnabled: Boolean

    /** Whether the current URL is shown separate from the addressbar when tapped. */
    var showAddressBarInFocusMode: Boolean

    /**
     * The crash reporting preference, between the following options:
     * - Ask before sending
     * - Send automatically
     * - Never send
     */
    var crashReportOption: CrashReportOption

    /** Enable or disable the translations prompt after a page that can be translated is loaded. */
    fun enableOrDisablePageLoadTranslationsPrompt(enableTranslationsPrompt: Boolean) {
        if (enableTranslationsPrompt) {
            FxNimbusHelper.enablePageLoadTranslationsPrompt()
        } else {
            FxNimbusHelper.disablePageLoadTranslationsPrompt()
        }
    }

    /** Enable or disable the IP Protection feature. */
    fun enableOrDisableIPProtection(enableIPProtection: Boolean) {
        if (enableIPProtection) {
            FxNimbusHelper.enableIPProtection()
        } else {
            FxNimbusHelper.disableIPProtection()
        }
    }

    fun applyFlagUpdates()

    fun resetAllFeatureFlags()

    companion object {
        val settings = InstrumentationRegistry.getInstrumentation().targetContext.components.settings
    }
}

/** All "Enhanced Tracking Protection" modes. */
enum class ETPPolicy {
    STANDARD,
    STRICT,
    CUSTOM,
}

enum class OpenLinksInApp {
    ALWAYS,
    NEVER,
    ASK,
}
