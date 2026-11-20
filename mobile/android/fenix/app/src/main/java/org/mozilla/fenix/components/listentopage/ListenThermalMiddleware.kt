/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.listentopage

import mozilla.components.feature.listentopage.ListenAction
import mozilla.components.feature.listentopage.ListenState
import mozilla.components.feature.listentopage.playback.PlaybackPhase
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.fenix.GleanMetrics.ListenToPage

/**
 * [Middleware] that records how hot the device runs while an article is read. Sessions on devices whose thermal status
 * cannot be trusted are not recorded at all. This would indicate devices with older thermal sensors and HALs.
 *
 * @param monitor Reports the thermal status.
 */
class ListenThermalMiddleware(private val monitor: ThermalStatusMonitor) : Middleware<ListenState, ListenAction> {

    private val logger = Logger("ListenPerformance")

    private var session: Session = Session.NotReading

    override fun invoke(
        store: Store<ListenState, ListenAction>,
        next: (ListenAction) -> Unit,
        action: ListenAction,
    ) {
        next(action)

        when (action) {
            is ListenAction.Session.ListenRequested,
            is ListenAction.Session.StopRequested,
            ListenAction.Playback.PlaybackEnded,
            ListenAction.Playback.PlaybackFailed,
            ListenAction.Synthesis.SynthesisFailed -> endSession()

            is ListenAction.Playback.PlaybackStarted -> onPlaying()

            is ListenAction.Playback.StateChangeObserved ->
                when (action.playbackState.phase) {
                    PlaybackPhase.Playing -> onPlaying()
                    PlaybackPhase.Paused -> onPaused()
                    else -> Unit
                }

            ListenAction.Content.ContentUnavailable,
            is ListenAction.Content.ContentReady,
            is ListenAction.Voices.VoiceSelected,
            is ListenAction.Voices.AvailableVoicesLoaded,
            ListenAction.Voices.NoOfflineVoicesAvailable,
            ListenAction.Controls.PlayPauseClicked,
            ListenAction.Controls.RewindClicked,
            ListenAction.Controls.ForwardClicked,
            ListenAction.Controls.VoicesClicked,
            ListenAction.Controls.VoicesDismissed,
            ListenAction.Controls.PlaybackSpeedClicked,
            ListenAction.Controls.PlaybackSpeedDismissed,
            is ListenAction.Controls.PlaybackSpeedSelected,
            is ListenAction.Playback.ArticleProgressReverted,
            is ListenAction.Playback.SeekRequested,
            ListenAction.Playback.PlaybackWaiting,
            is ListenAction.ModeChanged,
            ListenAction.ErrorDismissed -> Unit

            // The session's own actions, which are internal to it.
            else -> Unit
        }
    }

    /** Audio is playing: a session starts, or one that was paused is observed again. */
    private fun onPlaying() {
        val current = session
        when {
            current == Session.NotReading -> startSession()
            current is Session.Reading && current.isPaused -> {
                // Heat from while it was paused is not counted as entering throttling, so the status starts over here.
                val status = monitor.currentStatus.takeIf { it != ThermalStatus.Unknown } ?: current.status
                session = current.copy(status = status, maxStatus = maxOf(current.maxStatus, status), isPaused = false)
                monitor.startObserving(::onStatusChanged)
            }
        }
    }

    /** Audio paused: the session is kept, but nothing is observed until it plays again. */
    private fun onPaused() {
        val reading = session as? Session.Reading ?: return
        if (reading.isPaused) return

        monitor.stopObserving()
        session = reading.copy(isPaused = true)
    }

    private fun startSession() {
        // Session that start in an Unknown state indicate a lack of thermal support, so we do not attempt recording
        // them.
        val status = if (monitor.isSupported()) monitor.currentStatus else ThermalStatus.Unknown
        if (status == ThermalStatus.Unknown) {
            logger.debug("Thermal status is not supported, the session is not recorded")
            session = Session.Unsupported
            return
        }

        session = Session.Reading(status = status)
        logger.debug("Thermal status at first audio is ${status.label}")
        monitor.startObserving(::onStatusChanged)
    }

    private fun onStatusChanged(status: ThermalStatus) {
        val reading = session as? Session.Reading ?: return
        if (reading.isPaused || status == ThermalStatus.Unknown) return
        val enteredThrottling = status >= THROTTLING && reading.status < THROTTLING

        session =
            reading.copy(
                status = status,
                maxStatus = maxOf(reading.maxStatus, status),
                throttlingEntries = if (enteredThrottling) reading.throttlingEntries + 1 else reading.throttlingEntries,
            )
        logger.debug("Thermal status changed to ${status.label}")
    }

    private fun endSession() {
        val ended = session
        session = Session.NotReading
        if (ended !is Session.Reading) return

        if (!ended.isPaused) monitor.stopObserving()
        ListenToPage.sessionMaxThermalStatus[ended.maxStatus.label].add()
        if (ended.throttlingEntries > 0) {
            ListenToPage.thermalThrottlingEntered.add(ended.throttlingEntries)
        }
        logger.debug("Thermal max was ${ended.maxStatus.label}, throttling entered ${ended.throttlingEntries} times")
    }

    private sealed interface Session {
        /** No audio has played since the last session ended. */
        data object NotReading : Session

        /** The device cannot report a status worth recording. */
        data object Unsupported : Session

        /**
         * Audio has played on a device whose status is being recorded.
         *
         * @property status The status the device last reported.
         * @property maxStatus The hottest status reported while audio was playing.
         * @property throttlingEntries How many times the status has risen into [THROTTLING] or above.
         * @property isPaused Whether the audio is paused, while which the status is not observed.
         */
        data class Reading(
            val status: ThermalStatus,
            val maxStatus: ThermalStatus = status,
            val throttlingEntries: Int = 0,
            val isPaused: Boolean = false,
        ) : Session
    }

    private companion object {
        // Android describes LIGHT as throttling that the user does not notice, so throttling starts at MODERATE.
        val THROTTLING = ThermalStatus.Moderate
    }
}

/** The label the status is recorded under in telemetry. */
private val ThermalStatus.label: String
    get() =
        when (this) {
            ThermalStatus.None -> "none"
            ThermalStatus.Light -> "light"
            ThermalStatus.Moderate -> "moderate"
            ThermalStatus.Severe -> "severe"
            ThermalStatus.Critical -> "critical"
            ThermalStatus.Emergency -> "emergency"
            ThermalStatus.Shutdown -> "shutdown"
            ThermalStatus.Unknown -> "unknown"
        }
