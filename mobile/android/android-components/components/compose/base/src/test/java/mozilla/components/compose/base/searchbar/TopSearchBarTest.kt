/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.base.searchbar

import androidx.compose.material3.SearchBarState
import androidx.compose.material3.SearchBarValue
import androidx.compose.material3.rememberSearchBarState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.hasSetTextAction
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performTextInput
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class TopSearchBarTest {

    @get:Rule val composeTestRule = createComposeRule()

    private var expanded by mutableStateOf(false)
    private val reported = mutableListOf<Boolean>()
    private var query by mutableStateOf("")
    private val reportedQueries = mutableListOf<String>()
    private lateinit var state: SearchBarState
    private lateinit var scope: CoroutineScope

    private fun setContent(initiallyExpanded: Boolean) {
        expanded = initiallyExpanded
        composeTestRule.setContent {
            state = rememberSearchBarState()
            scope = rememberCoroutineScope()
            TopSearchBar(
                state = state,
                query = query,
                onQueryChange = {
                    reportedQueries.add(it)
                    query = it
                },
                onSearch = {},
                expanded = expanded,
                onExpandedChange = {
                    reported.add(it)
                    expanded = it
                },
            )
        }
        composeTestRule.waitForIdle()
    }

    @Test
    fun `GIVEN the caller starts expanded WHEN the bar is created THEN it expands without reporting a change`() {
        setContent(initiallyExpanded = true)

        assertEquals(SearchBarValue.Expanded, state.targetValue)
        assertEquals(emptyList<Boolean>(), reported)
    }

    @Test
    fun `WHEN the caller changes expanded THEN the bar follows without reporting it back`() {
        setContent(initiallyExpanded = false)

        expanded = true
        composeTestRule.waitForIdle()
        assertEquals(SearchBarValue.Expanded, state.targetValue)

        expanded = false
        composeTestRule.waitForIdle()
        assertEquals(SearchBarValue.Collapsed, state.targetValue)

        assertEquals(emptyList<Boolean>(), reported)
    }

    @Test
    fun `WHEN the bar expands on its own THEN the change is reported once`() {
        setContent(initiallyExpanded = false)

        composeTestRule.runOnIdle { scope.launch { state.animateToExpanded() } }
        composeTestRule.waitForIdle()

        assertEquals(listOf(true), reported)
        assertTrue(expanded)
    }

    private fun inputField() = composeTestRule.onNode(hasSetTextAction())

    private fun inputText() = inputField().fetchSemanticsNode().config[SemanticsProperties.EditableText].text

    @Test
    fun `WHEN the user types or clears the query THEN each change is reported`() {
        setContent(initiallyExpanded = true)

        inputField().performTextInput("abc")
        composeTestRule.onNodeWithContentDescription("Clear text").performClick()
        composeTestRule.waitForIdle()

        assertEquals(listOf("abc", ""), reportedQueries)
        assertEquals("", inputText())
    }

    @Test
    fun `WHEN the caller changes the query THEN the field shows it without reporting it back`() {
        setContent(initiallyExpanded = true)

        query = "xyz"
        composeTestRule.waitForIdle()

        assertEquals("xyz", inputText())
        assertEquals(emptyList<String>(), reportedQueries)
    }
}
