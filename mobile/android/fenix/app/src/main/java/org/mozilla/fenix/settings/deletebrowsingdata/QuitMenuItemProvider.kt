/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.deletebrowsingdata

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.compose.menu.ui.MenuItemState
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.MenuHost
import org.mozilla.fenix.components.menu.MenuItemProvider
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.store.MenuAction

/**
 * [MenuItemProvider] for the menu item allowing to delete the browsing data of this session and quit the application.
 *
 * Only shown to users who asked for their data to be deleted when they quit.
 *
 * @param appName The name of the application, which the item offers to quit.
 * @param deletesBrowsingDataOnQuit Whether the user asked for their browsing data to be deleted when quitting.
 * @param deleteBrowsingDataController Provides the [DeleteBrowsingDataController] for deleting the data the user wants
 *   gone when they quit the application. Asked for it only if they ever do, since building it is not free.
 * @param quitApplicationDelegate Quits the application, once there is nothing left to delete.
 * @param applicationScope [CoroutineScope] tied to the lifetime of the application, on which to delete the data since
 *   that must complete even after the menu is closed.
 * @param target [MenuTarget] for which this menu item would be shown for.
 */
class QuitMenuItemProvider(
    appName: String,
    deletesBrowsingDataOnQuit: Boolean,
    private val deleteBrowsingDataController: () -> DeleteBrowsingDataController,
    private val quitApplicationDelegate: () -> Unit,
    private val applicationScope: CoroutineScope,
    private val target: MenuTarget,
) : MenuItemProvider {
    override val itemsFlow: StateFlow<List<MenuItem>> =
        if (deletesBrowsingDataOnQuit) {
            MutableStateFlow(
                listOf(
                    when (target) {
                        MenuTarget.CustomizeMenu -> defaultMenuItem(appName)
                        else -> defaultMenuItem(appName).copy(onClickEvent = MenuAction.DeleteBrowsingDataAndQuit)
                    }
                )
            )
        } else {
            MutableStateFlow(emptyList())
        }

    override fun handles(event: MenuEvent) =
        target != MenuTarget.Preview && (event == MenuAction.DeleteBrowsingDataAndQuit)

    /**
     * The data is deleted after the menu is closed, and the application quit only after that. What is needed for this
     * is resolved before closing the menu, and the work outliving the menu uses only that.
     */
    override fun onEvent(event: MenuEvent, menu: MenuHost) {
        val controller = deleteBrowsingDataController()
        val quitApplication = quitApplicationDelegate

        menu.dismiss()

        applicationScope.launch { controller.clearBrowsingDataOnQuit(onDeletionComplete = quitApplication) }
    }

    companion object {
        private fun defaultMenuItem(appName: String) =
            StandardMenuItem(
                title =
                    MenuItemTitle(Text.Resource(R.string.browser_menu_delete_browsing_data_on_quit, listOf(appName))),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_cross_circle_fill_24),
                state = MenuItemState.WARNING,
            )
    }
}
