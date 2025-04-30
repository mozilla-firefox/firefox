/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.widget

import android.content.Context
import android.util.AttributeSet
import android.view.View
import androidx.core.view.isVisible
import androidx.preference.ListPreference
import androidx.preference.PreferenceViewHolder
import org.mozilla.focus.R
import org.mozilla.focus.ext.settings

/**
 * Autocomplete preference that will show a sub screen to configure the autocomplete behavior.
 */
class CookiesPreference(context: Context, attrs: AttributeSet?) : ListPreference(context, attrs) {

    override fun onBindViewHolder(holder: PreferenceViewHolder) {
        super.onBindViewHolder(holder)
        updateSummary()
        showIcon(holder)
    }

    override fun notifyChanged() {
        super.notifyChanged()
        updateSummary()
    }

    fun updateSummary() {
        val settings = context.settings

        val cookieOptionIndex =
            context.resources.getStringArray(R.array.cookies_options_entry_values)
                .asList().indexOf(settings.shouldBlockCookiesValue)
        this.summary =
            context.resources.getStringArray(R.array.cookies_options_entries)[cookieOptionIndex]
    }

    private fun showIcon(holder: PreferenceViewHolder?) {
        val widgetFrame: View? = holder?.findViewById(android.R.id.widget_frame)
        widgetFrame?.isVisible = true
    }
}
