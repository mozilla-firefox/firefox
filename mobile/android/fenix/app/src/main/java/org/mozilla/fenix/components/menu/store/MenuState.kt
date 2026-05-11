/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.store

import android.graphics.Bitmap
import androidx.compose.runtime.Immutable
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.feature.addons.Addon
import mozilla.components.lib.state.State
import mozilla.components.support.ktx.kotlin.isAboutUrl
import mozilla.components.support.ktx.kotlin.isContentUrl
import org.mozilla.fenix.components.menu.MenuAccessPoint

/**
 * Value type that represents the state of the menu.
 *
 * @property browserMenuState The [BrowserMenuState] of the current browser session if any.
 * @property customTabSessionId The ID of the custom tab session if navigating from
 * an external access point, and null otherwise.
 * @property extensionMenuState The [ExtensionMenuState] to display.
 * @property summarizationMenuState The [SummarizationMenuState] that handles summarization menu item
 * @property ipProtectionMenuState The [IPProtectionMenuState] for the IP protection menu item.
 * @property isMoreMenuExpanded Whether or not the "more menu" is expanded.
 * @property isDesktopMode Whether or not the desktop mode is enabled for the currently visited
 * page.
 */
data class MenuState(
    val browserMenuState: BrowserMenuState? = null,
    val customTabSessionId: String? = null,
    val extensionMenuState: ExtensionMenuState = ExtensionMenuState(),
    val summarizationMenuState: SummarizationMenuState = SummarizationMenuState.Default,
    val ipProtectionMenuState: IPProtectionMenuState = IPProtectionMenuState(),
    val isMoreMenuExpanded: Boolean = false,
    val isDesktopMode: Boolean = false,
) : State {

    /**
     * Check whether to enable the WebCompat Reporter menu button. The reporter is not accessible
     * from about and content URLs.
     */
    val isWebCompatEnabled: Boolean
        get() {
            val url = browserMenuState?.selectedTab?.content?.url
            val isAboutUrl = url?.isAboutUrl() ?: false
            val isContentUrl = url?.isContentUrl() ?: false
            return !isAboutUrl && !isContentUrl
        }
}

/**
 * Value type that represents the state of the browser menu.
 *
 * @property selectedTab The current selected [TabSessionState].
 * @property bookmarkState The [BookmarkState] of the selected tab.
 * @property isPinned Whether or not the selected tab is a pinned shortcut.
 * @property isLoading Whether or not the selected tab is loading.
 */
data class BrowserMenuState(
    val selectedTab: TabSessionState,
    val bookmarkState: BookmarkState = BookmarkState(),
    val isPinned: Boolean = false,
    val isLoading: Boolean = false,
)

/**
 * Value type that represents the state of the extension submenu.
 *
 * @property recommendedAddons A list of recommended [Addon]s to suggest.
 * @property availableAddons A list of installed and enabled [Addon]s to be shown.
 * @property addonInstallationInProgress The [Addon] that is currently being installed.
 * @property browserWebExtensionMenuItem A list of [WebExtensionMenuItem]s
 * to be shown in the menu.
 * @property accesspoint The [MenuAccessPoint] that was used to navigate to the menu dialog.
 */
data class ExtensionMenuState(
    val recommendedAddons: List<Addon> = emptyList(),
    val availableAddons: List<Addon> = emptyList(),
    val addonInstallationInProgress: Addon? = null,
    val browserWebExtensionMenuItem: List<WebExtensionMenuItem> = emptyList(),
    val accesspoint: MenuAccessPoint? = null,
) {

    /**
     * Get the number of web extensions to be shown in the menu.
     */
    val webExtensionsCount: Int
        get() {
            return when (accesspoint) {
                MenuAccessPoint.Browser, MenuAccessPoint.External -> {
                    browserWebExtensionMenuItem.size
                }
                MenuAccessPoint.Home -> {
                    availableAddons.size
                }
                else -> 0
            }
        }

    /**
     * All web extensions disabled.
     */
    val allWebExtensionsDisabled: Boolean
        get() {
            return (
                (recommendedAddons.isEmpty() || accesspoint == MenuAccessPoint.External) &&
                        availableAddons.isEmpty() && browserWebExtensionMenuItem.isEmpty()
            ) ||
            (
                (accesspoint == MenuAccessPoint.Browser || accesspoint == MenuAccessPoint.External) &&
                    browserWebExtensionMenuItem.isEmpty() && availableAddons.isNotEmpty()
            )
        }
}

/**
 * Value type that represents the bookmark state of a tab.
 *
 * @property guid The id of the bookmark.
 * @property isBookmarked Whether or not the selected tab is bookmarked.
 */
data class BookmarkState(
    val guid: String? = null,
    val isBookmarked: Boolean = false,
)

/**
 * Represents the state of the summarization menu items.
 *
 * @property visible Whether the menu item is visible altogether.
 * @property enabled Whether the menu item is enabled.
 * @property highlighted Whether the menu item is highlighted.
 * @property showNewFeatureBadge Whether the "new" badge should be shown
 * @property overflowMenuHighlighted Whether the overflow menu item is highlighted
 */
@Immutable
data class SummarizationMenuState(
    val visible: Boolean,
    val enabled: Boolean,
    val highlighted: Boolean,
    val showNewFeatureBadge: Boolean,
    val overflowMenuHighlighted: Boolean,
) {
    companion object {
        val Default = SummarizationMenuState(
            visible = false,
            highlighted = false,
            enabled = false,
            showNewFeatureBadge = false,
            overflowMenuHighlighted = false,
        )
    }
}

/**
 * Installed extensions actions to display relevant to the browser as a whole.
 *
 * @property id The id of the web extension.
 * @property label The label of the web extension menu item.
 * @property enabled Indicates if web extension menu item should be enabled or disabled.
 * @property icon The icon that should be shown in the menu.
 * @property badgeText Menu item badge text.
 * @property badgeTextColor Menu item badge text color.
 * @property badgeBackgroundColor Menu item badge background color.
 * @property onClick A callback to be executed when the web extension menu item is clicked.
 */
data class WebExtensionMenuItem(
    val id: String,
    val label: String,
    val enabled: Boolean?,
    val icon: Bitmap?,
    val badgeText: String?,
    val badgeTextColor: Int?,
    val badgeBackgroundColor: Int?,
    val onClick: () -> Unit,
)

/**
 * Properties for the translation menu.
 *
 * @property isTranslationSupported Whether or not the page is supported for translation.
 * @property isPdf Whether or not the page is a PDF.
 * @property isTranslated Whether or not the page is already translated.
 * @property translatedLanguage The language the page is translated to.
 * @property onTranslatePageMenuClick A callback to be executed when the translate page menu
 */
data class TranslationInfo(
    val isTranslationSupported: Boolean,
    val isPdf: Boolean,
    val isTranslated: Boolean,
    val translatedLanguage: String,
    val onTranslatePageMenuClick: () -> Unit,
)

/**
 * Represents the display states of the IP protection menu item.
 */
enum class IPProtectionMenuStatus {
    /**
     * IP protection is inactive.
     */
    Disabled,

    /**
     * IP protection is in the process of activating.
     */
    Activating,

    /**
     * IP protection is active.
     */
    Enabled,

    /**
     * IP protection is paused until the data limit resets.
     */
    DataLimitReached,

    /**
     * IP protection has errored.
     */
    ConnectionError,

    /**
     * User needs to authenticate or to authorize ip protection service before IP protection can be used.
     */
    AuthRequired,
}

/**
 * Represents the state of the IP protection menu item.
 *
 * @property status The current [IPProtectionMenuStatus] shown in the badge.
 * @property dataLimitGb The total monthly data allowance in GB.
 */
@Immutable
data class IPProtectionMenuState(
    val status: IPProtectionMenuStatus = IPProtectionMenuStatus.Disabled,
    val dataLimitGb: Int = -1,
)
