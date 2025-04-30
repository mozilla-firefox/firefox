/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.search

import android.view.WindowManager.LayoutParams
import androidx.fragment.app.Fragment
import androidx.navigation.NavBackStackEntry
import androidx.navigation.NavController
import androidx.navigation.fragment.findNavController
import io.mockk.Called
import io.mockk.Runs
import io.mockk.every
import io.mockk.just
import io.mockk.mockk
import io.mockk.mockkStatic
import io.mockk.spyk
import io.mockk.unmockkStatic
import io.mockk.verify
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.browser.state.state.SearchState
import mozilla.components.support.test.robolectric.testContext
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.helpers.FenixRobolectricTestRunner

@RunWith(FenixRobolectricTestRunner::class)
internal class SearchDialogFragmentTest {
    private val navController: NavController = mockk()
    private val fragment = SearchDialogFragment()

    @Before
    fun setup() {
        mockkStatic("androidx.navigation.fragment.FragmentKt")
        every { any<Fragment>().findNavController() } returns navController
    }

    @After
    fun teardown() {
        unmockkStatic("androidx.navigation.fragment.FragmentKt")
    }

    @Test
    fun `GIVEN this is the only visible fragment WHEN asking for the previous destination THEN return null`() {
        every { navController.currentBackStack.value } returns ArrayDeque(listOf(getDestination(fragmentName)))

        assertNull(fragment.getPreviousDestination())
    }

    @Test
    fun `GIVEN this and FragmentB on top of this are visible WHEN asking for the previous destination THEN return null`() {
        every { navController.currentBackStack.value } returns ArrayDeque(
            listOf(
                getDestination(fragmentName),
                getDestination("FragmentB"),
            ),
        )

        assertNull(fragment.getPreviousDestination())
    }

    @Test
    fun `GIVEN FragmentA, this and FragmentB are visible WHEN asking for the previous destination THEN return FragmentA`() {
        val fragmentADestination = getDestination("FragmentA")
        every { navController.currentBackStack.value } returns ArrayDeque(
            listOf(
                fragmentADestination,
                getDestination(fragmentName),
                getDestination("FragmentB"),
            ),
        )

        assertSame(fragmentADestination, fragment.getPreviousDestination())
    }

    @Test
    fun `GIVEN FragmentA and this on top of it are visible WHEN asking for the previous destination THEN return FragmentA`() {
        val fragmentADestination = getDestination("FragmentA")
        every { navController.currentBackStack.value } returns ArrayDeque(
            listOf(
                fragmentADestination,
                getDestination(fragmentName),
            ),
        )

        assertSame(fragmentADestination, fragment.getPreviousDestination())
    }

    @Test
    fun `GIVEN the default search engine is currently selected WHEN checking the need to update the current search engine THEN don't to anything`() {
        val searchDialogFragment = spyk(fragment)
        val interactor = spyk(SearchDialogInteractor(mockk()))

        every { searchDialogFragment.interactor } returns interactor

        val defaultSearchEngine: SearchEngine = mockk {
            every { id } returns "default"
        }
        val otherSearchEngine: SearchEngine = mockk {
            every { id } returns "other"
        }

        every { searchDialogFragment.requireContext() } returns testContext
        every { testContext.components.core.store.state.search } returns SearchState(
            regionSearchEngines = listOf(defaultSearchEngine, otherSearchEngine),
            userSelectedSearchEngineId = "default",
        )

        searchDialogFragment.maybeSelectShortcutEngine(defaultSearchEngine.id)

        verify { interactor wasNot Called }
    }

    @Test
    fun `GIVEN the default search engine is not currently selected WHEN checking the need to update the current search engine THEN update it to the current engine`() {
        val searchDialogFragment = spyk(fragment)
        val interactor = spyk(SearchDialogInteractor(mockk()))

        every { searchDialogFragment.interactor } returns interactor
        every { interactor.onSearchShortcutEngineSelected(any()) } just Runs

        val defaultSearchEngine: SearchEngine = mockk {
            every { id } returns "default"
        }
        val otherSearchEngine: SearchEngine = mockk {
            every { id } returns "other"
        }

        every { searchDialogFragment.requireContext() } returns testContext
        every { testContext.components.core.store.state.search } returns SearchState(
            regionSearchEngines = listOf(defaultSearchEngine, otherSearchEngine),
            userSelectedSearchEngineId = "default",
        )

        searchDialogFragment.maybeSelectShortcutEngine(otherSearchEngine.id)

        verify { interactor.onSearchShortcutEngineSelected(any()) }
    }

    @Test
    fun `GIVEN the currently selected search engine is unknown WHEN checking the need to update the current search engine THEN don't do anything`() {
        fragment.nullableInteractor = mockk()

        fragment.maybeSelectShortcutEngine(null)

        verify { fragment.interactor wasNot Called }
    }

    @Test
    fun `GIVEN app is in private mode WHEN search dialog is created THEN the dialog is secure`() {
        val activity: HomeActivity = mockk(relaxed = true)
        val fragment = spyk(SearchDialogFragment())
        val layoutParams = LayoutParams()
        layoutParams.flags = LayoutParams.FLAG_SECURE

        every { activity.browsingModeManager.mode.isPrivate } returns true
        every { activity.window } returns mockk(relaxed = true) {
            every { attributes } returns LayoutParams().apply { flags = LayoutParams.FLAG_SECURE }
        }
        every { fragment.requireActivity() } returns activity
        every { fragment.requireContext() } returns testContext

        val dialog = fragment.onCreateDialog(null)

        assertEquals(LayoutParams.FLAG_SECURE, dialog.window?.attributes?.flags!! and LayoutParams.FLAG_SECURE)
    }

    @Test
    fun `GIVEN app is in normal mode WHEN search dialog is created THEN the dialog is not secure`() {
        val activity: HomeActivity = mockk(relaxed = true)
        val fragment = spyk(SearchDialogFragment())
        val layoutParams = LayoutParams()
        layoutParams.flags = LayoutParams.FLAG_SECURE

        every { activity.browsingModeManager.mode.isPrivate } returns false
        every { activity.window } returns mockk(relaxed = true) {
            every { attributes } returns LayoutParams().apply { flags = LayoutParams.FLAG_SECURE }
        }
        every { fragment.requireActivity() } returns activity
        every { fragment.requireContext() } returns testContext

        val dialog = fragment.onCreateDialog(null)

        assertEquals(0, dialog.window?.attributes?.flags!! and LayoutParams.FLAG_SECURE)
    }
}

private val fragmentName = SearchDialogFragment::class.java.canonicalName?.substringAfterLast('.')!!

private fun getDestination(destinationName: String): NavBackStackEntry {
    return mockk {
        every { destination } returns mockk {
            every { displayName } returns "test.id/$destinationName"
        }
    }
}
