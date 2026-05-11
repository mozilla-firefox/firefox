/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.search.awesomebar

import android.content.Context
import android.graphics.Bitmap
import android.graphics.drawable.Drawable
import androidx.annotation.DrawableRes
import androidx.appcompat.content.res.AppCompatResources
import androidx.core.graphics.BlendModeColorFilterCompat.createBlendModeColorFilterCompat
import androidx.core.graphics.BlendModeCompat.SRC_IN
import androidx.core.graphics.drawable.toBitmap
import mozilla.components.support.ktx.android.content.getColorFromAttr
import org.mozilla.fenix.R
import com.google.android.material.R as materialR
import mozilla.components.ui.icons.R as iconsR

/**
 * Provides themed/tinted icons for search suggestions.
 * This interface abstracts the retrieval and potential processing (like tinting)
 * of icons used within the awesomebar suggestions.
 */
interface SuggestionIconProvider {

    /**
     * Provides a standard search icon, typically tinted with the primary text color.
     * The result is cached for efficiency.
     *
     * @return A [Bitmap] of the search icon, or null if it cannot be loaded/created.
     */
    fun getSearchIconBitmap(): Bitmap?

    /**
     * Provides a search icon that might include additional visual elements (e.g., "search with").
     * The result is cached for efficiency.
     *
     * @return A [Bitmap] of the "search with" icon, or null if it cannot be loaded/created.
     */
    fun getSearchWithIconBitmap(): Bitmap?

    /**
     * Provides a standard history icon.
     * The result is cached for efficiency.
     *
     * @return A [Bitmap] of the history icon, or null if it cannot be loaded/created.
     */
    fun getHistoryIconBitmap(): Bitmap?

    /**
     * Provides an icon for bookmark suggestions.
     * The result is cached for efficiency.
     *
     * @return A [Drawable] of the bookmark icon, or null if it cannot be loaded/created.
     */
    fun getBookmarkIconDrawable(): Drawable?

    /**
     * Provides an icon for local tabs (open tabs) suggestions.
     * This method returns a Drawable as it might be used directly where a Drawable is expected,
     * and it's assumed to be used infrequently enough not to warrant bitmap caching by default.
     *
     * @return A [Drawable] of the local tab icon, or null if it cannot be loaded/created.
     */
    fun getLocalTabIconDrawable(): Drawable?

    /**
     * Provides a desktop icon.
     *
     * @return A [Drawable] of the desktop icon, or null if it cannot be loaded.
     */
    fun getDesktopIconDrawable(): Drawable?

    /**
     * Provides a mobile icon.
     *
     * @return A [Drawable] of the mobile icon, or null if it cannot be loaded/created.
     */
    fun getMobileIconDrawable(): Drawable?

    /**
     * Provides a tablet icon.
     *
     * @return A [Drawable] of the tablet icon, or null if it cannot be loaded/created.
     */
    fun getTabletIconDrawable(): Drawable?

    /**
     * Provides a generic icon for a given drawable resource ID, potentially tinted.
     * This can be used for other icons like synced devices, etc.
     * This method does not cache its result as inputs can vary.
     *
     * @param drawableRes The resource ID of the drawable.
     * @param tintWithPrimaryColor If true, the icon will be tinted with the primary text color.
     * @return A [Bitmap] of the icon, or null if it cannot be loaded/created.
     */
    fun getGenericIconBitmap(
        @DrawableRes drawableRes: Int,
        tintWithPrimaryColor: Boolean = false,
    ): Bitmap?
}

/**
 * Default implementation of [SuggestionIconProvider].
 * It loads drawables using [AppCompatResources], applies tinting where appropriate,
 * and converts them to [Bitmap]s. Frequently used icons are cached lazily.
 *
 * @param context The [Context] used for resource loading and attribute retrieval.
 */
class DefaultSuggestionIconProvider(private val context: Context) : SuggestionIconProvider {

    private val primaryTextColor: Int by lazy {
        context.getColorFromAttr(materialR.attr.colorOnSurface)
    }

    private val searchIconBitmapInstance: Bitmap? by lazy {
        AppCompatResources.getDrawable(context, iconsR.drawable.mozac_ic_search_24)?.apply {
            colorFilter = createBlendModeColorFilterCompat(
                primaryTextColor,
                SRC_IN,
            )
        }?.toBitmap()
    }

    private val searchWithIconBitmapInstance: Bitmap? by lazy {
        AppCompatResources.getDrawable(context, R.drawable.ic_search_with)?.toBitmap()
    }

    private val historyIconBitmapInstance: Bitmap? by lazy {
        AppCompatResources.getDrawable(context, iconsR.drawable.mozac_ic_history_24)?.toBitmap()
    }

    override fun getSearchIconBitmap(): Bitmap? {
        return searchIconBitmapInstance
    }

    override fun getSearchWithIconBitmap(): Bitmap? {
        return searchWithIconBitmapInstance
    }

    override fun getHistoryIconBitmap(): Bitmap? {
        return historyIconBitmapInstance
    }

    override fun getBookmarkIconDrawable(): Drawable? {
        return AppCompatResources.getDrawable(context, R.drawable.ic_search_results_bookmarks)
    }

    override fun getLocalTabIconDrawable(): Drawable? {
        return AppCompatResources.getDrawable(context, R.drawable.ic_search_results_tab)
    }

    override fun getDesktopIconDrawable(): Drawable? {
        return AppCompatResources.getDrawable(context, R.drawable.ic_search_results_device_desktop)
    }

    override fun getMobileIconDrawable(): Drawable? {
        return AppCompatResources.getDrawable(context, R.drawable.ic_search_results_device_mobile)
    }

    override fun getTabletIconDrawable(): Drawable? {
        return AppCompatResources.getDrawable(context, R.drawable.ic_search_results_device_tablet)
    }

    override fun getGenericIconBitmap(
        @DrawableRes drawableRes: Int,
        tintWithPrimaryColor: Boolean,
    ): Bitmap? {
        return AppCompatResources.getDrawable(context, drawableRes)?.apply {
            if (tintWithPrimaryColor) {
                colorFilter = createBlendModeColorFilterCompat(
                    primaryTextColor,
                    SRC_IN,
                )
            }
        }?.toBitmap()
    }
}
