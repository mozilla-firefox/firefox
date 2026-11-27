/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.hapticfeedback.HapticFeedback
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.semantics.SemanticsActions
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assertCountEquals
import androidx.compose.ui.test.assertHeightIsEqualTo
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.getUnclippedBoundsInRoot
import androidx.compose.ui.test.hasAnyAncestor
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasScrollAction
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.isRoot
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onAllNodesWithTag
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollToNode
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.unit.dp
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.compose.base.R as baseR
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.base.theme.layout.AcornLayout
import mozilla.components.compose.base.theme.layout.AcornSpace
import mozilla.components.compose.menu.R
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.CustomizableMenuAction
import mozilla.components.compose.menu.store.CustomizableMenuAction.AttemptedRemovalOfMandatorySection
import mozilla.components.compose.menu.store.CustomizableMenuAction.MenuStructureChanged
import mozilla.components.compose.menu.store.CustomizableMenuState
import mozilla.components.compose.menu.store.CustomizableMenuStore
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.lib.state.Middleware
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.ui.icons.R as iconsR
import org.junit.Assert.assertEquals
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
    private val moveUp = testContext.getString(R.string.mozac_menu_customize_move_up)
    private val moveDown = testContext.getString(R.string.mozac_menu_customize_move_down)
    private val hide = testContext.getString(R.string.mozac_menu_customize_hide)
    private val unhide = testContext.getString(R.string.mozac_menu_customize_unhide)
    private val close = testContext.getString(baseR.string.mozac_compose_base_close_button_content_description)
    private val hapticFeedback = RecordingHapticFeedback()

    @Test
    fun `GIVEN shown, mandatory and hidden sections THEN the shown and mandatory ones are shown in their order with the hidden ones below the hidden sections title`() {
        setContent(
            menuState(
                shown = listOf(row("Shown 1"), row("Mandatory"), row("Shown 2")),
                hidden = listOf(row("Hidden")),
                mandatoryIds = setOf("Mandatory"),
            )
        )

        val shown1Bounds = sectionText("Shown 1").getUnclippedBoundsInRoot()
        val mandatoryBounds = sectionText("Mandatory").getUnclippedBoundsInRoot()
        val shown2Bounds = sectionText("Shown 2").getUnclippedBoundsInRoot()
        val hiddenTitleBounds = composeTestRule.onNodeWithText(hiddenSectionsTitle).getUnclippedBoundsInRoot()
        val hiddenBounds = sectionText("Hidden").getUnclippedBoundsInRoot()
        assertTrue(shown1Bounds.bottom <= mandatoryBounds.top)
        assertTrue(mandatoryBounds.bottom <= shown2Bounds.top)
        assertTrue(shown2Bounds.bottom <= hiddenTitleBounds.top)
        assertTrue(hiddenTitleBounds.bottom <= hiddenBounds.top)
    }

    @Test
    fun `GIVEN no hidden sections THEN the hide section area is shown`() {
        setContent(menuState(shown = listOf(row("Shown"))))

        composeTestRule.onNodeWithText(dragToHide).assertIsDisplayed()
    }

    @Test
    fun `GIVEN hidden sections THEN the hide section area is not shown`() {
        setContent(menuState(shown = listOf(row("Shown")), hidden = listOf(row("Hidden"))))

        composeTestRule.onNodeWithText(dragToHide).assertDoesNotExist()
    }

    @Test
    fun `GIVEN the menu is customized AND hidden sections THEN the button to reset it is shown below the hidden sections`() {
        setContent(menuState(shown = listOf(row("Shown")), hidden = listOf(row("Hidden")), isCustomized = true))

        val hiddenBounds = sectionText("Hidden").getUnclippedBoundsInRoot()
        val resetBounds = composeTestRule.onNodeWithText(resetToDefault).getUnclippedBoundsInRoot()
        assertTrue(hiddenBounds.bottom <= resetBounds.top)
    }

    @Test
    fun `GIVEN the menu is customized AND no hidden sections THEN the button to reset it is shown below the hide section area`() {
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

        composeTestRule
            .onNode(hasScrollAction())
            .performScrollToNode(
                hasContentDescription(testContext.getString(R.string.mozac_menu_customize_unnamed_section, "Hidden 9"))
            )

        sectionText("Hidden 9").assertIsDisplayed()
    }

    @Test
    fun `WHEN a shown section is dragged above another shown section THEN it is shown before it`() {
        val store = setContent(menuState(shown = listOf(row("Shown 1"), row("Shown 2"))))

        longPressAndDrag(title = "Shown 2", to = { boundsOf("Shown 1").topCenter })

        assertEquals(listOf("Shown 2", "Shown 1"), shownIds(store))
    }

    @Test
    fun `WHEN a shown section is dragged below another shown section THEN it is shown after it`() {
        val store = setContent(menuState(shown = listOf(row("Shown 1"), row("Shown 2"), row("Shown 3"))))

        longPressAndDrag(title = "Shown 1", to = { boundsOf("Shown 3").bottomCenter })

        assertEquals(listOf("Shown 2", "Shown 3", "Shown 1"), shownIds(store))
    }

    @Test
    fun `WHEN a shown section is dragged below a mandatory section THEN it is shown after it`() {
        val store =
            setContent(
                menuState(
                    shown = listOf(row("Shown 1"), row("Mandatory"), row("Shown 2")),
                    mandatoryIds = setOf("Mandatory"),
                )
            )

        longPressAndDrag(title = "Shown 1", to = { boundsOf("Mandatory").bottomCenter })

        assertEquals(listOf("Mandatory", "Shown 1", "Shown 2"), shownIds(store))
    }

    @Test
    fun `GIVEN a mandatory section shown first WHEN a shown section is dragged above it THEN it is shown before it`() {
        val store =
            setContent(menuState(shown = listOf(row("Mandatory"), row("Shown")), mandatoryIds = setOf("Mandatory")))

        longPressAndDrag(title = "Shown", to = { boundsOf("Mandatory").topCenter })

        assertEquals(listOf("Shown", "Mandatory"), shownIds(store))
    }

    @Test
    fun `WHEN a mandatory section is dragged above a shown section THEN it is shown before it`() {
        val store =
            setContent(menuState(shown = listOf(row("Shown"), row("Mandatory")), mandatoryIds = setOf("Mandatory")))

        longPressAndDrag(title = "Mandatory", to = { boundsOf("Shown").topCenter })

        assertEquals(listOf("Mandatory", "Shown"), shownIds(store))
    }

    @Test
    fun `GIVEN no hidden sections WHEN a visible section is dragged into hide section area THEN it is hidden`() {
        val store =
            setContent(
                menuState(
                    shown = listOf(row("Shown 1"), row("Shown 2"), row("Mandatory")),
                    mandatoryIds = setOf("Mandatory"),
                )
            )

        longPressAndDrag(title = "Shown 1", to = { boundsOf(dragToHide).center })

        assertEquals(listOf("Shown 2", "Mandatory"), shownIds(store))
        assertEquals(listOf("Shown 1"), hiddenIds(store))
    }

    @Test
    fun `GIVEN hidden sections WHEN a shown section is dragged over them THEN it is hidden next to them`() {
        val store =
            setContent(menuState(shown = listOf(row("Shown 1"), row("Shown 2")), hidden = listOf(row("Hidden"))))

        longPressAndDrag(title = "Shown 1", to = { boundsOf("Hidden").bottomCenter })

        assertEquals(listOf("Shown 2"), shownIds(store))
        assertEquals(listOf("Hidden", "Shown 1"), hiddenIds(store))
    }

    @Test
    fun `GIVEN no hidden sections WHEN a shown section is dragged past the hide section area THEN it is put back where it was`() {
        val state = menuState(shown = listOf(row("Shown 1"), row("Shown 2")), isCustomized = true)
        val store = setContent(state)

        longPressAndDrag(title = "Shown 1", to = { boundsOf(resetToDefault).center })

        assertEquals(state, store.state)
    }

    @Test
    fun `GIVEN hidden sections and no reset button WHEN a shown section is dragged below the last hidden section THEN it is put back where it was`() {
        val state = menuState(shown = listOf(row("Shown 1"), row("Shown 2")), hidden = listOf(row("Hidden")))
        val store = setContent(state)

        longPressAndDrag(
            title = "Shown 1",
            to = { boundsOf("Hidden").let { Offset(it.center.x, it.bottom + it.height) } },
        )

        assertEquals(state, store.state)
    }

    @Test
    fun `WHEN a hidden section is dragged below a shown section THEN it is shown after it`() {
        val store =
            setContent(
                menuState(
                    shown = listOf(row("Shown"), row("Mandatory")),
                    hidden = listOf(row("Hidden")),
                    mandatoryIds = setOf("Mandatory"),
                )
            )

        longPressAndDrag(title = "Hidden", to = { boundsOf("Shown").bottomCenter })

        assertEquals(listOf("Shown", "Hidden", "Mandatory"), shownIds(store))
        assertEquals(emptyList<String>(), hiddenIds(store))
    }

    @Test
    fun `GIVEN one hidden section WHEN it is dragged THEN the hide section area is shown`() {
        val state = menuState(shown = listOf(row("Shown")), hidden = listOf(row("Hidden")))
        val store = setContent(state)

        longPressAndMove(title = "Hidden", to = { boundsOf("Hidden").center })

        composeTestRule.onNodeWithText(dragToHide).assertIsDisplayed()

        release()

        assertEquals(state, store.state)
    }

    @Test
    fun `GIVEN no hidden sections WHEN a mandatory section is dragged into the hide section area THEN it is put back where it was`() {
        val state = menuState(shown = listOf(row("Shown"), row("Mandatory")), mandatoryIds = setOf("Mandatory"))
        val store = setContent(state)

        longPressAndDrag(title = "Mandatory", to = { boundsOf(dragToHide).center })

        assertEquals(state, store.state)
    }

    @Test
    fun `WHEN a mandatory section is dragged over the hidden sections THEN the app is told the user tried to hide it`() {
        val actions = mutableListOf<CustomizableMenuAction>()
        setContent(
            menuState(shown = listOf(row("Shown"), row("Mandatory")), mandatoryIds = setOf("Mandatory")),
            middleware =
                listOf { _, next, action ->
                    actions += action
                    next(action)
                },
        )

        longPressAndMove(title = "Mandatory", to = { boundsOf(dragToHide).center })

        assertEquals(
            listOf(AttemptedRemovalOfMandatorySection("Mandatory")),
            actions.filterIsInstance<AttemptedRemovalOfMandatorySection>(),
        )

        release()
    }

    @Test
    fun `WHEN a shown section is dragged over the hidden sections THEN the app is not told the user tried to hide a mandatory section`() {
        val actions = mutableListOf<CustomizableMenuAction>()
        setContent(
            menuState(shown = listOf(row("Shown"), row("Mandatory")), mandatoryIds = setOf("Mandatory")),
            middleware =
                listOf { _, next, action ->
                    actions += action
                    next(action)
                },
        )

        longPressAndDrag(title = "Shown", to = { boundsOf(dragToHide).center })

        assertEquals(
            emptyList<CustomizableMenuAction>(),
            actions.filterIsInstance<AttemptedRemovalOfMandatorySection>(),
        )
    }

    @Test
    fun `WHEN a mandatory section is dragged over the hidden sections again in the same drag THEN the app is told only once`() {
        val actions = mutableListOf<CustomizableMenuAction>()
        setContent(
            menuState(shown = listOf(row("Shown"), row("Mandatory")), mandatoryIds = setOf("Mandatory")),
            middleware =
                listOf { _, next, action ->
                    actions += action
                    next(action)
                },
        )

        longPressAndMove(title = "Mandatory", to = { boundsOf(dragToHide).center })
        moveTo { boundsOf("Shown").center }
        moveTo { boundsOf(dragToHide).center }

        assertEquals(1, actions.count { it is AttemptedRemovalOfMandatorySection })

        release()
    }

    @Test
    fun `WHEN a mandatory section is dragged over the hidden sections in two drags THEN the app is told for each of them`() {
        val actions = mutableListOf<CustomizableMenuAction>()
        setContent(
            menuState(shown = listOf(row("Shown"), row("Mandatory")), mandatoryIds = setOf("Mandatory")),
            middleware =
                listOf { _, next, action ->
                    actions += action
                    next(action)
                },
        )

        longPressAndDrag(title = "Mandatory", to = { boundsOf(dragToHide).center })
        longPressAndDrag(title = "Mandatory", to = { boundsOf(dragToHide).center })

        assertEquals(2, actions.count { it is AttemptedRemovalOfMandatorySection })
    }

    @Test
    fun `GIVEN hidden sections WHEN a mandatory section is dragged over them THEN it is put back where it was`() {
        val state =
            menuState(
                shown = listOf(row("Mandatory"), row("Shown")),
                hidden = listOf(row("Hidden")),
                mandatoryIds = setOf("Mandatory"),
            )
        val store = setContent(state)

        longPressAndDrag(title = "Mandatory", to = { boundsOf("Hidden").center })

        assertEquals(state, store.state)
    }

    @Test
    fun `WHEN the gesture dragging a shown section above another shown section is cancelled THEN it is put back where it was`() {
        val state = menuState(shown = listOf(row("Shown 1"), row("Shown 2")))
        val store = setContent(state)
        val boundsBeforeDrag = boundsOf("Shown 2")

        longPressAndMove(title = "Shown 2", to = { boundsOf("Shown 1").topCenter })
        cancelGesture()

        assertEquals(state, store.state)
        assertEquals(boundsBeforeDrag, boundsOf("Shown 2"))
        composeTestRule.onNode(hasScrollAction()).assertExists()
    }

    @Test
    fun `GIVEN more sections than fit on the screen WHEN a shown section is dragged past the end of the list THEN the list is scrolled`() {
        setContent(menuState(shown = List(10) { row("Shown $it") }, hidden = List(10) { row("Hidden $it") }))
        assertEquals(0f, scrolledByValue())

        longPressAndMove(title = "Shown 0", to = { rootBounds().bottomCenter - Offset(0f, 1f) })

        assertTrue(scrolledByValue() > 0f)
        release()
    }

    @Test
    fun `WHEN a section is dragged to where it would be dropped somewhere else THEN the user feels it`() {
        setContent(menuState(shown = listOf(row("Shown 1"), row("Shown 2"))))
        longPressAndMove(title = "Shown 2", to = { boundsOf("Shown 2").center })
        val feltBefore = hapticFeedback.performed.size

        moveTo { boundsOf("Shown 1").topCenter }

        assertTrue(hapticFeedback.performed.size > feltBefore)
        release()
    }

    @Test
    fun `WHEN a section is dragged back to where it was THEN the user feels it`() {
        setContent(menuState(shown = listOf(row("Shown 1"), row("Shown 2"))))
        longPressAndMove(title = "Shown 2", to = { boundsOf("Shown 2").center })
        moveTo { boundsOf("Shown 1").topCenter }
        val feltBefore = hapticFeedback.performed.size

        moveTo { boundsOf("Shown 1").bottomCenter }

        assertTrue(hapticFeedback.performed.size > feltBefore)
        release()
    }

    @Test
    fun `GIVEN no hidden sections WHEN a section is dragged into the hide section area THEN the user feels it`() {
        setContent(menuState(shown = listOf(row("Shown 1"), row("Shown 2"))))
        longPressAndMove(title = "Shown 2", to = { boundsOf("Shown 2").center })
        val feltBefore = hapticFeedback.performed.size

        moveTo { boundsOf(dragToHide).center }

        assertTrue(hapticFeedback.performed.size > feltBefore)
        release()
    }

    @Test
    fun `GIVEN a section taking much space WHEN it is long pressed without moving THEN the hide section area stays where it is AND the user only feels the long press`() {
        setContent(menuState(shown = listOf(row("Shown"), row(id = "Tall", "Tall 1", "Tall 2", "Tall 3"))))
        val dropAreaBefore = boundsOf(dragToHide)

        longPressAndMove(title = "Tall 1", to = { boundsOf("Tall 1").center })

        assertEquals(dropAreaBefore, boundsOf(dragToHide))
        assertEquals(1, hapticFeedback.performed.size)
        release()
    }

    @Test
    fun `WHEN a mandatory section is dragged over the hidden sections THEN the user does not feel it`() {
        setContent(menuState(shown = listOf(row("Shown"), row("Mandatory")), mandatoryIds = setOf("Mandatory")))
        // On the way there the line showing where it is put back is felt, as for any other section.
        longPressAndMove(title = "Mandatory", to = { boundsOf(dragToHide).center })
        val feltBefore = hapticFeedback.performed.size

        // Over the hidden sections it cannot be dropped, so nothing shows where it is.
        moveTo { boundsOf(dragToHide).bottomCenter }

        assertEquals(feltBefore, hapticFeedback.performed.size)
        release()
    }

    @Test
    fun `WHEN a section is dragged THEN the sections below it take its space`() {
        setContent(menuState(shown = listOf(row("Shown 1"), row("Shown 2"))))
        val shown1 = boundsOf("Shown 1")

        longPressAndMove(title = "Shown 1", to = { shown1.center })

        assertTrue(boundsOf("Shown 2").top < shown1.bottom)

        release()
    }

    @Test
    fun `WHEN a section is dragged below another section THEN the line showing where it is dropped takes space after that section`() {
        setContent(menuState(shown = listOf(row("Shown 1"), row("Shown 2"), row("Shown 3"))))
        val oldGap = gapBetween("Shown 2", "Shown 3")

        longPressAndMove(title = "Shown 1", to = { boundsOf("Shown 2").bottomCenter })

        assertEquals(oldGap + dropLineSpace(), gapBetween("Shown 2", "Shown 3"), 0.5f)
        release()
    }

    @Test
    fun `WHEN a section is dragged above the first section THEN the line showing where it is dropped takes space before it`() {
        setContent(menuState(shown = listOf(row("Shown 1"), row("Shown 2"), row("Shown 3"))))
        val oldTop = boundsOf("Shown 1").top

        longPressAndMove(title = "Shown 3", to = { Offset(boundsOf("Shown 1").center.x, oldTop) })

        assertEquals(oldTop + dropLineSpace(), boundsOf("Shown 1").top, 0.5f)
        release()
    }

    @Test
    fun `WHEN a section is dragged back to where it was THEN the line showing it is dropped there takes space where the section was`() {
        setContent(menuState(shown = listOf(row("Shown 1"), row("Shown 2"), row("Shown 3"))))
        val oldGap = gapBetween("Shown 1", "Shown 2")
        val shown2 = boundsOf("Shown 2")

        longPressAndMove(title = "Shown 2", to = { shown2.topCenter })

        assertEquals(oldGap + dropLineSpace(), gapBetween("Shown 1", "Shown 3"), 0.5f)
        release()
    }

    @Test
    fun `WHEN a section dragged back to where it was is let go THEN the line showing where it is dropped takes no more space`() {
        setContent(menuState(shown = listOf(row("Shown 1"), row("Shown 2"), row("Shown 3"))))
        val oldGap = gapBetween("Shown 1", "Shown 2")
        val shown2 = boundsOf("Shown 2")

        longPressAndDrag(title = "Shown 2", to = { shown2.topCenter })

        assertEquals(oldGap, gapBetween("Shown 1", "Shown 2"), 0.5f)
    }

    @Test
    fun `WHEN the gesture dragging a section is cancelled THEN the line showing where it is dropped takes no more space`() {
        setContent(menuState(shown = listOf(row("Shown 1"), row("Shown 2"), row("Shown 3"))))
        val oldGap = gapBetween("Shown 2", "Shown 3")

        longPressAndMove(title = "Shown 1", to = { boundsOf("Shown 2").bottomCenter })
        cancelGesture()

        assertEquals(oldGap, gapBetween("Shown 2", "Shown 3"), 0.5f)
    }

    @Test
    fun `WHEN a hidden section is dragged onto the bottom of the hidden sections title THEN the line showing where it is dropped takes space above the title`() {
        setContent(menuState(shown = listOf(row("Shown 1"), row("Shown 2")), hidden = listOf(row("Hidden"))))
        val oldGap = gapBetween("Shown 2", hiddenSectionsTitle)

        longPressAndMove(title = "Hidden", to = { boundsOf(hiddenSectionsTitle).bottomCenter })

        assertEquals(oldGap + dropLineSpace(), gapBetween("Shown 2", hiddenSectionsTitle), 0.5f)
        release()
    }

    @Test
    fun `GIVEN hidden sections WHEN a shown section is dragged over them THEN the line showing where it is dropped takes space below the hidden sections title`() {
        setContent(menuState(shown = listOf(row("Shown 1"), row("Shown 2")), hidden = listOf(row("Hidden"))))
        val oldGap = gapBetween(hiddenSectionsTitle, "Hidden")

        longPressAndMove(title = "Shown 1", to = { boundsOf("Hidden").topCenter })

        assertEquals(oldGap + dropLineSpace(), gapBetween(hiddenSectionsTitle, "Hidden"), 0.5f)
        release()
    }

    @Test
    fun `GIVEN hidden sections WHEN a shown section is dragged among the shown sections THEN the shown sections below it take its space AND the hidden sections stay where they are`() {
        setContent(
            menuState(
                shown = listOf(row("Shown 1"), row("Shown 2")),
                hidden = listOf(row("Hidden")),
                isCustomized = true,
            )
        )
        val before = hiddenAreaBounds(hiddenSectionsTitle, "Hidden", resetToDefault)
        val shown2Before = boundsOf("Shown 2")

        longPressAndMove(title = "Shown 1", to = { boundsOf("Shown 2").bottomCenter })

        assertTrue(boundsOf("Shown 2").top < shown2Before.top)
        assertEquals(before, hiddenAreaBounds(hiddenSectionsTitle, "Hidden", resetToDefault))

        release()

        assertEquals(before, hiddenAreaBounds(hiddenSectionsTitle, "Hidden", resetToDefault))
    }

    @Test
    fun `GIVEN hidden sections WHEN a shown section is picked up, dragged among the shown sections and let go THEN the hidden sections title does not move at any frame`() {
        setContent(menuState(shown = listOf(row("Shown 1"), row("Shown 2")), hidden = listOf(row("Hidden"))))
        val titleBefore = boundsOf(hiddenSectionsTitle)
        val shown2Before = boundsOf("Shown 2")
        composeTestRule.mainClock.autoAdvance = false

        composeTestRule.onRoot().performTouchInput { down(boundsOf("Shown 1").center) }
        assertTitleStaysFor(frames = 60, at = titleBefore)
        val step = (shown2Before.bottomCenter - boundsOf("Shown 1").center) / 10f
        repeat(10) {
            composeTestRule.onRoot().performTouchInput { moveBy(step) }
            assertTitleStaysFor(frames = 3, at = titleBefore)
        }
        assertTrue(boundsOf("Shown 2").top < shown2Before.top)
        composeTestRule.onRoot().performTouchInput { up() }
        assertTitleStaysFor(frames = 60, at = titleBefore)
    }

    @Test
    fun `GIVEN hidden sections WHEN the gesture dragging a shown section is cancelled THEN the hidden sections title does not move at any frame`() {
        setContent(menuState(shown = listOf(row("Shown 1"), row("Shown 2")), hidden = listOf(row("Hidden"))))
        val titleBefore = boundsOf(hiddenSectionsTitle)
        longPressAndMove(title = "Shown 1", to = { boundsOf("Shown 2").bottomCenter })
        composeTestRule.mainClock.autoAdvance = false

        composeTestRule.onRoot().performTouchInput { cancel() }

        assertTitleStaysFor(frames = 60, at = titleBefore)
    }

    @Test
    fun `GIVEN hidden sections WHEN a shown section is dragged back to where it was THEN the hidden sections stay where they are`() {
        setContent(menuState(shown = listOf(row("Shown 1"), row("Shown 2")), hidden = listOf(row("Hidden"))))
        val before = hiddenAreaBounds()
        val shown2 = boundsOf("Shown 2")

        longPressAndMove(title = "Shown 2", to = { shown2.topCenter })

        assertEquals(before, hiddenAreaBounds())
        release()
    }

    @Test
    fun `GIVEN hidden sections WHEN a shown section is dragged onto the bottom of the hidden sections title THEN the hidden sections stay where they are`() {
        setContent(menuState(shown = listOf(row("Shown 1"), row("Shown 2")), hidden = listOf(row("Hidden"))))
        val before = hiddenAreaBounds()

        longPressAndMove(title = "Shown 1", to = { boundsOf(hiddenSectionsTitle).bottomCenter })

        assertEquals(before, hiddenAreaBounds())
        release()
    }

    @Test
    fun `GIVEN hidden sections WHEN a shown section is dragged over them THEN the hidden sections title stays where it is`() {
        setContent(menuState(shown = listOf(row("Shown 1"), row("Shown 2")), hidden = listOf(row("Hidden"))))
        val titleBefore = boundsOf(hiddenSectionsTitle)

        longPressAndMove(title = "Shown 1", to = { boundsOf("Hidden").topCenter })

        assertEquals(titleBefore, boundsOf(hiddenSectionsTitle))
        release()
    }

    @Test
    fun `GIVEN no hidden sections WHEN a shown section is dragged among the shown sections THEN the hide section area and the reset button stay where they are`() {
        setContent(menuState(shown = listOf(row("Shown 1"), row("Shown 2")), isCustomized = true))
        val dropAreaBefore = boundsOf(dragToHide)
        val resetBefore = boundsOf(resetToDefault)

        longPressAndMove(title = "Shown 1", to = { boundsOf("Shown 2").bottomCenter })

        assertEquals(dropAreaBefore, boundsOf(dragToHide))
        assertEquals(resetBefore, boundsOf(resetToDefault))
        release()
    }

    @Test
    fun `GIVEN hidden sections WHEN a hidden section is dragged THEN the hidden sections below it take its space`() {
        setContent(menuState(shown = listOf(row("Shown")), hidden = listOf(row("Hidden 1"), row("Hidden 2"))))
        val hidden1 = boundsOf("Hidden 1")

        longPressAndMove(title = "Hidden 1", to = { hidden1.center })

        assertTrue(boundsOf("Hidden 2").top < hidden1.bottom)
        release()
    }

    @Test
    fun `WHEN a shown section is dragged by its handle above another shown section THEN it is shown before it`() {
        val store = setContent(menuState(shown = listOf(row("Shown 1"), row("Shown 2"))))

        dragByHandle(title = "Shown 2", to = { boundsOf("Shown 1").topCenter })

        assertEquals(listOf("Shown 2", "Shown 1"), shownIds(store))
    }

    @Test
    fun `GIVEN no hidden sections WHEN a shown section is dragged by its handle into the hide section area THEN it is hidden`() {
        val store = setContent(menuState(shown = listOf(row("Shown 1"), row("Shown 2"))))

        dragByHandle(title = "Shown 1", to = { boundsOf(dragToHide).center })

        assertEquals(listOf("Shown 2"), shownIds(store))
        assertEquals(listOf("Shown 1"), hiddenIds(store))
    }

    @Test
    fun `WHEN a hidden section is dragged by its handle below a shown section THEN it is shown after it`() {
        val store =
            setContent(
                menuState(
                    shown = listOf(row("Shown"), row("Mandatory")),
                    hidden = listOf(row("Hidden")),
                    mandatoryIds = setOf("Mandatory"),
                )
            )

        dragByHandle(title = "Hidden", to = { boundsOf("Shown").bottomCenter })

        assertEquals(listOf("Shown", "Hidden", "Mandatory"), shownIds(store))
        assertEquals(emptyList<String>(), hiddenIds(store))
    }

    @Test
    fun `WHEN the handle of a section is held for longer than a long press and then moved THEN its actions are shown AND the section is not dragged`() {
        val state = menuState(shown = List(10) { row("Shown $it") }, hidden = List(10) { row("Hidden $it") })
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
    fun `WHEN the drag handle is moved sideways and held for longer than a long press THEN no sheet is shown AND moving it vertically drags the section`() {
        val store = setContent(menuState(shown = listOf(row("Shown 1"), row("Shown 2"), row("Shown 3"))))

        pressHandle("Shown 1")
        composeTestRule.onRoot().performTouchInput {
            moveBy(Offset(-viewConfiguration.touchSlop * 2, 0f))
            advanceEventTime(viewConfiguration.longPressTimeoutMillis * 2)
            move()
        }
        composeTestRule.waitForIdle()
        composeTestRule.onNodeWithText(moveUp).assertDoesNotExist()
        moveTo { boundsOf("Shown 3").bottomCenter }
        release()

        assertEquals(listOf("Shown 2", "Shown 3", "Shown 1"), shownIds(store))
    }

    @Test
    fun `WHEN the drag handle of a shown section is long pressed THEN its move up, move down and hide actions are shown`() {
        setContent(menuState(shown = listOf(row("Shown 1"), row("Shown 2"), row("Shown 3"))))

        longPressHandle("Shown 2")

        composeTestRule.onNodeWithText(moveUp).assertIsEnabled()
        composeTestRule.onNodeWithText(moveDown).assertIsEnabled()
        composeTestRule.onNodeWithText(hide).assertIsEnabled()
        composeTestRule.onNodeWithText(unhide).assertDoesNotExist()
    }

    @Test
    fun `GIVEN the first shown section WHEN its drag handle is long pressed THEN move up is disabled`() {
        setContent(menuState(shown = listOf(row("Shown 1"), row("Shown 2"))))

        longPressHandle("Shown 1")

        composeTestRule.onNodeWithText(moveUp).assertIsNotEnabled()
        composeTestRule.onNodeWithText(moveDown).assertIsEnabled()
    }

    @Test
    fun `GIVEN the last shown section WHEN its drag handle is long pressed THEN move down is disabled`() {
        setContent(menuState(shown = listOf(row("Shown 1"), row("Shown 2"))))

        longPressHandle("Shown 2")

        composeTestRule.onNodeWithText(moveUp).assertIsEnabled()
        composeTestRule.onNodeWithText(moveDown).assertIsNotEnabled()
    }

    @Test
    fun `GIVEN a mandatory section WHEN its drag handle is long pressed THEN hide is disabled`() {
        setContent(menuState(shown = listOf(row("Shown"), row("Mandatory")), mandatoryIds = setOf("Mandatory")))

        longPressHandle("Mandatory")

        composeTestRule.onNodeWithText(hide).assertIsNotEnabled()
        composeTestRule.onNodeWithText(moveUp).assertIsEnabled()
    }

    @Test
    fun `GIVEN a hidden section WHEN its drag handle is long pressed THEN move up and move down are shown disabled AND unhide is enabled`() {
        setContent(menuState(shown = listOf(row("Shown")), hidden = listOf(row("Hidden 1"), row("Hidden 2"))))

        longPressHandle("Hidden 2")

        composeTestRule.onNodeWithText(moveUp).assertIsNotEnabled()
        composeTestRule.onNodeWithText(moveDown).assertIsNotEnabled()
        composeTestRule.onNodeWithText(unhide).assertIsEnabled()
        composeTestRule.onNodeWithText(hide).assertDoesNotExist()
    }

    @Test
    fun `WHEN move down is chosen in the sheet THEN the section is moved and saved like the accessibility action AND the sheet is closed`() {
        val actions = mutableListOf<CustomizableMenuAction>()
        val store =
            setContent(
                menuState(shown = listOf(row("Shown 1"), row("Shown 2"), row("Shown 3"))),
                middleware =
                    listOf { _, next, action ->
                        actions += action
                        next(action)
                    },
            )
        longPressHandle("Shown 1")

        composeTestRule.onNodeWithText(moveDown).performClick()
        composeTestRule.waitForIdle()

        assertEquals(listOf("Shown 2", "Shown 1", "Shown 3"), shownIds(store))
        assertEquals(
            MenuStructureChanged(
                items = store.state.menuGroups,
                hiddenGroupIds = emptySet(),
                movedGroupId = "Shown 1",
            ),
            actions.filterIsInstance<MenuStructureChanged>().single(),
        )
        composeTestRule.onNodeWithText(moveDown).assertDoesNotExist()
    }

    @Test
    fun `WHEN hide is chosen in the sheet THEN the section is the first hidden one`() {
        val store =
            setContent(menuState(shown = listOf(row("Shown 1"), row("Shown 2")), hidden = listOf(row("Hidden"))))
        longPressHandle("Shown 1")

        composeTestRule.onNodeWithText(hide).performClick()
        composeTestRule.waitForIdle()

        assertEquals(listOf("Shown 2"), shownIds(store))
        assertEquals(listOf("Shown 1", "Hidden"), hiddenIds(store))
    }

    @Test
    fun `WHEN unhide is chosen THEN the section is the last shown one`() {
        val store =
            setContent(menuState(shown = listOf(row("Shown 1"), row("Shown 2")), hidden = listOf(row("Hidden"))))
        longPressHandle("Hidden")

        composeTestRule.onNodeWithText(unhide).performClick()
        composeTestRule.waitForIdle()

        assertEquals(listOf("Shown 1", "Shown 2", "Hidden"), shownIds(store))
        assertEquals(emptyList<String>(), hiddenIds(store))
    }

    @Test
    fun `WHEN the drag handle is tapped THEN no sheet is shown`() {
        setContent(menuState(shown = listOf(row("Shown 1"), row("Shown 2"))))

        pressHandle("Shown 2")
        release()

        composeTestRule.onNodeWithText(moveUp).assertDoesNotExist()
    }

    @Test
    fun `WHEN the drag handle is held for a long press THEN the sheet is shown before it is let go`() {
        setContent(menuState(shown = listOf(row("Shown 1"), row("Shown 2"))))

        pressHandle("Shown 2")
        composeTestRule.onRoot().performTouchInput {
            advanceEventTime(viewConfiguration.longPressTimeoutMillis * 2)
            move()
        }
        composeTestRule.waitForIdle()

        composeTestRule.onNodeWithText(moveUp).assertIsDisplayed()
    }

    @Test
    fun `WHEN the drag handle is held for a long press THEN a long press haptic is felt`() {
        setContent(menuState(shown = listOf(row("Shown 1"), row("Shown 2"))))

        longPressHandle("Shown 2")

        assertEquals(listOf(HapticFeedbackType.LongPress), hapticFeedback.performed)
    }

    @Test
    fun `WHEN the rest of the section is long pressed and let go without moving THEN no sheet is shown`() {
        setContent(menuState(shown = listOf(row("Shown 1"), row("Shown 2"))))

        longPressAndMove(title = "Shown 2", to = { boundsOf("Shown 2").center })
        release()

        composeTestRule.onNodeWithText(moveUp).assertDoesNotExist()
    }

    @Test
    fun `WHEN the sheet is closed with its handle THEN the sections are not changed AND the sheet is gone`() {
        val state = menuState(shown = listOf(row("Shown 1"), row("Shown 2")))
        val store = setContent(state)
        longPressHandle("Shown 2")

        composeTestRule.onNodeWithContentDescription(close).performClick()
        composeTestRule.waitForIdle()

        assertEquals(state, store.state)
        composeTestRule.onNodeWithText(moveUp).assertDoesNotExist()
    }

    @Test
    fun `WHEN another action is chosen while the sheet hides after an action THEN only the first action is applied`() {
        val store = setContent(menuState(shown = listOf(row("Shown 1"), row("Shown 2"), row("Shown 3"))))
        longPressHandle("Shown 1")
        composeTestRule.mainClock.autoAdvance = false

        composeTestRule.onNodeWithText(moveDown).performClick()
        composeTestRule.onNodeWithText(hide).performClick()
        composeTestRule.mainClock.autoAdvance = true
        composeTestRule.waitForIdle()

        assertEquals(listOf("Shown 2", "Shown 1", "Shown 3"), shownIds(store))
        assertEquals(emptyList<String>(), hiddenIds(store))
    }

    @Test
    fun `WHEN a section is moved by its content without a long press THEN the sections are not reordered`() {
        val state = menuState(shown = listOf(row("Shown 1"), row("Shown 2")))
        val store = setContent(state)

        val from = boundsOf("Shown 2").center
        composeTestRule.onRoot().performTouchInput { down(from) }
        moveTo { boundsOf("Shown 1").topCenter }
        release()

        assertEquals(state, store.state)
    }

    @Test
    fun `GIVEN a section dragged by its handle WHEN the handle of another section is touched by a second finger THEN the first section is still dragged`() {
        val store = setContent(menuState(shown = listOf(row("Shown 1"), row("Shown 2"), row("Shown 3"))))
        pickUpByHandle("Shown 1")

        val secondFinger = handleOf("Shown 3")
        composeTestRule.onRoot().performTouchInput {
            down(1, secondFinger)
            moveBy(1, Offset(0f, -viewConfiguration.touchSlop * 3))
            up(1)
        }
        composeTestRule.waitForIdle()
        moveTo { boundsOf("Shown 3").bottomCenter }
        release()

        assertEquals(listOf("Shown 2", "Shown 3", "Shown 1"), shownIds(store))
    }

    @Test
    fun `GIVEN sections whose items can be edited THEN only the shown ones have the button editing them`() {
        setContent(
            menuState(
                shown = listOf(row("Editable"), row("Other")),
                hidden = listOf(row("Hidden editable")),
                editableIds = setOf("Editable", "Hidden editable"),
            )
        )

        composeTestRule.onAllNodesWithTag(EDIT_GROUP_ITEMS_BUTTON_TAG, useUnmergedTree = true).assertCountEquals(1)
        composeTestRule
            .onAllNodes(hasTestTag(EDIT_GROUP_ITEMS_BUTTON_TAG) and sectionAncestor("Editable"), useUnmergedTree = true)
            .assertCountEquals(1)
        composeTestRule
            .onAllNodes(hasTestTag(EDIT_GROUP_ITEMS_BUTTON_TAG) and sectionAncestor("Other"), useUnmergedTree = true)
            .assertCountEquals(0)
    }

    @Test
    fun `WHEN a section whose items can be edited is dragged THEN it has no button nor action editing them AND has them again once dropped`() {
        val editItems = testContext.getString(R.string.mozac_menu_customize_edit_items)
        setContent(menuState(shown = listOf(row("Editable"), row("Other")), editableIds = setOf("Editable")))
        assertTrue(editItems in customActionLabelsOf("Editable"))

        longPressAndMove(title = "Editable", to = { boundsOf("Other").bottomCenter })

        composeTestRule.onAllNodes(hasContentDescription("Editable", substring = true)).assertCountEquals(1)
        composeTestRule.onAllNodesWithTag(EDIT_GROUP_ITEMS_BUTTON_TAG, useUnmergedTree = true).assertCountEquals(0)
        assertTrue(editItems !in customActionLabelsOf("Editable"))

        release()

        composeTestRule.onAllNodesWithTag(EDIT_GROUP_ITEMS_BUTTON_TAG, useUnmergedTree = true).assertCountEquals(1)
        assertTrue(editItems in customActionLabelsOf("Editable"))
    }

    @Test
    fun `GIVEN no section whose items can be edited THEN no section has the button editing its items`() {
        setContent(menuState(shown = listOf(row("First"), row("Second"))))

        composeTestRule.onAllNodesWithTag(EDIT_GROUP_ITEMS_BUTTON_TAG, useUnmergedTree = true).assertCountEquals(0)
    }

    @Test
    fun `WHEN the user taps the button editing the items of a section THEN tell the store`() {
        val actions = mutableListOf<CustomizableMenuAction>()
        val captureActions: Middleware<CustomizableMenuState, CustomizableMenuAction> = { _, next, action ->
            actions.add(action)
            next(action)
        }
        setContent(menuState(shown = listOf(row("Editable")), editableIds = setOf("Editable")), listOf(captureActions))

        composeTestRule.onNodeWithTag(EDIT_GROUP_ITEMS_BUTTON_TAG, useUnmergedTree = true).performClick()

        assertTrue(CustomizableMenuAction.EditGroupItemsClicked("Editable") in actions)
    }

    @Test
    fun `GIVEN a section of icons without titles THEN its items are 48dp tall`() {
        val iconsSection =
            MenuItemsGroup.Grid(
                id = "icons",
                items =
                    (1..5).map {
                        StandardMenuItem(
                            title = MenuItemTitle(Text.String("Icon $it")),
                            icon = MenuItemIconRes(iconsR.drawable.mozac_ic_settings_24),
                        )
                    },
                showTitles = false,
            )
        setContent(menuState(shown = listOf(iconsSection)))

        composeTestRule.onNodeWithContentDescription("Icon 1", useUnmergedTree = true).assertHeightIsEqualTo(48.dp)
    }

    private fun sectionAncestor(itemNames: String) = hasAnyAncestor(hasContentDescription(itemNames, substring = true))

    private fun customActionLabelsOf(itemNames: String) =
        composeTestRule
            .onAllNodes(hasContentDescription(itemNames, substring = true))
            .fetchSemanticsNodes()
            .flatMap { it.config.getOrElse(SemanticsActions.CustomActions) { emptyList() } }
            .map { it.label }

    private fun setContent(
        state: CustomizableMenuState,
        middleware: List<Middleware<CustomizableMenuState, CustomizableMenuAction>> = emptyList(),
    ): CustomizableMenuStore {
        val store = CustomizableMenuStore(initialState = state, middleware = middleware)
        composeTestRule.setContent {
            AcornTheme {
                CompositionLocalProvider(LocalHapticFeedback provides hapticFeedback) {
                    CustomizeMenuContent(store = store)
                }
            }
        }
        return store
    }

    /** Bounds of the node with [text], which can be the text of a section. */
    private fun boundsOf(text: String) =
        composeTestRule.onNodeWithText(text, useUnmergedTree = true).fetchSemanticsNode().boundsInRoot

    /** The node of the text of a section, merged into the node of the section for accessibility services. */
    private fun sectionText(text: String) = composeTestRule.onNodeWithText(text, useUnmergedTree = true)

    /** Bounds of the nodes with [texts], by default the hidden sections title and the hidden section "Hidden". */
    private fun hiddenAreaBounds(vararg texts: String = arrayOf(hiddenSectionsTitle, "Hidden")) = texts.map {
        boundsOf(it)
    }

    /** Let the clock run for [frames] frames, checking at each that the hidden sections title is [at]. */
    private fun assertTitleStaysFor(frames: Int, at: Rect) {
        repeat(frames) {
            composeTestRule.mainClock.advanceTimeByFrame()
            assertEquals(at, boundsOf(hiddenSectionsTitle))
        }
    }

    private fun gapBetween(above: String, below: String) = boundsOf(below).top - boundsOf(above).bottom

    /** Space the drop line takes: the spacing between sections plus the line's thickness. */
    private fun dropLineSpace() =
        with(composeTestRule.density) {
            (AcornSpace.Small.static150.roundToPx() + AcornLayout.AcornBorder.heaviest.roundToPx()).toFloat()
        }

    private fun rootBounds() = composeTestRule.onRoot().fetchSemanticsNode().boundsInRoot

    /**
     * How far the list is scrolled, found without its scroll actions as these are gone while a section is dragged. The
     * list is the first match, as the sheet of the actions of a section is another root with a scrollable column.
     */
    private fun scrolledByValue() =
        composeTestRule
            .onAllNodes(SemanticsMatcher.keyIsDefined(SemanticsProperties.VerticalScrollAxisRange))[0]
            .fetchSemanticsNode()
            .config[SemanticsProperties.VerticalScrollAxisRange]
            .value()

    /** Long press the node with [title], drag it in steps to [to] and let go of it. */
    private fun longPressAndDrag(title: String, to: () -> Offset) {
        longPressAndMove(title, to)
        release()
    }

    /**
     * Long press the node with [title] and drag it in steps to [to], letting the list react in between.
     *
     * @param to Where to drag the node to, found once the other nodes took its space.
     */
    private fun longPressAndMove(title: String, to: () -> Offset) {
        val from = boundsOf(title).center
        composeTestRule.onRoot().performTouchInput {
            down(from)
            advanceEventTime(viewConfiguration.longPressTimeoutMillis * 2)
            // Send the delay with a stationary move, an empty batch would drop it.
            move()
        }
        composeTestRule.waitForIdle()
        moveTo(to)
    }

    /**
     * Drag in steps the finger which is down from where it is to [to], letting the list react in between.
     *
     * @param to Where to drag the finger to, found once the nodes moved.
     */
    private fun moveTo(to: () -> Offset) {
        var from = Offset.Zero
        composeTestRule.onRoot().performTouchInput { from = checkNotNull(currentPosition()) }
        val steps = 10
        val step = (to() - from) / steps.toFloat()
        repeat(steps) {
            composeTestRule.onRoot().performTouchInput { moveBy(step) }
            composeTestRule.waitForIdle()
        }
    }

    private fun release() {
        composeTestRule.onRoot().performTouchInput { up() }
        composeTestRule.waitForIdle()
    }

    private fun cancelGesture() {
        composeTestRule.onRoot().performTouchInput { cancel() }
        composeTestRule.waitForIdle()
    }

    /** Tap the handle of the section with [title], drag it in steps to [to] and let go of it. */
    private fun dragByHandle(title: String, to: () -> Offset) {
        pickUpByHandle(title)
        moveTo(to)
        release()
    }

    /**
     * Tap the handle of the section with [title] and move it just far enough for the section to be dragged, letting the
     * other sections take its space.
     */
    private fun pickUpByHandle(title: String) {
        pressHandle(title)
        composeTestRule.onRoot().performTouchInput { moveBy(Offset(0f, viewConfiguration.touchSlop * 3)) }
        composeTestRule.waitForIdle()
    }

    /** Hold the handle of the section with [title] for longer than a long press, then let go of it. */
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

    /** Where the handle of the section with [title] is, at the end of the row of the section. */
    private fun handleOf(title: String): Offset {
        val listBounds = composeTestRule.onRoot().fetchSemanticsNode().boundsInRoot
        val distanceFromEnd = with(composeTestRule.density) { 30.dp.toPx() }
        return Offset(listBounds.right - distanceFromEnd, boundsOf(title).center.y)
    }

    /** [HapticFeedback] that records every feedback type it is asked to perform, in order, in [performed]. */
    private class RecordingHapticFeedback : HapticFeedback {
        val performed = mutableListOf<HapticFeedbackType>()

        override fun performHapticFeedback(hapticFeedbackType: HapticFeedbackType) {
            performed += hapticFeedbackType
        }
    }

    private fun shownIds(store: CustomizableMenuStore) =
        store.state.menuGroups.map { it.id }.filterNot { it in store.state.hiddenGroupIds }

    private fun hiddenIds(store: CustomizableMenuStore) =
        store.state.menuGroups.map { it.id }.filter { it in store.state.hiddenGroupIds }

    /**
     * State of a screen showing the [shown] groups, with the [mandatoryIds] ones as mandatory, then the [hidden] ones.
     */
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
}
