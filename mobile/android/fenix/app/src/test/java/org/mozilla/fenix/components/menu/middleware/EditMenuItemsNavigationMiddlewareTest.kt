/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.middleware

import androidx.navigation.NavController
import androidx.navigation.NavDirections
import androidx.navigation.NavOptions
import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.ExpansionToggled
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.PreviewClicked
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.Update
import mozilla.components.compose.menu.store.CustomizableMenuItemsState
import mozilla.components.compose.menu.store.CustomizableMenuItemsStore
import org.junit.Test
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.MenuAccessPoint
import org.mozilla.fenix.components.menu.compose.customize.EditMenuItemsFragmentDirections

class EditMenuItemsNavigationMiddlewareTest {
    private val navController: NavController = mockk(relaxed = true)

    @Test
    fun `WHEN PreviewClicked is dispatched THEN navigate to MenuFragment`() {
        val store = createStore()

        store.dispatch(PreviewClicked)

        verify {
            navController.navigate(
                EditMenuItemsFragmentDirections.actionEditMenuItemsFragmentToMenuFragment(
                    accessPoint = MenuAccessPoint.Preview
                ),
                null,
            )
        }
    }

    @Test
    fun `WHEN other actions are dispatched THEN no navigation occurs`() {
        val store = createStore()

        store.dispatch(ExpansionToggled("more"))
        store.dispatch(Update(emptyList(), emptyList()))

        verify(exactly = 0) {
            navController.navigate(any<NavDirections>(), any<NavOptions>())
        }
    }

    private fun createStore(): CustomizableMenuItemsStore {
        every { navController.currentDestination?.id } returns R.id.editMenuItemsFragment
        return CustomizableMenuItemsStore(
            initialState = CustomizableMenuItemsState(),
            middleware = listOf(EditMenuItemsNavigationMiddleware(navController)),
        )
    }
}
