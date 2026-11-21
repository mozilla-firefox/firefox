/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray

import io.mockk.mockk
import java.util.UUID
import junit.framework.TestCase
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertNull
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.Metrics
import org.mozilla.fenix.GleanMetrics.TabSearch
import org.mozilla.fenix.GleanMetrics.TabsTray
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.nimbus.FakeNimbusEventStore
import org.mozilla.fenix.tabstray.data.TabGroupTheme
import org.mozilla.fenix.tabstray.data.TabStorageUpdate
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.data.createTab
import org.mozilla.fenix.tabstray.data.createTabGroup
import org.mozilla.fenix.tabstray.navigation.TabManagerNavDestination
import org.mozilla.fenix.tabstray.redux.action.TabGroupAction
import org.mozilla.fenix.tabstray.redux.action.TabSearchAction
import org.mozilla.fenix.tabstray.redux.action.TabsTrayAction
import org.mozilla.fenix.tabstray.redux.state.Page
import org.mozilla.fenix.tabstray.redux.state.TabGroupFormState
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState
import org.mozilla.fenix.tabstray.redux.store.TabsTrayStore
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class) // for gleanTestRule
class TabsTrayTelemetryMiddlewareTest {

    private lateinit var store: TabsTrayStore
    private lateinit var tabsTrayTelemetryMiddleware: TabsTrayTelemetryMiddleware
    private val eventStore = FakeNimbusEventStore()

    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    @Before
    fun setup() {
        tabsTrayTelemetryMiddleware = TabsTrayTelemetryMiddleware(eventStore)
        store =
            TabsTrayStore(
                middlewares = listOf(tabsTrayTelemetryMiddleware),
                initialState = TabsTrayState(),
            )
    }

    @Test
    fun `WHEN inactive tabs are updated THEN report the count of inactive tabs`() {
        assertNull(TabsTray.hasInactiveTabs.testGetValue())
        assertNull(Metrics.inactiveTabsCount.testGetValue())

        store.dispatch(
            TabsTrayAction.TabDataUpdateReceived(
                TabStorageUpdate(
                    selectedTabId = "id",
                    normalItems = emptyList(),
                    normalTabCount = 0,
                    selectedNormalItemIndex = 0,
                    inactiveTabs = listOf(mockk(), mockk()),
                    privateTabs = emptyList(),
                    selectedPrivateItemIndex = 0,
                    tabGroups = emptyList(),
                )
            )
        )
        assertNotNull(TabsTray.hasInactiveTabs.testGetValue())
        assertNotNull(Metrics.inactiveTabsCount.testGetValue())
        assertEquals(2L, Metrics.inactiveTabsCount.testGetValue())
    }

    @Test
    fun `WHEN multi select mode from menu is entered THEN relevant metrics are collected`() {
        assertNull(TabsTray.enterMultiselectMode.testGetValue())

        store.dispatch(TabsTrayAction.EnterSelectMode)

        assertNotNull(TabsTray.enterMultiselectMode.testGetValue())
        val snapshot = TabsTray.enterMultiselectMode.testGetValue()!!
        assertEquals(1, snapshot.size)
        assertEquals("false", snapshot.single().extra?.getValue("tab_selected"))
    }

    @Test
    fun `WHEN multi select mode by long press is entered THEN relevant metrics are collected`() {
        store.dispatch(TabsTrayAction.AddSelectTab(mockk()))

        assertNotNull(TabsTray.enterMultiselectMode.testGetValue())
        val snapshot = TabsTray.enterMultiselectMode.testGetValue()!!
        assertEquals(1, snapshot.size)
        assertEquals("true", snapshot.single().extra?.getValue("tab_selected"))
    }

    @Test
    fun `WHEN the inactive tabs auto close dialog is shown THEN the metric is reported`() {
        assertNull(TabsTray.autoCloseSeen.testGetValue())

        store.dispatch(TabsTrayAction.TabAutoCloseDialogShown)

        assertNotNull(TabsTray.autoCloseSeen.testGetValue())
    }

    @Test
    fun `WHEN the share all normal tabs button is clicked THEN the metric is reported`() {
        assertNull(TabsTray.shareAllTabs.testGetValue())

        store.dispatch(TabsTrayAction.ShareAllNormalTabs)

        assertNotNull(TabsTray.shareAllTabs.testGetValue())
    }

    @Test
    fun `WHEN the share all private tabs button is clicked THEN the metric is reported`() {
        assertNull(TabsTray.shareAllTabs.testGetValue())

        store.dispatch(TabsTrayAction.ShareAllPrivateTabs)

        assertNotNull(TabsTray.shareAllTabs.testGetValue())
    }

    @Test
    fun `WHEN the select all normal tabs button is clicked THEN the metric is reported`() {
        assertNull(TabsTray.selectAllNormalTabs.testGetValue())

        store.dispatch(TabsTrayAction.SelectAllNormalTabs)

        assertNotNull(TabsTray.selectAllNormalTabs.testGetValue())
    }

    @Test
    fun `WHEN the delete all normal tabs button is clicked THEN the metric is reported`() {
        assertNull(TabsTray.closeAllTabs.testGetValue())

        store.dispatch(TabsTrayAction.CloseAllNormalTabs)

        assertNotNull(TabsTray.closeAllTabs.testGetValue())
    }

    @Test
    fun `WHEN the delete all private tabs button is clicked THEN the metric is reported`() {
        assertNull(TabsTray.closeAllTabs.testGetValue())

        store.dispatch(TabsTrayAction.CloseAllPrivateTabs)

        assertNotNull(TabsTray.closeAllTabs.testGetValue())
    }

    @Test
    fun `GIVEN one tab selected WHEN the bookmark selected tabs button is clicked THEN the metric is reported`() {
        assertNull(TabsTray.bookmarkSelectedTabs.testGetValue())

        store.dispatch(TabsTrayAction.BookmarkSelectedTabs(1))

        assertNotNull(TabsTray.bookmarkSelectedTabs.testGetValue())
        val snapshot = TabsTray.bookmarkSelectedTabs.testGetValue()!!
        assertEquals(1, snapshot.size)
        assertEquals("1", snapshot.single().extra?.getValue("tab_count"))
        assertEquals(1, Metrics.bookmarksAdd["tabs_tray"].testGetValue())

        eventStore.assertRecorded("bookmark_added")
    }

    @Test
    fun `GIVEN multiple tabs selected WHEN the bookmark selected tabs button is clicked THEN the metric is reported`() {
        assertNull(TabsTray.bookmarkSelectedTabs.testGetValue())

        store.dispatch(TabsTrayAction.BookmarkSelectedTabs(2))

        assertNotNull(TabsTray.bookmarkSelectedTabs.testGetValue())
        val snapshot = TabsTray.bookmarkSelectedTabs.testGetValue()!!
        assertEquals(1, snapshot.size)
        assertEquals("2", snapshot.single().extra?.getValue("tab_count"))
        assertEquals(2, Metrics.bookmarksAdd["tabs_tray"].testGetValue())

        eventStore.assertRecorded("bookmark_added", "bookmark_added")
    }

    @Test
    fun `WHEN the three dot button's menu is shown THEN the metric is reported`() {
        assertNull(TabsTray.menuOpened.testGetValue())

        store.dispatch(TabsTrayAction.ThreeDotMenuShown)

        assertNotNull(TabsTray.menuOpened.testGetValue())
    }

    /** [TabSearch.tabSearchIconClicked] coverage */
    @Test
    fun `WHEN tab search icon is clicked THEN record tab search icon clicked telemetry`() {
        TestCase.assertNull(TabSearch.tabSearchIconClicked.testGetValue())

        store.dispatch(TabsTrayAction.TabSearchClicked)

        TestCase.assertNotNull(TabSearch.tabSearchIconClicked.testGetValue())

        val snapshot = TabSearch.tabSearchIconClicked.testGetValue()!!
        assertEquals(1, snapshot.size)

        assertEquals("tab_search_icon_clicked", snapshot.single().name)
    }

    /** [TabSearch.resultClicked] coverage */
    @Test
    fun `WHEN a tab search result is clicked THEN record result clicked telemetry`() {
        TestCase.assertNull(TabSearch.resultClicked.testGetValue())

        val tabs =
            listOf(
                createTab(url = "www.mozilla.com"),
                createTab(url = "www.developer.mozilla.org"),
            )
        store.dispatch(TabSearchAction.SearchResultsUpdated(results = tabs))

        store.dispatch(TabSearchAction.SearchResultClicked(tabs[1]))

        TestCase.assertNotNull(TabSearch.resultClicked.testGetValue())

        val snapshot = TabSearch.resultClicked.testGetValue()!!
        assertEquals(1, snapshot.size)

        assertEquals("result_clicked", snapshot.single().name)
    }

    /** [TabSearch.navigateBackIconClicked] coverage */
    @Test
    fun `WHEN the navigation back icon is clicked THEN record navigate back icon clicked telemetry`() {
        TestCase.assertNull(TabSearch.navigateBackIconClicked.testGetValue())

        store =
            TabsTrayStore(
                middlewares = listOf(tabsTrayTelemetryMiddleware),
                initialState = TabsTrayState(backStack = listOf(TabManagerNavDestination.TabSearch)),
            )

        store.dispatch(TabsTrayAction.NavigateBackInvoked)

        TestCase.assertNotNull(TabSearch.navigateBackIconClicked.testGetValue())

        val snapshot = TabSearch.navigateBackIconClicked.testGetValue()!!
        assertEquals(1, snapshot.size)

        assertEquals("navigate_back_icon_clicked", snapshot.single().name)
    }

    /** [TabsTray.tabGroupCreated] coverage */
    @Test
    fun `GIVEN creating a new group WHEN SaveClicked is dispatched THEN the creation metric is reported`() {
        assertNull(TabsTray.tabGroupCreated.testGetValue())

        val createFormState =
            TabGroupFormState(
                tabGroupId = null,
                name = "New Group",
                nextTabGroupNumber = 1,
                edited = true,
            )
        store =
            TabsTrayStore(
                middlewares = listOf(tabsTrayTelemetryMiddleware),
                initialState = TabsTrayState(tabGroupState = TabsTrayState.TabGroupState(formState = createFormState)),
            )

        store.dispatch(TabGroupAction.SaveClicked)

        assertNotNull(TabsTray.tabGroupCreated.testGetValue())
    }

    @Test
    fun `GIVEN editing an existing group WHEN SaveClicked is dispatched THEN the creation metric is NOT reported`() {
        assertNull(TabsTray.tabGroupCreated.testGetValue())

        val editFormState =
            TabGroupFormState(
                tabGroupId = "existing group",
                name = "Edited Group",
                edited = true,
            )
        store =
            TabsTrayStore(
                middlewares = listOf(tabsTrayTelemetryMiddleware),
                initialState = TabsTrayState(tabGroupState = TabsTrayState.TabGroupState(formState = editFormState)),
            )

        store.dispatch(TabGroupAction.SaveClicked)

        assertNull(TabsTray.tabGroupCreated.testGetValue())
    }

    /** [TabsTray.tabGroupDeleted] coverage */
    @Test
    fun `WHEN a tab group deletion is confirmed THEN the deletion metric is reported`() {
        assertNull(TabsTray.tabGroupDeleted.testGetValue())

        val mockGroup =
            TabsTrayItem.TabGroup(
                id = "test group",
                title = "Test",
                theme = TabGroupTheme.default,
                tabs = mutableListOf(),
            )
        store.dispatch(TabGroupAction.DeleteConfirmed(mockGroup))

        assertNotNull(TabsTray.tabGroupDeleted.testGetValue())
    }

    @Test
    fun `WHEN closing the last tab and deleting the group is confirmed THEN the deletion metric is reported`() {
        assertNull(TabsTray.tabGroupDeleted.testGetValue())

        val mockGroup =
            TabsTrayItem.TabGroup(
                id = "test group",
                title = "Test",
                theme = TabGroupTheme.default,
                tabs = mutableListOf(),
            )
        store.dispatch(TabGroupAction.CloseTabAndDeleteGroupConfirmed(mockGroup))

        assertNotNull(TabsTray.tabGroupDeleted.testGetValue())
    }

    /** [TabsTray.tabGroupUngrouped] and [TabsTray.ungroupConfirmationDisabled] coverage */
    @Test
    fun `WHEN an ungroup is confirmed THEN the ungroup metric is reported`() {
        assertNull(TabsTray.tabGroupUngrouped.testGetValue())

        store.dispatch(TabGroupAction.UngroupConfirmed(group = createTabGroup(), dontAskAgain = false))

        assertNotNull(TabsTray.tabGroupUngrouped.testGetValue())
    }

    @Test
    fun `GIVEN the confirmation was not previously disabled WHEN an ungroup is confirmed with don't ask again THEN the opt out metric is reported`() {
        assertNull(TabsTray.ungroupConfirmationDisabled.testGetValue())

        store.dispatch(TabGroupAction.UngroupConfirmed(group = createTabGroup(), dontAskAgain = true))

        assertNotNull(TabsTray.ungroupConfirmationDisabled.testGetValue())
    }

    @Test
    fun `WHEN an ungroup is confirmed without don't ask again THEN the opt out metric is not reported`() {
        assertNull(TabsTray.ungroupConfirmationDisabled.testGetValue())

        store.dispatch(TabGroupAction.UngroupConfirmed(group = createTabGroup(), dontAskAgain = false))

        assertNull(TabsTray.ungroupConfirmationDisabled.testGetValue())
    }

    @Test
    fun `GIVEN the confirmation was previously disabled WHEN an ungroup is confirmed THEN only the ungroup metric is reported`() {
        val suppressedStore =
            TabsTrayStore(
                middlewares = listOf(tabsTrayTelemetryMiddleware),
                initialState =
                    TabsTrayState(tabGroupState = TabsTrayState.TabGroupState(skipUngroupConfirmation = true)),
            )

        suppressedStore.dispatch(TabGroupAction.UngroupConfirmed(group = createTabGroup(), dontAskAgain = true))

        assertNotNull(TabsTray.tabGroupUngrouped.testGetValue())
        assertNull(TabsTray.ungroupConfirmationDisabled.testGetValue())
    }

    /** [TabsTray.tabAddedToGroup] coverage */
    @Test
    fun `WHEN a single tab is added to a tab group THEN the metric is reported with count 1`() {
        assertNull(TabsTray.tabAddedToGroup.testGetValue())

        store.dispatch(
            TabGroupAction.TabAddedToExistingTabGroup(tabId = "id", groupId = "id", groupTitle = "Tab Group 1")
        )

        assertNotNull(TabsTray.tabAddedToGroup.testGetValue())
        val snapshot = TabsTray.tabAddedToGroup.testGetValue()!!
        assertEquals(1, snapshot.size)
        assertEquals("1", snapshot.single().extra?.getValue("tab_count"))
    }

    @Test
    fun `GIVEN multiselect mode WHEN multiple tabs are added to a tab group THEN the metric is reported with the correct count`() {
        assertNull(TabsTray.tabAddedToGroup.testGetValue())

        val mockTab1 = createTab(url = "www.example1.com")
        val mockTab2 = createTab(url = "www.example2.com")
        val mockTab3 = createTab(url = "www.example3.com")

        store =
            TabsTrayStore(
                middlewares = listOf(tabsTrayTelemetryMiddleware),
                initialState =
                    TabsTrayState(mode = TabsTrayState.Mode.Select(selectedTabs = setOf(mockTab1, mockTab2, mockTab3))),
            )

        store.dispatch(TabGroupAction.SelectedTabsAddedToGroup("id"))

        assertNotNull(TabsTray.tabAddedToGroup.testGetValue())
        val snapshot = TabsTray.tabAddedToGroup.testGetValue()!!
        assertEquals(1, snapshot.size)
        assertEquals("3", snapshot.single().extra?.getValue("tab_count"))
    }

    /** [TabsTray.tabGroupOpened] coverage */
    @Test
    fun `GIVEN tab groups page WHEN TabGroupClicked is dispatched THEN the tab group opened metric is reported with group_screen source`() {
        assertNull(TabsTray.tabGroupOpened.testGetValue())

        store =
            TabsTrayStore(
                middlewares = listOf(tabsTrayTelemetryMiddleware),
                initialState = TabsTrayState(selectedPage = Page.TabGroups),
            )

        val tabGroup =
            TabsTrayItem.TabGroup(
                id = "test group",
                title = "Test",
                theme = TabGroupTheme.default,
                tabs = mutableListOf(),
            )
        store.dispatch(TabGroupAction.TabGroupClicked(tabGroup))

        assertNotNull(TabsTray.tabGroupOpened.testGetValue())
        val snapshot = TabsTray.tabGroupOpened.testGetValue()!!

        assertEquals(1, snapshot.size)
        assertEquals("group_screen", snapshot.single().extra?.getValue("source"))
    }

    @Test
    fun `GIVEN normal tabs page WHEN TabGroupClicked is dispatched THEN the tab group opened metric is reported with tab_screen source`() {
        assertNull(TabsTray.tabGroupOpened.testGetValue())

        store =
            TabsTrayStore(
                middlewares = listOf(tabsTrayTelemetryMiddleware),
                initialState = TabsTrayState(selectedPage = Page.NormalTabs),
            )

        val tabGroup =
            TabsTrayItem.TabGroup(
                id = "test group",
                title = "Test",
                theme = TabGroupTheme.default,
                tabs = mutableListOf(),
            )
        store.dispatch(TabGroupAction.TabGroupClicked(tabGroup))

        assertNotNull(TabsTray.tabGroupOpened.testGetValue())
        val snapshot = TabsTray.tabGroupOpened.testGetValue()!!

        assertEquals(1, snapshot.size)
        assertEquals("tab_screen", snapshot.single().extra?.getValue("source"))
    }

    /** [TabsTray.tabGroupClosed] coverage */
    @Test
    fun `WHEN a tab group is closed THEN the tab group closed metric is reported`() {
        assertNull(TabsTray.tabGroupClosed.testGetValue())

        val mockGroup =
            TabsTrayItem.TabGroup(
                id = "test group",
                title = "Test",
                theme = TabGroupTheme.default,
                tabs = mutableListOf(),
            )
        store.dispatch(TabGroupAction.CloseTabGroupClicked(mockGroup))

        assertNotNull(TabsTray.tabGroupClosed.testGetValue())
    }

    /** [TabsTray.tabGroupOnboardingShown] coverage */
    @Test
    fun `WHEN the onboarding card is shown THEN the onboarding shown metric is reported`() {
        assertNull(TabsTray.tabGroupOnboardingShown.testGetValue())

        store.dispatch(TabGroupAction.OnboardingShown)

        assertNotNull(TabsTray.tabGroupOnboardingShown.testGetValue())
    }

    @Test
    fun `WHEN the onboarding card is shown multiple times THEN each impression is reported`() {
        store.dispatch(TabGroupAction.OnboardingShown)
        store.dispatch(TabGroupAction.OnboardingShown)

        assertEquals(2, TabsTray.tabGroupOnboardingShown.testGetValue()?.size)
    }

    /** [TabsTray.tabGroupOnboardingDismissed] coverage */
    @Test
    fun `WHEN the onboarding card is dismissed THEN the onboarding dismissed metric is reported`() {
        assertNull(TabsTray.tabGroupOnboardingDismissed.testGetValue())

        store.dispatch(TabGroupAction.OnboardingDismissed)

        assertNotNull(TabsTray.tabGroupOnboardingDismissed.testGetValue())
    }

    /** [Metrics.tabGroupCreationMode] coverage */
    @Test
    fun `WHEN AddToNewTabGroup is dispatched THEN the tab group creation mode menu metric is reported`() {
        assertNull(Metrics.tabGroupCreationMode["menu"].testGetValue())

        store.dispatch(TabGroupAction.AddToNewTabGroup)

        assertEquals(1, Metrics.tabGroupCreationMode["menu"].testGetValue())
    }

    @Test
    fun `WHEN NewTabGroupFabClicked is dispatched THEN the tab group creation mode fab metric is reported`() {
        assertNull(Metrics.tabGroupCreationMode["fab"].testGetValue())

        store.dispatch(TabGroupAction.NewTabGroupFabClicked)

        assertEquals(1, Metrics.tabGroupCreationMode["fab"].testGetValue())
    }

    @Test
    fun `WHEN NewTabGroupMenuClicked is dispatched THEN the tab group creation mode menu metric is reported`() {
        assertNull(Metrics.tabGroupCreationMode["menu"].testGetValue())

        store.dispatch(TabGroupAction.NewTabGroupMenuClicked)

        assertEquals(1, Metrics.tabGroupCreationMode["menu"].testGetValue())
    }

    @Test
    fun `GIVEN a target tab WHEN DragAndDropTwoTabs is dispatched THEN the drag_and_drop metric is reported if both are tabs`() {
        assertNull(Metrics.tabGroupCreationMode["drag_and_drop"].testGetValue())

        val mockSourceTab = createTab(url = "www.source.com").copy(id = "source_id")
        val mockTargetTab = createTab(url = "www.target.com").copy(id = "target_id")
        store =
            TabsTrayStore(
                middlewares = listOf(tabsTrayTelemetryMiddleware),
                initialState =
                    TabsTrayState(
                        normalTabsState = TabsTrayState.NormalTabsState(items = listOf(mockSourceTab, mockTargetTab))
                    ),
            )

        store.dispatch(TabGroupAction.DragAndDropInitiated(sourceId = "source_id", destinationId = "target_id"))

        assertEquals(1, Metrics.tabGroupCreationMode["drag_and_drop"].testGetValue())
    }

    @Test
    fun `GIVEN a source group WHEN DragAndDropTwoTabs is dispatched THEN the drag_and_drop metric is NOT reported`() {
        assertNull(Metrics.tabGroupCreationMode["drag_and_drop"].testGetValue())

        val mockSourceGroup = createTabGroup(id = "source_id")
        val mockTargetTab = createTab(url = "www.target.com").copy(id = "target_id")
        store =
            TabsTrayStore(
                middlewares = listOf(tabsTrayTelemetryMiddleware),
                initialState =
                    TabsTrayState(
                        normalTabsState = TabsTrayState.NormalTabsState(items = listOf(mockSourceGroup, mockTargetTab))
                    ),
            )

        store.dispatch(TabGroupAction.DragAndDropTwoTabs(sourceTabId = "source_id", destinationTabId = "target_id"))

        assertNull(Metrics.tabGroupCreationMode["drag_and_drop"].testGetValue())
    }

    @Test
    fun `GIVEN a target group WHEN DragAndDropTwoTabs is dispatched THEN the drag_and_drop metric is NOT reported`() {
        assertNull(Metrics.tabGroupCreationMode["drag_and_drop"].testGetValue())

        val mockSourceTab = createTab(url = "www.source.com").copy(id = "source_id")
        val mockTargetGroup =
            TabsTrayItem.TabGroup(
                id = "target_id",
                title = "Test",
                theme = TabGroupTheme.default,
                tabs = mutableListOf(),
            )
        store =
            TabsTrayStore(
                middlewares = listOf(tabsTrayTelemetryMiddleware),
                initialState =
                    TabsTrayState(
                        normalTabsState = TabsTrayState.NormalTabsState(items = listOf(mockSourceTab, mockTargetGroup))
                    ),
            )

        store.dispatch(TabGroupAction.DragAndDropTwoTabs(sourceTabId = "source_id", destinationTabId = "target_id"))

        assertNull(Metrics.tabGroupCreationMode["drag_and_drop"].testGetValue())
    }

    /** [TabsTray.tabGroupScreenOpened] coverage */
    @Test
    fun `WHEN the Tab Groups page is selected THEN the group screen show metric is reported`() {
        assertNull(TabsTray.tabGroupScreenOpened.testGetValue())

        store.dispatch(TabsTrayAction.PageSelected(Page.TabGroups))

        assertNotNull(TabsTray.tabGroupScreenOpened.testGetValue())
    }

    /** [TabsTray.tabGroupCreateCancel] coverage */
    @Test
    fun `GIVEN AddToTabGroup destination WHEN NavigateBackInvoked is dispatched THEN the cancel metric is reported`() {
        assertNull(TabsTray.tabGroupCreateCancel.testGetValue())

        store =
            TabsTrayStore(
                middlewares = listOf(tabsTrayTelemetryMiddleware),
                initialState = TabsTrayState(backStack = listOf(TabManagerNavDestination.AddToTabGroup)),
            )

        store.dispatch(TabsTrayAction.NavigateBackInvoked)

        assertNotNull(TabsTray.tabGroupCreateCancel.testGetValue())
    }

    @Test
    fun `GIVEN a tab WHEN TabDragStart is dispatched THEN the longpress drag metric is reported with item_type tab`() {
        assertNull(TabsTray.tabLongPressDrag.testGetValue())

        val mockTab = createTab(url = "www.example.com").copy(id = "123")
        store =
            TabsTrayStore(
                middlewares = listOf(tabsTrayTelemetryMiddleware),
                initialState = TabsTrayState(normalTabsState = TabsTrayState.NormalTabsState(items = listOf(mockTab))),
            )

        store.dispatch(TabsTrayAction.TabDragStart(sourceId = "123", preserveSelectMode = false))

        val snapshot = TabsTray.tabLongPressDrag.testGetValue()!!
        assertEquals(1, snapshot.size)
        assertEquals(
            TabsTrayTelemetryMiddleware.TabItemType.TAB.telemetryId,
            snapshot.single().extra?.getValue("item_type"),
        )
    }

    @Test
    fun `GIVEN a tab group WHEN TabDragStart is dispatched THEN the longpress drag metric is reported with item_type tab_group`() {
        assertNull(TabsTray.tabLongPressDrag.testGetValue())

        val mockGroup =
            TabsTrayItem.TabGroup(
                id = "123",
                title = "Test",
                theme = TabGroupTheme.default,
                tabs = mutableListOf(),
            )
        store =
            TabsTrayStore(
                middlewares = listOf(tabsTrayTelemetryMiddleware),
                initialState =
                    TabsTrayState(normalTabsState = TabsTrayState.NormalTabsState(items = listOf(mockGroup))),
            )

        store.dispatch(TabsTrayAction.TabDragStart(sourceId = "123", preserveSelectMode = false))

        val snapshot = TabsTray.tabLongPressDrag.testGetValue()!!
        assertEquals(1, snapshot.size)
        assertEquals(
            TabsTrayTelemetryMiddleware.TabItemType.TAB_GROUP.telemetryId,
            snapshot.single().extra?.getValue("item_type"),
        )
    }

    @Test
    fun `GIVEN an unknown source id WHEN TabDragStart is dispatched THEN the longpress drag metric is reported with item_type unknown`() {
        assertNull(TabsTray.tabLongPressDrag.testGetValue())

        store.dispatch(TabsTrayAction.TabDragStart(sourceId = "123", preserveSelectMode = false))

        val snapshot = TabsTray.tabLongPressDrag.testGetValue()!!
        assertEquals(1, snapshot.size)
        assertEquals(
            TabsTrayTelemetryMiddleware.TabItemType.UNKNOWN.telemetryId,
            snapshot.single().extra?.getValue("item_type"),
        )
    }

    /** [TabsTray.tabLongPressDragRearrangedPosition] coverage */
    @Test
    fun `GIVEN a tab WHEN ReorderTabsTrayItem is dispatched THEN the longpress drag rearranged metric is reported`() {
        assertNull(TabsTray.tabLongPressDragRearrangedPosition.testGetValue())

        val mockTab = createTab(url = "www.example.com").copy(id = "123")
        store =
            TabsTrayStore(
                middlewares = listOf(tabsTrayTelemetryMiddleware),
                initialState = TabsTrayState(normalTabsState = TabsTrayState.NormalTabsState(items = listOf(mockTab))),
            )

        store.dispatch(
            TabsTrayAction.ReorderTabsTrayItem(
                sourceId = "123",
                destinationId = "321",
                placeAfter = true,
            )
        )

        assertNotNull(TabsTray.tabLongPressDragRearrangedPosition.testGetValue())
    }

    @Test
    fun `GIVEN a tab group WHEN ReorderTabsTrayItem is dispatched THEN the longpress drag rearranged metric is NOT reported`() {
        assertNull(TabsTray.tabLongPressDragRearrangedPosition.testGetValue())

        val mockGroup =
            TabsTrayItem.TabGroup(
                id = "123",
                title = "Test",
                theme = TabGroupTheme.default,
                tabs = mutableListOf(),
            )
        store =
            TabsTrayStore(
                middlewares = listOf(tabsTrayTelemetryMiddleware),
                initialState =
                    TabsTrayState(normalTabsState = TabsTrayState.NormalTabsState(items = listOf(mockGroup))),
            )

        store.dispatch(
            TabsTrayAction.ReorderTabsTrayItem(
                sourceId = "123",
                destinationId = "321",
                placeAfter = true,
            )
        )

        assertNull(TabsTray.tabLongPressDragRearrangedPosition.testGetValue())
    }

    // region TabItemLongClicked

    @Test
    fun `GIVEN normal mode, WHEN TabItemLongClicked invoked with TabGroup, THEN long press recorded`() {
        val tabGroup = createTabGroup(title = "TestGroup", tabs = mutableListOf(createTab(url = "example.com")))

        val store = setupStore(items = listOf(tabGroup), mode = TabsTrayState.Mode.Normal)

        store.dispatch(TabsTrayAction.TabItemLongClicked(tabGroup))

        assertEquals(expected = 1, actual = TabsTray.tabLongPress.testGetValue()?.size)
    }

    @Test
    fun `GIVEN normal mode, WHEN TabItemLongClicked invoked with TabGroup, THEN enter select mode telemetry is recorded`() {
        val tabGroup = createTabGroup(title = "TestGroup", tabs = mutableListOf(createTab(url = "example.com")))

        val store = setupStore(items = listOf(tabGroup), mode = TabsTrayState.Mode.Normal)

        store.dispatch(TabsTrayAction.TabItemLongClicked(tabGroup))

        assertNotNull(TabsTray.enterMultiselectMode.testGetValue())
    }

    @Test
    fun `GIVEN normal mode, WHEN TabItemLongClicked invoked with normal tab, THEN long press recorded`() {
        val tab = createTab(url = "mozilla.org", title = "TestTab", private = false)

        val store = setupStore(items = listOf(tab), mode = TabsTrayState.Mode.Normal)

        store.dispatch(TabsTrayAction.TabItemLongClicked(tab))

        assertEquals(expected = 1, actual = TabsTray.tabLongPress.testGetValue()?.size)
    }

    @Test
    fun `GIVEN normal mode, WHEN TabItemLongClicked invoked with normal tab, THEN enter select mode telemetry is recorded`() {
        val tab = createTab(url = "mozilla.org", title = "TestTab", private = false)

        val store = setupStore(items = listOf(tab), mode = TabsTrayState.Mode.Normal)

        store.dispatch(TabsTrayAction.TabItemLongClicked(tab))

        assertNotNull(TabsTray.enterMultiselectMode.testGetValue())
    }

    @Test
    fun `GIVEN normal mode, WHEN TabItemLongClicked invoked with private tab, THEN long press is recorded`() {
        val tab = createTab(url = "mozilla.org", title = "TestTab", private = true)

        val store = setupStore(items = listOf(tab), mode = TabsTrayState.Mode.Normal)

        store.dispatch(TabsTrayAction.TabItemLongClicked(tab))

        assertNotNull(TabsTray.tabLongPress.testGetValue())
    }

    @Test
    fun `GIVEN normal mode, WHEN TabItemLongClicked invoked with private tab, THEN enter select mode telemetry is not recorded`() {
        val tab = createTab(url = "mozilla.org", title = "TestTab", private = true)

        val store = setupStore(items = listOf(tab), mode = TabsTrayState.Mode.Normal)

        store.dispatch(TabsTrayAction.TabItemLongClicked(tab))

        assertNull(TabsTray.enterMultiselectMode.testGetValue())
    }

    @Test
    fun `GIVEN select mode with selected tabs, WHEN TabItemLongClicked invoked with TabGroup, THEN long press is recorded`() {
        val tabGroup = createTabGroup(title = "TestGroup", tabs = mutableListOf(createTab(url = "example.com")))
        val tab = createTab(title = "Test Tab", url = "mozilla.org")

        val store =
            setupStore(
                items = listOf(tabGroup, tab),
                mode = TabsTrayState.Mode.Select(selectedTabs = setOf(tab)),
            )

        store.dispatch(TabsTrayAction.TabItemLongClicked(tabGroup))

        assertNotNull(TabsTray.tabLongPress.testGetValue())
    }

    @Test
    fun `GIVEN select mode with selected tabs, WHEN TabItemLongClicked invoked with TabGroup, THEN enter select mode telemetry is not recorded`() {
        val tabGroup = createTabGroup(title = "TestGroup", tabs = mutableListOf(createTab(url = "example.com")))
        val tab = createTab(title = "Test Tab", url = "mozilla.org")

        val store =
            setupStore(
                items = listOf(tabGroup),
                mode = TabsTrayState.Mode.Select(selectedTabs = setOf(tab)),
            )

        store.dispatch(TabsTrayAction.TabItemLongClicked(tabGroup))

        assertNull(TabsTray.enterMultiselectMode.testGetValue())
    }

    @Test
    fun `GIVEN select mode with selected tabs, WHEN TabItemLongClicked invoked with tab, THEN long press is recorded`() {
        val tabGroup = createTabGroup(title = "TestGroup", tabs = mutableListOf(createTab(url = "example.com")))
        val tab = createTab(title = "Test Tab", url = "mozilla.org")

        val store =
            setupStore(
                items = listOf(tabGroup, tab),
                mode = TabsTrayState.Mode.Select(selectedTabs = setOf(tab)),
            )

        store.dispatch(TabsTrayAction.TabItemLongClicked(tab))

        assertNotNull(TabsTray.tabLongPress.testGetValue())
    }

    @Test
    fun `GIVEN select mode with selected tabs, WHEN TabItemLongClicked invoked with tab, THEN enter select mode telemetry is not recorded`() {
        val tabGroup = createTabGroup(title = "TestGroup", tabs = mutableListOf(createTab(url = "example.com")))
        val tab = createTab(title = "Test Tab", url = "mozilla.org")

        val store =
            setupStore(
                items = listOf(tabGroup),
                mode = TabsTrayState.Mode.Select(selectedTabs = setOf(tab)),
            )

        store.dispatch(TabsTrayAction.TabItemLongClicked(tab))

        assertNull(TabsTray.enterMultiselectMode.testGetValue())
    }

    // endregion

    private fun setupStore(
        items: List<TabsTrayItem>,
        mode: TabsTrayState.Mode = TabsTrayState.Mode.Normal,
    ): TabsTrayStore {
        return TabsTrayStore(
            middlewares = listOf(TabsTrayTelemetryMiddleware(nimbusEventStore = FakeNimbusEventStore())),
            initialState =
                TabsTrayState(
                    mode = mode,
                    selectedPage = Page.NormalTabs,
                    normalTabsState = TabsTrayState.NormalTabsState(items = items),
                ),
        )
    }

    @Test
    fun `GIVEN creating a new group WHEN SaveClicked is dispatched THEN tabGroupNamed and tabGroupCreated are reported`() {
        assertNull(TabsTray.tabGroupNamed.testGetValue())
        assertNull(TabsTray.tabGroupCreated.testGetValue())

        val createFormState =
            TabGroupFormState(
                tabGroupId = null,
                name = "New Name",
                edited = true,
            )
        store =
            TabsTrayStore(
                middlewares = listOf(tabsTrayTelemetryMiddleware),
                initialState = TabsTrayState(tabGroupState = TabsTrayState.TabGroupState(formState = createFormState)),
            )

        store.dispatch(TabGroupAction.SaveClicked)

        assertNotNull(TabsTray.tabGroupNamed.testGetValue())
        assertNotNull(TabsTray.tabGroupCreated.testGetValue())
    }

    @Test
    fun `GIVEN editing an existing group WHEN SaveClicked is dispatched with a new name THEN tabGroupNameChanged is reported`() {
        assertNull(TabsTray.tabGroupNameChanged.testGetValue())

        val groupId = "existing_id"
        val originalGroup =
            TabsTrayItem.TabGroup(
                id = groupId,
                title = "Old Name",
                theme = TabGroupTheme.default,
                tabs = mutableListOf(),
            )
        val editFormState =
            TabGroupFormState(
                tabGroupId = groupId,
                name = "New Name",
                edited = true,
            )
        store =
            TabsTrayStore(
                middlewares = listOf(tabsTrayTelemetryMiddleware),
                initialState =
                    TabsTrayState(
                        tabGroupState =
                            TabsTrayState.TabGroupState(
                                groups = listOf(originalGroup),
                                formState = editFormState,
                            )
                    ),
            )

        store.dispatch(TabGroupAction.SaveClicked)

        assertNotNull(TabsTray.tabGroupNameChanged.testGetValue())
    }

    @Test
    fun `GIVEN editing an existing group WHEN SaveClicked is dispatched with the same name THEN tabGroupNameChanged is NOT reported`() {
        assertNull(TabsTray.tabGroupNameChanged.testGetValue())

        val groupId = "existing_id"
        val originalGroup =
            TabsTrayItem.TabGroup(
                id = groupId,
                title = "Same Name",
                theme = TabGroupTheme.default,
                tabs = mutableListOf(),
            )
        val editFormState =
            TabGroupFormState(
                tabGroupId = groupId,
                name = "Same Name",
                edited = true,
            )
        store =
            TabsTrayStore(
                middlewares = listOf(tabsTrayTelemetryMiddleware),
                initialState =
                    TabsTrayState(
                        tabGroupState =
                            TabsTrayState.TabGroupState(
                                groups = listOf(originalGroup),
                                formState = editFormState,
                            )
                    ),
            )

        store.dispatch(TabGroupAction.SaveClicked)

        assertNull(TabsTray.tabGroupNameChanged.testGetValue())
    }

    @Test
    fun `GIVEN creating a new group WHEN ThemeChanged is dispatched THEN tabGroupColorAssigned is reported`() {
        assertNull(TabsTray.tabGroupColorAssigned.testGetValue())

        val createFormState =
            TabGroupFormState(
                tabGroupId = null,
                name = "Group",
            )
        store =
            TabsTrayStore(
                middlewares = listOf(tabsTrayTelemetryMiddleware),
                initialState = TabsTrayState(tabGroupState = TabsTrayState.TabGroupState(formState = createFormState)),
            )

        store.dispatch(TabGroupAction.ThemeChanged(TabGroupTheme.Red))

        assertNotNull(TabsTray.tabGroupColorAssigned.testGetValue())
        val snapshot = TabsTray.tabGroupColorAssigned.testGetValue()!!
        assertEquals(TabGroupTheme.Red.name, snapshot.single().extra?.getValue("color"))
    }

    @Test
    fun `GIVEN editing an existing group WHEN ThemeChanged is dispatched THEN tabGroupColorChanged is reported`() {
        assertNull(TabsTray.tabGroupColorChanged.testGetValue())

        val editFormState =
            TabGroupFormState(
                tabGroupId = "existing_id",
                name = "Group",
            )
        store =
            TabsTrayStore(
                middlewares = listOf(tabsTrayTelemetryMiddleware),
                initialState = TabsTrayState(tabGroupState = TabsTrayState.TabGroupState(formState = editFormState)),
            )

        store.dispatch(TabGroupAction.ThemeChanged(TabGroupTheme.Yellow))

        assertNotNull(TabsTray.tabGroupColorChanged.testGetValue())
        val snapshot = TabsTray.tabGroupColorChanged.testGetValue()!!
        assertEquals(TabGroupTheme.Yellow.name, snapshot.single().extra?.getValue("tab_group_color_changed"))
    }

    @Test
    fun `WHEN EditTabGroupClicked is dispatched THEN tabGroupEdited is reported`() {
        assertNull(TabsTray.tabGroupEdited.testGetValue())

        store.dispatch(
            TabGroupAction.EditTabGroupClicked(
                TabsTrayItem.TabGroup(
                    id = UUID.randomUUID().toString(),
                    title = "Test",
                    theme = TabGroupTheme.default,
                    tabs = mutableListOf(),
                )
            )
        )

        assertNotNull(TabsTray.tabGroupEdited.testGetValue())
    }
}
