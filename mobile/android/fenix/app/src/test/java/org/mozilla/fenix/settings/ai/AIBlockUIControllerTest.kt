/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.ai

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class AIBlockUIControllerTest {

    @Test
    fun `onDismiss sets showDialogFlow to false`() {
        val controller = AIBlockUIController(onBlockDialog = {})

        controller.onToggle(currentlyBlocked = false)
        controller.onDialogDismiss()

        assertFalse(controller.showDialogFlow.value)
    }

    @Test
    fun `onConfirm sets showDialogFlow to false and calls onBlockDialog with true`() {
        var blockDialogValue: Boolean? = null
        val controller = AIBlockUIController(onBlockDialog = { blockDialogValue = it })

        controller.onDialogConfirm()

        assertFalse(controller.showDialogFlow.value)
        assertEquals(true, blockDialogValue)
    }

    @Test
    fun `onToggle when currently blocked calls onBlockDialog with false`() {
        var blockDialogValue: Boolean? = null
        val controller = AIBlockUIController(onBlockDialog = { blockDialogValue = it })

        controller.onToggle(currentlyBlocked = true)

        assertEquals(false, blockDialogValue)
    }

    @Test
    fun `onToggle when not currently blocked shows dialog`() {
        val controller = AIBlockUIController(onBlockDialog = {})

        controller.onToggle(currentlyBlocked = false)

        assertTrue(controller.showDialogFlow.value)
    }

    @Test
    fun `onToggle when not blocked does not call onBlockDialog`() {
        var blockDialogCalled = false
        val controller = AIBlockUIController(onBlockDialog = { blockDialogCalled = true })

        controller.onToggle(currentlyBlocked = false)

        assertFalse(blockDialogCalled)
    }
}
