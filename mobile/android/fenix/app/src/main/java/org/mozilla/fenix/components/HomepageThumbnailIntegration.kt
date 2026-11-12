/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Rect
import android.view.View
import androidx.compose.material3.ColorScheme
import androidx.compose.ui.graphics.toArgb
import androidx.core.graphics.createBitmap
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.browser.thumbnails.HomepageThumbnails
import mozilla.components.browser.thumbnails.RequestHomepageScreenshot
import mozilla.components.compose.base.theme.Theme
import mozilla.components.compose.base.theme.acornDarkColorScheme
import mozilla.components.compose.base.theme.acornLightColorScheme
import mozilla.components.compose.base.theme.acornOledColorScheme
import mozilla.components.compose.base.theme.acornPrivateColorScheme
import mozilla.components.concept.engine.utils.ABOUT_HOME_URL
import mozilla.components.support.base.feature.LifecycleAwareFeature
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.isSystemInDarkTheme
import org.mozilla.fenix.theme.getTheme

/**
 * Homepage delegate to take screenshot of homepage when view loads and display as a thumbnail in tabs tray.
 *
 * @param context A [Context] used to check for low memory.
 * @param view The [View] to take screenshot of.
 * @param store The [BrowserStore] used to look up the current selected tab.
 * @param appStore The [AppStore] used to look up the current browsing mode.
 * @param homepageContentBounds Lamba that provides the bounds of the homepage content (excluding the toolbar and system
 *   bar padding) within [view], used to crop the screenshot to the content only. Returns null when the bounds are
 *   unknown, in which case the full [view] is captured.
 */
class HomepageThumbnailIntegration(
    private val context: Context,
    private val view: View,
    private val store: BrowserStore,
    private val appStore: AppStore,
    private val homepageContentBounds: () -> Rect?,
) : LifecycleAwareFeature {
    private val feature by lazy {
        HomepageThumbnails(
            context = context,
            store = store,
            homepageUrl = ABOUT_HOME_URL,
            homepageRequest = ::homepageRequest,
        )
    }
    private var backgroundColor: Int = 0

    override fun start() {
        backgroundColor = getColor(context, appStore.state.mode).surface.toArgb()
        feature.start()
    }

    override fun stop() {
        feature.stop()
    }

    private fun homepageRequest(requestHomepageScreenshot: RequestHomepageScreenshot) {
        view.post {
            val bitmap = createBitmap(view.width, view.height)
            val canvas = Canvas(bitmap)
            canvas.drawColor(backgroundColor)
            view.draw(canvas)
            requestHomepageScreenshot(bitmap.cropToBounds(homepageContentBounds()))
        }
    }

    /**
     * Crops [this] bitmap to [bounds] so the thumbnail will only include the homepage content. Returns the original
     * bitmap when [bounds] is null or does not describe a valid region within the bitmap.
     */
    private fun Bitmap.cropToBounds(bounds: Rect?): Bitmap {
        if (bounds == null) {
            return this
        }

        val left = bounds.left.coerceIn(0, width)
        val top = bounds.top.coerceIn(0, height)
        val right = bounds.right.coerceIn(left, width)
        val bottom = bounds.bottom.coerceIn(top, height)

        val croppedWidth = right - left
        val croppedHeight = bottom - top

        if (croppedWidth <= 0 || croppedHeight <= 0) {
            return this
        }

        return Bitmap.createBitmap(this, left, top, croppedWidth, croppedHeight)
    }

    /** Get the color palette for the [Theme] resolved from the current browsing mode. */
    private fun getColor(context: Context, mode: BrowsingMode): ColorScheme =
        when (getTheme(mode, context.isSystemInDarkTheme(), context.components.settings.shouldUseOledTheme)) {
            Theme.Light -> acornLightColorScheme()
            Theme.Dark -> acornDarkColorScheme()
            Theme.Oled -> acornOledColorScheme()
            Theme.Private -> acornPrivateColorScheme()
        }
}
