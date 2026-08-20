package org.mozilla.fenix.tabstray.ui.tabitems

import androidx.compose.material3.Surface
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.hasAnyAncestor
import androidx.compose.ui.test.hasClickAction
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.mozilla.fenix.tabstray.TabsTrayTestTag
import org.mozilla.fenix.tabstray.controller.NoOpTabInteractionHandler
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.data.createTab
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState
import org.mozilla.fenix.tabstray.ui.tabpage.TabLayout
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.theme.Theme

@RunWith(AndroidJUnit4::class)
class TabGroupOnboardingItemTest {
    @get:Rule val composeTestRule = createComposeRule()

    private val context = InstrumentationRegistry.getInstrumentation().targetContext

    private val dismissContentDescription
        get() = context.getString(R.string.tab_group_onboarding_item_dismiss_content_description)

    @Test
    fun verifyOnboardingGridItemVisible() {
        val title = context.getString(R.string.tab_group_onboarding_item_title)
        val description = context.getString(R.string.tab_group_onboarding_grid_item_description)

        composeTestRule.setContent {
            FirefoxTheme(theme = Theme.Light) {
                Surface {
                    TabGroupOnboardingGridItem(onDismiss = {})
                }
            }
        }

        composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_GROUP_ONBOARDING_GRID_ITEM).assertIsDisplayed()
        composeTestRule
            .onNodeWithTag(
                TabsTrayTestTag.TAB_GROUP_ONBOARDING_ILLUSTRATION,
                useUnmergedTree = true,
            )
            .assertIsDisplayed()
        composeTestRule.onNodeWithText(title).assertIsDisplayed()
        composeTestRule.onNodeWithText(description).assertIsDisplayed()
    }

    @Test
    fun verifyOnboardingListItemVisible() {
        val title = context.getString(R.string.tab_group_onboarding_item_title)
        val description = context.getString(R.string.tab_group_onboarding_list_item_description)

        composeTestRule.setContent {
            FirefoxTheme(theme = Theme.Light) {
                Surface {
                    TabGroupOnboardingListItem(onDismiss = {})
                }
            }
        }

        composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_GROUP_ONBOARDING_LIST_ITEM).assertIsDisplayed()
        composeTestRule
            .onNodeWithTag(
                TabsTrayTestTag.TAB_GROUP_ONBOARDING_ILLUSTRATION,
                useUnmergedTree = true,
            )
            .assertIsDisplayed()
        composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_GROUP_ONBOARDING_ITEM_DISMISS).assertIsDisplayed()
        composeTestRule.onNodeWithText(title).assertIsDisplayed()
        composeTestRule.onNodeWithText(description).assertIsDisplayed()
    }

    @Test
    fun verifyOnboardingGridItemDismiss() {
        var dismissed = false

        composeTestRule.setContent {
            FirefoxTheme(theme = Theme.Light) {
                Surface {
                    TabGroupOnboardingGridItem(onDismiss = { dismissed = true })
                }
            }
        }

        composeTestRule
            .onNode(hasClickAction() and hasAnyAncestor(hasTestTag(TabsTrayTestTag.TAB_GROUP_ONBOARDING_GRID_ITEM)))
            .performClick()

        assertTrue(dismissed)
    }

    @Test
    fun verifyOnboardingListItemDismissInvokesCallback() {
        var dismissed = false

        composeTestRule.setContent {
            FirefoxTheme(theme = Theme.Light) {
                Surface {
                    TabGroupOnboardingListItem(onDismiss = { dismissed = true })
                }
            }
        }

        composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_GROUP_ONBOARDING_ITEM_DISMISS).performClick()

        assertTrue(dismissed)
    }

    @Test
    fun verifyOnboardingGridItemDismissIsLabeled() {
        composeTestRule.setContent {
            FirefoxTheme(theme = Theme.Light) {
                Surface {
                    TabGroupOnboardingGridItem(onDismiss = {})
                }
            }
        }

        composeTestRule
            .onNode(hasContentDescription(dismissContentDescription) and hasClickAction())
            .assertIsDisplayed()
    }

    @Test
    fun verifyOnboardingListItemDismissIsLabeled() {
        composeTestRule.setContent {
            FirefoxTheme(theme = Theme.Light) {
                Surface {
                    TabGroupOnboardingListItem(onDismiss = {})
                }
            }
        }

        composeTestRule
            .onNode(hasContentDescription(dismissContentDescription) and hasClickAction())
            .assertIsDisplayed()
    }

    @Test
    fun verifyOnboardingDisplayedInGrid() {
        setTabLayoutContent(displayTabGroupOnboarding = true)

        composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_GROUP_ONBOARDING_GRID_ITEM).assertIsDisplayed()
    }

    @Test
    fun verifyOnboardingNotDisplayedInGrid() {
        setTabLayoutContent(displayTabGroupOnboarding = false)

        composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_GROUP_ONBOARDING_GRID_ITEM).assertDoesNotExist()
    }

    @Test
    fun verifyOnboardingDisplayedInList() {
        setTabLayoutContent(displayTabGroupOnboarding = true, displayTabsInGrid = false)

        composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_GROUP_ONBOARDING_LIST_ITEM).assertIsDisplayed()
    }

    @Test
    fun verifyOnboardingNotDisplayedInList() {
        setTabLayoutContent(displayTabGroupOnboarding = false, displayTabsInGrid = false)

        composeTestRule.onNodeWithTag(TabsTrayTestTag.TAB_GROUP_ONBOARDING_LIST_ITEM).assertDoesNotExist()
    }

    private fun setTabLayoutContent(
        displayTabGroupOnboarding: Boolean,
        displayTabsInGrid: Boolean = true,
    ) {
        val tabs: List<TabsTrayItem> =
            listOf(
                createTab(url = "www.mozilla.org"),
                createTab(url = "www.example.com"),
            )
        composeTestRule.setContent {
            FirefoxTheme(theme = Theme.Light) {
                Surface {
                    TabLayout(
                        tabs = tabs,
                        displayTabsInGrid = displayTabsInGrid,
                        dragAndDropEnabled = true,
                        displayTabGroupOnboarding = displayTabGroupOnboarding,
                        selectedItemIndex = 0,
                        selectionMode = TabsTrayState.Mode.Normal,
                        focusEnabled = true,
                        tabInteractionHandler = NoOpTabInteractionHandler,
                        onTabClose = {},
                        onItemClick = {},
                        onItemLongClick = {},
                        onEditTabGroupClick = {},
                        onCloseTabGroupClick = {},
                        onShareTabGroupClick = {},
                        onDeleteTabGroupClick = {},
                        onTabGroupOnboardingDismiss = {},
                        liveReorderEnabled = false,
                    )
                }
            }
        }
    }
}
