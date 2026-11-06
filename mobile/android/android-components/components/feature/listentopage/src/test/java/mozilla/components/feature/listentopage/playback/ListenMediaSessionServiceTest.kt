/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.playback

import android.app.Activity
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.os.Bundle
import android.os.Looper
import android.os.Process
import android.view.KeyEvent
import androidx.annotation.OptIn
import androidx.core.app.NotificationCompat
import androidx.media3.common.MediaItem
import androidx.media3.common.MediaLibraryInfo
import androidx.media3.common.PlaybackParameters
import androidx.media3.common.Player
import androidx.media3.common.util.UnstableApi
import androidx.media3.session.CommandButton
import androidx.media3.session.DefaultMediaNotificationProvider
import androidx.media3.session.MediaNotification
import androidx.media3.session.MediaSession
import androidx.media3.session.SessionCommand
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.feature.listentopage.PlaybackSpeed
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.whenever
import mozilla.components.ui.icons.R as iconsR
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.Shadows.shadowOf
import org.robolectric.android.controller.ServiceController

@RunWith(AndroidJUnit4::class)
class ListenMediaSessionServiceTest {

    // Every service a test started, each holding a session. A session left open keeps its id, which no session built
    // later in the same process can then take.
    private val services = mutableListOf<ServiceController<ListenMediaSessionService>>()

    @After
    fun tearDown() {
        services.forEach { runCatching { it.destroy() } }
        shadowOf(Looper.getMainLooper()).idle()
    }

    @Test
    fun `test that swiping the app out of the recents list ends the service`() {
        val service = startService()

        service.onTaskRemoved(null)

        assertTrue(shadowOf(service).isStoppedBySelf)
    }

    //  Test to cover scenario where Listen to page is opened from a shortcut - bug 2086312
    @OptIn(UnstableApi::class)
    @Test
    fun `test that removing a task excluded from recents leaves the reading playing`() {
        val service = startReading()

        service.onTaskRemoved(taskExcludedFromRecents())

        assertFalse(shadowOf(service).isStoppedBySelf)
        assertTrue(service.isPlaybackOngoing)
    }

    //  Test to cover scenario where Listen to page is opened from a shortcut - bug 2086312
    @Test
    fun `test that removing a task excluded from recents ends the service when nothing is playing`() {
        val service = startService()

        service.onTaskRemoved(null)

        assertTrue(shadowOf(service).isStoppedBySelf)
    }

    @Test
    fun `test that the service ends rather than come back from being killed with nothing to play`() {
        val service = startService()

        val restart = service.onStartCommand(null, 0, 1)

        assertEquals(Service.START_NOT_STICKY, restart)
        assertTrue(shadowOf(service).isStoppedBySelf)
    }

    @Test
    fun `test that dismissing the notification ends the service`() {
        val service = startService()
        val dismissal = mediaButton().putExtra(MediaNotification.NOTIFICATION_DISMISSED_EVENT_KEY, true)

        service.onStartCommand(dismissal, 0, 1)

        assertTrue(shadowOf(service).isStoppedBySelf)
    }

    @Test
    fun `test that a notification control leaves the service running`() {
        val service = startService()

        service.onStartCommand(mediaButton(), 0, 1)

        assertFalse(shadowOf(service).isStoppedBySelf)
    }

    @OptIn(UnstableApi::class)
    @Test
    fun `test that a notification update landing after the playback ended takes the notification away`() {
        val service = startService()
        val session = checkNotNull(service.onGetSession(mock()))
        val notificationManager = testContext.getSystemService(NotificationManager::class.java)
        service.onTaskRemoved(null)

        val notification =
            NotificationCompat.Builder(testContext, DefaultMediaNotificationProvider.DEFAULT_CHANNEL_ID)
                .setSmallIcon(iconsR.drawable.mozac_ic_logo_firefox_24)
                .build()
        notificationManager.notify(LISTEN_NOTIFICATION_ID, notification)
        service.onUpdateNotificationAsync(session, false)

        assertTrue(shadowOf(notificationManager).allNotifications.isEmpty())
    }

    @OptIn(UnstableApi::class)
    @Test
    fun `test that the notification takes the user to the tab being read`() {
        val session = startSession()
        val openTab = openTabPendingIntent()

        ListenSessionCallback(session.player as SessionPlayer, ownPackage = testContext.packageName)
            .onCustomCommand(
                session,
                controllerFrom(testContext.packageName),
                SessionCommand(COMMAND_SET_SESSION_ACTIVITY, Bundle.EMPTY),
                openTab.toSessionActivityArgs(),
            )

        assertEquals(openTab, session.sessionActivity)
    }

    @Test
    fun `test that the delete intent of the notification is read as a dismissal`() {
        val dismissal = mediaButton().putExtra(MediaNotification.NOTIFICATION_DISMISSED_EVENT_KEY, true)

        assertTrue(dismissal.isNotificationDismissal())
    }

    @Test
    fun `test that the notification skips by time rather than by chunk`() {
        val controls = notificationControls(testContext.resources, PlaybackSpeed.Default)

        assertEquals(
            listOf(Player.COMMAND_SEEK_BACK, Player.COMMAND_SEEK_FORWARD),
            controls.take(2).map { it.playerCommand },
        )
    }

    @Test
    fun `test that the skip controls say and show how far they move`() {
        val controls = notificationControls(testContext.resources, PlaybackSpeed.Default)

        assertEquals("Back 10 seconds", controls.first().displayName.toString())
        assertEquals(CommandButton.ICON_SKIP_BACK_10, controls.first().icon)
        assertEquals("Forward 30 seconds", controls[1].displayName.toString())
        assertEquals(CommandButton.ICON_SKIP_FORWARD_30, controls[1].icon)
    }

    @OptIn(UnstableApi::class)
    @Test
    fun `test that the speed control sits right of the forward control`() {
        val speed = notificationControls(testContext.resources, PlaybackSpeed.Default).last()

        assertEquals(ACTION_CYCLE_SPEED, speed.sessionCommand?.customAction)
        assertEquals(
            listOf(CommandButton.SLOT_FORWARD_SECONDARY, CommandButton.SLOT_OVERFLOW),
            speed.slots.asList(),
        )
    }

    @Test
    fun `test that the speed control says and shows the speed that is on`() {
        val speed = notificationControls(testContext.resources, PlaybackSpeed.X1_5).last()

        assertEquals("Playback rate 50 percent faster", speed.displayName.toString())
        assertEquals(iconsR.drawable.mozac_ic_playback_speed_1_5x_24, speed.iconResId)
    }

    @Test
    fun `test that a speed too precise for its icon is still said in full`() {
        val speed = notificationControls(testContext.resources, PlaybackSpeed.X1_25).last()

        assertEquals("Playback rate 25 percent faster", speed.displayName.toString())
        assertEquals(iconsR.drawable.mozac_ic_playback_speed_1_2x_24, speed.iconResId)
    }

    @Test
    fun `test that the speed control shows our icons rather than those of media3`() {
        val speed = notificationControls(testContext.resources, PlaybackSpeed.Default).last()

        assertEquals(CommandButton.ICON_UNDEFINED, speed.icon)
    }

    @Test
    fun `test that the speed of the voice itself is said in words rather than as a multiple`() {
        val speed = notificationControls(testContext.resources, PlaybackSpeed.X1).last()

        assertEquals("Playback speed normal", speed.displayName.toString())
    }

    @Test
    fun `test that every speed is said as itself`() {
        val said =
            PlaybackSpeed.entries.map { speed ->
                notificationControls(testContext.resources, speed).last().displayName.toString()
            }

        assertEquals(PlaybackSpeed.entries.size, said.toSet().size)
    }

    @OptIn(UnstableApi::class)
    @Test
    fun `test that the speed control shows a speed set from outside the notification`() {
        val session = startSession()

        session.player.setPlaybackSpeed(PlaybackSpeed.X0_5.multiplier)
        shadowOf(Looper.getMainLooper()).idle()

        val speed = session.mediaButtonPreferences.last()
        assertEquals("Playback set to half speed", speed.displayName.toString())
        assertEquals(iconsR.drawable.mozac_ic_playback_speed_0_5x_24, speed.iconResId)
    }

    @Test
    fun `test that the speed control steps the player on from whatever speed it is at`() {
        assertEquals(PlaybackSpeed.X1_5, playerAt(1.3f).nextSpeed())
    }

    @Test
    fun `test that the speed control wraps round to the slowest speed`() {
        assertEquals(PlaybackSpeed.X0_25, playerAt(2f).nextSpeed())
    }

    private fun startService(): ListenMediaSessionService =
        Robolectric.buildService(ListenMediaSessionService::class.java).create().also { services.add(it) }.get()

    @OptIn(UnstableApi::class)
    private fun startSession(): MediaSession = checkNotNull(startService().onGetSession(mock()))

    /**
     * A service that media3 has put in the foreground because its session is reading. The session is added the way a
     * controller binding to the service adds it, and the player is held buffering a seek so that it counts as playing
     * without any audio to load.
     */
    @OptIn(UnstableApi::class)
    private fun startReading(): ListenMediaSessionService {
        val service = startService()
        val session = checkNotNull(service.onGetSession(mock()))
        service.addSession(session)
        val player = session.player as SessionPlayer

        player.setMediaItem(MediaItem.fromUri("file:///chunk"))
        player.playWhenReady = true
        player.bufferAt(0)
        shadowOf(Looper.getMainLooper()).idle()

        check(service.isPlaybackOngoing) { "The reading did not bring the service to the foreground" }
        return service
    }

    /**
     * The task of an activity with `excludeFromRecents`, such as the one a launcher shortcut opens the app through,
     * which Android drops from the recents list once another task is in front of it.
     */
    private fun taskExcludedFromRecents() =
        Intent(Intent.ACTION_VIEW).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_EXCLUDE_FROM_RECENTS)

    private fun playerAt(speed: Float): Player = mock {
        whenever(this.playbackParameters).thenReturn(PlaybackParameters(speed))
    }

    private fun mediaButton() =
        Intent(Intent.ACTION_MEDIA_BUTTON)
            .putExtra(Intent.EXTRA_KEY_EVENT, KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_MEDIA_STOP))

    // Media3 rejects a session activity that does not start one, so this stands in for whatever the app opens a tab
    // with.
    private fun openTabPendingIntent(): PendingIntent =
        PendingIntent.getActivity(
            testContext,
            0,
            Intent(testContext, Activity::class.java),
            PendingIntent.FLAG_IMMUTABLE,
        )

    @OptIn(UnstableApi::class)
    private fun controllerFrom(packageName: String): MediaSession.ControllerInfo =
        MediaSession.ControllerInfo.createTestOnlyControllerInfo(
            packageName,
            Process.myPid(),
            Process.myUid(),
            MediaLibraryInfo.VERSION_INT,
            MediaLibraryInfo.VERSION_INT,
            /* trusted = */ true,
            Bundle.EMPTY,
            /* isPackageNameVerified = */ true,
        )
}
