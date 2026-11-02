/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.helpers

import android.util.Log
import kotlinx.coroutines.runBlocking
import mozilla.components.feature.sitepermissions.SitePermissionsRules
import mozilla.components.lib.crash.store.CrashReportOption
import org.mozilla.fenix.R
import org.mozilla.fenix.crashes.crashReportOption
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.getPreferenceKey
import org.mozilla.fenix.helpers.Constants.TAG
import org.mozilla.fenix.helpers.ETPPolicy.CUSTOM
import org.mozilla.fenix.helpers.ETPPolicy.STANDARD
import org.mozilla.fenix.helpers.ETPPolicy.STRICT
import org.mozilla.fenix.helpers.FeatureSettingsHelper.Companion.settings
import org.mozilla.fenix.helpers.TestHelper.appContext
import org.mozilla.fenix.settings.PhoneFeature
import org.mozilla.fenix.utils.Settings

/** Helper for querying the status and modifying various features and settings in the application. */
class FeatureSettingsHelperDelegate : FeatureSettingsHelper {
    /**
     * The current feature flags used inside the app before the tests start. These will be restored when the tests end.
     */
    private val initialFeatureFlags =
        FeatureFlags(
            isHomepageAsNewTabEnabled = settings.enableHomepageAsNewTab,
            isPocketEnabled = settings.showPocketRecommendationsFeature,
            isBookmarksHomeFeatureEnabled = settings.showBookmarksHomeFeature,
            isRecentTabsFeatureEnabled = settings.showRecentTabsFeature,
            isRecentlyVisitedFeatureEnabled = settings.historyMetadataUIFeature,
            isWallpaperOnboardingEnabled = settings.showWallpaperOnboarding,
            isDeleteSitePermissionsEnabled = settings.deleteSitePermissions,
            isOpenInAppBannerEnabled = settings.shouldShowOpenInAppBanner,
            etpPolicy = getETPPolicy(settings),
            isLocationPermissionEnabled = getFeaturePermission(PhoneFeature.LOCATION, settings),
            isMicrosurveyEnabled = settings.microsurveyFeatureEnabled,
            shouldUseBottomToolbar = settings.shouldUseBottomToolbar,
            onboardingFeatureEnabled = settings.onboardingFeatureEnabled,
            isTabSwipeCFREnabled = settings.hasShownTabSwipeCFR,
            isTermsOfServiceAccepted = settings.hasAcceptedTermsOfService,
            openLinksInApp = getOpenLinksInApp(settings),
            tabManagerOpeningAnimationEnabled = settings.tabManagerOpeningAnimationEnabled,
            hasSeenShakeToSummarizeToolbarCfr = settings.shakeToSummarizeToolbarCfrShown,
            shakeToSummarizeFeatureFlagEnabled = settings.shakeToSummarizeFeatureFlagEnabled,
            shouldUseExpandedToolbar = settings.shouldUseExpandedToolbar,
            isTabStripEnabled = settings.isTabStripEnabled,
            nativeShareSheetEnabled = settings.nativeShareSheetEnabled,
            showVoiceSearchInDisplayToolbar = settings.showVoiceSearchInDisplayToolbar,
            isHomepageTrendingRecentSearchEnabled = settings.enableHomepageTrendingRecentSearch,
            isGoogleLensIntegrationEnabled = settings.googleLensIntegrationEnabled,
            isGoogleLensIntegrationUserEnabled = settings.googleLensIntegrationUserEnabled,
            isFxSuggestEnabled = settings.enableFxSuggest,
            isPdfToolsEnabled = settings.enablePdfTools,
            showAddressBarInFocusMode = settings.showAddressBarInFocusMode,
            crashReportOption = settings.crashReportOption(),
        )

    /** The current feature flags updated in tests. */
    private var updatedFeatureFlags = initialFeatureFlags.copy()

    override var isHomepageAsNewTabEnabled: Boolean by updatedFeatureFlags::isHomepageAsNewTabEnabled
    override var isPocketEnabled: Boolean by updatedFeatureFlags::isPocketEnabled
    override var isBookmarksHomeFeatureEnabled: Boolean by updatedFeatureFlags::isBookmarksHomeFeatureEnabled
    override var isWallpaperOnboardingEnabled: Boolean by updatedFeatureFlags::isWallpaperOnboardingEnabled
    override var isRecentTabsFeatureEnabled: Boolean by updatedFeatureFlags::isRecentTabsFeatureEnabled
    override var isRecentlyVisitedFeatureEnabled: Boolean by updatedFeatureFlags::isRecentlyVisitedFeatureEnabled
    override var isOpenInAppBannerEnabled: Boolean by updatedFeatureFlags::isOpenInAppBannerEnabled
    override var etpPolicy: ETPPolicy by updatedFeatureFlags::etpPolicy
    override var isLocationPermissionEnabled: SitePermissionsRules.Action by
        updatedFeatureFlags::isLocationPermissionEnabled
    override var isMicrosurveyEnabled: Boolean by updatedFeatureFlags::isMicrosurveyEnabled
    override var shouldUseBottomToolbar: Boolean by updatedFeatureFlags::shouldUseBottomToolbar
    override var onboardingFeatureEnabled: Boolean by updatedFeatureFlags::onboardingFeatureEnabled
    override var isTabSwipeCFREnabled: Boolean by updatedFeatureFlags::isTabSwipeCFREnabled
    override var isTermsOfServiceAccepted: Boolean by updatedFeatureFlags::isTermsOfServiceAccepted
    override var openLinksInExternalApp: OpenLinksInApp by updatedFeatureFlags::openLinksInApp
    override var tabManagerOpeningAnimationEnabled: Boolean by updatedFeatureFlags::tabManagerOpeningAnimationEnabled
    override var hasSeenShakeToSummarizeToolbarCfr: Boolean by updatedFeatureFlags::hasSeenShakeToSummarizeToolbarCfr
    override var shakeToSummarizeFeatureFlagEnabled: Boolean by updatedFeatureFlags::shakeToSummarizeFeatureFlagEnabled
    override var shouldUseExpandedToolbar: Boolean by updatedFeatureFlags::shouldUseExpandedToolbar
    override var isTabStripEnabled: Boolean by updatedFeatureFlags::isTabStripEnabled
    override var nativeShareSheetEnabled: Boolean by updatedFeatureFlags::nativeShareSheetEnabled
    override var showVoiceSearchInDisplayToolbar: Boolean by updatedFeatureFlags::showVoiceSearchInDisplayToolbar
    override var isHomepageTrendingRecentSearchEnabled: Boolean by
        updatedFeatureFlags::isHomepageTrendingRecentSearchEnabled
    override var isGoogleLensIntegrationEnabled: Boolean by updatedFeatureFlags::isGoogleLensIntegrationEnabled
    override var isGoogleLensIntegrationUserEnabled: Boolean by updatedFeatureFlags::isGoogleLensIntegrationUserEnabled
    override var isFxSuggestEnabled: Boolean by updatedFeatureFlags::isFxSuggestEnabled
    override var isPdfToolsEnabled: Boolean by updatedFeatureFlags::isPdfToolsEnabled
    override var showAddressBarInFocusMode: Boolean by updatedFeatureFlags::showAddressBarInFocusMode
    override var crashReportOption: CrashReportOption by updatedFeatureFlags::crashReportOption

    override fun applyFlagUpdates() {
        Log.i(TAG, "applyFlagUpdates: Trying to apply the updated feature flags: $updatedFeatureFlags")
        applyFeatureFlags(updatedFeatureFlags)
        Log.i(TAG, "applyFlagUpdates: Applied the updated feature flags: $updatedFeatureFlags")
    }

    override fun resetAllFeatureFlags() {
        Log.i(TAG, "resetAllFeatureFlags: Trying to reset the feature flags to: $initialFeatureFlags")
        applyFeatureFlags(initialFeatureFlags)
        Log.i(TAG, "resetAllFeatureFlags: Performed feature flags reset to: $initialFeatureFlags")
    }

    override var isDeleteSitePermissionsEnabled: Boolean by updatedFeatureFlags::isDeleteSitePermissionsEnabled

    private fun applyFeatureFlags(featureFlags: FeatureFlags) {
        settings.enableHomepageAsNewTab = featureFlags.isHomepageAsNewTabEnabled
        settings.showPocketRecommendationsFeature = featureFlags.isPocketEnabled
        settings.showBookmarksHomeFeature = featureFlags.isBookmarksHomeFeatureEnabled
        settings.showRecentTabsFeature = featureFlags.isRecentTabsFeatureEnabled
        settings.historyMetadataUIFeature = featureFlags.isRecentlyVisitedFeatureEnabled
        settings.showWallpaperOnboarding = featureFlags.isWallpaperOnboardingEnabled
        settings.deleteSitePermissions = featureFlags.isDeleteSitePermissionsEnabled
        settings.shouldShowOpenInAppBanner = featureFlags.isOpenInAppBannerEnabled
        settings.microsurveyFeatureEnabled = featureFlags.isMicrosurveyEnabled
        settings.shouldUseBottomToolbar = featureFlags.shouldUseBottomToolbar
        setETPPolicy(featureFlags.etpPolicy)
        setPermissions(PhoneFeature.LOCATION, featureFlags.isLocationPermissionEnabled)
        settings.onboardingFeatureEnabled = featureFlags.onboardingFeatureEnabled
        settings.hasShownTabSwipeCFR = !featureFlags.isTabSwipeCFREnabled
        settings.hasAcceptedTermsOfService = featureFlags.isTermsOfServiceAccepted
        setOpenLinksInApp(featureFlags.openLinksInApp)
        settings.tabManagerOpeningAnimationEnabled = featureFlags.tabManagerOpeningAnimationEnabled
        settings.shakeToSummarizeToolbarCfrShown = featureFlags.hasSeenShakeToSummarizeToolbarCfr
        settings.shakeToSummarizeFeatureFlagEnabled = featureFlags.shakeToSummarizeFeatureFlagEnabled
        settings.shouldUseExpandedToolbar = featureFlags.shouldUseExpandedToolbar
        settings.isTabStripEnabled = featureFlags.isTabStripEnabled
        settings.nativeShareSheetEnabled = featureFlags.nativeShareSheetEnabled
        settings.showVoiceSearchInDisplayToolbar = featureFlags.showVoiceSearchInDisplayToolbar
        settings.enableHomepageTrendingRecentSearch = featureFlags.isHomepageTrendingRecentSearchEnabled
        settings.showAddressBarInFocusMode = featureFlags.showAddressBarInFocusMode
        settings.crashReportChoice = featureFlags.crashReportOption.label
        settings.googleLensIntegrationEnabled = featureFlags.isGoogleLensIntegrationEnabled
        settings.googleLensIntegrationUserEnabled = featureFlags.isGoogleLensIntegrationUserEnabled
        settings.enableFxSuggest = featureFlags.isFxSuggestEnabled
        settings.enablePdfTools = featureFlags.isPdfToolsEnabled
    }
}

private data class FeatureFlags(
    var isHomepageAsNewTabEnabled: Boolean,
    var isPocketEnabled: Boolean,
    var isBookmarksHomeFeatureEnabled: Boolean,
    var isRecentTabsFeatureEnabled: Boolean,
    var isRecentlyVisitedFeatureEnabled: Boolean,
    var isWallpaperOnboardingEnabled: Boolean,
    var isDeleteSitePermissionsEnabled: Boolean,
    var isOpenInAppBannerEnabled: Boolean,
    var etpPolicy: ETPPolicy,
    var isLocationPermissionEnabled: SitePermissionsRules.Action,
    var isMicrosurveyEnabled: Boolean,
    var shouldUseBottomToolbar: Boolean,
    var onboardingFeatureEnabled: Boolean,
    var isTabSwipeCFREnabled: Boolean,
    var isTermsOfServiceAccepted: Boolean,
    var openLinksInApp: OpenLinksInApp,
    var tabManagerOpeningAnimationEnabled: Boolean,
    var hasSeenShakeToSummarizeToolbarCfr: Boolean,
    var shakeToSummarizeFeatureFlagEnabled: Boolean,
    var shouldUseExpandedToolbar: Boolean,
    var isTabStripEnabled: Boolean,
    var nativeShareSheetEnabled: Boolean,
    var showVoiceSearchInDisplayToolbar: Boolean,
    var isHomepageTrendingRecentSearchEnabled: Boolean,
    var showAddressBarInFocusMode: Boolean,
    var crashReportOption: CrashReportOption,
    var isGoogleLensIntegrationEnabled: Boolean,
    var isGoogleLensIntegrationUserEnabled: Boolean,
    var isFxSuggestEnabled: Boolean,
    var isPdfToolsEnabled: Boolean,
)

internal fun getETPPolicy(settings: Settings): ETPPolicy {
    return when {
        settings.useStrictTrackingProtection -> STRICT
        settings.useCustomTrackingProtection -> CUSTOM
        else -> STANDARD
    }
}

private fun setETPPolicy(policy: ETPPolicy) {
    when (policy) {
        STRICT -> {
            Log.i(TAG, "setETPPolicy: Trying to set ETP policy to: \"Strict\"")
            settings.setStrictETP()
            Log.i(TAG, "setETPPolicy: ETP policy was set to: \"Strict\"")
        }
        // The following two cases update ETP in the same way "setStrictETP" does.
        STANDARD -> {
            Log.i(TAG, "setETPPolicy: Trying to set ETP policy to: \"Standard\"")
            settings.preferences
                .edit()
                .putBoolean(
                    appContext.getPreferenceKey(R.string.pref_key_tracking_protection_strict_default),
                    false,
                )
                .putBoolean(
                    appContext.getPreferenceKey(R.string.pref_key_tracking_protection_custom_option),
                    false,
                )
                .putBoolean(
                    appContext.getPreferenceKey(R.string.pref_key_tracking_protection_standard_option),
                    true,
                )
                .commit()
            Log.i(TAG, "setETPPolicy: ETP policy was set to: \"Standard\"")
        }
        CUSTOM -> {
            Log.i(TAG, "setETPPolicy: Trying to set ETP policy to: \"Custom\"")
            settings.preferences
                .edit()
                .putBoolean(
                    appContext.getPreferenceKey(R.string.pref_key_tracking_protection_strict_default),
                    false,
                )
                .putBoolean(
                    appContext.getPreferenceKey(R.string.pref_key_tracking_protection_standard_option),
                    true,
                )
                .putBoolean(
                    appContext.getPreferenceKey(R.string.pref_key_tracking_protection_custom_option),
                    true,
                )
                .commit()
            Log.i(TAG, "setETPPolicy: ETP policy was set to: \"Custom\"")
        }
    }
}

internal fun getOpenLinksInApp(settings: Settings): OpenLinksInApp {
    return when (settings.openLinksInExternalApp) {
        appContext.getString(R.string.pref_key_open_links_in_apps_always) -> OpenLinksInApp.ALWAYS
        appContext.getString(R.string.pref_key_open_links_in_apps_ask) -> OpenLinksInApp.ASK
        appContext.getString(R.string.pref_key_open_links_in_apps_never) -> OpenLinksInApp.NEVER
        else -> {
            Log.i(
                TAG,
                "getOpenLinksInApp: Unknown preference value found: \"${settings.openLinksInExternalApp}\", defaulting to \"Ask before opening\".",
            )
            OpenLinksInApp.ASK
        }
    }
}

private fun setOpenLinksInApp(value: OpenLinksInApp) {
    val prefValue =
        when (value) {
            OpenLinksInApp.ALWAYS -> appContext.getString(R.string.pref_key_open_links_in_apps_always)
            OpenLinksInApp.ASK -> appContext.getString(R.string.pref_key_open_links_in_apps_ask)
            OpenLinksInApp.NEVER -> appContext.getString(R.string.pref_key_open_links_in_apps_never)
        }
    settings.openLinksInExternalApp = prefValue
    Log.i(TAG, "setOpenLinksInApp: Set the preference to \"$prefValue\".")
}

internal fun getFeaturePermission(feature: PhoneFeature, settings: Settings): SitePermissionsRules.Action {
    Log.i(TAG, "getFeaturePermission: The default permission for $feature is ${feature.getAction(settings)}.")
    return feature.getAction(settings)
}

private fun setPermissions(feature: PhoneFeature, action: SitePermissionsRules.Action) {
    runBlocking {
        Log.i(TAG, "setPermissions: Trying to set $action permission for $feature.")
        appContext.components.settings.setSitePermissionsPhoneFeatureAction(feature, action)
        Log.i(TAG, "setPermissions: Set $action permission for $feature.")
    }
}
