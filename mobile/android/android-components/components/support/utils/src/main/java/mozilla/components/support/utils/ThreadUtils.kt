/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.utils

import android.os.Handler
import android.os.HandlerThread
import android.os.Looper
import mozilla.components.support.utils.ext.threadIdCompat

@Suppress("unused")
object ThreadUtils {
    private val looperBackgroundThread by lazy {
        HandlerThread("BackgroundThread")
    }
    private val looperBackgroundHandler by lazy {
        looperBackgroundThread.start()
        Handler(looperBackgroundThread.looper)
    }
    private val handler = Handler(Looper.getMainLooper())
    private val uiThread = Looper.getMainLooper().thread

    private var handlerForTest: Handler? = null

    private fun backgroundHandler(): Handler {
        return handlerForTest ?: looperBackgroundHandler
    }

    fun setHandlerForTest(handler: Handler) {
        handlerForTest = handler
    }

    fun postToBackgroundThread(runnable: Runnable) {
        backgroundHandler().post(runnable)
    }

    fun postToBackgroundThread(runnable: () -> Unit) {
        backgroundHandler().post(runnable)
    }

    fun postToMainThread(runnable: Runnable) {
        handler.post(runnable)
    }

    fun postToMainThreadDelayed(runnable: Runnable, delayMillis: Long) {
        handler.postDelayed(runnable, delayMillis)
    }

    fun assertOnUiThread() {
        val currentThread = Thread.currentThread()
        val currentThreadId = currentThread.threadIdCompat()
        val expectedThreadId = uiThread.threadIdCompat()

        if (currentThreadId == expectedThreadId) {
            return
        }

        throw IllegalThreadStateException("Expected UI thread, but running on " + currentThread.name)
    }
}
