/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups

import androidx.navigation.NavController
import io.mockk.mockk
import io.mockk.verify
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import mozilla.components.browser.state.state.createTab
import mozilla.components.concept.engine.prompt.ShareData
import mozilla.components.support.test.robolectric.testContext
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.TabsTray
import org.mozilla.fenix.components.share.ShareSource
import org.mozilla.fenix.components.usecases.ShareUseCases
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.data.createTabGroup
import org.mozilla.fenix.tabstray.redux.action.TabGroupAction
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class) // for gleanTestRule
class ExpandedTabGroupActionsBuilderTest {

    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    private val tab1 = TabsTrayItem.Tab(tab = createTab(url = "https://mozilla.org", title = "Mozilla"))
    private val tab2 = TabsTrayItem.Tab(tab = createTab(url = "https://firefox.com", title = "Firefox"))
    private val group = createTabGroup(title = "My group", tabs = listOf(tab1, tab2))

    private val dispatched = mutableListOf<TabGroupAction>()

    private fun buildActions(
        homepageAsNewTabEnabled: Boolean = true,
        addNewHomepageTab: () -> String = { "new-tab" },
        onTabClick: (TabsTrayItem.Tab) -> Unit = {},
        onShareClick: () -> Unit = {},
        onAddNewTabCompleted: () -> Unit = {},
    ) =
        expandedTabGroupActions(
            group = group,
            homepageAsNewTabEnabled = homepageAsNewTabEnabled,
            dispatch = { dispatched.add(it) },
            addNewHomepageTab = addNewHomepageTab,
            onTabClick = onTabClick,
            onShareClick = onShareClick,
            onAddNewTabCompleted = onAddNewTabCompleted,
        )

    @Test
    fun `WHEN a tab is closed THEN TabClosed is dispatched for that tab and group`() {
        buildActions().onTabClose(tab1)

        assertEquals(
            expected = listOf<TabGroupAction>(TabGroupAction.TabClosed(tab = tab1, group = group)),
            actual = dispatched,
        )
    }

    @Test
    fun `WHEN delete is clicked THEN DeleteClicked is dispatched for the group`() {
        buildActions().onDeleteTabGroupClick()

        assertEquals(expected = listOf<TabGroupAction>(TabGroupAction.DeleteClicked(group)), actual = dispatched)
    }

    @Test
    fun `WHEN edit is clicked THEN EditTabGroupClicked is dispatched for the group`() {
        buildActions().onEditTabGroupClick()

        assertEquals(
            expected = listOf<TabGroupAction>(TabGroupAction.EditTabGroupClicked(group = group)),
            actual = dispatched,
        )
    }

    @Test
    fun `WHEN close group is clicked THEN CloseTabGroupClicked is dispatched for the group`() {
        buildActions().onCloseTabGroupClick()

        assertEquals(
            expected = listOf<TabGroupAction>(TabGroupAction.CloseTabGroupClicked(group = group)),
            actual = dispatched,
        )
    }

    @Test
    fun `WHEN ungroup is clicked THEN UngroupRequested is dispatched for the group`() {
        buildActions().onUngroupTabGroupClick()

        assertEquals(
            expected = listOf<TabGroupAction>(TabGroupAction.UngroupRequested(group = group)),
            actual = dispatched,
        )
    }

    @Test
    fun `WHEN a tab item is clicked THEN onTabClick is invoked with that tab`() {
        var clicked: TabsTrayItem.Tab? = null
        buildActions(onTabClick = { clicked = it }).onItemClick(tab2)

        assertEquals(expected = tab2, actual = clicked)
        assertTrue(dispatched.isEmpty())
    }

    @Test
    fun `WHEN share is clicked THEN onShareClick is invoked`() {
        var shared = false
        buildActions(onShareClick = { shared = true }).onShareTabGroupClick()

        assertTrue(shared)
    }

    @Test
    fun `GIVEN HNT is enabled WHEN add tab is clicked THEN a new tab is opened and added to the group`() {
        var added = false
        val actions =
            buildActions(
                homepageAsNewTabEnabled = true,
                addNewHomepageTab = { "new-tab" },
                onAddNewTabCompleted = { added = true },
            )

        assertNotNull(actions.onAddNewTabClick).invoke()

        assertEquals(
            expected =
                listOf<TabGroupAction>(
                    TabGroupAction.TabAddedToExistingTabGroup(tabId = "new-tab", groupId = group.id)
                ),
            actual = dispatched,
        )
        assertTrue(added)
    }

    @Test
    fun `GIVEN HNT is disabled WHEN building actions THEN add tab is not available`() {
        assertNull(buildActions(homepageAsNewTabEnabled = false).onAddNewTabClick)
    }

    @Test
    fun `WHEN a tab group is shared THEN telemetry is recorded and the group's tabs are shared`() {
        val shareUseCases = mockk<ShareUseCases>(relaxed = true)

        shareTabGroup(
            context = testContext,
            scope = CoroutineScope(Dispatchers.Unconfined),
            navController = mockk<NavController>(relaxed = true),
            shareUseCases = shareUseCases,
            group = group,
            dotColor = 0xFF2196F3.toInt(),
        )

        val snapshot = TabsTray.shareTabGroup.testGetValue()!!
        assertEquals("2", snapshot.single().extra?.getValue("tab_count"))

        verify(timeout = 5000) {
            shareUseCases.shareItems(
                items =
                    listOf(
                        ShareData(url = tab1.url, title = tab1.title, private = tab1.private),
                        ShareData(url = tab2.url, title = tab2.title, private = tab2.private),
                    ),
                source = ShareSource.TABS_TRAY,
                isPrivate = false,
                subject = "My group",
                chooserActions = any(),
                thumbnailUri = any(),
                navigateToShareFragment = any(),
            )
        }
    }
}
