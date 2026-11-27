/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.annotation.StringRes
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.semantics.CollectionInfo
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.SemanticsActions
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.assertCountEquals
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.getUnclippedBoundsInRoot
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasScrollAction
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.isHeading
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.test.performScrollToKey
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.unit.dp
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.compose.base.R as baseR
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.R
import mozilla.components.compose.menu.data.CustomizableMenuItem
import mozilla.components.compose.menu.data.ExpandableMenuItem
import mozilla.components.compose.menu.data.MenuItemSummary
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.ItemsChanged
import mozilla.components.compose.menu.store.CustomizableMenuItemsState
import mozilla.components.compose.menu.store.CustomizableMenuItemsStore
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemState
import mozilla.components.lib.state.Middleware
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class EditMenuItemsAccessibilityTest {
    @get:Rule val composeTestRule = createComposeRule()

    private val dragToReorder = testContext.getString(R.string.mozac_menu_customize_drag_items_to_reorder)
    private val hiddenItemsTitle = testContext.getString(R.string.mozac_menu_customize_hidden_items)
    private val dragToHide = testContext.getString(R.string.mozac_menu_customize_drag_items_to_hide)
    private val preview = testContext.getString(R.string.mozac_menu_customize_preview)
    private val expand = testContext.getString(R.string.mozac_menu_customize_expand)
    private val collapse = testContext.getString(R.string.mozac_menu_customize_collapse)
    private val expandedState = testContext.getString(baseR.string.mozac_compose_base_a11y_state_label_expanded)
    private val collapsedState = testContext.getString(baseR.string.mozac_compose_base_a11y_state_label_collapsed)
    private val moveUp = testContext.getString(R.string.mozac_menu_customize_move_up)
    private val moveDown = testContext.getString(R.string.mozac_menu_customize_move_down)
    private val hide = testContext.getString(R.string.mozac_menu_customize_hide_item)
    private val unhide = testContext.getString(R.string.mozac_menu_customize_unhide_item)

    @Test
    fun `THEN the titles above the shown items and above the hidden items are headings`() {
        setContent(CustomizableMenuItemsState(items = listOf(item("Shown"))))

        composeTestRule.onNodeWithText(dragToReorder).assert(isHeading())
        composeTestRule.onNodeWithText(hiddenItemsTitle).assert(isHeading())
    }

    @Test
    fun `THEN preview is a button`() {
        setContent(CustomizableMenuItemsState(items = listOf(item("Shown"))))

        composeTestRule
            .onNodeWithText(preview)
            .assert(SemanticsMatcher.expectValue(SemanticsProperties.Role, Role.Button))
    }

    @Test
    fun `GIVEN no hidden items THEN the text of the area to hide items is read`() {
        setContent(CustomizableMenuItemsState(items = listOf(item("Shown"))))

        composeTestRule.onNodeWithText(dragToHide).assertIsDisplayed()
    }

    @Test
    fun `THEN the list does not tell how many rows it has, which are not all items`() {
        setContent(CustomizableMenuItemsState(items = listOf(item("First"), item("Second"))))

        composeTestRule
            .onNode(hasScrollAction())
            .assert(SemanticsMatcher.expectValue(SemanticsProperties.CollectionInfo, CollectionInfo(-1, 1)))
    }

    @Test
    fun `THEN each shown item is a single node reading its name and its position among the shown items`() {
        setContent(CustomizableMenuItemsState(items = listOf(item("First"), item("Second")), hiddenItems = emptyList()))

        assertSingleNode("First", shownItem("First", 1, 2), "First")
        assertSingleNode("Second", shownItem("Second", 2, 2), "Second")
    }

    @Test
    fun `GIVEN an expanded item THEN it and each of its sub-items are a single node`() {
        setContent(
            CustomizableMenuItemsState(
                items = listOf(expandable("More", item("Sub")), item("Last")),
                expandedItemIds = setOf("More"),
            )
        )

        assertSingleNode("More", shownItem("More", 1, 3), "More")
        assertSingleNode("Sub", shownItem("Sub", 2, 3), "Sub")
    }

    @Test
    fun `GIVEN an item with an empty summary THEN it reads only its title and its position`() {
        val emptySummary =
            item("Extensions").let { it.copy(item = (it.item as StandardMenuItem).copy(summary = summary(""))) }
        setContent(CustomizableMenuItemsState(items = listOf(emptySummary)))

        composeTestRule.onNodeWithContentDescription(shownItem("Extensions", 1, 1)).assertExists()
    }

    @Test
    fun `GIVEN an item with a summary THEN it and its summary are a single node`() {
        val withSummary =
            item("Extensions").let {
                it.copy(item = (it.item as StandardMenuItem).copy(summary = summary("Ad blocker")))
            }
        setContent(CustomizableMenuItemsState(items = listOf(withSummary)))

        assertSingleNode("Extensions", shownItem("Extensions, Ad blocker", 1, 1), "Extensions", "Ad blocker")
    }

    @Test
    fun `GIVEN an expanded item THEN it and its sub-items are counted among the shown items`() {
        setContent(
            CustomizableMenuItemsState(
                items = listOf(item("First"), expandable("More", item("Sub 1"), item("Sub 2")), item("Last")),
                expandedItemIds = setOf("More"),
            )
        )

        composeTestRule.onNodeWithContentDescription(shownItem("First", 1, 5)).assertExists()
        composeTestRule.onNodeWithContentDescription(shownItem("More", 2, 5)).assertExists()
        composeTestRule.onNodeWithContentDescription(shownItem("Sub 1", 3, 5)).assertExists()
        composeTestRule.onNodeWithContentDescription(shownItem("Sub 2", 4, 5)).assertExists()
        composeTestRule.onNodeWithContentDescription(shownItem("Last", 5, 5)).assertExists()
    }

    @Test
    fun `GIVEN a collapsed item THEN its sub-items are not counted among the shown items`() {
        setContent(
            CustomizableMenuItemsState(
                items = listOf(item("First"), expandable("More", item("Sub 1"), item("Sub 2")), item("Last"))
            )
        )

        composeTestRule.onNodeWithContentDescription(shownItem("First", 1, 3)).assertExists()
        composeTestRule.onNodeWithContentDescription(shownItem("More", 2, 3)).assertExists()
        composeTestRule.onNodeWithContentDescription(shownItem("Last", 3, 3)).assertExists()
    }

    @Test
    fun `GIVEN items the app does not show THEN they are not counted among the shown items`() {
        setContent(
            CustomizableMenuItemsState(
                items =
                    listOf(
                        CustomizableMenuItem(id = "app hidden", item = null),
                        item("First"),
                        CustomizableMenuItem(id = "other app hidden", item = null),
                        item("Second"),
                    )
            )
        )

        composeTestRule.onNodeWithContentDescription(shownItem("First", 1, 2)).assertExists()
        composeTestRule.onNodeWithContentDescription(shownItem("Second", 2, 2)).assertExists()
    }

    @Test
    fun `GIVEN an item with a summary THEN it reads its title then its summary`() {
        val withSummary =
            item("Extensions").let {
                it.copy(item = (it.item as StandardMenuItem).copy(summary = summary("Ad blocker, Dark mode")))
            }
        setContent(CustomizableMenuItemsState(items = listOf(withSummary)))

        composeTestRule
            .onNodeWithContentDescription(shownItem("Extensions, Ad blocker, Dark mode", 1, 1))
            .assertExists()
    }

    @Test
    fun `GIVEN an item with a content description THEN it reads that instead of its title and summary`() {
        val described =
            item("Extensions").let {
                it.copy(
                    item =
                        (it.item as StandardMenuItem).copy(
                            contentDescription = Text.String("Add-ons"),
                            summary = summary("Ad blocker"),
                        )
                )
            }
        setContent(CustomizableMenuItemsState(items = listOf(described)))

        composeTestRule.onNodeWithContentDescription(shownItem("Add-ons", 1, 1)).assertExists()
    }

    @Test
    fun `THEN a hidden item reads only its name AND it is not disabled`() {
        setContent(
            CustomizableMenuItemsState(
                items = listOf(item("Shown")),
                hiddenItems = listOf(item("Hidden").withState(MenuItemState.DISABLED)),
            )
        )

        composeTestRule
            .onNodeWithContentDescription("Hidden")
            .assert(SemanticsMatcher.keyNotDefined(SemanticsProperties.Disabled))
        composeTestRule.onNodeWithText("Hidden").assertDoesNotExist()
    }

    @Test
    fun `THEN an item which is not expandable has only the custom actions moving, moving into the expandable item and hiding it`() {
        setContent(CustomizableMenuItemsState(items = listOf(item("Shown"), expandable("More", item("Sub")))))

        assertEquals(listOf(moveDown, moveInto("More"), hide), actionLabelsOf("Shown"))
    }

    @Test
    fun `GIVEN a collapsed expandable item THEN its custom actions are to expand it, then to move and hide it`() {
        setContent(CustomizableMenuItemsState(items = listOf(item("Shown"), expandable("More", item("Sub")))))

        assertEquals(listOf(expand, moveUp, hide), actionLabelsOf("More"))
    }

    @Test
    fun `GIVEN an expanded expandable item THEN its only custom action is to collapse it`() {
        setContent(
            CustomizableMenuItemsState(
                items = listOf(item("Shown"), expandable("More", item("Sub"))),
                expandedItemIds = setOf("More"),
            )
        )

        assertEquals(listOf(collapse), actionLabelsOf("More"))
    }

    @Test
    fun `WHEN the custom action of a collapsed expandable item is done THEN it is expanded and announced`() {
        val store =
            setContent(CustomizableMenuItemsState(items = listOf(item("Shown"), expandable("More", item("Sub")))))

        performAction("More", expand)

        assertEquals(setOf("More"), store.state.expandedItemIds)
        announcementNode().assert(hasAnnouncement(R.string.mozac_menu_customize_item_expanded, "More"))
        composeTestRule.onNodeWithContentDescription(shownItem("Sub", 3, 3)).assertExists()
    }

    @Test
    fun `WHEN the custom action of an expanded expandable item is done THEN it is collapsed and announced`() {
        val store =
            setContent(
                CustomizableMenuItemsState(
                    items = listOf(item("Shown"), expandable("More", item("Sub"))),
                    expandedItemIds = setOf("More"),
                )
            )

        composeTestRule.onNodeWithContentDescription(shownItem("Sub", 3, 3)).assertExists()
        performAction("More", collapse)

        assertTrue(store.state.expandedItemIds.isEmpty())
        announcementNode().assert(hasAnnouncement(R.string.mozac_menu_customize_item_collapsed, "More"))
        composeTestRule.onNodeWithContentDescription(shownItem("Sub", 3, 3)).assertDoesNotExist()
        composeTestRule.onNodeWithContentDescription(shownItem("More", 2, 2)).assertExists()
    }

    @Test
    fun `WHEN the custom action of a hidden expandable item is done THEN it is expanded and announced`() {
        val store =
            setContent(
                CustomizableMenuItemsState(
                    items = listOf(item("Shown")),
                    hiddenItems = listOf(expandable("More", item("Sub"), state = MenuItemState.DISABLED)),
                )
            )

        assertEquals(listOf(expand, unhide), actionLabelsOf("More", isHidden = true))
        performAction("More", expand, isHidden = true)

        assertEquals(setOf("More"), store.state.expandedItemIds)
        announcementNode().assert(hasAnnouncement(R.string.mozac_menu_customize_item_expanded, "More"))
        composeTestRule.onNodeWithContentDescription("Sub").assertExists()
    }

    @Test
    fun `WHEN the custom action of a hidden expanded item is done THEN it is collapsed and announced`() {
        val store =
            setContent(
                CustomizableMenuItemsState(
                    items = listOf(item("Shown")),
                    hiddenItems = listOf(expandable("More", item("Sub"), state = MenuItemState.DISABLED)),
                    expandedItemIds = setOf("More"),
                )
            )

        composeTestRule.onNodeWithContentDescription("Sub").assertExists()
        assertEquals(listOf(collapse), actionLabelsOf("More", isHidden = true))
        performAction("More", collapse, isHidden = true)

        assertTrue(store.state.expandedItemIds.isEmpty())
        announcementNode().assert(hasAnnouncement(R.string.mozac_menu_customize_item_collapsed, "More"))
        composeTestRule.onNodeWithContentDescription("Sub").assertDoesNotExist()
    }

    @Test
    fun `GIVEN several shown items THEN each can be moved only where there is room to move`() {
        setContent(CustomizableMenuItemsState(items = listOf(item("First"), item("Second"), item("Third"))))

        assertEquals(listOf(moveDown, hide), actionLabelsOf("First"))
        assertEquals(listOf(moveUp, moveDown, hide), actionLabelsOf("Second"))
        assertEquals(listOf(moveUp, hide), actionLabelsOf("Third"))
    }

    @Test
    fun `GIVEN a single shown item THEN its only custom action is to hide it`() {
        setContent(CustomizableMenuItemsState(items = listOf(item("Only")), hiddenItems = listOf(item("Hidden"))))

        assertEquals(listOf(hide), actionLabelsOf("Only"))
    }

    @Test
    fun `GIVEN a hidden item THEN its only custom action is to unhide it`() {
        setContent(
            CustomizableMenuItemsState(
                items = listOf(item("First"), item("Second")),
                hiddenItems = listOf(item("Hidden 1"), item("Hidden 2")),
            )
        )

        assertEquals(listOf(unhide), actionLabelsOf("Hidden 1", isHidden = true))
        assertEquals(listOf(unhide), actionLabelsOf("Hidden 2", isHidden = true))
    }

    @Test
    fun `GIVEN a sub-item THEN it can only be moved inside the expanded item, moved out of it, or hidden`() {
        setContent(
            CustomizableMenuItemsState(
                items = listOf(item("First"), expandable("More", item("Sub 1"), item("Sub 2"))),
                expandedItemIds = setOf("More"),
            )
        )

        assertEquals(listOf(moveDown, moveOutOf("More"), hide), actionLabelsOf("Sub 1"))
        assertEquals(listOf(moveUp, moveOutOf("More"), hide), actionLabelsOf("Sub 2"))
    }

    @Test
    fun `WHEN an item is moved down THEN it is moved, saved as moved by the user and announced with its position`() {
        val actions = mutableListOf<CustomizableMenuItemsAction>()
        val store =
            setContent(
                CustomizableMenuItemsState(items = listOf(item("First"), item("Second"), item("Third"))),
                listOf(capturing(actions)),
            )

        performAction("First", moveDown)

        assertEquals(listOf("Second", "First", "Third"), store.state.items.map { it.id })
        assertEquals(
            ItemsChanged(items = store.state.items, hiddenItems = emptyList(), movedItemId = "First"),
            actions.filterIsInstance<ItemsChanged>().single(),
        )
        announcementNode().assert(hasAnnouncement(R.string.mozac_menu_customize_item_moved, "First", 2, 3))
        composeTestRule.onNodeWithContentDescription(shownItem("First", 2, 3)).assertIsDisplayed()
        composeTestRule.onNodeWithContentDescription(shownItem("Second", 1, 3)).assertIsDisplayed()
    }

    @Test
    fun `WHEN an item is moved up THEN it is moved and announced with its position`() {
        val store = setContent(CustomizableMenuItemsState(items = listOf(item("First"), item("Second"), item("Third"))))

        performAction("Third", moveUp)

        assertEquals(listOf("First", "Third", "Second"), store.state.items.map { it.id })
        announcementNode().assert(hasAnnouncement(R.string.mozac_menu_customize_item_moved, "Third", 2, 3))
        composeTestRule.onNodeWithContentDescription(shownItem("Third", 2, 3)).assertIsDisplayed()
    }

    @Test
    fun `WHEN an item is moved down past an expanded item THEN it is announced after the sub-items of the item`() {
        val store =
            setContent(
                CustomizableMenuItemsState(
                    items = listOf(item("First"), expandable("More", item("Sub 1"), item("Sub 2")), item("Last")),
                    expandedItemIds = setOf("More"),
                )
            )

        performAction("First", moveDown)

        assertEquals(listOf("More", "First", "Last"), store.state.items.map { it.id })
        announcementNode().assert(hasAnnouncement(R.string.mozac_menu_customize_item_moved, "First", 4, 5))
        composeTestRule.onNodeWithContentDescription(shownItem("Sub 2", 3, 5)).assertExists()
    }

    @Test
    fun `WHEN a collapsed expandable item is moved up THEN it keeps its sub-items`() {
        val store =
            setContent(CustomizableMenuItemsState(items = listOf(item("First"), expandable("More", item("Sub")))))

        performAction("More", moveUp)

        assertEquals(listOf("More", "First"), store.state.items.map { it.id })
        assertEquals(listOf("Sub"), store.state.items.first().subItems.map { it.id })
        announcementNode().assert(hasAnnouncement(R.string.mozac_menu_customize_item_moved, "More", 1, 2))
    }

    @Test
    fun `WHEN a sub-item is moved THEN it stays in the expanded item and is announced with its position in it`() {
        val store =
            setContent(
                CustomizableMenuItemsState(
                    items = listOf(item("First"), expandable("More", item("Sub 1"), item("Sub 2"))),
                    expandedItemIds = setOf("More"),
                )
            )

        performAction("Sub 1", moveDown)

        assertEquals(listOf("Sub 2", "Sub 1"), store.state.items.last().subItems.map { it.id })
        announcementNode().assert(hasAnnouncement(R.string.mozac_menu_customize_sub_item_moved, "Sub 1", 2, 2, "More"))
    }

    @Test
    fun `GIVEN more items than fit on the screen WHEN an item is moved down THEN it stays where it was on the screen`() {
        setContent(CustomizableMenuItemsState(items = (1..20).map { item("Item $it") }))
        val before = rowNode("Item 2", isHidden = false).fetchSemanticsNode()
        val topBefore = before.boundsInRoot.top

        performAction("Item 2", moveDown)

        val after = rowNode("Item 2", isHidden = false).fetchSemanticsNode()
        rowNode("Item 2", isHidden = false).assertIsDisplayed()
        assertEquals(before.id, after.id)
        assertEquals(topBefore, after.boundsInRoot.top)
    }

    @Test
    fun `GIVEN an item is being dragged WHEN another item is moved down THEN nothing is moved or announced`() {
        val actions = mutableListOf<CustomizableMenuItemsAction>()
        val store =
            setContent(
                CustomizableMenuItemsState(items = listOf(item("First"), item("Second"), item("Third"))),
                listOf(capturing(actions)),
            )

        startDragging("First")
        performAction("Second", moveDown)

        assertEquals(listOf("First", "Second", "Third"), store.state.items.map { it.id })
        assertTrue(actions.none { it is ItemsChanged })
        announcementNode().assert(SemanticsMatcher.keyNotDefined(SemanticsProperties.ContentDescription))
    }

    @Test
    fun `GIVEN an item is being dragged WHEN another item is expanded or collapsed THEN nothing changes or is announced`() {
        val actions = mutableListOf<CustomizableMenuItemsAction>()
        val store =
            setContent(
                CustomizableMenuItemsState(
                    items =
                        listOf(item("First"), expandable("More", item("Sub 1")), expandable("Other", item("Sub 2"))),
                    expandedItemIds = setOf("Other"),
                ),
                listOf(capturing(actions)),
            )

        startDragging("First")
        performAction("More", expand)
        performAction("Other", collapse)

        assertEquals(setOf("Other"), store.state.expandedItemIds)
        assertTrue(actions.none { it is CustomizableMenuItemsAction.ExpansionToggled })
        announcementNode().assert(SemanticsMatcher.keyNotDefined(SemanticsProperties.ContentDescription))
    }

    @Test
    fun `WHEN the move down actions of two items fetched before either is done are done one after the other THEN only the first is done`() {
        val actions = mutableListOf<CustomizableMenuItemsAction>()
        val store =
            setContent(
                CustomizableMenuItemsState(items = listOf(item("First"), item("Second"), item("Third"))),
                listOf(capturing(actions)),
            )
        val moveFirstDown = customAction("First", moveDown)
        val moveSecondDown = customAction("Second", moveDown)

        composeTestRule.runOnIdle {
            moveFirstDown.action()
            moveSecondDown.action()
        }
        composeTestRule.waitForIdle()

        assertEquals(listOf("Second", "First", "Third"), store.state.items.map { it.id })
        assertEquals(1, actions.count { it is ItemsChanged })
    }

    @Test
    fun `WHEN the same custom action is done twice THEN the item is moved and saved once`() {
        val actions = mutableListOf<CustomizableMenuItemsAction>()
        val store =
            setContent(
                CustomizableMenuItemsState(items = listOf(item("First"), item("Second"), item("Third"))),
                listOf(capturing(actions)),
            )
        val action =
            rowNode("First", isHidden = false).fetchSemanticsNode().config[SemanticsActions.CustomActions].single {
                it.label == moveDown
            }

        composeTestRule.runOnIdle {
            action.action()
            action.action()
        }
        composeTestRule.waitForIdle()

        assertEquals(listOf("Second", "First", "Third"), store.state.items.map { it.id })
        assertEquals(1, actions.count { it is ItemsChanged })
    }

    @Test
    fun `GIVEN items around a collapsed expandable item THEN it can be expanded, moved up or down and hidden`() {
        setContent(
            CustomizableMenuItemsState(items = listOf(item("First"), expandable("More", item("Sub")), item("Last")))
        )

        assertEquals(listOf(expand, moveUp, moveDown, hide), actionLabelsOf("More"))
    }

    @Test
    fun `GIVEN items around an expanded expandable item THEN it can only be collapsed`() {
        setContent(
            CustomizableMenuItemsState(
                items = listOf(item("First"), expandable("More", item("Sub")), item("Last")),
                expandedItemIds = setOf("More"),
            )
        )

        assertEquals(listOf(collapse), actionLabelsOf("More"))
    }

    @Test
    fun `GIVEN an item partly below the screen WHEN it is moved down THEN it is fully shown`() {
        setContent(CustomizableMenuItemsState(items = (1..20).map { item("Item $it") }))
        val screenBottom = composeTestRule.onRoot().getUnclippedBoundsInRoot().bottom
        val partlyShown =
            (1..20)
                .map { "Item $it" }
                .filter { composeTestRule.onAllNodes(rowMatcher(it, false)).fetchSemanticsNodes().isNotEmpty() }
                .first {
                    val bounds = rowNode(it, isHidden = false).getUnclippedBoundsInRoot()
                    bounds.top < screenBottom && bounds.bottom > screenBottom
                }

        performAction(partlyShown, moveDown)

        rowNode(partlyShown, isHidden = false).assertIsDisplayed()
        assertTrue(rowNode(partlyShown, isHidden = false).getUnclippedBoundsInRoot().bottom <= screenBottom)
    }

    @Test
    fun `WHEN an item is hidden THEN it is the first hidden item, saved as moved by the user and announced`() {
        val actions = mutableListOf<CustomizableMenuItemsAction>()
        val store =
            setContent(
                CustomizableMenuItemsState(
                    items = listOf(item("First"), item("Second")),
                    hiddenItems = listOf(item("Hidden")),
                ),
                listOf(capturing(actions)),
            )

        performAction("Second", hide)

        assertEquals(listOf("First"), store.state.items.map { it.id })
        assertEquals(listOf("Second", "Hidden"), store.state.hiddenItems.map { it.id })
        assertEquals(
            ItemsChanged(items = store.state.items, hiddenItems = store.state.hiddenItems, movedItemId = "Second"),
            actions.filterIsInstance<ItemsChanged>().single(),
        )
        announcementNode().assert(hasAnnouncement(R.string.mozac_menu_customize_item_hidden, "Second"))
        rowNode("Second", isHidden = true).assertIsDisplayed()
        assertEquals(listOf(unhide), actionLabelsOf("Second", isHidden = true))
    }

    @Test
    fun `WHEN a sub-item is hidden THEN it leaves the expanded item and is announced`() {
        val store =
            setContent(
                CustomizableMenuItemsState(
                    items = listOf(item("First"), expandable("More", item("Sub 1"), item("Sub 2"))),
                    expandedItemIds = setOf("More"),
                )
            )

        performAction("Sub 1", hide)

        assertEquals(listOf("Sub 2"), store.state.items.last().subItems.map { it.id })
        assertEquals(listOf("Sub 1"), store.state.hiddenItems.map { it.id })
        announcementNode().assert(hasAnnouncement(R.string.mozac_menu_customize_item_hidden, "Sub 1"))
        rowNode("Sub 1", isHidden = true).assertIsDisplayed()
    }

    @Test
    fun `GIVEN the expandable item is not the last one WHEN a hidden item is unhidden THEN it is the last one and announced`() {
        val actions = mutableListOf<CustomizableMenuItemsAction>()
        val store =
            setContent(
                CustomizableMenuItemsState(
                    items = listOf(item("First"), expandable("More", item("Sub")), item("Last")),
                    hiddenItems = listOf(item("Hidden")),
                ),
                listOf(capturing(actions)),
            )

        performAction("Hidden", unhide, isHidden = true)

        assertEquals(listOf("First", "More", "Last", "Hidden"), store.state.items.map { it.id })
        assertTrue(store.state.hiddenItems.isEmpty())
        assertEquals(
            ItemsChanged(items = store.state.items, hiddenItems = emptyList(), movedItemId = "Hidden"),
            actions.filterIsInstance<ItemsChanged>().single(),
        )
        announcementNode().assert(hasAnnouncement(R.string.mozac_menu_customize_item_shown, "Hidden"))
        composeTestRule.onNodeWithContentDescription(shownItem("Hidden", 4, 4)).assertIsDisplayed()
    }

    @Test
    fun `WHEN a sub-item of a hidden expandable item is unhidden THEN it is shown and the expandable item stays hidden`() {
        val store =
            setContent(
                CustomizableMenuItemsState(
                    items = listOf(item("First")),
                    hiddenItems =
                        listOf(expandable("More", item("Sub 1"), item("Sub 2"), state = MenuItemState.DISABLED)),
                    expandedItemIds = setOf("More"),
                )
            )

        assertEquals(listOf(unhide), actionLabelsOf("Sub 1", isHidden = true))
        performAction("Sub 1", unhide, isHidden = true)

        assertEquals(listOf("First", "Sub 1"), store.state.items.map { it.id })
        assertEquals(listOf("More"), store.state.hiddenItems.map { it.id })
        assertEquals(listOf("Sub 2"), store.state.hiddenItems.single().subItems.map { it.id })
        announcementNode().assert(hasAnnouncement(R.string.mozac_menu_customize_item_shown, "Sub 1"))
        composeTestRule.onNodeWithContentDescription(shownItem("Sub 1", 2, 2)).assertIsDisplayed()
    }

    @Test
    fun `WHEN the last shown item is hidden THEN nothing is moved or announced and it is reported`() {
        val actions = mutableListOf<CustomizableMenuItemsAction>()
        val store =
            setContent(
                CustomizableMenuItemsState(items = listOf(item("Only")), hiddenItems = listOf(item("Hidden"))),
                listOf(capturing(actions)),
            )

        performAction("Only", hide)

        assertEquals(listOf("Only"), store.state.items.map { it.id })
        assertEquals(listOf("Hidden"), store.state.hiddenItems.map { it.id })
        assertEquals(
            listOf(CustomizableMenuItemsAction.AttemptedEmptySection("Only")),
            actions.filterIsInstance<CustomizableMenuItemsAction.AttemptedEmptySection>(),
        )
        assertTrue(actions.none { it is ItemsChanged })
        announcementNode().assert(SemanticsMatcher.keyNotDefined(SemanticsProperties.ContentDescription))
    }

    @Test
    fun `GIVEN more items than fit on the screen WHEN an item near the bottom is hidden THEN it is in view among the hidden items`() {
        setContent(CustomizableMenuItemsState(items = (1..20).map { item("Item $it") }))
        val screenBottom = composeTestRule.onRoot().getUnclippedBoundsInRoot().bottom
        val nearBottom =
            (1..20)
                .map { "Item $it" }
                .filter { composeTestRule.onAllNodes(rowMatcher(it, false)).fetchSemanticsNodes().isNotEmpty() }
                .last { rowNode(it, isHidden = false).getUnclippedBoundsInRoot().bottom <= screenBottom }

        performAction(nearBottom, hide)

        rowNode(nearBottom, isHidden = true).assertIsDisplayed()
        val bounds = rowNode(nearBottom, isHidden = true).getUnclippedBoundsInRoot()
        assertTrue(bounds.top >= 0.dp)
        assertTrue(bounds.bottom <= screenBottom)
    }

    @Test
    fun `GIVEN more hidden items than fit on the screen WHEN the last hidden item is unhidden THEN it is in view`() {
        setContent(
            CustomizableMenuItemsState(
                items = listOf(item("Shown"), expandable("More", item("Sub"))),
                hiddenItems = (1..40).map { item("Hidden $it") },
            )
        )
        composeTestRule.onNode(hasScrollAction()).performScrollToKey("Hidden 40")
        val screenBottom = composeTestRule.onRoot().getUnclippedBoundsInRoot().bottom
        rowNode("Hidden 40", isHidden = true).assertIsDisplayed()
        composeTestRule.onAllNodes(rowMatcher("More", isHidden = false)).assertCountEquals(0)

        performAction("Hidden 40", unhide, isHidden = true)

        rowNode("Hidden 40", isHidden = false).assertIsDisplayed()
        val bounds = rowNode("Hidden 40", isHidden = false).getUnclippedBoundsInRoot()
        assertTrue(bounds.top >= 0.dp)
        assertTrue(bounds.bottom <= screenBottom)
    }

    @Test
    fun `GIVEN the hide action of the only shown item WHEN a hidden item is unhidden before it runs THEN running it does not report that the section cannot be empty`() {
        val actions = mutableListOf<CustomizableMenuItemsAction>()
        setContent(
            CustomizableMenuItemsState(items = listOf(item("Only")), hiddenItems = listOf(item("Hidden"))),
            listOf(capturing(actions)),
        )
        val hideOnly = customAction("Only", hide)
        val unhideHidden =
            rowNode("Hidden", isHidden = true).fetchSemanticsNode().config[SemanticsActions.CustomActions].single {
                it.label == unhide
            }

        composeTestRule.runOnIdle {
            unhideHidden.action()
            hideOnly.action()
        }
        composeTestRule.waitForIdle()

        assertTrue(actions.any { it is ItemsChanged })
        assertTrue(actions.none { it is CustomizableMenuItemsAction.AttemptedEmptySection })
    }

    @Test
    fun `GIVEN an item is being dragged WHEN another item is hidden or unhidden THEN nothing changes or is reported`() {
        val actions = mutableListOf<CustomizableMenuItemsAction>()
        val store =
            setContent(
                CustomizableMenuItemsState(
                    items = listOf(item("First"), item("Second")),
                    hiddenItems = listOf(item("Hidden")),
                ),
                listOf(capturing(actions)),
            )

        startDragging("First")
        performAction("Second", hide)
        performAction("Hidden", unhide, isHidden = true)

        assertEquals(listOf("First", "Second"), store.state.items.map { it.id })
        assertEquals(listOf("Hidden"), store.state.hiddenItems.map { it.id })
        assertTrue(actions.none { it is ItemsChanged })
        announcementNode().assert(SemanticsMatcher.keyNotDefined(SemanticsProperties.ContentDescription))
    }

    @Test
    fun `GIVEN an item is being dragged WHEN the last shown item is hidden THEN it is not reported`() {
        val actions = mutableListOf<CustomizableMenuItemsAction>()
        setContent(
            CustomizableMenuItemsState(items = listOf(item("Only")), hiddenItems = listOf(item("Hidden"))),
            listOf(capturing(actions)),
        )

        startDragging("Only")
        performAction("Only", hide)

        assertTrue(actions.none { it is CustomizableMenuItemsAction.AttemptedEmptySection })
    }

    @Test
    fun `GIVEN a collapsed expandable item THEN an item at the top level can be moved into it`() {
        val state =
            CustomizableMenuItemsState(items = listOf(item("First"), expandable("More", item("Sub")), item("Last")))
        setContent(state)

        assertEquals(listOf(moveDown, moveInto("More"), hide), actionLabelsOf("First"))
        assertEquals(listOf(moveUp, moveInto("More"), hide), actionLabelsOf("Last"))
    }

    @Test
    fun `GIVEN an expanded expandable item THEN an item at the top level can be moved into it`() {
        setContent(
            CustomizableMenuItemsState(
                items = listOf(item("First"), expandable("More", item("Sub")), item("Last")),
                expandedItemIds = setOf("More"),
            )
        )

        assertEquals(listOf(moveDown, moveInto("More"), hide), actionLabelsOf("First"))
        assertEquals(listOf(moveUp, moveInto("More"), hide), actionLabelsOf("Last"))
    }

    @Test
    fun `THEN the actions moving an item into or out of an expandable item name it as it is read on screen`() {
        setContent(
            CustomizableMenuItemsState(
                items = listOf(item("First"), expandable("Overflow", item("Sub")).withContentDescription("Other menu")),
                expandedItemIds = setOf("Overflow"),
            )
        )

        assertEquals(listOf(moveDown, moveInto("Other menu"), hide), actionLabelsOf("First"))
        assertEquals(listOf(moveOutOf("Other menu"), hide), actionLabelsOf("Sub"))
        assertEquals("Move into “Other menu”", moveInto("Other menu"))
        assertEquals("Move out of “Other menu”", moveOutOf("Other menu"))
    }

    @Test
    fun `GIVEN an item which cannot be inside the expandable item THEN it cannot be moved into it`() {
        val topLevelOnly = item("Top level only").copy(isTopLevelOnly = true)
        setContent(
            CustomizableMenuItemsState(items = listOf(item("First"), topLevelOnly, expandable("More", item("Sub"))))
        )

        assertEquals(listOf(moveUp, moveDown, hide), actionLabelsOf("Top level only"))
        assertEquals(listOf(expand, moveUp, hide), actionLabelsOf("More"))
    }

    @Test
    fun `GIVEN no shown expandable item THEN no item can be moved into it`() {
        setContent(
            CustomizableMenuItemsState(
                items = listOf(item("First"), item("Second")),
                hiddenItems = listOf(expandable("More", item("Sub"), state = MenuItemState.DISABLED)),
            )
        )

        assertEquals(listOf(moveDown, hide), actionLabelsOf("First"))
        assertEquals(listOf(expand, unhide), actionLabelsOf("More", isHidden = true))
    }

    @Test
    fun `WHEN an item is moved into a collapsed expandable item THEN it is its first sub-item and the item stays collapsed`() {
        val actions = mutableListOf<CustomizableMenuItemsAction>()
        val store =
            setContent(
                CustomizableMenuItemsState(
                    items = listOf(item("First"), item("Second"), expandable("More", item("Sub 1"), item("Sub 2")))
                ),
                listOf(capturing(actions)),
            )

        performAction("First", moveInto("More"))

        assertEquals(listOf("Second", "More"), store.state.items.map { it.id })
        assertEquals(listOf("First", "Sub 1", "Sub 2"), store.state.items.last().subItems.map { it.id })
        assertEquals(emptySet<String>(), store.state.expandedItemIds)
        assertEquals(
            listOf(ItemsChanged(items = store.state.items, hiddenItems = emptyList(), movedItemId = "First")),
            actions.filter { it is ItemsChanged || it is CustomizableMenuItemsAction.ExpansionToggled },
        )
        announcementNode().assert(hasAnnouncement(R.string.mozac_menu_customize_item_moved_into, "First", "More"))
        rowNode("First", isHidden = false).assertDoesNotExist()
        composeTestRule.onNodeWithContentDescription(shownItem("More", 2, 2)).assertExists()

        performAction("More", expand)

        composeTestRule.onNodeWithContentDescription(shownItem("First", 3, 5)).assertIsDisplayed()
        composeTestRule.onNodeWithContentDescription(shownItem("Sub 1", 4, 5)).assertExists()
        assertEquals(listOf(moveDown, moveOutOf("More"), hide), actionLabelsOf("First"))
    }

    @Test
    fun `WHEN an item is moved into an expanded expandable item THEN it is its first sub-item and the expandable item stays expanded`() {
        val actions = mutableListOf<CustomizableMenuItemsAction>()
        val store =
            setContent(
                CustomizableMenuItemsState(
                    items = listOf(expandable("More", item("Sub")), item("Last")),
                    expandedItemIds = setOf("More"),
                ),
                listOf(capturing(actions)),
            )

        performAction("Last", moveInto("More"))

        assertEquals(listOf("Last", "Sub"), store.state.items.single().subItems.map { it.id })
        assertEquals(setOf("More"), store.state.expandedItemIds)
        assertTrue(actions.none { it is CustomizableMenuItemsAction.ExpansionToggled })
        announcementNode().assert(hasAnnouncement(R.string.mozac_menu_customize_item_moved_into, "Last", "More"))
        composeTestRule.onNodeWithContentDescription(shownItem("Last", 2, 3)).assertIsDisplayed()
    }

    @Test
    fun `GIVEN the expandable item is not the last one WHEN a sub-item is moved out of it THEN it is the last one and announced`() {
        val actions = mutableListOf<CustomizableMenuItemsAction>()
        val store =
            setContent(
                CustomizableMenuItemsState(
                    items = listOf(item("First"), expandable("More", item("Sub 1"), item("Sub 2")), item("Last")),
                    expandedItemIds = setOf("More"),
                ),
                listOf(capturing(actions)),
            )

        performAction("Sub 2", moveOutOf("More"))

        assertEquals(listOf("First", "More", "Last", "Sub 2"), store.state.items.map { it.id })
        assertEquals(listOf("Sub 1"), store.state.items[1].subItems.map { it.id })
        assertEquals(setOf("More"), store.state.expandedItemIds)
        assertEquals(
            ItemsChanged(items = store.state.items, hiddenItems = emptyList(), movedItemId = "Sub 2"),
            actions.filterIsInstance<ItemsChanged>().single(),
        )
        assertTrue(actions.none { it is CustomizableMenuItemsAction.ExpansionToggled })
        announcementNode().assert(hasAnnouncement(R.string.mozac_menu_customize_item_moved_out_of, "Sub 2", "More"))
        composeTestRule.onNodeWithContentDescription(shownItem("Sub 2", 5, 5)).assertIsDisplayed()
        assertEquals(listOf(moveUp, moveInto("More"), hide), actionLabelsOf("Sub 2"))
    }

    @Test
    fun `GIVEN two shown expandable items WHEN a sub-item of the second one is moved out THEN it is just above it and named`() {
        val actions = mutableListOf<CustomizableMenuItemsAction>()
        val store =
            setContent(
                CustomizableMenuItemsState(
                    items =
                        listOf(expandable("More", item("Sub 1")), expandable("Other", item("Sub 2"), item("Sub 3"))),
                    expandedItemIds = setOf("Other"),
                ),
                listOf(capturing(actions)),
            )

        assertEquals(listOf(moveUp, moveOutOf("Other"), hide), actionLabelsOf("Sub 3"))
        performAction("Sub 3", moveOutOf("Other"))

        assertEquals(listOf("More", "Sub 3", "Other"), store.state.items.map { it.id })
        assertEquals(listOf("Sub 2"), store.state.items.last().subItems.map { it.id })
        announcementNode().assert(hasAnnouncement(R.string.mozac_menu_customize_item_moved_out_of, "Sub 3", "Other"))
    }

    @Test
    fun `GIVEN more items than fit on the screen WHEN an item is moved into an expanded expandable item THEN it is in view`() {
        setContent(
            CustomizableMenuItemsState(
                items = (1..20).map { item("Item $it") } + expandable("More", item("Sub")),
                expandedItemIds = setOf("More"),
            )
        )

        performAction("Item 2", moveInto("More"))

        rowNode("Item 2", isHidden = false).assertIsDisplayed()
    }

    @Test
    fun `GIVEN an item is being dragged WHEN another item is moved into or out of the expandable item THEN nothing changes`() {
        val actions = mutableListOf<CustomizableMenuItemsAction>()
        val store =
            setContent(
                CustomizableMenuItemsState(
                    items = listOf(item("First"), item("Second"), expandable("More", item("Sub")))
                ),
                listOf(capturing(actions)),
            )

        startDragging("First")
        performAction("Second", moveInto("More"))

        assertEquals(listOf("First", "Second", "More"), store.state.items.map { it.id })
        assertTrue(store.state.expandedItemIds.isEmpty())
        assertTrue(actions.none { it is ItemsChanged || it is CustomizableMenuItemsAction.ExpansionToggled })
        announcementNode().assert(SemanticsMatcher.keyNotDefined(SemanticsProperties.ContentDescription))
    }

    @Test
    fun `WHEN the move into action of an item fetched before the items changed is done THEN nothing changes`() {
        val actions = mutableListOf<CustomizableMenuItemsAction>()
        val store =
            setContent(
                CustomizableMenuItemsState(
                    items = listOf(item("First"), item("Second"), expandable("More", item("Sub")))
                ),
                listOf(capturing(actions)),
            )
        val moveFirstDown = customAction("First", moveDown)
        val moveSecondInto = customAction("Second", moveInto("More"))

        composeTestRule.runOnIdle {
            moveFirstDown.action()
            moveSecondInto.action()
        }
        composeTestRule.waitForIdle()

        assertEquals(listOf("Second", "First", "More"), store.state.items.map { it.id })
        assertTrue(store.state.expandedItemIds.isEmpty())
        assertTrue(actions.none { it is CustomizableMenuItemsAction.ExpansionToggled })
    }

    @Test
    fun `GIVEN a collapsed expandable item THEN it is read as a collapsed button and activating it expands it`() {
        val store =
            setContent(CustomizableMenuItemsState(items = listOf(item("Shown"), expandable("More", item("Sub")))))

        rowNode("More", isHidden = false)
            .assert(SemanticsMatcher.expectValue(SemanticsProperties.Role, Role.Button))
            .assert(SemanticsMatcher.expectValue(SemanticsProperties.StateDescription, collapsedState))
        activate("More")

        assertEquals(setOf("More"), store.state.expandedItemIds)
        announcementNode().assert(hasAnnouncement(R.string.mozac_menu_customize_item_expanded, "More"))
    }

    @Test
    fun `GIVEN an expanded expandable item THEN it is read as an expanded button and activating it collapses it`() {
        val store =
            setContent(
                CustomizableMenuItemsState(
                    items = listOf(item("Shown"), expandable("More", item("Sub"))),
                    expandedItemIds = setOf("More"),
                )
            )

        rowNode("More", isHidden = false)
            .assert(SemanticsMatcher.expectValue(SemanticsProperties.Role, Role.Button))
            .assert(SemanticsMatcher.expectValue(SemanticsProperties.StateDescription, expandedState))
        activate("More")

        assertTrue(store.state.expandedItemIds.isEmpty())
        announcementNode().assert(hasAnnouncement(R.string.mozac_menu_customize_item_collapsed, "More"))
    }

    @Test
    fun `THEN an item which is not expandable cannot be activated`() {
        setContent(CustomizableMenuItemsState(items = listOf(item("Shown"), expandable("More", item("Sub")))))

        rowNode("Shown", isHidden = false)
            .assert(SemanticsMatcher.keyNotDefined(SemanticsActions.OnClick))
            .assert(SemanticsMatcher.keyNotDefined(SemanticsProperties.StateDescription))
    }

    @Test
    fun `GIVEN nothing was announced THEN the live region tells nothing`() {
        setContent(CustomizableMenuItemsState(items = listOf(item("Shown"))))

        announcementNode()
            .assert(SemanticsMatcher.expectValue(SemanticsProperties.LiveRegion, LiveRegionMode.Polite))
            .assert(SemanticsMatcher.keyNotDefined(SemanticsProperties.ContentDescription))
    }

    @Test
    fun `THEN the announcements are drawn over the list, for accessibility services to be told about them`() {
        setContent(CustomizableMenuItemsState(items = listOf(item("First"), item("Second"))))

        val announcement = announcementNode().fetchSemanticsNode()
        val list = composeTestRule.onNode(hasScrollAction()).fetchSemanticsNode()
        val siblings = checkNotNull(announcement.parent).children.map { it.id }
        assertTrue(list.id in siblings)
        assertTrue(siblings.indexOf(announcement.id) > siblings.indexOf(list.id))
    }

    private fun announcementNode() =
        composeTestRule.onNode(SemanticsMatcher.keyIsDefined(SemanticsProperties.LiveRegion))

    private fun hasAnnouncement(@StringRes stringRes: Int, vararg args: Any) =
        SemanticsMatcher.expectValue(
            SemanticsProperties.ContentDescription,
            listOf(testContext.getString(stringRes, *args)),
        )

    private fun moveInto(name: String) = testContext.getString(R.string.mozac_menu_customize_move_into, name)

    private fun moveOutOf(name: String) = testContext.getString(R.string.mozac_menu_customize_move_out_of, name)

    private fun shownItem(name: String, position: Int, count: Int) =
        testContext.getString(R.string.mozac_menu_customize_shown_item, name, position, count)

    private fun assertSingleNode(name: String, description: String, vararg texts: String) {
        composeTestRule
            .onAllNodes(hasContentDescription(name, substring = true) or hasText(name, substring = true))
            .assertCountEquals(1)
        composeTestRule.onNodeWithContentDescription(description).assertExists()
        texts.forEach { composeTestRule.onNodeWithText(it).assertDoesNotExist() }
    }

    private fun rowMatcher(name: String, isHidden: Boolean) =
        SemanticsMatcher("Row of $name") { node ->
            val description =
                node.config.getOrElse(SemanticsProperties.ContentDescription) { emptyList() }.singleOrNull()
            if (isHidden) description == name else description?.startsWith("$name. Position ") == true
        }

    private fun rowNode(name: String, isHidden: Boolean) = composeTestRule.onNode(rowMatcher(name, isHidden))

    private fun actionLabelsOf(name: String, isHidden: Boolean = false) =
        rowNode(name, isHidden)
            .fetchSemanticsNode()
            .config
            .getOrElse(SemanticsActions.CustomActions) { emptyList() }
            .map { it.label }

    private fun startDragging(name: String) {
        val center = rowNode(name, isHidden = false).fetchSemanticsNode().boundsInRoot.center
        composeTestRule.onRoot().performTouchInput {
            down(center)
            advanceEventTime(viewConfiguration.longPressTimeoutMillis * 2)
            move()
        }
        composeTestRule.waitForIdle()
        composeTestRule.onRoot().performTouchInput { moveBy(Offset(0f, viewConfiguration.touchSlop * 3)) }
        composeTestRule.waitForIdle()
    }

    private fun customAction(name: String, label: String) =
        rowNode(name, isHidden = false).fetchSemanticsNode().config[SemanticsActions.CustomActions].single {
            it.label == label
        }

    private fun activate(name: String) {
        val action = rowNode(name, isHidden = false).fetchSemanticsNode().config[SemanticsActions.OnClick].action
        composeTestRule.runOnIdle { checkNotNull(action)() }
        composeTestRule.waitForIdle()
    }

    private fun performAction(name: String, label: String, isHidden: Boolean = false) {
        val action =
            rowNode(name, isHidden).fetchSemanticsNode().config[SemanticsActions.CustomActions].single {
                it.label == label
            }
        composeTestRule.runOnIdle { action.action() }
        composeTestRule.waitForIdle()
    }

    private fun setContent(
        state: CustomizableMenuItemsState,
        middleware: List<Middleware<CustomizableMenuItemsState, CustomizableMenuItemsAction>> = emptyList(),
    ): CustomizableMenuItemsStore {
        val store = CustomizableMenuItemsStore(initialState = state, middleware = middleware)
        composeTestRule.setContent { AcornTheme { EditMenuItemsContent(store = store) } }
        return store
    }

    private fun capturing(
        actions: MutableList<CustomizableMenuItemsAction>
    ): Middleware<CustomizableMenuItemsState, CustomizableMenuItemsAction> = { _, next, action ->
        actions.add(action)
        next(action)
    }

    private fun summary(text: String) = MenuItemSummary(Text.String(text))

    private fun item(title: String) =
        CustomizableMenuItem(id = title, item = StandardMenuItem(title = MenuItemTitle(Text.String(title))))

    private fun CustomizableMenuItem.withState(state: MenuItemState) =
        copy(item = (item as StandardMenuItem).copy(state = state))

    private fun CustomizableMenuItem.withContentDescription(description: String) =
        copy(item = (item as ExpandableMenuItem).copy(contentDescription = Text.String(description)))

    private fun expandable(
        title: String,
        vararg subItems: CustomizableMenuItem,
        state: MenuItemState = MenuItemState.DEFAULT,
    ) =
        CustomizableMenuItem(
            id = title,
            item =
                ExpandableMenuItem(
                    title = MenuItemTitle(Text.String(title)),
                    onClickEvent = object : MenuEvent {},
                    subMenuItems = emptyList(),
                    state = state,
                ),
            subItems = subItems.toList(),
        )
}
