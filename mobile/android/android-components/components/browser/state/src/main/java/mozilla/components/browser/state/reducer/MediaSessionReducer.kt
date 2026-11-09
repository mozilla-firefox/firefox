/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.state.reducer

import mozilla.components.browser.state.action.MediaSessionAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.MediaSessionState
import mozilla.components.browser.state.state.SessionState
import mozilla.components.concept.engine.mediasession.MediaSession

internal object MediaSessionReducer {
    /** [MediaSessionAction] Reducer function for modifying the [MediaSessionState] of a [SessionState]. */
    fun reduce(state: BrowserState, action: MediaSessionAction): BrowserState {
        return when (action) {
            is MediaSessionAction.ActivatedMediaSessionAction ->
                state.addMediaSession(action.tabId, action.mediaSessionController)
            is MediaSessionAction.DeactivatedMediaSessionAction -> state.removeMediaSession(action.tabId)
            is MediaSessionAction.UpdateMediaMetadataAction -> state.updateMediaMetadata(action.tabId, action.metadata)
            is MediaSessionAction.UpdateMediaPlaybackStateAction ->
                state.updatePlaybackState(action.tabId, action.playbackState)
            is MediaSessionAction.UpdateMediaFeatureAction -> state.updateMediaFeature(action.tabId, action.features)
            is MediaSessionAction.UpdateMediaPositionStateAction ->
                state.updatePositionState(action.tabId, action.positionState)
            is MediaSessionAction.UpdateMediaMutedAction -> state.updateMuted(action.tabId, action.muted)
            is MediaSessionAction.UpdateMediaFullscreenAction ->
                state.updateFullscreen(
                    action.tabId,
                    action.fullScreen,
                    action.elementMetadata,
                )
            is MediaSessionAction.UpdateMediaAudioSessionTypeAction ->
                state.updateAudioSessionType(action.tabId, action.audioSessionType)
        }
    }
}

private fun BrowserState.updateAudioSessionType(
    tabId: String,
    audioSessionType: MediaSession.AudioSessionType,
): BrowserState {
    return updateTabOrCustomTabState(tabId) { current ->
        current.createCopy(mediaSessionState = current.mediaSessionState?.copy(audioSessionType = audioSessionType))
    }
}

private fun BrowserState.addMediaSession(
    tabId: String,
    mediaSessionController: MediaSession.Controller,
): BrowserState {
    return updateTabOrCustomTabState(tabId) { current ->
        current.createCopy(mediaSessionState = MediaSessionState(controller = mediaSessionController))
    }
}

private fun BrowserState.removeMediaSession(tabId: String): BrowserState {
    return updateTabOrCustomTabState(tabId) { current ->
        current.createCopy(mediaSessionState = null)
    }
}

private fun BrowserState.updateMediaMetadata(
    tabId: String,
    metadata: MediaSession.Metadata,
): BrowserState {
    return updateTabOrCustomTabState(tabId) { current ->
        val mss = current.mediaSessionState
        val trackChanged = mss?.metadata != null && mss.metadata.title != metadata.title
        current.createCopy(
            mediaSessionState =
                mss?.copy(
                    metadata = metadata,
                    positionState = if (trackChanged) mss.positionState.copy(position = 0.0) else mss.positionState,
                )
        )
    }
}

private fun BrowserState.updatePlaybackState(
    tabId: String,
    playbackState: MediaSession.PlaybackState,
): BrowserState {
    return updateTabOrCustomTabState(tabId) { current ->
        current.createCopy(mediaSessionState = current.mediaSessionState?.copy(playbackState = playbackState))
    }
}

private fun BrowserState.updateMediaFeature(
    tabId: String,
    features: MediaSession.Feature,
): BrowserState {
    return updateTabOrCustomTabState(tabId) { current ->
        current.createCopy(mediaSessionState = current.mediaSessionState?.copy(features = features))
    }
}

private fun BrowserState.updatePositionState(
    tabId: String,
    positionState: MediaSession.PositionState,
): BrowserState {
    return updateTabOrCustomTabState(tabId) { current ->
        current.createCopy(mediaSessionState = current.mediaSessionState?.copy(positionState = positionState))
    }
}

private fun BrowserState.updateMuted(
    tabId: String,
    muted: Boolean,
): BrowserState {
    return updateTabOrCustomTabState(tabId) { current ->
        current.createCopy(mediaSessionState = current.mediaSessionState?.copy(muted = muted))
    }
}

private fun BrowserState.updateFullscreen(
    tabId: String,
    fullscreen: Boolean,
    elementMetadata: MediaSession.ElementMetadata?,
): BrowserState {
    return updateTabOrCustomTabState(tabId) { current ->
        current.createCopy(
            mediaSessionState =
                current.mediaSessionState?.copy(
                    fullscreen = fullscreen,
                    elementMetadata = elementMetadata,
                )
        )
    }
}
