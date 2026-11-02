/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.media.notification

import android.app.Notification
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Bitmap
import androidx.annotation.OptIn
import androidx.core.app.NotificationCompat
import androidx.media3.common.util.UnstableApi
import androidx.media3.session.MediaSession
import androidx.test.ext.junit.runners.AndroidJUnit4
import java.util.UUID
import kotlin.test.assertNotNull
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.MediaSessionState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.mediasession.MediaSession as MozMediaSession
import mozilla.components.feature.media.R
import mozilla.components.feature.media.player.BrowserStorePlayer
import mozilla.components.feature.media.service.AbstractMediaSessionService
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.whenever
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.ArgumentMatchers
import org.mockito.Mockito.doReturn
import org.mockito.Mockito.spy

@OptIn(UnstableApi::class)
@RunWith(AndroidJUnit4::class)
class MediaNotificationTest {
    private lateinit var context: Context
    private lateinit var player: BrowserStorePlayer
    private lateinit var mediaSession: MediaSession

    @Before
    fun setUp() {
        context =
            spy(testContext).also {
                val packageManager: PackageManager = mock()
                doReturn(Intent()).`when`(packageManager).getLaunchIntentForPackage(ArgumentMatchers.anyString())
                doReturn(packageManager).`when`(it).packageManager
            }
        player = BrowserStorePlayer(context, BrowserStore())
        mediaSession = MediaSession.Builder(context, player).setId("MediaNotificationTest-${UUID.randomUUID()}").build()
    }

    @After
    fun tearDown() {
        mediaSession.release()
        player.release()
    }

    @Test
    fun `media session notification for playing state`() = runTest {
        val state =
            BrowserState(
                tabs =
                    listOf(
                        createTab(
                            "https://www.mozilla.org",
                            id = "test-tab",
                            title = "Mozilla",
                            mediaSessionState =
                                MediaSessionState(mock(), playbackState = MozMediaSession.PlaybackState.PLAYING),
                        )
                    )
            )

        val notification =
            MediaNotification(context, AbstractMediaSessionService::class.java)
                .create(state.tabs[0], mediaSession, null)

        assertEquals("https://www.mozilla.org", notification.text)
        assertEquals("Mozilla", notification.title)
        assertEquals(R.drawable.mozac_feature_media_playing, notification.iconResource)
    }

    @Test
    fun `media session notification for paused state`() = runTest {
        val state =
            BrowserState(
                tabs =
                    listOf(
                        createTab(
                            "https://www.mozilla.org",
                            id = "test-tab",
                            title = "Mozilla",
                            mediaSessionState =
                                MediaSessionState(mock(), playbackState = MozMediaSession.PlaybackState.PAUSED),
                        )
                    )
            )

        val notification =
            MediaNotification(context, AbstractMediaSessionService::class.java)
                .create(state.tabs[0], mediaSession, null)

        assertEquals("https://www.mozilla.org", notification.text)
        assertEquals("Mozilla", notification.title)
        assertEquals(R.drawable.mozac_feature_media_paused, notification.iconResource)
    }

    @Test
    fun `media session notification for stopped state`() = runTest {
        val state =
            BrowserState(
                tabs =
                    listOf(
                        createTab(
                            "https://www.mozilla.org",
                            id = "test-tab",
                            title = "Mozilla",
                            mediaSessionState =
                                MediaSessionState(mock(), playbackState = MozMediaSession.PlaybackState.STOPPED),
                        )
                    )
            )

        val notification =
            MediaNotification(context, AbstractMediaSessionService::class.java)
                .create(state.tabs[0], mediaSession, null)

        assertEquals("", notification.text)
        assertEquals("", notification.title)
    }

    @Test
    fun `media session notification for playing state in private mode`() = runTest {
        val state =
            BrowserState(
                tabs =
                    listOf(
                        createTab(
                            "https://www.mozilla.org",
                            id = "test-tab",
                            title = "Mozilla",
                            private = true,
                            mediaSessionState =
                                MediaSessionState(mock(), playbackState = MozMediaSession.PlaybackState.PLAYING),
                        )
                    )
            )

        val notification =
            MediaNotification(context, AbstractMediaSessionService::class.java)
                .create(state.tabs[0], mediaSession, null)

        assertEquals("", notification.text)
        assertEquals("A site is playing media", notification.title)
        assertEquals(R.drawable.mozac_feature_media_playing, notification.iconResource)
    }

    @Test
    fun `media session notification for paused state in private mode`() = runTest {
        val state =
            BrowserState(
                tabs =
                    listOf(
                        createTab(
                            "https://www.mozilla.org",
                            id = "test-tab",
                            title = "Mozilla",
                            private = true,
                            mediaSessionState =
                                MediaSessionState(mock(), playbackState = MozMediaSession.PlaybackState.PAUSED),
                        )
                    )
            )

        val notification =
            MediaNotification(context, AbstractMediaSessionService::class.java)
                .create(state.tabs[0], mediaSession, null)

        assertEquals("", notification.text)
        assertEquals("A site is playing media", notification.title)
        assertEquals(R.drawable.mozac_feature_media_paused, notification.iconResource)
    }

    @Test
    fun `media session notification with metadata in non private mode`() = runTest {
        val mediaSessionState: MediaSessionState = mock()
        val metadata: MozMediaSession.Metadata = mock()
        whenever(mediaSessionState.metadata).thenReturn(metadata)
        whenever(mediaSessionState.playbackState).thenReturn(MozMediaSession.PlaybackState.PAUSED)
        whenever(mediaSessionState.features).thenReturn(MozMediaSession.Feature())
        whenever(metadata.title).thenReturn("test title")

        val state =
            BrowserState(
                tabs =
                    listOf(
                        createTab(
                            "https://www.mozilla.org",
                            id = "test-tab",
                            title = "Mozilla",
                            private = false,
                            mediaSessionState = mediaSessionState,
                        )
                    )
            )

        val notification =
            MediaNotification(context, AbstractMediaSessionService::class.java)
                .create(state.tabs[0], mediaSession, null)

        assertEquals("https://www.mozilla.org", notification.text)
        assertEquals("test title", notification.title)
        assertEquals(R.drawable.mozac_feature_media_paused, notification.iconResource)
    }

    @Test
    fun `GIVEN artwork WHEN creating the notification THEN the artwork is the large icon`() {
        val tab =
            createTab(
                "https://www.mozilla.org",
                mediaSessionState = MediaSessionState(mock(), playbackState = MozMediaSession.PlaybackState.PLAYING),
            )
        val helper = MediaNotification(context, AbstractMediaSessionService::class.java)

        assertNull(helper.create(tab, mediaSession, null).getLargeIcon())
        assertNotNull(
            helper.create(tab, mediaSession, Bitmap.createBitmap(1, 1, Bitmap.Config.ARGB_8888)).getLargeIcon()
        )
    }

    @Test
    fun `WHEN no next-previous features are advertised THEN the media session notification only shows play-pause`() =
        runTest {
            val state =
                BrowserState(
                    tabs =
                        listOf(
                            createTab(
                                "https://www.mozilla.org",
                                id = "test-tab",
                                title = "Mozilla",
                                mediaSessionState =
                                    MediaSessionState(
                                        mock(),
                                        playbackState = MozMediaSession.PlaybackState.PLAYING,
                                    ),
                            )
                        )
                )

            val notification =
                MediaNotification(context, AbstractMediaSessionService::class.java)
                    .create(state.tabs[0], mediaSession, null)

            assertEquals(1, notification.actions.size)
            assertEquals(
                context.getString(R.string.mozac_feature_media_notification_action_pause),
                notification.actions[0].title.toString(),
            )
            assertArrayEquals(intArrayOf(0), notification.compactActions)
        }

    @Test
    fun `WHEN the NEXT_TRACK feature is advertised THEN the media session notification adds a next action`() = runTest {
        val state =
            BrowserState(
                tabs =
                    listOf(
                        createTab(
                            "https://www.mozilla.org",
                            id = "test-tab",
                            title = "Mozilla",
                            mediaSessionState =
                                MediaSessionState(
                                    mock(),
                                    playbackState = MozMediaSession.PlaybackState.PLAYING,
                                    features = MozMediaSession.Feature(MozMediaSession.Feature.NEXT_TRACK),
                                ),
                        )
                    )
            )

        val notification =
            MediaNotification(context, AbstractMediaSessionService::class.java)
                .create(state.tabs[0], mediaSession, null)

        assertEquals(2, notification.actions.size)
        assertEquals(
            context.getString(R.string.mozac_feature_media_notification_action_pause),
            notification.actions[0].title.toString(),
        )
        assertEquals(
            context.getString(R.string.mozac_feature_media_notification_action_next),
            notification.actions[1].title.toString(),
        )
        assertArrayEquals(intArrayOf(0, 1), notification.compactActions)
    }

    @Test
    fun `WHEN the PREVIOUS_TRACK feature is advertised THEN the media session notification adds a previous action`() =
        runTest {
            val state =
                BrowserState(
                    tabs =
                        listOf(
                            createTab(
                                "https://www.mozilla.org",
                                id = "test-tab",
                                title = "Mozilla",
                                mediaSessionState =
                                    MediaSessionState(
                                        mock(),
                                        playbackState = MozMediaSession.PlaybackState.PAUSED,
                                        features = MozMediaSession.Feature(MozMediaSession.Feature.PREVIOUS_TRACK),
                                    ),
                            )
                        )
                )

            val notification =
                MediaNotification(context, AbstractMediaSessionService::class.java)
                    .create(state.tabs[0], mediaSession, null)

            assertEquals(2, notification.actions.size)
            assertEquals(
                context.getString(R.string.mozac_feature_media_notification_action_previous),
                notification.actions[0].title.toString(),
            )
            assertEquals(
                context.getString(R.string.mozac_feature_media_notification_action_play),
                notification.actions[1].title.toString(),
            )
            assertArrayEquals(intArrayOf(0, 1), notification.compactActions)
        }

    @Test
    fun `WHEN both next-previous features are advertised THEN the media session notification shows previous, play-pause and next`() =
        runTest {
            val state =
                BrowserState(
                    tabs =
                        listOf(
                            createTab(
                                "https://www.mozilla.org",
                                id = "test-tab",
                                title = "Mozilla",
                                mediaSessionState =
                                    MediaSessionState(
                                        mock(),
                                        playbackState = MozMediaSession.PlaybackState.PLAYING,
                                        features =
                                            MozMediaSession.Feature(
                                                MozMediaSession.Feature.PREVIOUS_TRACK or
                                                    MozMediaSession.Feature.NEXT_TRACK
                                            ),
                                    ),
                            )
                        )
                )

            val notification =
                MediaNotification(context, AbstractMediaSessionService::class.java)
                    .create(state.tabs[0], mediaSession, null)

            assertEquals(3, notification.actions.size)
            assertEquals(
                context.getString(R.string.mozac_feature_media_notification_action_previous),
                notification.actions[0].title.toString(),
            )
            assertEquals(
                context.getString(R.string.mozac_feature_media_notification_action_pause),
                notification.actions[1].title.toString(),
            )
            assertEquals(
                context.getString(R.string.mozac_feature_media_notification_action_next),
                notification.actions[2].title.toString(),
            )
            assertArrayEquals(intArrayOf(0, 1, 2), notification.compactActions)
        }

    @Test
    fun `media session notification with metadata in private mode`() = runTest {
        val mediaSessionState: MediaSessionState = mock()
        val metadata: MozMediaSession.Metadata = mock()
        whenever(mediaSessionState.metadata).thenReturn(metadata)
        whenever(mediaSessionState.playbackState).thenReturn(MozMediaSession.PlaybackState.PAUSED)
        whenever(mediaSessionState.features).thenReturn(MozMediaSession.Feature())
        whenever(metadata.title).thenReturn("test title")

        val state =
            BrowserState(
                tabs =
                    listOf(
                        createTab(
                            "https://www.mozilla.org",
                            id = "test-tab",
                            title = "Mozilla",
                            private = true,
                            mediaSessionState = mediaSessionState,
                        )
                    )
            )

        val notification =
            MediaNotification(context, AbstractMediaSessionService::class.java)
                .create(state.tabs[0], mediaSession, null)

        assertEquals("", notification.text)
        assertEquals("A site is playing media", notification.title)
        assertEquals(R.drawable.mozac_feature_media_paused, notification.iconResource)
    }
}

private val Notification.text: String?
    get() = extras.getString(NotificationCompat.EXTRA_TEXT)

private val Notification.title: String?
    get() = extras.getString(NotificationCompat.EXTRA_TITLE)

private val Notification.iconResource: Int
    @Suppress("DEPRECATION") get() = icon

private val Notification.compactActions: IntArray
    get() = extras.getIntArray(Notification.EXTRA_COMPACT_ACTIONS) ?: IntArray(0)
