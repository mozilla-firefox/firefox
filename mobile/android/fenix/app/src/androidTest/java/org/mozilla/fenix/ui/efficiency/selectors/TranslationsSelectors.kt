/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.selectors

import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.helpers.SelectorStrategy

object TranslationsSelectors {

    val TRANSLATIONS_OPTIONS_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.translation_option_bottom_sheet_title_heading),
            description = "Translations sheet options button",
            groups = listOf("requiredForPage"),
        )

    val TRANSLATION_SETTINGS_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.translation_option_bottom_sheet_translation_settings),
            description = "the Translation settings button on the options sheet",
            groups = listOf(),
        )

    /**
     * The "Never translate <language>" row on the options sheet. Uses the parameterized resource rather than the legacy
     * test's hand-concatenated "Never translate " + language literal.
     *
     * MERGED, not the usual COMPOSE_BY_TEXT, and that is load-bearing. SwitchListItem puts the state on the row itself
     * as `semantics { selected = checked; role = Role.Switch }` and blanks the Switch with clearAndSetSemantics, so no
     * ToggleableState exists anywhere in the tree. Only in the merged tree does the row absorb its label, making the
     * node that matches this text the same node that carries the state; COMPOSE_BY_TEXT forces useUnmergedTree and
     * lands on a stateless descendant instead. Assert with mozVerifyElementIsSelected — mozVerifyElementIsChecked can
     * never pass here.
     */
    @Suppress("ktlint:standard:function-naming", "FunctionName")
    fun NEVER_TRANSLATE_LANGUAGE(language: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT_MERGED,
            value = getStringResource(R.string.translation_option_bottom_sheet_never_translate_in_language, language),
            description = "the Never translate $language row",
            groups = listOf(),
        )

    val NEVER_TRANSLATE_DESCRIPTION =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.translation_option_bottom_sheet_switch_description),
            description = "the never-translate override description",
            groups = listOf(),
        )

    val all =
        listOf(
            TRANSLATIONS_OPTIONS_BUTTON,
            TRANSLATION_SETTINGS_BUTTON,
            NEVER_TRANSLATE_LANGUAGE(),
            NEVER_TRANSLATE_DESCRIPTION,
        )
}
