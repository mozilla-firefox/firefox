/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.browser.integration

import android.app.Activity
import android.os.Build
import androidx.annotation.VisibleForTesting
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.browser.toolbar.BrowserToolbar
import mozilla.components.concept.engine.EngineView
import mozilla.components.feature.prompts.dialog.FullScreenNotification
import mozilla.components.feature.prompts.dialog.FullScreenNotificationToast
import mozilla.components.feature.prompts.dialog.GestureNavUtils
import mozilla.components.feature.session.FullScreenFeature
import mozilla.components.feature.session.SessionUseCases
import mozilla.components.support.base.feature.LifecycleAwareFeature
import mozilla.components.support.base.feature.UserInteractionHandler
import mozilla.components.support.ktx.android.content.isEdgeToEdgeDisabled
import mozilla.components.support.ktx.android.view.enterImmersiveMode
import mozilla.components.support.ktx.android.view.exitImmersiveMode
import org.mozilla.focus.R
import org.mozilla.focus.ext.disableDynamicBehavior
import org.mozilla.focus.ext.enableDynamicBehavior
import org.mozilla.focus.ext.hide
import org.mozilla.focus.ext.showAsFixed

/** Integration for the full-screen feature, managing immersive mode and toolbar visibility. */
class FullScreenIntegration(
    val activity: Activity,
    val store: BrowserStore,
    tabId: String?,
    sessionUseCases: SessionUseCases,
    private val toolbarView: BrowserToolbar,
    private val engineView: EngineView,
    private val isAccessibilityEnabled: () -> Boolean,
) : LifecycleAwareFeature, UserInteractionHandler {
    @VisibleForTesting
    internal var feature =
        FullScreenFeature(
            store,
            sessionUseCases,
            tabId,
            ::viewportFitChanged,
            ::fullScreenChanged,
        )

    override fun start() {
        feature.start()
    }

    override fun stop() {
        feature.stop()
    }

    @VisibleForTesting
    internal fun fullScreenChanged(
        enabled: Boolean,
        fullScreenNotification: FullScreenNotification =
            FullScreenNotificationToast(
                activity,
                activity.resources.getString(R.string.exit_fullscreen_with_gesture_short),
                activity.resources.getString(R.string.exit_fullscreen_with_back_button_short),
                GestureNavUtils,
            ),
    ) {
        if (enabled) {
            enterBrowserFullscreen()

            fullScreenNotification.show()

            switchToImmersiveMode()
        } else {
            // If the video is in PiP, but is not in fullscreen anymore we should move the task containing
            // this activity to the back of the activity stack
            if (activity.isInPictureInPictureMode) {
                activity.moveTaskToBack(false)
            }
            exitBrowserFullscreen()

            exitImmersiveMode()
        }
    }

    override fun onBackPressed(): Boolean {
        return feature.onBackPressed()
    }

    @VisibleForTesting
    internal fun viewportFitChanged(viewportFit: Int) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P && activity.isEdgeToEdgeDisabled()) {
            activity.window.attributes = activity.window.attributes.apply { layoutInDisplayCutoutMode = viewportFit }
        }
    }

    /**
     * Hide system bars. They can be revealed temporarily with system gestures, such as swiping from the top of the
     * screen. These transient system bars will overlay app’s content, may have some degree of transparency, and will
     * automatically hide after a short timeout.
     */
    @VisibleForTesting
    internal fun switchToImmersiveMode() {
        activity.enterImmersiveMode()
    }

    /** Show the system bars again. */
    fun exitImmersiveMode() {
        activity.exitImmersiveMode()
    }

    @VisibleForTesting
    internal fun enterBrowserFullscreen() {
        if (isAccessibilityEnabled()) {
            toolbarView.hide(engineView)
        } else {
            toolbarView.collapse()
            toolbarView.disableDynamicBehavior(engineView)
        }
    }

    @VisibleForTesting
    internal fun exitBrowserFullscreen() {
        if (isAccessibilityEnabled()) {
            toolbarView.showAsFixed(activity, engineView)
        } else {
            toolbarView.enableDynamicBehavior(activity, engineView)
            toolbarView.expand()
        }
    }
}
