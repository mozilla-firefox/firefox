/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.media.service

import android.app.ForegroundServiceStartNotAllowedException
import android.app.Notification
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.media.AudioManager
import android.os.Looper
import androidx.annotation.OptIn
import androidx.core.app.NotificationManagerCompat
import androidx.media3.common.util.UnstableApi
import androidx.media3.session.MediaSession
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.test.assertNotNull
import kotlinx.coroutines.CoroutineExceptionHandler
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.MainScope
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.isActive
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.action.MediaSessionAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.MediaSessionState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.base.crash.CrashReporting
import mozilla.components.concept.engine.mediasession.MediaSession as MozMediaSession
import mozilla.components.concept.engine.mediasession.MediaSession.PlaybackState
import mozilla.components.feature.media.facts.MediaFacts
import mozilla.components.feature.media.focus.AudioFocus
import mozilla.components.feature.media.notification.MediaNotification
import mozilla.components.feature.media.player.BrowserStorePlayer
import mozilla.components.support.base.Component
import mozilla.components.support.base.android.NotificationsDelegate
import mozilla.components.support.base.facts.Action
import mozilla.components.support.base.facts.processor.CollectionProcessor
import mozilla.components.support.base.ids.SharedIdsHelper
import mozilla.components.support.test.any
import mozilla.components.support.test.argumentCaptor
import mozilla.components.support.test.coMock
import mozilla.components.support.test.eq
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.whenever
import mozilla.components.support.utils.ext.stopForegroundCompat
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.anyBoolean
import org.mockito.Mockito.anyInt
import org.mockito.Mockito.doNothing
import org.mockito.Mockito.doReturn
import org.mockito.Mockito.doThrow
import org.mockito.Mockito.never
import org.mockito.Mockito.spy
import org.mockito.Mockito.times
import org.mockito.Mockito.verify
import org.mockito.Mockito.verifyNoMoreInteractions
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config

@OptIn(UnstableApi::class)
@RunWith(AndroidJUnit4::class)
class MediaSessionServiceDelegateTest {

    private val notificationId = SharedIdsHelper.getIdForTag(testContext, AbstractMediaSessionService.NOTIFICATION_TAG)

    private val createdSessions = mutableListOf<MediaSession>()
    private val createdPlayers = mutableListOf<BrowserStorePlayer>()

    @After
    fun tearDown() {
        createdSessions.forEach { it.release() }
        createdPlayers.forEach { it.release() }
    }

    private fun createDelegate(
        context: Context = testContext,
        service: AbstractMediaSessionService = mock(),
        store: BrowserStore = BrowserStore(),
        crashReporter: CrashReporting? = mock(),
        notificationsDelegate: NotificationsDelegate = mock(),
        scope: CoroutineScope = MainScope(),
    ): MediaSessionServiceDelegate {
        val delegate =
            MediaSessionServiceDelegate(
                context,
                service,
                store,
                crashReporter,
                notificationsDelegate,
                if (scope is TestScope) CoroutineScope(scope.coroutineContext + SupervisorJob()) else scope,
            )
        createdSessions += delegate.mediaSession
        createdPlayers += delegate.player
        return delegate
    }

    @Test
    fun `GIVEN the injected scope is cancelled THEN the notification scope is cancelled too`() = runTest {
        val parent = MainScope()
        val delegate = createDelegate(scope = parent)
        delegate.onCreate()
        assertTrue(delegate.notificationScope!!.isActive)

        parent.cancel()

        assertFalse(delegate.notificationScope!!.isActive)
    }

    @Test
    fun `WHEN the service is created THEN initialize the notification scope`() = runTest {
        val delegate = createDelegate(scope = this)

        delegate.onCreate()

        assertNotNull(delegate.notificationScope)
    }

    @Test
    fun `GIVEN the delegate was shut down WHEN the service is destroyed THEN the session and player are released`() =
        runTest {
            val notificationsDelegate: NotificationsDelegate = mock()
            whenever(notificationsDelegate.notificationManagerCompat).thenReturn(mock())
            val delegate = createDelegate(notificationsDelegate = notificationsDelegate, scope = this)
            delegate.onCreate()
            delegate.mediaSession = mock()
            delegate.player = mock()

            delegate.shutdown()

            verify(delegate.mediaSession, never()).release()
            verify(delegate.player, never()).release()

            delegate.onDestroy()

            verify(delegate.mediaSession).release()
            verify(delegate.player).release()
        }

    @Test
    fun `WHEN the service is destroyed THEN stop notification updates and abandon audio focus`() = runTest {
        val delegate = createDelegate(scope = this)
        delegate.audioFocus = mock()

        delegate.onDestroy()

        verify(delegate.audioFocus)!!.abandon()
        verify(delegate.service, never()).stopSelf()
        assertNull(delegate.notificationScope)
    }

    @Test
    fun `GIVEN media playing started WHEN a new play command is received THEN resume media and emit telemetry`() =
        runTest {
            val delegate = createDelegate(scope = this)
            delegate.player = mock()

            CollectionProcessor.withFactCollection { facts ->
                delegate.onStartCommand(Intent(AbstractMediaSessionService.ACTION_PLAY))

                verify(delegate.player).play()
                assertEquals(1, facts.size)
                with(facts[0]) {
                    assertEquals(Component.FEATURE_MEDIA, component)
                    assertEquals(Action.PLAY, action)
                    assertEquals(MediaFacts.Items.NOTIFICATION, item)
                }
            }
        }

    @Test
    fun `GIVEN media playing started WHEN a new pause command is received THEN pause media and emit telemetry`() =
        runTest {
            val delegate = createDelegate(scope = this)
            delegate.player = mock()

            CollectionProcessor.withFactCollection { facts ->
                delegate.onStartCommand(Intent(AbstractMediaSessionService.ACTION_PAUSE))

                verify(delegate.player).pause()
                assertEquals(1, facts.size)
                with(facts[0]) {
                    assertEquals(Component.FEATURE_MEDIA, component)
                    assertEquals(Action.PAUSE, action)
                    assertEquals(MediaFacts.Items.NOTIFICATION, item)
                }
            }
        }

    @Test
    fun `GIVEN an active paused tab WHEN ACTION_PLAY is received THEN the tab's controller is played`() = runTest {
        val controller: MozMediaSession.Controller = mock()
        val store =
            BrowserStore(
                BrowserState(
                    tabs =
                        listOf(
                            createTab(
                                "https://www.mozilla.org",
                                mediaSessionState = MediaSessionState(controller, playbackState = PlaybackState.PAUSED),
                            )
                        )
                )
            )
        val delegate = createDelegate(store = store, scope = this)
        // Intentionally do not replace delegate.player: we want the real BrowserStorePlayer
        // so play() routes through handleSetPlayWhenReady to the controller.

        delegate.onStartCommand(Intent(AbstractMediaSessionService.ACTION_PLAY))
        shadowOf(Looper.getMainLooper()).idle()

        verify(controller).play()
    }

    @Test
    fun `GIVEN an active playing tab WHEN ACTION_PAUSE is received THEN the tab's controller is paused`() = runTest {
        val controller: MozMediaSession.Controller = mock()
        val store =
            BrowserStore(
                BrowserState(
                    tabs =
                        listOf(
                            createTab(
                                "https://www.mozilla.org",
                                mediaSessionState =
                                    MediaSessionState(controller, playbackState = PlaybackState.PLAYING),
                            )
                        )
                )
            )
        val delegate = createDelegate(store = store, scope = this)

        delegate.onStartCommand(Intent(AbstractMediaSessionService.ACTION_PAUSE))
        shadowOf(Looper.getMainLooper()).idle()

        verify(controller).pause()
    }

    @Test
    fun `GIVEN an active playing tab WHEN ACTION_NEXT_TRACK is received THEN the tab's controller advances and telemetry is emitted`() =
        runTest {
            val tab =
                getMediaTab(
                    features =
                        MozMediaSession.Feature(
                            MozMediaSession.Feature.NEXT_TRACK or MozMediaSession.Feature.PREVIOUS_TRACK
                        )
                )
            val delegate = createDelegate(store = BrowserStore(BrowserState(tabs = listOf(tab))), scope = this)

            CollectionProcessor.withFactCollection { facts ->
                delegate.onStartCommand(Intent(AbstractMediaSessionService.ACTION_NEXT_TRACK))
                shadowOf(Looper.getMainLooper()).idle()

                verify(tab.mediaSessionState!!.controller).nextTrack()
                assertEquals(1, facts.size)
                with(facts[0]) {
                    assertEquals(Component.FEATURE_MEDIA, component)
                    assertEquals(Action.NEXT, action)
                    assertEquals(MediaFacts.Items.NOTIFICATION, item)
                }
            }
        }

    @Test
    fun `GIVEN an active playing tab WHEN ACTION_PREV_TRACK is received THEN the tab's controller rewinds and telemetry is emitted`() =
        runTest {
            val tab =
                getMediaTab(
                    features =
                        MozMediaSession.Feature(
                            MozMediaSession.Feature.NEXT_TRACK or MozMediaSession.Feature.PREVIOUS_TRACK
                        )
                )
            val delegate = createDelegate(store = BrowserStore(BrowserState(tabs = listOf(tab))), scope = this)

            CollectionProcessor.withFactCollection { facts ->
                delegate.onStartCommand(Intent(AbstractMediaSessionService.ACTION_PREV_TRACK))
                shadowOf(Looper.getMainLooper()).idle()

                verify(tab.mediaSessionState!!.controller).previousTrack()
                assertEquals(1, facts.size)
                with(facts[0]) {
                    assertEquals(Component.FEATURE_MEDIA, component)
                    assertEquals(Action.PREVIOUS, action)
                    assertEquals(MediaFacts.Items.NOTIFICATION, item)
                }
            }
        }

    @Test
    fun `WHEN the task is removed THEN stop media in all tabs and shutdown`() = runTest {
        val notificationManagerCompat: NotificationManagerCompat = mock()
        val notificationsDelegate: NotificationsDelegate = mock()
        whenever(notificationsDelegate.notificationManagerCompat).thenReturn(notificationManagerCompat)

        val mediaTab1 = getMediaTab()
        val mediaTab2 = getMediaTab(PlaybackState.PAUSED)
        val store = BrowserStore(BrowserState(tabs = listOf(mediaTab1, mediaTab2)))
        val delegate = createDelegate(store = store, notificationsDelegate = notificationsDelegate)

        delegate.onTaskRemoved()

        verify(mediaTab1.mediaSessionState!!.controller).stop()
        verify(mediaTab2.mediaSessionState!!.controller).stop()
        verify(delegate.service).stopSelf()
    }

    @Test
    fun `WHEN handling playing media THEN emit telemetry`() = runTest {
        val mediaTab = getMediaTab()
        val delegate = createDelegate(scope = this)

        CollectionProcessor.withFactCollection { facts ->
            delegate.handleMediaPlaying(mediaTab)

            assertEquals(1, facts.size)
            with(facts[0]) {
                assertEquals(Component.FEATURE_MEDIA, component)
                assertEquals(Action.PLAY, action)
                assertEquals(MediaFacts.Items.STATE, item)
            }
        }
    }

    @Test
    fun `WHEN handling playing media THEN register becoming-noisy listener`() = runTest {
        val mediaTab = getMediaTab()
        val delegate = spy(createDelegate())

        delegate.handleMediaPlaying(mediaTab)

        verify(delegate).registerBecomingNoisyListenerIfNeeded()
    }

    @Test
    fun `GIVEN the service is already in foreground WHEN handling playing media THEN request audio focus and update notification`() =
        runTest {
            val mediaTab = getMediaTab()
            val notificationsDelegate: NotificationsDelegate = mock()
            val delegate = createDelegate(notificationsDelegate = notificationsDelegate, scope = this)
            delegate.onCreate()
            delegate.audioFocus = mock()
            delegate.isForegroundService = true

            delegate.handleMediaPlaying(mediaTab)
            testScheduler.advanceUntilIdle()

            verify(delegate.audioFocus).request(eq(mediaTab.id), any())
            verify(notificationsDelegate).notify(any(), eq(delegate.notificationId), any(), any(), any(), eq(false))
        }

    // The delegate reads the audio-session type from the session state at
    // request time, and the feature re-runs this on every mediaSessionState
    // change, so a type that arrives after playback started is picked up on the
    // next request rather than being stuck at the default. Every type must be
    // forwarded to the focus request exactly once, with no default request made
    // first. One case per type so each runs in a fresh test environment.
    private fun TestScope.assertFocusRequestUsesType(type: MozMediaSession.AudioSessionType) {
        val mediaTab =
            createTab(
                url = "https://www.mozilla.org",
                mediaSessionState =
                    MediaSessionState(
                        mock(),
                        playbackState = PlaybackState.PLAYING,
                        audioSessionType = type,
                    ),
            )
        val delegate = createDelegate(scope = this)
        delegate.onCreate()
        delegate.audioFocus = mock()
        delegate.isForegroundService = true

        delegate.handleMediaPlaying(mediaTab)
        testScheduler.advanceUntilIdle()

        verify(delegate.audioFocus, times(1)).request(mediaTab.id, type)
        verifyNoMoreInteractions(delegate.audioFocus)
    }

    @Test
    fun `GIVEN a foreground service WHEN playing media with auto type THEN focus is requested as auto`() = runTest {
        assertFocusRequestUsesType(MozMediaSession.AudioSessionType.AUTO)
    }

    @Test
    fun `GIVEN a foreground service WHEN playing media with playback type THEN focus is requested as playback`() =
        runTest {
            assertFocusRequestUsesType(MozMediaSession.AudioSessionType.PLAYBACK)
        }

    @Test
    fun `GIVEN a foreground service WHEN playing media with transient type THEN focus is requested as transient`() =
        runTest {
            assertFocusRequestUsesType(MozMediaSession.AudioSessionType.TRANSIENT)
        }

    @Test
    fun `GIVEN a foreground service WHEN playing media with transient-solo type THEN focus is requested as transient-solo`() =
        runTest {
            assertFocusRequestUsesType(MozMediaSession.AudioSessionType.TRANSIENT_SOLO)
        }

    @Test
    fun `GIVEN a foreground service WHEN playing media with ambient type THEN focus is requested as ambient`() =
        runTest {
            assertFocusRequestUsesType(MozMediaSession.AudioSessionType.AMBIENT)
        }

    @Test
    fun `GIVEN a foreground service WHEN playing media with play-and-record type THEN focus is requested as play-and-record`() =
        runTest {
            assertFocusRequestUsesType(MozMediaSession.AudioSessionType.PLAY_AND_RECORD)
        }

    @Test
    fun `GIVEN the service is not in foreground WHEN playing media with transient type THEN the focus requested after starting foreground uses that type`() =
        runTest {
            val mediaTab =
                createTab(
                    url = "https://www.mozilla.org",
                    mediaSessionState =
                        MediaSessionState(
                            mock(),
                            playbackState = PlaybackState.PLAYING,
                            audioSessionType = MozMediaSession.AudioSessionType.TRANSIENT,
                        ),
                )
            val delegate = createDelegate(scope = this)
            delegate.onCreate()
            delegate.audioFocus = mock()
            delegate.isForegroundService = false

            delegate.handleMediaPlaying(mediaTab)
            testScheduler.advanceUntilIdle()

            verify(delegate.audioFocus).request(mediaTab.id, MozMediaSession.AudioSessionType.TRANSIENT)
        }

    @Test
    fun `GIVEN the service is not in foreground WHEN handling playing media THEN start the media service as foreground`() =
        runTest {
            val mediaTab = getMediaTab()
            val delegate = createDelegate(scope = this)
            delegate.onCreate()
            delegate.audioFocus = mock()
            delegate.isForegroundService = false

            delegate.handleMediaPlaying(mediaTab)
            testScheduler.advanceUntilIdle()

            verify(delegate.service).startForeground(eq(delegate.notificationId), any())
            assertTrue(delegate.isForegroundService)
        }

    @Test
    fun `GIVEN the service is not in foreground WHEN handling playing media THEN audio focus is requested after foreground service is started`() =
        runTest {
            val mediaTab = getMediaTab()
            val delegate = createDelegate(scope = this)
            delegate.onCreate()
            delegate.audioFocus = mock()
            delegate.isForegroundService = false

            delegate.handleMediaPlaying(mediaTab)
            testScheduler.advanceUntilIdle()

            val inOrder = org.mockito.Mockito.inOrder(delegate.service, delegate.audioFocus)
            inOrder.verify(delegate.service).startForeground(eq(delegate.notificationId), any())
            inOrder.verify(delegate.audioFocus).request(eq(mediaTab.id), any())
        }

    @Test
    fun `WHEN updating the notification for a new media state THEN post a new notification`() = runTest {
        val mediaTab = getMediaTab()
        val notificationsDelegate: NotificationsDelegate = mock()
        val delegate = createDelegate(notificationsDelegate = notificationsDelegate, scope = this)
        delegate.onCreate()
        val notification: Notification = mock()
        delegate.notificationHelper = coMock {
            doReturn(notification).`when`(this).create(mediaTab, delegate.mediaSession)
        }

        delegate.updateNotification(mediaTab)
        testScheduler.advanceUntilIdle()

        verify(notificationsDelegate)
            .notify(any(), eq(delegate.notificationId), eq(notification), any(), any(), eq(false))
    }

    @Test
    fun `GIVEN a transient focus loss WHEN media content is updated THEN only the notification is refreshed`() =
        runTest {
            val mediaTab = getMediaTab(PlaybackState.PAUSED)
            val notificationsDelegate: NotificationsDelegate = mock()
            val delegate = createDelegate(notificationsDelegate = notificationsDelegate, scope = this)
            delegate.onCreate()
            delegate.isForegroundService = true
            delegate.isTransientAudioFocusLoss = true
            val notification: Notification = mock()
            delegate.notificationHelper = coMock {
                doReturn(notification).`when`(this).create(mediaTab, delegate.mediaSession)
            }

            CollectionProcessor.withFactCollection { facts ->
                delegate.handleMediaUpdated(mediaTab)
                testScheduler.advanceUntilIdle()

                assertTrue(facts.isEmpty())
            }

            verify(notificationsDelegate)
                .notify(any(), eq(delegate.notificationId), eq(notification), any(), any(), eq(false))
            assertTrue(delegate.isForegroundService)
            assertTrue(delegate.isTransientAudioFocusLoss)
        }

    @Test
    fun `WHEN starting the service as foreground THEN use start with a new notification for the current media state`() =
        runTest {
            val mediaTab = getMediaTab()
            val delegate = createDelegate(scope = this)
            delegate.onCreate()
            delegate.audioFocus = mock()
            val notification: Notification = mock()
            delegate.notificationHelper = coMock {
                doReturn(notification).`when`(this).create(mediaTab, delegate.mediaSession)
            }

            delegate.startForeground(mediaTab, coroutineContext)
            testScheduler.advanceUntilIdle()

            verify(delegate.service).startForeground(eq(delegate.notificationId), eq(notification))
            assertTrue(delegate.isForegroundService)
            verify(delegate.audioFocus).request(eq(mediaTab.id), any())
        }

    @Test
    fun `WHEN handling paused media THEN emit telemetry`() = runTest {
        val mediaTab = getMediaTab(PlaybackState.PAUSED)
        val delegate = createDelegate(scope = this)

        CollectionProcessor.withFactCollection { facts ->
            delegate.handleMediaPaused(mediaTab)

            assertEquals(1, facts.size)
            with(facts[0]) {
                assertEquals(Component.FEATURE_MEDIA, component)
                assertEquals(Action.PAUSE, action)
                assertEquals(MediaFacts.Items.STATE, item)
            }
        }
    }

    @Test
    fun `WHEN handling paused media THEN update internal state and notification and stop the service`() = runTest {
        val mediaTab = getMediaTab(PlaybackState.PAUSED)
        val notificationManagerCompat = spy(NotificationManagerCompat.from(testContext))
        val notificationsDelegate = spy(NotificationsDelegate(notificationManagerCompat))
        doReturn(true).`when`(notificationManagerCompat).areNotificationsEnabled()

        val notificationHelper: MediaNotification = mock()
        val notification: Notification = mock()
        val notificationId = SharedIdsHelper.getIdForTag(testContext, AbstractMediaSessionService.NOTIFICATION_TAG)

        val delegate = spy(createDelegate(notificationsDelegate = notificationsDelegate, scope = this))
        delegate.isForegroundService = true
        delegate.notificationHelper = notificationHelper
        delegate.audioFocus = mock()
        delegate.isTransientAudioFocusLoss = false
        // Extract outside the stubbing: accessing the spy's field mid-stub trips UnfinishedStubbingException.
        val session = delegate.mediaSession

        doReturn(notification).`when`(notificationHelper).create(mediaTab, session)

        delegate.onCreate()

        delegate.handleMediaPaused(mediaTab)
        testScheduler.advanceUntilIdle()

        verify(delegate).unregisterBecomingNoisyListenerIfNeeded()
        verify(delegate.service).stopForegroundCompat(false)
        verify(notificationsDelegate).notify(null, notificationId, notification)
        assertFalse(delegate.isForegroundService)
    }

    @Test
    fun `GIVEN transient audio focus loss WHEN paused twice THEN only the second pause stops foreground`() = runTest {
        val mediaTab = getMediaTab(PlaybackState.PAUSED)
        val notificationManagerCompat = spy(NotificationManagerCompat.from(testContext))
        val notificationsDelegate = spy(NotificationsDelegate(notificationManagerCompat))
        doReturn(true).`when`(notificationManagerCompat).areNotificationsEnabled()

        val notificationHelper: MediaNotification = mock()
        val notification: Notification = mock()

        val delegate = spy(createDelegate(notificationsDelegate = notificationsDelegate, scope = this))
        delegate.isForegroundService = true
        delegate.notificationHelper = notificationHelper
        delegate.audioFocus = mock()
        delegate.isTransientAudioFocusLoss = true
        // Extract outside the stubbing: accessing the spy's field mid-stub trips UnfinishedStubbingException.
        val session = delegate.mediaSession

        doReturn(notification).`when`(notificationHelper).create(mediaTab, session)

        delegate.onCreate()

        delegate.handleMediaPaused(mediaTab)
        testScheduler.advanceUntilIdle()

        verify(delegate, never()).unregisterBecomingNoisyListenerIfNeeded()
        verify(delegate.service, never()).stopForegroundCompat(false)
        assertTrue(delegate.isForegroundService)
        assertFalse(delegate.isTransientAudioFocusLoss)

        delegate.handleMediaPaused(mediaTab)
        testScheduler.advanceUntilIdle()

        verify(delegate).unregisterBecomingNoisyListenerIfNeeded()
        verify(delegate.service).stopForegroundCompat(false)
        assertFalse(delegate.isForegroundService)
    }

    @Test
    fun `WHEN handling stopped media THEN emit telemetry`() = runTest {
        val mediaTab = getMediaTab(PlaybackState.STOPPED)
        val delegate = createDelegate(scope = this)

        CollectionProcessor.withFactCollection { facts ->
            delegate.handleMediaStopped(mediaTab)

            assertEquals(1, facts.size)
            with(facts[0]) {
                assertEquals(Component.FEATURE_MEDIA, component)
                assertEquals(Action.STOP, action)
                assertEquals(MediaFacts.Items.STATE, item)
            }
        }
    }

    @Test
    fun `WHEN handling stopped media THEN update internal state and cancel notification and stop the service`() =
        runTest {
            val mediaTab = getMediaTab(PlaybackState.STOPPED)
            val notificationManagerCompat = spy(NotificationManagerCompat.from(testContext))
            val notificationsDelegate = spy(NotificationsDelegate(notificationManagerCompat))
            doReturn(true).`when`(notificationManagerCompat).areNotificationsEnabled()

            val delegate = spy(createDelegate(notificationsDelegate = notificationsDelegate, scope = this))
            delegate.isForegroundService = true
            delegate.audioFocus = mock()
            delegate.notificationHelper = mock()
            delegate.onCreate()

            delegate.handleMediaStopped(mediaTab)
            testScheduler.advanceUntilIdle()

            verify(delegate).unregisterBecomingNoisyListenerIfNeeded()
            verify(delegate.service).stopForegroundCompat(false)
            verify(delegate.audioFocus).abandon()
            verify(notificationManagerCompat).cancel(eq(notificationId))
            assertFalse(delegate.isForegroundService)
        }

    @Test
    fun `WHEN there is no media playing THEN stop the media service`() = runTest {
        val notificationManagerCompat: NotificationManagerCompat = mock()
        val notificationsDelegate: NotificationsDelegate = mock()
        whenever(notificationsDelegate.notificationManagerCompat).thenReturn(notificationManagerCompat)

        val delegate = createDelegate(notificationsDelegate = notificationsDelegate)
        delegate.audioFocus = mock()

        delegate.handleNoMedia()

        verify(delegate.service).stopSelf()
    }

    @Test
    fun `WHEN stopping running in foreground THEN stop the foreground service`() = runTest {
        val delegate = createDelegate(scope = this)
        delegate.isForegroundService = true

        delegate.stopForeground()

        verify(delegate.service).stopForegroundCompat(false)
        assertFalse(delegate.isForegroundService)
    }

    @Test
    fun `GIVEN a audio noisy receiver is already registered WHEN trying to register a new one THEN return early`() =
        runTest {
            val context = spy(testContext)
            val delegate = createDelegate(context = context)
            delegate.noisyAudioStreamReceiver = mock()

            delegate.registerBecomingNoisyListenerIfNeeded()

            verify(context, never()).registerReceiver(any(), any(), eq(Context.RECEIVER_NOT_EXPORTED))
        }

    @Test
    fun `GIVEN a audio noisy receiver is not already registered WHEN trying to register a new one THEN register it`() =
        runTest {
            val delegate = spy(createDelegate())
            val receiverCaptor = argumentCaptor<BroadcastReceiver>()

            delegate.registerBecomingNoisyListenerIfNeeded()

            verify(delegate).registerBecomingNoisyListener(receiverCaptor.capture())
            assertEquals(BecomingNoisyReceiver::class.java, receiverCaptor.value.javaClass)
        }

    @Test
    fun `GIVEN a audio noisy receiver is already registered WHEN trying to unregister one THEN unregister it`() =
        runTest {
            val context = spy(testContext)
            val delegate = createDelegate(context = context, store = BrowserStore(mock()), scope = this)
            delegate.noisyAudioStreamReceiver = mock()
            context.registerReceiver(
                delegate.noisyAudioStreamReceiver,
                delegate.intentFilter,
                Context.RECEIVER_NOT_EXPORTED,
            )
            val receiverCaptor = argumentCaptor<BroadcastReceiver>()

            delegate.unregisterBecomingNoisyListenerIfNeeded()

            verify(context).unregisterReceiver(receiverCaptor.capture())
            assertEquals(BecomingNoisyReceiver::class.java, receiverCaptor.value.javaClass)
            assertNull(delegate.noisyAudioStreamReceiver)
        }

    @Test
    fun `GIVEN a audio noisy receiver is not already registered WHEN trying to unregister one THEN return early`() =
        runTest {
            val context = spy(testContext)
            val delegate = createDelegate(context = context)

            delegate.unregisterBecomingNoisyListenerIfNeeded()

            verify(context, never()).unregisterReceiver(any())
        }

    @Test
    fun `WHEN the delegate is shutdown THEN cleanup resources and stop the media service`() = runTest {
        val notificationManagerCompat: NotificationManagerCompat = mock()
        val notificationsDelegate: NotificationsDelegate = mock()
        whenever(notificationsDelegate.notificationManagerCompat).thenReturn(notificationManagerCompat)

        val delegate = createDelegate(notificationsDelegate = notificationsDelegate)
        delegate.mediaSession = mock()
        delegate.player = mock()
        delegate.audioFocus = mock()

        delegate.shutdown()

        verify(delegate.mediaSession, never()).release()
        verify(delegate.player, never()).release()
        verify(delegate.audioFocus, never()).abandon()
        verify(delegate.service).stopSelf()
        assertNull(delegate.noisyAudioStreamReceiver)
    }

    @Test
    fun `GIVEN an in-flight notification coroutine WHEN shutdown is called THEN the notification is not posted`() =
        runTest {
            val notificationManagerCompat: NotificationManagerCompat = mock()
            val notificationsDelegate: NotificationsDelegate = mock()
            whenever(notificationsDelegate.notificationManagerCompat).thenReturn(notificationManagerCompat)

            val delegate = createDelegate(notificationsDelegate = notificationsDelegate, scope = this)
            delegate.onCreate()
            delegate.mediaSession = mock()
            delegate.player = mock()
            val notification: Notification = mock()
            delegate.notificationHelper = coMock {
                doReturn(notification).`when`(this).create(any(), any())
            }

            delegate.updateNotification(getMediaTab(PlaybackState.PAUSED))
            delegate.shutdown()
            testScheduler.advanceUntilIdle()

            verify(notificationsDelegate, never()).notify(any(), anyInt(), any(), any(), any(), anyBoolean())
        }

    @Test
    fun `GIVEN the delegate has been shutdown WHEN handleMediaPlaying is called THEN the service starts in the foreground and requests audio focus`() =
        runTest {
            val notificationManagerCompat: NotificationManagerCompat = mock()
            val notificationsDelegate: NotificationsDelegate = mock()
            whenever(notificationsDelegate.notificationManagerCompat).thenReturn(notificationManagerCompat)

            val delegate = spy(createDelegate(notificationsDelegate = notificationsDelegate, scope = this))
            // Avoid registering a real BroadcastReceiver on testContext after shutdown.
            doNothing().`when`(delegate).registerBecomingNoisyListenerIfNeeded()
            delegate.onCreate()
            delegate.audioFocus = mock()
            val notification: Notification = mock()
            delegate.notificationHelper = coMock { doReturn(notification).`when`(this).create(any(), any()) }
            val mediaTab = getMediaTab()

            delegate.shutdown()
            testScheduler.advanceUntilIdle()

            delegate.handleMediaPlaying(mediaTab)
            testScheduler.advanceUntilIdle()

            verify(delegate.service).startForeground(notificationId, notification)
            verify(delegate.audioFocus).request(eq(mediaTab.id), any())
        }

    @Test
    fun `GIVEN no active media tab WHEN ACTION_PAUSE is received THEN the command is handled without crashing and notification telemetry still fires`() =
        runTest {
            val delegate = createDelegate(store = BrowserStore(), scope = this)

            CollectionProcessor.withFactCollection { facts ->
                delegate.onStartCommand(Intent(AbstractMediaSessionService.ACTION_PAUSE))
                shadowOf(Looper.getMainLooper()).idle()

                assertEquals(1, facts.size)
                with(facts[0]) {
                    assertEquals(Component.FEATURE_MEDIA, component)
                    assertEquals(Action.PAUSE, action)
                    assertEquals(MediaFacts.Items.NOTIFICATION, item)
                }
            }
        }

    @Test
    fun `WHEN the delegate is created THEN its mediaSession is backed by the delegate's BrowserStorePlayer`() =
        runTest {
            val delegate = createDelegate(scope = this)

            // Media3 routes framework media-button events (lockscreen, Bluetooth, Android Auto) to
            // the Player bound to the MediaSession. Proving that wiring here lets BrowserStorePlayerTest
            // own the metadata/command assertions without duplicating them through the delegate.
            assertSame(delegate.player, delegate.mediaSession.player)
        }

    @Test
    fun `when device is becoming noisy, playback is paused`() = runTest {
        val controller: MozMediaSession.Controller = mock()
        val initialState =
            BrowserState(
                tabs =
                    listOf(
                        createTab(
                            "https://www.mozilla.org",
                            mediaSessionState = MediaSessionState(controller, playbackState = PlaybackState.PLAYING),
                        )
                    )
            )
        val store = BrowserStore(initialState)
        val service: AbstractMediaSessionService = mock()
        val delegate = createDelegate(service = service, store = store)
        delegate.onCreate()
        delegate.audioFocus = mock()
        delegate.handleMediaPlaying(initialState.tabs[0])

        delegate.deviceBecomingNoisy(testContext)
        shadowOf(Looper.getMainLooper()).idle()

        verify(controller).pause()
    }

    @Test
    fun `GIVEN the active media tab changed after the noisy listener was registered WHEN device is becoming noisy THEN the new active tab is paused`() =
        runTest {
            val tab1 = getMediaTab()
            val tab2 = getMediaTab(PlaybackState.PAUSED)
            val store = BrowserStore(BrowserState(tabs = listOf(tab1, tab2)))
            val delegate = createDelegate(store = store)
            delegate.onCreate()
            delegate.audioFocus = mock()
            delegate.handleMediaPlaying(tab1)

            store.dispatch(MediaSessionAction.UpdateMediaPlaybackStateAction(tab1.id, PlaybackState.PAUSED))
            store.dispatch(MediaSessionAction.UpdateMediaPlaybackStateAction(tab2.id, PlaybackState.PLAYING))
            shadowOf(Looper.getMainLooper()).idle()
            delegate.deviceBecomingNoisy(testContext)
            shadowOf(Looper.getMainLooper()).idle()

            verify(tab2.mediaSessionState!!.controller).pause()
            verify(tab1.mediaSessionState!!.controller, never()).pause()
        }

    @Test
    @Config(sdk = [31])
    fun `GIVEN device is at least API level 31 WHEN startForeground throws an exception THEN catch and pass the exception to the crash reporter`() =
        runTest {
            val crashReporter: CrashReporting = mock()
            val service: AbstractMediaSessionService = mock()
            val delegate = createDelegate(service = service, crashReporter = crashReporter, scope = this)
            delegate.onCreate()
            delegate.audioFocus = mock()
            val notification: Notification = mock()
            delegate.notificationHelper = coMock {
                doReturn(notification).`when`(this).create(mock(), delegate.mediaSession)
            }

            val exception = ForegroundServiceStartNotAllowedException("Test thrown exception")
            doThrow(exception).`when`(service).startForeground(anyInt(), any())

            delegate.startForeground(mock(), coroutineContext)
            testScheduler.advanceUntilIdle()

            verify(crashReporter).submitCaughtException(exception)
            verify(delegate.audioFocus, never()).request(any(), any())
        }

    @Test
    @Config(sdk = [30])
    fun `GIVEN device is less than 31 WHEN startForeground throws an exception THEN the exception is not swallowed and crash reporter is not notified`() =
        runTest {
            var caught: Throwable? = null
            val exceptionHandler = CoroutineExceptionHandler { _, t -> caught = t }

            val crashReporter: CrashReporting = mock()
            val service: AbstractMediaSessionService = mock()
            val delegate = createDelegate(service = service, crashReporter = crashReporter, scope = this)
            delegate.onCreate()
            delegate.audioFocus = mock()
            val notification: Notification = mock()
            delegate.notificationHelper = coMock {
                doReturn(notification).`when`(this).create(mock(), delegate.mediaSession)
            }

            val exception = RuntimeException("Test thrown exception")
            doThrow(exception).`when`(service).startForeground(anyInt(), any())

            delegate.startForeground(mock(), exceptionHandler)
            testScheduler.advanceUntilIdle()

            assertSame(exception, caught)
            assertTrue(delegate.notificationScope!!.isActive)
            verify(crashReporter, never()).submitCaughtException(any())
            verify(delegate.audioFocus, never()).request(any(), any())
        }

    @Test
    fun `GIVEN playing media WHEN the same tab updates again while playing THEN audio focus is requested once`() =
        runTest {
            val mediaTab = getMediaTab()
            val delegate = delegateWithMockedFocus()

            delegate.handleMediaPlaying(mediaTab)
            delegate.handleMediaPlaying(mediaTab.withPosition(5.0))
            testScheduler.advanceUntilIdle()

            verify(delegate.audioFocus, times(1)).request(any(), any())
        }

    @Test
    fun `GIVEN the service is not in foreground WHEN playing media updates after starting foreground THEN audio focus is requested once`() =
        runTest {
            val mediaTab = getMediaTab()
            val delegate = delegateWithMockedFocus()
            delegate.isForegroundService = false

            delegate.handleMediaPlaying(mediaTab)
            testScheduler.advanceUntilIdle()
            delegate.handleMediaPlaying(mediaTab.withPosition(5.0))
            testScheduler.advanceUntilIdle()

            verify(delegate.audioFocus, times(1)).request(any(), any())
        }

    @Test
    fun `GIVEN playing media WHEN paused and played again THEN audio focus is requested again`() = runTest {
        val mediaTab = getMediaTab()
        val delegate = delegateWithMockedFocus()

        delegate.handleMediaPlaying(mediaTab)
        delegate.handleMediaPaused(mediaTab.withPlaybackState(PlaybackState.PAUSED))
        delegate.isForegroundService = true
        delegate.handleMediaPlaying(mediaTab)
        testScheduler.advanceUntilIdle()

        verify(delegate.audioFocus, times(2)).request(eq(mediaTab.id), any())
    }

    @Test
    fun `GIVEN playing media WHEN stopped and played again THEN audio focus is requested again`() = runTest {
        val mediaTab = getMediaTab()
        val delegate = delegateWithMockedFocus()

        delegate.handleMediaPlaying(mediaTab)
        delegate.handleMediaStopped(mediaTab.withPlaybackState(PlaybackState.STOPPED))
        delegate.isForegroundService = true
        delegate.handleMediaPlaying(mediaTab)
        testScheduler.advanceUntilIdle()

        verify(delegate.audioFocus, times(2)).request(eq(mediaTab.id), any())
    }

    @Test
    fun `GIVEN playing media WHEN the delegate is shutdown and the tab plays again THEN audio focus is requested and the notification posted again`() =
        runTest {
            val mediaTab = getMediaTab()
            val delegate = delegateWithMockedFocus()

            delegate.handleMediaPlaying(mediaTab)
            delegate.shutdown()
            delegate.handleMediaPlaying(mediaTab)
            testScheduler.advanceUntilIdle()

            verify(delegate.audioFocus, times(2)).request(eq(mediaTab.id), any())
            verify(delegate.notificationsDelegate).notify(any(), anyInt(), any(), any(), any(), anyBoolean())
        }

    @Test
    fun `GIVEN the service is starting foreground WHEN the delegate is shutdown before it has started THEN audio focus is not requested`() =
        runTest {
            val mediaTab = getMediaTab()
            val delegate = delegateWithMockedFocus()
            delegate.isForegroundService = false

            delegate.handleMediaPlaying(mediaTab)
            delegate.shutdown()
            testScheduler.advanceUntilIdle()

            verify(delegate.audioFocus, never()).request(any(), any())
        }

    @Test
    fun `GIVEN playing media WHEN another tab starts playing THEN audio focus is requested for it`() = runTest {
        val mediaTab1 = getMediaTab()
        val mediaTab2 = getMediaTab()
        val delegate = delegateWithMockedFocus()

        delegate.handleMediaPlaying(mediaTab1)
        delegate.handleMediaPlaying(mediaTab2)
        testScheduler.advanceUntilIdle()

        verify(delegate.audioFocus).request(eq(mediaTab1.id), any())
        verify(delegate.audioFocus).request(eq(mediaTab2.id), any())
    }

    private suspend fun TestScope.assertTypeChangeRequestsFocus(type: MozMediaSession.AudioSessionType) {
        val mediaTab = getMediaTab()
        val delegate = delegateWithMockedFocus()

        delegate.handleMediaPlaying(mediaTab)
        delegate.handleMediaPlaying(mediaTab.withAudioSessionType(type))
        testScheduler.advanceUntilIdle()

        verify(delegate.audioFocus).request(mediaTab.id, MozMediaSession.AudioSessionType.AUTO)
        verify(delegate.audioFocus).request(mediaTab.id, type)
    }

    @Test
    fun `GIVEN playing media WHEN the type changes to playback THEN the focus request uses the playback type`() =
        runTest {
            assertTypeChangeRequestsFocus(MozMediaSession.AudioSessionType.PLAYBACK)
        }

    @Test
    fun `GIVEN playing media WHEN the type changes to transient THEN the focus request uses the transient type`() =
        runTest {
            assertTypeChangeRequestsFocus(MozMediaSession.AudioSessionType.TRANSIENT)
        }

    @Test
    fun `GIVEN playing media WHEN the type changes to transient-solo THEN the focus request uses the transient-solo type`() =
        runTest {
            assertTypeChangeRequestsFocus(MozMediaSession.AudioSessionType.TRANSIENT_SOLO)
        }

    @Test
    fun `GIVEN playing media WHEN the type changes to ambient THEN the focus request uses the ambient type`() =
        runTest {
            assertTypeChangeRequestsFocus(MozMediaSession.AudioSessionType.AMBIENT)
        }

    @Test
    fun `GIVEN playing media WHEN the type changes to play-and-record THEN the focus request uses the play-and-record type`() =
        runTest {
            assertTypeChangeRequestsFocus(MozMediaSession.AudioSessionType.PLAY_AND_RECORD)
        }

    @Test
    fun `GIVEN the service is starting foreground WHEN media pauses before it has started THEN audio focus is not requested for the paused media`() =
        runTest {
            val mediaTab = getMediaTab()
            val delegate = delegateWithMockedFocus()
            delegate.isForegroundService = false

            delegate.handleMediaPlaying(mediaTab)
            delegate.handleMediaPaused(mediaTab.withPlaybackState(PlaybackState.PAUSED))
            testScheduler.advanceUntilIdle()
            verify(delegate.audioFocus, never()).request(any(), any())

            delegate.isForegroundService = true
            delegate.handleMediaPlaying(mediaTab)
            testScheduler.advanceUntilIdle()
            verify(delegate.audioFocus, times(1)).request(eq(mediaTab.id), any())
        }

    @Test
    fun `GIVEN the service is starting foreground WHEN media stops before it has started THEN audio focus is not requested`() =
        runTest {
            val mediaTab = getMediaTab()
            val delegate = delegateWithMockedFocus()
            delegate.isForegroundService = false

            delegate.handleMediaPlaying(mediaTab)
            delegate.handleMediaStopped(mediaTab.withPlaybackState(PlaybackState.STOPPED))
            testScheduler.advanceUntilIdle()

            verify(delegate.audioFocus, never()).request(any(), any())
        }

    @Test
    fun `GIVEN the service is starting foreground WHEN another tab starts playing before it has started THEN audio focus is requested only for that tab`() =
        runTest {
            val mediaTab1 = getMediaTab()
            val mediaTab2 = getMediaTab()
            val delegate = delegateWithMockedFocus()
            delegate.isForegroundService = false

            delegate.handleMediaPlaying(mediaTab1)
            delegate.handleMediaPlaying(mediaTab2)
            testScheduler.advanceUntilIdle()

            verify(delegate.audioFocus, never()).request(eq(mediaTab1.id), any())
            verify(delegate.audioFocus, times(1)).request(eq(mediaTab2.id), any())
        }

    @Test
    fun `GIVEN the audio focus request was denied WHEN the tab updates again while playing THEN audio focus is requested again`() =
        runTest {
            val mediaTab = getMediaTab()
            val delegate = delegateWithMockedFocus()
            doReturn(false).`when`(delegate.audioFocus).request(any(), any())

            delegate.handleMediaPlaying(mediaTab)
            delegate.handleMediaPlaying(mediaTab.withPosition(5.0))
            testScheduler.advanceUntilIdle()

            verify(delegate.audioFocus, times(2)).request(eq(mediaTab.id), any())
        }

    @Test
    fun `GIVEN the request made after starting foreground was denied WHEN the tab updates again while playing THEN audio focus is requested again`() =
        runTest {
            val mediaTab = getMediaTab()
            val delegate = delegateWithMockedFocus()
            delegate.isForegroundService = false
            doReturn(false).`when`(delegate.audioFocus).request(any(), any())

            delegate.handleMediaPlaying(mediaTab)
            testScheduler.advanceUntilIdle()
            delegate.handleMediaPlaying(mediaTab.withPosition(5.0))
            testScheduler.advanceUntilIdle()

            verify(delegate.audioFocus, times(2)).request(eq(mediaTab.id), any())
        }

    private suspend fun TestScope.assertNoFocusRequestAfterSystemFocusLoss(focusChange: Int) {
        val audioManager: AudioManager = mock()
        doReturn(AudioManager.AUDIOFOCUS_REQUEST_GRANTED).`when`(audioManager).requestAudioFocus(any())
        val mediaTab = getMediaTab()
        val store = BrowserStore(BrowserState(tabs = listOf(mediaTab)))
        val delegate = delegateWithMockedFocus(store)
        delegate.audioFocus =
            AudioFocus(audioManager, store, onTransientFocusLoss = { delegate.isTransientAudioFocusLoss = it })

        delegate.handleMediaPlaying(mediaTab)
        delegate.audioFocus.onAudioFocusChange(focusChange)
        delegate.handleMediaPlaying(mediaTab.withPosition(5.0))
        testScheduler.advanceUntilIdle()

        verify(audioManager, times(1)).requestAudioFocus(any())
    }

    @Test
    fun `GIVEN playing media WHEN the system takes audio focus transiently and the tab keeps updating THEN audio focus is not requested again`() =
        runTest {
            assertNoFocusRequestAfterSystemFocusLoss(AudioManager.AUDIOFOCUS_LOSS_TRANSIENT)
        }

    @Test
    fun `GIVEN playing media WHEN the system takes audio focus permanently and the tab keeps updating THEN audio focus is not requested again`() =
        runTest {
            assertNoFocusRequestAfterSystemFocusLoss(AudioManager.AUDIOFOCUS_LOSS)
        }

    @Test
    fun `GIVEN the system took audio focus permanently WHEN the media session receives play THEN audio focus is requested again and the tab is told to play`() =
        runTest {
            val audioManager: AudioManager = mock()
            doReturn(AudioManager.AUDIOFOCUS_REQUEST_GRANTED).`when`(audioManager).requestAudioFocus(any())
            val mediaTab = getMediaTab()
            val store = BrowserStore(BrowserState(tabs = listOf(mediaTab)))
            val delegate = delegateWithMockedFocus(store)
            delegate.audioFocus =
                AudioFocus(audioManager, store, onTransientFocusLoss = { delegate.isTransientAudioFocusLoss = it })

            delegate.handleMediaPlaying(mediaTab)
            delegate.audioFocus.onAudioFocusChange(AudioManager.AUDIOFOCUS_LOSS)
            delegate.handleMediaPlaying(mediaTab.withPosition(5.0))
            verify(audioManager, times(1)).requestAudioFocus(any())

            delegate.player.play()
            shadowOf(Looper.getMainLooper()).idle()
            testScheduler.advanceUntilIdle()

            verify(audioManager, times(2)).requestAudioFocus(any())
            verify(mediaTab.mediaSessionState!!.controller).play()
        }

    @Test
    fun `GIVEN playing media WHEN the media session receives play THEN audio focus is requested again and the tab is told to play`() =
        runTest {
            val mediaTab = getMediaTab()
            val delegate = delegateWithMockedFocus(BrowserStore(BrowserState(tabs = listOf(mediaTab))))

            delegate.handleMediaPlaying(mediaTab)
            delegate.handleMediaPlaying(mediaTab.withPosition(5.0))
            delegate.player.play()
            shadowOf(Looper.getMainLooper()).idle()
            testScheduler.advanceUntilIdle()

            verify(delegate.audioFocus, times(2)).request(eq(mediaTab.id), any())
            verify(mediaTab.mediaSessionState!!.controller).play()
        }

    @Test
    fun `GIVEN audio focus is denied WHEN the media session receives play THEN the tab is not told to play`() =
        runTest {
            val mediaTab = getMediaTab(PlaybackState.PAUSED)
            val delegate = delegateWithMockedFocus(BrowserStore(BrowserState(tabs = listOf(mediaTab))))
            doReturn(false).`when`(delegate.audioFocus).request(any(), any())

            delegate.player.play()
            shadowOf(Looper.getMainLooper()).idle()
            testScheduler.advanceUntilIdle()

            verify(delegate.audioFocus).request(eq(mediaTab.id), any())
            verify(mediaTab.mediaSessionState!!.controller, never()).play()
        }

    @Test
    fun `GIVEN the service is starting foreground WHEN the media session receives play before it has started THEN audio focus is requested once it has started`() =
        runTest {
            val mediaTab = getMediaTab()
            val delegate = delegateWithMockedFocus(BrowserStore(BrowserState(tabs = listOf(mediaTab))))
            delegate.isForegroundService = false

            delegate.handleMediaPlaying(mediaTab)
            delegate.player.play()
            shadowOf(Looper.getMainLooper()).idle()
            testScheduler.advanceUntilIdle()

            verify(delegate.audioFocus, times(1)).request(eq(mediaTab.id), any())
            verify(mediaTab.mediaSessionState!!.controller).play()
        }

    @Test
    fun `GIVEN the service is not in foreground WHEN the media session receives play THEN audio focus is not requested yet and the tab is told to play`() =
        runTest {
            val mediaTab = getMediaTab(PlaybackState.PAUSED)
            val delegate = delegateWithMockedFocus(BrowserStore(BrowserState(tabs = listOf(mediaTab))))
            delegate.isForegroundService = false

            delegate.player.play()
            shadowOf(Looper.getMainLooper()).idle()
            testScheduler.advanceUntilIdle()

            verify(delegate.audioFocus, never()).request(any(), any())
            verify(mediaTab.mediaSessionState!!.controller).play()
        }

    private suspend fun TestScope.delegateWithMockedFocus(
        store: BrowserStore = BrowserStore()
    ): MediaSessionServiceDelegate {
        val notificationsDelegate: NotificationsDelegate = mock()
        whenever(notificationsDelegate.notificationManagerCompat).thenReturn(mock())
        val delegate = createDelegate(store = store, notificationsDelegate = notificationsDelegate, scope = this)
        delegate.onCreate()
        delegate.audioFocus = mock()
        doReturn(true).`when`(delegate.audioFocus).request(any(), any())
        delegate.isForegroundService = true
        val notification: Notification = mock()
        delegate.notificationHelper = coMock { doReturn(notification).`when`(this).create(any(), any()) }
        return delegate
    }

    private fun TabSessionState.withPosition(position: Double) =
        copy(
            mediaSessionState =
                mediaSessionState!!.copy(
                    positionState = MozMediaSession.PositionState(duration = 100.0, position = position)
                )
        )

    private fun TabSessionState.withPlaybackState(playbackState: PlaybackState) =
        copy(mediaSessionState = mediaSessionState!!.copy(playbackState = playbackState))

    private fun TabSessionState.withAudioSessionType(type: MozMediaSession.AudioSessionType) =
        copy(mediaSessionState = mediaSessionState!!.copy(audioSessionType = type))

    private fun getMediaTab(
        playbackState: PlaybackState = PlaybackState.PLAYING,
        features: MozMediaSession.Feature = MozMediaSession.Feature(),
    ) =
        createTab(
            title = "Mozilla",
            url = "https://www.mozilla.org",
            mediaSessionState = MediaSessionState(mock(), playbackState = playbackState, features = features),
        )
}
