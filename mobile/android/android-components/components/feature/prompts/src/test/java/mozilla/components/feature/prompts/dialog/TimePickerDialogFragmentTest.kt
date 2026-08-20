/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.prompts.dialog

import android.widget.NumberPicker
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.google.android.material.datepicker.CalendarConstraints.DateValidator
import java.util.Calendar
import java.util.Date
import java.util.TimeZone
import mozilla.components.feature.prompts.R
import mozilla.components.feature.prompts.dialog.TimePickerDialogFragment.Companion.SELECTION_TYPE_MONTH
import mozilla.components.feature.prompts.ext.epochMillisAt
import mozilla.components.feature.prompts.ext.month
import mozilla.components.feature.prompts.ext.toCalendar
import mozilla.components.feature.prompts.ext.year
import mozilla.components.support.ktx.kotlin.toDate
import mozilla.components.support.test.ext.appCompatContext
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.doReturn
import org.mockito.Mockito.spy
import org.mockito.MockitoAnnotations.openMocks

@RunWith(AndroidJUnit4::class)
class TimePickerDialogFragmentTest {

    @Before
    fun setup() {
        testContext.setTheme(com.google.android.material.R.style.Theme_MaterialComponents_Light)
        openMocks(this)
    }

    @Test
    fun `onTimeChanged must update the selectedDate`() {
        val dialogPicker = TimePickerDialogFragment.newInstance("sessionId", "uid", false, Date(), null, null)

        dialogPicker.onTimeChanged(mock(), 1, 12)

        val calendar = dialogPicker.selectedDate.toCalendar()

        assertEquals(calendar.hour, 1)
        assertEquals(calendar.minutes, 12)
    }

    @Test
    fun `building a month picker`() {
        val initialDate = "2018-06".toDate("yyyy-MM")
        val minDate = "2018-04".toDate("yyyy-MM")
        val maxDate = "2018-09".toDate("yyyy-MM")

        val initialDateCal = initialDate.toCalendar()
        val minCal = minDate.toCalendar()
        val maxCal = maxDate.toCalendar()

        val fragment =
            spy(
                TimePickerDialogFragment.newInstance(
                    "sessionId",
                    "uid",
                    false,
                    initialDate,
                    minDate,
                    maxDate,
                    SELECTION_TYPE_MONTH,
                )
            )

        doReturn(appCompatContext).`when`(fragment).requireContext()

        val dialog = fragment.onCreateDialog(null)
        dialog.show()

        val monthPicker = dialog.findViewById<NumberPicker>(R.id.month_chooser)
        val yearPicker = dialog.findViewById<NumberPicker>(R.id.year_chooser)

        assertEquals(initialDateCal.year, yearPicker.value)
        assertEquals(initialDateCal.month, monthPicker.value)

        assertEquals(minCal.year, yearPicker.minValue)
        assertEquals(minCal.month, monthPicker.minValue)

        assertEquals(maxCal.year, yearPicker.maxValue)
        assertEquals(maxCal.month, monthPicker.maxValue)

        fragment.onDateSet(mock(), 8, 2019)
        val selectedDate = fragment.selectedDate.toCalendar()

        assertEquals(2019, selectedDate.year)
        assertEquals(7, selectedDate.month)
    }

    @Test(expected = IllegalArgumentException::class)
    fun `creating a TimePickerDialogFragment with an invalid type selection will throw an exception`() {
        val initialDate = "2018-06-12T19:30".toDate("yyyy-MM-dd'T'HH:mm")
        val minDate = "2018-06-07T00:00".toDate("yyyy-MM-dd'T'HH:mm")
        val maxDate = "2018-06-14T00:00".toDate("yyyy-MM-dd'T'HH:mm")
        val fragment =
            spy(
                TimePickerDialogFragment.newInstance(
                    "sessionId",
                    "uid",
                    false,
                    initialDate,
                    minDate,
                    maxDate,
                    -223,
                )
            )

        doReturn(appCompatContext).`when`(fragment).requireContext()

        val dialog = fragment.onCreateDialog(null)
        dialog.show()
    }

    @Test(expected = IllegalArgumentException::class)
    fun `creating a TimePickerDialogFragment with empty title and an invalid type selection will throw an exception `() {
        val initialDate = "2018-06-12T19:30".toDate("yyyy-MM-dd'T'HH:mm")
        val minDate = "2018-06-07T00:00".toDate("yyyy-MM-dd'T'HH:mm")
        val maxDate = "2018-06-14T00:00".toDate("yyyy-MM-dd'T'HH:mm")
        val fragment =
            spy(
                TimePickerDialogFragment.newInstance(
                    "sessionId",
                    "uid",
                    true,
                    initialDate,
                    minDate,
                    maxDate,
                    -223,
                )
            )

        doReturn(appCompatContext).`when`(fragment).requireContext()

        val dialog = fragment.onCreateDialog(null)
        dialog.show()
    }

    @Test
    fun `GIVEN a max date in a zone ahead of UTC WHEN building the calendar constraints THEN the max day is valid and the day after is not`() {
        withTimeZone("Europe/Paris") {
            val validator = dateValidatorOf(maxDate = "2018-12-31".toDate("yyyy-MM-dd"))

            assertTrue(validator.isValid(epochMillisAt(2018, 12, 31)))
            assertFalse(validator.isValid(epochMillisAt(2019, 1, 1)))
        }
    }

    @Test
    fun `GIVEN a min date in a zone behind UTC WHEN building the calendar constraints THEN the min day is valid and the day before is not`() {
        withTimeZone("America/New_York") {
            val validator = dateValidatorOf(minDate = "2018-12-31".toDate("yyyy-MM-dd"))

            assertTrue(validator.isValid(epochMillisAt(2018, 12, 31)))
            assertFalse(validator.isValid(epochMillisAt(2018, 12, 30)))
        }
    }

    @Test
    fun `GIVEN a min date with a time of day WHEN building the calendar constraints THEN the min day is valid and the day before is not`() {
        withTimeZone("UTC") {
            val validator = dateValidatorOf(minDate = "2018-06-07T08:30".toDate("yyyy-MM-dd'T'HH:mm"))

            assertTrue(validator.isValid(epochMillisAt(2018, 6, 7)))
            assertFalse(validator.isValid(epochMillisAt(2018, 6, 6)))
        }
    }

    @Test
    fun `GIVEN no min or max date WHEN building the calendar constraints THEN every day is valid`() {
        withTimeZone("UTC") {
            val validator = dateValidatorOf()

            assertTrue(validator.isValid(epochMillisAt(1970, 1, 1)))
            assertTrue(validator.isValid(epochMillisAt(2100, 12, 31)))
        }
    }

    private fun dateValidatorOf(minDate: Date? = null, maxDate: Date? = null): DateValidator =
        TimePickerDialogFragment.newInstance(
                sessionId = "sessionId",
                promptRequestUID = "uid",
                shouldDismissOnLoad = false,
                initialDate = Date(0),
                minDate = minDate,
                maxDate = maxDate,
            )
            .buildCalendarConstraints()
            .dateValidator

    private val Calendar.minutes: Int
        get() = get(Calendar.MINUTE)

    private val Calendar.hour: Int
        get() = get(Calendar.HOUR_OF_DAY)
}

/** Runs [block] with the default timezone pinned to [zoneId] and restores the previous timezone afterwards. */
private inline fun <T> withTimeZone(zoneId: String, block: () -> T): T {
    val original = TimeZone.getDefault()
    TimeZone.setDefault(TimeZone.getTimeZone(zoneId))
    try {
        return block()
    } finally {
        TimeZone.setDefault(original)
    }
}
