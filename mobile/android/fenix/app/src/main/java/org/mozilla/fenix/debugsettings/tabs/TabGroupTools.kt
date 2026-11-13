/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
package org.mozilla.fenix.debugsettings.tabs

import android.annotation.SuppressLint
import androidx.annotation.VisibleForTesting
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.platform.SoftwareKeyboardController
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.core.text.isDigitsOnly
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.launch
import mozilla.components.browser.state.action.TabListAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.button.FilledButton
import mozilla.components.compose.base.textfield.TextField
import mozilla.components.compose.base.theme.PreviewThemeProvider
import mozilla.components.compose.base.theme.Theme
import mozilla.components.compose.base.utils.toLocaleString
import mozilla.components.feature.tabgroups.storage.data.TabGroup
import mozilla.components.feature.tabgroups.storage.data.TabGroupData
import mozilla.components.feature.tabgroups.storage.repository.TabGroupRepository
import org.mozilla.fenix.R
import org.mozilla.fenix.debugsettings.ui.DebugDrawer
import org.mozilla.fenix.tabstray.data.TabGroupTheme
import org.mozilla.fenix.tabstray.repository.uistate.DefaultTabManagerUiStateRepository
import org.mozilla.fenix.tabstray.repository.uistate.data.PersistedUIState
import org.mozilla.fenix.theme.FirefoxTheme

@VisibleForTesting private const val MAX_TAB_GROUPS_GENERATED = 100

private val TAB_GROUP_COLORS = TabGroupTheme.entries.map { it.name }

/**
 * Tab Group Tools UI for [DebugDrawer] that displays tab group counts and allows tab group bulk creation.
 *
 * @param tabGroupRepository [TabGroupRepository] used to observe and modify tab group data.
 * @param browserStore [BrowserStore] used to fire any tab creation actions.
 */
@SuppressLint("VisibleForTests")
@Composable
fun TabGroupTools(
    tabGroupRepository: TabGroupRepository,
    browserStore: BrowserStore,
) {
    val tabGroupData by tabGroupRepository.tabGroupDataFlow.collectAsState(initial = TabGroupData())

    val totalGroupCount = tabGroupData.tabGroups.size
    val closedGroupCount = remember(tabGroupData) { tabGroupData.tabGroups.count { it.closed } }
    val openGroupCount = totalGroupCount - closedGroupCount

    val coroutineScope = rememberCoroutineScope()
    val context = LocalContext.current

    val tabManagerUiStateRepository = remember {
        DefaultTabManagerUiStateRepository(
            context = context,
            stateFlowScope = coroutineScope,
        )
    }

    Surface {
        TabGroupToolsContent(
            openGroupCount = openGroupCount,
            closedGroupCount = closedGroupCount,
            totalGroupCount = totalGroupCount,
            onCreateGroupsClick = { groupQuantity, tabsPerGroup, isClosed ->
                var debugGroupCounter = totalGroupCount + 1

                coroutineScope.launch {
                    createTabGroupsUseCase(
                        groupQuantity = groupQuantity,
                        tabsPerGroup = tabsPerGroup,
                        isClosed = isClosed,
                        tabGroupRepository = tabGroupRepository,
                        browserStore = browserStore,
                        getAndIncrementCounter = { debugGroupCounter++ },
                    )
                }
            },
            onAutoPopulateClick = {
                coroutineScope.launch {
                    autoPopulateTabGroupsUseCase(tabGroupRepository, browserStore)
                }
            },
            onResetOnboarding = {
                coroutineScope.launch {
                    val currentState = tabManagerUiStateRepository.uiState.value ?: PersistedUIState()
                    tabManagerUiStateRepository.initializeDataStore(
                        initialUiState =
                            currentState.copy(
                                hasUserDismissedTabGroupOnboarding = false,
                                tabGroupOnboardingImpressionCount = 0,
                                hasUserEverHadOneTabGroup = false,
                                hasViewedTabGroupsPage = false,
                                hasUserEverUsedTabGroupMenuItem = false,
                                tabGroupMenuImpressionCount = 0,
                                tabGroupToolbarMenuHighlightImpressionCount = 0,
                            )
                    )
                }
            },
            onRemoveAllGroupsClick = {
                coroutineScope.launch {
                    tabGroupRepository.deleteAllTabGroupData()
                }
            },
        )
    }
}

private fun generateTabGroup(
    counter: Int,
    isClosed: Boolean = false,
    tabIDs: List<String>,
    timestamp: Long = System.currentTimeMillis(),
): TabGroup {
    return TabGroup(
        title = "Tab Group $counter",
        theme = TabGroupTheme.entries.random().name,
        closed = isClosed,
        lastModified = timestamp,
        tabIds = tabIDs,
    )
}

/**
 * Auto-populates the browser and database with a realistic mock state for testing tab groups. This initializes the
 * state by generating an interleaved sequence to mirror a real-world tab tray: 1 tab group, 4 ungrouped tabs, 4 more
 * tab groups, and 16 more ungrouped tabs. Each group contains a predefined representative number of tabs.
 *
 * @param tabGroupRepository [TabGroupRepository] used to save the generated tab groups to the database.
 * @param browserStore [BrowserStore] used to dispatch the created tabs into the live session.
 * @param now The current time in milliseconds, applied as the last-modified time of the generated groups.
 */
private suspend fun autoPopulateTabGroupsUseCase(
    tabGroupRepository: TabGroupRepository,
    browserStore: BrowserStore,
    now: Long = System.currentTimeMillis(),
) {
    val scenarios =
        listOf(
            Triple("Work", TAB_GROUP_COLORS.random(), 8),
            Triple("Shopping", TAB_GROUP_COLORS.random(), 4),
            Triple("Recipes", TAB_GROUP_COLORS.random(), 12),
            Triple("Travel", TAB_GROUP_COLORS.random(), 3),
            Triple("News", TAB_GROUP_COLORS.random(), 6),
        )

    var ungroupedTabCounter = 1

    scenarios.forEachIndexed { index, (title, theme, tabCount) ->
        val groupTabs =
            List(tabCount) { i ->
                createTab(url = "https://example.com", title = "$title Item ${i + 1}")
            }

        browserStore.dispatch(TabListAction.AddMultipleTabsAction(tabs = groupTabs))

        val newGroup =
            TabGroup(
                title = title,
                theme = theme,
                closed = false,
                lastModified = now,
                tabIds = groupTabs.map { it.id },
            )

        tabGroupRepository.createTabGroupWithTabs(tabGroup = newGroup)

        val ungroupedCountToGenerate =
            when (index) {
                0 -> 4
                scenarios.lastIndex -> 16
                else -> 0
            }

        if (ungroupedCountToGenerate > 0) {
            val ungroupedTabs =
                List(ungroupedCountToGenerate) {
                    createTab(
                        url = "https://www.mozilla.org",
                        title = "Ungrouped Tab ${ungroupedTabCounter++}",
                    )
                }
            browserStore.dispatch(TabListAction.AddMultipleTabsAction(tabs = ungroupedTabs))
        }
    }
}

private suspend fun createTabGroupsUseCase(
    groupQuantity: Int,
    tabsPerGroup: Int,
    isClosed: Boolean,
    tabGroupRepository: TabGroupRepository,
    browserStore: BrowserStore,
    getAndIncrementCounter: () -> Int,
) {
    repeat(groupQuantity) {
        if (tabsPerGroup > 0) {
            val realTabs =
                List(tabsPerGroup) { index ->
                    createTab(url = "https://example.com", title = "Generated Tab ${index + 1}")
                }
            val newGroup =
                generateTabGroup(
                    counter = getAndIncrementCounter(),
                    isClosed = isClosed,
                    tabIDs = realTabs.map { it.id },
                )

            browserStore.dispatch(TabListAction.AddMultipleTabsAction(tabs = realTabs))

            tabGroupRepository.createTabGroupWithTabs(tabGroup = newGroup)
        } else {
            val newGroup =
                generateTabGroup(
                    counter = getAndIncrementCounter(),
                    isClosed = isClosed,
                    tabIDs = emptyList(),
                )
            tabGroupRepository.addNewTabGroup(tabGroup = newGroup)
        }
    }
}

@Composable
private fun TabGroupToolsContent(
    openGroupCount: Int,
    closedGroupCount: Int,
    totalGroupCount: Int,
    onCreateGroupsClick: ((groupQuantity: Int, tabsPerGroup: Int, isClosed: Boolean) -> Unit),
    onAutoPopulateClick: () -> Unit,
    onResetOnboarding: () -> Unit,
    onRemoveAllGroupsClick: () -> Unit,
) {
    Column(
        modifier =
            Modifier.fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(all = FirefoxTheme.layout.space.static200),
        verticalArrangement = Arrangement.spacedBy(FirefoxTheme.layout.space.static200),
    ) {
        TabGroupCounter(
            openGroupCount = openGroupCount,
            closedGroupCount = closedGroupCount,
            totalGroupCount = totalGroupCount,
        )

        TabGroupCreationTool(
            onCreateGroupsClick = onCreateGroupsClick,
            onAutoPopulateClick = onAutoPopulateClick,
            onResetOnboarding = onResetOnboarding,
            onRemoveAllGroupsClick = onRemoveAllGroupsClick,
        )
    }
}

@Composable
private fun TabGroupCounter(
    openGroupCount: Int,
    closedGroupCount: Int,
    totalGroupCount: Int,
) {
    Column {
        Text(
            text = stringResource(R.string.debug_drawer_tab_group_tools_count_title),
            style = FirefoxTheme.typography.headline5,
        )

        Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static200))

        TabGroupCountRow(
            groupType = stringResource(R.string.debug_drawer_tab_group_tools_count_open),
            count = openGroupCount,
        )

        TabGroupCountRow(
            groupType = stringResource(R.string.debug_drawer_tab_group_tools_count_closed),
            count = closedGroupCount,
        )

        Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static100))

        HorizontalDivider()

        Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static100))

        TabGroupCountRow(
            groupType = stringResource(R.string.debug_drawer_tab_group_tools_count_total),
            count = totalGroupCount,
        )
    }
}

@Composable
private fun TabGroupCountRow(groupType: String, count: Int) {
    Row(
        modifier = Modifier.fillMaxWidth().padding(start = FirefoxTheme.layout.space.static200),
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Text(
            text = groupType,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            style = FirefoxTheme.typography.headline6,
        )
        Text(
            text = count.toLocaleString(),
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            style = FirefoxTheme.typography.headline6,
        )
    }
}

private const val DEFAULT_QUANTITY = 1

@Composable
private fun TabGroupCreationTool(
    onCreateGroupsClick: ((groupQuantity: Int, tabsPerGroup: Int, isClosed: Boolean) -> Unit),
    onAutoPopulateClick: () -> Unit,
    onResetOnboarding: () -> Unit,
    onRemoveAllGroupsClick: () -> Unit,
) {
    var groupQuantityToCreate by rememberSaveable { mutableStateOf(DEFAULT_QUANTITY.toLocaleString()) }
    var groupQuantityErrorID by rememberSaveable { mutableStateOf<Int?>(null) }
    var groupQuantityHasError by rememberSaveable { mutableStateOf(false) }

    var tabsPerGroup by rememberSaveable { mutableStateOf(DEFAULT_QUANTITY.toLocaleString()) }
    var tabsPerGroupErrorID by rememberSaveable { mutableStateOf<Int?>(null) }
    var tabsPerGroupHasError by rememberSaveable { mutableStateOf(false) }

    val keyboardController = LocalSoftwareKeyboardController.current
    val hasAnyError = groupQuantityHasError || tabsPerGroupHasError

    Column {
        Text(
            text = stringResource(R.string.debug_drawer_tab_group_tools_creation_tool_title),
            style = FirefoxTheme.typography.headline5,
        )
        Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static100))

        TabGroupInputField(
            value = groupQuantityToCreate,
            onValueChange = {
                groupQuantityToCreate = it
                groupQuantityErrorID = validateTabGroupInput(it)
                groupQuantityHasError = groupQuantityErrorID != null
            },
            labelResId = R.string.debug_drawer_tab_group_tools_creation_quantity_label,
            errorId = groupQuantityErrorID,
            hasError = groupQuantityHasError,
            keyboardController = keyboardController,
        )

        Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static100))

        TabGroupInputField(
            value = tabsPerGroup,
            onValueChange = {
                tabsPerGroup = it
                tabsPerGroupErrorID = validateTabGroupInput(it)
                tabsPerGroupHasError = tabsPerGroupErrorID != null
            },
            labelResId = R.string.debug_drawer_tab_group_tools_creation_tabs_label,
            errorId = tabsPerGroupErrorID,
            hasError = tabsPerGroupHasError,
            keyboardController = keyboardController,
        )

        Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static100))

        TabGroupActionButtons(
            hasAnyError = hasAnyError,
            onAddOpenGroupClick = {
                onCreateGroupsClick(groupQuantityToCreate.toInt(), tabsPerGroup.toInt(), false)
            },
            onAddClosedGroupClick = {
                onCreateGroupsClick(groupQuantityToCreate.toInt(), tabsPerGroup.toInt(), true)
            },
            onAutoPopulateClick = onAutoPopulateClick,
            onResetOnboarding = onResetOnboarding,
            onRemoveAllGroupsClick = onRemoveAllGroupsClick,
        )
    }
}

@Composable
private fun TabGroupInputField(
    value: String,
    onValueChange: (String) -> Unit,
    labelResId: Int,
    errorId: Int?,
    hasError: Boolean,
    keyboardController: SoftwareKeyboardController?,
) {
    val errorText =
        when (errorId) {
            null -> ""
            R.string.debug_drawer_tab_group_tools_quantity_exceed_max_error -> {
                stringResource(id = errorId, MAX_TAB_GROUPS_GENERATED)
            }
            else -> stringResource(id = errorId)
        }

    TextField(
        value = value,
        onValueChange = onValueChange,
        placeholder = "",
        errorText = errorText,
        modifier = Modifier.fillMaxWidth(),
        label = stringResource(labelResId),
        isError = hasError,
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
        keyboardActions = KeyboardActions(onDone = { keyboardController?.hide() }),
    )
}

@Composable
private fun TabGroupActionButtons(
    hasAnyError: Boolean,
    onAddOpenGroupClick: () -> Unit,
    onAddClosedGroupClick: () -> Unit,
    onAutoPopulateClick: () -> Unit,
    onResetOnboarding: () -> Unit,
    onRemoveAllGroupsClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(modifier = modifier) {
        FilledButton(
            text = stringResource(R.string.debug_drawer_tab_group_tools_creation_button_open),
            modifier = Modifier.fillMaxWidth(),
            enabled = !hasAnyError,
            onClick = onAddOpenGroupClick,
        )

        Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static100))

        FilledButton(
            text = stringResource(R.string.debug_drawer_tab_group_tools_creation_button_closed),
            modifier = Modifier.fillMaxWidth(),
            enabled = !hasAnyError,
            onClick = onAddClosedGroupClick,
        )

        Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static200))

        FilledButton(
            text = stringResource(R.string.debug_drawer_tab_group_tools_creation_button_auto_populate),
            modifier = Modifier.fillMaxWidth(),
            onClick = onAutoPopulateClick,
        )

        Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static200))

        HorizontalDivider()

        Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static200))

        FilledButton(
            text = stringResource(R.string.debug_drawer_tab_group_tools_creation_button_remove_all),
            modifier = Modifier.fillMaxWidth(),
            onClick = onRemoveAllGroupsClick,
        )

        Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static200))

        HorizontalDivider()

        Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static200))

        FilledButton(
            text = stringResource(R.string.debug_drawer_tab_group_tools_creation_button_reset_onboarding),
            modifier = Modifier.fillMaxWidth(),
            onClick = onResetOnboarding,
        )

        Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static200))
    }
}

private fun validateTabGroupInput(text: String): Int? {
    return when {
        text.isEmpty() -> R.string.debug_drawer_tab_group_tools_quantity_empty_error
        !text.isDigitsOnly() -> R.string.debug_drawer_tab_group_tools_quantity_non_digits_error
        text.toInt() > MAX_TAB_GROUPS_GENERATED -> R.string.debug_drawer_tab_group_tools_quantity_exceed_max_error
        text.toInt() == 0 -> R.string.debug_drawer_tab_group_tools_quantity_non_zero_error
        else -> null
    }
}

@Preview
@Composable
private fun TabGroupToolsPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    val mockTabGroupRepository = PreviewTabGroupRepository

    FirefoxTheme(theme) {
        TabGroupTools(
            tabGroupRepository = mockTabGroupRepository,
            browserStore = BrowserStore(initialState = BrowserState()),
        )
    }
}

private val PreviewTabGroupRepository =
    object : TabGroupRepository {
        private val mutableTabGroupFlow = MutableStateFlow(TabGroupData())

        override val tabGroupDataFlow: Flow<TabGroupData>
            get() = mutableTabGroupFlow

        override suspend fun createTabGroupWithTabs(tabGroup: TabGroup) {
            val updatedAssignments = HashMap(mutableTabGroupFlow.value.tabGroupAssignments)
            tabGroup.tabIds.forEach { id -> updatedAssignments[id] = tabGroup.id }
            mutableTabGroupFlow.emit(
                mutableTabGroupFlow.value.copy(
                    tabGroups = mutableTabGroupFlow.value.tabGroups + tabGroup,
                    tabGroupAssignments = updatedAssignments,
                )
            )
        }

        override suspend fun deleteAllTabGroupData() {
            mutableTabGroupFlow.emit(TabGroupData())
        }

        override suspend fun addNewTabGroup(tabGroup: TabGroup) {
            mutableTabGroupFlow.emit(
                mutableTabGroupFlow.value.copy(tabGroups = mutableTabGroupFlow.value.tabGroups + tabGroup)
            )
        }

        override suspend fun updateTabGroup(tabGroup: TabGroup) {
            // no-op
        }

        override suspend fun closeTabGroup(tabGroupId: String) {
            // no-op
        }

        override suspend fun openTabGroup(tabGroupId: String) {
            // no-op
        }

        override suspend fun closeAllTabGroups() {
            // no-op
        }

        override suspend fun deleteTabGroupById(tabGroupId: String) {
            // no-op
        }

        override suspend fun deleteTabGroupsById(ids: List<String>) {
            // no-op
        }

        override suspend fun ungroupTabGroup(tabGroupId: String) {
            // no-op
        }

        override suspend fun addTabGroupAssignment(tabId: String, tabGroupId: String) {
            // no-op
        }

        override suspend fun addTabsToTabGroup(tabGroupId: String, tabIds: List<String>) {
            // no-op
        }

        override suspend fun updateTabGroupAssignment(tabId: String, tabGroupId: String) {
            // no-op
        }

        override suspend fun deleteTabGroupAssignmentById(tabId: String) {
            // no-op
        }

        override suspend fun deleteTabGroupAssignmentsById(tabIds: List<String>) {
            // no-op
        }

        override suspend fun deleteAllTabGroupAssignmentsForGroup(tabGroupId: String) {
            // no-op
        }
    }
