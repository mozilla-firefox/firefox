/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.awesomebar.provider

import java.time.LocalDateTime
import java.time.ZoneId
import java.util.Locale
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class ProviderUtilsTest {

    // --- parseIsoDate tests ---

    @Test
    fun `parseIsoDate parses valid ISO date string`() {
        val timeZone = ZoneId.of("UTC")
        val result = parseIsoDate("2025-10-05T13:05:00+00:00", timeZone)

        assertEquals(LocalDateTime.of(2025, 10, 5, 13, 5, 0), result)
    }

    @Test
    fun `parseIsoDate returns null for invalid date string`() {
        val result = parseIsoDate("not-a-date", ZoneId.of("UTC"))

        assertNull(result)
    }

    @Test
    fun `parseIsoDate converts offset date into the provided timezone`() {
        val timeZone = ZoneId.of("UTC-5")
        val result = parseIsoDate("2025-10-05T13:05:00-07:00", timeZone)

        assertEquals(LocalDateTime.of(2025, 10, 5, 15, 5, 0), result)
    }

    // --- formatShortTime tests ---

    @Test
    fun `formatShortTime returns 12-hour format for US locale`() {
        val dateTime = LocalDateTime.of(2025, 10, 5, 17, 30, 0)

        val result = formatShortTime(dateTime, Locale.US)

        assertEquals("5:30\u202FPM", result)
    }

    @Test
    fun `formatShortTime returns 24-hour format for France locale`() {
        val dateTime = LocalDateTime.of(2025, 10, 5, 17, 30, 0)

        val result = formatShortTime(dateTime, Locale.FRANCE)

        assertEquals("17:30", result)
    }
}
