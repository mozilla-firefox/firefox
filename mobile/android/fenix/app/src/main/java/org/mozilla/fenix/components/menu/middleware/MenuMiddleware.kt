/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.middleware

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import mozilla.components.compose.menu.store.MenuAction
import mozilla.components.compose.menu.store.MenuAction.Init
import mozilla.components.compose.menu.store.MenuAction.Update
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.store.MenuState
import mozilla.components.compose.menu.store.MenuStore
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.fenix.components.menu.MenuBuilder
import org.mozilla.fenix.components.menu.MenuHost
import org.mozilla.fenix.components.menu.MenuItemEventRouter
import org.mozilla.fenix.components.menu.MenuItemsRegistry

/**
 * [MenuStore] middleware keeping the menu up to date and letting the providers of its items handle all user
 * interactions with them.
 *
 * @param menuItemsRegistry Builds the [MenuItemsRegistry] with the items of the menu to show, waiting until they are
 *   known.
 * @param host [MenuHost] for the providers of the menu items to interact with.
 * @param scope [CoroutineScope] tied to the lifetime of the menu, on which to keep it up to date.
 */
class MenuMiddleware(
    private val menuItemsRegistry: suspend () -> MenuItemsRegistry,
    private val host: MenuHost,
    private val scope: CoroutineScope,
) : Middleware<MenuState, MenuAction> {
    private val logger = Logger("MenuMiddleware")
    private var eventRouter: MenuItemEventRouter? = null

    override fun invoke(
        store: Store<MenuState, MenuAction>,
        next: (MenuAction) -> Unit,
        action: MenuAction,
    ) {
        when (action) {
            is Init -> buildMenuStructure(store)
            is MenuEvent -> if (eventRouter?.route(action) != true) logger.warn("No menu item handles $action")
            else -> Unit
        }

        next(action)
    }

    private fun buildMenuStructure(store: Store<MenuState, MenuAction>) = scope.launch {
        val registry = menuItemsRegistry()
        eventRouter = MenuItemEventRouter(registry = registry, host = host)
        MenuBuilder(registry).menuStructure.collect { store.dispatch(Update(it)) }
    }
}
