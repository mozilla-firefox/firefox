/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel.store

import androidx.annotation.StringRes
import mozilla.components.browser.state.state.SessionState
import mozilla.components.concept.engine.permission.SitePermissions
import mozilla.components.concept.engine.permission.SitePermissions.AutoplayStatus
import mozilla.components.lib.state.State
import org.mozilla.fenix.R
import org.mozilla.fenix.settings.PhoneFeature
import org.mozilla.fenix.trackingprotection.TrackerBuckets
import org.mozilla.fenix.trackingprotection.TrackingProtectionCategory

typealias WebsitePermissionsState = Map<PhoneFeature, WebsitePermission>

/**
 * Value type that represents the state of the unified trust panel.
 *
 * @property baseDomain The base domain of the current site used to display the clear site data dialog.
 * @property isTrackingProtectionEnabled Flag indicating whether enhanced tracking protection is enabled.
 * @property numberOfTrackersBlocked The numbers of trackers blocked by enhanced tracking protection.
 * @property bucketedTrackers Mapping of trackers sorted into different tracking protection categories.
 * @property detailedTrackerCategory The [TrackingProtectionCategory] which will be shown in the tracker
 * category details panel.
 * @property sessionState The [SessionState] of the current tab.
 * @property sitePermissions The [SitePermissions] that contains the statuses of website permissions
 * for the current site.
 * @property websiteInfoState [State] containing information about the website connection.
 * @property websitePermissionsState Mapping of [PhoneFeature]s to [WebsitePermission]s.
 */
data class TrustPanelState(
    val baseDomain: String? = null,
    val isTrackingProtectionEnabled: Boolean = true,
    val numberOfTrackersBlocked: Int = 0,
    val bucketedTrackers: TrackerBuckets = TrackerBuckets(),
    val detailedTrackerCategory: TrackingProtectionCategory? = null,
    val sessionState: SessionState? = null,
    val sitePermissions: SitePermissions? = null,
    val websiteInfoState: WebsiteInfoState = WebsiteInfoState(),
    val websitePermissionsState: WebsitePermissionsState = mapOf(),
) : State

/**
 * Value type that represents the website connection security state.
 *
 * @property isSecured Whether the website connection is secured or not.
 * @property websiteUrl The URL of the current web page.
 * @property websiteTitle The title of the current web page.
 * @property certificateName the certificate name of the current web page.
 */
data class WebsiteInfoState(
    val isSecured: Boolean = true,
    val websiteUrl: String = "",
    val websiteTitle: String = "",
    val certificateName: String = "",
)

/**
 * Wrapper over a website permission encompassing all its needed state to be rendered on the screen.
 *
 * Contains a limited number of implementations because there is a known, finite number of permissions
 * we need to display to the user.
 *
 * @property isVisible Whether this permission should be shown to the user.
 * @property deviceFeature The Android device feature available for the current website.
 * for the app by the user or not.
 */
sealed class WebsitePermission(
    open val isVisible: Boolean,
    open val deviceFeature: PhoneFeature,
) {
    /**
     * Represents the autoplay permission.
     * @property autoplayValue The currently selected [AutoplayValue] status for autoplay.
     * @property isVisible Whether this permission should be shown to the user.
     * @property deviceFeature The Android device feature available for the current website.
     * for the app by the user or not.
     */
    data class Autoplay(
        val autoplayValue: AutoplayValue,
        override val isVisible: Boolean,
        override val deviceFeature: PhoneFeature,
    ) : WebsitePermission(isVisible, deviceFeature)

    /**
     * Represents a toggleable permission.
     * @property isEnabled Visual indication about whether this permission is *enabled* / *disabled*.
     * @property isBlockedByAndroid Whether the corresponding *dangerous* Android permission is granted
     * for the app by the user or not.
     * @property isVisible Whether this permission should be shown to the user.
     * @property deviceFeature The Android device feature available for the current website.
     */
    data class Toggleable(
        val isEnabled: Boolean,
        val isBlockedByAndroid: Boolean,
        override val isVisible: Boolean,
        override val deviceFeature: PhoneFeature,
    ) : WebsitePermission(isVisible, deviceFeature)
}

/**
 * Represents the different possible autoplay values for a site.
 */
enum class AutoplayValue(
    @StringRes val title: Int,
    val autoplayAudibleStatus: AutoplayStatus,
    val autoplayInaudibleStatus: AutoplayStatus,
) {
    AUTOPLAY_ALLOW_ALL(
        title = R.string.quick_setting_option_autoplay_allowed,
        autoplayAudibleStatus = AutoplayStatus.ALLOWED,
        autoplayInaudibleStatus = AutoplayStatus.ALLOWED,
    ),
    AUTOPLAY_BLOCK_ALL(
        title = R.string.quick_setting_option_autoplay_blocked,
        autoplayAudibleStatus = AutoplayStatus.BLOCKED,
        autoplayInaudibleStatus = AutoplayStatus.BLOCKED,
    ),
    AUTOPLAY_BLOCK_AUDIBLE(
        title = R.string.quick_setting_option_autoplay_block_audio,
        autoplayAudibleStatus = AutoplayStatus.BLOCKED,
        autoplayInaudibleStatus = AutoplayStatus.ALLOWED,
    ),
}
