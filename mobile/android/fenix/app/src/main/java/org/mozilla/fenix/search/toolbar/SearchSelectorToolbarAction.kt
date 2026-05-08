/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.search.toolbar

import android.content.Context
import android.graphics.drawable.BitmapDrawable
import android.view.View
import android.view.ViewGroup
import androidx.annotation.VisibleForTesting
import androidx.core.graphics.drawable.toDrawable
import androidx.core.graphics.scale
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.concept.menu.Orientation
import mozilla.components.concept.toolbar.Toolbar
import mozilla.components.lib.state.ext.flow
import mozilla.components.support.ktx.android.content.getColorFromAttr
import mozilla.components.support.ktx.android.content.res.resolveAttribute
import mozilla.components.support.ktx.android.view.toScope
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.pixelSizeFor
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.search.SearchFragmentStore
import org.mozilla.fenix.telemetry.ACTION_SEARCH_ENGINE_SELECTOR_CLICKED
import org.mozilla.fenix.telemetry.SOURCE_ADDRESS_BAR
import com.google.android.material.R as materialR
import org.mozilla.fenix.GleanMetrics.Toolbar as GleanMetricsToolbar

/**
 * A [Toolbar.Action] implementation that shows a [SearchSelector].
 *
 * @param store [SearchFragmentStore] containing the complete state of the search UX.
 * @param defaultSearchEngine The user selected or default [SearchEngine].
 * @param menu An instance of [SearchSelectorMenu] to display a popup menu for the search
 * selections.
 * @param mainDispatcher [CoroutineDispatcher] to be used for the view scope.
 */
class SearchSelectorToolbarAction(
    private val store: SearchFragmentStore,
    private val defaultSearchEngine: SearchEngine?,
    private val menu: SearchSelectorMenu,
    private val mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
) : Toolbar.Action {
    private var updateIconJob: Job? = null

    override fun createView(parent: ViewGroup): View {
        val context = parent.context

        // Only search engines with type APPLICATION (tabs, history, bookmarks) will have a valid icon at this time.
        // For other search engines show the icon of the default search engine which should be shown in the selector.
        val initialSearchEngine = store.state.searchEngineSource.searchEngine ?: defaultSearchEngine
        return SearchSelector(context).apply {
            initialSearchEngine?.let {
                this.setIcon(
                    icon = initialSearchEngine.getScaledIcon(this.context),
                    contentDescription = context.getString(
                        R.string.search_engine_selector_content_description,
                        initialSearchEngine.name,
                    ),
                )
            }

            setOnClickListener {
                val orientation = if (context.settings().shouldUseBottomToolbar) {
                    Orientation.UP
                } else {
                    Orientation.DOWN
                }

                GleanMetricsToolbar.buttonTapped.record(
                    GleanMetricsToolbar.ButtonTappedExtra(
                        source = SOURCE_ADDRESS_BAR,
                        item = ACTION_SEARCH_ENGINE_SELECTOR_CLICKED,
                    ),
                )

                menu.menuController.show(
                    anchor = it.findViewById(R.id.search_selector),
                    orientation = orientation,
                )
            }

            val topPadding = pixelSizeFor(R.dimen.search_engine_engine_icon_top_margin)
            setPadding(0, topPadding, 0, 0)

            setBackgroundResource(
                context.theme.resolveAttribute(android.R.attr.selectableItemBackgroundBorderless),
            )
        }
    }

    override fun bind(view: View) {
        // It may happen that this View is binded multiple times.
        // Prevent launching new coroutines for every time this is binded and only update the icon once.
        if (updateIconJob?.isActive != true) {
            updateIconJob = (view as? SearchSelector)?.toScope(mainDispatcher = mainDispatcher)?.launch {
                store.flow()
                    .map { state -> state.searchEngineSource.searchEngine }
                    .filterNotNull()
                    .distinctUntilChanged()
                    .collect { searchEngine ->
                        view.setIcon(
                            icon = searchEngine.getScaledIcon(view.context).apply {
                                // Setting tint manually for icons that were converted from Drawable
                                // to Bitmap. Search Engine icons are stored as Bitmaps, hence
                                // theming/attribute mechanism won't work.
                                if (searchEngine.type == SearchEngine.Type.APPLICATION) {
                                    setTint(view.context.getColorFromAttr(materialR.attr.colorOnSurface))
                                }
                            },
                            contentDescription = view.context.getString(
                                R.string.search_engine_selector_content_description,
                                searchEngine.name,
                            ),
                        )
                    }
            }.also {
                it?.start()
            }
        }
    }
}

/**
 * Get the search engine icon appropriately scaled to be shown in the selector.
 */
@VisibleForTesting
internal fun SearchEngine.getScaledIcon(context: Context): BitmapDrawable {
    val iconSize =
        context.pixelSizeFor(R.dimen.preference_icon_drawable_size)
    val scaledIcon = icon.scale(iconSize, iconSize, filter = true)

    return scaledIcon.toDrawable(context.resources)
}
