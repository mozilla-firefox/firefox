/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.state.internal

import android.os.Handler
import android.os.Looper
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineExceptionHandler
import kotlinx.coroutines.asCoroutineDispatcher
import kotlinx.coroutines.cancel
import mozilla.components.lib.state.Action
import mozilla.components.lib.state.Store
import mozilla.components.lib.state.StoreException
import mozilla.components.support.utils.ext.threadIdCompat
import java.util.concurrent.Executors
import java.util.concurrent.ThreadFactory

/**
 * The default [StoreDispatcher] used in a [Store]. It uses a single thread for dispatching and
 * processing [Action]s.
 */
internal class DefaultStoreDispatcher(
    threadNamePrefix: String? = null,
) : StoreDispatcher {
    private val storeThreadFactory = StoreThreadFactory(threadNamePrefix)
    private val threadFactory: ThreadFactory = storeThreadFactory
    private val exceptionHandler = CoroutineExceptionHandler { coroutineContext, throwable ->
        // We want exceptions in the reducer to crash the app and not get silently ignored. Therefore we rethrow the
        // exception on the main thread.
        Handler(Looper.getMainLooper()).postAtFrontOfQueue {
            throw StoreException("Exception while reducing state", throwable)
        }

        // Once an exception happened we do not want to accept any further actions. So let's cancel the scope which
        // will cancel all jobs and not accept any new ones.
        coroutineContext.cancel()
    }

    private val dispatcher: CoroutineDispatcher by lazy {
        Executors.newSingleThreadExecutor(
            threadFactory,
        ).asCoroutineDispatcher()
    }

    /**
     * See [StoreDispatcher.coroutineContext].
     */
    override val coroutineContext = dispatcher + exceptionHandler

    /**
     * See [StoreDispatcher.assertOnThread].
     */
    override fun assertOnThread() {
        val currentThread = Thread.currentThread()
        val currentThreadId = currentThread.threadIdCompat()
        val expectedThreadId = storeThreadFactory.threadId

        if (currentThreadId == expectedThreadId) {
            return
        }

        throw IllegalThreadStateException(
            "Expected `store` thread, but running on thread `${currentThread.name}`. " +
                "Leaked MiddlewareContext or did you mean to use `MiddlewareContext.store.dispatch`?",
        )
    }
}
