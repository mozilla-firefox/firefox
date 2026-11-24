/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.getUnclippedBoundsInRoot
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.unit.dp
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.R
import mozilla.components.compose.menu.data.CustomizableMenuItem
import mozilla.components.compose.menu.data.ExpandableMenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction
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

    @Test
    fun `GIVEN shown and hidden items THEN the shown ones are above the hidden items title and the hidden ones below it`() {
        setContent(
            CustomizableMenuItemsState(
                items = listOf(item("Shown 1"), item("Shown 2")),
                hiddenItems = listOf(item("Hidden")),
            )
        )

        val dragBarBounds = composeTestRule.onNodeWithText(dragToReorder).getUnclippedBoundsInRoot()
        val shown1Bounds = composeTestRule.onNodeWithText("Shown 1").getUnclippedBoundsInRoot()
        val shown2Bounds = composeTestRule.onNodeWithText("Shown 2").getUnclippedBoundsInRoot()
        val titleBounds = composeTestRule.onNodeWithText(hiddenItemsTitle).getUnclippedBoundsInRoot()
        val hiddenBounds = composeTestRule.onNodeWithText("Hidden").getUnclippedBoundsInRoot()
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

        composeTestRule.onNodeWithText("Shown").assertIsDisplayed()
    }

    @Test
    fun `WHEN the user taps an expandable item THEN its sub-items are shown or not`() {
        setContent(
            CustomizableMenuItemsState(
                items = listOf(expandable("More", item("Sub-item"))),
                expandedItemIds = setOf("More"),
            )
        )
        composeTestRule.onNodeWithText("Sub-item").assertIsDisplayed()

        composeTestRule.onNodeWithText("More").performClick()
        composeTestRule.onNodeWithText("Sub-item").assertDoesNotExist()

        composeTestRule.onNodeWithText("More").performClick()
        composeTestRule.onNodeWithText("Sub-item").assertIsDisplayed()
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
        composeTestRule.onNodeWithText("Sub-item").assertIsDisplayed()

        composeTestRule.onNodeWithText("More").performClick()
        composeTestRule.onNodeWithText("Sub-item").assertDoesNotExist()

        composeTestRule.onNodeWithText("More").performClick()
        composeTestRule.onNodeWithText("Sub-item").assertIsDisplayed()
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
            ItemsChanged(items = store.state.items, hiddenItems = emptyList()),
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
    fun `WHEN a shown item is dragged by its handle among the sub-items of another item THEN it is put back`() {
        val actions = mutableListOf<CustomizableMenuItemsAction>()
        val more = expandable("More", item("Sub 1"), item("Sub 2"))
        val store =
            setContent(
                CustomizableMenuItemsState(items = listOf(item("Shown"), more), expandedItemIds = setOf("More")),
                listOf(capturing(actions)),
            )

        dragByHandle(title = "Shown", to = { boundsOf("Sub 2").center })

        assertEquals(listOf("Shown", "More"), store.state.items.map { it.id })
        assertTrue(actions.none { it is ItemsChanged })
    }

    @Test
    fun `WHEN a shown item is dragged by its handle into the area to hide items THEN it is put back`() {
        val actions = mutableListOf<CustomizableMenuItemsAction>()
        val store =
            setContent(
                CustomizableMenuItemsState(items = listOf(item("Shown 1"), item("Shown 2"))),
                listOf(capturing(actions)),
            )

        dragByHandle(title = "Shown 1", to = { boundsOf(dragToHide).center })

        assertEquals(listOf("Shown 1", "Shown 2"), store.state.items.map { it.id })
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
            ItemsChanged(items = store.state.items, hiddenItems = emptyList()),
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

    private fun boundsOf(text: String) = composeTestRule.onNodeWithText(text).fetchSemanticsNode().boundsInRoot

    /** Tap the handle of the row with [title], drag it in steps to [to] and let go of it. */
    private fun dragByHandle(title: String, to: () -> Offset) {
        val listBounds = composeTestRule.onRoot().fetchSemanticsNode().boundsInRoot
        val distanceFromEnd = with(composeTestRule.density) { 30.dp.toPx() }
        dragFrom(Offset(listBounds.right - distanceFromEnd, boundsOf(title).center.y), afterLongPress = false, to = to)
    }

    /** Press at [start], hold it for a long press if [afterLongPress], drag in steps to [to] and let go. */
    private fun dragFrom(start: Offset, afterLongPress: Boolean, to: () -> Offset) {
        composeTestRule.onRoot().performTouchInput {
            down(start)
            if (!afterLongPress) moveBy(Offset(0f, viewConfiguration.touchSlop * 3))
        }
        if (afterLongPress) composeTestRule.mainClock.advanceTimeBy(LONG_PRESS_WAIT_MILLIS)
        composeTestRule.onRoot().performTouchInput { moveBy(Offset(0f, viewConfiguration.touchSlop * 3)) }
        composeTestRule.waitForIdle()

        var from = Offset.Zero
        composeTestRule.onRoot().performTouchInput { from = checkNotNull(currentPosition()) }
        val steps = 10
        val step = (to() - from) / steps.toFloat()
        repeat(steps) {
            composeTestRule.onRoot().performTouchInput { moveBy(step) }
            composeTestRule.waitForIdle()
        }
        composeTestRule.onRoot().performTouchInput { up() }
        composeTestRule.waitForIdle()
    }

    private fun item(title: String) =
        CustomizableMenuItem(id = title, item = StandardMenuItem(title = MenuItemTitle(Text.String(title))))

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
