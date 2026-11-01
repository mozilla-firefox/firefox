/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

import io.mockk.every
import io.mockk.spyk
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class MenuDialogFragmentTest {
    private lateinit var fragment: MenuDialogFragment

    private val reviewUpdateDescription
        get() =
            testContext.getString(
                R.string.browser_menu_extensions_review_update_description,
                "uBlock Origin, Dark Reader",
            )

    @Before
    fun setUp() {
        fragment = spyk(MenuDialogFragment())
        every { fragment.requireContext() } returns testContext
    }

    @Test
    fun `GIVEN pending extension updates WHEN in a tab or on the homepage THEN the extensions item names them`() {
        for (accessPoint in listOf(MenuAccessPoint.Browser, MenuAccessPoint.Home)) {
            assertEquals(reviewUpdateDescription, pendingUpdatesDescription(accessPoint))
        }
    }

    @Test
    fun `GIVEN pending extension updates WHEN in a custom tab or with extensions disabled THEN they are not mentioned`() {
        assertNull(pendingUpdatesDescription(MenuAccessPoint.External))
        assertNull(pendingUpdatesDescription(MenuAccessPoint.Browser, isExtensionsProcessDisabled = true))
    }

    private fun pendingUpdatesDescription(accessPoint: MenuAccessPoint, isExtensionsProcessDisabled: Boolean = false) =
        fragment.getPendingExtensionUpdatesDescription(
            accessPoint = accessPoint,
            isExtensionsProcessDisabled = isExtensionsProcessDisabled,
            pendingExtensionUpdateNames = listOf("uBlock Origin", "Dark Reader"),
        )
}
