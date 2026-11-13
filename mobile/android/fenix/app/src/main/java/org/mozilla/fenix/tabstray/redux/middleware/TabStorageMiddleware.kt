/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.redux.middleware

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.launch
import mozilla.components.feature.tabdata.coordinator.TabDataCoordinator
import mozilla.components.feature.tabdata.coordinator.data.findTab
import mozilla.components.feature.tabdata.coordinator.data.findTabGroup
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.tabstray.data.TabGroupTheme
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.data.toTabStorageUpdate
import org.mozilla.fenix.tabstray.redux.action.TabGroupAction
import org.mozilla.fenix.tabstray.redux.action.TabsTrayAction
import org.mozilla.fenix.tabstray.redux.action.TabsTrayAction.InitAction
import org.mozilla.fenix.tabstray.redux.action.TabsTrayAction.TabDataUpdateReceived
import org.mozilla.fenix.tabstray.redux.action.TabsTrayAction.TabsStorageAction
import org.mozilla.fenix.tabstray.redux.state.TabGroupFormState
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState

/** The kinds of item a drag and drop gesture can start from or land on. */
private enum class GestureItem {
    Tab,
    TabGroup,
    Unknown,
}

/**
 * [Middleware] that reacts to [TabsTrayAction] and performs tab data side effects.
 *
 * @param tabDataCoordinator The [TabDataCoordinator] used to observe and update tab data.
 * @param fenixBrowserUseCases The [FenixBrowserUseCases] used to add a homepage tab for starter tab groups.
 * @param scope The [CoroutineScope] for running the tab data updates off of the main thread.
 * @param mainScope The [CoroutineScope] used for returning to the main thread.
 */
class TabStorageMiddleware(
    private val tabDataCoordinator: TabDataCoordinator,
    private val fenixBrowserUseCases: FenixBrowserUseCases,
    private val scope: CoroutineScope = CoroutineScope(Dispatchers.Default),
    private val mainScope: CoroutineScope = CoroutineScope(Dispatchers.Main),
) : Middleware<TabsTrayState, TabsTrayAction> {

    private val logger = Logger(tag = "TabStorageMiddleware")

    override fun invoke(
        store: Store<TabsTrayState, TabsTrayAction>,
        next: (TabsTrayAction) -> Unit,
        action: TabsTrayAction,
    ) {
        when (action) {
            is TabsStorageAction ->
                processAction(
                    action = action,
                    store = store,
                )

            else -> {}
        }

        next(action)
    }

    private fun processAction(
        action: TabsStorageAction,
        store: Store<TabsTrayState, TabsTrayAction>,
    ) {
        when (action) {
            InitAction -> {
                // Set up the tab data observer and set the Flow collection to the lifetime of main scope
                mainScope.launch {
                    tabDataCoordinator.tabsSnapshot.filterNotNull().collect { snapshot ->
                        scope.launch {
                            val tabStorageUpdate = snapshot.toTabStorageUpdate()

                            mainScope.launch {
                                store.dispatch(TabDataUpdateReceived(tabStorageUpdate = tabStorageUpdate))
                            }
                        }
                    }
                }
            }

            TabGroupAction.SaveClicked -> handleSaveClicked(store)

            is TabGroupAction.SelectedTabsAddedToGroup -> {
                val selectedTabIds = store.state.mode.selectedTabs.map { it.id }
                val selectedTabGroupIds = store.state.mode.selectedTabGroups.map { it.id } - action.groupId

                scope.launch {
                    tabDataCoordinator.addTabsToTabGroup(tabGroupId = action.groupId, tabIds = selectedTabIds)

                    // If group(s) were merged, delete them, but do NOT delete the destination group if it was also
                    // selected.
                    if (selectedTabGroupIds.isNotEmpty()) {
                        tabDataCoordinator.deleteTabGroupsById(tabGroupIds = selectedTabGroupIds)
                    }
                }
            }

            is TabGroupAction.DeleteConfirmed ->
                scope.launch { tabDataCoordinator.deleteTabGroupAndTabs(tabGroupId = action.group.id) }

            is TabGroupAction.CloseTabAndDeleteGroupConfirmed ->
                scope.launch { tabDataCoordinator.deleteTabGroupAndTabs(tabGroupId = action.group.id) }

            is TabGroupAction.UngroupConfirmed ->
                scope.launch { tabDataCoordinator.ungroupTabGroup(tabGroupId = action.group.id) }

            is TabGroupAction.DragAndDropInitiated -> handleDragAndDrop(action = action, store = store)

            is TabGroupAction.OpenTabGroupClicked ->
                scope.launch { tabDataCoordinator.openTabGroup(tabGroupId = action.group.id) }

            is TabGroupAction.CloseTabGroupClicked ->
                scope.launch { tabDataCoordinator.closeTabGroup(tabGroupId = action.group.id) }

            is TabsTrayAction.ReorderTabsTrayItem -> {
                scope.launch {
                    tabDataCoordinator.reorderItem(
                        sourceId = action.sourceId,
                        destinationId = action.destinationId,
                        placeAfter = action.placeAfter,
                    )
                }
            }

            is TabGroupAction.TabClosed -> {
                scope.launch {
                    tabDataCoordinator.removeTabFromTabGroup(tabGroupId = action.group.id, tabId = action.tab.id)
                }
            }

            // todo-bug-2069535: Hook up to the coordinator storage layer when it is ready
            is TabGroupAction.TabAddedToNewTabGroup -> {
                scope.launch {}
            }

            is TabGroupAction.TabAddedToExistingTabGroup -> {
                scope.launch {
                    tabDataCoordinator.addTabsToTabGroup(tabGroupId = action.groupId, tabIds = listOf(action.tabId))
                }
            }

            is TabGroupAction.TabMovedToExistingTabGroup -> {
                scope.launch {
                    tabDataCoordinator.addTabsToTabGroup(tabGroupId = action.groupId, tabIds = listOf(action.tabId))
                }
            }
        }
    }

    /**
     * Handles the drag and drop action based on the source and target types.
     *
     * @param action: The DragAndDropInitiated action
     * @param store: The TabsTrayStore
     */
    private fun handleDragAndDrop(
        action: TabGroupAction.DragAndDropInitiated,
        store: Store<TabsTrayState, TabsTrayAction>,
    ) {
        scope.launch {
            val source = gestureItemFor(id = action.sourceId)
            val target = gestureItemFor(id = action.destinationId)
            val areSourceAndTargetTabs = source == GestureItem.Tab && target == GestureItem.Tab
            try {
                when {
                    // Source and target are tabs
                    areSourceAndTargetTabs -> {
                        mainScope.launch {
                            store.dispatch(
                                TabGroupAction.DragAndDropTwoTabs(
                                    sourceTabId = action.sourceId,
                                    destinationTabId = action.destinationId,
                                )
                            )
                        }
                    }
                    // Source and target are groups
                    source == GestureItem.TabGroup && target == GestureItem.TabGroup -> {
                        tabDataCoordinator.mergeTabGroups(
                            sourceTabGroupId = action.sourceId,
                            destinationTabGroupId = action.destinationId,
                        )
                    }
                    // Source is tab, target is group
                    source == GestureItem.Tab && target == GestureItem.TabGroup -> {
                        tabDataCoordinator.addTabsToTabGroup(
                            tabGroupId = action.destinationId,
                            tabIds = listOf(action.sourceId),
                        )
                    }
                    // Source is group, target is tab
                    source == GestureItem.TabGroup && target == GestureItem.Tab -> {
                        tabDataCoordinator.mergeTabGroupIntoTab(
                            tabGroupId = action.sourceId,
                            tabId = action.destinationId,
                        )
                    }

                    else -> {
                        logger.warn(
                            "DragAndDropInitiated:  Source or target not found or unsupported.  No action taken."
                        )
                    }
                }
            } catch (e: CancellationException) {
                // Don't swallow cancellation exceptions
                throw e
            } catch (@Suppress("TooGenericExceptionCaught") e: Exception) {
                // There are a broad variety of exceptions that Room could throw here
                logger.error(message = "DragAndDropInitiated:  Storage layer failure.", throwable = e)
            } finally {
                // Signal that drop handling is finished except for the tab->tab case, which goes into edit flow
                // The drag should be released even if the storage layer encounters an exception
                if (!areSourceAndTargetTabs) {
                    mainScope.launch {
                        store.dispatch(TabGroupAction.DragAndDropProcessed)
                    }
                }
            }
        }
    }

    private fun gestureItemFor(id: String): GestureItem {
        val snapshot = tabDataCoordinator.tabsSnapshot.value ?: return GestureItem.Unknown

        return when {
            snapshot.findTabGroup(id = id) != null -> GestureItem.TabGroup
            snapshot.findTab(id = id) != null -> GestureItem.Tab
            else -> GestureItem.Unknown
        }
    }

    private fun handleSaveClicked(store: Store<TabsTrayState, TabsTrayAction>) {
        val formState = store.state.tabGroupState.formState ?: return
        val mode = store.state.mode

        if (formState.isStarterTabGroup) {
            createStarterTabGroup(name = formState.name, theme = formState.theme, store = store)
            return
        }

        // Capture the selection synchronously to prevent selection loss by the Reducer
        val selectedTabIds = mode.selectedTabs.map { it.id }
        scope.launch {
            val newGroupId =
                when (mode) {
                    is TabsTrayState.Mode.DragAndDrop -> {
                        handleSaveFromDragAndDrop(formState = formState, mode = mode)
                    }

                    is TabsTrayState.Mode.Normal,
                    is TabsTrayState.Mode.Select -> {
                        handleSaveFromMultiSelection(
                            formState = formState,
                            selectedTabIds = selectedTabIds,
                        )
                    }
                }
            if (newGroupId != null) {
                mainScope.launch {
                    store.dispatch(TabGroupAction.NewGroupCreated(newGroupId))
                }
            }
        }
    }

    private fun createStarterTabGroup(
        name: String,
        theme: TabGroupTheme,
        store: Store<TabsTrayState, TabsTrayAction>,
    ) = scope.launch {
        val newTabId =
            fenixBrowserUseCases.addNewHomepageTab(
                private = false,
                startLoading = false,
                selectTab = false,
            )
        val tabGroupId =
            tabDataCoordinator.createTabGroup(
                title = name,
                theme = theme.toStorageValue(),
                tabIds = listOf(newTabId),
            )
        mainScope.launch {
            store.dispatch(
                TabGroupAction.OpenCreatedTabGroup(
                    group =
                        TabsTrayItem.TabGroup(
                            id = tabGroupId,
                            title = name,
                            theme = theme,
                            tabs = emptyList(),
                        )
                )
            )
        }
    }

    private suspend fun handleSaveFromDragAndDrop(
        formState: TabGroupFormState,
        mode: TabsTrayState.Mode.DragAndDrop,
    ): String? {
        val destinationId = mode.destinationId ?: return null

        // The dragged tab is sequenced from the tab it was dropped on.
        return tabDataCoordinator.createTabGroup(
            title = formState.name,
            theme = formState.theme.toStorageValue(),
            tabIds = listOf(mode.sourceId, destinationId),
            anchorTabId = destinationId,
        )
    }

    private suspend fun handleSaveFromMultiSelection(
        formState: TabGroupFormState,
        selectedTabIds: List<String>,
    ): String? {
        if (formState.tabGroupId != null) {
            tabDataCoordinator.updateTabGroup(
                tabGroupId = formState.tabGroupId,
                title = formState.name,
                theme = formState.theme.toStorageValue(),
            )
            return null
        }

        return tabDataCoordinator.createTabGroup(
            title = formState.name,
            theme = formState.theme.toStorageValue(),
            tabIds = selectedTabIds,
        )
    }
}
