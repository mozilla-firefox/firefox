/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.annotation.StringRes
import androidx.compose.ui.semantics.CollectionInfo
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.SemanticsActions
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.assertCountEquals
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasScrollAction
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.isHeading
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithText
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
import mozilla.components.compose.menu.store.CustomizableMenuItemsState
import mozilla.components.compose.menu.store.CustomizableMenuItemsStore
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemState
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
    fun `THEN an item which is not expandable has no custom actions`() {
        setContent(CustomizableMenuItemsState(items = listOf(item("Shown"), expandable("More", item("Sub")))))

        assertEquals(emptyList<String>(), actionLabelsOf("Shown"))
    }

    @Test
    fun `GIVEN a collapsed expandable item THEN its custom action is to expand it`() {
        setContent(CustomizableMenuItemsState(items = listOf(item("Shown"), expandable("More", item("Sub")))))

        assertEquals(listOf(expand), actionLabelsOf("More"))
    }

    @Test
    fun `GIVEN an expanded expandable item THEN its custom action is to collapse it`() {
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

        assertEquals(listOf(expand), actionLabelsOf("More", isHidden = true))
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

    private fun shownItem(name: String, position: Int, count: Int) =
        testContext.getString(R.string.mozac_menu_customize_shown_item, name, position, count)

    private fun assertSingleNode(name: String, description: String, vararg texts: String) {
        composeTestRule
            .onAllNodes(hasContentDescription(name, substring = true) or hasText(name, substring = true))
            .assertCountEquals(1)
        composeTestRule.onNodeWithContentDescription(description).assertExists()
        texts.forEach { composeTestRule.onNodeWithText(it).assertDoesNotExist() }
    }

    private fun rowNode(name: String, isHidden: Boolean) =
        composeTestRule.onNode(
            SemanticsMatcher("Row of $name") { node ->
                val description =
                    node.config.getOrElse(SemanticsProperties.ContentDescription) { emptyList() }.singleOrNull()
                if (isHidden) description == name else description?.startsWith("$name. Position ") == true
            }
        )

    private fun actionLabelsOf(name: String, isHidden: Boolean = false) =
        rowNode(name, isHidden)
            .fetchSemanticsNode()
            .config
            .getOrElse(SemanticsActions.CustomActions) { emptyList() }
            .map { it.label }

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

    private fun setContent(state: CustomizableMenuItemsState): CustomizableMenuItemsStore {
        val store = CustomizableMenuItemsStore(initialState = state)
        composeTestRule.setContent { AcornTheme { EditMenuItemsContent(store = store) } }
        return store
    }

    private fun summary(text: String) = MenuItemSummary(Text.String(text))

    private fun item(title: String) =
        CustomizableMenuItem(id = title, item = StandardMenuItem(title = MenuItemTitle(Text.String(title))))

    private fun CustomizableMenuItem.withState(state: MenuItemState) =
        copy(item = (item as StandardMenuItem).copy(state = state))

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
