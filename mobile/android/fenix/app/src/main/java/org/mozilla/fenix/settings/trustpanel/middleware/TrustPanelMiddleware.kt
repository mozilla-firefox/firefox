/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel.middleware

import androidx.activity.result.ActivityResultLauncher
import androidx.core.net.toUri
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.content.blocking.TrackerLog
import mozilla.components.concept.engine.permission.SitePermissions
import mozilla.components.feature.session.SessionUseCases
import mozilla.components.feature.session.TrackingProtectionUseCases
import mozilla.components.lib.publicsuffixlist.PublicSuffixList
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.MiddlewareContext
import mozilla.components.lib.state.Store
import mozilla.components.support.ktx.kotlin.getOrigin
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.PermissionStorage
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.settings.toggle
import org.mozilla.fenix.settings.trustpanel.store.AutoplayValue
import org.mozilla.fenix.settings.trustpanel.store.TrustPanelAction
import org.mozilla.fenix.settings.trustpanel.store.TrustPanelState
import org.mozilla.fenix.settings.trustpanel.store.WebsitePermission
import org.mozilla.fenix.settings.trustpanel.store.WebsitePermissionsState
import org.mozilla.fenix.trackingprotection.TrackerBuckets
import org.mozilla.fenix.trackingprotection.TrackingProtectionCategory
import org.mozilla.fenix.utils.Settings

/**
 * [Middleware] implementation for handling [TrustPanelAction] and managing the [TrustPanelState] for the menu
 * dialog.
 *
 * @param appStore The [AppStore] used to dispatch actions to display a snackbar.
 * @param engine The browser [Engine] used to clear site data.
 * @param publicSuffixList The [PublicSuffixList] used to obtain the base domain of the current site.
 * @param sessionUseCases [SessionUseCases] used to reload the page after toggling tracking protection.
 * @param trackingProtectionUseCases [TrackingProtectionUseCases] used to add/remove sites from the
 * tracking protection exceptions list.
 * @param settings Used to obtain site permission information when the current site's [SitePermissions]
 * is not available.
 * @param permissionStorage The [PermissionStorage] used to update permission changes for the current site.
 * @param requestPermissionsLauncher Used to execute an ActivityResultContract to request runtime permissions.
 * @param onDismiss Callback invoked to dismiss the trust panel.
 * @param scope [CoroutineScope] used to launch coroutines.
 */
@Suppress("LongParameterList")
class TrustPanelMiddleware(
    private val appStore: AppStore,
    private val engine: Engine,
    private val publicSuffixList: PublicSuffixList,
    private val sessionUseCases: SessionUseCases,
    private val trackingProtectionUseCases: TrackingProtectionUseCases,
    private val settings: Settings,
    private val permissionStorage: PermissionStorage,
    private val requestPermissionsLauncher: ActivityResultLauncher<Array<String>>,
    private val onDismiss: suspend () -> Unit,
    private val scope: CoroutineScope = CoroutineScope(Dispatchers.IO),
) : Middleware<TrustPanelState, TrustPanelAction> {

    override fun invoke(
        context: MiddlewareContext<TrustPanelState, TrustPanelAction>,
        next: (TrustPanelAction) -> Unit,
        action: TrustPanelAction,
    ) {
        val currentState = context.state
        val store = context.store

        when (action) {
            is TrustPanelAction.ClearSiteData -> clearSiteData(currentState)
            is TrustPanelAction.RequestClearSiteDataDialog -> requestClearSiteDataDialog(currentState, store)
            TrustPanelAction.ToggleTrackingProtection -> toggleTrackingProtection(currentState)
            is TrustPanelAction.UpdateTrackersBlocked,
            -> updateTrackersBlocked(currentState, action.trackerLogs, store)
            is TrustPanelAction.TogglePermission,
            -> togglePermission(currentState, action.permission, store)
            is TrustPanelAction.UpdateAutoplayValue,
            -> updateAutoplayValue(currentState, action.autoplayValue, store)

            else -> Unit
        }

        next(action)
    }

    private fun toggleTrackingProtection(
        currentState: TrustPanelState,
    ) = scope.launch {
        currentState.sessionState?.let { session ->
            if (currentState.isTrackingProtectionEnabled) {
                trackingProtectionUseCases.addException(session.id)
            } else {
                trackingProtectionUseCases.removeException(session.id)
            }

            sessionUseCases.reload.invoke(session.id)
        }
    }

    private fun updateTrackersBlocked(
        currentState: TrustPanelState,
        newTrackersBlocked: List<TrackerLog>,
        store: Store<TrustPanelState, TrustPanelAction>,
    ) = scope.launch {
        currentState.bucketedTrackers.updateIfNeeded(newTrackersBlocked)
        store.dispatch(
            TrustPanelAction.UpdateNumberOfTrackersBlocked(currentState.bucketedTrackers.numberOfTrackersBlocked()),
        )
    }

    private fun requestClearSiteDataDialog(
        currentState: TrustPanelState,
        store: Store<TrustPanelState, TrustPanelAction>,
    ) = scope.launch {
        val host = currentState.sessionState?.content?.url?.toUri()?.host.orEmpty()
        val domain = publicSuffixList.getPublicSuffixPlusOne(host).await()

        domain?.let { baseDomain ->
            store.dispatch(TrustPanelAction.UpdateBaseDomain(baseDomain))
        }
    }

    private fun clearSiteData(
        currentState: TrustPanelState,
    ) = scope.launch {
        currentState.baseDomain?.let {
            engine.clearData(
                host = it,
                data = Engine.BrowsingData.select(
                    Engine.BrowsingData.AUTH_SESSIONS,
                    Engine.BrowsingData.ALL_SITE_DATA,
                ),
            )

            appStore.dispatch(AppAction.SiteDataCleared)
        }

        onDismiss()
    }

    private fun togglePermission(
        currentState: TrustPanelState,
        permission: WebsitePermission.Toggleable,
        store: Store<TrustPanelState, TrustPanelAction>,
    ) = scope.launch {
        if (permission.isBlockedByAndroid) {
            requestPermissionsLauncher.launch(permission.deviceFeature.androidPermissionsList)
            return@launch
        }

        if (currentState.sitePermissions == null) {
            store.dispatch(TrustPanelAction.Navigate.ManagePhoneFeature(permission.deviceFeature))
            return@launch
        }

        currentState.sessionState?.let { session ->
            val updatedSitePermissions = currentState.sitePermissions.toggle(permission.deviceFeature)
            permissionStorage.updateSitePermissions(
                sitePermissions = updatedSitePermissions,
                private = session.content.private,
            )

            store.dispatch(TrustPanelAction.UpdateSitePermissions(updatedSitePermissions))
            store.dispatch(TrustPanelAction.WebsitePermissionAction.TogglePermission(permission.deviceFeature))

            sessionUseCases.reload.invoke(session.id)
        }
    }

    private fun updateAutoplayValue(
        currentState: TrustPanelState,
        autoplayValue: AutoplayValue,
        store: Store<TrustPanelState, TrustPanelAction>,
    ) = scope.launch {
        if (currentState.websitePermissionsState.getAutoplayValue() == autoplayValue) {
            return@launch
        }

        currentState.sessionState?.let { session ->
            val updatedSitePermissions: SitePermissions

            if (currentState.sitePermissions == null) {
                val origin = requireNotNull(session.content.url.getOrigin()) {
                    "An origin is required to change a autoplay settings from the door hanger"
                }
                updatedSitePermissions = settings.getSitePermissionsCustomSettingsRules().toSitePermissions(origin)
                    .updateAutoplayPermissions(autoplayValue)
                permissionStorage.add(
                    sitePermissions = updatedSitePermissions,
                    private = session.content.private,
                )
            } else {
                updatedSitePermissions = currentState.sitePermissions.updateAutoplayPermissions(autoplayValue)
                permissionStorage.updateSitePermissions(
                    sitePermissions = updatedSitePermissions,
                    private = session.content.private,
                )
            }

            store.dispatch(TrustPanelAction.UpdateSitePermissions(updatedSitePermissions))
            store.dispatch(TrustPanelAction.WebsitePermissionAction.ChangeAutoplay(autoplayValue))

            sessionUseCases.reload.invoke(session.id)
        }
    }

    private fun SitePermissions.updateAutoplayPermissions(autoplayValue: AutoplayValue) = this.copy(
        autoplayAudible = autoplayValue.autoplayAudibleStatus,
        autoplayInaudible = autoplayValue.autoplayInaudibleStatus,
    )

    private fun WebsitePermissionsState.getAutoplayValue() =
        (this[org.mozilla.fenix.settings.PhoneFeature.AUTOPLAY] as? WebsitePermission.Autoplay)?.autoplayValue
}

private fun TrackerBuckets.numberOfTrackersBlocked() = TrackingProtectionCategory.entries
    .sumOf { trackingProtectionCategory -> this.get(trackingProtectionCategory, true).size }
