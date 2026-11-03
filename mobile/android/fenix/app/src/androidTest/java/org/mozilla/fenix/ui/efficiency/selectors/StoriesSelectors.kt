/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.selectors

import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.ui.efficiency.helpers.PageReadinessProfiles
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.helpers.SelectorContainer
import org.mozilla.fenix.ui.efficiency.helpers.SelectorGroup
import org.mozilla.fenix.ui.efficiency.helpers.SelectorStrategy

object StoriesSelectors : SelectorContainer {
    enum class Group : SelectorGroup {
        STORIES_VIEW
    }

    // Centered top-app-bar title of the Stories view. Static (independent of the story data), so it is the
    // page's identity anchor. Keyed off the string resource so it survives localization.
    val NEWS_HEADER =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.stories_screen_text_news),
            description = "Stories view 'News' header",
            readiness = PageReadinessProfiles.IDENTITY_ANCHOR,
        )

    // Top-left navigation button. Clicking it returns to the homepage.
    val BACK_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.stories_back_button_content_description),
            description = "Stories view back button",
            groups = setOf(Group.STORIES_VIEW),
        )
}
