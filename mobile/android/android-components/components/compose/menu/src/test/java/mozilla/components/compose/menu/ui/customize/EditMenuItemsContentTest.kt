/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.hapticfeedback.HapticFeedback
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.getUnclippedBoundsInRoot
import androidx.compose.ui.test.isRoot
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.unit.dp
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.compose.base.R as baseR
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.R
import mozilla.components.compose.menu.data.CustomizableMenuItem
import mozilla.components.compose.menu.data.ExpandableMenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.AttemptedEmptySection
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.AttemptedMoveAsSubItem
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.ItemsChanged
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.PreviewClicked
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
class EditMenuItemsContentTest {
    @get:Rule val composeTestRule = createComposeRule()

    private val dragToReorder = testContext.getString(R.string.mozac_menu_customize_drag_items_to_reorder)
    private val hiddenItemsTitle = testContext.getString(R.string.mozac_menu_customize_hidden_items)
    private val dragToHide = testContext.getString(R.string.mozac_menu_customize_drag_items_to_hide)
    private val preview = testContext.getString(R.string.mozac_menu_customize_preview)
    private val moveUp = testContext.getString(R.string.mozac_menu_customize_move_up)
    private val moveDown = testContext.getString(R.string.mozac_menu_customize_move_down)
    private val hide = testContext.getString(R.string.mozac_menu_customize_hide_item)
    private val unhide = testContext.getString(R.string.mozac_menu_customize_unhide_item)
    private val close = testContext.getString(baseR.string.mozac_compose_base_close_button_content_description)
    private val hapticFeedback = RecordingHapticFeedback()

    @Test
    fun `GIVEN shown and hidden items THEN the shown ones are above the hidden items title and the hidden ones below it`() {
        setContent(
            CustomizableMenuItemsState(
                items = listOf(item("Shown 1"), item("Shown 2")),
                hiddenItems = listOf(item("Hidden")),
            )
        )

        val dragBarBounds = composeTestRule.onNodeWithText(dragToReorder).getUnclippedBoundsInRoot()
        val shown1Bounds = rowOf("Shown 1").getUnclippedBoundsInRoot()
        val shown2Bounds = rowOf("Shown 2").getUnclippedBoundsInRoot()
        val titleBounds = composeTestRule.onNodeWithText(hiddenItemsTitle).getUnclippedBoundsInRoot()
        val hiddenBounds = rowOf("Hidden").getUnclippedBoundsInRoot()
        assertTrue(dragBarBounds.bottom <= shown1Bounds.top)
        assertTrue(shown1Bounds.bottom <= shown2Bounds.top)
        assertTrue(shown2Bounds.bottom <= titleBounds.top)
        assertTrue(titleBounds.bottom <= hiddenBounds.top)
    }

    @Test
    fun `GIVEN no hidden items THEN the area to hide items is shown`() {
        setContent(CustomizableMenuItemsState(items = listOf(item("Shown"))))

        composeTestRule.onNodeWithText(dragToHide).assertIsDisplayed()
    }

    @Test
    fun `GIVEN hidden items THEN the area to hide items is not shown`() {
        setContent(CustomizableMenuItemsState(items = listOf(item("Shown")), hiddenItems = listOf(item("Hidden"))))

        composeTestRule.onNodeWithText(dragToHide).assertDoesNotExist()
    }

    @Test
    fun `GIVEN an item the app does not show THEN it is not shown`() {
        setContent(
            CustomizableMenuItemsState(
                items = listOf(CustomizableMenuItem(id = "app hidden", item = null), item("Shown"))
            )
        )

        rowOf("Shown").assertIsDisplayed()
    }

    @Test
    fun `WHEN the user taps an expandable item THEN its sub-items are shown or not`() {
        setContent(
            CustomizableMenuItemsState(
                items = listOf(expandable("More", item("Sub-item"))),
                expandedItemIds = setOf("More"),
            )
        )
        rowOf("Sub-item").assertIsDisplayed()

        rowOf("More").performClick()
        rowOf("Sub-item").assertDoesNotExist()

        rowOf("More").performClick()
        rowOf("Sub-item").assertIsDisplayed()
    }

    @Test
    fun `WHEN the user taps a hidden expandable item THEN its sub-items are shown or not`() {
        setContent(
            CustomizableMenuItemsState(
                items = listOf(item("Shown")),
                hiddenItems = listOf(expandable("More", item("Sub-item"), state = MenuItemState.DISABLED)),
                expandedItemIds = setOf("More"),
            )
        )
        rowOf("Sub-item").assertIsDisplayed()

        rowOf("More").performClick()
        rowOf("Sub-item").assertDoesNotExist()

        rowOf("More").performClick()
        rowOf("Sub-item").assertIsDisplayed()
    }

    @Test
    fun `WHEN the user taps preview THEN tell the store`() {
        val actions = mutableListOf<CustomizableMenuItemsAction>()
        val captureActions: Middleware<CustomizableMenuItemsState, CustomizableMenuItemsAction> = { _, next, action ->
            actions.add(action)
            next(action)
        }
        setContent(CustomizableMenuItemsState(items = listOf(item("Shown"))), listOf(captureActions))

        composeTestRule.onNodeWithText(preview).performClick()

        assertTrue(PreviewClicked in actions)
    }

    @Test
    fun `WHEN a shown item is dragged by its handle above another one THEN it is shown before it`() {
        val actions = mutableListOf<CustomizableMenuItemsAction>()
        val store =
            setContent(
                CustomizableMenuItemsState(items = listOf(item("Shown 1"), item("Shown 2"), item("Shown 3"))),
                listOf(capturing(actions)),
            )

        dragByHandle(title = "Shown 3", to = { boundsOf("Shown 1").topCenter })

        assertEquals(listOf("Shown 3", "Shown 1", "Shown 2"), store.state.items.map { it.id })
        assertEquals(
            ItemsChanged(items = store.state.items, hiddenItems = emptyList(), movedItemId = "Shown 3"),
            actions.filterIsInstance<ItemsChanged>().single(),
        )
    }

    @Test
    fun `WHEN a sub-item is dragged by its handle below another sub-item THEN it is shown after it`() {
        val more = expandable("More", item("Sub 1"), item("Sub 2"), item("Sub 3"))
        val store =
            setContent(CustomizableMenuItemsState(items = listOf(item("Shown"), more), expandedItemIds = setOf("More")))

        dragByHandle(title = "Sub 1", to = { boundsOf("Sub 2").bottomCenter })

        val subItems = store.state.items.first { it.id == "More" }.subItems
        assertEquals(listOf("Sub 2", "Sub 1", "Sub 3"), subItems.map { it.id })
        assertEquals(listOf("Shown", "More"), store.state.items.map { it.id })
    }

    @Test
    fun `WHEN a shown item is dragged by its handle into the area to hide items THEN it is hidden`() {
        val actions = mutableListOf<CustomizableMenuItemsAction>()
        val store =
            setContent(
                CustomizableMenuItemsState(items = listOf(item("Shown 1"), item("Shown 2"))),
                listOf(capturing(actions)),
            )

        dragByHandle(title = "Shown 1", to = { textBoundsOf(dragToHide).center })

        assertEquals(listOf("Shown 2"), store.state.items.map { it.id })
        assertEquals(listOf("Shown 1"), store.state.hiddenItems.map { it.id })
        assertEquals(
            ItemsChanged(items = store.state.items, hiddenItems = store.state.hiddenItems, movedItemId = "Shown 1"),
            actions.filterIsInstance<ItemsChanged>().single(),
        )
    }

    @Test
    fun `WHEN the only shown item is dragged by its handle into the area to hide items THEN it is put back`() {
        val actions = mutableListOf<CustomizableMenuItemsAction>()
        val store = setContent(CustomizableMenuItemsState(items = listOf(item("Shown"))), listOf(capturing(actions)))

        dragByHandle(title = "Shown", to = { textBoundsOf(dragToHide).center })

        assertEquals(listOf("Shown"), store.state.items.map { it.id })
        assertTrue(actions.none { it is ItemsChanged })
    }

    @Test
    fun `WHEN a hidden item is dragged by its handle above a shown one THEN it is shown before it`() {
        val store =
            setContent(
                CustomizableMenuItemsState(
                    items = listOf(item("Shown 1"), item("Shown 2")),
                    hiddenItems = listOf(item("Hidden")),
                )
            )

        dragByHandle(title = "Hidden", to = { boundsOf("Shown 1").topCenter })

        assertEquals(listOf("Hidden", "Shown 1", "Shown 2"), store.state.items.map { it.id })
        assertTrue(store.state.hiddenItems.isEmpty())
    }

    @Test
    fun `WHEN a collapsed hidden expandable item is dragged by its handle above a shown one THEN it is shown`() {
        val store =
            setContent(
                CustomizableMenuItemsState(
                    items = listOf(item("Shown")),
                    hiddenItems = listOf(expandable("More", item("Sub 1"), state = MenuItemState.DISABLED)),
                )
            )

        dragByHandle(title = "More", to = { boundsOf("Shown").topCenter })

        assertEquals(listOf("More", "Shown"), store.state.items.map { it.id })
        assertEquals(listOf("Sub 1"), store.state.items.first().subItems.map { it.id })
        assertTrue(store.state.hiddenItems.isEmpty())
    }

    @Test
    fun `WHEN a shown sub-item is dragged by its handle into the area to hide items THEN it is hidden`() {
        val more = expandable("More", item("Sub 1"), item("Sub 2"))
        val store =
            setContent(CustomizableMenuItemsState(items = listOf(item("Shown"), more), expandedItemIds = setOf("More")))

        dragByHandle(title = "Sub 1", to = { textBoundsOf(dragToHide).center })

        assertEquals(listOf("Sub 2"), store.state.items.first { it.id == "More" }.subItems.map { it.id })
        assertEquals(listOf("Sub 1"), store.state.hiddenItems.map { it.id })
    }

    @Test
    fun `WHEN an expanded hidden item is pressed where the handle of other items is and moved THEN nothing is saved`() {
        val actions = mutableListOf<CustomizableMenuItemsAction>()
        val store =
            setContent(
                CustomizableMenuItemsState(
                    items = listOf(item("Shown")),
                    hiddenItems = listOf(expandable("More", item("Sub 1"), state = MenuItemState.DISABLED)),
                    expandedItemIds = setOf("More"),
                ),
                listOf(capturing(actions)),
            )

        dragByHandle(title = "More", to = { boundsOf("Shown").topCenter })

        assertEquals(listOf("Shown"), store.state.items.map { it.id })
        assertTrue(actions.none { it is ItemsChanged })
    }

    @Test
    fun `WHEN a shown item is long pressed on its body and dragged above another one THEN it is shown before it`() {
        val actions = mutableListOf<CustomizableMenuItemsAction>()
        val store =
            setContent(
                CustomizableMenuItemsState(items = listOf(item("Shown 1"), item("Shown 2"), item("Shown 3"))),
                listOf(capturing(actions)),
            )

        dragFrom(start = boundsOf("Shown 3").center, afterLongPress = true, to = { boundsOf("Shown 1").topCenter })

        assertEquals(listOf("Shown 3", "Shown 1", "Shown 2"), store.state.items.map { it.id })
        assertEquals(
            ItemsChanged(items = store.state.items, hiddenItems = emptyList(), movedItemId = "Shown 3"),
            actions.filterIsInstance<ItemsChanged>().single(),
        )
    }

    @Test
    fun `WHEN an expanded item is pressed where the handle of other items is and moved THEN nothing is saved`() {
        val actions = mutableListOf<CustomizableMenuItemsAction>()
        val store =
            setContent(
                CustomizableMenuItemsState(
                    items = listOf(item("Shown"), expandable("More", item("Sub 1"))),
                    expandedItemIds = setOf("More"),
                ),
                listOf(capturing(actions)),
            )

        dragByHandle(title = "More", to = { boundsOf("Shown").topCenter })

        assertEquals(listOf("Shown", "More"), store.state.items.map { it.id })
        assertTrue(actions.none { it is ItemsChanged })
    }

    @Test
    fun `WHEN a collapsed expandable item is dragged by its handle above another item THEN it is shown before it`() {
        val actions = mutableListOf<CustomizableMenuItemsAction>()
        val store =
            setContent(
                CustomizableMenuItemsState(items = listOf(item("Shown"), expandable("More", item("Sub 1")))),
                listOf(capturing(actions)),
            )

        dragByHandle(title = "More", to = { boundsOf("Shown").topCenter })

        assertEquals(listOf("More", "Shown"), store.state.items.map { it.id })
        assertEquals(listOf("Sub 1"), store.state.items.first().subItems.map { it.id })
        assertEquals("More", actions.filterIsInstance<ItemsChanged>().single().movedItemId)
    }

    @Test
    fun `WHEN a collapsed expandable item is long pressed and dragged above another item THEN it stays collapsed`() {
        val actions = mutableListOf<CustomizableMenuItemsAction>()
        val store =
            setContent(
                CustomizableMenuItemsState(items = listOf(item("Shown"), expandable("More", item("Sub 1")))),
                listOf(capturing(actions)),
            )

        dragFrom(start = boundsOf("More").center, afterLongPress = true, to = { boundsOf("Shown").topCenter })

        assertEquals(listOf("More", "Shown"), store.state.items.map { it.id })
        assertTrue(actions.none { it is CustomizableMenuItemsAction.ExpansionToggled })
    }

    @Test
    fun `WHEN a collapsed expandable item is long pressed and let go where it is THEN it stays collapsed`() {
        val actions = mutableListOf<CustomizableMenuItemsAction>()
        setContent(
            CustomizableMenuItemsState(items = listOf(item("Shown"), expandable("More", item("Sub 1")))),
            listOf(capturing(actions)),
        )

        composeTestRule.onRoot().performTouchInput { down(boundsOf("More").center) }
        composeTestRule.mainClock.advanceTimeBy(LONG_PRESS_WAIT_MILLIS)
        composeTestRule.onRoot().performTouchInput { up() }
        composeTestRule.waitForIdle()

        assertTrue(actions.none { it is CustomizableMenuItemsAction.ExpansionToggled })
    }

    @Test
    fun `WHEN a shown item is dragged by its handle among the sub-items of an expanded item THEN it is one of them`() {
        val more = expandable("More", item("Sub 1"), item("Sub 2"))
        val store =
            setContent(CustomizableMenuItemsState(items = listOf(item("Shown"), more), expandedItemIds = setOf("More")))

        dragByHandle(title = "Shown", to = { boundsOf("Sub 2").topCenter })

        assertEquals(listOf("More"), store.state.items.map { it.id })
        assertEquals(listOf("Sub 1", "Shown", "Sub 2"), store.state.items.single().subItems.map { it.id })
    }

    @Test
    fun `WHEN a sub-item is dragged by its handle below the sub-items of its expanded item THEN it is a top level item`() {
        val more = expandable("More", item("Sub 1"), item("Sub 2"))
        val store =
            setContent(CustomizableMenuItemsState(items = listOf(more, item("Shown")), expandedItemIds = setOf("More")))

        dragByHandle(title = "Sub 1", to = { boundsOf("Shown").topCenter })

        assertEquals(listOf("More", "Sub 1", "Shown"), store.state.items.map { it.id })
        assertEquals(listOf("Sub 2"), store.state.items.first().subItems.map { it.id })
    }

    @Test
    fun `WHEN the last sub-item is dragged out of its expanded item THEN the item is collapsed and can be dragged by its handle`() {
        val store =
            setContent(
                CustomizableMenuItemsState(
                    items = listOf(expandable("More", item("Sub 1")), item("Shown")),
                    expandedItemIds = setOf("More"),
                )
            )

        dragByHandle(title = "Sub 1", to = { boundsOf("Shown").topCenter })
        assertEquals(emptySet<String>(), store.state.expandedItemIds)
        dragByHandle(title = "More", to = { boundsOf("Shown").topCenter })

        assertEquals(listOf("Sub 1", "More", "Shown"), store.state.items.map { it.id })
    }

    @Test
    fun `WHEN the only shown item is dragged into the area to hide items THEN the user is told once`() {
        val actions = mutableListOf<CustomizableMenuItemsAction>()
        setContent(CustomizableMenuItemsState(items = listOf(item("Shown"))), listOf(capturing(actions)))

        dragByHandle(
            title = "Shown",
            via = listOf({ textBoundsOf(dragToHide).center }, { boundsOf("Shown").center }),
            to = { textBoundsOf(dragToHide).center },
        )

        assertEquals(listOf(AttemptedEmptySection("Shown")), actions.filterIsInstance<AttemptedEmptySection>())
    }

    @Test
    fun `WHEN an item which can be hidden is dragged into the area to hide items THEN the user is not told`() {
        val actions = mutableListOf<CustomizableMenuItemsAction>()
        setContent(
            CustomizableMenuItemsState(items = listOf(item("Shown 1"), item("Shown 2"))),
            listOf(capturing(actions)),
        )

        dragByHandle(title = "Shown 1", to = { textBoundsOf(dragToHide).center })

        assertTrue(actions.none { it is AttemptedEmptySection })
    }

    @Test
    fun `WHEN an item which can only be directly in its section is let go among the sub-items of an expanded item THEN it stays where it was and the user is told`() {
        val actions = mutableListOf<CustomizableMenuItemsAction>()
        val more = expandable("More", item("Sub 1"), item("Sub 2"))
        val store =
            setContent(
                CustomizableMenuItemsState(
                    items = listOf(topLevelOnlyItem("Top level only"), more),
                    expandedItemIds = setOf("More"),
                ),
                listOf(capturing(actions)),
            )

        dragByHandle(title = "Top level only", to = { boundsOf("Sub 2").topCenter })

        assertEquals(listOf("Top level only", "More"), store.state.items.map { it.id })
        assertEquals(
            listOf(AttemptedMoveAsSubItem("Top level only")),
            actions.filterIsInstance<AttemptedMoveAsSubItem>(),
        )
        assertTrue(actions.none { it is ItemsChanged })
    }

    @Test
    fun `WHEN an item which can only be directly in its section is let go at the top level after being dragged past an expanded item THEN the user is not told`() {
        val actions = mutableListOf<CustomizableMenuItemsAction>()
        val more = expandable("More", item("Sub 1"), item("Sub 2"))
        val store =
            setContent(
                CustomizableMenuItemsState(
                    items = listOf(topLevelOnlyItem("Top level only"), more, item("Last")),
                    expandedItemIds = setOf("More"),
                ),
                listOf(capturing(actions)),
            )

        dragByHandle(title = "Top level only", to = { boundsOf("Last").topCenter })

        assertEquals(listOf("More", "Top level only", "Last"), store.state.items.map { it.id })
        assertTrue(actions.none { it is AttemptedMoveAsSubItem })
    }

    @Test
    fun `WHEN the drag handle of a shown item between others is long pressed THEN move up, move down and hide item are enabled`() {
        setContent(CustomizableMenuItemsState(items = listOf(item("Shown 1"), item("Shown 2"), item("Shown 3"))))

        longPressHandle("Shown 2")

        composeTestRule.onNodeWithText(moveUp).assertIsEnabled()
        composeTestRule.onNodeWithText(moveDown).assertIsEnabled()
        composeTestRule.onNodeWithText(hide).assertIsEnabled()
        composeTestRule.onNodeWithText(unhide).assertDoesNotExist()
    }

    @Test
    fun `GIVEN the first shown item WHEN its drag handle is long pressed THEN move up is disabled and move down is enabled`() {
        setContent(CustomizableMenuItemsState(items = listOf(item("Shown 1"), item("Shown 2"))))

        longPressHandle("Shown 1")

        composeTestRule.onNodeWithText(moveUp).assertIsNotEnabled()
        composeTestRule.onNodeWithText(moveDown).assertIsEnabled()
    }

    @Test
    fun `GIVEN the last shown item WHEN its drag handle is long pressed THEN move up is enabled and move down is disabled`() {
        setContent(CustomizableMenuItemsState(items = listOf(item("Shown 1"), item("Shown 2"))))

        longPressHandle("Shown 2")

        composeTestRule.onNodeWithText(moveUp).assertIsEnabled()
        composeTestRule.onNodeWithText(moveDown).assertIsNotEnabled()
    }

    @Test
    fun `GIVEN a shown expandable item WHEN the drag handle of a shown item is long pressed THEN move into is enabled`() {
        setContent(CustomizableMenuItemsState(items = listOf(item("Shown"), expandable("More", item("Sub")))))

        longPressHandle("Shown")

        composeTestRule.onNodeWithText(moveInto("More")).assertIsEnabled()
    }

    @Test
    fun `GIVEN an item which can only be directly in its section WHEN its drag handle is long pressed THEN no move into action is shown`() {
        setContent(
            CustomizableMenuItemsState(items = listOf(topLevelOnlyItem("Extensions"), expandable("More", item("Sub"))))
        )

        longPressHandle("Extensions")

        composeTestRule.onNodeWithText(moveDown).assertIsEnabled()
        composeTestRule.onNodeWithText(hide).assertIsEnabled()
        composeTestRule.onNodeWithText(moveInto("More")).assertDoesNotExist()
    }

    @Test
    fun `GIVEN no shown expandable item WHEN the drag handle of a shown item is long pressed THEN no move into action is shown`() {
        setContent(
            CustomizableMenuItemsState(
                items = listOf(item("Shown 1"), item("Shown 2")),
                hiddenItems = listOf(expandable("More", item("Sub"), state = MenuItemState.DISABLED)),
            )
        )

        longPressHandle("Shown 1")

        composeTestRule.onNodeWithText(moveDown).assertIsEnabled()
        composeTestRule.onNodeWithText(moveInto("More")).assertDoesNotExist()
    }

    @Test
    fun `GIVEN a sub-item of an expanded item WHEN its drag handle is long pressed THEN move out of is enabled and move up or down is disabled at its ends`() {
        setContent(
            CustomizableMenuItemsState(
                items = listOf(item("Shown"), expandable("More", item("Sub 1"), item("Sub 2"))),
                expandedItemIds = setOf("More"),
            )
        )

        longPressHandle("Sub 1")

        composeTestRule.onNodeWithText(moveUp).assertIsNotEnabled()
        composeTestRule.onNodeWithText(moveDown).assertIsEnabled()
        composeTestRule.onNodeWithText(moveOutOf("More")).assertIsEnabled()
        composeTestRule.onNodeWithText(hide).assertIsEnabled()
        composeTestRule.onNodeWithText(moveInto("More")).assertDoesNotExist()

        composeTestRule.onNodeWithContentDescription(close).performClick()
        composeTestRule.waitForIdle()

        longPressHandle("Sub 2")

        composeTestRule.onNodeWithText(moveUp).assertIsEnabled()
        composeTestRule.onNodeWithText(moveDown).assertIsNotEnabled()
        composeTestRule.onNodeWithText(moveOutOf("More")).assertIsEnabled()
    }

    @Test
    fun `GIVEN a collapsed expandable item WHEN its drag handle is long pressed THEN move up, move down and hide item are shown and no move into`() {
        setContent(
            CustomizableMenuItemsState(items = listOf(item("First"), expandable("More", item("Sub")), item("Last")))
        )

        longPressHandle("More")

        composeTestRule.onNodeWithText(moveUp).assertIsEnabled()
        composeTestRule.onNodeWithText(moveDown).assertIsEnabled()
        composeTestRule.onNodeWithText(hide).assertIsEnabled()
        composeTestRule.onNodeWithText(moveInto("More")).assertDoesNotExist()
    }

    @Test
    fun `GIVEN a hidden item WHEN its drag handle is long pressed THEN move up and move down are disabled and unhide item is enabled`() {
        setContent(
            CustomizableMenuItemsState(
                items = listOf(item("Shown")),
                hiddenItems = listOf(item("Hidden 1"), item("Hidden 2")),
            )
        )

        longPressHandle("Hidden 2")

        composeTestRule.onNodeWithText(moveUp).assertIsNotEnabled()
        composeTestRule.onNodeWithText(moveDown).assertIsNotEnabled()
        composeTestRule.onNodeWithText(unhide).assertIsEnabled()
        composeTestRule.onNodeWithText(hide).assertDoesNotExist()
    }

    @Test
    fun `GIVEN the only shown item WHEN its drag handle is long pressed THEN hide item is disabled and clicking it changes nothing`() {
        val actions = mutableListOf<CustomizableMenuItemsAction>()
        val state = CustomizableMenuItemsState(items = listOf(item("Only")), hiddenItems = listOf(item("Hidden")))
        val store = setContent(state, listOf(capturing(actions)))

        longPressHandle("Only")

        composeTestRule.onNodeWithText(hide).assertIsNotEnabled()
        composeTestRule.onNodeWithText(hide).performClick()
        composeTestRule.waitForIdle()

        assertEquals(state, store.state)
        assertTrue(actions.none { it is ItemsChanged || it is AttemptedEmptySection })
        composeTestRule.onNodeWithText(hide).assertIsDisplayed()
    }

    @Test
    fun `WHEN the drag handle is held for a long press and then moved THEN the sheet is shown, the state is unchanged and the list is not scrolled`() {
        val state =
            CustomizableMenuItemsState(
                items = List(10) { item("Shown $it") },
                hiddenItems = List(10) { item("Hidden $it") },
            )
        val store = setContent(state)
        val boundsBefore = boundsOf("Shown 0")

        val from = handleOf("Shown 0")
        composeTestRule.onRoot().performTouchInput {
            down(from)
            advanceEventTime(viewConfiguration.longPressTimeoutMillis * 2)
            move()
        }
        composeTestRule.waitForIdle()
        val mainRoot = composeTestRule.onAllNodes(isRoot())[0]
        mainRoot.performTouchInput { moveBy(Offset(0f, -viewConfiguration.touchSlop * 20)) }
        composeTestRule.waitForIdle()
        mainRoot.performTouchInput { up() }
        composeTestRule.waitForIdle()

        composeTestRule.onNodeWithText(moveDown).assertIsEnabled()
        assertEquals(state, store.state)
        assertEquals(boundsBefore, boundsOf("Shown 0"))
        assertEquals(0f, scrolledByValue())
    }

    @Test
    fun `WHEN the drag handle is held for a long press THEN the sheet is shown before the finger is lifted and the user feels it`() {
        setContent(CustomizableMenuItemsState(items = listOf(item("Shown 1"), item("Shown 2"))))

        pressHandle("Shown 2")
        composeTestRule.onRoot().performTouchInput {
            advanceEventTime(viewConfiguration.longPressTimeoutMillis * 2)
            move()
        }
        composeTestRule.waitForIdle()

        composeTestRule.onNodeWithText(moveUp).assertIsDisplayed()
        assertEquals(listOf(HapticFeedbackType.LongPress), hapticFeedback.performed)
    }

    @Test
    fun `WHEN the drag handle is moved sideways and held for a long press THEN no sheet is shown and moving vertically drags the item`() {
        val store =
            setContent(CustomizableMenuItemsState(items = listOf(item("Shown 1"), item("Shown 2"), item("Shown 3"))))

        pressHandle("Shown 1")
        composeTestRule.onRoot().performTouchInput {
            moveBy(Offset(-viewConfiguration.touchSlop * 2, 0f))
            advanceEventTime(viewConfiguration.longPressTimeoutMillis * 2)
            move()
        }
        composeTestRule.waitForIdle()
        composeTestRule.onNodeWithText(moveUp).assertDoesNotExist()
        moveInSteps(boundsOf("Shown 3").bottomCenter)
        composeTestRule.onRoot().performTouchInput { up() }
        composeTestRule.waitForIdle()

        assertEquals(listOf("Shown 2", "Shown 3", "Shown 1"), store.state.items.map { it.id })
    }

    @Test
    fun `WHEN the drag handle is tapped THEN no sheet is shown`() {
        setContent(CustomizableMenuItemsState(items = listOf(item("Shown 1"), item("Shown 2"))))

        pressHandle("Shown 2")
        composeTestRule.onRoot().performTouchInput { up() }
        composeTestRule.waitForIdle()

        composeTestRule.onNodeWithText(moveUp).assertDoesNotExist()
    }

    @Test
    fun `GIVEN an expanded item or a hidden expanded item WHEN long pressing where its handle would be THEN no sheet is shown`() {
        setContent(
            CustomizableMenuItemsState(
                items = listOf(item("Shown"), expandable("More", item("Sub 1"))),
                hiddenItems = listOf(expandable("Hidden More", item("Sub 2"), state = MenuItemState.DISABLED)),
                expandedItemIds = setOf("More", "Hidden More"),
            )
        )

        longPressHandle("More")
        composeTestRule.onNodeWithText(moveUp).assertDoesNotExist()

        longPressHandle("Hidden More")
        composeTestRule.onNodeWithText(moveUp).assertDoesNotExist()
    }

    @Test
    fun `WHEN move down is chosen in the sheet THEN the item is moved in the store and the sheet closes`() {
        val actions = mutableListOf<CustomizableMenuItemsAction>()
        val store =
            setContent(
                CustomizableMenuItemsState(items = listOf(item("Shown 1"), item("Shown 2"), item("Shown 3"))),
                listOf(capturing(actions)),
            )
        longPressHandle("Shown 1")

        composeTestRule.onNodeWithText(moveDown).performClick()
        composeTestRule.waitForIdle()

        assertEquals(listOf("Shown 2", "Shown 1", "Shown 3"), store.state.items.map { it.id })
        assertEquals(
            ItemsChanged(items = store.state.items, hiddenItems = emptyList(), movedItemId = "Shown 1"),
            actions.filterIsInstance<ItemsChanged>().single(),
        )
        composeTestRule.onNodeWithText(moveDown).assertDoesNotExist()
    }

    @Test
    fun `WHEN hide item is chosen in the sheet THEN the item is the first hidden one`() {
        val store =
            setContent(
                CustomizableMenuItemsState(
                    items = listOf(item("Shown 1"), item("Shown 2")),
                    hiddenItems = listOf(item("Hidden")),
                )
            )
        longPressHandle("Shown 1")

        composeTestRule.onNodeWithText(hide).performClick()
        composeTestRule.waitForIdle()

        assertEquals(listOf("Shown 2"), store.state.items.map { it.id })
        assertEquals(listOf("Shown 1", "Hidden"), store.state.hiddenItems.map { it.id })
    }

    @Test
    fun `WHEN unhide item is chosen in the sheet THEN the item is placed after the last shown item which is not More`() {
        val store =
            setContent(
                CustomizableMenuItemsState(
                    items = listOf(item("Shown 1"), item("Shown 2"), expandable("More", item("Sub"))),
                    hiddenItems = listOf(item("Hidden")),
                )
            )
        longPressHandle("Hidden")

        composeTestRule.onNodeWithText(unhide).performClick()
        composeTestRule.waitForIdle()

        assertEquals(listOf("Shown 1", "Shown 2", "Hidden", "More"), store.state.items.map { it.id })
        assertTrue(store.state.hiddenItems.isEmpty())
    }

    @Test
    fun `WHEN move into is chosen in the sheet THEN the item is the first sub-item, More stays collapsed and the sheet closes`() {
        val store =
            setContent(
                CustomizableMenuItemsState(
                    items = listOf(item("Shown"), expandable("More", item("Sub"))),
                    expandedItemIds = emptySet(),
                )
            )
        longPressHandle("Shown")

        composeTestRule.onNodeWithText(moveInto("More")).performClick()
        composeTestRule.waitForIdle()

        assertEquals(listOf("More"), store.state.items.map { it.id })
        assertEquals(listOf("Shown", "Sub"), store.state.items.single().subItems.map { it.id })
        assertTrue(store.state.expandedItemIds.isEmpty())
        composeTestRule.onNodeWithText(moveInto("More")).assertDoesNotExist()
    }

    @Test
    fun `WHEN move out of is chosen in the sheet THEN the item is placed after the last shown item which is not More`() {
        val store =
            setContent(
                CustomizableMenuItemsState(
                    items = listOf(item("Shown"), expandable("More", item("Sub 1"), item("Sub 2"))),
                    expandedItemIds = setOf("More"),
                )
            )
        longPressHandle("Sub 1")

        composeTestRule.onNodeWithText(moveOutOf("More")).performClick()
        composeTestRule.waitForIdle()

        assertEquals(listOf("Shown", "Sub 1", "More"), store.state.items.map { it.id })
        assertEquals(listOf("Sub 2"), store.state.items.last().subItems.map { it.id })
        composeTestRule.onNodeWithText(moveOutOf("More")).assertDoesNotExist()
    }

    @Test
    fun `WHEN a disabled action is clicked in the sheet THEN nothing changes and the sheet stays open`() {
        val state = CustomizableMenuItemsState(items = listOf(item("Shown 1"), item("Shown 2")))
        val store = setContent(state)
        longPressHandle("Shown 1")

        composeTestRule.onNodeWithText(moveUp).performClick()
        composeTestRule.waitForIdle()

        assertEquals(state, store.state)
        composeTestRule.onNodeWithText(moveDown).assertIsDisplayed()
    }

    @Test
    fun `WHEN the sheet is dismissed with its handle THEN the items are unchanged and the sheet closes`() {
        val state = CustomizableMenuItemsState(items = listOf(item("Shown 1"), item("Shown 2")))
        val store = setContent(state)
        longPressHandle("Shown 2")

        composeTestRule.onNodeWithContentDescription(close).performClick()
        composeTestRule.waitForIdle()

        assertEquals(state, store.state)
        composeTestRule.onNodeWithText(moveUp).assertDoesNotExist()
    }

    @Test
    fun `WHEN the item loses its row while the sheet is open without a choice THEN the sheet closes`() {
        val store = setContent(CustomizableMenuItemsState(items = listOf(item("Shown 1"), item("Shown 2"))))
        longPressHandle("Shown 1")
        composeTestRule.onNodeWithText(moveDown).assertIsDisplayed()

        store.dispatch(
            ItemsChanged(items = listOf(item("Shown 2")), hiddenItems = emptyList(), movedItemId = "Shown 1")
        )
        composeTestRule.waitForIdle()

        composeTestRule.onNodeWithText(moveDown).assertDoesNotExist()
    }

    @Test
    fun `WHEN another action is chosen while the sheet hides after move into THEN only the first action is applied`() {
        val store =
            setContent(
                CustomizableMenuItemsState(
                    items = listOf(item("Shown 1"), item("Shown 2"), expandable("More", item("Sub")))
                )
            )
        longPressHandle("Shown 1")
        composeTestRule.mainClock.autoAdvance = false

        composeTestRule.onNodeWithText(moveInto("More")).performClick()
        composeTestRule.onNodeWithText(hide).performClick()
        composeTestRule.mainClock.autoAdvance = true
        composeTestRule.waitForIdle()

        assertEquals(listOf("Shown 2", "More"), store.state.items.map { it.id })
        assertEquals(listOf("Shown 1", "Sub"), store.state.items.last().subItems.map { it.id })
        assertTrue(store.state.hiddenItems.isEmpty())
    }

    private fun setContent(
        state: CustomizableMenuItemsState,
        middleware: List<Middleware<CustomizableMenuItemsState, CustomizableMenuItemsAction>> = emptyList(),
    ): CustomizableMenuItemsStore {
        val store = CustomizableMenuItemsStore(initialState = state, middleware = middleware)
        composeTestRule.setContent {
            AcornTheme {
                CompositionLocalProvider(LocalHapticFeedback provides hapticFeedback) {
                    EditMenuItemsContent(store = store)
                }
            }
        }
        return store
    }

    private fun capturing(
        actions: MutableList<CustomizableMenuItemsAction>
    ): Middleware<CustomizableMenuItemsState, CustomizableMenuItemsAction> = { _, next, action ->
        actions.add(action)
        next(action)
    }

    private fun boundsOf(name: String) = rowOf(name).fetchSemanticsNode().boundsInRoot

    private fun textBoundsOf(text: String) = composeTestRule.onNodeWithText(text).fetchSemanticsNode().boundsInRoot

    private fun rowOf(name: String) =
        composeTestRule.onNode(
            SemanticsMatcher("Row of $name") { node ->
                val description =
                    node.config.getOrElse(SemanticsProperties.ContentDescription) { emptyList() }.singleOrNull()
                description == name || description?.startsWith("$name. Position ") == true
            }
        )

    private fun moveInto(name: String) = testContext.getString(R.string.mozac_menu_customize_move_into, name)

    private fun moveOutOf(name: String) = testContext.getString(R.string.mozac_menu_customize_move_out_of, name)

    private fun scrolledByValue() =
        composeTestRule
            .onAllNodes(SemanticsMatcher.keyIsDefined(SemanticsProperties.VerticalScrollAxisRange))[0]
            .fetchSemanticsNode()
            .config[SemanticsProperties.VerticalScrollAxisRange]
            .value()

    /**
     * Tap the handle of the row with [title], drag it in steps through each of [via], in order, each evaluated when its
     * leg starts, then to [to] and let go of it.
     */
    private fun dragByHandle(title: String, via: List<() -> Offset> = emptyList(), to: () -> Offset) {
        dragFrom(handleOf(title), afterLongPress = false, via = via, to = to)
    }

    /** Hold the handle of the row with [title] for longer than a long press, then let go of it. */
    private fun longPressHandle(title: String) {
        val from = handleOf(title)
        composeTestRule.onRoot().performTouchInput {
            down(from)
            advanceEventTime(viewConfiguration.longPressTimeoutMillis * 2)
            // Send the delay with a stationary move, an empty batch would drop it.
            move()
            up()
        }
        composeTestRule.waitForIdle()
    }

    private fun pressHandle(title: String) {
        val from = handleOf(title)
        composeTestRule.onRoot().performTouchInput { down(from) }
    }

    /** Where the handle of the row with [title] is, at the end of the row. */
    private fun handleOf(title: String): Offset {
        val listBounds = composeTestRule.onRoot().fetchSemanticsNode().boundsInRoot
        val distanceFromEnd = with(composeTestRule.density) { 30.dp.toPx() }
        return Offset(listBounds.right - distanceFromEnd, boundsOf(title).center.y)
    }

    private class RecordingHapticFeedback : HapticFeedback {
        val performed = mutableListOf<HapticFeedbackType>()

        override fun performHapticFeedback(hapticFeedbackType: HapticFeedbackType) {
            performed += hapticFeedbackType
        }
    }

    /**
     * Press at [start], hold it for a long press if [afterLongPress], drag in steps through each of [via], in order,
     * each evaluated when its leg starts, then to [to] and let go.
     */
    private fun dragFrom(
        start: Offset,
        afterLongPress: Boolean,
        via: List<() -> Offset> = emptyList(),
        to: () -> Offset,
    ) {
        composeTestRule.onRoot().performTouchInput {
            down(start)
            if (!afterLongPress) moveBy(Offset(0f, viewConfiguration.touchSlop * 3))
        }
        if (afterLongPress) composeTestRule.mainClock.advanceTimeBy(LONG_PRESS_WAIT_MILLIS)
        composeTestRule.onRoot().performTouchInput { moveBy(Offset(0f, viewConfiguration.touchSlop * 3)) }
        composeTestRule.waitForIdle()

        (via + to).forEach { moveInSteps(it()) }
        composeTestRule.onRoot().performTouchInput { up() }
        composeTestRule.waitForIdle()
    }

    private fun moveInSteps(to: Offset) {
        var from = Offset.Zero
        composeTestRule.onRoot().performTouchInput { from = checkNotNull(currentPosition()) }
        val steps = 10
        val step = (to - from) / steps.toFloat()
        repeat(steps) {
            composeTestRule.onRoot().performTouchInput { moveBy(step) }
            composeTestRule.waitForIdle()
        }
    }

    private fun item(title: String) =
        CustomizableMenuItem(id = title, item = StandardMenuItem(title = MenuItemTitle(Text.String(title))))

    private fun topLevelOnlyItem(title: String) = item(title).copy(isTopLevelOnly = true)

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

private const val LONG_PRESS_WAIT_MILLIS = 1_000L
