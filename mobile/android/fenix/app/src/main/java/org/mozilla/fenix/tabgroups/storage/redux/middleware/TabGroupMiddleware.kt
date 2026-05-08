/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups.storage.redux.middleware

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import mozilla.components.browser.state.action.BrowserAction
import mozilla.components.browser.state.action.TabListAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import org.mozilla.fenix.tabgroups.storage.repository.TabGroupRepository

/**
 * Processes [TabListAction]s to keep [TabGroupRepository] synced with [BrowserState].
 *
 * @param tabGroupRepository [TabGroupRepository] used to invoke tab group storage side effects.
 * @param scope [CoroutineScope] used to execute tab group storage side effects.
 */
class TabGroupMiddleware(
    private val tabGroupRepository: TabGroupRepository,
    private val scope: CoroutineScope = CoroutineScope(Dispatchers.IO),
) : Middleware<BrowserState, BrowserAction> {

    override fun invoke(
        store: Store<BrowserState, BrowserAction>,
        next: (BrowserAction) -> Unit,
        action: BrowserAction,
    ) {
        next(action)

        when (action) {
            is TabListAction -> processAction(action)
            else -> {}
        }
    }

    internal fun processAction(action: TabListAction) {
        when (action) {
            TabListAction.RemoveAllNormalTabsAction -> {
                scope.launch {
                    tabGroupRepository.closeAllTabGroups()
                }
            }
            is TabListAction.RemoveAllTabsAction -> {
                scope.launch {
                    tabGroupRepository.closeAllTabGroups()
                }
            }
            is TabListAction.RemoveTabAction -> {
                scope.launch {
                    tabGroupRepository.deleteTabGroupAssignmentById(tabId = action.tabId)
                }
            }
            is TabListAction.RemoveTabsAction -> {
                scope.launch {
                    tabGroupRepository.deleteTabGroupAssignmentsById(tabIds = action.tabIds)
                }
            }
            is TabListAction.AddTabAction -> {
                // Only inherit a group when the new tab has an EXPLICIT parent
                // (e.g. long-press → "Open in new tab" / "Open in background
                // tab"). Plain "new tab" entry points — the toolbar's "+"
                // counter, the tab-tray "+" menu, or homepage-awesome-bar
                // searches — pass no parentId, so they correctly produce
                // ungrouped tabs even when the active tab is in a group.
                if (action.tab.content.private) return
                val parentTabId = action.tab.parentId ?: return
                scope.launch {
                    val parentGroupId = tabGroupRepository.fetchTabGroupAssignments()[parentTabId]
                        ?: return@launch
                    tabGroupRepository.addTabGroupAssignment(
                        tabId = action.tab.id,
                        tabGroupId = parentGroupId,
                    )
                }
            }

            is TabListAction.AddMultipleTabsAction,
            is TabListAction.MoveTabsAction,
            is TabListAction.RestoreAction,
            is TabListAction.SelectTabAction,
            is TabListAction.RemoveAllPrivateTabsAction, // Tab groups are not supported in Private browsing
                -> {} // no-op
        }
    }
}
