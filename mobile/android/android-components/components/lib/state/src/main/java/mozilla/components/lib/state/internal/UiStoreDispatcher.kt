/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.state.internal

import android.os.Looper
import kotlinx.coroutines.Dispatchers
import mozilla.components.lib.state.Action
import mozilla.components.lib.state.UiStore
import mozilla.components.support.utils.ext.threadIdCompat
import kotlin.coroutines.CoroutineContext

/**
 * The default [StoreDispatcher] used in a [UiStore]. It uses the [Dispatchers.Main] thread for
 * dispatching and processing [Action]s.
 */
internal class UiStoreDispatcher : StoreDispatcher {

    /**
     * See [StoreDispatcher.coroutineContext].
     */
    override val coroutineContext: CoroutineContext = Dispatchers.Main.immediate

    /**
     * See [StoreDispatcher.assertOnThread].
     */
    override fun assertOnThread() {
        val currentThread = Thread.currentThread()
        val expectedThreadId = Looper.getMainLooper().thread.threadIdCompat()

        if (currentThread.threadIdCompat() == expectedThreadId) {
            return
        }

        throw IllegalThreadStateException(
            "Expected Main thread, but running on thread `${currentThread.name}`. " +
                "Leaked MiddlewareContext or did you mean to use `MiddlewareContext.store.dispatch`?",
        )
    }
}
