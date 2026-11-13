/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
package org.mozilla.fenix.tabstray.ui.fab

import androidx.compose.ui.semantics.SemanticsActions
import androidx.compose.ui.semantics.getOrNull
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.text.TextLayoutResult
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.browser.state.state.createTab
import mozilla.components.compose.base.theme.Theme
import mozilla.components.compose.base.theme.acornDarkColorScheme
import mozilla.components.compose.base.theme.acornLightColorScheme
import mozilla.components.compose.base.theme.acornPrivateColorScheme
import mozilla.components.support.test.robolectric.testContext
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.mozilla.fenix.tabstray.TabsTrayTestTag
import org.mozilla.fenix.tabstray.TabsTrayTestTag.CLOSE_ALL_TABS
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.redux.state.Page
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState.Mode
import org.mozilla.fenix.tabstray.redux.store.TabsTrayStore
import org.mozilla.fenix.tabstray.syncedtabs.SyncedTabsListItem
import org.mozilla.fenix.theme.FirefoxTheme

@RunWith(AndroidJUnit4::class)
class TabManagerFloatingToolbarTest {
    @get:Rule val composeTestRule = createComposeRule()

    private val testTabs =
        listOf(
            TabsTrayItem.Tab(tab = createTab(url = "https://www.google.com", id = "a")),
            TabsTrayItem.Tab(tab = createTab(url = "https://www.duckduckgo.com", id = "b")),
        )

    @Test
    fun `Close all tabs menu item in light theme uses Error color`() {
        val initialState = TabsTrayState(normalTabsState = TabsTrayState.NormalTabsState(items = testTabs))

        setTestContent(initialState = initialState)

        composeTestRule.onNodeWithTag(TabsTrayTestTag.THREE_DOT_BUTTON).performClick()
        composeTestRule.onNodeWithTag(CLOSE_ALL_TABS).assertExists().assert(hasTextColor(acornLightColorScheme().error))
    }

    @Test
    fun `Close all tabs menu item in private theme uses Error color`() {
        val initialState = TabsTrayState(normalTabsState = TabsTrayState.NormalTabsState(items = testTabs))

        setTestContent(
            initialState = initialState,
            theme = Theme.Private,
        )

        composeTestRule.onNodeWithTag(TabsTrayTestTag.THREE_DOT_BUTTON).performClick()
        composeTestRule
            .onNodeWithTag(CLOSE_ALL_TABS)
            .assertExists()
            .assert(hasTextColor(acornPrivateColorScheme().error))
    }

    @Test
    fun `Close all tabs menu item in dark theme uses Error color`() {
        val initialState = TabsTrayState(normalTabsState = TabsTrayState.NormalTabsState(items = testTabs))

        setTestContent(
            initialState = initialState,
            theme = Theme.Dark,
        )

        composeTestRule.onNodeWithTag(TabsTrayTestTag.THREE_DOT_BUTTON).performClick()
        composeTestRule.onNodeWithTag(CLOSE_ALL_TABS).assertExists().assert(hasTextColor(acornDarkColorScheme().error))
    }

    @Test
    fun `Select all tabs menu item is not displayed on private tabs page`() {
        val initialState =
            TabsTrayState(
                selectedPage = Page.PrivateTabs,
                privateBrowsing = TabsTrayState.PrivateBrowsingState(tabs = testTabs),
            )

        setTestContent(initialState = initialState)

        composeTestRule.onNodeWithTag(TabsTrayTestTag.THREE_DOT_BUTTON).performClick()
        composeTestRule.onNodeWithTag(TabsTrayTestTag.SELECT_ALL_TABS).assertDoesNotExist()
    }

    @Test
    fun `Select all tabs menu item is not displayed on synced tabs page`() {
        val initialState =
            TabsTrayState(
                selectedPage = Page.SyncedTabs,
                privateBrowsing = TabsTrayState.PrivateBrowsingState(tabs = testTabs),
            )

        setTestContent(initialState = initialState)

        composeTestRule.onNodeWithTag(TabsTrayTestTag.THREE_DOT_BUTTON).performClick()
        composeTestRule.onNodeWithTag(TabsTrayTestTag.SELECT_ALL_TABS).assertDoesNotExist()
    }

    @Test
    fun `GIVEN user is not signed in WHEN on synced tabs page THEN clicking FAB does not trigger sync`() {
        var clicked = false
        val initialState =
            TabsTrayState(
                selectedPage = Page.SyncedTabs,
                sync = TabsTrayState.SyncState(isSignedIn = false),
            )

        setTestContent(
            initialState = initialState,
            onSyncedTabsFabClicked = { clicked = true },
        )

        composeTestRule.onNodeWithTag(TabsTrayTestTag.FAB).performClick()

        assert(!clicked)
    }

    @Test
    fun `GIVEN reauth error exists WHEN on synced tabs page THEN clicking FAB does not trigger sync`() {
        val reauthErrorString = testContext.getString(R.string.synced_tabs_reauth)
        var clicked = false
        val initialState =
            TabsTrayState(
                selectedPage = Page.SyncedTabs,
                sync =
                    TabsTrayState.SyncState(
                        syncedTabs = listOf(SyncedTabsListItem.Error(errorText = reauthErrorString)),
                        isSignedIn = false,
                    ),
            )

        setTestContent(
            initialState = initialState,
            onSyncedTabsFabClicked = { clicked = true },
        )

        composeTestRule.onNodeWithTag(TabsTrayTestTag.FAB).performClick()

        assert(!clicked)
    }

    @Test
    fun `GIVEN user is signed in and no errors WHEN on synced tabs page THEN clicking FAB triggers sync`() {
        var clicked = false
        val initialState =
            TabsTrayState(
                selectedPage = Page.SyncedTabs,
                sync = TabsTrayState.SyncState(syncedTabs = emptyList(), isSignedIn = true),
            )

        setTestContent(
            initialState = initialState,
            onSyncedTabsFabClicked = { clicked = true },
        )

        composeTestRule.onNodeWithTag(TabsTrayTestTag.FAB).assertIsDisplayed().performClick()

        assert(clicked)
    }

    @Test
    fun `GIVEN mode is Select WHEN toolbar is rendered THEN it is hidden`() {
        setTestContent(
            initialState =
                TabsTrayState(
                    mode = Mode.Select(),
                    selectedPage = Page.NormalTabs,
                )
        )

        composeTestRule.onNodeWithTag(TabsTrayTestTag.FAB).assertDoesNotExist()
    }

    private fun setTestContent(
        initialState: TabsTrayState,
        theme: Theme = Theme.Light,
        onSyncedTabsFabClicked: () -> Unit = {},
    ) {
        val tabsTrayStore = TabsTrayStore(initialState = initialState)

        composeTestRule.setContent {
            FirefoxTheme(theme = theme) {
                TabManagerFloatingToolbar(
                    state = tabsTrayStore.state,
                    onAction = tabsTrayStore::dispatch,
                    onOpenNewNormalTabClicked = {},
                    onOpenNewPrivateTabClicked = {},
                    onSyncedTabsFabClicked = onSyncedTabsFabClicked,
                    onTabSettingsClick = {},
                    onAccountSettingsClick = {},
                    onDeleteAllTabsClick = {},
                    onRecentlyClosedClick = {},
                )
            }
        }
    }

    @Test
    fun `GIVEN on tab groups page WHEN clicking the FAB THEN the tab groups fab callback is invoked`() {
        var clicked = false
        val state = TabsTrayState(selectedPage = Page.TabGroups)

        composeTestRule.setContent {
            FloatingToolbarFAB(
                state = state,
                onOpenNewNormalTabClicked = {},
                onOpenNewPrivateTabClicked = {},
                onSyncedTabsFabClicked = {},
                onTabGroupsFabClicked = { clicked = true },
            )
        }

        composeTestRule.onNodeWithTag(TabsTrayTestTag.FAB).assertIsDisplayed().performClick()

        assert(clicked)
    }

    @Test
    fun `GIVEN homepage as new tab is enabled WHEN on the normal tabs menu THEN the new tab group item is shown`() {
        val initialState =
            TabsTrayState(
                normalTabsState = TabsTrayState.NormalTabsState(items = testTabs),
                config = TabsTrayState.TabsTrayConfig(homepageAsNewTabEnabled = true),
            )

        setTestContent(initialState = initialState)

        composeTestRule.onNodeWithTag(TabsTrayTestTag.THREE_DOT_BUTTON).performClick()
        composeTestRule.onNodeWithTag(TabsTrayTestTag.NEW_TAB_GROUP).assertExists()
    }

    @Test
    fun `GIVEN homepage as new tab is disabled WHEN on the normal tabs menu THEN the new tab group item is not shown`() {
        val initialState =
            TabsTrayState(
                normalTabsState = TabsTrayState.NormalTabsState(items = testTabs),
                config = TabsTrayState.TabsTrayConfig(homepageAsNewTabEnabled = false),
            )

        setTestContent(initialState = initialState)

        composeTestRule.onNodeWithTag(TabsTrayTestTag.THREE_DOT_BUTTON).performClick()
        composeTestRule.onNodeWithTag(TabsTrayTestTag.NEW_TAB_GROUP).assertDoesNotExist()
    }

    private fun hasTextColor(color: androidx.compose.ui.graphics.Color) =
        SemanticsMatcher("Has text color matching $color") { node ->
            val textLayoutResults = mutableListOf<TextLayoutResult>()
            node.config.getOrNull(SemanticsActions.GetTextLayoutResult)?.action?.invoke(textLayoutResults) == true &&
                textLayoutResults.first().layoutInput.style.color == color
        }
}
