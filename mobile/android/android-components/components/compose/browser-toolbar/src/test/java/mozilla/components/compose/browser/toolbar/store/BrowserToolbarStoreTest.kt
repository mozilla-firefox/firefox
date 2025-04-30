/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.browser.toolbar.store

import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.compose.browser.toolbar.R
import mozilla.components.compose.browser.toolbar.concept.Action.ActionButton
import mozilla.components.compose.browser.toolbar.concept.PageOrigin
import mozilla.components.compose.browser.toolbar.store.BrowserDisplayToolbarAction.BrowserActionsEndUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserDisplayToolbarAction.BrowserActionsStartUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserDisplayToolbarAction.PageActionsEndUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserDisplayToolbarAction.PageActionsStartUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserDisplayToolbarAction.PageOriginUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent
import mozilla.components.support.test.rule.MainCoroutineRule
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import kotlin.random.Random

@RunWith(AndroidJUnit4::class)
class BrowserToolbarStoreTest {

    @get:Rule
    val coroutineTestRule = MainCoroutineRule()

    @Test
    fun `WHEN toggle edit mode action is dispatched THEN update the mode and edit text states`() {
        val store = BrowserToolbarStore()
        val editMode = true

        assertEquals(Mode.DISPLAY, store.state.mode)

        store.dispatch(BrowserToolbarAction.ToggleEditMode(editMode = editMode))

        assertEquals(Mode.EDIT, store.state.mode)
        assertNull(store.state.editState.editText)
    }

    @Test
    fun `WHEN update edit text action is dispatched THEN update edit text state`() {
        val store = BrowserToolbarStore()
        val text = "Mozilla"

        assertNull(store.state.editState.editText)

        store.dispatch(BrowserEditToolbarAction.UpdateEditText(text = text))

        assertEquals(text, store.state.editState.editText)
    }

    @Test
    fun `WHEN add edit action start is dispatched THEN update edit actions start state`() {
        val store = BrowserToolbarStore()
        val action1 = fakeActionButton()
        val action2 = fakeActionButton()

        assertEquals(0, store.state.editState.editActionsStart.size)

        store.dispatch(BrowserEditToolbarAction.AddEditActionStart(action = action1))

        assertEquals(1, store.state.editState.editActionsStart.size)
        assertEquals(action1, store.state.editState.editActionsStart.first())

        store.dispatch(BrowserEditToolbarAction.AddEditActionStart(action = action2))

        assertEquals(2, store.state.editState.editActionsStart.size)
        assertEquals(action1, store.state.editState.editActionsStart.first())
        assertEquals(action2, store.state.editState.editActionsStart.last())
    }

    @Test
    fun `WHEN add edit action end is dispatched THEN update edit actions end state`() {
        val store = BrowserToolbarStore()
        val action1 = fakeActionButton()
        val action2 = fakeActionButton()

        assertEquals(0, store.state.editState.editActionsEnd.size)

        store.dispatch(BrowserEditToolbarAction.AddEditActionEnd(action = action1))

        assertEquals(1, store.state.editState.editActionsEnd.size)
        assertEquals(action1, store.state.editState.editActionsEnd.first())

        store.dispatch(BrowserEditToolbarAction.AddEditActionEnd(action = action2))

        assertEquals(2, store.state.editState.editActionsEnd.size)
        assertEquals(action1, store.state.editState.editActionsEnd.first())
        assertEquals(action2, store.state.editState.editActionsEnd.last())
    }

    @Test
    fun `WHEN updating start browser actions THEN replace the old actions with the new ones`() {
        val store = BrowserToolbarStore()
        val action1 = fakeActionButton()
        val action2 = fakeActionButton()
        val action3 = fakeActionButton()
        assertEquals(0, store.state.displayState.browserActionsStart.size)

        store.dispatch(BrowserActionsStartUpdated(listOf(action1)))
        assertEquals(listOf(action1), store.state.displayState.browserActionsStart)

        store.dispatch(BrowserActionsStartUpdated(listOf(action2, action3)))
        assertEquals(listOf(action2, action3), store.state.displayState.browserActionsStart)
    }

    @Test
    fun `WHEN updating start page actions THEN replace old actions with the new one`() {
        val store = BrowserToolbarStore()
        val action1 = fakeActionButton()
        val action2 = fakeActionButton()
        val action3 = fakeActionButton()
        assertEquals(0, store.state.displayState.pageActionsStart.size)

        store.dispatch(PageActionsStartUpdated(listOf(action1)))
        assertEquals(listOf(action1), store.state.displayState.pageActionsStart)

        store.dispatch(PageActionsStartUpdated(listOf(action2, action3)))
        assertEquals(listOf(action2, action3), store.state.displayState.pageActionsStart)

        store.dispatch(PageActionsStartUpdated(emptyList()))
        assertEquals(0, store.state.displayState.pageActionsStart.size)
    }

    @Test
    fun `WHEN updating end page actions THEN replace old actions with the new one`() {
        val store = BrowserToolbarStore()
        val action1 = fakeActionButton()
        val action2 = fakeActionButton()
        val action3 = fakeActionButton()
        assertEquals(0, store.state.displayState.pageActionsEnd.size)

        store.dispatch(PageActionsEndUpdated(listOf(action1)))
        assertEquals(listOf(action1), store.state.displayState.pageActionsEnd)

        store.dispatch(PageActionsEndUpdated(listOf(action2, action3)))
        assertEquals(listOf(action2, action3), store.state.displayState.pageActionsEnd)

        store.dispatch(PageActionsEndUpdated(emptyList()))
        assertEquals(0, store.state.displayState.pageActionsEnd.size)
    }

    @Test
    fun `WHEN updating the page origin details THEN replace the old details with the new ones`() {
        val store = BrowserToolbarStore()
        val defaultPageDetails = PageOrigin(
            hint = R.string.mozac_browser_toolbar_search_hint,
            title = null,
            url = null,
            onClick = object : BrowserToolbarEvent {},
        )
        val newPageDetails = PageOrigin(
            hint = Random.nextInt(),
            title = "test",
            url = "https://firefox.com",
            onClick = object : BrowserToolbarEvent {},
            onLongClick = object : BrowserToolbarEvent {},
        )
        assertPageOriginEquals(defaultPageDetails, store.state.displayState.pageOrigin)

        store.dispatch(PageOriginUpdated(newPageDetails))

        assertEquals(newPageDetails, store.state.displayState.pageOrigin)
    }

    @Test
    fun `WHEN updating end browser actions THEN replace the old actions with the new ones`() {
        val store = BrowserToolbarStore()
        val action1 = fakeActionButton()
        val action2 = fakeActionButton()
        val action3 = fakeActionButton()
        assertEquals(0, store.state.displayState.browserActionsEnd.size)

        store.dispatch(BrowserActionsEndUpdated(listOf(action1)))
        assertEquals(listOf(action1), store.state.displayState.browserActionsEnd)

        store.dispatch(BrowserActionsEndUpdated(listOf(action2, action3)))
        assertEquals(listOf(action2, action3), store.state.displayState.browserActionsEnd)
    }

    private fun fakeActionButton() = ActionButton(
        icon = Random.nextInt(),
        contentDescription = Random.nextInt(),
        tint = Random.nextInt(),
        onClick = object : BrowserToolbarEvent {},
    )

    private fun assertPageOriginEquals(expected: PageOrigin, actual: PageOrigin) {
        assertEquals(expected.hint, actual.hint)
        assertEquals(expected.title, actual.title)
        assertEquals(expected.url, actual.url)
        // Cannot check the onClick and onLongClick anonymous object
    }
}
