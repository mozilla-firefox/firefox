/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.base.utils

import android.graphics.Typeface
import android.text.style.StyleSpan
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.core.text.HtmlCompat
import androidx.core.text.getSpans

/**
 * Method used to transform HTML strings containing bold style to [AnnotatedString] to be used inside composables.
 */
fun parseHtml(html: String): AnnotatedString {
    val text = HtmlCompat.fromHtml(html, HtmlCompat.FROM_HTML_MODE_COMPACT)
    return buildAnnotatedString {
        append(text)
        for (span in text.getSpans<StyleSpan>()) {
            if (span.style == Typeface.BOLD) {
                val start = text.getSpanStart(span)
                val end = text.getSpanEnd(span)
                addStyle(SpanStyle(fontWeight = FontWeight.Bold), start, end)
            }
        }
    }
}
