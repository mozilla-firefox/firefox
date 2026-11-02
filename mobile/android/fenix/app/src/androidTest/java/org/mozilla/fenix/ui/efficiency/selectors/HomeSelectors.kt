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

object HomeSelectors : SelectorContainer {
    enum class Group : SelectorGroup {
        TOP_SITES,
        TOP_SITES_COMPOSE,
        PRIVATE_BROWSING,
        HOME_SCREEN,
        CONTINUE,
        RECENT_BOOKMARKS_SECTION,
        PRIVATE_BROWSING_HOME_SCREEN,
        TOP_SITE_ITEM,
        HOME_SCREEN_UI,
        ADD_TO_HOMEPAGE_BOTTOM_SHEET,
    }

    val TOP_SITES_LIST =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID,
            value = "top_sites_list",
            description = "Top Sites List",
            groups = setOf(Group.TOP_SITES),
        )

    val TOP_SITES_LIST_COMPOSE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = "top_sites_list",
            description = "Top Sites List",
            groups = setOf(Group.TOP_SITES_COMPOSE),
        )

    val HOMEPAGE_VIEW =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = "homepage.view",
            description = "Homepage view",
            readiness = PageReadinessProfiles.IDENTITY_ANCHOR,
        )

    val MAIN_MENU_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.content_description_menu),
            description = "Three Dot Menu",
            readiness = PageReadinessProfiles.READY_CONTENT,
        )

    // Use UIAutomator when navigating from BrowserPage — avoids Compose sync hanging when GeckoView is active.
    val MAIN_MENU_BUTTON_UIAUTOMATOR =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_DESCRIPTION_CONTAINS,
            value = getStringResource(R.string.content_description_menu),
            description = "Three Dot Menu",
        )

    val PRIVATE_BROWSING_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.content_description_private_browsing),
            description = "Private browsing button",
            groups = setOf(Group.PRIVATE_BROWSING),
        )

    val HOME_WORDMARK_LOGO =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = "homepage.wordmark.logo",
            description = "the home screen wordmark logo",
            groups = setOf(Group.HOME_SCREEN),
        )

    val HOME_WORDMARK_TEXT =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = "homepage.wordmark.text",
            description = "the home screen wordmark text",
            groups = setOf(Group.HOME_SCREEN),
        )

    val COLLECTIONS_HEADER =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.collections_header),
            description = "the Collections header",
            groups = setOf(Group.HOME_SCREEN),
        )

    val TAB_COUNTER_ZERO =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = "Non-private Tabs Open: 0. Tap to switch tabs.",
            description = "the tab counter showing zero open tabs",
            groups = setOf(Group.HOME_SCREEN),
        )

    val CONTINUE_SECTION =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.recent_tabs_header_2),
            description = "Continue section header",
            groups = setOf(Group.CONTINUE),
        )

    val BOOKMARKS_SECTION_TITLE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.home_bookmarks_title),
            description = "Bookmarks section header",
            groups = setOf(Group.RECENT_BOOKMARKS_SECTION),
        )

    val BOOKMARKS_SECTION_SHOW_ALL_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.home_bookmarks_show_all_content_description),
            description = "Bookmarks section 'Show All' button",
            groups = setOf(Group.RECENT_BOOKMARKS_SECTION),
        )

    @Suppress("ktlint:standard:function-naming", "FunctionName")
    fun BOOKMARKS_SECTION_ITEM(bookmarkTitle: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG_AND_TEXT,
            value = "bookmark.title",
            secondaryValue = bookmarkTitle,
            description = "Bookmarks item with title: $bookmarkTitle",
        )

    val PRIVATE_BROWSING_INFO_CARD_TITLE =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = getStringResource(R.string.felt_privacy_desc_card_title),
            description = "Private browsing info card title",
            groups = setOf(Group.PRIVATE_BROWSING_HOME_SCREEN),
        )

    val PRIVATE_BROWSING_INFO_CARD_DESCRIPTION =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = getStringResource(R.string.felt_privacy_info_card_subtitle_3),
            description = "Private browsing info card description",
            groups = setOf(Group.PRIVATE_BROWSING_HOME_SCREEN),
        )

    // The learn more link is a Compose LinkText wrapped in a Box tagged with this test tag; the tag is the reliable
    // handle for both asserting the link is shown and clicking it (matching the legacy robot's click by tag).
    val PRIVATE_BROWSING_LEARN_MORE_LINK =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = "homepage.private.browsing.learn.more.link",
            description = "Private browsing 'Who might be able to see my activity?' learn more link",
        )

    @Suppress("FunctionName")
    fun TOP_SITE_ITEM(topSiteTitle: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_ON_ALL_NODES_BY_TAG_WITH_CHILD_TEXT_ON_FIRST,
            value = "top_sites_list.top_site_item",
            secondaryValue = topSiteTitle,
            description = "Top site item with title: $topSiteTitle",
            groups = setOf(Group.TOP_SITE_ITEM),
        )

    val FIRST_TOP_SITE_ITEM =
        Selector(
            strategy = SelectorStrategy.COMPOSE_ON_ALL_NODES_BY_TAG_ON_FIRST,
            value = "top_sites_list.top_site_item",
            description = "First top site item",
            groups = setOf(Group.TOP_SITE_ITEM),
        )

    val ADD_SHORTCUT_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = "top_sites_list.add_shortcut_item.add_shortcut_title",
            description = "Add shortcut button",
        )

    val ADD_TO_HOMEPAGE_BOTTOM_SHEET_TITLE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.homepage_shortcuts_add_to_homepage),
            description = "Add to homepage bottom sheet title",
            groups = setOf(Group.ADD_TO_HOMEPAGE_BOTTOM_SHEET),
        )

    val ADD_TO_HOMEPAGE_BOTTOM_SHEET_ADD_WEBSITE_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.homepage_shortcuts_add_website),
            description = "Add to homepage bottom sheet add website button",
            groups = setOf(Group.ADD_TO_HOMEPAGE_BOTTOM_SHEET),
        )

    val ADD_TO_HOMEPAGE_BOTTOM_SHEET_POPULAR_SITES_SECTION_TITLE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.homepage_shortcuts_popular_sites),
            description = "Add to homepage bottom sheet popular sites title",
            groups = setOf(Group.ADD_TO_HOMEPAGE_BOTTOM_SHEET),
        )

    // All popular-site cells. The suggestions are data-driven (top-ranked Merino manifest domains minus
    // the current top sites), so their titles are unstable and messy — count the tagged cells instead of
    // asserting individual titles. There are always POPULAR_SITES_LIMIT of them.
    val ADD_TO_HOMEPAGE_BOTTOM_SHEET_POPULAR_SITES =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = "add.to.home.page.popular.site.item",
            description = "Add to homepage bottom sheet popular site cells",
        )

    // A single popular-site cell, identified by its title. Matches the tagged clickable cell whose child
    // text is the title and resolves the cell container (not the bare title text node), mirroring
    // TOP_SITE_ITEM — so clicking it fires the cell's onClick.
    @Suppress("ktlint:standard:function-naming", "FunctionName")
    fun ADD_TO_HOMEPAGE_BOTTOM_SHEET_POPULAR_SITE_ITEM(popularSiteTitle: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_ON_ALL_NODES_BY_TAG_WITH_CHILD_TEXT_ON_FIRST,
            value = "add.to.home.page.popular.site.item",
            secondaryValue = popularSiteTitle,
            description = "Add to homepage bottom sheet popular site cell with title: $popularSiteTitle",
        )

    val ADD_TO_HOMEPAGE_BOTTOM_SHEET_POPULAR_SITES_ITEM_TITLE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG_AND_TEXT,
            value = "add.to.home.page.popular.site.item.title",
            description = "Add to homepage bottom sheet popular site title",
        )

    // Snackbar shown on the homepage after a shortcut is added (same string as the browser-menu flow in
    // BrowserPageSelectors). The snackbar is a system-window toast, so it is matched via UIAutomator.
    val ADDED_TO_SHORTCUTS_SNACKBAR_TEXT =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.snackbar_added_to_shortcuts),
            description = "Added to shortcuts snackbar text",
        )

    // The legacy robot hardcodes the English literal "Recently visited"; this keys off the string resource
    // it duplicates, so the selector survives localization.
    val RECENTLY_VISITED_HEADER =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.history_metadata_header_2),
            description = "Recently visited section header",
        )

    // A search group in the "Recently visited" section, titled with the search term. Exact text, because the
    // term is also the query and a CONTAINS match would collide with the awesomebar and history rows.
    // The group's "N pages" size caption is a sibling text node — assert it with
    // mozVerifyElementHasSiblingWithText, not as part of this selector.
    @Suppress("ktlint:standard:function-naming", "FunctionName")
    fun RECENTLY_VISITED_SEARCH_GROUP(searchTerm: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = searchTerm,
            description = "'$searchTerm' search group",
        )

    // The "blocked trackers" widget pill. We assert this (a COMPOSE_BY_TAG handle) rather than the card's
    // label text because the label is state-dependent: a fresh homepage reads "Help catch trackers" while a
    // homepage that has blocked at least one tracker reads "You're protected". The protectionStatusPill tag
    // (TRACKERS_BLOCKED_CARD) is present in both states, so it is a stable, state-independent presence check;
    // asserting either label would flake as soon as the tracker count changes.
    val TRACKERS_BLOCKED_CARD =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = "trackersBlockedCard.protectionStatusPill",
            description = "Blocked trackers widget pill",
            groups = setOf(Group.HOME_SCREEN_UI),
        )

    // Private-browsing toggle in the homepage header. Hidden when the middle search bar is enabled, so it is not a
    // page-identity anchor.
    val PRIVATE_MODE_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = "private.browsing.homepage.button",
            description = "Private browsing homepage button",
            groups = setOf(Group.HOME_SCREEN_UI),
        )

    // Stories entry point in the homepage header. Only rendered when showPocketRecommendationsFeature (Pocket) is on,
    // so it is not a page-identity anchor.
    val STORIES_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.homepage_all_stories),
            description = "Stories button",
            groups = setOf(Group.HOME_SCREEN_UI),
        )
}
