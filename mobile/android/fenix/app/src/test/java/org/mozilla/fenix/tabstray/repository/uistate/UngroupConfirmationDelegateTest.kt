/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.repository.uistate

import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Test
import org.mozilla.fenix.tabstray.data.createTabGroup
import org.mozilla.fenix.tabstray.fakes.FakeTabManagerUiStateRepository
import org.mozilla.fenix.tabstray.redux.action.TabGroupAction
import org.mozilla.fenix.tabstray.repository.uistate.data.PersistedUIState

@OptIn(ExperimentalCoroutinesApi::class)
class UngroupConfirmationDelegateTest {

    private val group = createTabGroup(id = "group-1")

    @Test
    fun `GIVEN skip is enabled WHEN resolve is called THEN ungroup is confirmed without the dialog`() = runTest {
        val delegate = createDelegate(skipUngroupConfirmation = true)
        val dispatched = mutableListOf<TabGroupAction>()

        delegate.resolve(group, dispatched::add)

        assertEquals(TabGroupAction.UngroupConfirmed(group = group, dontAskAgain = true), dispatched.single())
    }

    @Test
    fun `GIVEN skip is disabled WHEN resolve is called THEN the confirmation is requested`() = runTest {
        val delegate = createDelegate(skipUngroupConfirmation = false)
        val dispatched = mutableListOf<TabGroupAction>()

        delegate.resolve(group, dispatched::add)

        assertEquals(TabGroupAction.UngroupConfirmationRequested(group = group), dispatched.single())
    }

    @Test
    fun `GIVEN dont ask again is true WHEN setSkip is called THEN the preference is stored`() = runTest {
        val repository =
            FakeTabManagerUiStateRepository(initialPersistedUIState = PersistedUIState(skipUngroupConfirmation = false))
        val delegate = UngroupConfirmationDelegate(repository = repository, scope = backgroundScope)

        delegate.setSkip(dontAskAgain = true)
        runCurrent()

        assertTrue(repository.uiState.value!!.skipUngroupConfirmation)
    }

    @Test
    fun `GIVEN dont ask again is false WHEN setSkip is called THEN the preference is unchanged`() = runTest {
        val repository =
            FakeTabManagerUiStateRepository(initialPersistedUIState = PersistedUIState(skipUngroupConfirmation = false))
        val delegate = UngroupConfirmationDelegate(repository = repository, scope = backgroundScope)

        delegate.setSkip(dontAskAgain = false)
        runCurrent()

        assertFalse(repository.uiState.value!!.skipUngroupConfirmation)
    }

    private fun TestScope.createDelegate(skipUngroupConfirmation: Boolean) =
        UngroupConfirmationDelegate(
            repository =
                FakeTabManagerUiStateRepository(
                    initialPersistedUIState = PersistedUIState(skipUngroupConfirmation = skipUngroupConfirmation)
                ),
            scope = backgroundScope,
        )
}
