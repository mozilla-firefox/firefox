/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.middleware

import androidx.navigation.NavController
import androidx.navigation.NavDirections
import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import mozilla.components.compose.menu.store.CustomizableMenuAction.BackPressed
import mozilla.components.compose.menu.store.CustomizableMenuAction.Init
import mozilla.components.compose.menu.store.CustomizableMenuAction.PreviewClicked
import mozilla.components.compose.menu.store.CustomizableMenuAction.ResetToDefaultClicked
import mozilla.components.compose.menu.store.CustomizableMenuAction.ResetToDefaultConfirmed
import mozilla.components.compose.menu.store.CustomizableMenuState
import mozilla.components.compose.menu.store.CustomizableMenuStore
import org.junit.Test
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.MenuAccessPoint
import org.mozilla.fenix.components.menu.compose.customize.CustomizeMenuFragmentDirections

class CustomizeMenuNavigationMiddlewareTest {
    private val navController: NavController = mockk(relaxed = true)

    @Test
    fun `WHEN PreviewClicked is dispatched THEN navigate to MenuFragment`() {
        val store = createStore()

        store.dispatch(PreviewClicked)

        verify {
            navController.navigate(
                CustomizeMenuFragmentDirections.actionCustomizeMenuFragmentToMenuFragment(
                    accessPoint = MenuAccessPoint.Preview
                ),
                null,
            )
        }
    }

    @Test
    fun `WHEN ResetToDefaultClicked is dispatched THEN navigate to ResetToDefaultDialogFragment`() {
        val store = createStore()

        store.dispatch(ResetToDefaultClicked)

        verify {
            navController.navigate(
                CustomizeMenuFragmentDirections.actionCustomizeMenuFragmentToResetToDefaultDialogFragment(),
                null,
            )
        }
    }

    @Test
    fun `WHEN other actions are dispatched THEN no navigation occurs`() {
        val store = createStore()

        store.dispatch(Init)
        store.dispatch(BackPressed)
        store.dispatch(ResetToDefaultConfirmed)

        verify(exactly = 0) {
            navController.navigate(any<NavDirections>())
        }
    }

    private fun createStore(): CustomizableMenuStore {
        every { navController.currentDestination?.id } returns R.id.customizeMenuFragment
        return CustomizableMenuStore(
            initialState = CustomizableMenuState(emptyList()),
            middleware = listOf(CustomizeMenuNavigationMiddleware(navController)),
        )
    }
}
