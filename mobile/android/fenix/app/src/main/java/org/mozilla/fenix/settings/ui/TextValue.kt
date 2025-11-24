/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.ui

import android.content.Context
import androidx.annotation.StringRes
import androidx.compose.runtime.Composable
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource

/**
 * Represents a text value backed by a string resource ID.
 * This allows for lazy resolution of strings in Compose.
 *
 * @property resId The string resource ID.
 * @property formatArgs Optional format arguments for the string resource.
 *                      These can be strings, numbers, or other TextValue instances.
 */
data class TextValue(
    @param:StringRes val resId: Int,
    val formatArgs: List<Any> = emptyList(),
) {
    /**
     * Resolves a [TextValue] to a string in a Composable context.
     * Format arguments can be:
     * - Plain values (String, Int, etc.) - used directly
     * - @StringRes Int - resolved via stringResource
     * - TextValue - resolved recursively
     *
     * @return The resolved string value.
     */
    fun resolve(context: Context): String {
        if (formatArgs.isEmpty()) return context.getString(resId)

        val resolvedArgs = formatArgs.map {
            when (it) {
                is TextValue -> it.resolve(context)
                is Int -> context.getString(it)
                else -> it
            }
        }.toTypedArray()

        return context.getString(resId, *resolvedArgs)
    }

    /**
     * Resolves a [TextValue] to a string in a Composable context.
     * Format arguments can be:
     * - Plain values (String, Int, etc.) - used directly
     * - @StringRes Int - resolved via stringResource
     * - TextValue - resolved recursively
     *
     * @return The resolved string value.
     */
    @Composable
    fun resolve(): String = resolve(LocalContext.current)

    companion object {
        /**
         * Creates a [TextValue] from a string resource ID.
         *
         * @param resId The string resource ID.
         * @param formatArgs Optional format arguments.
         */
        fun fromRes(@StringRes resId: Int, vararg formatArgs: Any): TextValue =
            TextValue(resId, formatArgs.toList())
    }
}
