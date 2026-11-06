/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel

import io.mockk.mockk
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.mozilla.fenix.settings.trustpanel.store.WebsiteInfoState

class WebsiteInfoStateTest {

    @Test
    fun `GIVEN QWAC certificate data WHEN checking isQwac THEN it is true`() {
        assertTrue(WebsiteInfoState(certificate = mockk(), qwac = mockk()).isQwac)
    }

    @Test
    fun `GIVEN certificate data but no QWAC certificate data yet WHEN checking isQwac THEN it is false`() {
        assertFalse(WebsiteInfoState(certificate = mockk()).isQwac)
    }

    @Test
    fun `GIVEN no certificate data WHEN checking isQwac THEN it is false`() {
        assertFalse(WebsiteInfoState(isSecured = false).isQwac)
    }
}
