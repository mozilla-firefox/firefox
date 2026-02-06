/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.debugsettings.store

import mozilla.components.lib.state.Action
import org.mozilla.fenix.debugsettings.autofill.AutofillTools
import org.mozilla.fenix.debugsettings.gleandebugtools.ui.GleanDebugToolsScreen
import org.mozilla.fenix.debugsettings.ui.DebugDrawerHome
import org.mozilla.fenix.debugsettings.addresses.AddressesTools as AddressesScreen
import org.mozilla.fenix.debugsettings.cfrs.CfrTools as CfrToolsScreen
import org.mozilla.fenix.debugsettings.logins.LoginsTools as LoginsScreen
import org.mozilla.fenix.debugsettings.region.RegionTools as RegionScreen
import org.mozilla.fenix.debugsettings.tabs.TabTools as TabToolsScreen

/**
 * [Action] implementation related to [DebugDrawerStore].
 */
sealed class DebugDrawerAction : Action {

    /**
     * [DebugDrawerAction] fired when the user opens the drawer.
     */
    object DrawerOpened : DebugDrawerAction()

    /**
     * [DebugDrawerAction] fired when the user closes the drawer.
     */
    object DrawerClosed : DebugDrawerAction()

    /**
     * [DebugDrawerAction] fired when a navigation event occurs for a specific destination.
     */
    sealed class NavigateTo : DebugDrawerAction() {

        /**
         * [NavigateTo] action fired when the debug drawer needs to navigate to [DebugDrawerHome].
         */
        object Home : NavigateTo()

        /**
         * [NavigateTo] action fired when the debug drawer needs to navigate to [TabToolsScreen].
         */
        object TabTools : NavigateTo()

        /**
         * [NavigateTo] action fired when the debug drawer needs to navigate to [LoginsScreen].
         */
        object Logins : NavigateTo()

        /**
         * [NavigateTo] action fired when the debug drawer needs to navigate to [AddressesScreen].
         */
        data object Addresses : NavigateTo()

        /**
         * [NavigateTo] action fired when the debug drawer needs to navigate to [CreditCardsTools].
         */
        data object CreditCards : NavigateTo()

        /**
         * [NavigateTo] action fired when the debug drawer needs to navigate to [AutofillTools].
         */
        object Autofill : NavigateTo()

        /**
         * [NavigateTo] action fired when the debug drawer needs to navigate to [CfrToolsScreen].
         */
        object CfrTools : NavigateTo()

        /**
         * [NavigateTo] action fired when the debug drawer needs to navigate to [GleanDebugToolsScreen].
         */
        object GleanDebugTools : NavigateTo()

        /**
         * [NavigateTo] action fired when the debug drawer needs to navigate to [RegionScreen].
         */
        object RegionDebugTools : NavigateTo()

        /**
         * [NavigateTo] action fired when the debug drawer needs to navigate to [AddonsDebugToolsScreen].
         */
        object AddonsDebugTools : NavigateTo()

        /**
         * [NavigateTo] action fired when the debug drawer needs to navigate to [CrashTools].
         */
        object CrashDebugTools : NavigateTo()

        /**
         * [NavigateTo] action fired when the debug drawer needs to navigate to [IntegrityTools].
         */
        object IntegrityDebugTools : NavigateTo()
    }

    /**
     * [DebugDrawerAction] fired when a back navigation event occurs.
     */
    object OnBackPressed : DebugDrawerAction()
}
