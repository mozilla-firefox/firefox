/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.pictureinpicture

import android.content.Context
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.lib.state.ext.flowScoped

/**
 * Starts [activityClass] to show a video in a picture-in-picture window once the session cloning it has arrived.
 *
 * See also [PictureInPictureSessionFeature], which the [AbstractPictureInPictureActivity] started by this feature uses
 * to render the video.
 *
 * @param context The context the window is started from.
 * @param store The store the session is handed over through.
 * @param activityClass The activity showing the window.
 * @param mainDispatcher Dispatcher the browser state is observed on.
 */
class PictureInPictureWindowFeature(
    private val context: Context,
    private val store: BrowserStore,
    private val activityClass: Class<out AbstractPictureInPictureActivity>,
    private val mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
) {
    private var scope: CoroutineScope? = null

    /** Starts observing the store for sessions to show in the window. */
    fun start() {
        if (scope != null) {
            return
        }
        scope =
            store.flowScoped(dispatcher = mainDispatcher) { flow ->
                flow
                    .map { state -> state.pictureInPictureWindow.session != null }
                    .distinctUntilChanged()
                    .collect { hasSession ->
                        if (hasSession) {
                            onSessionArrived()
                        }
                    }
            }
    }

    /** Stops observing the store. */
    fun stop() {
        scope?.cancel()
        scope = null
    }

    private fun onSessionArrived() {
        context.startActivity(AbstractPictureInPictureActivity.createIntent(context, activityClass))
    }
}
