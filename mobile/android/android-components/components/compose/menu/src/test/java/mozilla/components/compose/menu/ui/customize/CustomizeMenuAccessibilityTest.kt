/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.ui.semantics.CollectionInfo
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.hasScrollAction
import androidx.compose.ui.test.isHeading
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithText
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
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class CustomizeMenuAccessibilityTest {
    @get:Rule val composeTestRule = createComposeRule()

    private val dragToReorder = testContext.getString(R.string.mozac_menu_customize_drag_to_reorder)
    private val hiddenSectionsTitle = testContext.getString(R.string.mozac_menu_customize_hidden_sections)
    private val preview = testContext.getString(R.string.mozac_menu_customize_preview)
    private val resetToDefault = testContext.getString(R.string.mozac_menu_customize_reset_to_default)

    @Test
    fun `THEN the titles above the shown sections and above the hidden sections are headings`() {
        setContent(menuState(shown = listOf(row("Shown"))))

        composeTestRule.onNodeWithText(dragToReorder).assert(isHeading())
        composeTestRule.onNodeWithText(hiddenSectionsTitle).assert(isHeading())
    }

    @Test
    fun `GIVEN the menu is customized THEN preview and reset to default are buttons`() {
        setContent(menuState(shown = listOf(row("Shown")), isCustomized = true))

        composeTestRule.onNodeWithText(preview).assert(hasRole(Role.Button))
        composeTestRule.onNodeWithText(resetToDefault).assert(hasRole(Role.Button))
    }

    @Test
    fun `THEN each section is a single node reading the names of its items and its position among the shown sections`() {
        setContent(
            menuState(
                shown = listOf(row(id = "first", "First 1", "First 2"), row(id = "second", "Second")),
                hidden = listOf(row("Hidden")),
            )
        )

        composeTestRule.onNodeWithContentDescription(shownSection("First 1, First 2", 1, 2)).assertExists()
        composeTestRule.onNodeWithContentDescription(shownSection("Second", 2, 2)).assertExists()
        composeTestRule.onNodeWithText("First 1").assertDoesNotExist()
        composeTestRule.onNodeWithText("First 2").assertDoesNotExist()
    }

    @Test
    fun `THEN a hidden section reads the names of its items without a position AND it is not disabled`() {
        setContent(
            menuState(
                shown = listOf(row("Shown")),
                hidden = listOf(row(id = "hidden", "Hidden 1", "Hidden 2")),
            )
        )

        composeTestRule
            .onNodeWithContentDescription(label("Hidden 1, Hidden 2"))
            .assert(SemanticsMatcher.keyNotDefined(SemanticsProperties.Disabled))
    }

    @Test
    fun `THEN the list does not tell how many rows it has, which are not all sections`() {
        setContent(menuState(shown = listOf(row("First"), row("Second")), hidden = listOf(row("Hidden"))))

        composeTestRule
            .onNode(hasScrollAction())
            .assert(SemanticsMatcher.expectValue(SemanticsProperties.CollectionInfo, CollectionInfo(-1, 1)))
    }

    @Test
    fun `GIVEN a section with more items than its collapsed list shows THEN it reads the names of all of them`() {
        setContent(menuState(shown = listOf(row(id = "long", "One", "Two", "Three", "Four", "Five"))))

        composeTestRule.onNodeWithContentDescription(shownSection("One, Two, Three, Four, Five", 1, 1)).assertExists()
    }

    @Test
    fun `GIVEN a grid section THEN it reads the names of its items`() {
        setContent(menuState(shown = listOf(grid(id = "grid", "Back", "Forward"))))

        composeTestRule.onNodeWithContentDescription(shownSection("Back, Forward", 1, 1)).assertExists()
    }

    @Test
    fun `GIVEN a named section THEN it reads its name before the names of its items`() {
        setContent(menuState(shown = listOf(grid(id = "grid", "Back", "Forward").copy(name = Text.String("Controls")))))

        composeTestRule
            .onNodeWithContentDescription(shownSection("Back, Forward", 1, 1, name = "Controls"))
            .assertExists()
    }

    /**
     * What the section with [itemNames] and [name], if any, reads when it is shown at [position] among [count] shown
     * sections.
     */
    private fun shownSection(itemNames: String, position: Int, count: Int, name: String? = null) =
        testContext.getString(R.string.mozac_menu_customize_shown_section, label(itemNames, name), position, count)

    /** What the section with [itemNames] and [name], if any, is read as, before its position if it is shown. */
    private fun label(itemNames: String, name: String? = null) =
        if (name == null) {
            testContext.getString(R.string.mozac_menu_customize_unnamed_section, itemNames)
        } else {
            testContext.getString(R.string.mozac_menu_customize_named_section, name, itemNames)
        }

    private fun hasRole(role: Role) = SemanticsMatcher.expectValue(SemanticsProperties.Role, role)

    private fun setContent(state: CustomizableMenuState) {
        val store = CustomizableMenuStore(initialState = state)
        composeTestRule.setContent {
            AcornTheme {
                CustomizeMenuContent(store = store)
            }
        }
    }

    private fun menuState(
        shown: List<MenuItemsGroup>,
        hidden: List<MenuItemsGroup> = emptyList(),
        isCustomized: Boolean = false,
    ) =
        CustomizableMenuState(
            menuGroups = shown + hidden,
            hiddenGroupIds = hidden.map { it.id }.toSet(),
            isCustomized = isCustomized,
        )

    private fun row(title: String) = row(id = title, title)

    private fun row(id: String, vararg titles: String) =
        MenuItemsGroup.Row(
            id = id,
            items =
                titles.map { title ->
                    StandardMenuItem(
                        title = MenuItemTitle(Text.String(title)),
                        icon = MenuItemIconRes(iconsR.drawable.mozac_ic_settings_24),
                    )
                },
        )

    private fun grid(id: String, vararg titles: String) =
        MenuItemsGroup.Grid(
            id = id,
            items =
                titles.map { title ->
                    StandardMenuItem(
                        title = MenuItemTitle(Text.String(title)),
                        icon = MenuItemIconRes(iconsR.drawable.mozac_ic_settings_24),
                    )
                },
        )
}
