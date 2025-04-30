/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel.middleware

import androidx.navigation.NavController
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.MiddlewareContext
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.settings.trustpanel.TrustPanelFragmentDirections
import org.mozilla.fenix.settings.trustpanel.store.TrustPanelAction
import org.mozilla.fenix.settings.trustpanel.store.TrustPanelState
import org.mozilla.fenix.settings.trustpanel.store.TrustPanelStore

/**
 * [Middleware] implementation for handling navigating events based on [TrustPanelAction]s that are
 * dispatched to the [TrustPanelStore].
 *
 * @param navController [NavController] used for navigation.
 * @param privacySecurityPrefKey Preference key used to scroll to the Privacy and security category within settings.
 * @param scope [CoroutineScope] used to launch coroutines.
 */
class TrustPanelNavigationMiddleware(
    private val navController: NavController,
    private val privacySecurityPrefKey: String,
    private val scope: CoroutineScope = CoroutineScope(Dispatchers.Main),
) : Middleware<TrustPanelState, TrustPanelAction> {

    @Suppress("CyclomaticComplexMethod", "LongMethod")
    override fun invoke(
        context: MiddlewareContext<TrustPanelState, TrustPanelAction>,
        next: (TrustPanelAction) -> Unit,
        action: TrustPanelAction,
    ) {
        next(action)

        scope.launch {
            when (action) {
                is TrustPanelAction.Navigate.PrivacySecuritySettings -> navController.nav(
                    R.id.trustPanelFragment,
                    TrustPanelFragmentDirections.actionGlobalSettingsFragment(
                        preferenceToScrollTo = privacySecurityPrefKey,
                    ),
                )

                is TrustPanelAction.Navigate.ManagePhoneFeature -> navController.nav(
                    R.id.trustPanelFragment,
                    TrustPanelFragmentDirections.actionGlobalSitePermissionsManagePhoneFeature(action.phoneFeature),
                )

                else -> Unit
            }
        }
    }
}
