/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.pictureinpicture

import android.app.PictureInPictureParams
import android.content.Context
import android.content.Intent
import android.content.res.Configuration
import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import mozilla.components.browser.state.selector.findCustomTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.Engine
import mozilla.components.support.base.feature.ViewBoundFeatureWrapper
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.utils.SwitchTabIntent

/**
 * Shows the video in a native picture-in-picture window of its own, once its session has arrived. Started by
 * [PictureInPictureWindowFeature].
 */
abstract class AbstractPictureInPictureActivity : AppCompatActivity() {
    protected abstract val store: BrowserStore
    protected abstract val engine: Engine
    protected abstract val pictureInPictureUseCases: PictureInPictureUseCases

    private val logger = Logger("PictureInPictureActivity")

    private val pictureInPictureSessionFeature = ViewBoundFeatureWrapper<PictureInPictureSessionFeature>()

    /**
     * Invoked when the user expands the window, to bring the browser back on the tab the video came from. Unless the
     * video came from a custom tab, defaults to launching the app with a [SwitchTabIntent], which the app has to
     * handle.
     *
     * @param tabId The ID of the tab the video came from.
     */
    protected open fun onReturnToTab(tabId: String) {
        if (store.state.findCustomTab(tabId) != null) {
            return
        }
        val intent =
            packageManager.getLaunchIntentForPackage(packageName)?.apply {
                action = SwitchTabIntent.ACTION_SWITCH_TAB
                putExtra(SwitchTabIntent.EXTRA_TAB_ID, tabId)
            } ?: return
        startActivity(intent)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        if (store.state.pictureInPictureWindow.session == null) {
            logger.debug("No session to show, finishing")
            finish()
            return
        }
        val engineView = engine.createView(this)
        setContentView(engineView.asView())
        pictureInPictureSessionFeature.set(
            PictureInPictureSessionFeature(
                store = store,
                engineView = engineView,
                onVideoGone = ::finish,
                onReturnToTab = ::onReturnToTab,
                pictureInPictureUseCases = pictureInPictureUseCases,
            ),
            owner = this,
            view = engineView.asView(),
        )
    }

    override fun onResume() {
        super.onResume()
        if (isInPictureInPictureMode || isFinishing) {
            return
        }
        if (!tryEnterPictureInPictureMode()) {
            logger.debug("Refused picture-in-picture mode, finishing")
            finish()
        }
    }

    private fun tryEnterPictureInPictureMode(): Boolean =
        try {
            enterPictureInPictureMode(buildPipParams())
        } catch (e: IllegalStateException) {
            logger.warn("Entering picture-in-picture mode failed", e)
            false
        }

    override fun onPictureInPictureModeChanged(isInPictureInPictureMode: Boolean, newConfig: Configuration) {
        super.onPictureInPictureModeChanged(isInPictureInPictureMode, newConfig)
        pictureInPictureSessionFeature.get()?.onPictureInPictureModeChanged(isInPictureInPictureMode)
        if (!isInPictureInPictureMode) {
            finish()
        }
    }

    private fun buildPipParams(): PictureInPictureParams = PictureInPictureParams.Builder().build()

    internal companion object {
        /** Creates the intent starting [activityClass]. */
        fun createIntent(context: Context, activityClass: Class<out AbstractPictureInPictureActivity>): Intent =
            Intent(context, activityClass).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
    }
}
