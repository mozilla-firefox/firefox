/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.media

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.coroutines.ContinuationInterceptor
import kotlin.test.assertNotNull
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.action.MediaSessionAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.MediaSessionState
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.state.createCustomTab
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.mediasession.MediaSession
import mozilla.components.concept.engine.mediasession.MediaSession.PlaybackState
import mozilla.components.feature.media.service.MediaServiceBinder
import mozilla.components.feature.media.service.MediaSessionDelegate
import mozilla.components.feature.media.service.MediaSessionServiceDelegate
import mozilla.components.support.test.any
import mozilla.components.support.test.argumentCaptor
import mozilla.components.support.test.eq
import mozilla.components.support.test.mock
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.anyInt
import org.mockito.Mockito.atLeastOnce
import org.mockito.Mockito.doReturn
import org.mockito.Mockito.never
import org.mockito.Mockito.times
import org.mockito.Mockito.verify

@RunWith(AndroidJUnit4::class)
class MediaSessionFeatureTest {

    @Test
    fun `WHEN the feature starts THEN it starts observing the store`() = runTest {
        val store = BrowserStore()
        val feature =
            MediaSessionFeature(
                mock(),
                MediaSessionServiceDelegate::class.java,
                store,
            )
        assertNull(feature.scope)

        feature.start()

        assertNotNull(feature.scope)
    }

    @Test
    fun `GIVEN a started feature WHEN it is stopped THEN the store is not observed for updates anymore`() = runTest {
        val feature =
            MediaSessionFeature(
                mock(),
                MediaSessionServiceDelegate::class.java,
                BrowserStore(),
                mainDispatcher = coroutineContext[ContinuationInterceptor] as CoroutineDispatcher,
            )

        feature.stop()

        assertNull(feature.scope)
    }

    @Test
    fun `WHEN the media service is bound THEN store this and show the current media playing status`() = runTest {
        val mediaTab = getMediaTab(PlaybackState.PLAYING)
        val initialState = BrowserState(tabs = listOf(mediaTab))
        val store = BrowserStore(initialState)
        val mediaServiceClass = MediaSessionServiceDelegate::class.java
        val feature =
            MediaSessionFeature(
                mock(),
                mediaServiceClass,
                store,
                mainDispatcher = coroutineContext[ContinuationInterceptor] as CoroutineDispatcher,
            )

        val mediaService: MediaSessionServiceDelegate = mock()
        val binder = MediaServiceBinder(mediaService)

        feature.mediaServiceConnection.onServiceConnected(mock(), binder)

        assertEquals(mediaService, feature.mediaService)
        verify(feature.mediaService)!!.handleMediaPlaying(mediaTab)
    }

    @Test
    fun `GIVEN media service is bound WHEN the service is disconnected THEN cleanup local properties`() = runTest {
        val mediaService: ComponentName = mock()
        val feature =
            MediaSessionFeature(
                mock(),
                mediaService.javaClass,
                BrowserStore(),
                mainDispatcher = coroutineContext[ContinuationInterceptor] as CoroutineDispatcher,
            )
        feature.mediaService = mock()

        feature.mediaServiceConnection.onServiceDisconnected(mediaService)

        assertNull(feature.mediaService)
    }

    @Test
    fun `GIVEN feature is started but media service is not WHEN media starts playing THEN bind to the media service and show the playing status`() =
        runTest {
            val mockApplicationContext: Context = mock()
            val mediaTab = getMediaTab(PlaybackState.PLAYING)
            val initialState = BrowserState(tabs = listOf(mediaTab))
            val store = BrowserStore(initialState)
            val mediaServiceClass = MediaSessionServiceDelegate::class.java
            val feature =
                MediaSessionFeature(
                    mockApplicationContext,
                    mediaServiceClass,
                    store,
                    mainDispatcher = coroutineContext[ContinuationInterceptor] as CoroutineDispatcher,
                )

            doReturn(true)
                .`when`(mockApplicationContext)
                .bindService(
                    any<Intent>(),
                    any(),
                    anyInt(),
                )
            val mediaServiceIntentCaptor = argumentCaptor<Intent>()

            feature.start()
            testScheduler.advanceUntilIdle()

            verify(mockApplicationContext)
                .bindService(
                    mediaServiceIntentCaptor.capture(),
                    any(),
                    eq(Context.BIND_AUTO_CREATE),
                )
            assertEquals(
                mediaServiceClass.name,
                mediaServiceIntentCaptor.value.component!!.className,
            )
        }

    @Test
    fun `GIVEN feature and media service are started WHEN media starts playing in a normal tab THEN handle showing the new playing status`() =
        runTest {
            val mediaTab = getMediaTab(PlaybackState.PLAYING)
            val initialState = BrowserState(tabs = listOf(mediaTab))
            val store = BrowserStore(initialState)
            val feature =
                MediaSessionFeature(
                    mock(),
                    MediaSessionServiceDelegate::class.java,
                    store,
                    mainDispatcher = coroutineContext[ContinuationInterceptor] as CoroutineDispatcher,
                )

            feature.mediaService = mock()

            feature.start()
            testScheduler.advanceUntilIdle()

            verify(feature.mediaService)!!.handleMediaPlaying(mediaTab)
        }

    @Test
    fun `GIVEN media is playing WHEN the audio-session type arrives later THEN re-handle playing media with the new type`() =
        runTest {
            val mediaTab = getMediaTab(PlaybackState.PLAYING)
            val store = BrowserStore(BrowserState(tabs = listOf(mediaTab)))
            val feature =
                MediaSessionFeature(
                    mock(),
                    MediaSessionServiceDelegate::class.java,
                    store,
                    mainDispatcher = coroutineContext[ContinuationInterceptor] as CoroutineDispatcher,
                )
            feature.mediaService = mock()
            feature.start()
            testScheduler.advanceUntilIdle()

            // The audio-session type is delivered asynchronously, after playback
            // already started. Because the feature observes mediaSessionState, a
            // later type change re-handles the playing media, so audio focus is
            // re-requested with the updated type instead of staying at the default.
            store.dispatch(
                MediaSessionAction.UpdateMediaAudioSessionTypeAction(
                    mediaTab.id,
                    MediaSession.AudioSessionType.TRANSIENT,
                )
            )
            testScheduler.advanceUntilIdle()

            val captor = argumentCaptor<SessionState>()
            verify(feature.mediaService, atLeastOnce())!!.handleMediaPlaying(captor.capture())
            assertEquals(
                MediaSession.AudioSessionType.TRANSIENT,
                captor.value.mediaSessionState?.audioSessionType,
            )
        }

    @Test
    fun `GIVEN media is playing WHEN the page title loads THEN only the media content is updated`() = runTest {
        val mediaTab = getMediaTab(PlaybackState.PLAYING)
        val store = BrowserStore(BrowserState(tabs = listOf(mediaTab)))
        val feature =
            MediaSessionFeature(
                mock(),
                MediaSessionServiceDelegate::class.java,
                store,
                mainDispatcher = coroutineContext[ContinuationInterceptor] as CoroutineDispatcher,
            )
        feature.mediaService = mock()
        feature.start()
        testScheduler.advanceUntilIdle()

        store.dispatch(ContentAction.UpdateTitleAction(mediaTab.id, "Mozilla"))
        testScheduler.advanceUntilIdle()

        verify(feature.mediaService, times(1))!!.handleMediaPlaying(any())
        val captor = argumentCaptor<SessionState>()
        verify(feature.mediaService)!!.handleMediaUpdated(captor.capture())
        assertEquals("Mozilla", captor.value.content.title)
    }

    @Test
    fun `GIVEN media is paused WHEN the favicon loads THEN only the media content is updated`() = runTest {
        val mediaTab = getMediaTab(PlaybackState.PAUSED)
        val store = BrowserStore(BrowserState(tabs = listOf(mediaTab)))
        val feature =
            MediaSessionFeature(
                mock(),
                MediaSessionServiceDelegate::class.java,
                store,
                mainDispatcher = coroutineContext[ContinuationInterceptor] as CoroutineDispatcher,
            )
        feature.mediaService = mock()
        feature.start()
        testScheduler.advanceUntilIdle()

        val icon = Bitmap.createBitmap(1, 1, Bitmap.Config.ARGB_8888)
        store.dispatch(ContentAction.UpdateIconAction(mediaTab.id, mediaTab.content.url, icon))
        testScheduler.advanceUntilIdle()

        verify(feature.mediaService, times(1))!!.handleMediaPaused(any())
        val captor = argumentCaptor<SessionState>()
        verify(feature.mediaService)!!.handleMediaUpdated(captor.capture())
        assertEquals(icon, captor.value.content.icon)
    }

    @Test
    fun `GIVEN media with a metadata title WHEN the page title changes THEN the media content is not updated`() =
        runTest {
            val mediaTab =
                createTab(
                    "https://www.mozilla.org",
                    mediaSessionState =
                        MediaSessionState(
                            mock(),
                            metadata = MediaSession.Metadata(title = "Song", getArtwork = null),
                            playbackState = PlaybackState.PLAYING,
                        ),
                )
            val store = BrowserStore(BrowserState(tabs = listOf(mediaTab)))
            val feature =
                MediaSessionFeature(
                    mock(),
                    MediaSessionServiceDelegate::class.java,
                    store,
                    mainDispatcher = coroutineContext[ContinuationInterceptor] as CoroutineDispatcher,
                )
            feature.mediaService = mock()
            feature.start()
            testScheduler.advanceUntilIdle()

            store.dispatch(ContentAction.UpdateTitleAction(mediaTab.id, "Mozilla"))
            testScheduler.advanceUntilIdle()

            verify(feature.mediaService, never())!!.handleMediaUpdated(any())
        }

    @Test
    fun `GIVEN media in a private tab WHEN the favicon loads THEN the media content is not updated`() = runTest {
        val mediaTab =
            createTab(
                "https://www.mozilla.org",
                private = true,
                mediaSessionState = MediaSessionState(mock(), playbackState = PlaybackState.PLAYING),
            )
        val store = BrowserStore(BrowserState(tabs = listOf(mediaTab)))
        val feature =
            MediaSessionFeature(
                mock(),
                MediaSessionServiceDelegate::class.java,
                store,
                mainDispatcher = coroutineContext[ContinuationInterceptor] as CoroutineDispatcher,
            )
        feature.mediaService = mock()
        feature.start()
        testScheduler.advanceUntilIdle()

        val icon = Bitmap.createBitmap(1, 1, Bitmap.Config.ARGB_8888)
        store.dispatch(ContentAction.UpdateIconAction(mediaTab.id, mediaTab.content.url, icon))
        testScheduler.advanceUntilIdle()

        verify(feature.mediaService, never())!!.handleMediaUpdated(any())
    }

    @Test
    fun `GIVEN feature and media service are started WHEN media starts playing in a custom tab THEN handle showing the new playing status`() =
        runTest {
            val mediaTab = getCustomTabWithMedia(PlaybackState.PLAYING)
            val initialState = BrowserState(customTabs = listOf(mediaTab))
            val store = BrowserStore(initialState)
            val feature =
                MediaSessionFeature(
                    mock(),
                    MediaSessionServiceDelegate::class.java,
                    store,
                    mainDispatcher = coroutineContext[ContinuationInterceptor] as CoroutineDispatcher,
                )

            feature.mediaService = mock()

            feature.start()
            testScheduler.advanceUntilIdle()

            verify(feature.mediaService)!!.handleMediaPlaying(mediaTab)
        }

    @Test
    fun `GIVEN feature and media service are started WHEN media is paused in a normal tab THEN handle showing the new playing status`() =
        runTest {
            val mediaTab = getMediaTab(PlaybackState.PAUSED)
            val initialState = BrowserState(tabs = listOf(mediaTab))
            val store = BrowserStore(initialState)
            val feature =
                MediaSessionFeature(
                    mock(),
                    MediaSessionServiceDelegate::class.java,
                    store,
                    mainDispatcher = coroutineContext[ContinuationInterceptor] as CoroutineDispatcher,
                )

            feature.mediaService = mock()

            feature.start()
            testScheduler.advanceUntilIdle()

            verify(feature.mediaService)!!.handleMediaPaused(mediaTab)
        }

    @Test
    fun `GIVEN feature and media service are started WHEN media is paused in a custom tab THEN handle showing the new playing status`() =
        runTest {
            val mediaTab = getCustomTabWithMedia(PlaybackState.PAUSED)
            val initialState = BrowserState(customTabs = listOf(mediaTab))
            val store = BrowserStore(initialState)
            val feature =
                MediaSessionFeature(
                    mock(),
                    MediaSessionServiceDelegate::class.java,
                    store,
                    mainDispatcher = coroutineContext[ContinuationInterceptor] as CoroutineDispatcher,
                )

            feature.mediaService = mock()

            feature.start()
            testScheduler.advanceUntilIdle()

            verify(feature.mediaService)!!.handleMediaPaused(mediaTab)
        }

    @Test
    fun `GIVEN feature and media service are started WHEN media is stopped in a normal tab THEN handle showing the new playing status`() =
        runTest {
            val mediaTab = getMediaTab(PlaybackState.STOPPED)
            val initialState = BrowserState(tabs = listOf(mediaTab))
            val store = BrowserStore(initialState)
            val feature =
                MediaSessionFeature(
                    mock(),
                    MediaSessionServiceDelegate::class.java,
                    store,
                    mainDispatcher = coroutineContext[ContinuationInterceptor] as CoroutineDispatcher,
                )

            feature.mediaService = mock()

            feature.start()
            testScheduler.advanceUntilIdle()

            verify(feature.mediaService)!!.handleMediaStopped(mediaTab)
        }

    @Test
    fun `GIVEN feature and media service are started WHEN media is stopped in a custom tab THEN handle showing the new playing status`() =
        runTest {
            val mediaTab = getCustomTabWithMedia(PlaybackState.STOPPED)
            val initialState = BrowserState(customTabs = listOf(mediaTab))
            val store = BrowserStore(initialState)
            val feature =
                MediaSessionFeature(
                    mock(),
                    MediaSessionServiceDelegate::class.java,
                    store,
                    mainDispatcher = coroutineContext[ContinuationInterceptor] as CoroutineDispatcher,
                )

            feature.mediaService = mock()

            feature.start()
            testScheduler.advanceUntilIdle()

            verify(feature.mediaService)!!.handleMediaStopped(mediaTab)
        }

    @Test
    fun `GIVEN feature and media service are started WHEN the media status is unknown THEN disconnect the media service and cleanup`() =
        runTest {
            val mockApplicationContext: Context = mock()
            val mediaTab = getMediaTab(PlaybackState.UNKNOWN)
            val initialState = BrowserState(tabs = listOf(mediaTab))
            val store = BrowserStore(initialState)
            val feature =
                MediaSessionFeature(
                    mockApplicationContext,
                    MediaSessionServiceDelegate::class.java,
                    store,
                    mainDispatcher = coroutineContext[ContinuationInterceptor] as CoroutineDispatcher,
                )

            val mediaService: MediaSessionDelegate = mock()
            feature.mediaService = mediaService

            feature.start()
            testScheduler.advanceUntilIdle()

            verify(mediaService).handleNoMedia()
            verify(mockApplicationContext).unbindService(feature.mediaServiceConnection)
            assertNull(feature.mediaService)
        }

    @Test
    fun `GIVEN feature and media service are started WHEN there is no media tab THEN stop the media service and cleanup`() =
        runTest {
            val mockApplicationContext: Context = mock()
            val initialState = BrowserState()
            val store = BrowserStore(initialState)
            val feature =
                MediaSessionFeature(
                    mockApplicationContext,
                    MediaSessionServiceDelegate::class.java,
                    store,
                    mainDispatcher = coroutineContext[ContinuationInterceptor] as CoroutineDispatcher,
                )

            val mediaService: MediaSessionDelegate = mock()
            feature.mediaService = mediaService

            feature.start()
            testScheduler.advanceUntilIdle()

            verify(mediaService).handleNoMedia()
            verify(mockApplicationContext).unbindService(feature.mediaServiceConnection)
            assertNull(feature.mediaService)
        }

    @Test
    fun `GIVEN a normal tab is playing media WHEN media is deactivated THEN stop the media service and cleanup`() =
        runTest {
            val mockApplicationContext: Context = mock()
            val mediaTab = getMediaTab(PlaybackState.PLAYING)
            val initialState = BrowserState(tabs = listOf(mediaTab))
            val store = BrowserStore(initialState)
            val feature =
                MediaSessionFeature(
                    mockApplicationContext,
                    MediaSessionServiceDelegate::class.java,
                    store,
                    mainDispatcher = coroutineContext[ContinuationInterceptor] as CoroutineDispatcher,
                )

            val mediaService: MediaSessionDelegate = mock()
            feature.mediaService = mediaService

            feature.start()
            testScheduler.advanceUntilIdle()

            store.dispatch(MediaSessionAction.DeactivatedMediaSessionAction(store.state.tabs[0].id))
            testScheduler.advanceUntilIdle()

            verify(mediaService).handleNoMedia()
            verify(mockApplicationContext).unbindService(feature.mediaServiceConnection)
            assertNull(feature.mediaService)
        }

    @Test
    fun `GIVEN a custom tab is playing media WHEN media is deactivated THEN stop the media service and cleanup`() =
        runTest {
            val mockApplicationContext: Context = mock()
            val mediaTab = getMediaTab(PlaybackState.UNKNOWN)
            val customTabWithMedia = getCustomTabWithMedia(PlaybackState.PLAYING)
            val initialState =
                BrowserState(
                    tabs = listOf(mediaTab),
                    customTabs = listOf(customTabWithMedia),
                )
            val store = BrowserStore(initialState)
            val feature =
                MediaSessionFeature(
                    mockApplicationContext,
                    MediaSessionServiceDelegate::class.java,
                    store,
                    mainDispatcher = coroutineContext[ContinuationInterceptor] as CoroutineDispatcher,
                )

            val mediaService: MediaSessionDelegate = mock()
            feature.mediaService = mediaService

            feature.start()
            testScheduler.advanceUntilIdle()

            store.dispatch(MediaSessionAction.DeactivatedMediaSessionAction(store.state.customTabs[0].id))
            testScheduler.advanceUntilIdle()

            verify(mediaService).handleNoMedia()
            verify(mockApplicationContext).unbindService(feature.mediaServiceConnection)
            assertNull(feature.mediaService)
        }

    private fun getMediaTab(playbackState: PlaybackState = PlaybackState.PLAYING) =
        createTab(
            "https://www.mozilla.org",
            mediaSessionState =
                MediaSessionState(
                    mock(),
                    playbackState = playbackState,
                ),
        )

    private fun getCustomTabWithMedia(playbackState: PlaybackState = PlaybackState.PLAYING) =
        createCustomTab(
            "https://www.mozilla.org",
            mediaSessionState =
                MediaSessionState(
                    mock(),
                    playbackState = playbackState,
                ),
        )
}
