/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.listentopage

import mozilla.components.feature.listentopage.ListenAction
import mozilla.components.feature.listentopage.ListenState
import mozilla.components.feature.listentopage.PlaybackPhase
import mozilla.components.feature.listentopage.PlaybackState
import mozilla.components.feature.listentopage.content.LanguageSource
import mozilla.components.feature.listentopage.facts.ListenToPageFacts
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import mozilla.components.support.base.Component
import mozilla.components.support.base.facts.Fact
import mozilla.components.support.base.facts.FactProcessor
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.ListenToPage

/** [Middleware] that records telemetry for the Listen to Page player. */
internal class ListenToPageTelemetryMiddleware : Middleware<ListenState, ListenAction>, FactProcessor {

    // Record once per session, since PlaybackStarted is dispatched every chunk
    private var playbackStartedRecorded = false

    // Record once per session, since PlaybackEnded is dispatched each replay or speed change after the article ends
    private var playbackEndedRecorded = false

    override fun invoke(
        store: Store<ListenState, ListenAction>,
        next: (ListenAction) -> Unit,
        action: ListenAction,
    ) {
        handleListenToPageAction(store, action)
        next(action)
    }

    private fun handleListenToPageAction(store: Store<ListenState, ListenAction>, action: ListenAction) {
        when (action) {
            is ListenAction.Session -> handleSession(action)
            is ListenAction.Content -> handleContent(action)
            is ListenAction.Playback -> handlePlayback(store, action)
            is ListenAction.Controls -> handleControls(store, action)
            is ListenAction.Voices -> handleVoices(store, action)
            is ListenAction.Synthesis.SynthesisFailed -> ListenToPage.synthesisFailed.record(NoExtras())
            else -> Unit
        }
    }

    private fun handleSession(action: ListenAction.Session) {
        playbackStartedRecorded = false
        playbackEndedRecorded = false
        when (action) {
            is ListenAction.Session.ListenRequested -> ListenToPage.requested.record(NoExtras())
            is ListenAction.Session.StopRequested -> {
                val source =
                    when (action.source) {
                        ListenAction.Session.StopSource.CloseButton -> CLOSE_BUTTON
                        ListenAction.Session.StopSource.TabClosed -> TAB_CLOSED
                        ListenAction.Session.StopSource.ReaderModeClosed -> READER_MODE_CLOSED
                        ListenAction.Session.StopSource.MediaSessionEndedOutsideApp -> MEDIA_SESSION_ENDED_OUTSIDE_APP
                        ListenAction.Session.StopSource.FeatureDisabled -> FEATURE_DISABLED
                    }
                ListenToPage.stopped.record(ListenToPage.StoppedExtra(source = source))
            }
        }
    }

    private fun handleContent(action: ListenAction.Content) {
        when (action) {
            is ListenAction.Content.ContentReady -> {
                val languageSource =
                    when (action.languageSource) {
                        LanguageSource.Page -> PAGE
                        LanguageSource.Fallback -> DEVICE
                    }
                ListenToPage.contentReady.record(
                    ListenToPage.ContentReadyExtra(language = action.languageTag, languageSource = languageSource)
                )
            }

            ListenAction.Content.ContentUnavailable -> ListenToPage.contentUnavailable.record(NoExtras())
        }
    }

    private fun handlePlayback(store: Store<ListenState, ListenAction>, action: ListenAction.Playback) {
        when (action) {
            is ListenAction.Playback.PlaybackStarted ->
                if (!playbackStartedRecorded) {
                    playbackStartedRecorded = true
                    ListenToPage.playbackStarted.record(NoExtras())
                }

            ListenAction.Playback.PlaybackEnded ->
                if (!playbackEndedRecorded) {
                    playbackEndedRecorded = true
                    ListenToPage.playbackEnded.record(NoExtras())
                }

            ListenAction.Playback.PlaybackFailed -> ListenToPage.playbackFailed.record(NoExtras())
            is ListenAction.Playback.SeekRequested -> {
                val direction = if (action.fraction >= store.state.articleProgress.fraction) FORWARD else BACKWARD
                ListenToPage.seekPerformed.record(ListenToPage.SeekPerformedExtra(direction = direction))
            }

            else -> Unit
        }
    }

    private fun handleControls(store: Store<ListenState, ListenAction>, action: ListenAction.Controls) {
        when (action) {
            ListenAction.Controls.PlayPauseClicked ->
                if (store.state.playbackState.isPlayingOrBuffering) {
                    ListenToPage.pausePressed.record(ListenToPage.PausePressedExtra(source = IN_APP))
                } else {
                    ListenToPage.playPressed.record(ListenToPage.PlayPressedExtra(source = IN_APP))
                }

            ListenAction.Controls.RewindClicked ->
                ListenToPage.skipBackPressed.record(ListenToPage.SkipBackPressedExtra(source = IN_APP))
            ListenAction.Controls.ForwardClicked ->
                ListenToPage.skipForwardPressed.record(ListenToPage.SkipForwardPressedExtra(source = IN_APP))
            ListenAction.Controls.VoicesClicked -> ListenToPage.voiceListOpened.record(NoExtras())
            ListenAction.Controls.VoicesDismissed -> ListenToPage.voiceListDismissed.record(NoExtras())
            ListenAction.Controls.PlaybackSpeedClicked -> ListenToPage.speedListOpened.record(NoExtras())
            ListenAction.Controls.PlaybackSpeedDismissed -> ListenToPage.speedListDismissed.record(NoExtras())
            is ListenAction.Controls.PlaybackSpeedSelected ->
                ListenToPage.speedSelected.record(
                    ListenToPage.SpeedSelectedExtra(
                        speed = action.playbackSpeed.multiplier.toString(),
                        source = IN_APP,
                    )
                )
        }
    }

    private fun handleVoices(store: Store<ListenState, ListenAction>, action: ListenAction.Voices) {
        when (action) {
            is ListenAction.Voices.VoiceSelected -> {
                val isDefault = action.voice == store.state.voiceState.availableVoices.firstOrNull()
                val source =
                    when (action.source) {
                        ListenAction.Voices.VoiceSource.UserSelected -> USER_SELECTED
                        ListenAction.Voices.VoiceSource.Fallback -> FALLBACK
                    }
                ListenToPage.voiceSelected.record(
                    ListenToPage.VoiceSelectedExtra(isDefault = isDefault, source = source)
                )
            }

            ListenAction.Voices.NoOfflineVoicesAvailable -> {
                val language = store.state.languageTag.orEmpty()
                ListenToPage.noOfflineVoice.record(ListenToPage.NoOfflineVoiceExtra(language = language))
            }

            is ListenAction.Voices.AvailableVoicesLoaded -> Unit
        }
    }

    override fun process(fact: Fact) {
        if (fact.component != Component.FEATURE_LISTENTOPAGE) return

        when (fact.item) {
            ListenToPageFacts.Items.PLAY_PRESSED ->
                ListenToPage.playPressed.record(ListenToPage.PlayPressedExtra(source = MEDIA_CONTROLS))

            ListenToPageFacts.Items.PAUSE_PRESSED ->
                ListenToPage.pausePressed.record(ListenToPage.PausePressedExtra(source = MEDIA_CONTROLS))

            ListenToPageFacts.Items.SKIP_BACK_PRESSED ->
                ListenToPage.skipBackPressed.record(ListenToPage.SkipBackPressedExtra(source = MEDIA_CONTROLS))

            ListenToPageFacts.Items.SKIP_FORWARD_PRESSED ->
                ListenToPage.skipForwardPressed.record(ListenToPage.SkipForwardPressedExtra(source = MEDIA_CONTROLS))

            ListenToPageFacts.Items.SPEED_SELECTED ->
                ListenToPage.speedSelected.record(
                    ListenToPage.SpeedSelectedExtra(speed = fact.value, source = MEDIA_CONTROLS)
                )
        }
    }
}

private const val FORWARD = "forward"
private const val BACKWARD = "backward"
private const val CLOSE_BUTTON = "close_button"
private const val TAB_CLOSED = "tab_closed"
private const val READER_MODE_CLOSED = "reader_mode_closed"
private const val MEDIA_SESSION_ENDED_OUTSIDE_APP = "media_session_ended_outside_app"
private const val FEATURE_DISABLED = "feature_disabled"
private const val USER_SELECTED = "user_selected"
private const val FALLBACK = "fallback"
private const val PAGE = "page"
private const val DEVICE = "device"
private const val IN_APP = "in_app"
private const val MEDIA_CONTROLS = "media_controls"

private val PlaybackState.isPlayingOrBuffering: Boolean
    get() = phase == PlaybackPhase.Playing || phase == PlaybackPhase.Buffering
