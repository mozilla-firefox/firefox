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
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.getUnclippedBoundsInRoot
import androidx.compose.ui.test.hasScrollAction
import androidx.compose.ui.test.isHeading
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.test.performScrollToNode
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.R
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.CustomizableMenuAction
import mozilla.components.compose.menu.store.CustomizableMenuAction.MenuStructureChanged
import mozilla.components.compose.menu.store.CustomizableMenuAction.Update
import mozilla.components.compose.menu.store.CustomizableMenuState
import mozilla.components.compose.menu.store.CustomizableMenuStore
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.compose.menu.ui.utils.A11Y_ANNOUNCEMENT_DURATION
import mozilla.components.lib.state.Middleware
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.ui.icons.R as iconsR
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
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
    private val moveUp = testContext.getString(R.string.mozac_menu_customize_move_up)
    private val moveDown = testContext.getString(R.string.mozac_menu_customize_move_down)
    private val hide = testContext.getString(R.string.mozac_menu_customize_hide)
    private val unhide = testContext.getString(R.string.mozac_menu_customize_unhide)
    private val editItems = testContext.getString(R.string.mozac_menu_customize_edit_items)

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

    @Test
    fun `GIVEN three shown sections THEN each can be hidden, the first moved down, the middle one up and down and the last one up`() {
        setContent(menuState(shown = listOf(row("First"), row("Middle"), row("Last")), hidden = listOf(row("Hidden"))))

        assertEquals(listOf(moveDown, hide), actionsOf("First"))
        assertEquals(listOf(moveUp, moveDown, hide), actionsOf("Middle"))
        assertEquals(listOf(moveUp, hide), actionsOf("Last"))
    }

    @Test
    fun `GIVEN hidden sections THEN they can only be added to the menu`() {
        setContent(menuState(shown = listOf(row("Shown")), hidden = listOf(row("Hidden 1"), row("Hidden 2"))))

        assertEquals(listOf(unhide), actionsOf("Hidden 1"))
        assertEquals(listOf(unhide), actionsOf("Hidden 2"))
    }

    @Test
    fun `GIVEN a mandatory section THEN it can be moved up and down but not hidden`() {
        setContent(
            menuState(
                shown = listOf(row("First"), row("Mandatory"), row("Last")),
                mandatoryIds = setOf("Mandatory"),
            )
        )

        assertEquals(listOf(moveUp, moveDown), actionsOf("Mandatory"))
    }

    @Test
    fun `GIVEN a section whose items can be edited THEN it has the custom action editing them, after the ones moving it`() {
        setContent(
            menuState(
                shown = listOf(row("First"), row("Editable")),
                hidden = listOf(row("Hidden editable")),
                editableIds = setOf("Editable", "Hidden editable"),
            )
        )

        assertEquals(listOf(moveUp, hide, editItems), actionsOf("Editable"))
        assertEquals(listOf(unhide), actionsOf("Hidden editable"))
        assertEquals(listOf(moveDown, hide), actionsOf("First"))
    }

    @Test
    fun `WHEN the custom action editing the items of a section is performed THEN tell the store`() {
        val actions = mutableListOf<CustomizableMenuAction>()
        val captureActions: Middleware<CustomizableMenuState, CustomizableMenuAction> = { _, next, action ->
            actions.add(action)
            next(action)
        }
        setContent(
            menuState(shown = listOf(row("First"), row("Editable")), editableIds = setOf("Editable")),
            middleware = listOf(captureActions),
        )

        performAction("Editable", editItems)

        assertTrue(CustomizableMenuAction.EditGroupItemsClicked("Editable") in actions)
    }

    @Test
    fun `WHEN a shown section is hidden THEN it is the first hidden section`() {
        val store = setContent(menuState(shown = listOf(row("First"), row("Second")), hidden = listOf(row("Hidden"))))

        performAction("First", hide)

        assertEquals(listOf("Second", "First", "Hidden"), store.state.menuGroups.map { it.id })
        assertEquals(setOf("First", "Hidden"), store.state.hiddenGroupIds)
        assertEquals(listOf(unhide), actionsOf("First"))
    }

    @Test
    fun `WHEN a hidden section is added to the menu THEN it is the last shown section`() {
        val store =
            setContent(
                menuState(
                    shown = listOf(row("First"), row("Second")),
                    hidden = listOf(row("Hidden 1"), row("Hidden 2")),
                )
            )

        performAction("Hidden 2", unhide)

        assertEquals(listOf("First", "Second", "Hidden 2", "Hidden 1"), store.state.menuGroups.map { it.id })
        assertEquals(setOf("Hidden 1"), store.state.hiddenGroupIds)
        composeTestRule.onNodeWithContentDescription(shownSection("Hidden 2", 3, 3)).assertExists()
    }

    @Test
    fun `WHEN a shown section is moved up THEN it is shown before the section that was before it`() {
        val store = setContent(menuState(shown = listOf(row("First"), row("Second"), row("Third"))))

        performAction("Third", moveUp)

        assertEquals(listOf("First", "Third", "Second"), store.state.menuGroups.map { it.id })
        assertTrue(store.state.hiddenGroupIds.isEmpty())
        composeTestRule.onNodeWithContentDescription(shownSection("Third", 2, 3)).assertExists()
    }

    @Test
    fun `WHEN a shown section is moved down THEN it is shown after the section that was after it AND the move is saved`() {
        val actions = mutableListOf<CustomizableMenuAction>()
        val store =
            setContent(
                menuState(shown = listOf(row("First"), row("Second")), hidden = listOf(row("Hidden"))),
                middleware =
                    listOf { _, next, action ->
                        actions += action
                        next(action)
                    },
            )

        performAction("First", moveDown)

        assertEquals(
            MenuStructureChanged(
                items = listOf(row("Second"), row("First"), row("Hidden")),
                hiddenGroupIds = setOf("Hidden"),
                movedGroupId = "First",
            ),
            actions.filterIsInstance<MenuStructureChanged>().single(),
        )
        assertEquals(listOf("Second", "First", "Hidden"), store.state.menuGroups.map { it.id })
    }

    @Test
    fun `GIVEN no section was moved by an action THEN nothing is announced`() {
        setContent(menuState(shown = listOf(row("First"), row("Second"))))

        announcementNode().assert(SemanticsMatcher.keyNotDefined(SemanticsProperties.ContentDescription))
    }

    @Test
    fun `WHEN a section is moved up or down THEN its new position is announced politely`() {
        setContent(menuState(shown = listOf(row("First"), row("Second"), row("Third"))))

        performAction("First", moveDown)

        announcementNode()
            .assert(SemanticsMatcher.expectValue(SemanticsProperties.LiveRegion, LiveRegionMode.Polite))
            .assert(hasAnnouncement(R.string.mozac_menu_customize_section_moved, "First", 2, 3))

        performAction("Third", moveUp)

        announcementNode().assert(hasAnnouncement(R.string.mozac_menu_customize_section_moved, "Third", 2, 3))
    }

    @Test
    fun `WHEN a section is hidden THEN it is announced as removed`() {
        setContent(menuState(shown = listOf(row("First"), row("Second"))))

        performAction("Second", hide)

        announcementNode().assert(hasAnnouncement(R.string.mozac_menu_customize_section_hidden, "Second"))
    }

    @Test
    fun `WHEN a hidden section is added to the menu THEN it is announced as added`() {
        setContent(menuState(shown = listOf(row("Shown")), hidden = listOf(row(id = "hidden", "Hidden 1", "Hidden 2"))))

        performAction("Hidden 1, Hidden 2", unhide)

        announcementNode().assert(hasAnnouncement(R.string.mozac_menu_customize_section_shown, "Hidden 1, Hidden 2"))
    }

    @Test
    fun `GIVEN a named section WHEN it is moved THEN it is announced by its name`() {
        val named = grid(id = "grid", "Back", "Forward").copy(name = Text.String("Controls"))
        setContent(menuState(shown = listOf(named, row("Second"))))

        performAction("Back, Forward", moveDown, name = "Controls")

        announcementNode().assert(hasAnnouncement(R.string.mozac_menu_customize_section_moved, "Controls", 2, 2))
    }

    @Test
    fun `GIVEN more sections than fit on the screen WHEN a section is moved up or down THEN it stays the same node where it was on the screen`() {
        setContent(menuState(shown = (1..10).map { row("Shown $it") }))
        val before = composeTestRule.onNode(section("Shown 3")).fetchSemanticsNode()
        val topBefore = before.boundsInRoot.top

        performAction("Shown 3", moveDown)
        performAction("Shown 3", moveDown)
        performAction("Shown 3", moveUp)

        val after = composeTestRule.onNode(section("Shown 3")).fetchSemanticsNode()
        assertEquals(before.id, after.id)
        assertEquals(topBefore, after.boundsInRoot.top)
        composeTestRule.onNodeWithContentDescription(shownSection("Shown 3", 4, 10)).assertExists()
    }

    @Test
    fun `GIVEN more sections than fit on the screen WHEN a section at the top is hidden THEN it stays the same node and is shown`() {
        setContent(menuState(shown = (1..10).map { row("Shown $it") }, hidden = listOf(row("Hidden"))))
        val before = composeTestRule.onNode(section("Shown 2")).fetchSemanticsNode()

        performAction("Shown 2", hide)

        composeTestRule.onNode(section("Shown 2")).assertIsDisplayed()
        assertEquals(before.id, composeTestRule.onNode(section("Shown 2")).fetchSemanticsNode().id)
    }

    @Test
    fun `GIVEN more sections than fit on the screen WHEN a hidden section at the bottom is added to the menu THEN it stays the same node and is shown`() {
        setContent(menuState(shown = (1..10).map { row("Shown $it") }, hidden = (1..10).map { row("Hidden $it") }))
        composeTestRule.onNode(hasScrollAction()).performScrollToNode(section("Hidden 10"))
        val before = composeTestRule.onNode(section("Hidden 10")).fetchSemanticsNode()

        performAction("Hidden 10", unhide)

        composeTestRule.onNode(section("Hidden 10")).assertIsDisplayed()
        assertEquals(before.id, composeTestRule.onNode(section("Hidden 10")).fetchSemanticsNode().id)
    }

    @Test
    fun `THEN the announcements are drawn over the list, for accessibility services to be told about them`() {
        setContent(menuState(shown = listOf(row("First"), row("Second"))))

        val announcement = announcementNode().fetchSemanticsNode()
        val list = composeTestRule.onNode(hasScrollAction()).fetchSemanticsNode()
        val siblings = checkNotNull(announcement.parent).children.map { it.id }
        assertTrue(siblings.indexOf(announcement.id) > siblings.indexOf(list.id))
    }

    @Test
    fun `WHEN a move is announced THEN the announcement is gone shortly after`() {
        setContent(menuState(shown = listOf(row("First"), row("Second"))))

        performAction("First", moveDown)
        composeTestRule.mainClock.advanceTimeBy(A11Y_ANNOUNCEMENT_DURATION.inWholeMilliseconds + 100)
        composeTestRule.waitForIdle()

        announcementNode().assert(SemanticsMatcher.keyNotDefined(SemanticsProperties.ContentDescription))
    }

    @Test
    fun `WHEN the same move result is announced again THEN it is announced anew`() {
        val store = setContent(menuState(shown = listOf(row("First"), row("Second"))))
        val announced = hasAnnouncement(R.string.mozac_menu_customize_section_hidden, "First")

        performAction("First", hide)
        announcementNode().assert(announced)
        composeTestRule.mainClock.advanceTimeBy(A11Y_ANNOUNCEMENT_DURATION.inWholeMilliseconds + 100)
        composeTestRule.runOnIdle {
            store.dispatch(Update(listOf(row("First"), row("Second")), emptySet(), emptySet(), isCustomized = false))
        }
        composeTestRule.waitForIdle()
        announcementNode().assert(SemanticsMatcher.keyNotDefined(SemanticsProperties.ContentDescription))
        performAction("First", hide)

        announcementNode().assert(announced)
    }

    @Test
    fun `GIVEN a section partly below the screen WHEN it is moved down THEN it is fully shown`() {
        setContent(menuState(shown = (1..10).map { row("Shown $it") }))
        val screenBottom = composeTestRule.onRoot().getUnclippedBoundsInRoot().bottom
        val partlyShown =
            (1..10)
                .map { "Shown $it" }
                .filter { composeTestRule.onAllNodes(section(it)).fetchSemanticsNodes().isNotEmpty() }
                .first {
                    val bounds = composeTestRule.onNode(section(it)).getUnclippedBoundsInRoot()
                    bounds.top < screenBottom && bounds.bottom > screenBottom
                }

        performAction(partlyShown, moveDown)

        composeTestRule.onNode(section(partlyShown)).assertIsDisplayed()
        val bottom = composeTestRule.onNode(section(partlyShown)).getUnclippedBoundsInRoot().bottom
        assertTrue(bottom <= screenBottom)
    }

    private fun announcementNode() =
        composeTestRule.onNode(SemanticsMatcher.keyIsDefined(SemanticsProperties.LiveRegion))

    private fun hasAnnouncement(@StringRes stringRes: Int, vararg args: Any) =
        SemanticsMatcher.expectValue(
            SemanticsProperties.ContentDescription,
            listOf(testContext.getString(stringRes, *args)),
        )

    private fun actionsOf(itemNames: String) =
        composeTestRule
            .onNode(section(itemNames))
            .fetchSemanticsNode()
            .config
            .getOrElse(SemanticsActions.CustomActions) { emptyList() }
            .map { it.label }

    private fun performAction(itemNames: String, actionLabel: String, name: String? = null) {
        val action =
            composeTestRule
                .onNode(section(itemNames, name))
                .fetchSemanticsNode()
                .config[SemanticsActions.CustomActions]
                .single { it.label == actionLabel }
        composeTestRule.runOnIdle { action.action() }
        composeTestRule.waitForIdle()
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

    /** The node of the section with [itemNames] and [name], if any, read with its position if it is shown. */
    private fun section(itemNames: String, name: String? = null) =
        SemanticsMatcher("Section with $itemNames") { node ->
            val label = label(itemNames, name)
            val description =
                node.config.getOrElse(SemanticsProperties.ContentDescription) { emptyList() }.singleOrNull()
            description == label || description?.startsWith("$label. ") == true
        }

    private fun hasRole(role: Role) = SemanticsMatcher.expectValue(SemanticsProperties.Role, role)

    private fun setContent(
        state: CustomizableMenuState,
        middleware: List<Middleware<CustomizableMenuState, CustomizableMenuAction>> = emptyList(),
    ): CustomizableMenuStore {
        val store = CustomizableMenuStore(initialState = state, middleware = middleware)
        composeTestRule.setContent {
            AcornTheme {
                CustomizeMenuContent(store = store)
            }
        }
        return store
    }

    private fun menuState(
        shown: List<MenuItemsGroup>,
        hidden: List<MenuItemsGroup> = emptyList(),
        mandatoryIds: Set<String> = emptySet(),
        isCustomized: Boolean = false,
        editableIds: Set<String> = emptySet(),
    ) =
        CustomizableMenuState(
            menuGroups = shown + hidden,
            mandatoryGroupIds = mandatoryIds,
            hiddenGroupIds = hidden.map { it.id }.toSet(),
            isCustomized = isCustomized,
            editableGroupIds = editableIds,
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
