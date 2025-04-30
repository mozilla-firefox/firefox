/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel.store

import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.state.content.PermissionHighlightsState
import mozilla.components.concept.engine.permission.SitePermissions
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import org.jetbrains.annotations.VisibleForTesting
import org.mozilla.fenix.settings.PhoneFeature
import org.mozilla.fenix.utils.Settings

/**
 * The [Store] for holding the [TrustPanelState] and applying [TrustPanelAction]s.
 */
class TrustPanelStore(
    initialState: TrustPanelState = TrustPanelState(),
    middleware: List<Middleware<TrustPanelState, TrustPanelAction>> = emptyList(),
) : Store<TrustPanelState, TrustPanelAction>(
    initialState = initialState,
    reducer = ::reducer,
    middleware = middleware,
) {
    @Suppress("LongParameterList")
    constructor (
        isTrackingProtectionEnabled: Boolean,
        websiteInfoState: WebsiteInfoState,
        sessionState: SessionState?,
        settings: Settings,
        sitePermissions: SitePermissions?,
        permissionHighlights: PermissionHighlightsState,
        isPermissionBlockedByAndroid: (PhoneFeature) -> Boolean,
        middleware: List<Middleware<TrustPanelState, TrustPanelAction>> = emptyList(),
    ) : this(
        initialState = TrustPanelState(
            isTrackingProtectionEnabled = isTrackingProtectionEnabled,
            sessionState = sessionState,
            sitePermissions = sitePermissions,
            websiteInfoState = websiteInfoState,
            websitePermissionsState = createWebsitePermissionState(
                settings = settings,
                sitePermissions = sitePermissions,
                permissionHighlights = permissionHighlights,
                isPermissionBlockedByAndroid = isPermissionBlockedByAndroid,
            ),
        ),
        middleware = middleware,
    )

    /**
     * Companion containing methods for creating initial [WebsitePermissionsState] and autoplay values.
     */
    companion object {
        /**
         * Construct an initial [WebsitePermissionsState] to be rendered by the Protection Panel
         * containing the permissions requested by the current website.
         *
         * @param settings The application [Settings].
         * @param sitePermissions [SitePermissions]? list of website permissions and their status.
         * @param permissionHighlights [PermissionHighlightsState] used to determine whether a permission
         * should be brought to the user's attention.
         * @param isPermissionBlockedByAndroid Callback invoked to determine whether a permission is blocked
         * by Android.
         */
        @VisibleForTesting
        fun createWebsitePermissionState(
            settings: Settings,
            sitePermissions: SitePermissions?,
            permissionHighlights: PermissionHighlightsState,
            isPermissionBlockedByAndroid: (PhoneFeature) -> Boolean,
        ) = PhoneFeature.entries
            .filterNot { it == PhoneFeature.AUTOPLAY_AUDIBLE || it == PhoneFeature.AUTOPLAY_INAUDIBLE }
            .associateWith { phoneFeature ->
                if (phoneFeature == PhoneFeature.AUTOPLAY) {
                    WebsitePermission.Autoplay(
                        autoplayValue = sitePermissions.toAutoplayValue(),
                        isVisible = sitePermissions != null || permissionHighlights.isAutoPlayBlocking,
                        deviceFeature = phoneFeature,
                    )
                } else {
                    val status = phoneFeature.getStatus(sitePermissions, settings)
                    WebsitePermission.Toggleable(
                        isEnabled = status.isAllowed(),
                        isBlockedByAndroid = isPermissionBlockedByAndroid(phoneFeature),
                        isVisible = sitePermissions != null && status.doNotAskAgain(),
                        deviceFeature = phoneFeature,
                    )
                }
            }

        private fun SitePermissions?.toAutoplayValue() = this?.let { sitePermissions ->
            AutoplayValue.entries.find {
                it.autoplayAudibleStatus == sitePermissions.autoplayAudible &&
                    it.autoplayInaudibleStatus == sitePermissions.autoplayInaudible
            }
        } ?: AutoplayValue.AUTOPLAY_BLOCK_AUDIBLE
    }
}

private fun reducer(state: TrustPanelState, action: TrustPanelAction): TrustPanelState {
    return when (action) {
        is TrustPanelAction.Navigate,
        is TrustPanelAction.ClearSiteData,
        is TrustPanelAction.RequestClearSiteDataDialog,
        is TrustPanelAction.UpdateTrackersBlocked,
        is TrustPanelAction.TogglePermission,
        is TrustPanelAction.UpdateAutoplayValue,
        -> state

        is TrustPanelAction.WebsitePermissionAction -> state.copy(
            websitePermissionsState = WebsitePermissionsStateReducer.reduce(
                state.websitePermissionsState,
                action,
            ),
        )

        is TrustPanelAction.UpdateDetailedTrackerCategory -> state.copy(
            detailedTrackerCategory = action.detailedTrackerCategory,
        )
        is TrustPanelAction.UpdateBaseDomain -> state.copy(
            baseDomain = action.baseDomain,
        )
        is TrustPanelAction.ToggleTrackingProtection -> state.copy(
            isTrackingProtectionEnabled = !state.isTrackingProtectionEnabled,
        )
        is TrustPanelAction.UpdateNumberOfTrackersBlocked -> state.copy(
            numberOfTrackersBlocked = action.newNumberOfTrackersBlocked,
        )
        is TrustPanelAction.UpdateSitePermissions -> state.copy(
            sitePermissions = action.sitePermissions,
        )
    }
}

private object WebsitePermissionsStateReducer {
    /**
     * Handles creating a new [WebsitePermissionsState] based on the
     * specific [TrustPanelAction.WebsitePermissionAction]
     */
    fun reduce(
        state: WebsitePermissionsState,
        action: TrustPanelAction.WebsitePermissionAction,
    ): WebsitePermissionsState {
        val key = action.updatedFeature
        val value = state[key]

        return when (action) {
            is TrustPanelAction.WebsitePermissionAction.GrantPermissionBlockedByAndroid -> {
                val toggleable = value as WebsitePermission.Toggleable
                val newWebsitePermission = toggleable.copy(
                    isBlockedByAndroid = false,
                )

                state + Pair(key, newWebsitePermission)
            }
            is TrustPanelAction.WebsitePermissionAction.TogglePermission -> {
                val toggleable = value as WebsitePermission.Toggleable
                val newWebsitePermission = toggleable.copy(
                    isEnabled = !value.isEnabled,
                )

                state + Pair(key, newWebsitePermission)
            }
            is TrustPanelAction.WebsitePermissionAction.ChangeAutoplay -> {
                val autoplay = value as WebsitePermission.Autoplay
                val newWebsitePermission = autoplay.copy(
                    autoplayValue = action.autoplayValue,
                )
                state + Pair(key, newWebsitePermission)
            }
        }
    }
}
