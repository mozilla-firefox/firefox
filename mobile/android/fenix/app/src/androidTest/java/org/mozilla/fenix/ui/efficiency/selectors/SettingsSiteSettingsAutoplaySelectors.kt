/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.selectors

import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.helpers.SelectorStrategy

/**
 * The Autoplay permission screen: four native RadioButtons in the layout shared by every permission screen.
 *
 * Two things about this screen decided the strategies below, both taken from a live dump rather than from the legacy
 * robot. First, each radio's text is the label AND its subtext joined by a newline ("Block audio only\nRecommended"),
 * so an exact-text match never resolves. Second, the ids are shared with Camera/Location/Microphone, where the 3rd and
 * 4th radios are merely hidden — so an id cannot identify this screen. Hence: a text-CONTAINS anchor on the one label
 * that is autoplay-only, and res-ids for the per-option assertions. UIAUTOMATOR_WITH_RES_ID resolves against the
 * accessibility tree, which contains only displayed nodes, so presence here carries the visibility guarantee that the
 * legacy withEffectiveVisibility(VISIBLE) checks did.
 */
object SettingsSiteSettingsAutoplaySelectors {

    val AUTOPLAY_CELLULAR_DATA_LABEL =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = "cellular data only",
            description = "the Block audio and video on cellular data only label",
            groups = listOf("requiredForPage"),
        )

    val ALLOW_AUDIO_AND_VIDEO_RADIO =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID,
            value = "ask_to_allow_radio",
            description = "the Allow audio and video radio button",
            groups = listOf(),
        )

    val BLOCK_AUDIO_AND_VIDEO_ON_CELLULAR_RADIO =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID,
            value = "block_radio",
            description = "the Block audio and video on cellular data only radio button",
            groups = listOf("autoplayOptions"),
        )

    val BLOCK_AUDIO_ONLY_RADIO =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID,
            value = "third_radio",
            description = "the Block audio only radio button",
            groups = listOf("autoplayOptions"),
        )

    val BLOCK_AUDIO_AND_VIDEO_RADIO =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID,
            value = "fourth_radio",
            description = "the Block audio and video radio button",
            groups = listOf("autoplayOptions"),
        )

    val all =
        listOf(
            AUTOPLAY_CELLULAR_DATA_LABEL,
            ALLOW_AUDIO_AND_VIDEO_RADIO,
            BLOCK_AUDIO_AND_VIDEO_ON_CELLULAR_RADIO,
            BLOCK_AUDIO_ONLY_RADIO,
            BLOCK_AUDIO_AND_VIDEO_RADIO,
        )
}
