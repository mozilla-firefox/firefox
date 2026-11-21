/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ext

import io.mockk.every
import io.mockk.mockk
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.test.runTest
import mozilla.components.lib.publicsuffixlist.PublicSuffixList
import org.junit.Assert.assertEquals
import org.junit.Test

class StringTest {
    private var publicSuffixList: PublicSuffixList = mockk()

    @Test
    fun `Simplified Url`() {
        val urlTest = "https://www.amazon.com"
        val new = urlTest.simplifiedUrl()
        assertEquals(new, "amazon.com")
    }

    @Test
    fun getBaseDomainUrl() = runTest {
        val testCases =
            listOf(
                "https://prod-1.storage.jamendo.com/?trackid=2233894&format=mp35&download=true" to "jamendo.com",
                "blob:https://www.pinterest.com/98752e42-2707-44b4-81e3-161a04f0d3aa" to "pinterest.com",
                "https://mozilla.org" to "mozilla.org",
                "http://wikipedia.org" to "wikipedia.org",
                "http://plus.google.com" to "google.com",
                "https://en.m.wikipedia.org/wiki/Main_Page" to "wikipedia.org",
            )

        testCases.forEach { (raw, escaped) ->
            every { publicSuffixList.getPublicSuffixPlusOne(any()) } returns CompletableDeferred(escaped)
            assertEquals(escaped, raw.getBaseDomainUrl(publicSuffixList))
        }
    }
}
