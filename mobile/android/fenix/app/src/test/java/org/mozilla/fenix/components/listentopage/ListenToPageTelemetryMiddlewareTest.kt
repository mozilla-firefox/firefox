/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.listentopage

import java.util.Locale
import kotlin.test.assertNotNull
import mozilla.components.feature.listentopage.ArticleProgress
import mozilla.components.feature.listentopage.ChunkState
import mozilla.components.feature.listentopage.ListenAction
import mozilla.components.feature.listentopage.ListenState
import mozilla.components.feature.listentopage.ListenStore
import mozilla.components.feature.listentopage.PlaybackPhase
import mozilla.components.feature.listentopage.PlaybackSpeed
import mozilla.components.feature.listentopage.PlaybackState
import mozilla.components.feature.listentopage.Voice
import mozilla.components.feature.listentopage.VoiceState
import mozilla.components.feature.listentopage.content.LanguageSource
import mozilla.components.feature.listentopage.listenReducer
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.ListenToPage
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class ListenToPageTelemetryMiddlewareTest {

    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    @Test
    fun `WHEN the play button is pressed while not playing THEN record play_pressed`() {
        val store = createStore(phase = PlaybackPhase.Paused)
        assertNull(ListenToPage.playPressed.testGetValue())

        store.dispatch(ListenAction.Controls.PlayPauseClicked)

        assertNotNull(ListenToPage.playPressed.testGetValue())
        assertNull(ListenToPage.pausePressed.testGetValue())
    }

    @Test
    fun `WHEN the play button is pressed while playing THEN record pause_pressed`() {
        val store = createStore(phase = PlaybackPhase.Playing)
        assertNull(ListenToPage.pausePressed.testGetValue())

        store.dispatch(ListenAction.Controls.PlayPauseClicked)

        assertNotNull(ListenToPage.pausePressed.testGetValue())
        assertNull(ListenToPage.playPressed.testGetValue())
    }

    @Test
    fun `WHEN the play button is pressed while buffering THEN record pause_pressed`() {
        val store = createStore(phase = PlaybackPhase.Buffering)

        store.dispatch(ListenAction.Controls.PlayPauseClicked)

        assertNotNull(ListenToPage.pausePressed.testGetValue())
        assertNull(ListenToPage.playPressed.testGetValue())
    }

    @Test
    fun `WHEN skip back is pressed THEN record skip_back_pressed`() {
        val store = createStore(phase = PlaybackPhase.Playing)

        store.dispatch(ListenAction.Controls.RewindClicked)

        assertNotNull(ListenToPage.skipBackPressed.testGetValue())
    }

    @Test
    fun `WHEN skip forward is pressed THEN record skip_forward_pressed`() {
        val store = createStore(phase = PlaybackPhase.Playing)

        store.dispatch(ListenAction.Controls.ForwardClicked)

        assertNotNull(ListenToPage.skipForwardPressed.testGetValue())
    }

    @Test
    fun `WHEN the progress bar is seeked ahead of the current position THEN record seek_performed forward`() {
        val store = createStore(phase = PlaybackPhase.Playing, positionMs = 5_000L, durationMs = 20_000L)

        store.dispatch(ListenAction.Playback.SeekRequested(fraction = 0.75f))

        assertEquals("forward", ListenToPage.seekPerformed.testGetValue()!!.single().extra?.get("direction"))
    }

    @Test
    fun `WHEN the progress bar is seeked behind the current position THEN record seek_performed backward`() {
        val store = createStore(phase = PlaybackPhase.Playing, positionMs = 15_000L, durationMs = 20_000L)

        store.dispatch(ListenAction.Playback.SeekRequested(fraction = 0.25f))

        assertEquals("backward", ListenToPage.seekPerformed.testGetValue()!!.single().extra?.get("direction"))
    }

    @Test
    fun `WHEN the voice list is opened THEN record voice_list_opened`() {
        val store = createStore(phase = PlaybackPhase.Paused)

        store.dispatch(ListenAction.Controls.VoicesClicked)

        assertNotNull(ListenToPage.voiceListOpened.testGetValue())
    }

    @Test
    fun `WHEN the voice list is dismissed THEN record voice_list_dismissed`() {
        val store = createStore(phase = PlaybackPhase.Paused)

        store.dispatch(ListenAction.Controls.VoicesDismissed)

        assertNotNull(ListenToPage.voiceListDismissed.testGetValue())
    }

    @Test
    fun `WHEN the default voice is selected THEN record voice_selected is_default true`() {
        val default = Voice(id = "en-us", locale = Locale.US)
        val store =
            createStore(phase = PlaybackPhase.Paused, availableVoices = listOf(default, Voice("en-gb", Locale.UK)))

        store.dispatch(ListenAction.Voices.VoiceSelected(default))

        assertEquals("true", ListenToPage.voiceSelected.testGetValue()!!.single().extra?.get("is_default"))
    }

    @Test
    fun `WHEN a non-default voice is selected THEN record voice_selected is_default false`() {
        val default = Voice(id = "en-us", locale = Locale.US)
        val other = Voice(id = "en-gb", locale = Locale.UK)
        val store = createStore(phase = PlaybackPhase.Paused, availableVoices = listOf(default, other))

        store.dispatch(ListenAction.Voices.VoiceSelected(other))

        assertEquals("false", ListenToPage.voiceSelected.testGetValue()!!.single().extra?.get("is_default"))
    }

    @Test
    fun `WHEN the speed list is opened THEN record speed_list_opened`() {
        val store = createStore(phase = PlaybackPhase.Paused)

        store.dispatch(ListenAction.Controls.PlaybackSpeedClicked)

        assertNotNull(ListenToPage.speedListOpened.testGetValue())
    }

    @Test
    fun `WHEN the speed list is dismissed THEN record speed_list_dismissed`() {
        val store = createStore(phase = PlaybackPhase.Paused)

        store.dispatch(ListenAction.Controls.PlaybackSpeedDismissed)

        assertNotNull(ListenToPage.speedListDismissed.testGetValue())
    }

    @Test
    fun `WHEN a speed is selected THEN record speed_selected with the multiplier`() {
        val store = createStore(phase = PlaybackPhase.Paused)

        store.dispatch(ListenAction.Controls.PlaybackSpeedSelected(PlaybackSpeed.X1_5))

        assertEquals("1.5", ListenToPage.speedSelected.testGetValue()!!.single().extra?.get("speed"))
    }

    @Test
    fun `WHEN the user selects a voice THEN record voice_selected source user_selected`() {
        val voice = Voice(id = "en-us", locale = Locale.US)
        val store = createStore(phase = PlaybackPhase.Paused, availableVoices = listOf(voice))

        store.dispatch(ListenAction.Voices.VoiceSelected(voice, ListenAction.Voices.VoiceSource.UserSelected))

        assertEquals("user_selected", ListenToPage.voiceSelected.testGetValue()!!.single().extra?.get("source"))
    }

    @Test
    fun `WHEN a voice is chosen as a fallback THEN record voice_selected source fallback`() {
        val voice = Voice(id = "en-us", locale = Locale.US)
        val store = createStore(phase = PlaybackPhase.Paused, availableVoices = listOf(voice))

        store.dispatch(ListenAction.Voices.VoiceSelected(voice, ListenAction.Voices.VoiceSource.Fallback))

        assertEquals("fallback", ListenToPage.voiceSelected.testGetValue()!!.single().extra?.get("source"))
    }

    @Test
    fun `WHEN listening is requested THEN record requested`() {
        val store = createStore(phase = PlaybackPhase.Idle)

        store.dispatch(ListenAction.Session.ListenRequested(tabId = "tab-1", url = "https://example.org"))

        assertNotNull(ListenToPage.requested.testGetValue())
    }

    @Test
    fun `WHEN content is ready THEN record content_ready with its language`() {
        val store = createStore(phase = PlaybackPhase.Idle)

        store.dispatch(ListenAction.Content.ContentReady(languageTag = "en-US"))

        assertEquals("en-US", ListenToPage.contentReady.testGetValue()!!.single().extra?.get("language"))
    }

    @Test
    fun `WHEN content is ready in the language the page declares THEN record content_ready with the page source`() {
        val store = createStore(phase = PlaybackPhase.Idle)

        store.dispatch(ListenAction.Content.ContentReady(languageTag = "de", languageSource = LanguageSource.Page))

        val extra = ListenToPage.contentReady.testGetValue()!!.single().extra
        assertEquals("de", extra?.get("language"))
        assertEquals("page", extra?.get("language_source"))
    }

    @Test
    fun `WHEN content is ready in the fallback language THEN record content_ready with the device source`() {
        val store = createStore(phase = PlaybackPhase.Idle)

        store.dispatch(
            ListenAction.Content.ContentReady(languageTag = "en-US", languageSource = LanguageSource.Fallback)
        )

        val extra = ListenToPage.contentReady.testGetValue()!!.single().extra
        assertEquals("en-US", extra?.get("language"))
        assertEquals("device", extra?.get("language_source"))
    }

    @Test
    fun `WHEN playback starts across chunks THEN record playback_started once per session`() {
        val store = createStore(phase = PlaybackPhase.Buffering)

        store.dispatch(ListenAction.Playback.PlaybackStarted(chunk = ChunkState(index = 0), positionMs = 0L))
        store.dispatch(ListenAction.Playback.PlaybackStarted(chunk = ChunkState(index = 1), positionMs = 0L))

        assertEquals(1, ListenToPage.playbackStarted.testGetValue()!!.size)
    }

    @Test
    fun `WHEN a new session starts THEN playback_started can record again`() {
        val store = createStore(phase = PlaybackPhase.Buffering)

        store.dispatch(ListenAction.Playback.PlaybackStarted(chunk = ChunkState(index = 0), positionMs = 0L))
        store.dispatch(ListenAction.Session.ListenRequested(tabId = "tab-1", url = "https://example.org"))
        store.dispatch(ListenAction.Playback.PlaybackStarted(chunk = ChunkState(index = 0), positionMs = 0L))

        assertEquals(2, ListenToPage.playbackStarted.testGetValue()!!.size)
    }

    @Test
    fun `WHEN playback ends THEN record playback_ended`() {
        val store = createStore(phase = PlaybackPhase.Playing)

        store.dispatch(ListenAction.Playback.PlaybackEnded)

        assertNotNull(ListenToPage.playbackEnded.testGetValue())
    }

    @Test
    fun `WHEN playback ends more than once THEN record playback_ended once per session`() {
        val store = createStore(phase = PlaybackPhase.Playing)

        store.dispatch(ListenAction.Playback.PlaybackEnded)
        store.dispatch(ListenAction.Playback.PlaybackEnded)

        assertEquals(1, ListenToPage.playbackEnded.testGetValue()!!.size)
    }

    @Test
    fun `WHEN a new session starts THEN playback_ended can record again`() {
        val store = createStore(phase = PlaybackPhase.Playing)

        store.dispatch(ListenAction.Playback.PlaybackEnded)
        store.dispatch(ListenAction.Session.ListenRequested(tabId = "tab-1", url = "https://example.org"))
        store.dispatch(ListenAction.Playback.PlaybackEnded)

        assertEquals(2, ListenToPage.playbackEnded.testGetValue()!!.size)
    }

    @Test
    fun `WHEN the session is stopped from the close button THEN record stopped with close_button`() {
        val store = createStore(phase = PlaybackPhase.Playing)

        store.dispatch(ListenAction.Session.StopRequested(ListenAction.Session.StopSource.CloseButton))

        assertEquals("close_button", ListenToPage.stopped.testGetValue()!!.single().extra?.get("source"))
    }

    @Test
    fun `WHEN the session is stopped by the tab closing THEN record stopped with tab_closed`() {
        val store = createStore(phase = PlaybackPhase.Playing)

        store.dispatch(ListenAction.Session.StopRequested(ListenAction.Session.StopSource.TabClosed))

        assertEquals("tab_closed", ListenToPage.stopped.testGetValue()!!.single().extra?.get("source"))
    }

    @Test
    fun `WHEN the session is stopped by closing reader mode THEN record stopped with reader_mode_closed`() {
        val store = createStore(phase = PlaybackPhase.Playing)

        store.dispatch(ListenAction.Session.StopRequested(ListenAction.Session.StopSource.ReaderModeClosed))

        assertEquals("reader_mode_closed", ListenToPage.stopped.testGetValue()!!.single().extra?.get("source"))
    }

    @Test
    fun `WHEN the session is stopped by turning the feature off THEN record stopped with feature_disabled`() {
        val store = createStore(phase = PlaybackPhase.Paused)

        store.dispatch(ListenAction.Session.StopRequested(ListenAction.Session.StopSource.FeatureDisabled))

        assertEquals("feature_disabled", ListenToPage.stopped.testGetValue()!!.single().extra?.get("source"))
    }

    @Test
    fun `WHEN the media session ended outside the app THEN record stopped with media_session_ended_outside_app`() {
        val store = createStore(phase = PlaybackPhase.Paused)

        store.dispatch(ListenAction.Session.StopRequested(ListenAction.Session.StopSource.MediaSessionEndedOutsideApp))

        assertEquals(
            "media_session_ended_outside_app",
            ListenToPage.stopped.testGetValue()!!.single().extra?.get("source"),
        )
    }

    @Test
    fun `WHEN no offline voice is available THEN record no_offline_voice with the language`() {
        val store = createStore(phase = PlaybackPhase.Idle, languageTag = "en-US")

        store.dispatch(ListenAction.Voices.NoOfflineVoicesAvailable)

        assertEquals("en-US", ListenToPage.noOfflineVoice.testGetValue()!!.single().extra?.get("language"))
    }

    @Test
    fun `WHEN content is unavailable THEN record content_unavailable`() {
        val store = createStore(phase = PlaybackPhase.Idle)

        store.dispatch(ListenAction.Content.ContentUnavailable)

        assertNotNull(ListenToPage.contentUnavailable.testGetValue())
    }

    @Test
    fun `WHEN synthesis fails THEN record synthesis_failed`() {
        val store = createStore(phase = PlaybackPhase.Buffering)

        store.dispatch(ListenAction.Synthesis.SynthesisFailed)

        assertNotNull(ListenToPage.synthesisFailed.testGetValue())
    }

    @Test
    fun `WHEN playback fails THEN record playback_failed`() {
        val store = createStore(phase = PlaybackPhase.Playing)

        store.dispatch(ListenAction.Playback.PlaybackFailed)

        assertNotNull(ListenToPage.playbackFailed.testGetValue())
    }

    @Test
    fun `WHEN an untracked control is pressed THEN record nothing`() {
        val store = createStore(phase = PlaybackPhase.Playing)

        store.dispatch(ListenAction.Controls.PlaybackSpeedClicked)

        assertNull(ListenToPage.playPressed.testGetValue())
        assertNull(ListenToPage.voiceListOpened.testGetValue())
    }

    private fun createStore(
        phase: PlaybackPhase,
        positionMs: Long = 0L,
        durationMs: Long = 0L,
        availableVoices: List<Voice> = emptyList(),
        languageTag: String? = null,
    ) =
        ListenStore(
            initialState =
                ListenState(
                    languageTag = languageTag,
                    playbackState = PlaybackState(phase = phase),
                    articleProgress = ArticleProgress(positionMs = positionMs, durationMs = durationMs),
                    voiceState = VoiceState(availableVoices = availableVoices),
                ),
            reducer = ::listenReducer,
            middleware = listOf(ListenToPageTelemetryMiddleware()),
        )
}
