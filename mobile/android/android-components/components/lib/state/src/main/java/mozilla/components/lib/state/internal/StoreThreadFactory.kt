/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.state.internal

import mozilla.components.lib.state.Store
import mozilla.components.support.base.utils.NamedThreadFactory
import mozilla.components.support.utils.ext.threadIdCompat
import java.util.concurrent.Executors
import java.util.concurrent.ThreadFactory

/**
 * Custom [ThreadFactory] implementation wrapping [Executors.defaultThreadFactory]/[NamedThreadFactory]
 * that allows asserting whether a caller is on the created thread.
 *
 * For usage with [Executors.newSingleThreadExecutor]: Only the last created thread is kept and
 * compared when [StoreDispatcher.assertOnThread] is called with [threadId].
 *
 * @param threadNamePrefix Optional prefix with which to name threads for the [Store]. If not provided,
 * the naming scheme will be deferred to [Executors.defaultThreadFactory]
 */
internal class StoreThreadFactory(
    threadNamePrefix: String?,
) : ThreadFactory {
    @Volatile
    private var thread: Thread? = null

    private val actualFactory = if (threadNamePrefix != null) {
        NamedThreadFactory(threadNamePrefix)
    } else {
        Executors.defaultThreadFactory()
    }

    val threadId: Long?
        get() = thread?.threadIdCompat()

    override fun newThread(r: Runnable): Thread {
        return actualFactory.newThread(r).also {
            thread = it
        }
    }
}
