/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.pictureinpicture

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import mozilla.components.browser.state.state.PictureInPictureSession
import mozilla.components.browser.state.state.PictureInPictureWindowState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.EngineView
import mozilla.components.lib.state.ext.flowScoped
import mozilla.components.support.base.feature.LifecycleAwareFeature

/**
 * Renders the session cloning a video into [engineView] while its picture-in-picture window is up. Rendering begins on
 * [start] and the window goes down on [stop].
 *
 * See also [PictureInPictureWindowFeature], which starts the [AbstractPictureInPictureActivity] this feature renders
 * the video in.
 *
 * @param store The store the session is handed over through.
 * @param engineView The view the video is rendered in.
 * @param pictureInPictureUseCases Closes the window on [stop].
 * @param mainDispatcher Dispatcher the browser state is observed on.
 * @param onVideoGone Invoked once the engine says the video being shown is gone.
 * @param onReturnToTab Invoked with the ID of the tab the video comes from when the user expands the window.
 */
class PictureInPictureSessionFeature(
    private val store: BrowserStore,
    private val engineView: EngineView,
    private val pictureInPictureUseCases: PictureInPictureUseCases,
    private val mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
    private val onVideoGone: () -> Unit,
    private val onReturnToTab: (tabId: String) -> Unit = {},
) : LifecycleAwareFeature {
    private var scope: CoroutineScope? = null
    private var renderedSession: PictureInPictureSession? = null
    private var isInPictureInPictureMode = false

    /** Renders the session the window already has, or is waiting on, without asking the engine for a video. */
    override fun start() {
        if (scope != null) {
            return
        }
        scope =
            store.flowScoped(dispatcher = mainDispatcher) { flow ->
                flow
                    .map { state -> state.pictureInPictureWindow }
                    .distinctUntilChanged()
                    .collect { window -> update(window) }
            }
    }

    /**
     * Should be called when the activity the window is shown in goes into or out of picture-in-picture mode.
     *
     * @param isInPictureInPictureMode True if the activity is in picture-in-picture mode.
     */
    fun onPictureInPictureModeChanged(isInPictureInPictureMode: Boolean) {
        this.isInPictureInPictureMode = isInPictureInPictureMode
        val session = renderedSession ?: return
        session.engineSession.onPipModeChanged(isInPictureInPictureMode)
        if (!isInPictureInPictureMode) {
            onReturnToTab(session.tabId)
        }
    }

    /** Takes the window down, closing its session. */
    override fun stop() {
        val currentScope = scope ?: return
        currentScope.cancel()
        scope = null
        renderedSession = null
        engineView.release()
        pictureInPictureUseCases.closeWindow()
    }

    /**
     * Renders the session of the window if it is new, or says the video is gone once the window has no session left and
     * waits on none.
     */
    private fun update(window: PictureInPictureWindowState) {
        val session = window.session
        val isWindowClosed = session == null && window.requestedTabId == null
        val hasNewSession = session != null && session != renderedSession
        when {
            isWindowClosed -> {
                renderedSession = null
                onVideoGone()
            }
            hasNewSession -> {
                renderedSession = session
                engineView.render(session.engineSession)
                session.engineSession.onPipModeChanged(isInPictureInPictureMode)
            }
            else -> Unit
        }
    }
}
