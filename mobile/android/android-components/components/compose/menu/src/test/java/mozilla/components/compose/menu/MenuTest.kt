/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.width
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.test.assertCountEquals
import androidx.compose.ui.test.assertHeightIsAtLeast
import androidx.compose.ui.test.assertHeightIsEqualTo
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.getUnclippedBoundsInRoot
import androidx.compose.ui.test.hasScrollAction
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onAllNodesWithContentDescription
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollToNode
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.height
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.R as MozacMenuR
import mozilla.components.compose.menu.data.BannerMenuItem
import mozilla.components.compose.menu.data.MenuAttribution
import mozilla.components.compose.menu.data.MenuItemSummary
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuAction
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.store.MenuState
import mozilla.components.compose.menu.store.MenuStore
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.lib.state.Middleware
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.ui.icons.R as iconsR
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class MenuTest {
    @get:Rule val composeTestRule = createComposeRule()

    @Test
    fun `GIVEN a short menu with a footer THEN wrap content instead of filling the maximum height`() {
        setMenu(listOf(row("Item"), stickyGroup("Footer")))

        composeTestRule.onNodeWithText("Item").assertIsDisplayed()
        composeTestRule.onNodeWithContentDescription("Footer").assertIsDisplayed()
        val menuBounds = composeTestRule.onNodeWithTag("menu").getUnclippedBoundsInRoot()
        val listBounds = composeTestRule.onNode(hasScrollAction()).getUnclippedBoundsInRoot()
        val footerBounds = composeTestRule.onNodeWithContentDescription("Footer").getUnclippedBoundsInRoot()
        assertTrue(menuBounds.height < 300.dp)
        assertTrue(listBounds.bottom <= footerBounds.top)
    }

    @Test
    fun `GIVEN a long menu WHEN scrolling to the end THEN keep header and footer visible without covering content`() {
        setMenu(listOf(stickyGroup("Header")) + List(20) { row("Item $it") } + stickyGroup("Footer"))
        val headerBefore = composeTestRule.onNodeWithContentDescription("Header").getUnclippedBoundsInRoot()
        val footerBefore = composeTestRule.onNodeWithContentDescription("Footer").getUnclippedBoundsInRoot()

        composeTestRule.onNode(hasScrollAction()).performScrollToNode(hasText("Item 19"))

        composeTestRule.onNodeWithContentDescription("Header").assertIsDisplayed()
        composeTestRule.onNodeWithContentDescription("Footer").assertIsDisplayed()
        composeTestRule.onNodeWithText("Item 19").assertIsDisplayed()
        val headerAfter = composeTestRule.onNodeWithContentDescription("Header").getUnclippedBoundsInRoot()
        val footerAfter = composeTestRule.onNodeWithContentDescription("Footer").getUnclippedBoundsInRoot()
        val listBounds = composeTestRule.onNode(hasScrollAction()).getUnclippedBoundsInRoot()
        val lastItemBounds = composeTestRule.onNodeWithText("Item 19").getUnclippedBoundsInRoot()
        assertEquals(headerBefore.top, headerAfter.top)
        assertEquals(footerBefore.bottom, footerAfter.bottom)
        assertTrue(listBounds.bottom <= footerAfter.top)
        assertTrue(lastItemBounds.bottom <= listBounds.bottom)
    }

    @Test
    fun `WHEN a footer is added and removed THEN restore the original menu height`() {
        val groups = listOf(row("Item"))
        val store = setMenu(groups)
        val originalHeight = composeTestRule.onNodeWithTag("menu").getUnclippedBoundsInRoot().height

        composeTestRule.runOnIdle { store.dispatch(MenuAction.Update(groups + stickyGroup("Footer"))) }

        composeTestRule.onNodeWithContentDescription("Footer").assertIsDisplayed()
        assertTrue(composeTestRule.onNodeWithTag("menu").getUnclippedBoundsInRoot().height > originalHeight)

        composeTestRule.runOnIdle { store.dispatch(MenuAction.Update(groups)) }

        composeTestRule.onNodeWithContentDescription("Footer").assertDoesNotExist()
        assertEquals(originalHeight, composeTestRule.onNodeWithTag("menu").getUnclippedBoundsInRoot().height)
    }

    @Test
    fun `GIVEN a navigation only sticky header THEN keep the spacing around it`() {
        setMenu(listOf(stickyGroup("Header"), row("Item")))

        val menuBounds = composeTestRule.onNodeWithTag("menu").getUnclippedBoundsInRoot()
        val headerBounds = composeTestRule.onNodeWithContentDescription("Header").getUnclippedBoundsInRoot()
        val listBounds = composeTestRule.onNode(hasScrollAction()).getUnclippedBoundsInRoot()
        assertEquals(12f, (headerBounds.top - menuBounds.top).value, 0.5f)
        assertEquals(8f, (listBounds.top - headerBounds.bottom).value, 0.5f)
    }

    @Test
    fun `GIVEN a sticky header followed by a banner THEN space the banner from the header`() {
        val bannerGroup = MenuItemsGroup.Row(id = "banner", items = listOf(banner))
        val store = setMenu(listOf(bannerGroup, row("Item")))
        val offsetWithoutHeader = bannerOffsetInList()

        composeTestRule.runOnIdle {
            store.dispatch(MenuAction.Update(listOf(stickyGroup("Header"), bannerGroup, row("Item"))))
        }

        assertEquals(offsetWithoutHeader, bannerOffsetInList(), 0.5f)
    }

    @Test
    fun `GIVEN a single sticky group THEN show it only once`() {
        setMenu(listOf(stickyGroup("Header")))

        composeTestRule.onAllNodesWithContentDescription("Header").assertCountEquals(1)
        composeTestRule.onNodeWithContentDescription("Header").assertIsDisplayed()
        assertTrue(composeTestRule.onNodeWithTag("menu").getUnclippedBoundsInRoot().height < 300.dp)
    }

    @Test
    fun `GIVEN a sticky grid group THEN do not display visible text and provide it as content description`() {
        setMenu(listOf(stickyGroup("StickyItem")))

        composeTestRule.onNodeWithContentDescription("StickyItem").assertIsDisplayed()
        composeTestRule.onNode(hasText("StickyItem"), useUnmergedTree = true).assertDoesNotExist()
    }

    @Test
    fun `GIVEN top attribution WHEN scrolling THEN keep it above the sticky header`() {
        assertAttributionStaysOutsideScrollingContent(showAtTop = true)
    }

    @Test
    fun `GIVEN bottom attribution WHEN scrolling THEN keep it below the sticky footer`() {
        assertAttributionStaysOutsideScrollingContent(showAtTop = false)
    }

    @Test
    fun `GIVEN a short menu with attribution THEN wrap content and preserve attribution when items update`() {
        val store =
            setMenu(
                groups = listOf(row("Item")),
                attribution = MenuAttribution(Text.String("Powered by Focus"), showAtTop = false, icon = null),
            )

        composeTestRule.onNodeWithText("Powered by Focus").assertIsDisplayed()
        assertTrue(composeTestRule.onNodeWithTag("menu").getUnclippedBoundsInRoot().height < 300.dp)

        composeTestRule.runOnIdle { store.dispatch(MenuAction.Update(listOf(row("Updated item")))) }

        composeTestRule.onNodeWithText("Updated item").assertIsDisplayed()
        composeTestRule.onNodeWithText("Powered by Focus").assertIsDisplayed()
    }

    private fun assertAttributionStaysOutsideScrollingContent(showAtTop: Boolean) {
        setMenu(
            groups = listOf(stickyGroup("Header")) + List(20) { row("Item $it") } + stickyGroup("Footer"),
            attribution = MenuAttribution(Text.String("Powered by Focus"), showAtTop = showAtTop, icon = null),
        )
        val attributionBefore = composeTestRule.onNodeWithText("Powered by Focus").getUnclippedBoundsInRoot()

        composeTestRule.onNode(hasScrollAction()).performScrollToNode(hasText("Item 19"))

        composeTestRule.onNodeWithText("Powered by Focus").assertIsDisplayed()
        composeTestRule.onNodeWithContentDescription("Header").assertIsDisplayed()
        composeTestRule.onNodeWithContentDescription("Footer").assertIsDisplayed()
        composeTestRule.onNodeWithText("Item 19").assertIsDisplayed()
        val attributionAfter = composeTestRule.onNodeWithText("Powered by Focus").getUnclippedBoundsInRoot()
        val headerBounds = composeTestRule.onNodeWithContentDescription("Header").getUnclippedBoundsInRoot()
        val footerBounds = composeTestRule.onNodeWithContentDescription("Footer").getUnclippedBoundsInRoot()
        val listBounds = composeTestRule.onNode(hasScrollAction()).getUnclippedBoundsInRoot()
        val lastItemBounds = composeTestRule.onNodeWithText("Item 19").getUnclippedBoundsInRoot()
        assertEquals(attributionBefore, attributionAfter)
        assertTrue(listBounds.bottom <= footerBounds.top)
        assertTrue(lastItemBounds.bottom <= listBounds.bottom)
        if (showAtTop) {
            assertTrue(attributionAfter.bottom <= headerBounds.top)
        } else {
            assertTrue(footerBounds.bottom <= attributionAfter.top)
        }
    }

    @Test
    fun `GIVEN a banner in a list WHEN showing the menu THEN show its title and subtitle`() {
        setMenu(listOf(MenuItemsGroup.Row(id = "banner", items = listOf(banner))))

        composeTestRule.onNodeWithText(BANNER_TITLE).assertIsDisplayed()
        composeTestRule.onNodeWithText(BANNER_SUBTITLE).assertIsDisplayed()
    }

    @Test
    fun `WHEN clicking a banner THEN dispatch its click event`() {
        val events = mutableListOf<MenuAction>()
        setMenu(listOf(MenuItemsGroup.Row(id = "banner", items = listOf(banner))), middleware = recording(events))

        composeTestRule.onNodeWithText(BANNER_TITLE).performClick()

        assertEquals(listOf<MenuAction>(BannerClicked), events.filterIsInstance<MenuEvent>())
    }

    @Test
    fun `WHEN clicking the button of a banner THEN dispatch its click event`() {
        val events = mutableListOf<MenuAction>()
        setMenu(listOf(MenuItemsGroup.Row(id = "banner", items = listOf(banner))), middleware = recording(events))

        composeTestRule.onNodeWithText(BANNER_BUTTON).performClick()

        assertEquals(listOf<MenuAction>(BannerClicked), events.filterIsInstance<MenuEvent>())
    }

    @Test
    fun `WHEN dismissing a banner THEN dispatch its dismiss event`() {
        val closeButtonContentDescription =
            testContext.getString(MozacMenuR.string.mozac_menu_banner_dismiss_description)
        val events = mutableListOf<MenuAction>()
        setMenu(listOf(MenuItemsGroup.Row(id = "banner", items = listOf(banner))), middleware = recording(events))

        composeTestRule.onNodeWithContentDescription(closeButtonContentDescription).performClick()

        assertEquals(listOf<MenuAction>(BannerDismissed), events.filterIsInstance<MenuEvent>())
    }

    @Test
    fun `GIVEN a banner in a grid WHEN showing the menu THEN don't show it`() {
        setMenu(listOf(MenuItemsGroup.Grid(id = "banner", items = listOf(banner)), row("Item")))

        composeTestRule.onNodeWithText("Item").assertIsDisplayed()
        composeTestRule.onNodeWithText(BANNER_TITLE).assertDoesNotExist()
    }

    @Test
    fun `GIVEN a grid not showing titles in the middle of the menu THEN show its items without titles`() {
        setMenu(listOf(row("Item"), MenuItemsGroup.Grid(id = "grid", items = listOf(item("Grid")), showTitles = false)))

        composeTestRule.onNodeWithContentDescription("Grid").assertIsDisplayed()
        composeTestRule.onNode(hasText("Grid"), useUnmergedTree = true).assertDoesNotExist()
    }

    @Test
    fun `GIVEN a sticky grid item with a content description THEN announce it instead of its hidden title`() {
        val stickyItem = item("Bookmark").copy(contentDescription = Text.String("Bookmarked"))
        setMenu(listOf(MenuItemsGroup.Grid(id = "header", items = listOf(stickyItem), isSticky = true), row("Item")))

        composeTestRule.onNodeWithContentDescription("Bookmarked").assertIsDisplayed()
        composeTestRule.onNodeWithContentDescription("Bookmark").assertDoesNotExist()
        composeTestRule.onNode(hasText("Bookmark"), useUnmergedTree = true).assertDoesNotExist()
    }

    @Test
    fun `GIVEN a grid in the middle of the menu THEN show the titles of its items`() {
        setMenu(listOf(row("Item"), MenuItemsGroup.Grid(id = "grid", items = listOf(item("Grid")))))

        composeTestRule.onNodeWithText("Grid").assertIsDisplayed()
    }

    @Test
    fun `GIVEN a grid without titles moved between other sections THEN its items are 48dp tall`() {
        setMenu(listOf(row("Top"), iconsGrid(), row("Bottom")))

        composeTestRule.onNodeWithContentDescription("Icon 1").assertHeightIsEqualTo(48.dp)
    }

    @Test
    fun `GIVEN a grid with titles between other sections THEN its items keep space above and below their content`() {
        setMenu(listOf(row("Top"), iconsGrid(showTitles = true), row("Bottom")))

        composeTestRule.onNode(hasText("Icon 1")).assertHeightIsAtLeast(52.dp)
    }

    @Test
    fun `GIVEN a grid without titles as the sticky header THEN its items stay 48dp tall`() {
        setMenu(listOf(iconsGrid(), row("Item")))

        composeTestRule.onNodeWithContentDescription("Icon 1").assertHeightIsEqualTo(48.dp)
    }

    @Test
    fun `WHEN menu with sticky header is scrollable THEN report dark header background`() {
        var isHeaderDark = false
        val groups = listOf(stickyGroup("Header")) + List(20) { row("Item $it") }
        val store = MenuStore(initialState = MenuState(groups))
        composeTestRule.setContent {
            AcornTheme {
                Box(Modifier.fillMaxSize()) {
                    Menu(
                        store = store,
                        modifier = Modifier.width(320.dp).heightIn(max = 300.dp),
                        onHeaderDarkBackgroundChanged = { isHeaderDark = it },
                    )
                }
            }
        }

        composeTestRule.runOnIdle {
            assertTrue(isHeaderDark)
        }
    }

    private fun bannerOffsetInList(): Float {
        val listBounds = composeTestRule.onNode(hasScrollAction()).getUnclippedBoundsInRoot()
        val bannerBounds = composeTestRule.onNodeWithText(BANNER_TITLE).getUnclippedBoundsInRoot()
        return (bannerBounds.top - listBounds.top).value
    }

    private fun recording(events: MutableList<MenuAction>): List<Middleware<MenuState, MenuAction>> =
        listOf { _, next, action ->
            events += action
            next(action)
        }

    private fun setMenu(
        groups: List<MenuItemsGroup>,
        attribution: MenuAttribution? = null,
        middleware: List<Middleware<MenuState, MenuAction>> = emptyList(),
    ): MenuStore {
        val store = MenuStore(initialState = MenuState(groups, attribution), middleware = middleware)
        composeTestRule.setContent {
            AcornTheme {
                Box(Modifier.fillMaxSize()) {
                    Menu(store, Modifier.width(320.dp).heightIn(max = 300.dp).testTag("menu"))
                }
            }
        }
        return store
    }

    private fun row(title: String) = MenuItemsGroup.Row(id = title, items = listOf(item(title)))

    private fun stickyGroup(title: String) =
        MenuItemsGroup.Grid(id = title, items = listOf(item(title)), isSticky = true)

    private fun iconsGrid(showTitles: Boolean = false) =
        MenuItemsGroup.Grid(
            id = "icons",
            items = (1..5).map { item("Icon $it") },
            isSticky = true,
            showTitles = showTitles,
        )

    private data object BannerClicked : MenuEvent

    private data object BannerDismissed : MenuEvent

    private val banner =
        BannerMenuItem(
            title = MenuItemTitle(Text.String(BANNER_TITLE)),
            summary = MenuItemSummary(Text.String(BANNER_SUBTITLE)),
            icon = MenuItemIconRes(iconsR.drawable.mozac_ic_globe_24),
            actionButtonText = Text.String(BANNER_BUTTON),
            onClickEvent = BannerClicked,
            onDismissEvent = BannerDismissed,
        )

    private fun item(title: String) =
        StandardMenuItem(
            title = MenuItemTitle(Text.String(title)),
            icon = MenuItemIconRes(iconsR.drawable.mozac_ic_settings_24),
            onClickEvent = object : MenuEvent {},
        )

    private companion object {
        const val BANNER_TITLE = "Banner title"
        const val BANNER_SUBTITLE = "Banner subtitle"
        const val BANNER_BUTTON = "Banner button"
    }
}
