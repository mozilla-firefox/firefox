/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.search

import android.content.Context
import android.graphics.Bitmap
import androidx.appcompat.content.res.AppCompatResources.getDrawable
import androidx.core.graphics.drawable.toBitmap
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import mozilla.components.browser.state.action.BrowserAction
import mozilla.components.browser.state.action.InitAction
import mozilla.components.browser.state.action.SearchAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.feature.search.ext.createApplicationSearchEngine
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import org.mozilla.fenix.R
import mozilla.components.ui.icons.R as iconsR

const val HISTORY_SEARCH_ENGINE_ID = "history_search_engine_id"
const val BOOKMARKS_SEARCH_ENGINE_ID = "bookmarks_search_engine_id"
const val TABS_SEARCH_ENGINE_ID = "tabs_search_engine_id"

/**
 * [Middleware] implementation for creating and loading the application search engines
 *
 * @param context the applications context.
 * @param stringProvider a function that will return a string for a given resource id.
 * @param bitmapProvider a function that will return a bitmap for a given resource id.
 * @param scope [CoroutineScope] used to launch coroutines.
 */
class ApplicationSearchMiddleware(
    context: Context,
    private val stringProvider: (Int) -> String = { context.getString(it) },
    private val bitmapProvider: (Int) -> Bitmap = { getDrawable(context, it)?.toBitmap()!! },
    private val scope: CoroutineScope = CoroutineScope(Dispatchers.IO),
) : Middleware<BrowserState, BrowserAction> {
    override fun invoke(
        store: Store<BrowserState, BrowserAction>,
        next: (BrowserAction) -> Unit,
        action: BrowserAction,
    ) {
        if (action is InitAction) {
            loadSearchEngines(store)
        }

        next(action)
    }

    private fun loadSearchEngines(
        store: Store<BrowserState, BrowserAction>,
    ) = scope.launch {
        val searchEngines = listOf(
            createApplicationSearchEngine(
                id = BOOKMARKS_SEARCH_ENGINE_ID,
                name = stringProvider(R.string.library_bookmarks),
                url = "",
                icon = bitmapProvider(iconsR.drawable.mozac_ic_bookmark_tray_fill_24),
            ),
            createApplicationSearchEngine(
                id = TABS_SEARCH_ENGINE_ID,
                name = stringProvider(R.string.preferences_tabs),
                url = "",
                icon = bitmapProvider(iconsR.drawable.mozac_ic_tab_tray_24),
            ),
            createApplicationSearchEngine(
                id = HISTORY_SEARCH_ENGINE_ID,
                name = stringProvider(R.string.library_history),
                url = "",
                icon = bitmapProvider(iconsR.drawable.mozac_ic_history_24),
            ),
        )

        store.dispatch(SearchAction.ApplicationSearchEnginesLoaded(searchEngines))
    }
}
