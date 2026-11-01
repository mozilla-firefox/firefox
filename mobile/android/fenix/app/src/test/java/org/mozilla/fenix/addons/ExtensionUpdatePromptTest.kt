/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.addons

import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.extension.PendingExtensionUpdate
import mozilla.components.browser.state.store.BrowserStore
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class ExtensionUpdatePromptTest {

    private val answers = mutableListOf<Boolean>()
    private val pendingUpdate =
        PendingExtensionUpdate(
            extensionId = "extension",
            permissions = listOf("privacy"),
            origins = emptyList(),
            dataCollectionPermissions = emptyList(),
            onPermissionsGranted = { answers.add(it) },
        )
    private val store = BrowserStore(BrowserState(pendingExtensionUpdates = mapOf("extension" to pendingUpdate)))

    @Test
    fun `showing the prompt asks for the review of the pending update`() {
        showPendingExtensionUpdate(store, "extension")

        assertEquals("extension", store.state.pendingExtensionUpdateReviewRequest)
        assertTrue(answers.isEmpty())
    }

    @Test
    fun `cancelling denies the update and removes it`() {
        cancelPendingExtensionUpdate(store, "extension")

        assertEquals(listOf(false), answers)
        assertTrue(store.state.pendingExtensionUpdates.isEmpty())
    }

    @Test
    fun `cancelling an extension without a pending update does nothing`() {
        cancelPendingExtensionUpdate(store, "another-extension")

        assertTrue(answers.isEmpty())
        assertEquals(setOf("extension"), store.state.pendingExtensionUpdates.keys)
    }
}
