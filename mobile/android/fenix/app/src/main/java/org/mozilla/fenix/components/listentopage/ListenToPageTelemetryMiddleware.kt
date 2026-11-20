/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.listentopage

import mozilla.components.feature.listentopage.ListenAction
import mozilla.components.feature.listentopage.ListenState
import mozilla.components.feature.listentopage.PlaybackState
import mozilla.components.feature.listentopage.content.LanguageSource
import mozilla.components.feature.listentopage.facts.ListenToPageFacts
import mozilla.components.feature.listentopage.playback.PlaybackPhase
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
            is ListenAction.User.Session -> handleSession(action)
            is ListenAction.System.Content -> handleContent(action)
            is ListenAction.System.Playback -> handlePlayback(action)
            is ListenAction.User.Controls -> handleControls(store, action)
            is ListenAction.User.Voices -> handleUserVoices(store, action)
            is ListenAction.System.Voices -> handleSystemVoices(store, action)
            is ListenAction.System.Synthesis.SynthesisFailed -> ListenToPage.synthesisFailed.record(NoExtras())
            else -> Unit
        }
    }

    private fun handleSession(action: ListenAction.User.Session) {
        playbackStartedRecorded = false
        playbackEndedRecorded = false
        when (action) {
            is ListenAction.User.Session.ListenRequested -> ListenToPage.requested.record(NoExtras())
            is ListenAction.User.Session.StopRequested -> {
                val source =
                    when (action.source) {
                        ListenAction.User.Session.StopSource.CloseButton -> CLOSE_BUTTON
                        ListenAction.User.Session.StopSource.TabClosed -> TAB_CLOSED
                        ListenAction.User.Session.StopSource.ReaderModeClosed -> READER_MODE_CLOSED
                        ListenAction.User.Session.StopSource.MediaSessionEndedOutsideApp ->
                            MEDIA_SESSION_ENDED_OUTSIDE_APP
                        ListenAction.User.Session.StopSource.FeatureDisabled -> FEATURE_DISABLED
                    }
                ListenToPage.stopped.record(ListenToPage.StoppedExtra(source = source))
            }
        }
    }

    private fun handleContent(action: ListenAction.System.Content) {
        when (action) {
            is ListenAction.System.Content.ContentReady -> {
                val languageSource =
                    when (action.languageSource) {
                        LanguageSource.Page -> PAGE
                        LanguageSource.Fallback -> DEVICE
                    }
                ListenToPage.contentReady.record(
                    ListenToPage.ContentReadyExtra(language = action.languageTag, languageSource = languageSource)
                )
            }

            ListenAction.System.Content.ContentUnavailable -> ListenToPage.contentUnavailable.record(NoExtras())
        }
    }

    private fun handlePlayback(action: ListenAction.System.Playback) {
        when (action) {
            is ListenAction.System.Playback.PlaybackStarted ->
                if (!playbackStartedRecorded) {
                    playbackStartedRecorded = true
                    ListenToPage.playbackStarted.record(NoExtras())
                }

            ListenAction.System.Playback.PlaybackEnded ->
                if (!playbackEndedRecorded) {
                    playbackEndedRecorded = true
                    ListenToPage.playbackEnded.record(NoExtras())
                }

            ListenAction.System.Playback.PlaybackFailed -> ListenToPage.playbackFailed.record(NoExtras())

            else -> Unit
        }
    }

    private fun handleControls(store: Store<ListenState, ListenAction>, action: ListenAction.User.Controls) {
        when (action) {
            ListenAction.User.Controls.PlayPauseClicked ->
                if (store.state.playbackState.isPlayingOrBuffering) {
                    ListenToPage.pausePressed.record(ListenToPage.PausePressedExtra(source = IN_APP))
                } else {
                    ListenToPage.playPressed.record(ListenToPage.PlayPressedExtra(source = IN_APP))
                }

            ListenAction.User.Controls.RewindClicked ->
                ListenToPage.skipBackPressed.record(ListenToPage.SkipBackPressedExtra(source = IN_APP))
            ListenAction.User.Controls.ForwardClicked ->
                ListenToPage.skipForwardPressed.record(ListenToPage.SkipForwardPressedExtra(source = IN_APP))
            ListenAction.User.Controls.PlaybackSpeedClicked -> ListenToPage.speedListOpened.record(NoExtras())
            ListenAction.User.Controls.PlaybackSpeedDismissed -> ListenToPage.speedListDismissed.record(NoExtras())
            is ListenAction.User.Controls.PlaybackSpeedSelected ->
                ListenToPage.speedSelected.record(
                    ListenToPage.SpeedSelectedExtra(
                        speed = action.playbackSpeed.multiplier.toString(),
                        source = IN_APP,
                    )
                )
            is ListenAction.User.Controls.SeekRequested -> {
                val direction = if (action.fraction >= store.state.articleProgress.fraction) FORWARD else BACKWARD
                ListenToPage.seekPerformed.record(ListenToPage.SeekPerformedExtra(direction = direction))
            }
        }
    }

    private fun handleUserVoices(store: Store<ListenState, ListenAction>, action: ListenAction.User.Voices) {
        when (action) {
            is ListenAction.User.Voices.VoiceSelected -> {
                val isDefault = action.voice == store.state.voiceState.availableVoices.firstOrNull()
                val source =
                    when (action.source) {
                        ListenAction.User.Voices.VoiceSource.UserSelected -> USER_SELECTED
                        ListenAction.User.Voices.VoiceSource.Fallback -> FALLBACK
                    }
                ListenToPage.voiceSelected.record(
                    ListenToPage.VoiceSelectedExtra(isDefault = isDefault, source = source)
                )
            }

            ListenAction.User.Voices.VoicesClicked -> ListenToPage.voiceListOpened.record(NoExtras())
            ListenAction.User.Voices.VoicesDismissed -> ListenToPage.voiceListDismissed.record(NoExtras())
        }
    }

    private fun handleSystemVoices(store: Store<ListenState, ListenAction>, action: ListenAction.System.Voices) {
        when (action) {
            ListenAction.System.Voices.NoOfflineVoicesAvailable -> {
                val language = store.state.languageTag.orEmpty()
                ListenToPage.noOfflineVoice.record(ListenToPage.NoOfflineVoiceExtra(language = language))
            }

            is ListenAction.System.Voices.AvailableVoicesLoaded -> Unit
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
