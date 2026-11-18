/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.media.middleware

import android.app.Application
import android.content.Intent
import android.os.Binder
import androidx.test.ext.junit.runners.AndroidJUnit4
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import kotlin.time.Duration.Companion.minutes
import kotlinx.coroutines.test.runTest
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Shadows

@RunWith(AndroidJUnit4::class)
class NotificationDismissedReceiverTest {
    private val shadowApp = Shadows.shadowOf(testContext.applicationContext as Application)

    private fun registeredCount() =
        shadowApp.registeredReceivers.count {
            it.intentFilter.hasAction(ACTION_RECORDING_DEVICES_NOTIFICATION_DISMISSED)
        }

    @Test
    fun `register is idempotent`() = runTest {
        val receiver = NotificationDismissedReceiver(testContext, this) {}

        repeat(5) { receiver.register() }

        assertEquals(1, registeredCount())
    }

    @Test
    fun `unregister without register is a no-op`() = runTest {
        val receiver = NotificationDismissedReceiver(testContext, this) {}

        receiver.unregister()

        assertEquals(0, registeredCount())
    }

    @Test
    fun `register can be called again after unregister`() = runTest {
        val receiver = NotificationDismissedReceiver(testContext, this) {}

        receiver.register()
        receiver.unregister()
        receiver.register()

        assertEquals(1, registeredCount())
    }

    @Test
    fun `concurrent register calls register the receiver only once`() = runTest {
        val receiver = NotificationDismissedReceiver(testContext, this) {}
        val threads = 16
        val executor = Executors.newFixedThreadPool(threads)
        val ready = CountDownLatch(threads)
        val start = CountDownLatch(1)

        repeat(threads) {
            executor.execute {
                ready.countDown()
                start.await()
                receiver.register()
            }
        }
        ready.await()
        start.countDown()
        executor.shutdown()
        executor.awaitTermination(10, TimeUnit.SECONDS)

        assertEquals(1, registeredCount())
    }

    @Test
    fun `concurrent register and unregister leave state consistent`() = runTest {
        val receiver = NotificationDismissedReceiver(testContext, this) {}
        val threads = 8
        val executor = Executors.newFixedThreadPool(threads)
        val start = CountDownLatch(1)

        repeat(threads) { index ->
            executor.execute {
                start.await()
                repeat(200) {
                    if (index % 2 == 0) receiver.register() else receiver.unregister()
                }
            }
        }
        start.countDown()
        executor.shutdown()
        executor.awaitTermination(10, TimeUnit.SECONDS)

        receiver.unregister()
        assertEquals(0, registeredCount())
    }

    @Test
    fun `callback runs once after the reminder delay when dismissed repeatedly`() = runTest {
        Shadows.shadowOf(testContext.packageManager).setNameForUid(Binder.getCallingUid(), testContext.packageName)
        var calls = 0
        val receiver = NotificationDismissedReceiver(testContext, this) { calls++ }
        receiver.register()

        receiver.onReceive(testContext, Intent())
        testScheduler.advanceTimeBy(4.minutes)
        receiver.onReceive(testContext, Intent())
        testScheduler.advanceTimeBy(4.minutes)
        assertEquals(0, calls)

        testScheduler.advanceTimeBy(2.minutes)
        assertEquals(1, calls)
    }

    @Test
    fun `unregister cancels the pending reminder`() = runTest {
        Shadows.shadowOf(testContext.packageManager).setNameForUid(Binder.getCallingUid(), testContext.packageName)
        var calls = 0
        val receiver = NotificationDismissedReceiver(testContext, this) { calls++ }
        receiver.register()

        receiver.onReceive(testContext, Intent())
        receiver.unregister()
        testScheduler.advanceTimeBy(10.minutes)

        assertEquals(0, calls)
    }

    @Test
    fun `dismissal received after unregister does not schedule a reminder`() = runTest {
        Shadows.shadowOf(testContext.packageManager).setNameForUid(Binder.getCallingUid(), testContext.packageName)
        var calls = 0
        val receiver = NotificationDismissedReceiver(testContext, this) { calls++ }
        receiver.register()
        receiver.unregister()

        receiver.onReceive(testContext, Intent())
        testScheduler.advanceTimeBy(10.minutes)

        assertEquals(0, calls)
    }
}
