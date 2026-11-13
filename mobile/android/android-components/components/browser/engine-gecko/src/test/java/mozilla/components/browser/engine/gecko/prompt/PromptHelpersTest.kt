/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.engine.gecko.prompt

import java.security.InvalidParameterException
import java.util.Date
import mozilla.components.concept.engine.prompt.PromptRequest.TimeSelection.Type
import mozilla.components.support.ktx.kotlin.toDate
import mozilla.components.support.test.mock
import mozilla.components.test.ReflectionUtils
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import org.mozilla.geckoview.GeckoSession.PromptDelegate.DateTimePrompt
import org.mozilla.geckoview.GeckoSession.PromptDelegate.DateTimePrompt.Type.DATE
import org.mozilla.geckoview.GeckoSession.PromptDelegate.DateTimePrompt.Type.DATETIME_LOCAL
import org.mozilla.geckoview.GeckoSession.PromptDelegate.DateTimePrompt.Type.MONTH
import org.mozilla.geckoview.GeckoSession.PromptDelegate.DateTimePrompt.Type.TIME
import org.mozilla.geckoview.GeckoSession.PromptDelegate.DateTimePrompt.Type.WEEK

class PromptHelpersTest {

    @Test
    fun `WHEN type is DATE THEN the date, format and DATE selection are returned`() {
        val result = dateTimePrompt(type = DATE, defaultValue = "2025-01-02").dateAndSelectionType()

        assertEquals(Triple("2025-01-02".toDate("yyyy-MM-dd"), "yyyy-MM-dd", Type.DATE), result)
    }

    @Test
    fun `WHEN type is WEEK THEN the date, format and DATE selection are returned`() {
        val result = dateTimePrompt(type = WEEK, defaultValue = "2025-W05").dateAndSelectionType()

        assertEquals(Triple("2025-W05".toDate("yyyy-'W'ww"), "yyyy-'W'ww", Type.DATE), result)
    }

    @Test
    fun `WHEN type is MONTH THEN the date, format and MONTH selection are returned`() {
        val result = dateTimePrompt(type = MONTH, defaultValue = "2025-01").dateAndSelectionType()

        assertEquals(Triple("2025-01".toDate("yyyy-MM"), "yyyy-MM", Type.MONTH), result)
    }

    @Test
    fun `WHEN type is DATETIME_LOCAL THEN the date, format and DATE_AND_TIME selection are returned`() {
        val result = dateTimePrompt(type = DATETIME_LOCAL, defaultValue = "2025-01-02T12:30").dateAndSelectionType()

        assertEquals(
            Triple("2025-01-02T12:30".toDate("yyyy-MM-dd'T'HH:mm"), "yyyy-MM-dd'T'HH:mm", Type.DATE_AND_TIME),
            result,
        )
    }

    @Test
    fun `WHEN type is TIME without a seconds stepValue THEN the minutes format is returned`() {
        val expected = Triple("12:30".toDate("HH:mm"), "HH:mm", Type.TIME)

        listOf("", "60", "any").forEach { stepValue ->
            val result =
                dateTimePrompt(type = TIME, defaultValue = "12:30", stepValue = stepValue).dateAndSelectionType()

            assertEquals("stepValue=$stepValue", expected, result)
        }
    }

    @Test
    fun `WHEN type is TIME with a seconds stepValue THEN the seconds format is returned for any defaultValue precision`() {
        assertEquals(
            Triple("12:00".toDate("HH:mm"), "HH:mm:ss", Type.TIME),
            dateTimePrompt(type = TIME, defaultValue = "12:00", stepValue = "1").dateAndSelectionType(),
        )
        assertEquals(
            Triple("12:00:30".toDate("HH:mm:ss"), "HH:mm:ss", Type.TIME),
            dateTimePrompt(type = TIME, defaultValue = "12:00:30", stepValue = "1").dateAndSelectionType(),
        )
    }

    @Test
    fun `WHEN type is TIME with a milliseconds stepValue THEN the milliseconds format is returned for any defaultValue precision`() {
        assertEquals(
            Triple("12:00".toDate("HH:mm"), "HH:mm:ss.SSS", Type.TIME),
            dateTimePrompt(type = TIME, defaultValue = "12:00", stepValue = "0.1").dateAndSelectionType(),
        )
        assertEquals(
            Triple("12:00:30".toDate("HH:mm:ss"), "HH:mm:ss.SSS", Type.TIME),
            dateTimePrompt(type = TIME, defaultValue = "12:00:30", stepValue = "0.1").dateAndSelectionType(),
        )
        assertEquals(
            Triple("12:00:30.250".toDate("HH:mm:ss.SSS"), "HH:mm:ss.SSS", Type.TIME),
            dateTimePrompt(type = TIME, defaultValue = "12:00:30.250", stepValue = "0.1").dateAndSelectionType(),
        )
    }

    @Test
    fun `WHEN defaultValue is empty or not valid THEN the current date and the most precise format are returned`() {
        listOf(null, "", "not a date").forEach { defaultValue ->
            val before = Date()
            val (date, format, type) =
                dateTimePrompt(type = TIME, defaultValue = defaultValue, stepValue = "1").dateAndSelectionType()
            val after = Date()

            assertTrue("defaultValue=$defaultValue", !date.before(before) && !date.after(after))
            assertEquals("HH:mm:ss", format)
            assertEquals(Type.TIME, type)
        }
    }

    @Test
    fun `WHEN type is not valid THEN an InvalidParameterException is thrown`() {
        assertThrows(InvalidParameterException::class.java) {
            dateTimePrompt(type = 42).dateAndSelectionType()
        }
    }

    private fun DateTimePrompt.dateAndSelectionType(): Triple<Date, String, Type> =
        context(validStepValue()) {
            toDateAndSelectionType()
        }

    private fun dateTimePrompt(
        type: Int,
        defaultValue: String? = "",
        stepValue: String? = "",
    ): DateTimePrompt {
        val prompt: DateTimePrompt = mock()
        ReflectionUtils.setField(prompt, "type", type)
        ReflectionUtils.setField(prompt, "defaultValue", defaultValue)
        ReflectionUtils.setField(prompt, "stepValue", stepValue)
        return prompt
    }
}
