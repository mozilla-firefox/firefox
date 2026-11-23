/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.selectors

import androidx.core.text.HtmlCompat
import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.TestHelper.appName
import org.mozilla.fenix.helpers.TestHelper.shortAppName
import org.mozilla.fenix.ui.efficiency.helpers.PageReadinessProfiles
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.helpers.SelectorContainer
import org.mozilla.fenix.ui.efficiency.helpers.SelectorGroup
import org.mozilla.fenix.ui.efficiency.helpers.SelectorStrategy

object UnifiedTrustPanelSelectors : SelectorContainer {
    enum class Group : SelectorGroup {
        CLEAR_COOKIES_AND_SITE_DATA_DIALOG
    }

    val CLEAR_COOKIES_AND_SITE_DATA_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.clear_site_data),
            description = "Unified trust panel clear cookies and site button",
            readiness = PageReadinessProfiles.IDENTITY_ANCHOR,
        )

    val CLEAR_COOKIES_AND_SITE_DATA_DIALOG_TITLE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.clear_site_data),
            description = "Unified trust panel clear site data dialog title",
            groups = setOf(Group.CLEAR_COOKIES_AND_SITE_DATA_DIALOG),
        )

    @Suppress("FunctionName")
    fun CLEAR_COOKIES_AND_SITE_DATA_DIALOG_DESCRIPTION(webSite: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value =
                HtmlCompat.fromHtml(
                        getStringResource(R.string.clear_site_data_dialog_description, argument = webSite),
                        HtmlCompat.FROM_HTML_MODE_LEGACY,
                    )
                    .toString(),
            description = "Unified trust panel clear cookies and site data dialog description",
            groups = setOf(Group.CLEAR_COOKIES_AND_SITE_DATA_DIALOG),
        )

    val CLEAR_COOKIES_AND_SITE_DATA_DIALOG_CLEAR_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.clear_site_data_dialog_positive_button_text),
            description = "Unified trust panel clear site data dialog clear button",
            groups = setOf(Group.CLEAR_COOKIES_AND_SITE_DATA_DIALOG),
        )

    val CLEAR_COOKIES_AND_SITE_DATA_DIALOG_CANCEL_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.clear_site_data_dialog_negative_button_text),
            description = "Unified trust panel clear site data dialog cancel button",
            groups = setOf(Group.CLEAR_COOKIES_AND_SITE_DATA_DIALOG),
        )

    // ── Site identity ───────────────────────────────────────────────────────
    // Match the unique testTag AND the text. Text alone is ambiguous — the host also renders in the
    // address bar (gotcha A7) — but tag alone would stop asserting which site the panel is describing.
    @Suppress("FunctionName")
    fun WEBSITE_TITLE(webSite: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG_AND_TEXT,
            value = "unified.trust.panel.website",
            secondaryValue = webSite,
            description = "Unified trust panel website title: $webSite",
        )

    @Suppress("FunctionName")
    fun WEBSITE_URL(webSiteURL: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG_AND_TEXT,
            value = "unified.trust.panel.website.url",
            secondaryValue = webSiteURL,
            description = "Unified trust panel website url: $webSiteURL",
        )

    // ── Enhanced Tracking Protection banner (content-description, substring) ──
    val ETP_BANNER_PROTECTED_TITLE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION_SUBSTRING,
            value = getStringResource(R.string.protection_panel_banner_protected_title, argument = shortAppName),
            description = "ETP banner: protected title",
        )

    val ETP_BANNER_NOT_SECURE_TITLE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION_SUBSTRING,
            value = getStringResource(R.string.protection_panel_banner_not_secure_title, argument = appName),
            description = "ETP banner: not-secure title",
        )

    val ETP_BANNER_NOT_SECURE_DESCRIPTION =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION_SUBSTRING,
            value = getStringResource(R.string.protection_panel_banner_not_secure_description),
            description = "ETP banner: not-secure description",
        )

    val ETP_BANNER_NOT_PROTECTED_TITLE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION_SUBSTRING,
            value = getStringResource(R.string.protection_panel_banner_not_protected_title),
            description = "ETP banner: not-protected title",
        )

    val ETP_BANNER_NOT_PROTECTED_DESCRIPTION =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION_SUBSTRING,
            value =
                getStringResource(R.string.protection_panel_banner_not_protected_description, argument = shortAppName),
            description = "ETP banner: not-protected description",
        )

    // ── ETP toggle row ───────────────────────────────────────────────────────
    val ETP_TOGGLE_LABEL =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.protection_panel_etp_toggle_label),
            description = "ETP toggle label",
        )

    val ETP_TOGGLE_ENABLED_DESCRIPTION =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.protection_panel_etp_toggle_enabled_description_2),
            description = "ETP toggle enabled description",
        )

    val ETP_TOGGLE_DISABLED_DESCRIPTION =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.protection_panel_etp_toggle_disabled_description_2),
            description = "ETP toggle disabled description",
        )

    val ETP_TOGGLE_ON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.protection_panel_etp_toggle_on),
            description = "ETP toggle ON state",
        )

    val ETP_TOGGLE_OFF =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.protection_panel_etp_toggle_off),
            description = "ETP toggle OFF state",
        )

    // ── Trackers-blocked option ──────────────────────────────────────────────
    val TRACKERS_DISABLED_NONE_BLOCKED =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.protection_panel_etp_disabled_no_trackers_blocked),
            description = "Trackers: ETP disabled, none blocked",
        )

    val TRACKERS_BLOCKED_ON_SITE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION_SUBSTRING,
            value = "blocked on this site",
            description = "Trackers: blocked on this site",
        )

    val TRACKERS_PROTECTED_NONE_BLOCKED =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION_SUBSTRING,
            value = getStringResource(R.string.protection_panel_banner_protected_no_blocked_trackers_description),
            description = "Trackers: protected, none blocked",
        )

    // ── Connection / site security ───────────────────────────────────────────
    val CONNECTION_SECURE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.connection_security_panel_secure),
            description = "Connection: secure",
        )

    val CONNECTION_VERIFIED_BY =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT_SUBSTRING,
            value = "Verified by",
            description = "Connection: verified-by line",
        )

    val CONNECTION_NOT_SECURE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.connection_security_panel_not_secure),
            description = "Connection: not secure",
        )

    // ── Panel chrome ─────────────────────────────────────────────────────────
    val PRIVACY_SETTINGS_LINK =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = "Privacy Settings Links available",
            description = "Privacy settings hyperlink",
        )

    // ── Trackers-blocked details sub-panel ─────────────────────────────────────
    // Opened by clicking the banner (TRACKERS_BLOCKED_ON_SITE) when trackers were blocked. Each category row is a
    // MenuItem whose label is a count-prefixed plural ("3 cryptominers"), so match the distinctive, count-free
    // substring. Only categories with at least one blocked tracker are rendered.
    val TRACKERS_DETAILS_SOCIAL_MEDIA =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT_SUBSTRING,
            value = "social media tracker",
            description = "Trackers details: social media trackers category row",
        )

    val TRACKERS_DETAILS_CROSS_SITE_COOKIES =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT_SUBSTRING,
            value = "cross-site tracking cookie",
            description = "Trackers details: cross-site cookies category row",
        )

    val TRACKERS_DETAILS_CRYPTOMINERS =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT_SUBSTRING,
            value = "cryptominer",
            description = "Trackers details: cryptominers category row",
        )

    val TRACKERS_DETAILS_FINGERPRINTERS =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT_SUBSTRING,
            value = "fingerprinter",
            description = "Trackers details: fingerprinters category row",
        )

    val TRACKERS_DETAILS_TRACKING_CONTENT =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT_SUBSTRING,
            value = "tracking content",
            description = "Trackers details: tracking content category row",
        )

    // ── Individual tracker-category detail panel ───────────────────────────────
    // Reached by clicking a category row above; shows the category title, a description and a "Blocked" list of
    // tracker hostnames. We assert the title and the "Blocked" header (hostnames are data-dependent).
    val CATEGORY_DETAIL_SOCIAL_MEDIA_TITLE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.etp_social_media_trackers_title),
            description = "Category detail: Social Media Trackers title",
        )

    val CATEGORY_DETAIL_CROSS_SITE_COOKIES_TITLE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.etp_cookies_title_2),
            description = "Category detail: Cross-Site Cookies title",
        )

    val CATEGORY_DETAIL_CRYPTOMINERS_TITLE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.etp_cryptominers_title),
            description = "Category detail: Cryptominers title",
        )

    val CATEGORY_DETAIL_FINGERPRINTERS_TITLE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.etp_known_fingerprinters_title),
            description = "Category detail: Known Fingerprinters title",
        )

    val CATEGORY_DETAIL_TRACKING_CONTENT_TITLE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.etp_tracking_content_title),
            description = "Category detail: Tracking Content title",
        )

    val CATEGORY_DETAIL_BLOCKED_HEADER =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.enhanced_tracking_protection_blocked),
            description = "Category detail: Blocked header",
        )
}
