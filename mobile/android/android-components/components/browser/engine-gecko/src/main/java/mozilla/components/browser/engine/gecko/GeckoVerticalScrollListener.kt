/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.engine.gecko

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.cancel
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.callbackFlow
import kotlinx.coroutines.flow.filter
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.flatMapLatest
import kotlinx.coroutines.flow.launchIn
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.flow.shareIn
import kotlinx.coroutines.isActive
import mozilla.components.support.ktx.kotlinx.coroutines.flow.windowed
import org.mozilla.geckoview.GeckoSession
import org.mozilla.geckoview.GeckoSession.CompositorScrollDelegate
import org.mozilla.geckoview.GeckoSession.ScrollPositionUpdate

/**
 * Delegate for observing scroll related updates from a given [GeckoSession]
 * and exposing this data through [scrollYPosition] and [scrollYDeltas] flows.
 */
@OptIn(ExperimentalCoroutinesApi::class) // for flatMapLatest
internal class GeckoVerticalScrollListener(
    private val dispatcher: CoroutineDispatcher = Dispatchers.Main,
) {
    private var updatesScope: CoroutineScope? = null
    private val currentGeckoSession = MutableStateFlow<GeckoSession?>(null)

    private val _scrollYPosition = MutableStateFlow(0f)
    private val _scrollYDeltas = MutableStateFlow(0f)
    private val _zoomFactor = MutableStateFlow(1f)

    /**
     * Running flow of the current scroll position in pixels.
     */
    val scrollYPosition: StateFlow<Float> = _scrollYPosition.asStateFlow()

    /**
     * Running flow of scroll deltas in pixels.
     */
    val scrollYDeltas: StateFlow<Float> = _scrollYDeltas.asStateFlow()

    /**
     * Running flow of the current zoom factor applied to the content.
     */
    val zoomFactor: StateFlow<Float> = _zoomFactor.asStateFlow()

    /**
     * Start observing [geckoSession] for scroll related updates.
     */
    fun observe(geckoSession: GeckoSession) {
        if (updatesScope?.isActive != true) {
            updatesScope = CoroutineScope(dispatcher)
            startCollectors()
        }
        currentGeckoSession.value = geckoSession
    }

    /**
     * Stops observing the current session and resets the data flows to 0f.
     */
    fun release() {
        currentGeckoSession.value = null
        updatesScope?.cancel()

        _scrollYPosition.value = 0f
        _scrollYDeltas.value = 0f
        _zoomFactor.value = 1f
    }

    private fun startCollectors() {
        val scope = updatesScope ?: return

        // Single source of truth for the scroll position in the current session.
        val sharedPositionFlow: SharedFlow<ScrollPositionUpdate> = currentGeckoSession
            .filterNotNull()
            .flatMapLatest { session ->
                callbackFlow {
                    val delegate = CompositorScrollDelegate { trySend(it) }
                    session.compositorScrollDelegate = delegate
                    awaitClose { session.compositorScrollDelegate = null }
                }
            }
            .shareIn(scope, SharingStarted.Eagerly)

        sharedPositionFlow
            .map { it.scrollY * it.zoom }
            .onEach { _scrollYPosition.value = it }
            .launchIn(scope)

        sharedPositionFlow
            .map { it.zoom }
            .onEach { _zoomFactor.value = it }
            .launchIn(scope)

        sharedPositionFlow
            .windowed(2, 1)
            .map { (old, new) ->
                // Report a new scroll delta if not in the progress of zooming in/out.
                // Or if coming back to no in/out zoom report immediately avoiding the need of two
                // data points for scrolling with no in/out zoom.
                if (new.zoom == old.zoom || new.zoom == 1f) {
                    (new.scrollY - old.scrollY) * new.zoom
                } else {
                    0f
                }
            }
            .filter { it != 0f }
            .onEach { _scrollYDeltas.value = it }
            .launchIn(scope)
    }
}

private class CompositorScrollDelegate(
    private val scrollUpdatesCallback: (ScrollPositionUpdate) -> Unit,
) : CompositorScrollDelegate {
    override fun onScrollChanged(session: GeckoSession, update: ScrollPositionUpdate) {
        scrollUpdatesCallback(update)
    }
}
