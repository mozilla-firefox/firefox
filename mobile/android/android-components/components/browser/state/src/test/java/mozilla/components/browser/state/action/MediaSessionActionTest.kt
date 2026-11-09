/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.state.action

import kotlin.test.assertNotNull
import mozilla.components.browser.state.reducer.BrowserStateReducer
import mozilla.components.browser.state.selector.findTab
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.MediaSessionState
import mozilla.components.browser.state.state.createTab
import mozilla.components.concept.engine.mediasession.MediaSession
import mozilla.components.support.test.mock
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class MediaSessionActionTest {
    @Test
    fun `ActivatedMediaSessionAction updates media session state`() {
        var state = BrowserState(tabs = listOf(createTab("https://www.mozilla.org", id = "test-tab")))

        val mediaSessionController: MediaSession.Controller = mock()

        state =
            BrowserStateReducer.reduce(
                state,
                MediaSessionAction.ActivatedMediaSessionAction(
                    "test-tab",
                    mediaSessionController,
                ),
            )

        val mediaSessionState: MediaSessionState? = state.findTab("test-tab")?.mediaSessionState
        assertNotNull(mediaSessionState)
        assertEquals(mediaSessionController, mediaSessionState.controller)
    }

    @Test
    fun `DeactivatedMediaSessionAction updates media session state`() {
        var state = BrowserState(tabs = listOf(createTab("https://www.mozilla.org", id = "test-tab")))

        val mediaSessionController: MediaSession.Controller = mock()

        state =
            BrowserStateReducer.reduce(
                state,
                MediaSessionAction.ActivatedMediaSessionAction(
                    "test-tab",
                    mediaSessionController,
                ),
            )

        state =
            BrowserStateReducer.reduce(
                state,
                MediaSessionAction.DeactivatedMediaSessionAction("test-tab"),
            )

        val mediaSessionState: MediaSessionState? = state.findTab("test-tab")?.mediaSessionState
        assertNull(mediaSessionState)
    }

    @Test
    fun `UpdateMediaMetadataAction updates media session state`() {
        var state = BrowserState(tabs = listOf(createTab("https://www.mozilla.org", id = "test-tab")))

        val mediaSessionController: MediaSession.Controller = mock()
        val metadata: MediaSession.Metadata = mock()

        state =
            BrowserStateReducer.reduce(
                state,
                MediaSessionAction.ActivatedMediaSessionAction(
                    "test-tab",
                    mediaSessionController,
                ),
            )

        state =
            BrowserStateReducer.reduce(
                state,
                MediaSessionAction.UpdateMediaMetadataAction(
                    "test-tab",
                    metadata,
                ),
            )

        val mediaSessionState: MediaSessionState? = state.findTab("test-tab")?.mediaSessionState
        assertNotNull(mediaSessionState)
        assertEquals(mediaSessionController, mediaSessionState.controller)
        assertEquals(metadata, mediaSessionState.metadata)
    }

    @Test
    fun `UpdateMediaMetadataAction resets the position state only when the title changes`() {
        val position = MediaSession.PositionState(duration = 100.0, position = 30.0, playbackRate = 1.0)
        var state = BrowserState(tabs = listOf(createTab("https://www.mozilla.org", id = "test-tab")))
        fun reduce(action: MediaSessionAction) = BrowserStateReducer.reduce(state, action).also { state = it }
        fun position() = state.findTab("test-tab")?.mediaSessionState?.positionState
        fun metadata(title: String, artist: String? = null) =
            reduce(
                MediaSessionAction.UpdateMediaMetadataAction(
                    "test-tab",
                    MediaSession.Metadata(title, artist, getArtwork = null),
                )
            )

        reduce(MediaSessionAction.ActivatedMediaSessionAction("test-tab", mock()))
        reduce(MediaSessionAction.UpdateMediaPositionStateAction("test-tab", position))
        metadata("A")
        assertEquals(position, position())

        metadata("A", "X")
        assertEquals(position, position())

        metadata("B")
        assertEquals(position.copy(position = 0.0), position())
    }

    @Test
    fun `UpdateMediaPlaybackStateAction updates media session state`() {
        var state = BrowserState(tabs = listOf(createTab("https://www.mozilla.org", id = "test-tab")))

        val mediaSessionController: MediaSession.Controller = mock()
        val playbackState: MediaSession.PlaybackState = mock()

        state =
            BrowserStateReducer.reduce(
                state,
                MediaSessionAction.ActivatedMediaSessionAction(
                    "test-tab",
                    mediaSessionController,
                ),
            )

        state =
            BrowserStateReducer.reduce(
                state,
                MediaSessionAction.UpdateMediaPlaybackStateAction(
                    "test-tab",
                    playbackState,
                ),
            )

        val mediaSessionState: MediaSessionState? = state.findTab("test-tab")?.mediaSessionState
        assertNotNull(mediaSessionState)
        assertEquals(mediaSessionController, mediaSessionState.controller)
        assertEquals(playbackState, mediaSessionState.playbackState)
    }

    @Test
    fun `UpdateMediaFeatureAction updates media session state`() {
        var state = BrowserState(tabs = listOf(createTab("https://www.mozilla.org", id = "test-tab")))

        val mediaSessionController: MediaSession.Controller = mock()
        val features: MediaSession.Feature = mock()

        state =
            BrowserStateReducer.reduce(
                state,
                MediaSessionAction.ActivatedMediaSessionAction(
                    "test-tab",
                    mediaSessionController,
                ),
            )

        state =
            BrowserStateReducer.reduce(
                state,
                MediaSessionAction.UpdateMediaFeatureAction(
                    "test-tab",
                    features,
                ),
            )

        val mediaSessionState: MediaSessionState? = state.findTab("test-tab")?.mediaSessionState
        assertNotNull(mediaSessionState)
        assertEquals(mediaSessionController, mediaSessionState.controller)
        assertEquals(features, mediaSessionState.features)
    }

    @Test
    fun `UpdateMediaPositionStateAction updates media session state`() {
        var state = BrowserState(tabs = listOf(createTab("https://www.mozilla.org", id = "test-tab")))

        val mediaSessionController: MediaSession.Controller = mock()
        val positionState: MediaSession.PositionState = mock()

        state =
            BrowserStateReducer.reduce(
                state,
                MediaSessionAction.ActivatedMediaSessionAction(
                    "test-tab",
                    mediaSessionController,
                ),
            )

        state =
            BrowserStateReducer.reduce(
                state,
                MediaSessionAction.UpdateMediaPositionStateAction(
                    "test-tab",
                    positionState,
                ),
            )

        val mediaSessionState: MediaSessionState? = state.findTab("test-tab")?.mediaSessionState
        assertNotNull(mediaSessionState)
        assertEquals(mediaSessionController, mediaSessionState.controller)
        assertEquals(positionState, mediaSessionState.positionState)
    }

    @Test
    fun `UpdateMediaMutedAction updates media session state`() {
        var state = BrowserState(tabs = listOf(createTab("https://www.mozilla.org", id = "test-tab")))

        val mediaSessionController: MediaSession.Controller = mock()

        state =
            BrowserStateReducer.reduce(
                state,
                MediaSessionAction.ActivatedMediaSessionAction(
                    "test-tab",
                    mediaSessionController,
                ),
            )

        state =
            BrowserStateReducer.reduce(
                state,
                MediaSessionAction.UpdateMediaMutedAction(
                    "test-tab",
                    true,
                ),
            )

        val mediaSessionState: MediaSessionState? = state.findTab("test-tab")?.mediaSessionState
        assertNotNull(mediaSessionState)
        assertEquals(mediaSessionController, mediaSessionState.controller)
        assertEquals(true, mediaSessionState.muted)
    }

    @Test
    fun `UpdateMediaFullscreenAction updates media session state`() {
        var state = BrowserState(tabs = listOf(createTab("https://www.mozilla.org", id = "test-tab")))

        val mediaSessionController: MediaSession.Controller = mock()
        val elementMetadata: MediaSession.ElementMetadata = mock()

        state =
            BrowserStateReducer.reduce(
                state,
                MediaSessionAction.ActivatedMediaSessionAction(
                    "test-tab",
                    mediaSessionController,
                ),
            )

        state =
            BrowserStateReducer.reduce(
                state,
                MediaSessionAction.UpdateMediaFullscreenAction(
                    "test-tab",
                    true,
                    elementMetadata,
                ),
            )

        val mediaSessionState: MediaSessionState? = state.findTab("test-tab")?.mediaSessionState
        assertNotNull(mediaSessionState)
        assertEquals(mediaSessionController, mediaSessionState.controller)
        assertEquals(true, mediaSessionState.fullscreen)
        assertEquals(elementMetadata, mediaSessionState.elementMetadata)
    }

    @Test
    fun `updates are ignore if media session is not activated`() {
        var state = BrowserState(tabs = listOf(createTab("https://www.mozilla.org", id = "test-tab")))

        val elementMetadata: MediaSession.ElementMetadata = mock()

        state =
            BrowserStateReducer.reduce(
                state,
                MediaSessionAction.UpdateMediaFullscreenAction(
                    "test-tab",
                    true,
                    elementMetadata,
                ),
            )

        val mediaSessionState: MediaSessionState? = state.findTab("test-tab")?.mediaSessionState
        assertNull(mediaSessionState)

        state =
            BrowserStateReducer.reduce(
                state,
                MediaSessionAction.UpdateMediaMutedAction(
                    "test-tab",
                    true,
                ),
            )
        assertNull(mediaSessionState)
    }
}
