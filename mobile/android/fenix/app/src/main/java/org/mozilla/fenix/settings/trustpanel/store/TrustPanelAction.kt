/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel.store

import java.security.cert.X509Certificate
import mozilla.components.concept.engine.content.blocking.TrackerLog
import mozilla.components.concept.engine.permission.SitePermissions
import mozilla.components.lib.state.Action
import org.mozilla.fenix.components.menu.store.IPProtectionMenuState
import org.mozilla.fenix.settings.PhoneFeature
import org.mozilla.fenix.trackingprotection.TrackingProtectionCategory

/** Actions to dispatch through the [TrustPanelStore] to modify the [TrustPanelState]. */
sealed class TrustPanelAction : Action {

    /** [TrustPanelAction] dispatched when the current site's data is cleared. */
    data object ClearSiteData : TrustPanelAction()

    /** [TrustPanelAction] dispatched when tracking protection is toggled. */
    data object ToggleTrackingProtection : TrustPanelAction()

    /** [TrustPanelAction] dispatched when the clear site data dialog is requested. */
    data object RequestClearSiteDataDialog : TrustPanelAction()

    /**
     * [TrustPanelAction] dispatched when the site's base domain is updated for the clear site data dialog.
     *
     * @property baseDomain The base domain of the current site.
     */
    data class UpdateBaseDomain(val baseDomain: String) : TrustPanelAction()

    /**
     * [TrustPanelAction] dispatched when the detailed tracker category is updated for the tracker category details
     * panel.
     *
     * @property detailedTrackerCategory The [TrackingProtectionCategory] for which detailed information should be
     *   displayed.
     */
    data class UpdateDetailedTrackerCategory(val detailedTrackerCategory: TrackingProtectionCategory) :
        TrustPanelAction()

    /**
     * [TrustPanelAction] dispatched when the number of blocked trackers is updated.
     *
     * @property newNumberOfTrackersBlocked Updated number of trackers that have been blocked
     */
    data class UpdateNumberOfTrackersBlocked(val newNumberOfTrackersBlocked: Int) : TrustPanelAction()

    /**
     * [TrustPanelAction] dispatched when the list of trackers is changed.
     *
     * @property trackerLogs The new list of blocked [TrackerLog]s.
     */
    data class UpdateTrackersBlocked(val trackerLogs: List<TrackerLog>) : TrustPanelAction()

    /**
     * [TrustPanelAction] dispatched when any site permission is changed.
     *
     * @property sitePermissions Updated [SitePermissions] for the current site.
     */
    data class UpdateSitePermissions(val sitePermissions: SitePermissions) : TrustPanelAction()

    /**
     * [TrustPanelAction] dispatched when the IP protection menu state is updated.
     *
     * @property state The updated [IPProtectionMenuState].
     */
    data class UpdateIPProtectionMenuState(val state: IPProtectionMenuState) : TrustPanelAction()

    /** [TrustPanelAction] dispatched when QWAC status is requested. */
    data object RequestQWAC : TrustPanelAction()

    /**
     * [TrustPanelAction] dispatched when QWAC status is determined.
     *
     * @property qwac The QWAC ([X509Certificate]) in use, or null if none.
     * @property forCertificate The certificate of the web page when the QWAC status was requested.
     */
    data class UpdateQWAC(val qwac: X509Certificate?, val forCertificate: X509Certificate?) : TrustPanelAction()

    /**
     * [TrustPanelAction] dispatched when the security info of the current web page changes.
     *
     * @property isSecured Whether the website connection is secured or not.
     * @property certificate The certificate presented by the current web page, or null if none.
     */
    data class UpdateSecurityInfo(
        val isSecured: Boolean,
        val certificate: X509Certificate?,
    ) : TrustPanelAction()

    /**
     * [TrustPanelAction] dispatched when a toggleable permission is toggled.
     *
     * @property permission Requested [WebsitePermission.Toggleable] to be toggled.
     */
    data class TogglePermission(val permission: WebsitePermission.Toggleable) : TrustPanelAction()

    /**
     * [TrustPanelAction] dispatched when autoplay value is updated.
     *
     * @property autoplayValue Requested [AutoplayValue] to be selected.
     */
    data class UpdateAutoplayValue(val autoplayValue: AutoplayValue) : TrustPanelAction()

    /**
     * All possible [WebsitePermissionsState] changes as result of user / system interactions.
     *
     * @property updatedFeature [PhoneFeature] backing a certain [WebsitePermission].
     */
    sealed class WebsitePermissionAction(open val updatedFeature: PhoneFeature) : TrustPanelAction() {
        /**
         * Change resulting from a previously blocked [WebsitePermission] being granted permission by Android.
         *
         * @property updatedFeature [PhoneFeature] backing a certain [WebsitePermission]. Allows to easily identify
         *   which permission changed
         */
        class GrantPermissionBlockedByAndroid(override val updatedFeature: PhoneFeature) :
            WebsitePermissionAction(updatedFeature)

        /**
         * Change resulting from toggling a specific [WebsitePermission] for the current website.
         *
         * @property updatedFeature [PhoneFeature] backing a certain [WebsitePermission]. Allows to easily identify
         *   which permission changed
         */
        class TogglePermission(override val updatedFeature: PhoneFeature) : WebsitePermissionAction(updatedFeature)

        /**
         * Change resulting from changing a specific [WebsitePermission.Autoplay] for the current website.
         *
         * @property autoplayValue [AutoplayValue] backing a certain [WebsitePermission.Autoplay]. Allows to easily
         *   identify which permission changed
         */
        class ChangeAutoplay(val autoplayValue: AutoplayValue) : WebsitePermissionAction(PhoneFeature.AUTOPLAY)
    }

    /** [TrustPanelAction] dispatched when a navigation event occurs for a specific destination. */
    sealed class Navigate : TrustPanelAction() {
        /** [Navigate] action dispatched when navigating to the privacy and security settings. */
        data object PrivacySecuritySettings : Navigate()

        /**
         * [Navigate] action dispatched when navigating to the manage phone feature fragment.
         *
         * @property phoneFeature Requested [PhoneFeature] to be managed.
         */
        data class ManagePhoneFeature(val phoneFeature: PhoneFeature) : Navigate()

        /** [Navigate] action dispatched when navigating to view the security certificate. */
        data object SecurityCertificate : Navigate()

        /** [Navigate] action dispatched when navigating to view the QWAC. */
        data object QWAC : Navigate()

        /** [Navigate] action dispatched when showing the protections dashboard. */
        data object TrackersProtectionDashboard : Navigate()

        /** [Navigate] action dispatched when navigating to the IP protection settings screen. */
        data object IPProtectionSettings : Navigate()
    }
}
