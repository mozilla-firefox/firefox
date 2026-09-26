/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.toolbar

import android.view.ViewGroup
import android.view.ViewTreeObserver
import androidx.annotation.VisibleForTesting
import androidx.coordinatorlayout.widget.CoordinatorLayout
import kotlin.math.roundToInt
import mozilla.components.compose.browser.toolbar.R as toolbarR
import mozilla.components.concept.engine.EngineView
import mozilla.components.feature.pwa.feature.WebAppHideToolbarFeature
import mozilla.components.feature.session.FullScreenFeature
import mozilla.components.support.base.feature.LifecycleAwareFeature
import mozilla.components.support.ktx.android.view.pixelSizeFor
import mozilla.components.support.utils.ext.isKeyboardVisible
import org.mozilla.fenix.utils.Settings

/**
 * Helper for ensuring the top and bottom toolbars are correctly configured depending on whether the IME is shown or not
 * and will dynamically adapt all related properties when the IME visibility changes. This will also handle the
 * scenarios in which the toolbars should not be visible as it happens when the engine view is shown in fullscreen.
 *
 * @param fullScreenFeature The [FullScreenFeature] instance used to check when the website is in fullscreen.
 * @param webAppHideToolbarFeature The [WebAppHideToolbarFeature] instance used to check when the toolbars should be
 *   hidden for the current PWA configuration.
 * @param settings [Settings] object to get the toolbar position and other settings.
 * @param browserLayout The root layout of the engine view.
 * @param engineView The engine View rendering web content.
 * @param toolbar The top toolbar layout.
 * @param topToolbarHeight The height of the top toolbar.
 * @param onToolbarsReset Callback to be invoked when the toolbars need to be reset.
 */
class ToolbarsIntegration(
    private val fullScreenFeature: () -> FullScreenFeature?,
    private val webAppHideToolbarFeature: () -> WebAppHideToolbarFeature?,
    private val settings: Settings,
    private val browserLayout: ViewGroup,
    private val engineView: EngineView,
    private val toolbar: BrowserToolbarComposable,
    private val topToolbarHeight: () -> Int,
    private val onToolbarsReset: () -> Unit,
) : LifecycleAwareFeature {
    private var layoutListener: ViewTreeObserver.OnGlobalLayoutListener? = null

    override fun start() {
        if (settings.shouldUseMinimalBottomToolbarWhenEnteringText) {
            var wasKeyboardShown = false

            layoutListener = ViewTreeObserver.OnGlobalLayoutListener {
                val isKeyboardShown = browserLayout.isKeyboardVisible()
                if (wasKeyboardShown != isKeyboardShown) {
                    wasKeyboardShown = isKeyboardShown
                    onKeyboardShown(isKeyboardShown)
                }
            }
            browserLayout.viewTreeObserver.addOnGlobalLayoutListener(layoutListener)
        }
    }

    override fun stop() {
        layoutListener?.let {
            browserLayout.viewTreeObserver.removeOnGlobalLayoutListener(it)
        }
    }

    @VisibleForTesting
    internal fun onKeyboardShown(isKeyboardShown: Boolean) {
        // Ignore any updates relating to the toolbar if the toolbar should not be visible.
        if (fullScreenFeature()?.isFullScreen == true || webAppHideToolbarFeature()?.shouldToolbarsBeVisible == false) {
            return
        }

        val browserLayoutParams = browserLayout.layoutParams as CoordinatorLayout.LayoutParams
        when (isKeyboardShown) {
            true -> {
                toolbar.disableScrolling()
                toolbar.expand()

                val isUsingBottomToolbar = settings.shouldUseBottomToolbar
                val isTopToolbarShown = browserLayout.translationY.roundToInt() > 0

                browserLayoutParams.behavior = null
                browserLayoutParams.topMargin =
                    when {
                        isTopToolbarShown -> 0
                        else -> topToolbarHeight()
                    }
                browserLayoutParams.bottomMargin =
                    when {
                        isUsingBottomToolbar ->
                            browserLayout.pixelSizeFor(toolbarR.dimen.mozac_minimal_display_toolbar_height)
                        isTopToolbarShown -> topToolbarHeight()
                        else -> 0
                    }

                engineView.apply {
                    setDynamicToolbarMaxHeight(0)
                    setVerticalClipping(0)
                }
            }
            false -> {
                toolbar.enableScrolling()

                browserLayoutParams.topMargin = 0
                browserLayoutParams.bottomMargin = 0
                onToolbarsReset()
            }
        }
    }
}
