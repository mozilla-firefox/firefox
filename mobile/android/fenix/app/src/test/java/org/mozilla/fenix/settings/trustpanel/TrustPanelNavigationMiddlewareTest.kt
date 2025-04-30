/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel

import androidx.navigation.NavController
import io.mockk.mockk
import io.mockk.verify
import kotlinx.coroutines.test.runTest
import mozilla.components.support.test.rule.MainCoroutineRule
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.settings.PhoneFeature
import org.mozilla.fenix.settings.trustpanel.middleware.TrustPanelNavigationMiddleware
import org.mozilla.fenix.settings.trustpanel.store.TrustPanelAction
import org.mozilla.fenix.settings.trustpanel.store.TrustPanelState
import org.mozilla.fenix.settings.trustpanel.store.TrustPanelStore

class TrustPanelNavigationMiddlewareTest {

    @get:Rule
    val coroutinesTestRule = MainCoroutineRule()
    private val scope = coroutinesTestRule.scope

    private val navController: NavController = mockk(relaxed = true)

    @Test
    fun `WHEN navigate to privacy security settings action is dispatched THEN navigate to privacy and security settings`() = runTest {
        val privacySecurityPrefKey = "pref_key_privacy_security_category"
        val store = createStore(privacySecurityPrefKey = privacySecurityPrefKey)
        store.dispatch(TrustPanelAction.Navigate.PrivacySecuritySettings).join()

        verify {
            navController.nav(
                R.id.trustPanelFragment,
                TrustPanelFragmentDirections.actionGlobalSettingsFragment(
                    preferenceToScrollTo = privacySecurityPrefKey,
                ),
            )
        }
    }

    @Test
    fun `WHEN navigate to manage phone feature is dispatched THEN navigate to manage phone feature`() = runTest {
        val store = createStore()
        store.dispatch(TrustPanelAction.Navigate.ManagePhoneFeature(PhoneFeature.CAMERA)).join()

        verify {
            navController.nav(
                R.id.trustPanelFragment,
                TrustPanelFragmentDirections.actionGlobalSitePermissionsManagePhoneFeature(PhoneFeature.CAMERA),
            )
        }
    }

    private fun createStore(
        trustPanelState: TrustPanelState = TrustPanelState(),
        privacySecurityPrefKey: String = "",
    ) = TrustPanelStore(
        initialState = trustPanelState,
        middleware = listOf(
            TrustPanelNavigationMiddleware(
                navController = navController,
                privacySecurityPrefKey = privacySecurityPrefKey,
                scope = scope,
            ),
        ),
    )
}
