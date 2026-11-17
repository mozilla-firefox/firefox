/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.perf

import android.os.strictmode.Violation
import io.mockk.MockKAnnotations
import io.mockk.every
import io.mockk.impl.annotations.MockK
import io.mockk.impl.annotations.RelaxedMockK
import io.mockk.verify
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.utils.ManufacturerChecker
import org.junit.Before
import org.junit.Test
import org.mozilla.fenix.helpers.StackTraces

class ThreadPenaltyDeathWithIgnoresListenerTest {

    @RelaxedMockK private lateinit var logger: Logger
    private lateinit var listener: ThreadPenaltyDeathWithIgnoresListener

    @RelaxedMockK private lateinit var mockManufacturerChecker: ManufacturerChecker

    @MockK private lateinit var violation: Violation
    private lateinit var stackTrace: Array<StackTraceElement>

    @Before
    fun setUp() {
        MockKAnnotations.init(this)

        listener = ThreadPenaltyDeathWithIgnoresListener(logger, mockManufacturerChecker)

        stackTrace = emptyArray()
        every { violation.stackTrace } answers { stackTrace }
    }

    @Test(expected = RuntimeException::class)
    fun `WHEN provided an arbitrary violation that does not conflict with ignores THEN we throw an exception`() {
        stackTrace = Exception().stackTrace
        listener.onThreadViolation(violation)
    }

    @Test
    fun `GIVEN we're on a Samsung WHEN provided an IdsController violation THEN it will be ignored and logged`() {
        every { mockManufacturerChecker.isSamsung() } returns true

        every { violation.stackTrace } returns getIdsControllerStackTrace()
        listener.onThreadViolation(violation)

        verify { logger.debug("Ignoring StrictMode ThreadPolicy violation", violation) }
    }

    @Test(expected = RuntimeException::class)
    fun `GIVEN we're not on a Samsung WHEN provided an IdsController violation THEN we throw an exception`() {
        every { mockManufacturerChecker.isSamsung() } returns false

        every { violation.stackTrace } returns getIdsControllerStackTrace()
        listener.onThreadViolation(violation)
    }

    @Test
    fun `GIVEN we're on a Samsung WHEN provided a FreecessController violation THEN it will be ignored and logged`() {
        every { mockManufacturerChecker.isSamsung() } returns true

        every { violation.stackTrace } returns getFreecessControllerStackTrace()
        listener.onThreadViolation(violation)

        verify { logger.debug("Ignoring StrictMode ThreadPolicy violation", violation) }
    }

    @Test(expected = RuntimeException::class)
    fun `GIVEN we're not on a Samsung WHEN provided a FreecessController violation THEN we throw an exception`() {
        every { mockManufacturerChecker.isSamsung() } returns false

        every { violation.stackTrace } returns getFreecessControllerStackTrace()
        listener.onThreadViolation(violation)
    }

    @Test
    fun `GIVEN we're on a Samsung WHEN provided the EdmStorageProvider violation THEN it will be ignored and logged`() {
        every { mockManufacturerChecker.isSamsung() } returns true

        every { violation.stackTrace } returns getEdmStorageProviderStackTrace()
        listener.onThreadViolation(violation)

        verify { logger.debug("Ignoring StrictMode ThreadPolicy violation", violation) }
    }

    @Test
    fun `GIVEN we're on an LG WHEN provided the EdmStorageProvider violation THEN it will be ignored and logged`() {
        every { mockManufacturerChecker.isLG() } returns true

        every { violation.stackTrace } returns getEdmStorageProviderStackTrace()
        listener.onThreadViolation(violation)

        verify { logger.debug("Ignoring StrictMode ThreadPolicy violation", violation) }
    }

    @Test(expected = RuntimeException::class)
    fun `GIVEN we're not on a Samsung or LG WHEN provided the EdmStorageProvider violation THEN we throw an exception`() {
        every { mockManufacturerChecker.isSamsung() } returns false
        every { mockManufacturerChecker.isLG() } returns false

        every { violation.stackTrace } returns getEdmStorageProviderStackTrace()
        listener.onThreadViolation(violation)
    }

    @Test
    fun `WHEN provided the InstrumentationHooks violation THEN it will be ignored and logged`() {
        every { violation.stackTrace } returns getInstrumentationHooksStackTrace()
        listener.onThreadViolation(violation)

        verify { logger.debug("Ignoring StrictMode ThreadPolicy violation", violation) }
    }

    @Test
    fun `GIVEN we're on a Xiaomi WHEN provided the MultiLangHelper violation THEN it will be ignored and logged`() {
        every { mockManufacturerChecker.isXiaomi() } returns true

        every { violation.stackTrace } returns getXiaomiMultiLangHelperStackTrace()
        listener.onThreadViolation(violation)

        verify { logger.debug("Ignoring StrictMode ThreadPolicy violation", violation) }
    }

    @Test(expected = RuntimeException::class)
    fun `GIVEN we're not on a Xiaomi WHEN provided the MultiLangHelper violation THEN we throw an exception`() {
        every { mockManufacturerChecker.isXiaomi() } returns false

        every { violation.stackTrace } returns getXiaomiMultiLangHelperStackTrace()
        listener.onThreadViolation(violation)
    }

    @Test
    fun `WHEN violation is null THEN we don't throw an exception`() {
        listener.onThreadViolation(null)
    }

    private fun getIdsControllerStackTrace() = StackTraces.getStackTraceFromLogcat("IdsControllerLogcat.txt")

    private fun getFreecessControllerStackTrace() = StackTraces.getStackTraceFromLogcat("FreecessControllerLogcat.txt")

    private fun getEdmStorageProviderStackTrace() =
        StackTraces.getStackTraceFromLogcat("EdmStorageProviderBaseLogcat.txt")

    private fun getInstrumentationHooksStackTrace() =
        StackTraces.getStackTraceFromLogcat("InstrumentationHooksLogcat.txt")

    private fun getXiaomiMultiLangHelperStackTrace() =
        StackTraces.getStackTraceFromLogcat("XiaomiMultiLangHelperLogcat.txt")
}
