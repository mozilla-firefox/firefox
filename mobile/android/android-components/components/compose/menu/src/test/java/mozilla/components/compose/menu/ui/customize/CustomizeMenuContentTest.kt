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
import androidx.compose.ui.test.getUnclippedBoundsInRoot
import androidx.compose.ui.test.hasScrollAction
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.test.performScrollToNode
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.unit.dp
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.R
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.CustomizableMenuAction
import mozilla.components.compose.menu.store.CustomizableMenuAction.AttemptedRemovalOfMandatorySection
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

        val hiddenBounds = composeTestRule.onNodeWithText("Hidden").getUnclippedBoundsInRoot()
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

        composeTestRule.onNode(hasScrollAction()).performScrollToNode(hasText("Hidden 9"))

        composeTestRule.onNodeWithText("Hidden 9").assertIsDisplayed()
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
    fun `GIVEN a section taking so much space that the hide section area takes its place WHEN it is long pressed without moving THEN the user only feels the long press`() {
        setContent(menuState(shown = listOf(row("Shown"), row(id = "Tall", "Tall 1", "Tall 2", "Tall 3"))))

        longPressAndMove(title = "Tall 1", to = { boundsOf("Tall 1").center })

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
    fun `WHEN the handle of a section is held for longer than a long press and then dragged THEN the section is dragged`() {
        val store = setContent(menuState(shown = listOf(row("Shown 1"), row("Shown 2"))))

        pressHandle("Shown 2")
        composeTestRule.onRoot().performTouchInput {
            advanceEventTime(viewConfiguration.longPressTimeoutMillis * 2)
            move()
        }
        composeTestRule.waitForIdle()
        moveTo { boundsOf("Shown 1").topCenter }
        release()

        assertEquals(listOf("Shown 2", "Shown 1"), shownIds(store))
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

    private fun boundsOf(text: String) = composeTestRule.onNodeWithText(text).fetchSemanticsNode().boundsInRoot

    private fun rootBounds() = composeTestRule.onRoot().fetchSemanticsNode().boundsInRoot

    /** How far the list is scrolled, found without its scroll actions as these are gone while a section is dragged. */
    private fun scrolledByValue() =
        composeTestRule
            .onNode(SemanticsMatcher.keyIsDefined(SemanticsProperties.VerticalScrollAxisRange))
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
    ) =
        CustomizableMenuState(
            menuGroups = shown + hidden,
            mandatoryGroupIds = mandatoryIds,
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
}
