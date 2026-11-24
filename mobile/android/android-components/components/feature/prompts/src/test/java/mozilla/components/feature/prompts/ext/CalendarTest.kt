/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.prompts.ext

import java.time.ZoneId
import java.time.ZoneOffset
import java.util.Date
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.junit.runners.JUnit4

@RunWith(JUnit4::class)
class CalendarTest {

    @Test
    fun `GIVEN a date at local midnight in a zone ahead of UTC WHEN converting it to a UTC day start THEN midnight UTC on that local day is returned`() {
        val parisZone = ZoneId.of("Europe/Paris")

        assertEquals(
            epochMillisAt(2026, 8, 13),
            Date(epochMillisAt(2026, 8, 13, zone = parisZone)).toLocalDayStartAsUtcMillis(parisZone),
        )
    }

    @Test
    fun `GIVEN a date at local midnight in a zone behind UTC WHEN converting it to a UTC day start THEN midnight UTC on that local day is returned`() {
        val newYorkZone = ZoneId.of("America/New_York")

        assertEquals(
            epochMillisAt(2026, 8, 13),
            Date(epochMillisAt(2026, 8, 13, zone = newYorkZone)).toLocalDayStartAsUtcMillis(newYorkZone),
        )
    }

    @Test
    fun `GIVEN a date already at midnight UTC WHEN converting it to a UTC day start THEN the value is unchanged`() {
        assertEquals(
            epochMillisAt(2026, 8, 13),
            Date(epochMillisAt(2026, 8, 13)).toLocalDayStartAsUtcMillis(ZoneOffset.UTC),
        )
    }

    @Test
    fun `GIVEN a date with a time of day WHEN converting it to a UTC day start THEN the time of day is dropped`() {
        assertEquals(
            epochMillisAt(2026, 8, 13),
            Date(epochMillisAt(2026, 8, 13, hour = 8, minute = 30)).toLocalDayStartAsUtcMillis(ZoneOffset.UTC),
        )
    }

    @Test
    fun `GIVEN a local day on which DST starts WHEN converting it to a UTC day start THEN midnight UTC on that local day is returned`() {
        val aucklandZone = ZoneId.of("Pacific/Auckland")

        assertEquals(
            epochMillisAt(2026, 9, 27),
            Date(epochMillisAt(2026, 9, 27, zone = aucklandZone)).toLocalDayStartAsUtcMillis(aucklandZone),
        )
    }

    @Test
    fun `GIVEN local midnight on a day on which DST starts WHEN converting it to a UTC day start in UTC THEN midnight UTC on the UTC day is returned`() {
        val aucklandZone = ZoneId.of("Pacific/Auckland")

        assertEquals(
            epochMillisAt(2026, 9, 26),
            Date(epochMillisAt(2026, 9, 27, zone = aucklandZone)).toLocalDayStartAsUtcMillis(ZoneOffset.UTC),
        )
    }

    @Test
    fun `GIVEN midnight UTC on a day WHEN converting it to a local date in a zone ahead of UTC THEN local midnight on that day is returned`() {
        val parisZone = ZoneId.of("Europe/Paris")

        assertEquals(
            Date(epochMillisAt(2026, 8, 13, zone = parisZone)),
            epochMillisAt(2026, 8, 13).utcDayStartToLocalDate(parisZone),
        )
    }

    @Test
    fun `GIVEN midnight UTC on a day WHEN converting it to a local date in a zone behind UTC THEN local midnight on that day is returned`() {
        val newYorkZone = ZoneId.of("America/New_York")

        assertEquals(
            Date(epochMillisAt(2026, 8, 13, zone = newYorkZone)),
            epochMillisAt(2026, 8, 13).utcDayStartToLocalDate(newYorkZone),
        )
    }

    @Test
    fun `GIVEN midnight UTC on a day on which DST starts WHEN converting it to a local date THEN local midnight on that day is returned`() {
        val aucklandZone = ZoneId.of("Pacific/Auckland")

        assertEquals(
            Date(epochMillisAt(2026, 9, 27, zone = aucklandZone)),
            epochMillisAt(2026, 9, 27).utcDayStartToLocalDate(aucklandZone),
        )
    }

    @Test
    fun `GIVEN midnight UTC on a day on which DST ends WHEN converting it to a local date THEN local midnight on that day is returned`() {
        val aucklandZone = ZoneId.of("Pacific/Auckland")

        assertEquals(
            Date(epochMillisAt(2026, 4, 5, zone = aucklandZone)),
            epochMillisAt(2026, 4, 5).utcDayStartToLocalDate(aucklandZone),
        )
    }
}
