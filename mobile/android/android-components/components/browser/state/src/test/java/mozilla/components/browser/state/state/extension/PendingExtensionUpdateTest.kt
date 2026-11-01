/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.state.state.extension

import org.junit.Assert.assertEquals
import org.junit.Test

class PendingExtensionUpdateTest {

    @Test
    fun `complete answers the engine only once`() {
        val answers = mutableListOf<Boolean>()
        val pendingUpdate = pendingExtensionUpdate { answers.add(it) }

        pendingUpdate.complete(granted = true)
        pendingUpdate.complete(granted = false)

        assertEquals(listOf(true), answers)
    }

    @Test
    fun `complete passes false to the engine callback`() {
        val answers = mutableListOf<Boolean>()

        pendingExtensionUpdate { answers.add(it) }.complete(granted = false)

        assertEquals(listOf(false), answers)
    }

    private fun pendingExtensionUpdate(onPermissionsGranted: (Boolean) -> Unit) =
        PendingExtensionUpdate(
            extensionId = "extension",
            permissions = listOf("privacy"),
            origins = emptyList(),
            dataCollectionPermissions = emptyList(),
            onPermissionsGranted = onPermissionsGranted,
        )
}
