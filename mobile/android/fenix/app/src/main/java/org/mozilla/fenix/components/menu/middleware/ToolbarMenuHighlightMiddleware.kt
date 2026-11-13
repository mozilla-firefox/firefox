/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.middleware

import mozilla.components.compose.menu.store.MenuAction
import mozilla.components.compose.menu.store.MenuAction.Init
import mozilla.components.compose.menu.store.MenuState
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.SupportedMenuNotifications
import org.mozilla.fenix.components.menu.MenuAccessPoint
import org.mozilla.fenix.summarization.onboarding.SummarizationFeatureDiscoveryConfiguration
import org.mozilla.fenix.summarization.onboarding.SummarizeDiscoveryEvent
import org.mozilla.fenix.tabgroups.TabGroupsFeatureDiscoveryConfiguration

/**
 * Records that the menu was opened from the browser toolbar for every feature currently highlighting the toolbar menu
 * button, so those highlights get dismissed.
 *
 * @param accessPoint The [MenuAccessPoint] the menu was opened from.
 * @param isRestored Whether this is an already open menu being recreated, e.g. after a theme change, rather than the
 *   user opening it.
 * @param appStore [AppStore] holding the features currently highlighting the toolbar menu button.
 * @param tabGroupsDiscovery Feature discovery configuration of tab groups.
 * @param summarizeDiscovery Feature discovery configuration of page summarization.
 */
class ToolbarMenuHighlightMiddleware(
    private val accessPoint: MenuAccessPoint,
    private val isRestored: Boolean,
    private val appStore: AppStore,
    private val tabGroupsDiscovery: TabGroupsFeatureDiscoveryConfiguration,
    private val summarizeDiscovery: SummarizationFeatureDiscoveryConfiguration,
) : Middleware<MenuState, MenuAction> {
    override fun invoke(store: Store<MenuState, MenuAction>, next: (MenuAction) -> Unit, action: MenuAction) {
        if (action is Init && !isRestored && accessPoint == MenuAccessPoint.Browser) {
            recordToolbarMenuOpened()
        }

        next(action)
    }

    private fun recordToolbarMenuOpened() {
        val notifications = appStore.state.supportedMenuNotifications
        if (SupportedMenuNotifications.TabGroups in notifications) {
            tabGroupsDiscovery.recordToolbarOpened()
        }
        if (SupportedMenuNotifications.Summarize in notifications) {
            summarizeDiscovery.cacheDiscoveryEvent(SummarizeDiscoveryEvent.ToolbarOverflowInteraction)
        }
    }
}
