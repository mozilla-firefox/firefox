/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.isHeading
import androidx.compose.ui.test.junit4.v2.createComposeRule
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
        isCustomized: Boolean = false,
    ) =
        CustomizableMenuState(
            menuGroups = shown,
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
