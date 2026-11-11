/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
package org.mozilla.fenix.tabgroups.flow

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.annotation.VisibleForTesting
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalResources
import androidx.compose.ui.res.stringResource
import androidx.fragment.app.DialogFragment
import androidx.fragment.compose.content
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.navigation.fragment.navArgs
import androidx.navigation3.runtime.NavEntry
import androidx.navigation3.runtime.entryProvider
import androidx.navigation3.scene.DialogSceneStrategy
import androidx.navigation3.ui.NavDisplay
import mozilla.components.compose.base.theme.layout.AcornWindowSize
import mozilla.components.compose.draggable.NoOpItemInteractionHandler
import mozilla.components.lib.state.helpers.StoreProvider.Companion.storeProvider
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.navigation.BottomSheetSceneStrategy
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.tabgroups.AddToTabGroup
import org.mozilla.fenix.tabgroups.EditTabGroup
import org.mozilla.fenix.tabgroups.ExpandedTabGroup
import org.mozilla.fenix.tabgroups.expandedTabGroupActions
import org.mozilla.fenix.tabgroups.shareTabGroup
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.data.toTabStorageUpdate
import org.mozilla.fenix.tabstray.redux.action.TabGroupAction
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.utils.Settings

/** Fragment that hosts Tab Group feature flows outside the TabsTray. */
class TabGroupFlowFragment : DialogFragment() {

    @VisibleForTesting internal lateinit var store: TabGroupFlowStore

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setStyle(STYLE_NO_FRAME, R.style.TabGroupFlowDialogStyle)
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View? {
        store = setupStore()
        return content {
            val state by store.stateFlow.collectAsStateWithLifecycle()

            LaunchedEffect(state.backStack) {
                // Root will always be in the stack
                if (state.backStack.size <= 1) {
                    // This will commit a fragment transaction, so cannot happen after onSaveInstanceState()
                    // unless state loss is allowed
                    dismissAllowingStateLoss()
                }
            }
            FirefoxTheme {
                val windowSize = FirefoxTheme.windowSize
                val sceneStrategies = remember {
                    listOf(BottomSheetSceneStrategy<TabGroupFlowDestination>(), DialogSceneStrategy())
                }
                NavDisplay(
                    backStack = state.backStack,
                    onBack = { store.dispatch(TabGroupAction.NavigateBackInvoked) },
                    sceneStrategies = sceneStrategies,
                    entryProvider = getEntryProvider(state = state, windowSize = windowSize),
                )
            }
        }
    }

    @VisibleForTesting
    @OptIn(ExperimentalMaterial3Api::class)
    @Composable
    internal fun getEntryProvider(
        state: TabGroupFlowState,
        windowSize: AcornWindowSize,
    ): (TabGroupFlowDestination) -> NavEntry<TabGroupFlowDestination> {
        val resources = LocalResources.current
        return entryProvider {
            entry<TabGroupFlowDestination.Root> {}
            entry<TabGroupFlowDestination.EditTabGroup>(
                metadata =
                    BottomSheetSceneStrategy.bottomSheet(
                        skipPartiallyExpanded = true,
                        handleContentDescription =
                            stringResource(id = R.string.edit_tab_group_bottom_sheet_grabber_content_description),
                        showBetaLabel = true,
                    )
            ) {
                val formState = state.formState
                requireNotNull(formState) {
                    "Form state must not be null when navigating to the edit sheet"
                }

                EditTabGroup(
                    formState = formState,
                    onTabGroupNameChange = { newName ->
                        store.dispatch(TabGroupAction.NameChanged(newName))
                    },
                    onTabGroupThemeChange = { newTheme ->
                        store.dispatch(TabGroupAction.ThemeChanged(newTheme))
                    },
                    onConfirmSave = {
                        store.dispatch(TabGroupAction.SaveClicked)
                    },
                )
            }

            entry<TabGroupFlowDestination.AddToTabGroup>(
                metadata =
                    BottomSheetSceneStrategy.bottomSheet(
                        handleContentDescription =
                            stringResource(id = R.string.add_to_tab_group_bottom_sheet_grabber_content_description),
                        showBetaLabel = true,
                    )
            ) {
                AddToTabGroup(
                    tabGroups = state.groups,
                    onAddToNewTabGroup = {
                        store.dispatch(TabGroupAction.TabAddedToNewTabGroup(tabId = it.tabId))
                    },
                    onAddToExistingTabGroup = { group ->
                        store.dispatch(TabGroupAction.TabAddedToExistingTabGroup(groupId = group.id, tabId = it.tabId))
                    },
                )
            }

            entry<TabGroupFlowDestination.ExpandedTabGroup>(
                metadata = { destination ->
                    BottomSheetSceneStrategy.bottomSheet(
                        handleContentDescription = resources.getString(R.string.tab_group_sheet_dismiss_description),
                        showBetaLabel = true,
                        fullyExpandOnFirstOpen =
                            destination.group.shouldFullyExpandOnFirstOpen(windowSize = windowSize),
                    )
                }
            ) { args ->
                ExpandedTabGroupEntry(group = args.group, state = state)
            }
        }
    }

    @Composable
    private fun ExpandedTabGroupEntry(
        group: TabsTrayItem.TabGroup,
        state: TabGroupFlowState,
    ) {
        val expandedGroup = state.groups.find { it.id == group.id } ?: group
        val dotColor = expandedGroup.theme.primary.toArgb()
        ExpandedTabGroup(
            group = expandedGroup,
            actions =
                expandedTabGroupActions(
                    group = expandedGroup,
                    homepageAsNewTabEnabled = state.config.homepageAsNewTabEnabled,
                    dispatch = store::dispatch,
                    addNewHomepageTab = {
                        requireComponents.useCases.fenixBrowserUseCases.addNewHomepageTab(private = false)
                    },
                    onTabClick = {
                        requireComponents.useCases.tabsUseCases.selectTab(it.id)
                        store.dispatch(TabGroupAction.NavigateBackInvoked)
                    },
                    onShareClick = { shareTabGroup(group = expandedGroup, dotColor = dotColor) },
                ),
            displayTabsInGrid = state.config.displayTabsInGrid,
            itemInteractionHandler = NoOpItemInteractionHandler,
        )
    }

    private fun setupStore(): TabGroupFlowStore {
        val settings = requireComponents.settings
        val args by navArgs<TabGroupFlowFragmentArgs>()

        return storeProvider.get { restoredState ->
            TabGroupFlowStore(
                initialState =
                    restoredState?.copy(
                        config =
                            restoredState.config.copy(
                                displayTabsInGrid = settings.gridTabView,
                                homepageAsNewTabEnabled = settings.enableHomepageAsNewTab,
                            )
                    ) ?: createInitialState(settings = settings, args = args),
                middlewares = listOf(TabGroupFlowTelemetryMiddleware()),
            )
        }
    }

    @VisibleForTesting
    internal fun createInitialState(
        settings: Settings,
        args: TabGroupFlowFragmentArgs,
    ): TabGroupFlowState {
        return TabGroupFlowState(
            backStack = buildBackStack(args.entryPoint),
            config =
                TabGroupFlowState.Config(
                    homepageAsNewTabEnabled = settings.enableHomepageAsNewTab,
                    displayTabsInGrid = settings.gridTabView,
                ),
        )
    }

    private fun buildBackStack(tabGroupFlowEntryPoint: TabGroupFlowEntryPoint): List<TabGroupFlowDestination> {
        val destination =
            when (tabGroupFlowEntryPoint) {
                is TabGroupFlowEntryPoint.AddTabToGroup -> {
                    TabGroupFlowDestination.AddToTabGroup(tabId = tabGroupFlowEntryPoint.tabId)
                }
                is TabGroupFlowEntryPoint.EditGroup -> {
                    TabGroupFlowDestination.EditTabGroup
                }
                is TabGroupFlowEntryPoint.ViewGroup -> {
                    requireComponents.tabDataCoordinator.tabsSnapshot.value
                        ?.toTabStorageUpdate()
                        ?.tabGroups
                        ?.find { it.id == tabGroupFlowEntryPoint.groupId }
                        ?.let { TabGroupFlowDestination.ExpandedTabGroup(group = it) }
                }
            }
        return listOfNotNull(TabGroupFlowDestination.Root, destination)
    }
}
