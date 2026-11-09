/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.test

import android.os.Handler
import android.os.Looper
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import org.junit.Assert.assertEquals
import org.junit.Assert.assertSame
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/**
 * N.B: These tests use [Dispatchers.Unconfined] as opposed to typically using `StandardTestDispatcher` because we don't
 * want to manually advance the dispatcher when the main looper call in the function should be doing this.
 */
@RunWith(AndroidJUnit4::class)
class AssertionsTest {

    @Test
    fun `assertNoUncaughtException runs the block and passes when nothing is thrown`() {
        var ran = false

        assertNoUncaughtException { ran = true }

        assertTrue(ran)
    }

    @Test
    fun `assertNoUncaughtException fails when a coroutine throws an uncaught exception`() {
        val exception = IllegalStateException("boom")

        val error =
            assertThrows(AssertionError::class.java) {
                assertNoUncaughtException {
                    CoroutineScope(Dispatchers.Unconfined).launch { throw exception }
                }
            }

        assertSame(exception, error.cause)
    }

    @Test
    fun `assertNoUncaughtException drains the main looper before it checks for exceptions`() {
        val exception = IllegalStateException("boom")

        val error =
            assertThrows(AssertionError::class.java) {
                assertNoUncaughtException {
                    Handler(Looper.getMainLooper()).post {
                        CoroutineScope(Dispatchers.Unconfined).launch { throw exception }
                    }
                }
            }

        assertSame(exception, error.cause)
    }

    @Test
    fun `assertNoUncaughtException does not wrap an exception thrown directly by the block`() {
        val exception = IllegalStateException("boom")

        val thrown =
            assertThrows(IllegalStateException::class.java) {
                assertNoUncaughtException { throw exception }
            }

        assertSame(exception, thrown)
    }

    @Test
    fun `assertNoUncaughtException restores the previous handler`() {
        val thread = Thread.currentThread()
        val originalHandler = thread.uncaughtExceptionHandler
        val previousHandler = Thread.UncaughtExceptionHandler { _, _ -> }
        thread.uncaughtExceptionHandler = previousHandler

        try {
            assertNoUncaughtException {}
            assertEquals(previousHandler, thread.uncaughtExceptionHandler)

            assertThrows(AssertionError::class.java) {
                assertNoUncaughtException {
                    CoroutineScope(Dispatchers.Unconfined).launch { throw IllegalStateException() }
                }
            }
            assertEquals(previousHandler, thread.uncaughtExceptionHandler)

            assertThrows(IllegalStateException::class.java) {
                assertNoUncaughtException { throw IllegalStateException() }
            }
            assertEquals(previousHandler, thread.uncaughtExceptionHandler)
        } finally {
            thread.uncaughtExceptionHandler = originalHandler
        }
    }
}
