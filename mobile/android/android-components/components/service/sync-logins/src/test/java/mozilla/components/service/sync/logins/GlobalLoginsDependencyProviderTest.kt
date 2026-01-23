/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.sync.logins

import mozilla.components.concept.storage.LoginsStorage
import mozilla.components.support.test.mock
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test

class GlobalLoginsDependencyProviderTest {

    @Before
    @After
    fun cleanUp() {
        GlobalLoginsDependencyProvider.loginsStorage = null
    }

    @Test(expected = IllegalArgumentException::class)
    fun `requirePlacesStorage called without calling initialize, exception returned`() {
        GlobalLoginsDependencyProvider.requireLoginsStorage()
    }

    @Test
    fun `requirePlacesStorage called after calling initialize, loginsStorage returned`() {
        val loginsStorage = mock<LoginsStorage>()
        GlobalLoginsDependencyProvider.initialize(lazy { loginsStorage })
        assertEquals(loginsStorage, GlobalLoginsDependencyProvider.requireLoginsStorage())
    }
}
