/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.selectors

import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.ui.efficiency.helpers.PageReadinessProfiles
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.helpers.SelectorContainer
import org.mozilla.fenix.ui.efficiency.helpers.SelectorStrategy

object TranslationsSelectors : SelectorContainer {

    val TRANSLATIONS_OPTIONS_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.translation_option_bottom_sheet_title_heading2),
            description = "Translations sheet options button",
            readiness = PageReadinessProfiles.IDENTITY_ANCHOR,
        )

    val TRANSLATION_SETTINGS_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.translation_option_bottom_sheet_translation_settings),
            description = "the Translation settings button on the options sheet",
        )

    val TRANSLATION_SHEET_BACK_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = "Navigate back",
            description = "the Translation sheet back button",
        )

    val TRANSLATION_SHEET_HANDLE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.translation_option_bottom_sheet_close_content_description),
            description = "the Translation sheet handle",
        )

    val TRANSLATION_IN_PROGRESS_SNACK_BAR =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.translation_in_progress_snackbar),
            description = "the Translation in progress snackbar",
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
        )

    @Suppress("ktlint:standard:function-naming", "FunctionName")
    fun ALWAYS_TRANSLATE_LANGUAGE(language: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT_MERGED,
            value = getStringResource(R.string.translation_option_bottom_sheet_always_translate_in_language, language),
            description = "the Always translate $language row",
        )

    val ALWAYS_OFFER_TO_TRANSLATE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT_MERGED,
            value = getStringResource(R.string.translation_option_bottom_sheet_always_translate),
            description = "the Always translate row",
        )

    val NEVER_TRANSLATE_DESCRIPTION =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.translation_option_bottom_sheet_switch_description),
            description = "the never-translate override description",
        )
}
