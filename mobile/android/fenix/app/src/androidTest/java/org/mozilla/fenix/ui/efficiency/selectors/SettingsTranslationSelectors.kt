/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.selectors

import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.helpers.SelectorStrategy

object SettingsTranslationSelectors {

    val DOWNLOAD_LANGUAGES_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.translation_settings_download_language),
            description = "Download languages button",
            groups = listOf("requiredForPage"),
        )

    /**
     * A row in the Download languages list, matched on a content-description substring because the row's description
     * also carries the download size and state. Pure by design (gotcha B4): it only substitutes the caller's language
     * name.
     */
    @Suppress("ktlint:standard:function-naming", "FunctionName")
    fun DOWNLOAD_LANGUAGE_ROW(language: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION_SUBSTRING,
            value = language,
            description = "the $language row in the Download languages list",
            groups = listOf(),
        )

    /**
     * Proof that at least one language model is actually downloaded: the bulk row's label is "All languages" while
     * nothing is downloaded and "Delete all languages" once something is, so its text alone carries the state.
     *
     * Deliberately not matched on the row content descriptions. Those do encode state — each is "<language> <size>"
     * plus "Download" or "Delete" — but a content-description substring of "Delete" matches both the language row and
     * this bulk row, and Compose reports that ambiguity as "not found" (gotcha A41), which reads exactly like the
     * download having failed. Substring rather than exact text because the rendered label appends the size, e.g.
     * "Delete all languages (45.53 MB)".
     */
    val ANY_LANGUAGE_DOWNLOADED =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT_SUBSTRING,
            value = getStringResource(R.string.download_language_all_languages_item_preference_to_delete),
            description = "the Delete all languages row, shown only when a model is downloaded",
            groups = listOf(),
        )

    val AUTOMATIC_TRANSLATION_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.translation_settings_automatic_translation),
            description = "the Automatic translation button",
            groups = listOf(),
        )

    /**
     * The Never translate row under Automatic translation > <language>. A RadioButtonListItem rather than a switch, but
     * the same semantics story as the options sheet: `selected` sits on the merged row, and the radio icon's
     * "<label>.radio.button" testTag survives a clearAndSetSemantics that strips its state — so that tag identifies the
     * icon but cannot be asserted against. Hence MERGED text plus mozVerifyElementIsSelected.
     */
    val NEVER_TRANSLATE_PREFERENCE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT_MERGED,
            value = getStringResource(R.string.automatic_translation_option_never_translate_title_preference),
            description = "the Never translate preference row",
            groups = listOf(),
        )

    /** A language row in the Automatic translation list. */
    @Suppress("ktlint:standard:function-naming", "FunctionName")
    fun AUTOMATIC_TRANSLATION_LANGUAGE(language: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = language,
            description = "the $language row in the Automatic translation list",
            groups = listOf(),
        )

    val all =
        listOf(
            DOWNLOAD_LANGUAGES_BUTTON,
            DOWNLOAD_LANGUAGE_ROW(),
            ANY_LANGUAGE_DOWNLOADED,
            AUTOMATIC_TRANSLATION_BUTTON,
            NEVER_TRANSLATE_PREFERENCE,
            AUTOMATIC_TRANSLATION_LANGUAGE(),
        )
}
