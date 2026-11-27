/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.media.fullscreen

import android.app.Activity
import android.content.pm.ActivityInfo
import android.view.WindowManager
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import mozilla.components.browser.state.selector.findCustomTabOrSelectedTab
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.mediasession.MediaSession
import mozilla.components.lib.state.ext.flowScoped
import mozilla.components.support.base.feature.LifecycleAwareFeature

/** Feature that will auto-rotate the device to the correct orientation for the media aspect ratio. */
class MediaSessionFullscreenFeature(
    private val activity: Activity,
    private val store: BrowserStore,
    private val tabId: String?,
    private val mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
) : LifecycleAwareFeature {

    private var scope: CoroutineScope? = null

    override fun start() {
        scope =
            store.flowScoped(dispatcher = mainDispatcher) { flow ->
                flow
                    .map {
                        it.tabs + it.customTabs
                    }
                    .map { tab ->
                        tab.firstOrNull { it.mediaSessionState?.fullscreen == true }
                    }
                    .distinctUntilChanged { old, new ->
                        old.hasSameOrientationInformationAs(new)
                    }
                    .collect { state ->
                        // There should only be one fullscreen session.
                        if (state == null) {
                            activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_USER
                            activity.window?.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                            return@collect
                        }

                        if (store.state.findCustomTabOrSelectedTab(tabId)?.id == state.id) {
                            setOrientationForTabState(state)
                        }
                        setDeviceSleepModeForTabState(state)
                    }
            }
    }

    @Suppress("SourceLockedOrientationActivity") // We deliberately want to lock the orientation here.
    private fun setOrientationForTabState(activeTabState: SessionState) {
        when (activeTabState.mediaSessionState?.elementMetadata?.portrait) {
            true -> activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_USER_PORTRAIT

            false ->
                if (activity.isInPictureInPictureMode) {
                    activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
                } else {
                    activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_USER
                }

            null -> activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_USER
        }
    }

    private fun setDeviceSleepModeForTabState(activeTabState: SessionState) {
        activeTabState.mediaSessionState?.let {
            when (activeTabState.mediaSessionState?.playbackState) {
                MediaSession.PlaybackState.PLAYING -> {
                    activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                }

                else -> {
                    activity.window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                }
            }
        }
    }

    private fun SessionState?.hasSameOrientationInformationAs(other: SessionState?): Boolean =
        this?.mediaSessionState?.fullscreen == other?.mediaSessionState?.fullscreen &&
            this?.mediaSessionState?.playbackState == other?.mediaSessionState?.playbackState &&
            this?.mediaSessionState?.elementMetadata == other?.mediaSessionState?.elementMetadata &&
            this?.content?.pictureInPictureEnabled == other?.content?.pictureInPictureEnabled

    override fun stop() {
        scope?.cancel()
    }
}
