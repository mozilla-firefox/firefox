/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.sync.autofill

import mozilla.components.concept.storage.CreditCardsAddressesStorage
import mozilla.components.concept.storage.LoginsStorage
import mozilla.components.support.test.mock
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test

class GlobalAutofillDependencyProviderTest {

    @Before
    @After
    fun cleanUp() {
        GlobalAutofillDependencyProvider.autofillStorage = null
    }

    @Test(expected = IllegalArgumentException::class)
    fun `requirePlacesStorage called without calling initialize, exception returned`() {
        GlobalAutofillDependencyProvider.requireAutofillStorage()
    }

    @Test
    fun `requirePlacesStorage called after calling initialize, loginsStorage returned`() {
        val loginsStorage = mock<CreditCardsAddressesStorage>()
        GlobalAutofillDependencyProvider.initialize(lazy { loginsStorage })
        assertEquals(loginsStorage, GlobalAutofillDependencyProvider.requireAutofillStorage())
    }
}
