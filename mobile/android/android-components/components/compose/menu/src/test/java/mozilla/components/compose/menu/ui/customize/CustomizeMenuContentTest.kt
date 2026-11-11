/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.getUnclippedBoundsInRoot
import androidx.compose.ui.test.hasScrollAction
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performScrollToNode
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.R
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.CustomizableMenuState
import mozilla.components.compose.menu.store.CustomizableMenuStore
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.ui.icons.R as iconsR
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class CustomizeMenuContentTest {
    @get:Rule val composeTestRule = createComposeRule()

    private val hiddenSectionsTitle = testContext.getString(R.string.mozac_menu_customize_hidden_sections)
    private val dragToHide = testContext.getString(R.string.mozac_menu_customize_drag_to_hide)
    private val resetToDefault = testContext.getString(R.string.mozac_menu_customize_reset_to_default)

    @Test
    fun `GIVEN shown, mandatory and hidden sections THEN the shown and mandatory ones are shown in their order with the hidden ones below the hidden sections title`() {
        setContent(
            menuState(
                shown = listOf(row("Shown 1"), row("Mandatory"), row("Shown 2")),
                hidden = listOf(row("Hidden")),
                mandatoryIds = setOf("Mandatory"),
            )
        )

        val shown1Bounds = composeTestRule.onNodeWithText("Shown 1").getUnclippedBoundsInRoot()
        val mandatoryBounds = composeTestRule.onNodeWithText("Mandatory").getUnclippedBoundsInRoot()
        val shown2Bounds = composeTestRule.onNodeWithText("Shown 2").getUnclippedBoundsInRoot()
        val hiddenTitleBounds = composeTestRule.onNodeWithText(hiddenSectionsTitle).getUnclippedBoundsInRoot()
        val hiddenBounds = composeTestRule.onNodeWithText("Hidden").getUnclippedBoundsInRoot()
        assertTrue(shown1Bounds.bottom <= mandatoryBounds.top)
        assertTrue(mandatoryBounds.bottom <= shown2Bounds.top)
        assertTrue(shown2Bounds.bottom <= hiddenTitleBounds.top)
        assertTrue(hiddenTitleBounds.bottom <= hiddenBounds.top)
    }

    @Test
    fun `GIVEN no hidden sections THEN the area to drag sections into to hide them is shown`() {
        setContent(menuState(shown = listOf(row("Shown"))))

        composeTestRule.onNodeWithText(dragToHide).assertIsDisplayed()
    }

    @Test
    fun `GIVEN hidden sections THEN the area to drag sections into to hide them is not shown`() {
        setContent(menuState(shown = listOf(row("Shown")), hidden = listOf(row("Hidden"))))

        composeTestRule.onNodeWithText(dragToHide).assertDoesNotExist()
    }

    @Test
    fun `GIVEN the menu is customized AND hidden sections THEN the button to reset it is shown below the hidden sections`() {
        setContent(menuState(shown = listOf(row("Shown")), hidden = listOf(row("Hidden")), isCustomized = true))

        val hiddenBounds = composeTestRule.onNodeWithText("Hidden").getUnclippedBoundsInRoot()
        val resetBounds = composeTestRule.onNodeWithText(resetToDefault).getUnclippedBoundsInRoot()
        assertTrue(hiddenBounds.bottom <= resetBounds.top)
    }

    @Test
    fun `GIVEN the menu is customized AND no hidden sections THEN the button to reset it is shown below the area to drag sections into to hide them`() {
        setContent(menuState(shown = listOf(row("Shown")), isCustomized = true))

        val dragToHideBounds = composeTestRule.onNodeWithText(dragToHide).getUnclippedBoundsInRoot()
        val resetBounds = composeTestRule.onNodeWithText(resetToDefault).getUnclippedBoundsInRoot()
        assertTrue(dragToHideBounds.bottom <= resetBounds.top)
    }

    @Test
    fun `GIVEN the menu is not customized THEN the button to reset it is not shown`() {
        setContent(menuState(shown = listOf(row("Shown")), isCustomized = false))

        composeTestRule.onNodeWithText(resetToDefault).assertDoesNotExist()
    }

    @Test
    fun `GIVEN more sections than fit on the screen WHEN scrolling to the last one THEN it is shown`() {
        setContent(menuState(shown = List(10) { row("Shown $it") }, hidden = List(10) { row("Hidden $it") }))

        composeTestRule.onNode(hasScrollAction()).performScrollToNode(hasText("Hidden 9"))

        composeTestRule.onNodeWithText("Hidden 9").assertIsDisplayed()
    }

    private fun setContent(state: CustomizableMenuState) {
        composeTestRule.setContent {
            AcornTheme {
                CustomizeMenuContent(store = CustomizableMenuStore(initialState = state))
            }
        }
    }

    /**
     * State of a screen showing the [shown] groups, with the [mandatoryIds] ones as mandatory, then the [hidden] ones.
     */
    private fun menuState(
        shown: List<MenuItemsGroup>,
        hidden: List<MenuItemsGroup> = emptyList(),
        mandatoryIds: Set<String> = emptySet(),
        isCustomized: Boolean = false,
    ) =
        CustomizableMenuState(
            menuGroups = shown + hidden,
            mandatoryGroupIds = mandatoryIds,
            hiddenGroupIds = hidden.map { it.id }.toSet(),
            isCustomized = isCustomized,
        )

    private fun row(title: String) =
        MenuItemsGroup.Row(
            id = title,
            items =
                listOf(
                    StandardMenuItem(
                        title = MenuItemTitle(Text.String(title)),
                        icon = MenuItemIconRes(iconsR.drawable.mozac_ic_settings_24),
                    )
                ),
        )
}
