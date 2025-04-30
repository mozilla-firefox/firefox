/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.search.awesomebar

import android.content.Context
import android.graphics.Bitmap
import androidx.annotation.VisibleForTesting
import androidx.appcompat.content.res.AppCompatResources
import androidx.core.graphics.BlendModeColorFilterCompat.createBlendModeColorFilterCompat
import androidx.core.graphics.BlendModeCompat.SRC_IN
import androidx.core.graphics.drawable.toBitmap
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.browser.state.state.searchEngines
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.awesomebar.AwesomeBar
import mozilla.components.support.ktx.android.content.getColorFromAttr
import org.mozilla.fenix.R
import java.util.UUID

/**
 * A [AwesomeBar.SuggestionProvider] implementation that provides search engine suggestions.
 */
class ShortcutsSuggestionProvider(
    private val store: BrowserStore,
    private val context: Context,
    private val selectShortcutEngine: (engine: SearchEngine) -> Unit,
    private val selectShortcutEngineSettings: () -> Unit,
) : AwesomeBar.SuggestionProvider {
    override val id: String = UUID.randomUUID().toString()

    /**
     * Retrieves a tinted settings icon Bitmap.
     *
     * @return A [Bitmap] representing the tinted settings icon, or `null` if the drawable
     *         resource cannot be found or accessed.
     * @see R.drawable.mozac_ic_settings_24
     * @see R.attr.textPrimary
     */
    @VisibleForTesting
    internal fun settingsIcon(): Bitmap? =
        AppCompatResources.getDrawable(context, R.drawable.mozac_ic_settings_24)?.apply {
            colorFilter = createBlendModeColorFilterCompat(
                context.getColorFromAttr(R.attr.textPrimary),
                SRC_IN,
            )
        }?.toBitmap()

    override suspend fun onInputChanged(text: String): List<AwesomeBar.Suggestion> {
        val suggestions = mutableListOf<AwesomeBar.Suggestion>()

        store.state.search.searchEngines.mapTo(suggestions) {
            AwesomeBar.Suggestion(
                provider = this,
                id = it.id,
                icon = it.icon,
                title = it.name,
                onSuggestionClicked = {
                    selectShortcutEngine(it)
                },
            )
        }

        suggestions.add(
            AwesomeBar.Suggestion(
                provider = this,
                id = context.getString(R.string.search_shortcuts_engine_settings),
                icon = settingsIcon(),
                title = context.getString(R.string.search_shortcuts_engine_settings),
                onSuggestionClicked = {
                    selectShortcutEngineSettings()
                },
            ),
        )
        return suggestions
    }
}
