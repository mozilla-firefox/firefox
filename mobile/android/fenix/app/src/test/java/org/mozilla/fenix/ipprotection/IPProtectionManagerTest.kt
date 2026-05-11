/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ipprotection

import junit.framework.TestCase.assertFalse
import junit.framework.TestCase.assertTrue
import org.junit.Test

class IPProtectionManagerTest {
    @Test
    fun `WHEN canShowIPProtectionPrompt returns true THEN shouldShowIPProtectionPrompt returns true`() {
        val repository = FakeIPProtectionPromptRepository()

        val ipProtectionManager = IPProtectionManager(repository)

        assertTrue(ipProtectionManager.shouldShowIPProtectionPrompt())
    }

    @Test
    fun `WHEN canShowIPProtectionPrompt returns false THEN shouldShowIPProtectionPrompt returns false`() {
        val repository = FakeIPProtectionPromptRepository(canShowIPProtectionPrompt = false)

        val ipProtectionManager = IPProtectionManager(repository)

        assertFalse(ipProtectionManager.shouldShowIPProtectionPrompt())
    }
}
