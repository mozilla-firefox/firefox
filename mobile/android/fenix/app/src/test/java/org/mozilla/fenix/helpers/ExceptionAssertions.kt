/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.helpers

import android.os.Looper
import org.robolectric.Shadows.shadowOf

/** A collection of test helper functions for asserting on caught or uncaught exceptions. */
object ExceptionAssertions {
    /**
     * Runs the code in [block], drains the main looper, and fails if any exception reached the current thread's
     * [Thread.UncaughtExceptionHandler].
     *
     * Exceptions thrown inside a coroutine are delivered to that handler instead of the caller. In production that
     * crashes the app, but under Robolectric it is only logged, so a test that calls the code under test passes even
     * when the code would crash in production.
     *
     * Usage:
     *
     * assertNoUncaughtException { methodUnderTest() }
     */
    fun assertNoUncaughtException(block: () -> Unit) {
        val thread = Thread.currentThread()
        val previousHandler = thread.uncaughtExceptionHandler
        var uncaught: Throwable? = null
        thread.uncaughtExceptionHandler = Thread.UncaughtExceptionHandler { _, e -> uncaught = uncaught ?: e }

        try {
            block()
            shadowOf(Looper.getMainLooper()).idle()
        } finally {
            thread.uncaughtExceptionHandler = previousHandler
        }

        uncaught?.let { throw AssertionError("Uncaught exception in code under test", it) }
    }
}
