/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.media.focus

import android.media.AudioManager
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.MediaSessionState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.base.crash.CrashReporting
import mozilla.components.concept.engine.mediasession.MediaSession
import mozilla.components.feature.media.service.AbstractMediaSessionService
import mozilla.components.feature.media.service.MediaSessionServiceDelegate
import mozilla.components.support.base.android.NotificationsDelegate
import mozilla.components.support.test.any
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.doReturn
import org.mockito.Mockito.never
import org.mockito.Mockito.verify
import org.mockito.Mockito.verifyNoMoreInteractions

@RunWith(AndroidJUnit4::class)
class AudioFocusTest {
    private lateinit var audioManager: AudioManager

    @Before
    fun setUp() {
        audioManager = mock()
    }

    @Test
    fun `Successful request will not change media session in state`() {
        doReturn(AudioManager.AUDIOFOCUS_REQUEST_GRANTED)
            .`when`(audioManager).requestAudioFocus(any())

        val controller: MediaSession.Controller = mock()
        val mediaSessionState = MediaSessionState(
            controller,
            playbackState = MediaSession.PlaybackState.PLAYING,
        )
        val tabSession = createTab(
            "https://www.mozilla.org",
            mediaSessionState = mediaSessionState,
        )
        val initialState = BrowserState(
            tabs = listOf(tabSession),
        )
        val store = BrowserStore(initialState)
        val service: AbstractMediaSessionService = mock()
        val crashReporter: CrashReporting = mock()
        val notificationsDelegate: NotificationsDelegate = mock()
        val delegate = MediaSessionServiceDelegate(testContext, service, store, crashReporter, notificationsDelegate)

        delegate.onCreate()

        val audioFocus = AudioFocus(audioManager, store)
        audioFocus.request(tabSession.id)

        verify(audioManager).requestAudioFocus(any())
        verifyNoMoreInteractions(mediaSessionState.controller)
    }

    @Test
    fun `Failed request will pause media session`() {
        doReturn(AudioManager.AUDIOFOCUS_REQUEST_FAILED)
            .`when`(audioManager).requestAudioFocus(any())

        val controller: MediaSession.Controller = mock()
        val mediaSessionState = MediaSessionState(
            controller,
            playbackState = MediaSession.PlaybackState.PLAYING,
        )
        val tabSession = createTab(
            "https://www.mozilla.org",
            mediaSessionState = mediaSessionState,
        )
        val initialState = BrowserState(
            tabs = listOf(tabSession),
        )
        val store = BrowserStore(initialState)
        val service: AbstractMediaSessionService = mock()
        val crashReporter: CrashReporting = mock()
        val notificationsDelegate: NotificationsDelegate = mock()
        val delegate = MediaSessionServiceDelegate(testContext, service, store, crashReporter, notificationsDelegate)

        delegate.onCreate()

        val audioFocus = AudioFocus(audioManager, store)
        audioFocus.request(tabSession.id)

        verify(audioManager).requestAudioFocus(any())
        verify(mediaSessionState.controller).pause()
    }

    @Test
    fun `Delayed request will pause media`() {
        doReturn(AudioManager.AUDIOFOCUS_REQUEST_DELAYED)
            .`when`(audioManager).requestAudioFocus(any())

        val controller: MediaSession.Controller = mock()
        val mediaSessionState = MediaSessionState(
            controller,
            playbackState = MediaSession.PlaybackState.PLAYING,
        )
        val tabSession = createTab(
            "https://www.mozilla.org",
            mediaSessionState = mediaSessionState,
        )
        val initialState = BrowserState(
            tabs = listOf(tabSession),
        )
        val store = BrowserStore(initialState)
        val service: AbstractMediaSessionService = mock()
        val crashReporter: CrashReporting = mock()
        val notificationsDelegate: NotificationsDelegate = mock()
        val delegate = MediaSessionServiceDelegate(testContext, service, store, crashReporter, notificationsDelegate)

        delegate.onCreate()

        val audioFocus = AudioFocus(audioManager, store)
        audioFocus.request(tabSession.id)

        verify(audioManager).requestAudioFocus(any())
        verify(mediaSessionState.controller).pause()
    }

    @Test
    fun `WHEN AUDIOFOCUS_REQUEST_DELAYED is received THEN transient loss callback is invoked with true`() {
        var notified = false
        val store = BrowserStore(BrowserState(tabs = listOf(createTab("https://www.mozilla.org"))))
        val audioFocus = AudioFocus(audioManager, store, onTransientFocusLoss = { notified = it })
        doReturn(AudioManager.AUDIOFOCUS_REQUEST_DELAYED)
            .`when`(audioManager).requestAudioFocus(any())

        audioFocus.request("tabId")

        assertTrue(notified)
    }

    @Test
    fun `Will pause and resume playing media on and after transient focus loss`() {
        doReturn(AudioManager.AUDIOFOCUS_REQUEST_GRANTED)
            .`when`(audioManager).requestAudioFocus(any())

        val controller: MediaSession.Controller = mock()
        val mediaSessionState = MediaSessionState(
            controller,
            playbackState = MediaSession.PlaybackState.PLAYING,
        )
        val tabSession = createTab(
            "https://www.mozilla.org",
            mediaSessionState = mediaSessionState,
        )
        val initialState = BrowserState(
            tabs = listOf(tabSession),
        )
        val store = BrowserStore(initialState)
        val service: AbstractMediaSessionService = mock()
        val crashReporter: CrashReporting = mock()
        val notificationsDelegate: NotificationsDelegate = mock()
        val delegate = MediaSessionServiceDelegate(testContext, service, store, crashReporter, notificationsDelegate)

        delegate.onCreate()

        val audioFocus = AudioFocus(audioManager, store)
        audioFocus.request(tabSession.id)

        verify(audioManager).requestAudioFocus(any())
        verifyNoMoreInteractions(mediaSessionState.controller)

        audioFocus.onAudioFocusChange(AudioManager.AUDIOFOCUS_LOSS_TRANSIENT)

        verify(mediaSessionState.controller).pause()
        verifyNoMoreInteractions(mediaSessionState.controller)

        audioFocus.onAudioFocusChange(AudioManager.AUDIOFOCUS_GAIN)

        verify(mediaSessionState.controller).play()
        verifyNoMoreInteractions(mediaSessionState.controller)
    }

    @Test
    fun `Will not resume paused media after transient focus loss`() {
        doReturn(AudioManager.AUDIOFOCUS_REQUEST_GRANTED)
            .`when`(audioManager).requestAudioFocus(any())

        val controller: MediaSession.Controller = mock()
        val mediaSessionState = MediaSessionState(
            controller,
            playbackState = MediaSession.PlaybackState.PAUSED,
        )
        val tabSession = createTab(
            "https://www.mozilla.org",
            mediaSessionState = mediaSessionState,
        )
        val initialState = BrowserState(
            tabs = listOf(tabSession),
        )
        val store = BrowserStore(initialState)
        val service: AbstractMediaSessionService = mock()
        val crashReporter: CrashReporting = mock()
        val notificationsDelegate: NotificationsDelegate = mock()
        val delegate = MediaSessionServiceDelegate(testContext, service, store, crashReporter, notificationsDelegate)

        delegate.onCreate()

        val audioFocus = AudioFocus(audioManager, store)
        audioFocus.request(tabSession.id)

        verify(audioManager).requestAudioFocus(any())
        verifyNoMoreInteractions(mediaSessionState.controller)

        audioFocus.onAudioFocusChange(AudioManager.AUDIOFOCUS_LOSS_TRANSIENT)

        verify(mediaSessionState.controller).pause()
        verifyNoMoreInteractions(mediaSessionState.controller)

        audioFocus.onAudioFocusChange(AudioManager.AUDIOFOCUS_GAIN)

        verify(mediaSessionState.controller, never()).play()
        verifyNoMoreInteractions(mediaSessionState.controller)
    }

    @Test
    fun `Will resume media sessio nplayback when gaining focus after being delayed`() {
        doReturn(AudioManager.AUDIOFOCUS_REQUEST_DELAYED)
            .`when`(audioManager).requestAudioFocus(any())

        val controller: MediaSession.Controller = mock()
        val mediaSessionState = MediaSessionState(
            controller,
            playbackState = MediaSession.PlaybackState.PLAYING,
        )
        val tabSession = createTab(
            "https://www.mozilla.org",
            mediaSessionState = mediaSessionState,
        )
        val initialState = BrowserState(
            tabs = listOf(tabSession),
        )
        val store = BrowserStore(initialState)
        val service: AbstractMediaSessionService = mock()
        val crashReporter: CrashReporting = mock()
        val notificationsDelegate: NotificationsDelegate = mock()
        val delegate = MediaSessionServiceDelegate(testContext, service, store, crashReporter, notificationsDelegate)

        delegate.onCreate()

        val audioFocus = AudioFocus(audioManager, store)
        audioFocus.request(tabSession.id)

        verify(audioManager).requestAudioFocus(any())
        verify(mediaSessionState.controller).pause()

        audioFocus.onAudioFocusChange(AudioManager.AUDIOFOCUS_GAIN)

        verify(mediaSessionState.controller).play()
        verifyNoMoreInteractions(mediaSessionState.controller)
    }

    @Test(expected = IllegalStateException::class)
    fun `An unknown audio focus response will throw an exception`() {
        doReturn(-1).`when`(audioManager).requestAudioFocus(any())

        val controller: MediaSession.Controller = mock()
        val mediaSessionState = MediaSessionState(
            controller,
            playbackState = MediaSession.PlaybackState.PLAYING,
        )
        val tabSession = createTab(
            "https://www.mozilla.org",
            mediaSessionState = mediaSessionState,
        )
        val initialState = BrowserState(
            tabs = listOf(tabSession),
        )
        val store = BrowserStore(initialState)
        val service: AbstractMediaSessionService = mock()
        val crashReporter: CrashReporting = mock()
        val notificationsDelegate: NotificationsDelegate = mock()
        val delegate = MediaSessionServiceDelegate(testContext, service, store, crashReporter, notificationsDelegate)

        delegate.onCreate()

        val audioFocus = AudioFocus(audioManager, store)
        audioFocus.request(tabSession.id)
    }

    @Test
    fun `An unknown focus change event will be ignored`() {
        doReturn(AudioManager.AUDIOFOCUS_REQUEST_GRANTED)
            .`when`(audioManager).requestAudioFocus(any())

        val controller: MediaSession.Controller = mock()
        val mediaSessionState = MediaSessionState(
            controller,
            playbackState = MediaSession.PlaybackState.PLAYING,
        )
        val tabSession = createTab(
            "https://www.mozilla.org",
            mediaSessionState = mediaSessionState,
        )
        val initialState = BrowserState(
            tabs = listOf(tabSession),
        )
        val store = BrowserStore(initialState)
        val service: AbstractMediaSessionService = mock()
        val crashReporter: CrashReporting = mock()
        val notificationsDelegate: NotificationsDelegate = mock()
        val delegate = MediaSessionServiceDelegate(testContext, service, store, crashReporter, notificationsDelegate)

        delegate.onCreate()

        val audioFocus = AudioFocus(audioManager, store)
        audioFocus.request(tabSession.id)

        verify(audioManager).requestAudioFocus(any())
        verifyNoMoreInteractions(mediaSessionState.controller)

        audioFocus.onAudioFocusChange(999)
        verifyNoMoreInteractions(mediaSessionState.controller)
    }

    @Test
    fun `An audio focus loss will pause media and regain will not resume automatically`() {
        doReturn(AudioManager.AUDIOFOCUS_REQUEST_GRANTED)
            .`when`(audioManager).requestAudioFocus(any())

        val controller: MediaSession.Controller = mock()
        val mediaSessionState = MediaSessionState(
            controller,
            playbackState = MediaSession.PlaybackState.PLAYING,
        )
        val tabSession = createTab(
            "https://www.mozilla.org",
            mediaSessionState = mediaSessionState,
        )
        val initialState = BrowserState(
            tabs = listOf(tabSession),
        )
        val store = BrowserStore(initialState)
        val service: AbstractMediaSessionService = mock()
        val crashReporter: CrashReporting = mock()
        val notificationsDelegate: NotificationsDelegate = mock()
        val delegate = MediaSessionServiceDelegate(testContext, service, store, crashReporter, notificationsDelegate)

        delegate.onCreate()

        val audioFocus = AudioFocus(audioManager, store)
        audioFocus.request(tabSession.id)

        verify(audioManager).requestAudioFocus(any())
        verifyNoMoreInteractions(mediaSessionState.controller)

        audioFocus.onAudioFocusChange(AudioManager.AUDIOFOCUS_LOSS)

        verify(mediaSessionState.controller).pause()
        verifyNoMoreInteractions(mediaSessionState.controller)

        audioFocus.onAudioFocusChange(AudioManager.AUDIOFOCUS_GAIN)
        verify(mediaSessionState.controller, never()).play()
        verifyNoMoreInteractions(mediaSessionState.controller)
    }

    @Test
    fun `WHEN AUDIOFOCUS_LOSS_TRANSIENT is received THEN transient loss callback is invoked with true`() {
        var notified = false
        val store = BrowserStore(BrowserState(tabs = listOf(createTab("https://www.mozilla.org"))))
        val audioFocus = AudioFocus(audioManager, store, onTransientFocusLoss = { notified = it })

        audioFocus.onAudioFocusChange(AudioManager.AUDIOFOCUS_LOSS_TRANSIENT)

        assertTrue(notified)
    }

    @Test
    fun `WHEN AUDIOFOCUS_GAIN is received after transient loss THEN transient loss callback is invoked with false`() {
        var notified = true
        val store = BrowserStore(BrowserState(tabs = listOf(createTab("https://www.mozilla.org"))))
        val audioFocus = AudioFocus(audioManager, store, onTransientFocusLoss = { notified = it })

        audioFocus.onAudioFocusChange(AudioManager.AUDIOFOCUS_LOSS_TRANSIENT)
        audioFocus.onAudioFocusChange(AudioManager.AUDIOFOCUS_GAIN)

        assertFalse(notified)
    }

    @Test
    fun `WHEN AUDIOFOCUS_LOSS is received THEN transient loss callback is invoked with false`() {
        var notified = true
        val store = BrowserStore(BrowserState(tabs = listOf(createTab("https://www.mozilla.org"))))
        val audioFocus = AudioFocus(audioManager, store, onTransientFocusLoss = { notified = it })

        audioFocus.onAudioFocusChange(AudioManager.AUDIOFOCUS_LOSS_TRANSIENT)
        audioFocus.onAudioFocusChange(AudioManager.AUDIOFOCUS_LOSS)

        assertFalse(notified)
    }

    @Test
    fun `WHEN audio focus is abandoned THEN transient loss callback is invoked with false`() {
        doReturn(AudioManager.AUDIOFOCUS_REQUEST_GRANTED)
            .`when`(audioManager).requestAudioFocus(any())

        var notified = true
        val store = BrowserStore(BrowserState(tabs = listOf(createTab("https://www.mozilla.org"))))
        val audioFocus = AudioFocus(audioManager, store, onTransientFocusLoss = { notified = it })

        audioFocus.onAudioFocusChange(AudioManager.AUDIOFOCUS_LOSS_TRANSIENT)
        audioFocus.abandon()

        assertFalse(notified)
    }
}
