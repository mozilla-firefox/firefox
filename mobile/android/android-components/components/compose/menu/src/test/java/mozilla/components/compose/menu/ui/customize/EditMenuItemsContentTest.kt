/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.getUnclippedBoundsInRoot
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.R
import mozilla.components.compose.menu.data.CustomizableMenuItem
import mozilla.components.compose.menu.data.ExpandableMenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.PreviewClicked
import mozilla.components.compose.menu.store.CustomizableMenuItemsState
import mozilla.components.compose.menu.store.CustomizableMenuItemsStore
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemState
import mozilla.components.lib.state.Middleware
import mozilla.components.support.test.robolectric.testContext
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

    private fun setContent(
        state: CustomizableMenuItemsState,
        middleware: List<Middleware<CustomizableMenuItemsState, CustomizableMenuItemsAction>> = emptyList(),
    ) {
        val store = CustomizableMenuItemsStore(initialState = state, middleware = middleware)
        composeTestRule.setContent { AcornTheme { EditMenuItemsContent(store = store) } }
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
