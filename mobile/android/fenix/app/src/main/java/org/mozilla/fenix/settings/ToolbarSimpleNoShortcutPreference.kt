/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings

import android.content.Context
import android.util.AttributeSet
import android.view.View.GONE
import android.view.View.VISIBLE
import android.widget.ImageView
import androidx.preference.PreferenceViewHolder
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.browsingmode.BrowsingMode.Normal
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.nimbus.FxNimbus

const val NO_SHORTCUT_SIMPLE_TOOLBAR_TYPE = "simple_no_shortcut"

internal class ToolbarSimpleNoShortcutPreference
@JvmOverloads
constructor(
    context: Context,
    attrs: AttributeSet? = null,
) : ToolbarShortcutPreference(context, attrs) {
    var isTranslationsFeatureEnabled: Boolean = false

    override val options: List<ShortcutOption>
        get() = simpleShortcutOptions.filterNot {
            (it.key == ShortcutType.SUMMARIZE && !isSummarizationAvailable) ||
                (it.key == ShortcutType.TRANSLATE && !isTranslationsFeatureAvailable)
        }

    // Summarization is unavailable in private browsing, so keep the option visible but disabled.
    override fun isOptionEnabled(option: ShortcutOption): Boolean =
        option.key != ShortcutType.SUMMARIZE || isBrowsingInNormalMode

    /** Optional callback for when a new shortcut option is selected. */
    var optionChangedListener: ((ShortcutOption?) -> Unit)? = null

    override fun readSelectedKey(): String = context.components.settings.toolbarSimpleShortcutKey

    override fun writeSelectedKey(key: String) {
        context.components.settings.toolbarSimpleShortcutKey = key
        optionChangedListener?.invoke((options.firstOrNull { it.key.value == key }))
    }

    override fun getToolbarType(): String = NO_SHORTCUT_SIMPLE_TOOLBAR_TYPE

    override fun getSelectedIconImageView(holder: PreferenceViewHolder): ImageView? {
        val simplePreview = holder.findViewById(R.id.toolbar_simple_shortcut_preview)
        val simpleNoShortcutPreview = holder.findViewById(R.id.toolbar_simple_no_shortcut_preview)
        val expandedPreview = holder.findViewById(R.id.toolbar_expanded_shortcut_preview)

        simplePreview.visibility = GONE
        simpleNoShortcutPreview.visibility = VISIBLE
        expandedPreview.visibility = GONE

        return null
    }

    private val isSummarizationAvailable: Boolean
        get() = context.components.core.summarizeFeatureSettings.shouldShowFeature

    private val isBrowsingInNormalMode: Boolean
        get() = context.components.appStore.state.mode == Normal

    private val isTranslationsFeatureAvailable: Boolean
        get() {
            val browserStore = this.context.components.core.store
            val isTranslationEngineSupported = browserStore.state.translationEngine.isEngineSupported ?: false

            return isTranslationEngineSupported &&
                isTranslationsFeatureEnabled &&
                FxNimbus.features.translations.value().mainFlowToolbarEnabled
        }
}
